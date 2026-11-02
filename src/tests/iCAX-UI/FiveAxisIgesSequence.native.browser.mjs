import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyIgesAfterStep({page,rpc,root,output,errors}) {
  page.setDefaultTimeout(180000);
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const large=resolve(root,'TestData/automotive-stamping/mercedes-benz-t-node/Mercedes-Benz_T-Node.stp');
  const iges=resolve(root,'TestData/automotive-stamping/autoform-easyblank-example/easyblank_example_01.igs');
  await selectWorkpieceFile(page,large);
  await page.waitForFunction(()=>!fixture.view.pending);
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const first=await read();assert.equal(first.workpieces.length,1);
  const attempts=[];
  // Wait until native preview has actually started, then open before it finishes.
  // A click faster than the preview debounce would miss the original overlap.
  for(let i=0;i<3;i++){
    const previewCount=await page.evaluate(()=>fixture.sdoTimings.filter(c=>c.method==='CamWorkpiece.PreviewFile').length);
    await selectWorkpieceFile(page,iges,{open:false});
    await page.waitForFunction(count=>fixture.sdoTimings.filter(c=>c.method==='CamWorkpiece.PreviewFile').length>count,previewCount);
    assert.equal(await page.evaluate(()=>fixture.view.fiveAxisFilePicker.previewStatus),'loading');
    const response=await page.evaluate(async()=>{const start=performance.now();await fixture.context.sceneProxy.invoke('CamScene.ReadState',{});return performance.now()-start;});
    assert.ok(response<2000,`Scene blocked by IGES preview: ${response} ms`);
    await page.locator('footer [data-nesting-file-action="open"]').click();
    await page.waitForFunction(()=>!fixture.view.pending);
    const error=await page.evaluate(()=>fixture.view.error),state=await read();
    attempts.push({attempt:i+1,error,workpieces:state.workpieces.length,stateReadDuringPreviewMs:response});
    writeFileSync(resolve(output,'iges-sequence-attempts.json'),JSON.stringify(attempts,null,2));
    assert.equal(error,'',`IGES import after STEP: ${error}`);
    assert.equal(state.workpieces.length,i+2);
    assert.deepEqual(state.workpieces[0],first.workpieces[0]);
    const imported=state.workpieces.at(-1);
    assert.equal(imported.importMetadata.transferredRoots,216);
    assert.equal(imported.importMetadata.validBRep,true);
    await page.waitForFunction(()=>fixture.view.viewport.sceneObjects.size===1);
    assert.equal(await page.evaluate(id=>fixture.view.viewport.sceneObjects.has(id),first.placements[0].id),false);
  }
  await page.screenshot({path:resolve(output,'step-then-iges.png')});
  const before=await read();
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  const reopened=await rpc({action:'save-reopen'});
  assert.deepEqual(reopened.workpieces,before.workpieces);
  assert.deepEqual(reopened.placements,before.placements);
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'iges-sequence-report.json'),JSON.stringify({passed:true,attempts,
    coverage:'Real STEP followed by repeated IGES picker imports, native Scene, View and embedded save/reopen; Edge, not standalone CEF'},null,2));
  console.log('STEP followed by IGES imports passed');
}
