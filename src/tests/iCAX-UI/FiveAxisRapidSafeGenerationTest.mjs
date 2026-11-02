import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {serveBrowserAsset} from './browserPackageRuntime.mjs';
import {openRapid,rapidCommand,rapidPreview,renderRapid,renderRapidTree,generateRapids,rapidUnifiedCollisionClear} from '../../apps/five-axis-cam/webpage/rapidPlanning.mjs';
import {renderGeneration} from '../../apps/five-axis-cam/webpage/toolpathGeneration.mjs';
import {machiningTaskTimeoutMs} from '../../apps/five-axis-cam/webpage/operationTiming.mjs';

const policy={strategy:'Quick',marginMm:.2,liftHeightMm:10};
const route=(from,to,offset=0)=>({from,to,fromPlacementId:'placement',toPlacementId:'placement',endpointStatus:'Current',inputKey:`${from}/${to}-original`,policy:structuredClone(policy),strategy:'Quick',geometryStatus:'Generated',validation:'Unchecked',planningMethod:'Automatic',frame:'Setup',referencePoint:'ToolTip',points:[[offset,0,1],[offset+1,0,3],[offset+2,0,1]],directions:[[0,0,1],[0,0,1],[0,0,1]]});
function deferred(){let resolve;const promise=new Promise(done=>resolve=done);return {promise,resolve};}
async function until(test){for(let i=0;i<100;++i){if(test())return;await new Promise(setImmediate);}assert.fail('operation did not reach the expected state');}
function fixture({manual=true,candidateStatus='Clear',holdPreview=false,holdApply=false}={}){
 const first=route('a','b'),second=route('b','c',10);
 if(manual){first.manualPath={points:[[1,2,3]],directions:[[0,0,1]]};first.points[1]=[1,2,3];first.planningMethod='Manual';}
 const native={expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'setup',jobId:'job',workpieces:[],placements:[],fixtures:[],intentToolpaths:[],setups:[{id:'setup',rapidSettings:{...policy,followHeightMm:1,collisionThresholdMm:.1},generatedGeometry:{status:'Current',revision:'geometry-1',operations:['a','b','c'].map((intentId,index)=>({intentId,order:index+1})),transitions:[first,second],rapidClearCount:'0'}}]};
 const view={fiveAxisArea:'toolpath',fiveCamPage:'rapid',fiveAxisUI:{tools:{toolpath:'rapid'}},scene:{domain:structuredClone(native)},pending:false};
 const calls=[],logs=[],previewGate=deferred(),applyGate=deferred(),candidate={...route('a','b'),points:[[0,0,1],[1,8,9],[2,0,1]],validation:candidateStatus,geometryStatus:candidateStatus==='Collision'?'Failed':'Generated',reason:candidateStatus==='Clear'?'':'RAPID_NO_FEASIBLE_ROUTE_WITHIN_PARAMETERS'},record={policy:structuredClone(policy),inputKey:'automatic-checked',result:candidate};
 let saved=0;
 const context={isCurrentProject:()=>true,projectProxy:{state:{projectPath:'C:/tests/safe-rapid.i5cam'},async autosave(){saved++;return {saved:true};}},sceneProxy:{async invoke(method,payload,options){
  calls.push({method,payload:structuredClone(payload),options:structuredClone(options)});
  if(method==='CamScene.ReadState')return structuredClone(native);
  if(method==='CamRapid.Preview'){if(holdPreview)await previewGate.promise;return {status:'Completed',epoch:'epoch',taskId:'safe-preview',result:{connections:{'a/b':structuredClone(record)}}};}
  if(method==='CamRapid.Apply'){
   assert.equal(payload.taskId,'safe-preview');assert.equal(candidateStatus,'Clear');if(holdApply)await applyGate.promise;
   native.setups[0].generatedGeometry.transitions[0]={...native.setups[0].generatedGeometry.transitions[0],...structuredClone(record.result),inputKey:record.inputKey};delete native.setups[0].generatedGeometry.transitions[0].manualPath;
   native.expected.mutationSequence=String(Number(native.expected.mutationSequence)+1);return {changed:true};
  }
  if(method==='CamRapid.Generate')return {status:'Completed',epoch:'epoch',taskId:'batch',result:{count:'2',generatedCount:'2',calculatedCount:'2',reusedCount:'0'}};
  throw Error(`unexpected native mutation ${method}`);
 }}};
 const ops={renderProject(){},appendProjectLog(_context,level,message){logs.push({level,message});},refreshSceneState(){assert.fail('rapid checks must preserve the existing scene');}};
 openRapid(view);return {native,view,context,ops,calls,logs,previewGate,applyGate,candidate,saves:()=>saved};
}

