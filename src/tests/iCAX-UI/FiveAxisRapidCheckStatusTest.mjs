import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {serveBrowserAsset} from './browserPackageRuntime.mjs';
import {openRapid,rapidPick,rapidFieldChanged,rapidCommand,rapidPreview,renderRapid,renderRapidTree,renderRapidParameters,rapidError} from '../../apps/five-axis-cam/webpage/rapidPlanning.mjs';
import * as collision from '../../apps/five-axis-cam/webpage/rapidCollisionCheck.mjs';
import {rapidDependencyKey} from '../../apps/five-axis-cam/webpage/rapidDependencies.mjs';

const policy={strategy:'Quick',marginMm:.2,liftHeightMm:10};
const savedReason='手动路径已保存，尚未通过碰撞检查。';
function deferred(){let resolve;const promise=new Promise(done=>resolve=done);return {promise,resolve};}
function route(from,to,offset=0){
 return {from,to,fromPlacementId:'placement',toPlacementId:'placement',endpointStatus:'Current',inputKey:`${from}/${to}-saved`,policy:structuredClone(policy),strategy:'Quick',geometryStatus:'Draft',validation:'Unchecked',reason:savedReason,planningMethod:'Manual',frame:'Setup',referencePoint:'ToolTip',lengthMm:12,
  points:[[offset,0,1],[offset+1,2,3],[offset+2,0,1]],directions:[[0,0,1],[0,0,1],[0,0,1]],manualPath:{points:[[offset+1,2,3]],directions:[[0,0,1]]}};
}
function fixture({status='Clear',hold=false}={}){
 const native={expected:{epoch:'check-status',mutationSequence:'1'},activeSetupId:'setup',jobId:'job',workpieces:[],placements:[],fixtures:[],intentToolpaths:[],setups:[{id:'setup',rapidSettings:{...policy,followHeightMm:1,collisionThresholdMm:.1},generatedGeometry:{status:'Current',revision:'geometry-1',operations:['a','b','c'].map((intentId,index)=>({intentId,order:index+1})),transitions:[route('a','b'),route('b','c',10)],rapidClearCount:'0'}}]};
 const view={fiveAxisArea:'toolpath',fiveCamPage:'rapid',fiveAxisUI:{tools:{toolpath:'rapid'}},scene:{domain:structuredClone(native)},pending:false};
 const calls=[],gate=deferred();let saves=0;
 const context={isCurrentProject:()=>true,projectProxy:{state:{projectPath:'C:/tests/rapid-check-status.i5cam'},async autosave(){saves++;throw Error('A collision check must not save the project.');}},sceneProxy:{async invoke(method,payload){
  calls.push({method,payload:structuredClone(payload)});
  if(method!=='CamRapid.CheckPath')throw Error(`A collision check must not invoke ${method}.`);
  if(hold)await gate.promise;
  const result={geometryStatus:status==='Clear'?'Generated':'Failed',validation:status,reason:status==='Clear'?'':'RAPID_MANUAL_PATH_BLOCKED',obstacleId:status==='Clear'?'':'fixture',candidateCount:'1',certifiedClearanceMm:status==='Clear'?.8:0,
   // A check is evidence about the displayed source, not a geometry update.
   // Different returned sampling must never replace authored points or source.
   points:[[999,999,999],[1000,1000,1000]],directions:[[0,1,0],[0,1,0]],planningMethod:'Manual'};
  return {status:'Completed',epoch:'check-status',taskId:'check-task',result:{connections:{[`${payload.from}/${payload.to}`]:{result}}}};
 }}};
 const ops={renderProject(){},appendProjectLog(){},refreshSceneState(){throw Error('A check must not recreate the scene.');}};
 openRapid(view);return {native,view,context,ops,calls,gate,saves:()=>saves};
}
const geometry=f=>f.view.scene.domain.setups[0].generatedGeometry;
const first=f=>geometry(f).transitions[0];
const selectedRow=(html,id='a/b')=>html.match(new RegExp(`<button[^>]*data-five-rapid-id="${id}"[^>]*>[\\s\\S]*?<\\/button>`))?.[0]||'';
async function runCheck(f){await collision.checkRapidCollision(f.context,f.view,f.ops,rapidError);}

