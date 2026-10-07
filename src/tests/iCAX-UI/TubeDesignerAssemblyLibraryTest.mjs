import assert from "node:assert/strict";
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { assemblyPresentationShape, assemblyShapeOrder, normalizeAssemblyCatalogue } from "../../apps/tube-designer/webpage/assemblyCatalog.mjs";
import {
  assemblyBlankRows,
  assemblyFinishedRows,
  assemblySceneRows,
  assemblyLibraryState,
  assemblyParameterValues,
  ensureAssemblyLibraryPreview,
  handleAssemblyLibraryAction,
  renderAssemblyLibraryLeftPane,
  renderAssemblyLibraryRightPane,
  renderAssemblyLibraryViewportOverlay,
} from "../../apps/tube-designer/webpage/assemblyLibrary.mjs";
import { assemblyProcessInput, createFinishedProduct, finishedProductInput, finishedProductKey } from "../../apps/tube-designer/webpage/finishedProductModel.mjs";

const root = fileURLToPath(new URL("../../apps/tube-designer/templates/assembly/", import.meta.url));
const raw = readdirSync(root, { withFileTypes: true })
  .filter((entry) => entry.isDirectory())
  .filter((entry) => existsSync(fileURLToPath(new URL(`../../apps/tube-designer/templates/assembly/${entry.name}/assembly.json`, import.meta.url))))
  .map((entry) => JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/assembly/${entry.name}/assembly.json`, import.meta.url), "utf8")));
const templates = normalizeAssemblyCatalogue(raw);

assert.ok(templates.length >= 10, "装配库应覆盖两件、三件和四件场景");
assert.ok(templates.every((item) => item.schema === "icax.assembly-template" && item.schemaVersion === 1));
assert.ok(templates.every((item) => item.participants.length >= 2 && item.participants.length <= 4), "装配模板必须描述 2～4 个逻辑零件");
assert.ok(templates.every((item) => item.assemblyPath.length > 0), "装配模板必须包含实现步骤");
assert.ok(templates.every((item) => item.outputs.assemblyInstruction), "装配模板必须输出装配指导");
assert.ok(templates.every((item) => item.inputContract.roles.length === item.participants.length), "每个装配工艺必须声明本次调用所需的实际局部角色");
assert.ok(raw.every((item) => !item.previewScene.designParts && !item.productInput), "成品位置与全局造型准入不能保存到装配工艺模板中");
assert.ok(templates.every((item) => item.previewScene.manufacturingParts.length === item.outputs.manufacturingPartCount), "每个装配模板必须覆盖全部下料结果");
assert.ok(templates.every((item) => item.parameterDiagram?.schemaVersion === 2), "每个装配模板必须提供二维参数示意图");
const ids = new Set(templates.map((item) => item.id));
assert.ok(["two-end-end-angle", "mechanical-fastener", "t-contact-fit", "t-profile-insert",
  "cross-through", "insert-sleeve", "weld-interface", "bend", "tab-slot-lock"]
  .every((id) => ids.has(id)), "资源库必须显示现有工艺模板");
assert.equal(ids.has("three-end-end-middle"), false, "两个 T 连接不能重复列为独立的双支管模板");
assert.equal(ids.has("two-end-middle"), false, "隐藏的三端汇交模板不能作为可见卡片出现");
assert.ok(["through-bolt", "slot-bolt-adjustable", "saddle-weld"].every((id) => !ids.has(id)),
  "兼容模板不能继续作为重复卡片出现");

for (const template of templates) {
  for (const process of template.partProcesses) {
    const ids = process.resource ? [process.resource.id]
      : process.resourceSelection.resources.map((resource) => resource.id);
    for (const id of ids) {
      const processPath = fileURLToPath(new URL(`../../apps/tube-designer/templates/mold/${id}/tool.json`, import.meta.url));
      assert.equal(existsSync(processPath), true, `${template.id} 引用的单件工艺 ${id} 必须存在`);
    }
  }
}

const bend = templates.find((item) => item.id === "bend");
assert.ok(templates.every((item) => ["l", "t", "cross", "straight", "orthogonal-corner", "pi", "parallel"].includes(item.layoutShape)),
  "每份可见模板必须声明典型连接形态，不能借用件数或工艺作为形态");
assert.deepEqual(assemblyShapeOrder(templates), [
  ["l", "L形"], ["t", "T形"], ["cross", "十字形"], ["straight", "直线形"],
  ["orthogonal-corner", "三向直角节点"],
  ["parallel", "并行形"],
]);
assert.equal(assemblyPresentationShape({ ...bend, layoutShape: "unknown", category: "integrated" }), "other",
  "未知形态不可由一体成形工艺分类推断");
assert.equal(bend.displayName, "冷折弯");
assert.equal(bend.category, "integrated");
assert.equal(bend.layoutShape, "l");
assert.equal(bend.manufacturingPlan.realization, "integrated");
assert.equal(bend.manufacturingPlan.blankParts.length, 1);
assert.deepEqual(bend.partProcesses, []);
assert.ok(!bend.parameters.some((item) => ["bendMethod", "slotProcess"].includes(item.key)));
const notchedTemplates = new Map([
  ["segmented-bend", "segmented-bend"],
  ["node-v-notch-integrated", "v-notch-sharp"],
  ["node-embedded-arc-integrated", "embedded-arc-notch"],
  ["node-edge-arc-integrated", "edge-arc-groove"],
  ["flexible-slit-bend-integrated", "flexible-slit-bend"],
]);
for (const [templateId, toolId] of notchedTemplates) {
  const template = templates.find((item) => item.id === templateId);
  assert.ok(template, `${templateId} 应为独立装配模板`);
  assert.equal(template.category, bend.category);
  assert.equal(template.partProcesses.length, 1);
  assert.equal(template.partProcesses[0].resource.id, toolId);
  assert.equal(template.partProcesses[0].id, "node-slot");
  assert.ok(!template.parameters.some((item) => ["bendMethod", "slotProcess"].includes(item.key)));
  template.partProcesses[0].resource.descriptor = JSON.parse(readFileSync(
    new URL(`../../apps/tube-designer/templates/mold/${toolId}/tool.json`, import.meta.url), "utf8"));
}
const embeddedArc = notchedTemplates.get("node-embedded-arc-integrated");
assert.equal(embeddedArc, "embedded-arc-notch");
assert.ok(templates.find((item) => item.id === "node-embedded-arc-integrated").partProcesses[0]
  .resource.descriptor.parameters.some((item) => item.key === "arcRadius"));

const tabSlot = templates.find((item) => item.id === "tab-slot-lock");
assert.ok(tabSlot.parameters.some((item) => item.key === "straightDepth"));
assert.ok(!tabSlot.parameters.some((item) => item.key === "tipRadius"));
assert.deepEqual(tabSlot.partProcesses[0].parameterBindings, {
  gender: "male", width: "$nominalWidth", straightDepth: "$straightDepth",
});
assert.deepEqual(tabSlot.partProcesses[1].parameterBindings, {
  gender: "female", width: "$nominalWidth", straightDepth: "$straightDepth",
  sideClearance: "$fitClearance", axialClearance: "$fitClearance",
});

const editorContext = { sceneProxy: { resources: {}, async invoke(method, payload) {
  if (method === "TubeDesigner.CheckAssemblyTemplateApplicability") return {
    schema: "icax.assembly-applicability", schemaVersion: 1, templateId: payload.templateId,
    applicable: true, reason: "",
  };
  if (method === "TubeDesigner.ResolveAssemblyTemplatePreview" && payload.finishedOnly) return {
    schema: "icax.finished-product-preview", schemaVersion: 1,
    finishedProduct: structuredClone(payload.finishedProduct), layoutShape: payload.finishedProduct.shapeId,
    sceneParameters: structuredClone(payload.finishedProduct.parameters),
    designParts: Object.entries(payload.finishedProduct.spans).map(([id, span]) => ({
      id: `design-${id}`, role: id, label: id,
      request: { ...structuredClone(span), features: [], ends: {} },
      matrix: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
    })),
  };
  throw new Error(`unexpected editor ${method}`);
} } };
const grantedLicense = { featureSchemaVersion: 1, capabilities: { "product.design": true } };
const view = { activeAreaId: "assemblies", tubeDesignerLicense: grantedLicense, tubeDesignerAssemblyTemplates: templates,
  tubeDesignerAssemblyLibrary: { selectedId: "bend" } };
const productView = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
  scene: { tubeDesigner: { activeProductId: "product-a", generationRun: { entityId: "run-a" } } } };
const defaultExample = templates.find((item) => item.exampleInput?.shapeId);
assert.equal(assemblyLibraryState(productView).selectedId, defaultExample.id,
  "资源库始终选择独立工艺示例，不依赖当前产品节点");
assert.equal(Object.hasOwn(assemblyLibraryState(productView), "workMode"), false,
  "装配资源库不再有产品节点工作模式");
assert.match(renderAssemblyLibraryLeftPane({}, productView), /data-tube-assembly-id="mechanical-fastener"/);
assert.match(renderAssemblyLibraryRightPane({}, productView), /data-finished-product-editor/);
for (const html of [renderAssemblyLibraryLeftPane({}, productView), renderAssemblyLibraryRightPane({}, productView),
  renderAssemblyLibraryViewportOverlay({}, productView)]) {
  assert.doesNotMatch(html, /当前产品连接|选择连接节点|产品节点|工艺示例|tube-designer-binding-|tube-designer-assembly-work-mode|产品生成模型/);
}
const generatedLater = { tubeDesignerAssemblyTemplates: templates, scene: { tubeDesigner: {} } };
const selectedBeforeGeneration = assemblyLibraryState(generatedLater).selectedId;
generatedLater.scene.tubeDesigner = { activeProductId: "product-later", generationRun: { entityId: "run-later" } };
assert.equal(assemblyLibraryState(generatedLater).selectedId, selectedBeforeGeneration,
  "产品生成后资源库仍保留原工艺示例选择");
assert.equal(renderAssemblyLibraryLeftPane({}, generatedLater), renderAssemblyLibraryLeftPane({}, productView));
for (const action of ["tube-designer-assembly-work-mode", "tube-designer-assembly-select-connection", "tube-designer-assembly-product-part", "tube-designer-binding-preview"]) {
  assert.equal((await handleAssemblyLibraryAction({}, productView, action,
    { dataset: { tubeAssemblyMode: "product", tubeConnectionKey: "joint-ab" } }, { renderProject() {} })).handled, false,
    "已删除的产品节点动作不能继续进入装配资源库");
}
assert.ok(templates.some((item) => item.id === "wrap-a-over-b"), "L 形包接必须保留独立工艺模板");
assert.ok(!templates.some((item) => item.id === "wrap-b-over-a"), "主件和支件由角色映射选择，不能显示两张重复包接卡片");
const wrapTemplate = templates.find((item) => item.id === "wrap-a-over-b");
const wrapExampleView = { tubeDesignerAssemblyTemplates: templates,
  tubeDesignerAssemblyLibrary: { selectedId: wrapTemplate.id } };
const wrapConditionalKeys = ["pairCount", "tabWidth", "tabLength", "sideClearance"];
for (const key of wrapConditionalKeys) {
  const definition = wrapTemplate.parameters.find((parameter) => parameter.key === key);
  assert.equal(definition.level, "basic", `${key} 的普通显示分组必须由模板声明`);
  assert.deepEqual(definition.visibleWhen, { op: "eq", parameter: "maleFemale", value: true });
  assert.doesNotMatch(renderAssemblyLibraryRightPane({}, wrapExampleView),
    new RegExp(`data-tube-assembly-parameter="${key}"`), "关闭公母应隐藏依赖字段");
}
assemblyLibraryState(wrapExampleView).parameterDrafts[wrapTemplate.id] = { maleFemale: true };
const wrapEnabledHtml = renderAssemblyLibraryRightPane({}, wrapExampleView);
for (const key of wrapConditionalKeys)
  assert.match(wrapEnabledHtml, new RegExp(`data-tube-assembly-parameter="${key}"`));
assert.doesNotMatch(wrapEnabledHtml, /<summary><span>更多参数<\/span>/,
  "启用公母后字段应直接显示，无须展开更多参数");
const declarativeWrap = structuredClone(wrapTemplate);
declarativeWrap.id = "declarative-wrap-fixture";
Object.assign(declarativeWrap.parameters.find((parameter) => parameter.key === "tabWidth"), {
  displayName: "模板自定义宽度", defaultValue: 11.5, min: 2, max: 40, step: 0.25,
});
const declarativeView = { tubeDesignerAssemblyTemplates: [declarativeWrap],
  tubeDesignerAssemblyLibrary: { selectedId: declarativeWrap.id,
    parameterDrafts: { [declarativeWrap.id]: { maleFemale: true } } } };
const declarativeHtml = renderAssemblyLibraryRightPane({}, declarativeView);
assert.match(declarativeHtml, /模板自定义宽度/);
assert.match(declarativeHtml,
  /value="11\.5" min="2"\s+max="40"\s+step="0\.25"[^>]*data-tube-assembly-parameter="tabWidth"/,
  "通用编辑器必须读取任意模板的名称、默认值、范围和步长");
assert.doesNotMatch(declarativeHtml, /<summary><span>更多参数<\/span>/);
const state = assemblyLibraryState(view);
assert.match(renderAssemblyLibraryLeftPane({}, view), /冷折弯/);
assert.match(renderAssemblyLibraryLeftPane({}, view), /data-tube-assembly-shape="l"[^>]*><span>▾ L形<\/span>/);
assert.match(renderAssemblyLibraryLeftPane({}, view), /连续母材 · 多处折弯/);
assert.match(renderAssemblyLibraryRightPane({}, view), /折弯因子 K/);
assert.doesNotMatch(renderAssemblyLibraryRightPane({}, view), /槽口参数/);
assert.doesNotMatch(renderAssemblyLibraryRightPane({}, view), /未绑定产品零件|当前示例方案/);
assert.doesNotMatch(renderAssemblyLibraryRightPane({}, view), /工艺明细|等待计算当前工艺结果/,
  "尚未计算时右栏不应出现空的工艺明细卡");
assert.match(renderAssemblyLibraryRightPane({}, view), />重置工艺</);
assert.match(renderAssemblyLibraryRightPane({}, view), />重置成品</);
assert.match(renderAssemblyLibraryRightPane({}, view), /data-finished-product-editor/);
assert.doesNotMatch(renderAssemblyLibraryRightPane({}, view), /data-tube-assembly-parameter="angle"/);
const overlay = renderAssemblyLibraryViewportOverlay({}, view);
assert.match(overlay, /data-tube-assembly-view="finished" aria-pressed="true"/,
  "独立成品默认选择成品示意，不能强制切到下料");
assert.match(overlay, /class="tube-assembly-preview-pane scene"[^>]*aria-label="[^"]+"/,
  "视口简化后仍需保留供辅助技术读取的场景说明");
assert.match(overlay, /<header title="[^"]+">/);
assert.doesNotMatch(overlay, /tube-assembly-preview-caption|tube-assembly-preview-title/,
  "模板名和长说明已在右栏呈现，不应在视口顶端重复显示");
assert.doesNotMatch(overlay, /data-tube-assembly-camera=|tube-designer-assembly-camera/,
  "顶栏不能再出现等轴、前视、顶视、适合的第二套相机按钮");
assert.match(overlay, /data-tube-assembly-view="finished"/);
assert.match(overlay, /data-tube-assembly-view="exploded"/);
assert.match(overlay, /data-tube-assembly-scene-viewport/);
assert.doesNotMatch(overlay, /data-tube-assembly-finished-viewport|data-tube-assembly-blank-viewport/);
assert.doesNotMatch(overlay, /data-tube-assembly-diagram-dock/);
assert.match(renderAssemblyLibraryRightPane({}, view), /data-cam-change-action="tube-designer-assembly-parameter-change"/);
const libraryHtml = renderAssemblyLibraryLeftPane({}, view);
assert.deepEqual([...libraryHtml.matchAll(/data-tube-assembly-shape="([^"]+)"[^>]*><span>[^<]+<\/span><small>(\d+)<\/small>/g)]
  .map((match) => [match[1], Number(match[2])]),
  [...assemblyShapeOrder(templates.filter((template) => !template.inputContract.supportedInputModes?.includes("stock-operation")))
    .map(([shape]) => [shape, templates.filter((template) => !template.inputContract.supportedInputModes?.includes("stock-operation")
      && assemblyPresentationShape(template) === shape).length]),
    ["fold-processing", templates.filter((template) => template.inputContract.supportedInputModes?.includes("stock-operation")).length]],
  "连接示例按局部连接展示；可重复折弯工艺独立归入折弯加工组");
assert.doesNotMatch(libraryHtml, /tube-designer-finished-shape-change|<select/, "场景树不能重复提供造型下拉框");
assert.match(libraryHtml, /共线形/);
assert.doesNotMatch(libraryHtml, /class="tube-connection-library-group-heading"[^>]*><span>[^<]*(?:一体成形|两件 ·|三件 ·|四件 ·)/);
assert.match(libraryHtml, /同轴套接 \/ 伸缩/);
assert.match(libraryHtml, /孔系紧固|搭接焊 \/ 塞焊 \/ 槽焊/,
  "独立并行成品应可选择其紧固和焊接工艺");
assert.doesNotMatch(libraryHtml, /三端汇交/);
assert.match(libraryHtml, /贯穿连接/);
assert.doesNotMatch(libraryHtml, /四端汇交/);
assert.doesNotMatch(libraryHtml, /对穿螺栓装配|长孔调节装配|鞍口相贯焊接/);
await handleAssemblyLibraryAction(editorContext, view, "tube-designer-assembly-toggle-shape",
  { dataset: { tubeAssemblyShape: "l" } }, { renderProject() {} });
assert.ok(assemblyLibraryState(view).collapsed.includes("l"));
assert.match(renderAssemblyLibraryLeftPane({}, view), /data-tube-assembly-shape="l"[^>]*aria-expanded="false"[^>]*>.*?<div class="tube-connection-library-cards" hidden>/s);
assert.equal(assemblyLibraryState(view).selectedId, "bend", "折叠形态组不能更改当前工艺");
await handleAssemblyLibraryAction(editorContext, view, "tube-designer-assembly-toggle-shape",
  { dataset: { tubeAssemblyShape: "l" } }, { renderProject() {} });
assert.ok(!assemblyLibraryState(view).collapsed.includes("l"));
const shapeSearchView = { tubeDesignerAssemblyTemplates: templates,
  tubeDesignerAssemblyLibrary: { selectedId: "bend", search: "T形" } };
const shapeSearchHtml = renderAssemblyLibraryLeftPane({}, shapeSearchView);
assert.match(shapeSearchHtml, /data-tube-assembly-shape="t"/);
assert.doesNotMatch(shapeSearchHtml, /data-tube-assembly-shape="l"/);

let renders = 0;
const ops = { renderProject() { renders += 1; } };
await handleAssemblyLibraryAction(editorContext, view, "tube-designer-assembly-select",
  { dataset: { tubeAssemblyId: "segmented-bend" } }, ops);
assert.equal(assemblyLibraryState(view).selectedId, "segmented-bend");
assert.match(renderAssemblyLibraryRightPane({}, view), /分段折弯槽口/);
assert.doesNotMatch(renderAssemblyLibraryRightPane({}, view), /data-tube-assembly-parameter="bendMethod"|data-tube-assembly-parameter="slotProcess"/);
assert.ok(renders >= 1, "异步模板检查完成后应刷新编辑器");

await handleAssemblyLibraryAction(editorContext, view, "tube-designer-assembly-select",
  { dataset: { tubeAssemblyId: "node-embedded-arc-integrated" } }, ops);
const embeddedHtml = renderAssemblyLibraryRightPane({}, view);
assert.match(embeddedHtml, /嵌入圆弧半径 R/);
assert.match(embeddedHtml, /data-tube-part-process-parameter="arcRadius"/);
assert.doesNotMatch(embeddedHtml, /data-tube-part-process-parameter="bendRadius"/);

await handleAssemblyLibraryAction(editorContext, view, "tube-designer-assembly-select",
  { dataset: { tubeAssemblyId: "node-edge-arc-integrated" } }, ops);
const edgeArcHtml = renderAssemblyLibraryRightPane({}, view);
assert.doesNotMatch(edgeArcHtml, /加工设置/);
assert.match(edgeArcHtml, /更多参数/);
assert.match(edgeArcHtml, /底部保留厚度/);
assert.match(edgeArcHtml, /data-tube-part-process-parameter="bridge"/);
assert.match(edgeArcHtml, /data-tube-part-process-parameter="leftArc"/);
assert.doesNotMatch(edgeArcHtml, /data-tube-part-process-parameter="angle"/);
assert.doesNotMatch(edgeArcHtml, /tube-connection-library-process-advanced/);

await handleAssemblyLibraryAction(editorContext, view, "tube-designer-assembly-select",
  { dataset: { tubeAssemblyId: "node-v-notch-integrated" } }, ops);
await handleAssemblyLibraryAction(editorContext, view, "tube-designer-assembly-process-parameter-change", {
  checked: true,
  dataset: { tubeAssemblyId: "node-v-notch-integrated", tubeAssemblyProcess: "node-slot", tubePartProcess: "v-notch-sharp", tubePartProcessParameter: "maleFemale" },
}, ops);
assert.equal(assemblyLibraryState(view).processDrafts["node-v-notch-integrated"]["node-slot"]["v-notch-sharp"].maleFemaleSize, 2,
  "assembly reuse must fill公母尺寸 from the source管型 wall thickness");
await handleAssemblyLibraryAction(editorContext, view, "tube-designer-assembly-process-parameter-change", {
  value: "relief",
  dataset: { tubeAssemblyId: "node-v-notch-integrated", tubeAssemblyProcess: "node-slot", tubePartProcess: "v-notch-sharp", tubePartProcessParameter: "bottomStrategy" },
}, ops);
const vHtml = renderAssemblyLibraryRightPane({}, view);
assert.match(vHtml, /V 槽/);
assert.match(vHtml, /释放孔形状/);
assert.match(vHtml, /包围圆/);

await handleAssemblyLibraryAction(editorContext, view, "tube-designer-assembly-select",
  { dataset: { tubeAssemblyId: "flexible-slit-bend-integrated" } }, ops);
assert.match(renderAssemblyLibraryRightPane({}, view), /柔性折弯缝/);
await handleAssemblyLibraryAction(editorContext, view, "tube-designer-assembly-select",
  { dataset: { tubeAssemblyId: "bend" } }, ops);

const tabSlotView = { tubeDesignerAssemblyTemplates: templates, tubeDesignerAssemblyLibrary: { selectedId: "tab-slot-lock" } };
const tabSlotHtml = renderAssemblyLibraryRightPane({}, tabSlotView);
assert.doesNotMatch(tabSlotHtml, /插舌端部成形|插槽端部成形/,
  "固定单件工序名称不应重复占用参数面板");
assert.doesNotMatch(tabSlotHtml, /零件对照|tube-connection-library-part/);
assert.doesNotMatch(tabSlotHtml, /加工设置|tube-connection-library-process-section/,
  "固定单件工序无可编辑字段时不应显示空卡片");

await handleAssemblyLibraryAction(editorContext, view, "tube-designer-assembly-set-view", {
  dataset: { tubeAssemblyView: "exploded" },
}, ops);
assert.equal(assemblyLibraryState(view).exploded, true, "独立成品示例允许在点击下料时请求下料结果");
assert.match(renderAssemblyLibraryViewportOverlay({}, view), /data-tube-assembly-view="exploded" aria-pressed="true"/);
await handleAssemblyLibraryAction(editorContext, view, "tube-designer-assembly-set-view", {
  dataset: { tubeAssemblyView: "finished" },
}, ops);
assert.equal(assemblyLibraryState(view).exploded, false);

await handleAssemblyLibraryAction(editorContext, view, "tube-designer-assembly-toggle-diagram", {}, ops);
assert.match(renderAssemblyLibraryViewportOverlay({}, view), /data-tube-assembly-diagram-dock/);
assert.match(renderAssemblyLibraryViewportOverlay({}, view), /成品外形与尺寸/);
assert.match(renderAssemblyLibraryViewportOverlay({}, view), /data-tube-assembly-diagram-mode="process"/);
assert.doesNotMatch(renderAssemblyLibraryViewportOverlay({}, view), /尺寸随参数实时更新/);

const previewView = {
  activeAreaId: "assemblies",
  tubeDesignerLicense: grantedLicense,
  tubeDesignerAssemblyTemplates: templates,
  tubeDesignerAssemblyLibrary: { selectedId: "bend", showDiagram: false },
};
const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const explodedMatrix = [1, 0, 0, 42, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const invocations = [];
const productDesignParts = (product, roles = Object.fromEntries(Object.keys(product.spans).map((id) => [id, id]))) =>
  Object.entries(roles).map(([role, spanId]) => ({ id: `design-${role}`, role, label: spanId,
    request: { ...structuredClone(product.spans[spanId]), features: [], ends: {} },
    matrix: identity, compareMatrix: identity }));
const processDesignParts = (input) => Object.entries(input.parts).map(([role, part]) => ({
  id: `design-${role}`, role, label: role, matrix: structuredClone(part.matrix), compareMatrix: identity,
  request: { profileRef: structuredClone(part.profileRef), parameters: structuredClone(part.parameters),
    length: part.length, features: [], ends: {} },
}));
const previewContext = { sceneProxy: { resources: {}, async invoke(method, payload) {
  invocations.push({ method, payload });
  if (method === "TubeDesigner.CheckAssemblyTemplateApplicability") return {
    schema: "icax.assembly-applicability", schemaVersion: 1, templateId: payload.templateId,
    applicable: payload.processInput?.schema === "icax.assembly-process-input",
    reason: "",
  };
  if (method === "TubeDesigner.GetAssemblyTemplateExampleProduct") return {
    schema: "icax.assembly-example-product", schemaVersion: 1, templateId: payload.templateId,
    finishedProduct: createFinishedProduct(templates.find((item) => item.id === payload.templateId).exampleInput.shapeId),
  };
  if (method === "TubeDesigner.ResolveAssemblyTemplatePreview" && payload.finishedOnly) return {
    schema: "icax.finished-product-preview", schemaVersion: 1,
    finishedProduct: structuredClone(payload.finishedProduct), layoutShape: payload.finishedProduct.shapeId,
    sceneParameters: structuredClone(payload.finishedProduct.parameters),
    designParts: productDesignParts(payload.finishedProduct),
  };
  if (method === "TubeDesigner.ResolveAssemblyTemplatePreview") return {
    schema: "icax.assembly-preview-plan", templateId: "bend",
    processInput: structuredClone(payload.processInput), parameters: structuredClone(payload.parameters),
    sceneParameters: structuredClone(payload.processInput.geometry),
    resolvedWorkflow: {
      schema: "icax.assembly-workflow", schemaVersion: 1, scope: "template-example", realization: "integrated",
      blankParts: [{ id: "bentBlank", label: "连续折弯下料件", participantRoles: ["segmentA", "segmentB"], sourceRole: "segmentA" }],
      partOperations: [],
      assemblySteps: [{ id: "bend", label: "绕折弯轴形成目标角度", kind: "折弯", direction: "折弯轴", distance: 90, unit: "°" }],
      bom: [], bomStatus: "none-for-configuration", validationStatus: "dimension-checked-example",
      checks: [{ id: "bend-radius", status: "pass", detail: "折弯半径满足示例要求" }],
      parameterEffects: { bendRadius: { active: true, previewGeometry: false, manufacturingGeometry: false, workflow: true } },
    },
    designParts: processDesignParts(payload.processInput),
    manufacturingParts: [{ id: "manufacturing-blank", sourceRole: "segmentA", participantRoles: ["segmentA", "segmentB"], label: "blank", request: { length: 200, features: [], ends: {} }, matrix: identity, explodedMatrix, compareMatrix: identity }],
  };
  if (method === "TubeDesigner.PreviewAssemblyManufacturingPart") {
    const index = invocations.filter((item) => item.method.endsWith("PreviewAssemblyManufacturingPart")).length;
    return { previewComputed: true, resultValid: true, solidCount: 1, geometry: { url: `memory://result-${index}`, version: 1 } };
  }
  throw new Error(`unexpected ${method}`);
} } };
previewView.tubeDesignerAssemblyLibraryRenderProject = () => renderAssemblyLibraryViewportOverlay(previewContext, previewView);
const originalProductInput = structuredClone(finishedProductInput(previewView));
const completePreview = async (context, previewView) => {
  for (let tick = 0; tick < 20; tick += 1) {
    const pending = ensureAssemblyLibraryPreview(context, previewView);
    const selection = assemblyLibraryState(previewView).selectionRequest;
    if (!pending?.promise && !selection?.promise) return pending;
    await Promise.all([pending?.promise, selection?.promise]);
  }
  assert.fail("选择模板与预览生成应在有限步骤内完成");
};
const request = ensureAssemblyLibraryPreview(previewContext, previewView);
await request.promise;
await completePreview(previewContext, previewView);
assert.equal(previewView.tubeDesignerAssemblyLibrary.previewError, "", "独立成品预览应成功完成");
assert.equal(previewView.tubeDesignerAssemblyLibrary.preview.processReady, false);
assert.equal(invocations.filter((item) => item.method === "TubeDesigner.ResolveAssemblyTemplatePreview").length, 1,
  "选择工艺时只请求原始成品，不提前计算工艺方案");
