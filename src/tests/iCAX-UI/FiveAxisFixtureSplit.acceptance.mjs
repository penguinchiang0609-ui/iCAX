import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {selectWorkpieceFile,selectModelFile} from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyFixtureSplit({page,rpc,info,output}){
  const cases=[],read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}}),command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest);
  await page.evaluate(async()=>{
    const {renderRibbonCommandIcon}=await import('/src/iCAX-UI/SDK/AppShell/app/ribbonIcons.mjs');
    // The Scene bridge has no Product history UI endpoint. Keep that read-only
    // availability disabled; all library mutations still use native Scene SDOs.
    const productInvoke=fixture.context.productProxy.invoke.bind(fixture.context.productProxy);
    fixture.context.productProxy.invoke=(method,payload)=>method==='Product.GetUserDataHistory'?Promise.resolve({canUndo:false,canRedo:false}):productInvoke(method,payload);
    const host=document.createElement('nav');host.id='test-fixture-ribbon';host.className='ribbon';host.style.cssText='height:110px;background:#fff;display:flex;gap:16px;padding:10px;box-sizing:border-box';document.body.prepend(host);
    fixture.context.mount.style.height='calc(100vh - 110px)';
    fixture.context.actions.refreshProductRibbon=()=>{
      const group=fixture.entry.getRibbonDefinition(fixture.context).tabs.find(t=>t.id==='workpiece').groups.find(g=>g.title==='夹具');
      host.replaceChildren(...group.commands.map(c=>{const b=document.createElement('button');b.type='button';b.dataset.commandId=c.id;b.disabled=!!c.disabled;b.title=c.title;b.style.cssText='display:grid;justify-items:center;align-content:center;gap:5px;min-width:76px;background:transparent;border:0;color:#29434e';b.innerHTML=renderRibbonCommandIcon(c.iconName)+`<span>${c.title}</span>`;return b;}));
    };
    host.addEventListener('click',event=>{const button=event.target.closest('button');if(button&&!button.disabled)void fixture.entry.handleRibbonCommand(fixture.context,button.dataset.commandId);});
    fixture.context.actions.refreshProductRibbon();fixture.splitCanvas=fixture.view.viewport.renderer.domElement;
  });
  const ribbon=page.locator('#test-fixture-ribbon'),support=ribbon.getByRole('button',{name:'新增支撑',exact:true}),add=ribbon.getByRole('button',{name:'新增夹具',exact:true});
  assert.equal(await support.count(),1);assert.equal(await add.count(),1);assert.equal(await support.isDisabled(),true);assert.equal(await add.isDisabled(),true);
  await selectWorkpieceFile(page,info.sourcePath);await idle();assert.equal(await support.isDisabled(),true);assert.equal(await add.isDisabled(),true);
  await command('five.setup.create');await idle();assert.equal(await support.isEnabled(),true);assert.equal(await add.isEnabled(),true);
  const before=await read();
  await support.click();await page.locator('.five-fixture-generator').waitFor();await idle();
  assert.equal(await page.locator('.five-design-header h2').innerText(),'新增支撑');assert.equal(await page.locator('.five-fixture-chooser,[data-five-key="fixture-add"]').count(),0);
  await page.locator('[data-five-fixture-field="design:spacing"]').fill('77');
  await page.screenshot({path:resolve(output,'new-support-direct.png')});
  await page.locator('[data-five-command="five.fixture.design.cancel"]').last().click();await idle();assert.deepEqual(await read(),before);
  cases.push('Two separate ribbon commands are disabled without a setup; New support opens the actual support editor directly, and Cancel discards its parameter edits');

  await add.click();await page.getByRole('dialog',{name:'新增夹具',exact:true}).waitFor();await idle();
  assert.equal(await page.locator('.five-fixture-generator,.five-fixture-chooser').count(),0);
  await page.screenshot({path:resolve(output,'new-fixture-direct.png')});
  await page.keyboard.press('Escape');await idle();assert.equal(await page.getByRole('dialog',{name:'新增夹具',exact:true}).count(),0);assert.deepEqual(await read(),before);
  cases.push('New fixture opens the fixture selector directly without the old two-choice popup; Escape closes it without changing the scene');

  await add.click();await page.getByRole('dialog',{name:'新增夹具',exact:true}).waitFor();await idle();
  await page.locator('[data-five-command="five.fixture.library.import"]').click();
  await page.locator('[data-nesting-file-directory]').waitFor();assert.equal(await page.locator('.five-blocking-progress').count(),0);
  await selectModelFile(page,info.sourcePath);await idle();
  const imported=await read();assert.equal(imported.fixtureUnits.length,before.fixtureUnits.length+1);assert.ok(imported.fixtures.length>before.fixtures.length);
  assert.deepEqual(imported.workpieces,before.workpieces);assert.deepEqual(imported.placements,before.placements);
  assert.equal(await page.getByRole('dialog',{name:'新增夹具',exact:true}).count(),0);
  await page.screenshot({path:resolve(output,'fixture-imported.png')});
  await rpc({action:'undo'});await command('five.refresh');await idle();assert.deepEqual((await read()).fixtureUnits,before.fixtureUnits);
  await rpc({action:'redo'});await command('five.refresh');await idle();assert.deepEqual((await read()).fixtureUnits,imported.fixtureUnits);
  cases.push('The real file picker stays usable; importing through New fixture uses native LibraryImport and LibraryUse to add one fixture unit, preserves the workpiece, and supports native undo/redo');

  await support.click();await page.locator('.five-fixture-generator').waitFor();await idle();
  await page.locator('[data-five-command="five.fixture.design.cancel"]').last().click();await idle();assert.deepEqual((await read()).fixtureUnits,imported.fixtureUnits);
  await ribbon.getByRole('button',{name:'工装库',exact:true}).click();await page.getByRole('dialog',{name:'工装库',exact:true}).waitFor();await idle();
  await page.locator('[data-five-command="five.fixture.library.close"]').click();await idle();
  assert.equal(await page.evaluate(()=>fixture.splitCanvas===fixture.view.viewport.renderer.domElement),true);
  assert.equal(await page.locator('.five-fixture-chooser,[data-five-key="fixture-add"]').count(),0);
  cases.push('Adding a new support preserves existing imported fixtures; library management remains available and all navigation retains the viewport canvas');
  return {cases,ribbonHarness:'production ribbon definition and icons with real production entry dispatch; isolated browser shell',productHistoryCovered:false};
}
