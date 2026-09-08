import assert from "node:assert/strict";
import {
  handleNestingPunchPartAction,
  renderNestingPunchPartDialog,
  NEW_PART_ID,
} from "../../apps/tube-designer/webpage/nestingPunchPart.mjs";
import { buildPunchPreviewRows } from "../../apps/tube-designer/webpage/punchEditor.mjs";

const profile = {
  id: "round",
  name: "系统圆管",
  profileType: "parametric-package",
  descriptor: { parameters: [
    { key: "width", displayName: "外径", valueType: "number", defaultValue: 40, unit: "mm" },
    { key: "wallThickness", displayName: "壁厚", valueType: "number", defaultValue: 2, unit: "mm" },
  ] },
  defaultParameters: { width: 40, wallThickness: 2 },
  previewProfile: {
    kind: "round", name: "系统圆管", width: 40, depth: 40, diameter: 40,
    specification: "Φ40 × 2", contours: [{ kind: "circle", radius: 20 }, { kind: "circle", radius: 18 }],
  },
};
const view = {
  activeAreaId: "nesting",
  pending: false,
  tubeDesignerSystemProfiles: [profile],
  tubeDesignerNestingSelectedPartIds: [],
};
const operations = { renderProject() {}, showNotice() {} };
const previewCalls = [];
const previewContext = {
  sceneProxy: {
    async invoke(method, request) {
      previewCalls.push({ method, request });
      return {
        toolsOnly:true,
        baseGeometry: { url: "memory://main-tube", version: previewCalls.length },
        baseMaterial: { url: "memory://main-tube-material", version: 1 },
        ...(request.features?.length ? {
          toolGeometry: { url: "memory://punch-tools", version: previewCalls.length },
          toolMaterial: { url: "memory://punch-tool-material", version: 1 },
          toolCount: request.features.length,
        } : { toolCount: 0 }),
        baseBounds: { width: 900 },
      };
    },
  },
};

