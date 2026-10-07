import assert from "node:assert/strict";

const durable = new Map();
globalThis.localStorage = { getItem: key => durable.get(key) ?? null, setItem: (key, value) => durable.set(key, value) };
const { createWindowStateMemory } = await import("../../iCAX-UI/SDK/Forms/windowStateMemory.mjs");
const { rememberComponentCreationDraft, restoreComponentCreationDraft } = await import("../../apps/tube-designer/webpage/componentCreationMemory.mjs");
const { componentLibraryState, handleComponentLibraryAction, ensureComponentCSGPreview,
  openComponentCSGEditor } = await import("../../apps/tube-designer/webpage/componentLibrary.mjs");
const checks = [];
async function test(name, run) { durable.clear(); await run(); checks.push(name); }
const primitives = { extrusion: { parameters: { height: 30 } }, box: { parameters: { width: 40, depth: 40, height: 20 } } };
const feature = (primitive, index = 0) => ({ id: `fresh-${index}`, name: primitive, primitive,
  operation: index ? "union" : "base", parameters: { ...primitives[primitive].parameters },
  transform: { position: { x: 0, y: 0, z: 0 }, rotation: { x: 0, y: 0, z: 0 } },
  ...(primitive === "extrusion" ? { profile: { key: "builtin:solid-rectangle", parameters: { width: 40, depth: 30 } } } : {}) });
const draft = () => ({ mode: "create", id: "", name: "新零件", category: "自制", material: "", description: "",
  features: [feature("extrusion")], selectedId: "fresh-0", selectedNodeKind: "primitive", tool: "view" });
const storage = () => createWindowStateMemory({ namespace: "icax.tube-designer.window-state" });
const rectangle = (width = 40, depth = 20) => ({ name: "当前矩形", width, depth, contours: [{ kind: "polygon",
  points: [[-width / 2, -depth / 2], [width / 2, -depth / 2], [width / 2, depth / 2], [-width / 2, depth / 2]] }] });
const libraryProfile = () => ({ id: "rect", name: "当前矩形", profileType: "parametric-package", profileForm: "parametric",
  descriptor: { parameters: [{ key: "width", valueType: "number", defaultValue: 40, min: 1, max: 100 },
    { key: "depth", valueType: "number", defaultValue: 20, min: 1 },
    { key: "internal", valueType: "number", defaultValue: 1, presentation: { visible: false } }] },
  defaultParameters: { width: 40, depth: 20, internal: 1 }, previewProfile: rectangle() });
function harness(profiles = []) {
  const calls = [], snapshots = [];
  const view = { activeAreaId: "components", tubeDesignerSystemProfiles: profiles, viewport: {
    async applyViewSnapshot(value) { snapshots.push(value); return { applied: true }; },
    setVisibleEntityIds() {}, setSelectedObjectIds() {}, setDimensionAnnotations() {}, setStandardView() {}, fitViewToViewport() {},
  } };
  const state = componentLibraryState(view); state.loadState = "loaded";
  const context = { productProxy: { async invoke(method, payload) { calls.push({ scope: "product", method, payload: structuredClone(payload) });
    if (method === "TubeDesigner.EvaluateProfilePackage") return { profile: { ...rectangle(payload.parameters.width, payload.parameters.depth), parameters: payload.parameters } };
    if (method === "TubeDesigner.CreateComponentCSGModel") return { model: { id: "saved", scope: "user", modelType: "csg", revision: 1, ...payload } };
    throw Error("Unexpected product method: " + method);
  } }, sceneProxy: { resources: {}, async invoke(method, payload) {
    calls.push({ scope: "scene", method, payload: structuredClone(payload) });
    assert.equal(method, "TubeDesigner.GenerateComponentCSGPreview");
    return { valid: true, geometryResourceId: "resource://preview", geometryResourceVersion: 1, bounds: { width: 40, depth: 40, height: 30 } };
  } } };
  const surface = { addEventListener() {}, classList: { add() {}, remove() {} } };
  const mount = { querySelector: selector => selector === "[data-component-csg-manipulation]" ? surface : null };
  context.mount = mount;
  const ops = { renderProject() {}, showNotice() {} };
  const act = (suffix, target = {}) => handleComponentLibraryAction(context, view, "tube-designer-component-" + suffix, target, ops);
  return { view, state, context, calls, snapshots, mount, act };
}
async function preview(h) {
  ensureComponentCSGPreview(h.context, h.view, h.mount);
  const deadline = Date.now() + 2000;
  while (!["ready", "error"].includes(h.state.csgDraft.previewStatus) && Date.now() < deadline)
    await new Promise(resolve => setTimeout(resolve, 10));
  assert.equal(h.state.csgDraft.previewStatus, "ready", h.state.csgDraft.previewError);
}
async function close(h) { await h.act("cancel-csg"); ensureComponentCSGPreview(h.context, h.view, h.mount); }

