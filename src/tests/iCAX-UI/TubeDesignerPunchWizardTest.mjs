import assert from "node:assert/strict";
import {
  createPunchWizardState,
  normalizePunchFeature,
  punchWizardProfileInfo,
  readPunchWizardFeatures,
  resolvePunchDistribution,
  renderPunchWizardDialog as renderPunchWizardView,
  validatePunchFeature,
  addPunchWizardFeature, updatePunchWizardField, removePunchWizardFeature,
} from "../../apps/tube-designer/webpage/punchWizard.mjs";
import {
  canEditNestingSideSketch,
  renderNestingViewportOverlay,
} from "../../apps/tube-designer/webpage/partsArea.mjs";
import { buildPunchPreviewRows } from "../../apps/tube-designer/webpage/punchEditor.mjs";
import { handleDesignerAreaAction } from "../../apps/tube-designer/webpage/designerActions.mjs";
import { createTubeCadFixture } from "./fixtures/tubeCadDocument.mjs";

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

// Creation has no published part geometry to hydrate.
assert.equal(createPunchWizardState(part).preview,null);
assert.deepEqual(punchWizardProfileInfo(part), { round: false, width: 40, depth: 20, diameter: 0 });
const creationFeature=values=>normalizePunchFeature({recordKind:"tool",toolRef:{id:"fixture-circle",version:"1",digest:"fixture"},...values});
assert.equal(validatePunchFeature(part, creationFeature({type:"circle",face:"top",station:600,offset:0,diameter:10})), "");
// Geometric footprint checks use the actual native BRep, not a guessed box face.
assert.equal(validatePunchFeature(part, creationFeature({type:"circle",face:"top",station:600,offset:18,diameter:10})), "");
assert.match(validatePunchFeature(part, creationFeature({station:600,arrayCount:2,arrayPitch:0})), /不能为零/);
assert.match(validatePunchFeature(part, creationFeature({station:600,blindHole:true,cutDepth:0})), /盲孔深度/);

const creationDraft={features:[{type:"circle",face:"top",station:320,diameter:8,arrayCount:2,arrayPitch:100}]};
assert.equal(readPunchWizardFeatures(creationDraft)[0].arrayCount,2);
assert.equal(createPunchWizardState(part,creationDraft).features.length,1);
assert.equal(normalizePunchFeature({ type: "unknown", arrayCount: 999 }).type, "unknown");
assert.match(validatePunchFeature(part,{type:"unknown",station:600}),/请选择刀具模板/);

view.tubeDesignerPunchWizard=createPunchWizardState(part);
assert.equal(view.tubeDesignerPunchWizard.partId, part.entityId);
const creationDialog=renderPunchWizardView(part,view,{tableMode:true,showEnds:true,title:"新建冲孔零件"});
assert.match(creationDialog,/新建冲孔零件/);
assert.match(creationDialog,/冲孔记录/);
assert.match(creationDialog,/位置 \/ 姿态/);
assert.match(creationDialog,/阵列维度/);
assert.match(creationDialog,/左端面/);
assert.match(creationDialog,/右端面/);
assert.match(creationDialog,/data-cam-viewcube/);
assert.deepEqual(
  (({station,arrayPitch,reference})=>({station,arrayPitch,reference}))(
    resolvePunchDistribution({arrayCount:4,distributionMode:"end-margins",headMargin:100,tailMargin:200},1200)),
  {station:100,arrayPitch:300,reference:"start"},
);

view.tubeDesignerPunchWizard.draft = creationFeature({station:600,diameter:8,arrayCount:2,arrayPitch:100});
assert.equal(addPunchWizardFeature(view,part),true);
assert.equal(view.tubeDesignerPunchWizard.features.length, 1);
assert.equal(view.tubeDesignerPunchWizard.features[0].arrayCount, 2);
assert.equal(updatePunchWizardField(view, {
  value: "640", dataset: { tubeDesignerPunchField: "station", tubeDesignerPunchIndex: "0" },
}),true);
assert.equal(view.tubeDesignerPunchWizard.features[0].station, 640);
assert.equal(view.tubeDesignerPunchWizard.selectedFeatureId, view.tubeDesignerPunchWizard.features[0].id);
assert.equal(removePunchWizardFeature(view,"0"),true);
assert.equal(view.tubeDesignerPunchWizard.features.length, 0);

view.tubeDesignerPunchWizard.draft = { ...view.tubeDesignerPunchWizard.draft, station: 600, diameter: 8, arrayCount: 1 };
assert.equal(addPunchWizardFeature(view,part),true);
const savedPart={...part,manufacturingGeometryResourceVersion:1,properties:{"manufacturing.partKind":"tube","manufacturing.process":"punch",
  "tubeDesigner.cadDocument":{documentResourceId:"saved-document",documentResourceVersion:1}}};
