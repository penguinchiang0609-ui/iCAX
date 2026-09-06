import assert from "node:assert/strict";
import {
  scheduleNestingPlanHydration, cancelNestingPlanHydration,
  encodeNestingGeometry, meshBounds, nestingPlacementMatrix,
  getNestingPlacementColor,
} from "../../apps/tube-designer/webpage/nestingPreview.mjs";
import { loadRenderResource, parseRenderGeometryResource, rgbaToCssColor } from "../../iCAX-UI/SDK/Viewport/renderResource.mjs";

const box = {
  positions: [-50, -5, -8, -50, 5, -8, -50, 5, 8, -50, -5, 8,
    50, -5, -8, 50, 5, -8, 50, 5, 8, 50, -5, 8],
  indices: [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
    1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7],
};
const part = { entityId: "part-a", profile: { kind: "rect", width: 10, depth: 16 },
  thumbnailGeometryResourceId: "icax-resource://part-a", thumbnailGeometryResourceVersion: 3 };
const makePlan = (id = "stock-a") => ({ id, stockLength: 300, placements: [
  { partId: part.entityId, instanceId: "part-a-1", start: 0, end: 100, length: 100 },
  { partId: part.entityId, instanceId: "part-a-2", start: 105, end: 205, length: 100 },
] });
const waitUntil = async (predicate) => {
  for (let i = 0; i < 100; i++) {
    if (predicate()) return;
    await new Promise((resolve) => setTimeout(resolve, 2));
  }
  throw new Error("Preview did not finish");
};

function fixture() {
  let sourceReads = 0, generation = 0;
  const header = { dataset: {} }, status = { dataset: {}, setAttribute() {} };
  const records = [];
  const viewport = {
    setDimensionAnnotations() {}, setPresentationAxis(...args) { this.axes = args; },
    setStandardView(name) { this.standardView = name; }, fitViewToViewport() { this.fitCount = (this.fitCount ?? 0) + 1; },
    setVisibleEntityIds(ids) { this.visibleEntityIds = [...ids]; },
    async applyViewSnapshot(snapshot, resources) {
      const current = ++generation;
      const record = { snapshot, resources: new Map() };
      records.push(record);
      for (const row of snapshot.rows) {
        for (const reference of [row.data.geometry, row.data.material].filter(Boolean)) {
          record.resources.set(reference.url, await loadRenderResource(resources, reference));
        }
      }
      if (current !== generation) return { applied: false, superseded: true };
      this.latestSnapshot = snapshot;
      return { applied: true, entityIds: snapshot.rows.map((row) => row.entityId) };
    },
  };
  const context = {
    mount: { querySelector(selector) { return selector.includes("preview-status") ? status : header; } },
    sceneProxy: { resources: { async get(url, options) {
      assert.equal(url, part.thumbnailGeometryResourceId);
      assert.equal(options.headers.get("ICAX-Resource-Version"), "3");
      sourceReads++;
      return new Response(encodeNestingGeometry(box));
    } } },
  };
  const view = { activeAreaId: "nesting", tubeDesignerNestingSelectionKind: "plan", tubeDesignerActiveNestingPlanId: "stock-a", viewport };
  return { context, view, header, status, records, sourceReads: () => sourceReads };
}

