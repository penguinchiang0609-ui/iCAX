import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {copyFileSync,mkdirSync,writeFileSync,readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out=resolve(process.env.NODE_EDIT_OUTPUT||'output/tests/machine-node-edit-20261024');mkdirSync(out,{recursive:true});
const browser=await chromium.connectOverCDP(process.env.MACHINE_CDP||'http://127.0.0.1:9498'),page=browser.contexts()[0].pages()[0],evidence=[];
const settle=()=>page.waitForFunction(()=>{const s=mv.fiveMachineIK,p=mv.fiveMachineParameterEdits,c=mv.fiveMachineColliders;return !mv.pending&&!mv.fiveMachine?.busy&&!mv.fiveHistoryBusy&&!__icaxAppShell.getState().pendingCount&&!s?.gesture&&!s?.inflight&&!s?.finishing&&!s?.editTask&&!s?.fileSaving&&!p?.request&&!p?.fileSaving&&![...(p?.entries.values()||[])].some(x=>x.timer||x.ready)&&!c?.saving&&![...(c?.drafts.values()||[])].some(x=>x.timer||x.requested);},null,{timeout:30000});
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await settle();};
const select=id=>command('five.machine.part:'+id);
const read=()=>page.evaluate(()=>{const m=mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId);return {joints:Object.fromEntries(m.joints.map(j=>[j.entityId,j.position])),world:Object.fromEntries(m.elements.map(e=>{const o=mv.viewport.sceneObjects.get(e.entityId);o?.updateWorldMatrix(true,false);return [e.entityId,o?.matrixWorld.toArray()];})),collisions:m.collisions.map(c=>({entityId:c.entityId,name:c.name||c.collisionName,enabled:c.enabled!==false,pose:c.pose.map(v=>v===0?0:v),geometry:c.geometry})).sort((a,b)=>(a.entityId+a.name).localeCompare(b.entityId+b.name))};});
async function bind(path){
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
 await page.waitForFunction(async path=>{if(__icaxAppShell.getState().activeProjectPath.replaceAll('\\','/')!==path.replaceAll('\\','/'))return false;const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.fixture={view:mv};return mv?.scene?.machines?.length&&!mv.pending&&!__icaxAppShell.getState().pendingCount;},path);
 await page.locator('[data-tab-id="simulation"]').click();await settle();await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();await settle();
 await page.waitForFunction(()=>mv.viewport?.sceneObjects?.size>0);await page.evaluate(()=>{mv.viewport.setViewDirection([1,-1.5,.8]);mv.viewport.fitViewToViewport(1);});
}
const changed=(a,b)=>Object.keys(a).filter(id=>Math.abs(a[id]-b[id])>1e-6);
async function gesture(axis,pixels){await gizmoDriver(page).drag(axis,pixels);await settle();return page.evaluate(()=>({status:mv.fiveMachineIK.lastResult?.status,message:mv.fiveMachineIK.message,stream:mv.fiveMachineIK.lastStreamStats}));}
async function edgePoint(index){return page.evaluate(async index=>{
 const THREE=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js'),s=mv.fiveMachineColliders,vp=mv.viewport,r=vp.renderer.domElement.getBoundingClientRect(),o=s.overlay.children[index];o.updateWorldMatrix(true,false);
 const p=o.geometry.attributes.position;
 for(let i=0;i<p.count;i+=2)for(const t of [.2,.4,.6,.8]){
  const a=new THREE.Vector3().fromBufferAttribute(p,i),b=new THREE.Vector3().fromBufferAttribute(p,i+1),q=a.lerp(b,t).applyMatrix4(o.matrixWorld).project(vp.camera),x=r.left+(q.x+1)*r.width/2,y=r.top+(1-q.y)*r.height/2;
  if(Math.abs(q.z)>1||document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;
  const m=vp.transformManipulator;if(m){m.update();const ray=m.control.getRaycaster();ray.setFromCamera({x:2*(x-r.left)/r.width-1,y:1-2*(y-r.top)/r.height},vp.camera);if(ray.intersectObject(m.control._gizmo.picker[m.control.mode],true).some(h=>h.object.visible))continue;}
  return {x,y};
 }throw Error('No visible wire away from the joint handle');
},index);}
async function colliders(part){
 await select(part);await command('five.machine-ik.close');
 const boxes=await page.evaluate(()=>structuredClone(mv.fiveMachineColliders.drafts.get(mv.fiveMachineSelectedPartId).boxes));assert.ok(boxes.length>=2);
 for(const projectionMode of ['perspective','orthographic']){
  await select(part);await command('five.machine-ik.close');await page.evaluate(projectionMode=>{mv.viewport.setCameraState({...mv.viewport.getCameraState(),projectionMode});},projectionMode);
  const point=await edgePoint(1);await page.mouse.click(point.x,point.y);assert.ok(!await page.evaluate(()=>mv.fiveMachineColliders.mode),'wire click cannot start collider editing');
  await select(part);await command('five.machine-ik.close');const double=await edgePoint(1);await page.mouse.dblclick(double.x,double.y);assert.ok(!await page.evaluate(()=>mv.fiveMachineColliders.mode),'wire double-click cannot start collider editing');
 }
 await select(part);const card=page.locator(`[data-box-id="${boxes[1].id}"]`);await card.scrollIntoViewIfNeeded();await card.click({position:{x:5,y:5}});assert.ok(!await page.evaluate(()=>mv.fiveMachineColliders.mode),'card background cannot start editing');
 assert.equal(await card.locator('[data-five-machine-field$=":size:0"]').isDisabled(),true);
 const before=await read();await card.locator('[data-five-command$=":bounds"]').click();await settle();
 assert.deepEqual(await page.evaluate(()=>({mode:mv.fiveMachineColliders.mode,active:mv.fiveMachineColliders.active,pose:mv.fiveMachineIK.mode,selected:[...mv.viewport.selectedObjectIds]})),{mode:'bounds',active:boxes[1].id,pose:null,selected:[]});
 assert.equal(await page.locator(`[data-box-id="${boxes[0].id}"] [data-five-machine-field$=":size:0"]`).isDisabled(),true);
 const point=await edgePoint(0);await page.mouse.click(point.x,point.y);assert.equal(await page.evaluate(()=>mv.fiveMachineColliders.active),boxes[1].id,'wire cannot switch the active collider');
 await page.locator('[data-five-command="five.machine.box-edit:'+boxes[1].id+':translate"]').click();await settle();
 await page.evaluate(()=>{mv.viewport.setViewDirection([1,-1.5,.8]);mv.viewport.fitViewToViewport(1);});
 await gizmoDriver(page).drag('X',3);await settle();const moved=await read();assert.deepEqual(moved.joints,before.joints);assert.deepEqual(moved.world,before.world);assert.notDeepEqual(moved.collisions,before.collisions);
 assert.deepEqual(moved.collisions.filter(c=>c.entityId!==part||c.name!==boxes[1].name),before.collisions.filter(c=>c.entityId!==part||c.name!==boxes[1].name));
 await command('edit.undo');assert.deepEqual((await read()).collisions,before.collisions);await command('edit.redo');assert.deepEqual((await read()).collisions,moved.collisions);
 // Delay an actual native save; changes made while waiting must retain DOM,
 // input selection, both sidebars and existing nested scrollers.
 await page.setViewportSize({width:1600,height:650});const input=page.locator(`[data-five-machine-field="box:${boxes[1].id}:position:0"]`);await input.scrollIntoViewIfNeeded();
 await page.evaluate(()=>{const proxy=mv.viewportSceneProxy;window.originalInvoke=proxy.invoke;window.holdCollider=true;proxy.invoke=async function(method,...args){const r=await originalInvoke.call(this,method,...args);if(method==='Machine.SetBoxColliders'&&holdCollider)await new Promise(resolve=>window.releaseCollider=resolve);return r;};window.retainedCanvas=mv.viewport.renderer.domElement;});
 const x=Number(await input.inputValue());await input.fill(String(x+.1));await page.waitForFunction(()=>!!window.releaseCollider);
 await input.fill(String(x+.2));await input.evaluate(e=>{window.retainedInput=e;e.setSelectionRange(1,4);window.scrollNodes=[...document.querySelectorAll('.cam-context-pane,.cam-info-pane,.cam-context-pane *,.cam-info-pane *')].filter(n=>n.clientHeight>0&&n.scrollHeight>n.clientHeight+2);for(const [i,n] of scrollNodes.entries())n.scrollTop=Math.min(n.scrollHeight-n.clientHeight,25+i*13);window.retainedScroll=scrollNodes.map(n=>n.scrollTop);holdCollider=false;releaseCollider();});
 await settle();const retention=await input.evaluate(e=>({node:e===retainedInput,focus:document.activeElement===e,range:[e.selectionStart,e.selectionEnd],value:e.value,canvas:mv.viewport.renderer.domElement===retainedCanvas,scroll:scrollNodes.map(n=>n.scrollTop),expected:retainedScroll,left:scrollNodes.some(n=>n.matches('.cam-context-pane')||n.closest('.cam-context-pane')),right:scrollNodes.some(n=>n.matches('.cam-info-pane')||n.closest('.cam-info-pane'))}));
 assert.equal(retention.node,true);assert.equal(retention.focus,true);assert.equal(retention.canvas,true);assert.deepEqual(retention.range,[1,4]);assert.equal(retention.value,String(x+.2));assert.deepEqual(retention.scroll,retention.expected);assert.ok(retention.left&&retention.right);
 await page.evaluate(()=>mv.viewportSceneProxy.invoke=originalInvoke);await input.blur();await settle();
 await page.setViewportSize({width:1600,height:1000});await page.locator('[data-five-command="five.machine.box-edit-end"]').click();await settle();assert.equal(await page.evaluate(()=>mv.fiveMachineColliders.mode),null);
 assert.ok(await page.evaluate(()=>!!mv.fiveMachineIK.mode),'finish returns to node motion');
 await page.screenshot({path:resolve(out,'collider-edit-finished.png')});evidence.push({kind:'collider',explicitPerBoxEntry:true,wireDoesNotEnterOrSwitch:true,onlySelectedBoxChanged:true,modelUnchanged:true,undoRedo:true,retention});
}
try{
 await page.setViewportSize({width:1600,height:1000});await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');const cdp=await page.context().newCDPSession(page);await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:true});
 if(process.argv.includes('--reopen')){
  const saved=JSON.parse(readFileSync(resolve(out,'node-edit-expected.json'),'utf8'));
  for(const x of saved){await bind(x.path);const actual=await read();assert.deepEqual(actual.joints,x.state.joints);assert.deepEqual(actual.collisions,x.state.collisions);}
  writeFileSync(resolve(out,'node-edit-reopened.json'),JSON.stringify({pass:true,machines:saved.length},null,2));console.log('PASS fresh-process reopen preserves joint poses and collider edits');
 }else{
  const saved=[];
  for(const model of ['ir1600','lc3015']){
   const path=resolve(out,model+'-node-edit.i5cam');copyFileSync(resolve('output/tests/machine-surface-20261024',model+'-surface.i5cam'),path);await bind(path);
   const ids=await page.evaluate(()=>Object.fromEntries(mv.scene.machines[0].joints.map(j=>[j.jointName,j.entityId])));
   const name=model==='ir1600'?'J2':'X',joint=ids[name];assert.ok(joint,JSON.stringify(ids));await select(joint);
   assert.equal(await page.evaluate(()=>mv.fiveMachineIK.motionMode),'FK');assert.deepEqual(await page.evaluate(()=>mv.viewport.transformManipulator.options.axes),['Z']);
   const before=await read(),r=await gesture('Z',model==='ir1600'?-6:3),after=await read();assert.equal(r.status,'Ready',JSON.stringify(r));assert.deepEqual(changed(before.joints,after.joints),[joint]);
   assert.ok(Object.keys(before.world).filter(id=>id!==joint&&JSON.stringify(before.world[id])!==JSON.stringify(after.world[id])).length>0,'descendants follow');assert.deepEqual(before.collisions,after.collisions);
   await command('edit.undo');assert.deepEqual((await read()).joints,before.joints);await command('edit.redo');assert.deepEqual((await read()).joints,after.joints);
   // Cancelling a real gesture restores the committed pose without a record.
   const cancelBase=await read();await gizmoDriver(page).drag('Z',-5,false);await page.waitForFunction(()=>mv.fiveMachineIK.gesture?.stream?.processedSequence>0n);await page.keyboard.press('Escape');await page.mouse.up();await settle();assert.deepEqual((await read()).joints,cancelBase.joints);
   if(model==='ir1600'){
    await select(ids.J3);await page.locator('.cam-info-pane [data-five-command="five.machine-ik.ik"]').click();await command('five.machine-ik.rotate');
    const midBefore=await read(),midResult=await gesture('Y',-6),midAfter=await read();assert.equal(midResult.status,'Ready',JSON.stringify(midResult));assert.ok(changed(midBefore.joints,midAfter.joints).includes(ids.J3));
    for(const n of ['J4','J5','J6'])assert.equal(midAfter.joints[ids[n]],midBefore.joints[ids[n]],'IK preserves downstream coordinates');assert.notDeepEqual(midBefore.world[ids.J6],midAfter.world[ids.J6]);
    await page.screenshot({path:resolve(out,'intermediate-node-ik.png')});
    await select(ids.J1);assert.equal(await page.evaluate(()=>mv.fiveMachineIK.tip),ids.J1);await command('five.machine-ik.rotate');const rootChild=await gesture('Z',4);assert.equal(rootChild.status,'Ready',JSON.stringify(rootChild));
    await select(ids.pedestal_mount);assert.equal(await page.locator('.cam-info-pane [data-five-command="five.machine-ik.ik"]').isDisabled(),true);assert.equal(await page.evaluate(()=>mv.viewport.transformManipulator),null);
    await colliders(ids.J2);evidence.push({model,fkJoint:name,intermediateIK:'J3',firstJointIK:'J1',downstreamCoordinatesPreserved:true,gesture:r});
   }else evidence.push({model,fkJoint:name,gesture:r});
   await page.evaluate(path=>mv.viewportSceneProxy.invoke('Project.Save',{projectPath:path}),path);await settle();saved.push({path,state:await read()});
   await page.screenshot({path:resolve(out,model+'-node-edit.png')});
  }
  writeFileSync(resolve(out,'node-edit-expected.json'),JSON.stringify(saved,null,2));writeFileSync(resolve(out,'node-edit-acceptance.json'),JSON.stringify({pass:true,evidence},null,2));console.log('PASS native CEF: FK, intermediate IK, descendants, cancellation, undo/redo, explicit collider editing, stable asynchronous sidebar interaction');
 }
}catch(error){await page.screenshot({path:resolve(out,'node-edit-failure.png')}).catch(()=>{});throw error;}
finally{await browser.close();}
