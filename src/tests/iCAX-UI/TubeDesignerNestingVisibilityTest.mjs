import assert from "node:assert/strict";
import test from "node:test";
import { clearPartsViewportAnnotations, renderNestingViewportOverlay } from "../../apps/tube-designer/webpage/partsArea.mjs";
import { encodeNestingGeometry } from "../../apps/tube-designer/webpage/nestingPreview.mjs";
import { loadRenderResource } from "../../iCAX-UI/SDK/Viewport/renderResource.mjs";

const geometry = {
  positions: [-50, -10, -8, -50, 10, -8, -50, 10, 8, -50, -10, 8,
    50, -10, -8, 50, 10, -8, 50, 10, 8, 50, -10, 8],
  indices: [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
    1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7],
};
const measurement = {
  available: true, source: "final-brep", length: 100,
  section: { width: 20, height: 16 }, features: [],
  linearReference: { start: [-50, 0, 0], end: [50, 0, 0],
    sectionAxes: [[0, 1, 0], [0, 0, 1]], dimensionOffsetDirection: [0, 1, 0] },
};
const part = (entityId) => ({
  entityId, name: entityId, length: 100, quantity: 1,
  profile: { kind: "rect", width: 20, depth: 16, specification: "20 × 16" },
  thumbnailGeometryResourceId: `icax-resource://${entityId}`,
  thumbnailGeometryResourceVersion: 4, manufacturingGeometryResourceVersion: 7,
  properties: { "manufacturing.partKind": "tube", "nesting.snapshot": { source: "standard-part" } },
});
const tick = () => new Promise((resolve) => setImmediate(resolve));
async function until(predicate) {
  for (let index = 0; index < 100; index++) {
    if (predicate()) return;
    await tick();
  }
  assert.fail("Part hydration did not reach the expected state");
}
function deferred() {
  let resolve;
  const promise = new Promise((complete) => { resolve = complete; });
  return { promise, resolve };
}

function makeViewport() {
  let generation = 0;
  return {
    snapshots: [], visibleEntityIds: [], visibilityWrites: [], annotations: [], fitCount: 0,
    setVisibleEntityIds(ids) { this.visibleEntityIds = [...ids]; this.visibilityWrites.push([...ids]); },
    setDimensionAnnotations(annotations) { this.annotations = annotations; },
    setPresentationAxis() {}, setStandardView() {},
    fitViewToViewport() { this.fitCount++; },
    async applyViewSnapshot(snapshot, resources) {
      const current = ++generation;
      const record = { snapshot, resources: new Map() };
      this.snapshots.push(record);
      // Parse the real encoded geometry/material resource format; only GPU drawing
      // is mocked. Like the shared viewport, a newer snapshot supersedes old loads.
      for (const row of snapshot.rows) {
        for (const reference of [row.data.geometry, row.data.material].filter(Boolean)) {
          record.resources.set(reference.url, await loadRenderResource(resources, reference));
        }
      }
      if (current !== generation) return { applied: false, superseded: true };
      const entityIds = snapshot.rows.map((row) => String(row.entityId));
      this.setVisibleEntityIds(entityIds);
      return { applied: true, entityIds };
    },
  };
}

function fixture() {
  const parts = [part("standard-1"), part("standard-2")];
  const reads = [], measures = [], resourceDelays = new Map(), measurementDelays = new Map();
  const view = {
    activeAreaId: "nesting", viewport: makeViewport(),
    tubeDesignerActivePartId: "standard-1", tubeDesignerActiveNestingPartId: "standard-1",
    tubeDesignerNestingSelectionKind: "part",
    scene: { tubeDesigner: { nestingGroups: [{ name: "独立下料零件", parts }] } },
  };
  const context = {
    mount: { querySelector() { return null; }, querySelectorAll() { return []; } },
    sceneProxy: {
      resources: { async get(url, options) {
        reads.push(String(url));
        assert.equal(options.headers.get("ICAX-Resource-Version"), "4");
        await resourceDelays.get(String(url));
        return new Response(encodeNestingGeometry(geometry));
      } },
      async invoke(method, payload) {
        assert.equal(method, "TubeDesigner.MeasurePartGeometry");
        assert.equal(payload.resourceVersion, 7);
        measures.push(payload.partEntityId);
        return await measurementDelays.get(payload.partEntityId) ?? structuredClone(measurement);
      },
    },
  };
  return { view, context, parts, reads, measures, resourceDelays, measurementDelays,
    render() {
      const html = renderNestingViewportOverlay(context, view);
      // Preserve the actual workbench order: build overlay, then mountRenderViewport
      // hides all custom-view objects, THEN the queued hydration restores its part.
      view.viewport.setVisibleEntityIds([]);
      return html;
    },
    select(id) {
      view.tubeDesignerActivePartId = id;
      view.tubeDesignerActiveNestingPartId = id;
      view.tubeDesignerNestingSelectionKind = "part";
      return this.render();
    },
    ready(id = view.tubeDesignerActivePartId) {
      return until(() => view.tubeDesignerPartMeasurementState?.status === "ready"
        && view.tubeDesignerPartMeasurementState.key === `${id}@7`
        && view.viewport.visibleEntityIds.includes(id));
    },
  };
}

