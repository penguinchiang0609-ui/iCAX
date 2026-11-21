import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createServer} from 'node:http';
import {readFileSync} from 'node:fs';
import {resolve,extname} from 'node:path';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {renderTransformTools,renderTransformValue,transformShortcutMode} from '../../apps/five-axis-cam/webpage/transformHud.mjs';
import {renderFixturePlacement,syncFixturePlacement,disposeFixturePlacement} from '../../apps/five-axis-cam/webpage/fixturePlacement.mjs';
import {fixtureFieldChanged} from '../../apps/five-axis-cam/webpage/fixtureActions.mjs';
import {renderRapidPathTransform} from '../../apps/five-axis-cam/webpage/rapidPathEditing.mjs';
import {renderMachineIKTransform,cancelMachinePoseDraft,syncMachineIK,beginMachinePoseHudEdit,finishMachinePoseHudEdit,machineIKField,selectMachineAxis} from '../../apps/five-axis-cam/webpage/machineIK.mjs';
import {renderMachineColliders,renderMachineColliderHud,machineColliderValueChanged,cancelMachineColliderValue,disposeMachineColliders,machineColliderCommand} from '../../apps/five-axis-cam/webpage/machineColliders.mjs';
import {renderToolAxisPointRotation} from '../../apps/five-axis-cam/webpage/toolAxisPointRotation.mjs';
import {selectTool,workspaceState} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';
import {rapidDependencyKey} from '../../apps/five-axis-cam/webpage/rapidDependencies.mjs';

const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
test('shared transform modes expose only supported W/E/R commands and exclude typing, modifiers, IME and repeated keys',()=>{
 const html=renderTransformTools({owner:'test',title:'变换',mode:'translate',modes:[['translate','平移','move'],['rotate','旋转','turn']]});
 assert.match(html,/data-transform-mode="translate"[^>]*aria-keyshortcuts="W"[^>]*aria-pressed="true"/);assert.match(html,/<kbd>E<\/kbd>/);assert.doesNotMatch(html,/scale|<kbd>R/);
 assert.equal(transformShortcutMode({key:'e'}),'rotate');assert.equal(transformShortcutMode({key:'R'}),'scale');assert.equal(transformShortcutMode({key:'m'}),'bounds');assert.equal(transformShortcutMode({key:'F'}),'focus');
 for(const key of ['ctrlKey','metaKey','altKey','shiftKey','isComposing','repeat','defaultPrevented'])assert.equal(transformShortcutMode({key:'w',[key]:true}),null,key);
 assert.equal(transformShortcutMode({key:'e',target:{closest:()=>({})}}),null);
 const value=renderTransformValue({owner:'test',mode:'rotate',fields:[{name:'angle',label:'X',value:'-',readonly:true}],attribute:'data-value'});assert.match(value,/value="-" readonly/);assert.match(value,/<small>°<\/small>/);
});

test('collider tools live beside the object in every mode, with M bounds and F focus and one inline finish button',()=>{
 const view={fiveAxisArea:'simulation',fiveMachine:{machineId:'m'},fiveMachineSelectedPartId:'part',scene:{machines:[]},fiveMachineColliders:{partId:'part',drafts:new Map(),active:'box',mode:'bounds',show:true}};
 const m={entityId:'m',elements:[{entityId:'part',kind:'part'}],collisions:[{id:'box',entityId:'part',name:'box',pose:[0,0,0,0,0,0],geometry:{type:'box',size:[.1,.1,.1]}}]};view.scene.machines=[m];selectTool(view,'machine-instances');
 const sidebar=renderMachineColliders(view,m);assert.doesNotMatch(sidebar,/five-collider-tools|box-focus|完成编辑/);assert.match(sidebar,/完成碰撞盒编辑/);
 for(const mode of ['translate','rotate','scale','bounds']){view.fiveMachineColliders.mode=mode;const hud=renderMachineColliderHud(view);assert.equal((hud.match(/data-transform-mode=/g)||[]).length,5);assert.match(hud,/data-transform-mode="bounds"[^>]*aria-keyshortcuts="M"/);assert.match(hud,/data-transform-mode="focus"[^>]*aria-keyshortcuts="F"/);assert.match(hud,new RegExp(`data-transform-mode="${mode}"[^>]*aria-pressed="true"`));assert.doesNotMatch(hud,/data-transform-mode="focus"[^>]*aria-pressed/);if(mode==='bounds')assert.doesNotMatch(hud,/five-placement-hud/);}
 view.fiveMachineColliders.mode=null;assert.equal(renderMachineColliderHud(view),'');
});

