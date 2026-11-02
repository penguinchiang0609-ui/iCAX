import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';
import {verifyReopenedFrame,activeHandles} from './FiveAxisAlignmentResume.native.browser.mjs';

export async function verifyWorkpieceRemoval({page,read,rpc,refresh,output,secondPath}){
  const cases=[],initial=await read(),first=initial.placements[0];
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisImportActive);
  const remove=()=>page.locator('.cam-info-pane').getByRole('button',{name:'删除工件',exact:true});
  assert.equal(await remove().count(),1);assert.equal(await page.locator('.cam-context-pane [data-five-command="workpiece.remove"]').count(),0);
  await selectWorkpieceFile(page,secondPath);await idle();await activeHandles(page);
  const two=await read(),second=two.placements.find(p=>p.id!==first.id);assert.ok(second);
  await page.screenshot({path:resolve(output,'right-panel-delete.png')});
  await remove().click();await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.scene.domain.placements.length===1);await activeHandles(page);
  const one=await read();assert.deepEqual(one.placements,[first]);assert.deepEqual(one.workpieces,initial.workpieces);
  assert.deepEqual(one.setups.find(s=>s.id===first.setupId),initial.setups.find(s=>s.id===first.setupId));
  assert.equal(await page.evaluate(()=>fixture.view.scene.activePlacementId),first.id);
  assert.equal(await page.evaluate(()=>fixture.view.viewport.sceneObjects.has(fixture.view.scene.activePlacementId)),true);
  await rpc({action:'undo'});await refresh();await activeHandles(page);assert.deepEqual((await read()).placements,two.placements);
  await rpc({action:'redo'});await refresh();await activeHandles(page);assert.deepEqual((await read()).placements,one.placements);
  await verifyReopenedFrame({page,rpc,read});
  cases.push('The right panel deletes only the current real imported workpiece; the remaining workpiece and its setup stay unchanged, become active, and survive native undo/redo plus save/reopen');

  await page.evaluate(()=>{fixture.dropApply=true;fixture.dropQuery=true;});await remove().click();await idle();
  await page.getByRole('button',{name:'核对删除结果',exact:true}).waitFor();const empty=await read();assert.equal(empty.workpieces.length,0);
  assert.equal(await page.evaluate(()=>!!fixture.view.viewport.transformManipulator),false);
  assert.equal(await page.locator('[data-five-workpiece]:not(:disabled)').count(),0);
  await page.getByRole('button',{name:'核对删除结果',exact:true}).click();await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.scene.domain.workpieces.length===0);
  assert.deepEqual(await read(),empty);assert.equal(await remove().count(),0);
  assert.equal(await page.evaluate(()=>fixture.view.viewport.sceneObjects.size),0);assert.equal(await page.locator('.five-placement-tools').count(),0);
  cases.push('Lost deletion and query replies expose a retry with the same command; no transform or workpiece switch is possible until reconciliation, and the last deletion clears model, handles and parameters');
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  const reopened=await rpc({action:'save-reopen'});assert.equal(reopened.workpieces.length,0);assert.equal(reopened.placements.length,0);
  await page.evaluate(async()=>{const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);});
  await refresh();await selectWorkpieceFile(page,secondPath);await idle();await activeHandles(page);assert.equal((await read()).workpieces.length,1);
  await page.screenshot({path:resolve(output,'reimport-after-last-delete.png')});
  cases.push('An empty project saves/reopens and accepts another import with active Move handles');
  return cases;
}
