import assert from "node:assert/strict";
import {readFileSync,readdirSync} from "node:fs";
import {handlePartDrawingAction,openPartDrawing,getPartDrawingPayload,renderPartDrawingDialog,attachPartDrawingEditor,defaultDrawingProfile,beginDrawingOperation,finishDrawingOperation} from "../../apps/tube-designer/webpage/partDrawing.mjs";
import {installDrawingCatalogue} from "../../apps/tube-designer/webpage/partDrawingModel.mjs";
import {buildPartDrawingPreviewRows} from "../../apps/tube-designer/webpage/partDrawingPreview.mjs";
import {renderNestingRightPane} from "../../apps/tube-designer/webpage/partsArea.mjs";
const root=new URL("../../apps/tube-designer/templates/_shared/punch-tools/",import.meta.url);
const tools=readdirSync(root,{withFileTypes:true}).filter(d=>d.isDirectory()).map(d=>JSON.parse(readFileSync(new URL(d.name+"/tool.json",root)))).map(t=>({...t,digest:"fixture",defaultParameters:Object.fromEntries(t.parameters.map(p=>[p.key,p.defaultValue]))}));
const profile={schema:"icax.imported-tube-profile",schemaVersion:1,kind:"imported-dxf",name:"圆管",width:40,depth:40,contours:[{kind:"circle",radius:20,center:[0,0]},{kind:"circle",radius:18,center:[0,0]}]};
const library={id:"round",name:"圆管",previewProfile:profile,defaultParameters:{diameter:40},descriptor:{parameters:[{key:"diameter",displayName:"直径",valueType:"number",defaultValue:40}]}};
const view={tubeDesignerSystemProfiles:[library],tubeDesignerTemplateProfiles:[{...library,id:"template",templateId:"test"}],tubeDesignerUserData:{profiles:[{...library,id:"my"}]}};
assert.equal(defaultDrawingProfile({...view,tubeDesignerSystemProfiles:[{...library,id:"angle",name:"角钢"},library]}).id,"round");
assert.equal(defaultDrawingProfile({...view,tubeDesignerSystemProfiles:[{...library,id:"angle"}]}).id,"angle");
assert.equal(defaultDrawingProfile({tubeDesignerSystemProfiles:[{id:"broken"}]}),undefined);
const calls=[];let choice="C:\\fixture\\截面.dxf",reject=false;
const result={toolsOnly:true,geometry:{url:"final",version:1},baseGeometry:{url:"blank"},
  toolPreviews:[{target:"feature",key:"test-tool",geometry:{url:"tools"}}],partEntityId:"saved",tubeDesigner:{nestingGroups:[]}};
