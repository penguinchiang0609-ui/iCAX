import assert from 'node:assert/strict';import {createRequire} from 'node:module';import {writeFileSync} from 'node:fs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
try{
 const b=await chromium.connectOverCDP('http://127.0.0.1:9482'),p=b.contexts()[0].pages()[0],out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation';
 await p.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 const path=out+'/工业机床碰撞验证.i5cam';await p.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
 await p.waitForFunction(async path=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);return __icaxAppShell.getState().activeProjectPath===path&&mv.scene?.machines?.length&&!mv.pending;},path);
 await p.locator('[data-tab-id="simulation"]').click();await p.locator('[data-command-id="five.tool.machine-simulation"]').click();await p.locator('[data-five-command="five.machine.build"]').click();await p.waitForFunction(()=>!mv.fiveMachine.busy,{},{timeout:120000});
 assert.equal(await p.evaluate(()=>mv.fiveMachine.report?.status),'Ready');
 await p.setViewportSize({width:1600,height:1000});
 const seek=async value=>p.locator('[data-five-machine-field="seek"]').evaluate((e,value)=>{e.value=value;e.dispatchEvent(new Event('input',{bubbles:true}));},String(value));
 await seek(40.5);
 const trace=await p.evaluate(()=>{
  const s=mv.fiveMachineObservation,t=s.trail;window.traceGeometry=t.lines.map(b=>b.line.geometry);window.objectReferences=new Map(mv.viewport.sceneObjects);
  return {counts:t.lines.map(b=>b.line.geometry.drawRange.count),colors:t.lines.map(b=>b.line.material.color.getHexString()),dashed:t.lines[0].line.material.isLineDashedMaterial,cut:t.lines[1].line.material.isLineBasicMaterial,cursor:mv.fiveMachine.cursor,total:t.lines.map(b=>b.ends.length*2)};
 });
 assert.ok(trace.counts[0]>0,'approach is G00');assert.ok(trace.counts[1]>0&&trace.counts[1]<trace.total[1]);assert.equal(trace.dashed,true);assert.deepEqual(trace.colors,['ffa340','ff3030']);
 await seek(12.5);const back=await p.evaluate(()=>mv.fiveMachineObservation.trail.lines.map(b=>b.line.geometry.drawRange.count));assert.ok(back[1]<trace.counts[1]);
 await seek(40.5);
 await p.locator('[data-five-machine-field="observeRapid"]').uncheck();assert.equal(await p.evaluate(()=>mv.fiveMachineObservation.trail.lines[0].line.visible),false);
 await p.locator('[data-five-machine-field="observeRapid"]').check();
 const views=[];
 for(const theta of [0,Math.PI/2,Math.PI,-Math.PI/2]){
  await p.evaluate(theta=>{mv.viewport.setCameraState({target:{x:500,y:0,z:1050},radius:2800,theta,phi:1.35});mv.fiveMachineObservation.signature='';},theta);
  await p.waitForTimeout(180);
  views.push(await p.evaluate(()=>({camera:mv.viewport.getCameraState(),faded:[...mv.fiveMachineObservation.faded.keys()].map(o=>o.userData.objectId),ms:mv.fiveMachineObservation.lastRaycastMs})));
 }
 assert.ok(views.some(v=>v.faded.length>0),'front machine parts become translucent');assert.ok(new Set(views.map(v=>JSON.stringify(v.faded))).size>1,'occluders follow camera');
 const selected=views.find(v=>v.faded.length>0);await p.evaluate(camera=>{mv.viewport.setCameraState(camera);mv.fiveMachineObservation.signature='';},selected.camera);await p.waitForTimeout(180);
 const opaque=await p.evaluate(()=>{const ids=[mv.fiveMachine.report.nodes[mv.fiveMachine.report.tool].id,...mv.scene.domain.placements.map(p=>p.id)];return ids.every(id=>{let ok=true;mv.viewport.sceneObjects.get(id)?.traverse(o=>{if(o.isMesh)for(const m of Array.isArray(o.material)?o.material:[o.material])ok&&=m.opacity===1;});return ok;});});assert.equal(opaque,true,'workpiece and cutting head stay opaque');
 await p.screenshot({path:out+'/camera-occlusion-verified.png'});
 await p.locator('[data-five-machine-field="observeAutomatic"]').uncheck();await p.waitForTimeout(180);assert.equal(await p.evaluate(()=>mv.fiveMachineObservation.faded.size),0);
 await p.locator('[data-five-machine-field="observeAutomatic"]').check();
 // Zoom into the actual cutting area for the exported visual evidence.
 await p.evaluate(()=>{const r=mv.fiveMachine.report,part=mv.scene.domain.placements[0],o=mv.viewport.sceneObjects.get(part.id);const target={x:o.matrix.elements[12],y:o.matrix.elements[13],z:o.matrix.elements[14]+25};mv.viewport.setCameraState({target,radius:250,theta:-Math.PI/2,phi:1.1});});await p.waitForTimeout(200);
 await p.screenshot({path:out+'/executed-cut-trail-verified.png'});
 const identity=await p.evaluate(()=>({geometry:traceGeometry.every((g,i)=>mv.fiveMachineObservation.trail.lines[i].line.geometry===g),objects:[...objectReferences].every(([id,o])=>mv.viewport.sceneObjects.get(id)===o),nativeCollidersVisible:[...mv.viewport.colliderObjects.values()].some(o=>o.visible)}));assert.equal(identity.geometry,true);assert.equal(identity.objects,true);assert.equal(identity.nativeCollidersVisible,false);
 await p.locator('[data-five-command="five.machine.reset"]').click();assert.deepEqual(await p.evaluate(()=>mv.fiveMachineObservation.trail.lines.map(b=>b.line.geometry.drawRange.count)),[0,0]);
 await p.locator('[data-tab-id="workpiece"]').click();assert.equal(await p.evaluate(()=>mv.fiveMachineObservation.trail),null);assert.equal(await p.evaluate(()=>mv.fiveMachineObservation.faded.size),0);
 writeFileSync(out+'/observation-acceptance.json',JSON.stringify({trace,back,views,opaque,identity,resetAndExit:true},null,2));console.log(JSON.stringify({pass:true,trace,back,views,identity}));process.exit(0);
}catch(e){console.error(e);process.exit(1)}
