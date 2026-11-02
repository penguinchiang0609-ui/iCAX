import assert from "node:assert/strict";
import { handleDesignerAreaAction } from "../../apps/tube-designer/webpage/designerActions.mjs";
import { renderNestingViewportOverlay } from "../../apps/tube-designer/webpage/partsArea.mjs";
import { renderPartDrawingDialog } from "../../apps/tube-designer/webpage/partDrawing.mjs";
import { createTubeCadFixture, tubeCadFixtureResult } from "./fixtures/tubeCadDocument.mjs";

// Imported parts arrive with a neutral document; each editor opens that exact
// graph directly. No additional recovery recipe is stored in the snapshot.
const original={entityId:"imported-1",name:"导入管材",length:800,independentNesting:true,
  manufacturingGeometryResourceId:"final",manufacturingGeometryResourceVersion:7,
  properties:{"manufacturing.partKind":"tube","manufacturing.imported":true,
    "tubeDesigner.cadDocument":{documentResourceId:"document-imported-1",documentResourceVersion:1}}};
const geometry=createTubeCadFixture(800);
const retained={ID:"retained",Label:"Part-surface grouped internal analysis",Data:{type:"SResidualBRepNode",value:{ResourceID:"exact-body",Evidence:{Confidence:0.95}}},
  Relations:null,RemovalSemantics:null,Metadata:{sourceFace:"unmodified"}};
geometry.SolidNodes.push(retained);
const scene={tubeDesigner:{nestingGroups:[{parts:[original]}]}},untouched=structuredClone(scene);
// A saved independent part without a document cannot fall back to creation or a recipe editor.
const missingDocument=structuredClone(original);delete missingDocument.properties["tubeDesigner.cadDocument"];
const missingView={activeAreaId:"nesting",scene:{tubeDesigner:{nestingGroups:[{parts:[missingDocument]}]}}};
for(const action of ["drawing-open","punch-open","part-open-sketch"])
  await assert.rejects(()=>handleDesignerAreaAction({sceneProxy:{async invoke(){throw new Error("Must not invoke a fallback API");}}},missingView,
    `tube-designer-${action}`,{dataset:{tubeDesignerPartId:original.entityId}},{renderProject(){}}),/没有可编辑的管材 CAD 文档/);
assert.equal(missingView.tubeDesignerPartDrawing,undefined);assert.equal(missingView.tubeDesignerPunchWizard,undefined);
const view={activeAreaId:"nesting",scene,tubeDesignerActiveNestingPartId:original.entityId,tubeDesignerNestingSelectionKind:"part"};
const html=renderNestingViewportOverlay({},view);
for(const entry of ["drawing-open","punch-open","part-open-sketch"])assert.match(html,new RegExp(`tube-designer-${entry}`));
assert.doesNotMatch(html,/tube-designer-drawing-recover/);
let current=structuredClone(geometry),revision="1",version=7;
const profile={name:"CAD 截面",width:40,depth:40,contours:[{kind:"circle",center:[0,0],radius:20},{kind:"circle",center:[0,0],radius:18}]};
const drawingDocument=part=>({...tubeCadFixtureResult(part,current,revision,version),authoringView:{mode:"drawing",
  drawing:{length:current.SolidNodes.find(node=>node.ID==="stock").Data.value.Last,name:part.name,quantity:1,material:"",section:{source:"cad",profile}},
  features:[],ends:{start:{type:"keep"},end:{type:"keep"}},readOnlyNodes:[{nodeId:"retained",label:"保留实体"}]}});
const calls=[],context={sceneProxy:{async invoke(method,payload){
  calls.push({method,payload:structuredClone(payload)});
  if(method==="TubeDesigner.GetTubeCadDocument")return payload.authoringView==="drawing"?drawingDocument(original):tubeCadFixtureResult(original,current,revision,version);
  if(method==="TubeDesigner.ApplyTubeCadDocument"){
    assert.equal(payload.authoringEdit.mode,"drawing");
    assert.deepEqual(payload.authoringEdit.features,[]);
    assert.deepEqual(payload.authoringEdit.ends,[]);
    current.SolidNodes.find(node=>node.ID==="stock").Data.value.Last=payload.authoringEdit.main.length;
    revision=String(Number(revision)+1);version++;
    return {...tubeCadFixtureResult(original,current,revision,version),snapshot:{tubeDesigner:view.scene.tubeDesigner}};
  }
  throw new Error(`Unexpected old editor call ${method}`);
}}},ops={renderProject(){},appendProjectLog(){}};
const act=(action,target={})=>handleDesignerAreaAction(context,view,action,target,ops);
await act("tube-designer-drawing-open",{dataset:{tubeDesignerPartId:original.entityId}});
assert.deepEqual(calls[0].payload,{partEntityId:original.entityId,resourceVersion:7,authoringView:"drawing"});
assert.equal(view.tubeDesignerPartDrawing.state.cadSession.revision,"1");
assert.equal(view.tubeDesignerCadEditor,undefined);
const editorHtml=renderPartDrawingDialog(view);
assert.match(editorHtml,/td-draw-workbench/);
assert.match(editorHtml,/保留实体|保留构造/);
assert.doesNotMatch(editorHtml,/sourceFace|Confidence|Part-surface grouped internal analysis/);
await act("tube-designer-drawing-main-change",{dataset:{drawingField:"length"},value:"820"});
await act("tube-designer-drawing-apply");
assert.equal(calls.at(-1).method,"TubeDesigner.ApplyTubeCadDocument");
assert.equal(calls.at(-1).payload.resourceVersion,7);assert.equal(calls.at(-1).payload.revision,"1");
assert.deepEqual(current.SolidNodes.find(node=>node.ID==="retained"),retained);
assert.equal(current.SolidNodes.find(node=>node.ID==="stock").Data.value.Last,820);
assert.deepEqual(view.scene,untouched,"Routing edits do not add a second editor recipe to the scene");
assert.equal(view.tubeDesignerPartDrawing,null);
await act("tube-designer-punch-open",{dataset:{tubeDesignerPartId:original.entityId}});
assert.equal(view.tubeDesignerCadEditor.mode,"punch");
assert.equal(calls.filter(call=>call.method==="TubeDesigner.GetTubeCadDocument").length,2,"Each existing workspace reads the same authoritative document");