test('every selected generated, unchecked, failed or manual rapid offers one safe action with explicit busy state',()=>{
 const f=fixture();
 for(const status of ['Generated','Draft','Failed']){
  f.view.scene.domain.setups[0].generatedGeometry.transitions[0].geometryStatus=status;
  const markup=renderRapid(f.view,true),button=markup.match(/<button[^>]*data-five-command="five.rapid.safe-generate"[^>]*>[^<]*<\/button>/g);
  assert.equal(button.length,1);assert.match(button[0],/>自动生成安全空移</);assert.doesNotMatch(button[0],/disabled/);
 }
 f.view.fiveRapid.running=true;assert.match(renderRapid(f.view,true),/data-five-command="five.rapid.safe-generate"[^>]*disabled/);
});

test('a failed automatic search keeps the saved manual route, editor and every other segment unchanged',async()=>{
 const f=fixture({candidateStatus:'Collision',holdPreview:true}),state=f.view.fiveRapid,t=f.view.scene.domain.setups[0].generatedGeometry.transitions[0];
 state.pathEdit={points:structuredClone(t.points),directions:structuredClone(t.directions),values:t.points[1].map(String),selected:1,selectedPoints:[1],changed:false,revision:2,savedRevision:2};
 const editor=state.pathEdit,before=structuredClone(f.native),display=structuredClone(rapidPreview(f.view,f.view.scene.domain.setups[0].generatedGeometry));
 const work=rapidCommand(f.context,f.view,'safe-generate',f.ops);await until(()=>f.calls.some(c=>c.method==='CamRapid.Preview'));
 assert.deepEqual(rapidPreview(f.view,f.view.scene.domain.setups[0].generatedGeometry),display);assert.equal(state.pathEdit,editor);
 f.previewGate.resolve();await work;
 assert.deepEqual(f.native,before);assert.deepEqual(rapidPreview(f.view,f.view.scene.domain.setups[0].generatedGeometry),display);assert.equal(state.pathEdit,editor);assert.equal(state.preview,undefined);
 assert.match(f.view.error,/原空移路径已保留/);assert.equal(f.calls.some(c=>c.method==='CamRapid.Apply'||c.method==='CamRapid.EditPolicy'||c.method==='CamRapid.Generate'),false);assert.equal(f.saves(),0);
 const previewCall=f.calls.find(c=>c.method==='CamRapid.Preview'),payload=previewCall.payload;assert.equal(payload.from,'a');assert.equal(payload.to,'b');assert.equal(Object.hasOwn(payload,'manualPath'),false);assert.equal(previewCall.options.timeoutMs,machiningTaskTimeoutMs,'cold collision preparation needs the machining timeout rather than the short default request timeout');assert.ok(machiningTaskTimeoutMs>15000);
});

test('a successful automatic search replaces only the selected route after Apply acknowledgment and removes its manual override',async()=>{
 const f=fixture({holdApply:true}),before=structuredClone(f.native),state=f.view.fiveRapid,original=structuredClone(f.view.scene.domain.setups[0].generatedGeometry);
 const work=rapidCommand(f.context,f.view,'safe-generate',f.ops);await until(()=>f.calls.some(c=>c.method==='CamRapid.Apply'));
 assert.deepEqual(f.native,before);assert.deepEqual(rapidPreview(f.view,f.view.scene.domain.setups[0].generatedGeometry),original,'the checked candidate does not replace the displayed route before commit');
 f.applyGate.resolve();await work;
 const actual=f.native.setups[0].generatedGeometry.transitions;assert.equal(actual[0].validation,'Clear');assert.equal(actual[0].geometryStatus,'Generated');assert.equal(Object.hasOwn(actual[0],'manualPath'),false);assert.deepEqual(actual[0].points,f.candidate.points);assert.deepEqual(actual[1],before.setups[0].generatedGeometry.transitions[1]);
 assert.equal(f.calls.filter(c=>c.method==='CamRapid.Preview').length,1);assert.equal(f.calls.filter(c=>c.method==='CamRapid.Apply').length,1);assert.equal(f.calls.some(c=>['CamRapid.Generate','CamRapid.EditPolicy'].includes(c.method)),false);assert.equal(f.saves(),1);assert.equal(f.view.error,'');assert.equal(state.safeCommitPending,false);
});