const context={appProxy:{bridge:{async openFileDialog(){return choice;}}},productProxy:{async invoke(method,payload){calls.push({method,payload});return {profile:structuredClone(profile)};}},sceneProxy:{async invoke(method,payload){calls.push({method,payload});if(reject)throw new Error("裁剪无效");return result;}}};
const ops={renderProject(){},showNotice(){}};
const act=(name,target={})=>handlePartDrawingAction(context,view,"tube-designer-drawing-"+name,target,ops);
const command=name=>act("command",{dataset:{drawingCommand:name}});
const node=id=>act("select-node",{dataset:{drawingNode:id}});
const unrelatedPunchState={untouched:"existing machining wizard"};view.tubeDesignerPunchWizard=unrelatedPunchState;
openPartDrawing(view);let s=view.tubeDesignerPartDrawing.state;installDrawingCatalogue(s,{tools});
assert.equal(view.tubeDesignerPunchWizard,unrelatedPunchState,"Opening drawing keeps the independent machining wizard untouched");
assert.equal(view.tubeDesignerPartDrawing.mode,"main");
assert.deepEqual(buildPartDrawingPreviewRows(result).map(r=>r.data.geometry.url),["blank","tools"]);
for(const label of ["系统内置","模板自带","我的","外部 DXF","特征树","应用主管"])assert.ok(renderPartDrawingDialog(view).includes(label));
await command("branch");assert.match(s.error,/先应用主管/);
await act("section-select",{dataset:{drawingSection:"main"},value:"__dxf__"});assert.equal(s.drawing.section.source,"dxf");
assert.equal(calls.some(c=>c.method.includes("SaveImported")),false);
const imported=structuredClone(s.drawing.section);choice="";
await act("section-select",{dataset:{drawingSection:"main"},value:"__dxf__"});assert.deepEqual(s.drawing.section,imported);
await act("section-select",{dataset:{drawingSection:"main"},value:"system:round"});
await act("section-parameter",{dataset:{drawingSection:"main",drawingParameter:"diameter"},value:"50"});
assert.equal(calls.at(-1).payload.parameters.diameter,50);assert.equal(s.drawing.section.pendingParameters,undefined);
const beforeMainCommit=calls.length;
await act("commit-operation");assert.equal(view.tubeDesignerPartDrawing.mainApplied,true);assert.equal(view.tubeDesignerPartDrawing.mode,"");
assert.equal(calls.length,beforeMainCommit,"Applying the main tube only updates the local drawing tree");
await command("branch");assert.match(renderPartDrawingDialog(view),/支管截面/);
assert.equal(s.draft.section.key,"system:round","New branch selects a usable default profile without an extra selection");
assert.deepEqual(s.draft.section.parameters,{diameter:40});
assert.equal(getPartDrawingPayload(view,{includeDraft:true}).features[0].section.profile.contours.length,2);
assert.equal(s.error,"");
choice="C:\\fixture\\支管.dxf";await act("section-select",{dataset:{drawingSection:"branch"},value:"__dxf__"});
const draft=getPartDrawingPayload(view,{includeDraft:true});assert.equal(draft.features.length,1);assert.equal(getPartDrawingPayload(view).features.length,0);
// Axial columns and transverse rows combine; UI direction signs never depend on the datum.
const field=(key,value)=>act("field-change",{dataset:{tubeDesignerPunchField:key},value:String(value)});
await field("arrayCount",3);await field("arraySpacing",70);await field("rowCount",2);await field("rowSpacing",8);
for(const face of ["top","left","round"]) {
  await field("drawingArrayMode",face);await field("rowDirection","negative");
  const f=getPartDrawingPayload(view,{includeDraft:true}).features[0];
  assert.equal(f.face,face);assert.equal(f.arrayCount*f.rowCount,6);assert.equal(f.rowPitch,-8);
  assert.match(renderPartDrawingDialog(view),face==="round"?/角度间隔/:/排间距/);
  assert.equal("drawingArrayMode" in f,false);assert.equal("rowDirection" in f,false);
}
await field("arrayDirection","negative");assert.equal(s.draft.arrayPitch,-70);
await field("reference","end");assert.equal(s.draft.arrayPitch,70);
await field("arraySpacing",90);assert.equal(s.draft.arrayPitch,90);
await field("arrayDirection","positive");assert.equal(s.draft.arrayPitch,-90);
await field("reference","start");assert.equal(s.draft.arrayPitch,90);
const beforeInvalid=s.draft.rowPitch;await field("rowSpacing",-5);assert.equal(s.draft.rowPitch,beforeInvalid);assert.match(s.error,/正数/);
await field("drawingArrayMode","top");await field("arrayCount",1);await field("rowCount",1);await field("rowDirection","positive");
await command("v-notch");assert.match(s.error,/尚未应用/);assert.equal(s.draft.type,"branch-profile");
s.draft.toolParameters.angle=NaN;await act("commit-operation");assert.equal(s.features.length,0);assert.match(s.error,/有效数字/);
s.draft.toolParameters.angle=90;
const beforeFeatureCommit=calls.length;
reject=true;await act("commit-operation");assert.equal(s.features.length,1);assert.equal(s.features[0].toolTarget,"part");
assert.equal(calls.length,beforeFeatureCommit,"An applied branch remains editable and performs no native Boolean request");
reject=false;
const first=s.features[0].id;await node(first);assert.equal(s.editingId,first);
await field("rowCount",3);await field("drawingArrayMode","left");await field("rowDirection","negative");
await act("commit-operation");await node(first);
assert.equal(s.draft.face,"left");assert.equal(s.draft.rowCount,3);assert.equal(s.draft.rowPitch,-8);
await field("drawingArrayMode","top");await field("rowCount",1);await act("commit-operation");await node(first);
await act("field-change",{dataset:{tubeDesignerPunchField:"station"},value:"220"});
assert.equal(s.features[0].station,250);assert.equal(getPartDrawingPayload(view,{includeDraft:true}).features[0].station,220);
await act("cancel-operation");assert.equal(s.features[0].station,250);
await node(first);await act("field-change",{dataset:{tubeDesignerPunchField:"station"},value:"230"});await act("commit-operation");
assert.equal(s.features[0].station,230);assert.equal(view.tubeDesignerPartDrawing.selected,first);
await command("v-notch");assert.equal(s.draft.section,undefined);await act("commit-operation");assert.equal(s.features.length,2);
await command("start");assert.notEqual(s.ends.start.type,"keep");await act("cancel-operation");assert.equal(s.ends.start.type,"keep");
const beforeCancel=s.history.length;
await command("main");await act("main-change",{dataset:{drawingField:"length"},value:"800"});await act("cancel-operation");assert.equal(s.drawing.length,500);
assert.equal(s.history.length,beforeCancel,"Cancelled parameters must not become undoable model changes");
await command("main");await act("main-change",{dataset:{drawingField:"length"},value:"700"});await act("main-change",{dataset:{drawingField:"length"},value:"800"});await act("commit-operation");assert.equal(s.baseLength,800);
assert.equal(s.history.length,beforeCancel+1,"Apply must create one undo step, not a step per parameter");
await act("undo");assert.equal(s.baseLength,500);await act("redo");assert.equal(s.baseLength,800);
const payload=getPartDrawingPayload(view);assert.equal(JSON.stringify(payload).includes("C:\\fixture"),false);
await act("apply");assert.equal(calls.at(-1).method,"TubeDesigner.AddPartDrawing");assert.equal(view.tubeDesignerPartDrawing,null);
assert.equal(calls.at(-1).payload.toolsOnly,undefined,"The final Generate action, unlike editing previews, requests a real finished part");
assert.equal(view.tubeDesignerPunchWizard,unrelatedPunchState,"Finishing drawing must not close or replace machining state");
const part={entityId:"saved",name:"已保存零件",length:800,manufacturingGeometryResourceId:"final",manufacturingGeometryResourceVersion:7,properties:{"tubeDesigner.punchWizard":{...payload,baseLength:800}}};
{
  const newPart={...part,properties:{"tubeDesigner.partDrawing":structuredClone(part.properties["tubeDesigner.punchWizard"])}};
  const independentView={};openPartDrawing(independentView,newPart);
  installDrawingCatalogue(independentView.tubeDesignerPartDrawing.state,{tools});
  assert.equal(independentView.tubeDesignerPartDrawing.state.features.length,payload.features.length);
  assert.equal(getPartDrawingPayload(independentView).drawing.length,800);
  assert.equal(newPart.properties["tubeDesigner.punchWizard"],undefined,"Reading the dedicated drawing recipe must not rewrite the original part as a machining wizard");
  assert.equal(independentView.tubeDesignerPunchWizard,undefined);
  const inspector=renderNestingRightPane({}, {activeAreaId:"nesting",tubeDesignerActiveNestingPartId:newPart.entityId,
    scene:{tubeDesigner:{nestingGroups:[{parts:[{...newPart,profile,properties:{...newPart.properties,"manufacturing.partKind":"tube"}}]}]}}});
  assert.match(inspector,/data-cam-action="tube-designer-drawing-open"[^>]*>三维编辑/);
  assert.doesNotMatch(inspector,/tube-designer-punch-open|冲孔向导/);
}
view.tubeDesignerSystemProfiles=[];view.tubeDesignerTemplateProfiles=[];view.tubeDesignerUserData={profiles:[]};
openPartDrawing(view,part);s=view.tubeDesignerPartDrawing.state;installDrawingCatalogue(s,{tools:[]});
await command("main");assert.match(renderPartDrawingDialog(view),/主管截面和长度已锁定/);
await act("main-change",{dataset:{drawingField:"length"},value:"900"});assert.equal(s.drawing.length,800);
await act("cancel-operation");await node(s.features[0].id);assert.match(renderPartDrawingDialog(view),/仅可删除/);
await act("selected-copy");assert.match(s.error,/仅可删除/);
await act("selected-remove");assert.equal(s.features.length,1);await node(s.features[0].id);await act("selected-remove");
await act("apply");assert.equal(calls.at(-1).method,"TubeDesigner.ApplyPartDrawing");assert.equal(calls.at(-1).payload.resourceVersion,7);
// Coalesce edits while a previous native preview is still in flight.
openPartDrawing(view);s=view.tubeDesignerPartDrawing.state;s.drawing.section={source:"dxf",profile};installDrawingCatalogue(s,{tools});
let release;const slow={sceneProxy:{async invoke(method,payload){calls.push({method,payload});await new Promise(r=>{release=r;});return {...result,geometry:undefined,toolsOnly:true};}}};
attachPartDrawingEditor(slow,view,null,ops);await new Promise(r=>setTimeout(r,280));
assert.equal(s.previewPending,true);s.drawing.length=650;s.revision++;attachPartDrawingEditor(slow,view,null,ops);
assert.notEqual(view.pending,true,"Intermediate preview uses inline progress, not the global input-blocking operation");
assert.equal(view.tubeDesignerOperation,null);
assert.equal(calls.at(-1).method,"TubeDesigner.PreviewPartDrawing");
assert.equal(calls.at(-1).payload.toolsOnly,true,"Editing uses the dedicated tools-only preview, never cutting the main tube");
release();await new Promise(r=>setTimeout(r,20));assert.equal(s.preview,null);
await new Promise(r=>setTimeout(r,260));assert.equal(calls.at(-1).payload.drawing.length,650);
release();await new Promise(r=>setTimeout(r,20));assert.equal(s.preview.revision,s.revision);
assert.equal(s.error,"");assert.equal(s.preview.geometry,undefined);assert.equal(s.preview.baseGeometry.url,"blank");
view.tubeDesignerPartDrawing=null;
assert.equal(view.tubeDesignerPunchWizard,unrelatedPunchState);
{
  const progressView={};
  const progress=beginDrawingOperation({},progressView,"正在更新主管与刀具");
  const first=progressView.tubeDesignerOperation;
  assert.equal(first.kind,"part-drawing");assert.equal(progressView.pending,true);
  progress.onReport({completed:2,total:5,message:"更新支管"});
  assert.equal(first.phaseLabel,"2 / 5");assert.equal(first.message,"更新支管");
  beginDrawingOperation({},progressView,"下一操作");
  assert.equal(finishDrawingOperation(progressView,first),false,"A stale response does not unlock a different drawing operation");
  assert.equal(progressView.pending,true);finishDrawingOperation(progressView);assert.equal(progressView.pending,false);
}
{
  const catalogueView={};openPartDrawing(catalogueView);
  const catalogueCalls=[];
  const catalogueContext={sceneProxy:{async invoke(method,payload,options){catalogueCalls.push({method,payload,options});return {tools};}}};
  attachPartDrawingEditor(catalogueContext,catalogueView,null,ops);
  assert.equal(catalogueView.pending,true);
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(catalogueCalls[0].method,"TubeDesigner.GetPartDrawingTools");
  assert.equal(catalogueView.tubeDesignerPartDrawing.state.catalogueStatus,"ready");assert.equal(catalogueView.pending,false);
}
await Promise.all([
  {toolsOnly:true,baseGeometry:{url:"partial-base"},previewToolsComplete:false,resultError:"支管刀具构造失败"},
  {toolsOnly:true,baseGeometry:{url:"partial-base"},previewToolsComplete:false},
  {baseGeometry:{url:"old-endpoint-base"},geometry:{url:"already-cut"}},
].map(async response=>{
  const candidate={tubeDesignerSystemProfiles:[library]};openPartDrawing(candidate);
  const candidateState=candidate.tubeDesignerPartDrawing.state;installDrawingCatalogue(candidateState,{tools});
  attachPartDrawingEditor({sceneProxy:{async invoke(method,request){
    assert.equal(method,"TubeDesigner.PreviewPartDrawing");assert.equal(request.toolsOnly,true);return response;
  }}},candidate,null,ops);
  await new Promise(resolve=>setTimeout(resolve,280));
  assert.equal(candidateState.preview,null,"Partial or Boolean-preview responses must not be accepted as a complete drawing tool preview");
  assert.ok(candidateState.previewComputeError?.message);assert.equal(candidateState.previewPending,false);
}));
{
  const previousRaf=globalThis.requestAnimationFrame;
  globalThis.requestAnimationFrame=()=>0;
  try {
    const hiddenView={};openPartDrawing(hiddenView);let invoked=false;
    attachPartDrawingEditor({sceneProxy:{async invoke(){invoked=true;return {tools};}}},hiddenView,null,ops);
    await new Promise(resolve=>setTimeout(resolve,140));
    assert.equal(invoked,true,"Hidden windows still start native work after the bounded paint fallback");
    assert.equal(hiddenView.pending,false);
  } finally {
    if(previousRaf===undefined)delete globalThis.requestAnimationFrame;else globalThis.requestAnimationFrame=previousRaf;
  }
}
console.log("Part drawing: CAD command flow, transactional edits, live sections, latest-only preview, degraded locks and save/reopen passed.");