function testRigidTransform() {
  const parsed = parseRenderGeometryResource(encodeNestingGeometry(box));
  assert.deepEqual([...parsed.positions], box.positions);
  assert.deepEqual([...parsed.indices], box.indices);
  const bounds = meshBounds(parsed);
  assert.deepEqual(bounds.center, [0, 0, 0]);
  const transform = nestingPlacementMatrix(bounds, { start: 105, end: 205 });
  // The shared viewport transposes row-major matrix into THREE's column-major array.
  const threeMatrix = Array.from({ length: 16 }, (_, i) => transform[(i % 4) * 4 + Math.floor(i / 4)]);
  const apply = (point, m = threeMatrix) => [0, 1, 2].map((row) => m[row] * point[0] + m[4 + row] * point[1] + m[8 + row] * point[2] + m[12 + row]);
  assert.deepEqual(apply([-50, -5, -8]), [105, -5, -8]);
  assert.deepEqual(apply([50, 5, 8]), [205, 5, 8]);
  const reverse = nestingPlacementMatrix(bounds, { start: 105, end: 205, reversed: true });
  assert.equal(reverse[0], -1); assert.equal(reverse[5], -1); assert.equal(reverse[10], 1);
  assert.equal(reverse[3] - 50, 105); assert.equal(reverse[3] + 50, 205);
  const rotated = nestingPlacementMatrix(bounds, { start: 105, end: 205, rotationRadians: Math.PI });
  assert.ok(Math.abs(rotated[5] + 1) < 1e-12);
  assert.ok(Math.abs(rotated[10] + 1) < 1e-12);
  assert.ok(Math.abs(rotated[6]) < 1e-12 && Math.abs(rotated[9]) < 1e-12);
  assert.throws(() => nestingPlacementMatrix(bounds, { start: 0, end: 90 }), /实际零件长度/);
  assert.throws(() => meshBounds({ positions: [0, 0, 0, NaN, 0, 0, 0, 0, 0] }), /无效坐标/);
}

async function testActualPartsAndRemnant() {
  const f = fixture(), plan = makePlan();
  scheduleNestingPlanHydration(f.context, f.view, plan, [part]);
  await waitUntil(() => f.view.tubeDesignerNestingPreviewState?.status === "ready");
  assert.equal(f.sourceReads(), 1, "repeated instances share one real geometry read");
  assert.equal(f.view.tubeDesignerNestingPreviewState.approximate, false);
  const record = f.records.at(-1), rows = record.snapshot.rows;
  assert.equal(rows.length, 4);
  assert.equal(new Set(rows.map((row) => row.entityId)).size, 4);
  assert.equal(rows[0].data.geometry.url, part.thumbnailGeometryResourceId);
  assert.equal(rows[0].data.localToWorldMatrix[3], 50);
  assert.equal(rows[1].data.localToWorldMatrix[3], 155);
  assert.notEqual(rows[0].data.material.url, rows[1].data.material.url);
  for (const [index, row] of rows.slice(0, 2).entries()) {
    const material = record.resources.get(row.data.material.url).data;
    const linear = rgbaToCssColor(material.colorRGBA);
    const display = [linear.r, linear.g, linear.b].map((byte) => {
      const channel = byte / 255;
      return Math.round((channel <= 0.0031308 ? channel * 12.92 : 1.055 * channel ** (1 / 2.4) - 0.055) * 255);
    });
    const expected = getNestingPlacementColor(index).slice(1).match(/../g).map((hex) => Number.parseInt(hex, 16));
    assert.ok(display.every((channel, axis) => Math.abs(channel - expected[axis]) <= 2), "3D linear material displays the same saturated color as the CSS swatch");
  }
  assert.equal(new Set(Array.from({ length: 6 }, (_, index) => getNestingPlacementColor(index))).size, 6);
  assert.equal(getNestingPlacementColor(6), getNestingPlacementColor(0));
  assert.equal(getNestingPlacementColor(0, true), "#ffd400");
  assert.ok(Array.from({ length: 24 }, (_, index) => getNestingPlacementColor(index)).every((color) => color !== "#ffd400"));
  const envelope = rows.find((row) => row.entityId.endsWith(":stock-envelope"));
  const outline = record.resources.get(envelope.data.geometry.url).data;
  assert.equal(outline.geometryKind, 2);
  assert.equal(outline.kind, "polyline");
  const along = [...outline.points].filter((_, index) => index % 3 === 0);
  assert.equal(Math.min(...along), 0); assert.equal(Math.max(...along), 300);
  for (let index = 0; index < outline.points.length; index += 6) {
    assert.equal(outline.points[index], outline.points[index + 3], "Stock envelope must not draw gray longitudinal lines across colored parts");
  }
  const envelopeColor = rgbaToCssColor(record.resources.get(envelope.data.material.url).data.colorRGBA);
  assert.ok(envelopeColor.a > 0 && envelopeColor.a < 1, "remnant is translucent");
  const remnant = rows.find((row) => row.entityId.endsWith(":remnant"));
  assert.equal(remnant.data.localToWorldMatrix[3], 205);
  const remnantBounds = meshBounds(record.resources.get(remnant.data.geometry.url).data);
  assert.equal(remnantBounds.max[0] - remnantBounds.min[0], 95);
  assert.equal(f.header.dataset.nestingPreviewPlan, "stock-a");
  assert.equal(f.header.dataset.nestingPreviewEntityCount, "4");
  assert.deepEqual(f.view.viewport.axes, [[1, 0, 0], [1, 0, 0]]);
  assert.equal(f.view.viewport.fitCount, 1);
  f.status.textContent = "replaced DOM";
  scheduleNestingPlanHydration(f.context, f.view, plan, [part]);
  assert.equal(f.status.textContent, "replaced DOM", "publish waits until workbench render completes");
  await new Promise((resolve) => queueMicrotask(resolve));
  assert.match(f.status.textContent, /实际零件/);
  assert.equal(f.view.viewport.fitCount, 1, "unrelated renders preserve user's camera");
  f.view.viewport.setVisibleEntityIds([]);
  scheduleNestingPlanHydration(f.context, f.view, plan, [part]);
  await new Promise((resolve) => queueMicrotask(resolve));
  assert.deepEqual(f.view.viewport.visibleEntityIds, rows.map((row) => row.entityId), "repeat plan selection restores visibility without reloading geometry");
}

