import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyIgesCuts({page,rpc,root,output,errors}) {
  page.setDefaultTimeout(180000);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  await selectWorkpieceFile(page,resolve(root,'TestData/automotive-stamping/autoform-easyblank-example/easyblank_example_01.igs'));await idle();
  // Reproduce the user's placement edits before extraction, with current revisions.
  for(const angle of [30,60,90])await page.evaluate(async angle=>{
    const d=fixture.view.scene.domain,p=d.placements[0],a=angle*Math.PI/180,c=Math.cos(a),s=Math.sin(a);
    await fixture.context.sceneProxy.invoke('CamPlacement.SetTransform',{commandId:crypto.randomUUID(),expected:d.expected,placementId:p.id,setupId:p.setupId,
      transform:[c,-s,0,0,s,c,0,0,0,0,1,0,0,0,0,1]});
    await fixture.entry.handleRibbonCommand(fixture.context,'five.refresh');
  },angle);
  await page.evaluate(()=>fixture.view.viewport.fitView());
  await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
  await command('five.tool.extract');await command('five.extraction.automatic');await idle();
  const candidates=await page.locator('[data-five-candidate]').count();
  assert.equal(candidates,219);
  await page.locator('[data-five-command="five.extraction.confirm"]').click();await idle();
  await page.screenshot({path:resolve(output,'iges-cut-confirmation.png')});
  writeFileSync(resolve(output,'confirmation-state.json'),JSON.stringify(await page.evaluate(()=>({error:fixture.view.error,logs:fixture.logs,paths:fixture.view.scene.domain.intentToolpaths.length})),null,2));
  const state=await read();assert.equal(state.intentToolpaths.length,181);
  assert.equal(await page.locator('[data-five-intent]').count(),state.intentToolpaths.length);
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  await command('five.refresh');await idle();assert.deepEqual((await read()).intentToolpaths,state.intentToolpaths);
  const rows=page.locator('[data-five-intent]');await rows.first().click();await rows.last().click({modifiers:['Shift']});await idle();
  assert.equal(await page.locator('[data-five-intent][aria-pressed="true"]').count(),181);
  await command('five.tool.reverse');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const reversed=await read();
  for(const cut of state.intentToolpaths){
    const after=reversed.intentToolpaths.find(c=>c.id===cut.id);assert.equal(after.reversed,!cut.reversed);
    assert.deepEqual(after.curveRef,cut.curveRef);assert.deepEqual(after.startPoint,cut.startPoint);
  }
  await rpc({action:'undo'});await command('five.refresh');await idle();assert.deepEqual((await read()).intentToolpaths,state.intentToolpaths);
  await rpc({action:'redo'});await command('five.refresh');await idle();assert.deepEqual((await read()).intentToolpaths,reversed.intentToolpaths);
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.intentToolpaths,reversed.intentToolpaths);
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'iges-cuts-report.json'),JSON.stringify({passed:true,candidates,cuts:state.intentToolpaths.length,placementRotations:3,batchReverse:181,undoRedo:true,saveReopen:true,coverage:'Production Scene and UI in Edge; standalone CEF not automated'},null,2));
}
