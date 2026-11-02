import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { gizmoDriver } from './FiveAxisPlacementGizmo.native.browser.mjs';

export async function verifyScaleGizmo({page,command,read,refresh,rpc,base,output}) {
  const {drag,pick,numeric}=gizmoDriver(page),cases=[];
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fivePlacement?.request);
  const bounds=()=>page.evaluate(()=>{
    const object=fixture.view.viewport.sceneObjects.get(fixture.view.scene.domain.placements[0].id);
    object.geometry.computeBoundingBox();const b=object.geometry.boundingBox;
    return {size:b.getSize(object.position.clone()).toArray(),center:b.getCenter(object.position.clone()).toArray()};
  });
  const near=(a,b)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-4,`${a} != ${b}`));
  const initial=await read(),original=await bounds();
  const undo=async()=>{await command('five.tool.select');await rpc({action:'undo'});await refresh();};
  for(const projection of ['perspective','orthographic']) {
    await page.locator(`button[data-projection-mode="${projection}"]`).click();
    for(const axis of ['X','Y','Z','XYZ']) {
      await command('five.tool.scale');await pick(axis);
      assert.equal(await numeric().inputValue(),'1');
      assert.equal(await numeric().getAttribute('aria-label'),'缩放比例');
      assert.equal(await page.locator('.five-placement-field>span').innerText(),'XYZ');
      assert.equal(await page.evaluate(()=>fixture.view.viewport.transformManipulator.control.showXY||fixture.view.viewport.transformManipulator.control.showXZ||fixture.view.viewport.transformManipulator.control.showYZ),false);
      const before=await read();await numeric().fill('1.5');
      assert.deepEqual(await read(),before,'typing scale is only a preview');
      await page.evaluate(()=>{window.scaleCanvas=document.querySelector('.icax-three-viewport canvas');window.scaleInput=document.querySelector('[data-five-placement-field]');});
      await numeric().press('Enter');await idle();
      const after=await read(),actual=await bounds();
      assert.notDeepEqual(after.workpieces[0].geometryRef,before.workpieces[0].geometryRef);
      assert.deepEqual(after.workpieces[0].sourceRef,before.workpieces[0].sourceRef);
      near(after.placements[0].transform,base);
      near(actual.size,original.size.map(v=>v*1.5));
      near(actual.center,original.center);
      assert.equal(await page.evaluate(()=>scaleCanvas===document.querySelector('.icax-three-viewport canvas')&&scaleInput===document.querySelector('[data-five-placement-field]')),true);
      await numeric().press('Enter');await idle();assert.deepEqual(await read(),after,'repeated Enter is a no-op');
      // Replacing the previous gesture's factor uses the current geometry once.
      await numeric().fill('2');await numeric().press('Enter');await idle();
      near((await bounds()).size,original.size.map(v=>v*2));
      if(axis==='XYZ'&&projection==='perspective')await page.screenshot({path:resolve(output,'scale-uniform.png')});
      for(const invalid of ['0','-1','abc']) {
        const saved=await read();await numeric().fill(invalid);await numeric().press('Enter');await page.waitForTimeout(30);
        assert.deepEqual(await read(),saved);assert.ok(await page.locator('.five-placement-error').count());
      }
      await undo();await undo();near((await bounds()).size,original.size);
    }
    cases.push(`${projection}: every axis box and center scale XYZ equally; plane handles hidden; exact dimensions, fixed center, rigid placement, replacement input, undo and input/canvas identity`);
  }
  for(const axis of ['X','Y','Z','XYZ']) {
    await command('five.tool.scale');const before=await read();
    await drag(axis,24,false);assert.deepEqual(await read(),before,'held scale drag is preview only');
    assert.notEqual(Number(await numeric().inputValue()),1);
    const factor=Number(await numeric().inputValue());await page.mouse.up();await idle();
    near((await bounds()).size,original.size.map(v=>v*factor));
    await undo();near((await bounds()).size,original.size);
    await command('five.tool.scale');const cancelBefore=await read();
    await drag(axis,24,false);await page.keyboard.press('Escape');await page.mouse.up();
    assert.deepEqual(await read(),cancelBefore);near((await bounds()).size,original.size);
  }
  cases.push('real mouse scale drag previews then commits on release; Escape restores the exact original geometry');
  await command('five.tool.scale');await pick('XYZ');await numeric().fill('1.25');
  await page.evaluate(()=>{fixture.holdMethod='CamScene.ReadState';fixture.hold=true;});
  await numeric().press('Enter');await page.waitForFunction(()=>fixture.held);
  await page.evaluate(()=>{const input=document.querySelector('[data-five-placement-field]');input.focus({preventScroll:true});input.setSelectionRange(1,3);fixture.release();});
  await idle();
  assert.deepEqual(await page.evaluate(()=>{const input=document.querySelector('[data-five-placement-field]');return {focus:document.activeElement===input,selection:[input.selectionStart,input.selectionEnd]};}),{focus:true,selection:[1,3]});
  near((await bounds()).size,original.size.map(v=>v*1.25));await undo();
  cases.push('delayed scale resource refresh preserves the latest input focus and selection');
  await command('five.tool.scale');await pick('X');await numeric().fill('1.2');
  await page.evaluate(()=>{fixture.dropApply=true;fixture.dropQuery=true;});await numeric().press('Enter');
  await page.waitForFunction(()=>!fixture.view.pending);
  assert.equal(await numeric().getAttribute('readonly'),'');
  assert.ok(await page.evaluate(()=>fixture.view.fivePlacement.request));
  await numeric().press('Enter');await idle();
  near((await bounds()).size,original.size.map(v=>v*1.2));await undo();
  cases.push('lost scale/task receipts retry the same command and never scale the geometry twice');
  await page.locator('button[data-projection-mode="perspective"]').click();
  assert.deepEqual((await read()).workpieces,initial.workpieces);
  await command('five.tool.select');return cases;
}
