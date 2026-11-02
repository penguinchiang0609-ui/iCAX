import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,writeFileSync} from 'node:fs';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/fixture-workspace/standalone');mkdirSync(output,{recursive:true});
const runtime=resolve(process.env.ICAX_CEF_RUNTIME||'output/tests/fixture-workspace/cef-runtime'),base=pathToFileURL(runtime+'/').href;
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${process.env.ICAX_CEF_PORT||9447}`);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');assert.ok(page);
page.setDefaultTimeout(45000);
const report={passed:false,standaloneCef:true,realNativeQueue:true,realResourceBridge:true,cases:[],errors:[]};page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fiveAxisImportActive);
const tab=async id=>{await page.locator(`[data-tab-id="${id}"]`).click();await idle();assert.equal(await page.evaluate(()=>fixture.view.fiveAxisArea),id);};
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
let importedFixtureId='';
try{
  await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
  const created=await page.evaluate(p=>__icaxAppShell.createProject({projectName:'独立装夹工作页',projectPath:p,productId:'icax.five-axis-cam'}),resolve(output,'fixture-workspace.i5cam'));
  await page.waitForFunction(id=>window.__icaxWorkbench?.currentProjectId===id&&__icaxAppShell.getState().activeProjectId===id,created.activeProjectId);
  await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);await idle();
  assert.deepEqual((await page.locator('[data-tab-id]').allTextContents()).map(s=>s.trim()),['工件','装夹','CAM','孪生','帮助']);
  await tab('fixture');assert.match(await page.locator('.five-fixture-editor').innerText(),/请先导入工件/);
  await page.locator('[data-five-command="five.area.workpiece"]').click();await idle();
  const cover=resolve('TestData/FA/刀路/刀路/整理测试刀路/多槽圆盖-3-7b202.igs');
  await page.evaluate(p=>__icaxWorkbench.importWorkpiece(p),cover);await idle();await page.locator('.five-placement-tools').waitFor();
  await page.evaluate(()=>fixture.canvas=fixture.view.viewport.renderer.domElement);
  assert.equal(await page.locator('[data-command-id="five.tool.fixture"]').count(),0);
  await page.getByRole('button',{name:'锁定工件',exact:true}).click();await idle();const locked=await read();assert.equal(locked.workpieces[0].locked,true);
  await tab('fixture');assert.equal(await page.locator('.five-placement-tools').count(),0);
  await page.locator('[data-command-id="five.fixture.focus.design"]').click();await idle();assert.ok(await page.locator('[data-five-key="fixture-design"]').isVisible());
  await page.locator('[data-command-id="five.fixture.focus.check"]').click();await idle();assert.ok(await page.locator('[data-five-key="fixture-check"]').isVisible());
  assert.equal(await page.locator('[data-command-id="five.tool.fine"]').isDisabled(),true);
  report.cases.push('Real CEF renders five tabs; Fixture has an empty-state route and working design/check entries while the locked workpiece remains protected');
  const support=resolve('TestData/FA/刀路/刀路/整理测试刀路/多种斜孔-多种边-206.STEP');
  const imported=await page.evaluate(async sourcePath=>{
    const proxy=fixture.view.viewportSceneProxy,state=await proxy.invoke('CamScene.ReadState',{});
    let task=await proxy.invoke('CamFixture.Import',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,sourcePath},{timeoutMs:120000});
    while(task.status==='Running'){await new Promise(r=>setTimeout(r,50));task=await proxy.invoke('CamFixture.QueryTask',{epoch:task.epoch,taskId:task.taskId},{timeoutMs:120000});}
    if(task.status!=='Completed')throw Error(JSON.stringify(task));return task.result;
  },support);
  importedFixtureId=imported.fixtureId;
  await page.locator('[data-five-command="five.refresh"]').click();await idle();
  await page.locator('[data-five-fixture-field="name"]').fill('装夹页工装');
  await page.locator('[data-five-command="five.fixture.apply"]').click();await idle();
  assert.equal((await read()).fixtures.find(f=>f.id===imported.fixtureId).name,'装夹页工装');
  for(const area of ['workpiece','toolpath','fixture'])await tab(area);
  assert.equal(await page.evaluate(()=>fixture.canvas===fixture.view.viewport.renderer.domElement),true);
  const after=await read();assert.deepEqual(after.workpieces,locked.workpieces);assert.deepEqual(after.placements,locked.placements);
  report.cases.push('A real native fixture imports and edits in Fixture even while the workpiece is locked; Workpiece/CAM/Fixture switches retain model, placement and canvas');
  await tab('help');await page.locator(`[data-five-command="five.fixture.select:${imported.fixtureId}"]`).click();await idle();
  assert.equal(await page.evaluate(()=>fixture.view.fiveAxisArea),'fixture');
  await page.screenshot({path:resolve(output,'fixture-workspace.png')});
  await tab('workpiece');await page.getByRole('button',{name:'解锁工件',exact:true}).click();await idle();await page.locator('.five-placement-tools').waitFor();
  await tab('fixture');assert.equal(await page.locator('[data-command-id="five.tool.fine"]').isDisabled(),false);
  await page.locator('[data-command-id="five.tool.fine"]').click();await idle();
  assert.equal(await page.evaluate(()=>fixture.view.fiveAxisArea),'fixture');
  await page.locator('[data-command-id="five.tool.fixture"]').click();await idle();
  assert.equal(await page.evaluate(()=>fixture.view.fiveAxisRegistrationOpen),false);
  report.cases.push('Selecting a fixture from Help opens Fixture; unlocking restores placement and enables measured calibration on Fixture; returning to management clears calibration picking');
  assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{
  if(importedFixtureId)await page.evaluate(async id=>{
    const proxy=fixture.view.viewportSceneProxy,state=await proxy.invoke('CamScene.ReadState',{}),item=state.fixtures.find(f=>f.id===id);
    if(item)await proxy.invoke('CamFixture.Remove',{commandId:crypto.randomUUID(),expected:state.expected,setupId:item.setupId,fixtureId:id});
  },importedFixtureId).then(async()=>{
    await page.locator('[data-five-command="five.refresh"]').click();await idle();
    await page.screenshot({path:resolve(output,'fixture-workspace-clean.png')});
  }).catch(error=>{report.cleanupError=error.message;report.passed=false;process.exitCode=1;});
  writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();
}
