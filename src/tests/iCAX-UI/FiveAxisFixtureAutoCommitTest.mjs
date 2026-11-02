import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createServer} from 'node:http';
import {readFileSync} from 'node:fs';
import {resolve,extname,sep} from 'node:path';
import * as actions from '../../apps/five-axis-cam/webpage/fixtureActions.mjs';
import {selectTool} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';

const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
const clone=value=>structuredClone(value);
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const until=async condition=>{for(let i=0;i<400&&!condition();++i)await sleep(5);assert.ok(condition(),'Expected asynchronous phase to start');};
const updateCalls=f=>f.calls.filter(([method])=>method==='CamFixture.UnitUpdate');
function domain(epoch='epoch-a'){
 const unit=(id,x)=>({id,setupId:'setup',name:id,kind:'Imported',inputRevision:'1',partIds:[id+'-part'],transform:identity.map((n,i)=>i===3?x:n),enabled:true,locked:false});
 return {expected:{epoch,mutationSequence:'1'},activeSetupId:'setup',jobId:'job',inputRevision:'1',setups:[{id:'setup',enabled:true,locked:false}],workpieces:[],placements:[],fixtures:[],fixtureUnits:[unit('unit-a',0),unit('unit-b',50)],intentToolpaths:[]};
}
function fixture(){
 const f={server:domain(),calls:[],saved:0,renders:0,logs:[],active:true,held:[],holdNextUpdate:false,holdNextRead:false};
 const view=f.view={disposed:false,pending:false,fiveAxisArea:'workpiece',activeAreaId:'workpiece',scene:{domain:clone(f.server),model:{isLoaded:true},intentToolpaths:[]},fiveAxisFixtures:{selected:'unit-a',drafts:{},clearance:'2'}};
 selectTool(view,'fixture');
 const document={activeElement:null},mount={ownerDocument:document,isConnected:true,contains:()=>true,querySelector:()=>null,querySelectorAll:()=>Object.values(f.fields||{})};
 const context=f.context={mount,project:{projectId:'project-a',projectPath:'C:/fixture-a.i5cam'},isCurrentProject:()=>f.active,
  projectProxy:{state:{projectPath:'C:/fixture-a.i5cam'},async autosave(){++f.saved;}},actions:{refreshProjectHistoryControls(){}}};
 const proxy=f.proxy={sceneId:'scene-a',state:{sceneId:'scene-a'},async invoke(method,payload={}){
  f.calls.push([method,clone(payload)]);
  if(method==='CamFixture.UnitUpdate'){
   assert.deepEqual(payload.expected,f.server.expected,'Each queued command must capture the current acknowledged expected value');
   const item=f.server.fixtureUnits.find(u=>u.id===payload.unitId);assert.ok(item,'A request must target an existing captured unit');
   if(Object.hasOwn(payload,'name'))item.name=payload.name;
   if(Object.hasOwn(payload,'transform'))item.transform=clone(payload.transform);
   item.inputRevision=String(Number(item.inputRevision)+1);f.server.expected={...f.server.expected,mutationSequence:String(Number(f.server.expected.mutationSequence)+1)};
   if(f.holdNextUpdate){f.holdNextUpdate=false;await new Promise(resolve=>{f.releaseUpdate=resolve;f.held.push(resolve);});}
   return {unitId:item.id};
  }
  if(method==='CamScene.ReadState'){
   const reply=clone(f.server);
   if(f.holdNextRead){f.holdNextRead=false;await new Promise(resolve=>{f.releaseRead=resolve;f.held.push(resolve);});}
   return reply;
  }
  if(method==='CamScene.QueryCommand')return {status:'Unknown'};
  throw Error('Unexpected method '+method);
 }};
 context.sceneProxy=proxy;
 f.ops={renderProject(){++f.renders;},appendProjectLog(...args){f.logs.push(args);},async refreshSceneState(){throw Error('Automatic field commits must not use the full shared scene rebuild');}};
 f.fields=Object.fromEntries(['name','x','y','z','rx','ry','rz'].map(name=>{
  const field={dataset:{fiveFixtureField:name},tagName:'INPUT',type:'text',ownerDocument:document,value:name==='name'?'unit-a':'0',defaultValue:name==='name'?'unit-a':'0',isConnected:true,readOnly:false,disabled:false,
   hasAttribute(attribute){return attribute==='data-five-fixture-field';},getAttribute(attribute){return attribute==='data-five-fixture-field'?name:null;},closest(selector){return selector.includes('five-fixture-editor')?mount:selector.includes('data-five-fixture-field')||selector==='input'?this:null;},focus(){document.activeElement=this;},setSelectionRange(start,end,direction='none'){this.selectionStart=start;this.selectionEnd=end;this.selectionDirection=direction;}};
  return [name,field];
 }));
 f.begin=name=>{const field=f.fields[name];field.focus();actions.beginFixtureInput(context,view,field);return field;};
 f.type=(name,value)=>{const field=f.fields[name];field.value=String(value);actions.fixtureFieldChanged(view,name,field.value,field,context);return field;};
 f.finish=(name,cancel=false)=>actions.finishFixtureInput(context,view,f.fields[name],cancel,f.ops);
 f.cleanup=()=>{f.active=false;view.disposed=true;view.pending=false;for(const release of f.held)release();};
 return f;
}
function scopedTest(name,body){test(name,{timeout:10000},async t=>{const f=fixture();t.after(f.cleanup);await body(f);});}

