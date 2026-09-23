import assert from "node:assert/strict";
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { normalizeAssemblyCatalogue } from "../../apps/tube-designer/webpage/assemblyCatalog.mjs";
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
assert.ok(templates.every((item) => item.previewScene.designParts.length === item.participants.length), "每个装配模板必须给出全部逻辑零件的三维位置");
assert.ok(templates.every((item) => item.previewScene.manufacturingParts.length === item.outputs.manufacturingPartCount), "每个装配模板必须覆盖全部下料结果");
assert.ok(templates.every((item) => item.parameterDiagram?.schemaVersion === 2), "每个装配模板必须提供二维参数示意图");
const ids = new Set(templates.map((item) => item.id));
assert.ok(["two-end-end-angle", "two-end-middle", "mechanical-fastener", "three-end-end-end",
  "three-end-end-middle", "four-end-end-end-end", "insert-sleeve", "weld-interface", "bend", "tab-slot-lock"]
  .every((id) => ids.has(id)), "资源库必须显示六类场景中的全部模板");
assert.ok(["through-bolt", "slot-bolt-adjustable", "saddle-weld"].every((id) => !ids.has(id)),
  "兼容模板不能继续作为重复卡片出现");

for (const template of templates) {
  for (const process of template.partProcesses) {
    const ids = process.resource ? [process.resource.id] : process.resourceSelection.options;
    for (const id of ids) {
      const processPath = fileURLToPath(new URL(`../../apps/tube-designer/templates/mold/${id}/tool.json`, import.meta.url));
      assert.equal(existsSync(processPath), true, `${template.id} 引用的单件工艺 ${id} 必须存在`);
    }
  }
}

