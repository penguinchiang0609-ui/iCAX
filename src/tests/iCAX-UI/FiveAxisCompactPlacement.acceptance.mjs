import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyCompactPlacement({page,rpc,info,output}){
  await selectWorkpieceFile(page,info.sourcePath);await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisImportActive&&fixture.view.viewport?.sceneObjects.size===1);
  await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.setup.create'));await page.waitForFunction(()=>!fixture.view.pending);
  const field=(group,axis)=>page.locator(`[data-five-transform-field="${group}-${axis}"]`),sizes=[];
  for(const width of [320,260]){
    await page.evaluate(width=>document.querySelector('.five-workspace').style.setProperty('--cam-right-width',width+'px'),width);
    for(const group of ['position','rotation']){
      const boxes=await page.locator(`[data-five-transform-field^="${group}-"]`).evaluateAll(nodes=>nodes.map(n=>{const r=n.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,right:r.right};}));
      assert.equal(boxes.length,3);assert.ok(boxes.every(b=>Math.abs(b.y-boxes[0].y)<1&&b.width>20&&b.width<110));assert.ok(boxes[0].x<boxes[1].x&&boxes[1].x<boxes[2].x);sizes.push({width,group,boxes});
    }
  }
  await page.evaluate(()=>document.querySelector('.five-workspace').style.removeProperty('--cam-right-width'));await page.screenshot({path:resolve(output,'compact-setup-position-rotation.png')});
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}}),before=await read();
  await field('position','X').fill('12.5');await field('position','X').press('Enter');await page.waitForFunction(()=>!fixture.view.fivePlacement?.saving&&!fixture.view.fivePlacement?.request);assert.equal((await read()).placements.find(p=>p.setupId).transform[3],12.5);
  await rpc({action:'undo'});await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));assert.deepEqual((await read()).placements,before.placements);
  return {cases:['Setup position XYZ and rotation XYZ each occupy one compact row at 320px and 260px sidebar widths; real numeric placement and native undo still work'],sizes};
}
