import assert from 'node:assert/strict';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
const output=resolve('output/tests/lead-compensation-20261026/standalone');mkdirSync(output,{recursive:true});
const browser=await chromium.connectOverCDP('http://127.0.0.1:9568'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));page.setDefaultTimeout(120000);
const report={passed:false,coverage:{standaloneCef:true,native:true},cases:[]};
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending,null,{timeout:120000});
const invoke=(method,payload={})=>page.evaluate(({method,payload})=>fixture.view.viewportSceneProxy.invoke(method,payload,{timeoutMs:120000}),{method,payload}),read=()=>invoke('CamScene.ReadState');
let stress;
try{
  await idle();await page.locator('[data-five-command="five.process.load"]').click();await idle();
  const initial=await read(),id=initial.intentToolpaths[0].id;
  const widthOf=async()=>{const s=await read();return (await invoke('CamProcess.Read',{expected:s.expected,intentIds:[id]})).items[0].assignment.kerf.widthMm;};
  const beforeWidth=await widthOf(),afterWidth=beforeWidth===.6?.5:.6;
  stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after,.five-cut-tree::after{content:"";display:block;height:1800px;flex-shrink:0}.five-cut-tree{max-height:260px;overflow:auto}'});
  await page.evaluate(()=>{const c=fixture.view.viewportSceneProxy.sdoClient,base=c.invoke;fixture.restore=()=>c.invoke=base;fixture.canvas=fixture.view.viewport.renderer.domElement;fixture.hold=true;c.invoke=async function(...args){const r=await base.apply(this,args);if(args[1]==='CamProcess.PreviewCompensation'&&fixture.hold){fixture.hold=false;fixture.held=true;await new Promise(resolve=>fixture.release=resolve);fixture.held=false;}return r;};});
  const width=page.locator('[data-five-field="cutKerf"]');await width.fill(String(afterWidth));await width.blur();await page.locator('[data-five-command="five.process.apply"]').click();await idle();await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const i=fixture.input=document.querySelector('[data-five-field="cutKerf"]');i.focus({preventScroll:true});i.setSelectionRange(0,1);fixture.events=0;i.addEventListener('lead-regression',()=>fixture.events++);const nodes=['.cam-context-pane','.cam-info-pane','.five-cut-tree'].map(s=>document.querySelector(s));nodes.forEach((n,i)=>n.scrollTop=70+i*25);return nodes.map(n=>n.scrollTop);});
  await page.evaluate(()=>{['.cam-context-pane','.cam-info-pane','.five-cut-tree'].forEach(s=>document.querySelector(s).scrollTop+=20);fixture.release();});await page.waitForFunction(()=>!fixture.view.fiveCompensation.loading);
  report.interaction=await page.evaluate(()=>{fixture.input.dispatchEvent(new Event('lead-regression'));return {input:fixture.input===document.querySelector('[data-five-field="cutKerf"]'),canvas:fixture.canvas===fixture.view.viewport.renderer.domElement,focus:document.activeElement===fixture.input,selection:[fixture.input.selectionStart,fixture.input.selectionEnd],scroll:['.cam-context-pane','.cam-info-pane','.five-cut-tree'].map(s=>document.querySelector(s).scrollTop),events:fixture.events};});
  assert.equal(report.interaction.input,true);assert.equal(report.interaction.canvas,true);assert.equal(report.interaction.focus,true);assert.deepEqual(report.interaction.selection,[0,1]);assert.deepEqual(report.interaction.scroll,scroll.map(n=>n+20));assert.equal(report.interaction.events,1);
  await page.evaluate(()=>fixture.restore());await stress.evaluate(n=>n.remove());stress=null;
  report.cases.push('Real native response delayed: input/canvas/listener/focus/selection and latest dual-sidebar plus nested scroll remain intact');
  console.log('Native response interaction preservation passed');
  await page.evaluate(()=>document.activeElement.blur());await page.locator('[data-action="undo-project"]').click();await idle();
  if(await widthOf()===afterWidth){await page.locator('[data-action="undo-project"]').click();await idle();}
  assert.equal(await widthOf(),beforeWidth);await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));await idle();await page.waitForFunction(w=>!fixture.view.fiveCompensation.loading&&fixture.view.fiveCompensation.items.some(i=>i.widthMm===w),beforeWidth);
  await page.locator('[data-action="redo-project"]').click();await idle();assert.equal(await widthOf(),afterWidth);await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));await idle();await page.waitForFunction(w=>!fixture.view.fiveCompensation.loading&&fixture.view.fiveCompensation.items.some(i=>i.widthMm===w),afterWidth);
  report.cases.push('Actual native toolbar undo and redo restore compensation and visible lead geometry');
  console.log('Native toolbar undo and redo passed');
  const state=await read(),ids=state.intentToolpaths.map(c=>c.id),saved=await invoke('CamProcess.PreviewLeads',{expected:state.expected,intentIds:ids});
  await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('app.save'));await idle();const project=await page.evaluate(()=>__icaxAppShell.getState().activeProjectPath);
  await page.evaluate(()=>{void __icaxAppShell.closeProject(__icaxAppShell.getState().activeProjectId);});
  await page.waitForFunction(()=>!__icaxAppShell.getState().activeProjectId||!!document.querySelector('dialog[open]'));
  if(await page.getByRole('button',{name:'直接关闭',exact:true}).count())await page.getByRole('button',{name:'直接关闭',exact:true}).click();
  await page.waitForFunction(()=>!__icaxAppShell.getState().activeProjectId);
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
  await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};});await idle();
  await page.locator('[data-tab-id="toolpath"]').click();await idle();await page.waitForFunction(()=>fixture.view.viewport?.sceneObjects.size>0);
  const after=await read();assert.deepEqual(await invoke('CamProcess.PreviewLeads',{expected:after.expected,intentIds:ids}),saved);
  report.cases.push('Explicit native project save, close and reopen reproduce identical three-hole lead and compensation geometry');
  await page.evaluate(()=>{const v=fixture.view,vp=v.viewport,c=v.scene.intentToolpaths[0],o=vp.sceneObjects.get(c.placementId);o.updateWorldMatrix(true,false);const p=vp.camera.position.clone().set(...c.startPoint).applyMatrix4(o.matrixWorld);vp.setViewDirection([0,0,1]);vp.setCameraState({...vp.getCameraState(),target:{x:p.x,y:p.y,z:p.z},radius:24});});
  await page.screenshot({path:resolve(output,'引线补偿细节.png')});report.passed=true;report.project=project;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'interaction-failure.png')}).catch(()=>{});}
finally{await page.evaluate(()=>{fixture.restore?.();fixture.release?.();}).catch(()=>{});await stress?.evaluate(n=>n.remove()).catch(()=>{});writeFileSync(resolve(output,'interaction-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();}
