import assert from "node:assert/strict";
import {
  componentLibraryState, openComponentCSGEditor, renderComponentLibraryRightPane,
  handleComponentLibraryAction,
} from "../../apps/tube-designer/webpage/componentLibrary.mjs";
import { openPartDrawing, renderDrawingSection } from "../../apps/tube-designer/webpage/partDrawing.mjs";
import {
  handleNestingPunchPartAction, renderNestingPunchPartDialog,
} from "../../apps/tube-designer/webpage/nestingPunchPart.mjs";

const ops = { renderProject() {}, showNotice() {} };
const view = {};
openComponentCSGEditor(view);
const state = componentLibraryState(view);
const act = (suffix, target = {}) => handleComponentLibraryAction({}, view, `tube-designer-component-${suffix}`, target, ops);
const snapshot = () => state.csgDraft.features[0].profile.snapshot;
const annotation = parameter => snapshot().parameterDiagram.annotations.find(item => item.parameter === parameter);

for (const [key, expected] of [
  ["builtin:solid-rectangle", ["width", "depth"]],
  ["builtin:solid-circle", ["diameter"]],
  ["builtin:ring", ["outerDiameter", "innerDiameter"]],
  ["builtin:l-section", ["width", "depth", "thickness"]],
]) {
  await act("csg-profile-select", { value: key });
  assert.deepEqual(snapshot().parameterDiagram.annotations.map(item => item.parameter), expected);
  const html = renderComponentLibraryRightPane({}, view);
  assert.match(html, /data-profile-parameter-scope/);
  assert.match(html, /data-profile-parameter-diagram/);
  for (const parameter of expected) {
    assert.ok(html.includes(`data-profile-annotation-key="${parameter}"`));
    assert.ok(html.includes(`data-profile-parameter-key="${parameter}"`));
  }
}
await act("csg-profile-parameter-change", { value: "64", dataset: { csgProfileParameter: "width" } });
await act("csg-profile-parameter-change", { value: "8", dataset: { csgProfileParameter: "thickness" } });
assert.deepEqual(annotation("width").from, [-32, -20]);
assert.deepEqual(annotation("width").to, [32, -20]);
assert.deepEqual(annotation("thickness").from, [32, -20]);
assert.deepEqual(annotation("thickness").to, [32, -12]);
const savedFeature = structuredClone(state.csgDraft.features[0]);
delete savedFeature.profile.snapshot.parameterDiagram;
const contoursBefore = structuredClone(savedFeature.profile.snapshot.contours);
openComponentCSGEditor(view, { scope: "user", modelType: "csg", csgDefinition: { features: [savedFeature] } });
assert.deepEqual(snapshot().contours, contoursBefore, "Legacy annotation migration must not change stored geometry");
assert.deepEqual(annotation("thickness").to, [32, -12]);

const profileSnapshot = structuredClone(snapshot());
const libraryProfile = {
  id: "l-section", name: "L 形程式", profileType: "parametric-package",
  defaultParameters: structuredClone(profileSnapshot.parameters),
  descriptor: { parameters: structuredClone(profileSnapshot.parameterDefinitions) },
  previewProfile: profileSnapshot,
};
const drawingView = { tubeDesignerSystemProfiles: [libraryProfile] };
openPartDrawing(drawingView);
drawingView.tubeDesignerPartDrawing.state.draft.section = structuredClone(drawingView.tubeDesignerPartDrawing.state.drawing.section);
for (const section of ["main", "branch"]) {
  const html = renderDrawingSection(drawingView, section);
  assert.match(html, /data-profile-parameter-scope/);
  assert.match(html, /data-profile-parameter-key="thickness"/);
  assert.match(html, /data-profile-annotation-key="thickness"/);
}
drawingView.tubeDesignerPartDrawing.state.draft.section = {
  source: "dxf", name: "本地截面", profile: { ...profileSnapshot, editableParameters: false },
};
assert.doesNotMatch(renderDrawingSection(drawingView, "branch"), /data-profile-parameter-diagram/);