async function testMissingResourceIsExplicit() {
  const f = fixture();
  f.context.sceneProxy.resources.get = async () => new Response(null, { status: 404 });
  scheduleNestingPlanHydration(f.context, f.view, makePlan(), [part]);
  await waitUntil(() => f.view.tubeDesignerNestingPreviewState?.status === "ready");
  assert.equal(f.view.tubeDesignerNestingPreviewState.approximateCount, 2);
  assert.match(f.status.textContent, /资源不可用.*包络/);
}

async function testSwitchDuringSourceRead() {
  const f = fixture();
  let release;
  f.context.sceneProxy.resources.get = () => new Promise((resolve) => { release = () => resolve(new Response(encodeNestingGeometry(box))); });
  scheduleNestingPlanHydration(f.context, f.view, makePlan(), [part]);
  await waitUntil(() => release);
  f.view.tubeDesignerActiveNestingPlanId = "stock-b";
  scheduleNestingPlanHydration(f.context, f.view, makePlan("stock-b"), [part]);
  release();
  await waitUntil(() => f.view.tubeDesignerNestingPreviewState?.status === "ready");
  assert.equal(f.view.tubeDesignerNestingPreviewState.planId, "stock-b");
  assert.ok(f.records.every((record) => record.snapshot.rows.every((row) => row.entityId.startsWith("nesting:stock-b:"))));
}

async function testCancelInFlightApply() {
  const f = fixture();
  let release;
  const originalApply = f.view.viewport.applyViewSnapshot.bind(f.view.viewport);
  f.view.viewport.applyViewSnapshot = (snapshot, resources) => {
    if (!snapshot.rows.length) return originalApply(snapshot, resources);
    // Hold the final resource read after applyViewSnapshot already assigned its generation.
    const blocked = { get: async (...args) => {
      if (!release) await new Promise((resolve) => { release = resolve; });
      return resources.get(...args);
    } };
    return originalApply(snapshot, blocked);
  };
  scheduleNestingPlanHydration(f.context, f.view, makePlan(), [part]);
  await waitUntil(() => release);
  f.view.activeAreaId = "products";
  cancelNestingPlanHydration(f.view);
  release();
  await waitUntil(() => f.view.viewport.latestSnapshot);
  await new Promise((resolve) => setTimeout(resolve, 10));
  assert.equal(f.view.viewport.latestSnapshot.rows.length, 0);
  assert.notEqual(f.view.tubeDesignerNestingPreviewState?.status, "ready");
}

testRigidTransform();
await testActualPartsAndRemnant();
await testMissingResourceIsExplicit();
await testSwitchDuringSourceRead();
await testCancelInFlightApply();
console.log("Nesting preview tests passed.");
