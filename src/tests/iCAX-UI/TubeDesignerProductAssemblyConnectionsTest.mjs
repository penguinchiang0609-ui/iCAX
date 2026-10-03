import assert from "node:assert/strict";
import {
  ensureProductAssemblyConnections,
  renderProductAssemblyConnections,
} from "../../apps/tube-designer/webpage/productAssemblyConnections.mjs";

const view = {
  activeAreaId: "assemblies",
  scene: { tubeDesigner: {
    activeProductId: "product-a",
    generationRun: { entityId: "run-a" },
    product: { modelOutdated: false, partsOutdated: false },
  } },
};
const pending = [];
let renders = 0;
const context = { sceneProxy: { invoke(method, payload) {
  assert.equal(method, "TubeDesigner.GetProductAssemblyConnections");
  return new Promise((resolve, reject) => pending.push({ payload, resolve, reject }));
} } };
const ops = { renderProject() { renders += 1; } };

const first = ensureProductAssemblyConnections(context, view, ops);
assert.deepEqual(pending[0].payload, { productEntityId: "product-a", generationRunId: "run-a" });
assert.match(renderProductAssemblyConnections(view), /正在读取已提交模型/);
assert.equal(ensureProductAssemblyConnections(context, view, ops), first);

// A response for an old product must not replace the current product list.
view.scene.tubeDesigner.activeProductId = "product-b";
view.scene.tubeDesigner.generationRun.entityId = "run-b";
const second = ensureProductAssemblyConnections(context, view, ops);
pending[0].resolve({
  productEntityId: "product-a", generationRunId: "run-a",
  source: "committed-product-model", connections: [{ key: "stale" }],
});
await first;
assert.doesNotMatch(renderProductAssemblyConnections(view), /stale/);

pending[1].resolve({
  productEntityId: "product-b", generationRunId: "run-b",
  source: "committed-product-model", modelOutdated: false, partsOutdated: true,
  connections: [{ key: "joint.001", kind: "weld", participants: [
    { itemKey: "frame.left", name: "左边框", manufacturingMappingStatus: "persisted" },
    { itemKey: "bar.001", name: "第 1 竖杆", manufacturingMappingStatus: "persisted" },
  ] }],
});
await second;
const html = renderProductAssemblyConnections(view, "joint.001");
assert.match(html, /1 处 · 已提交模型/);
assert.match(html, /A：左边框 \+ B：第 1 竖杆/);
assert.match(html, /焊接/);
assert.match(html, /下料件已保存/);
assert.match(html, /拆单已变化/);
assert.match(html, /data-cam-action="tube-designer-assembly-select-connection"/);
assert.match(html, /aria-pressed="true"/);
assert.doesNotMatch(html, /<details>/, "real connections must be visible without opening a collapsed section");
assert.match(html, /先选构件的连接节点，再选装配工艺/);
assert.equal(renders, 1);

const sharedSources = ["left", "front", "right"].map((itemKey, index) => ({
  itemKey, name: `${index + 1} 段成品管`, manufacturingMappingStatus: "shared-stock",
  stockRefs: [{ stockEntityId: "stock-u", itemKey: "stock.top", partMappingStatus: "persisted",
    spans: [{ stockStart: index * 300, startReserve: 0, endReserve: 0 }] }],
}));
view.tubeDesignerAssemblyProductConnections.result.connections[0].participants = sharedSources;
const unchangedSources = JSON.stringify(sharedSources);
assert.match(renderProductAssemblyConnections(view), /共用母材已保存/);
assert.doesNotMatch(renderProductAssemblyConnections(view), /含非独立下料构件/);
assert.equal(JSON.stringify(sharedSources), unchangedSources);
sharedSources[0].stockRefs[0].partMappingStatus = "planned";
assert.match(renderProductAssemblyConnections(view), /共用母材待生成/);
sharedSources[0].stockRefs[0].partMappingStatus = "transient";
assert.match(renderProductAssemblyConnections(view), /共用母材待保存/);
sharedSources[0].stockRefs[0].partMappingStatus = "not-present";
assert.match(renderProductAssemblyConnections(view), /下料件未就绪/);

// Recomputing actual stocks invalidates cached status even when the finished
// product run and the manually bound process list are unchanged.
const revisionView = { activeAreaId: "assemblies", scene: { tubeDesigner: {
  activeProductId: "revision-product", generationRun: { entityId: "revision-run" }, product: {},
  manufacturingGroups: [{ productEntityId: "revision-product", generationRunId: "revision-run",
    parts: [{ entityId: "same-stock", length: 900, manufacturingGeometryResourceVersion: 1 }] }],
} } };
let revisionReads = 0;
const revisionContext = { sceneProxy: { async invoke(method, payload) {
  revisionReads += 1;
  return { ...payload, source: "committed-product-model", connections: [] };
} } };
await ensureProductAssemblyConnections(revisionContext, revisionView, ops);
await ensureProductAssemblyConnections(revisionContext, revisionView, ops);
assert.equal(revisionReads, 1);
revisionView.scene.tubeDesigner.manufacturingGroups[0].parts[0].manufacturingGeometryResourceVersion = 2;
await ensureProductAssemblyConnections(revisionContext, revisionView, ops);
assert.equal(revisionReads, 2, "new geometry of the same stock must refresh connection manufacturing status");
revisionView.scene.tubeDesigner.product.partsOutdated = true;
await ensureProductAssemblyConnections(revisionContext, revisionView, ops);
assert.equal(revisionReads, 3);

view.scene.tubeDesigner.activeProductId = "";
view.scene.tubeDesigner.product = null;
assert.match(renderProductAssemblyConnections(view), /选中产品后/);

console.log("TubeDesignerProductAssemblyConnectionsTest: passed");
