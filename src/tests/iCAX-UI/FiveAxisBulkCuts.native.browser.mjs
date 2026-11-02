import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';

// All geometry, mutations, receipts, View resources and persistence are native.
// Only transport delivery is delayed to exercise the real SDK timeout timers.
export async function verifyBulkCuts({page,rpc,info,output,calls,errors}) {
  page.setDefaultTimeout(120000);
  console.log('Bulk: importing native 480-hole STEP');
  await selectWorkpieceFile(page,info.sourcePath);
  await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisImportActive);
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  await page.waitForFunction(()=>fixture.view.viewport?.getDebugState()?.objectCount>0);
  console.log('Bulk: imported; delaying View response');
  await page.evaluate(async()=>{
    window.extractionModule=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');
    window.originalCanvas=document.querySelector('canvas');
    fixture.delayResponses={'View.GetOrCreate':12000};
    await fixture.context.actions.selectRibbonTab('toolpath');
  });
  await page.waitForFunction(()=>fixture.sdoTimings.some(t=>t.method==='View.GetOrCreate'&&t.elapsedMs>=12000));
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const extract=async()=>{
    console.log('Bulk: extracting candidates');
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.tool.extract'));
    await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.extraction.automatic'));
    await page.waitForFunction(()=>!fixture.view.pending);
    const result=await page.evaluate(()=>extractionModule.extractionResult(fixture.view));
    assert.ok(result,await page.evaluate(()=>fixture.view.error));
    assert.equal(result.candidates.length,481);
    const holes=result.candidates.filter(c=>c.geometryKind==='Circle');assert.equal(holes.length,480);
    assert.ok(holes.every(c=>c.confirmationEligibility==='Ready'&&c.machiningKind==='Cut'));
    const outer=result.candidates.filter(c=>c.geometryKind!=='Circle');assert.equal(outer.length,1);
    await page.locator(`[data-five-candidate="${outer[0].candidateId}"]`).click();
    assert.equal(await page.evaluate(()=>extractionModule.extractionDraft(fixture.view).extraction.selected.length),480);
    return result;
  };
  const result=await extract();
  console.log('Bulk: confirming 480 cuts with delayed response');
  await page.evaluate(()=>{fixture.delayResponses={'CamCut.Create':16000};});
  await page.locator('[data-five-command="five.extraction.confirm"]').click();
  await page.waitForFunction(()=>!!extractionModule.extractionDraft(fixture.view).extraction.confirmationRequest);
  const pendingRequest=await page.evaluate(()=>extractionModule.extractionDraft(fixture.view).extraction.confirmationRequest);
  assert.equal(await page.locator('[data-five-command="five.extraction.confirm"]').isDisabled(),true);
  for(const close of await page.locator('[data-five-command="five.extraction.close"]').all())assert.equal(await close.isDisabled(),true);
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.extraction.close'));
  assert.equal(await page.getByRole('dialog',{name:'添加刀路'}).count(),1);
  await page.screenshot({path:resolve(output,'creating-480.png')});
  await page.waitForFunction(()=>!fixture.view.pending);
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const created=await invoke('CamScene.ReadState');
  console.log('Bulk: native batch committed');
  assert.equal(created.intentToolpaths.length,480);
  await page.waitForFunction(()=>fixture.view.fiveToolAxisDisplay?.data?.cuts?.length===480);
  const poses=await page.evaluate(()=>({errors:fixture.view.fiveToolAxisDisplay.data.errors,groups:fixture.view.fiveToolAxisDisplay.group.children.length}));
  assert.deepEqual(poses.errors,[]);assert.equal(poses.groups,480,'all paths remain visible beyond the per-request sample budget');
  assert.equal(await page.locator('.five-cut-tree [data-five-intent]').count(),480);
  assert.equal(await page.evaluate(()=>document.querySelector('canvas')===originalCanvas),true);
  assert.equal(calls.filter(c=>c.method==='CamCut.Create').length,1);
  assert.equal(calls.filter(c=>c.method==='CamScene.QueryCommand').length,0,'no premature receipt reconciliation');
  const receipt=await invoke('CamScene.QueryCommand',{epoch:pendingRequest.expected.epoch,commandId:pendingRequest.commandId});
  assert.equal(receipt.status,'Completed');assert.equal(receipt.result.createdIntentIds.length,480);
  assert.deepEqual(await invoke('CamCut.Create',pendingRequest),receipt.result,'exact retry returns original receipt');
  assert.equal((await invoke('CamScene.ReadState')).intentToolpaths.length,480);
  assert.equal((await rpc({action:'undo'})).state.intentToolpaths.length,0,'one undo removes the whole batch');
  assert.deepEqual((await rpc({action:'redo'})).state.intentToolpaths,created.intentToolpaths);
  await page.screenshot({path:resolve(output,'created-480.png')});
  const timings=await page.evaluate(()=>fixture.sdoTimings);
  const createTiming=timings.find(t=>t.method==='CamCut.Create');assert.ok(createTiming.elapsedMs>=16000);
  assert.ok(createTiming.nativeMs<15000,`480 native cuts took ${createTiming.nativeMs} ms`);
  const logs=await page.evaluate(()=>fixture.logs);assert.ok(!logs.some(row=>row[0]==='error'),JSON.stringify(logs));
  // An undelivered request times out, and the real receipt query is NotFound.
  // Keep its identity until an explicit retry establishes the final outcome.
  assert.equal((await rpc({action:'undo'})).state.intentToolpaths.length,0);
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
  await extract();
  await page.evaluate(()=>{fixture.delayResponses={};fixture.withholdCreateOnce=true;fixture.forceCreateTimeoutOnce=50;});
  await page.locator('[data-five-command="five.extraction.confirm"]').click();
  await page.waitForFunction(()=>!fixture.view.pending);
  assert.match(await page.evaluate(()=>fixture.view.error),/创建结果尚未确认/);
  const uncertain=await page.evaluate(()=>extractionModule.extractionDraft(fixture.view).extraction.confirmationRequest);
  assert.ok(uncertain?.commandId);
  assert.equal((await invoke('CamScene.ReadState')).intentToolpaths.length,0);
  // This retry reaches native and commits, but its response arrives after the
  // shortened test timer. The real receipt must recover success without error.
  await page.evaluate(()=>{fixture.forceCreateTimeoutOnce=50;fixture.delayResponses={'CamCut.Create':200};});
  await page.locator('[data-five-command="five.extraction.confirm"]').click();
  await page.waitForFunction(()=>!fixture.view.pending);
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const recovered=await invoke('CamScene.ReadState');assert.equal(recovered.intentToolpaths.length,480);
  const recoveredReceipt=await invoke('CamScene.QueryCommand',{epoch:uncertain.expected.epoch,commandId:uncertain.commandId});
  assert.equal(recoveredReceipt.status,'Completed');
  assert.equal(await page.evaluate(()=>extractionModule.extractionDraft(fixture.view).extraction.confirmationRequest??null),null);
  assert.equal((await rpc({action:'undo'})).state.intentToolpaths.length,0);
  assert.deepEqual((await rpc({action:'redo'})).state.intentToolpaths,recovered.intentToolpaths);
  assert.deepEqual(errors,[]);
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.intentToolpaths,recovered.intentToolpaths);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,candidates:result.candidates.length,created:480,
    timings,logs,undoRedo:true,idempotentReceipt:true,notFoundPreservesRequest:true,lostResponseRecovered:true,saveReopen:true,canvasPreserved:true,
    coverage:'Real native 480-hole STEP, production picker/extraction/create/View/persistence, real SDK timeouts with delayed delivery, Edge browser; standalone CEF not automated'},null,2));
  console.log(`480-cut native browser regression passed; native creation ${createTiming.nativeMs.toFixed(0)} ms`);
}
