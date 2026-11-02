import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyFixtureDirectionAndSolid({page,rpc,info,output,modelPath,calculate}){
  const cases=[],read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}}),command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest,undefined,{timeout:120000});
  calculate??=async action=>{await page.locator(`[data-five-command="five.fixture.design.${action}"]`).click();await idle();};
  await selectWorkpieceFile(page,modelPath||info.sourcePath);await idle();await page.waitForFunction(()=>fixture.view.viewport?.sceneObjects.size===1);
  let state=await read();const placement=state.placements[0];
  const frame={origin:[44.30594070598242,-8.166350465124992,70],x:[1,0,0],y:[0,1,0],z:[0,0,1]};
  await rpc({action:'invoke',scope:'scene',method:'CamPlacement.ApplyRegistration',payload:{commandId:crypto.randomUUID(),expected:state.expected,setupId:'',placementId:placement.id,resetWorkpieceFrame:true,registration:{method:'Alignment',modelFrame:'Workpiece',targetFrame:'Setup',unit:'mm',frame}}});
  await command('five.refresh');await command('five.setup.create');await idle();state=await read();await rpc({action:'invoke',scope:'scene',method:'CamPlacement.SetTransform',payload:{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,placementId:placement.id,transform:[1,0,0,0,0,1,0,0,0,0,1,77.351483,0,0,0,1]}});
  await command('five.refresh');const before=await read();await command('five.fixture.generate');await idle();
  const menu=page.locator('.five-support-direction-menu'),field=name=>page.locator(`[data-five-fixture-field="design:${name}"]`),arrow=()=>page.evaluate(()=>!!fixture.view.viewport.scene.getObjectByName('fixture-support-direction'));
  assert.equal(await arrow(),false);const calls=await page.evaluate(()=>fixture.methods.length);
  await menu.locator('summary').click();await page.waitForFunction(()=>!!fixture.view.fixtureSupportArrow);assert.equal(await arrow(),true);
  await field('supportX').fill('1');await field('supportZ').fill('1');
  const direction=await page.evaluate(()=>fixture.view.viewport.camera.position.clone().set(0,1,0).applyQuaternion(fixture.view.fixtureSupportArrow.group.quaternion).toArray());
  assert.ok(Math.abs(direction[0]-Math.SQRT1_2)<1e-8&&Math.abs(direction[2]-Math.SQRT1_2)<1e-8);
  await page.screenshot({path:resolve(output,'direction-settings-open.png')});
  await page.locator('.five-design-header h2').click();await page.waitForFunction(()=>!fixture.view.fixtureSupportArrow);assert.equal(await arrow(),false);
  await menu.locator('summary').click();await field('supportX').fill('0');await field('supportZ').fill('1');await field('supportZ').press('Escape');await page.waitForFunction(()=>!fixture.view.fixtureSupportArrow);assert.equal(await arrow(),false);
  assert.equal(await page.evaluate(start=>fixture.methods.slice(start).length,calls),0);
  cases.push('The scene direction arrow is absent by default, appears when Advanced configuration opens, follows edited vector values, and disappears on outside focus or Escape without native calls');
  await calculate('auto');assert.equal(await page.evaluate(()=>fixture.view.error),'');assert.equal(await arrow(),false);
  const selected=await page.evaluate(()=>[...fixture.view.fixtureDesignDraft.supportFaces]);assert.equal(selected.length,46);
  await field('pattern').selectOption('Parallel');await field('spacing').fill('100');
  await page.screenshot({path:resolve(output,'direction-settings-closed.png')});
  await calculate('generate');assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const candidate=await page.evaluate(()=>fixture.view.fiveFixtureGenerated?.design.candidate);assert.ok(candidate?.parts.length>1);assert.equal(candidate.supportCheck.status,'Supported');
  assert.deepEqual((await read()).workpieces,before.workpieces);assert.equal((await read()).fixtures.length,0);await page.screenshot({path:resolve(output,'support-generated.png')});
  cases.push('The actual translated HatRail, 46 batch-selected faces, 100 mm parallel spacing, solid plates and R1 tenons generate a supported candidate without modifying the workpiece or adopting fixtures');
  return {cases,selected,partCount:candidate.parts.length,model:modelPath||info.sourcePath};
}
