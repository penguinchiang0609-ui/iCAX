import assert from "node:assert/strict";
import { beginNewPartSideSketch, handleSketchAreaAction,
  renderSectionSketchDialog, validateSideSketchDraft } from "../../apps/tube-designer/webpage/sketchArea.mjs";
import { handleNestingStandardPartRibbonCommand, handleNestingStandardPartAction,
  renderNestingStandardPartDialog } from "../../apps/tube-designer/webpage/nestingStandardPart.mjs";
import { canEditNestingSideSketch, handlePartsAreaAction } from "../../apps/tube-designer/webpage/partsArea.mjs";

const profile = { id: "rectangle", name: "矩形管", width: 40, depth: 20,
  contours: [{ kind: "polygon", points: [[-20,-10],[20,-10],[20,10],[-20,10]] }] };
const reference = { available: true, profile, length: 300, unfolding: { available: true,
  method: "section-arc-length", length: 300, perimeter: 120,
  surfaces: [{ inner: false, uPeriod: 120, rectangle: { available: true, width: 300, height: 120 },
    wires: [40, 60, 100].map(u => ({ role: "profile-junction", points: [[0,u],[300,u]] })) }] },
  preview: { baseGeometry: { url: "preview:base", version: 1 }, baseBounds: { min:[0,-20,-10], max:[300,20,10] } } };
const circle = { id: "seam", kind: "circle", cx: 122, cy: 80, radius: 6, closed: true };
const line = { id: "slit", kind: "line", x1: 115, y1: 20, x2: 125, y2: 45 };
const multi = { id: "multiple-laps", kind: "rectangle", x: -247, y: -15, width: 614, height: 45, closed: true };
function harness() {
  const calls = [];
  const view = { activeAreaId: "nesting", scene: { tubeDesigner: { nestingGroups: [] } },
    tubeDesignerSystemProfiles: [profile], tubeDesignerUserData: { profiles: [] } };
  const context = { sceneProxy: { async invoke(method, payload) {
    calls.push({ method, payload: structuredClone(payload) });
    if (method === "TubeDesigner.PreviewNestingSideSketchPart") return structuredClone(reference);
    if (method === "TubeDesigner.AddNestingSideSketchPart") {
      const part = { entityId: "created", independentNesting: true, length: payload.length,
        manufacturingGeometryResourceVersion: 7, profile,
        properties: { "tubeDesigner.sideSketch": { ...payload.sketch, targetPartId: "created" },
          "tubeDesigner.sideSketchRecipe": { profileRef: payload.profileRef, parameters: payload.parameters, length: payload.length } } };
      return { partEntityId: "created", tubeDesigner: { nestingGroups: [{ parts: [part] }] } };
    }
    if (method === "TubeDesigner.SavePartSketch") return { tubeDesigner: view.scene.tubeDesigner };
    if (method === "TubeDesigner.ReleaseNestingSideSketchPreview") return {};
    throw new Error(`Unexpected method ${method}`);
  } }, actions: { async selectRibbonTab() {}, async refreshActiveSceneState() {} } };
  const ops = { renderProject() {}, showNotice() {} };
  return { view, context, ops, calls };
}

{
  const shape = { id: "editable-validation", kind: "polyline", closed: false,
    points: [[5, 5], [15, 5], [15, 15]] };
  const draft = { coordinateSpace: "arc-length-axial", entities: [shape] };
  assert.equal(validateSideSketchDraft(draft, { length: 300 }, reference).ready, true);
  shape.points = [[5, 5], [15, 15], [5, 15], [15, 5]];
  assert.match(validateSideSketchDraft(draft, { length: 300 }, reference).message, /自相交/,
    "An in-place edit must invalidate the previous geometry check");
  shape.points = [[5, 5], [15, 5], [15, 15]];
  assert.equal(validateSideSketchDraft(draft, { length: 300 }, reference).ready, true,
    "Undoing an invalid edit must immediately restore a valid result");
  shape.points[2][1] = 350;
  assert.match(validateSideSketchDraft(draft, { length: 300 }, reference).message, /自动截断/,
    "Bounds must update after a nested point is moved");
}