test('a read-only Clear check updates tree, parameters, editor status and canvas display without replacing the saved manual source',async()=>{
 const f=fixture(),before=structuredClone(f.view.scene.domain),source=structuredClone(first(f));
 assert.match(selectedRow(renderRapidTree(f.view)),/待碰撞检测/);
 await runCheck(f);
 assert.deepEqual(f.calls.map(call=>call.method),['CamRapid.CheckPath']);assert.equal(f.saves(),0);
 assert.deepEqual(f.calls[0].payload.manualPath,source.manualPath);assert.deepEqual(f.view.scene.domain,before);assert.deepEqual(f.native,before);
 const displayed=collision.rapidCollisionDisplay(f.view,first(f));
 assert.equal(displayed.validation,'Clear');assert.equal(displayed.geometryStatus,'Generated');assert.ok(!displayed.reason);assert.ok(!displayed.obstacleId);
 assert.deepEqual(displayed.points,source.points);assert.deepEqual(displayed.directions,source.directions);assert.deepEqual(displayed.manualPath,source.manualPath);
 assert.equal(first(f).geometryStatus,'Draft');assert.equal(first(f).validation,'Unchecked');assert.equal(first(f).reason,savedReason);
 assert.match(selectedRow(renderRapidTree(f.view)),/检测通过/);assert.match(renderRapidParameters(f.view),/检测通过/);
 const panel=renderRapid(f.view,true);assert.match(panel,/当前刀头模型检测通过/);assert.doesNotMatch(panel,/手动路径已保存，尚未通过碰撞检查|待碰撞检测/);
 const canvas=rapidPreview(f.view,geometry(f));assert.equal(canvas.transitions[0].validation,'Clear');assert.equal(canvas.transitions[0].geometryStatus,'Generated');assert.ok(!canvas.transitions[0].reason);
 assert.deepEqual(canvas.transitions[0].points,source.points);assert.deepEqual(canvas.transitions[0].manualPath,source.manualPath);assert.deepEqual(canvas.transitions[1],before.setups[0].generatedGeometry.transitions[1]);
});

test('a blocked check uses the same failure in every display while preserving the authored route',async()=>{
 const f=fixture({status:'Unchecked'}),before=structuredClone(f.view.scene.domain);await runCheck(f);
 const displayed=collision.rapidCollisionDisplay(f.view,first(f));assert.equal(displayed.validation,'Unchecked');assert.equal(displayed.geometryStatus,'Failed');assert.equal(displayed.reason,'RAPID_MANUAL_PATH_BLOCKED');
 assert.deepEqual(displayed.manualPath,first(f).manualPath);assert.deepEqual(displayed.points,first(f).points);
 assert.match(selectedRow(renderRapidTree(f.view)),/未通过|检测到碰撞/);assert.match(renderRapidParameters(f.view),/未通过|检测到碰撞/);
 assert.match(renderRapid(f.view,true),/当前段未通过检测/);assert.match(renderRapid(f.view,true),/手动路径未通过碰撞检查/);assert.doesNotMatch(renderRapid(f.view,true),/手动路径已保存，尚未通过碰撞检查/);
 assert.doesNotMatch(selectedRow(renderRapidTree(f.view)),/候选路径/);assert.doesNotMatch(renderRapidParameters(f.view),/候选路径/);assert.doesNotMatch(renderRapid(f.view,true),/候选路径/);
 const canvas=rapidPreview(f.view,geometry(f));assert.equal(canvas.transitions[0].reason,displayed.reason);assert.equal(canvas.transitions[0].geometryStatus,'Failed');assert.deepEqual(f.view.scene.domain,before);
});

test('per-route evidence survives selection changes and is discarded when its native dependency key changes',async()=>{
 const f=fixture();await runCheck(f);assert.ok(collision.storedRapidCollisionCheck(f.view,first(f)));
 rapidPick(f.view,'b/c');assert.equal(collision.rapidCollisionDisplay(f.view,geometry(f).transitions[1]).validation,'Unchecked');
 assert.match(selectedRow(renderRapidTree(f.view)),/检测通过/);assert.match(renderRapidParameters(f.view),/待碰撞检测/);assert.doesNotMatch(renderRapid(f.view,true),/当前刀头模型检测通过/);
 rapidPick(f.view,'a/b');assert.match(renderRapidParameters(f.view),/检测通过/);assert.match(renderRapid(f.view,true),/当前刀头模型检测通过/);
 first(f).inputKey='changed-head-fixture-or-endpoints';
 assert.equal(collision.storedRapidCollisionCheck(f.view,first(f)),null);assert.equal(collision.rapidCollisionDisplay(f.view,first(f)).validation,'Unchecked');
 assert.match(selectedRow(renderRapidTree(f.view)),/待碰撞检测/);assert.match(renderRapidParameters(f.view),/待碰撞检测/);assert.doesNotMatch(renderRapid(f.view,true),/当前刀头模型检测通过/);
 assert.equal(rapidPreview(f.view,geometry(f)).transitions[0].validation,'Unchecked');
});

