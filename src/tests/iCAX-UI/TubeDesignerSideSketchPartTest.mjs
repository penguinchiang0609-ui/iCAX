import assert from "node:assert/strict";
import { beginNewPartSideSketch, handleSketchAreaAction,
  renderSectionSketchDialog, validateSideSketchDraft } from "../../apps/tube-designer/webpage/sketchArea.mjs";
import { handleNestingStandardPartRibbonCommand, handleNestingStandardPartAction,
  renderNestingStandardPartDialog } from "../../apps/tube-designer/webpage/nestingStandardPart.mjs";
import { canEditNestingSideSketch } from "../../apps/tube-designer/webpage/partsArea.mjs";
import { handleDesignerAreaAction } from "../../apps/tube-designer/webpage/designerActions.mjs";

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
  let revision=1,resourceVersion=7;
  let savedSketch={entities:[structuredClone(circle)],trajectoryWidth:0.5,coordinateSpace:"arc-length-axial"};
  let geometry={BaseNodeID:"base",RootNodeID:"side-cut",SolidNodes:[{ID:"base",Label:"原材",Data:{type:"SExtrudedRegionNode",value:{
    First:0,Last:300,Section:{Boundaries:[]}}}},{ID:"side-cut",Label:"侧壁轮廓",Data:{type:"SWrappedVolumeNode",value:{UVRegion:{FillRule:0,Boundaries:[{
      ID:"cut-loop",Curves:[{ID:"cut-circle",Segment:{Curve:{type:"Circle2",value:{Radius:6,Placement:{Location:{X:122,Y:80},XDirection:{X:1,Y:0},YDirection:{X:0,Y:1}}}},
        Range:{First:0,Last:Math.PI*2,Reversed:false}}}]}]}}}}],UVFeatures:[],Metadata:{}};
  const boundarySnapshot=()=>({geometry:{Section:structuredClone(geometry.SolidNodes[0].Data.value.Section),
    MachiningFaces:{Faces:[{Id:1}],Metadata:{revision}}},construction:structuredClone(geometry),
    placement:{Location:{X:0,Y:0,Z:0},XDirection:{X:1,Y:0,Z:0},YDirection:{X:0,Y:1,Z:0},ZDirection:{X:0,Y:0,Z:1}}});
  const view = { activeAreaId: "nesting", scene: { tubeDesigner: { nestingGroups: [] } },
    tubeDesignerSystemProfiles: [profile], tubeDesignerUserData: { profiles: [] } };
  const context = { sceneProxy: { async invoke(method, payload) {
    calls.push({ method, payload: structuredClone(payload) });
    if (method === "TubeDesigner.PreviewNestingSideSketchPart") return structuredClone(reference);
    if(method==="TubeDesigner.GetTubeCadDocument")return {partEntityId:"created",resourceVersion,
      documentResourceId:"cad-created",revision:String(revision),...boundarySnapshot(),
      authoringView:{mode:"side",reference:structuredClone(reference),sketch:structuredClone(savedSketch),
        bindings:{entities:savedSketch.entities.map(entity=>({entityId:entity.id,nodeId:"side-cut"}))},
        readOnlyNodes:[],preview:structuredClone(reference.preview)}};
    if(method==="TubeDesigner.ApplyTubeCadDocument") {
      savedSketch=structuredClone(payload.authoringEdit.sketch);revision++;resourceVersion++;
      const snapshot=structuredClone(view.scene.tubeDesigner);
      snapshot.nestingGroups[0].parts[0].manufacturingGeometryResourceVersion=resourceVersion;
      return {partEntityId:"created",resourceVersion,revision:String(revision),...boundarySnapshot(),snapshot:{tubeDesigner:snapshot}};
    }
    if (method === "TubeDesigner.AddNestingSideSketchPart") {
      savedSketch=structuredClone(payload.sketch);
      const part = { entityId: "created", independentNesting: true, length: payload.length,
        manufacturingGeometryResourceVersion: 7, profile,
        properties: { "manufacturing.partKind":"tube","manufacturing.process":"side-sketch",
          "tubeDesigner.cadDocument":{documentResourceId:"cad-created",documentResourceVersion:1} } };
      return { partEntityId: "created", tubeDesigner: { nestingGroups: [{ parts: [part] }] } };
    }
    if (["TubeDesigner.ReleaseNestingSideSketchPreview","TubeDesigner.ReleaseTubeCadDocumentPreview"].includes(method)) return {};
    throw new Error(`Unexpected method ${method}`);
  } }, actions: { async selectRibbonTab() {}, async refreshActiveSceneState() {} } };
  const ops = { renderProject() {}, showNotice() {} };
  return { view, context, ops, calls };
}

