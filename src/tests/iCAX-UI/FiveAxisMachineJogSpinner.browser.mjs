import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {browserReportDirectory,browserAssetPath,serveBrowserAsset} from './browserPackageRuntime.mjs';

const {chromium}=createRequire(import.meta.url)('playwright'),output=browserReportDirectory('machine-jog-spinner-20261029');
const browser=await chromium.launch({channel:'msedge',headless:true}),report={passed:false,cases:[],nativeTransport:'controlled receipts; production render, jog lifecycle, parameter queue and DOM patch'},errors=[];
try{
 const page=await browser.newPage({viewport:{width:1400,height:950}});page.on('pageerror',e=>errors.push(e.message));
 await page.route('http://127.0.0.1:49197/**',serveBrowserAsset);await page.goto('http://127.0.0.1:49197/');
 await page.addStyleTag({path:browserAssetPath('apps/five-axis-cam/webpage/workspace.css')});
 await page.evaluate(async()=>{
  const ui=await import('/src/apps/five-axis-cam/webpage/machineSimulation.mjs'),queue=await import('/src/apps/five-axis-cam/webpage/machineParameterEdits.mjs');
  const {attachMachineJog}=await import('/src/apps/five-axis-cam/webpage/machineJog.mjs'),{selectTool}=await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs'),{patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs');
  document.body.innerHTML='<style>body{font:14px sans-serif;margin:0;background:#f6f9fb;color:#203c48}.layout{display:grid;grid-template-columns:260px 1fr 360px;height:900px;gap:10px}.cam-context-pane,.cam-info-pane{overflow:auto;padding:12px}.nested{height:80px;overflow:auto}.nested-content{height:600px}.spacer{height:1400px}canvas{width:100%;height:100%;background:#132831}</style><main class="layout"><aside class="cam-context-pane"></aside><canvas></canvas><aside class="cam-info-pane"></aside></main>';
  const native={entityId:'machine',name:'IR-1600 六轴激光加工机械臂',enabled:true,elements:[],tools:[],joints:[{entityId:'j1',jointName:'J1',type:'revolute',position:85.21*Math.PI/180,lower:-Math.PI,upper:Math.PI,velocity:1,homePosition:0},{entityId:'x',jointName:'X',type:'prismatic',position:12.34,lower:-100,upper:100,velocity:20,homePosition:0}]};
  native.elements=[{entityId:'machine',kind:'machine',name:'机械臂'},{entityId:'j1',kind:'part',name:'J1'},{entityId:'x',kind:'part',name:'X'}];
  const domain={expected:{epoch:'fixture',mutationSequence:'1'},activeSetupId:'setup',setups:[{id:'setup',name:'装夹 1',machineSetup:{}}],placements:[],workpieces:[],fixtures:[],fixtureUnits:[],intentToolpaths:[]};
  const view={fiveAxisArea:'simulation',activeAreaId:'simulation',scene:{machines:[structuredClone(native)],machineDefinitions:[],domain,model:{isLoaded:false}},viewport:{sceneObjects:new Map(),setTransformPreviews(){},clearTransformPreviews(){},renderer:{domElement:document.querySelector('canvas')}}};selectTool(view,'machine-simulation');
  const calls=[],pending=[];let saves=0,holdMove=false,heldStart=false,releaseStart,currentJog=null;
  const context={mount:document.body,project:{projectId:'p'},projectProxy:{state:{projectPath:'C:/test/spinner.i5cam'},async autosave(){saves++;}},isCurrentProject:()=>true,actions:{async refreshNativeProjectHistory(){},refreshProjectHistoryControls(){}},sceneProxy:{pdo:{enabled:true,async withWriteDescriptor(d,write){write(new ArrayBuffer(2048));return true;},async withReadDescriptor(){return null;}},async invoke(method,request){
   calls.push({method,request:structuredClone(request)});
   if(method==='CamSimulation.MoveMachine'){
    if(holdMove)await new Promise(release=>pending.push(release));
    for(const [id,value]of Object.entries(request.positions)){const j=native.joints.find(j=>j.entityId===id);if(value<j.lower||value>j.upper)return {status:'Rejected',collisionStatus:'Blocked',reason:'Axis limit'};j.position=value;}
    domain.expected.mutationSequence=String(Number(domain.expected.mutationSequence)+1);return {status:'Ready',collisionStatus:'Clear',positions:request.positions};
   }
   if(method==='CamScene.ReadState')return structuredClone(domain);
   if(method==='Machine.List')return {machines:[structuredClone(native)]};
   if(method==='Machine.SetEnabled'){assertMachine(request);native.enabled=request.enabled;return {};}
   if(method==='CamSimulation.StartMachineJog'){
    currentJog=request;if(heldStart)await new Promise(release=>releaseStart=release);
    return {token:request.token,epoch:'fixture',status:'Running',positions:{},controlPDO:{id:'control',payloadSize:2048},resultPDO:{id:'result',payloadSize:2048}};
   }
   if(method==='CamSimulation.StopMachineJog')return {token:request.token,status:'Stopped',positions:{},changed:false};
   throw Error('Unexpected method '+method);
  }}};
  const nested=side=>`<div class="nested nested-${side}" data-five-key="nested-${side}"><div class="nested-content" data-five-key="nested-content-${side}"></div></div><div class="spacer" data-five-key="spacer-${side}"></div>`;
  function render(){patchWorkspacePanels(document.body,[['.cam-context-pane',ui.renderMachineLeft(view)+nested('left')],['.cam-info-pane',ui.renderMachineRight(view)+nested('right')]]);}
  function assertMachine(request){if(request.machineEntityId!==native.entityId)throw Error('Wrong machine toggle');}
  const ops={renderProject:render,async refreshSceneState(){view.scene.machines=[structuredClone(native)];},async fitViewAfterRenderPublish(){},appendProjectLog(){}};render();const binding={context,view,ops};attachMachineJog(document.body,binding);
  document.body.addEventListener('input',e=>{if(e.target.dataset.fiveMachineField)ui.machineField(context,view,e.target.dataset.fiveMachineField,e.target.value,ops);});
  document.body.addEventListener('focusout',e=>{if(e.target.dataset.fiveMachineField)queue.finishMachineParameterField(view);});
  document.body.addEventListener('change',e=>{if(e.target.tagName==='SELECT'&&e.target.dataset.fiveMachineField){ui.machineField(context,view,e.target.dataset.fiveMachineField,e.target.value,ops);render();}});
  document.body.addEventListener('click',e=>{const action=e.target.closest('[data-five-command]')?.dataset.fiveCommand;if(action?.startsWith('five.machine.'))void ui.machineCommand(context,view,action.slice(13),ops);});
  window.f={view,context,ops,native,calls,pending,render,binding,selectTool,queue,command:action=>ui.machineCommand(context,view,action,ops),holdMove:v=>holdMove=v,holdStart:v=>heldStart=v,releaseStart:()=>releaseStart(),get saves(){return saves;},get currentJog(){return currentJog;}};
 });
 const input=id=>page.locator(`[data-five-machine-field="joint:${id}"]`),arrow=(id,plus)=>page.locator(`[data-five-command="five.machine.jog-${plus?'plus':'minus'}:${id}"]`);
 const idle=()=>page.waitForFunction(()=>!f.view.fiveMachine.busy&&!f.view.fiveMachineParameterEdits?.request&&!f.view.fiveMachineParameterEdits?.entries.size&&!f.view.fiveMachineParameterEdits?.fileSaving);
 const nativeValue=id=>page.evaluate(id=>{const j=f.native.joints.find(j=>j.entityId===id);return j.position*(j.type==='prismatic'?1:180/Math.PI);},id);
 const near=(a,b)=>assert.ok(Math.abs(a-b)<1e-8,`${a} != ${b}`);
 assert.equal(await page.getByRole('button',{name:/[−＋] 点动/}).count(),0);assert.equal(await page.locator('.five-machine-jog-input').count(),2);
 const geometry=await page.evaluate(()=>[...document.querySelectorAll('.five-machine-jog-input')].map(n=>{const i=n.querySelector('input').getBoundingClientRect();return [...n.querySelectorAll('button')].map(b=>{const r=b.getBoundingClientRect();return r.left>=i.left&&r.right<=i.right&&r.top>=i.top&&r.bottom<=i.bottom;});}));assert.ok(geometry.flat().every(Boolean),'arrows fit inside input bounds');
 await arrow('j1',true).click();await idle();near(await nativeValue('j1'),86.21);await arrow('j1',false).click();await idle();near(await nativeValue('j1'),85.21);
 assert.equal(await page.evaluate(()=>f.calls.filter(c=>c.method==='CamSimulation.MoveMachine').length),2);
 await page.locator('[data-five-machine-field="jogStep"]').selectOption('0.1');await arrow('x',true).click();await idle();near(await nativeValue('x'),12.44);await arrow('x',false).click();await idle();near(await nativeValue('x'),12.34);
 await input('j1').press('ArrowUp');await idle();near(await nativeValue('j1'),85.31);await input('j1').press('ArrowDown');await idle();near(await nativeValue('j1'),85.21);
 await input('x').fill('25.67');await arrow('x',true).click();await idle();near(await nativeValue('x'),25.77);
 report.cases.push('integrated upper/lower arrows for rotary and linear axes; one click is exactly one checked move; fractional step and typed draft applied before jog; keyboard arrows preserve decimals');
 // Replies must preserve the latest interaction, including scroll performed after submission.
 await page.evaluate(()=>f.holdMove(true));await input('x').fill('26.78');await page.waitForFunction(()=>f.pending.length===1);
 const scroll=await page.evaluate(()=>{const text=document.querySelector('[data-five-machine-field="position0"]');text.focus({preventScroll:true});text.value='12.345';text.setSelectionRange(1,4);f.nodes={text,canvas:document.querySelector('canvas'),input:document.querySelector('[data-five-machine-field="joint:x"]'),arrow:document.querySelector('[data-five-command="five.machine.jog-plus:x"]')};f.events=0;text.addEventListener('audit',()=>f.events++);f.panes=['.cam-context-pane','.cam-info-pane','.nested-left','.nested-right'].map(s=>document.querySelector(s));return f.panes.map((n,i)=>{n.scrollTop=40+i*15;return n.scrollTop;});});
 await page.evaluate(()=>{f.holdMove(false);f.pending.shift()();});await idle();
 const interaction=await page.evaluate(()=>{const n=f.nodes;n.text.dispatchEvent(new Event('audit'));return {same:n.text===document.querySelector('[data-five-machine-field="position0"]')&&n.canvas===document.querySelector('canvas')&&n.input===document.querySelector('[data-five-machine-field="joint:x"]')&&n.arrow===document.querySelector('[data-five-command="five.machine.jog-plus:x"]'),focused:document.activeElement===n.text,selection:[n.text.selectionStart,n.text.selectionEnd],scroll:f.panes.map(n=>n.scrollTop),events:f.events};});
 assert.equal(interaction.same,true);assert.equal(interaction.focused,true);assert.deepEqual(interaction.selection,[1,4]);assert.deepEqual(interaction.scroll,scroll);assert.equal(interaction.events,1);report.interaction=interaction;
 report.cases.push('async reply preserves canvas/input/arrow/listener nodes, focused text selection and latest scroll in both panes and nested containers');
 const hold=async mode=>{
  const b=arrow('j1',true);await b.scrollIntoViewIfNeeded();const r=await b.boundingBox();await page.mouse.move(r.x+r.width/2,r.y+r.height/2);await page.mouse.down();await page.waitForFunction(()=>f.view.fiveMachineJog?.running);
  if(mode==='leave')await page.mouse.move(r.x-20,r.y-20);
  else if(mode==='blur')await page.evaluate(()=>window.dispatchEvent(new Event('blur')));
  else await page.mouse.up();
  await page.waitForFunction(()=>f.view.fiveMachineJog?.ended);await page.mouse.up();
 };
 const moves=await page.evaluate(()=>f.calls.filter(c=>c.method==='CamSimulation.MoveMachine').length);
 for(const mode of ['release','leave','blur'])await hold(mode);
 assert.equal(await page.evaluate(()=>f.calls.filter(c=>c.method==='CamSimulation.MoveMachine').length),moves,'held arrow never issues a trailing step');
 assert.equal(await page.evaluate(()=>f.calls.filter(c=>c.method==='CamSimulation.StartMachineJog').length),3);assert.equal(await page.evaluate(()=>f.calls.filter(c=>c.method==='CamSimulation.StopMachineJog').length),3);
 // Pending Start is reliably stopped after release, with no click-generated move.
 await page.evaluate(()=>f.holdStart(true));await arrow('x',false).scrollIntoViewIfNeeded();const r=await arrow('x',false).boundingBox();await page.mouse.move(r.x+r.width/2,r.y+r.height/2);await page.mouse.down();await page.waitForFunction(()=>f.currentJog?.jointId==='x');await page.mouse.up();await page.evaluate(()=>f.releaseStart());await page.waitForFunction(()=>f.view.fiveMachineJog?.ended);
 assert.equal(await page.evaluate(()=>f.calls.filter(c=>c.method==='CamSimulation.MoveMachine').length),moves);
 report.cases.push('integrated arrow hold retains Start/Stop stream lifecycle, release/leave/window blur and pending-Start race; no trailing click');
 await page.evaluate(()=>{f.view.fiveMachineSelectedPartId='j1';f.selectTool(f.view,'machine-instances');f.render();});assert.equal(await page.locator('.five-machine-jog-input').count(),0);assert.equal(await input('j1').getAttribute('type'),'number');assert.equal(await page.locator('[data-five-machine-field="lower:j1"]').count(),1);
 await page.evaluate(()=>{f.selectTool(f.view,'machine-simulation');f.render();});assert.equal(await page.locator('.five-machine-jog-input').count(),2);assert.equal(await page.locator('[data-five-machine-field="lower:j1"]').count(),0);
 const toggle=page.getByRole('switch',{name:'机床实例启用状态'});assert.equal(await toggle.getAttribute('aria-checked'),'true');await toggle.click();await idle();assert.equal(await toggle.getAttribute('aria-checked'),'false');assert.equal(await page.locator('[data-five-command="five.machine.mount"]').isDisabled(),true);assert.match(await page.locator('[data-five-machine-instance] option:checked').textContent(),/已禁用/);await toggle.click();await idle();assert.equal(await toggle.getAttribute('aria-checked'),'true');assert.equal(await page.locator('[data-five-command="five.machine.mount"]').isDisabled(),false);
 await page.evaluate(()=>{f.selectTool(f.view,'machine-instances');f.view.fiveMachineSelectedPartId='j1';f.render();});assert.equal(await toggle.count(),1);assert.equal(await toggle.isVisible(),true);await page.evaluate(()=>{f.selectTool(f.view,'machine-simulation');f.render();});report.cases.push('instance enable switch remains beside selector on both pages and while a part is selected; native toggle refreshes disabled label, mount availability and autosave');
 await page.evaluate(()=>document.activeElement?.blur());await page.locator('[data-five-key="machine-joints"]').scrollIntoViewIfNeeded();await page.screenshot({path:resolve(output,'spinner.png')});
 const selection=await page.evaluate(async()=>{
  const stored={machineId:'machine',supportId:'support-a',toolId:'tool-a',tcp:[160.25,0,0],beam:[1,0,0],placement:[1,0,0,34.5,0,1,0,0,0,0,1,366.75,0,0,0,1],toolCollisionRepresentation:'OwnMesh'};
  f.view.scene.domain.setups[0].machineSetup=stored;f.view.scene.machines.push({...structuredClone(f.native),entityId:'other'});f.render();await f.command('select:other');
  const s=f.view.fiveMachine,other={tcp:[...s.tcp],beam:[...s.beam],position:[...s.position],tool:s.toolId,support:s.supportId,dirty:s.dirty};await f.command('select:machine');
  return {other,bound:{tcp:s.tcp,beam:s.beam,position:s.position,tool:s.toolId,support:s.supportId,dirty:s.dirty},error:s.error};
 });assert.deepEqual(selection.other,{tcp:['0','0','0'],beam:['0','0','-1'],position:['0','0','0'],tool:'',support:'',dirty:true});assert.deepEqual(selection.bound,{tcp:['160.25','0','0'],beam:['1','0','0'],position:['34.5','0','366.75'],tool:'tool-a',support:'support-a',dirty:false});assert.equal(selection.error,'');report.cases.push('machine switch clears other-instance TCP/beam/placement and restores the bound machine configuration when returning');
 assert.deepEqual(errors,[]);report.passed=true;
}finally{writeFileSync(resolve(output,'report.json'),JSON.stringify({...report,errors},null,2));await browser.close();}
console.log(JSON.stringify(report));
