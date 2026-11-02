import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyOrderingBeforeGeneration({page,rpc,calls,output}){
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const setup=s=>s.setups.find(x=>x.id===s.activeSetupId);
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const before=await read();assert.equal(setup(before).generatedGeometry.status,'Missing');
  const generationCalls=()=>calls.filter(c=>['CamPlan.Generate','CamRapid.Generate','CamSimulation.CheckCollision'].includes(c.method));
  assert.equal(generationCalls().length,0);
  const started=Date.now();await command('five.tool.auto-order');await idle();const automaticMs=Date.now()-started;
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const auto=await read(),ids=setup(auto).definitionOrder;
  assert.equal(auto.intentToolpaths.find(c=>c.id===ids.at(-1)).boundaryRole,'OuterTrim');
  assert.equal(setup(auto).generatedGeometry.status,'Missing');
  assert.deepEqual(auto.intentToolpaths,before.intentToolpaths);
  assert.equal(generationCalls().length,0,'sorting must never start geometry, travel or collision tasks');
  await page.screenshot({path:resolve(output,'automatic-before-generation.png')});
  await command('five.tool.manual-order');
  assert.equal(await page.locator('[data-five-order-id]').count(),ids.length);
  await page.locator(`[data-five-order-id="${ids.at(-1)}"]`).click();
  await page.locator(`[data-five-intent="${ids[1]}"]`).click();
  const selected=[ids.at(-1),ids[1]],expected=[...selected,...ids.filter(id=>!selected.includes(id))];
  assert.deepEqual(await page.evaluate(()=>fixture.view.fiveOrder.prefix),selected);
  // Save a draft with delayed native acceptance while both sidebars and the
  // nested dialog continue scrolling. Keep the live canvas and focus intact.
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.five-position-body::after{content:"";display:block;height:1000px}.five-position-body{height:130px;overflow:auto}.cam-info-pane{max-height:190px!important}'});
  await page.evaluate(()=>{fixture.holdMethod='CamPlan.SetOrder';fixture.hold=true;fixture.orderNodes={canvas:document.querySelector('canvas'),mode:document.querySelector('[data-five-command="five.order.after"]')};});
  await page.locator('[data-five-command="five.order.apply"]').click();await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const nodes=[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane')];nodes.forEach((n,i)=>n.scrollTop=27+i*9);return nodes.map(n=>n.scrollTop);});
  assert.ok(scroll.every(n=>n>0));await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  assert.equal(await page.evaluate(()=>fixture.orderNodes.canvas===document.querySelector('canvas')),true);
  assert.deepEqual(await page.evaluate(()=>[document.querySelector('.cam-context-pane'),document.querySelector('.cam-info-pane')].map(n=>n.scrollTop)),scroll);
  await stress.evaluate(n=>n.remove());
  const manual=await read();assert.deepEqual(setup(manual).definitionOrder,expected);assert.equal(setup(manual).generatedGeometry.status,'Missing');
  assert.equal(generationCalls().length,0);
  await rpc({action:'undo'});await command('five.refresh');assert.deepEqual(setup(await read()).definitionOrder,ids);
  await rpc({action:'redo'});await command('five.refresh');assert.deepEqual(setup(await read()).definitionOrder,expected);
  const reopened=await rpc({action:'save-reopen'});assert.deepEqual(setup(reopened).definitionOrder,expected);assert.equal(setup(reopened).generatedGeometry.status,'Missing');
  // Reopening replaces the native view stream; mount a fresh client as the
  // product does instead of asking its new scene for old cached view resources.
  await page.evaluate(()=>{fixture.orderCamera=fixture.view.viewport.getCameraState();fixture.entry.releaseProject(fixture.context);});
  await page.evaluate(async()=>{const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
    fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);});
  await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.viewport?.sceneObjects.size===1);
  await page.evaluate(()=>fixture.view.viewport.setCameraState(fixture.orderCamera));
  await rpc({action:'export-project',path:resolve(output,'disk-5x-ordered-before-generation.i5cam')});
  // A late automatic-sort response must not steal a text caret on another tab.
  await page.evaluate(()=>{fixture.holdMethod='CamPlan.SetOrder';fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.tool.auto-order');});
  await page.waitForFunction(()=>fixture.held);await page.evaluate(()=>fixture.context.actions.selectRibbonTab('help'));
  const search=page.locator('[data-five-search]');await search.fill('order-focus');await search.evaluate(n=>{n.focus();n.setSelectionRange(1,4);});
  await page.evaluate(()=>fixture.release());await idle();
  assert.deepEqual(await search.evaluate(n=>[document.activeElement===n,n.selectionStart,n.selectionEnd]),[true,1,4]);
  await rpc({action:'undo'});await command('five.refresh');
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));
  assert.deepEqual(setup(await read()).definitionOrder,expected);assert.equal(generationCalls().length,0);
  writeFileSync(resolve(output,'ordering-before-generation.json'),JSON.stringify({passed:true,automaticMs,operations:ids.length,
    cases:['automatic before any generated geometry','outer trim last','manual label and tree clicks','no generation or rapid planning during either sort','saved explicit order','undo redo','save reopen before generation','late response preserves focus and caret','canvas and both sidebar scroll positions preserved'],expected},null,2));
  return expected;
}
