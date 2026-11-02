import assert from 'node:assert/strict';
import {resolve} from 'node:path';

export async function verifySetupNaming({page,read,output}){
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveWorkpieceSwitching);
  const click=async command=>{await page.locator(`[data-five-key="setup-selector"] [data-five-command="${command}"]`).click();await idle();};
  await page.locator('[data-tab-id="fixture"]').click();await idle();
  const before=await read(),first=before.placements[0],originalName=before.setups.find(s=>s.id===first.setupId).name;
  await click('five.setup.edit-name');
  const input=page.locator('[data-five-setup-name]');
  assert.equal(await input.evaluate(e=>document.activeElement===e&&e.selectionStart===0&&e.selectionEnd===e.value.length),true);
  await input.fill('放弃的名称');await input.press('Escape');await idle();
  assert.equal((await read()).setups.find(s=>s.id===first.setupId).name,originalName);
  await click('five.setup.edit-name');await input.fill('  ');await input.press('Enter');await idle();
  assert.equal((await read()).setups.find(s=>s.id===first.setupId).name,originalName);
  await input.fill('底面装夹');
  await page.addStyleTag({content:'.setup-name-scroll-test .cam-context-pane::after,.setup-name-scroll-test .cam-info-pane::after{content:"";display:block;height:1600px}'});
  const positions=await page.evaluate(()=>{
    document.body.classList.add('setup-name-scroll-test');
    const input=document.querySelector('[data-five-setup-name]');
    fixture.nameInput=input;fixture.nameCanvas=fixture.view.viewport.renderer.domElement;
    input.focus({preventScroll:true});input.setSelectionRange(1,3);
    setTimeout(()=>fixture.nameInput.dispatchEvent(new CustomEvent('five-snap-state',{bubbles:true})),100);
    return ['.cam-context-pane','.cam-info-pane'].map((s,i)=>{const e=document.querySelector(s);e.scrollTop=70+i*20;return e.scrollTop;});
  });
  await page.waitForTimeout(180);
  assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane'].map(s=>document.querySelector(s).scrollTop)),positions);
  assert.equal(await input.evaluate(e=>e===fixture.nameInput&&document.activeElement===e&&e.selectionStart===1&&e.selectionEnd===3&&fixture.nameCanvas===fixture.view.viewport.renderer.domElement),true);
  await page.evaluate(()=>document.body.classList.remove('setup-name-scroll-test'));
  await input.press('Enter');await idle();
  assert.equal((await read()).setups.find(s=>s.id===first.setupId).name,'底面装夹');
  assert.equal(await input.count(),0);
  await click('five.setup.create');const second=(await read()).placements.find(p=>p.id!==first.id);
  await click('five.setup.edit-name');await input.fill('侧孔装夹');await click('five.setup.rename');
  await click(`five.setup.activate:${first.id}`);
  assert.equal((await read()).setups.find(s=>s.id===first.setupId).name,'底面装夹');
  assert.equal((await read()).setups.find(s=>s.id===second.setupId).name,'侧孔装夹');
  assert.deepEqual((await read()).workpieces,before.workpieces);
  assert.deepEqual((await read()).placements.find(p=>p.id===first.id).transform,first.transform);
  await page.screenshot({path:resolve(output,'setup-names.png')});
  return ['Inline setup naming supports select-all, Escape cancel, blank rejection, Enter/button save, independent setup names and unchanged geometry/placement',
    'Asynchronous UI refresh preserves the name input/canvas nodes, focus, selection and both sidebars scroll positions'];
}
