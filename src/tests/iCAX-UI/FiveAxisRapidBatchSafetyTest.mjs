import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {serveBrowserAsset} from './browserPackageRuntime.mjs';
import {rapidSafetyEnabled,selectRapidSafety,renderRapidSafety} from '../../apps/five-axis-cam/webpage/rapidDefaultStrategy.mjs';
import {openRapid,generateRapids,renderRapid,renderRapidTree} from '../../apps/five-axis-cam/webpage/rapidPlanning.mjs';
import {rapidDependencyKey} from '../../apps/five-axis-cam/webpage/rapidDependencies.mjs';
import {renderGeneration} from '../../apps/five-axis-cam/webpage/toolpathGeneration.mjs';
import {renderCamHeadCard,renderHeadSettings,renderSimulation,simulationCommand,simulationField} from '../../apps/five-axis-cam/webpage/trajectorySimulation.mjs';

function fixture({hold=false,blocked=false}={}){
 let release,listRelease;const gate=new Promise(resolve=>{release=resolve;}),listGate=new Promise(resolve=>{listRelease=resolve;});
 const route={from:'a',to:'b',inputKey:'original',endpointStatus:'Current',geometryStatus:'Generated',validation:'Unchecked',frame:'Setup',referencePoint:'ToolTip',points:[[0,0,1],[1,0,11],[2,0,1]],directions:[[0,0,1],[0,0,1],[0,0,1]],policy:{strategy:'Quick',marginMm:.2,liftHeightMm:10}};
 const head={kind:'Imported',name:'测试切割头',calibrated:true,tcpLocalPosition:[0,0,0],beamLocalDirection:[0,0,-1],collisionModel:{status:'Ready',boxCount:'1'}};
 const native={expected:{epoch:'batch-safe',mutationSequence:'1'},activeSetupId:'setup',setups:[{id:'setup',headModel:head,rapidSettings:{followHeightMm:1,collisionThresholdMm:.1,...route.policy},generatedGeometry:{status:'Current',revision:'cuts',operations:[{intentId:'a',order:1},{intentId:'b',order:2}],transitions:[route]}}],workpieces:[],placements:[],fixtures:[],intentToolpaths:[]};
 const view={pending:false,fiveAxisArea:'toolpath',fiveCamPage:'rapid',fiveAxisUI:{tools:{toolpath:'rapid'}},scene:{domain:structuredClone(native)}},calls=[],logs=[];let saves=0;
 const context={isCurrentProject:()=>true,mount:{querySelector:()=>null},projectProxy:{state:{projectPath:'C:/tests/batch-safe.i5cam'},async autosave(){++saves;return {saved:true};}},sceneProxy:{async invoke(method,payload){
  calls.push({method,payload:structuredClone(payload)});
  if(method==='CamHead.ListBuiltins'){await listGate;return {items:[{id:'LC-3015',name:'LC-3015 工业五轴激光机床'}]};}
  if(method==='CamHead.Configure'){
   const s=native.setups[0];s.headModel={...s.headModel,kind:'Builtin',builtinDefinitionId:payload.builtinDefinitionId,name:'LC-3015 工业五轴激光机床'};native.expected.mutationSequence=String(Number(native.expected.mutationSequence)+1);return {changed:true};
  }
  if(method==='CamScene.ReadState')return structuredClone(native);
  if(method==='CamRapid.Generate'){
   if(hold)await gate;
   const t=native.setups[0].generatedGeometry.transitions[0];t.validation=payload.safe&&!blocked?'Clear':'Unchecked';t.geometryStatus=payload.safe&&blocked?'Failed':'Generated';t.reason=payload.safe&&blocked?'RAPID_NO_FEASIBLE_ROUTE_WITHIN_PARAMETERS':'';
   if(payload.safe)t.points[1]=[1,8,11];native.expected.mutationSequence=String(Number(native.expected.mutationSequence)+1);
   return {status:'Completed',result:{count:'1',generatedCount:t.geometryStatus==='Generated'?'1':'0',clearCount:t.validation==='Clear'?'1':'0',calculatedCount:'1',reusedCount:'0'}};
  }
  throw Error(method);
 }}};
 const ops={renderProject(){},appendProjectLog(_context,level,message){logs.push({level,message});},async refreshSceneState(){view.scene.domain=structuredClone(native);}};
 openRapid(view);return {native,view,context,ops,calls,logs,release,listRelease,saves:()=>saves};
}
test('safety is an unchecked per-setup generation choice and changing it does not mutate the project',()=>{
 const f=fixture(),before=structuredClone(f.native);assert.equal(rapidSafetyEnabled(f.view),false);assert.doesNotMatch(renderRapidSafety(f.view),/checked/);
 selectRapidSafety(f.view,true);assert.equal(rapidSafetyEnabled(f.view),true);assert.match(renderRapidSafety(f.view),/checked/);assert.deepEqual(f.native,before);assert.equal(f.calls.length,0);
 f.view.scene.domain.setups.push({...f.view.scene.domain.setups[0],id:'other'});f.view.scene.domain.activeSetupId='other';assert.equal(rapidSafetyEnabled(f.view),false);f.view.scene.domain.activeSetupId='setup';assert.equal(rapidSafetyEnabled(f.view),true);
});
test('unchecked generation keeps the existing parameter-only request and awaiting-check result',async()=>{
 const f=fixture();await generateRapids(f.context,f.view,f.ops);assert.equal(f.calls.find(c=>c.method==='CamRapid.Generate').payload.safe,false);assert.match(f.logs.at(-1).message,/待碰撞检测/);assert.equal(f.view.scene.domain.setups[0].generatedGeometry.transitions[0].validation,'Unchecked');
});
test('safe generation freezes the chosen mode and discards evidence for replaced geometry with the same input key',async()=>{
 const f=fixture({hold:true});selectRapidSafety(f.view,true);
 f.view.fiveRapidCollisionChecks=new Map([['a/b',{key:rapidDependencyKey(f.view),policyKey:'old',result:{validation:'Unchecked'}}]]);f.view.fiveRapid.collisionCheck={result:{validation:'Unchecked'}};
 const work=generateRapids(f.context,f.view,f.ops);await new Promise(setImmediate);selectRapidSafety(f.view,false);assert.equal(rapidSafetyEnabled(f.view),true);assert.match(renderRapidSafety(f.view),/disabled/);
 f.release();await work;assert.equal(f.calls.find(c=>c.method==='CamRapid.Generate').payload.safe,true);assert.equal(f.view.fiveRapidCollisionChecks.has('a/b'),false);assert.equal(f.view.fiveRapid.collisionCheck,null);
 assert.equal(f.view.scene.domain.setups[0].generatedGeometry.transitions[0].validation,'Clear');assert.match(f.logs.at(-1).message,/安全空移碰撞检测通过/);assert.equal(f.saves(),1);
});
test('safe generation never labels an unresolved route as safe',async()=>{
 const f=fixture({blocked:true});selectRapidSafety(f.view,true);await generateRapids(f.context,f.view,f.ops);assert.match(f.view.error,/1 段.*未找到.*安全路径/);assert.equal(f.logs.at(-1).level,'error');assert.doesNotMatch(f.logs.at(-1).message,/安全空移碰撞检测通过/);
});
test('the safe batch summary reports actual passed and failed counts instead of awaiting collision checks',async()=>{
 for(const blocked of [false,true]){const f=fixture({blocked});selectRapidSafety(f.view,true);await generateRapids(f.context,f.view,f.ops);const html=renderGeneration(f.view);assert.match(html,new RegExp(`${blocked?'0':'1'}/1 段通过切割头碰撞检测`));assert.doesNotMatch(html,/待碰撞检测/);if(blocked)assert.match(html,/1 段未通过/);}
});
test('the current head and settings are visible in CAM before simulation; playback owns no head settings control',()=>{
 const f=fixture();assert.match(renderCamHeadCard(f.view),/切割头模型/);assert.match(renderCamHeadCard(f.view),/测试切割头/);assert.match(renderCamHeadCard(f.view),/设置切割头/);
 f.view.fiveSimulation={headOpen:true};assert.match(renderHeadSettings(f.view),/data-five-simulation-field="builtin-head"/);assert.match(renderHeadSettings(f.view),/data-five-simulation-field="tcp0"/);
 const previous=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};
 try{const playback=renderSimulation(f.view);assert.doesNotMatch(playback,/five\.simulation\.head-open|head-popover/);}finally{globalThis.cancelAnimationFrame=previous;}
});
test('actual browser head-list and selection callbacks preserve both panels, canvas, listeners, focus, selection and latest nested scrolling',async()=>{
 const require=createRequire(import.meta.url),{chromium}=require(process.env.ICAX_PLAYWRIGHT_MODULE||'C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
 const browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'msedge'});
 try{
  const page=await browser.newPage({viewport:{width:1400,height:900}});
  await page.route('http://127.0.0.1:19373/**',route=>new URL(route.request().url()).pathname==='/'?route.fulfill({contentType:'text/html',body:'<style>#mount{display:grid;grid-template-columns:280px 500px 500px}.left,.right{height:380px;overflow:auto}.head{height:200px;overflow:auto}.spacer{height:1500px}.five-head-popover{position:static!important;max-height:160px;overflow:auto}canvas{width:500px;height:380px}</style><main id="mount"><aside class="left"></aside><canvas></canvas><aside class="right"></aside><div class="head"></div></main>'}):serveBrowserAsset(route));
  for(const focus of ['left','right']){
   await page.goto('http://127.0.0.1:19373/');
   await page.evaluate(async({fixtureSource,focus})=>{
    const planning=await import('/src/apps/five-axis-cam/webpage/rapidPlanning.mjs'),head=await import('/src/apps/five-axis-cam/webpage/trajectorySimulation.mjs'),safety=await import('/src/apps/five-axis-cam/webpage/rapidDefaultStrategy.mjs'),{patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs');
    const make=new Function('openRapid',`return (${fixtureSource})`)(planning.openRapid),f=window.optionFixture=make();
    f.render=()=>patchWorkspacePanels(document.querySelector('#mount'),[['.left',head.renderCamHeadCard(f.view)+safety.renderRapidSafety(f.view)+'<input id="search" data-five-key="search" value="123456">'+planning.renderRapidTree(f.view)+'<div class="spacer" data-five-key="left-spacer"></div>'],['.right',planning.renderRapid(f.view,true)+'<div class="spacer" data-five-key="right-spacer"></div>'],['.head',head.renderHeadSettings(f.view)+'<div class="spacer" data-five-key="head-spacer"></div>']]);
    f.ops.renderProject=f.render;f.render();document.querySelector('#mount').addEventListener('click',event=>{if(event.target.closest('[data-five-command="five.simulation.head-open"]'))f.work=head.simulationCommand(f.context,f.view,'head-open',f.ops);});
    document.querySelector('#mount').addEventListener('change',event=>{if(event.target.hasAttribute('data-five-rapid-safe')){safety.selectRapidSafety(f.view,event.target.checked);f.render();}if(event.target.dataset.fiveSimulationField)head.simulationField(f.context,f.view,event.target.dataset.fiveSimulationField,event.target.value,f.ops);});
    f.selector=focus==='left'?'#search':'[data-five-rapid-field="marginMm"]';
   },{fixtureSource:fixture.toString(),focus});
   await page.locator('[data-five-rapid-safe]').check();assert.equal(await page.locator('[data-five-rapid-safe]').isChecked(),true);
   await page.locator('[data-five-command="five.simulation.head-open"]').click();await page.waitForFunction(()=>optionFixture.calls.some(c=>c.method==='CamHead.ListBuiltins'));
   const before=await page.evaluate(()=>{const f=optionFixture;f.input=document.querySelector(f.selector);f.canvas=document.querySelector('canvas');f.events=0;f.input.addEventListener('input',()=>f.events++);f.input.focus({preventScroll:true});f.input.setSelectionRange(0,1);const selectors=['.left','.right','.head','.five-head-popover'];selectors.forEach((s,i)=>document.querySelector(s).scrollTop=20+i*5);selectors.forEach((s,i)=>document.querySelector(s).scrollTop=50+i*10);return {scroll:selectors.map(s=>document.querySelector(s).scrollTop),selection:[f.input.selectionStart,f.input.selectionEnd]};});
   await page.evaluate(()=>{optionFixture.listRelease();return optionFixture.work;});
   const after=await page.evaluate(()=>{const f=optionFixture;f.input.dispatchEvent(new Event('input'));return {input:f.input===document.querySelector(f.selector),canvas:f.canvas===document.querySelector('canvas'),focused:document.activeElement===f.input,selection:[f.input.selectionStart,f.input.selectionEnd],scroll:['.left','.right','.head','.five-head-popover'].map(s=>document.querySelector(s).scrollTop),events:f.events};});
   assert.ok(after.input&&after.canvas&&after.focused);assert.deepEqual(after.selection,before.selection);assert.deepEqual(after.scroll,before.scroll);assert.equal(after.events,1);
   await page.locator('[data-five-simulation-field="builtin-head"]').selectOption('LC-3015');await page.waitForFunction(()=>!optionFixture.view.pending&&!optionFixture.view.fiveSimulation.busy&&optionFixture.native.setups[0].headModel.kind==='Builtin');
   assert.equal(await page.locator('[data-five-simulation-field="tcp0"]').count(),0);assert.match(await page.locator('.five-cam-head').innerText(),/LC-3015/);assert.equal(await page.evaluate(()=>optionFixture.view.fiveAxisUI.tools.toolpath),'rapid');
  }
 }finally{await browser.close();}
});