scopedTest('typing performs no native writes and name finish excludes an unrelated invalid pose draft',async f=>{
 f.begin('x');f.type('x','-');f.begin('name');f.type('name','新的夹具名称');
 assert.equal(updateCalls(f).length,0);assert.equal(f.renders,0);
 await f.finish('name');
 const updates=updateCalls(f);assert.equal(updates.length,1);assert.equal(updates[0][1].name,'新的夹具名称');assert.equal(Object.hasOwn(updates[0][1],'transform'),false);
 assert.equal(f.server.fixtureUnits[0].name,'新的夹具名称');assert.equal(actions.fixtureDraft(f.view).x,'-');assert.equal(actions.fixtureDraft(f.view).dirty,true);
});

scopedTest('generated support names save independently while generated positions reject without a native pose update',async f=>{
 f.server.fixtureUnits[0].kind='Generated';f.view.scene.domain.fixtureUnits[0].kind='Generated';
 f.begin('name');f.type('name','自动生成支撑架');await f.finish('name');
 assert.equal(updateCalls(f).length,1);assert.equal(updateCalls(f)[0][1].name,'自动生成支撑架');assert.equal(Object.hasOwn(updateCalls(f)[0][1],'transform'),false);
 assert.equal(f.server.fixtureUnits[0].name,'自动生成支撑架');f.begin('x');f.type('x','25');
 await assert.rejects(f.finish('x'),/生成支撑.*位置固定|不能平移或旋转/);
 assert.equal(updateCalls(f).length,1);assert.equal(f.server.fixtureUnits[0].transform[3],0);assert.equal(actions.fixtureDraft(f.view).x,'25');assert.equal(actions.fixtureDraft(f.view).dirty,true);
});

scopedTest('Enter followed by focusout saves the same field revision only once',async f=>{
 f.begin('x');f.type('x','10');await Promise.all([f.finish('x'),f.finish('x')]);await actions.flushFixtureEdits(f.context,f.view,f.ops);
 assert.equal(updateCalls(f).length,1);assert.equal(f.server.fixtureUnits[0].transform[3],10);assert.equal(actions.fixtureDraft(f.view).dirty,false);
 // The first job has left the queue but is still waiting for another command.
 // A later blur must find that job rather than creating the same native edit.
 f.view.pending=true;f.type('x','20');const waiting=f.finish('x');await sleep(30);const blurred=f.finish('x');f.view.pending=false;await Promise.all([waiting,blurred]);
 assert.deepEqual(updateCalls(f).map(([,request])=>request.transform[3]),[10,20]);
});

scopedTest('queued accepted snapshots never submit later in-progress typing and re-read expected before each request',async f=>{
 f.begin('x');f.type('x','10');f.holdNextUpdate=true;const first=f.finish('x');await until(()=>!!f.releaseUpdate);
 f.type('x','20');const second=f.finish('x');f.type('x','30');
 assert.equal(updateCalls(f).length,1);f.releaseUpdate();await Promise.all([first,second]);
 assert.deepEqual(updateCalls(f).map(([,request])=>request.transform[3]),[10,20]);assert.deepEqual(updateCalls(f).map(([,request])=>request.expected.mutationSequence),['1','2']);
 assert.equal(f.server.fixtureUnits[0].transform[3],20);assert.equal(actions.fixtureDraft(f.view).x,'30');assert.equal(actions.fixtureDraft(f.view).dirty,true);
 await f.finish('x');await actions.flushFixtureEdits(f.context,f.view,f.ops);assert.equal(f.server.fixtureUnits[0].transform[3],30);assert.equal(actions.fixtureDraft(f.view).dirty,false);
});