test('fixture HUD reuses the absolute draft and shared matrix validation; invalid text disables the gizmo without native edits',()=>{
 const view={fiveAxisArea:'operation',scene:{intentToolpaths:[],domain:{expected:{epoch:'e'},activeSetupId:'s',setups:[{id:'s'}],fixtureUnits:[{id:'f',setupId:'s',partIds:['p'],inputRevision:1,transform:identity,name:'夹具'}],fixtures:[{id:'p',transform:identity}]}}};selectTool(view,'fixture');
 const object=new THREE.Mesh(new THREE.BoxGeometry(10,10,10)),previews=[],vp={sceneObjects:new Map([['p',object]]),getGridState:()=>({snapEnabled:false}),setTransformPreview(id,m){previews.push({id,m});},clearTransformPreview(){},clearTransformManipulator(){},setTransformManipulator(options){this.options=options;this.transformManipulator={};}};view.viewport=vp;
 const context={mount:{querySelectorAll:()=>[]}},ops={renderProject(){}};
 fixtureFieldChanged(view,'x','12');let html=renderFixturePlacement(view);assert.match(html,/data-five-fixture-field="x"[^>]*value="12"/);assert.match(html,/five.fixture.move-mode:rotate/);assert.doesNotMatch(html,/data-transform-mode="scale"/);
 syncFixturePlacement(context,view,ops);assert.equal(vp.options.enabled,true);assert.equal(previews.at(-1).m[12],12);assert.equal(vp.options.planeTranslate,true);assert.equal(vp.options.screenRotate,true);
 fixtureFieldChanged(view,'x','-');syncFixturePlacement(context,view,ops);assert.equal(vp.options.enabled,false);assert.match(renderFixturePlacement(view),/value="-"/);disposeFixturePlacement(view);object.geometry.dispose();object.material.dispose();
});

test('rapid and cut pose tools expose supported modes while topology insertion suppresses numeric editing',()=>{
 const e={points:[[0,0,0],[1,2,3],[4,0,0]],directions:[[0,0,1],[0,0,1],[0,0,1]],selected:1,values:['1','2','3']},view={fiveAxisArea:'toolpath',scene:{intentToolpaths:[],domain:{expected:{epoch:'e'}}},fiveRapid:{open:true,pathEdit:e}};selectTool(view,'rapid');view.fiveRapid.key=rapidDependencyKey(view);
 let html=renderRapidPathTransform(view);assert.match(html,/path-position/);assert.match(html,/path-pose/);assert.match(html,/浮动空移位置 X/);assert.doesNotMatch(html,/data-transform-mode="scale"/);
 e.poseEditing=true;html=renderRapidPathTransform(view);assert.match(html,/data-five-rapid-field="angle-2"[^>]*readonly/);assert.match(html,/<small>°<\/small>/);
 e.inserting=true;html=renderRapidPathTransform(view);assert.match(html,/aria-pressed="false"/);assert.doesNotMatch(html,/five-placement-hud/);e.selected=0;assert.doesNotMatch(renderRapidPathTransform(view),/five-placement-hud/);
 const cut={id:'c'},v={fiveAxisArea:'toolpath',scene:{intentToolpaths:[cut],domain:{}}};selectTool(v,'keypoint');Object.assign(workspaceState(v),{selectedIntentIds:['c'],selectedIntentId:'c'});v.fiveAxisPointEdit={id:'c',axis:{},pointId:'p'};
 html=renderToolAxisPointRotation(v);assert.match(html,/five.points.edit/);assert.match(html,/<kbd>E<\/kbd>/);assert.doesNotMatch(html,/data-transform-mode="translate"|data-transform-mode="scale"/);
 v.fiveAxisPointDelete=true;assert.match(renderToolAxisPointRotation(v),/aria-pressed="false"/);
});