view.scene.tubeDesigner.nestingGroups[0].parts=[savedPart];
view.tubeDesignerPunchWizard=null;
let geometry=createTubeCadFixture(1200);
function tubeCadFixtureResult(part,construction,revision="1",resourceVersion=1) {
  return {partEntityId:part.entityId,resourceVersion,revision,documentResourceId:`document-${part.entityId}`,documentResourceVersion:Number(revision),
    geometry:{Section:structuredClone(construction.SolidNodes[0].Data.value.Section),MachiningFaces:{Faces:[{Id:1}],Metadata:{revision}}},
    construction:structuredClone(construction),placement:{Location:{X:0,Y:0,Z:0},XDirection:{X:1,Y:0,Z:0},YDirection:{X:0,Y:1,Z:0},ZDirection:{X:0,Y:0,Z:1}},
    nodes:construction.SolidNodes.map(node=>({nodeId:node.ID,kind:node.Data.type,base:node.ID===construction.BaseNodeID,
      sketchEditable:node.Data.type==="SExtrudedRegionNode",geometryEditable:node.Data.type==="SExtrudedRegionNode"}))};
}
const calls = [];
const context = {
  sceneProxy: {
    async invoke(method, request) {
      calls.push({ method, request });
      if(method==="TubeDesigner.GetTubeCadDocument") {
        const result=tubeCadFixtureResult(savedPart,geometry);
        if(request.authoringView==="side")result.authoringView={mode:"side",sketch:{entities:[],coordinateSpace:"arc-length-axial"},
          bindings:{entities:[]},reference:{available:true,length:1200,profile:savedPart.profile,
            unfolding:{available:true,method:"section-arc-length",length:1200,perimeter:120,
              surfaces:[{inner:false,uPeriod:120,rectangle:{available:true,width:1200,height:120},wires:[]}]}}};
        return result;
      }
      if(method==="TubeDesigner.ApplyTubeCadDocument"){
        geometry=structuredClone(request.construction);
        return {...tubeCadFixtureResult(savedPart,geometry,"2",2),snapshot:{tubeDesigner:view.scene.tubeDesigner}};
      }
      if(method==="TubeDesigner.ReleaseTubeCadDocumentPreview")return {};
      throw new Error(`Unexpected old saved-part apply: ${method}`);
    },
  },
  actions: { async refreshActiveSceneState() {} },
};
await handleDesignerAreaAction(context,view,"tube-designer-punch-open",{dataset:{tubeDesignerPartId:part.entityId}},operations);
assert.equal(calls[0].method,"TubeDesigner.GetTubeCadDocument");
const state=view.tubeDesignerCadEditor;
const hole=state.construction.SolidNodes.find(node=>node.ID!==state.construction.BaseNodeID&&node.Data.type==="SExtrudedRegionNode");
await handleDesignerAreaAction(context,view,"tube-designer-cad-select",{dataset:{cadNode:hole.ID}},operations);
await handleDesignerAreaAction(context,view,"tube-designer-cad-field",{dataset:{cadNode:hole.ID,cadField:"radius",cadLoop:hole.Data.value.Section.Boundaries[0].ID},value:"12"},operations);
await handleDesignerAreaAction(context,view,"tube-designer-cad-add-hole",{},operations);
await handleDesignerAreaAction(context,view,"tube-designer-cad-save",{},operations);
assert.equal(calls.at(-1).method,"TubeDesigner.ApplyTubeCadDocument");
assert.equal(geometry.SolidNodes.find(node=>node.ID===hole.ID).Data.value.Section.Boundaries[0].Curves[0].Segment.Curve.value.Radius,6);
assert.equal(geometry.SolidNodes.filter(node=>node.Data.type==="SExtrudedRegionNode").length,3);
assert.equal(view.tubeDesignerPunchWizard,null);
assert.equal(state.dirty,false);
assert.equal(state.geometry.MachiningFaces.Metadata.revision,"2");
assert.equal(calls.at(-1).request.geometry,undefined);
assert.ok(calls.at(-1).request.construction);
assert.equal(canEditNestingSideSketch(savedPart),true);
view.tubeDesignerActiveNestingPartId=part.entityId;view.tubeDesignerNestingSelectionKind="part";
assert.match(renderNestingViewportOverlay({}, view), /tube-designer-part-open-sketch/);
const sharedSketchResult = await handleDesignerAreaAction(context, view, "tube-designer-part-open-sketch", {
  dataset: { tubeDesignerPartId: part.entityId },
}, operations);
assert.equal(sharedSketchResult.handled,true);
assert.equal(view.tubeDesignerCadEditor,null);
assert.equal(view.tubeDesignerSketchDialogOpen,true);
assert.equal(view.tubeDesignerSketch.mode,"side");
assert.equal(view.tubeDesignerSketch.sideCadSession.partEntityId,part.entityId);
assert.equal(calls.filter(c=>c.method==="TubeDesigner.GetTubeCadDocument").at(-1).request.authoringView,"side");

console.log("Punch creation helpers, saved CAD diameter/add-hole and original side workbench dispatch checks passed.");