assert.equal(invocations.filter((item) => item.method === "TubeDesigner.PreviewAssemblyManufacturingPart").length, 0,
  "L 形成品示意无需计算下料件");
await handleAssemblyLibraryAction({}, previewView, "tube-designer-assembly-set-view", {
  dataset: { tubeAssemblyView: "exploded" },
}, { renderProject() {} });
await completePreview(previewContext, previewView);
assert.equal(previewView.tubeDesignerAssemblyLibrary.preview.processReady, true,
  "点击下料视图后才计算工艺与下料件");
const reviewHtml = renderAssemblyLibraryRightPane({}, previewView);
assert.doesNotMatch(reviewHtml, /工艺明细|连续折弯下料件|绕折弯轴形成目标角度/,
  "已计算的示例也不应重复展开工序与下料清单");
assert.doesNotMatch(reviewHtml, /仅影响工序与辅料|仅影响布局示例|仅说明，不改变下料或几何/,
  "参数旁不再显示冗余工艺说明");
assert.doesNotMatch(reviewHtml, /data-cam-action="tube-designer-binding-apply"/);
previewView.tubeDesignerAssemblyLibrary.preview.plan.resolvedWorkflow.bom = [
  { id: "bolt", label: "螺栓", quantity: 2, unit: "个", specificationStatus: "requires-size-selection" },
  { id: "washer", label: "垫片", quantity: 2, unit: "个", specificationStatus: "requires-standard" },
];
assert.doesNotMatch(renderAssemblyLibraryRightPane({}, previewView), /规格待确定/,
  "辅料细目只属于内部工艺结果，不应作为右栏常驻清单");