const failureView={activeAreaId:"nesting",scene:structuredClone(untouched)};
await handleDesignerAreaAction({sceneProxy:{async invoke(){throw new Error("中性文档读取失败");}}},failureView,
  "tube-designer-drawing-open",{dataset:{tubeDesignerPartId:original.entityId}},ops);
assert.equal(failureView.tubeDesignerPartDrawing.loading,false);
assert.equal(failureView.tubeDesignerPartDrawing.loadError,true);
assert.match(failureView.tubeDesignerPartDrawing.state.error,/中性文档读取失败/);
assert.equal(failureView.tubeDesignerPartDrawing.state.cadSession,undefined);
assert.match(renderPartDrawingDialog(failureView),/data-cam-action="tube-designer-drawing-apply"[^>]*disabled/);
assert.deepEqual(failureView.scene,untouched);

const second={...original,entityId:"imported-2"},switchedView={activeAreaId:"nesting",scene:{tubeDesigner:{nestingGroups:[{parts:[original,second]}]}}};
let finish;
const delayedContext={sceneProxy:{invoke(method,payload){
  assert.equal(method,"TubeDesigner.GetTubeCadDocument");
  return payload.partEntityId===original.entityId?new Promise(resolve=>finish=resolve):Promise.resolve(drawingDocument(second));
}}};
let switchedRenders=0;
const switchedOps={...ops,renderProject(){switchedRenders++;}};
const pending=handleDesignerAreaAction(delayedContext,switchedView,"tube-designer-drawing-open",{dataset:{tubeDesignerPartId:original.entityId}},switchedOps);
await new Promise(resolve=>setImmediate(resolve));
switchedView.scene={tubeDesigner:{nestingGroups:[{parts:[second]}]}};
delayedContext.sceneProxy={async invoke(method,payload){assert.equal(method,"TubeDesigner.GetTubeCadDocument");assert.equal(payload.partEntityId,second.entityId);return drawingDocument(second);}};
const rendersBeforeLateRead=switchedRenders;
finish(drawingDocument(original));await pending;
assert.equal(switchedView.tubeDesignerPartDrawing.state.cadSession,undefined,"A late read cannot attach a document to a replaced scene and proxy");
assert.equal(switchedRenders,rendersBeforeLateRead,"A late read cannot rerender the current scene through an old handler");
await handleDesignerAreaAction(delayedContext,switchedView,"tube-designer-drawing-open",{dataset:{tubeDesignerPartId:second.entityId}},ops);
assert.equal(switchedView.tubeDesignerPartDrawing.state.partId,second.entityId);
assert.equal(switchedView.tubeDesignerPartDrawing.state.cadSession.revision,revision);
{
  let rejectRead,isCurrent=true;
  const oldView={activeAreaId:"nesting",scene:structuredClone(untouched)},rendered=[];
  const oldContext={isCurrentProject:()=>isCurrent,sceneProxy:{invoke(){return new Promise((_resolve,reject)=>{rejectRead=reject;});}}};
  const localOps={renderProject(_context,renderedView){rendered.push(renderedView);}};
  const delayed=handleDesignerAreaAction(oldContext,oldView,"tube-designer-drawing-open",{dataset:{tubeDesignerPartId:original.entityId}},localOps);
  await new Promise(resolve=>setImmediate(resolve));
  isCurrent=false;
  const newView={activeAreaId:"nesting",scene:{tubeDesigner:{nestingGroups:[{parts:[second]}]}}};
  await handleDesignerAreaAction({sceneProxy:{async invoke(){return drawingDocument(second);}}},newView,"tube-designer-drawing-open",{dataset:{tubeDesignerPartId:second.entityId}},localOps);
  const previousRenders=rendered.filter(item=>item===oldView).length;
  rejectRead(new Error("旧项目迟到的读取失败"));await delayed;
  assert.equal(newView.tubeDesignerPartDrawing.state.error,"");
  assert.equal(rendered.filter(item=>item===oldView).length,previousRenders,"A failed old request cannot rerender over the current project's workbench");
}
console.log("Imported CAD actual 3D workbench routing, current CAS operations, unsupported-node preservation, failed reads and scene ownership checks passed (controlled native data).");
