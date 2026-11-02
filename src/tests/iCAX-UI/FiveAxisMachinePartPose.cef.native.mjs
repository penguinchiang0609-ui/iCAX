import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {writeFileSync} from 'node:fs';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
try{
 const browser=await chromium.connectOverCDP('http://127.0.0.1:9482'),page=browser.contexts()[0].pages()[0],out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation';
 await page.setViewportSize({width:1600,height:1000});await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),out+'/ik-adjusted.i5cam');
 await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.fixture={view:mv};return mv?.scene?.machines?.length&&!mv.pending;});
 await page.locator('[data-tab-id="simulation"]').click();await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();await page.waitForFunction(()=>mv?.scene?.machines&&!mv.pending);
 const saved=await page.evaluate(async()=>{const d=mv.scene.machineDefinitions.find(d=>d.name.includes('IR-1600')),r=await mv.viewportSceneProxy.invoke('Machine.Instantiate',{machineDefinitionId:d.id});return{id:r.machine.entityId};});
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));await page.waitForFunction(()=>!mv.pending);
 await page.locator('[data-five-machine-instance]').selectOption(saved.id);await page.waitForFunction(()=>!mv.fiveMachine.busy);
 const seed=await page.evaluate(async()=>{const m=mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId),j=m.joints.find(j=>j.jointName==='J5');return mv.viewportSceneProxy.invoke('CamSimulation.MoveMachine',{expected:mv.scene.domain.expected,machineId:m.entityId,positions:{[j.entityId]:.3}});});assert.equal(seed.status,'Ready');
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));await page.waitForFunction(()=>!mv.pending);
 const ids=await page.evaluate(()=>{const m=mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId);return {j2:m.joints.find(j=>j.jointName==='J2').entityId,j6:m.joints.find(j=>j.jointName==='J6').entityId,base:m.elements.find(e=>e.kind!=='machine'&&e.parentEntityId===m.entityId).entityId};});
 const select=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand('five.machine.part:'+id),id);};
 const jointFields=()=>page.locator('[data-five-machine-field^="joint:"]').evaluateAll(es=>es.map(e=>e.dataset.fiveMachineField));
 await select(ids.j2);assert.deepEqual(await jointFields(),['joint:'+ids.j2]);assert.equal(await page.locator('[data-five-machine-field^="ik-pose:"]').count(),6);
 await page.locator('[data-five-machine-field="ik-pose:0"]').fill('999999');await select(ids.j6);
 assert.deepEqual(await jointFields(),['joint:'+ids.j6]);assert.notEqual(await page.locator('[data-five-machine-field="ik-pose:0"]').inputValue(),'999999','draft must not transfer between selected parts');
 await select(ids.base);assert.deepEqual(await jointFields(),[]);assert.equal(await page.locator('[data-five-machine-field^="ik-pose:"]:disabled').count(),6);
 await select(ids.j6);
 const read=()=>page.evaluate(()=>{const m=mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId),o=mv.viewport.sceneObjects.get(mv.fiveMachineSelectedPartId);o.updateWorldMatrix(true,false);return {positions:m.joints.map(j=>j.position),world:o.matrixWorld.elements.slice(),result:mv.fiveMachineIK.lastResult,message:mv.fiveMachineIK.message};});
 const before=await read();const x=page.locator('[data-five-machine-field="ik-pose:0"]');await x.fill(String(before.world[12]+.5));
 await page.locator('[data-five-command="five.machine-ik.apply"]').click();await page.waitForFunction(()=>!mv.fiveMachineIK.inflight);
 const translated=await read();assert.equal(translated.result?.status,'Ready',JSON.stringify(translated));assert.ok(Math.abs(translated.world[12]-before.world[12]-.5)<.01,JSON.stringify({before,translated}));assert.ok(translated.positions.filter((v,i)=>Math.abs(v-before.positions[i])>1e-7).length>=2);
 const rz=page.locator('[data-five-machine-field="ik-pose:5"]');await rz.fill(String(Number(await rz.inputValue())+.05));await page.locator('[data-five-command="five.machine-ik.apply"]').click();await page.waitForFunction(()=>!mv.fiveMachineIK.inflight);
 const rotated=await read();assert.equal(rotated.result?.status,'Ready',JSON.stringify(rotated));assert.notDeepEqual(rotated.positions,translated.positions);
 // Delay a real native response; edits and scrolling made while it is pending
 // must survive the subsequent keyed inspector patch.
 await page.setViewportSize({width:1600,height:650});await x.fill(String(rotated.world[12]+.25));
 await page.evaluate(()=>{window.poseCanvas=mv.viewport.renderer.domElement;window.poseObjects=new Map(mv.viewport.sceneObjects);const proxy=mv.viewportSceneProxy;window.poseInvoke=proxy.invoke;proxy.invoke=async function(name,...args){const r=await poseInvoke.call(this,name,...args);if(name==='CamSimulation.MoveMachineIK'){window.poseReplyWaiting=true;await new Promise(resolve=>window.releasePoseReply=resolve);}return r;};void __icaxAppShell.executeRibbonCommand('five.machine-ik.apply');});
 await page.waitForFunction(()=>window.poseReplyWaiting);
 await page.evaluate(()=>{const input=document.querySelector('[data-five-machine-field="ik-pose:0"]');input.value='123.456';input.dispatchEvent(new Event('input',{bubbles:true}));input.focus({preventScroll:true});input.setSelectionRange(1,4);window.poseInput=input;window.posePanes=[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane')];posePanes.forEach((p,i)=>p.scrollTop=80+i*100);window.poseScroll=posePanes.map(p=>p.scrollTop);releasePoseReply();});
 await page.waitForFunction(()=>!mv.fiveMachineIK.inflight);
 const retained=await page.evaluate(()=>{mv.viewportSceneProxy.invoke=poseInvoke;return {input:poseInput===document.querySelector('[data-five-machine-field="ik-pose:0"]'),focus:document.activeElement===poseInput,value:poseInput.value,range:[poseInput.selectionStart,poseInput.selectionEnd],scroll:posePanes.map(p=>p.scrollTop),expectedScroll:poseScroll,canvas:poseCanvas===mv.viewport.renderer.domElement,objects:[...poseObjects].every(([id,o])=>mv.viewport.sceneObjects.get(id)===o)};});
 assert.equal(retained.input,true);assert.equal(retained.focus,true);assert.equal(retained.value,'123.456');assert.deepEqual(retained.range,[1,4]);assert.deepEqual(retained.scroll,retained.expectedScroll);assert.ok(retained.scroll.every(n=>n>0),'both sidebars actually scrolled');assert.equal(retained.canvas,true);assert.equal(retained.objects,true);
 await x.fill('1000000');const beforeReject=await read();await page.locator('[data-five-command="five.machine-ik.apply"]').click();await page.waitForFunction(()=>!mv.fiveMachineIK.inflight);const rejected=await read();assert.equal(rejected.result.status,'Unreachable');assert.deepEqual(rejected.positions,beforeReject.positions);
 await page.setViewportSize({width:1600,height:1000});await select(ids.j2);await select(ids.j6);await page.locator('[data-command-id="five.machine-ik.translate"]').click();
 await page.evaluate(()=>{mv.viewport.setViewDirection([1,-1.5,.8]);mv.viewport.fitViewToViewport(1);});await page.waitForTimeout(150);
 await gizmoDriver(page).drag('X',8);await page.waitForFunction(()=>!mv.fiveMachineIK.inflight&&!mv.fiveMachineIK.pending);assert.equal((await read()).result.status,'Ready');
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.machine-ik.close'));await page.setViewportSize({width:1600,height:1000});
 await page.locator(`[data-five-command="five.machine.tree-toggle:${saved.id}"]`).click();
 const point=await page.evaluate(async id=>{
  const THREE=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js'),vp=mv.viewport,object=vp.sceneObjects.get(id),center=new THREE.Box3().setFromObject(object).getCenter(new THREE.Vector3()),ray=new THREE.Raycaster();
  for(const theta of [0,Math.PI/2,Math.PI,-Math.PI/2]){
   vp.setCameraState({target:{x:center.x,y:center.y,z:center.z},radius:650,theta,phi:1.2});vp.camera.updateMatrixWorld(true);vp.scene.updateMatrixWorld(true);
   const rect=vp.renderer.domElement.getBoundingClientRect();
   for(let y=-100;y<=100;y+=5)for(let x=-100;x<=100;x+=5){const px=rect.width/2+x,py=rect.height/2+y;ray.setFromCamera(new THREE.Vector2(px/rect.width*2-1,1-py/rect.height*2),vp.camera);const hit=ray.intersectObjects([...vp.sceneObjects.values()].filter(o=>o.visible),true)[0];let o=hit?.object;while(o&&!o.userData.objectId)o=o.parent;if(o?.userData.objectId===id)return{x:rect.left+px,y:rect.top+py};}
  }throw Error('Selected robot part is not visible for picking');
 },ids.j2);
 await page.mouse.click(point.x,point.y);await page.waitForFunction(id=>mv.fiveMachineSelectedPartId===id,ids.j2);assert.deepEqual(await jointFields(),['joint:'+ids.j2]);
 const linked=await page.evaluate(id=>{const row=document.querySelector(`[data-machine-node="${id}"] > .five-machine-tree-row`),pane=row.closest('.cam-context-pane'),r=row.getBoundingClientRect(),b=pane.getBoundingClientRect();return {highlight:[...mv.viewport.selectedObjectIds],treeVisible:r.top>=b.top&&r.bottom<=b.bottom,fields:[...document.querySelectorAll('[data-five-machine-field^="ik-pose:"]')].map(e=>e.value)};},ids.j2);assert.deepEqual(linked.highlight,[ids.j2]);assert.equal(linked.treeVisible,true);assert.equal(linked.fields.length,6);
 await select(ids.j6);await page.evaluate(()=>{mv.viewport.setViewDirection([1,-1.5,.8]);mv.viewport.fitViewToViewport(1);});
 await page.locator('.cam-info-pane').evaluate(e=>e.scrollTop=0);await page.screenshot({path:out+'/selected-part-pose.png'});
 writeFileSync(out+'/selected-part-pose-acceptance.json',JSON.stringify({pass:true,ids,selectedJointOnly:true,fixedBaseReadOnly:true,numericTranslation:translated,numericRotation:rotated,retained,unreachableHeld:true,mouseDrag:true,viewportPick:linked},null,2));console.log('PASS selection-scoped inspector, numeric position/rotation IK, fixed base, unreachable rejection, asynchronous focus/selection/scroll/node retention, real mouse drag and viewport picking');process.exit(0);
}catch(e){console.error(e);process.exit(1);}
