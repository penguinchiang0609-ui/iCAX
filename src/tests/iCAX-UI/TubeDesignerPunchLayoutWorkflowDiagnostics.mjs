// Workflow regressions: production handlers/renderers, mocked native responses.
// This is not a desktop UI run or a native solid-intersection accuracy test.
import assert from "node:assert/strict";
import { resolvePunchLayout } from "../../apps/tube-designer/webpage/punchLayout.mjs";
import { createPunchWizardState, normalizePunchFeature, getPunchWizardPayload } from "../../apps/tube-designer/webpage/punchWizard.mjs";
import { handleNestingPunchPartAction, renderNestingPunchPartDialog } from "../../apps/tube-designer/webpage/nestingPunchPart.mjs";
import { buildPunchReviewSummary } from "../../apps/tube-designer/webpage/punchReview.mjs";
import { renderPunchLayoutOverview } from "../../apps/tube-designer/webpage/punchLayoutView.mjs";

const failures=[];
let passed=0, mathCases=0;
async function check(name,work) {
  try {await work();passed++;console.log(`PASS ${name}`);}
  catch(error){failures.push({name,message:error.message});console.log(`FAIL ${name}\n${error.message}`);}
}
const near=(actual,expected,message="")=>assert.ok(Math.abs(actual-expected)<=1e-7,`${message}: expected ${expected}, actual ${actual}`);
const absolute=(feature,length)=>feature.arrayOffsets.map(dx=>(feature.reference==="end"?length-feature.station:feature.reference==="center"?length/2+feature.station:feature.station)+dx);
function solved(feature,length) {
  const result=resolvePunchLayout(feature,length);assert.equal(result.layoutError,"");mathCases++;return result;
}

await check("unequal end margins across fractional/short/long stock and 2..1000 holes",()=>{
  for(const length of [1,37.5,1000,100000])for(const count of [2,3,6,17,1000]) {
    const a=length*.07,b=length*.13,p=(length-a-b)/(count-1);
    const feature={distributionMode:"end-margins",arrayCount:count,headMargin:a,tailMargin:b};
    const result=solved(feature,length);
    result.layoutSummary.positions.forEach((position,index)=>near(position,a+index*p));
    near(result.layoutSummary.headMargin,a);near(result.layoutSummary.tailMargin,b);
    const reopened=JSON.parse(JSON.stringify(result)),nextLength=length*1.7;
    const changed=solved(reopened,nextLength);
    near(changed.layoutSummary.headMargin,a);near(changed.layoutSummary.tailMargin,b);
    assert.equal(changed.arrayCount,count);
  }
});

await check("centre-out mirror symmetry, custom first pair, parity and end clipping",()=>{
  for(const length of [1,1000,100000])for(const pitch of [.125,120])for(const count of [1,2,5,6,999,1000]) {
    const mode=count%2?"hole":"gap",centerOffset=length*.1,center=length/2+centerOffset;
    const result=solved({distributionMode:"center-out",arrayCount:count,arrayPitch:pitch,centerMode:mode,centerOffset,centerFirstOffset:pitch*.75},length);
    const points=result.layoutSummary.positions;
    for(let index=0;index<points.length;index++)near(points[index]+points.at(-1-index),2*center);
    if(mode==="hole")near(points[(count-1)/2],center);
    else {near(points[count/2]-center,pitch*.75);near(center-points[count/2-1],pitch*.75);}
    const reopened=JSON.parse(JSON.stringify(result));
    assert.deepEqual(solved(reopened,length+123).layoutSummary.positions,solved({...result,station:987654},length+123).layoutSummary.positions);
  }
});

await check("forward/reverse signed pitch preserves exact rule instances beyond both ends",()=>{
  for(const reference of ["start","end","center"])for(const station of [-20,500,1020])for(const pitch of [-150,150]) {
    const result=solved({distributionMode:"pitch",reference,station,arrayPitch:pitch,arrayCount:8,skipInstancesText:"2,7"},1000);
    const origin=reference==="end"?1000-station:reference==="center"?500+station:station;
    assert.deepEqual(result.layoutSummary.positions,Array.from({length:8},(_,i)=>origin+(reference==="end"?-1:1)*i*pitch));
    assert.deepEqual(result.skippedInstances,["0:1","0:6"]);
    assert.equal(result.layoutSummary.actualCount,6);
    assert.deepEqual(absolute(result,1000),result.layoutSummary.positions);
  }
});