scopedTest('finished edits wait for another pending operation and eventually apply their last accepted value',async f=>{
 f.view.pending=true;f.begin('x');f.type('x','10');const first=f.finish('x');await sleep(25);assert.equal(updateCalls(f).length,0);
 f.type('x','20');const second=f.finish('x');f.type('x','30');f.view.pending=false;await Promise.all([first,second]);
 assert.equal(updateCalls(f).at(-1)[1].transform[3],20);assert.ok(updateCalls(f).every(([,request])=>request.transform[3]!==30));assert.equal(actions.fixtureDraft(f.view).x,'30');assert.equal(actions.fixtureDraft(f.view).dirty,true);
});

scopedTest('Escape restores only its current field without native writes or discarding another draft',async f=>{
 f.begin('x');f.type('x','12');f.begin('y');f.type('y','44');await f.finish('y',true);
 assert.equal(updateCalls(f).length,0);assert.equal(f.fields.y.value,'0');const draft=actions.fixtureDraft(f.view);assert.equal(draft.y,'0');assert.equal(draft.x,'12');assert.equal(draft.dirty,true);
});

scopedTest('a reused input cannot write text from the previous unit into the newly selected unit',async f=>{
 f.begin('x');f.type('x','10');f.view.fiveAxisFixtures.selected='unit-b';actions.fixtureDraft(f.view);
 f.type('x','999');await f.finish('x');
 assert.ok(updateCalls(f).every(([,request])=>request.unitId==='unit-a'));assert.equal(f.server.fixtureUnits[1].transform[3],50);assert.equal(actions.fixtureDraft(f.view).x,'50');
 // A native scene update may arrive before its original reply. Text that
 // happens to equal the old baseline is still an active editing session.
 f.view.fiveAxisFixtures.selected='unit-a';const before=actions.fixtureDraft(f.view),oldValue=before.x;f.begin('x');f.holdNextUpdate=true;f.type('x','30');const pending=f.finish('x');await until(()=>!!f.releaseUpdate);
 f.type('x',oldValue);assert.equal(before.dirty,false);f.view.scene.domain=clone(f.server);
 f.view.fiveAxisFixtures.selected='unit-b';actions.fixtureDraft(f.view);f.view.fiveAxisFixtures.selected='unit-a';
 const restored=actions.fixtureDraft(f.view);assert.equal(restored,before,'A pending edit must keep its captured draft across unit switches and a newer scene revision');assert.equal(restored.x,oldValue);
 f.releaseUpdate();await pending;assert.equal(actions.fixtureDraft(f.view).x,oldValue);assert.equal(actions.fixtureDraft(f.view).dirty,true);
});

scopedTest('project and proxy replacement cancels queued writes and cannot apply a late response to the new project',async f=>{
 f.begin('x');f.type('x','10');f.holdNextUpdate=true;const first=f.finish('x');await until(()=>!!f.releaseUpdate);f.type('x','20');const second=f.finish('x');
 const other=domain('epoch-b');other.fixtureUnits[0].transform[3]=80;const otherCalls=[];let otherSaved=0;
 f.context.project={projectId:'project-b',projectPath:'C:/fixture-b.i5cam'};f.context.sceneProxy={async invoke(method,payload){otherCalls.push([method,payload]);return clone(other);}};
 f.context.projectProxy={state:{projectPath:'C:/fixture-b.i5cam'},async autosave(){++otherSaved;}};f.view.scene.domain=clone(other);f.view.fiveAxisFixtures={selected:'unit-a',drafts:{},clearance:'2'};
 f.releaseUpdate();await Promise.allSettled([first,second]);
 assert.equal(updateCalls(f).length,1);assert.deepEqual(otherCalls,[]);assert.equal(otherSaved,0);assert.equal(f.view.scene.domain.expected.epoch,'epoch-b');assert.equal(f.view.scene.domain.fixtureUnits[0].transform[3],80);
});

scopedTest('an epoch change on the same proxy invalidates queued old-epoch values',async f=>{
 f.begin('x');f.type('x','10');f.holdNextUpdate=true;const first=f.finish('x');await until(()=>!!f.releaseUpdate);f.type('x','20');const second=f.finish('x');
 f.server=domain('epoch-next');f.server.fixtureUnits[0].transform[3]=75;f.view.scene.domain=clone(f.server);f.view.fiveAxisFixtures={selected:'unit-a',drafts:{},clearance:'2'};
 f.releaseUpdate();await Promise.allSettled([first,second]);
 assert.equal(updateCalls(f).length,1);assert.equal(f.view.scene.domain.expected.epoch,'epoch-next');assert.equal(actions.fixtureDraft(f.view).x,'75');
});

