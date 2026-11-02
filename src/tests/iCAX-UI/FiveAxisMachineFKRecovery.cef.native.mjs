import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out=resolve(process.env.FK_RECOVERY_OUTPUT||'output/work/machine-kinematics/machine-pose-collision-20261024');mkdirSync(out,{recursive:true});
const browser=await chromium.connectOverCDP(process.env.MACHINE_CDP||'http://127.0.0.1:9500'),page=browser.contexts()[0].pages()[0];
const settle=()=>page.waitForFunction(()=>{const s=mv.fiveMachineIK,p=mv.fiveMachineParameterEdits;return !mv.pending&&!mv.fiveMachine?.busy&&!__icaxAppShell.getState().pendingCount&&!s?.gesture&&!s?.inflight&&!s?.finishing&&!s?.fileSaving&&!s?.editTask&&!p?.request&&!p?.fileSaving&&![...(p?.entries.values()||[])].some(e=>e.timer||e.ready);},null,{timeout:30000});
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await settle();assert.equal(await page.evaluate(()=>mv.fiveMachine.error||''),'',id);};
const snapshot=()=>page.evaluate(()=>{const m=mv.scene.machines[0];return {machineId:m.entityId,setupId:mv.scene.domain.activeSetupId,joints:Object.fromEntries(m.joints.map(j=>[j.jointName,{id:j.entityId,position:j.position,type:j.type,lower:j.lower,upper:j.upper}]))};});
async function bind(path){await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);await page.waitForFunction(async path=>{if(__icaxAppShell.getState().activeProjectPath.replaceAll('\\','/')!==path.replaceAll('\\','/'))return false;const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.fixture={view:mv};return mv?.scene?.machines?.length&&!mv.pending&&!__icaxAppShell.getState().pendingCount;},path);await page.waitForTimeout(400);await page.locator('[data-tab-id="simulation"]').click();await settle();await command('five.tool.machine-instances');}
async function select(name){const s=await snapshot();await command('five.machine.part:'+s.joints[name].id);return s.joints[name].id;}
async function directFK(name,position){const s=await snapshot();return page.evaluate(async({s,name,position})=>mv.viewportSceneProxy.invoke('CamSimulation.MoveMachineIK',{expected:mv.scene.domain.expected,machineId:s.machineId,tipId:s.joints[name].id,motionMode:'FK',jointPosition:position}),{s,name,position});}
try{
 await page.setViewportSize({width:1600,height:1000});await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');const cdp=await page.context().newCDPSession(page);await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:true});
 await bind(resolve(out,'user-pose-copy.i5cam'));
 if(process.argv.includes('--baseline')){
  const s=await snapshot(),results=[];for(let i=0;i<3;++i){const start=Date.now(),r=await directFK('C',s.joints.C.position+.01);results.push({status:r.status,collisionStatus:r.collisionStatus,reason:r.reason,collisionMs:r.collisionMilliseconds,wallMs:Date.now()-start});await command('five.refresh');}
  writeFileSync(resolve(out,'before-fix.json'),JSON.stringify({results,userReported:JSON.parse(readFileSync(resolve(out,'user-state.json'),'utf8')).lastResult},null,2));console.log(JSON.stringify({baseline:true,results}));
 }else{
  const evidence=[];
  for(const name of ['C','A']){
   await select(name);await command('five.machine-ik.fk');await page.evaluate(()=>{mv.viewport.setViewDirection([1,-1.5,.8]);mv.viewport.fitViewToViewport(1);});
   for(let i=0;i<10;++i){
    const before=await snapshot(),previous=await page.evaluate(()=>mv.fiveMachineIK.lastResult?.editToken);await gizmoDriver(page).drag('Z',i%2?-4:4);await settle();
    const r=await page.evaluate(()=>({status:mv.fiveMachineIK.lastResult?.status,collision:mv.fiveMachineIK.lastResult?.collisionStatus,token:mv.fiveMachineIK.lastResult?.editToken,collisionMs:mv.fiveMachineIK.lastResult?.collisionMs,message:mv.fiveMachineIK.message,blocked:mv.fiveMachineIK.blocked})),after=await snapshot();
    assert.notEqual(r.token,previous,'each real pointer gesture starts a new native transaction');assert.equal(r.status,'Ready',JSON.stringify(r));assert.equal(r.collision,'Clear');assert.equal(r.message,'');assert.ok(!r.blocked);assert.ok(Math.abs(after.joints[name].position-before.joints[name].position)>1e-6);
    for(const [other,joint] of Object.entries(before.joints))if(other!==name)assert.equal(after.joints[other].position,joint.position,'FK only edits its selected axis');
    evidence.push({kind:'pointer-fk',axis:name,index:i,collisionMs:r.collisionMs});
   }
   // Cancel an accepted live gesture, then immediately start a fresh one.
   const cancelBase=await snapshot();await gizmoDriver(page).drag('Z',4,false);await page.waitForFunction(()=>mv.fiveMachineIK.gesture?.stream?.processedSequence>0n);await page.keyboard.press('Escape');await page.mouse.up();await settle();assert.deepEqual((await snapshot()).joints,cancelBase.joints);
   await gizmoDriver(page).drag('Z',-4);await settle();assert.equal(await page.evaluate(()=>mv.fiveMachineIK.lastResult?.status),'Ready');evidence.push({kind:'escape-and-retry',axis:name});
  }
  for(const name of ['X','Y','Z']){const s=await snapshot(),id=await select(name),target=s.joints[name].position+(name==='Z'?-1:1);const field=page.locator(`.cam-info-pane [data-five-machine-field="joint:${id}"]`);await field.fill(String(target));await field.blur();await settle();assert.equal(await page.evaluate(()=>mv.fiveMachine.error||''),'');assert.ok(Math.abs((await snapshot()).joints[name].position-target)<1e-7);evidence.push({kind:'numeric-axis',axis:name});}
  // A real tool/table interference still must reject the move atomically.
  const beforeCollision=await snapshot(),collision=await page.evaluate(async s=>mv.viewportSceneProxy.invoke('CamSimulation.MoveMachine',{expected:mv.scene.domain.expected,setupId:s.setupId,positions:{[s.joints.Z.id]:s.joints.Z.lower}}),beforeCollision);
  assert.equal(collision.collisionStatus,'Blocked',JSON.stringify(collision));await command('five.refresh');assert.deepEqual((await snapshot()).joints,beforeCollision.joints);evidence.push({kind:'real-collision-rejected',status:collision.collisionStatus,reason:collision.reason});
  const retry=await directFK('C',beforeCollision.joints.C.position+.005);assert.equal(retry.status,'Ready',JSON.stringify(retry));assert.equal(retry.collisionStatus,'Clear');await command('five.refresh');
  const times=evidence.filter(x=>x.kind==='pointer-fk').map(x=>x.collisionMs);writeFileSync(resolve(out,'acceptance.json'),JSON.stringify({pass:true,evidence,pointerGestures:times.length,minimumCollisionMs:Math.min(...times),maximumCollisionMs:Math.max(...times)},null,2));await page.screenshot({path:resolve(out,'working.png')});console.log(JSON.stringify({pass:true,pointerGestures:times.length,maxCollisionMs:Math.max(...times),cancelAndRetry:true,numericXYZ:true,realCollisionRejected:true}));
 }
}catch(error){await page.screenshot({path:resolve(out,'failure.png')}).catch(()=>{});console.error(error);process.exitCode=1;}
finally{await browser.close();}