const bend = templates.find((item) => item.id === "bend");
assert.equal(bend.manufacturingPlan.realization, "integrated");
assert.equal(bend.manufacturingPlan.blankParts.length, 1);
assert.deepEqual(bend.parameters.find((item) => item.key === "bendMethod").options.map((item) => item.value), ["cold", "notched"]);
assert.deepEqual(bend.partProcesses[0].resourceSelection.options, [
  "segmented-bend", "v-notch-sharp", "embedded-arc-notch",
  "edge-arc-groove", "flexible-slit-bend",
]);
assert.equal(JSON.parse(readFileSync(new URL("../../apps/tube-designer/templates/mold/segmented-bend/tool.json", import.meta.url), "utf8")).displayName, "分段折弯槽口");
assert.deepEqual(bend.partProcesses[0].parameterBindingsByResource["embedded-arc-notch"], { angle: "$angle" });
assert.deepEqual(bend.partProcesses[0].parameterBindingsByResource["edge-arc-groove"], { angle: "$angle" });
assert.deepEqual(bend.partProcesses[0].parameterBindingsByResource["flexible-slit-bend"], { angle: "$angle", bendRadius: "$bendRadius" });
const embeddedArc = JSON.parse(readFileSync(new URL("../../apps/tube-designer/templates/mold/embedded-arc-notch/tool.json", import.meta.url), "utf8"));
assert.ok(embeddedArc.parameters.some((item) => item.key === "arcRadius"));
assert.ok(!embeddedArc.parameters.some((item) => item.key === "bendRadius"));
bend.partProcesses[0].resourceSelection.resources = bend.partProcesses[0].resourceSelection.options.map((id) =>
  JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/mold/${id}/tool.json`, import.meta.url), "utf8")));

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

const view = { tubeDesignerAssemblyTemplates: templates, tubeDesignerAssemblyLibrary: { selectedId: "bend" } };
const state = assemblyLibraryState(view);
assert.match(renderAssemblyLibraryLeftPane({}, view), /一体折弯/);
assert.match(renderAssemblyLibraryRightPane({}, view), /冷折展开/);
assert.doesNotMatch(renderAssemblyLibraryRightPane({}, view), /槽口参数/);
assert.doesNotMatch(renderAssemblyLibraryRightPane({}, view), /逻辑零件|零件对照|下料归并|实现步骤|装配输出/);
assert.match(renderAssemblyLibraryRightPane({}, view), />重置</);
const overlay = renderAssemblyLibraryViewportOverlay({}, view);
assert.match(overlay, /成品：查看最终装配关系/);
assert.match(overlay, /data-tube-assembly-view="finished"[^>]+aria-pressed="true"/);
assert.match(overlay, /data-tube-assembly-view="exploded"/);
assert.match(overlay, /data-tube-assembly-scene-viewport/);
assert.doesNotMatch(overlay, /data-tube-assembly-finished-viewport|data-tube-assembly-blank-viewport/);
assert.doesNotMatch(overlay, /data-tube-assembly-diagram-dock/);
assert.match(renderAssemblyLibraryRightPane({}, view), /data-cam-change-action="tube-designer-assembly-parameter-change"/);
const libraryHtml = renderAssemblyLibraryLeftPane({}, view);
assert.match(libraryHtml, /同轴套接 \/ 伸缩/);
assert.match(libraryHtml, /孔系紧固/);
assert.match(libraryHtml, /搭接焊 \/ 塞焊 \/ 槽焊/);
assert.match(libraryHtml, /三端汇交/);
assert.match(libraryHtml, /四端汇交/);
assert.doesNotMatch(libraryHtml, /对穿螺栓装配|长孔调节装配|鞍口相贯焊接/);

let renders = 0;
const ops = { renderProject() { renders += 1; } };
await handleAssemblyLibraryAction({}, view, "tube-designer-assembly-parameter-change", {
  value: "notched",
  dataset: { tubeAssemblyId: "bend", tubeAssemblyParameter: "bendMethod" },
}, ops);
assert.equal(assemblyParameterValues(view).bendMethod, "notched");
assert.match(renderAssemblyLibraryRightPane({}, view), /加工设置/);
assert.doesNotMatch(renderAssemblyLibraryRightPane({}, view), /前置单件工艺/);
assert.match(renderAssemblyLibraryRightPane({}, view), /分段折弯槽口/);
assert.equal(renders, 1);

await handleAssemblyLibraryAction({}, view, "tube-designer-assembly-parameter-change", {
  value: "embedded-arc-notch",
  dataset: { tubeAssemblyId: "bend", tubeAssemblyParameter: "slotProcess" },
}, ops);
const embeddedHtml = renderAssemblyLibraryRightPane({}, view);
assert.match(embeddedHtml, /嵌入圆弧半径 R/);
assert.match(embeddedHtml, /data-tube-part-process-parameter="arcRadius"/);
assert.doesNotMatch(embeddedHtml, /data-tube-part-process-parameter="bendRadius"/);

await handleAssemblyLibraryAction({}, view, "tube-designer-assembly-parameter-change", {
  value: "edge-arc-groove",
  dataset: { tubeAssemblyId: "bend", tubeAssemblyParameter: "slotProcess" },
}, ops);
const edgeArcHtml = renderAssemblyLibraryRightPane({}, view);
assert.match(edgeArcHtml, /加工设置/);
assert.match(edgeArcHtml, /<small>边弧槽<\/small>/);
assert.match(edgeArcHtml, /底部保留厚度/);
assert.match(edgeArcHtml, /data-tube-part-process-parameter="bridge"/);
assert.match(edgeArcHtml, /data-tube-part-process-parameter="leftArc"/);
assert.doesNotMatch(edgeArcHtml, /data-tube-part-process-parameter="angle"/);
assert.match(edgeArcHtml, /<details class="tube-connection-library-process-advanced">/);
assert.doesNotMatch(edgeArcHtml, /tube-connection-library-process-advanced" open/);
assert.doesNotMatch(edgeArcHtml, /逻辑零件|下料归并|实现步骤|装配输出/);

await handleAssemblyLibraryAction({}, view, "tube-designer-assembly-parameter-change", {
  value: "v-notch-sharp",
  dataset: { tubeAssemblyId: "bend", tubeAssemblyParameter: "slotProcess" },
}, ops);
await handleAssemblyLibraryAction({}, view, "tube-designer-assembly-process-parameter-change", {
  checked: true,
  dataset: { tubeAssemblyId: "bend", tubeAssemblyProcess: "bend-slot", tubePartProcess: "v-notch-sharp", tubePartProcessParameter: "maleFemale" },
}, ops);
assert.equal(assemblyLibraryState(view).processDrafts.bend["bend-slot"]["v-notch-sharp"].maleFemaleSize, 2,
  "assembly reuse must fill公母尺寸 from the source管型 wall thickness");
await handleAssemblyLibraryAction({}, view, "tube-designer-assembly-process-parameter-change", {
  value: "relief",
  dataset: { tubeAssemblyId: "bend", tubeAssemblyProcess: "bend-slot", tubePartProcess: "v-notch-sharp", tubePartProcessParameter: "bottomStrategy" },
}, ops);
const vHtml = renderAssemblyLibraryRightPane({}, view);
assert.match(vHtml, /V槽/);
assert.match(vHtml, /释放孔形状/);
assert.match(vHtml, /包围圆/);

await handleAssemblyLibraryAction({}, view, "tube-designer-assembly-parameter-change", {
  value: "flexible-slit-bend",
  dataset: { tubeAssemblyId: "bend", tubeAssemblyParameter: "slotProcess" },
}, ops);
assert.match(renderAssemblyLibraryRightPane({}, view), /柔性折弯缝/);

const tabSlotView = { tubeDesignerAssemblyTemplates: templates, tubeDesignerAssemblyLibrary: { selectedId: "tab-slot-lock" } };
const tabSlotHtml = renderAssemblyLibraryRightPane({}, tabSlotView);
assert.match(tabSlotHtml, /插舌端部成形/);
assert.match(tabSlotHtml, /插槽端部成形/);
assert.doesNotMatch(tabSlotHtml, /零件对照|tube-connection-library-part/);
assert.match(tabSlotHtml, /<details class="tube-connection-library-parameter-section tube-connection-library-process-section">/);

await handleAssemblyLibraryAction({}, view, "tube-designer-assembly-set-view", {
  dataset: { tubeAssemblyView: "exploded" },
}, ops);
assert.equal(assemblyLibraryState(view).exploded, true);
assert.match(renderAssemblyLibraryViewportOverlay({}, view), /炸开：查看各下料件及加工形状/);
assert.match(renderAssemblyLibraryViewportOverlay({}, view), /data-tube-assembly-view="exploded"[^>]+aria-pressed="true"/);
await handleAssemblyLibraryAction({}, view, "tube-designer-assembly-set-view", {
  dataset: { tubeAssemblyView: "finished" },
}, ops);
assert.equal(assemblyLibraryState(view).exploded, false);

await handleAssemblyLibraryAction({}, view, "tube-designer-assembly-toggle-diagram", {}, ops);
assert.match(renderAssemblyLibraryViewportOverlay({}, view), /data-tube-assembly-diagram-dock/);

const previewView = {
  activeAreaId: "assemblies",
  tubeDesignerAssemblyTemplates: templates,
  tubeDesignerAssemblyLibrary: { selectedId: "bend", showDiagram: false },
};
const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const explodedMatrix = [1, 0, 0, 42, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const invocations = [];
const previewContext = { sceneProxy: { resources: {}, async invoke(method, payload) {
  invocations.push({ method, payload });
  if (method === "TubeDesigner.ResolveAssemblyTemplatePreview") return {
    schema: "icax.assembly-preview-plan", templateId: "bend",
    designParts: ["a", "b"].map((id) => ({ id: `design-${id}`, role: id, label: id, request: { length: 100, features: [], ends: {} }, matrix: identity, compareMatrix: identity })),
    manufacturingParts: [{ id: "manufacturing-blank", sourceRole: "a", participantRoles: ["a", "b"], label: "blank", request: { length: 200, features: [], ends: {} }, matrix: identity, explodedMatrix, compareMatrix: identity }],
  };
  if (method === "TubeDesigner.PreviewPunchWizard") {
    const index = invocations.filter((item) => item.method.endsWith("PreviewPunchWizard")).length;
    return { previewComputed: true, baseGeometry: { url: `memory://base-${index}`, version: 1 }, geometry: { url: `memory://result-${index}`, version: 1 } };
  }
  throw new Error(`unexpected ${method}`);
} } };
previewView.tubeDesignerAssemblyLibraryRenderProject = () => renderAssemblyLibraryViewportOverlay(previewContext, previewView);
const request = ensureAssemblyLibraryPreview(previewContext, previewView);
await request.promise;
assert.equal(invocations.filter((item) => item.method === "TubeDesigner.ResolveAssemblyTemplatePreview").length, 1);
assert.equal(invocations.filter((item) => item.method === "TubeDesigner.PreviewPunchWizard").length, 3);
const finishedRows = assemblyFinishedRows(previewView.tubeDesignerAssemblyLibrary.preview);
assert.equal(finishedRows.length, 2, "默认成品场景必须显示两个逻辑零件");
assert.deepEqual(finishedRows.map((item) => item.data.geometry.url), ["memory://base-1", "memory://base-2"], "一体成形的成品场景必须使用逻辑零件几何");
assert.equal(assemblyBlankRows(previewView.tubeDesignerAssemblyLibrary.preview).length, 1, "炸开场景必须显示归并后的一个连续下料零件");
assert.equal(assemblyBlankRows(previewView.tubeDesignerAssemblyLibrary.preview)[0].data.geometry.url.startsWith("memory://result-"), true);
assert.deepEqual(assemblyBlankRows(previewView.tubeDesignerAssemblyLibrary.preview)[0].roles, ["a", "b"]);
assert.deepEqual(assemblyBlankRows(previewView.tubeDesignerAssemblyLibrary.preview)[0].data.localToWorldMatrix, explodedMatrix,
  "炸开场景必须优先使用装配工艺声明的炸开姿态");
assert.deepEqual(assemblySceneRows(previewView.tubeDesignerAssemblyLibrary.preview, false), finishedRows,
  "默认场景必须显示装配完成的成品");
assert.deepEqual(assemblySceneRows(previewView.tubeDesignerAssemblyLibrary.preview, true),
  assemblyBlankRows(previewView.tubeDesignerAssemblyLibrary.preview), "炸开后必须显示真实下料件");

const separateFinishedRows = assemblyFinishedRows({
  designParts: ["a", "b"].map((role) => ({
    id: `design-${role}`, role, matrix: identity,
    response: { baseGeometry: { url: `memory://logical-${role}` }, baseMaterial: { url: `memory://logical-material-${role}` } },
  })),
  manufacturingParts: ["a", "b"].map((sourceRole) => ({
    sourceRole,
    response: { geometry: { url: `memory://cut-${sourceRole}` }, material: { url: `memory://cut-material-${sourceRole}` } },
  })),
});
assert.deepEqual(separateFinishedRows.map((item) => item.data.geometry.url), ["memory://cut-a", "memory://cut-b"],
  "分件装配的成品场景必须显示真实端切或接口几何，避免接合处出现假缺口");

console.log("TubeDesignerAssemblyLibraryTest: passed");
