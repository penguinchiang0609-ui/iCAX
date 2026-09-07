import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import {
  componentLibraryState, getComponentModelOptions, renderComponentModelField,
  renderComponentLibraryLeftPane, renderComponentLibraryRightPane, renderComponentLibraryDialogs,
  refreshComponentModels, handleComponentLibraryAction, handleComponentLibraryRibbonCommand,
  ensureComponentModelPreview, attachComponentLibrary,
} from "../../apps/tube-designer/webpage/componentLibrary.mjs";
import { renderDesignerAddParameterContent, buildPartCategories } from "../../apps/tube-designer/webpage/designerViews.mjs";
import { handleDesignerAreaAction } from "../../apps/tube-designer/webpage/designerActions.mjs";
import { ribbonDefinition } from "../../apps/tube-designer/webpage/ribbonDefinition.mjs";

const tests = [];
async function test(name, run) { await run(); tests.push(name); }
const deferred = () => { let resolve, reject; const promise = new Promise((a, b) => { resolve = a; reject = b; }); return { promise, resolve, reject }; };
const system = { id: "cap", scope: "system", name: "系统柱帽", category: "柱帽", sourcing: "purchased", material: "不锈钢", description: "内置", sourceFileName: "cap.brep", bounds: { width: 40, depth: 40, height: 12 }, revision: 1 };
const user = { ...system, id: "custom", scope: "user", name: "我的柱帽", revision: 7 };
const geometry = (id = "cap") => ({ geometryResourceId: `resource://${id}`, geometryResourceVersion: 1, materialResourceId: "material://metal", materialResourceVersion: 2 });
function harness(models = [system, user]) {
  const calls = [], snapshots = [], fits = [], visible = [], notices = [];
  let applied = {}, renderCount = 0;
  const view = { activeAreaId: "components", viewport: {
    async applyViewSnapshot(snapshot) { snapshots.push(snapshot); applied = snapshot; return { applied: true }; },
    getAppliedViewState() { return applied; }, setVisibleEntityIds(ids) { visible.push(ids); },
    fitViewForRevision(...args) { fits.push(args); }, setStandardView() {}, setSelectedObjectIds() {}, setDimensionAnnotations() {},
  } };
  const state = componentLibraryState(view);
  state.models = structuredClone(models);
  state.loadState = "loaded";
  const context = {
    productProxy: { async invoke(method, payload) { calls.push({ method, payload }); return { models }; } },
    sceneProxy: { resources: {}, async invoke(method, payload) { calls.push({ method, payload }); return geometry(payload.id); } },
  };
  const ops = { renderProject() { renderCount++; }, showNotice(_context, _view, text) { notices.push(text); } };
  const act = (suffix, target = {}) => handleComponentLibraryAction(context, view, `tube-designer-component-${suffix}`, target, ops);
  return { context, view, state, ops, act, calls, snapshots, fits, visible, notices, renders: () => renderCount };
}

await test("template, system, and personal model references stay strings and missing choices stay selected", () => {
  const { view } = harness();
  const template = { extensions: { modelResources: { clip: { displayName: { "zh-CN": "玻璃夹" } } } } };
  assert.deepEqual(getComponentModelOptions(template, view).map((option) => option.value), ["template:clip", "system:cap", "library:custom"]);
  const html = renderComponentModelField({ key: "capModel", displayName: "柱帽", presentation: { editor: "component-model" } }, "library:missing", false, { view, template });
  assert.match(html, /value="library:missing" selected/);
  assert.match(html, /data-tube-designer-parameter="capModel"/);
  assert.doesNotMatch(html, /data-tube-designer-value-type="number"|data-tube-designer-profile/);
  assert.match(html, /不作为管材截面/);
});

await test("system models are read only, user metadata is editable, and all labels are escaped", () => {
  const { view, state } = harness();
  state.models[0].name = '<script>alert("bad")</script>';
  const left = renderComponentLibraryLeftPane({}, view);
  assert.match(left, /&lt;script&gt;/);
  assert.doesNotMatch(left, /<script>/);
  const systemHtml = renderComponentLibraryRightPane({}, view);
  assert.match(systemHtml, /只读资源/);
  assert.doesNotMatch(systemHtml, /data-cam-action="tube-designer-component-(save|delete)"/);
  assert.match(systemHtml, /宽 X/);
  assert.match(systemHtml, /不能在此修改或缩放/);
  state.selectedKey = "user:custom";
  assert.match(renderComponentLibraryRightPane({}, view), /data-cam-action="tube-designer-component-save"/);
});