previewView.tubeDesignerAssemblyLibrary.preview.plan.resolvedWorkflow.validationStatus = "requires-definition";
previewView.tubeDesignerAssemblyLibrary.preview.plan.resolvedWorkflow.checks.push({
  id: "cut", status: "requires-definition", blocking: true, detail: "缺少必要的端部加工定义",
});
assert.doesNotMatch(renderAssemblyLibraryRightPane({}, previewView), /工艺明细/);
assert.match(renderAssemblyLibraryRightPane({}, previewView), /缺少必要的端部加工定义/);
assert.match(renderAssemblyLibraryRightPane({}, previewView), /role="alert">缺少必要的端部加工定义/);
const planInvocations = invocations.filter((item) => item.method === "TubeDesigner.ResolveAssemblyTemplatePreview");
assert.equal(planInvocations.length, 2);
assert.equal(planInvocations[0].payload.finishedOnly, true);
assert.deepEqual(planInvocations[0].payload.finishedProduct, originalProductInput);
assert.ok(!planInvocations[0].payload.templateId && !planInvocations[0].payload.parameters,
  "原始成品预览不能携带装配工艺或工艺参数");
assert.deepEqual(planInvocations[1].payload.processInput,
  assemblyProcessInput(bend, originalProductInput, { designParts: productDesignParts(originalProductInput) }),
  "工艺预览应接收从相同原始成品提取的实际局部数据");