const punchView = { tubeDesignerSystemProfiles: [libraryProfile] };
const evaluations = [];
let previewCount = 0;
const context = {
  productProxy: { async invoke(method, payload) {
    assert.equal(method, "TubeDesigner.EvaluateProfilePackage");
    evaluations.push(payload);
    const current = structuredClone(profileSnapshot);
    current.parameters = structuredClone(payload.parameters);
    const width = payload.parameters.width;
    current.width = width;
    current.contours[0].points = current.contours[0].points.map(([x, y]) => [x < 0 ? -width / 2 + (x + 32) : width / 2, y]);
    current.parameterDiagram.annotations[0].from[0] = -width / 2;
    current.parameterDiagram.annotations[0].to[0] = width / 2;
    return { profile: current };
  } },
  sceneProxy: { async invoke() { previewCount++; return {toolsOnly:true,baseGeometry:{url:"memory://preview",version:1}}; } },
};
const punchAct = (suffix, target = {}) => handleNestingPunchPartAction(context, punchView, `tube-designer-nesting-punch-create-${suffix}`, target, ops);
await punchAct("open");
assert.equal(evaluations.length, 0, "An up-to-date snapshot needs no additional evaluation");
let html = renderNestingPunchPartDialog(punchView);
assert.match(html, /主管参数示意图/);
assert.match(html, /<details class="tube-designer-nesting-profile-diagram"><summary>主管参数示意图<\/summary>/);
assert.doesNotMatch(html, /支管参数示意图/, "Detailed branch dimensions belong in the parameter dialog, not the main spreadsheet");
assert.match(html, /data-profile-parameter-key="thickness"/);
await punchAct("parameters-open", { dataset: { tubeDesignerPunchIndex: "draft" } });
html = renderNestingPunchPartDialog(punchView);
assert.match(html, /data-punch-parameter-dialog/);
assert.match(html, /支管参数示意图/);
assert.match(html, /data-profile-parameter-key="thickness"/);
await punchAct("parameters-cancel");
await punchAct("main-profile-parameter", { value: "80", dataset: { tubeDesignerMainProfileParameter: "width", tubeDesignerMainProfileValueType: "number" } });
assert.equal(evaluations.length, 1);
assert.equal(punchView.tubeDesignerNestingPunchPartDraft.diagramProfile.parameters.width, 80);
html = renderNestingPunchPartDialog(punchView);
assert.match(html, /width/);
assert.equal(punchView.pending, false);
await punchAct("draft-change", { value: "1200", dataset: { tubeDesignerNestingPunchField: "length" } });
assert.equal(evaluations.length, 1, "A length-only edit must reuse the section diagram");
let resolveEvaluation;
let pendingEvaluations=0;
context.productProxy.invoke = () => new Promise(resolve => { pendingEvaluations++;resolveEvaluation = resolve; });
const previousPreviewCount = previewCount;
const pendingChange = punchAct("main-profile-parameter", { value: "96", dataset: { tubeDesignerMainProfileParameter: "width", tubeDesignerMainProfileValueType: "number" } });
assert.equal(punchView.pending, true);
assert.match(punchView.tubeDesignerOperation.title,/生成主管截面/);
await punchAct("main-profile-parameter", { value: "100", dataset: { tubeDesignerMainProfileParameter: "width", tubeDesignerMainProfileValueType: "number" } });
assert.equal(pendingEvaluations,1,"The next parameter change cannot start another backend request while waiting");
assert.equal(previewCount,previousPreviewCount,"3D preview must wait for the section response");
punchView.tubeDesignerNestingPunchPartDraft = null;
punchView.tubeDesignerPunchWizard = null;
resolveEvaluation({ profile: structuredClone(profileSnapshot) });
await pendingChange;
assert.equal(previewCount, previousPreviewCount, "A stale diagram request must not start a preview in a replaced or closed editor");
assert.equal(punchView.tubeDesignerNestingPunchPartDraft, null);
assert.equal(punchView.pending,false);
assert.equal(punchView.tubeDesignerOperation,null);

const failureView={tubeDesignerSystemProfiles:[libraryProfile]};
const failureContext={sceneProxy:{async invoke(){return {toolsOnly:true,baseGeometry:{url:"memory://initial",version:1}};}},
  productProxy:{async invoke(){throw new Error("截面参数不合法");}}};
await handleNestingPunchPartAction(failureContext,failureView,"tube-designer-nesting-punch-create-open",{},ops);
let failedStagePreviewCalls=0;
failureContext.sceneProxy.invoke=async()=>{failedStagePreviewCalls++;return {toolsOnly:true,baseGeometry:{url:"memory://unexpected",version:1}};};
await handleNestingPunchPartAction(failureContext,failureView,"tube-designer-nesting-punch-create-main-profile-parameter",
  {value:"999",dataset:{tubeDesignerMainProfileParameter:"width",tubeDesignerMainProfileValueType:"number"}},ops);
assert.equal(failedStagePreviewCalls,0,"A failed section stage must not send a second invalid request for 3D geometry");
assert.match(failureView.tubeDesignerNestingPunchPartDraft.diagramError,/参数不合法/);
assert.equal(failureView.pending,false);assert.equal(failureView.tubeDesignerOperation,null);
console.log("Profile diagram consumers: built-in dimensions, legacy CSG reopen, isolated main/branch sections, DXF exclusion and punch parameter refresh passed.");
