import assert from 'node:assert/strict';
import {readFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve,sep,extname} from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';

const {chromium}=createRequire(import.meta.url)(process.env.ICAX_PLAYWRIGHT_MODULE||'C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const root=resolve(fileURLToPath(new URL('../../..',import.meta.url))),assetRoot=resolve(process.env.ICAX_BROWSER_RUNTIME_ROOT||resolve(root,'src')),output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/compact-cam-20261031');
mkdirSync(output,{recursive:true});
const browser=await chromium.launch({channel:'msedge',headless:true});
const report={passed:false,assetRoot,nativeTransport:'controlled fixture',standaloneCef:false,cases:[],errors:[]};
let page;
try{
 page=await browser.newPage({viewport:{width:1500,height:950}});page.setDefaultTimeout(10000);
 page.on('pageerror',e=>report.errors.push(e.stack||e.message));
 await page.route('http://127.0.0.1:49310/**',route=>{
  const pathname=new URL(route.request().url()).pathname;
  if(pathname==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><meta charset="utf-8"><body></body>'});
  if(!pathname.startsWith('/src/'))return route.abort();
  const path=resolve(assetRoot,decodeURIComponent(pathname).slice('/src/'.length));if(!path.startsWith(assetRoot+sep))return route.abort();
  let body=readFileSync(path,'utf8');
  // Expose the existing production binding without changing its implementation.
  if(pathname.endsWith('/five-axis-cam/webpage/entry.mjs'))body+='\nexport {attach as attachForTest,command as commandForTest};';
  return route.fulfill({contentType:extname(path)==='.css'?'text/css':'text/javascript',body});
 });
 await page.goto('http://127.0.0.1:49310/');
 await page.evaluate(async()=>{
  const base='/src/apps/five-axis-cam/webpage/';
  const [{attachForTest,commandForTest},{renderLeft},{renderHeadSettings,simulationCommand},{getRibbonDefinition},{getProjectView},{patchWorkspacePanels}]=await Promise.all([
   import(base+'entry.mjs'),import(base+'workspaceViews.mjs'),import(base+'trajectorySimulation.mjs'),import(base+'ribbon.mjs'),import('/src/apps/_shared/workbench/state/projectViewStore.mjs'),import(base+'workspaceDom.mjs')]);
  const link=document.createElement('link');link.rel='stylesheet';link.href=base+'workspace.css';document.head.append(link);await new Promise(r=>link.onload=r);
  document.body.innerHTML='<style>body{margin:0;font:14px sans-serif}.ribbon{height:100px;display:flex;justify-content:flex-end;gap:10px;padding:12px}.five-workspace{display:grid;grid-template-columns:300px 1fr 300px;height:800px}.cam-context-pane,.cam-info-pane{overflow:auto;min-height:0;background:#f6f9fa}.middle{position:relative;background:#13252d}canvas{width:100%;height:100%}.cam-workbench.five-workspace .cam-context-pane{grid-area:left;display:block;overflow:auto}.cam-workbench.five-workspace .middle{grid-area:viewer}.cam-workbench.five-workspace .cam-info-pane{grid-area:right}.nested{height:100px;overflow:auto}.spacer{height:1400px}</style><div class="ribbon"></div><main id="mount"><div class="cam-workbench five-workspace" data-five-area="toolpath" data-cam-page="true"><aside class="cam-context-pane"></aside><section class="middle"><canvas></canvas><div data-five-overlay></div></section><aside class="cam-info-pane"></aside></div></main>';
  const view=getProjectView('compact-test'),head={kind:'Builtin',name:'内置示意头',calibrated:true,collisionModel:{boxCount:25}};
  const native={expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'s1',workpieces:[{id:'w',name:'Video_HatRail_1200x220x70_t1p2.step',enabled:true,geometryRef:{url:'fixture.step'}},{id:'other',name:'其他工件',enabled:true,geometryRef:{url:'other.step'}}],setups:[{id:'s1',name:'姿态1',enabled:true,locked:false,headModel:head},{id:'s2',name:'姿态2',enabled:true,locked:false,headModel:head},{id:'other-s',name:'其他工件姿态',enabled:true}],placements:[{id:'p1',workpieceId:'w',setupId:'s1'},{id:'p2',workpieceId:'w',setupId:'s2'},{id:'other-p',workpieceId:'other',setupId:'other-s'}],intentToolpaths:[],fixtureUnits:[],fixtures:[]};
  Object.assign(view,{fiveAxisArea:'toolpath',activeAreaId:'toolpath',scene:{activePlacementId:'p1',domain:structuredClone(native),model:{isLoaded:true},intentToolpaths:[],machines:[]}});
  const f=window.fixture={view,native,requests:[],renders:0},mount=document.querySelector('#mount');
  f.context={mount,project:{projectId:'compact-test'},isCurrentProject:()=>true,sceneProxy:{async invoke(method,payload){
   f.requests.push({method,payload});
   if(method==='CamHead.ListBuiltins')return {items:[{id:'LC',name:'LC-3015'}]};
   const p=native.placements.find(p=>p.id===payload.placementId),s=native.setups.find(s=>s.id===p?.setupId);
   if(method==='CamWorkpiece.Activate')native.activeSetupId=p.setupId;
   else if(method==='CamSetup.SetLocked')s.locked=payload.locked;
   else if(method==='CamSetup.Manage'){if(payload.action==='rename')s.name=payload.name;else if(['enable','disable'].includes(payload.action))s.enabled=payload.action==='enable';else throw Error(payload.action);}
   else throw Error(method);
   native.expected.mutationSequence=String(Number(native.expected.mutationSequence)+1);return {placementId:p.id,setupId:s.id};
  }}};
  const tail='<div class="nested" data-five-key="nested"><div class="spacer"></div></div><div class="spacer" data-five-key="tail"></div>';
  f.render=()=>{f.renders++;patchWorkspacePanels(mount,[['.cam-context-pane',renderLeft(f.context,view)+tail],['.cam-info-pane','<input data-five-key="right-input" aria-label="右侧输入" value="abcdef">'+tail],['[data-five-overlay]',renderHeadSettings(view)]]);const groups=getRibbonDefinition(f.context).tabs.find(t=>t.id==='toolpath').groups;document.querySelector('.ribbon').innerHTML=groups.filter(g=>['整个加工任务','切割头'].includes(g.title)).map(g=>`<div>${g.commands.map(c=>`<button data-command-id="${c.id}" ${c.disabled?'disabled':''}>${c.title}</button>`).join('')}<p>${g.title}</p></div>`).join('');};
  f.ops={renderProject:f.render,appendProjectLog(){},async refreshSceneState(){view.scene.domain=structuredClone(native);view.scene.activePlacementId=native.placements.find(p=>p.setupId===native.activeSetupId).id;},async fitViewAfterRenderPublish(){}};
  f.context.handleAreaRibbonCommand=(_ctx,_view,id)=>commandForTest(f.context,view,id,f.ops);
  document.querySelector('.ribbon').addEventListener('click',e=>{if(e.target.dataset.commandId==='five.simulation.head-open')f.headWork=commandForTest(f.context,view,e.target.dataset.commandId,f.ops);});
  f.render();attachForTest(f.context,view,mount,f.ops);
 });
 const summary=page.locator('[data-five-setup-chooser] summary'),chooser=page.locator('[data-five-setup-chooser]');
 assert.equal(await page.locator('[data-five-key="setup-selector"] .five-setup-row').count(),1);
 assert.equal(await page.locator('.cam-context-pane .five-cam-head').count(),0);
 assert.equal(await page.locator('.cam-context-pane [data-five-command="five.simulation.head-open"]').count(),0);
 const height=(await page.locator('[data-five-key="setup-selector"]').boundingBox()).height;
 await summary.click();assert.equal(await page.locator('.five-setup-options button:visible').count(),2);
 assert.equal((await page.locator('[data-five-key="setup-selector"]').boundingBox()).height,height);
 await page.locator('[data-five-command="five.setup.activate:p2"]').click();
 await page.waitForFunction(()=>fixture.view.scene.activePlacementId==='p2'&&!fixture.view.pending);
 assert.equal(await chooser.getAttribute('open'),null);assert.match(await summary.innerText(),/姿态2/);
 await summary.focus();await summary.press('ArrowDown');assert.equal(await chooser.getAttribute('open'),'');
 await page.keyboard.press('End');assert.equal(await page.locator('[data-five-command="five.setup.activate:p2"]').evaluate(e=>e===document.activeElement),true);
 await page.keyboard.press('Escape');assert.equal(await chooser.getAttribute('open'),null);
 await page.getByRole('button',{name:'锁定装夹',exact:true}).click();await page.waitForFunction(()=>fixture.native.setups[1].locked&&!fixture.view.pending);
 await page.getByRole('button',{name:'解锁装夹',exact:true}).click();await page.waitForFunction(()=>!fixture.native.setups[1].locked&&!fixture.view.pending);
 await page.getByRole('button',{name:'禁用装夹',exact:true}).click();await page.waitForFunction(()=>!fixture.native.setups[1].enabled&&!fixture.view.pending);assert.match(await summary.innerText(),/已禁用/);
 await page.getByRole('button',{name:'启用装夹',exact:true}).click();await page.waitForFunction(()=>fixture.native.setups[1].enabled&&!fixture.view.pending);
 report.cases.push('One compact current setup; dropdown overlays without changing panel height, excludes other workpieces, switches and closes; keyboard navigation; lock/unlock and disable/enable use existing command routes');
 await summary.dblclick();const input=page.getByRole('textbox',{name:'装夹名称',exact:true});await input.fill('侧孔装夹');
 assert.equal(await input.evaluate(e=>document.activeElement===e),true);
 const before=await page.evaluate(()=>{const f=fixture;f.input=document.querySelector('[data-five-setup-name]');f.canvas=document.querySelector('canvas');f.events=0;f.input.addEventListener('input',()=>f.events++);f.input.focus({preventScroll:true});f.input.setSelectionRange(1,3);const selectors=['.cam-context-pane','.cam-info-pane','.cam-context-pane .nested','.cam-info-pane .nested'];selectors.forEach((s,i)=>document.querySelector(s).scrollTop=30+i*10);setTimeout(()=>f.render(),100);selectors.forEach((s,i)=>document.querySelector(s).scrollTop=60+i*10);return selectors.map(s=>document.querySelector(s).scrollTop);});
 await page.waitForTimeout(180);
 const after=await page.evaluate(()=>{const f=fixture;f.input.dispatchEvent(new Event('input',{bubbles:true}));return {same:f.input===document.querySelector('[data-five-setup-name]')&&f.canvas===document.querySelector('canvas'),focus:document.activeElement===f.input,range:[f.input.selectionStart,f.input.selectionEnd],events:f.events,scroll:['.cam-context-pane','.cam-info-pane','.cam-context-pane .nested','.cam-info-pane .nested'].map(s=>document.querySelector(s).scrollTop)};});
 assert.ok(after.same&&after.focus);assert.deepEqual(after.range,[1,3]);assert.equal(after.events,1);assert.deepEqual(after.scroll,before);
 await input.press('Enter');await page.waitForFunction(()=>fixture.native.setups[1].name==='侧孔装夹'&&!fixture.view.pending);assert.equal(await input.count(),0);
 await summary.dblclick();await input.fill('放弃');await input.press('Escape');assert.equal(await input.count(),0);assert.equal(await page.evaluate(()=>fixture.native.setups[1].name),'侧孔装夹');
 await summary.click();await page.getByRole('textbox',{name:'右侧输入',exact:true}).click();assert.equal(await chooser.getAttribute('open'),null);
 const rightBefore=await page.evaluate(()=>{const f=fixture;f.rightInput=document.querySelector('[aria-label="右侧输入"]');f.rightInput.focus({preventScroll:true});f.rightInput.setSelectionRange(2,4);f.rightInput.addEventListener('input',()=>f.rightEvents=(f.rightEvents||0)+1);const selectors=['.cam-context-pane','.cam-info-pane','.cam-context-pane .nested','.cam-info-pane .nested'];selectors.forEach((s,i)=>document.querySelector(s).scrollTop=40+i*10);setTimeout(()=>{f.view.scene.domain.setups[1].enabled=false;f.render();},100);selectors.forEach((s,i)=>document.querySelector(s).scrollTop=80+i*10);return selectors.map(s=>document.querySelector(s).scrollTop);});
 await page.waitForTimeout(180);
 const rightAfter=await page.evaluate(()=>{const f=fixture;f.rightInput.dispatchEvent(new Event('input',{bubbles:true}));return {same:f.rightInput===document.querySelector('[aria-label="右侧输入"]')&&f.canvas===document.querySelector('canvas'),focus:document.activeElement===f.rightInput,range:[f.rightInput.selectionStart,f.rightInput.selectionEnd],events:f.rightEvents,scroll:['.cam-context-pane','.cam-info-pane','.cam-context-pane .nested','.cam-info-pane .nested'].map(s=>document.querySelector(s).scrollTop)};});
 assert.ok(rightAfter.same&&rightAfter.focus);assert.deepEqual(rightAfter.range,[2,4]);assert.equal(rightAfter.events,1);assert.deepEqual(rightAfter.scroll,rightBefore);
 await page.evaluate(()=>{fixture.view.scene.domain.setups[1].enabled=true;fixture.render();});
 report.cases.push('Double-click rename, Enter save and Escape cancel; asynchronous panel patch retains canvas/input/listener, focus, selection and latest scrolling in both sidebars and nested containers; removed editor does not receive stale focus');
 await page.evaluate(()=>{document.querySelector('.cam-context-pane').scrollTop=0;fixture.view.pending=true;fixture.render();});
 await summary.click();assert.equal(await chooser.getAttribute('open'),null);assert.equal(await page.getByRole('button',{name:'设置切割头',exact:true}).isDisabled(),true);
 await page.evaluate(()=>{fixture.view.pending=false;fixture.render();});
 await page.getByRole('button',{name:'设置切割头',exact:true}).click();await page.waitForFunction(()=>fixture.view.fiveSimulation?.builtinHeads?.length===1);
 assert.equal(await page.getByRole('dialog',{name:'切割头',exact:true}).isVisible(),true);
 assert.equal(await page.getByRole('combobox',{name:'内置切割头',exact:true}).count(),1);
 assert.equal(await page.evaluate(()=>fixture.view.fiveAxisArea),'toolpath');
 await page.getByRole('button',{name:'关闭切割头',exact:true}).click();assert.equal(await page.getByRole('dialog',{name:'切割头',exact:true}).count(),0);
 report.cases.push('CAM ribbon opens and closes the existing cutting-head settings and loads built-in heads; pending work disables the ribbon and setup chooser');
 assert.deepEqual(report.errors,[]);await page.screenshot({path:resolve(output,'compact-cam.png')});report.passed=true;
}catch(error){report.failure=error.stack;report.state=await page?.evaluate(()=>({error:window.fixture?.view.error,pending:window.fixture?.view.pending,requests:window.fixture?.requests,active:window.fixture?.view.scene.activePlacementId}));console.log(JSON.stringify(report));throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await browser.close();}
console.log(JSON.stringify(report,null,2));