assert.ok(!Object.hasOwn(planInvocations[1].payload, "finishedProduct"), "工艺接口不能收到全局成品输入");
assert.equal(planInvocations[1].payload.templateId, "bend");
assert.equal(invocations.filter((item) => item.method === "TubeDesigner.PreviewAssemblyManufacturingPart").length, 1,
  "独立 L 形成品外形直接复用前端网格，原生预览只计算下料件");
assert.equal(assemblyBlankRows(previewView.tubeDesignerAssemblyLibrary.preview).length, 1, "炸开场景必须显示归并后的一个连续下料零件");
assert.equal(assemblyBlankRows(previewView.tubeDesignerAssemblyLibrary.preview)[0].data.geometry.url.startsWith("memory://result-"), true);
assert.deepEqual(assemblyBlankRows(previewView.tubeDesignerAssemblyLibrary.preview)[0].roles, ["segmentA", "segmentB"]);
assert.deepEqual(assemblyBlankRows(previewView.tubeDesignerAssemblyLibrary.preview)[0].data.localToWorldMatrix, explodedMatrix,
  "炸开场景必须优先使用装配工艺声明的炸开姿态");
const logicalFinishedRows = assemblySceneRows(previewView.tubeDesignerAssemblyLibrary.preview, false);
assert.deepEqual(logicalFinishedRows.map((row) => row.data.geometry.url),
  previewView.tubeDesignerAssemblyLibrary.preview.finishedShape.rows.map((row) => row.data.geometry.url),
  "L 形成品图应使用独立成品外形，不借用加工后的下料件");
