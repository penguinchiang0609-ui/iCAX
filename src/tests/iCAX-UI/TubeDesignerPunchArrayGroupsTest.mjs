import assert from "node:assert/strict";
import { resolvePunchArrayGroups, normalizePunchArrayGroup, migrateLegacyPunchArrays,
  parsePunchArraySkipText, punchArraySkipText, punchArrayGroupInstanceCount, validatePunchArrayGroups } from "../../apps/tube-designer/webpage/punchArrayGroups.mjs";
import { resolvePunchLayout } from "../../apps/tube-designer/webpage/punchLayout.mjs";
import { createPunchWizardState, normalizePunchFeature, resolvePunchDistribution, openPunchParameters,
  closePunchParameters, editPunchWizardFeature, validatePunchWizard, addPunchWizardFeature } from "../../apps/tube-designer/webpage/punchWizard.mjs";
import { handlePunchArrayGroupAction } from "../../apps/tube-designer/webpage/punchArrayGroupActions.mjs";

let cases=0;
const I=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
const group=(id,axis,count=2,spacing=50,extra={})=>({id,type:"linear",axis,count,spacing,...extra});
function resolve(feature,length=1000) {
  const result=resolvePunchArrayGroups(feature,length);
  assert.equal(result.arrayGroupsError,"",JSON.stringify(feature));
  assert.deepEqual(resolvePunchArrayGroups(result,length),result,"Explicit arrays must be idempotent.");
  assert.deepEqual(result.arrayCount,1);assert.deepEqual(result.rowCount,1);
  assert.deepEqual(result.arrayOffsets,[0]);assert.deepEqual(result.rowOffsets,[0]);
  for(const m of result.arrayTransforms) {
    assert.equal(m.length,16);assert.ok(m.every(Number.isFinite));
    assert.deepEqual(m.slice(12),[0,0,0,1]);
    for(let r=0;r<3;r++)for(let c=0;c<3;c++)assert.ok(Math.abs([0,1,2].reduce((sum,k)=>sum+m[r*4+k]*m[c*4+k],0)-(r===c?1:0))<1e-8);
  }
  cases++;return result;
}
function invalid(feature,match,length=1000) {
  const result=resolvePunchArrayGroups(feature,length);
  assert.match(result.arrayGroupsError,match);assert.deepEqual(result.arrayTransforms,[]);
  assert.equal(punchArrayGroupInstanceCount(feature,length),0);cases++;
}
const apply=(m,p)=>[0,1,2].map(row=>[0,1,2].reduce((sum,k)=>sum+m[row*4+k]*p[k],m[row*4+3]));
const near=(a,b)=>{assert.equal(a.length,b.length);a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-8,`${a} != ${b}`));};

const legacy={station:200,reference:"end",arrayCount:3,rowCount:2,arrayPitch:50,rowPitch:20,skipInstancesText:"2:3"};
assert.deepEqual(resolvePunchArrayGroups(legacy,1000),legacy,"Absence of arrayGroups never implicitly migrates old recipes.");cases++;
assert.equal(punchArrayGroupInstanceCount(legacy,1000),5);
assert.deepEqual(resolve({station:300,arrayGroups:[]}).arrayTransforms,[I]);
assert.deepEqual(resolve({arrayGroups:[group("off","Y",NaN,NaN,{enabled:false})]}).arrayTransforms,[I]);
const center=resolve({station:100,reference:"center",offset:17,rotation:23,arrayCount:99,rowCount:99,arrayGroups:[group("x","X")]});
assert.equal(center.station,100);assert.equal(center.reference,"center");assert.equal(center.offset,17);assert.equal(center.rotation,23);
assert.equal(center.arrayGroupsSummary.seedX,600);
assert.deepEqual(center.arrayTransforms.map(m=>m[3]),[0,50]);
assert.deepEqual(center.arrayTransforms.map(m=>apply(m,[600,17,0])[0]),[600,650]);
assert.deepEqual(resolve({...center,reference:"end",station:100}).arrayTransforms.map(m=>m[3]),[0,50],"Explicit direction is independent of the pose reference.");