test('opening global rapid settings preserves explicit route dependencies and its checked tree and canvas status',async()=>{
 const f=fixture();await runCheck(f);const before=structuredClone(f.view.scene.domain),routeKey=rapidDependencyKey(f.view,'a/b');
 openRapid(f.view,true);
 assert.equal(f.view.fiveRapid.settings,true);assert.equal(rapidDependencyKey(f.view,'a/b'),routeKey);assert.notEqual(rapidDependencyKey(f.view),routeKey);
 assert.equal(rapidDependencyKey(f.view),JSON.stringify([before.expected.epoch,'setup',before.setups[0].rapidSettings]));
 assert.ok(collision.storedRapidCollisionCheck(f.view,first(f)));assert.match(selectedRow(renderRapidTree(f.view)),/检测通过/);
 const canvas=rapidPreview(f.view,geometry(f));assert.equal(canvas.transitions[0].validation,'Clear');assert.equal(canvas.transitions[0].geometryStatus,'Generated');
 assert.deepEqual(canvas.transitions[0].points,first(f).points);assert.deepEqual(canvas.transitions[0].manualPath,first(f).manualPath);
 assert.deepEqual(f.view.scene.domain,before);assert.deepEqual(f.calls.map(call=>call.method),['CamRapid.CheckPath']);assert.equal(f.saves(),0);
});

test('editing a manual draft cannot inherit a previous Clear verdict or alter its cached geometry',async()=>{
 const f=fixture();await runCheck(f);const original=structuredClone(first(f));
 await rapidCommand(f.context,f.view,'path-edit',f.ops);rapidFieldChanged(f.view,'pathY','8');
 assert.ok(f.view.fiveRapid.pathEdit.changed);assert.notEqual(collision.rapidCollisionDisplay(f.view,first(f)).validation,'Clear');
 assert.doesNotMatch(selectedRow(renderRapidTree(f.view)),/检测通过/);assert.doesNotMatch(renderRapidParameters(f.view),/检测通过/);assert.doesNotMatch(renderRapid(f.view,true),/当前刀头模型检测通过/);
 const canvas=rapidPreview(f.view,geometry(f));assert.notEqual(canvas.transitions[0].validation,'Clear');assert.equal(canvas.transitions[0].points[1][1],8);
 assert.deepEqual(first(f),original);assert.deepEqual(f.calls.map(call=>call.method),['CamRapid.CheckPath']);assert.equal(f.saves(),0);
});

test('late Clear responses cannot qualify another route, a changed epoch, a changed source or a changed policy',async()=>{
 for(const change of ['selection','epoch','source','policy']){
  const f=fixture({hold:true}),work=runCheck(f);assert.equal(f.calls.length,1);
  if(change==='selection')rapidPick(f.view,'b/c');
  if(change==='epoch')f.view.scene.domain.expected.epoch='another-open-project';
  if(change==='source')first(f).inputKey='changed-while-checking';
  if(change==='policy')rapidFieldChanged(f.view,'marginMm','.3');
  f.gate.resolve();await work;
  assert.equal(collision.storedRapidCollisionCheck(f.view,first(f)),null,change);
  assert.notEqual(collision.rapidCollisionDisplay(f.view,first(f)).validation,'Clear',change);
  assert.doesNotMatch(selectedRow(renderRapidTree(f.view)),/检测通过/,change);assert.doesNotMatch(renderRapid(f.view,true),/当前刀头模型检测通过/,change);
  assert.deepEqual(f.calls.map(call=>call.method),['CamRapid.CheckPath'],change);assert.equal(f.saves(),0);
 }
});

test('collision-only inputs invalidate evidence while keeping the geometry key and saved route',async()=>{
 for(const change of ['head','head-revision','head-box','threshold','fixture']){
  const f=fixture(),setup=f.view.scene.domain.setups[0];setup.headInstanceId='head-a';setup.headModel={instance:{revision:'1'},collisionModel:{boxes:[{halfExtents:[1,1,1]}]}};
  await runCheck(f);const original=structuredClone(first(f)),geometryKey=rapidDependencyKey(f.view);assert.equal(collision.rapidCollisionDisplay(f.view,first(f)).validation,'Clear');
  if(change==='head')setup.headInstanceId='head-b';
  if(change==='head-revision')setup.headModel.instance.revision='2';
  if(change==='head-box')setup.headModel.collisionModel.boxes[0].halfExtents[0]=2;
  if(change==='threshold')setup.rapidSettings.collisionThresholdMm=.3;
  if(change==='fixture')f.view.scene.domain.fixtures.push({id:'fixture',geometryRef:{url:'fixture',version:'2'},enabled:true,collisionEnabled:true});
  assert.equal(rapidDependencyKey(f.view),geometryKey,change);assert.deepEqual(first(f),original,change);assert.equal(collision.storedRapidCollisionCheck(f.view,first(f)),null,change);assert.notEqual(collision.rapidCollisionDisplay(f.view,first(f)).validation,'Clear',change);
 }
});

