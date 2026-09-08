import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import {
  componentLibraryState, componentModelKey, getComponentModels, getVisibleComponentModels, getComponentModelOptions, renderComponentModelField,
  renderComponentLibraryLeftPane, renderComponentLibraryRightPane, renderComponentLibraryDialogs,
  renderComponentLibraryViewportOverlay,
  refreshComponentModels, handleComponentLibraryAction, handleComponentLibraryRibbonCommand,
  ensureComponentModelPreview, ensureComponentCSGPreview, attachComponentLibrary,
} from "../../apps/tube-designer/webpage/componentLibrary.mjs";
import { renderDesignerAddParameterContent, buildPartCategories } from "../../apps/tube-designer/webpage/designerViews.mjs";
import { handleDesignerAreaAction } from "../../apps/tube-designer/webpage/designerActions.mjs";
import { ribbonDefinition } from "../../apps/tube-designer/webpage/ribbonDefinition.mjs";

const tests = [];
async function test(name, run) { await run(); tests.push(name); }

await test("bottom import button appears only in personal components", () => {
  for (const scope of ["system", "template", "user"]) {
    const view = {};
    componentLibraryState(view).scope = scope;
    const html = renderComponentLibraryLeftPane({}, view);
    assert.equal(html.includes('data-cam-action="tube-designer-component-import"'), scope === "user");
    assert.equal(html.includes('data-cam-action="tube-designer-component-draw"'), scope === "user");
  }
});
const deferred = () => { let resolve, reject; const promise = new Promise((a, b) => { resolve = a; reject = b; }); return { promise, resolve, reject }; };
const system = { id: "cap", scope: "system", name: "系统柱帽", category: "柱帽", sourcing: "purchased", material: "不锈钢", description: "内置", sourceFileName: "cap.brep", bounds: { width: 40, depth: 40, height: 12 }, revision: 1 };
const user = { ...system, id: "custom", scope: "user", name: "我的柱帽", revision: 7 };
const templateA = { ...system, id: "cap", scope: "template", name: "模板甲柱帽", templateId: "guard-a", templateName: "护栏甲" };
const templateB = { ...templateA, name: "模板乙柱帽", templateId: "guard-b", templateName: "护栏乙" };
const geometry = (id = "cap") => ({ geometryResourceId: `resource://${id}`, geometryResourceVersion: 1, materialResourceId: "material://metal", materialResourceVersion: 2 });
function harness(models = [system, user]) {
  const calls = [], snapshots = [], fits = [], visible = [], notices = [];
  let applied = {}, renderCount = 0;
  const view = { activeAreaId: "components", viewport: {
    async applyViewSnapshot(snapshot) { snapshots.push(snapshot); applied = snapshot; return { applied: true }; },
    getAppliedViewState() { return applied; }, setVisibleEntityIds(ids) { visible.push(ids); },
    fitViewForRevision(...args) { fits.push(args); }, fitViewToViewport(...args) { fits.push(args); },
    setStandardView() {}, setSelectedObjectIds() {}, setDimensionAnnotations() {},
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

await test("three source tabs replace all and template resources group by owner before category", async () => {
  const h = harness([system, templateA, templateB, user]);
  const systemHtml = renderComponentLibraryLeftPane({}, h.view);
  assert.equal((systemHtml.match(/data-component-scope=/g) ?? []).length, 3);
  assert.match(systemHtml, /data-component-scope="system"/);
  assert.match(systemHtml, /data-component-scope="template"/);
  assert.match(systemHtml, /data-component-scope="user"/);
  assert.doesNotMatch(systemHtml, /data-component-scope="all"|模板甲柱帽|模板乙柱帽|我的柱帽/);
  await h.act("scope", { dataset: { componentScope: "template" } });
  const html = renderComponentLibraryLeftPane({}, h.view);
  assert.match(html, /data-component-template-id="guard-a"/);
  assert.match(html, /data-component-template-id="guard-b"/);
  assert.equal((html.match(/tube-component-library-template"/g) ?? []).length, 2);
  assert.match(html, /护栏甲/);
  assert.match(html, /仅所属模板可用/);
  assert.doesNotMatch(html, /data-component-key="system:cap"|data-component-key="user:custom"/);
  await h.act("search", { value: "护栏乙" });
  assert.deepEqual(getVisibleComponentModels(h.view).map(componentModelKey), [componentModelKey(templateB)]);
  assert.equal(h.state.selectedKey, componentModelKey(templateB));
  await h.act("search", { value: "guard-a" });
  assert.equal(h.state.selectedKey, componentModelKey(templateA));
});

await test("library removes ordinary refresh but retains error recovery and parameter-field refresh", async () => {
  const h = harness();
  const html = renderComponentLibraryLeftPane({}, h.view);
  assert.doesNotMatch(html, /data-cam-action="tube-designer-component-refresh"/);
  assert.doesNotMatch(html, /data-cam-action="tube-designer-component-import"/);
  assert.equal(await handleComponentLibraryRibbonCommand(h.context, h.view, "components.refresh", h.ops), false);
  assert.equal(h.calls.length, 0);
  h.state.loadState = "error"; h.state.error = "读取失败";
  assert.match(renderComponentLibraryLeftPane({}, h.view), /data-cam-action="tube-designer-component-refresh">重新读取/);
  const field = renderComponentModelField({ key: "cap", displayName: "柱帽" }, "system:cap", false, { view: h.view });
  assert.match(field, /刷新可选配件/);
});

await test("selection is remembered per source and cannot target a hidden resource", async () => {
  const second = { ...system, id: "second", name: "第二系统配件" };
  const h = harness([system, second, templateA, user]);
  await h.act("select", { dataset: { componentKey: "system:second" } });
  await h.act("scope", { dataset: { componentScope: "user" } });
  assert.equal(h.state.selectedKey, "user:custom");
  await h.act("select", { dataset: { componentKey: "system:cap" } });
  assert.equal(h.state.selectedKey, "user:custom", "Hidden source selection is ignored");
  await h.act("scope", { dataset: { componentScope: "system" } });
  assert.equal(h.state.selectedKey, "system:second");
  await h.act("search", { value: "does-not-exist" });
  assert.equal(h.state.selectedKey, "");
  assert.match(renderComponentLibraryRightPane({}, h.view), /从左侧选择配件/);
  assert.doesNotMatch(renderComponentLibraryRightPane({}, h.view), /第二系统配件/);
  await h.act("search", { value: "" });
  assert.equal(h.state.selectedKey, "system:second", "An empty result does not erase remembered selection");
  await h.act("scope", { dataset: { componentScope: "template" } });
  assert.equal(h.state.selectedKey, componentModelKey(templateA));
  await h.act("scope", { dataset: { componentScope: "user" } });
  assert.equal(h.state.selectedKey, "user:custom");
});

await test("same resource keys in different templates remain independent and neither can be edited", async () => {
  const h = harness([system, templateA, templateB, { ...templateA, templateId: "" }, user]);
  assert.equal(getComponentModels(h.view).length, 4, "A template owner is required");
  assert.notEqual(componentModelKey(templateA), componentModelKey(templateB));
  assert.notEqual(componentModelKey({ ...templateA, templateId: "a:b", id: "c" }), componentModelKey({ ...templateA, templateId: "a", id: "b:c" }));
  await h.act("scope", { dataset: { componentScope: "template" } });
  for (const model of [templateA, templateB]) {
    const key = componentModelKey(model);
    await h.act("select", { dataset: { componentKey: key } });
    const html = renderComponentLibraryRightPane({}, h.view);
    assert.match(html, /模板自带配件/);
    assert.match(html, /只读资源/);
    assert.match(html, new RegExp(model.templateName));
    assert.doesNotMatch(html, /data-cam-action="tube-designer-component-(save|delete)"/);
    await h.act("draft", { dataset: { componentField: "name" }, value: "不允许修改" });
    await h.act("save", { dataset: { componentKey: key } });
    await h.act("delete", { dataset: { componentKey: key } });
    assert.equal(h.state.drafts[key], undefined);
    assert.equal(h.state.deleteDialog, undefined);
  }
  assert.equal(h.calls.length, 0);
});

await test("parameter choices expose only the current template's owned resources and never alias other owners as library", () => {
  const onlyB = { ...templateB, id: "only-b", name: "乙专属模型" };
  const h = harness([system, templateA, templateB, onlyB, user]);
  const descriptor = { id: "guard-a", extensions: { modelResources: { cap: { name: "甲柱帽" }, local: { name: "甲本地模型" } } } };
  const options = getComponentModelOptions(descriptor, h.view);
  assert.equal(options.filter((option) => option.value === "template:cap").length, 1);
  assert.ok(options.some((option) => option.value === "template:local"));
  assert.ok(options.some((option) => option.value === "system:cap"));
  assert.ok(options.some((option) => option.value === "library:custom"));
  assert.ok(options.every((option) => option.value !== "template:only-b" && option.value !== "library:cap" && option.value !== "library:only-b"));
  assert.ok(options.every((option) => !option.label.includes("乙")));
  assert.ok(getComponentModelOptions({ id: "unrelated" }, h.view).every((option) => !option.value.startsWith("template:")));
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
  state.scope = "user";
  assert.match(renderComponentLibraryRightPane({}, view), /data-cam-action="tube-designer-component-save"/);
});

await test("concurrent list reads are deduplicated and retry works after missing bridge", async () => {
  const h = harness([]), request = deferred();
  h.state.scope = "user";
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
  assert.equal(h.state.scope, "user");
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

await test("CSG drawing builds an editable boolean tree, saves it to My Components, and reopens it", async () => {
  const h = harness();
  const originalModelCount = h.state.models.length;
  assert.equal(await handleComponentLibraryRibbonCommand(h.context, h.view, "components.draw", h.ops), true);
  assert.equal(renderComponentLibraryDialogs(h.view), "");
  let html = renderComponentLibraryLeftPane({}, h.view)
    + renderComponentLibraryRightPane({}, h.view)
    + renderComponentLibraryViewportOverlay({}, h.view);
  assert.match(html, /CSG 结构树/);
  assert.match(html, /完成前不会写入“我的配件”/);
  assert.match(html, /拉伸体/);
  assert.match(html, /程式截面 · 参数可修改/);
  assert.match(html, /定式截面 · DXF \/ 二维草图/);
  assert.match(html, /导入 DXF/);
  assert.match(html, /转到二维草图/);
  assert.match(html, /长方体/);
  assert.match(html, /圆柱体/);
  assert.match(html, /球体/);
  assert.match(html, /圆锥体/);
  assert.match(html, /完成并保存零件/);
  assert.match(html, /data-component-csg-workspace/);
  assert.match(html, /data-csg-tool="move"/);
  assert.match(html, /当前零件实时结果/);
  assert.doesNotMatch(html, /tube-component-csg-dialog|data-component-csg-viewport/);
  await h.act("csg-profile-parameter-change", { dataset: { csgProfileParameter: "width" }, value: "48" });
  assert.equal(h.state.csgDraft.features[0].profile.parameters.width, 48);
  assert.equal(h.state.csgDraft.features[0].profile.snapshot.width, 48);
  assert.equal(h.calls.length, 0, "Built-in programmatic profiles recalculate locally without saving");
  await h.act("csg-add", { dataset: { csgPrimitive: "cylinder" } });
  const cylinderId = h.state.csgDraft.selectedId;
  html = renderComponentLibraryLeftPane({}, h.view);
  assert.match(html, /role="tree"/);
  assert.match(html, /operation-node operation-union/);
  assert.match(html, /布尔运算 · 生成实体/);
  await h.act("csg-select", { dataset: { csgFeatureId: cylinderId, csgNodeKind: "operation" } });
  html = renderComponentLibraryRightPane({}, h.view);
  assert.match(html, /data-csg-operation="union"/);
  assert.match(html, /data-csg-operation="difference"/);
  assert.match(html, /data-csg-operation="intersection"/);
  await h.act("csg-operation", { dataset: { csgOperation: "difference" } });
  assert.equal(h.calls.length, 0);
  assert.equal(h.state.models.length, originalModelCount);
  await h.act("csg-select", { dataset: { csgFeatureId: cylinderId, csgNodeKind: "primitive" } });
  await h.act("csg-tool", { dataset: { csgTool: "move" } });
  assert.equal(h.state.csgDraft.tool, "move");
  await h.act("csg-feature-change", { dataset: { csgFeatureField: "parameters.radius" }, value: "8" });
  await h.act("csg-feature-change", { dataset: { csgFeatureField: "position.x" }, value: "3" });
  await h.act("csg-model-change", { dataset: { csgModelField: "name" }, value: "CSG 柱帽" });
  h.context.productProxy.invoke = async (method, payload) => {
    h.calls.push({ method, payload });
    return { model: { ...user, id: "csg-1", revision: 1, name: payload.name, modelType: "csg", csgDefinition: payload.csgDefinition } };
  };
  await h.act("save-csg");
  assert.equal(h.calls[0].method, "TubeDesigner.CreateComponentCSGModel");
  assert.equal(h.calls[0].payload.csgDefinition.schema, "icax.component-csg");
  assert.equal(h.calls[0].payload.csgDefinition.evaluation, "left-fold");
  assert.equal(h.calls[0].payload.csgDefinition.features[0].primitive, "extrusion");
  assert.equal(h.calls[0].payload.csgDefinition.features[0].profile.sourceType, "parametric");
  assert.equal(h.calls[0].payload.csgDefinition.features[0].profile.parameters.width, 48);
  assert.equal(h.calls[0].payload.csgDefinition.features[1].operation, "difference");
  assert.equal(h.calls[0].payload.csgDefinition.features[1].parameters.radius, 8);
  assert.equal(h.state.scope, "user");
  assert.equal(h.state.selectedKey, "user:csg-1");
  assert.match(renderComponentLibraryRightPane({}, h.view), /可编辑 CSG/);
  assert.match(renderComponentLibraryRightPane({}, h.view), /继续绘制/);
  await h.act("edit-csg", { dataset: { componentKey: "user:csg-1" } });
  html = renderComponentLibraryLeftPane({}, h.view) + renderComponentLibraryRightPane({}, h.view);
  assert.match(html, /编辑已有零件/);
  assert.match(html, /CSG 柱帽/);
  h.calls.length = 0;
  await h.act("csg-model-change", { dataset: { csgModelField: "name" }, value: "CSG 柱帽二版" });
  h.context.productProxy.invoke = async (method, payload) => {
    h.calls.push({ method, payload });
    return { model: { ...user, id: "csg-1", revision: 2, name: payload.name, modelType: "csg", csgDefinition: payload.csgDefinition } };
  };
  await h.act("save-csg");
  assert.equal(h.calls[0].method, "TubeDesigner.UpdateComponentCSGModel");
  assert.equal(h.calls[0].payload.id, "csg-1");
  assert.equal(h.calls[0].payload.expectedRevision, 1);
  assert.equal(h.state.models.find((model) => model.id === "csg-1").name, "CSG 柱帽二版");
  await h.act("edit-csg", { dataset: { componentKey: "user:csg-1" } });
  await h.act("cancel-csg");
  assert.equal(h.state.csgDraft, null);
});

await test("CSG extrusion accepts library program profiles, embedded DXF, and a 2D sketch without intermediate component records", async () => {
  const h = harness();
  h.view.tubeDesignerSystemProfiles = [{
    id: "rect", name: "矩形管", profileType: "parametric-package",
    descriptor: { parameters: [
      { key: "width", displayName: { "zh-CN": "外宽" }, valueType: "number", defaultValue: 40, min: 1, step: 1 },
      { key: "depth", displayName: { "zh-CN": "外高" }, valueType: "number", defaultValue: 20, min: 1, step: 1 },
    ] },
    defaultParameters: { width: 40, depth: 20 },
    previewProfile: { schema: "icax.imported-tube-profile", schemaVersion: 1, kind: "parametric-package",
      name: "矩形管", width: 40, depth: 20, contourCount: 1,
      contours: [{ kind: "polygon", points: [[-20, -10], [20, -10], [20, 10], [-20, 10]] }] },
  }];
  await h.act("draw");
  await h.act("csg-profile-select", { value: "system:rect" });
  const feature = h.state.csgDraft.features[0];
  assert.equal(feature.profile.key, "system:rect");
  assert.equal(feature.profile.sourceType, "parametric");
  assert.match(renderComponentLibraryRightPane({}, h.view), /系统内置 · 矩形管/);
  h.context.productProxy.invoke = async (method, payload) => {
    h.calls.push({ method, payload });
    if (method === "TubeDesigner.EvaluateProfilePackage") return { profile: {
      ...feature.profile.snapshot, width: payload.parameters.width, parameters: payload.parameters,
      contours: [{ kind: "polygon", points: [[-27.5, -10], [27.5, -10], [27.5, 10], [-27.5, 10]] }],
    } };
    throw new Error(`unexpected ${method}`);
  };
  await h.act("csg-profile-parameter-change", { dataset: { csgProfileParameter: "width" }, value: "55" });
  assert.equal(h.calls[0].method, "TubeDesigner.EvaluateProfilePackage");
  assert.deepEqual(h.calls[0].payload.profileRef, { scope: "system", id: "rect" });
  assert.equal(feature.profile.parameters.width, 55);
  assert.equal(feature.profile.snapshot.width, 55);
  assert.equal(h.calls.some((call) => /CreateComponentCSGModel|UpdateComponentCSGModel/.test(call.method)), false);

  h.calls.length = 0;
  h.context.appProxy = { bridge: { async openFileDialog(options) {
    assert.deepEqual(options.filters[0].extensions, ["dxf"]);
    return "D:\\Profiles\\bracket.dxf";
  } } };
  h.context.productProxy.invoke = async (method, payload) => {
    h.calls.push({ method, payload });
    return { profile: { schema: "icax.imported-tube-profile", schemaVersion: 1, kind: "imported-dxf",
      name: "支架轮廓", width: 30, depth: 12, contourCount: 1,
      contours: [{ kind: "polygon", points: [[-15, -6], [15, -6], [15, 6], [-15, 6]] }] } };
  };
  await h.act("csg-import-profile-dxf");
  assert.equal(h.calls[0].method, "TubeDesigner.ImportProfileDxf");
  assert.equal(h.calls.some((call) => call.method === "TubeDesigner.SaveImportedProfile"), false);
  assert.equal(feature.profile.sourceType, "fixed");
  assert.equal(feature.profile.snapshot.name, "支架轮廓");
  assert.match(renderComponentLibraryRightPane({}, h.view), /编辑二维轮廓/);

  h.calls.length = 0;
  h.context.actions = { async selectRibbonTab(id) { h.context.activeRibbonTabId = id; } };
  await h.act("csg-new-profile-sketch");
  assert.equal(h.view.tubeDesignerSketchDialogOpen, true);
  assert.equal(h.view.tubeDesignerComponentCSGProfileReturn.featureId, feature.id);
  assert.ok(h.view.tubeDesignerSketch.section.entities.length > 0, "The current fixed contour opens for secondary editing");
  h.view.tubeDesignerSketch.sectionName = "手绘支架截面";
  h.view.tubeDesignerSketch.section.entities = [
    { id: "outline", kind: "rectangle", x: -10, y: -5, width: 20, height: 10, radius: 0, closed: true },
  ];
  h.view.tubeDesignerSketch.section.dirty = true;
  await handleDesignerAreaAction(h.context, h.view, "tube-designer-sketch-commit", {}, h.ops);
  assert.equal(h.calls.length, 0, "Sketch return embeds the section and does not save an intermediate library record");
  assert.equal(h.view.tubeDesignerSketchDialogOpen, false);
  assert.equal(h.view.activeAreaId, "components");
  assert.equal(feature.profile.sourceType, "fixed");
  assert.equal(feature.profile.name, "手绘支架截面");
  assert.equal(feature.profile.snapshot.sourceFormat, "icax.tube-sketch");
});

await test("CSG editing uses the component page viewport and only creates runtime preview resources", async () => {
  const h = harness();
  await h.act("draw");
  h.context.sceneProxy.invoke = async (method, payload) => {
    h.calls.push({ method, payload });
    return { ...geometry("csg-live"), valid: true, bounds: { width: 40, depth: 40, height: 20 },
      operandGeometryResourceId: "resource://csg-operand", operandGeometryResourceVersion: 1 };
  };
  const listeners = new Map();
  const surface = { addEventListener(name, listener) { listeners.set(name, listener); },
    classList: { add() {}, remove() {} } };
  const mount = { querySelector(selector) { return selector === "[data-component-csg-manipulation]" ? surface : null; } };
  h.context.mount = mount;
  const controller = ensureComponentCSGPreview(h.context, h.view, mount);
  assert.equal(controller.viewport, h.view.viewport);
  assert.equal(controller.ownsViewport, false);
  await new Promise(resolve => setTimeout(resolve, 150));
  assert.equal(h.calls[0].method, "TubeDesigner.GenerateComponentCSGPreview");
  assert.equal(h.snapshots.at(-1).rows.length, 2);
  assert.deepEqual(h.visible.at(-1), ["component-csg-live-result", "component-csg-live-operand"]);
  assert.equal(h.calls.some((call) => /CreateComponentCSGModel|UpdateComponentCSGModel/.test(call.method)), false);
  assert.equal(listeners.has("pointerdown"), true);
  await h.act("cancel-csg");
  assert.equal(ensureComponentCSGPreview(h.context, h.view, mount), null);
});

await test("metadata drafts preserve focus and their original expected revision across refresh", async () => {
  const h = harness();
  h.state.selectedKey = "user:custom";
  h.state.scope = "user";
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
  h.state.scope = "user";
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
  assert.equal(h.state.selectedKey, "", "Deleting the last user model must not select a hidden system model");
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

await test("STEP export supports all three sources, snapshots identity, and never depends on source paths or scaling", async () => {
  for (const model of [system, templateA, user]) {
    const h = harness([system, templateA, user]);
    await h.act("scope", { dataset: { componentScope: model.scope } });
    await h.act("select", { dataset: { componentKey: componentModelKey(model) } });
    let directoryOptions;
    h.context.appProxy = { bridge: { async openDirectoryDialog(options) {
      directoryOptions = options;
      assert.equal(h.view.pending, true, "Directory selection is part of the guarded operation");
      // Simulate an external refresh during the native directory picker. The
      // operation must not silently switch to the newly-selected model.
      h.state.scope = "system"; h.state.selectedKey = "system:other";
      h.state.models = [{ ...system, id: "other" }];
      return "D:\\Exports";
    } } };
    h.context.sceneProxy.invoke = async (method, payload) => {
      h.calls.push({ method, payload });
      assert.equal(h.view.pending, true);
      return { path: "D:\\Exports\\配件.step", format: "step", ...payload };
    };
    const handled = await handleComponentLibraryRibbonCommand(h.context, h.view, "components.export-step", h.ops);
    assert.equal(handled, true);
    assert.equal(directoryOptions.title, "选择配件 STEP 导出目录");
    assert.equal(directoryOptions.initialDirectory, "");
    assert.deepEqual(h.calls, [{ method: "TubeDesigner.ExportComponentModel", payload: {
      scope: model.scope, id: model.id, ...(model.scope === "template" ? { templateId: model.templateId } : {}), targetDirectory: "D:\\Exports",
    } }]);
    assert.equal(h.view.tubeDesignerComponentExportDirectory, "D:\\Exports");
    assert.equal(h.view.pending, false);
    assert.equal(h.view.tubeDesignerOperation, null);
    assert.match(h.notices[0], /配件 STEP 已导出/);
    assert.match(h.notices[0], /配件\.step/);
  }
});

await test("STEP export remembers the last directory and directory cancellation writes no file", async () => {
  const h = harness();
  renderComponentLibraryLeftPane({}, h.view);
  h.view.tubeDesignerComponentExportDirectory = "D:\\Previous";
  const paths = ["  ", "D:\\Next", null];
  const initials = [];
  h.context.productProxy.bridge = { async openDirectoryDialog(options) { initials.push(options.initialDirectory); return paths.shift(); } };
  h.context.sceneProxy.invoke = async (method, payload) => { h.calls.push({ method, payload }); return { path: "D:\\Next\\cap.step", format: "step" }; };
  await handleComponentLibraryRibbonCommand(h.context, h.view, "components.export-step", h.ops);
  assert.equal(h.calls.length, 0);
  assert.equal(h.notices.length, 0);
  assert.equal(h.view.tubeDesignerComponentExportDirectory, "D:\\Previous");
  assert.equal(h.view.pending, false);
  await handleComponentLibraryRibbonCommand(h.context, h.view, "components.export-step", h.ops);
  assert.equal(h.calls.length, 1);
  await handleComponentLibraryRibbonCommand(h.context, h.view, "components.export-step", h.ops);
  assert.deepEqual(initials, ["D:\\Previous", "D:\\Previous", "D:\\Next"]);
  assert.equal(h.calls.length, 1);
});

await test("empty, hidden, and unselected models never open an export directory picker", async () => {
  for (const setup of [
    (h) => { h.state.models = []; },
    (h) => { h.state.selectedKey = ""; },
    (h) => { h.state.selectedKey = "system:cap"; h.state.scope = "user"; },
    (h) => { h.state.selectedKey = "system:cap"; h.state.search = "no matching part"; },
  ]) {
    const h = harness(); let pickers = 0;
    setup(h);
    h.context.appProxy = { bridge: { async openDirectoryDialog() { pickers++; return "D:\\Exports"; } } };
    assert.equal(await handleComponentLibraryRibbonCommand(h.context, h.view, "components.export-step", h.ops), true);
    assert.equal(pickers, 0);
    assert.equal(h.calls.length, 0);
    assert.match(h.view.error, /请先选择当前列表/);
  }
});

await test("pending guards duplicate exports through both directory selection and backend writing", async () => {
  const h = harness(), directory = deferred(), writing = deferred(); let pickers = 0;
  renderComponentLibraryLeftPane({}, h.view);
  h.context.appProxy = { bridge: { async openDirectoryDialog() { pickers++; return directory.promise; } } };
  h.context.sceneProxy.invoke = async (method, payload) => { h.calls.push({ method, payload }); return writing.promise; };
  const first = handleComponentLibraryRibbonCommand(h.context, h.view, "components.export-step", h.ops);
  assert.equal(h.view.pending, true);
  await handleComponentLibraryRibbonCommand(h.context, h.view, "components.export-step", h.ops);
  assert.equal(pickers, 1);
  directory.resolve("D:\\Exports");
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(h.calls.length, 1);
  await handleComponentLibraryRibbonCommand(h.context, h.view, "components.export-step", h.ops);
  assert.equal(h.calls.length, 1);
  writing.resolve({ path: "D:\\Exports\\cap.step", format: "step" });
  await first;
  assert.equal(h.view.pending, false);
  assert.equal(h.notices.length, 1);
});

await test("directory, bridge, backend, and invalid-result failures are visible and always release pending", async () => {
  for (const scenario of ["missing-picker", "picker-error", "missing-scene", "backend-error", "missing-path", "wrong-format"]) {
    const h = harness();
    renderComponentLibraryLeftPane({}, h.view);
    h.context.appProxy = { bridge: { async openDirectoryDialog() {
      if (scenario === "picker-error") throw new Error("目录选择失败");
      return "D:\\Exports";
    } } };
    h.context.sceneProxy.invoke = async () => {
      if (scenario === "backend-error") throw new Error("写入失败");
      return { ...(scenario === "missing-path" ? {} : { path: "D:\\Exports\\part.step" }), format: scenario === "wrong-format" ? "brep" : "step" };
    };
    if (scenario === "missing-picker") delete h.context.appProxy.bridge.openDirectoryDialog;
    if (scenario === "missing-scene") delete h.context.sceneProxy.invoke;
    await assert.rejects(handleComponentLibraryRibbonCommand(h.context, h.view, "components.export-step", h.ops));
    assert.ok(h.view.error, scenario);
    assert.equal(h.state.error, h.view.error);
    assert.equal(h.view.pending, false);
    assert.equal(h.view.tubeDesignerOperation, null);
    assert.equal(h.notices.length, 0);
  }
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
  h.state.scope = "user";
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  h.state.selectedKey = "system:cap";
  h.state.scope = "system";
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

await test("template owner identity travels in preview requests, cache keys, revisions and scene entities", async () => {
  const h = harness([templateA, templateB]);
  await h.act("scope", { dataset: { componentScope: "template" } });
  await h.act("select", { dataset: { componentKey: componentModelKey(templateA) } });
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  await h.act("select", { dataset: { componentKey: componentModelKey(templateB) } });
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  assert.deepEqual(h.calls.map((call) => call.payload), [
    { scope: "template", id: "cap", templateId: "guard-a" },
    { scope: "template", id: "cap", templateId: "guard-b" },
  ]);
  assert.notEqual(h.snapshots[0].revision, h.snapshots[1].revision);
  assert.notEqual(h.snapshots[0].rows[0].entityId, h.snapshots[1].rows[0].entityId);
  assert.equal(h.state.previewCache.size, 2);
  await h.act("select", { dataset: { componentKey: componentModelKey(templateA) } });
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  assert.equal(h.calls.length, 2, "Returning to owner A reuses only A's cached mesh");
  assert.equal(h.snapshots.at(-1).rows[0].entityId, h.snapshots[0].rows[0].entityId);
});

await test("empty source and empty search clear the scene and invalidate in-flight previews", async () => {
  const h = harness([system]);
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  await h.act("scope", { dataset: { componentScope: "template" } });
  assert.equal(h.state.selectedKey, "");
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  assert.deepEqual(h.snapshots.at(-1), { revision: "components-empty", rows: [] });
  assert.deepEqual(h.visible.at(-1), []);
  assert.match(renderComponentLibraryLeftPane({}, h.view), /模板自带”暂无配件/);
  const pending = deferred();
  h.state.previewCache.clear();
  h.context.sceneProxy.invoke = () => pending.promise;
  await h.act("scope", { dataset: { componentScope: "system" } });
  const preview = ensureComponentModelPreview(h.context, h.view, h.ops);
  await h.act("search", { value: "no matching model" });
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  pending.resolve(geometry());
  await preview;
  assert.equal(h.state.previewRequest, null);
  assert.equal(h.state.selectedKey, "");
  assert.deepEqual(h.snapshots.at(-1).rows, []);
  assert.deepEqual(h.visible.at(-1), []);
  await h.act("search", { value: "" });
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  assert.equal(h.state.selectedKey, "system:cap");
  assert.deepEqual(h.visible.at(-1), ["component-model:system:cap"]);
});

await test("empty state clears after any already-running mesh application without a stale model flash", async () => {
  const h = harness([system]), applying = deferred();
  const originalApply = h.view.viewport.applyViewSnapshot;
  h.view.viewport.applyViewSnapshot = async (snapshot) => {
    if (snapshot.rows.length) await applying.promise;
    return originalApply(snapshot);
  };
  const preview = ensureComponentModelPreview(h.context, h.view, h.ops);
  await new Promise((resolve) => setImmediate(resolve));
  await h.act("search", { value: "no matching model" });
  const empty = ensureComponentModelPreview(h.context, h.view, h.ops);
  applying.resolve();
  await Promise.all([preview, empty]);
  assert.deepEqual(h.snapshots.at(-1), { revision: "components-empty", rows: [] });
  assert.deepEqual(h.visible.at(-1), []);
  assert.equal(h.fits.length, 0, "The superseded model must not reposition the camera");
});

await test("out of order preview responses cannot replace latest selection or another area", async () => {
  const h = harness(), first = deferred(), second = deferred();
  h.context.sceneProxy.invoke = (_method, payload) => payload.id === "cap" ? first.promise : second.promise;
  const a = ensureComponentModelPreview(h.context, h.view, h.ops);
  h.state.selectedKey = "user:custom";
  h.state.scope = "user";
  const b = ensureComponentModelPreview(h.context, h.view, h.ops);
  second.resolve(geometry("custom")); await b;
  first.resolve(geometry("cap")); await a;
  assert.equal(h.snapshots.length, 1);
  assert.equal(h.snapshots[0].rows[0].entityId, "component-model:user:custom");
  const third = deferred();
  h.context.sceneProxy.invoke = () => third.promise;
  h.state.selectedKey = "system:cap";
  h.state.scope = "system";
  const c = ensureComponentModelPreview(h.context, h.view, h.ops);
  h.view.activeAreaId = "view";
  third.resolve(geometry("cap")); await c;
  assert.equal(h.snapshots.length, 1);
});

await test("slow preview shows progress through model application and clears on success or failure", async () => {
  for (const fail of [false, true]) {
    const h = harness([system]);
    const loading = deferred();
    const applying = deferred();
    h.context.sceneProxy.invoke = () => loading.promise;
    h.view.viewport.applyViewSnapshot = () => applying.promise;
    const preview = ensureComponentModelPreview(h.context, h.view, h.ops);
    assert.doesNotMatch(renderComponentLibraryViewportOverlay({}, h.view), /role="progressbar"/);
    await new Promise(resolve => setTimeout(resolve, 230));
    assert.match(renderComponentLibraryViewportOverlay({}, h.view), /role="progressbar"/);
    if (fail) loading.reject(new Error("加载失败"));
    else {
      loading.resolve(geometry(system.id));
      await new Promise(resolve => setTimeout(resolve, 0));
      assert.match(renderComponentLibraryViewportOverlay({}, h.view), /装载并显示三维模型/);
      applying.resolve({ applied: true });
    }
    await preview;
    assert.equal(h.state.previewRequest, null);
    assert.doesNotMatch(renderComponentLibraryViewportOverlay({}, h.view), /role="progressbar"/);
    if (fail) assert.match(h.state.previewError, /加载失败/);
  }
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

await test("returning from an empty source does not leave a failed preview silently blocked", async () => {
  const h = harness([system]);
  h.context.sceneProxy.invoke = async () => { throw new Error("temporary"); };
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  assert.match(h.state.previewError, /temporary/);
  await h.act("scope", { dataset: { componentScope: "template" } });
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  await h.act("scope", { dataset: { componentScope: "system" } });
  h.context.sceneProxy.invoke = async () => geometry();
  await ensureComponentModelPreview(h.context, h.view, h.ops);
  assert.equal(h.state.previewError, "");
  assert.deepEqual(h.visible.at(-1), ["component-model:system:cap"]);
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
  assert.deepEqual(tab.groups.flatMap((group) => group.commands.map((item) => item.id)), ["components.draw", "components.import", "components.export-step"]);
  const h = harness();
  await h.act("scope", { dataset: { componentScope: "user" } });
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