// Each of the nine existing length rules remains available inside an X group.
const rules=[
  {distributionMode:"pitch",count:4,spacing:150},
  {distributionMode:"equal",count:4},
  {distributionMode:"middle-fixed",count:5,spacing:100},
  {distributionMode:"end-margins",headMargin:100,tailMargin:150,count:6},
  {distributionMode:"center-out",count:6,spacing:100,centerMode:"gap",centerFirstOffset:null},
  {distributionMode:"fill",headMargin:100,tailMargin:100,spacing:180,fillAlign:"center"},
  {distributionMode:"max-spacing",headMargin:100,tailMargin:100,maxSpacing:180},
  {distributionMode:"sequence",spacingSequence:"50*3,100*2"},
  {distributionMode:"positions",positionList:"900 100 500 -20 1020"},
];
for(const rule of rules)for(const length of [1000,1200]) {
  const expected=resolvePunchLayout({...rule,station:125,arrayCount:rule.count,arrayPitch:rule.spacing},length);
  assert.equal(expected.layoutError,"");
  const actual=resolve({station:125,arrayGroups:[group("x","X",rule.count,rule.spacing,rule)]},length);
  near(actual.arrayTransforms.map(m=>125+m[3]),expected.layoutSummary.positions);
}
assert.deepEqual(resolve({station:900,arrayGroups:[group("x","X",3,100,{direction:"negative"})]}).arrayTransforms.map(m=>m[3]),[0,-100,-200]);
assert.deepEqual(resolve({station:900,arrayGroups:[group("x","X",3,100,{direction:"negative",distributionMode:"sequence",spacingSequence:"50 100"})]}).arrayTransforms.map(m=>m[3]),[0,-50,-150]);
assert.deepEqual(resolve({station:980,arrayGroups:[group("x","X",3,15)]}).arrayTransforms.map(m=>980+m[3]),[980,995,1010],"End-clipped instances remain a native intersection decision.");

const xy=resolve({arrayGroups:[group("x","X",2,50),group("y","Y",3,20)]});
assert.deepEqual(xy.arrayTransforms.map(m=>[m[3],m[7],m[11]]),[[0,0,0],[50,0,0],[0,20,0],[50,20,0],[0,40,0],[50,40,0]]);
assert.deepEqual(xy.arrayInstances.map(i=>i.indices),[{x:0,y:0},{x:1,y:0},{x:0,y:1},{x:1,y:1},{x:0,y:2},{x:1,y:2}]);
const polar=(axis,extra={})=>({id:"p",type:"polar",axis,count:4,angleMode:"full-circle",startAngle:0,...extra});
for(const axis of ["X","Y","Z"]) {
  const ring=resolve({arrayGroups:[polar(axis)]});
  assert.deepEqual(ring.arrayGroupsSummary.groups[0].values,[0,90,180,270]);
  near(apply(ring.arrayTransforms[0],[550,10,20]),[550,10,20]);
}
near(apply(resolve({arrayGroups:[polar("X")]}).arrayTransforms[1],[500,10,20]),[500,-20,10]);
near(apply(resolve({arrayGroups:[polar("Y")]}).arrayTransforms[1],[550,10,20]),[520,10,-50]);
near(apply(resolve({arrayGroups:[polar("Z")]}).arrayTransforms[1],[550,10,20]),[490,50,20]);
near(apply(resolve({arrayGroups:[polar("Z",{origin:[20,30,40]})]}).arrayTransforms[1],[30,30,40]),[20,40,40]);
const xp=resolve({station:500,arrayGroups:[group("x","X",2,50),polar("Z")]});
const px=resolve({station:500,arrayGroups:[polar("Z"),group("x","X",2,50)]});
near(apply(xp.arrayTransforms[3],[500,0,0]),[500,50,0]);
near(apply(px.arrayTransforms[5],[500,0,0]),[550,0,0]);
assert.notDeepEqual(xp.arrayTransforms,px.arrayTransforms,"Later groups left-multiply, so changing group order is intentional.");
assert.deepEqual(resolve({arrayGroups:[polar("X",{angleMode:"angle-range",startAngle:30,endAngle:390})]}).arrayGroupsSummary.groups[0].values,[30,120,210,300]);
assert.deepEqual(resolve({arrayGroups:[polar("X",{angleMode:"angle-range",startAngle:360,endAngle:0})]}).arrayGroupsSummary.groups[0].values,[360,270,180,90]);
assert.deepEqual(resolve({arrayGroups:[polar("X",{angleMode:"angle-range",count:3,startAngle:0,endAngle:180})]}).arrayGroupsSummary.groups[0].values,[0,90,180]);
assert.equal(resolve({arrayGroups:[polar("X",{count:1,angleMode:"angle-range",startAngle:360,endAngle:0})]}).arrayCandidateCount,1);
invalid({arrayGroups:[polar("X",{angleMode:"pitch",count:2,angleStep:360})]},/重复角度/);
invalid({arrayGroups:[polar("X",{angleMode:"angle-range",count:1,startAngle:0,endAngle:90})]},/只有一个/);
invalid({arrayGroups:[polar("X",{angleMode:"angle-range",endAngle:361})]},/一整圈/);