test('a late Clear collision response is discarded when only the head changes',async()=>{
 const f=fixture({hold:true}),geometryKey=rapidDependencyKey(f.view),original=structuredClone(first(f)),work=runCheck(f);f.view.scene.domain.setups[0].headInstanceId='changed-head';f.gate.resolve();await work;
 assert.equal(rapidDependencyKey(f.view),geometryKey);assert.deepEqual(first(f),original);assert.equal(collision.storedRapidCollisionCheck(f.view,first(f)),null);assert.notEqual(collision.rapidCollisionDisplay(f.view,first(f)).validation,'Clear');
});

test('changing policy or project epoch invalidates a completed check without modifying the saved path',async()=>{
 for(const change of ['policy','epoch']){
  const f=fixture();await runCheck(f);const source=structuredClone(first(f));
  if(change==='policy')rapidFieldChanged(f.view,'marginMm','.3');else f.view.scene.domain.expected.epoch='another-epoch';
  assert.notEqual(collision.rapidCollisionDisplay(f.view,first(f)).validation,'Clear',change);
  assert.doesNotMatch(selectedRow(renderRapidTree(f.view)),/检测通过/,change);assert.doesNotMatch(renderRapid(f.view,true),/当前刀头模型检测通过/,change);assert.deepEqual(first(f),source);
 }
});

test('missing or empty native input keys fall back to authored source identity and reject changed coordinates',async()=>{
 for(const inputKey of [undefined,'']){
  const f=fixture();if(inputKey===undefined)delete first(f).inputKey;else first(f).inputKey=inputKey;openRapid(f.view);
  await runCheck(f);assert.equal(collision.rapidCollisionDisplay(f.view,first(f)).validation,'Clear');
  first(f).points[1][1]=7;first(f).manualPath.points[0][1]=7;
  assert.equal(collision.storedRapidCollisionCheck(f.view,first(f)),null,`inputKey=${String(inputKey)}`);
  assert.notEqual(collision.rapidCollisionDisplay(f.view,first(f)).validation,'Clear');assert.doesNotMatch(selectedRow(renderRapidTree(f.view)),/检测通过/);
 }
});