test('machine part HUD appears as one compact axis value and Escape cancels only unsubmitted numeric drafts',()=>{
 const object=new THREE.Object3D();object.position.set(2,3,4);object.updateMatrixWorld();
 const m={entityId:'m',enabled:true,elements:[{entityId:'tip',parentEntityId:'root'},{entityId:'root'}],joints:[{entityId:'root',type:'prismatic'}]},view={fiveAxisArea:'simulation',fiveMachine:{machineId:'m'},fiveMachineSelectedPartId:'tip',fiveMachineIK:{mode:'translate',machineId:'m',tip:'tip',motionMode:'IK'},scene:{machines:[m]},viewport:{sceneObjects:new Map([['tip',object]])}};selectTool(view,'machine-instances');
 let html=renderMachineIKTransform(view);assert.doesNotMatch(html,/five-placement-hud|ik-pose:/);selectMachineAxis(view,'X');html=renderMachineIKTransform(view);assert.match(html,/data-five-machine-field="ik-delta"[^>]*value="0"/);assert.equal((html.match(/<input\b/g)||[]).length,1);assert.doesNotMatch(html,/data-transform-mode="scale"|five-transform-caption/);
 const s=view.fiveMachineIK,p=s.pose;let calls=0;p.dirty=true;p.error='invalid';p.timer=setTimeout(()=>calls++,30);p.requested={values:[1]};assert.equal(cancelMachinePoseDraft({},view,{renderProject(){}}),true);assert.equal(p.timer,null);assert.equal(p.requested,null);assert.equal(s.drafts.has(p.key),false);assert.equal(calls,0);
 renderMachineIKTransform(view);s.pose.dirty=true;s.pose.error='invalid';view.viewport.setTransformManipulator=o=>{view.viewport.options=o;};syncMachineIK({},view,{renderProject(){}});assert.equal(view.viewport.options.enabled,false);
 s.inflight=Promise.resolve();assert.equal(cancelMachinePoseDraft({},view,{renderProject(){}}),false);
});

test('machine floating numeric typing does not enqueue native updates, and cancelling a collider candidate preserves its saved box',async()=>{
 const object=new THREE.Object3D();object.updateMatrixWorld();const m={entityId:'m',enabled:true,elements:[{entityId:'tip',parentEntityId:'root'},{entityId:'root'}],joints:[{entityId:'root',type:'prismatic'}],collisions:[{id:'box',entityId:'tip',pose:[0,0,0,0,0,0],geometry:{type:'box',size:[.1,.1,.1]}}]},view={fiveAxisArea:'simulation',fiveMachine:{machineId:'m'},fiveMachineSelectedPartId:'tip',fiveMachineIK:{mode:'translate',machineId:'m',tip:'tip',motionMode:'IK'},scene:{machines:[m]},viewport:{sceneObjects:new Map([['tip',object]])}};selectTool(view,'machine-instances');
 const context={mount:{querySelector:()=>null},isCurrentProject:()=>true},ops={renderProject(){}};renderMachineIKTransform(view);selectMachineAxis(view,'X');assert.equal(beginMachinePoseHudEdit(view),true);machineIKField(context,view,'ik-delta','23',ops);assert.equal(view.fiveMachineIK.pose.values[0],'23');assert.equal(view.fiveMachineIK.pose.timer,null);assert.equal(view.fiveMachineIK.editTask,undefined);
 assert.equal(finishMachinePoseHudEdit(context,view,true,ops),true);assert.equal(view.fiveMachineIK.pose.values[0],'0');assert.equal(view.fiveMachineIK.pose.dirty,false);
 view.fiveMachineColliders={partId:'tip',drafts:new Map(),show:true,mode:'translate',active:'box'};renderMachineColliders(view,m);const s=view.fiveMachineColliders,d=s.drafts.get('tip'),before=structuredClone(d.boxes[0]);s.hud={id:'box',mode:'translate',axis:'x',world:new THREE.Matrix4().toArray(),inverse:new THREE.Matrix4().toArray(),baseBox:structuredClone(before),space:'world',value:'0',values:['0','0','0']};
 await machineColliderCommand(context,view,m,'box-value-apply',ops);assert.deepEqual(d.boxes[0],before);assert.equal(d.dirty,false);assert.equal(d.requested,undefined,'Leaving an untouched HUD creates no native save');
 machineColliderValueChanged(context,view,'value','9',ops);assert.equal(s.hud.candidate.position[0],9);assert.deepEqual(d.boxes[0],before);assert.equal(d.dirty,false);assert.equal(d.timer,undefined);assert.equal(d.requested,undefined);
 cancelMachineColliderValue(context,view,ops);assert.equal(s.hud,null);assert.deepEqual(d.boxes[0],before);disposeMachineColliders(view);
});