test("added standard part remains visible through cached workbench renders without reload or remeasurement", async () => {
  const f = fixture();
  f.render();
  await f.ready();
  const viewport = f.view.viewport;
  assert.deepEqual(viewport.visibleEntityIds, ["standard-1"]);
  assert.ok(viewport.annotations.length > 0, "final-BRep dimensions are drawn alongside the solid");
  assert.equal(viewport.snapshots[0].resources.get("icax-resource://standard-1").data.kind, "mesh");
  const fitCount = viewport.fitCount;
  for (let index = 0; index < 20; index++) {
    f.render();
    assert.deepEqual(viewport.visibleEntityIds, [], "shared mount has hidden the existing mesh");
    await tick();
    assert.deepEqual(viewport.visibleEntityIds, ["standard-1"], "cached hydration restores the mesh, not only annotations");
    assert.ok(viewport.annotations.length > 0);
  }
  assert.equal(viewport.snapshots.length, 1);
  assert.deepEqual(f.reads, ["icax-resource://standard-1"]);
  assert.deepEqual(f.measures, ["standard-1"]);
  assert.equal(viewport.fitCount, fitCount, "cached renders do not refit the user's camera");
  f.view.tubeDesignerPartDimensionsVisible = false;
  f.render();
  await tick();
  assert.deepEqual(viewport.visibleEntityIds, ["standard-1"]);
  assert.deepEqual(viewport.annotations, [], "hiding dimensions must not hide the actual part");
});

test("switching parts restores only the selected part and reuses dimension results when returning", async () => {
  const f = fixture();
  f.render(); await f.ready();
  f.select("standard-2"); await f.ready("standard-2");
  assert.deepEqual(f.view.viewport.visibleEntityIds, ["standard-2"]);
  f.render(); await tick();
  assert.deepEqual(f.view.viewport.visibleEntityIds, ["standard-2"]);
  f.select("standard-1"); await f.ready("standard-1");
  assert.deepEqual(f.view.viewport.visibleEntityIds, ["standard-1"]);
  assert.deepEqual(f.measures, ["standard-1", "standard-2"]);
  assert.equal(f.view.viewport.snapshots.length, 3);
});

test("same entity and version with a new geometry resource id remeasures", async () => {
  const f = fixture();
  f.parts[0].manufacturingGeometryResourceId = "final-resource-a";
  f.render();
  await until(() => f.view.tubeDesignerPartMeasurementState?.key === "standard-1@final-resource-a@7"
    && f.view.tubeDesignerPartMeasurementState.status === "ready");
  assert.deepEqual(f.measures, ["standard-1"]);

  // Version numbers are scoped to a resource identity. A replacement resource
  // can legitimately start at the same version, but it must not reuse the old
  // annotation measurement.
  f.parts[0].manufacturingGeometryResourceId = "final-resource-b";
  f.render();
  await until(() => f.view.tubeDesignerPartMeasurementState?.key === "standard-1@final-resource-b@7"
    && f.view.tubeDesignerPartMeasurementState.status === "ready");
  assert.deepEqual(f.measures, ["standard-1", "standard-1"]);
});

test("a recreated viewport reloads the current geometry despite an unchanged part key", async () => {
  const f = fixture();
  f.render(); await f.ready();
  const previous = f.view.viewport;
  f.view.viewport = makeViewport();
  f.render(); await f.ready();
  assert.equal(f.view.viewport.snapshots.length, 1, "new viewport has no resident geometry to reuse");
  assert.deepEqual(f.view.viewport.visibleEntityIds, ["standard-1"]);
  assert.deepEqual(f.measures, ["standard-1"], "dimension data is independent of viewport ownership");
  assert.equal(previous.snapshots.length, 1);
});

