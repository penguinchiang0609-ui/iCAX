import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {writeFileSync} from 'node:fs';

export async function verifyLayerCollapse({page,rpc,output,errors}) {
 const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
 const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveLayers?.busy);
 const refresh=async()=>{await command('five.refresh');await idle();};
 const toggle=()=>command('five.layer.toggle');
 const cases=[];
 await rpc({action:'prepare-axis-test'});await refresh();
 await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await idle();
 await page.waitForFunction(()=>fixture.view.viewport?.getDebugState()?.objectCount>0);
 const initial=await rpc({action:'read-state'});
 const cut=initial.intentToolpaths[0];assert.ok(cut);
 await page.locator(`[data-five-intent="${cut.id}"]`).click();await idle();
 await page.evaluate(()=>{
  const view=fixture.view;
  fixture.collapseNodes={canvas:view.viewport.renderer.domElement,strip:document.querySelector('.five-layer-strip'),swatches:document.querySelector('.five-layer-swatches'),toggle:document.querySelector('[data-five-command="five.layer.toggle"]')};
 });
 await page.getByRole('button',{name:'折叠图层',exact:true}).click();
 assert.equal(await page.locator('.five-layer-swatches').isVisible(),false);
 assert.equal(await page.getByRole('button',{name:'新增图层',exact:true}).isVisible(),false);
 assert.equal(await page.getByRole('button',{name:'展开图层',exact:true}).getAttribute('aria-expanded'),'false');
 assert.equal(await page.locator('.five-layer-strip strong').textContent(),'图层');
 const box=await page.locator('.five-layer-strip').boundingBox();assert.ok(box.width<=80&&box.height<50);
 await refresh();assert.equal(await page.locator('.five-layer-swatches').isVisible(),false);
 await page.screenshot({path:resolve(output,'图层折叠.png')});
 await page.getByRole('button',{name:'展开图层',exact:true}).focus();await page.keyboard.press('Enter');
 assert.equal(await page.locator('.five-layer-swatches').isVisible(),true);
 await page.getByRole('button',{name:'折叠图层',exact:true}).focus();await page.keyboard.press('Space');
 assert.equal(await page.locator('.five-layer-swatches').isVisible(),false);
 await toggle();
 assert.equal(await page.getByRole('button',{name:'折叠图层',exact:true}).getAttribute('aria-expanded'),'true');
 const after=await rpc({action:'read-state'});
 assert.deepEqual(after.layers,initial.layers);assert.deepEqual(after.intentToolpaths,initial.intentToolpaths);assert.deepEqual(after.expected,initial.expected);
 assert.ok(await page.evaluate(()=>{const n=fixture.collapseNodes;return n.canvas===fixture.view.viewport.renderer.domElement&&n.strip===document.querySelector('.five-layer-strip')&&n.swatches===document.querySelector('.five-layer-swatches')&&n.toggle===document.querySelector('[data-five-command="five.layer.toggle"]');}));
 await page.screenshot({path:resolve(output,'图层展开.png')});
 cases.push('实际带孔板场景：鼠标、Enter、Space 折叠展开；右侧仅图层标签及箭头；刷新保留折叠状态，原生图层、刀路、工程版本不变');
 // Add enough real layers to exercise scroll restoration with production controls.
 for(let i=0;i<4;i++){await page.locator('[data-five-command="five.layer.add"]').first().click();await idle();}
 await page.evaluate(()=>{
  const style=document.createElement('style');style.id='collapse-scroll-test';style.textContent='.cam-context-pane{height:150px!important;overflow:auto!important}.cam-info-pane{height:240px!important;overflow:auto!important}.five-path-list{max-height:70px!important;overflow:auto!important}.five-layer-edit-list{max-height:105px!important}.five-layer-swatches{max-height:110px!important}';document.head.append(style);
  const input=document.querySelector('[data-five-cut-command="beforeCommands"]');
  input.focus({preventScroll:true});input.value='G04 P12345';input.dispatchEvent(new Event('input',{bubbles:true}));input.setSelectionRange(2,6);
  const n=fixture.collapseInteraction={input,canvas:fixture.view.viewport.renderer.domElement,left:document.querySelector('.cam-context-pane'),right:document.querySelector('.cam-info-pane'),nested:document.querySelector('.five-path-list'),swatches:document.querySelector('.five-layer-swatches'),list:document.querySelector('.five-layer-edit-list'),events:0};
  input.addEventListener('input',()=>n.events++);
  n.left.scrollTop=31;n.right.scrollTop=107;n.nested.scrollTop=19;n.swatches.scrollTop=57;n.list.scrollTop=131;
  fixture.collapseSnapshot=()=>({scroll:[n.left,n.right,n.nested,n.swatches,n.list].map(el=>el.scrollTop),focus:document.activeElement===n.input,selection:[n.input.selectionStart,n.input.selectionEnd],events:n.events,value:n.input.value});
 });
 const interaction=await page.evaluate(()=>fixture.collapseSnapshot());
 await toggle();assert.equal(await page.locator('.five-layer-manager').isVisible(),false);await toggle();
 assert.deepEqual(await page.evaluate(()=>fixture.collapseSnapshot()),interaction);
 cases.push('折叠展开保留双侧栏、嵌套列表、图层色条和管理列表滚动，保留输入焦点、文本选区及监听器');
 // Hold a real native response; scroll and toggle after the request was sent.
 await page.evaluate(()=>{fixture.holdMethod='CamCut.ManageLayers';fixture.hold=true;});
 await page.locator('[data-five-layer-field="color"]').first().evaluate(input=>{input.value='#aabbcc';input.dispatchEvent(new Event('input',{bubbles:true}));input.dispatchEvent(new Event('change',{bubbles:true}));});
 await page.waitForFunction(()=>fixture.held);
 const latest=await page.evaluate(()=>{const n=fixture.collapseInteraction;n.left.scrollTop=42;n.right.scrollTop=121;n.nested.scrollTop=25;n.swatches.scrollTop=63;n.list.scrollTop=143;n.input.focus({preventScroll:true});n.input.setSelectionRange(3,7);return fixture.collapseSnapshot();});
 await toggle();await page.evaluate(()=>fixture.release());await idle();
 assert.equal(await page.locator('.five-layer-swatches').isVisible(),false);await toggle();
 assert.deepEqual(await page.evaluate(()=>fixture.collapseSnapshot()),latest);
 const layerId=initial.layers.visibleLayerId;
 await command(`five.layer.delete:${layerId}`);
 assert.equal(await page.locator('[data-five-layer-target]').count(),1);
 assert.deepEqual(await page.evaluate(()=>fixture.collapseSnapshot()),latest);
 const retained=await page.evaluate(()=>{const n=fixture.collapseInteraction;n.input.dispatchEvent(new Event('input',{bubbles:true}));return {input:n.input===document.querySelector('[data-five-cut-command="beforeCommands"]'),canvas:n.canvas===fixture.view.viewport.renderer.domElement,events:n.events};});
 assert.deepEqual(retained,{input:true,canvas:true,events:1});
 await page.locator('[data-five-layer-target]').focus();await idle();await command('five.layer.delete-cancel');
 assert.equal(await page.locator('[data-five-layer-target]').count(),0);
 assert.equal(await page.evaluate(()=>document.activeElement.hasAttribute('data-five-layer-field')),false);
 cases.push('原生异步响应前继续滚动并折叠，响应后展开保留最新交互及输入和画布节点；条件字段变化不误抢焦点');
 assert.deepEqual(errors,[]);
 writeFileSync(resolve(output,'collapse-report.json'),JSON.stringify({passed:true,cases,interaction,latest,coverage:{browser:'Edge',nativeRepository:true,productionViewport:true,standaloneCef:false}},null,2));
 console.log('PASS native layer collapse and interaction preservation');
}
