import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyLargeWorkpiecePreview({page,rpc,root,output,calls,errors}) {
  page.setDefaultTimeout(120000);
  const small=resolve(root,'TestData/automotive-stamping/autoform-easyblank-example/easyblank_example_01.igs');
  const large=resolve(root,'TestData/automotive-stamping/mercedes-benz-t-node/Mercedes-Benz_T-Node.stp');
  await selectWorkpieceFile(page,small);
  await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.scene?.domain?.workpieces?.length===1);
  const original=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const responseTimes=[];
  const readDuringPreview=async()=>{
    const r=await page.evaluate(async()=>{
      const start=performance.now(),state=await fixture.context.sceneProxy.invoke('CamScene.ReadState',{});
      return {elapsedMs:performance.now()-start,state};
    });
    responseTimes.push(r.elapsedMs);
    assert.ok(r.elapsedMs<2000,`State must stay responsive during STEP preview: ${r.elapsedMs} ms`);
    assert.deepEqual(r.state,original);
  };
  const awaitPreviewStarted=()=>page.waitForFunction(()=>fixture.sdoTimings.filter(c=>c.method==='CamWorkpiece.PreviewFile').length>fixture.previewStartCount);
  await page.evaluate(()=>fixture.previewStartCount=fixture.sdoTimings.filter(c=>c.method==='CamWorkpiece.PreviewFile').length);
  await selectWorkpieceFile(page,large,{open:false});
  await awaitPreviewStarted();
  await readDuringPreview();
  await page.screenshot({path:resolve(output,'large-preview-running.png')});
  // Opening while the large preview is running must promptly cancel it and
  // send the normal import. All calls use the real SDK's unmodified deadlines.
  const importStarted=Date.now();
  await page.locator('footer [data-nesting-file-action="open"]').click();
  await page.waitForFunction(()=>fixture.view.fiveAxisImportTask?.status==='Running',null,{timeout:5000});
  await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.scene?.domain?.workpieces?.length===2,null,{timeout:180000});
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  await page.waitForFunction(()=>fixture.view.viewport.getDebugState().objectCount===1);
  assert.equal(await page.evaluate(id=>fixture.view.viewport.sceneObjects.has(id),original.placements[0].id),false);
  const imported=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  assert.deepEqual(imported.workpieces.find(w=>w.id===original.workpieces[0].id),original.workpieces[0]);
  const importMs=Date.now()-importStarted;
  await page.screenshot({path:resolve(output,'large-second-workpiece.png')});
  // Start another uncached large preview, then navigate away and choose a
  // different file. The late old result must neither block nor replace it.
  await page.evaluate(()=>fixture.previewStartCount=fixture.sdoTimings.filter(c=>c.method==='CamWorkpiece.PreviewFile').length);
  await selectWorkpieceFile(page,large,{open:false});
  await awaitPreviewStarted();
  const switchStarted=Date.now(),input=page.locator('[data-nesting-file-directory]');
  await input.fill(small.replaceAll('\\','/').slice(0,small.replaceAll('\\','/').lastIndexOf('/')));
  await input.press('Enter');
  await page.locator('[data-nesting-file-is-directory="false"]').filter({hasText:'easyblank_example_01.igs'}).click();
  await page.waitForFunction(()=>fixture.view.fiveAxisFilePicker?.previewStatus==='ready',null,{timeout:15000});
  const switchMs=Date.now()-switchStarted;
  assert.ok((await page.evaluate(()=>fixture.view.fiveAxisFilePicker.selectedPath)).endsWith('easyblank_example_01.igs'));
  await page.locator('footer [data-nesting-file-action="cancel"]').click();
  await page.waitForFunction(()=>!fixture.view.fiveAxisFilePicker);
  assert.deepEqual(await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}}),imported);
  const timings=await page.evaluate(()=>fixture.sdoTimings);
  assert.ok(calls.filter(c=>c.method==='CamWorkpiece.ReleaseFilePreview').length>=3);
  assert.deepEqual(errors,[]);
  const timeouts=await page.evaluate(()=>fixture.logs.filter(row=>JSON.stringify(row).includes('timed out')));
  assert.deepEqual(timeouts,[]);
  writeFileSync(resolve(output,'large-preview-report.json'),JSON.stringify({passed:true,source:large,sourceBytes:109713661,
    responseTimes,importMs,switchMs,workpieces:imported.workpieces.length,
    sdkDefaultTimeoutMs:await page.evaluate(()=>fixture.sdoClient.timeoutMs),timings,
    coverage:'Production picker, real SDOClient deadlines, native Scene methods and viewport; Edge, not standalone CEF'},null,2));
  console.log('Large file preview remains responsive; second workpiece import and preview cancellation passed');
}
