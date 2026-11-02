import assert from "node:assert/strict";
import { handlePartDrawingAction, attachPartDrawingEditor } from "../../apps/tube-designer/webpage/partDrawing.mjs";

const profile={contours:[{kind:"circle",center:[0,0],radius:5}],width:10,depth:10};
const tool={id:"branch-profile",version:"1",digest:"current",target:"part",requiresSection:true,parameters:[],defaultParameters:{}};
const part={entityId:"part",name:"共享图",length:500,quantity:1,manufacturingGeometryResourceVersion:9,
  properties:{"tubeDesigner.cadDocument":{documentResourceId:"doc"}}};
const view={activeAreaId:"nesting",scene:{tubeDesigner:{nestingGroups:[{parts:[part]}]}}};
const calls=[],previews=[];
const boundarySnapshot={geometry:{Section:{FillRule:0,Boundaries:[]},MachiningFaces:{Faces:[{Id:"1"}]}},
  construction:{SolidNodes:[],UVFeatures:[]},placement:{Location:{X:0,Y:0,Z:0},XDirection:{X:1,Y:0,Z:0},YDirection:{X:0,Y:1,Z:0},ZDirection:{X:0,Y:0,Z:1}}};
const previewSnapshot=url=>({geometry:{url,version:1},documentGeometry:structuredClone(boundarySnapshot.geometry),
  documentConstruction:structuredClone(boundarySnapshot.construction),documentPlacement:structuredClone(boundarySnapshot.placement)});
const context={sceneProxy:{async invoke(method,payload){
  calls.push({method,payload:structuredClone(payload)});
  if(method==="TubeDesigner.GetTubeCadDocument")return {...structuredClone(boundarySnapshot),revision:"17",resourceVersion:9,
    authoringView:{mode:"drawing",drawing:{length:500,name:part.name,quantity:1,material:"",section:{source:"cad",profile}},
      features:[{id:"cut",cadNodeId:"cut",type:tool.id,toolTarget:"part",section:{source:"cad",profile},station:100,
        angle:90,azimuth:0,roll:0,offsetY:0,offsetZ:0,direction:"through",length:80}],ends:{}}};
  if(method==="TubeDesigner.GetPartDrawingTools")return {tools:[tool]};
  if(method==="TubeDesigner.PreviewTubeCadDocument")return new Promise(resolve=>previews.push({payload,resolve}));
  if(method==="TubeDesigner.ReleaseTubeCadDocumentPreview")return {released:true,resourceCount:0};
  throw new Error("Unexpected API: "+method);
}}};
const ops={renderProject(){}};
const action=(suffix,target={})=>handlePartDrawingAction(context,view,"tube-designer-drawing-"+suffix,target,ops);
const until=async predicate=>{
  const deadline=Date.now()+3000;
  while(!predicate()){if(Date.now()>deadline)throw new Error("Timed out waiting for CAD session");await new Promise(resolve=>setTimeout(resolve,10));}
};
await action("open",{dataset:{tubeDesignerPartId:part.entityId}});
const state=view.tubeDesignerPartDrawing.state;
attachPartDrawingEditor(context,view,null,ops);
await until(()=>state.catalogueStatus==="ready");
attachPartDrawingEditor(context,view,null,ops);
await until(()=>previews.length===1);
assert.equal(previews[0].payload.revision,"17");assert.equal(previews[0].payload.resourceVersion,9);
assert.deepEqual(previews[0].payload.authoringEdit.features,[]);
await action("select-node",{dataset:{drawingNode:"cut"}});
await action("field-change",{dataset:{tubeDesignerPunchField:"station"},value:"120"});
attachPartDrawingEditor(context,view,null,ops);
previews[0].resolve(previewSnapshot("old"));
await until(()=>previews.length===2);
assert.equal(state.preview,null,"An old document projection cannot overwrite a later edit");
assert.equal(previews[1].payload.authoringEdit.features[0].nodeId,"cut");
assert.equal(previews[1].payload.authoringEdit.features[0].feature.station,120);
await action("cancel");
previews[1].resolve(previewSnapshot("late"));
await until(()=>calls.some(call=>call.method==="TubeDesigner.ReleaseTubeCadDocumentPreview"));
assert.equal(view.tubeDesignerPartDrawing,null);assert.equal(state.preview,null);
assert.equal(calls.at(-1).payload.previewResourceKey,state.previewResourceKey);
assert.ok(calls.every(call=>!/(?:ApplyPartDrawing|GetPartDrawing$|AddPartDrawing|ReleasePartDrawingPreview)/.test(call.method)));
console.log("Saved 3D CAD session: current CAS preview, coalesced local edit, stale-response rejection and native resource release verified.");
