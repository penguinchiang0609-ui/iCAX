// Creation layout mathematics and exact preview-counter ownership.
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

const margins={id:"margins",type:"circle",diameter:10,layoutDatum:"base",distributionMode:"end-margins",headMargin:100,tailMargin:150,arrayCount:6};
await check("changing length invalidates old exact counters until a matching preview returns",()=>{
  const state={baseLength:1000,revision:2,features:[normalizePunchFeature(margins)],tools:[],catalogueStatus:"ready",preview:{revision:1,length:1000,toolCountExact:true,appliedPunchToolCount:6,outsideToolCount:0,endToolCount:0}};
  assert.equal(buildPunchReviewSummary(state).exactPreviewCounts,false);
  state.preview.revision=2;assert.equal(buildPunchReviewSummary(state).exactPreviewCounts,true);
  state.previewPending=true;assert.equal(buildPunchReviewSummary(state).exactPreviewCounts,false);
  state.previewPending=false;state.parameterEditor={index:"0"};assert.equal(buildPunchReviewSummary(state).exactPreviewCounts,false);
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

console.log(JSON.stringify({regressionScope:"pure creation layout solver and review helpers; current batch workflow covered separately",mathCases,passed,failed:failures.length,failures},null,2));
if(failures.length)process.exitCode=1;
