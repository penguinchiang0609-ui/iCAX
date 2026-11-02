import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {serveBrowserAsset,browserReportDirectory,browserAssetPath} from './browserPackageRuntime.mjs';
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const output=browserReportDirectory('machine-definition-manager-20261029'),browser=await chromium.launch({channel:'msedge',headless:true});
try{
 const page=await browser.newPage({viewport:{width:1600,height:1000}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.route('https://machine-manager.test/**',route=>{
  const path=new URL(route.request().url()).pathname;
  if(path.endsWith('.png')||path.endsWith('.css'))return route.fulfill({contentType:path.endsWith('.png')?'image/png':'text/css',body:readFileSync(browserAssetPath(path.slice(5)))});
  return serveBrowserAsset(route);
 });
 await page.goto('https://machine-manager.test/');
 await page.evaluate(async()=>{
  const [{machineCommand,renderMachineLeft,renderMachineRight,renderMachineDefinitionManager},{getProjectView},{selectTool,activeTool},{getRibbonDefinition},{patchWorkspacePanels},{syncFloatingDialogs},{commandAvailability}]=await Promise.all([import('/src/apps/five-axis-cam/webpage/machineSimulation.mjs'),import('/src/apps/_shared/workbench/state/projectViewStore.mjs'),import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs'),import('/src/apps/five-axis-cam/webpage/ribbon.mjs'),import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs'),import('/src/apps/five-axis-cam/webpage/floatingDialogs.mjs'),import('/src/apps/five-axis-cam/webpage/commandAvailability.mjs')]);
  const stylesheet=document.createElement('link');stylesheet.rel='stylesheet';stylesheet.href='/src/apps/five-axis-cam/webpage/workspace.css';document.head.append(stylesheet);
  document.body.innerHTML='<style>body{margin:0;font:14px Arial;background:#e5ecef}#mount{display:block;position:relative;height:100vh}.test-ribbon{height:140px;background:white;padding:20px;display:flex;gap:24px}.cam-context-pane,.cam-info-pane{position:absolute;top:180px;height:760px;width:260px;overflow:auto;background:#f5f8f9;padding:16px}.cam-context-pane{left:0}.cam-info-pane{right:0}.test-scene{position:absolute;left:290px;right:290px;top:170px;bottom:20px;background:#12252d}.test-overlay{position:absolute;left:290px;top:170px;right:290px;bottom:20px;pointer-events:none}.test-scroll{height:1200px}.test-nested{height:50px;overflow:auto}</style><main id="mount" class="five-workspace"><nav class="test-ribbon"></nav><aside class="cam-context-pane"></aside><div class="test-scene"><canvas></canvas></div><aside class="cam-info-pane"></aside><div class="test-overlay" data-five-overlay></div></main>';
  const ids=['b21ac27d-75e8-4847-a80a-4c25feaa3015','bf0d5260-2b94-4454-929e-c075b1ee1600','f4ed0130-37ef-45fb-9883-7af0b1fc1000','a30b2cab-c634-4b3a-94c0-c7ad1eac0800'];
  let definitions=ids.map((id,i)=>({id,name:['LC-3015 工业五轴激光机床','IR-1600 六轴激光加工机械臂','RC-1000 转盘式五轴激光机床（4+1）','TC-800 摇篮式五轴激光机床（3+2）'][i],format:i===1?'SDF':'URDF',enabled:true})),machines=[],calls=[],fail=false;
  const projectId='manager-test',view=getProjectView(projectId);Object.assign(view,{fiveAxisArea:'simulation',activeAreaId:'simulation',scene:{machineDefinitions:structuredClone(definitions),machines,domain:{setups:[],placements:[],workpieces:[],fixtureUnits:[]},intentToolpaths:[]}});
  const mount=document.querySelector('#mount'),context={mount,project:{projectId},isCurrentProject:()=>true,actions:{refreshProjectHistoryControls(){},async refreshNativeProjectHistory(){}},projectProxy:{state:{projectPath:'C:/tests/manager.i5cam'},async autosave(){}},productProxy:{async invoke(method,payload){calls.push({method,payload:structuredClone(payload)});if(method==='Product.GetSettingsHistory')return {undoSteps:[],redoSteps:[]};if(method==='MachineDefinition.List')return {definitions:structuredClone(definitions)};await new Promise(r=>setTimeout(r,160));if(fail){fail=false;throw Error('导入文件读取失败');}if(method==='MachineDefinition.Import'){const d={id:'imported',name:'导入的机床',format:'URDF',enabled:true};definitions.push(d);return {machineDefinitionId:d.id};}const d=definitions.find(d=>d.id===payload.machineDefinitionId);if(method==='MachineDefinition.SetEnabled')d.enabled=payload.enabled;else if(method==='MachineDefinition.Delete')definitions=definitions.filter(d=>d.id!==payload.machineDefinitionId);else throw Error(method);return {};}}};
  context.productProxy.bridge={async openFileDialog(){return 'C:/tests/imported.urdf';}};
  context.sceneProxy={async invoke(method,payload){calls.push({method,payload});if(method==='Machine.Instantiate'){const d=definitions.find(d=>d.id===payload.machineDefinitionId);if(!d||!d.enabled)throw Error('disabled');const machine={entityId:'instance-'+machines.length,name:d.name,enabled:true,elements:[],joints:[]};machines.push(machine);return {machine};}throw Error(method);}};
  const extra=side=>`<div data-five-key="extra-${side}"><input aria-label="${side}草稿" value="保持文本光标"><div class="test-nested" data-five-key="nested-${side}"><div style="height:500px">嵌套滚动</div></div><div class="test-scroll"></div></div>`;
  const ops={renderProject(){patchWorkspacePanels(mount,[['.cam-context-pane',renderMachineLeft(view)+extra('左侧')],['.cam-info-pane',renderMachineRight(view)+extra('右侧')],['[data-five-overlay]',renderMachineDefinitionManager(view)]]);syncFloatingDialogs(mount,binding);},async refreshSceneState(){view.scene.machines=structuredClone(machines);},async fitViewAfterRenderPublish(){},appendProjectLog(){}};
  const binding={view,context,area:'simulation'};
  mount.addEventListener('click',e=>{const b=e.target.closest('[data-five-command]');if(b&&!b.disabled)void machineCommand(context,view,b.dataset.fiveCommand.slice(13),ops);});
  const groups=getRibbonDefinition(context).tabs.find(t=>t.id==='simulation').groups;
  mount.querySelector('.test-ribbon').innerHTML=groups.map(g=>`<div><strong>${g.title}</strong><div>${g.commands.map(c=>`<button data-test-command="${c.id}">${c.title}</button>`).join('')}</div></div>`).join('');
  mount.addEventListener('click',e=>{const id=e.target.dataset.testCommand;if(['five.tool.machine-definitions','five.machine-panel.instantiate'].includes(id))void machineCommand(context,view,'definitions-open',ops);});
  window.manager={view,context,ops,calls,ids,groups,command:action=>machineCommand(context,view,action,ops),render:()=>ops.renderProject(),availability:id=>commandAvailability(view,id),activeTool:()=>activeTool(view),failNext(){fail=true;}};ops.renderProject();
 });
 const dialog=page.getByRole('dialog',{name:'机床定义管理'}),rows=dialog.getByRole('listitem');
 assert.equal(await page.locator('.cam-context-pane [aria-label="机床定义"]').count(),0);
 assert.deepEqual(await page.evaluate(()=>manager.groups[0].commands.map(c=>[c.id,c.title])),[['five.tool.machine-definitions','管理']]);
 await page.locator('[data-test-command="five.tool.machine-definitions"]').click();await dialog.waitFor();assert.equal(await rows.count(),4);
 await page.waitForFunction(()=>[...document.querySelectorAll('.five-machine-thumbnail')].every(i=>i.complete&&i.naturalWidth>0));
 await page.screenshot({path:resolve(output,'机床定义管理.png'),style:'[data-five-key^=extra-]{visibility:hidden}'});
 assert.equal(await page.evaluate(()=>manager.activeTool()),'machine-instances');
 const robot=await page.evaluate(()=>manager.ids[1]),status=dialog.locator(`[data-five-command="five.machine.definition-toggle:${robot}"]`);
 await page.evaluate(()=>{window.before={canvas:document.querySelector('canvas'),left:document.querySelector('.cam-context-pane'),right:document.querySelector('.cam-info-pane'),row:document.querySelectorAll('.five-machine-definition-row')[1],status:document.querySelectorAll('.five-machine-definition-status')[1]};});
 await status.click();await page.waitForFunction(()=>!manager.view.fiveMachine.busy);assert.equal(await status.getAttribute('aria-checked'),'false');
 assert.equal(await page.evaluate(()=>document.activeElement===before.status),true);
 await status.press('Space');await page.waitForFunction(()=>!manager.view.fiveMachine.busy);assert.equal(await status.getAttribute('aria-checked'),'true');
 // Refresh while both panes and nested containers are scrolled. Continue editing/scrolling after dispatch.
 await page.evaluate(()=>{
  const left=before.left,right=before.right,input=left.querySelector('input');window.editor=input;input.focus({preventScroll:true});input.setSelectionRange(1,4);left.scrollTop=35;right.scrollTop=80;left.querySelector('.test-nested').scrollTop=45;right.querySelector('.test-nested').scrollTop=65;
  window.pending=manager.command('definition-toggle:'+manager.ids[1]);input.value='异步期间继续输入';input.setSelectionRange(2,6);left.scrollTop=110;right.scrollTop=170;left.querySelector('.test-nested').scrollTop=100;right.querySelector('.test-nested').scrollTop=120;
 });await page.evaluate(()=>pending);
 assert.deepEqual(await page.evaluate(()=>({canvas:before.canvas===document.querySelector('canvas'),row:before.row===document.querySelectorAll('.five-machine-definition-row')[1],input:editor===before.left.querySelector('input'),focused:document.activeElement===editor,selection:[editor.selectionStart,editor.selectionEnd],value:editor.value,scroll:[before.left.scrollTop,before.right.scrollTop,before.left.querySelector('.test-nested').scrollTop,before.right.querySelector('.test-nested').scrollTop]})),{canvas:true,row:true,input:true,focused:true,selection:[2,6],value:'异步期间继续输入',scroll:[110,170,100,120]});
 await dialog.locator(`[data-five-command="five.machine.definition-select:${robot}"]`).click();assert.equal(await dialog.getByRole('button',{name:'创建实例',exact:true}).isDisabled(),true);
 await dialog.getByRole('button',{name:'启用',exact:true}).click();await page.waitForFunction(()=>!manager.view.fiveMachine.busy);
 await dialog.getByRole('button',{name:'创建实例',exact:true}).click();await page.waitForFunction(()=>!manager.view.fiveMachine.busy);assert.equal(await dialog.count(),0);assert.equal(await page.evaluate(()=>manager.view.scene.machines.length),1);
 await page.locator('[data-test-command="five.machine-panel.instantiate"]').click();await dialog.waitFor();
 await dialog.getByRole('button',{name:'删除',exact:true}).click();await page.waitForFunction(()=>!manager.view.fiveMachine.busy);assert.equal(await rows.count(),3);assert.equal(await page.evaluate(()=>manager.view.scene.machines.length),1);
 await dialog.getByRole('button',{name:'新增 / 导入',exact:true}).click();await page.waitForFunction(()=>!manager.view.fiveMachine.busy);assert.equal(await rows.count(),4);assert.match(await dialog.locator('[data-five-key="definition-properties"]').textContent(),/导入的机床/);assert.equal(await dialog.locator('.five-machine-thumbnail-empty').count(),1);
 await page.evaluate(()=>manager.failNext());await dialog.getByRole('button',{name:'新增 / 导入',exact:true}).click();await page.waitForFunction(()=>!manager.view.fiveMachine.busy);assert.match(await dialog.getByRole('alert').textContent(),/读取失败/);
 await page.evaluate(()=>{manager.view.fiveMachine.error='';manager.render();before.left.scrollTop=0;before.right.scrollTop=0;});
 await page.screenshot({path:resolve(output,'机床定义管理-导入后.png'),style:'[data-five-key^=extra-]{visibility:hidden}'});
 await page.setViewportSize({width:900,height:700});await page.waitForTimeout(100);const bounds=await dialog.boundingBox();assert.ok(bounds.x>=0&&bounds.x+bounds.width<=900);assert.ok(bounds.y>=0&&bounds.y+bounds.height<=700);await page.screenshot({path:resolve(output,'机床定义管理-小窗口.png')});
 await dialog.getByRole('button',{name:'关闭',exact:true}).click();assert.equal(await dialog.count(),0);assert.equal(await page.locator('.cam-context-pane [aria-label="机床定义"]').count(),0);
 const fromCAM=await page.evaluate(async()=>{const f=manager,navigation=[];f.view.fiveAxisArea=f.view.activeAreaId='toolpath';f.view.fiveSimulation={headOpen:true};f.context.actions.selectRibbonTab=async area=>{navigation.push(area);f.view.fiveAxisArea=f.view.activeAreaId=area;};await f.command('instantiate:imported');return {navigation,area:f.view.fiveAxisArea,headOpen:f.view.fiveSimulation.headOpen,tool:f.activeTool()};});
 assert.deepEqual(fromCAM,{navigation:['simulation'],area:'simulation',headOpen:false,tool:'machine-instances'});
 assert.deepEqual(errors,[]);
 const result={passed:true,browser:'Microsoft Edge',nativeTransport:'controlled protocol fixture',standaloneCef:false,managementOnlyRibbon:true,definitionListOnlyInDialog:true,realBundledThumbnails:4,enableDisable:true,importDelete:true,instancesRetained:true,creationFromDialog:true,disabledCreationBlocked:true,asyncFocusSelectionAndScrollPreserved:true,canvasAndRowsRetained:true,smallWindowFits:true};writeFileSync(resolve(output,'report.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));
}finally{await browser.close();}
