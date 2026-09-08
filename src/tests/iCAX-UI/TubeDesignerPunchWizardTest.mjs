import assert from "node:assert/strict";
import {
  createPunchWizardState,
  normalizePunchFeature,
  punchWizardProfileInfo,
  readPunchWizardFeatures,
  resolvePunchDistribution,
  renderPunchWizardDialog as renderPunchWizardView,
  validatePunchFeature,
} from "../../apps/tube-designer/webpage/punchWizard.mjs";
import { handlePartsAreaAction, renderNestingRightPane, renderPunchWizardDialog } from "../../apps/tube-designer/webpage/partsArea.mjs";
import { buildPunchPreviewRows } from "../../apps/tube-designer/webpage/punchEditor.mjs";

const part = {
  entityId: "nest-part-1",
  name: "横梁",
  partNumber: "P-001",
  length: 1200,
  independentNesting: true,
  profile: { kind: "rect", displayName: "矩形管", specification: "40 × 20 × 1.5", width: 40, depth: 20, wallThickness: 1.5 },
  properties: { "manufacturing.partKind": "tube" },
};

const view = {
  activeAreaId: "nesting",
  pending: false,
  scene: { tubeDesigner: { nestingGroups: [{ productEntityId: "batch", parts: [part] }] } },
};
const operations = { renderProject() {} };

// Reopening a saved thumbnail must use the original blank's midpoint before
// any native preview request, including a straight tube with zero holes.
const savedStraight = { ...part, thumbnailGeometryResourceId: "saved-straight", thumbnailGeometryResourceVersion: 4 };
const straightState = createPunchWizardState(savedStraight);
assert.equal(straightState.features.length, 0);
assert.equal(straightState.preview.isOriginal, true);
assert.equal(straightState.preview.length, 1200);
for (const mode of ["tools", "result"]) {
  assert.deepEqual(buildPunchPreviewRows(straightState.preview, mode)[0].data.localToWorldMatrix.map(value => value === 0 ? 0 : value),
    [1,0,0,-600, 0,1,0,0, 0,0,1,0, 0,0,0,1]);
}
const savedEndCut = { ...savedStraight, length: 900, properties: { ...part.properties,
  "tubeDesigner.punchWizard": { baseLength: 1200, features: [], ends: { start: { type: "end-miter", trim: 300 }, end: { type: "keep" } } },
} };
const endCutState = createPunchWizardState(savedEndCut);
assert.equal(endCutState.baseLength, 1200);
assert.equal(endCutState.preview.length, 1200);
assert.equal(buildPunchPreviewRows(endCutState.preview)[0].data.localToWorldMatrix[3], -600,
  "A shortened end-cut part must not recenter around the shorter finished length");
assert.deepEqual(endCutState.ends, savedEndCut.properties["tubeDesigner.punchWizard"].ends);
assert.equal(createPunchWizardState({ thumbnailGeometryResourceId: "unknown-length" }).preview.length, 0,
  "The editor's default 1000 mm is not evidence for the origin of an unknown saved geometry");

assert.deepEqual(punchWizardProfileInfo(part), { round: false, width: 40, depth: 20, diameter: 0 });
assert.equal(validatePunchFeature(part, { type: "circle", face: "top", station: 600, offset: 0, diameter: 10 }), "");
// Geometric footprint checks use the actual native BRep, not a guessed box face.
assert.equal(validatePunchFeature(part, { type: "circle", face: "top", station: 600, offset: 18, diameter: 10 }), "");
assert.match(validatePunchFeature(part, { type: "circle", station: 600, arrayCount: 2, arrayPitch: 0 }), /不能为零/);
assert.match(validatePunchFeature(part, { type: "slot", face: "top", station: 600, spanAlong: 10, spanAcross: 12 }), /长度必须大于宽度/);

const existingPart = {
  ...part,
  properties: {
    ...part.properties,
    "tubeDesigner.punchWizard": {
      features: [{ type: "circle", face: "top", station: 320, diameter: 8, arrayCount: 2, arrayPitch: 100 }],
    },
  },
};
assert.equal(readPunchWizardFeatures(existingPart)[0].arrayCount, 2);
assert.equal(createPunchWizardState(existingPart).features.length, 1);
assert.equal(normalizePunchFeature({ type: "unknown", arrayCount: 999 }).type, "unknown");
assert.match(validatePunchFeature(part,{type:"unknown",station:600}),/无法识别/);