test('real browser preserves the live floating input, caret and latest nested/side-pane scroll through asynchronous patches',async()=>{
 const root=resolve(import.meta.dirname,'../../..'),server=createServer((request,response)=>{try{const pathname=decodeURIComponent(new URL(request.url,'http://local').pathname);if(pathname==='/'){response.setHeader('content-type','text/html');response.end('<!doctype html><html><body></body></html>');return;}const file=resolve(root,'.'+pathname);if(!file.startsWith(root+'\\')&&!file.startsWith(root+'/'))throw Error('path');response.setHeader('content-type',extname(file)==='.css'?'text/css':'text/javascript');response.end(readFileSync(file));}catch{response.statusCode=404;response.end();}});
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const require=createRequire(import.meta.url),{chromium}=require(process.env.ICAX_PLAYWRIGHT_MODULE||'C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');let browser;
 try{
  browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'msedge'});const page=await browser.newPage({viewport:{width:1200,height:900}});await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.evaluate(async()=>{
   const h=await import('/src/apps/five-axis-cam/webpage/transformHud.mjs'),{patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs');
   const style=document.createElement('style');style.textContent='.cam-context-pane,.cam-info-pane{width:200px;height:220px;overflow:auto;float:left}.nested{height:170px;overflow:auto}.long{height:1800px}[data-five-overlay]{position:relative;height:500px;width:700px;overflow:auto}';document.head.append(style);
   const mount=document.createElement('main');document.body.append(mount);window.regression={h,mount,patchWorkspacePanels,changed:0,commands:[]};
   const pane=(cls,text)=>`<aside class="${cls}"><div class="nested" data-five-key="${cls}-nested"><div class="long"><input data-five-key="${cls}-input" value="${text}"></div></div><div class="long"></div></aside>`;
   regression.pane=pane;regression.overlay=(value,extra=false)=>h.renderTransformTools({owner:'test',title:'变换',mode:'translate',modes:[['translate','平移','move'],['rotate','旋转','turn']]})+h.renderTransformValue({owner:'test',mode:'translate',fields:[{name:'x',label:'X',value,aria:'悬浮 X'},...(extra?[{name:'y',label:'Y',value:'0'}]:[])],attribute:'data-value'});
   mount.innerHTML=pane('cam-context-pane','left')+pane('cam-info-pane','right')+'<canvas tabindex="0"></canvas><div data-five-overlay>'+regression.overlay('12345')+'</div>';
   regression.input=mount.querySelector('[data-value="x"]');regression.canvas=mount.querySelector('canvas');regression.input.addEventListener('input',()=>regression.changed++);
   mount.addEventListener('keydown',event=>{const b=h.transformShortcutButton(event,mount);if(b){event.preventDefault();if(b.getAttribute('aria-pressed')!=='true')regression.commands.push(b.dataset.fiveCommand);}});
   const inside=(cls,text)=>pane(cls,text).replace(/^<aside[^>]*>/,'').replace(/<\/aside>$/,'');regression.patch=(value,extra)=>patchWorkspacePanels(mount,[['.cam-context-pane',inside('cam-context-pane','changed')],['.cam-info-pane',inside('cam-info-pane','changed')],['[data-five-overlay]',regression.overlay(value,extra)]]);
  });
  const input=page.getByRole('textbox',{name:'悬浮 X'});await input.focus();await input.evaluate(e=>{e.value='12e45';e.setSelectionRange(2,3,'backward');e.dispatchEvent(new Event('input',{bubbles:true}));});
  await page.evaluate(()=>{regression.late=new Promise(resolve=>regression.release=()=>{regression.patch('99999',true);resolve();});});
  await page.evaluate(()=>{for(const [i,p] of [...document.querySelectorAll('.cam-context-pane,.cam-info-pane')].entries()){p.scrollTop=80+i*45;p.querySelector('.nested').scrollTop=230+i*70;}regression.release();});await page.evaluate(()=>regression.late);
  assert.deepEqual(await page.evaluate(()=>({sameInput:regression.input===document.querySelector('[data-value="x"]'),sameCanvas:regression.canvas===document.querySelector('canvas'),active:document.activeElement===regression.input,value:regression.input.value,start:regression.input.selectionStart,end:regression.input.selectionEnd,direction:regression.input.selectionDirection,scrolls:[...document.querySelectorAll('.cam-context-pane,.cam-info-pane')].map(p=>[p.scrollTop,p.querySelector('.nested').scrollTop])})),{sameInput:true,sameCanvas:true,active:true,value:'12e45',start:2,end:3,direction:'backward',scrolls:[[80,230],[125,300]]});
  await input.press('e');assert.equal(await input.inputValue(),'12e45');assert.equal(await page.evaluate(()=>regression.changed),2);assert.deepEqual(await page.evaluate(()=>regression.commands),[]);
  await page.locator('canvas').focus();await page.keyboard.press('e');await page.keyboard.press('w');for(const key of ['Control+e','Alt+e','Shift+e','Meta+e'])await page.keyboard.press(key);await page.evaluate(()=>regression.canvas.dispatchEvent(new KeyboardEvent('keydown',{key:'e',bubbles:true,isComposing:true})));
  assert.deepEqual(await page.evaluate(()=>regression.commands),['turn']);
  await input.focus();await page.evaluate(()=>regression.patch('new',false));assert.equal(await input.evaluate(e=>document.activeElement===e),true);assert.equal(await page.locator('[data-value="y"]').count(),0);assert.equal(await page.evaluate(()=>regression.canvas===document.querySelector('canvas')),true);
 }finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
});