const profiles=[
  {id:"rect",name:"A矩形管",defaultParameters:{width:40},descriptor:{parameters:[{key:"width",valueType:"number",defaultValue:40}]},previewProfile:{kind:"rect",width:40,depth:20,parameters:{width:40},contours:[{kind:"roundedRectangle",width:40,height:20,radius:0}]}},
  {id:"round",name:"B圆管",defaultParameters:{width:50},descriptor:{parameters:[{key:"width",valueType:"number",defaultValue:50}]},previewProfile:{kind:"round",width:50,depth:50,diameter:50,parameters:{width:50},contours:[{kind:"circle",radius:25}]}}
];
async function fixture(profileDrafts={}) {
  const view={pending:false,tubeDesignerSystemProfiles:structuredClone(profiles),tubeDesignerProfileDrafts:structuredClone(profileDrafts)},requests=[];
  const context={sceneProxy:{async invoke(method,request){
    assert.equal(method,"TubeDesigner.PreviewPunchWizard");requests.push(structuredClone(request));
    // Counters are fixture data only; actual clipping is tested in native tests.
    return {toolsOnly:true,baseGeometry:{url:"fixture-stock",version:requests.length},
      length:request.length,baseBounds:{width:request.length},toolCountExact:false,
      appliedPunchToolCount:request.features.reduce((n,f)=>n+f.arrayCount*f.rowCount,0),outsideToolCount:0,endToolCount:0};
  }},productProxy:{async invoke(method,request){
    assert.equal(method,"TubeDesigner.EvaluateProfilePackage");
    const profile=structuredClone(profiles.find(item=>item.id===request.profileRef.id).previewProfile);
    profile.parameters=structuredClone(request.parameters);profile.width=request.parameters.width;
    return {profile};
  }}};
  const ops={renderProject(){}};
  const action=(name,target={})=>handleNestingPunchPartAction(context,view,"tube-designer-nesting-punch-create-"+name,target,ops);
  await action("open");
  const state=view.tubeDesignerPunchWizard;state.catalogueStatus="ready";
  return {view,state,requests,action};
}
const margins={id:"margins",type:"circle",diameter:10,layoutDatum:"base",distributionMode:"end-margins",headMargin:100,tailMargin:150,arrayCount:6};
const center={id:"center",type:"circle",diameter:8,layoutDatum:"base",distributionMode:"center-out",centerMode:"gap",centerFirstOffset:100,centerOffset:0,arrayCount:6,arrayPitch:150};
const crossed={id:"crossed",type:"circle",diameter:10,layoutDatum:"base",distributionMode:"pitch",station:980,arrayCount:3,arrayPitch:15};

await check("real creation handler keeps rules, end records and summaries when stock length/profile change",async()=>{
  const {view,state,requests,action}=await fixture();
  state.features=[margins,center,crossed].map(normalizePunchFeature);
  state.ends={start:{type:"keep"},end:{type:"keep"}};
  const raw=structuredClone(state.features),ends=structuredClone(state.ends);
  await action("draft-change",{value:"1200",dataset:{tubeDesignerNestingPunchField:"length"}});
  assert.equal(state.baseLength,1200);assert.equal(requests.at(-1).length,1200);
  assert.deepEqual(absolute(requests.at(-1).features[0],1200),[100,290,480,670,860,1050]);
  assert.deepEqual(absolute(requests.at(-1).features[1],1200),[200,350,500,700,850,1000]);
  assert.deepEqual(absolute(requests.at(-1).features[2],1200),[980,995,1010]);
  assert.deepEqual(state.features,raw,"changing stock geometry does not mutate editable layout rules");
  await action("draft-change",{value:"system:round",dataset:{tubeDesignerNestingPunchField:"profileKey"}});
  assert.equal(requests.at(-1).profileRef.id,"round");assert.deepEqual(state.features,raw);assert.deepEqual(state.ends,ends);
  const summary=buildPunchReviewSummary(state,{length:1200});
  assert.equal(summary.length,1200);assert.equal(summary.positions,15);assert.equal(summary.groups.length,2);
  assert.match(renderNestingPunchPartDialog(view),/刀具、位置姿态、多组阵列分别设置/);
  const payload=getPunchWizardPayload(view),reopened=createPunchWizardState({entityId:"saved",length:1200,properties:{"tubeDesigner.punchWizard":{...payload,baseLength:1200}}});
  reopened.catalogueStatus="ready";
  assert.deepEqual(getPunchWizardPayload({tubeDesignerPunchWizard:reopened}).features,payload.features);
});

