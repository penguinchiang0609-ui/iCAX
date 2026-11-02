import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { verifyCutStarting } from './FiveAxisCutStarting.native.browser.mjs';

export async function verifyMultiFace({ page, rpc, calls, output }) {
  page.setDefaultTimeout(30000);
  const invoke = (method, payload = {}) => rpc({ action: 'invoke', scope: 'scene', method, payload });
  const read = () => invoke('CamScene.ReadState');
  const command = id => page.evaluate(id => fixture.entry.handleRibbonCommand(fixture.context, id), id);
  const idle = () => page.waitForFunction(() => !fixture.view.pending);
  const select = async ids => {
    const state = await read();
    const sources = await invoke('CamCut.QuerySources', { expected: state.expected, setupId: state.activeSetupId, placementId: state.placements[0].id });
    const faces = ids.map(id => sources.faceUses.find(f => f.localId === id));
    assert.ok(faces.every(Boolean));
    await command('five.tool.extract');
    await page.evaluate(faces => {
      const state = extractionModule.extractionDraft(fixture.view).extraction;
      state.mode = 'Faces'; state.faces = faces; extractionModule.extractionChanged(fixture.view);
      fixture.entry.mountProject(fixture.context);
    }, faces);
  };
  const result = () => page.evaluate(() => ({ error: fixture.view.error, result: extractionModule.extractionResult(fixture.view), selected: extractionModule.extractionDraft(fixture.view).extraction.selected }));
  const original = await read(); assert.equal(original.intentToolpaths.length, 27);
  await select(['4', '16', '7']);
  await command('five.extraction.run'); await idle();
  let extracted = await result(); assert.equal(extracted.error, '');
  assert.equal(extracted.result.candidates.length, 32);
  assert.equal(extracted.result.candidates.filter(c => c.confirmationEligibility !== 'Ready').length, 0);
  assert.equal(extracted.selected.length, 6);
  assert.match(await page.getByRole('dialog', { name: '添加刀路' }).innerText(), /26 条已存在/);
  assert.equal(await page.getByRole('button', { name: '确定', exact: true }).isEnabled(), true);
  await page.getByRole('button', { name: '确定', exact: true }).click(); await idle();
  let state = await read(); assert.equal(state.intentToolpaths.length, 33);
  for (const old of original.intentToolpaths) assert.deepEqual(state.intentToolpaths.find(c => c.id === old.id), old, 'existing edited paths are not silently replaced by a larger patch');
  assert.equal(new Set(state.intentToolpaths.map(c => c.sourceKey)).size, 33);
  await rpc({ action: 'undo' }); await rpc({ action: 'undo' }); await command('five.refresh');
  assert.equal((await read()).intentToolpaths.length, 0);

  await select(['4', '16', '7']);
  await command('five.extraction.run'); await idle();
  extracted = await result(); assert.equal(extracted.error, ''); assert.equal(extracted.selected.length, 32);
  assert.equal(extracted.result.candidates.filter(c => c.machiningKind === 'Mark').length, 21);
  assert.equal(extracted.result.candidates.filter(c => c.machiningKind === 'Cut').length, 11);
  const oppositeFaces = [...new Set(extracted.result.candidates.filter(c => c.machiningKind === 'Cut').flatMap(c => c.termination.terminalFaces))];
  assert.ok(oppositeFaces.length > 0);
  await page.evaluate(() => fixture.view.viewport.fitView());
  await page.screenshot({ path: resolve(output, 'multi-face-ready.png') });
  await page.getByRole('button', { name: '确定', exact: true }).click(); await idle();
  state = await read(); assert.equal(state.intentToolpaths.length, 32);
  assert.equal(state.intentToolpaths.filter(c => c.machiningKind === 'Mark').length, 21);
  assert.equal(state.intentToolpaths.filter(c => c.machiningKind === 'Cut').length, 11);
  let paths = state.intentToolpaths;

  await select(oppositeFaces); await command('five.extraction.run'); await idle();
  extracted = await result(); assert.equal(extracted.result.candidates.length, 11);
  assert.equal(extracted.selected.length, 11); assert.equal(extracted.error, '');
  assert.ok(extracted.result.candidates.every(c => !paths.some(p => p.sourceKey === c.sourceKey)));
  await page.getByRole('button', { name: '确定', exact: true }).click(); await idle();
  state = await read(); assert.equal(state.intentToolpaths.length, 43);
  for (const frontPath of paths) assert.deepEqual(state.intentToolpaths.find(c => c.id === frontPath.id), frontPath);

  // Each opposite-side path remains independently manageable by the user.
  const backPath = state.intentToolpaths.find(c => !paths.some(p => p.id === c.id));
  await page.locator(`[data-five-intent="${backPath.id}"]`).click();
  await command('five.tree.Delete'); await idle();
  let remaining = (await read()).intentToolpaths; assert.equal(remaining.length, 42);
  assert.ok(!remaining.some(c => c.id === backPath.id));
  for (const frontPath of paths) assert.deepEqual(remaining.find(c => c.id === frontPath.id), frontPath);
  await rpc({ action: 'undo' }); await command('five.refresh'); assert.equal((await read()).intentToolpaths.length, 43);
  await rpc({ action: 'redo' }); await command('five.refresh'); assert.equal((await read()).intentToolpaths.length, 42);
  await rpc({ action: 'undo' }); await rpc({ action: 'undo' }); await command('five.refresh');
  assert.equal((await read()).intentToolpaths.length, 32);

  await rpc({ action: 'undo' }); await command('five.refresh');
  assert.equal((await read()).intentToolpaths.length, 0);
  await select(['4', '16', '7', ...oppositeFaces]); await command('five.extraction.run'); await idle();
  extracted = await result(); assert.equal(extracted.error, ''); assert.equal(extracted.selected.length, 43);
  assert.equal(extracted.result.candidates.filter(c => c.machiningKind === 'Mark').length, 21);
  assert.equal(extracted.result.candidates.filter(c => c.machiningKind === 'Cut').length, 22);
  assert.equal(extracted.result.candidates.filter(c => c.boundary.sourceSpans.every(s => oppositeFaces.includes(s.faceId))).length, 11);
  assert.equal(extracted.result.candidates.filter(c => c.boundary.sourceSpans.every(s => ['4', '16', '7'].includes(s.faceId))).length, 32);
  const candidates=extracted.result.candidates;
  writeFileSync(resolve(output,'classification.json'),JSON.stringify(candidates.map(c=>({kind:c.geometryKind,role:c.boundaryRole,spans:c.boundary.sourceSpans,normal:c.boundary.normal,issues:c.issues})),null,2));
  for(const face of ['7','6'])assert.equal(candidates.filter(c=>c.geometryKind==='Slot'&&c.boundary.sourceSpans.length===5&&c.boundary.sourceSpans.every(s=>s.faceId===face)).length,2);
  assert.equal(candidates.filter(c=>c.boundaryRole==='OuterTrim').length,2);
  await page.evaluate(() => fixture.view.viewport.fitView());
  await page.screenshot({ path: resolve(output, 'both-sides-ready.png') });
  await page.getByRole('button', { name: '确定', exact: true }).click(); await idle();
  state = await read(); paths = state.intentToolpaths; assert.equal(paths.length, 43);
  const stage=c=>c.machiningKind==='Mark'?0:c.boundaryRole==='OuterTrim'?2:1;
  const ordered=state.setups.find(s=>s.id===state.activeSetupId).definitionOrder.map(id=>paths.find(c=>c.id===id));
  assert.deepEqual(ordered.map(stage),[...ordered.map(stage)].sort((a,b)=>a-b));
  assert.equal(paths.filter(c=>c.cutType==='StandardHole').length,20);
  assert.equal(await page.locator('[data-five-intent]').filter({hasText:'修边'}).count(),2);

  // Reproduce the original Confirm -> extract -> empty selection path. The
  // useful native extraction result must not be overwritten by a generic error.
  await select(['4', '16', '7']);
  const before = calls.filter(c => c.method === 'CamCut.Create').length;
  await page.getByRole('button', { name: '确定', exact: true }).click(); await idle();
  extracted = await result();
  assert.equal(extracted.error, '所选面的刀路已存在，无需重复添加。');
  assert.equal(calls.filter(c => c.method === 'CamCut.Create').length, before);
  assert.equal(await page.getByRole('button', { name: '确定', exact: true }).isDisabled(), true);
  assert.match(await page.getByRole('dialog', { name: '添加刀路' }).innerText(), /32 条已存在/);
  assert.doesNotMatch(await page.getByRole('dialog', { name: '添加刀路' }).innerText(), /待判定|有效候选/);
  await command('five.extraction.close');
  await page.evaluate(() => fixture.entry.releaseProject(fixture.context));
  await rpc({ action: 'save-reopen' });
  await page.evaluate(async () => {
    const { getProjectView } = await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const { ViewClient } = await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
    fixture.view = getProjectView(fixture.context.project.projectId);
    fixture.context.sceneProxy.views = new ViewClient(fixture.context.sceneProxy);
    fixture.entry.mountProject(fixture.context);
  });
  await idle(); await page.waitForFunction(() => fixture.view.viewport?.getDebugState()?.objectCount === 1);
  assert.deepEqual((await read()).intentToolpaths, paths);
  await command('five.tool.extract'); await command('five.extraction.close');
  await page.evaluate(() => fixture.view.viewport.fitView());
  await page.screenshot({ path: resolve(output, 'multi-face-confirmed.png') });
  // Generated tool axes must follow the oriented machining face on BOTH skins.
  state=await read();const setup=state.setups.find(s=>s.id===state.activeSetupId),placementId=state.placements[0].id;
  const faceMap=await invoke('CamPlacement.ReadReferenceFaces',{expected:state.expected,setupId:setup.id,placementId});
  const normals=new Map();
  for(const cut of paths.filter(c=>c.cutType==='StandardHole')){
    const candidate=candidates.find(c=>c.sourceKey===cut.sourceKey),faceId=candidate.boundary.sourceSpans[0].faceId;
    if(!normals.has(faceId))normals.set(faceId,(await invoke('CamPlacement.ResolveReference',{expected:state.expected,setupId:setup.id,placementId,query:{mode:'plane',frame:'Workpiece',nearPoint:cut.startPoint,maximumDistanceMm:.01,triangleIndex:faceMap.faces.find(f=>f.faceId===faceId).first}})).direction);
  }
  for(const [a,b] of [['4','13'],['7','6']])assert.ok(normals.get(a).reduce((sum,x,i)=>sum+x*normals.get(b)[i],0)<-.99999);
  let task=await invoke('CamPlan.Generate',{commandId:crypto.randomUUID(),expected:state.expected,setupId:setup.id});
  while(task.status==='Running'){await new Promise(r=>setTimeout(r,100));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',task.error);
  state=await read();const generated=state.setups.find(s=>s.id===setup.id).generatedGeometry;
  for(const cut of paths.filter(c=>c.cutType==='StandardHole')){
    const faceId=candidates.find(c=>c.sourceKey===cut.sourceKey).boundary.sourceSpans[0].faceId,normal=normals.get(faceId);
    for(const part of generated.operations.find(o=>o.intentId===cut.id).parts)for(const d of part.directions)assert.ok(d.reduce((sum,x,i)=>sum+x*normal[i],0)>.99999);
  }
  await command('five.refresh');
  assert.equal(await page.evaluate(async()=>{const {buildPlayback}=await import('/src/apps/five-axis-cam/webpage/trajectoryPlayback.mjs');const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');const d=fixture.view.scene.domain,s=d.setups.find(s=>s.id===d.activeSetupId),frames=buildPlayback(s,d.placements).frames;return frames.length>0&&frames.every(f=>new THREE.Vector3(0,0,1).applyQuaternion(f.rotation).dot(f.direction)>.99999);}),true);
  const slot=paths.find(c=>c.geometryKind==='Slot'&&candidates.find(k=>k.sourceKey===c.sourceKey)?.boundary.sourceSpans.every(s=>s.faceId==='7'));
  await page.locator(`[data-five-intent="${slot.id}"]`).click();
  await verifyCutStarting({page,rpc,calls,output,intentId:slot.id,automotive:true,focusContour:true});
  writeFileSync(resolve(output, 'multi-face-report.json'), JSON.stringify({ passed: true, faces: [4, 16, 7], oppositeFaces, candidates: 43, cut: 22, mark: 21, unresolved: 0, unchangedSingleFaceSources: 26,
    cases: ['multi-face native extraction and browser confirmation', 'same-side repeat extraction prevents duplicates', 'existing single-face paths are preserved', 'opposite-side paths can be added after the front side', 'opposite-side paths delete independently without changing front paths', 'delete undo redo restores independent paths', 'simultaneous opposite skins retain both sides', 'confirm after re-extraction retains already-exists feedback', 'save reopen retains both sides'],
    coverage: { native: 'production SDO and persistence', browser: 'Edge', faceSelection: 'exact native face IDs supplied to the browser draft', standaloneCef: false } }, null, 2));
}