assert.ok(logicalFinishedRows.every((row) => row.data.geometry.url.startsWith("icax-assembly-finished://")));
const { armA, armB } = originalProductInput.spans;
assert.deepEqual(previewView.tubeDesignerAssemblyLibrary.preview.finishedShape.mesh.metadata.section,
  { width: armA.parameters.width, depth: armA.parameters.depth, wall: armA.parameters.wallThickness,
    outerRadius: armA.parameters.cornerRadius, innerRadius: armA.parameters.innerRadius,
    lengthA: armA.length, lengthB: armB.length },
  "成品外形尺寸应来自原始成品输入");
assert.deepEqual(assemblySceneRows(previewView.tubeDesignerAssemblyLibrary.preview, true),
  assemblyBlankRows(previewView.tubeDesignerAssemblyLibrary.preview), "炸开后必须显示真实下料件");

const formedRow = { entityId: "assembly-formed:continuousBlank", roles: ["a", "b"], data: {
  geometry: { url: "memory://formed-bend", version: 1 }, geometryKind: 1, renderClass: 1,
  visible: true, selectable: false,
} };
previewView.tubeDesignerAssemblyLibrary.preview.formedPreview = {
  rows: [formedRow], resources: new Map([["memory://formed-bend", new ArrayBuffer(8)]]),
};
assert.deepEqual(assemblySceneRows(previewView.tubeDesignerAssemblyLibrary.preview, false), logicalFinishedRows,
  "切换单件成形预览配方不能改变 L 形成品形态");
assert.deepEqual(assemblySceneRows(previewView.tubeDesignerAssemblyLibrary.preview, true),
  assemblyBlankRows(previewView.tubeDesignerAssemblyLibrary.preview),
  "下料件视图仍应显示真实加工几何");
await handleAssemblyLibraryAction({}, previewView, "tube-designer-assembly-set-view", {
  dataset: { tubeAssemblyView: "finished" },
}, { renderProject() {} });
assert.match(renderAssemblyLibraryViewportOverlay({}, previewView), /成品外形与尺寸/);
assert.match(renderAssemblyLibraryViewportOverlay({}, previewView), /data-tube-assembly-view="finished"/);
await handleAssemblyLibraryAction({}, previewView, "tube-designer-assembly-set-view", {
  dataset: { tubeAssemblyView: "exploded" },
}, { renderProject() {} });
assert.equal(assemblyLibraryState(previewView).exploded, true);
assert.match(renderAssemblyLibraryViewportOverlay({}, previewView), /按模板加工的下料件（成形前）/);

const separatePreview = {
  designParts: ["a", "b"].map((role) => ({
    id: `design-${role}`, role, matrix: identity,
    response: { baseGeometry: { url: `memory://logical-${role}` }, baseMaterial: { url: `memory://logical-material-${role}` } },
  })),
  manufacturingParts: ["a", "b"].map((sourceRole) => ({
    sourceRole,
    response: { geometry: { url: `memory://cut-${sourceRole}` }, material: { url: `memory://cut-material-${sourceRole}` } },
  })),
};
const separateFinishedRows = assemblyFinishedRows(separatePreview);
assert.deepEqual(separateFinishedRows.map((item) => item.data.geometry.url),
  ["memory://logical-a", "memory://logical-b"],
  "成品只显示两件构件的产品外形，不能显示插舌、开槽等加工后的下料几何");
assert.deepEqual(separateFinishedRows.map((item) => item.data.material.url),
  ["memory://logical-material-a", "memory://logical-material-b"]);
assert.deepEqual(assemblySceneRows(separatePreview, false), separateFinishedRows);
assert.deepEqual(assemblyBlankRows(separatePreview).map((item) => item.data.geometry.url),
  ["memory://cut-a", "memory://cut-b"], "加工细节只在下料件中显示");
const changedProcess = structuredClone(separatePreview);
changedProcess.manufacturingParts[0].response.geometry.url = "memory://welded-a";
changedProcess.manufacturingParts[1].response.geometry.url = "memory://glued-b";
assert.deepEqual(assemblySceneRows(changedProcess, false), separateFinishedRows,
  "同一成品可选择不同装配工艺，不能因此改变成品外形或摆位");
assert.notDeepEqual(assemblyBlankRows(changedProcess), assemblyBlankRows(separatePreview),
  "不同工艺仍应改变各自下料结果");