const skipped=resolve({...xy,arraySkips:[{x:1,y:2}]});
assert.equal(skipped.arrayCandidateCount,6);assert.equal(skipped.arrayTransforms.length,5);
assert.equal(punchArraySkipText(skipped),"2:3");
assert.deepEqual(parsePunchArraySkipText("2:3,1:*",skipped.arrayGroups),[{x:1,y:2},{x:0}]);
const three=resolve({...skipped,arrayGroups:[...skipped.arrayGroups,group("z","Z",2,10)]});
assert.equal(three.arrayTransforms.length,10,"A migrated partial selector applies across subsequently added axes.");
const reordered=resolve({...three,arrayGroups:[three.arrayGroups[2],three.arrayGroups[0],three.arrayGroups[1]]});
assert.equal(reordered.arrayTransforms.length,10);assert.equal(punchArraySkipText(reordered),"*:2:3");
const dormant=resolve({...skipped,arrayGroups:skipped.arrayGroups.map(g=>g.id==="y"?{...g,enabled:false}:g)});
assert.equal(dormant.arrayTransforms.length,2);assert.equal(dormant.arrayGroupsSummary.dormantSkipCount,1);
assert.equal(resolve({...skipped,arraySkipText:"2:*"}).arrayTransforms.length,3);
invalid({...skipped,arraySkips:[{x:0},{x:1}]},/所有阵列组合/);
invalid({...skipped,arraySkips:[{missing:1}]},/不存在/);
invalid({...skipped,arraySkips:[{x:2}]},/范围/);
invalid({...skipped,arraySkips:[{x:1},{x:1}]},/重复/);
invalid({...skipped,arraySkipText:"*:*"},/所有阵列组合/);
invalid({...skipped,arraySkipText:"0:1"},/正整数/);
invalid({...skipped,arraySkipText:"eval(1):1"},/填写/);
assert.deepEqual(resolve(JSON.parse(JSON.stringify(three))),three,"JSON recipe round trips preserve indices and matrices.");