await handlePartsAreaAction({}, view, "tube-designer-punch-open", { dataset: { tubeDesignerPartId: part.entityId } }, operations);
assert.equal(view.tubeDesignerPunchWizard.partId, part.entityId);
const existingDialog = renderPunchWizardDialog({}, view);
assert.match(existingDialog, /主管冲孔向导/);
assert.match(existingDialog, /主管 · 横梁/);
assert.match(existingDialog, /冲孔记录/);
assert.match(existingDialog, /孔刀拉伸体/);
assert.match(existingDialog, /刀具、位置姿态、多组阵列分别设置/);
assert.match(existingDialog, /左端面/);
assert.match(existingDialog, /右端面/);
assert.doesNotMatch(existingDialog, /data-tube-designer-punch-row="draft"/);
assert.match(existingDialog, /暂无冲孔记录，点击“＋ 添加行”开始/);
assert.match(existingDialog, /data-cam-action="tube-designer-punch-add"[^>]*data-tube-designer-punch-new/);
assert.match(existingDialog, /删除所选/);
assert.match(existingDialog, /两端独立设置，不参与孔数与阵列/);
assert.match(existingDialog, /data-cam-viewcube/);
assert.doesNotMatch(existingDialog, /class="tube-designer-punch-editor\b/);
assert.ok(existingDialog.indexOf('class="tube-designer-punch-sheet-scene"') < existingDialog.indexOf('class="tube-designer-punch-sheet"'));
assert.match(renderPunchWizardView(part, view), /应用到零件/);

assert.deepEqual(
  (({station,arrayPitch,reference})=>({station,arrayPitch,reference}))(
    resolvePunchDistribution({arrayCount:4,distributionMode:"end-margins",headMargin:100,tailMargin:200},1200)),
  {station:100,arrayPitch:300,reference:"start"},
);

view.tubeDesignerPunchWizard.draft = { ...view.tubeDesignerPunchWizard.draft, station: 600, diameter: 8, arrayCount: 2, arrayPitch: 100 };
await handlePartsAreaAction({}, view, "tube-designer-punch-add", {}, operations);
assert.equal(view.tubeDesignerPunchWizard.features.length, 1);
assert.equal(view.tubeDesignerPunchWizard.features[0].arrayCount, 2);
await handlePartsAreaAction({}, view, "tube-designer-punch-field-change", {
  value: "640", dataset: { tubeDesignerPunchField: "station", tubeDesignerPunchIndex: "0" },
}, operations);
assert.equal(view.tubeDesignerPunchWizard.features[0].station, 640);
assert.equal(view.tubeDesignerPunchWizard.selectedFeatureId, view.tubeDesignerPunchWizard.features[0].id);
await handlePartsAreaAction({}, view, "tube-designer-punch-selection-change", {
  checked: true, dataset: { tubeDesignerPunchIndex: "0" },
}, operations);
assert.deepEqual(view.tubeDesignerPunchWizard.selectedFeatureIds, [view.tubeDesignerPunchWizard.features[0].id]);
await handlePartsAreaAction({}, view, "tube-designer-punch-remove-selected", {}, operations);
assert.equal(view.tubeDesignerPunchWizard.features.length, 0);
assert.deepEqual(view.tubeDesignerPunchWizard.selectedFeatureIds, []);
view.tubeDesignerPunchWizard.draft = { ...view.tubeDesignerPunchWizard.draft, station: 600, diameter: 8, arrayCount: 2, arrayPitch: 100 };
await handlePartsAreaAction({}, view, "tube-designer-punch-add", {}, operations);
await handlePartsAreaAction({}, view, "tube-designer-punch-remove", { dataset: { tubeDesignerPunchIndex: "0" } }, operations);
assert.equal(view.tubeDesignerPunchWizard.features.length, 0);

view.tubeDesignerPunchWizard.draft = { ...view.tubeDesignerPunchWizard.draft, station: 600, diameter: 8, arrayCount: 1 };
await handlePartsAreaAction({}, view, "tube-designer-punch-add", {}, operations);
const calls = [];
const context = {
  sceneProxy: {
    async invoke(method, request) {
      calls.push({ method, request });
      return { tubeDesigner: view.scene.tubeDesigner };
    },
  },
  actions: { async refreshActiveSceneState() {} },
};
await handlePartsAreaAction(context, view, "tube-designer-punch-apply", { dataset: { tubeDesignerPartId: part.entityId } }, operations);
assert.equal(calls[0].method, "TubeDesigner.ApplyPunchWizard");
assert.equal(calls[0].request.features.length, 1);
assert.equal(view.tubeDesignerPunchWizard, null);
assert.equal(view.pending, false);

assert.match(renderNestingRightPane({}, view), /tube-designer-part-open-sketch/);
view.tubeDesignerPartMeasurementState = {
  key: `${part.entityId}@0`,
  status: "ready",
  report: {
    length: 1200,
    holes: [],
    sideProjection: {
      width: 1200, height: 20,
      outline: [[0, 0], [1200, 0], [1200, 20], [0, 20]],
      segments: [],
    },
  },
};
await handlePartsAreaAction({}, view, "tube-designer-part-open-sketch", {
  dataset: { tubeDesignerPartId: part.entityId },
}, operations);
assert.equal(view.tubeDesignerSketchDialogOpen, true);
assert.equal(view.tubeDesignerSketch.sideTargetKind, "part");
assert.equal(view.tubeDesignerSketch.targetPartId, part.entityId);

console.log("Punch wizard state, validation, rendering, array editing and native apply dispatch checks passed.");
