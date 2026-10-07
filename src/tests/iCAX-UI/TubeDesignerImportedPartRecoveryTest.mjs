import assert from "node:assert/strict";
import { handlePartDrawingAction } from "../../apps/tube-designer/webpage/partDrawing.mjs";
import { renderNestingViewportOverlay } from "../../apps/tube-designer/webpage/partsArea.mjs";

const profile = { schema: "icax.imported-tube-profile", schemaVersion: 1,
  kind: "imported-dxf", name: "圆管", width: 40, depth: 40,
  contours: [{ kind: "circle", radius: 20, center: [0, 0] },
    { kind: "circle", radius: 18, center: [0, 0] }] };
const original = { entityId: "imported-1", name: "导入管材", length: 800,
  independentNesting: true, manufacturingGeometryResourceId: "final",
  manufacturingGeometryResourceVersion: 7,
  properties: { "manufacturing.partKind": "tube", "manufacturing.imported": true,
    "tubeDesigner.profile": profile } };
const scene = { tubeDesigner: { nestingGroups: [{ parts: [original] }] } };
const recipe = { drawing: { schemaVersion: 1, length: 800,
  section: { source: "dxf", name: "圆管", profile } },
  baseLength: 800, features: [], ends: { start: { type: "keep" }, end: { type: "keep" } } };
const untouchedScene = structuredClone(scene);
const view = { activeAreaId: "nesting", scene,
  tubeDesignerActiveNestingPartId: original.entityId,
  tubeDesignerNestingSelectionKind: "part" };
const before = renderNestingViewportOverlay({}, view);
assert.doesNotMatch(before, /tube-designer-drawing-recover|tube-designer-punch-open/, "Imported CAD has no edit entry; explicit native recovery is still tested below");

const calls = [];
const context = { sceneProxy: { async invoke(method, payload, options) {
  calls.push({ method, payload, options });
  if(method==="TubeDesigner.RecoverImportedPartDrawing")
    return { partEntityId: original.entityId, resourceVersion: 7, ready: true };
  if(method==="TubeDesigner.GetPartDrawing")
    return { partEntityId: original.entityId, resourceVersion: 7, definition: recipe };
  throw new Error(`Unexpected method ${method}`);
} } };
const ops = { renderProject() {}, showNotice() {} };
await handlePartDrawingAction(context, view, "tube-designer-drawing-recover",
  { dataset: { tubeDesignerPartId: original.entityId } }, ops);
assert.equal(calls[0].method, "TubeDesigner.RecoverImportedPartDrawing");
assert.deepEqual(calls[0].payload,
  { partEntityId: original.entityId, resourceVersion: 7 });
assert.equal(calls[1].method,"TubeDesigner.GetPartDrawing");
assert.deepEqual(calls[1].payload,
  { partEntityId: original.entityId, resourceVersion: 7 });
assert.notEqual(view.pending, true);
assert.equal(view.tubeDesignerPartRecoveryPending, "");
assert.equal(view.tubeDesignerPartDrawing.part.entityId, original.entityId);
assert.equal(view.tubeDesignerPartDrawing.state.drawing.length, 800);
assert.doesNotMatch(renderNestingViewportOverlay({}, view), /tube-designer-drawing-recover/);

const failureView = { activeAreaId: "nesting", scene: untouchedScene };
await assert.rejects(() => handlePartDrawingAction(
  { sceneProxy: { async invoke() { throw new Error("复杂自由曲面暂不支持"); } } },
  failureView, "tube-designer-drawing-recover",
  { dataset: { tubeDesignerPartId: original.entityId } }, ops),
/三维特征识别失败：复杂自由曲面暂不支持/);
assert.equal(failureView.scene, untouchedScene);
assert.equal(failureView.tubeDesignerPartDrawing, undefined);
assert.notEqual(failureView.pending, true);
assert.equal(failureView.tubeDesignerPartRecoveryPending, "");

const detailFailureScene=structuredClone(untouchedScene);
const detailFailureView={ activeAreaId:"nesting", scene:detailFailureScene,
  tubeDesignerActiveNestingPartId:original.entityId };
await assert.rejects(() => handlePartDrawingAction(
  { sceneProxy: { async invoke(method) {
    if(method==="TubeDesigner.RecoverImportedPartDrawing")
      return {partEntityId:original.entityId,resourceVersion:7,ready:true};
    throw new Error("编辑定义读取失败");
  } } }, detailFailureView, "tube-designer-drawing-recover",
  { dataset: { tubeDesignerPartId: original.entityId } }, ops),
/三维特征识别失败：编辑定义读取失败/);
assert.equal(detailFailureView.scene,detailFailureScene);
assert.equal(detailFailureView.tubeDesignerPartDrawing,undefined);
assert.equal(detailFailureScene.tubeDesigner.nestingGroups[0].parts[0].properties["tubeDesigner.partDrawing"],undefined);

const switchedView = { activeAreaId: "nesting", scene: structuredClone(untouchedScene),
  tubeDesignerActiveNestingPartId: original.entityId };
let finishRequest;
const switchedWork = handlePartDrawingAction({ sceneProxy: { invoke() {
  return new Promise(resolve => { finishRequest = resolve; });
} } }, switchedView, "tube-designer-drawing-recover",
{ dataset: { tubeDesignerPartId: original.entityId } }, ops);
await new Promise(resolve => setTimeout(resolve, 0));
switchedView.tubeDesignerActiveNestingPartId = "another-part";
finishRequest({ partEntityId: original.entityId, resourceVersion: 7, ready: true });
await switchedWork;
assert.equal(switchedView.tubeDesignerPartDrawing, undefined,
  "Late recovery must not replace a newer editor selection");
console.log("Imported part recovery UI: action, response, error and original geometry preservation passed.");