const migrated=migrateLegacyPunchArrays(legacy,1000);
assert.equal(legacy.arrayGroups,undefined);assert.equal(migrated.arrayGroups[0].direction,"negative");
assert.deepEqual(migrated.arraySkips,[{"legacy-length":2,"legacy-rows":1}]);
const migratedResult=resolve(migrated);
assert.deepEqual(migratedResult.arrayTransforms.map(m=>[m[3],m[7]]),[[0,0],[-50,0],[-100,0],[0,20],[-50,20]]);
for(const rule of rules) {
  const old={station:125,...rule,arrayCount:rule.count??1,arrayPitch:rule.spacing??50};
  const solved=resolvePunchLayout(old,1000),migrated=resolve(migrateLegacyPunchArrays(old,1000));
  near(migrated.arrayTransforms.map(m=>migrated.arrayGroupsSummary.seedX+m[3]),solved.layoutSummary.positions);
}
for(const face of ["top","bottom","left","right"]) {
  const moved=resolve(migrateLegacyPunchArrays({station:500,face,rowCount:2,rowPitch:-20},1000));
  const component=["left","right"].includes(face)?11:7;
  assert.deepEqual(moved.arrayTransforms.map(m=>m[component]),[0,-20]);
  const partMoved=resolve(migrateLegacyPunchArrays({toolTarget:"part",station:500,face,rowCount:2,rowPitch:-20},1000));
  assert.deepEqual(partMoved.arrayTransforms.map(m=>m[face==="left"?11:7]),[0,-20],"Part-coordinate legacy rows follow the native left-only Z mapping.");
}
const round=resolve(migrateLegacyPunchArrays({station:500,face:"round",offset:17,rowDistributionMode:"full-circle",rowStartAngle:30,rowCount:4},1000));
assert.equal(round.offset,0);assert.deepEqual(round.arrayGroupsSummary.groups[1].values,[30,120,210,300]);
const phase=resolve(migrateLegacyPunchArrays({station:500,face:"round",offset:17,rowPitch:90,rowCount:4},1000));
assert.equal(phase.offset,17);assert.deepEqual(phase.arrayGroupsSummary.groups[1].values,[0,90,180,270]);

assert.equal(resolve({arrayGroups:[group("x","X",1000,1)]}).arrayTransforms.length,1000);
assert.equal(resolve({opposite:true,arrayGroups:[group("x","X",500,1)]}).arrayGroupsSummary.expandedCount,1000);
assert.equal(resolve({opposite:true,through:true,arrayGroups:[group("x","X",1000,1)]}).arrayTransforms.length,1000);
invalid({arrayGroups:[group("x","X",1001,1)]},/1000/);
invalid({arrayGroups:[group("x","X",100,1),group("y","Y",11,1)]},/笛卡尔组合/);
invalid({arrayGroups:[group("x","X",2,50),group("x2","X",2,50)]},/完全重复/);
assert.equal(resolve({arrayGroups:[group("x","X",2,50),group("x2","X",2,50)],arraySkips:[{x:1,x2:0}]}).arrayTransforms.length,3,"Skip selectors may explicitly remove a coincident Cartesian member.");
invalid({opposite:true,arrayGroups:[group("x","X",501,1)],arraySkips:[{x:0}]},/跳过组合仍计入/);
invalid({arrayGroups:[group("x","X",2,0)]},/不能为零/);
invalid({arrayGroups:[group("y","Y",2,0)]},/非零/);
invalid({arrayGroups:[group("x","normal",2,50)]},/世界 X/);
invalid({arrayGroups:[group("x","X",NaN,50)]},/整数/);
invalid({arrayGroups:[group("x","X",2,NaN)]},/有效数字/);
invalid({arrayGroups:[group("x","X",2,50),group("x","Y",2,20)]},/唯一/);
invalid({arrayGroups:[null]},/参数对象/);
invalid({arrayGroups:{}},/有效列表/);
invalid({station:Infinity,arrayGroups:[]},/有效数字/);
invalid({arrayGroups:[polar("X",{origin:[0,NaN,0]})]},/三个有效坐标/);
invalid({arrayGroups:[group("x","X",2,50,{distributionMode:"sequence",spacingSequence:"globalThis.arrayPwned=true"})]},/有效数字/);
assert.equal(globalThis.arrayPwned,undefined);
assert.equal(validatePunchArrayGroups({enabled:false,arrayGroups:[null]},1000),"");
assert.equal(normalizePunchArrayGroup({centerFirstOffset:" "}).centerFirstOffset,null);