test('actual browser check completion preserves panel nodes, listeners, latest dual and nested scrolling, focus and selection',async()=>{
 const require=createRequire(import.meta.url),{chromium}=require(process.env.ICAX_PLAYWRIGHT_MODULE||'C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
 const browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'msedge'});
 try{
  const page=await browser.newPage({viewport:{width:1350,height:900}}),errors=[];page.on('pageerror',error=>errors.push(error.message));
  await page.route('http://127.0.0.1:19369/**',request=>new URL(request.request().url()).pathname==='/'?request.fulfill({contentType:'text/html',body:'<style>body{margin:0}#mount{display:grid;grid-template-columns:280px 500px 500px;gap:12px}.left,.right{height:420px;overflow:auto}.spacer{height:1500px}.five-rapid-body{height:240px;overflow:auto}.five-rapid-body::after{content:"";display:block;height:900px}input{width:100px}canvas{width:500px;height:420px}</style><main id="mount"><aside class="left cam-context-pane"></aside><canvas></canvas><aside class="right cam-info-pane"></aside></main>'}):serveBrowserAsset(request));
  // The browser renders actual product modules with controlled native results.
  // Standalone CEF/native-geometry coverage belongs to the separate UI run.
  for(const status of ['Clear','Unchecked'])for(const focus of ['left','right']){
   await page.goto('http://127.0.0.1:19369/');
   await page.evaluate(async({fixtureSource,routeSource,deferredSource,policy,savedReason,status,focus})=>{
    const planning=await import('/src/apps/five-axis-cam/webpage/rapidPlanning.mjs'),collision=await import('/src/apps/five-axis-cam/webpage/rapidCollisionCheck.mjs'),{patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs');
    const makeRoute=new Function('policy','savedReason',`return (${routeSource})`)(policy,savedReason),makeDeferred=new Function(`return (${deferredSource})`)();
    const makeFixture=new Function('openRapid','route','policy','deferred',`return (${fixtureSource})`)(planning.openRapid,makeRoute,policy,makeDeferred),f=window.checkStatusFixture=makeFixture({status,hold:true});
    const mount=document.querySelector('#mount');f.render=()=>patchWorkspacePanels(mount,[['.left',`<label data-five-key="left-field">查找连接<input id="left-input" value="123456"></label>${planning.renderRapidTree(f.view)}<div class="spacer" data-five-key="left-spacer"></div>`],['.right',planning.renderRapid(f.view,true)+'<div class="spacer" data-five-key="right-spacer"></div>']]);
    f.ops.renderProject=f.render;if(focus==='right')await planning.rapidCommand(f.context,f.view,'path-edit',f.ops);f.render();f.before=JSON.stringify(f.view.scene.domain);f.inputSelector=focus==='left'?'#left-input':'[data-five-rapid-field="pathY"]';
    mount.addEventListener('click',event=>{const button=event.target.closest('[data-five-command="five.rapid.check"]');if(button&&!button.disabled)f.work=collision.checkRapidCollision(f.context,f.view,f.ops,planning.rapidError);});
   },{fixtureSource:fixture.toString(),routeSource:route.toString(),deferredSource:deferred.toString(),policy,savedReason,status,focus});
   await page.locator('[data-five-command="five.rapid.check"]').click();await page.waitForFunction(()=>checkStatusFixture.calls.length===1);
   const before=await page.evaluate(()=>{
    const f=checkStatusFixture;f.input=document.querySelector(f.inputSelector);f.canvas=document.querySelector('canvas');f.events=0;f.input.addEventListener('input',()=>f.events++);f.input.focus({preventScroll:true});f.input.setSelectionRange(0,1);
    const selectors=['.left','.right','.five-rapid-body'];selectors.forEach((selector,index)=>document.querySelector(selector).scrollTop=40+index*15);
    // Scroll again after the asynchronous request starts. Restoration must use
    // this latest state, not positions captured before the request.
    selectors.forEach((selector,index)=>document.querySelector(selector).scrollTop=90+index*25);
    return {scroll:selectors.map(selector=>document.querySelector(selector).scrollTop),selection:[f.input.selectionStart,f.input.selectionEnd],focused:document.activeElement===f.input};
   });
   assert.ok(before.focused);assert.ok(before.scroll.every(value=>value>0));await page.evaluate(()=>{checkStatusFixture.gate.resolve();return checkStatusFixture.work;});
   const after=await page.evaluate(async()=>{
    const f=checkStatusFixture,{rapidPreview}=await import('/src/apps/five-axis-cam/webpage/rapidPlanning.mjs');f.input.dispatchEvent(new Event('input'));
    return {input:f.input===document.querySelector(f.inputSelector),canvas:f.canvas===document.querySelector('canvas'),focused:document.activeElement===f.input,selection:[f.input.selectionStart,f.input.selectionEnd],scroll:['.left','.right','.five-rapid-body'].map(selector=>document.querySelector(selector).scrollTop),events:f.events,unchanged:JSON.stringify(f.view.scene.domain)===f.before,calls:f.calls.map(call=>call.method),tree:document.querySelector('[data-five-rapid-id="a/b"]').textContent,panel:document.querySelector('.right').textContent,canvasValidation:rapidPreview(f.view,f.view.scene.domain.setups[0].generatedGeometry).transitions[0].validation};
   });
   assert.ok(after.input&&after.canvas&&after.focused,JSON.stringify({status,focus,before,after}));assert.deepEqual(after.selection,before.selection);assert.deepEqual(after.scroll,before.scroll);assert.equal(after.events,1);assert.equal(after.unchanged,true);assert.deepEqual(after.calls,['CamRapid.CheckPath']);assert.equal(after.canvasValidation,status);
   if(status==='Clear'){assert.match(after.tree,/检测通过/);assert.match(after.panel,/当前刀头模型检测通过/);assert.doesNotMatch(after.panel,/手动路径已保存，尚未通过碰撞检查|待碰撞检测/);}
   else{assert.match(after.tree,/未通过|检测到碰撞/);assert.match(after.panel,/当前段未通过检测/);assert.match(after.panel,/手动路径未通过碰撞检查/);}
  }
  assert.deepEqual(errors,[]);
 }finally{await browser.close();}
});