await test("concurrent list reads are deduplicated and retry works after missing bridge", async () => {
  const h = harness([]), request = deferred();
  let reads = 0;
  h.context.productProxy.invoke = async () => { reads++; return request.promise; };
  const one = refreshComponentModels(h.context, h.view, h.ops);
  const two = refreshComponentModels(h.context, h.view, h.ops);
  request.resolve({ models: [user] });
  await Promise.all([one, two]);
  assert.equal(reads, 1);
  assert.equal(h.state.selectedKey, "user:custom");
  delete h.context.productProxy;
  await refreshComponentModels(h.context, h.view, h.ops);
  assert.equal(h.state.loadState, "error");
  assert.equal(h.state.loadPromise, null);
  h.context.productProxy = { invoke: async () => ({ models: [system] }) };
  await refreshComponentModels(h.context, h.view, h.ops);
  assert.equal(h.state.loadState, "loaded");
});

await test("import opens filtered file picker, requires confirmation, defaults purchased, and selects returned model", async () => {
  const h = harness();
  let dialog;
  h.context.appProxy = { bridge: { async openFileDialog(options) { dialog = options; return "D:\\Models\\新柱帽.STEP"; } } };
  await h.act("import");
  assert.deepEqual(dialog.filters[0].extensions, ["step", "stp", "brep"]);
  assert.equal(h.calls.length, 0);
  assert.equal(h.state.importDraft.sourcing, "purchased");
  assert.match(renderComponentLibraryDialogs(h.view), /导入三维配件/);
  await h.act("cancel-import");
  assert.equal(h.state.importDraft, null);
  await h.act("import");
  h.context.productProxy.invoke = async (method, payload) => {
    h.calls.push({ method, payload });
    assert.equal(h.view.pending, true);
    assert.equal(h.view.tubeDesignerOperation.kind, "components");
    return { model: { ...user, id: "new", name: payload.name } };
  };
  await h.act("confirm-import");
  assert.equal(h.calls[0].method, "TubeDesigner.ImportComponentModel");
  assert.equal(h.calls[0].payload.sourcePath, "D:\\Models\\新柱帽.STEP");
  assert.equal(h.calls[0].payload.sourcing, "purchased");
  assert.equal(h.state.selectedKey, "user:new");
  assert.equal(h.view.pending, false);
  assert.equal(h.state.importDraft, null);
});

await test("unsupported import extensions and empty names never mutate the library", async () => {
  const h = harness();
  h.context.appProxy = { bridge: { async openFileDialog() { return "D:\\model.exe"; } } };
  await assert.rejects(h.act("import"), /STEP、STP 或 BREP/);
  h.state.importDraft = { sourcePath: "D:\\a.step", name: " ", sourcing: "purchased" };
  await assert.rejects(h.act("confirm-import"), /模型名称/);
  assert.equal(h.calls.length, 0);
});

await test("metadata drafts preserve focus and their original expected revision across refresh", async () => {
  const h = harness();
  h.state.selectedKey = "user:custom";
  const before = h.renders();
  await h.act("draft", { dataset: { componentField: "name" }, value: "更新名称" });
  assert.equal(h.renders(), before);
  h.state.models[1].revision = 8;
  h.context.productProxy.invoke = async (method, payload) => { h.calls.push({ method, payload }); return { model: { ...user, ...payload, revision: 9 } }; };
  await h.act("save");
  assert.equal(h.calls[0].payload.expectedRevision, 7);
  assert.equal(h.calls[0].payload.name, "更新名称");
  assert.equal(h.state.drafts["user:custom"], undefined);
  await h.act("save", { dataset: { componentKey: "system:cap" } });
  await h.act("delete", { dataset: { componentKey: "system:cap" } });
  assert.equal(h.calls.length, 1);
  assert.equal(h.state.deleteDialog, undefined);
});