test('Generated without Clear cannot be applied as a safe replacement',async()=>{
 const f=fixture({candidateStatus:'Unchecked'}),before=structuredClone(f.native);await rapidCommand(f.context,f.view,'safe-generate',f.ops);
 assert.deepEqual(f.native,before);assert.equal(f.calls.some(c=>c.method==='CamRapid.Apply'),false);assert.match(f.view.error,/原空移路径已保留/);
});

test('batch generation describes geometry as awaiting collision checks and does not invent clearance',async()=>{
 const f=fixture({manual:false});await generateRapids(f.context,f.view,f.ops);
 assert.match(f.logs.at(-1).message,/已生成，待碰撞检测/);assert.doesNotMatch(f.logs.at(-1).message,/通过避障/);
 assert.match(renderGeneration(f.view),/已生成，待碰撞检测/);assert.match(renderRapidTree(f.view),/已生成，待碰撞检测/);assert.doesNotMatch(renderRapidTree(f.view),/已避障|检测通过/);
 assert.equal(f.view.scene.domain.setups[0].generatedGeometry.transitions[0].validation,'Unchecked');
 f.view.scene.domain.setups[0].generatedGeometry.transitions[0].reason='CAM_RAPID_COLLISION_CHECK_REQUIRED';assert.doesNotMatch(renderRapid(f.view,true),/CAM_RAPID_COLLISION_CHECK_REQUIRED/);
 const setup=f.view.scene.domain.setups[0];setup.simulationCheck={validity:'Current',status:'Clear',collisionCount:'0',uncheckedCount:'0',frames:[{role:'Rapid',intentId:'b'},{role:'Rapid',intentId:'c'}]};
 assert.equal(rapidUnifiedCollisionClear(f.view),true);assert.match(renderGeneration(f.view),/统一碰撞检测通过/);assert.match(renderRapidTree(f.view),/统一碰撞检测通过/);
 setup.simulationCheck.status='Collision';setup.simulationCheck.collisionCount='1';assert.equal(rapidUnifiedCollisionClear(f.view),false);assert.doesNotMatch(renderRapidTree(f.view),/统一碰撞检测通过/);assert.match(renderGeneration(f.view),/统一碰撞检测发现干涉/);assert.match(renderRapidTree(f.view),/尚未单独检测/);
 setup.simulationCheck.status='Clear';setup.simulationCheck.collisionCount='0';setup.simulationCheck.frames.pop();assert.equal(rapidUnifiedCollisionClear(f.view),false,'a report missing a rapid cannot qualify all routes');
});

