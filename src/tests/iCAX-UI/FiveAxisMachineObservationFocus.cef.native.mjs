import assert from 'node:assert/strict';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from './node_modules/playwright/index.mjs';

const out=resolve(process.env.OBSERVATION_OUTPUT||'output/tests/machine-transparency-20261029');mkdirSync(out,{recursive:true});
const browser=await chromium.connectOverCDP(process.env.MACHINE_CDP||'http://127.0.0.1:9586');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));
let original;
const command=id=>page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);
const settle=()=>page.waitForTimeout(180);
const toggle=value=>page.locator('[data-five-machine-field="observeAutomatic"]').evaluate((input,value)=>{input.checked=value;input.dispatchEvent(new Event('input',{bubbles:true}));input.dispatchEvent(new Event('change',{bubbles:true}));},value);
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready'&&!__icaxAppShell.getState().pendingCount);
 if(process.env.OBSERVATION_PROJECT){
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),resolve(process.env.OBSERVATION_PROJECT));
  await page.waitForFunction(async()=>{const{findProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');const view=findProjectView(__icaxAppShell.getState().activeProjectId);return view?.scene?.machines?.length&&!view.pending&&!__icaxAppShell.getState().pendingCount;});
  await page.evaluate(()=>__icaxAppShell.selectRibbonTab('simulation'));await settle();
 }
 await page.waitForFunction(async()=>{const{findProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');const view=findProjectView(__icaxAppShell.getState().activeProjectId);return view?.scene?.domain&&view.scene.machines?.some(m=>m.entityId===view.fiveMachine?.machineId)&&!view.pending&&!__icaxAppShell.getState().pendingCount;});
 if(process.env.OBSERVATION_TABLE_ID)await page.evaluate(async id=>{const{findProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs'),{setMachineObservationTable}=await import('../../../apps/five-axis-cam/webpage/machineObservation.mjs');setMachineObservationTable(findProjectView(__icaxAppShell.getState().activeProjectId),id);},process.env.OBSERVATION_TABLE_ID);
 original=await page.evaluate(async()=>{
  const{findProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs'),{activeTool}=await import('../../../apps/five-axis-cam/webpage/workspaceModel.mjs');
  window.mv=findProjectView(__icaxAppShell.getState().activeProjectId);
  window.observationReferences={canvas:mv.viewport.renderer.domElement,objects:new Map(mv.viewport.sceneObjects),geometry:new Map(mv.viewport.geometryObjects),domain:JSON.stringify(mv.scene.domain),machines:JSON.stringify(mv.scene.machines)};
  return {tool:activeTool(mv),area:mv.fiveAxisArea,selected:mv.fiveMachineSelectedPartId||'',camera:mv.viewport.getCameraState(),automatic:mv.fiveMachineObservation.automatic};
 });
 await page.evaluate(()=>__icaxAppShell.selectRibbonTab('simulation'));await command('five.tool.machine-instances');await command('five.machine.part:');
 await page.evaluate(()=>{observationReferences.canvas=mv.viewport.renderer.domElement;observationReferences.objects=new Map(mv.viewport.sceneObjects);observationReferences.geometry=new Map(mv.viewport.geometryObjects);});
 assert.equal(await page.locator('[data-five-machine-field="observeAutomatic"]').count(),1);
 assert.equal(await page.locator('[data-five-machine-field="observeTrail"]').count(),0);
 await toggle(true);
 await page.evaluate(camera=>{mv.viewport.setCameraState(camera);mv.fiveMachineObservation.signature='';},original.camera);
 await settle();
 const instance=await page.evaluate(()=>{const m=mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId),tableId=mv.fiveMachineObservation.tableId||m.elements.find(e=>e.workSupport)?.entityId;return {faded:[...mv.fiveMachineObservation.faded.keys()].map(o=>({id:o.userData.objectId,opacity:o.material.opacity,transparent:o.material.transparent,depthWrite:o.material.depthWrite})),tableId,tableOpacity:mv.viewport.sceneObjects.get(tableId)?.material.opacity,unconfigured:!mv.scene.domain.setups.find(s=>s.id===mv.scene.domain.activeSetupId)?.machineSetup?.machineId,declaredTools:m.tools.length,rayMs:mv.fiveMachineObservation.lastRaycastMs};});
 assert.ok(instance.faded.length>0,'the actual unconfigured native machine must become translucent');
 assert.ok(instance.faded.every(o=>o.opacity>0&&o.opacity<1&&o.transparent&&!o.depthWrite));
 assert.equal(instance.tableOpacity,1,'the worktable stays clearly visible');assert.ok(instance.rayMs>=0);
 assert.equal(await page.evaluate(()=>{const s=mv.fiveMachineObservation,m=mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId);return m.elements.some(e=>{const o=mv.viewport.sceneObjects.get(e.entityId);return o?.isMesh&&e.entityId!==s.tableId&&!s.faded.has(o)&&o.material.opacity===1;});}),true,'unobstructing body parts remain opaque in the actual machine');
 const identity=await page.evaluate(()=>{
  const field=document.querySelector('[data-five-machine-field="post:name"]'),left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane');
  if(field){field.focus({preventScroll:true});field.setSelectionRange(1,3);}
  left.scrollTop=200;right.scrollTop=400;
  window.observationInteraction={field,left,right,leftScroll:left.scrollTop,rightScroll:right.scrollTop,start:field?.selectionStart,end:field?.selectionEnd};
  return !!field;
 });
 assert.equal(identity,true,'real editable field for focus preservation');
 await toggle(false);await settle();
 const disabled=await page.evaluate(()=>({faded:mv.fiveMachineObservation.faded.size,preserved:(()=>{const s=observationInteraction;return s.field===document.querySelector('[data-five-machine-field="post:name"]')&&document.activeElement===s.field&&s.field.selectionStart===s.start&&s.field.selectionEnd===s.end&&s.left.scrollTop===s.leftScroll&&s.right.scrollTop===s.rightScroll;})()}));
 assert.equal(disabled.faded,0);assert.equal(disabled.preserved,true);
 await toggle(true);await settle();
 const selectedId=instance.faded[0].id;
 await command('five.machine.part:'+selectedId);await settle();
 const selected=await page.evaluate(id=>{const object=mv.viewport.sceneObjects.get(id);return {opacity:object.material.opacity,faded:mv.fiveMachineObservation.faded.has(object),selected:[...mv.viewport.selectedObjectIds]};},selectedId);
 assert.equal(selected.opacity,.16);assert.equal(selected.faded,true);assert.deepEqual(selected.selected,[selectedId]);
 await command('five.machine.part:');await toggle(false);await settle();
 const noTint=await page.evaluate(id=>{const object=mv.viewport.sceneObjects.get(id);return !object.userData.selectionMaterialState&&object.material.opacity===1;},selectedId);
 assert.equal(noTint,true,'restored materials do not retain selection state');
 await toggle(true);await settle();
 const beforeGesture=await page.evaluate(()=>mv.viewport.getCameraState());
 const box=await page.locator('.cam-viewport canvas').first().boundingBox().catch(()=>null)
  ||await page.evaluate(()=>{const r=mv.viewport.renderer.domElement.getBoundingClientRect();return{x:r.x,y:r.y,width:r.width,height:r.height};});
 await page.mouse.move(box.x+box.width*.6,box.y+box.height*.55);await page.mouse.down({button:'middle'});
 await page.mouse.move(box.x+box.width*.6+70,box.y+box.height*.55+25,{steps:10});await page.mouse.up({button:'middle'});await settle();
 const afterGesture=await page.evaluate(()=>mv.viewport.getCameraState());
 assert.notDeepEqual(afterGesture,beforeGesture,'real pointer orbit remains active');
 const views=[];
 for(const theta of [0,Math.PI/2,Math.PI,-Math.PI/2]){
  await page.evaluate(({camera,theta})=>{mv.viewport.setCameraState({...camera,theta});mv.fiveMachineObservation.signature='';},{camera:original.camera,theta});await settle();
  views.push(await page.evaluate(()=>({faded:[...mv.fiveMachineObservation.faded.keys()].map(o=>o.userData.objectId).sort(),rayMs:mv.fiveMachineObservation.lastRaycastMs,tableOpacity:mv.viewport.sceneObjects.get(mv.fiveMachineObservation.tableId)?.material.opacity})));
 }
 assert.ok(new Set(views.map(v=>JSON.stringify(v.faded))).size>1,'occluders follow the camera while the table target stays fixed');
 assert.ok(views.every(v=>v.tableOpacity===1));
 await page.evaluate(camera=>{mv.viewport.setCameraState(camera);mv.fiveMachineObservation.signature='';},original.camera);await settle();
 await page.screenshot({path:resolve(out,'instance-transparency-verified.png')});
 const stable=await page.evaluate(()=>({canvas:observationReferences.canvas===mv.viewport.renderer.domElement,objects:[...observationReferences.objects].every(([id,o])=>mv.viewport.sceneObjects.get(id)===o),geometry:[...observationReferences.geometry].every(([id,g])=>mv.viewport.geometryObjects.get(id)===g),domain:observationReferences.domain===JSON.stringify(mv.scene.domain),machines:observationReferences.machines===JSON.stringify(mv.scene.machines)}));
 assert.ok(Object.values(stable).every(Boolean),JSON.stringify(stable));
 await page.evaluate(()=>__icaxAppShell.selectRibbonTab('workpiece'));await settle();
 assert.equal(await page.evaluate(()=>mv.fiveMachineObservation.faded.size),0);
 await page.evaluate(()=>__icaxAppShell.selectRibbonTab('simulation'));await command('five.tool.machine-simulation');await settle();
 assert.equal(await page.locator('[data-five-machine-field="observeTrail"]').count(),1);
 assert.equal(await page.evaluate(()=>observationReferences.domain===JSON.stringify(mv.scene.domain)&&observationReferences.machines===JSON.stringify(mv.scene.machines)),true,'area transitions preserve native geometry and configuration');
 const report={passed:true,nativeWindow:true,instance,selected,disabled,noTint,realPointerOrbit:true,views,stable};
 writeFileSync(resolve(out,'native-acceptance.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
}catch(error){await page.screenshot({path:resolve(out,'native-failure.png')}).catch(()=>{});throw error;}
finally{
 if(original){await page.evaluate(area=>__icaxAppShell.selectRibbonTab(area),original.area).catch(()=>{});
  await command('five.tool.'+original.tool).catch(()=>{});
  await command('five.machine.part:'+original.selected).catch(()=>{});
  await page.evaluate(original=>{mv.viewport.setCameraState(original.camera);mv.fiveMachineObservation.automatic=original.automatic;mv.fiveMachineObservation.signature='';},original).catch(()=>{});
 }
 await browser.close();
}
