import assert from "node:assert/strict";
import { selectPunchProfileSource, updatePunchProfileParameter } from "../../apps/tube-designer/webpage/punchProfileSource.mjs";
import { createPunchWizardState, openPunchParameters, closePunchParameters } from "../../apps/tube-designer/webpage/punchWizard.mjs";

const part={entityId:"source-test",length:1000};
const geometry=width=>({width,parameters:{width},contours:[{kind:"circle",radius:width/2}]});
const profile={id:"rect",name:"截面",defaultParameters:{width:20},descriptor:{parameters:[{key:"width",valueType:"number"}]},previewProfile:geometry(20)};
const descriptor={id:"branch-profile",version:"1",digest:"exact",target:"part",requiresSection:true,defaultParameters:{angle:90}};
async function fixture(end=false) {
  const view={pending:false,tubeDesignerSystemProfiles:[structuredClone(profile)],tubeDesignerProfileDrafts:{"system:rect":{parameters:{width:80}}},tubeDesignerPunchWizard:createPunchWizardState(part)};
  const state=view.tubeDesignerPunchWizard;
  state.catalogueStatus="ready";state.tools=[descriptor,{...descriptor,id:"end-profile",target:"end"}];
  state.features=[{id:"row-0",station:500}];
  const dataset=end?{tubeDesignerPunchEnd:"start",tubeDesignerPunchIndex:"start"}:{tubeDesignerPunchIndex:"0"};
  await selectPunchProfileSource({},view,{value:"system:rect",dataset});
  return {view,state,dataset,item:end?state.ends.start:state.features[0]};
}
function deferredContext() {
  const calls=[];
  return {calls,context:{sceneProxy:{invoke(method,args,options){
    assert.equal(method,"TubeDesigner.GenerateProfilePreview");
    return new Promise((resolve,reject)=>calls.push({args,options,resolve,reject}));
  }}}};
}
const target=(dataset,width)=>({value:String(width),dataset:{...dataset,tubeDesignerPunchProfileParameter:"width"}});

// Unsaved changes in another library editor must not be paired with old geometry.
const selected=await fixture();
assert.equal(selected.item.section.parameters.width,20);
assert.equal(selected.item.section.profile.width,20);
assert.equal(selected.view.tubeDesignerProfileDrafts["system:rect"].parameters.width,80);

// Normal interaction is single-flight, visibly busy, and waits for completion.
for(const isEnd of [false,true]) {
  const {view,state,dataset,item}=await fixture(isEnd),{calls,context}=deferredContext();
  const renders=[],ops={renderProject(_context,current){renders.push({pending:current.pending,title:current.tubeDesignerOperation?.title});}};
  openPunchParameters(view,isEnd?"start":"0",isEnd?"start":"");
  const first=updatePunchProfileParameter(context,view,target(dataset,30),ops);
  assert.equal(view.pending,true);
  assert.match(view.tubeDesignerOperation.title,/正在生成/);
  assert.equal(typeof calls[0].options.onReport,"function");
  calls[0].options.onReport({payload:{completed:1,total:2,message:"构造截面轮廓"}});
  assert.equal(view.tubeDesignerOperation.phaseLabel,"1 / 2");
  assert.equal(view.tubeDesignerOperation.message,"构造截面轮廓");
  assert.equal(item.section.profile,null);
  assert.equal(closePunchParameters(view,true,part),false,"pending section generation must block committing old contours");
  assert.equal(await updatePunchProfileParameter(context,view,target(dataset,40),ops),false);
  assert.equal(await selectPunchProfileSource(context,view,{value:"system:rect",dataset},ops),false);
  assert.equal(calls.length,1,"busy input must not dispatch another backend job");
  assert.equal(item.section.parameters.width,30);
  calls[0].resolve({profile:geometry(30)});assert.equal(await first,true);
  assert.equal(view.pending,false);assert.equal(view.tubeDesignerOperation,null);
  const second=updatePunchProfileParameter(context,view,target(dataset,40),ops);
  calls[1].resolve({profile:geometry(40)});assert.equal(await second,true);
  assert.equal(item.section.parameters.width,40);assert.equal(item.section.profile.width,40);
  assert.equal(closePunchParameters(view,true,part),true);
  assert.equal(state.history.length,2,"selection plus one parameter transaction");
  assert.deepEqual(renders.map(render=>render.pending),[true,false,true,false]);
}

// External corruption cannot let an older response overwrite or unlock a newer
// owner. This forcibly bypasses busy only to exercise the defensive guard.
const defensive=await fixture(),defensiveCalls=deferredContext();
const older=updatePunchProfileParameter(defensiveCalls.context,defensive.view,target(defensive.dataset,30));
defensive.view.pending=false;
const newer=updatePunchProfileParameter(defensiveCalls.context,defensive.view,target(defensive.dataset,40));
const newerOperation=defensive.view.tubeDesignerOperation;
defensiveCalls.calls[0].resolve({profile:geometry(30)});assert.equal(await older,false);
assert.equal(defensive.view.pending,true);assert.equal(defensive.view.tubeDesignerOperation,newerOperation);
defensiveCalls.calls[1].resolve({profile:geometry(40)});assert.equal(await newer,true);
assert.equal(defensive.item.section.profile.width,40);