const noRender = { renderProject() {} };
const waitFor = async (predicate, message) => {
  for (let tick = 0; tick < 100; tick += 1) {
    if (predicate()) return;
    await new Promise((resolve) => setImmediate(resolve));
  }
  assert.fail(message);
};
const deferred = () => {
  let resolve;
  const promise = new Promise((done) => { resolve = done; });
  return { promise, resolve };
};
const lazyPreviewFixture = () => {
  const fixture = {
    view: { activeAreaId: "assemblies", tubeDesignerLicense: grantedLicense, tubeDesignerAssemblyTemplates: templates,
      tubeDesignerAssemblyLibrary: { selectedId: "bend" } },
    calls: [], nextFinishedGate: null, nextProcessGate: null, nextBlankGate: null,
    applicability: null, exampleProduct: null, nextCheckGate: null, nextExampleGate: null,
    nextFinishedError: null,
  };
  fixture.context = { sceneProxy: { resources: {}, async invoke(method, payload) {
    fixture.calls.push({ method, payload: structuredClone(payload) });
    if (method === "TubeDesigner.CheckAssemblyTemplateApplicability") {
      const gate = fixture.nextCheckGate;
      fixture.nextCheckGate = null;
      if (gate) await gate.promise;
      const template = templates.find((item) => item.id === payload.templateId);
      return { schema: "icax.assembly-applicability", schemaVersion: 1, templateId: template.id,
        applicable: fixture.applicability ? fixture.applicability(payload)
          : payload.processInput?.schema === "icax.assembly-process-input",
        reason: "当前成品不能应用该工艺" };
    }
    if (method === "TubeDesigner.GetAssemblyTemplateExampleProduct") {
      const gate = fixture.nextExampleGate;
      fixture.nextExampleGate = null;
      if (gate) await gate.promise;
      const template = templates.find((item) => item.id === payload.templateId);
      return { schema: "icax.assembly-example-product", schemaVersion: 1, templateId: template.id,
        finishedProduct: fixture.exampleProduct ? fixture.exampleProduct(payload)
          : createFinishedProduct(template.exampleInput.shapeId) };
    }
    if (method === "TubeDesigner.ResolveAssemblyTemplatePreview") {
      if (payload.finishedOnly) {
        const error = fixture.nextFinishedError;
        fixture.nextFinishedError = null;
        if (error) throw error;
        const gate = fixture.nextFinishedGate;
        fixture.nextFinishedGate = null;
        if (gate) await gate.promise;
        return {
          schema: "icax.finished-product-preview", schemaVersion: 1,
          finishedProduct: structuredClone(payload.finishedProduct), layoutShape: payload.finishedProduct.shapeId,
          sceneParameters: structuredClone(payload.finishedProduct.parameters),
          designParts: productDesignParts(payload.finishedProduct),
        };
      }
      const gate = fixture.nextProcessGate;
      fixture.nextProcessGate = null;
      if (gate) await gate.promise;
      const template = templates.find((item) => item.id === payload.templateId);
      return {
        schema: "icax.assembly-preview-plan", templateId: template.id,
        processInput: structuredClone(payload.processInput), parameters: structuredClone(payload.parameters),
        sceneParameters: structuredClone(payload.processInput.geometry),
        designParts: processDesignParts(payload.processInput),
        manufacturingParts: Array.from({ length: template.outputs.manufacturingPartCount }, (_, index) => ({
          id: `manufacturing-${index}`, sourceRole: Object.keys(payload.processInput.parts)[index],
          label: `测试下料件 ${index + 1}`, request: { length: 210 + index + (payload.parameters.bendRadius ?? 0)
            + Object.values(payload.processInput.parts)[0].length / 100, features: [], ends: {} },
          matrix: identity, explodedMatrix, compareMatrix: identity,
        })),
      };
    }
    if (method === "TubeDesigner.PreviewPunchWizard" || method === "TubeDesigner.PreviewAssemblyManufacturingPart") {
      const index = fixture.calls.filter((call) => call.method === method).length;
      const gate = fixture.nextBlankGate;
      fixture.nextBlankGate = null;
      if (gate) await gate.promise;
      return { previewComputed: true, resultValid: true, solidCount: 1,
        baseGeometry: { url: `memory://lazy-base-${index}`, version: 1 },
        geometry: { url: `memory://lazy-result-${index}`, version: 1 } };
    }
    throw new Error(`unexpected ${method}`);
  } } };
  fixture.counts = () => ({
    finished: fixture.calls.filter((call) => call.method === "TubeDesigner.ResolveAssemblyTemplatePreview" && call.payload.finishedOnly).length,
    process: fixture.calls.filter((call) => call.method === "TubeDesigner.ResolveAssemblyTemplatePreview" && !call.payload.finishedOnly).length,
    native: fixture.calls.filter((call) => call.method === "TubeDesigner.PreviewAssemblyManufacturingPart").length,
  });
  fixture.ensure = () => ensureAssemblyLibraryPreview(fixture.context, fixture.view);
  fixture.settle = async () => {
    await completePreview(fixture.context, fixture.view);
    assert.equal(assemblyLibraryState(fixture.view).selectionRequest ?? null, null,
      "独立成品的适用性检查应完成");
    assert.equal(assemblyLibraryState(fixture.view).previewRequest ?? null, null,
      "所请求的成品或下料预览应完成");
    assert.equal(assemblyLibraryState(fixture.view).previewError, "");
  };
  fixture.select = async (id) => handleAssemblyLibraryAction(fixture.context, fixture.view,
    "tube-designer-assembly-select", { dataset: { tubeAssemblyId: id } }, noRender);
  fixture.setView = async (mode) => handleAssemblyLibraryAction(fixture.context, fixture.view,
    "tube-designer-assembly-set-view", { dataset: { tubeAssemblyView: mode } }, noRender);
  fixture.editProcess = async (value) => handleAssemblyLibraryAction(fixture.context, fixture.view,
    "tube-designer-assembly-parameter-change", {
      value: String(value), dataset: { tubeAssemblyId: "bend", tubeAssemblyParameter: "bendRadius" },
    }, noRender);
  return fixture;
};

const lazy = lazyPreviewFixture();
await lazy.settle();
assert.deepEqual(lazy.counts(), { finished: 1, process: 0, native: 0 });
const sharedFinishedShape = assemblyLibraryState(lazy.view).preview.finishedShape;
const sharedFinishedRows = assemblySceneRows(assemblyLibraryState(lazy.view).preview, false);
for (const template of templates.filter((item) => item.exampleInput?.shapeId === "l")) {
  await lazy.select(template.id);
  await lazy.settle();
  assert.equal(assemblyLibraryState(lazy.view).preview.finishedShape, sharedFinishedShape,
    "同一输入的 L 形成品网格必须跨工艺复用");
  assert.deepEqual(assemblySceneRows(assemblyLibraryState(lazy.view).preview, false), sharedFinishedRows);
}
assert.deepEqual(lazy.counts(), { finished: 1, process: 0, native: 0 },
  "切换全部 L 形工艺不得重新计算成品或提前制造下料");
await lazy.select("bend");
await lazy.editProcess(41);
await lazy.settle();
assert.deepEqual(lazy.counts(), { finished: 1, process: 0, native: 0 },
  "成品视图编辑工艺参数只更新草稿");
await lazy.setView("exploded");
await lazy.settle();
assert.deepEqual(lazy.counts(), { finished: 1, process: 1, native: 1 });
assert.equal(lazy.calls.find((call) => call.method.endsWith("ResolveAssemblyTemplatePreview") && !call.payload.finishedOnly)
  .payload.parameters.bendRadius, 41, "首次下料应使用等待期间最新工艺草稿");
await lazy.setView("finished");
await lazy.settle();
await lazy.setView("exploded");
await lazy.settle();
assert.deepEqual(lazy.counts(), { finished: 1, process: 1, native: 1 },
  "视图来回切换应复用已完成的相同下料结果");
await lazy.editProcess(42);
await lazy.settle();
assert.deepEqual(lazy.counts(), { finished: 1, process: 2, native: 2 },
  "下料视图修改工艺参数应重新生成下料但保留成品缓存");
await lazy.setView("finished");
await lazy.editProcess(43);
await lazy.settle();
assert.deepEqual(lazy.counts(), { finished: 1, process: 2, native: 2 },
  "返回成品后修改工艺不应继续生成下料");
await lazy.setView("exploded");
await lazy.settle();
assert.deepEqual(lazy.counts(), { finished: 1, process: 3, native: 3 });
assert.equal(lazy.calls.filter((call) => call.method.endsWith("ResolveAssemblyTemplatePreview") && !call.payload.finishedOnly)
  .at(-1).payload.parameters.bendRadius, 43);

await lazy.setView("finished");
const originalLength = finishedProductInput(lazy.view).spans.armA.length;
await handleAssemblyLibraryAction(lazy.context, lazy.view, "tube-designer-finished-length-change", {
  value: String(originalLength + 11), dataset: { finishedShape: "l", finishedSpan: "armA" },
}, noRender);
await lazy.settle();
assert.deepEqual(lazy.counts(), { finished: 2, process: 3, native: 3 },
  "改变成品输入必须重算成品，工艺缓存不能跨不同输入复用");
assert.notEqual(assemblyLibraryState(lazy.view).preview.finishedShape, sharedFinishedShape);
await lazy.select("two-end-end-angle");
await lazy.settle();
assert.deepEqual(lazy.counts(), { finished: 2, process: 3, native: 3 },
  "更新后的成品仍须跨 L 形工艺共享");
