import assert from "node:assert/strict";
import { existsSync, readFileSync, readdirSync } from "node:fs";
import {
  assemblyParameterValues, assemblyPreviewKey, assemblyPreviewParameterGroups, assemblySceneParts,
  ensureAssemblyLibraryPreview, handleAssemblyLibraryAction, renderAssemblyLibraryRightPane,
} from "../../apps/tube-designer/webpage/assemblyLibrary.mjs";
import { createFinishedProduct, finishedProductInput, finishedProductKey, finishedProductShape } from "../../apps/tube-designer/webpage/finishedProductModel.mjs";
import { productAssemblyConnectionIdentity } from "../../apps/tube-designer/webpage/productAssemblyConnections.mjs";
import { productAssemblyBindingIdentity } from "../../apps/tube-designer/webpage/productAssemblyBindings.mjs";
import { renderFinishedProductEditor } from "../../apps/tube-designer/webpage/finishedProductEditor.mjs";

const load = (id) => JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/assembly/${id}/assembly.json`, import.meta.url), "utf8"));
const [miter, bend, sleeve, tabSlot] = ["two-end-end-angle", "bend", "insert-sleeve", "tab-slot-lock"].map(load);
const profiles = [{ id: "rect", name: "矩形管", profileForm: "parametric",
  defaultParameters: { width: 60, depth: 40, wallThickness: 2 }, descriptor: { parameters: [
    { key: "width", displayName: "宽度", valueType: "number", defaultValue: 60, min: 10 },
    { key: "depth", displayName: "高度", valueType: "number", defaultValue: 40, min: 10 },
  ] } }];
const userProfiles = [{ id: "my-ellipse", name: "我的椭圆管", profileForm: "parametric",
  defaultParameters: { width: 48, depth: 32 }, descriptor: { parameters: [
    { key: "width", displayName: "宽度", valueType: "number", defaultValue: 48, min: 10 },
    { key: "depth", displayName: "高度", valueType: "number", defaultValue: 32, min: 10 },
  ] } }];
const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: [miter, bend],
  tubeDesignerSystemProfiles: profiles, tubeDesignerUserData: { profiles: userProfiles },
  tubeDesignerAssemblyLibrary: { selectedId: miter.id, workMode: "example", workModeUserSelected: true,
    parameterDrafts: { [miter.id]: { fitGap: 2 } },
    processDrafts: { [miter.id]: { custom: { draft: { amount: 3 } } } } } };
const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const calls = [];
const context = { sceneProxy: { resources: {}, async invoke(method, payload) {
  calls.push({ method, payload: structuredClone(payload) });
  if (method === "TubeDesigner.CheckAssemblyTemplateApplicability") return {
    schema: "icax.assembly-applicability", schemaVersion: 1, templateId: payload.templateId,
    applicable: true, reason: "",
  };
  if (method === "TubeDesigner.ResolveAssemblyTemplatePreview" && payload.finishedOnly) return {
    schema: "icax.finished-product-preview", schemaVersion: 1, finishedProduct: structuredClone(payload.finishedProduct),
    layoutShape: payload.finishedProduct.shapeId, sceneParameters: payload.finishedProduct.parameters,
    designParts: Object.entries(payload.finishedProduct.spans).map(([id, span]) => ({ id: `design-${id}`, role: id,
      label: id, request: { ...structuredClone(span), features: [], ends: {} }, matrix: identity, compareMatrix: identity })),
  };
  if (method === "TubeDesigner.PreviewPunchWizard") return { previewComputed: true,
    baseGeometry: { url: "memory://product-base", version: 1 }, geometry: { url: "memory://cut", version: 1 } };
  throw new Error(`unexpected ${method}`);
} } };
const ops = { renderProject() {} };
const target = (span, extra = {}) => ({ dataset: { finishedShape: "l", finishedSpan: span, ...extra } });
const processBefore = structuredClone(view.tubeDesignerAssemblyLibrary.processDrafts);
const parametersBefore = assemblyPreviewParameterGroups(view, miter).parameters;
const product = finishedProductInput(view);
const productBefore = structuredClone(product);
const defaultParts = assemblySceneParts(view, miter);
assert.deepEqual(Object.keys(defaultParts), ["memberA", "memberB"]);
assert.equal(defaultParts.memberA.length, product.spans.armA.length);
const html = renderAssemblyLibraryRightPane(context, view);
assert.ok(html.includes("成品参数") && html.includes("工艺参数"));
assert.ok(html.indexOf('data-finished-parameter="angle"') < html.indexOf("工艺参数"));
assert.ok(html.indexOf('data-tube-assembly-parameter="fitGap"') > html.indexOf("工艺参数"));
assert.doesNotMatch(html, /data-tube-assembly-parameter="jointAngle"/);
assert.match(html, /我的椭圆管/);
const productEditor = renderFinishedProductEditor(view);
view.tubeDesignerAssemblyTemplates = [];
assert.equal(renderFinishedProductEditor(view), productEditor, "成品编辑器无需装配模板");
view.tubeDesignerAssemblyTemplates = [miter, bend];

const initialKey = assemblyPreviewKey(view, miter);
await handleAssemblyLibraryAction(context, view, "tube-designer-finished-length-change", { ...target("armB"), value: "310" }, ops);
await handleAssemblyLibraryAction(context, view, "tube-designer-finished-profile-change", { ...target("armB"), value: "user:my-ellipse" }, ops);
await handleAssemblyLibraryAction(context, view, "tube-designer-finished-profile-parameter-change",
  { ...target("armB", { finishedProfileParameter: "width" }), value: "72" }, ops);
assert.equal(product.spans.armB.length, 310);
assert.deepEqual(product.spans.armB.profileRef, { scope: "user", id: "my-ellipse" });
assert.deepEqual(product.spans.armB.parameters, { width: 72, depth: 32 });
assert.notEqual(assemblyPreviewKey(view, miter), initialKey);
assert.deepEqual(view.tubeDesignerAssemblyLibrary.processDrafts, processBefore);
assert.deepEqual(assemblyPreviewParameterGroups(view, miter).parameters, parametersBefore);
await handleAssemblyLibraryAction(context, view, "tube-designer-assembly-select", { dataset: { tubeAssemblyId: bend.id } }, ops);
assert.equal(finishedProductInput(view), product);
assert.equal(assemblySceneParts(view, bend).segmentB.length, 310);
assert.equal(assemblySceneParts(view, bend).segmentB.parameters.width, 72);
assert.ok(!Object.hasOwn(assemblySceneParts(view, bend), "memberB"), "工艺角色映射不能污染成品身份");
await handleAssemblyLibraryAction(context, view, "tube-designer-finished-parameter-change",
  { dataset: { finishedShape: "l", finishedParameter: "angle" }, value: "170" }, ops);
await handleAssemblyLibraryAction(context, view, "tube-designer-assembly-select", { dataset: { tubeAssemblyId: miter.id } }, ops);
assert.equal(product.parameters.angle, 170, "超出某工艺角度范围也不能改写成品角度");
assert.equal(calls.filter((call) => call.method === "TubeDesigner.GetAssemblyTemplateExampleProduct").length, 0);
const complete = async (value = view) => {
  for (let i = 0; i < 10; i += 1) { const pending = ensureAssemblyLibraryPreview(context, value); if (!pending?.promise) return; await pending.promise; }
  assert.fail("成品预览未完成");
};
await complete();
const finishedCall = calls.filter((call) => call.method === "TubeDesigner.ResolveAssemblyTemplatePreview").at(-1);
assert.equal(finishedCall.payload.finishedOnly, true);
assert.equal(finishedCall.payload.finishedProduct.parameters.angle, 170);
assert.ok(!Object.hasOwn(finishedCall.payload, "templateId") && !Object.hasOwn(finishedCall.payload, "parameters"));
assert.ok(!Object.hasOwn(finishedCall.payload, "sceneParts") && !Object.hasOwn(finishedCall.payload, "sceneParameters"));
const checkCall = calls.filter((call) => call.method === "TubeDesigner.CheckAssemblyTemplateApplicability").at(-1);
assert.ok(!checkCall.payload.finishedProduct && !checkCall.payload.processInput.shapeId);
assert.deepEqual(checkCall.payload.processInput.parts.memberB.profileRef, product.spans.armB.profileRef);
assert.deepEqual(checkCall.payload.processInput.parts.memberB.parameters, product.spans.armB.parameters);
assert.equal(checkCall.payload.processInput.parts.memberB.length, product.spans.armB.length);
assert.deepEqual(checkCall.payload.parameters, parametersBefore);
await handleAssemblyLibraryAction(context, view, "tube-designer-assembly-reset", { dataset: { tubeAssemblyId: miter.id } }, ops);
assert.equal(finishedProductInput(view), product, "重置工艺不能重置独立成品");
await handleAssemblyLibraryAction(context, view, "tube-designer-finished-reset", {}, ops);
assert.deepEqual(finishedProductInput(view), productBefore);

const sparseView = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: [miter], tubeDesignerSystemProfiles: profiles,
  tubeDesignerAssemblyLibrary: { selectedId: miter.id, workMode: "example", workModeUserSelected: true } };
const sparseBefore = structuredClone(finishedProductInput(sparseView));
await handleAssemblyLibraryAction(context, sparseView, "tube-designer-finished-profile-parameter-change",
  { ...target("armA", { finishedProfileParameter: "width" }), value: "73" }, ops);
assert.deepEqual(finishedProductInput(sparseView), { ...sparseBefore, spans: { ...sparseBefore.spans,
  armA: { ...sparseBefore.spans.armA, parameters: { ...sparseBefore.spans.armA.parameters, width: 73 } } } },
"单个截面参数编辑保留完整独立成品，而非冻结或生成模板覆盖值");

const coaxView = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: [tabSlot, sleeve], tubeDesignerSystemProfiles: profiles,
  tubeDesignerAssemblyLibrary: { selectedId: tabSlot.id, workMode: "example", workModeUserSelected: true } };
const coaxProduct = finishedProductInput(coaxView, "straight");
coaxView.tubeDesignerFinishedProduct.selectedShapeId = "straight";
coaxProduct.spans.first.length = 330;
const coaxBefore = structuredClone(coaxProduct);
await handleAssemblyLibraryAction(context, coaxView, "tube-designer-assembly-select", { dataset: { tubeAssemblyId: sleeve.id } }, ops);
assert.deepEqual(finishedProductInput(coaxView), coaxBefore,
"切换套接工艺不能把方管成品替换为模板偏好的圆管或长度");
assert.equal(assemblySceneParts(coaxView, sleeve).insertPart.parameters.width, coaxBefore.spans.first.parameters.width);
await handleAssemblyLibraryAction(context, coaxView, "tube-designer-assembly-parameter-change",
  { dataset: { tubeAssemblyId: sleeve.id, tubeAssemblyParameter: "connectionMode" }, value: "telescopic" }, ops);
assert.equal(assemblyPreviewParameterGroups(coaxView, sleeve).parameters.connectionMode, "telescopic");
assert.deepEqual(finishedProductInput(coaxView), coaxBefore);

const tTemplates = [load("t-contact-fit"), load("end-side-tab-slot"), load("t-profile-insert")];
const sharedT = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: tTemplates,
  tubeDesignerAssemblyLibrary: { selectedId: tTemplates[0].id, workMode: "example", workModeUserSelected: true } };
const tProduct = finishedProductInput(sharedT, "t");
sharedT.tubeDesignerFinishedProduct.selectedShapeId = "t";
tProduct.spans.main.length = 475; tProduct.spans.branch.length = 235;
const tBefore = finishedProductKey(tProduct);
for (const template of tTemplates) {
  await handleAssemblyLibraryAction(context, sharedT, "tube-designer-assembly-select", { dataset: { tubeAssemblyId: template.id } }, ops);
  assert.equal(finishedProductKey(finishedProductInput(sharedT)), tBefore);
  const parts = assemblySceneParts(sharedT, template);
  for (const [role, span] of Object.entries(template.exampleInput.roles)) assert.deepEqual(parts[role], tProduct.spans[span]);
}
const allRoot = new URL("../../apps/tube-designer/templates/assembly/", import.meta.url);
for (const entry of readdirSync(allRoot, { withFileTypes: true }).filter((item) => item.isDirectory()
    && existsSync(new URL(`${item.name}/assembly.json`, allRoot)))) {
  const current = load(entry.name);
  assert.ok(!current.parameters.some((item) => item.scope === "scene"), `${entry.name} 不得声明成品参数`);
  assert.ok(!Object.hasOwn(current.previewScene, "designParts"), `${entry.name} 不得声明成品几何`);
  const sampleView = { tubeDesignerAssemblyTemplates: [current] };
  const sample = createFinishedProduct(current.exampleInput.shapeId);
  sampleView.tubeDesignerFinishedProduct = { selectedShapeId: sample.shapeId, drafts: { [sample.shapeId]: sample } };
  assert.ok(!JSON.stringify(assemblySceneParts(sampleView, current)).includes("$"));
  assert.equal(finishedProductShape(sample.shapeId).spans.length, Object.keys(current.exampleInput.roles).length);
}

const productView = { tubeDesignerAssemblyTemplates: [miter],
  tubeDesignerAssemblyLibrary: { selectedId: miter.id, workMode: "product", workModeUserSelected: true },
  scene: { tubeDesigner: { activeProductId: "product", generationRun: { entityId: "run" } } } };
assert.ok(!renderAssemblyLibraryRightPane(context, productView).includes("data-finished-product-editor"),
"已生成产品节点使用其真实成品输入");
const actualT = tTemplates[2];
const productT = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: [actualT],
  tubeDesignerAssemblyLibrary: { selectedId: actualT.id, workMode: "product", workModeUserSelected: true,
    selectedProductConnectionProductKey: "product-t/run-t", selectedProductConnectionKey: "node-t",
    selectedProductTemplateConnectionKey: "node-t", selectedProductTemplateId: actualT.id },
  scene: { tubeDesigner: { activeProductId: "product-t", generationRun: { entityId: "run-t" } } },
  tubeDesignerAssemblyProductConnections: { key: "product-t/run-t/false/false", status: "ready",
    result: { productEntityId: "product-t", generationRunId: "run-t", modelOutdated: false,
      connections: [{ key: "node-t", properties: { topology: "T" }, participants: [
        { memberEntityId: "host-t" }, { memberEntityId: "branch-t" },
      ], nodeGeometry: { nodeGeometryStatus: "verified", axisAngleDeg: 75 } }] } },
  tubeDesignerProductAssemblyBindings: { key: "product-t/run-t/false/", productKey: "product-t/run-t",
    status: "ready", result: { members: [], bindings: [], capabilities: { anchorKinds: [], templates: [] },
      connections: [] }, drafts: {}, mutation: null, notice: "" } };
productT.tubeDesignerAssemblyProductConnections.key = productAssemblyConnectionIdentity(productT).key;
productT.tubeDesignerProductAssemblyBindings.key = productAssemblyBindingIdentity(productT).key;
productT.tubeDesignerProductAssemblyBindings.result.connections = productT.tubeDesignerAssemblyProductConnections.result.connections;
assert.equal(assemblyParameterValues(productT, actualT).intersectionAngle, 75,
  "产品节点角度必须读取已核验的成品几何");
assert.match(renderAssemblyLibraryRightPane(context, productT), /成品节点轴夹角：75°/);
assert.doesNotMatch(renderAssemblyLibraryRightPane(context, productT), /data-tube-assembly-parameter="intersectionAngle"/);
await handleAssemblyLibraryAction(context, productT, "tube-designer-assembly-parameter-change",
  { dataset: { tubeAssemblyId: actualT.id, tubeAssemblyParameter: "intersectionAngle" }, value: "90" }, ops);
assert.equal(assemblyParameterValues(productT, actualT).intersectionAngle, 75,
  "伪造工艺编辑事件不能覆盖已提交成品的轴夹角");
delete productT.tubeDesignerAssemblyProductConnections.result.connections[0].nodeGeometry.axisAngleDeg;
assert.equal(assemblyParameterValues(productT, actualT).intersectionAngle, null,
  "成品轴夹角未核验时不能默默回退为工艺模板默认值");
const unverifiedCalls = [];
await handleAssemblyLibraryAction({ sceneProxy: { invoke(method, payload) {
  unverifiedCalls.push({ method, payload }); return Promise.resolve({});
} } }, productT, "tube-designer-binding-preview", {}, ops);
assert.deepEqual(unverifiedCalls, [], "未核验成品几何不能发起产品加工预览");
assert.match(productT.tubeDesignerProductAssemblyBindings.drafts[actualT.id].previewError, /轴夹角未核验/);
console.log("assembly scene parameters passed: independent product editor, immutable complete inputs and process drafts");