scopedTest('an old scene read cannot replace a newer domain snapshot',async f=>{
 f.begin('x');f.type('x','10');f.holdNextRead=true;const finishing=f.finish('x');await until(()=>!!f.releaseRead);
 const newer=clone(f.server);newer.expected.mutationSequence='9';newer.fixtureUnits[0].name='later external edit';f.server=clone(newer);f.view.scene.domain=newer;
 f.releaseRead();await finishing;assert.equal(f.view.scene.domain.expected.mutationSequence,'9');assert.equal(f.view.scene.domain.fixtureUnits[0].name,'later external edit');
});

scopedTest('an explicit draft reset keeps the focused field usable without rebinding another unit',async f=>{
 f.begin('x');f.type('x','12');f.view.fiveAxisFixtures.drafts={};actions.fixtureDraft(f.view);
 f.type('x','15');await f.finish('x');assert.equal(f.server.fixtureUnits[0].transform[3],15);
 assert.equal(actions.fixtureDraft(f.view).x,'15');
});

test('real Edge input keeps its node, caret, listeners and latest pane/nested scrolling across queued replies',{timeout:30000},async()=>{
 const root=resolve(import.meta.dirname,'../../..'),server=createServer((request,response)=>{
  try{const pathname=decodeURIComponent(new URL(request.url,'http://local').pathname);if(pathname==='/'){response.setHeader('content-type','text/html');response.end('<!doctype html><meta charset="utf-8"><body></body>');return;}
   const file=resolve(root,'.'+pathname);if(!file.startsWith(root+sep))throw Error('path');response.setHeader('content-type',extname(file)==='.css'?'text/css':'text/javascript');response.end(readFileSync(file));
  }catch{response.statusCode=404;response.end();}
 });
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const require=createRequire(import.meta.url),{chromium}=require(process.env.ICAX_PLAYWRIGHT_MODULE||'C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');let browser;
 try{
  browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'msedge'});const page=await browser.newPage({viewport:{width:1200,height:800}});await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.evaluate(async()=>{
   const [actions,{renderFixtureEditor},{patchWorkspacePanels}]=await Promise.all([import('/src/apps/five-axis-cam/webpage/fixtureActions.mjs'),import('/src/apps/five-axis-cam/webpage/fixtureViews.mjs'),import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs')]);
   const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],server={expected:{epoch:'browser-e',mutationSequence:'1'},activeSetupId:'setup',jobId:'job',inputRevision:'1',setups:[{id:'setup'}],placements:[],workpieces:[],fixtures:[],intentToolpaths:[],fixtureUnits:[{id:'unit',setupId:'setup',name:'夹具',kind:'Imported',inputRevision:'1',partIds:[],transform:identity,enabled:true}]};
   const style=document.createElement('style');style.textContent='.cam-context-pane,.cam-info-pane{display:inline-block;width:340px;height:300px;overflow:auto;vertical-align:top}.nested{height:100px;overflow:auto}.spacer{height:1600px}.five-fixture-field{display:block}canvas{width:180px;height:200px}';document.head.append(style);
   const mount=document.createElement('main');mount.innerHTML='<section class="cam-context-pane"></section><canvas></canvas><section class="cam-info-pane"></section>';document.body.append(mount);
   const f=window.fixtureAuto={actions,server,updates:[],jobs:[],errors:[],saved:0,listenerCalls:0,hold:true,view:{disposed:false,pending:false,fiveAxisArea:'workpiece',activeAreaId:'workpiece',scene:{domain:structuredClone(server),model:{isLoaded:true},intentToolpaths:[]},fiveAxisFixtures:{selected:'unit',drafts:{},clearance:'2'}}};
   const context=f.context={mount,project:{projectId:'browser-project'},isCurrentProject:()=>true,actions:{refreshProjectHistoryControls(){}},projectProxy:{state:{projectPath:'C:/fixture-browser.i5cam'},async autosave(){++f.saved;}},sceneProxy:{async invoke(method,payload){
    if(method==='CamFixture.UnitUpdate'){f.updates.push(structuredClone(payload));const item=server.fixtureUnits[0];if(Object.hasOwn(payload,'name'))item.name=payload.name;if(payload.transform)item.transform=structuredClone(payload.transform);item.inputRevision=String(Number(item.inputRevision)+1);server.expected={...server.expected,mutationSequence:String(Number(server.expected.mutationSequence)+1)};if(f.hold){f.hold=false;await new Promise(resolve=>f.release=resolve);}return {unitId:item.id};}
    if(method==='CamScene.ReadState')return structuredClone(server);throw Error(method);
   }}};
   const left='<div class="nested" data-five-key="left-nested"><div class="spacer"></div></div><div class="spacer"></div>';
   f.render=()=>patchWorkspacePanels(mount,[['.cam-context-pane',left],['.cam-info-pane',renderFixtureEditor(f.view)+'<div class="nested" data-five-key="right-nested"><div class="spacer"></div></div><div class="spacer"></div>']]);
   f.ops={renderProject:f.render,appendProjectLog(){},async refreshSceneState(){throw Error('Unexpected full scene refresh');}};
   const finish=(target,cancel)=>{const job=actions.finishFixtureInput(context,f.view,target,cancel,f.ops);f.jobs.push(job);void job.catch(error=>f.errors.push(error.message));};
   mount.addEventListener('focusin',event=>actions.beginFixtureInput(context,f.view,event.target));
   mount.addEventListener('input',event=>{const name=event.target.dataset.fiveFixtureField;if(name)actions.fixtureFieldChanged(f.view,name,event.target.value,event.target,context);});
   mount.addEventListener('keydown',event=>{if(event.target.dataset.fiveFixtureField&&['Enter','Escape'].includes(event.key)&&!event.isComposing){event.preventDefault();finish(event.target,event.key==='Escape');}});
   mount.addEventListener('focusout',event=>{if(event.target.dataset.fiveFixtureField)finish(event.target,false);});f.render();
  });
  const input=page.locator('[data-five-fixture-field="x"]');await input.fill('10');assert.equal(await page.evaluate(()=>fixtureAuto.updates.length),0);await input.press('Enter');await page.waitForFunction(()=>!!fixtureAuto.release);
  await input.fill('20');await input.press('Enter');await input.fill('30');
  const expected=await page.evaluate(()=>{const f=fixtureAuto,input=document.querySelector('[data-five-fixture-field="x"]');f.kept={input,canvas:document.querySelector('canvas'),left:document.querySelector('.cam-context-pane'),right:document.querySelector('.cam-info-pane'),nested:document.querySelector('[data-five-key="left-nested"]')};input.addEventListener('retention-probe',()=>++f.listenerCalls);input.setSelectionRange(0,1,'backward');return ['left','right','nested'].map((key,i)=>{f.kept[key].scrollTop=70+i*40;return f.kept[key].scrollTop;});});
  await page.evaluate(async()=>{const f=fixtureAuto;f.release();await Promise.all(f.jobs);});
  const retained=await page.evaluate(()=>{const f=fixtureAuto,k=f.kept;k.input.dispatchEvent(new Event('retention-probe'));return {sameInput:k.input===document.querySelector('[data-five-fixture-field="x"]'),sameCanvas:k.canvas===document.querySelector('canvas'),focused:document.activeElement===k.input,value:k.input.value,selection:[k.input.selectionStart,k.input.selectionEnd,k.input.selectionDirection],scroll:['left','right','nested'].map(key=>k[key].scrollTop),listenerCalls:f.listenerCalls,updates:f.updates.map(r=>r.transform?.[3]),expected:f.updates.map(r=>r.expected.mutationSequence),draft:f.actions.fixtureDraft(f.view).x,errors:f.errors};});
  assert.deepEqual(retained,{sameInput:true,sameCanvas:true,focused:true,value:'30',selection:[0,1,'backward'],scroll:expected,listenerCalls:1,updates:[10,20],expected:['1','2'],draft:'30',errors:[]});
  await input.press('Escape');assert.equal(await input.inputValue(),'20');assert.equal(await page.evaluate(()=>fixtureAuto.updates.length),2);
  await input.fill('40');await input.press('Enter');await page.locator('canvas').click();await page.evaluate(async()=>{await Promise.all(fixtureAuto.jobs);await fixtureAuto.actions.flushFixtureEdits(fixtureAuto.context,fixtureAuto.view,fixtureAuto.ops);});
  assert.deepEqual(await page.evaluate(()=>fixtureAuto.updates.map(r=>r.transform?.[3])),[10,20,40]);assert.equal(await page.evaluate(()=>fixtureAuto.server.fixtureUnits[0].transform[3]),40);
 }finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
});
