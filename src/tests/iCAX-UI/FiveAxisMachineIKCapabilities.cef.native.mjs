import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
const require=createRequire(import.meta.url),{chromium}=require('playwright');
const root=resolve(process.env.SOURCE_ORIGIN_OUTPUT||'output/work/machine-kinematics/machine-source-origin-20261024/cef');
const out=resolve(process.env.IK_CAPABILITY_OUTPUT||'output/work/machine-kinematics/machine-ik-capabilities-20261024');mkdirSync(out,{recursive:true});
const browser=await chromium.connectOverCDP(process.env.MACHINE_CDP||'http://127.0.0.1:9500'),page=browser.contexts()[0].pages()[0];
const settle=()=>page.waitForFunction(()=>{const s=mv.fiveMachineIK,p=mv.fiveMachineParameterEdits;return !mv.pending&&!mv.fiveMachine?.busy&&!__icaxAppShell.getState().pendingCount&&!s?.gesture&&!s?.inflight&&!s?.finishing&&!s?.fileSaving&&!s?.editTask&&!s?.spaceChanging&&![...(s?.drafts?.values()||[])].some(d=>d.timer||d.requested||d.saving)&&!p?.request&&!p?.fileSaving&&![...(p?.entries.values()||[])].some(e=>e.timer||e.ready);},null,{timeout:30000});
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await settle();};
async function bind(path){
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
 await page.waitForFunction(async path=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.fixture={view:mv};return __icaxAppShell.getState().activeProjectPath.replaceAll('\\','/')===path.replaceAll('\\','/')&&mv?.scene?.machines?.length&&!mv.pending;},path);
 await page.locator('[data-tab-id="simulation"]').click();await settle();await command('five.tool.machine-instances');
}
const positions=()=>page.evaluate(()=>Object.fromEntries(mv.scene.machines[0].joints.map(j=>[j.entityId,j.position])));
const capability=()=>page.evaluate(async()=>{const {canRotateMachinePart}=await import('../../../apps/five-axis-cam/webpage/machineIK.mjs');return canRotateMachinePart(mv);});
const rotateAvailability=()=>page.evaluate(async()=>{const {commandAvailability}=await import('../../../apps/five-axis-cam/webpage/commandAvailability.mjs');return commandAvailability(mv,'five.machine-ik.rotate');});
const select=async predicate=>{const id=await page.evaluate(predicate=>{const m=mv.scene.machines[0];return predicate==='head'?m.tools[0].entityId:m.joints.find(j=>j.jointName===predicate).entityId;},predicate);await command('five.machine.part:'+id);return id;};
const tools=()=>page.locator('.five-placement-tools:not([hidden])');
const motion=()=>page.locator('.five-motion-switch:not([hidden])');
async function switchMotion(mode){await motion().locator(`[data-transform-switch="${mode}"]`).click();await settle();assert.equal(await page.evaluate(()=>mv.fiveMachineIK.motionMode),mode);assert.equal(await motion().locator(`[data-transform-switch="${mode}"]`).getAttribute('aria-pressed'),'true');}
async function setSpace(space){await page.locator(`[data-transform-space="${space}"]`).click();await settle();assert.equal(await page.evaluate(()=>mv.fivePlacementSpace||'world'),space);}
async function displayFrame(){return page.evaluate(async()=>{
 const THREE=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js'),m=mv.scene.machines[0],id=mv.fiveMachineSelectedPartId,node=m.elements.find(e=>e.entityId===id),object=mv.viewport.sceneObjects.get(id),parent=mv.viewport.sceneObjects.get(node.parentEntityId);object.updateWorldMatrix(true,false);parent.updateWorldMatrix(true,false);
 const expected=object.matrixWorld.clone();if(mv.fivePlacementSpace==='local')expected.premultiply(parent.matrixWorld.clone().invert());
 const values=[...document.querySelectorAll('.cam-info-pane [data-five-machine-field^="ik-pose:"]')].map(e=>Number(e.value)),position=new THREE.Vector3(),rotation=new THREE.Quaternion();expected.decompose(position,rotation,new THREE.Vector3());
 const positionError=Math.max(...values.slice(0,3).map((v,i)=>Math.abs(v-position.toArray()[i]))),angleError=values.length===6?new THREE.Quaternion().setFromEuler(new THREE.Euler(...values.slice(3).map(v=>v*Math.PI/180),'ZYX')).angleTo(rotation):0;
 return {space:mv.fivePlacementSpace||'world',values,positionError,angleError,gizmoSpace:mv.viewport.transformManipulator.control.space,caption:document.querySelector('.five-placement-hud .five-transform-caption')?.textContent};
});}
function verifyDisplay(frame){assert.ok(frame.positionError<.002,JSON.stringify(frame));assert.ok(frame.angleError<.0001,JSON.stringify(frame));assert.equal(frame.gizmoSpace,frame.space);assert.ok(!(frame.caption||'').includes('世界坐标'));}
try{
 await page.waitForFunction(()=>__icaxAppShell.getState().startupPhase==='ready');
 const original=await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs'),view=getProjectView(__icaxAppShell.getState().activeProjectId);if(view.fiveMachineIK?.dragging||view.fiveMachineIK?.inflight)throw Error('active user gesture');const path=__icaxAppShell.getState().activeProjectPath;await view.viewportSceneProxy.invoke('Project.Save',{projectPath:path});return {path,camera:view.viewport.getCameraState()};});
 // Reload saved application code, then continue with the same native project.
 await page.reload();await page.waitForFunction(()=>__icaxAppShell?.getState().startupPhase==='ready');
 const cdp=await page.context().newCDPSession(page);await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:true});
 await bind(resolve(root,'TC-800-centred.i5cam'));const head=await select('head');await command('five.machine-ik.translate');await setSpace('world');
 await page.evaluate(camera=>mv.viewport.setCameraState(camera),original.camera);
 assert.equal(await capability(),false);
 assert.equal(await motion().locator('[data-transform-switch="FK"]').isDisabled(),true);assert.equal(await motion().locator('[data-transform-switch="IK"]').isDisabled(),false);assert.equal(await tools().locator('[data-transform-switch]').count(),0);
 const panel=page.locator('.cam-info-pane'),hud=page.locator('.five-placement-hud[data-transform-owner="machine-ik"]');
 assert.equal(await panel.locator('[data-five-command="five.machine-ik.rotate"]').count(),0);assert.equal(await panel.locator('[data-five-machine-field^="ik-pose:"]').count(),3);
 assert.equal(await hud.count(),0,'IK values appear only after an axis is selected');assert.equal(await tools().locator('[data-transform-mode="rotate"]').count(),0);assert.equal((await rotateAvailability()).disabled,true);
 assert.equal(await page.evaluate(()=>mv.viewport.transformManipulator.control.mode),'translate');
 const before=await positions();await command('five.machine-ik.rotate');assert.equal(await page.evaluate(()=>mv.fiveMachineIK.mode),'translate');assert.deepEqual(await positions(),before);
 await gizmoDriver(page).drag('X',4);await settle();assert.equal(await page.evaluate(()=>mv.fiveMachineIK.lastResult?.status),'Ready');assert.notDeepEqual(await positions(),before);
 const after=await positions(),siblings=await page.evaluate(()=>mv.scene.machines[0].joints.filter(j=>['A','C'].includes(j.jointName)).map(j=>j.entityId));for(const id of siblings)assert.equal(after[id],before[id]);
 await command('edit.undo');assert.deepEqual(await positions(),before);
 // Use the actual floating buttons to switch both ways without moving the machine.
 await select('Y');const switchBefore=await positions();
 for(let i=0;i<3;i++){await switchMotion('FK');assert.deepEqual(await positions(),switchBefore);await switchMotion('IK');assert.deepEqual(await positions(),switchBefore);}
 await switchMotion('FK');await gizmoDriver(page).drag('Z',4);await settle();assert.equal(await page.evaluate(()=>mv.fiveMachineIK.lastResult?.status),'Ready');assert.notDeepEqual(await positions(),switchBefore);await command('edit.undo');assert.deepEqual(await positions(),switchBefore);
 await switchMotion('IK');await gizmoDriver(page).drag('X',4);await settle();assert.equal(await page.evaluate(()=>mv.fiveMachineIK.lastResult?.status),'Ready');assert.notDeepEqual(await positions(),switchBefore);await command('edit.undo');assert.deepEqual(await positions(),switchBefore);
 const coordinateFrames=[];coordinateFrames.push(await displayFrame());await setSpace('local');coordinateFrames.push(await displayFrame());coordinateFrames.forEach(verifyDisplay);assert.deepEqual(await positions(),switchBefore);
 const localInput=panel.locator('[data-five-machine-field="ik-pose:1"]'),goal=Number(await localInput.inputValue())+.5;
 const expectedTarget=await page.evaluate(goal=>{const m=mv.scene.machines[0],id=mv.fiveMachineSelectedPartId,node=m.elements.find(e=>e.entityId===id),object=mv.viewport.sceneObjects.get(id),parent=mv.viewport.sceneObjects.get(node.parentEntityId);object.updateWorldMatrix(true,false);parent.updateWorldMatrix(true,false);const local=parent.matrixWorld.clone().invert().multiply(object.matrixWorld);local.elements[13]=goal;return parent.matrixWorld.clone().multiply(local).toArray();},goal);
 await localInput.fill(String(goal));await localInput.blur();await settle();assert.equal(await page.evaluate(()=>mv.fiveMachineIK.lastResult?.status),'Ready');
 const actualTarget=await page.evaluate(()=>{const o=mv.viewport.sceneObjects.get(mv.fiveMachineSelectedPartId);o.updateWorldMatrix(true,false);return o.matrixWorld.toArray();});actualTarget.forEach((v,i)=>assert.ok(Math.abs(v-expectedTarget[i])<.002,`${i}: ${v} != ${expectedTarget[i]}`));await command('edit.undo');assert.deepEqual(await positions(),switchBefore);await setSpace('world');
 await select('head');await command('five.machine-ik.translate');
 // An asynchronous scene refresh retains the position editor and current pane state.
 await page.evaluate(()=>{const input=document.querySelector('.cam-info-pane [data-five-machine-field="ik-pose:0"]');input.focus({preventScroll:true});input.setSelectionRange(0,input.value.length);window.ikNodes={input,canvas:mv.viewport.renderer.domElement,left:document.querySelector('.cam-context-pane'),right:document.querySelector('.cam-info-pane')};window.ikScroll=[...document.querySelectorAll('.cam-context-pane,.cam-info-pane,.cam-context-pane *,.cam-info-pane *')].filter(n=>n.clientHeight>0&&n.scrollHeight>n.clientHeight+5&&['auto','scroll'].includes(getComputedStyle(n).overflowY)).map((node,i)=>{node.scrollTop=20+i*10;return {node,top:node.scrollTop};});});
 await command('five.refresh');const stable=await page.evaluate(()=>({input:document.querySelector('.cam-info-pane [data-five-machine-field="ik-pose:0"]')===ikNodes.input,canvas:mv.viewport.renderer.domElement===ikNodes.canvas,panes:document.querySelector('.cam-context-pane')===ikNodes.left&&document.querySelector('.cam-info-pane')===ikNodes.right,focus:document.activeElement===ikNodes.input,selection:[ikNodes.input.selectionStart,ikNodes.input.selectionEnd],scroll:ikScroll.every(({node,top})=>node.scrollTop===top)}));assert.ok(stable.input&&stable.canvas&&stable.panes&&stable.focus&&stable.scroll,JSON.stringify(stable));assert.equal(stable.selection[0],0);
 await select('C');assert.equal(await capability(),true);assert.equal((await rotateAvailability()).disabled,false);await switchMotion('FK');assert.equal(await page.evaluate(()=>mv.viewport.transformManipulator.control.mode),'rotate');await switchMotion('IK');const rotate=tools().locator('[data-transform-mode="rotate"]');assert.equal(await rotate.count(),1);assert.equal(await rotate.isDisabled(),false);await rotate.click();await settle();assert.equal(await page.evaluate(()=>mv.viewport.transformManipulator.control.mode),'rotate');
 await select('head');assert.equal(await page.evaluate(()=>mv.fiveMachineIK.mode),'translate');assert.equal(await page.evaluate(()=>mv.viewport.transformManipulator.control.mode),'translate');
 const preserved=[];
 for(const name of ['RC-1000','LC-3015','IR-1600']){
  await bind(resolve(root,name+'-centred.i5cam'));
  if(name==='RC-1000'){const support=await page.evaluate(()=>mv.scene.machines[0].elements.find(e=>e.workSupport).entityId);await command('five.machine.part:'+support);}else await select('head');
  assert.equal(await capability(),true,name);await command('five.machine-ik.rotate');assert.equal(await page.evaluate(()=>mv.viewport.transformManipulator.control.mode),'rotate',name);preserved.push(name);
  if(name==='IR-1600'){await setSpace('world');coordinateFrames.push(await displayFrame());await setSpace('local');coordinateFrames.push(await displayFrame());coordinateFrames.slice(-2).forEach(verifyDisplay);await setSpace('world');}
 }
 await bind(resolve(root,'TC-800-centred.i5cam'));await select('Y');await command('five.machine-ik.translate');await page.evaluate(camera=>mv.viewport.setCameraState(camera),original.camera);
 await setSpace('world');await gizmoDriver(page).pick('X');await settle();assert.equal(await hud.locator('input').count(),1);assert.equal(await hud.locator('[data-five-machine-field="ik-delta"]').count(),1);assert.equal(await hud.locator('.five-transform-caption').count(),0);await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
 const layout=await page.evaluate(()=>{const vp=mv.viewport,canvas=vp.renderer.domElement.getBoundingClientRect(),p=vp.transformManipulator.proxy.position.clone().project(vp.camera),x=canvas.left+(p.x+1)*canvas.width/2,y=canvas.top+(1-p.y)*canvas.height/2,rect=s=>{const r=document.querySelector(s).getBoundingClientRect();return {left:r.left,right:r.right,top:r.top,bottom:r.bottom};};return {x,y,tools:rect('.five-placement-tools'),motion:rect('.five-motion-switch'),hud:rect('.five-placement-hud'),lowerModeCount:document.querySelectorAll('.five-placement-tools [data-transform-switch]').length};});
 assert.equal(layout.lowerModeCount,0);assert.ok(Math.abs(layout.motion.left-layout.tools.right-5)<1,JSON.stringify(layout));assert.ok(Math.abs(layout.motion.bottom-layout.tools.bottom)<1,JSON.stringify(layout));assert.ok(layout.tools.top>layout.y,JSON.stringify(layout));assert.ok(layout.hud.right<=layout.motion.left||layout.hud.left>=layout.motion.right||layout.hud.bottom<=layout.motion.top||layout.hud.top>=layout.motion.bottom,JSON.stringify(layout));
 await page.screenshot({path:resolve(out,'tc-control-layout.png')});
 const report={pass:true,tcHeadTranslationOnly:true,unavailableCommandDoesNotMutate:true,realIKTranslation:'Ready',siblingRotaryUnchanged:true,undoRestores:true,floatingSwitch:{mouseCycles:3,doesNotMoveMachine:true,fkDrag:'Ready',ikDrag:'Ready',undoBoth:true},coordinateFrames,localNumericWorldTarget:true,layout,stable,rotaryChainsPreserved:preserved};writeFileSync(resolve(out,'acceptance.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
}catch(error){await page.screenshot({path:resolve(out,'failure.png')}).catch(()=>{});console.error(error);process.exitCode=1;}finally{await browser.close();}
