import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';
import {verifyObjectFrameProperties} from './FiveAxisObjectFrameProperties.acceptance.mjs';


export async function verifyPreparationPanels({page,rpc,info,output}){
  const cases=[],read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const done=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisImportActive);
  const refresh=()=>page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
  const dialog=page.locator('.five-blocking-progress');
  await page.evaluate(()=>{
    fixture.progressSamples=[];
    fixture.progressObserver=new MutationObserver(()=>fixture.progressSamples.push({
      count:document.querySelectorAll('.five-blocking-progress,.five-import-progress').length,
      title:document.querySelector('#five-blocking-progress-title')?.textContent||''}));
    fixture.progressObserver.observe(document.body,{subtree:true,childList:true,characterData:true});
    fixture.holdMethod='CamWorkpiece.QueryTask';fixture.hold=true;
  });
  await selectWorkpieceFile(page,info.sourcePath);await page.waitForFunction(()=>fixture.held);
  assert.equal(await dialog.count(),1);assert.equal(await page.locator('.five-import-progress').count(),0);
  assert.equal(await dialog.locator('h2').innerText(),'正在导入模型…');
  assert.equal(await dialog.locator('[role="progressbar"]').getAttribute('aria-valuenow'),null);
  await page.screenshot({path:resolve(output,'import-one-progress.png')});
  await page.getByRole('button',{name:'取消导入',exact:true}).click();
  await page.waitForFunction(()=>fixture.cancelAcknowledged);
  assert.equal(await dialog.locator('h2').innerText(),'正在取消导入…');
  assert.equal(await dialog.locator('button').isDisabled(),true);
  await page.evaluate(()=>fixture.release());await done();assert.equal(await dialog.count(),0);
  // A tiny STEP can finish natively before the pointer reaches Cancel.
  // Check the real request/terminal lifecycle, then clear that isolated result.
  if((await read()).workpieces.length){await rpc({action:'undo'});await refresh();}
  assert.equal((await read()).workpieces.length,0);
  cases.push('Native import cancellation uses one compact panel, sends the real task cancellation and releases the UI at its terminal state');

  await page.evaluate(()=>{fixture.holdAfterImport=true;});
  await selectWorkpieceFile(page,info.sourcePath);await page.waitForFunction(()=>fixture.held);
  assert.equal(await dialog.count(),1);assert.equal(await dialog.locator('h2').innerText(),'正在显示模型…');
  assert.equal(await dialog.locator('button').isVisible(),false);
  await page.screenshot({path:resolve(output,'import-display-progress.png')});
  await page.evaluate(()=>fixture.release());await done();
  await page.waitForFunction(()=>fixture.view.viewport?.getDebugState()?.objectCount===1);
  const samples=await page.evaluate(()=>{fixture.progressObserver.disconnect();return fixture.progressSamples;});
  assert.ok(samples.some(s=>s.count===1));assert.ok(samples.every(s=>s.count<=1&&!s.title.includes('选择')));
  assert.equal(await dialog.count(),0);
  cases.push('Native import, readback and display share a single progress owner, with no generic selection title or old overlay');

  const result=await verifyObjectFrameProperties({page,rpc,output});
  return {...result,cases:[...cases,...result.cases],samples};
}