lazy.applicability = (payload) => payload.templateId !== "t-contact-fit";
await lazy.select("t-contact-fit");
await lazy.settle();
assert.equal(lazy.counts().finished, 2);
assert.equal(lazy.counts().process, 3, "选择其他造型的工艺不能改写成品或预先生成下料");
assert.equal(finishedProductInput(lazy.view).shapeId, "l");
assert.match(assemblyLibraryState(lazy.view).selectionProblem, /不能应用/);
lazy.applicability = null;
await lazy.select("bend");
await lazy.settle();
assert.equal(lazy.counts().finished, 2,
  "选择不适用工艺再返回兼容工艺应复用原成品与原草稿");
assert.equal(finishedProductInput(lazy.view).spans.armA.length, originalLength + 11);

const cancelled = lazyPreviewFixture();
await cancelled.select("two-end-end-angle");
await cancelled.settle();
const cancelledFinishedShape = assemblyLibraryState(cancelled.view).preview.finishedShape;
cancelled.nextBlankGate = deferred();
const blankGate = cancelled.nextBlankGate;
await cancelled.setView("exploded");
const cancelledRequest = cancelled.ensure();
await waitFor(() => cancelled.counts().native === 1, "下料生成应进入第一个零件请求");
await cancelled.setView("finished");
await cancelled.settle();
blankGate.resolve();
await cancelledRequest.promise;
assert.equal(cancelled.counts().native, 1, "返回成品后过期请求不得继续计算第二个下料件");
assert.equal(assemblyLibraryState(cancelled.view).exploded, false);
assert.equal(assemblyLibraryState(cancelled.view).preview.finishedShape, cancelledFinishedShape);
assert.equal(assemblyLibraryState(cancelled.view).preview.processReady, false,
  "取消后不完整下料不能标记为已生成或覆盖成品");
assert.equal(assemblyLibraryState(cancelled.view).previewError, "");
await cancelled.setView("exploded");
await cancelled.settle();
assert.equal(assemblyLibraryState(cancelled.view).preview.processReady, true,
  "再次请求下料应能完成，不能沿用半成品结果");

const switched = lazyPreviewFixture();
await switched.settle();
const switchedFinishedShape = assemblyLibraryState(switched.view).preview.finishedShape;
switched.nextProcessGate = deferred();
const processGate = switched.nextProcessGate;
await switched.setView("exploded");
const switchedRequest = switched.ensure();
await waitFor(() => switched.counts().process === 1, "工艺预览应进入延迟方案请求");
await switched.select("two-end-end-angle");
await switched.settle();
processGate.resolve();
await switchedRequest.promise;
assert.deepEqual(switched.counts(), { finished: 1, process: 1, native: 0 },
  "快速切换工艺后过期方案不得开始生成旧工艺下料");
assert.equal(assemblyLibraryState(switched.view).selectedId, "two-end-end-angle");
assert.equal(assemblyLibraryState(switched.view).preview.plan.templateId, "two-end-end-angle",
  "晚到的旧工艺结果不得覆盖当前选择");
assert.equal(assemblyLibraryState(switched.view).preview.finishedShape, switchedFinishedShape);
assert.equal(assemblyLibraryState(switched.view).preview.processReady, false);
await switched.setView("exploded");
await switched.settle();
assert.equal(assemblyLibraryState(switched.view).preview.plan.templateId, "two-end-end-angle");
assert.equal(assemblyLibraryState(switched.view).preview.processReady, true);
assert.deepEqual(switched.counts(), { finished: 1, process: 2, native: 2 });

const pendingShared = lazyPreviewFixture();
pendingShared.nextFinishedGate = deferred();
const finishedGate = pendingShared.nextFinishedGate;
const firstFinishedRequest = pendingShared.ensure();
await waitFor(() => {
  pendingShared.ensure();
  return pendingShared.counts().finished === 1;
}, "成品生成应进入延迟请求");
const sharedGeometryRequest = assemblyLibraryState(pendingShared.view).previewRequest;
const secondSelection = pendingShared.select("two-end-end-angle");
await waitFor(() => assemblyLibraryState(pendingShared.view).selectionRequest?.templateId === "two-end-end-angle",
  "同 L 工艺检查应等待共享成品请求返回的真实局部姿态");
const secondFinishedRequest = pendingShared.ensure();
await new Promise((resolve) => setImmediate(resolve));
assert.deepEqual(pendingShared.counts(), { finished: 1, process: 0, native: 0 },
  "成品仍在生成时切换同 L 工艺，应共用同一进行中的请求");
assert.equal(pendingShared.calls.filter((call) => call.method === "TubeDesigner.CheckAssemblyTemplateApplicability").length,
  0, "实际局部姿态返回之前不能拿示例坐标代替检查输入");
finishedGate.resolve();
await Promise.all([firstFinishedRequest.promise, secondFinishedRequest.promise, sharedGeometryRequest?.promise, secondSelection]);
await pendingShared.settle();
assert.equal(assemblyLibraryState(pendingShared.view).preview.plan.templateId, "two-end-end-angle");
assert.equal(assemblyLibraryState(pendingShared.view).preview.processReady, false);
assert.equal(assemblyLibraryState(pendingShared.view).previewError, "");

const nextProject = lazyPreviewFixture();
const previousProjectShape = assemblyLibraryState(pendingShared.view).preview.finishedShape;
pendingShared.context = nextProject.context;
await pendingShared.settle();
assert.equal(nextProject.counts().finished, 1,
  "切换场景宿主时相同产品输入也不能沿用旧项目的资源缓存");
assert.notEqual(assemblyLibraryState(pendingShared.view).preview.finishedShape, previousProjectShape);

const applicableProduct = lazyPreviewFixture();
finishedProductInput(applicableProduct.view).spans.armA.length = 641;
finishedProductInput(applicableProduct.view).spans.armB.length = 433;
await applicableProduct.settle();
const keptProduct = finishedProductInput(applicableProduct.view);
const keptSnapshot = structuredClone(keptProduct);
const keptShape = assemblyLibraryState(applicableProduct.view).preview.finishedShape;
const keptCheckCount = applicableProduct.calls.filter((call) => call.method === "TubeDesigner.CheckAssemblyTemplateApplicability").length;
await applicableProduct.select("two-end-end-angle");
await applicableProduct.settle();
const keptChecks = applicableProduct.calls.filter((call) => call.method === "TubeDesigner.CheckAssemblyTemplateApplicability");
assert.equal(keptChecks.length, keptCheckCount + 1, "每次选择新工艺应先调用模板适用性函数");
assert.equal(keptChecks.at(-1).payload.templateId, "two-end-end-angle");
assert.deepEqual(keptChecks.at(-1).payload.processInput,
  assemblyProcessInput(templates.find((item) => item.id === "two-end-end-angle"), keptSnapshot,
    { designParts: productDesignParts(keptSnapshot) }),
  "适用性函数必须收到当前成品的完整必要局部数据，不能借用模板默认值");
assert.ok(!Object.hasOwn(keptChecks.at(-1).payload, "finishedProduct"));
assert.equal(finishedProductInput(applicableProduct.view), keptProduct,
  "原生模板认可当前成品时应保留其对象与全部输入");
assert.deepEqual(keptProduct, keptSnapshot);
assert.equal(assemblyLibraryState(applicableProduct.view).preview.finishedShape, keptShape);
assert.equal(applicableProduct.calls.filter((call) => call.method === "TubeDesigner.GetAssemblyTemplateExampleProduct").length, 0,
  "适用时不得读取或采用模板示例成品");
assert.deepEqual(applicableProduct.counts(), { finished: 1, process: 0, native: 0 },
  "适用且输入未改时既不重算成品，也不预先计算下料");

const rejectedProduct = lazyPreviewFixture();
await rejectedProduct.settle();
const rejectedOriginal = finishedProductInput(rejectedProduct.view);
const rejectedSnapshot = structuredClone(rejectedOriginal);
const suppliedExample = createFinishedProduct("l");
suppliedExample.spans.armA.length = 787;
suppliedExample.spans.armB.length = 519;
rejectedProduct.applicability = () => false;
rejectedProduct.exampleProduct = () => suppliedExample;
const rejectionCallsStart = rejectedProduct.calls.length;
await rejectedProduct.select("two-end-end-angle");
await rejectedProduct.settle();
assert.deepEqual(rejectedProduct.calls.slice(rejectionCallsStart).map((call) => call.method), [
  "TubeDesigner.CheckAssemblyTemplateApplicability",
], "工艺不适用只显示原因，不能自动读取示例或重算原成品");
assert.equal(finishedProductInput(rejectedProduct.view), rejectedOriginal);
assert.deepEqual(finishedProductInput(rejectedProduct.view), rejectedSnapshot);
assert.match(assemblyLibraryState(rejectedProduct.view).selectionProblem, /不能应用/);
assert.deepEqual(rejectedProduct.counts(), { finished: 1, process: 0, native: 0 });
await handleAssemblyLibraryAction(rejectedProduct.context, rejectedProduct.view,
  "tube-designer-assembly-load-example", {}, noRender);
