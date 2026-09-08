import assert from "node:assert/strict";
import test from "node:test";
import { clearPartsViewportAnnotations, renderNestingViewportOverlay } from "../../apps/tube-designer/webpage/partsArea.mjs";

const tick = () => new Promise(resolve => setTimeout(resolve, 2));
async function until(predicate) {
  const deadline = Date.now() + 3000;
  while (Date.now() < deadline) {
    if (predicate()) return;
    await tick();
  }
  assert.fail("Part progress did not reach the expected stage");
}
function deferred() {
  let resolve, reject;
  const promise = new Promise((complete, fail) => { resolve = complete; reject = fail; });
  return { promise, resolve, reject };
}
const measurement = {
  available: true, source: "final-brep", length: 100, section: { width: 20, height: 16 }, features: [],
  linearReference: { start: [-50, 0, 0], end: [50, 0, 0], dimensionOffsetDirection: [0, 1, 0] },
};
function node() {
  return { hidden: false, textContent: "", innerHTML: "", dataset: {}, attributes: new Map(),
    setAttribute(name, value) { this.attributes.set(name, String(value)); },
    getAttribute(name) { return this.attributes.get(name) ?? null; },
    removeAttribute(name) { this.attributes.delete(name); } };
}
function fixture() {
  const label = node(), name = node(), bar = node(), host = node();
  const progressNodes = selector => selector.includes("progress-label") ? label
    : selector.includes("progress-name") ? name : selector.includes("progressbar") ? bar : null;
  host.querySelector = progressNodes;
  host.querySelectorAll = selector => [progressNodes(selector)].filter(Boolean);
  let mounted = false, generation = 0;
  const snapshots = [], measures = [], meshDelays = new Map(), measureDelays = new Map();
  const parts = ["part-a", "part-b"].map(entityId => ({ entityId, name: entityId, length: 100, quantity: 1,
    thumbnailGeometryResourceId: `resource://${entityId}`, thumbnailGeometryResourceVersion: 1,
    manufacturingGeometryResourceVersion: 2, profile: { kind: "rect", width: 20, depth: 16 } }));
  const view = { activeAreaId: "nesting", tubeDesignerActivePartId: "part-a", tubeDesignerActiveNestingPartId: "part-a", tubeDesignerNestingSelectionKind: "part",
    scene: { tubeDesigner: { nestingGroups: [{ name: "标准零件", parts }] } },
    viewport: {
      visibleEntityIds: [], setVisibleEntityIds(ids) { this.visibleEntityIds = [...ids]; },
      setDimensionAnnotations() {}, setPresentationAxis() {}, setStandardView() {}, fitViewToViewport() {},
      async applyViewSnapshot(snapshot) {
        const current = ++generation;
        snapshots.push(snapshot);
        for (const row of snapshot.rows) await meshDelays.get(row.entityId);
        if (current !== generation) return { applied: false, superseded: true };
        const entityIds = snapshot.rows.map(row => String(row.entityId));
        this.setVisibleEntityIds(entityIds);
        return { applied: true, entityIds };
      },
    },
  };
  const context = { mount: {
    querySelectorAll(selector) { return mounted && selector === "[data-tube-designer-part-progress]" ? [host] : []; },
    querySelector(selector) { return mounted && selector === "[data-tube-designer-part-progress]" ? host : null; },
  }, sceneProxy: {
    resources: { get() { throw new Error("Geometry is handled by the viewport fixture"); } },
    async invoke(method, payload) {
      assert.equal(method, "TubeDesigner.MeasurePartGeometry");
      measures.push(payload.partEntityId);
      return await measureDelays.get(payload.partEntityId) ?? structuredClone(measurement);
    },
  } };
  return { context, view, parts, snapshots, measures, meshDelays, measureDelays, host, label, name, bar,
    render() {
      const html = renderNestingViewportOverlay(context, view);
      mounted = html.includes("data-tube-designer-part-progress");
      // Reflect the newly mounted host before its async DOM-only updates occur.
      host.hidden = !view.tubeDesignerPartProgress;
      label.textContent = view.tubeDesignerPartProgress?.label ?? "";
      name.textContent = view.tubeDesignerPartProgress?.partName ?? "";
      view.viewport.setVisibleEntityIds([]);
      return html;
    },
    select(id) { view.tubeDesignerActivePartId = id; return this.render(); },
    ready(id = view.tubeDesignerActivePartId) {
      return until(() => view.tubeDesignerPartMeasurementState?.status === "ready"
        && view.tubeDesignerPartMeasurementState.key === `${id}@2` && !view.tubeDesignerPartProgress);
    },
  };
}

test("model loading is announced synchronously and measurement updates the existing progress host", async () => {
  const f = fixture(), mesh = deferred(), measure = deferred();
  f.meshDelays.set("part-a", mesh.promise);
  f.measureDelays.set("part-a", measure.promise);
  const html = f.render();
  assert.equal(f.view.tubeDesignerPartProgress.phase, "geometry");
  assert.equal(f.view.tubeDesignerPartProgress.label, "正在加载三维模型…");
  assert.equal(f.view.tubeDesignerPartProgress.partName, "part-a");
  assert.match(html, /data-tube-designer-part-progress/);
  assert.match(html, /role="progressbar"/);
  assert.doesNotMatch(html, /aria-valuenow=/, "native operations expose stages, not an invented completion percentage");
  assert.equal(f.host.hidden, false);
  await until(() => f.snapshots.length === 1);
  assert.equal(f.view.tubeDesignerPartProgress.phase, "geometry");
  mesh.resolve();
  await until(() => f.measures.length === 1);
  assert.equal(f.view.tubeDesignerPartProgress.phase, "measurement");
  assert.equal(f.label.textContent, "正在复尺并生成标注…", "stage label is updated without a full render");
  assert.equal(f.host.hidden, false);
  assert.deepEqual(f.view.viewport.visibleEntityIds, ["part-a"], "the loaded model is shown while dimensions are calculated");
  measure.resolve(measurement);
  await f.ready();
  assert.equal(f.view.tubeDesignerPartProgress, null);
  assert.equal(f.host.hidden, true, "completion hides the already-mounted progress host");
});