await test("delete is explicitly confirmed using the recorded revision and preserves failures", async () => {
  const h = harness();
  h.state.selectedKey = "user:custom";
  await h.act("delete");
  assert.equal(h.calls.length, 0);
  assert.match(renderComponentLibraryDialogs(h.view), /确认删除/);
  await h.act("cancel-delete");
  assert.equal(h.state.models.length, 2);
  await h.act("delete");
  h.state.models[1].revision = 8;
  h.context.productProxy.invoke = async (method, payload) => { h.calls.push({ method, payload }); return { deleted: true }; };
  await h.act("confirm-delete");
  assert.deepEqual(h.calls[0], { method: "TubeDesigner.DeleteComponentModel", payload: { id: "custom", expectedRevision: 7 } });
  assert.equal(h.state.models.length, 1);
  assert.equal(h.state.selectedKey, "system:cap");
  h.state.models.push(user); h.state.selectedKey = "user:custom";
  await h.act("delete");
  h.context.productProxy.invoke = async () => { throw new Error("revision conflict"); };
  await assert.rejects(h.act("confirm-delete"), /revision conflict/);
  assert.equal(h.state.models.length, 2);
  assert.equal(h.view.pending, false);
  assert.ok(h.state.deleteDialog);
});

await test("late list responses cannot overwrite an imported model", async () => {
  const h = harness(), request = deferred();
  h.context.productProxy.invoke = async (method) => method.endsWith("ListComponentModels") ? request.promise : { model: { ...user, id: "new" } };
  const loading = refreshComponentModels(h.context, h.view, h.ops);
  h.state.importDraft = { sourcePath: "D:\\model.step", name: "新模型", sourcing: "purchased" };
  await h.act("confirm-import");
  request.resolve({ models: [system, user] });
  await loading;
  assert.equal(h.state.models.length, 3);
  assert.equal(h.state.selectedKey, "user:new");
});

await test("preview uses actual scene geometry, caches responses, and does not refit on rerender", async () => {
  const h = harness();
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  assert.equal(h.calls[0].method, "TubeDesigner.GenerateComponentModelPreview");
  assert.deepEqual(h.calls[0].payload, { scope: "system", id: "cap" });
  assert.deepEqual(h.snapshots[0].rows[0].data.geometry, { url: "resource://cap", version: 1 });
  assert.deepEqual(h.snapshots[0].rows[0].data.material, { url: "material://metal", version: 2 });
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  assert.equal(h.fits.length, 1);
  h.state.selectedKey = "user:custom";
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  h.state.selectedKey = "system:cap";
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  assert.equal(h.calls.length, 2);
  assert.equal(h.snapshots.length, 3);
});

await test("cached preview restores entity visibility after the shared workbench remount hides manual areas", async () => {
  const h = harness();
  h.ops.renderProject = () => {
    // createWorkbench.attachViewport clears visibility when the area does not
    // own a ViewReader snapshot, including every manual component preview.
    h.view.viewport.setVisibleEntityIds([]);
    attachComponentLibrary(h.context, h.view, null, h.ops);
  };
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(h.visible.at(-1), ["component-model:system:cap"]);
  assert.equal(h.snapshots.length, 1);
  assert.equal(h.fits.length, 1);
  h.ops.renderProject();
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(h.visible.at(-1), ["component-model:system:cap"]);
  assert.equal(h.snapshots.length, 1, "Remount should not reload the mesh");
  assert.equal(h.fits.length, 1, "Remount should preserve the user's camera");
});

await test("out of order preview responses cannot replace latest selection or another area", async () => {
  const h = harness(), first = deferred(), second = deferred();
  h.context.sceneProxy.invoke = (_method, payload) => payload.id === "cap" ? first.promise : second.promise;
  const a = ensureComponentModelPreview(h.context, h.view, h.ops);
  h.state.selectedKey = "user:custom";
  const b = ensureComponentModelPreview(h.context, h.view, h.ops);
  second.resolve(geometry("custom")); await b;
  first.resolve(geometry("cap")); await a;
  assert.equal(h.snapshots.length, 1);
  assert.equal(h.snapshots[0].rows[0].entityId, "component-model:user:custom");
  const third = deferred();
  h.context.sceneProxy.invoke = () => third.promise;
  h.state.selectedKey = "system:cap";
  const c = ensureComponentModelPreview(h.context, h.view, h.ops);
  h.view.activeAreaId = "view";
  third.resolve(geometry("cap")); await c;
  assert.equal(h.snapshots.length, 1);
});

