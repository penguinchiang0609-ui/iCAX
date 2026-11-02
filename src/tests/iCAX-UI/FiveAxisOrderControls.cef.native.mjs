import assert from 'node:assert/strict';
import {copyFileSync,existsSync,mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {chromium} from 'playwright';
import {restoreNativeCefViewport} from './nativeCefViewport.mjs';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/order-controls-20261101/cef');mkdirSync(output,{recursive:true});
const project=resolve(output,'order-controls.i5cam'),reopen=!!process.env.ICAX_ORDER_CONTROLS_REOPEN;
const source=resolve(process.env.ICAX_ORDER_SOURCE_PROJECT||'output/tests/order-pillar-recovery-20261101/simple-cef/pillar-1793530433912.i5cam');
const digest=path=>createHash('sha256').update(readFileSync(path)).digest('hex');
const sourceHash=digest(source);
if(!reopen){copyFileSync(source,project);if(existsSync(source+'.log'))copyFileSync(source+'.log',project+'.log');}
const base=pathToFileURL(resolve('src')+'/').href;
const report={passed:false,standaloneCef:true,realProject:true,filePicker:false,cases:[]};
const browser=await chromium.connectOverCDP(process.env.ICAX_ORDER_CDP_URL||'http://127.0.0.1:9631');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/AppShell/index.html'));assert.ok(page);page.setDefaultTimeout(120000);
const errors=[];page.on('pageerror',error=>errors.push(error.message));
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),project);
 await page.evaluate(async base=>{const store=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.orderControls={calls:[]};Object.defineProperty(orderControls,'view',{get:()=>store.getProjectView(__icaxAppShell.getState().activeProjectId)});},base);
 await page.waitForFunction(()=>!!orderControls.view?.scene?.domain&&orderControls.view.scene.intentToolpaths.length===21&&!orderControls.view.pending);
 await restoreNativeCefViewport(page);
 const idle=()=>page.waitForFunction(()=>!orderControls.view.pending&&!orderControls.view.fiveOrder?.request&&!orderControls.view.fiveOrder?.running&&!__icaxAppShell.getState().pendingCount);
 const read=()=>page.evaluate(()=>orderControls.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:180000}));
 const stateOf=state=>JSON.parse(JSON.stringify({workpieces:state.workpieces,placements:state.placements,intentToolpaths:state.intentToolpaths}));
 if(reopen){
  const state=stateOf(await read());assert.deepEqual(state,JSON.parse(readFileSync(resolve(output,'saved-state.json'),'utf8')));
  const expected=JSON.parse(readFileSync(resolve(output,'saved-order.json'),'utf8'));
  assert.deepEqual(await page.evaluate(()=>{const d=orderControls.view.scene.domain;return d.setups.find(s=>s.id===d.activeSetupId).definitionOrder;}),expected);
  report.cases.push('A fresh application process reopens the saved order with identical CAD, placements and all per-path geometry');
 }else{
  const before=await read();report.cutCount=before.intentToolpaths.length;
  await page.evaluate(()=>{const proxy=orderControls.view.viewportSceneProxy,original=proxy.invoke;orderControls.original=original;orderControls.proxy=proxy;proxy.invoke=async function(method,payload,...args){orderControls.calls.push({method,payload:structuredClone(payload)});return original.call(this,method,payload,...args);};});
  await page.evaluate(async()=>{await __icaxAppShell.selectRibbonTab('toolpath');await __icaxAppShell.executeRibbonCommand('five.cam-page.order');});await idle();
  const button=id=>page.locator(`.product-ribbon .ribbon-command[data-command-id="${id}"]`),row=id=>page.locator(`[data-five-intent="${id}"]`);
  const weight=page.locator('[data-ribbon-input="five.order.height-penalty"]');
  assert.equal(await page.locator('.ribbon-command-menu-toggle[data-command-id*="order"]').count(),0);
  for(const id of ['five.tool.manual-order-start','five.tool.manual-order-current'])assert.equal(await button(id).count(),1);
  assert.ok((await weight.boundingBox()).x+(await weight.boundingBox()).width<=(await button('five.tool.auto-order').boundingBox()).x);
  await weight.fill('4.5');assert.equal(await page.evaluate(()=>orderControls.calls.filter(c=>c.method==='CamPlan.BeginOrder').length),0);
  report.cases.push('Both manual commands are directly visible; the one automatic parameter is inline before its action, with no dropdowns or implicit sorting');
  const originalOrder=[...before.setups.find(s=>s.id===before.activeSetupId).definitionOrder];
  const enabled=originalOrder.filter(id=>before.intentToolpaths.some(c=>c.id===id&&c.enabled!==false&&!c.locked));assert.ok(enabled.length>=8);
  await page.evaluate(()=>{orderControls.view.viewport.setViewDirection([1,-1,1]);orderControls.view.viewport.fitView(1.1);});
  const pick=await page.evaluate(async({ids,base})=>{
   const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),{nearestCutLine}=await import(base+'apps/five-axis-cam/webpage/cutSelection.mjs');
   const v=orderControls.view,vp=v.viewport,box=vp.renderer.domElement.getBoundingClientRect();
   for(const id of ids){const cut=v.scene.intentToolpaths.find(c=>c.id===id),placement=v.scene.domain.placements.find(p=>p.id===cut.placementId),matrix=new T.Matrix4().set(...placement.transform);
    for(const p of cut.previewPoints){const q=new T.Vector3(...p).applyMatrix4(matrix).project(vp.camera),x=box.x+(q.x+1)*box.width/2,y=box.y+(1-q.y)*box.height/2;if(q.z>=-1&&q.z<=1&&document.elementFromPoint(x,y)===vp.renderer.domElement&&nearestCutLine(v,{clientX:x,clientY:y})?.intentId===id)return {id,x,y};}
   }throw Error('No visible path for the actual scene anchor pick');
  },{ids:enabled.slice(2,-4),base});
  await page.mouse.click(pick.x,pick.y);assert.equal(await page.evaluate(()=>orderControls.view.fiveAxisUI.selectedIntentId),pick.id);
  await button('five.tool.manual-order-current').click();await idle();
  const anchorIndex=originalOrder.indexOf(pick.id),prefix=originalOrder.slice(0,anchorIndex+1);
  assert.deepEqual(await page.evaluate(()=>orderControls.view.fiveOrder.prefix),prefix);
  assert.equal(await button('five.tool.manual-order-current').getAttribute('aria-pressed'),'true');assert.equal(await button('five.tool.manual-order-start').getAttribute('aria-pressed'),'false');
  assert.equal(await page.evaluate(()=>orderControls.calls.filter(c=>c.method==='CamPlan.SetOrder').length),0);
  await row(prefix[0]).click();assert.deepEqual(await page.evaluate(()=>orderControls.view.fiveOrder.prefix),prefix);
  const next=enabled.filter(id=>originalOrder.indexOf(id)>anchorIndex).at(-1);assert.ok(next);
  await row(next).click();await page.keyboard.press('Enter');await idle();assert.equal(await page.evaluate(()=>orderControls.view.error||''),'');
  const expected=[...prefix,next,...originalOrder.slice(anchorIndex+1).filter(id=>id!==next)];
  const actual=await page.evaluate(()=>{const d=orderControls.view.scene.domain;return d.setups.find(s=>s.id===d.activeSetupId).definitionOrder;});assert.deepEqual(actual,expected);
  const nativeManual=await page.evaluate(()=>{const v=orderControls.view;return v.viewportSceneProxy.invoke('CamPlan.QueryOrder',{setupId:v.scene.domain.activeSetupId,expected:v.scene.domain.expected});});assert.deepEqual(nativeManual.definitionOrder,expected);
  report.cases.push('A real scene click supplies the current anchor; entering manual mode preserves its entire prefix immediately, earlier rows are ignored, and only the chosen suffix is reordered in native state');
  await button('five.tool.manual-order-start').click();await idle();assert.deepEqual(await page.evaluate(()=>orderControls.view.fiveOrder.prefix),[]);
  await row(next).click();assert.deepEqual(await page.evaluate(()=>orderControls.view.fiveOrder.prefix),[next]);await page.keyboard.press('Escape');await idle();
  assert.deepEqual(await page.evaluate(()=>{const d=orderControls.view.scene.domain;return d.setups.find(s=>s.id===d.activeSetupId).definitionOrder;}),expected);
  await button('five.tool.manual-order-start').click();await row(next).click();await button('five.tool.manual-order-start').click();await idle();assert.equal(await page.evaluate(()=>orderControls.view.fiveOrder.open),false);
  report.cases.push('From-start begins with an empty draft; Escape discards it, while clicking its active command again saves it');
  await button('five.tool.auto-order').click();await idle();assert.equal(await page.evaluate(()=>orderControls.view.error||''),'');
  const calls=await page.evaluate(()=>orderControls.calls.filter(c=>/CamPlan\.(BeginOrder|SetOrder|Generate)/.test(c.method)));
  assert.equal(calls.filter(c=>c.method==='CamPlan.BeginOrder').length,1);assert.deepEqual(calls.find(c=>c.method==='CamPlan.BeginOrder').payload.options,{heightPenaltyWeight:4.5});assert.equal(calls.some(c=>c.method==='CamPlan.Generate'),false);
  const after=await read();assert.deepEqual(stateOf(after),stateOf(before));
  report.cases.push('Automatic sorting uses the inline height coefficient in one real command, without generating paths or mutating any CAD, placement or path geometry');
  assert.equal((await page.evaluate(path=>orderControls.view.viewportSceneProxy.project.save(path),project)).saved,true);
  writeFileSync(resolve(output,'saved-state.json'),JSON.stringify(stateOf(after),null,2));writeFileSync(resolve(output,'saved-order.json'),JSON.stringify(after.setups.find(s=>s.id===after.activeSetupId).definitionOrder,null,2));
  report.orderCalls=calls;report.project=project;
 }
 assert.equal(digest(source),sourceHash);assert.deepEqual(errors,[]);report.sourceUnchanged=true;report.passed=true;
 await page.screenshot({path:resolve(output,reopen?'reopened.png':'order-controls.png')});
}catch(error){report.error=error.stack;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{writeFileSync(resolve(output,reopen?'reopen-report.json':'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,cases:report.cases,error:report.error?.split('\n')[0]}));await browser.close();}
