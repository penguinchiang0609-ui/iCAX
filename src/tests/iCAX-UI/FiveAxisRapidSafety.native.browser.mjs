import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import {hasRapidGeometry} from '../../apps/five-axis-cam/webpage/rapidGeometry.mjs';

export async function verifyRapidSafety({page,rpc,output}) {
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState'), setup=s=>s.setups.find(t=>t.id===s.activeSetupId);
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const wait=async task=>{while(task.status==='Running'){await new Promise(r=>setTimeout(r,100));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',task.error);return task;};
  const request=s=>({commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId});
  await rpc({action:'undo'});await command('five.refresh');
  let state=await read();assert.equal(state.intentToolpaths.length,0);
  const sources=await invoke('CamCut.QuerySources',{expected:state.expected,setupId:state.activeSetupId,placementId:state.placements[0].id});
  await command('five.tool.extract');
  await page.evaluate(faces=>{const e=extractionModule.extractionDraft(fixture.view).extraction;e.mode='Faces';e.faces=faces;extractionModule.extractionChanged(fixture.view);fixture.entry.mountProject(fixture.context);},['4','16','7','13','14','6'].map(id=>sources.faceUses.find(f=>f.localId===id)));
  await command('five.extraction.run');await page.waitForFunction(()=>!fixture.view.pending);
  await page.locator('[data-five-command="five.extraction.confirm"]').click();await page.waitForFunction(()=>!fixture.view.pending);
  state=await read();assert.equal(state.intentToolpaths.length,43);
  await wait(await invoke('CamPlan.Generate',request(state)));
  state=await read();
  console.log('Rapid safety: default built-in head, 43 paths on both skins');
  await wait(await invoke('CamRapid.Generate',{...request(state),geometryRevision:setup(state).generatedGeometry.revision}));
  state=await read();const generated=setup(state).generatedGeometry;
  writeFileSync(resolve(output,'rapid-safety-paths.json'),JSON.stringify(generated,null,2));
  const clear=generated.transitions.filter(t=>t.validation==='Clear'),blocked=generated.transitions.filter(t=>t.validation!=='Clear');
  console.log('Rapid safety results',clear.length,'clear,',blocked.length,'blocked',blocked.map(t=>t.reason));
  assert.ok(clear.length>30,'real bracket must yield useful default safe motion, not simply reject every connection');
  assert.ok(clear.some(t=>t.points.length>4),'opposite-skin travel needs an exterior corridor');
  assert.ok(clear.every(t=>t.scope==='BuiltinHeadEnvelope'));
  await command('five.refresh');await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await command('five.tool.simulation');
  const playback=await page.evaluate(()=>({rapidFrames:fixture.view.fiveSimulation.frames.filter(f=>f.role==='Rapid').length,lines:fixture.view.fiveGeneratedPreview.group.children.filter(o=>o.userData.generatedRapid).length}));
  assert.equal(playback.lines,generated.transitions.filter(hasRapidGeometry).length);
  assert.equal(playback.rapidFrames,clear.reduce((n,t)=>n+t.points.length,0));
  await command('view.fit');await page.screenshot({path:resolve(output,'rapid-safe-both-sides.png')});
  // Real native continuous collision checker independently checks the displayed
  // head. Cuts may have their own process issues; safe rapids must remain clear.
  console.log('Rapid safety: independent full-head continuous collision check');
  await wait(await invoke('CamSimulation.CheckCollision',request(state)));
  const checked=setup(await read()).simulationCheck;
  const rapidSegments=checked.segments.filter(s=>checked.frames[Number(s.index)]?.role==='Rapid'&&checked.frames[Number(s.index)+1]?.role==='Rapid');
  writeFileSync(resolve(output,'rapid-safety-collision.json'),JSON.stringify(checked,null,2));
  assert.ok(rapidSegments.length>clear.length);
  assert.ok(rapidSegments.every(s=>s.status==='Clear'),JSON.stringify(rapidSegments.filter(s=>s.status!=='Clear')));
  console.log('Rapid safety:',rapidSegments.length,'independent rapid intervals clear; checking rejected endpoints');
  // Failed candidates remain visible for editing, while playback still
  // refuses to animate a connection that has not passed its safety check.
  state=await read();await invoke('CamRapid.Configure',{...request(state),settings:{...setup(state).rapidSettings,collisionThresholdMm:2}});
  state=await read();await wait(await invoke('CamRapid.Generate',{...request(state),geometryRevision:setup(state).generatedGeometry.revision}));
  const failed=setup(await read()).generatedGeometry;
  assert.equal(failed.transitions.filter(t=>t.validation==='Clear').length,0);
  await command('five.refresh');await command('five.tool.simulation');
  const candidates=failed.transitions.filter(hasRapidGeometry);assert.ok(candidates.length>0,'failed connections retain editable candidate geometry');
  const warningLines=await page.evaluate(()=>fixture.view.fiveGeneratedPreview.group.children.filter(o=>o.userData.generatedRapid).map(o=>({unverified:o.userData.rapidUnverified,color:o.material.color.getHex()})));
  assert.equal(warningLines.length,candidates.length);assert.ok(warningLines.every(o=>o.unverified&&[0xff862b,0xe65b59].includes(o.color)));
  assert.equal(await page.evaluate(()=>fixture.view.fiveSimulation.frames.filter(f=>f.role==='Rapid').length),0);
  const stop=await page.evaluate(async()=>{const {advancePlayback}=await import('/src/apps/five-axis-cam/webpage/trajectoryPlayback.mjs');return advancePlayback(fixture.view.fiveSimulation.frames,0,1e6);});
  assert.equal(stop.stopped,true);assert.match(stop.reason,/碰撞|检查/);
  await rpc({action:'undo'});await command('five.refresh');
  assert.deepEqual(setup(await read()).generatedGeometry,generated);
  const reopened=await rpc({action:'save-reopen'});assert.deepEqual(setup(reopened).generatedGeometry,generated);
  await command('five.refresh');await command('five.tool.simulation');
  writeFileSync(resolve(output,'rapid-safety-report.json'),JSON.stringify({passed:true,paths:43,clear:clear.length,blocked:blocked.length,independentlyCheckedRapidSegments:rapidSegments.length,cases:['default head participates without manual envelope','opposite skins route outside obstacles','every animated rapid passes independent continuous full-head check','failed paths stay visible in warning colors for editing and are excluded from animated motion','playback stops at failed connection','undo and save reopen preserve checks'],coverage:{native:true,browser:'Edge',standaloneCef:false}},null,2));
}