test("late geometry for the previous selection cannot restore or measure the old part", async () => {
  const f = fixture(), delayed = deferred();
  f.resourceDelays.set("icax-resource://standard-1", delayed.promise);
  f.render();
  await until(() => f.reads.length === 1);
  f.select("standard-2"); await f.ready("standard-2");
  f.view.viewport.visibilityWrites.length = 0;
  delayed.resolve(); await tick(); await tick();
  assert.deepEqual(f.view.viewport.visibleEntityIds, ["standard-2"]);
  assert.equal(f.view.viewport.visibilityWrites.some((ids) => ids.includes("standard-1")), false);
  assert.deepEqual(f.measures, ["standard-2"]);
});

test("late measurements cannot overwrite the selected part's annotations", async () => {
  const f = fixture(), delayed = deferred();
  f.measurementDelays.set("standard-1", delayed.promise);
  f.render(); await until(() => f.measures.length === 1);
  f.select("standard-2"); await f.ready("standard-2");
  const annotations = f.view.viewport.annotations;
  delayed.resolve({ ...measurement, length: 999 });
  await tick();
  assert.deepEqual(f.view.viewport.visibleEntityIds, ["standard-2"]);
  assert.equal(f.view.viewport.annotations, annotations);
  assert.equal(f.view.tubeDesignerPartMeasurementState.key, "standard-2@7");
});

test("plan selection supersedes a queued cached-part restore", async () => {
  const f = fixture();
  f.render(); await f.ready();
  f.render();
  f.view.tubeDesignerNestingSelectionKind = "plan";
  f.view.tubeDesignerActiveNestingPlanId = "stock-1";
  f.view.tubeDesignerNestingResult = { plans: [{ id: "stock-1", stockLength: 200,
    placements: [{ partId: "standard-1", instanceId: "instance-1", start: 0, end: 100, length: 100 }] }] };
  f.render();
  await until(() => f.view.tubeDesignerNestingPreviewState?.status === "ready");
  assert.ok(f.view.viewport.visibleEntityIds.length > 0);
  assert.ok(f.view.viewport.visibleEntityIds.every((id) => id.startsWith("nesting:stock-1:")));
  assert.deepEqual(f.view.viewport.annotations, []);
  assert.deepEqual(f.measures, ["standard-1"]);
});

test("missing geometry reports an error and never restores the previously displayed part", async () => {
  const f = fixture();
  f.render(); await f.ready();
  f.parts[1].thumbnailGeometryResourceId = "";
  f.select("standard-2");
  await until(() => f.view.tubeDesignerPartMeasurementState?.status === "error");
  assert.match(f.view.tubeDesignerPartMeasurementState.message, /缺少可显示的三维资源/);
  assert.deepEqual(f.view.viewport.visibleEntityIds, []);
  assert.deepEqual(f.view.viewport.annotations, [], "missing geometry cannot leave the previous part's dimensions on screen");
  assert.equal(f.view.viewport.snapshots.length, 1);
  assert.deepEqual(f.measures, ["standard-1"]);
});

test("clearing the parts list cancels queued visibility and clears stale dimensions", async () => {
  const f = fixture();
  f.render(); await f.ready();
  f.render();
  f.view.scene.tubeDesigner.nestingGroups = [];
  assert.match(f.render(), /下料三维场景/);
  await tick();
  assert.deepEqual(f.view.viewport.visibleEntityIds, []);
  assert.deepEqual(f.view.viewport.annotations, []);
  assert.equal(f.view.tubeDesignerPartViewportKey, "");
  assert.equal(f.view.viewport.snapshots.length, 1);
});

test("leaving the inspection area prevents a queued part restore", async () => {
  const f = fixture();
  f.render(); await f.ready();
  f.render();
  f.view.activeAreaId = "view";
  f.view.viewport.setVisibleEntityIds(["product-1"]);
  await tick();
  assert.deepEqual(f.view.viewport.visibleEntityIds, ["product-1"]);
});

