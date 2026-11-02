import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {selectModelFile} from './fixtures/fiveAxisFilePicker.mjs';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out=resolve(process.env.SPLIT_MACHINE_OUTPUT||'output/tests/split-chain-machines-20261024');mkdirSync(out,{recursive:true});
const defaultDefinitionCount=JSON.parse(readFileSync('src/apps/five-axis-cam/product.manifest.json','utf8')).capabilities.machineDefinition.defaults.length;
const browser=await chromium.connectOverCDP(process.env.MACHINE_CDP||'http://127.0.0.1:9500'),page=browser.contexts()[0].pages()[0];
const models=[{key:'RC-1000',folder:'rotary-four-plus-one',id:'f4ed0130-37ef-45fb-9883-7af0b1fc1000',branch:['C'],supportName:'中央装夹承托盘',supportWorldTop:0},
 {key:'TC-800',folder:'cradle-three-plus-two',id:'a30b2cab-c634-4b3a-94c0-c7ad1eac0800',branch:['A','C'],supportName:'C 回转工作台',supportWorldTop:1000}];
const settle=()=>page.waitForFunction(()=>{const p=mv.fiveMachineParameterEdits,s=mv.fiveMachineIK;return !mv.pending&&!mv.fiveMachine?.busy&&!__icaxAppShell.getState().pendingCount&&!s?.gesture&&!s?.finishing&&!s?.inflight&&!s?.fileSaving&&!p?.request&&!p?.fileSaving&&![...(p?.entries.values()||[])].some(e=>e.timer||e.ready);},null,{timeout:30000});
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await settle();const error=await page.evaluate(()=>mv.fiveMachine?.error||mv.error||'');assert.equal(error,'',id+': '+error);};
async function bind(path,create){
 if(create)await page.evaluate(({path,name})=>__icaxAppShell.createProject({productId:'icax.five-axis-cam',projectName:name+' 五轴联动演示',projectPath:path}),{path,name:path.includes('RC-1000')?'RC-1000 转盘式':'TC-800 摇篮式'});
 else await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
 await page.waitForFunction(async path=>{if(__icaxAppShell.getState().activeProjectPath.replaceAll('\\','/')!==path.replaceAll('\\','/'))return false;const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.fixture={view:mv};return mv?.scene?.machineDefinitions&&!mv.pending&&!__icaxAppShell.getState().pendingCount;},path);
 await page.waitForTimeout(400);
 await page.waitForFunction(count=>mv?.scene?.machineDefinitions?.length===count&&!mv.pending&&!__icaxAppShell.getState().pendingCount,defaultDefinitionCount);
}
const snapshot=()=>page.evaluate(()=>{
 const m=mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId)||mv.scene.machines[0],setup=mv.scene.domain.setups.find(s=>s.id===mv.scene.domain.activeSetupId),configuration=setup?.machineSetup,placement=mv.scene.domain.placements[0];
 const matrix=id=>{const o=mv.viewport.sceneObjects.get(id);o?.updateWorldMatrix(true,false);return o?.matrixWorld.toArray();};
 const support=configuration?.supportId&&mv.viewport.sceneObjects.get(configuration.supportId),part=placement&&mv.viewport.sceneObjects.get(placement.id);
 support?.updateWorldMatrix(true,false);part?.updateWorldMatrix(true,false);
 // Each native render object owns one mesh; its children can include the
 // temporary selected-object axis cones, which are not authored machine faces.
 let triangles=0;const ids=new Set(m.elements.map(e=>e.entityId));for(const [id,o] of mv.viewport.sceneObjects)if(ids.has(id)&&o.isMesh&&o.geometry)triangles+=(o.geometry.index?.count||o.geometry.attributes.position.count)/3;
 return {id:m.entityId,joints:Object.fromEntries(m.joints.map(j=>[j.jointName,j.position])),configuration,placement:placement?.transform,
  supportWorld:configuration&&matrix(configuration.supportId),partWorld:placement&&matrix(placement.id),relative:support&&part?support.matrixWorld.clone().invert().multiply(part.matrixWorld).toArray():null,triangles};
});
const near=(a,b,tolerance=1e-7)=>{assert.equal(a.length,b.length);a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<tolerance,`${i}: ${v} vs ${b[i]}`));};
const expectedTriangles=model=>{const folder=resolve('src/apps/five-axis-cam/resources/machines',model.folder),xml=readFileSync(resolve(folder,model.key+'.urdf'),'utf8');let count=0;for(const match of xml.matchAll(/filename="([^"]+\.stl)"/g))count+=readFileSync(resolve(folder,match[1])).readUInt32LE(80);return count;};
async function refresh(){await command('five.refresh');}
async function selectJoint(name){const id=await page.evaluate(name=>mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId).joints.find(j=>j.jointName===name).entityId,name);await command('five.machine.part:'+id);return id;}
async function setJoint(name,value){const id=await selectJoint(name),input=page.locator(`.cam-info-pane [data-five-machine-field="joint:${id}"]`);await input.fill(String(value));await input.blur();await settle();assert.equal(await page.evaluate(()=>mv.fiveMachine.error||''),'');}
async function stateAfterView(){await page.waitForFunction(()=>{const setup=mv.scene.domain.setups.find(s=>s.id===mv.scene.domain.activeSetupId),p=mv.scene.domain.placements[0];return !!setup?.machineSetup&&!!p&&!!mv.viewport.sceneObjects.get(p.id)&&!!mv.viewport.sceneObjects.get(setup.machineSetup.supportId);});await page.waitForTimeout(150);return snapshot();}
try{
 await page.setViewportSize({width:1600,height:1000});await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 const cdp=await page.context().newCDPSession(page);await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:true});
 if(process.argv.includes('--reopen')){
  const expected=JSON.parse(readFileSync(resolve(out,'cef-expected.json'),'utf8'));
  for(const record of expected){await bind(record.path,false);await page.locator('[data-tab-id="simulation"]').click();await settle();await command('five.tool.machine-instances');await refresh();const actual=await stateAfterView();assert.deepEqual(actual.joints,record.state.joints);assert.deepEqual(actual.configuration,record.state.configuration);near(actual.relative,record.state.relative);assert.equal(actual.triangles,record.state.triangles);await page.screenshot({path:resolve(out,record.key+'-reopened.png')});}
  writeFileSync(resolve(out,'cef-reopened.json'),JSON.stringify({pass:true,freshProcess:true,models:expected.map(x=>x.key)},null,2));console.log('PASS fresh process retains both split-chain machines, joint values, support bindings and mounted workpieces');process.exit(0);
 }
 const evidence=[],saved=[];
 for(const model of models){
  const path=resolve(out,model.key+'-demo.i5cam');await bind(path,true);
  const defaults=await page.evaluate(()=>mv.scene.machineDefinitions.map(d=>d.id));assert.ok(models.every(m=>defaults.includes(m.id)));assert.equal(defaults.length,defaultDefinitionCount);
  // Refresh the authored definition through the ordinary import operation;
  // the catalog intentionally seeds only once and preserves user deletion.
  await page.evaluate(async model=>{await mv.viewportSceneProxy.invoke('MachineDefinition.Import',{machineDefinitionId:model.id,name:model.key+(model.key==='RC-1000'?' 转盘式五轴激光机床（4+1）':' 摇篮式五轴激光机床（3+2）'),sourcePath:model.source});}, {...model,source:resolve('src/apps/five-axis-cam/resources/machines',model.folder,model.key+'.urdf')});
  await refresh();await page.locator('[data-tab-id="simulation"]').click();await settle();await command('five.tool.machine-definitions');
  await page.locator(`[data-five-command="five.machine.definition-select:${model.id}"]`).click();await page.locator('[data-command-id="five.machine-panel.instantiate"]').click();await settle();
  await page.waitForFunction(()=>mv.scene.machines.length===1&&mv.viewport.sceneObjects.size>=10);await page.evaluate(()=>{mv.viewport.setViewDirection([1,-1.5,.8]);mv.viewport.fitViewToViewport(1);});
  const initial=await snapshot();assert.equal(initial.triangles,expectedTriangles(model));
  const physical=await page.evaluate(()=>{const m=mv.scene.machines[0];return {joints:m.joints.filter(j=>j.type!=='fixed').map(j=>j.jointName),support:m.elements.filter(e=>e.workSupport).map(e=>e.name),tools:m.elements.filter(e=>e.hasTool).map(e=>e.name)};});
  assert.equal(physical.joints.length,5);assert.equal(physical.support.length,1);assert.equal(physical.tools.length,1);
  await page.screenshot({path:resolve(out,model.key+'-initial.png')});
  await page.locator('[data-tab-id="workpiece"]').click();await settle();await page.locator('[data-command-id="workpiece.import"]').click();await selectModelFile(page,resolve(out,'rotary-cup.step'),{preview:true});
  await page.waitForFunction(()=>mv.scene.domain.workpieces.length===1&&!mv.pending&&!__icaxAppShell.getState().pendingCount,null,{timeout:60000});
  await page.locator('[data-tab-id="simulation"]').click();await settle();await command('five.tool.machine-simulation');
  await page.locator('[data-command-id="five.machine-panel.mount"]').click();await settle();
  const mounting=await page.evaluate(()=>({error:mv.fiveMachine.error,mount:mv.fiveMachine.mounting}));assert.equal(mounting.error,'');assert.equal(mounting.mount?.collisionStatus,'Clear');
  await command('five.tool.machine-instances');let base=await stateAfterView();
  // surfaceZMm and supportBounds belong to the support's local frame. A
  // source-frame rebase changes them while the actual seating plane stays put.
  const seating=await page.evaluate(mount=>{
   const m=mv.scene.machines[0],setup=mv.scene.domain.setups.find(s=>s.id===mv.scene.domain.activeSetupId),configuration=setup.machineSetup,p=mv.scene.domain.placements[0];
   const support=mv.viewport.sceneObjects.get(configuration.supportId),part=mv.viewport.sceneObjects.get(p.id);
   const bounds=o=>{o.updateWorldMatrix(true,false);o.geometry.computeBoundingBox();const b=o.geometry.boundingBox.clone().applyMatrix4(o.matrixWorld);return {min:b.min.toArray(),max:b.max.toArray()};};
   const supportBounds=bounds(support),partBounds=bounds(part),centre=mount.supportBounds.min.map((v,i)=>(v+mount.supportBounds.max[i])/2),point=support.position.clone().set(centre[0],centre[1],mount.surfaceZMm).applyMatrix4(support.matrixWorld);
   return {supportName:m.elements.find(e=>e.entityId===configuration.supportId).name,supportWorldTop:supportBounds.max[2],partWorldBottom:partBounds.min[2],mountSurfaceWorldZ:point.z};
  },mounting.mount);
  assert.equal(seating.supportName,model.supportName,'mount uses the declared seating body, excluding raised chuck jaws');
  assert.ok(Math.abs(seating.supportWorldTop-model.supportWorldTop)<.01,JSON.stringify(seating));
  assert.ok(Math.abs(seating.mountSurfaceWorldZ-seating.supportWorldTop)<.01,JSON.stringify(seating));
  assert.ok(Math.abs(seating.partWorldBottom-seating.supportWorldTop)<.01,'the mounted workpiece bottom meets the physical support face: '+JSON.stringify(seating));
  mounting.seating=seating;
  await setJoint('C',30);let spun=await stateAfterView();near(spun.relative,base.relative);assert.notDeepEqual(spun.partWorld,base.partWorld);assert.ok(Math.abs(spun.joints.C-Math.PI/6)<1e-10);
  await command('edit.undo');let undone=await stateAfterView();near(undone.partWorld,base.partWorld);await command('edit.redo');let redone=await stateAfterView();near(redone.partWorld,spun.partWorld);
  if(model.key==='TC-800'){await setJoint('A',25);let tilted=await stateAfterView();near(tilted.relative,base.relative);assert.notDeepEqual(tilted.supportWorld,spun.supportWorld);assert.notDeepEqual(tilted.partWorld,spun.partWorld);}
  else{await setJoint('B',15);let tiltedHead=await stateAfterView();near(tiltedHead.supportWorld,spun.supportWorld);near(tiltedHead.partWorld,spun.partWorld);}
  // A real FK drag changes the workpiece branch, and its native accepted
  // matrices must move the mounted workpiece in the same gesture.
  await selectJoint(model.branch[0]);await command('five.machine-ik.fk');await page.evaluate(()=>{mv.viewport.setViewDirection([1,-1.5,.8]);mv.viewport.fitViewToViewport(1);});
  const beforeDrag=await snapshot();await gizmoDriver(page).drag('Z',4);await settle();const drag=await page.evaluate(()=>({status:mv.fiveMachineIK.lastResult?.status,message:mv.fiveMachineIK.message}));assert.equal(drag.status,'Ready',JSON.stringify(drag));const afterDrag=await stateAfterView();near(afterDrag.relative,beforeDrag.relative);assert.notDeepEqual(afterDrag.partWorld,beforeDrag.partWorld);
  // Move an intermediate tool-side node with IK. Its upstream linear axes
  // solve the target while downstream coordinates and the workpiece stay put.
  await selectJoint('Y');await command('five.machine-ik.ik');await command('five.machine-ik.translate');
  const beforeIK=await snapshot();await gizmoDriver(page).drag('X',4);await settle();const ik=await page.evaluate(()=>({status:mv.fiveMachineIK.lastResult?.status,message:mv.fiveMachineIK.message}));assert.equal(ik.status,'Ready',JSON.stringify(ik));const afterIK=await stateAfterView();assert.notDeepEqual(afterIK.joints,beforeIK.joints);assert.equal(afterIK.joints.Z,beforeIK.joints.Z);if(model.key==='RC-1000')assert.equal(afterIK.joints.B,beforeIK.joints.B);near(afterIK.partWorld,beforeIK.partWorld);
  const views=[];for(const projectionMode of ['perspective','orthographic']){await page.evaluate(projectionMode=>{mv.viewport.setCameraState({...mv.viewport.getCameraState(),projectionMode});},projectionMode);await page.waitForTimeout(150);await page.screenshot({path:resolve(out,model.key+'-'+projectionMode+'.png')});views.push(projectionMode);}
  await command('five.machine-ik.close');await page.evaluate(path=>mv.viewportSceneProxy.invoke('Project.Save',{projectPath:path}),path);await settle();
  const state=await snapshot();saved.push({key:model.key,path,state});evidence.push({key:model.key,physical,triangles:initial.triangles,mounting,workpieceFollows:true,undoRedo:true,gesture:drag,intermediateIK:ik,views});
 }
 writeFileSync(resolve(out,'cef-expected.json'),JSON.stringify(saved,null,2));writeFileSync(resolve(out,'cef-acceptance.json'),JSON.stringify({pass:true,evidence},null,2));console.log('PASS real CEF: four templates, two native instances, actual rotational STEP, mounted C/A following, FK drag, undo/redo and both projections');process.exit(0);
}catch(error){await page.screenshot({path:resolve(out,'cef-failure.png')}).catch(()=>{});console.error(error);process.exit(1);}
