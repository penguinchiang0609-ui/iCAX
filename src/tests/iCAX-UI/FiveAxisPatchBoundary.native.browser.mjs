import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyPatchBoundary({page,rpc,root,output,errors}) {
  page.setDefaultTimeout(180000);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  await selectWorkpieceFile(page,resolve(root,'TestData/FA/26.9.2零件.IGS'));await idle();
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
  await command('five.tool.extract');
  await page.evaluate(async()=>{
    window.patchExtraction=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');
    const d=fixture.view.scene.domain,p=d.placements[0];
    const source=await fixture.context.sceneProxy.invoke('CamCut.QuerySources',{expected:d.expected,setupId:p.setupId,placementId:p.id});
    const draft=patchExtraction.extractionDraft(fixture.view).extraction;
    draft.mode='Faces';draft.faces=source.faceUses.filter(f=>['1','176'].includes(f.localId));
    patchExtraction.extractionChanged(fixture.view);fixture.entry.mountProject(fixture.context);
  });
  await command('five.extraction.run');await idle();
  const result=await page.evaluate(()=>({error:fixture.view.error,result:patchExtraction.extractionResult(fixture.view)}));
  assert.equal(result.error,'');assert.equal(result.result.candidates.length,7);
  assert.ok(result.result.candidates.every(c=>c.confirmationEligibility==='Ready'));
  const seams=new Set(['2','13','24','35','39','50','61','6400','6391','6382','6373','6369','6360','6351']);
  const spans=result.result.candidates.flatMap(c=>c.boundary.sourceSpans);
  assert.equal(spans.length,114);assert.ok(spans.every(s=>!seams.has(s.edgeId)));
  writeFileSync(resolve(output,'patch-candidates.json'),JSON.stringify(result,null,2));
  await page.evaluate(()=>fixture.view.viewport.fitView(1.5));
  await page.screenshot({path:resolve(output,'adjacent-faces.png')});
  await page.locator('[data-five-command="five.extraction.confirm"]').click();await idle();
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const confirmed=await read();assert.equal(confirmed.intentToolpaths.length,7);
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  assert.equal(await page.locator('[data-five-intent]').count(),7);
  await page.screenshot({path:resolve(output,'confirmed-patch-boundaries.png')});
  await rpc({action:'undo'});await command('five.refresh');await idle();assert.equal((await read()).intentToolpaths.length,0);
  await rpc({action:'redo'});await command('five.refresh');await idle();assert.deepEqual((await read()).intentToolpaths,confirmed.intentToolpaths);
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.intentToolpaths,confirmed.intentToolpaths);
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,source:'TestData/FA/26.9.2零件.IGS',faces:[1,176],removedInternalEdgeUses:14,retainedExactSpans:114,closedContours:7,confirmedCuts:7,undoRedo:true,saveReopen:true,coverage:'Production import, extraction, creation, resources and scene in Edge; standalone CEF not automated'},null,2));
}
