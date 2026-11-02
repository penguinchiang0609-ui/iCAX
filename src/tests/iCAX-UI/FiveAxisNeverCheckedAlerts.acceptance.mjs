import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyNeverCheckedAlerts({page,rpc,info,output,modelPath}){
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}}),cases=[];
  await selectWorkpieceFile(page,modelPath||info.sourcePath);await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisImportActive&&fixture.view.viewport?.sceneObjects.size===1);
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.setup.create'));await page.waitForFunction(()=>!fixture.view.pending);
  const initial=await read();assert.equal(initial.intentToolpaths.length,0);assert.equal(initial.setups[0].simulationCheck.validity,'Missing');
  const state=()=>page.evaluate(()=>{const d=fixture.view.scene.domain,s=d.setups.find(s=>s.id===d.activeSetupId);return {report:s.simulationCheck,intents:d.intentToolpaths.length,geometry:s.generatedGeometry?.status,alerts:fixture.entry.getRibbonDefinition(fixture.context).alerts.items.map(a=>({id:a.id,title:a.title}))};});
  assert.deepEqual((await state()).alerts,[]);
  await page.evaluate(()=>fixture.alertMethodStart=fixture.methods.length);
  for(const [group,axis,value] of [['position','X','12.5'],['rotation','Z','30'],['position','Z','100']]){
    const input=page.locator(`[data-five-transform-field="${group}-${axis}"]`);await input.fill(value);await input.press('Enter');await page.waitForFunction(()=>!fixture.view.fivePlacement?.saving&&!fixture.view.fivePlacement?.request);
    const actual=await state(),native=await read();assert.deepEqual(actual.report,native.setups[0].simulationCheck);assert.equal(actual.report.validity,'Missing');assert.equal(actual.intents,0);assert.equal(actual.geometry,'Missing');assert.deepEqual(actual.alerts,[]);assert.deepEqual(native.workpieces,initial.workpieces);
  }
  const methods=await page.evaluate(()=>fixture.methods.slice(fixture.alertMethodStart).map(m=>m.method));assert.equal(methods.filter(m=>m==='CamPlacement.SetTransform').length,3);assert.equal(methods.includes('CamScene.ReadState'),false,'The compact receipt itself must retain Missing; a full native reread must not mask the bug');
  cases.push('Real native import/setup and three numeric position/rotation saves preserve Missing/Unchecked, no toolpaths, unchanged BRep and no expired-check alerts without falling back to a full scene read');
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await page.waitForFunction(()=>fixture.view.fiveAxisArea==='toolpath'&&!fixture.view.pending);
  assert.deepEqual((await state()).alerts,[]);assert.equal((await state()).report.validity,'Missing');await page.screenshot({path:resolve(output,'cam-without-phantom-check-warning.png')});
  cases.push('Entering the CAM page after moving an unchecked workpiece keeps the production ribbon alert list empty and does not invent a previous collision check');
  return {cases,methods,observed:await state(),model:modelPath||info.sourcePath};
}
