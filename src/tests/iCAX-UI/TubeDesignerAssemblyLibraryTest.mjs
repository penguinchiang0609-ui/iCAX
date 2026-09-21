import assert from "node:assert/strict";
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { normalizeAssemblyCatalogue } from "../../apps/tube-designer/webpage/assemblyCatalog.mjs";
import {
  assemblyBlankRows,
  assemblyFinishedRows,
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

assert.ok(templates.length >= 5, "装配库应覆盖折弯、插接、紧固和焊接");
assert.ok(templates.every((item) => item.schema === "icax.assembly-template" && item.schemaVersion === 1));
assert.ok(templates.every((item) => item.participants.length === 2), "装配模板必须且只能描述两个逻辑零件");
assert.ok(templates.every((item) => item.assemblyPath.length > 0), "装配模板必须包含实现步骤");
assert.ok(templates.every((item) => item.outputs.assemblyInstruction), "装配模板必须输出装配指导");
assert.ok(templates.every((item) => item.previewScene.designParts.length === 2), "每个装配模板必须给出两个逻辑零件的三维位置");
assert.ok(templates.every((item) => item.previewScene.manufacturingParts.length === item.outputs.manufacturingPartCount), "每个装配模板必须覆盖全部下料结果");
assert.ok(templates.every((item) => item.parameterDiagram?.schemaVersion === 2), "每个装配模板必须提供二维参数示意图");

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
assert.match(renderAssemblyLibraryLeftPane({}, view), /折弯/);
assert.match(renderAssemblyLibraryRightPane({}, view), /冷折展开/);
assert.doesNotMatch(renderAssemblyLibraryRightPane({}, view), /槽口参数/);
assert.doesNotMatch(renderAssemblyLibraryRightPane({}, view), /逻辑零件|下料归并|实现步骤|装配输出/);
assert.match(renderAssemblyLibraryRightPane({}, view), /恢复默认/);
const overlay = renderAssemblyLibraryViewportOverlay({}, view);
assert.match(overlay, /<strong>成品<\/strong>/);
assert.match(overlay, /<strong>下料<\/strong>/);
assert.match(overlay, /data-tube-assembly-finished-viewport/);
assert.match(overlay, /data-tube-assembly-blank-viewport/);
assert.doesNotMatch(overlay, /tube-designer-assembly-preview-mode/);
assert.match(overlay, /data-tube-assembly-diagram-dock/);
assert.match(renderAssemblyLibraryRightPane({}, view), /data-cam-change-action="tube-designer-assembly-parameter-change"/);

let renders = 0;
const ops = { renderProject() { renders += 1; } };
await handleAssemblyLibraryAction({}, view, "tube-designer-assembly-parameter-change", {
  value: "notched",
  dataset: { tubeAssemblyId: "bend", tubeAssemblyParameter: "bendMethod" },
}, ops);
assert.equal(assemblyParameterValues(view).bendMethod, "notched");
assert.match(renderAssemblyLibraryRightPane({}, view), /槽口参数/);
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
assert.match(edgeArcHtml, /槽口参数/);
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

const previewView = {
  activeAreaId: "assemblies",
  tubeDesignerAssemblyTemplates: templates,
  tubeDesignerAssemblyLibrary: { selectedId: "bend", showDiagram: false },
};
const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const invocations = [];
const previewContext = { sceneProxy: { resources: {}, async invoke(method, payload) {
  invocations.push({ method, payload });
  if (method === "TubeDesigner.ResolveAssemblyTemplatePreview") return {
    schema: "icax.assembly-preview-plan", templateId: "bend",
    designParts: ["a", "b"].map((id) => ({ id: `design-${id}`, label: id, request: { length: 100, features: [], ends: {} }, matrix: identity, compareMatrix: identity })),
    manufacturingParts: [{ id: "manufacturing-blank", label: "blank", request: { length: 200, features: [], ends: {} }, matrix: identity, compareMatrix: identity }],
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
assert.equal(assemblyFinishedRows(previewView.tubeDesignerAssemblyLibrary.preview).length, 2, "左侧成品视口必须显示两个逻辑零件");
assert.equal(assemblyBlankRows(previewView.tubeDesignerAssemblyLibrary.preview).length, 1, "右侧下料视口必须显示归并后的一个下料零件");
assert.equal(assemblyBlankRows(previewView.tubeDesignerAssemblyLibrary.preview)[0].data.geometry.url.startsWith("memory://result-"), true);

console.log("TubeDesignerAssemblyLibraryTest: passed");
