import assert from 'node:assert/strict';
import {resolve} from 'node:path';

export async function verifyWorkpieceEnable({page,read,rpc,refresh,output}){
  const initial=await read(),cases=[];
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const action=name=>page.locator('.cam-info-pane').getByRole('button',{name,exact:true});
  const commands=()=>page.evaluate(()=>fixture.entry.getRibbonDefinition(fixture.context).tabs.find(t=>t.id==='workpiece').groups[0].commands.map(c=>[c.id,c.title,c.disabled]));
  assert.deepEqual(await commands(),[['workpiece.import','导入',false],['workpiece.remove','删除',false],['workpiece.lock','锁定',false],['workpiece.enable','禁用',false]]);
  await command('workpiece.enable');await idle();await action('启用工件').waitFor();
  const disabled=await read();assert.equal(disabled.workpieces[0].enabled,false);assert.deepEqual(disabled.placements,initial.placements);
  assert.equal(disabled.setups[0].generatedGeometry.status,'Disabled');
  assert.match(await page.locator('[data-five-key="workpiece-list"]').innerText(),/已禁用/);
  assert.equal(await page.evaluate(()=>fixture.view.viewport.sceneObjects.size),1);
  const cmd=await commands();assert.equal(cmd.at(-1)[1],'启用');
  for(const area of ['fixture','toolpath','simulation']){
    await page.evaluate(area=>fixture.context.actions.selectRibbonTab(area),area);await idle();
    await page.waitForFunction(()=>fixture.view.viewport.sceneObjects.size===0);
    assert.equal(await page.locator('[data-five-workpiece],[data-five-intent],.five-fixture-editor').count(),0);
    assert.equal(await page.evaluate(()=>fixture.view.scene.intentToolpaths.length),0);
    assert.match(await page.locator('[data-five-key="no-enabled-workpiece"]').innerText(),/暂无已启用工件/);
  }
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('workpiece'));await idle();await page.waitForFunction(()=>fixture.view.viewport.sceneObjects.size===1);
  await rpc({action:'undo'});await refresh();assert.equal((await read()).workpieces[0].enabled,true);
  await rpc({action:'redo'});await refresh();assert.equal((await read()).workpieces[0].enabled,false);
  cases.push('Disabled workpiece remains available in Workpiece but disappears from Fixture/CAM/Simulation scenes, selectors, cuts and fixture controls; undo/redo restores enable state');
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});
  assert.equal(reopened.workpieces[0].enabled,false);
  await page.evaluate(async()=>{const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);});await idle();
  await action('启用工件').click();await idle();assert.equal((await read()).workpieces[0].enabled,true);
  await command('workpiece.lock');await idle();assert.equal((await read()).workpieces[0].locked,true);assert.equal((await commands())[2][1],'解锁');
  assert.equal((await commands())[1][2],true);
  await command('workpiece.enable');await idle();const protectedDisabled=await read();assert.equal(protectedDisabled.workpieces[0].enabled,false);assert.equal(protectedDisabled.workpieces[0].locked,true);
  await command('workpiece.enable');await idle();await command('workpiece.lock');await idle();
  assert.deepEqual((await read()).placements,initial.placements);
  await page.evaluate(()=>{fixture.dropApply=true;fixture.dropQuery=true;});await command('workpiece.enable');await idle();
  await action('核对启用状态').waitFor();const uncertain=await read();
  await action('核对启用状态').click();await idle();assert.deepEqual(await read(),uncertain);
  await action('启用工件').click();await idle();assert.equal((await read()).workpieces[0].enabled,true);
  await page.screenshot({path:resolve(output,'workpiece-actions.png')});
  cases.push('Enable state survives native save/reopen and unknown receipt recovery; ribbon labels follow current state, lock protects deletion, and enable switching preserves the independent pose lock');
  return cases;
}
