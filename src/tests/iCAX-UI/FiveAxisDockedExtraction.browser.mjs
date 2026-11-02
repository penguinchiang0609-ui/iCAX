import assert from 'node:assert/strict';
import {resolve} from 'node:path';

export async function verifyDockedExtraction(page,output){
 const cases=[];
 await page.evaluate(()=>__fiveFixture.populate(70));
 await page.locator('[data-tab-id="toolpath"]').click();
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.cam-page.cutting'));
 const panel=page.locator('.cam-info-pane [data-five-key="extraction-panel"]');
 for(const [mode,title] of [['Edges','选边'],['Region','选加工面'],['Faces','批量识别']]){
  await page.locator(`[data-command-id="five.extraction.begin.${mode}"]`).click();await panel.waitFor();
  assert.equal(await panel.getAttribute('aria-label'),title);
  assert.equal(await page.locator('.five-overlay [data-five-key="extraction-panel"], [data-five-floating="extraction"]').count(),0);
  assert.equal(await page.locator('[data-five-command^="five.extraction.mode."]').count(),0);
  assert.equal(await page.locator(`[data-command-id="five.extraction.begin.${mode}"]`).evaluate(n=>n.classList.contains('active')),true);
 }
 cases.push('Creation modes switch directly from the ribbon; their controls occupy one right inspector without a viewport dialog');
 await page.getByLabel('加工观察方向',{exact:true}).click();
 const input=page.getByRole('textbox',{name:'加工观察方向 Z',exact:true});await input.fill('-2');
 const style=await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{height:300px!important;display:block!important;overflow:auto!important}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;min-height:1000px}.five-path-list{height:130px!important;flex:none!important;overflow:auto!important}.five-path-list::after{content:"";display:block;min-height:1000px}'});
 await input.evaluate(n=>{window.dockedNodes={input:n,canvas:__fiveFixture.view.viewport.renderer.domElement,search:document.querySelector('[data-five-search]'),events:0};n.addEventListener('dock-probe',()=>dockedNodes.events++);window.dockedRefresh=new Promise(resolve=>window.dockedRelease=()=>{n.dispatchEvent(new CustomEvent('five-snap-state',{bubbles:true}));resolve();});});
 const scrolls=await page.evaluate(()=>{const n=dockedNodes.input;n.focus({preventScroll:true});n.setSelectionRange(0,2);return ['.cam-context-pane','.cam-info-pane','.five-path-list'].map((s,i)=>{const node=document.querySelector(s);node.scrollTop=70+i*25;return node.scrollTop;});});
 assert.ok(scrolls.every(n=>n>0));await page.evaluate(()=>dockedRelease());await page.evaluate(()=>dockedRefresh);
 const retained=await page.evaluate(()=>{const n=dockedNodes;n.input.dispatchEvent(new Event('dock-probe'));return {input:n.input===document.querySelector('[data-five-extraction-direction="2"]'),canvas:n.canvas===__fiveFixture.view.viewport.renderer.domElement,search:n.search===document.querySelector('[data-five-search]'),focused:document.activeElement===n.input,selection:[n.input.selectionStart,n.input.selectionEnd],scrolls:['.cam-context-pane','.cam-info-pane','.five-path-list'].map(s=>document.querySelector(s).scrollTop),events:n.events};});
 assert.deepEqual(retained,{input:true,canvas:true,search:true,focused:true,selection:[0,2],scrolls,events:1});
 await style.evaluate(n=>n.remove());await input.press('Escape');
 cases.push('Delayed refresh retains canvas/input/search nodes, listeners, the latest selection and all three scroll regions');
 const original=page.viewportSize();
 for(const width of [1280,1920]){await page.setViewportSize({width,height:900});const box=await panel.boundingBox(),pane=await page.locator('.cam-info-pane').boundingBox();assert.ok(box.x>=pane.x&&box.x+box.width<=pane.x+pane.width+1);}
 if(original)await page.setViewportSize(original);
 await page.screenshot({path:resolve(output,'docked-toolpath-creation.png')});
 await page.locator('[data-five-command="five.extraction.close"]').last().click();assert.equal(await panel.count(),0);
 await page.locator('[data-command-id="five.extraction.begin.Faces"]').click();await panel.waitFor();
 assert.equal(await page.getByRole('dialog',{name:'添加刀路',exact:true}).count(),0);
 await page.locator('[data-five-command="five.extraction.close"]').last().click();
 cases.push('Creation stays docked after resize and reopen; closing restores the ordinary inspector');
 return cases;
}
