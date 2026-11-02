import assert from 'node:assert/strict';
import {copyFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';

// Use the exported baseline before the browser regression edits cut 18.
const output=resolve('output/acceptance/selected-collision-20261102/full-head-pillar-cef');
mkdirSync(output,{recursive:true});
const source=resolve('output/acceptance/selected-collision-20261102/full-head-scoped-final/Pillar-before-edit.i5cam');
const project=resolve(output,'Pillar-current-full-head.i5cam');
const alreadyOpen=process.env.ICAX_SELECTED_HEAD_ALREADY_OPEN==='1';
if(!alreadyOpen)copyFileSync(source,project);
const browser=await chromium.connectOverCDP('http://127.0.0.1:9688');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/SDK/AppShell/index.html'));
page.setDefaultTimeout(240000);
const report={passed:false,standaloneCef:true,source,project},errors=[];
page.on('pageerror',e=>errors.push(e.message));
try{
 await page.waitForFunction(()=>__icaxAppShell.getState().startupPhase==='ready');
 if(alreadyOpen)assert.equal(await page.evaluate(()=>__icaxAppShell.getState().activeProjectPath),project);
 else await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
 await page.waitForFunction(async()=>{
  const app=__icaxAppShell.getState();if(app.pendingCount||!app.activeProjectId)return false;
  const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');
  window.pillarHeadView=getProjectView(app.activeProjectId);
  return !!pillarHeadView.scene?.domain;
 });
 const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!pillarHeadView.pending&&!pillarHeadView.fiveToolAxisRequest);
 const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();};
 await page.evaluate(()=>__icaxAppShell.selectRibbonTab('toolpath'));
 await command('five.cam-page.cutting');
 const baseline=await page.evaluate(()=>{
  const d=pillarHeadView.scene.domain,s=d.setups.find(x=>x.id===d.activeSetupId);
  return {cuts:s.generatedGeometry.operations.length,intent:s.generatedGeometry.operations.find(x=>x.order==='19')?.intentId,validity:s.simulationCheck.validity,head:s.headModel.instance};
 });
 assert.equal(baseline.cuts,21);assert.equal(baseline.validity,'Current');assert(baseline.intent);
 await page.locator(`[data-five-intent="${baseline.intent}"]`).click();await idle();
 await command('five.cam-inspector.collision');
 await page.waitForFunction(()=>pillarHeadView.fiveSimulation?.collisionReport?.segments?.length);
 const target=await page.evaluate(intent=>{
  const s=pillarHeadView.fiveSimulation,r=s.collisionReport;
  return r.segments.filter(x=>x.status!=='Unchecked'&&x.headPose&&r.frames[Number(x.index)]?.intentId===intent)
   .sort((a,b)=>Math.abs(Number(a.index)-s.cursor)-Math.abs(Number(b.index)-s.cursor))[0];
 },baseline.intent);
 assert(target,'The checked control cut must have a recorded pose to inspect');
 if(target.status==='Collision')await command('five.simulation.collision-result:'+target.index);
 else {
  let cursor=await page.evaluate(()=>pillarHeadView.fiveSimulation.cursor);
  for(let step=0;cursor!==Number(target.index)&&step<100;step++){
   await command(cursor>Number(target.index)?'five.simulation.previous':'five.simulation.next');
   cursor=await page.evaluate(()=>pillarHeadView.fiveSimulation.cursor);
  }
  assert.equal(cursor,Number(target.index));
 }
 await page.waitForFunction(()=>pillarHeadView.fiveSimulation.headAssembly?.userData.poseSource==='NativeCollision');
 report.observed=await page.evaluate(()=>{
  const v=pillarHeadView,s=v.fiveSimulation,r=s.collisionReport,d=v.scene.domain,setup=d.setups.find(x=>x.id===d.activeSetupId);
  const hit=r.segments.find(x=>Number(x.index)===Math.floor(s.cursor));s.group.updateMatrixWorld(true);
  return {selected:setup.headModel.instance,checked:r.headCoverage.headInstanceId,scope:r.headCoverage.scope,
   parts:s.headAssembly.children.map(x=>x.name),boxes:r.headBoxCount,
   partErrors:s.headAssembly.children.map((m,i)=>Math.max(...m.matrixWorld.clone().transpose().toArray().map((n,k)=>Math.abs(n-hit.headPose.partTransforms[i][k])))),
   poseSource:s.headAssembly.userData.poseSource,error:s.headPoseError,motion:r.headCoverage.motion,
   solves:r.headCoverage.inverseSolveCount,keyposes:r.headCoverage.keyposeCount,
   checkedIntent:r.checkedIntentId,checkedSegments:r.checkedSegmentCount,collisionCount:r.collisionCount};
 });
 const o=report.observed;
 assert.equal(o.selected.id,o.checked);assert.equal(o.checked,'54cbb3f4-df63-4f4f-9a09-d2bbbc12f6f4');
 assert.equal(o.checkedIntent,baseline.intent);assert.equal(o.parts.length,2);assert.equal(o.boxes,'9');
 assert.equal(o.scope,'CompleteSelectedHeadAssembly');assert.equal(o.motion,'KeyposeJointInterpolationWithOriginalTCP');
 assert(o.partErrors.every(e=>e<1e-7));assert.equal(o.error,'');assert.deepEqual(errors,[]);
 await command('five.simulation.collision-focus');
 await page.screenshot({path:resolve(output,'pillar-selected-head.png')});report.passed=true;
}catch(e){report.error=e.stack;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});process.exitCode=1;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();}