{
  const h = harness();
  await handleNestingStandardPartRibbonCommand(h.context, h.view, "nesting.draw-2d-part", h.ops);
  const html = renderNestingStandardPartDialog(h.view);
  assert.match(html, /二维绘制零件/);
  assert.match(html, /开始绘制/);
  assert.doesNotMatch(html, /导入 DXF/);
  h.view.tubeDesignerNestingStandardPartDraft.length = "300";
  await handleNestingStandardPartAction(h.context, h.view, "tube-designer-nesting-standard-confirm", null, h.ops);
  assert.equal(h.view.tubeDesignerSketchDialogOpen, true);
  assert.equal(h.view.tubeDesignerNestingStandardPartDraft, null);
  assert.deepEqual(h.calls.map(c => c.method), ["TubeDesigner.PreviewNestingSideSketchPart"]);
  assert.ok(h.calls[0].payload.previewResourceKey);
  const state = h.view.tubeDesignerSketch;
  assert.equal(state.mode, "side");
  assert.equal(state.sideReference.unfolding.perimeter, 120);
  const draft = state.sideByPart[state.targetPartId];
  draft.entities = [circle, line, multi]; draft.dirty = true; draft.trajectoryWidth = 0.8;
  assert.equal(validateSideSketchDraft(draft, state.sideTargetSnapshot, reference).ready, true);
  assert.match(validateSideSketchDraft(draft, state.sideTargetSnapshot, reference).message,/自动截断/);
  const editor = renderSectionSketchDialog(h.context, h.view);
  assert.match(editor, /tube-sketch-side-profile-junction/);
  assert.match(editor, /data-tube-sketch-profile-u="40"/);
  assert.match(editor, /蓝色虚线：截面曲线段交接/);
  assert.match(editor, /data-tube-sketch-periodic-copy/);
  assert.match(editor, /value="0.8"/);
  await handleSketchAreaAction(h.context, h.view, "tube-designer-sketch-commit", null, h.ops);
  assert.equal(h.calls.filter(c => c.method === "TubeDesigner.AddNestingSideSketchPart").length, 1);
  const payload = h.calls.find(c => c.method === "TubeDesigner.AddNestingSideSketchPart").payload;
  assert.equal(payload.sketch.trajectoryWidth, 0.8);
  assert.equal(payload.sketch.perimeter, 120);
  assert.equal(payload.sketch.coordinateSpace, "arc-length-axial");
  assert.deepEqual(payload.sketch.entities, [circle, line, multi], "Multiple periods and axial overflow must be saved without clipping the source drawing");
  assert.equal(payload.previewResourceKey, undefined);
  assert.equal(h.view.tubeDesignerActiveNestingPartId, "created");
  assert.equal(h.view.tubeDesignerSketchDialogOpen, false);
  const part = h.view.scene.tubeDesigner.nestingGroups[0].parts[0];
  assert.equal(canEditNestingSideSketch(part), true);
  assert.equal(canEditNestingSideSketch({ ...part, independentNesting: false }), false);
  assert.equal(canEditNestingSideSketch({ ...part, properties: {} }), false);
  await handlePartsAreaAction(h.context, h.view, "tube-designer-part-open-sketch", { dataset: { tubeDesignerPartId: part.entityId } }, h.ops);
  assert.equal(h.view.tubeDesignerSketchDialogOpen, true);
  const open = h.calls.filter(c => c.method === "TubeDesigner.PreviewNestingSideSketchPart").at(-1);
  assert.equal(open.payload.partEntityId, "created");
  assert.equal(open.payload.resourceVersion, 7);
  assert.equal(open.payload.profileRef, undefined, "Reopening must use the frozen base, not reevaluate a possibly changed profile library");
  assert.deepEqual(h.view.tubeDesignerSketch.sidePreviewPayload, { partEntityId: "created", resourceVersion: 7 });
  assert.deepEqual(h.view.tubeDesignerSketch.sidePreview, reference.preview);
  let reopened = h.view.tubeDesignerSketch.sideByPart.created;
  assert.equal(reopened.trajectoryWidth, 0.8);
  assert.deepEqual(reopened.entities, [circle, line, multi]);
  reopened.trajectoryWidth = 4;
  reopened.entities = [{ ...circle, cx: 900 }];
  reopened.dirty = true;
  await handleSketchAreaAction(h.context, h.view, "tube-designer-sketch-cancel-section", null, h.ops);
  assert.equal(h.view.tubeDesignerSketch.sideByPart.created, undefined);
  await handlePartsAreaAction(h.context, h.view, "tube-designer-part-open-sketch", { dataset: { tubeDesignerPartId: part.entityId } }, h.ops);
  reopened = h.view.tubeDesignerSketch.sideByPart.created;
  assert.equal(reopened.trajectoryWidth, 0.8, "Canceling existing edits must discard an unsaved slit width even when the resource version is unchanged");
  assert.deepEqual(reopened.entities, [circle, line, multi], "Reopening after cancel restores the persisted source drawing");
  reopened.trajectoryWidth = 1; reopened.dirty = true;
  await handleSketchAreaAction(h.context, h.view, "tube-designer-sketch-commit", null, h.ops);
  const save = h.calls.find(c => c.method === "TubeDesigner.SavePartSketch");
  assert.equal(save.payload.resourceVersion, 7);
  assert.equal(save.payload.sketch.trajectoryWidth, 1);
}

{
  const h = harness();
  beginNewPartSideSketch(h.view, { profile, length: 300 }, reference);
  h.view.tubeDesignerSketchDialogOpen = true;
  await handleSketchAreaAction(h.context, h.view, "tube-designer-sketch-cancel-section", null, h.ops);
  assert.deepEqual(h.calls.map(c => c.method), ["TubeDesigner.ReleaseNestingSideSketchPreview"],
    "Canceling an uncommitted side draft only releases its neutral preview");
  assert.equal(h.view.tubeDesignerSketch.sideCreationPayload, null);
}

