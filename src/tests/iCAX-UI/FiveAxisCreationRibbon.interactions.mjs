import assert from 'node:assert/strict';
import {resolve} from 'node:path';

export async function verifyCreationRibbon(page,output){
 const idle=()=>page.waitForFunction(()=>!__fiveFixture.view.pending);
 const creation=page.locator('.ribbon-command[data-command-id^="five.extraction.begin."]');
 assert.deepEqual(await creation.allTextContents().then(xs=>xs.map(x=>x.trim())),['批量识别','选边','选加工面']);
 assert.equal(await creation.locator('.command-icon.vector svg').count(),3);
 assert.equal(await page.locator('.ribbon-command-menu-toggle[data-command-id="five.extraction.begin"]').count(),0);
 for(const [mode,label] of [['Edges','选边'],['Region','选加工面'],['Faces','批量识别']]){
  await page.locator(`.ribbon-command[data-command-id="five.extraction.begin.${mode}"]`).click();await idle();
  assert.equal(await page.locator('.cam-info-pane [data-five-key="extraction-panel"]').getAttribute('aria-label'),label);
  assert.equal(await page.getByRole('dialog',{name:'添加刀路',exact:true}).count(),0);
  assert.equal(await page.evaluate(async()=>{const {extractionDraft}=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');return extractionDraft(__fiveFixture.view).extraction.mode;}),mode);
  assert.equal(await page.locator(`.ribbon-command[data-command-id="five.extraction.begin.${mode}"]`).evaluate(n=>n.classList.contains('active')),true);
  assert.equal(await page.locator('[data-five-command^="five.extraction.mode."]').count(),0);
  if(mode!=='Faces')continue;
  assert.match(await page.locator('.cam-info-pane [data-five-key="extraction-panel"]').innerText(),/非加工面集合/);
 }
 await page.getByLabel('加工观察方向',{exact:true}).click();
 await page.waitForFunction(()=>__fiveFixture.view.fiveExtractionDirection?.arrow?.name==='cut-extraction-observation-direction');
 const direction=page.getByRole('textbox',{name:'加工观察方向 Z',exact:true});await direction.fill('-2');
 const before=await page.evaluate(()=>{const v=__fiveFixture.view;window.creationNodes={input:document.querySelector('[data-five-extraction-direction="2"]'),canvas:v.viewport.renderer.domElement};creationNodes.input.focus({preventScroll:true});creationNodes.input.setSelectionRange(0,2);return creationNodes.input.value;});
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.display.numbers'));
 assert.equal(await direction.inputValue(),before);assert.deepEqual(await direction.evaluate(e=>({same:e===creationNodes.input,focus:e===document.activeElement,selection:[e.selectionStart,e.selectionEnd],canvas:creationNodes.canvas===__fiveFixture.view.viewport.renderer.domElement})),{same:true,focus:true,selection:[0,2],canvas:true});
 await direction.press('Escape');await page.waitForFunction(()=>!__fiveFixture.view.fiveExtractionDirection);
 await page.getByLabel('加工观察方向',{exact:true}).hover();await page.waitForTimeout(700);assert.match(await page.getByRole('tooltip').innerText(),/加工观察方向/);
 await page.screenshot({path:resolve(output,'creation-batch-direction.png')});
 await page.locator('[data-five-command="five.extraction.close"]').first().click();await idle();
 assert.equal(await page.locator('[data-five-key="extraction-panel"]').count(),0);
 await page.locator('.ribbon-command[data-command-id="five.extraction.begin.Faces"]').click();await idle();
 assert.equal(await page.locator('.cam-info-pane [data-five-key="extraction-panel"]').count(),1);
 await page.locator('[data-five-command="five.extraction.close"]').first().click();await idle();
 return {directModes:['批量识别','选边','选加工面'],dockedCreation:true,observationArrow:true,directionInputRefresh:true,tooltip:true};
}
