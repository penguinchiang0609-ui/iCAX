import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {writeFileSync} from 'node:fs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
try {
const browser=await chromium.connectOverCDP('http://127.0.0.1:9482'),page=browser.contexts()[0].pages()[0];
await page.waitForFunction(()=>__icaxAppShell?.getState().startupPhase==='ready');
await page.evaluate(()=>__icaxAppShell.openProject('D:/penguinchinage0609/iCAX/output/tests/machine-simulation/empty-instance.i5cam','icax.five-axis-cam'));
await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);return mv?.scene?.machines?.length&&!mv.pending;});
await page.locator('[data-tab-id="simulation"]').click();
await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();
await page.waitForFunction(()=>mv?.scene?.machines?.length&&!mv.pending&&mv.viewport?.sceneObjects?.size);
await page.locator('[data-five-machine-field="post:name"]').waitFor({state:'attached'});
await page.setViewportSize({width:1300,height:600});
const result=await page.evaluate(async()=>{
 const proxy=mv.viewportSceneProxy,original=proxy.invoke,calls=[],samples=[],m=mv.scene.machines[0];
 const x=m.joints.find(j=>j.jointName==='X'),c=m.joints.find(j=>j.jointName==='C');
 const objects=new Map(mv.viewport.sceneObjects),geometry=new Map(mv.viewport.geometryObjects),canvas=mv.viewport.renderer.domElement;
 const field=document.querySelector('[data-five-machine-field="post:name"]'),left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane');
 field.focus({preventScroll:true});field.setSelectionRange(1,3);left.scrollTop=80;right.scrollTop=600;
 const scroll=[left.scrollTop,right.scrollTop];let events=0;field.addEventListener('jog-regression',()=>events++);
 const before=()=>Object.fromEntries([...objects].map(([id,o])=>[id,[...o.matrix.elements]]));
 const native=()=>Object.fromEntries([...mv.viewport.transformPayloads].map(([id,p])=>[id,p.localToWorld]));
 const wait=()=>new Promise(r=>setTimeout(r,250));await wait();const start=before();
 proxy.invoke=async function(...args){const t=performance.now();try{return await original.apply(this,args);}finally{calls.push({method:args[0],ms:performance.now()-t});}};
 try {
  const cmd=async action=>{const t=performance.now();await __icaxAppShell.executeRibbonCommand('five.machine.'+action);samples.push(performance.now()-t);if(mv.fiveMachine.error)throw Error(mv.fiveMachine.error);await wait();};
  await cmd('jog-plus:'+x.entityId);const translated=before();
  await cmd('jog-minus:'+x.entityId);await cmd('jog-plus:'+c.entityId);const rotated=before();
  await cmd('jog-minus:'+c.entityId);
  for(let i=0;i<3;i++){await cmd('jog-plus:'+x.entityId);await cmd('jog-minus:'+x.entityId);}
  // Native limit rejection must leave both the joint and child transforms intact.
  let rejected=false;try{await original.call(proxy,'CamSimulation.CheckMachineMove',{expected:mv.scene.domain.expected,machineId:m.entityId,positions:{[x.entityId]:x.upper+1}});}catch(e){rejected=/LIMIT|行程/.test(e.message);}
  field.dispatchEvent(new Event('jog-regression'));
  return {samples,calls,start,translated,rotated,end:before(),native:native(),x:x.entityId,c:c.entityId,rejected,
   sameObjects:[...objects].every(([id,o])=>mv.viewport.sceneObjects.get(id)===o),sameGeometry:[...geometry].every(([id,o])=>mv.viewport.geometryObjects.get(id)===o),sameCanvas:canvas===mv.viewport.renderer.domElement,
   sameField:field===document.querySelector('[data-five-machine-field="post:name"]'),focused:document.activeElement===field,selection:[field.selectionStart,field.selectionEnd],scroll,afterScroll:[left.scrollTop,right.scrollTop],events};
 }finally{proxy.invoke=original;}
});
for(const k of ['sameObjects','sameGeometry','sameCanvas','sameField','focused','rejected'])assert.equal(result[k],true,k);
assert.deepEqual(result.selection,[1,3]);assert.deepEqual(result.afterScroll,result.scroll);assert.equal(result.events,1);
assert.ok(result.calls.every(c=>c.method==='CamSimulation.MoveMachine'));
const changed=(a,b)=>Object.keys(a).filter(id=>a[id].some((n,i)=>Math.abs(n-b[id][i])>1e-7));
const descendants=changed(result.start,result.translated);assert.ok(descendants.length>=4,'X must carry its children');
for(const id of descendants){assert.ok(Math.abs(result.translated[id][12]-result.start[id][12]-1)<1e-7);for(let i=0;i<16;i++)if(i!==12)assert.ok(Math.abs(result.translated[id][i]-result.start[id][i])<1e-7);}
assert.ok(changed(result.start,result.rotated).length>=2,'C must carry the cutting head');assert.equal(changed(result.start,result.end).length,0);
writeFileSync('output/tests/machine-simulation/jog-incremental-acceptance.json',JSON.stringify(result,null,2));
console.log(JSON.stringify({pass:true,warmMs:result.samples.slice(1),translatedParts:descendants.length,rotatedParts:changed(result.start,result.rotated).length,noSceneReads:true,objectsAndInteractionPreserved:true}));process.exit(0);
}catch(error){console.error(error);process.exit(1);}
