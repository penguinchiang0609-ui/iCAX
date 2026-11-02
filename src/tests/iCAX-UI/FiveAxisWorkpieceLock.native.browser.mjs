import assert from 'node:assert/strict';
import {resolve} from 'node:path';

export async function verifyWorkpieceLock({page,read,rpc,refresh,output}){
  const initial=await read(),id=initial.placements[0].id,cases=[];
  const button=name=>page.locator('.cam-info-pane').getByRole('button',{name,exact:true});
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const field=page.locator('[data-five-transform-field="position-X"]');
  assert.equal(await page.locator('[data-five-command="five.transform.lock"]').count(),0);
  assert.equal(await page.locator('[data-five-transform-field][readonly]').count(),0);
  // A dirty pose must be the exact pose protected by the next lock.
  await field.fill('13.5');
  await button('锁定工件').click();await idle();await button('解锁工件').waitFor();
  const locked=await read();assert.equal(locked.workpieces[0].locked,true);assert.equal(locked.placements[0].transform[3],13.5);
  assert.equal(await page.locator('[data-five-transform-field][readonly]').count(),9);
  assert.equal(await button('删除工件').isDisabled(),true);assert.equal(await page.locator('.five-placement-tools').count(),0);
  assert.equal(await page.evaluate(()=>!!fixture.view.viewport.transformManipulator),false);
  for(const tool of ['translate','rotate','scale','align'])await page.evaluate(tool=>fixture.entry.handleRibbonCommand(fixture.context,`five.tool.${tool}`),tool);
  await page.locator('.icax-three-viewport canvas').first().focus();for(const key of ['w','e','r'])await page.keyboard.press(key);
  assert.equal(await page.locator('.five-align-panel,.five-placement-tools').count(),0);assert.deepEqual(await read(),locked);
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await idle();assert.equal(await page.evaluate(()=>fixture.view.scene.model.locked),true);
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('workpiece'));await idle();await button('解锁工件').waitFor();
  cases.push('The persistent right-panel workpiece lock replaces the old transform lock; all nine fields, gizmos, W/E/R, alignment and deletion are blocked while CAM remains available');

  await rpc({action:'undo'});await refresh();assert.equal((await read()).workpieces[0].locked,false);await page.locator('.five-placement-tools').waitFor();
  await rpc({action:'redo'});await refresh();assert.equal((await read()).workpieces[0].locked,true);
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});assert.equal(reopened.workpieces[0].locked,true);
  await page.evaluate(async()=>{const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);});await idle();await button('解锁工件').waitFor();
  assert.equal(await page.locator('.five-placement-tools').count(),0);assert.equal(await button('删除工件').isDisabled(),true);
  await page.screenshot({path:resolve(output,'global-workpiece-locked.png')});
  await button('解锁工件').click();await idle();await page.locator('.five-placement-tools').waitFor();
  assert.equal(await button('删除工件').isDisabled(),false);assert.equal(await page.locator('[data-five-transform-field][readonly]').count(),0);
  assert.equal((await read()).placements.find(p=>p.id===id).transform[3],13.5);
  await page.evaluate(()=>{fixture.dropApply=true;fixture.dropQuery=true;});await button('锁定工件').click();await idle();await button('核对锁定结果').waitFor();
  const uncertain=await read();assert.equal(await page.locator('[data-five-transform-field][readonly]').count(),9);
  await button('核对锁定结果').click();await idle();assert.deepEqual(await read(),uncertain);await button('解锁工件').click();await idle();
  cases.push('Lock metadata survives undo/redo and project save/reopen; unlocking restores numeric and gizmo editing without changing the protected pose');
  return cases;
}