rejectedProduct.applicability = () => true;
await rejectedProduct.settle();
assert.equal(rejectedProduct.calls.find((call) => call.method === "TubeDesigner.GetAssemblyTemplateExampleProduct")
  .payload.templateId, "two-end-end-angle");
assert.deepEqual(finishedProductInput(rejectedProduct.view), suppliedExample,
  "显式载入示例才采用模板返回的完整成品");
assert.notEqual(finishedProductInput(rejectedProduct.view), suppliedExample, "宿主应复制模板返回的成品输入");
assert.deepEqual(rejectedOriginal, rejectedSnapshot, "获取新示例不能修改先前成品输入对象");
assert.deepEqual(rejectedProduct.counts(), { finished: 2, process: 0, native: 0 });

const supersededCheck = lazyPreviewFixture();
await supersededCheck.settle();
const supersededProduct = finishedProductInput(supersededCheck.view);
const supersededShape = assemblyLibraryState(supersededCheck.view).preview.finishedShape;
supersededCheck.applicability = (payload) => payload.templateId !== "two-end-end-angle";
supersededCheck.nextCheckGate = deferred();
const supersededGate = supersededCheck.nextCheckGate;
const firstCheckCount = supersededCheck.calls.filter((call) => call.method === "TubeDesigner.CheckAssemblyTemplateApplicability").length;
const obsoleteSelection = supersededCheck.select("two-end-end-angle");
await waitFor(() => supersededCheck.calls.filter((call) => call.method === "TubeDesigner.CheckAssemblyTemplateApplicability").length
  === firstCheckCount + 1, "旧模板的适用性检查应已发出");
await supersededCheck.select("wrap-a-over-b");
await supersededCheck.settle();
supersededGate.resolve();
await obsoleteSelection;
assert.equal(assemblyLibraryState(supersededCheck.view).selectedId, "wrap-a-over-b",
  "延迟适用性结果不得选回旧模板");
assert.equal(finishedProductInput(supersededCheck.view), supersededProduct);
assert.equal(assemblyLibraryState(supersededCheck.view).preview.finishedShape, supersededShape);
assert.equal(supersededCheck.calls.filter((call) => call.method === "TubeDesigner.GetAssemblyTemplateExampleProduct").length, 0,
  "被新选择取代的否定结果不能继续读取或采用旧模板示例");
assert.deepEqual(supersededCheck.counts(), { finished: 1, process: 0, native: 0 });

const supersededExample = lazyPreviewFixture();
await supersededExample.settle();
const exampleOriginalProduct = finishedProductInput(supersededExample.view);
const exampleOriginalShape = assemblyLibraryState(supersededExample.view).preview.finishedShape;
supersededExample.applicability = (payload) => payload.templateId !== "two-end-end-angle";
supersededExample.nextExampleGate = deferred();
const oldExampleGate = supersededExample.nextExampleGate;
await supersededExample.select("two-end-end-angle");
const oldExampleSelection = handleAssemblyLibraryAction(supersededExample.context, supersededExample.view,
  "tube-designer-assembly-load-example", {}, noRender);
await waitFor(() => supersededExample.calls.some((call) => call.method === "TubeDesigner.GetAssemblyTemplateExampleProduct"),
  "显式示例动作应开始读取所选工艺的示例成品");
await supersededExample.select("wrap-a-over-b");
await supersededExample.settle();
oldExampleGate.resolve();
await oldExampleSelection;
assert.equal(assemblyLibraryState(supersededExample.view).selectedId, "wrap-a-over-b");
assert.equal(finishedProductInput(supersededExample.view), exampleOriginalProduct,
  "晚到的旧模板示例也不得替换新选择已保留的成品");
assert.equal(assemblyLibraryState(supersededExample.view).preview.finishedShape, exampleOriginalShape);
assert.deepEqual(supersededExample.counts(), { finished: 1, process: 0, native: 0 });

const editedCheck = lazyPreviewFixture();
await editedCheck.settle();
const editedProduct = finishedProductInput(editedCheck.view);
editedCheck.applicability = (payload) => payload.templateId !== "two-end-end-angle"
  || payload.processInput.parts.memberA.length === 673;
editedCheck.nextCheckGate = deferred();
const editedGate = editedCheck.nextCheckGate;
const editedCheckCount = editedCheck.calls.filter((call) => call.method === "TubeDesigner.CheckAssemblyTemplateApplicability").length;
const editedSelection = editedCheck.select("two-end-end-angle");
await waitFor(() => editedCheck.calls.filter((call) => call.method === "TubeDesigner.CheckAssemblyTemplateApplicability").length
  === editedCheckCount + 1, "修改成品前适用性检查应已发出");
await handleAssemblyLibraryAction(editedCheck.context, editedCheck.view, "tube-designer-finished-length-change", {
  value: "673", dataset: { finishedShape: "l", finishedSpan: "armA" },
}, noRender);
const editedSnapshot = structuredClone(editedProduct);
editedGate.resolve();
await editedSelection;
assert.equal(editedProduct.spans.armA.length, 673, "延迟响应不得覆盖检查期间的新成品编辑");
assert.equal(finishedProductInput(editedCheck.view), editedProduct);
assert.equal(finishedProductKey(finishedProductInput(editedCheck.view)), finishedProductKey(editedSnapshot));
assert.equal(assemblyLibraryState(editedCheck.view).selectedId, "bend",
  "当前成品改变后旧输入的适用性结果不得继续采纳选择");
assert.equal(editedCheck.calls.filter((call) => call.method === "TubeDesigner.GetAssemblyTemplateExampleProduct").length, 0);
await editedCheck.settle();
assert.equal(assemblyLibraryState(editedCheck.view).previewError, "");

const failedFinished = lazyPreviewFixture();
failedFinished.nextFinishedError = new Error("成品生成暂时失败");
const unhandledFinishedErrors = [];
const observeUnhandledFinishedError = (error) => { unhandledFinishedErrors.push(error); };
process.on("unhandledRejection", observeUnhandledFinishedError);
try {
  await completePreview(failedFinished.context, failedFinished.view);
  assert.match(assemblyLibraryState(failedFinished.view).previewError, /成品生成暂时失败/);
  assert.deepEqual(failedFinished.counts(), { finished: 1, process: 0, native: 0 });
  for (let repaint = 0; repaint < 3; repaint += 1) {
    renderAssemblyLibraryViewportOverlay(failedFinished.context, failedFinished.view);
    failedFinished.ensure();
  }
  await new Promise((resolve) => setImmediate(resolve));
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(failedFinished.counts(), { finished: 1, process: 0, native: 0 },
    "成品生成失败后的刷新不能偷偷开启未观察的缓存重试");
  assert.deepEqual(unhandledFinishedErrors, [], "失败请求必须被观察，不能产生未处理的拒绝");
  await handleAssemblyLibraryAction(failedFinished.context, failedFinished.view,
    "tube-designer-assembly-retry-preview", {}, noRender);
  await failedFinished.settle();
  assert.deepEqual(failedFinished.counts(), { finished: 2, process: 0, native: 0 },
    "显式重新生成后才允许再次请求成品");
  assert.equal(assemblyLibraryState(failedFinished.view).preview.processReady, false);
  assert.ok(assemblyLibraryState(failedFinished.view).preview.finishedShape);
  assert.deepEqual(unhandledFinishedErrors, []);
} finally {
  process.off("unhandledRejection", observeUnhandledFinishedError);
}

console.log("TubeDesignerAssemblyLibraryTest: passed");
