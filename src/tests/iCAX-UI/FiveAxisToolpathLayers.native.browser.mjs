import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {writeFileSync} from 'node:fs';

export async function verifyToolpathLayers({page,rpc,output,errors}){
 page.setDefaultTimeout(90000);const report={passed:false,cases:[],coverage:{nativeRepository:true,productionSdo:true,productionViewport:true,standaloneCef:false,geometry:'原生导入、识别和生成的带孔板件'}};
 const read=()=>rpc({action:'read-state'}),command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
 const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveLayers?.busy);
 const refresh=async()=>{await command('five.refresh');await idle();};
 const snapshot=()=>page.evaluate(()=>({layers:fixture.view.scene.domain.layers,cuts:fixture.view.scene.intentToolpaths}));
 const layerList=s=>Object.values(s.layers.catalog).filter(l=>l.kind==='Visible').sort((a,b)=>Number(a.order)-Number(b.order));
 const choose=id=>page.locator(`[data-five-command="five.layer.choose:${id}"]`).click();
 await rpc({action:'prepare-axis-test'});await refresh();await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await idle();
 await page.waitForFunction(()=>fixture.view.viewport?.getDebugState()?.objectCount>0);
 let initial=await snapshot();assert.equal(layerList(initial).length,8);assert.ok(initial.cuts.length>=2);assert.equal(await page.locator('.five-layer-swatch').count(),8);
 const palette=layerList(initial),first=initial.cuts[0],second=initial.cuts[1];
 await page.locator(`[data-five-intent="${first.id}"]`).click();await idle();
 await page.waitForFunction(()=>fixture.view.fiveCompensation?.loading!==true);
 await page.evaluate(id=>{
  const v=fixture.view;fixture.layerNodes={canvas:v.viewport.renderer.domElement,left:document.querySelector('.cam-context-pane'),right:document.querySelector('.cam-info-pane')};
  v.viewport.scene.traverse(o=>{if(o.userData.cuttingLineId===id||o.userData.generatedIntentId===id||o.userData.compensatedIntentId===id)fixture.layerNodes.line??=o;});
  fixture.layerNodes.geometry=fixture.layerNodes.line?.geometry;
 },first.id);
 await choose(palette[2].id);await page.waitForFunction(({id,layer})=>fixture.view.scene.intentToolpaths.find(c=>c.id===id)?.layers.visibleLayerId===layer,{id:first.id,layer:palette[2].id});await idle();
 const current=await read(),changed=current.intentToolpaths.find(c=>c.id===first.id);assert.equal(changed.layers.visibleLayerId,palette[2].id);assert.deepEqual(changed.curveRef,first.curveRef);assert.deepEqual(changed.processRef,first.processRef);assert.equal(current.setups.find(s=>s.id===current.activeSetupId).generatedGeometry.status,'Current');
 assert.deepEqual(await page.evaluate(async()=>{const {workspaceState}=await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs');return workspaceState(fixture.view).selectedIntentIds;}),[first.id]);
 // Selection has its own highlight; inspect the layer material after deselection.
 await page.locator(`[data-five-check="c:${first.id}"]`).click();await idle();
 const material=await page.evaluate(id=>{const found=[];fixture.view.viewport.scene.traverse(o=>{if([o.userData.cuttingLineId,o.userData.generatedIntentId,o.userData.compensatedIntentId].includes(id)&&o.material?.color)found.push(o.material.color.getHexString());});return found;},first.id);assert.ok(material.length);assert.ok(material.every(c=>c===palette[2].color.slice(1).toLowerCase()));
 await page.locator(`[data-five-intent="${first.id}"]`).click();await idle();
 assert.ok(await page.evaluate(()=>fixture.layerNodes.canvas===fixture.view.viewport.renderer.domElement&&fixture.layerNodes.line?.geometry===fixture.layerNodes.geometry));await page.screenshot({path:resolve(output,'默认8图层.png')});await rpc({action:'export-project',path:resolve(output,'默认8图层.i5cam')});report.cases.push('选中刀路点色块，原生归属和实际线条材质即时改变，几何/工艺/生成状态不变');
 await page.locator(`[data-five-intent="${second.id}"]`).click({modifiers:['Control']});await choose(palette[4].id);await idle();
 for(const id of [first.id,second.id])assert.equal((await read()).intentToolpaths.find(c=>c.id===id).layers.visibleLayerId,palette[4].id);report.cases.push('多选刀路一次批量换层');
 await page.locator('[data-five-command="five.layer.add"]').click();await page.waitForFunction(()=>Object.values(fixture.view.scene.domain.layers.catalog).filter(l=>l.kind==='Visible').length===9);await idle();
 let added=layerList(await snapshot()).find(l=>!palette.some(p=>p.id===l.id));assert.ok(added);const addedId=added.id;
 const name=page.locator(`[data-five-layer-id="${addedId}"][data-five-layer-field="name"]`);await name.fill('自定义轮廓');await name.press('Tab');await idle();
 const color=page.locator(`[data-five-layer-id="${addedId}"][data-five-layer-field="color"]`);await color.evaluate(input=>{input.value='#123abc';input.dispatchEvent(new Event('input',{bubbles:true}));input.dispatchEvent(new Event('change',{bubbles:true}));});await page.waitForFunction(id=>fixture.view.scene.domain.layers.catalog[id]?.color.toLowerCase()==='#123abc',addedId);await idle();
 await choose(addedId);await idle();assert.equal((await read()).layers.catalog[addedId].name,'自定义轮廓');report.cases.push('新增第9层、重命名、自定义配色并应用到实际刀路');
 // Change visibility through the real management controls. Hidden paths remain
 // in the native machining list and lose only their viewport presentation.
 const count=(await read()).intentToolpaths.length;await page.locator(`[data-five-command="five.layer.visible:${addedId}"]`).click();await idle();assert.equal((await read()).intentToolpaths.length,count);assert.ok((await read()).intentToolpaths.find(c=>c.id===first.id).enabled);
 assert.equal(await page.evaluate(id=>{let visible=false;fixture.view.viewport.scene.traverse(o=>{if([o.userData.cuttingLineId,o.userData.generatedIntentId,o.userData.compensatedIntentId].includes(id)&&o.visible)visible=true;});return visible;},first.id),false);
 await page.locator(`[data-five-command="five.layer.visible:${addedId}"]`).click();await idle();report.cases.push('显示层隐藏不改变实际加工参与');
 // Keep the layer manager open while more layers make both nested layer lists
 // scrollable. The text input is an actual production command field.
 for(let i=0;i<7;i++){await page.locator('[data-five-command="five.layer.add"]').first().click();await idle();}
 await page.locator(`[data-five-intent="${first.id}"]`).click();await idle();
 await page.evaluate(()=>{
  const left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane'),list=document.querySelector('.five-layer-edit-list'),swatches=document.querySelector('.five-layer-swatches'),nested=document.querySelector('.five-cut-tree');
  const style=document.createElement('style');style.id='layer-scroll-test';style.textContent='.cam-context-pane{height:150px!important;max-height:150px!important;display:block!important;overflow:auto!important}.cam-info-pane{height:240px!important;max-height:240px!important;display:block!important;overflow:auto!important}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1000px}.five-cut-tree{height:80px!important;max-height:80px!important;overflow:auto!important}.five-cut-tree::after{content:"";display:block;height:500px}.five-layer-edit-list{max-height:105px!important}.five-layer-swatches{max-height:110px!important}';document.head.append(style);
  const input=document.querySelector('[data-five-cut-name]');fixture.originalLayerCutName=input.value;input.focus({preventScroll:true});input.value='图层交互回归草稿';input.dispatchEvent(new Event('input',{bubbles:true}));input.setSelectionRange(2,6);
  fixture.layerInteraction={left,right,list,swatches,nested,input,canvas:fixture.view.viewport.renderer.domElement,listener:0};input.addEventListener('input',()=>fixture.layerInteraction.listener++);
  fixture.holdMethod='CamCut.ManageLayers';fixture.hold=true;
 });
 await color.evaluate(input=>{input.value='#aabbcc';input.dispatchEvent(new Event('input',{bubbles:true}));input.dispatchEvent(new Event('change',{bubbles:true}));});await page.waitForFunction(()=>fixture.held);
 const interaction=await page.evaluate(()=>{const n=fixture.layerInteraction;n.left.scrollTop=31;n.right.scrollTop=107;n.list.scrollTop=131;n.swatches.scrollTop=57;if(n.nested)n.nested.scrollTop=19;n.input.focus({preventScroll:true});n.input.setSelectionRange(3,7);n.input.dispatchEvent(new Event('input',{bubbles:true}));return {left:n.left.scrollTop,right:n.right.scrollTop,list:n.list.scrollTop,swatches:n.swatches.scrollTop,nested:n.nested?.scrollTop,selection:[n.input.selectionStart,n.input.selectionEnd],listener:n.listener};});
 for(const key of ['left','right','list','swatches','nested'])assert.ok(interaction[key]>0,key+' must actually scroll before the response');
 await page.evaluate(()=>fixture.release());await idle();
 const retained=await page.evaluate(()=>{const n=fixture.layerInteraction;return {nodes:n.left===document.querySelector('.cam-context-pane')&&n.right===document.querySelector('.cam-info-pane')&&n.input===document.querySelector('[data-five-cut-name]')&&n.canvas===fixture.view.viewport.renderer.domElement,focused:document.activeElement===n.input,left:n.left.scrollTop,right:n.right.scrollTop,list:n.list.scrollTop,swatches:n.swatches.scrollTop,nested:n.nested?.scrollTop,selection:[n.input.selectionStart,n.input.selectionEnd],listener:n.listener,value:n.input.value};});
 assert.equal(retained.nodes,true);assert.equal(retained.focused,true);for(const key of ['left','right','list','swatches','nested','selection','listener'])assert.deepEqual(retained[key],interaction[key],key);assert.equal(retained.value,'图层交互回归草稿');report.cases.push('异步响应前继续滚动：双侧栏、嵌套列表、画布/输入节点、监听器、焦点与选区均保留');
 // Discard the uncommitted command draft without changing native machining.
 await page.evaluate(()=>{const n=fixture.layerInteraction;n.input.value=fixture.originalLayerCutName;n.input.dispatchEvent(new Event('input',{bubbles:true}));n.input.blur();document.querySelector('#layer-scroll-test').remove();});await idle();
 await page.locator(`[data-five-command="five.layer.delete:${addedId}"]`).click();await page.locator('[data-five-layer-target]').selectOption(palette[1].id);await page.locator('[data-five-command="five.layer.delete-confirm"]').click();await idle();
 let after=await read();assert.equal(after.layers.catalog[addedId],undefined);for(const id of [first.id,second.id])assert.equal(after.intentToolpaths.find(c=>c.id===id).layers.visibleLayerId,palette[1].id);
 await rpc({action:'undo'});await refresh();let undone=await read();assert.equal(undone.layers.catalog[addedId].color.toLowerCase(),'#aabbcc');assert.equal(undone.intentToolpaths.find(c=>c.id===first.id).layers.visibleLayerId,addedId);
 await rpc({action:'redo'});await refresh();after=await read();assert.equal(after.layers.catalog[addedId],undefined);report.cases.push('删层迁移刀路、原生撤销和重做');
 await page.locator(`[data-five-check="c:${first.id}"]`).click();await choose(palette[7].id);await idle();after=await read();assert.equal(after.layers.visibleLayerId,palette[7].id);assert.equal(after.intentToolpaths.find(c=>c.id===first.id).layers.visibleLayerId,palette[1].id);report.cases.push('无刀路选择时切换当前图层，已有刀路不被移动');
 const saved=await rpc({action:'save-reopen'});assert.deepEqual(saved.layers,after.layers);for(const id of [first.id,second.id])assert.equal(saved.intentToolpaths.find(c=>c.id===id).layers.visibleLayerId,palette[1].id);await refresh();
 await rpc({action:'export-project',path:resolve(output,'图层验收.i5cam')});await page.locator('[data-five-command="five.layer.close"]').click();await page.screenshot({path:resolve(output,'图层栏.png')});
 assert.deepEqual(errors,[]);report.passed=true;report.cases.push('保存重开保留自定义层数量、配色与刀路归属');report.palette=palette;report.retained=retained;report.project=resolve(output,'图层验收.i5cam');writeFileSync(resolve(output,'layers-report.json'),JSON.stringify(report,null,2));console.log('PASS native toolpath layers and browser interactions');
}