await check("changing length invalidates old exact counters until a matching preview returns",()=>{
  const state={baseLength:1000,revision:2,features:[normalizePunchFeature(margins)],tools:[],catalogueStatus:"ready",preview:{revision:1,length:1000,toolCountExact:true,appliedPunchToolCount:6,outsideToolCount:0,endToolCount:0}};
  assert.equal(buildPunchReviewSummary(state).exactPreviewCounts,false);
  state.preview.revision=2;assert.equal(buildPunchReviewSummary(state).exactPreviewCounts,true);
  state.previewPending=true;assert.equal(buildPunchReviewSummary(state).exactPreviewCounts,false);
  state.previewPending=false;state.parameterEditor={index:"0"};assert.equal(buildPunchReviewSummary(state).exactPreviewCounts,false);
});

await check("undo/redo keeps stock input, rule, native request and summary in one transaction",async()=>{
  const {view,state,requests,action}=await fixture();
  state.features=[normalizePunchFeature({...margins,tailMargin:100,arrayCount:5})];
  state.ends={start:{type:"end-miter",trim:12,rotation:15,datum:"long",toolParameters:{angle:30}},end:{type:"keep"}};
  const ends=structuredClone(state.ends);
  const verify=(length,count)=>{
    const draft=view.tubeDesignerNestingPunchPartDraft,request=requests.at(-1),feature=request.features[0];
    assert.equal(Number(draft.length),length);assert.equal(state.baseLength,length);
    assert.equal(state.creationInput.draft,draft,"restored form must remain bound to the current history snapshot");
    assert.equal(request.length,length);assert.equal(feature.arrayCount,count);assert.equal(state.features[0].arrayCount,count);
    near(request.length-absolute(feature,request.length).at(-1),feature.tailMargin,"tail margin must match the actual requested tube");
    assert.equal(buildPunchReviewSummary(state).length,length);
    assert.match(renderNestingPunchPartDialog(view),new RegExp('value="'+length+'" data-cam-change-action="tube-designer-nesting-punch-create-draft-change" data-tube-designer-nesting-punch-field="length"'));
    assert.deepEqual(state.ends,ends);assert.deepEqual(request.ends,ends);
  };
  await action("field-change",{value:"6",dataset:{tubeDesignerPunchField:"arrayCount",tubeDesignerPunchIndex:"0"}});
  verify(1000,6);
  await action("draft-change",{value:"1200",dataset:{tubeDesignerNestingPunchField:"length"}});
  verify(1200,6);
  await action("undo");verify(1000,6);
  await action("undo");verify(1000,5);
  await action("redo");verify(1000,6);
  await action("redo");verify(1200,6);
  await action("undo");verify(1000,6);
  await action("draft-change",{value:"1500",dataset:{tubeDesignerNestingPunchField:"length"}});
  verify(1500,6);assert.equal(state.future.length,0,"editing after Undo discards only the old redo branch");
  await action("redo");verify(1500,6);
});

