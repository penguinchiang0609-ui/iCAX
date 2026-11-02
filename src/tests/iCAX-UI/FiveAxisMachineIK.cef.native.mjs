import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {writeFileSync,readFileSync} from 'node:fs';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
try{
 const b=await chromium.connectOverCDP('http://127.0.0.1:9482'),page=b.contexts()[0].pages()[0],out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation',path=out+(process.argv.includes('--reopen')?'/ik-adjusted.i5cam':'/六轴机械臂SDF演示.i5cam');
 await page.setViewportSize({width:1600,height:1000});await page.waitForFunction(()=>__icaxAppShell.getState().startupPhase==='ready');
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
 await page.waitForFunction(async path=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.fixture={view:mv};return __icaxAppShell.getState().activeProjectPath===path&&mv?.scene?.machines?.length>=2&&!mv.pending;},path);
 await page.locator('[data-tab-id="simulation"]').click();await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();await page.waitForFunction(()=>mv?.scene?.machines&&!mv.pending);
 if(process.argv.includes('--reopen')){const saved=JSON.parse(readFileSync(out+'/machine-ik-acceptance.json','utf8')).saved;const actual=await page.evaluate(id=>mv.scene.machines.find(m=>m.entityId===id).joints.map(j=>({id:j.entityId,p:j.position})),saved.id);assert.deepEqual(actual,saved.positions);console.log('PASS IK adjusted joint positions survive process restart and project reopen');process.exit(0);}
 // Create a clean ordinary instance so older jogging sessions cannot leave the
 // test at a travel stop or a singular wrist configuration.
 const robot=await page.evaluate(async()=>{const d=mv.scene.machineDefinitions.find(d=>d.name.includes('IR-1600'));const r=await mv.viewportSceneProxy.invoke('Machine.Instantiate',{machineDefinitionId:d.id});return r.machine.entityId;});
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));await page.waitForFunction(()=>!mv.pending);
 await page.locator('[data-five-machine-instance]').selectOption(robot);await page.waitForFunction(()=>!mv.fiveMachine.busy);
 const initial=await page.evaluate(async()=>{const m=mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId),j=m.joints.find(j=>j.jointName==='J5');return await mv.viewportSceneProxy.invoke('CamSimulation.MoveMachine',{expected:mv.scene.domain.expected,machineId:m.entityId,positions:{[j.entityId]:.3}});});assert.equal(initial.status,'Ready');
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));await page.waitForFunction(()=>!mv.pending);
 const tip=await page.evaluate(()=>mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId).joints.find(j=>j.jointName==='J6').entityId);
 await page.locator(`[data-five-command="five.machine.part:${tip}"]`).click();await page.locator('[data-command-id="five.machine-ik.translate"]').click();
 await page.evaluate(()=>{mv.viewport.setViewDirection([1,-1.5,.8]);mv.viewport.fitViewToViewport(1);window.ikCanvas=mv.viewport.renderer.domElement;window.ikObjects=new Map(mv.viewport.sceneObjects);window.ikRight=document.querySelector('.cam-info-pane');});await page.waitForTimeout(150);
 const before=await page.evaluate(()=>mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId).joints.map(j=>j.position));
 await gizmoDriver(page).drag('X',8);await page.waitForFunction(()=>!mv.fiveMachineIK.inflight&&!mv.fiveMachineIK.pending);
 const translation=await page.evaluate(()=>({result:mv.fiveMachineIK.lastResult,message:mv.fiveMachineIK.message,positions:mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId).joints.map(j=>j.position),roundTrip:mv.fiveMachineIK.lastRoundTripMs}));
 assert.equal(translation.result.status,'Ready',JSON.stringify(translation));assert.equal(translation.result.solver,'Orocos KDL');assert.ok(translation.positions.filter((v,i)=>Math.abs(v-before[i])>1e-6).length>=2,'multiple parent joints must move');
 await page.locator('[data-command-id="five.machine-ik.rotate"]').click();await gizmoDriver(page).drag('Y',8);await page.waitForFunction(()=>!mv.fiveMachineIK.inflight&&!mv.fiveMachineIK.pending);
 const rotation=await page.evaluate(()=>({result:mv.fiveMachineIK.lastResult,message:mv.fiveMachineIK.message,roundTrip:mv.fiveMachineIK.lastRoundTripMs,positions:mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId).joints.map(j=>j.position)}));assert.notDeepEqual(rotation.positions,translation.positions);assert.equal(rotation.result.status,'Ready',JSON.stringify(rotation));
 const native=await page.evaluate(async tip=>{
  const m=mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId),invoke=(name,args)=>mv.viewportSceneProxy.invoke(name,args),read=()=>invoke('Machine.GetElement',{entityId:tip});
  const snap=await invoke('CamSimulation.CheckMachineMove',{expected:mv.scene.domain.expected,machineId:m.entityId,positions:{[tip]:m.joints.find(j=>j.entityId===tip).position}}),{machineForward}=await import('../../../apps/five-axis-cam/webpage/machineKinematics.mjs'),index=snap.nodes.findIndex(n=>n.id===tip);
  const matrix=machineForward(snap,snap.positions)[index],target=[0,4,8,12,1,5,9,13,2,6,10,14,3,7,11,15].map(i=>matrix.elements[i]);
  const base=await read();const impossible=[...target];impossible[3]+=100000;
  const rejected=await invoke('CamSimulation.MoveMachineIK',{expected:mv.scene.domain.expected,machineId:m.entityId,tipId:tip,target:impossible}),after=await read();
  const timing=[];for(let i=0;i<25;i++){const q=[...snap.positions];q[snap.nodes.findIndex(n=>n.id===m.joints.find(j=>j.jointName==='J2').entityId)]+=(i%2?.001:0);const w=machineForward(snap,q)[index];const movingTarget=[0,4,8,12,1,5,9,13,2,6,10,14,3,7,11,15].map(k=>w.elements[k]);const start=performance.now();const r=await invoke('CamSimulation.MoveMachineIK',{expected:mv.scene.domain.expected,machineId:m.entityId,tipId:tip,target:movingTarget});timing.push({solve:r.solveMilliseconds,total:r.totalMilliseconds,roundTrip:performance.now()-start,status:r.status});}
  const element=(await read()).machineElement,boxes=element.collisions.map(c=>({name:c.name,position:c.pose.slice(0,3).map(v=>v*1000),rotationRadians:c.pose.slice(3),size:c.geometry.size.map(v=>v*1000)}));
  const readJoints=async()=>Promise.all(m.joints.map(j=>invoke('Machine.GetElement',{entityId:j.entityId}).then(r=>r.machineElement.joint.position)));
  const beforeCollision=await readJoints();let collision;
  try{await invoke('Machine.SetBoxColliders',{entityId:tip,boxes:[{name:'IK collision regression',position:[0,0,0],rotationRadians:[0,0,0],size:[10000,10000,10000]}]});collision=await invoke('CamSimulation.MoveMachineIK',{expected:mv.scene.domain.expected,machineId:m.entityId,tipId:tip,target});}
  finally{await invoke('Machine.SetBoxColliders',{entityId:tip,boxes});}
  const collisionHeld=JSON.stringify(beforeCollision)===JSON.stringify(await readJoints());
  return {collision,collisionHeld,rejected,noMutation:JSON.stringify(base)===JSON.stringify(after),timing,stableCanvas:ikCanvas===mv.viewport.renderer.domElement,stableRight:ikRight===document.querySelector('.cam-info-pane'),stableObjects:[...ikObjects].every(([id,o])=>mv.viewport.sceneObjects.get(id)===o)};
 },tip);
 assert.equal(native.collision.collisionStatus,'Blocked');assert.equal(native.collisionHeld,true);assert.equal(native.rejected.status,'Unreachable');assert.equal(native.noMutation,true);assert.ok(native.timing.every(t=>t.status==='Ready'));for(const k of ['stableCanvas','stableRight','stableObjects'])assert.equal(native[k],true,k);
 const p95=key=>native.timing.map(t=>t[key]).sort((a,b)=>a-b)[Math.floor(native.timing.length*.95)];
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));await page.waitForFunction(()=>!mv.pending);const saved=await page.evaluate(async path=>{await mv.viewportSceneProxy.invoke('Project.Save',{projectPath:path});const m=mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId);return{id:m.entityId,positions:m.joints.map(j=>({id:j.entityId,p:j.position}))};},out+'/ik-adjusted.i5cam');
 await page.screenshot({path:out+'/machine-ik-drag.png'});writeFileSync(out+'/machine-ik-acceptance.json',JSON.stringify({pass:true,saved,translation,rotation,native,p95:{solve:p95('solve'),total:p95('total'),roundTrip:p95('roundTrip')}},null,2));console.log(JSON.stringify({pass:true,mouseTranslateRotate:true,parentChain:true,unreachableHeld:true,collisionHeld:true,p95:{solve:p95('solve'),total:p95('total'),roundTrip:p95('roundTrip')}}));process.exit(0);
}catch(e){console.error(e);process.exit(1);}
