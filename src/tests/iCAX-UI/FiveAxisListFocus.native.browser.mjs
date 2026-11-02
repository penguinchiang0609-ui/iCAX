import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {browserReportDirectory} from './browserPackageRuntime.mjs';

const output=browserReportDirectory('list-focus-20261031'),report={passed:false,standaloneCef:true,cases:[],errors:[]};
const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT||'http://127.0.0.1:9589');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/SDK/AppShell/index.html'));
let saved;
const inspect=()=>page.evaluate(async()=>{
 const base=new URL('../../../',location.href).href,{getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs'),T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js');
 const v=getProjectView(__icaxAppShell.getState().activeProjectId),vp=v.viewport,ids=v.fiveAxisUI.selectedIntentIds,box=new T.Box3(),points=[];
 for(const c of v.scene.intentToolpaths.filter(c=>ids.includes(c.id))){
  const p=v.scene.domain.placements.find(p=>p.id===c.placementId),m=new T.Matrix4().set(...p.transform);
  for(const point of c.previewPoints){const q=new T.Vector3(...point).applyMatrix4(m);box.expandByPoint(q);points.push(q);}
 }
 return {camera:vp.getCameraState(),quaternion:vp.camera.quaternion.toArray(),selected:[...ids],center:box.getCenter(new T.Vector3()).toArray(),maxNdc:Math.max(...points.map(p=>{p.project(vp.camera);return Math.max(Math.abs(p.x),Math.abs(p.y));})),maxDepth:Math.max(...points.map(p=>Math.abs(p.z)))};
});
function fitted(after,before){
 for(const key of ['projectionMode','theta','phi'])assert.equal(after.camera[key],before.camera[key],key);
 assert.ok(Math.abs(after.quaternion.reduce((n,v,i)=>n+v*before.quaternion[i],0))>1-1e-12);
 assert.ok(after.maxNdc<.95,JSON.stringify(after));assert.ok(after.maxDepth<1);
 for(const [i,key] of ['x','y','z'].entries())assert.ok(Math.abs(after.camera.target[key]-after.center[i])<1e-6);
}
try{
 page.on('pageerror',e=>report.errors.push(e.message));
 await page.waitForSelector('[data-five-intent]');
 saved=await page.evaluate(async()=>{
  const {getProjectView}=await import(new URL('../../../',location.href).href+'apps/_shared/workbench/state/projectViewStore.mjs');
  const v=getProjectView(__icaxAppShell.getState().activeProjectId);
  window.listFocusTest={v,canvas:v.viewport.renderer.domElement};
  return {id:__icaxAppShell.getState().activeProjectId,native:await v.viewportSceneProxy.invoke('CamScene.ReadState',{}),camera:v.viewport.getCameraState(),selection:Object.fromEntries(['selectedIntentId','selectedIntentIds','selectedGroupId','selectedGroupIds','cutSelectionAnchor'].map(key=>[key,v.fiveAxisUI[key]]))};
 });
 const rows=page.locator('.five-tree-row[data-five-intent]');assert.ok(await rows.count()>1);
 report.cutCount=await rows.count();report.fits=[];
 for(const projectionMode of ['perspective','orthographic'])for(const [theta,phi] of [[.1625,1.195],[-.63,.7],[1.45,3.8]]){
  await page.evaluate(({projectionMode,theta,phi})=>{const vp=listFocusTest.v.viewport;vp.setCameraState({...vp.getCameraState(),projectionMode,theta,phi,target:{x:-400,y:50,z:-150},radius:2200});},{projectionMode,theta,phi});
  const before=await inspect();await rows.first().dblclick();const after=await inspect();fitted(after,before);
  await rows.first().focus();await page.keyboard.press('f');const repeated=await inspect();fitted(repeated,after);assert.ok(Math.abs(repeated.camera.radius-after.camera.radius)<1e-8);
  report.fits.push({projectionMode,theta,phi,radius:after.camera.radius,maxNdc:after.maxNdc});
 }
 report.cases.push('Real list double-click and F preserve direction/quaternion and frame the entire path in both projections, including a rolled-over camera; repeated focus is stable');
 await page.evaluate(()=>listFocusTest.v.viewport.setCameraState({...listFocusTest.v.viewport.getCameraState(),radius:2200,theta:-.4,phi:1.1}));
 const beforeClick=await inspect();await rows.nth(1).click();assert.deepEqual((await inspect()).camera,beforeClick.camera);
 await page.keyboard.press('f');fitted(await inspect(),beforeClick);
 await rows.first().click();await rows.nth(1).click({modifiers:['Control']});const multiBefore=await inspect();assert.equal(multiBefore.selected.length,2);
 await page.keyboard.press('f');const multiAfter=await inspect();fitted(multiAfter,multiBefore);assert.deepEqual(multiAfter.selected,multiBefore.selected);
 report.cases.push('Single click only selects; F frames selected paths together without changing the selection');
 const retained=await page.evaluate(()=>{
  const s=listFocusTest;s.row=document.querySelector('.five-tree-row');s.scrollers=[...document.querySelectorAll('.cam-context-pane,.cam-info-pane,.five-cut-tree')];
  return s.scrollers.map(e=>e.scrollTop);
 });
 await rows.nth(1).focus();await page.keyboard.press('f');
 const dom=await page.evaluate(()=>({canvas:listFocusTest.canvas===listFocusTest.v.viewport.renderer.domElement,row:listFocusTest.row===document.querySelector('.five-tree-row'),scroll:listFocusTest.scrollers.map(e=>e.scrollTop)}));
 assert.equal(dom.canvas,true);assert.equal(dom.row,true);assert.deepEqual(dom.scroll,retained);
 const input=page.locator('input[placeholder="名称 / 固定编号"]');assert.equal(await input.count(),1);
 const value=await input.inputValue();await input.focus();const typing=await inspect();await page.keyboard.press('f');assert.deepEqual((await inspect()).camera,typing.camera);await input.fill(value);
 await rows.first().focus();const guarded=await inspect();
 await page.evaluate(()=>{for(const extra of [{ctrlKey:true},{metaKey:true},{altKey:true},{shiftKey:true},{repeat:true},{isComposing:true}])document.activeElement.dispatchEvent(new KeyboardEvent('keydown',{key:'f',bubbles:true,cancelable:true,...extra}));});
 assert.deepEqual((await inspect()).camera,guarded.camera);
 report.cases.push('Focus keeps canvas/list nodes and sidebar scrolls; input F and modified/repeated/composing F do not move the camera');
 const checks=await page.evaluate(async()=>{
  const {focusCuts}=await import(new URL('../../../',location.href).href+'apps/five-axis-cam/webpage/cutFocus.mjs'),v=listFocusTest.v,before=v.viewport.getCameraState();
  const empty=focusCuts(v,[]),unknown=focusCuts(v,['missing']);return {empty,unknown,unchanged:JSON.stringify(before)===JSON.stringify(v.viewport.getCameraState())};
 });assert.deepEqual(checks,{empty:false,unknown:false,unchanged:true});
 const after=await page.evaluate(()=>listFocusTest.v.viewportSceneProxy.invoke('CamScene.ReadState',{}));assert.deepEqual(after,saved.native);report.nativeUnchanged=true;
 await page.screenshot({path:resolve(output,'focused-toolpaths.png')});
 assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.failure=error.stack;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{
 if(saved)await page.evaluate(async saved=>{
  const v=listFocusTest.v;Object.assign(v.fiveAxisUI,saved.selection);v.fivePreferredCutSelection=[...saved.selection.selectedIntentIds];v.viewport.setCameraState(saved.camera);
  // Repaint restored selection with existing presentation commands only.
  await __icaxAppShell.executeRibbonCommand('five.display.numbers');await __icaxAppShell.executeRibbonCommand('five.display.numbers');
 },saved).catch(()=>{});
 writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();
}
