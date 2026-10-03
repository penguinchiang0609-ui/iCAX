import assert from "node:assert/strict";
import { assemblyProductRows, assemblyProductParts, assemblyProductPartRows,
  ensureAssemblyProductScene, assemblyProductSceneState, stopAssemblyProductScene,
  ensureAssemblyProductResources, assemblyProductResourceState, stopAssemblyProductResourceHydration,
} from "../../apps/tube-designer/webpage/assemblyProductScene.mjs";

const transform = [0, -1, 0, 128, 1, 0, 0, 56, 0, 0, 1, 32, 0, 0, 0, 1];
const view = { activeAreaId: "assemblies", tubeDesignerAssemblyLibrary: { workMode: "product" },
  scene: { tubeDesigner: { activeProductId: "product-a", generationRun: { entityId: "run-a" }, product: {},
    members: [{ entityId: "member-a" }], parts: [{ entityId: "part-a", thumbnailGeometryResourceId: "actual-cut-mesh", thumbnailGeometryResourceVersion: 7 }] } } };
const snapshot = { revision: "4", rows: [{ entityId: "member-a", data: { geometry: { url: "real-prototype", version: 4 }, localToWorldMatrix: transform } },
  { entityId: "other-product", data: { geometry: { url: "wrong" } } }] };
assert.deepEqual(assemblyProductRows(view, snapshot).map((row) => row.entityId), ["member-a"]);
assert.deepEqual(assemblyProductRows(view, snapshot)[0].data.localToWorldMatrix, transform);
assert.equal(assemblyProductRows(view, snapshot)[0].data.selectable, false);
assert.equal(snapshot.rows[0].data.selectable, undefined, "never mutate the shared View snapshot");
assert.deepEqual(assemblyProductPartRows(assemblyProductParts(view)[0])[0].data.geometry, { url: "actual-cut-mesh", version: 7 });
assert.equal(assemblyProductPartRows(assemblyProductParts(view)[0])[0].data.localToWorldMatrix, undefined,
  "normalized machining geometry must not borrow the product prototype placement");
view.scene.tubeDesigner.product.partsOutdated = true;
assert.deepEqual(assemblyProductParts(view), [], "outdated machining must not masquerade as the current result");
view.scene.tubeDesigner.product.partsOutdated = false;

const starts = [];
let renders = 0;
view.tubeDesignerAssemblyLibraryRenderProject = () => renders++;
const context = { sceneProxy: { views: { start(definition, options) {
  return new Promise((resolve) => starts.push({ definition, options, resolve }));
} } } };
const old = ensureAssemblyProductScene(context, view);
assert.strictEqual(ensureAssemblyProductScene(context, view), old);
assert.equal(starts.length, 1, "editing process parameters must reuse the product reader");
assert.equal(starts[0].definition.sources[0].parameters.activeProductId.value, "product-a");
view.scene.tubeDesigner.activeProductId = "product-b";
view.scene.tubeDesigner.generationRun.entityId = "run-b";
const current = ensureAssemblyProductScene(context, view);
let stopped = 0;
starts[0].options.onChange(snapshot);
starts[0].resolve({ stop: async () => stopped++, poll: async () => snapshot });
await old.promise;
assert.equal(stopped, 1);
assert.equal(renders, 0, "late previous-product data cannot repaint the new product");
starts[1].resolve({ snapshot, stop: async () => stopped++, poll: async () => snapshot });
await current.promise;
assert.strictEqual(assemblyProductSceneState(view).snapshot, snapshot);
assert.equal(renders, 1);
stopAssemblyProductScene(view);
starts[1].options.onChange({ ...snapshot, revision: "5" });
assert.equal(renders, 1, "closed readers cannot repaint after navigation");
assert.equal(stopped, 2);

const hydrationView = { activeAreaId: "assemblies", tubeDesignerAssemblyLibrary: { workMode: "product" },
  scene: { tubeDesigner: { activeProductId: "product-a", generationRun: { entityId: "run-a" } } } };
const hydrationCalls = [];
let hydrationRenders = 0;
hydrationView.tubeDesignerAssemblyLibraryRenderProject = () => hydrationRenders++;
const hydrationContext = { sceneProxy: { invoke(method, payload, options) {
  return new Promise((resolve) => hydrationCalls.push({ method, payload, options, resolve }));
} } };
const oldHydration = ensureAssemblyProductResources(hydrationContext, hydrationView);
assert.strictEqual(ensureAssemblyProductResources(hydrationContext, hydrationView), oldHydration,
  "one product needs only one restoration request");
await Promise.resolve();
assert.deepEqual(hydrationCalls[0].payload, { includeManufacturingGeometry: true });
assert.equal(hydrationCalls[0].options.timeoutMs, 180000);
hydrationView.scene.tubeDesigner.activeProductId = "product-b";
hydrationView.scene.tubeDesigner.generationRun.entityId = "run-b";
const newHydration = ensureAssemblyProductResources(hydrationContext, hydrationView);
await Promise.resolve();
hydrationCalls[0].resolve({ tubeDesigner: { activeProductId: "obsolete" } });
await oldHydration.promise;
assert.equal(hydrationRenders, 0, "late prior-product restore cannot repaint the new product");
const activeDesigner = hydrationView.scene.tubeDesigner;
hydrationCalls[1].resolve({ tubeDesigner: { activeProductId: "stale-snapshot" } });
await newHydration.promise;
assert.equal(assemblyProductResourceState(hydrationView).status, "ready");
assert.strictEqual(hydrationView.scene.tubeDesigner, activeDesigner,
  "the restore response must not replace a more recent binding or editor snapshot");
assert.equal(hydrationRenders, 1);
const reopenedSceneProxy = { ...hydrationContext.sceneProxy };
const reopenedHydration = ensureAssemblyProductResources({ sceneProxy: reopenedSceneProxy }, hydrationView);
assert.notStrictEqual(reopenedHydration, newHydration,
  "reopening the same product in a new scene must restore its new resource library");
await Promise.resolve();
assert.equal(hydrationCalls.length, 3);
hydrationCalls[2].resolve({ tubeDesigner: { activeProductId: "obsolete-after-reopen" } });
await reopenedHydration.promise;
assert.equal(hydrationRenders, 2);
assert.strictEqual(hydrationView.scene.tubeDesigner, activeDesigner);
stopAssemblyProductResourceHydration(hydrationView);
assert.equal(assemblyProductResourceState(hydrationView).status, "idle");
console.log("AssemblyProductSceneTest: passed");