// Cancel, deleting/replacing a row, changing its source and closing the wizard
// all invalidate a response even if the numeric parameters happen to be equal.
for(const operation of ["cancel","replace-row","replace-section","replace-state"]) {
  const {view,state,dataset,item}=await fixture(),{calls,context}=deferredContext();
  openPunchParameters(view,"0");
  const pending=updatePunchProfileParameter(context,view,target(dataset,30));
  // Busy UI blocks these actions; force the flag only to exercise cancellation
  // and external state-replacement defenses against an in-flight native reply.
  view.pending=false;
  if(operation==="cancel")closePunchParameters(view,false,part);
  if(operation==="replace-row")state.features[0]={...item,section:{...item.section,profile:geometry(80)}};
  if(operation==="replace-section")item.section={...item.section,profile:geometry(80)};
  if(operation==="replace-state")view.tubeDesignerPunchWizard=createPunchWizardState(part);
  const before=structuredClone(view.tubeDesignerPunchWizard);
  calls[0].resolve({profile:geometry(30)});
  assert.equal(await pending,false);
  assert.deepEqual(view.tubeDesignerPunchWizard,before,operation);
}

const missingService=await fixture(),before=structuredClone(missingService.item);
await assert.rejects(updatePunchProfileParameter({},missingService.view,target(missingService.dataset,99)),/未连接截面生成服务/);
assert.deepEqual(missingService.item,before,"missing service cannot mutate parameters or fake a regenerated profile");

const errors=await fixture(),errorCalls=deferredContext();
const stale=updatePunchProfileParameter(errorCalls.context,errors.view,target(errors.dataset,30));
errors.view.pending=false;
const latest=updatePunchProfileParameter(errorCalls.context,errors.view,target(errors.dataset,40));
errorCalls.calls[1].resolve({profile:geometry(40)});await latest;
errorCalls.calls[0].reject(new Error("outdated failure"));assert.equal(await stale,false);
assert.equal(errors.state.error,"");
const bad=updatePunchProfileParameter(errorCalls.context,errors.view,target(errors.dataset,50));
errorCalls.calls[2].resolve({profile:{contours:[]}});
await assert.rejects(bad,/没有生成有效截面/);
assert.equal(errors.item.section.profile,null);
assert.equal(errors.view.pending,false);assert.equal(errors.view.tubeDesignerOperation,null);

// The native picker stays interactive. Only the selected-file import takes the
// controlled busy/progress lock, and import failures always release that lock.
const dxf=await fixture(),dxfCalls=[],dxfRenders=[];
let pickFile;
const dxfContext={appProxy:{bridge:{openFileDialog:()=>new Promise(resolve=>{pickFile=resolve;})}},
  productProxy:{invoke(method,args,options){assert.equal(method,"TubeDesigner.ImportProfileDxf");return new Promise((resolve,reject)=>dxfCalls.push({args,options,resolve,reject}));}}};
const dxfOps={renderProject(_context,current){dxfRenders.push(current.pending);}};
const importPending=selectPunchProfileSource(dxfContext,dxf.view,{value:"__dxf__",dataset:dxf.dataset},dxfOps);
assert.equal(dxf.view.pending,false,"file selection is not a native calculation");
pickFile("C:\\fixtures\\part.dxf");await Promise.resolve();
assert.equal(dxf.view.pending,true);assert.match(dxf.view.tubeDesignerOperation.title,/导入本地 DXF/);
assert.equal(await selectPunchProfileSource(dxfContext,dxf.view,{value:"__dxf__",dataset:dxf.dataset},dxfOps),false);
assert.equal(dxfCalls.length,1);assert.equal(typeof dxfCalls[0].options.onReport,"function");
dxfCalls[0].resolve({profile:geometry(24)});assert.equal(await importPending,true);
assert.equal(dxf.item.section.source,"dxf");assert.equal(dxf.item.section.profile.width,24);
assert.equal(dxf.view.pending,false);assert.deepEqual(dxfRenders,[true,false]);
const failure=selectPunchProfileSource(dxfContext,dxf.view,{value:"__dxf__",dataset:dxf.dataset},dxfOps);
pickFile("C:\\fixtures\\bad.dxf");await Promise.resolve();
dxfCalls[1].reject(new Error("DXF parse failed"));await assert.rejects(failure,/DXF parse failed/);
assert.equal(dxf.view.pending,false);assert.equal(dxf.view.tubeDesignerOperation,null);
assert.equal(dxf.item.section.profile.width,24,"failed import must preserve the previously selected geometry");

console.log("TubeDesignerPunchProfileSourceTest passed: controlled single-flight progress, saved defaults, ownership guards, cancellation and failure recovery.");