await check("stock profile, per-profile dimensions and metadata share undo history without library mutations",async()=>{
  const libraryDrafts={"system:rect":{parameters:{width:42},untouched:"library draft"},"system:round":{parameters:{width:52}}};
  const {view,state,requests,action}=await fixture(libraryDrafts);
  state.features=[normalizePunchFeature({...margins,tailMargin:100,arrayCount:5})];
  const raw=structuredClone(state.features);
  const profile=(id)=>action("draft-change",{value:"system:"+id,dataset:{tubeDesignerNestingPunchField:"profileKey"}});
  const width=(value)=>action("main-profile-parameter",{value:String(value),dataset:{tubeDesignerMainProfileParameter:"width",tubeDesignerMainProfileValueType:"number"}});
  const verify=(id,value)=>{
    assert.equal(view.tubeDesignerNestingPunchPartDraft.profileKey,"system:"+id);
    assert.equal(requests.at(-1).profileRef.id,id);assert.equal(requests.at(-1).parameters.width,value);
    assert.equal(view.tubeDesignerNestingPunchPartDraft.diagramProfile.parameters.width,value);
    assert.equal(state.baseLength,1000);assert.deepEqual(state.features,raw);
    assert.match(renderNestingPunchPartDialog(view),new RegExp('<option value="system:'+id+'" selected>'));
    assert.match(renderNestingPunchPartDialog(view),new RegExp('value="'+value+'" data-cam-change-action="tube-designer-nesting-punch-create-main-profile-parameter"'));
    assert.deepEqual(view.tubeDesignerProfileDrafts,libraryDrafts,"main-tube edits cannot alter a separate library editor draft");
  };
  await width(60);verify("rect",60);
  await profile("round");verify("round",52);
  await width(70);verify("round",70);
  await action("undo");verify("round",52);
  await action("undo");verify("rect",60);
  await action("undo");verify("rect",42);
  await action("redo");verify("rect",60);
  await action("redo");verify("round",52);
  await action("redo");verify("round",70);
  const oldDraft=structuredClone(view.tubeDesignerNestingPunchPartDraft);
  for(const [field,value] of [["name","主管 A"],["material","Q235"],["quantity","3"]]) {
    await action("draft-change",{value,dataset:{tubeDesignerNestingPunchField:field}});
  }
  for(const field of ["quantity","material","name"]) {
    await action("undo");assert.equal(view.tubeDesignerNestingPunchPartDraft[field],oldDraft[field]);verify("round",70);
  }
  for(const [field,value] of [["name","主管 A"],["material","Q235"],["quantity","3"]]) {
    await action("redo");assert.equal(view.tubeDesignerNestingPunchPartDraft[field],value);verify("round",70);
  }
  const beforePopup=state.history.length;
  await action("parameters-open",{dataset:{tubeDesignerPunchIndex:"0"}});
  await action("draft-change",{value:"2500",dataset:{tubeDesignerNestingPunchField:"length"}});
  await width(99);assert.equal(state.history.length,beforePopup,"a modal transaction cannot also edit the main tube");
  await action("parameters-cancel");verify("round",70);
  assert.equal(state.creationInput.draft,view.tubeDesignerNestingPunchPartDraft,"cancel must rebind the stock input too");
  await width(75);verify("round",75);await action("undo");verify("round",70);
});

await check("skipped first/last holes distinguish unchanged candidates from enabled endpoints",()=>{
  const feature={...margins,arrayCount:5,tailMargin:100,skipInstancesText:"1,5"};
  const result=solved(feature,1000),html=renderPunchLayoutOverview(feature,1000);
  const active=result.layoutSummary.positions.filter((_,index)=>!result.skippedInstances.includes("0:"+index));
  assert.deepEqual(active,[300,500,700]);
  assert.match(html,/候选首孔中心端距 100 mm/);assert.match(html,/候选末孔中心端距 100 mm/);
  assert.match(html,/启用首孔中心端距 300 mm/);assert.match(html,/启用末孔中心端距 300 mm/);
  assert.match(html,/候选端距保留原布局规则/);assert.match(html,/不代表实际切孔边界/);
});

console.log(JSON.stringify({regressionScope:"production JS handlers and renderers; native response mocked; no desktop UI",mathCases,passed,failed:failures.length,failures},null,2));
if(failures.length)process.exitCode=1;