{
  const member = { entityId: "tube", length: 300 };
  const validate = draft => validateSideSketchDraft({ coordinateSpace:"arc-length-axial", ...draft },member,reference);
  assert.equal(validate({ entities: [circle], trajectoryWidth: 0 }).ready, false);
  assert.equal(validate({ entities: [circle], trajectoryWidth: 0.005 }).ready, false);
  assert.equal(validate({ entities: [circle], trajectoryWidth: "bad" }).ready, false);
  assert.equal(validate({ entities: [{ ...circle, cx: 120000 }] }).ready, true);
  assert.equal(validate({ entities: [{ ...circle, radius: 370 }] }).ready, true);
  assert.equal(validate({ entities: [{ id: "crossed", kind: "polyline", closed: true,
    points: [[20,20],[40,40],[20,40],[40,20]] }] }).ready, false);
  assert.equal(validate({ entities: [multi] }).ready, true,
    "Horizontal circumference is unbounded and vertical overflow is clipped by native geometry only");
}

{
  const h = harness();
  beginNewPartSideSketch(h.view, { profile, length: 300 }, reference);
  h.view.tubeDesignerSketchDialogOpen = true;
  const state = h.view.tubeDesignerSketch, draft = state.sideByPart[state.targetPartId];
  draft.entities = [circle];
  const originalProxy = h.context.sceneProxy;
  let resolve;
  const methods = [];
  originalProxy.invoke = (method, payload) => {
    methods.push({ method, payload });
    if (method === "TubeDesigner.ReleaseNestingSideSketchPreview") return Promise.resolve({});
    return new Promise(yes => { resolve = yes; });
  };
  const request = handleSketchAreaAction(h.context, h.view, "tube-designer-sketch-preview-side", null, h.ops);
  const key = state.sidePreviewResourceKey;
  await handleSketchAreaAction(h.context, h.view, "tube-designer-sketch-cancel-section", null, h.ops);
  h.context.sceneProxy = { invoke() { throw Error("Late release must use the original scene channel"); } };
  resolve(reference);
  await request;
  const releases = methods.filter(c => c.method === "TubeDesigner.ReleaseNestingSideSketchPreview");
  assert.equal(releases.length, 2, "Cancellation releases now and again after an in-flight preview returns");
  assert.ok(releases.every(c => c.payload.previewResourceKey === key));
}

{
  const h = harness();
  beginNewPartSideSketch(h.view, { profile, length: 300 }, reference);
  h.view.tubeDesignerSketchDialogOpen = true;
  const state = h.view.tubeDesignerSketch, draft = state.sideByPart[state.targetPartId];
  draft.entities = [circle]; draft.dirty = true;
  let resolve;
  h.context.sceneProxy.invoke = () => new Promise(yes => { resolve = yes; });
  const request = handleSketchAreaAction(h.context, h.view, "tube-designer-sketch-preview-side", null, h.ops);
  draft.entities = [line];
  resolve({ ...reference, preview: { ...reference.preview, baseGeometry: { url: "stale", version: 2 } } });
  await request;
  assert.equal(state.sidePreview.baseGeometry.url, "preview:base", "An obsolete preview never replaces newer user edits");
  assert.equal(state.sidePreviewPending, false);
}

{
  const h = harness();
  beginNewPartSideSketch(h.view, { profile, length: 300 }, reference);
  h.view.tubeDesignerSketchDialogOpen = true;
  const state = h.view.tubeDesignerSketch, draft = state.sideByPart[state.targetPartId];
  draft.entities = [circle]; draft.dirty = true;
  const originalInvoke = h.context.sceneProxy.invoke;
  let rejectCatalog, catalogStarted;
  const readingCatalog = new Promise(resolve => { catalogStarted = resolve; });
  h.context.sceneProxy.invoke = async (method, payload) => {
    if (method === "TubeDesigner.ReadNestingResult") {
      catalogStarted();
      return new Promise((resolve, reject) => { rejectCatalog = reject; });
    }
    const response = await originalInvoke(method, payload);
    if (method === "TubeDesigner.AddNestingSideSketchPart") {
      response.tubeDesigner.nestingTask = { revision: "previous-task", parts: [],
        request: { parts: [] }, result: { ready: true, revision: "previous-result" } };
    }
    return response;
  };
  const saving = handleSketchAreaAction(h.context, h.view, "tube-designer-sketch-commit", null, h.ops);
  await readingCatalog;
  assert.equal(h.view.tubeDesignerSketchDialogOpen, false,
    "A persisted part exits the editor before restoring an earlier nesting result");
  assert.equal(h.view.pending, false);
  assert.equal(state.sideSavePending, false);
  assert.equal(draft.dirty, false);
  assert.equal(state.sideCreationPayload, null);
  rejectCatalog(new Error("Saved nesting catalog is unavailable"));
  await saving;
  assert.equal(h.view.tubeDesignerSketchDialogOpen, false);
  assert.equal(state.sideSaveError, "");
  assert.equal(h.calls.filter(call => call.method === "TubeDesigner.AddNestingSideSketchPart").length, 1);
}

console.log("TubeDesignerSideSketchPartTest: creation, cancel, periodic cuts, slit width, reopen/edit, stale previews and exit before nesting restoration passed");