await handleNestingPunchPartAction(previewContext, view, "tube-designer-nesting-punch-create-open", {}, operations);
assert.equal(view.tubeDesignerPunchWizard.partId, NEW_PART_ID);
assert.equal(previewCalls[0].method, "TubeDesigner.PreviewPunchWizard");
assert.equal(previewCalls[0].request.profileRef.id, "round");
assert.equal(previewCalls[0].request.length, 1000);
assert.deepEqual(previewCalls[0].request.features, []);
assert.equal(view.tubeDesignerPunchWizard.preview.baseGeometry.url, "memory://main-tube");
let dialog = renderNestingPunchPartDialog(view);
assert.match(dialog, /新建主管冲孔件/);
assert.match(dialog, /主管与刀具场景/);
assert.match(dialog, /选择管型/);
assert.match(dialog, /外径/);
assert.match(dialog, /壁厚/);
assert.match(dialog, /周向角度/);
assert.match(dialog, /先创建主管/);
assert.match(dialog, /之后再从零件详情进入冲孔向导追加孔位/);
assert.match(dialog, /孔刀拉伸体/);
assert.match(dialog, /两端独立设置，不参与孔数与阵列/);
assert.match(dialog, /刀具、位置姿态、多组阵列分别设置/);
assert.match(dialog, /左端面/);
assert.match(dialog, /右端面/);
assert.match(dialog, /编辑形状/);
assert.match(dialog, /编辑位置 \/ 姿态/);
assert.match(dialog, /编辑阵列/);
assert.match(dialog, /长度 · 直线 · 圆周可组合/);
assert.match(dialog, /data-cam-viewcube/);
assert.doesNotMatch(dialog, /class="tube-designer-punch-editor\b/);
assert.ok(dialog.indexOf('class="tube-designer-punch-sheet-scene"') < dialog.indexOf('class="tube-designer-punch-sheet"'));
assert.equal(view.tubeDesignerPunchWizard.draft.recordKind,"branch");
assert.equal(view.tubeDesignerPunchWizard.draft.section.key,"system:round");

await handleNestingPunchPartAction(previewContext,view,"tube-designer-nesting-punch-create-main-profile-parameter",{
  value:"50",dataset:{tubeDesignerMainProfileParameter:"width",tubeDesignerMainProfileValueType:"number"},
},operations);
assert.equal(previewCalls.at(-1).request.parameters.width,50);

const dxfContext={...previewContext,
  appProxy:{bridge:{async openFileDialog(){return "C:\\fixtures\\custom-branch.dxf";}}},
  productProxy:{async invoke(method){assert.equal(method,"TubeDesigner.ImportProfileDxf");return {profile:{name:"custom-branch",contours:[{kind:"circle",radius:12}]}};}},
};
await handleNestingPunchPartAction(dxfContext,view,"tube-designer-nesting-punch-create-profile-select",{
  value:"__dxf__",dataset:{tubeDesignerPunchIndex:"draft"},
},operations);
assert.equal(view.tubeDesignerPunchWizard.draft.recordKind,"dxf");
assert.equal(view.tubeDesignerPunchWizard.draft.section.source,"dxf");

await handleNestingPunchPartAction(previewContext, view, "tube-designer-nesting-punch-create-record-kind-change", {
  value: "tool", dataset: { tubeDesignerPunchIndex: "draft" },
}, operations);
assert.ok(view.tubeDesignerPunchWizard.parameterEditor);
await handleNestingPunchPartAction(previewContext,view,"tube-designer-nesting-punch-create-parameters-apply",{},operations);
assert.equal(view.tubeDesignerPunchWizard.parameterEditor,null);

await handleNestingPunchPartAction(previewContext, view, "tube-designer-nesting-punch-create-field-change", {
  value: "420", dataset: { tubeDesignerPunchField: "station", tubeDesignerPunchIndex: "draft" },
}, operations);
assert.equal(previewCalls.at(-1).request.features.length, 1);
assert.equal(previewCalls.at(-1).request.features[0].station, 420);
assert.equal(view.tubeDesignerPunchWizard.features.length, 0);
assert.equal(view.tubeDesignerPunchWizard.preview.includesDraft, true);
assert.equal(view.tubeDesignerPunchWizard.preview.toolGeometry.url, "memory://punch-tools");
const previewRows = buildPunchPreviewRows(view.tubeDesignerPunchWizard.preview);
assert.deepEqual(previewRows.map((row) => row.entityId), ["punch-preview-blank", "punch-preview-tools"]);
assert.equal(previewRows[0].data.material.url, "memory://main-tube-material");
assert.equal(previewRows[1].data.material.url, "memory://punch-tool-material");
assert.equal(previewRows[0].data.localToWorldMatrix[3], -450);
assert.deepEqual(previewRows[0].data.localToWorldMatrix, previewRows[1].data.localToWorldMatrix);
dialog = renderNestingPunchPartDialog(view);
assert.match(dialog, /当前刀具位置已更新.*确认时才计算切除/);

await handleNestingPunchPartAction({}, view, "tube-designer-nesting-punch-create-draft-change", {
  value: "900", dataset: { tubeDesignerNestingPunchField: "length" },
}, operations);
view.tubeDesignerPunchWizard.draft = {
  ...view.tubeDesignerPunchWizard.draft, station: 450, diameter: 8, face: "round",
};
assert.equal(view.tubeDesignerPunchWizard.features.length, 0);

const calls = [];
const context = {
  sceneProxy: {
    async invoke(method, request) {
      calls.push({ method, request });
      return { tubeDesigner: { nestingGroups: [] }, partEntityId: "new-part" };
    },
  },
  actions: { async refreshActiveSceneState() {} },
};
await handleNestingPunchPartAction(context, view, "tube-designer-nesting-punch-create-apply", {}, operations);
const createCall = calls.find((call) => call.method === "TubeDesigner.AddNestingPunchPart");
assert.equal(createCall.method, "TubeDesigner.AddNestingPunchPart");
assert.equal(createCall.request.profileRef.id, "round");
assert.equal(createCall.request.length, 900);
assert.deepEqual(createCall.request.features, []);
assert.equal(view.tubeDesignerNestingPunchPartDraft, null);
assert.equal(view.tubeDesignerPunchWizard, null);

console.log("TubeDesigner nesting punch-part creation flow passed.");
