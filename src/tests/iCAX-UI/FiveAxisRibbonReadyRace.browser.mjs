import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {serveBrowserAsset,browserReportDirectory} from './browserPackageRuntime.mjs';
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const output=browserReportDirectory('five-axis-ribbon-ready-race'),browser=await chromium.launch({channel:'msedge',headless:true});
try{
 const page=await browser.newPage({viewport:{width:1400,height:950}});await page.route('https://ribbon-race.test/**',serveBrowserAsset);await page.goto('https://ribbon-race.test/');
 const result=await page.evaluate(async()=>{
  const {syncRibbonCommandAvailability}=await import('/src/apps/five-axis-cam/webpage/ribbon.mjs');
  document.body.innerHTML='<main class="workbench"><nav class="product-ribbon"><button class="ribbon-tab active" data-tab-id="simulation">孪生</button><button class="ribbon-command" data-command-id="five.tool.machine-simulation" disabled title="请先完成当前操作。">工件摆放</button></nav><section id="mount"><aside class="cam-context-pane" style="height:150px;overflow:auto"><div style="height:1500px"></div></aside><aside class="cam-info-pane" style="height:220px;overflow:auto"><input value="工程加工坐标"><div style="height:1500px"></div></aside><canvas></canvas></section></main>';
  const definition={tabs:[{id:'simulation',groups:[{commands:[{id:'five.tool.machine-simulation',disabled:false,unavailableReason:''}]}]}]},ribbon=document.querySelector('.product-ribbon'),input=document.querySelector('input'),left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane'),canvas=document.querySelector('canvas');
  const frozenSignature=JSON.stringify(definition);let listener=0;input.addEventListener('probe',()=>listener++);input.focus({preventScroll:true});input.value='等待结束后继续输入';input.setSelectionRange(2,6,'backward');left.scrollTop=240;right.scrollTop=180;
  const corrected=syncRibbonCommandAvailability(ribbon,definition),button=ribbon.querySelector('.ribbon-command');
  const first={corrected,disabled:button.disabled,title:button.getAttribute('title'),sameSignature:frozenSignature===JSON.stringify(definition),sameInput:input===document.querySelector('input'),focus:document.activeElement===input,selection:[input.selectionStart,input.selectionEnd],scroll:[left.scrollTop,right.scrollTop],canvas:canvas===document.querySelector('canvas')};
  // Replace only the shell's stale ribbon, while the definition and the live
  // workspace remain the exact same objects. The next attach must still sync.
  const stale=ribbon.cloneNode(true);stale.querySelector('.ribbon-command').disabled=true;stale.querySelector('.ribbon-command').title='请先完成当前操作。';ribbon.replaceWith(stale);await Promise.resolve();
  const afterRemount=syncRibbonCommandAvailability(stale,definition),current=stale.querySelector('.ribbon-command');input.dispatchEvent(new Event('probe'));
  const second={afterRemount,disabled:current.disabled,title:current.getAttribute('title'),sameInput:input===document.querySelector('input'),focus:document.activeElement===input,selection:[input.selectionStart,input.selectionEnd],scroll:[left.scrollTop,right.scrollTop],listener,canvas:canvas===document.querySelector('canvas')};
  const idle=syncRibbonCommandAvailability(stale,definition);definition.tabs[0].groups[0].commands[0]={id:'five.tool.machine-simulation',disabled:true,unavailableReason:'请先完成当前机床操作。'};syncRibbonCommandAvailability(stale,definition);
  return {first,second,idle,blocked:{disabled:current.disabled,title:current.title}};
 });
 assert.deepEqual(result.first,{corrected:true,disabled:false,title:null,sameSignature:true,sameInput:true,focus:true,selection:[2,6],scroll:[240,180],canvas:true});
 assert.deepEqual(result.second,{afterRemount:true,disabled:false,title:null,sameInput:true,focus:true,selection:[2,6],scroll:[240,180],listener:1,canvas:true});assert.equal(result.idle,false);assert.deepEqual(result.blocked,{disabled:true,title:'请先完成当前机床操作。'});
 const menuResult=await page.evaluate(async()=>{
  const {getRibbonDefinition,syncRibbonCommandAvailability}=await import('/src/apps/five-axis-cam/webpage/ribbon.mjs'),{getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs'),{selectTool}=await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs');
  const projectId='ribbon-open-menu',view=getProjectView(projectId),machine={entityId:'machine',enabled:true,elements:[],joints:[]};
  Object.assign(view,{fiveAxisArea:'simulation',fiveMachine:{machineId:'machine'},scene:{machines:[],machineDefinitions:[],intentToolpaths:[],domain:{placements:[],workpieces:[],setups:[],fixtureUnits:[]}}});selectTool(view,'machine-instances');
  const definition=()=>getRibbonDefinition({project:{projectId}}),instance=()=>definition().tabs.find(t=>t.id==='simulation').groups.find(g=>g.title==='机床实例').commands.find(c=>c.id==='five.tool.machine-instances'),main=instance();
  document.body.innerHTML=`<nav class="product-ribbon"><button class="ribbon-tab active" data-tab-id="simulation">孪生</button><div class="ribbon-split-command"><button class="ribbon-command" data-command-id="${main.id}" disabled>实例参数</button><button class="ribbon-command-menu-toggle" data-command-id="${main.id}" aria-expanded="true" disabled>展开</button><div class="ribbon-command-menu" role="menu">${main.menuItems.map(c=>`<button class="ribbon-command-menu-item" data-command-id="${c.id}">${c.title}</button>`).join('')}</div></div></nav><aside class="cam-context-pane" style="height:150px;overflow:auto"><div style="height:1500px"></div></aside><aside class="cam-info-pane" style="height:220px;overflow:auto"><input value="参数编辑"><div style="height:1500px"></div></aside><canvas></canvas>`;
  const ribbon=document.querySelector('.product-ribbon'),menu=document.querySelector('.ribbon-command-menu'),input=document.querySelector('input'),canvas=document.querySelector('canvas'),left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane');
  input.focus({preventScroll:true});input.setSelectionRange(1,3);left.scrollTop=120;right.scrollTop=230;let events=0;input.addEventListener('probe',()=>events++);
  const read=()=>({main:ribbon.querySelector('.ribbon-command').disabled,toggle:ribbon.querySelector('.ribbon-command-menu-toggle').disabled,items:[...menu.querySelectorAll('button')].map(b=>({disabled:b.disabled,title:b.title})),open:ribbon.querySelector('.ribbon-command-menu-toggle').getAttribute('aria-expanded')==='true'});
  syncRibbonCommandAvailability(ribbon,definition());const noMachine=read();
  view.scene.machines=[machine];syncRibbonCommandAvailability(ribbon,definition());const available=read();
  view.pending=true;syncRibbonCommandAvailability(ribbon,definition());const pending=read();view.pending=false;
  view.viewport={transformManipulator:{pointerId:1}};syncRibbonCommandAvailability(ribbon,definition());const dragging=read();view.viewport.transformManipulator.pointerId=null;
  view.fiveMachine.busy=true;syncRibbonCommandAvailability(ribbon,definition());const busy=read();view.fiveMachine.busy=false;
  syncRibbonCommandAvailability(ribbon,definition());const recovered=read(),idle=syncRibbonCommandAvailability(ribbon,definition());
  // Even an already-open menu inherits an authorization block on its parent.
  const blocked=definition(),parent=blocked.tabs.find(t=>t.id==='simulation').groups.find(g=>g.title==='机床实例').commands.find(c=>c.id===main.id);parent.authorizationRequired=true;parent.unavailableReason='权限不可用';syncRibbonCommandAvailability(ribbon,blocked);const authorization=read();
  input.dispatchEvent(new Event('probe'));
  return {noMachine,available,pending,dragging,busy,recovered,authorization,idle,stable:{menu:menu===document.querySelector('.ribbon-command-menu'),input:input===document.querySelector('input'),canvas:canvas===document.querySelector('canvas'),focus:document.activeElement===input,selection:[input.selectionStart,input.selectionEnd],scroll:[left.scrollTop,right.scrollTop],events}};
 });
 assert.equal(menuResult.noMachine.main,false);assert.equal(menuResult.noMachine.toggle,false);assert.ok(menuResult.noMachine.items.every(item=>item.disabled&&/机床实例/.test(item.title)));
 for(const key of ['available','recovered']){assert.equal(menuResult[key].main,false);assert.equal(menuResult[key].toggle,false);assert.ok(menuResult[key].items.every(item=>!item.disabled&&item.title===''));assert.equal(menuResult[key].open,true);}
 for(const key of ['pending','dragging','busy','authorization']){assert.equal(menuResult[key].main,true);assert.equal(menuResult[key].toggle,true);assert.ok(menuResult[key].items.every(item=>item.disabled&&item.title));assert.equal(menuResult[key].open,true);}
 assert.equal(menuResult.idle,false);assert.deepEqual(menuResult.stable,{menu:true,input:true,canvas:true,focus:true,selection:[1,3],scroll:[120,230],events:1});
 writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,standaloneCef:false,result,menuResult},null,2));console.log(JSON.stringify({passed:true,unchangedDefinitionStaleDOM:true,shellRibbonRemount:true,focusSelectionScrollAndListenerPreserved:true,actualBusyRemainsDisabled:true,openMenuAvailability:true,splitToggleAvailability:true}));
}finally{await browser.close();}