test("removing the last parts while geometry loads cancels the viewport's pending snapshot", async () => {
  const f = fixture(), delayed = deferred();
  f.resourceDelays.set("icax-resource://standard-1", delayed.promise);
  f.render(); await until(() => f.reads.length === 1);
  f.view.scene.tubeDesigner.nestingGroups = [];
  f.render(); await tick();
  delayed.resolve(); await tick(); await tick();
  assert.deepEqual(f.view.viewport.visibleEntityIds, [], "stale apply must not install a mesh before its receipt is rejected");
  assert.deepEqual(f.view.viewport.annotations, []);
  assert.deepEqual(f.measures, []);
});

test("leaving the area while geometry loads invalidates the old viewport generation", async () => {
  const f = fixture(), delayed = deferred();
  f.resourceDelays.set("icax-resource://standard-1", delayed.promise);
  f.render(); await until(() => f.reads.length === 1);
  f.view.activeAreaId = "view";
  clearPartsViewportAnnotations(f.view);
  await tick();
  f.view.viewport.setVisibleEntityIds(["product-1"]);
  delayed.resolve(); await tick(); await tick();
  assert.deepEqual(f.view.viewport.visibleEntityIds, ["product-1"]);
  assert.deepEqual(f.measures, []);
});

test("replacing the viewport while a part loads cancels the abandoned viewport load", async () => {
  const f = fixture(), delayed = deferred();
  f.resourceDelays.set("icax-resource://standard-1", delayed.promise);
  f.render(); await until(() => f.reads.length === 1);
  const abandoned = f.view.viewport;
  f.view.viewport = makeViewport();
  f.resourceDelays.delete("icax-resource://standard-1");
  f.render(); await f.ready();
  delayed.resolve(); await tick(); await tick();
  assert.deepEqual(abandoned.visibleEntityIds, []);
  assert.deepEqual(f.view.viewport.visibleEntityIds, ["standard-1"]);
  assert.deepEqual(f.measures, ["standard-1"]);
});

test("returning to a cached part while another part is loading prevents a late replacement", async () => {
  const f = fixture(), delayed = deferred();
  f.render(); await f.ready();
  f.resourceDelays.set("icax-resource://standard-2", delayed.promise);
  f.select("standard-2"); await until(() => f.reads.length === 2);
  f.select("standard-1"); await f.ready("standard-1");
  delayed.resolve(); await tick(); await tick();
  assert.deepEqual(f.view.viewport.visibleEntityIds, ["standard-1"], "cached A must supersede in-flight B at the renderer level");
  assert.deepEqual(f.measures, ["standard-1"]);
});

test("selecting a part without geometry cancels a previous pending mesh", async () => {
  const f = fixture(), delayed = deferred();
  f.resourceDelays.set("icax-resource://standard-1", delayed.promise);
  f.render(); await until(() => f.reads.length === 1);
  f.parts[1].thumbnailGeometryResourceId = "";
  f.select("standard-2");
  await until(() => f.view.tubeDesignerPartMeasurementState?.status === "error");
  delayed.resolve(); await tick(); await tick();
  assert.deepEqual(f.view.viewport.visibleEntityIds, []);
  assert.deepEqual(f.view.viewport.annotations, []);
  assert.equal(f.view.tubeDesignerPartMeasurementState.key, "standard-2@7");
  assert.deepEqual(f.measures, []);
});

test("switching to a plan cancels pending part geometry even before the plan can be prepared", async () => {
  const f = fixture(), delayed = deferred();
  f.resourceDelays.set("icax-resource://standard-1", delayed.promise);
  f.render(); await until(() => f.reads.length === 1);
  f.view.tubeDesignerNestingSelectionKind = "plan";
  f.view.tubeDesignerActiveNestingPlanId = "stock-1";
  f.view.tubeDesignerNestingResult = { plans: [{ id: "stock-1", stockLength: 200,
    placements: [{ partId: "standard-2", instanceId: "instance-2", start: 0, end: 100, length: 100 }] }] };
  f.render();
  await until(() => f.view.tubeDesignerNestingPreviewState?.status === "ready");
  delayed.resolve(); await tick(); await tick();
  assert.ok(f.view.viewport.visibleEntityIds.length > 0);
  assert.ok(f.view.viewport.visibleEntityIds.every((id) => id.startsWith("nesting:stock-1:")));
  assert.deepEqual(f.measures, []);
});
