import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {writeFileSync} from 'node:fs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
try{
 const browser=await chromium.connectOverCDP('http://127.0.0.1:9482'),page=browser.contexts()[0].pages()[0];
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 const path='D:/penguinchinage0609/iCAX/output/tests/machine-simulation/empty-instance.i5cam';
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
 await page.waitForFunction(async path=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.mi=(m,p)=>mv.viewportSceneProxy.invoke(m,p,{timeoutMs:10000});return __icaxAppShell.getState().activeProjectPath===path&&mv?.scene?.machines?.length&&!mv.pending;},path);
 await page.locator('[data-tab-id="simulation"]').click();await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();
 await page.locator('[data-five-machine-field="jogStep"]').selectOption('1');
 const x=await page.evaluate(()=>mv.scene.machines[0].joints.find(j=>j.jointName==='X'));
 const pWait=()=>page.waitForTimeout(200);
 const button=page.locator(`[data-five-command="five.machine.jog-plus:${x.entityId}"]`);
 await button.scrollIntoViewIfNeeded();
 const initial=await page.evaluate(()=>{
  window.calls=[];const proxy=mv.viewportSceneProxy,invoke=proxy.invoke;proxy.invoke=async function(...args){const start=performance.now();try{return await invoke.apply(this,args);}finally{calls.push({method:args[0],ms:performance.now()-start});}};
  window.jogObjects=new Map(mv.viewport.sceneObjects);window.jogGeometry=new Map(mv.viewport.geometryObjects);
  return mv.scene.machines[0].joints.find(j=>j.jointName==='X').position;
 });
 await button.click();await page.waitForFunction(initial=>!mv.fiveMachine.busy&&mv.scene.machines[0].joints.find(j=>j.jointName==='X').position===initial+1,initial);
 assert.equal(await page.evaluate(()=>calls.filter(c=>c.method==='CamSimulation.MoveMachine').length),1,'short click is exactly one step');
 await page.evaluate(()=>{
  window.poseFrames=[];window.renderFrames=[];window.monitorJog=true;let last=null;
  const x=mv.scene.machines[0].joints.find(j=>j.jointName==='X').entityId;
  const sample=t=>{if(!monitorJog)return;renderFrames.push(t);const o=mv.viewport.sceneObjects.get(x),v=o.matrix.elements[12];if(v!==last){poseFrames.push({t,v});last=v;}requestAnimationFrame(sample);};requestAnimationFrame(sample);
 });
 await button.scrollIntoViewIfNeeded();await pWait();
 const box=await button.boundingBox();await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();
 await page.waitForFunction(()=>mv.fiveMachineJog?.result?.status==='Running',{},{timeout:5000});
 await page.waitForTimeout(1800);await page.mouse.up();await page.waitForFunction(()=>!mv.fiveMachineJog?.running);
 const stopped=await page.evaluate(()=>{monitorJog=false;return {result:mv.fiveMachineJog.result,error:mv.fiveMachine.error,poseFrames,renderFrames,calls,sameObjects:[...jogObjects].every(([id,o])=>mv.viewport.sceneObjects.get(id)===o),sameGeometry:[...jogGeometry].every(([id,o])=>mv.viewport.geometryObjects.get(id)===o)};});
 assert.equal(stopped.error,'');assert.equal(stopped.result.status,'Stopped');assert.ok(Number(stopped.result.ticks)>50);assert.equal(stopped.sameObjects,true);assert.equal(stopped.sameGeometry,true);
 const position=stopped.result.positions[x.entityId];await page.waitForTimeout(400);
 assert.equal((await page.evaluate(id=>mi('Machine.GetElement',{entityId:id}),x.entityId)).machineElement.joint.position,position,'release holds position');
 assert.equal(stopped.calls.filter(c=>c.method==='CamSimulation.MoveMachine').length,1,'hold does not repeat click requests');
 assert.equal(stopped.calls.filter(c=>/ReadState|Machine.List|BuildMachine/.test(c.method)).length,0);
 // Leaving the button and window blur both stop without waiting for pointerup.
 const exits=[];
 for(const mode of ['leave','blur','cancel','capture']){
  await button.scrollIntoViewIfNeeded();const b=await button.boundingBox();await page.mouse.move(b.x+b.width/2,b.y+b.height/2);await page.mouse.down();
  await page.waitForFunction(()=>mv.fiveMachineJog?.held&&mv.fiveMachineJog?.result?.status==='Running');await page.waitForTimeout(150);
  if(mode==='leave')await page.mouse.move(b.x-20,b.y-20);
  else await page.evaluate(mode=>{if(mode==='blur')window.dispatchEvent(new Event('blur'));else mv.fiveMachineJog.button.dispatchEvent(new PointerEvent(mode==='cancel'?'pointercancel':'lostpointercapture',{bubbles:true,pointerId:mv.fiveMachineJog.pointerId}));},mode);
  await page.waitForFunction(()=>!mv.fiveMachineJog?.running);await page.mouse.up();exits.push({mode,status:await page.evaluate(()=>mv.fiveMachineJog.result.status)});
 }
 // Native deadman and stop-before-start race, independent of frontend handlers.
 const native=await page.evaluate(async()=>{
  const m=mv.scene.machines[0],j=m.joints.find(j=>j.jointName==='X');
  const request=token=>({token,expected:mv.scene.domain.expected,machineId:m.entityId,jointId:j.entityId,direction:1,speed:10});
  let token=crypto.randomUUID();await mi('CamSimulation.StopMachineJog',{token});const cancelled=await mi('CamSimulation.StartMachineJog',request(token));
  token=crypto.randomUUID();await mi('CamSimulation.StartMachineJog',request(token));await new Promise(r=>setTimeout(r,500));const expired=await mi('CamSimulation.KeepMachineJog',{token});const closed=await mi('CamSimulation.StopMachineJog',{token});
  // Restore the test joint with the ordinary safe atomic move.
  await mi('CamSimulation.MoveMachine',{expected:mv.scene.domain.expected,machineId:m.entityId,positions:{[j.entityId]:0}});
  const run=async(joint,direction,speed)=>{
   const token=crypto.randomUUID();let r=await mi('CamSimulation.StartMachineJog',{...request(token),jointId:joint.entityId,direction,speed});
   for(let i=0;i<150&&r.status==='Running';i++){await new Promise(r=>setTimeout(r,40));r=await mi('CamSimulation.KeepMachineJog',{token});}
   return mi('CamSimulation.StopMachineJog',{token});
  };
  let limit;
  try{await mi('Machine.SetJointLimits',{entityId:j.entityId,lower:j.lower,upper:2});limit=await run(j,1,20);}
  finally{await mi('Machine.SetJointLimits',{entityId:j.entityId,lower:j.lower,upper:j.upper});await mi('CamSimulation.MoveMachine',{expected:mv.scene.domain.expected,machineId:m.entityId,positions:{[j.entityId]:0}});}
  const z=m.joints.find(j=>j.jointName==='Z');const collision=await run(z,-1,Math.min(z.velocity,100));
  const collisionPosition=(await mi('Machine.GetElement',{entityId:z.entityId})).machineElement.joint.position;
  const safe=await mi('CamSimulation.CheckMachineMove',{expected:mv.scene.domain.expected,machineId:m.entityId,positions:{[z.entityId]:collisionPosition}});
  await mi('CamSimulation.MoveMachine',{expected:mv.scene.domain.expected,machineId:m.entityId,positions:{[z.entityId]:0}});
  const table=m.elements.find(e=>e.name==='工作台');const boxes=(await mi('Machine.GetElement',{entityId:table.entityId})).machineElement.collisions;
  return {cancelled,expired,closed,limit,collision,collisionPosition,safe:safe.collisionStatus,reopenedBox:boxes[0]};
 });
 assert.equal(native.cancelled.status,'Stopped');assert.equal(native.expired.status,'Expired');
 assert.equal(native.limit.status,'Limit');assert.equal(native.limit.positions[x.entityId],2);assert.equal(native.collision.status,'Blocked');assert.equal(native.safe,'Clear');
 assert.equal(native.reopenedBox.geometry.size[2],.181);assert.equal(native.reopenedBox.pose[0],.002);
 const percentile=(a,p)=>[...a].sort((a,b)=>a-b)[Math.floor((a.length-1)*p)];
 const delta=a=>a.slice(1).map((v,i)=>v-a[i]);
 const timing={ticks:stopped.result.ticks,collisionP95Ms:percentile(stopped.result.timings.map(t=>t.collisionMs),.95),tickP95Ms:percentile(stopped.result.timings.map(t=>t.tickMs),.95),nativeIntervalP95Ms:percentile(stopped.result.timings.map(t=>t.intervalMs),.95),rafP95Ms:percentile(delta(stopped.renderFrames),.95),poseIntervalP95Ms:percentile(delta(stopped.poseFrames.slice(3).map(f=>f.t)),.95)};
 writeFileSync('output/tests/machine-simulation/hold-jog-acceptance.json',JSON.stringify({timing,stopped,exits,native},null,2));console.log(JSON.stringify({pass:true,timing,exits}));process.exit(0);
}catch(e){console.error(e);process.exit(1);}