test("cached part redraws do not flash progress or recalculate dimensions", async () => {
  const f = fixture();
  f.render(); await f.ready();
  for (let index = 0; index < 10; index++) {
    f.render();
    assert.equal(f.view.tubeDesignerPartProgress, null);
    assert.equal(f.host.hidden, true);
    await tick();
    assert.equal(f.view.tubeDesignerPartProgress, null);
  }
  assert.equal(f.snapshots.length, 1);
  assert.deepEqual(f.measures, ["part-a"]);
});

test("geometry errors stop progress and retain the actionable measurement error", async () => {
  const f = fixture(), mesh = deferred();
  f.meshDelays.set("part-a", mesh.promise);
  f.render(); await until(() => f.snapshots.length === 1);
  mesh.reject(new Error("无法读取模型资源"));
  await until(() => f.view.tubeDesignerPartMeasurementState?.status === "error");
  assert.equal(f.view.tubeDesignerPartProgress, null);
  assert.equal(f.host.hidden, true);
  assert.match(f.view.tubeDesignerPartMeasurementState.message, /无法读取模型资源/);
  assert.deepEqual(f.measures, []);
});

test("measurement errors stop progress without hiding the successfully loaded model", async () => {
  const f = fixture(), measure = deferred();
  f.measureDelays.set("part-a", measure.promise);
  f.render(); await until(() => f.measures.length === 1);
  measure.reject(new Error("复尺失败，请检查实体"));
  await until(() => f.view.tubeDesignerPartMeasurementState?.status === "error");
  assert.equal(f.view.tubeDesignerPartProgress, null);
  assert.equal(f.host.hidden, true);
  assert.deepEqual(f.view.viewport.visibleEntityIds, ["part-a"]);
});

test("late results from the old selection cannot clear the new part's progress", async () => {
  const f = fixture(), oldMeasure = deferred(), nextMesh = deferred(), nextMeasure = deferred();
  f.measureDelays.set("part-a", oldMeasure.promise);
  f.meshDelays.set("part-b", nextMesh.promise);
  f.measureDelays.set("part-b", nextMeasure.promise);
  f.render(); await until(() => f.measures.length === 1);
  f.select("part-b");
  assert.equal(f.view.tubeDesignerPartProgress.partName, "part-b");
  await until(() => f.snapshots.length === 2);
  oldMeasure.resolve(measurement); await tick();
  assert.equal(f.view.tubeDesignerPartProgress.phase, "geometry");
  assert.equal(f.view.tubeDesignerPartProgress.partName, "part-b");
  nextMesh.resolve(); await until(() => f.measures.length === 2);
  assert.equal(f.view.tubeDesignerPartProgress.phase, "measurement");
  assert.equal(f.host.hidden, false);
  nextMeasure.resolve(measurement); await f.ready("part-b");
  assert.equal(f.host.hidden, true);
});

test("emptying the list clears progress and stale completion cannot revive it", async () => {
  const f = fixture(), mesh = deferred();
  f.meshDelays.set("part-a", mesh.promise);
  f.render(); await until(() => f.snapshots.length === 1);
  f.view.scene.tubeDesigner.nestingGroups = [];
  const html = f.render();
  assert.equal(f.view.tubeDesignerPartProgress, null);
  assert.doesNotMatch(html, /role="progressbar"/);
  mesh.resolve(); await tick(); await tick();
  assert.equal(f.view.tubeDesignerPartProgress, null);
});

test("leaving the inspection area clears progress before a pending response returns", async () => {
  const f = fixture(), measure = deferred();
  f.measureDelays.set("part-a", measure.promise);
  f.render(); await until(() => f.measures.length === 1);
  f.view.activeAreaId = "view";
  clearPartsViewportAnnotations(f.view);
  assert.equal(f.view.tubeDesignerPartProgress, null);
  measure.resolve(measurement); await tick();
  assert.equal(f.view.tubeDesignerPartProgress, null);
});

test("selecting a plan clears part progress instead of leaving a stale loading bar", async () => {
  const f = fixture(), measure = deferred();
  f.measureDelays.set("part-a", measure.promise);
  f.render(); await until(() => f.measures.length === 1);
  f.view.tubeDesignerNestingSelectionKind = "plan";
  f.view.tubeDesignerActiveNestingPlanId = "empty-plan";
  f.view.tubeDesignerActiveNestingPartId = "";
  f.view.tubeDesignerNestingResult = { plans: [{ id: "empty-plan", stockLength: 200, placements: [] }] };
  const html = f.render();
  assert.equal(f.view.tubeDesignerPartProgress, null);
  assert.doesNotMatch(html, /data-tube-designer-part-progress/);
  measure.resolve(measurement); await tick();
  assert.equal(f.view.tubeDesignerPartProgress, null);
});