// Independent array edits use one cancellable transaction, never renderer writes.
const part={entityId:"arrays",length:1000,profile:{kind:"rect",width:40,depth:20}};
const view={pending:false,tubeDesignerPunchWizard:createPunchWizardState(part)},s=view.tubeDesignerPunchWizard;
s.features=[normalizePunchFeature({...legacy,type:"circle",diameter:10,layoutDatum:"base"})];
const original=structuredClone(s.features[0]);
assert.equal(openPunchParameters(view,"0","","pose"),true);assert.equal(s.parameterEditor.mode,"pose");
assert.equal(s.features[0].arrayGroups,undefined);assert.equal(closePunchParameters(view,false,part),true);
assert.equal(openPunchParameters(view,"0","","arrays"),true);assert.equal(s.parameterEditor.mode,"arrays");
assert.ok(s.features[0].arrayGroups);assert.equal(s.history.length,0);
const change=(action,fields={},value="",checked=false)=>handlePunchArrayGroupAction(view,action,{dataset:{tubeDesignerPunchIndex:"0",...fields},value,checked});
assert.equal(change("array-group-add",{tubeDesignerPunchArrayType:"polar",tubeDesignerPunchArrayAxis:"Z"}).changed,true);
assert.equal(s.features[0].arrayGroups.length,3);
assert.equal(closePunchParameters(view,false,part),true);assert.deepEqual(s.features[0],original);assert.equal(s.history.length,0);
assert.deepEqual(s.restoreParameterFocus,{index:"0",end:"",mode:"arrays"});
openPunchParameters(view,"0","","arrays");
change("array-group-add",{tubeDesignerPunchArrayType:"linear",tubeDesignerPunchArrayAxis:"Z"});
const addedId=s.features[0].arrayGroups.at(-1).id;
const field=(name,value,checked=false)=>change("array-group-field",{tubeDesignerPunchArrayGroup:addedId,tubeDesignerPunchArrayField:name},value,checked);
field("spacing","12");field("originAuto","",false);field("originY","30");
assert.deepEqual(s.features[0].arrayGroups.at(-1).origin,[500,30,0]);
field("originAuto","",true);assert.equal(s.features[0].arrayGroups.at(-1).origin,null);
field("centerFirstOffset"," ");assert.equal(s.features[0].arrayGroups.at(-1).centerFirstOffset,null);
assert.equal(s.history.length,0);assert.equal(closePunchParameters(view,true,part),true);assert.equal(s.history.length,1);
const committed=structuredClone(s.features[0]);
assert.equal(editPunchWizardFeature(view,"undo"),true);assert.deepEqual(s.features[0],original);
assert.equal(editPunchWizardFeature(view,"redo"),true);assert.deepEqual(s.features[0],committed);
assert.equal(change("array-group-add").changed,false,"No array writes outside its transaction.");
openPunchParameters(view,"0","","arrays");
view.pending=true;assert.equal(field("count","99").changed,false);view.pending=false;
assert.equal(handlePunchArrayGroupAction(view,"array-group-add",{dataset:{tubeDesignerPunchIndex:"draft"}}).changed,false);
field("count","0");assert.equal(closePunchParameters(view,true,part),false);assert.match(s.parameterEditor.error,/整数/);
closePunchParameters(view,false,part);
openPunchParameters(view,"0","","arrays");
change("array-skips-change",{},"2:3:1");
change("array-group-remove",{tubeDesignerPunchArrayGroup:"legacy-rows"});
assert.deepEqual(s.features[0].arraySkips,[],"Removing a referenced group removes its selectors, not broadens them to wildcards.");
closePunchParameters(view,false,part);

const feature=n=>normalizePunchFeature({type:"circle",diameter:10,arrayGroups:[group("g","X",n,1)]});
s.features=[feature(600),feature(401)];assert.match(validatePunchWizard(view,part),/1000/);
s.features=[feature(600),feature(400)];assert.equal(validatePunchWizard(view,part),"");
s.features=[feature(600)];s.draft=feature(401);assert.equal(addPunchWizardFeature(view,part),false);
s.draft=feature(400);assert.equal(addPunchWizardFeature(view,part),true);
assert.equal(resolvePunchDistribution(feature(3),1000).arrayTransforms.length,3);
console.log(`Punch independent array groups: ${cases} mathematical scenarios plus transaction/budget regressions passed.`);
