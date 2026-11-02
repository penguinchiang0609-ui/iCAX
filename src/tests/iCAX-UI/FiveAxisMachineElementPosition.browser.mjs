import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {browserReportDirectory,serveBrowserAsset} from './browserPackageRuntime.mjs';

const {chromium}=createRequire(import.meta.url)(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const output=browserReportDirectory('machine-element-position');
const browser=await chromium.launch({channel:process.env.ICAX_BROWSER_CHANNEL||'msedge',headless:true});
const report={passed:false,cases:[],nativeTransport:'controlled command fixture; actual production right panel, queue and DOM patch'},errors=[];
try{
 const page=await browser.newPage({viewport:{width:1200,height:850}});page.on('pageerror',e=>errors.push(e.message));
 await page.route('http://127.0.0.1:49197/**',serveBrowserAsset);await page.goto('http://127.0.0.1:49197/');
 await page.evaluate(async()=>{
  const ui=await import('/src/apps/five-axis-cam/webpage/machineSimulation.mjs');
  const queue=await import('/src/apps/five-axis-cam/webpage/machineParameterEdits.mjs');
  const {selectTool}=await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs');
  const {patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs');
  document.body.innerHTML='<style>body{margin:0;font:14px sans-serif}.layout{display:grid;grid-template-columns:260px 1fr 330px;height:750px}.cam-context-pane,.cam-info-pane{overflow:auto;border:1px solid #999;padding:8px}.nested{height:230px;overflow:auto}.nested-content{height:1200px}.spacer{height:1400px}input{width:110px}canvas{width:500px;height:700px}</style><div class="layout"><aside class="cam-context-pane"></aside><canvas></canvas><aside class="cam-info-pane"></aside></div>';
  const native={entityId:'machine',name:'Five-axis fixture',enabled:true,elements:[{entityId:'machine',kind:'machine',name:'Machine'},{entityId:'c',kind:'part',name:'C chuck',parentEntityId:'machine',restPosition:[0,0,0]},{entityId:'support',kind:'part',name:'Support',parentEntityId:'c',restPosition:[0,0,0]}],joints:[{entityId:'c',jointName:'C',type:'revolute',position:0,lower:-Math.PI,upper:Math.PI,homePosition:0,velocity:1,axis:[0,0,1]},{entityId:'support',jointName:'support_mount',type:'fixed',position:0,lower:0,upper:0,homePosition:0,velocity:1,axis:[0,0,1]}],tools:[],collisions:[]};
  const domain={expected:{epoch:'fixture',mutationSequence:'0'},activeSetupId:'setup',setups:[{id:'setup',name:'Setup',machineSetup:{}}],placements:[],workpieces:[],fixtures:[],fixtureUnits:[],intentToolpaths:[]};
  const view={fiveAxisArea:'simulation',fiveMachineSelectedPartId:'c',scene:{machines:[structuredClone(native)],machineDefinitions:[],domain,model:{isLoaded:false},intentToolpaths:[]},viewport:{sceneObjects:new Map(),renderer:{domElement:document.querySelector('canvas')}}};
  selectTool(view,'machine-instances');
  const context={mount:document.body,project:{projectId:'project'},projectProxy:{state:{projectPath:'C:/controlled-fixture/project.i5cam'},autosave:async()=>{fixture.saves++;return {};}},isCurrentProject:()=>true,actions:{refreshProjectHistoryControls:()=>{},refreshNativeProjectHistory:async()=>{fixture.historyReads++;}},sceneProxy:{invoke:async(method,request)=>{
   if(method==='Machine.SetElementPosition'){
    const gate={request:structuredClone(request)};fixture.requests.push(gate);if(fixture.hold)await new Promise(resolve=>gate.release=resolve);
    native.elements.find(e=>e.entityId===request.entityId).restPosition=[...request.position];domain.expected.mutationSequence=String(Number(domain.expected.mutationSequence)+1);return {};
   }
   if(method==='CamSimulation.MoveMachine'){
    fixture.motionRequests.push(structuredClone(request));const [id,position]=Object.entries(request.positions)[0],joint=native.joints.find(j=>j.entityId===id);
    if(position<joint.lower||position>joint.upper)return {status:'Rejected',collisionStatus:'Blocked',reason:'controlled native axis limit rejection'};
    joint.position=position;domain.expected.mutationSequence=String(Number(domain.expected.mutationSequence)+1);return {status:'Ready',collisionStatus:'Clear',positions:request.positions};
   }
   if(method==='CamScene.ReadState')return structuredClone(domain);
   if(method==='Machine.List')return {machines:[structuredClone(native)]};
   throw Error('Unexpected fixture method '+method);
  }}};
  const ops={renderProject:()=>render(),refreshSceneState:async()=>true,appendProjectLog:()=>{}};
  const nested=side=>`<div class="nested nested-${side}" data-five-key="nested-${side}"><div class="nested-content" data-five-key="nested-content-${side}"></div></div><div class="spacer" data-five-key="spacer-${side}"></div>`;
  function render(){patchWorkspacePanels(document.body,[['.cam-context-pane',ui.renderMachineLeft(view)+nested('left')],['.cam-info-pane',ui.renderMachineRight(view)+nested('right')]]);}
  window.fixture={ui,queue,view,context,ops,native,domain,render,requests:[],motionRequests:[],saves:0,historyReads:0,hold:true,listenerCalls:0};render();
  document.body.addEventListener('input',event=>{const key=event.target.dataset.fiveMachineField;if(key)ui.machineField(context,view,key,event.target.value,ops);});
  document.body.addEventListener('focusout',event=>{if(event.target.dataset.fiveMachineField)queue.finishMachineParameterField(view);});
  document.body.addEventListener('change',event=>{if(event.target.tagName==='SELECT'&&event.target.dataset.fiveMachineField){ui.machineField(context,view,event.target.dataset.fiveMachineField,event.target.value,ops);render();}});
  document.body.addEventListener('keydown',event=>{if(event.key==='Escape'){queue.cancelInvalidMachineParameters(view);render();}});
 });
 const field=axis=>page.locator(`[data-five-machine-field="origin:${axis}"]`);
 assert.equal(await page.locator('[data-five-key="machine-origin"] h3').textContent(),'轴心位置 · mm');assert.equal(await field('x').getAttribute('aria-label'),'轴心位置 X');
 assert.equal(await page.locator('[data-five-command*="core-edit"],[data-five-machine-field^="core:"]').count(),0);
 await page.evaluate(()=>{const f=fixture;f.view.fiveMachine.snapshot={old:true};f.view.fiveMachine.report={old:true};f.view.fiveMachine.program={old:true};});
 await field('x').fill('25');await page.waitForFunction(()=>fixture.requests.length===1);
 assert.equal(await page.evaluate(()=>fixture.view.fiveMachine.snapshot===null&&fixture.view.fiveMachine.report===null&&fixture.view.fiveMachine.program===null),true);
 await field('x').fill('35');
 const held=await page.evaluate(()=>{
  const input=document.querySelector('[data-five-machine-field="origin:x"]');input.focus({preventScroll:true});input.setSelectionRange(0,2);fixture.nodes={input,canvas:document.querySelector('canvas'),left:document.querySelector('.cam-context-pane'),right:document.querySelector('.cam-info-pane'),nestedLeft:document.querySelector('.nested-left'),nestedRight:document.querySelector('.nested-right')};input.addEventListener('audit-origin',()=>fixture.listenerCalls++);
  // These are newer positions, changed after the first request was sent.
  return Object.fromEntries(['left','right','nestedLeft','nestedRight'].map((key,i)=>{fixture.nodes[key].scrollTop=80+i*25;return [key,fixture.nodes[key].scrollTop];}));
 });assert.ok(Object.values(held).every(v=>v>0));
 await page.evaluate(()=>fixture.requests[0].release());await page.waitForFunction(()=>fixture.requests.length===2);
 const intermediate=await page.evaluate(()=>{const n=fixture.nodes,input=document.querySelector('[data-five-machine-field="origin:x"]');input.dispatchEvent(new Event('audit-origin'));return {value:input.value,same:input===n.input&&document.querySelector('canvas')===n.canvas&&document.querySelector('.cam-context-pane')===n.left&&document.querySelector('.cam-info-pane')===n.right,focused:document.activeElement===input,selection:[input.selectionStart,input.selectionEnd],scroll:Object.fromEntries(['left','right','nestedLeft','nestedRight'].map(key=>[key,n[key].scrollTop])),listenerCalls:fixture.listenerCalls};});
 assert.equal(intermediate.value,'35');assert.equal(intermediate.same,true);assert.equal(intermediate.focused,true);assert.deepEqual(intermediate.selection,[0,2]);assert.deepEqual(intermediate.scroll,held);assert.equal(intermediate.listenerCalls,1);
 await page.evaluate(()=>{fixture.hold=false;fixture.requests[1].release();});await page.waitForFunction(()=>!fixture.view.fiveMachineParameterEdits.request&&!fixture.view.fiveMachineParameterEdits.entries.size&&!fixture.view.fiveMachineParameterEdits.fileSaving);
 const saved=await page.evaluate(()=>({requests:fixture.requests.map(r=>r.request),position:fixture.native.elements.find(e=>e.entityId==='c').restPosition,joint:fixture.native.joints[0],saves:fixture.saves,history:fixture.historyReads}));
 assert.deepEqual(saved.requests,[{entityId:'c',position:[25,0,0]},{entityId:'c',position:[35,0,0]}]);assert.deepEqual(saved.position,[35,0,0]);assert.deepEqual(saved.joint.axis,[0,0,1]);assert.ok(saved.saves>0&&saved.history>0);
 report.cases.push('rotary centre exposes only XYZ positions; old reply preserves newer text, canvas/input/listener identity, focus/selection and latest left/right/nested scroll; native queue snapshots commit in order and autosave');
 await page.evaluate(()=>{fixture.view.fiveMachineSelectedPartId='support';fixture.render();});assert.equal(await page.locator('[data-five-key="machine-origin"] h3').textContent(),'安装位置 · mm');await field('z').fill('8.5');await field('z').press('Tab');await page.waitForFunction(()=>fixture.native.elements.find(e=>e.entityId==='support').restPosition[2]===8.5&&!fixture.view.fiveMachineParameterEdits.entries.size);
 report.cases.push('fixed support exposes installation XYZ and submits translation without changing joint definition');
 const count=await page.evaluate(()=>fixture.requests.length);await field('x').fill('-');await page.waitForFunction(()=>!![...fixture.view.fiveMachineParameterEdits.entries.values()][0]?.error);await field('x').press('Escape');assert.equal(await page.evaluate(()=>fixture.requests.length),count);assert.equal(await page.evaluate(()=>fixture.view.fiveMachineParameterEdits.entries.size),0);assert.equal(await field('x').inputValue(),'0');
 report.cases.push('invalid position consumes no native edit; Escape clears origin draft and restores saved position');
 const idle=()=>page.waitForFunction(()=>!fixture.view.fiveMachineParameterEdits.request&&!fixture.view.fiveMachineParameterEdits.entries.size&&!fixture.view.fiveMachineParameterEdits.fileSaving);
 await page.evaluate(()=>{fixture.view.fiveMachineSelectedPartId='c';fixture.render();});const joint=page.locator('[data-five-machine-field="joint:c"]');
 assert.equal(await joint.getAttribute('type'),'number');assert.equal(await joint.getAttribute('step'),'1');assert.equal(await page.locator('[data-five-command^="five.machine.jog-minus:"],[data-five-command^="five.machine.jog-plus:"]').count(),0);assert.equal(await field('x').getAttribute('type'),'text');
 assert.equal(await page.locator('[data-five-machine-field="lower:c"]').count(),1);assert.equal(await page.locator('[data-five-machine-field="upper:c"]').count(),1);
 await joint.fill('85.21');await joint.press('ArrowUp');assert.equal(Number(await joint.inputValue()),86.21);await idle();
 await joint.press('ArrowDown');assert.equal(Number(await joint.inputValue()),85.21);await idle();
 await joint.fill('');await joint.pressSequentially('85.21');assert.equal(Number(await joint.inputValue()),85.21);await idle();
 const arrow=async up=>{await joint.hover();const box=await joint.boundingBox();await joint.click({position:{x:box.width-12,y:box.height*(up?.3:.7)}});};
 await arrow(true);assert.equal(Number(await joint.inputValue()),86.21);await idle();await arrow(false);assert.equal(Number(await joint.inputValue()),85.21);await idle();
 await page.locator('[data-five-machine-field="jogStep"]').selectOption('0.1');assert.equal(await joint.getAttribute('step'),'0.1');await joint.press('ArrowUp');assert.equal(Number(await joint.inputValue()),85.31);await idle();await joint.press('ArrowDown');assert.equal(Number(await joint.inputValue()),85.21);await idle();
 const accepted=await page.evaluate(()=>fixture.native.joints[0].position);await joint.fill('999');await page.waitForFunction(()=>!![...fixture.view.fiveMachineParameterEdits.entries.values()][0]?.error);assert.equal(await page.evaluate(()=>fixture.native.joints[0].position),accepted);await joint.fill('85.21');await idle();
 const motion=await page.evaluate(()=>fixture.motionRequests.map(r=>r.positions.c*180/Math.PI));assert.ok(motion.some(v=>Math.abs(v-86.21)<1e-9));assert.ok(motion.some(v=>Math.abs(v-85.31)<1e-9));
 report.cases.push('native number spinner arrows and keyboard retain fractional values (85.21 +/- 1 and +/- 0.1), allow decimal typing, reuse checked native queue/autosave, retain limits and reject out-of-range motion');report.motionDegrees=motion;
 assert.deepEqual(errors,[]);report.passed=true;report.saved=saved;await page.screenshot({path:resolve(output,'machine-element-position.png')});
}finally{writeFileSync(resolve(output,'machine-element-position-report.json'),JSON.stringify({...report,errors},null,2));await browser.close();}
console.log(JSON.stringify(report));
