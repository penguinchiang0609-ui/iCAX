import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,readFileSync,writeFileSync,existsSync} from 'node:fs';
import {verifyDenseSnap} from './FiveAxisDenseSnap.interactions.mjs';
import {verifySetupNaming} from './FiveAxisSetupNaming.interactions.mjs';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY),base=pathToFileURL(resolve(process.env.ICAX_CEF_RUNTIME)+'/').href;
mkdirSync(output,{recursive:true});
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${process.env.ICAX_CEF_PORT||9475}`);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');
assert.ok(page);page.setDefaultTimeout(45000);
const reopen=process.env.ICAX_ALIGNMENT_INFERENCE_REOPEN==='1';
const report=reopen?JSON.parse(readFileSync(resolve(output,'report.json'),'utf8')):{passed:false,standaloneCef:true,realPerforatedPart:true,cases:[],errors:[]};
page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fiveAxisImportActive);
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();};
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
const bind=async()=>{await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);await idle();};
try{
  await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
  if(!reopen){
    const projectPath=resolve(output,'多孔圆饼-吸附坐标系.i5cam');
    const created=await page.evaluate(p=>__icaxAppShell.createProject({projectName:'多孔圆饼-吸附坐标系',projectPath:p,productId:'icax.five-axis-cam'}),projectPath);
    await page.waitForFunction(id=>window.__icaxWorkbench?.currentProjectId===id&&__icaxAppShell.getState().activeProjectId===id,created.activeProjectId);await bind();
    await page.evaluate(p=>__icaxWorkbench.importWorkpiece(p),resolve('TestData/FA/刀路/刀路/整理测试刀路/组合件-多孔矮圆饼-带孔圆环.STEP'));await idle();
    report.cases.push(...await verifyDenseSnap({page,command,read,output,base}));
    if(process.env.ICAX_TEST_SETUP_NAMING==='1')report.cases.push(...await verifySetupNaming({page,read,output}));
    await page.evaluate(path=>{(globalThis.icax??globalThis.icaxNativeBridge).saveFileDialog=async()=>path;},projectPath);
    await command('app.save');assert.ok(existsSync(projectPath));
    writeFileSync(resolve(output,'expected-state.json'),JSON.stringify({projectPath,state:await read()},null,2));
  }else{
    const expected=JSON.parse(readFileSync(resolve(output,'expected-state.json'),'utf8'));
    await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),expected.projectPath);
    await page.waitForFunction(()=>window.__icaxWorkbench?.currentProjectId===__icaxAppShell.getState().activeProjectId);await bind();
    const state=JSON.parse(JSON.stringify(await read()));assert.deepEqual(state.workpieces,expected.state.workpieces);assert.deepEqual(state.placements,expected.state.placements);
    assert.deepEqual(state.setups,expected.state.setups);
    if(process.env.ICAX_TEST_SETUP_NAMING==='1')await page.locator('[data-tab-id="workpiece"]').click();
    await command('five.tool.align');await page.waitForFunction(()=>fixture.view.sceneSnapping?.catalogs.has(fixture.view.fiveAlignment?.placementId));
    await page.screenshot({path:resolve(output,'reopened-inference.png')});
    report.cases.push('Fresh standalone process reopens the exact rebased geometry and supports cursor inference again');
  }
  assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'cef-failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}


