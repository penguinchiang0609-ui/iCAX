import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,readFileSync,writeFileSync,existsSync} from 'node:fs';
import {verifyBoundsSnapping} from './FiveAxisBoundsSnap.interactions.mjs';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY),base=pathToFileURL(resolve(process.env.ICAX_CEF_RUNTIME)+'/').href;
mkdirSync(output,{recursive:true});
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${process.env.ICAX_CEF_PORT||9462}`);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');assert.ok(page);page.setDefaultTimeout(45000);
const reopen=process.env.ICAX_BOUNDS_REOPEN==='1',report=reopen?JSON.parse(readFileSync(resolve(output,'report.json'),'utf8')):{passed:false,standaloneCef:true,realNativeQueue:true,realResourceBridge:true,realTeapot:true,cases:[],errors:[]};
page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fiveAxisImportActive);
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
const bind=async()=>{await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);await idle();};
try{
  await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
  if(!reopen){
    const projectPath=resolve(output,'teapot-box-alignment.i5cam');
    const created=await page.evaluate(p=>__icaxAppShell.createProject({projectName:'茶壶包围盒吸附验收',projectPath:p,productId:'icax.five-axis-cam'}),projectPath);
    await page.waitForFunction(id=>window.__icaxWorkbench?.currentProjectId===id&&__icaxAppShell.getState().activeProjectId===id,created.activeProjectId);await bind();
    await page.evaluate(p=>__icaxWorkbench.importWorkpiece(p),resolve('TestData/FA/刀路/刀路/AI构造刀路/茶壶-壶身壶嘴把手-多面空移.step'));await idle();
    report.cases.push(...await verifyBoundsSnapping({page,base,read,output}));
    await page.evaluate(path=>{(globalThis.icax??globalThis.icaxNativeBridge).saveFileDialog=async()=>path;},projectPath);
    await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('app.save'));await idle();assert.ok(existsSync(projectPath));
    writeFileSync(resolve(output,'expected-state.json'),JSON.stringify({projectPath,state:await read()},null,2));
  }else{
    const expected=JSON.parse(readFileSync(resolve(output,'expected-state.json'),'utf8'));
    await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),expected.projectPath);
    await page.waitForFunction(()=>window.__icaxWorkbench?.currentProjectId===__icaxAppShell.getState().activeProjectId);await bind();
    const state=JSON.parse(JSON.stringify(await read()));assert.deepEqual(state.workpieces,expected.state.workpieces);assert.deepEqual(state.placements,expected.state.placements);
    await page.getByRole('button',{name:'自身坐标系重置',exact:true}).click();
    await page.waitForFunction(()=>fixture.view.sceneSnapping?.catalogs.has(fixture.view.scene.activePlacementId)&&fixture.view.sceneSnapping?.pending.size===0);
    const boxes=await page.evaluate(()=>fixture.view.sceneSnapping.catalogs.get(fixture.view.scene.activePlacementId).features.filter(f=>f.boxKind));
    assert.ok(boxes.every(f=>f.boxKind==='OBB'));assert.equal(boxes.filter(f=>f.mode==='plane').length,6);
    await page.screenshot({path:resolve(output,'reopened-bounding-boxes.png')});
    await page.locator('[data-five-command="five.align.close"]').last().click();await idle();
    report.cases.push('Actual standalone Save and fresh-process reopen preserve rebased geometry and recompute the selectable OBB faces, edges and points');
  }
  assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'cef-failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
