import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,writeFileSync} from 'node:fs';
import {verifyViewCubeDrag} from './FiveAxisViewCubeDrag.native.browser.mjs';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/viewcube-left-drag/standalone');mkdirSync(output,{recursive:true});
const runtime=resolve(process.env.ICAX_CEF_RUNTIME||'output/tests/direct-snap/cef-runtime'),base=pathToFileURL(runtime+'/').href;
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${process.env.ICAX_CEF_PORT||9440}`);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');assert.ok(page,'Owned diagnostic AppShell required');
const report={passed:false,standaloneCef:true,realNativeQueue:true,cases:[],errors:[]};page.on('pageerror',e=>report.errors.push(e.stack));
try{
  await page.setViewportSize({width:1600,height:952});await page.waitForFunction(()=>window.__icaxAppShell?.createProject);
  await page.evaluate(p=>__icaxAppShell.createProject({projectName:'ViewCube 左键拖拽验收',projectPath:p,productId:'icax.five-axis-cam'}),resolve(output,`viewcube-${Date.now()}.i5cam`));
  await page.waitForFunction(()=>window.__icaxWorkbench?.currentProjectId===__icaxAppShell.getState().activeProjectId);
  await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);
  await page.waitForFunction(()=>!!fixture.view.scene?.domain&&!fixture.view.pending);
  await page.evaluate(p=>__icaxWorkbench.importWorkpiece(p),resolve('TestData/FA/刀路/刀路/整理测试刀路/多槽圆盖-3-7b202.igs'));
  await page.waitForFunction(()=>!fixture.view.pending&&!!fixture.view.viewport?.transformManipulator);
  const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}));
  report.cases.push(...await verifyViewCubeDrag({page,read}));
  await page.evaluate(()=>{fixture.view.viewport.setStandardView('top-front-right');fixture.view.viewport.fitView(1.25);});
  const before=await read(),{pick,numeric}=gizmoDriver(page);await pick('X');await numeric().fill('7.25');
  const preview=()=>page.evaluate(()=>fixture.view.viewport.sceneObjects.get(fixture.view.fivePlacement.placementId).matrix.toArray());
  const draft=await preview();await numeric().evaluate(n=>{window.cubeDraftInput=n;n.setSelectionRange(1,3);});
  const p=await page.locator('.cam-viewcube [data-cam-view="front"] polygon').evaluate(n=>{const b=n.getBBox(),p=new DOMPoint(b.x+b.width/2,b.y+b.height/2).matrixTransform(n.getScreenCTM());return {x:p.x,y:p.y};});
  const camera=await page.evaluate(()=>fixture.view.viewport.getCameraState());
  await page.mouse.move(p.x,p.y);await page.mouse.down();await page.mouse.move(p.x-45,p.y+30,{steps:10});await page.mouse.up();
  assert.notDeepEqual(await page.evaluate(()=>fixture.view.viewport.getCameraState()),camera);
  assert.deepEqual(await preview(),draft);assert.equal(await numeric().inputValue(),'7.25');
  assert.deepEqual(await numeric().evaluate(n=>[n===window.cubeDraftInput,n===document.activeElement,n.selectionStart,n.selectionEnd]),[true,true,1,3]);
  assert.deepEqual(await read(),before);report.cases.push('Actual standalone cube drag preserves an unsaved placement preview, numeric input node, focus and selection, without native scene changes');
  await page.screenshot({path:resolve(output,'left-drag-with-live-placement.png')});
  await numeric().press('Escape');assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