{
  const h = harness();
  beginNewPartSideSketch(h.view, { profile, length: 300 }, reference);
  const state = h.view.tubeDesignerSketch;
  state.tool = "polyline";
  const points = [[-5, 80], [125, 80], [125, 81], [-5, 81]];
  for (const point of points) {
    state.cadInput = { mode: "absolute", x: String(point[0]), y: String(point[1]) };
    await handleSketchAreaAction(h.context, h.view, "tube-designer-sketch-numeric-apply", null, h.ops);
  }
  assert.deepEqual(state.command.points, points,
    "An exact numeric fourth vertex near the start must not close a narrow polygon early");
  state.cadInput = { mode: "absolute", x: "-5", y: "80" };
  await handleSketchAreaAction(h.context, h.view, "tube-designer-sketch-numeric-apply", null, h.ops);
  const entity = state.sideByPart[state.targetPartId].entities[0];
  assert.equal(entity.closed, true);
  assert.deepEqual(entity.points, points, "The explicit first point closes the complete four-sided cut");
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
  const openEditor=()=>handleDesignerAreaAction(h.context,h.view,"tube-designer-part-open-sketch",
    {dataset:{tubeDesignerPartId:part.entityId}},h.ops);
  await openEditor();
  assert.equal(h.view.tubeDesignerSketch.mode,"side");
  assert.equal(h.view.tubeDesignerSketchDialogOpen,true);
  assert.equal(h.view.tubeDesignerCadEditor,undefined,"The production edit entry must not open a separate CAD inspector");
  const existingEditor=renderSectionSketchDialog(h.context,h.view);
  assert.match(existingEditor,/tube-section-sketch-dialog/);
  assert.match(existingEditor,/tube-sketch-side-profile-junction/);
  assert.match(existingEditor,/tube-designer-sketch-dialog-command/);
  assert.doesNotMatch(existingEditor,/tube-cad-backdrop/);
  assert.doesNotMatch(existingEditor,/草图已修改，点击预览三维查看结果/,
    "An unchanged reopened part already has its actual native preview");
  const open = h.calls.filter(c => c.method === "TubeDesigner.GetTubeCadDocument").at(-1);
  assert.equal(open.payload.partEntityId, "created");
  assert.equal(open.payload.resourceVersion, 7);
  assert.equal(open.payload.authoringView,"side");
  assert.equal(h.view.tubeDesignerSketch.sideCadSession.geometry.MachiningFaces.Metadata.revision,1);
  assert.ok(Array.isArray(h.view.tubeDesignerSketch.sideCadSession.construction.SolidNodes));
  assert.equal(open.payload.profileRef, undefined, "Reopening must use the frozen base, not reevaluate a possibly changed profile library");
  const activeDraft=()=>h.view.tubeDesignerSketch.sideByPart[h.view.tubeDesignerSketch.targetPartId];
  assert.deepEqual(activeDraft().entities,[circle,line,multi],"Existing cuts reopen in the original side drawing canvas");
  activeDraft().entities[0].radius=8;activeDraft().dirty=true;
  await handleSketchAreaAction(h.context,h.view,"tube-designer-sketch-cancel-section",null,h.ops);
  await openEditor();
  assert.equal(activeDraft().entities[0].radius,6,"Canceling an existing side drawing edit discards its draft");
  activeDraft().entities[0].radius=8;activeDraft().dirty=true;
  await handleSketchAreaAction(h.context,h.view,"tube-designer-sketch-commit",null,h.ops);
  const save = h.calls.find(c => c.method === "TubeDesigner.ApplyTubeCadDocument");
  assert.equal(save.payload.resourceVersion, 7);
  assert.equal(save.payload.authoringEdit.mode,"side");
  assert.equal(save.payload.authoringEdit.sketch.entities[0].radius,8);
  assert.equal(save.payload.partEntityId,"created");
  assert.equal(save.payload.geometry,undefined,"The workbench submits an edit command against the current document");
  assert.equal(h.calls.filter(c=>c.method==="TubeDesigner.AddNestingSideSketchPart").length,1);
  assert.equal(h.view.tubeDesignerSketchDialogOpen,false);
  await openEditor();
  assert.equal(activeDraft().entities[0].radius,8);
  assert.equal(h.calls.filter(c=>c.method==="TubeDesigner.GetTubeCadDocument").at(-1).payload.resourceVersion,8);
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
  const h=harness();
  const part={entityId:"created",independentNesting:true,length:300,profile,
    manufacturingGeometryResourceVersion:7,properties:{"manufacturing.partKind":"tube",
      "tubeDesigner.cadDocument":{documentResourceId:"cad-created",documentResourceVersion:1}}};
  h.view.scene.tubeDesigner.nestingGroups=[{parts:[part]}];
  const invoke=h.context.sceneProxy.invoke;
  let resolveRead;
  h.context.sceneProxy.invoke=async(method,payload)=>{
    const result=await invoke(method,payload);
    if(method==="TubeDesigner.GetTubeCadDocument"&&payload.authoringView==="side")
      return new Promise(resolve=>{resolveRead=()=>resolve(result);});
    return result;
  };
  const reading=handleDesignerAreaAction(h.context,h.view,"tube-designer-part-open-sketch",
    {dataset:{tubeDesignerPartId:part.entityId}},h.ops);
  await new Promise(resolve=>setImmediate(resolve));
  await handleDesignerAreaAction(h.context,h.view,"tube-designer-punch-open",
    {dataset:{tubeDesignerPartId:part.entityId}},h.ops);
  resolveRead();await reading;
  assert.equal(h.view.tubeDesignerCadEditor.mode,"punch");
  assert.notEqual(h.view.tubeDesignerSketchDialogOpen,true,
    "A late side-document read cannot replace a subsequently opened editor in the same area");
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

function existingPart(h) {
  const part={entityId:"created",independentNesting:true,length:300,profile,
    manufacturingGeometryResourceVersion:7,properties:{"manufacturing.partKind":"tube",
      "tubeDesigner.cadDocument":{documentResourceId:"cad-created",documentResourceVersion:1}}};
  h.view.scene.tubeDesigner.nestingGroups=[{parts:[part]}];
  return part;
}
const openExisting=(h,part)=>handleDesignerAreaAction(h.context,h.view,"tube-designer-part-open-sketch",
  {dataset:{tubeDesignerPartId:part.entityId}},h.ops);

{
  const h=harness(),part=existingPart(h);
  let rejectRead,renders=0;
  h.ops.renderProject=()=>renders++;
  h.context.sceneProxy.invoke=()=>new Promise((resolve,reject)=>{rejectRead=reject;});
  const reading=openExisting(h,part);
  h.view.scene={tubeDesigner:{nestingGroups:[]}};
  rejectRead(new Error("Old scene document request failed"));
  await reading;
  assert.equal(renders,0,"An expired document error cannot render the new scene");
  assert.notEqual(h.view.tubeDesignerSketchDialogOpen,true);
}

{
  const h=harness(),part=existingPart(h);
  await openExisting(h,part);
  const state=h.view.tubeDesignerSketch,originalInvoke=h.context.sceneProxy.invoke;
  const preview=state.sidePreview;
  let resolvePreview;
  h.context.sceneProxy.invoke=(method,payload)=>method==="TubeDesigner.PreviewTubeCadDocument"
    ?new Promise(resolve=>{resolvePreview=resolve;}):originalInvoke(method,payload);
  const pending=handleSketchAreaAction(h.context,h.view,"tube-designer-sketch-preview-side",null,h.ops);
  const key=state.sidePreviewResourceKey;
  h.view.scene={tubeDesigner:{nestingGroups:[]}};
  resolvePreview({preview:{baseGeometry:{url:"expired-scene-preview",version:1}}});
  await pending;
  assert.equal(state.sidePreview,preview,"The same scene proxy does not make an old scene preview current");
  assert.ok(h.calls.some(c=>c.method==="TubeDesigner.ReleaseTubeCadDocumentPreview"&&c.payload.previewResourceKey===key));
}

{
  const h=harness(),part=existingPart(h);
  await openExisting(h,part);
  const state=h.view.tubeDesignerSketch,originalInvoke=h.context.sceneProxy.invoke;
  let resolvePreview;
  h.context.sceneProxy.invoke=(method,payload)=>method==="TubeDesigner.PreviewTubeCadDocument"
    ?new Promise(resolve=>{resolvePreview=resolve;}):originalInvoke(method,payload);
  const pending=handleSketchAreaAction(h.context,h.view,"tube-designer-sketch-preview-side",null,h.ops);
  const key=state.sidePreviewResourceKey;
  await handleSketchAreaAction(h.context,h.view,"tube-designer-sketch-cancel-section",null,h.ops);
  beginNewPartSideSketch(h.view,{profile,length:300},reference);
  h.view.tubeDesignerSketchDialogOpen=true;
  assert.equal(h.view.tubeDesignerSketch,state,"The original page reuses its state object");
  resolvePreview({preview:{baseGeometry:{url:"expired-existing-preview",version:1}}});
  await pending;
  assert.equal(state.sidePreview.baseGeometry.url,"preview:base");
  const releases=h.calls.filter(c=>c.payload.previewResourceKey===key);
  assert.equal(releases.length,2);
  assert.ok(releases.every(c=>c.method==="TubeDesigner.ReleaseTubeCadDocumentPreview"),
    "Late CAD preview release retains the request's API even after the state becomes a creation draft");
}

for(const rejectSave of [false,true]) {
  const h=harness(),part=existingPart(h);
  await openExisting(h,part);
  const state=h.view.tubeDesignerSketch,draft=state.sideByPart[state.targetPartId];
  draft.entities[0].radius=8;draft.dirty=true;
  const originalInvoke=h.context.sceneProxy.invoke;
  let finishSave;
  h.context.sceneProxy.invoke=async(method,payload)=>{
    const result=await originalInvoke(method,payload);
    if(method==="TubeDesigner.ApplyTubeCadDocument")return new Promise((resolve,reject)=>{
      finishSave=()=>rejectSave?reject(new Error("Expired save failed")):resolve(result);
    });
    return result;
  };
  const saving=handleSketchAreaAction(h.context,h.view,"tube-designer-sketch-commit",null,h.ops);
  await new Promise(resolve=>setImmediate(resolve));
  const nextScene={tubeDesigner:{nestingGroups:[]}},nextProgress={title:"New scene operation"};
  h.view.scene=nextScene;h.view.pending=true;h.view.progress=nextProgress;
  let renders=0;h.ops.renderProject=()=>renders++;
  finishSave();await saving;
  assert.equal(h.view.pending,true,"An old save cannot unlock a new scene's operation");
  assert.equal(h.view.progress,nextProgress);
  assert.equal(h.view.scene,nextScene);
  assert.equal(state.sideSaveError,"");
  assert.equal(renders,0,"Neither an old success nor an old error repaints the new scene");
}

console.log("TubeDesignerSideSketchPartTest: creation, cancel, original-page edit, stale scene/save ownership, captured preview release and nesting restoration passed");