await test("invalid previews stop automatic retry loops and explicit retry can recover", async () => {
  const h = harness(); let calls = 0;
  h.context.sceneProxy.invoke = async () => { calls++; return {}; };
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  assert.equal(calls, 1);
  assert.match(h.state.previewError, /有效的三维几何/);
  await h.act("retry-preview");
  h.context.sceneProxy.invoke = async () => geometry();
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  assert.equal(h.snapshots.length, 1);
  assert.equal(h.state.previewError, "");
});

await test("empty library clears the previous scene and lazy loading does not run outside relevant areas", async () => {
  const h = harness([]);
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  assert.deepEqual(h.snapshots[0], { revision: "components-empty", rows: [] });
  h.state.loadState = "idle"; h.view.activeAreaId = "view";
  attachComponentLibrary(h.context, h.view, null, h.ops);
  await Promise.resolve();
  assert.equal(h.calls.length, 0);
  h.view.scene = { tubeDesigner: { product: { templateId: "a" }, templates: [{ id: "a", parameters: [{ presentation: { editor: "component-model" } }] }] } };
  attachComponentLibrary(h.context, h.view, null, h.ops);
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(h.calls[0].method, "TubeDesigner.ListComponentModels");
});

await test("component tab and action routes are independent of tube profile and nesting workflows", async () => {
  const tab = ribbonDefinition.tabs.find((item) => item.id === "components");
  assert.equal(tab.title, "配件库");
  assert.deepEqual(tab.groups[0].commands.map((item) => item.id), ["components.import", "components.refresh"]);
  const h = harness();
  await handleDesignerAreaAction(h.context, h.view, "tube-designer-component-select", { dataset: { componentKey: "user:custom" } }, h.ops);
  assert.equal(h.state.selectedKey, "user:custom");
  assert.equal(await handleComponentLibraryRibbonCommand(h.context, h.view, "unrelated", h.ops), false);
  const entry = readFileSync(new URL("../../apps/tube-designer/webpage/entry.mjs", import.meta.url), "utf8");
  assert.match(entry, /components:\s*\{\s*left: renderComponentLibraryLeftPane/);
  assert.match(entry, /\["profiles", "components", "sketch", "nesting", "about"\]/);
});

await test("add template renders component selectors and breakdown distinguishes glass and accessories", () => {
  const h = harness();
  const template = { id: "model-guard", available: true, name: "护栏/玻璃护栏", extensions: { modelResources: { clip: { name: "玻璃夹" } } },
    parameters: [{ key: "clip", type: "text", defaultValue: "template:clip", displayName: "玻璃夹", presentation: { editor: "component-model" } }] };
  h.view.scene = { tubeDesigner: { templates: [template] } };
  h.view.tubeDesignerAddTemplateId = template.id;
  const html = renderDesignerAddParameterContent(h.view.scene.tubeDesigner, h.view);
  assert.match(html, /value="template:clip" selected/);
  assert.match(html, /value="library:custom"/);
  const parts = [
    { entityId: "glass", partKind: "glass", length: 300, properties: { "manufacturing.plate": { width: 400, height: 800, thickness: 8 } } },
    { entityId: "cap", partKind: "accessory", length: 40, properties: { "manufacturing.modelName": "柱帽", "manufacturing.modelBounds": { width: 40, depth: 40, height: 12 }, "manufacturing.sourcing": "purchased" } },
  ];
  const categories = buildPartCategories(parts);
  assert.match(categories[0].specifications[0], /玻璃 400 × 800 × 8 mm/);
  assert.match(categories[1].specifications[0], /柱帽 40 × 40 × 12 mm · 外购/);
  assert.deepEqual(categories.map((item) => item.lengths), [[], []]);
});

console.log(`TubeDesignerComponentLibraryTest: ${tests.length} passed`);
