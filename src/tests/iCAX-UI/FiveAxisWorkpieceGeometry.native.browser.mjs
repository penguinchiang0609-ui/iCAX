import assert from 'node:assert/strict';
import {resolve} from 'node:path';

export async function verifyWorkpieceGeometry({page,read,rpc,refresh,output,sourcePath}){
  const cases=[],before=await read(),first=before.placements[0].id;
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const tabs=await page.evaluate(()=>fixture.entry.getRibbonDefinition(fixture.context).tabs);
  assert.deepEqual(tabs.map(t=>t.title),['工件','装夹','CAM','孪生','帮助']);
  const geometry=tabs[0].groups.find(g=>g.title==='几何');
  assert.deepEqual(geometry.commands.map(c=>c.title),['几何检查','几何修复','几何编辑','测量']);
  await page.evaluate(()=>fixture.geometryCanvas=fixture.view.viewport.renderer.domElement);
  for(const id of ['inspection','cad-repair','cad-edit','measure']){
    await command(`five.tool.${id}`);await idle();assert.equal(await page.evaluate(()=>fixture.view.fiveAxisArea),'workpiece');
    assert.equal(await page.locator(`[data-five-tool="${id}"]`).count(),1);assert.equal(await page.locator('.five-placement-tools').count(),0);
  }
  await command('five.tool.inspection');await page.locator('[data-five-command="workpiece.check"]').click();await idle();
  assert.ok(await page.evaluate(()=>fixture.view.fiveAxisUI.inspection?.report));
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  await command('five.tool.cad-repair');await page.locator('[data-five-field="repairTolerance"]').fill('0.25');
  await command('five.tool.measure');await command('five.tool.cad-repair');
  assert.equal(await page.locator('[data-five-field="repairTolerance"]').inputValue(),'0.25');
  assert.deepEqual((await read()).placements,before.placements);
  cases.push('Geometry group replaces the CAD tab; inspection, repair, edit and measurement remain on Workpiece, use the same canvas and preserve placement and repair drafts');
  await command('five.tool.inspection');const state=await read();
  let task=await rpc({action:'invoke',scope:'scene',method:'CamWorkpiece.Import',payload:{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,sourcePath}});
  while(task.status==='Running'){await new Promise(r=>setTimeout(r,30));task=await rpc({action:'invoke',scope:'scene',method:'CamWorkpiece.QueryTask',payload:{epoch:task.epoch,taskId:task.taskId}});}
  assert.equal(task.status,'Completed');await refresh();
  await page.locator(`[data-five-workpiece="${first}"]`).click();await idle();
  assert.equal(await page.evaluate(()=>fixture.view.fiveAxisUI.tools.workpiece),'inspection');
  assert.equal(await page.locator('.five-placement-tools').count(),0);
  assert.equal(await page.evaluate(()=>fixture.view.viewport.renderer.domElement===fixture.geometryCanvas),true);
  await command('five.tool.translate');await page.locator('.five-placement-tools').waitFor();
  assert.equal(await page.evaluate(()=>fixture.view.viewport.transformManipulator.control.mode),'translate');
  await page.screenshot({path:resolve(output,'geometry-in-workpiece.png')});
  cases.push('Changing the workpiece retains the chosen geometry operation; selecting Placement restores the default move gizmo without recreating the viewport');
  return cases;
}