test('actual browser safe generation retains input and canvas nodes, selection, listeners and latest dual and nested scrolling on success and failure',async()=>{
 const require=createRequire(import.meta.url),{chromium}=require(process.env.ICAX_PLAYWRIGHT_MODULE||'C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
 const browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'msedge'});
 try{
  const page=await browser.newPage({viewport:{width:1300,height:850}});await page.route('http://127.0.0.1:19365/**',request=>new URL(request.request().url()).pathname==='/'?request.fulfill({contentType:'text/html',body:'<style>body{margin:0}#mount{display:grid;grid-template-columns:270px 500px 480px;gap:12px}.left,.right{height:420px;overflow:auto}.spacer{height:1500px}.five-rapid-body{height:240px;overflow:auto}.five-rapid-body::after{content:"";display:block;height:900px}input{width:90px}canvas{width:500px;height:420px}</style><main id="mount"><aside class="left cam-context-pane"></aside><canvas></canvas><aside class="right cam-info-pane"></aside></main>'}):serveBrowserAsset(request));
  for(const candidateStatus of ['Collision','Clear']){
   await page.goto('http://127.0.0.1:19365/');
   await page.evaluate(async({fixtureSource,routeSource,deferredSource,policy,candidateStatus})=>{
    const planning=await import('/src/apps/five-axis-cam/webpage/rapidPlanning.mjs'),{patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs');
    const assertions={equal(a,b,message){if(a!==b)throw Error(message||`${a} differs from ${b}`);},fail(message){throw Error(message);}};
    const makeRoute=new Function('policy',`return (${routeSource})`)(policy),makeDeferred=new Function(`return (${deferredSource})`)();
    const makeFixture=new Function('openRapid','route','policy','deferred','assert',`return (${fixtureSource})`)(planning.openRapid,makeRoute,policy,makeDeferred,assertions),f=window.safeFixture=makeFixture({manual:candidateStatus==='Collision',candidateStatus,holdPreview:true,holdApply:true});
    const mount=document.querySelector('#mount');f.render=()=>patchWorkspacePanels(mount,[['.left',`<label data-five-key="left-label">查找连接<input id="left-input" value="123456"></label>${planning.renderRapidTree(f.view)}<div class="spacer" data-five-key="left-spacer"></div>`],['.right',planning.renderRapid(f.view,true)+'<div class="spacer" data-five-key="right-spacer"></div>']]);
    f.ops.renderProject=f.render;f.render();mount.addEventListener('click',event=>{const button=event.target.closest('[data-five-command="five.rapid.safe-generate"]');if(button&&!button.disabled)f.work=planning.rapidCommand(f.context,f.view,'safe-generate',f.ops);});
    f.inputSelector=candidateStatus==='Collision'?'#left-input':'[data-five-rapid-field="marginMm"]';f.original=JSON.stringify(f.native);f.originalSecond=JSON.stringify(f.native.setups[0].generatedGeometry.transitions[1]);
   },{fixtureSource:fixture.toString(),routeSource:route.toString(),deferredSource:deferred.toString(),policy,candidateStatus});
   await page.locator('[data-five-command="five.rapid.safe-generate"]').click();await page.waitForFunction(()=>safeFixture.calls.some(c=>c.method==='CamRapid.Preview'));
   const before=await page.evaluate(()=>{
    const f=safeFixture;f.input=document.querySelector(f.inputSelector);f.canvas=document.querySelector('canvas');f.inputEvents=0;f.input.addEventListener('input',()=>f.inputEvents++);f.input.focus({preventScroll:true});f.input.setSelectionRange(0,2);
    const selectors=['.left','.right','.five-rapid-body'];selectors.forEach((s,i)=>document.querySelector(s).scrollTop=60+i*20);selectors.forEach((s,i)=>document.querySelector(s).scrollTop=90+i*30);
    return {scroll:selectors.map(s=>document.querySelector(s).scrollTop),selection:[f.input.selectionStart,f.input.selectionEnd]};
   });
   assert.ok(before.scroll.every(value=>value>0));await page.evaluate(()=>safeFixture.previewGate.resolve());
   if(candidateStatus==='Clear'){
    await page.waitForFunction(()=>safeFixture.calls.some(c=>c.method==='CamRapid.Apply'));
    assert.equal(await page.evaluate(()=>JSON.stringify(safeFixture.native)===safeFixture.original),true);await page.evaluate(()=>safeFixture.applyGate.resolve());
   }
   await page.evaluate(()=>safeFixture.work);
   const after=await page.evaluate(()=>{const f=safeFixture;f.input.dispatchEvent(new Event('input'));return {input:f.input===document.querySelector(f.inputSelector),canvas:f.canvas===document.querySelector('canvas'),focused:document.activeElement===f.input,selection:[f.input.selectionStart,f.input.selectionEnd],scroll:['.left','.right','.five-rapid-body'].map(s=>document.querySelector(s).scrollTop),events:f.inputEvents,unchanged:JSON.stringify(f.native)===f.original,other:JSON.stringify(f.native.setups[0].generatedGeometry.transitions[1])===f.originalSecond,validation:f.native.setups[0].generatedGeometry.transitions[0].validation};});
   assert.ok(after.input&&after.canvas&&after.focused,JSON.stringify({candidateStatus,before,after}));assert.deepEqual(after.selection,before.selection);assert.deepEqual(after.scroll,before.scroll);assert.equal(after.events,1);assert.equal(after.other,true);
   if(candidateStatus==='Collision')assert.equal(after.unchanged,true);else assert.equal(after.validation,'Clear');
  }
 }finally{await browser.close();}
});
