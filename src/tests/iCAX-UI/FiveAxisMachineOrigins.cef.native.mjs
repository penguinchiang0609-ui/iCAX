import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {copyFileSync,mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out=resolve(process.env.MACHINE_ORIGIN_OUTPUT||'output/work/machine-kinematics/machine-origin-20261024/cef');
mkdirSync(out,{recursive:true});
const browser=await chromium.connectOverCDP(process.env.MACHINE_CDP||'http://127.0.0.1:9500'),page=browser.contexts()[0].pages()[0];
const settle=()=>page.waitForFunction(()=>{const p=mv.fiveMachineParameterEdits,s=mv.fiveMachineIK;return !mv.pending&&!mv.fiveMachine?.busy&&!mv.fiveHistoryBusy&&!__icaxAppShell.getState().pendingCount&&!s?.gesture&&!s?.finishing&&!s?.inflight&&!s?.fileSaving&&!p?.request&&!p?.fileSaving&&![...(p?.entries.values()||[])].some(e=>e.timer||e.ready);},null,{timeout:30000});
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await settle();assert.equal(await page.evaluate(()=>mv.fiveMachine?.error||''),'',id);};
const near=(a,b,t=1e-6)=>{assert.equal(a.length,b.length);a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<t,`${i}: ${v} != ${b[i]}`));};
const input=axis=>page.locator(`.cam-info-pane [data-five-machine-field="origin:${axis}"]`);
async function bind(path){
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
 await page.waitForFunction(async path=>{if(__icaxAppShell.getState().activeProjectPath.replaceAll('\\','/')!==path.replaceAll('\\','/'))return false;const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);return mv?.scene?.machines?.length&&!mv.pending&&!__icaxAppShell.getState().pendingCount;},path);
 await page.waitForTimeout(400);await page.locator('[data-tab-id="simulation"]').click();await settle();await command('five.tool.machine-instances');
 await page.waitForFunction(()=>mv.viewport?.sceneObjects.size>10);await page.evaluate(()=>{mv.viewport.setViewDirection([1,-1.5,.8]);mv.viewport.fitViewToViewport(1);});
}
const read=()=>page.evaluate(()=>{
 const machine=mv.scene.machines[0],setup=mv.scene.domain.setups.find(s=>s.id===mv.scene.domain.activeSetupId),part=mv.scene.domain.placements[0];
 const world=id=>{const o=mv.viewport.sceneObjects.get(id);o?.updateWorldMatrix(true,false);return o?.matrixWorld.toArray();};
 return {id:machine.entityId,elements:machine.elements.map(e=>({id:e.entityId,parent:e.parentEntityId,kind:e.kind,...(Array.isArray(e.restPosition)?{restPosition:e.restPosition}:{})})),joints:machine.joints.map(j=>({id:j.entityId,name:j.jointName,type:j.type,axis:j.axis,position:j.position,lower:j.lower,upper:j.upper,home:j.homePosition,velocity:j.velocity})),
  world:Object.fromEntries(machine.elements.filter(e=>mv.viewport.sceneObjects.has(e.entityId)).map(e=>[e.entityId,world(e.entityId)])),configuration:setup.machineSetup,placement:part.transform,partWorld:world(part.id),supportWorld:world(setup.machineSetup.supportId)};
});
async function readNative(){return page.evaluate(()=>mv.viewportSceneProxy.invoke('CamSimulation.ReadMachine',{setupId:mv.scene.domain.activeSetupId}));}
async function select(id){await command('five.machine.part:'+id);await page.waitForFunction(id=>mv.fiveMachineSelectedPartId===id,id);}
async function edit(axis,value){await input(axis).fill(String(value));await input(axis).blur();await settle();assert.equal(await page.evaluate(()=>mv.fiveMachine.error||''),'');}
async function renderedPart(expected){await page.waitForFunction(expected=>{const p=mv.scene.domain.placements[0],o=mv.viewport.sceneObjects.get(p.id);return o&&expected.every((v,i)=>Math.abs(o.matrix.elements[i]-v)<1e-6);},expected);}
async function renderedObject(id,expected){await page.waitForFunction(({id,expected})=>{const o=mv.viewport.sceneObjects.get(id);return o&&expected.every((v,i)=>Math.abs(o.matrix.elements[i]-v)<1e-6);},{id,expected});}
function translatedInParent(state,id,amount,source=state.partWorld){const e=state.elements.find(e=>e.id===id),parent=state.world[e.parent]||[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],result=[...source];for(let i=0;i<3;++i)result[12+i]+=parent[8+i]*amount;return result;}
const evidence=[],saved=[];
try{
 await page.setViewportSize({width:1600,height:1000});await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 const cdp=await page.context().newCDPSession(page);await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:true});
 if(process.argv.includes('--reopen')){
  const expected=JSON.parse(readFileSync(resolve(out,'expected.json'),'utf8'));
  for(const record of expected){await bind(record.path);await command('five.refresh');const actual=await read();assert.deepEqual(actual.elements,record.state.elements);assert.deepEqual(actual.joints,record.state.joints);near(actual.partWorld,record.state.partWorld);for(const [id,world] of Object.entries(record.state.world))near(actual.world[id],world);await select(record.selected);near(await input('z').evaluate(e=>[Number(e.value)]),[record.value]);}
  writeFileSync(resolve(out,'reopened.json'),JSON.stringify({pass:true,freshProcess:true,models:expected.map(x=>x.model)},null,2));console.log('PASS fresh process preserves origin offsets, fixed machine axes and mounted workpiece frames');
 }else for(const model of ['RC-1000','TC-800']){
  const path=resolve(out,model+'-origin.i5cam');copyFileSync(resolve('output/tests/split-chain-machines-20261024',model+'-demo.i5cam'),path);await bind(path);
  const baseline=await read(),joint=baseline.joints.find(j=>j.name==='C'),origin=baseline.elements.find(e=>e.id===joint.id).restPosition;assert.equal(origin.length,3);assert.ok(Math.abs(joint.position)>.1,'verify the structural edit at a nonzero joint angle');
  await select(joint.id);assert.equal(await page.locator('[data-five-key="machine-origin"] [data-five-machine-field]').count(),3);assert.equal(await page.locator('[data-five-machine-field^="core:"],[data-five-machine-field^="origin:axis"],[data-five-machine-field^="origin:r"]').count(),0);
  const nativeBefore=await readNative(),target=origin[2]+2,expectedPart=translatedInParent(baseline,joint.id,2);await edit('z',target);await renderedPart(expectedPart);const changed=await read(),nativeAfter=await readNative();
  assert.deepEqual(changed.joints,baseline.joints,'origin editing cannot change axis/type/q/home/limits');assert.deepEqual(changed.elements.map(e=>[e.id,e.parent,e.kind]),baseline.elements.map(e=>[e.id,e.parent,e.kind]));assert.deepEqual(changed.placement,baseline.placement);assert.deepEqual(changed.configuration,baseline.configuration);
  near(changed.partWorld,expectedPart);const beforeNode=nativeBefore.nodes.find(n=>n.id===joint.id),afterNode=nativeAfter.nodes.find(n=>n.id===joint.id);const expectedOrigin=[...beforeNode.origin];expectedOrigin[11]+=2;near(afterNode.origin,expectedOrigin);assert.notEqual(nativeBefore.modelKey,nativeAfter.modelKey);
  const oldExport=await page.evaluate(async modelKey=>{try{await mv.viewportSceneProxy.invoke('CamSimulation.ExportMachineProgram',{expected:mv.scene.domain.expected,setupId:mv.scene.domain.activeSetupId,modelKey});return '';}catch(e){return e.message;}},nativeBefore.modelKey);assert.match(oldExport,/机床或后置已变化|重新仿真/);
  await command('edit.undo');await renderedPart(baseline.partWorld);near((await read()).partWorld,baseline.partWorld);await command('edit.redo');await renderedPart(expectedPart);near((await read()).partWorld,changed.partWorld);
  // The server must reject any attempt to turn this position-only operation
  // into axis/rotation/type/hierarchy editing, without a partial position write.
  const invalid=[];for(const extra of [{axis:[0,1,0]},{rotation:[0,0,1]},{parentEntityId:baseline.id},{jointType:'prismatic'}])invalid.push({entityId:joint.id,position:[900,800,700],...extra});invalid.push({entityId:joint.id,position:[1,2]},{entityId:baseline.id,position:[1,2,3]},{entityId:joint.id,position:[1,2,'3']});
  for(const request of invalid){const rejection=await page.evaluate(async request=>{try{await mv.viewportSceneProxy.invoke('Machine.SetElementPosition',request);return '';}catch(e){return e.message;}},request);assert.ok(rejection,JSON.stringify(request));}await command('five.refresh');assert.deepEqual((await read()).elements,changed.elements);assert.deepEqual((await read()).joints,changed.joints);
  const fixed=baseline.joints.find(j=>j.id===baseline.configuration.supportId&&j.type==='fixed')?.id||baseline.joints.find(j=>j.id===baseline.configuration.toolId&&j.type==='fixed')?.id;assert.ok(fixed,'exercise a genuine fixed part');await select(fixed);assert.equal(await page.locator('[data-five-key="machine-origin"] [data-five-machine-field]').count(),3);
  const fixedBase=await read(),fixedPosition=fixedBase.elements.find(e=>e.id===fixed).restPosition;await edit('z',fixedPosition[2]+1);await renderedObject(fixed,translatedInParent(fixedBase,fixed,1,fixedBase.world[fixed]));await renderedPart(fixed===baseline.configuration.supportId?translatedInParent(fixedBase,fixed,1):fixedBase.partWorld);const fixedChanged=await read();assert.deepEqual(fixedChanged.joints,baseline.joints);near(fixedChanged.configuration.placement,fixedBase.configuration.placement);
  let retention;
  if(model==='RC-1000'){
   await page.setViewportSize({width:1600,height:650});await select(joint.id);await input('x').scrollIntoViewIfNeeded();
   await page.evaluate(()=>{window.originProxy=mv.viewportSceneProxy;window.originInvoke=originProxy.invoke;window.originHeld=true;window.originCanvas=mv.viewport.renderer.domElement;originProxy.invoke=async function(method,...args){const result=await originInvoke.call(this,method,...args);if(method==='Machine.SetElementPosition'&&originHeld)await new Promise(resolve=>window.originRelease=resolve);return result;};});
   await input('x').fill('0.125');await page.waitForFunction(()=>!!window.originRelease);await input('x').fill('0.250');
   await input('x').evaluate(e=>{window.originInput=e;e.setSelectionRange(1,4);window.originScrollNodes=[...document.querySelectorAll('.cam-context-pane,.cam-info-pane,.cam-context-pane *,.cam-info-pane *')].filter(n=>n.clientHeight>0&&n.scrollHeight>n.clientHeight+2);for(const [i,n] of originScrollNodes.entries())n.scrollTop=Math.min(n.scrollHeight-n.clientHeight,25+i*13);window.originScroll=originScrollNodes.map(n=>n.scrollTop);originHeld=false;originRelease();});
   await settle();retention=await input('x').evaluate(e=>({sameNode:e===originInput,focus:document.activeElement===e,range:[e.selectionStart,e.selectionEnd],value:e.value,canvas:mv.viewport.renderer.domElement===originCanvas,scroll:originScrollNodes.map(n=>n.scrollTop),expected:originScroll,left:originScrollNodes.some(n=>n.matches('.cam-context-pane')||n.closest('.cam-context-pane')),right:originScrollNodes.some(n=>n.matches('.cam-info-pane')||n.closest('.cam-info-pane'))}));
   assert.equal(retention.sameNode,true);assert.equal(retention.focus,true);assert.equal(retention.canvas,true);assert.deepEqual(retention.range,[1,4]);assert.equal(retention.value,'0.250');assert.deepEqual(retention.scroll,retention.expected);assert.ok(retention.left&&retention.right);await page.evaluate(()=>mv.viewportSceneProxy.invoke=originInvoke);await input('x').blur();await settle();assert.equal((await read()).elements.find(e=>e.id===joint.id).restPosition[0],.25);await page.setViewportSize({width:1600,height:1000});
  }
  await command('five.machine.part:');assert.equal(await page.locator('[data-five-key="machine-origin"]').count(),0,'the instance summary has no structure position editor');await select(joint.id);await command('five.machine-ik.close');await page.evaluate(path=>mv.viewportSceneProxy.invoke('Project.Save',{projectPath:path}),path);await settle();
  const state=await read();saved.push({model,path,state,selected:joint.id,value:state.elements.find(e=>e.id===joint.id).restPosition[2]});evidence.push({model,rotaryOriginAtNonzeroAngle:true,fixedPartOffset:true,fixedPartRole:fixed===baseline.configuration.supportId?'support':'tool',mountedWorkpieceFollows:true,onlyThreePositionFields:true,structureAndAxesPreserved:true,undoRedo:true,invalidAtomicRequests:invalid.length,oldExportRejected:oldExport,retention});await page.screenshot({path:resolve(out,model+'-origin.png')});
 }
 if(!process.argv.includes('--reopen')){writeFileSync(resolve(out,'expected.json'),JSON.stringify(saved,null,2));writeFileSync(resolve(out,'acceptance.json'),JSON.stringify({pass:true,evidence},null,2));console.log('PASS native CEF: position-only machine offsets, fixed axes, following, undo/redo, invalid requests, stale export and retained editing state');}
}catch(error){await page.screenshot({path:resolve(out,'failure.png')}).catch(()=>{});console.error(error);process.exitCode=1;}
finally{await page.evaluate(()=>{if(window.originProxy&&window.originInvoke)originProxy.invoke=originInvoke;if(window.originRelease)originRelease();}).catch(()=>{});await browser.close();}