await test("creation stores only bounded user inputs and restores stable nodes through a fresh memory reader", () => {
  const original = draft(); original.name = "最近的零件"; original.features.push(feature("box", 1));
  original.features[0].id = "extrusion-A"; original.features[1].id = "box-B";
  original.features[0].profile = { key: "system:rect", parameters: { width: 55, depth: 20 }, parameterDefinitions: [{ key: "width" }, { key: "depth" }], snapshot: rectangle(55),
    profileRef: { scope: "system", id: "rect" }, geometryResourceId: "must-not-persist" };
  original.features[1].parameters.width = 17; original.features[1].transform.position.y = -12;
  original.selectedId = "box-B"; original.selectedNodeKind = "operation"; original.tool = "rotate";
  original.previewBounds = { width: 999 }; original.previewError = "old";
  assert.equal(rememberComponentCreationDraft(original, { primitives, storage: storage() }), true);
  const raw = [...durable.values()].join("");
  assert.doesNotMatch(raw, /snapshot|contours|profileRef|geometryResourceId|previewBounds|previewError/);
  const restored = draft(); restored.previewStatus = "idle";
  assert.equal(restoreComponentCreationDraft(restored, { primitives, createFeature: feature, storage: storage(),
    resolveProfile: (key, parameters) => ({ key, parameters, snapshot: rectangle(parameters.width) }) }), true);
  assert.deepEqual(restored.features.map(item => item.id), ["extrusion-A", "box-B"]);
  assert.equal(restored.features[1].parameters.width, 17); assert.equal(restored.features[1].transform.position.y, -12);
  assert.equal(restored.selectedId, "box-B"); assert.equal(restored.tool, "rotate");
  assert.equal(restored.features[0].profile.snapshot.width, 55); assert.equal(restored.previewStatus, "idle");
});
await test("saved entity edits cannot read or replace new creation defaults", () => {
  const memory = storage(), original = draft(); original.name = "创建草稿";
  rememberComponentCreationDraft(original, { primitives, storage: memory });
  const edited = { ...draft(), mode: "edit", id: "entity-one", name: "已有模型" };
  assert.equal(restoreComponentCreationDraft(edited, { primitives, createFeature: feature, storage: memory }), false);
  assert.equal(rememberComponentCreationDraft(edited, { primitives, storage: memory }), false);
  assert.equal(edited.name, "已有模型"); assert.equal(memory.read("component-csg-create", "inputs").name, "创建草稿");
});
await test("unknown primitives duplicate nodes and nonfinite dimensions never override current defaults", () => {
  const memory = storage(); memory.write("component-csg-create", "inputs", { name: "有效名字", selectedId: "missing", tool: "unsupported",
    features: [{ id: "unknown", primitive: "deleted" }, { id: "A", primitive: "extrusion", parameters: { height: "bad" }, profile: { key: "removed:profile", parameters: {} } },
      { id: "A", primitive: "box" }, { id: "B", primitive: "box", operation: "bad", parameters: { width: 18, unsafe: 7 } }] });
  const restored = draft(); restoreComponentCreationDraft(restored, { primitives, createFeature: feature, storage: memory, resolveProfile: () => null });
  assert.deepEqual(restored.features.map(item => item.id), ["A", "B"]);
  assert.equal(restored.features[0].parameters.height, 30); assert.equal(restored.features[1].operation, "union");
  assert.equal(restored.features[1].parameters.unsafe, undefined); assert.equal(restored.features[0].profile.key, "builtin:solid-rectangle");
  assert.equal(restored.selectedId, "A"); assert.equal(restored.tool, "view");
});
await test("actual CSG actions remember metadata builtin parameters transforms nodes and submitted payload", async () => {
  const h = harness(); await h.act("draw");
  await h.act("csg-model-change", { dataset: { csgModelField: "name" }, value: "验收零件" });
  await h.act("csg-model-change", { dataset: { csgModelField: "category" }, value: "支架" });
  await h.act("csg-profile-select", { value: "builtin:solid-circle" });
  await h.act("csg-profile-parameter-change", { dataset: { csgProfileParameter: "diameter" }, value: "62" });
  await h.act("csg-feature-change", { dataset: { csgFeatureField: "parameters.height" }, value: "75" });
  await h.act("csg-add", { dataset: { csgPrimitive: "box" } });
  await h.act("csg-feature-change", { dataset: { csgFeatureField: "parameters.width" }, value: "13" });
  await h.act("csg-feature-change", { dataset: { csgFeatureField: "position.x" }, value: "8" });
  const ids = h.state.csgDraft.features.map(item => item.id); await close(h); await h.act("draw");
  const restored = h.state.csgDraft;
  assert.equal(restored.name, "验收零件"); assert.equal(restored.category, "支架"); assert.deepEqual(restored.features.map(item => item.id), ids);
  assert.equal(restored.features[0].profile.key, "builtin:solid-circle"); assert.equal(restored.features[0].profile.snapshot.width, 62);
  assert.equal(restored.features[0].parameters.height, 75); assert.equal(restored.features[1].parameters.width, 13);
  assert.equal(restored.features[1].transform.position.x, 8); assert.equal(h.calls.length, 0);
  await preview(h); await h.act("save-csg");
  const submitted = h.calls.find(item => item.method === "TubeDesigner.CreateComponentCSGModel").payload;
  assert.equal(submitted.name, "验收零件"); assert.equal(submitted.csgDefinition.features[0].profile.parameters.diameter, 62);
  assert.equal(submitted.csgDefinition.features[1].parameters.width, 13);
  ensureComponentCSGPreview(h.context, h.view, h.mount);
});
await test("remembered library inputs rebuild the current profile before scene generation without persisting snapshots", async () => {
  const h = harness([libraryProfile()]); await h.act("draw"); await h.act("csg-profile-select", { value: "system:rect" });
  await h.act("csg-profile-parameter-change", { dataset: { csgProfileParameter: "width" }, value: "55" });
  await close(h); h.calls.length = 0;
  h.view.tubeDesignerSystemProfiles[0].previewProfile = rectangle(42); await h.act("draw"); await preview(h);
  assert.deepEqual(h.calls.map(item => [item.scope, item.method]), [["product", "TubeDesigner.EvaluateProfilePackage"], ["scene", "TubeDesigner.GenerateComponentCSGPreview"]]);
  assert.equal(h.calls[1].payload.csgDefinition.features[0].profile.snapshot.width, 55);
  assert.equal(h.calls[0].payload.parameters.internal, 1); assert.doesNotMatch([...durable.values()].join(""), /contours|snapshot|geometryResource|internal/);
  await close(h);
});
await test("unavailable current resource or unusable remembered profile uses a usable builtin creation default", async () => {
  const h = harness([libraryProfile()]); await h.act("draw"); await h.act("csg-profile-select", { value: "system:rect" });
  await h.act("csg-profile-parameter-change", { dataset: { csgProfileParameter: "width" }, value: "55" });
  await close(h); h.context.productProxy.invoke = async () => { throw Error("current resource no longer supports these inputs"); };
  await h.act("draw"); await preview(h);
  assert.equal(h.state.csgDraft.features[0].profile.key, "builtin:solid-rectangle"); await close(h);
  h.view.tubeDesignerSystemProfiles = []; await h.act("draw"); await preview(h);
  assert.equal(h.state.csgDraft.features[0].profile.key, "builtin:solid-rectangle"); await close(h);
});
await test("actual existing entity open keeps its own metadata and node definitions", async () => {
  const h = harness(); await h.act("draw"); await h.act("csg-model-change", { dataset: { csgModelField: "name" }, value: "新建草稿名字" }); await close(h);
  const entity = { id: "stored", scope: "user", modelType: "csg", revision: 9, name: "真实已有模型", category: "已有分类",
    csgDefinition: { features: [{ ...feature("box"), id: "entity-node", parameters: { width: 6, depth: 7, height: 8 } }] } };
  assert.equal(openComponentCSGEditor(h.view, entity), true); assert.equal(h.state.csgDraft.name, entity.name);
  assert.equal(h.state.csgDraft.features[0].id, "entity-node"); assert.equal(h.state.csgDraft.features[0].parameters.width, 6);
  await h.act("csg-model-change", { dataset: { csgModelField: "name" }, value: "修改已有模型" }); await close(h); await h.act("draw");
  assert.equal(h.state.csgDraft.name, "新建草稿名字"); await close(h);
});
console.log(`TubeDesignerComponentCreationMemoryTest: ${checks.length} passed`);
