import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,writeFileSync} from 'node:fs';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/workpiece-actions/standalone');mkdirSync(output,{recursive:true});
const runtime=resolve(process.env.ICAX_CEF_RUNTIME),base=pathToFileURL(runtime+'/').href;
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${process.env.ICAX_CEF_PORT||9448}`);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');assert.ok(page);page.setDefaultTimeout(45000);
const report={passed:false,standaloneCef:true,realNativeQueue:true,realResourceBridge:true,cases:[],errors:[]};page.on('pageerror',e=>report.errors.push(e.message));
const action=name=>page.locator(`[data-command-id="workpiece.${name}"]`);
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fiveAxisImportActive);
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 const created=await page.evaluate(p=>__icaxAppShell.createProject({projectName:'工件操作验收',projectPath:p,productId:'icax.five-axis-cam'}),resolve(output,'workpiece-actions.i5cam'));
 await page.waitForFunction(id=>window.__icaxWorkbench?.currentProjectId===id&&__icaxAppShell.getState().activeProjectId===id,created.activeProjectId);
 await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);await idle();
 for(const name of ['remove','lock','enable'])assert.equal(await action(name).isDisabled(),true);
 const source=resolve('TestData/FA/刀路/刀路/整理测试刀路/现场刀路-复杂组合零件124个零件-WM04T0124A-100-200.STEP');
 await page.evaluate(p=>__icaxWorkbench.importWorkpiece(p),source);await idle();await page.locator('.five-placement-tools').waitFor();
 assert.deepEqual((await page.locator('[data-tab-id]').allTextContents()).map(s=>s.trim()),['工件','工序','刀路','孪生','帮助']);
 for(const id of ['inspection','cad-repair','cad-edit','measure']){
   await page.locator(`[data-command-id="five.tool.${id}"]`).click();await idle();
   assert.equal(await page.evaluate(()=>fixture.view.fiveAxisArea),'workpiece');assert.equal(await page.locator(`[data-five-tool="${id}"]`).count(),1);
 }
 await page.locator('[data-command-id="five.tool.inspection"]').click();await idle();
 await page.locator('[data-five-command="workpiece.check"]').click();await idle();assert.ok(await page.evaluate(()=>fixture.view.fiveAxisUI.inspection?.report));
 await page.locator('[data-command-id="five.tool.translate"]').click();await idle();await page.locator('.five-placement-tools').waitFor();
 report.cases.push('Real standalone CEF has five tabs and a Geometry group on Workpiece; all four entries stay on Workpiece, real native inspection runs and Placement restores the move gizmo');
 assert.deepEqual(await page.locator('[data-command-id^="workpiece."]').allTextContents().then(a=>a.map(s=>s.trim())),['导入','删除','锁定','禁用']);
 assert.equal(await page.locator('[data-command-id="workpiece.import"]').getByText('导入模型',{exact:true}).count(),0);
 const before=await read();await page.locator('[data-five-transform-field="position-X"]').fill('13.5');await action('lock').click();await idle();
 const locked=await read();assert.equal(locked.workpieces[0].locked,true);assert.equal(locked.placements[0].transform[3],13.5);
 assert.equal((await action('lock').innerText()).trim(),'解锁');assert.equal(await action('remove').isDisabled(),true);
 await action('enable').click();await idle();const disabled=await read();assert.equal(disabled.workpieces[0].enabled,false);assert.equal(disabled.workpieces[0].locked,true);
 assert.equal((await action('enable').innerText()).trim(),'启用');assert.match(await page.locator('[data-five-key="workpiece-list"]').innerText(),/已禁用/);
 assert.equal(await page.evaluate(()=>fixture.view.viewport.sceneObjects.size),1);
 assert.equal(await page.locator('[data-five-transform-field][readonly]').count(),9);
 report.cases.push('Actual CEF ribbon has Import/Delete/Lock/Disable; numeric pose commits before locking, labels switch immediately, deletion is blocked by the lock and disabled workpiece remains visible');
 for(const area of ['fixture','toolpath','simulation']){
   await page.locator(`[data-tab-id="${area}"]`).click();await idle();await page.waitForFunction(()=>fixture.view.viewport.sceneObjects.size===0);
   assert.equal(await page.locator('[data-five-workpiece],[data-five-intent],.five-fixture-editor').count(),0);
   assert.match(await page.locator('[data-five-key="no-enabled-workpiece"]').innerText(),/暂无已启用工件/);
 }
 await page.screenshot({path:resolve(output,'disabled-hidden.png')});
 await page.locator('[data-tab-id="workpiece"]').click();await idle();await page.waitForFunction(()=>fixture.view.viewport.sceneObjects.size===1);
 await action('enable').click();await idle();await action('lock').click();await idle();await page.locator('.five-placement-tools').waitFor();
 const enabled=await read();assert.equal(enabled.workpieces[0].enabled,true);assert.equal(enabled.workpieces[0].locked,false);assert.deepEqual(enabled.placements,locked.placements);
 await page.screenshot({path:resolve(output,'workpiece-ribbon.png')});
 report.cases.push('Disabled workpiece is absent from Fixture/CAM/Simulation scene objects and lists; Workpiece retains it for re-enable with the original pose');
 const firstPlacement=enabled.placements[0].id;
 await page.evaluate(p=>__icaxWorkbench.importWorkpiece(p),resolve('TestData/FA/刀路/刀路/整理测试刀路/多槽圆盖-3-7b202.igs'));await idle();
 const two=await read(),secondPlacement=two.placements.find(p=>p.id!==firstPlacement).id;
 await action('enable').click();await idle();
 await page.locator('[data-tab-id="fixture"]').click();await idle();
 await page.waitForFunction(id=>!fixture.view.pending&&fixture.view.scene.activePlacementId===id&&fixture.view.viewport.sceneObjects.has(id),firstPlacement);
 assert.equal(await page.locator(`[data-five-workpiece="${secondPlacement}"]`).count(),0);assert.equal(await page.evaluate(id=>fixture.view.viewport.sceneObjects.has(id),secondPlacement),false);
 await page.locator('[data-tab-id="toolpath"]').click();await idle();assert.equal(await page.locator(`[data-five-workpiece="${secondPlacement}"]`).count(),0);
 await page.locator('[data-tab-id="workpiece"]').click();await idle();assert.equal(await page.locator('[data-five-workpiece]').count(),2);
 await page.locator(`[data-five-workpiece="${secondPlacement}"]`).click();await idle();await action('enable').click();await idle();
 await page.locator('[data-tab-id="fixture"]').click();await idle();assert.equal(await page.locator('[data-five-workpiece]').count(),2);
 assert.equal(await page.evaluate(()=>fixture.view.scene.activePlacementId),secondPlacement);
 report.cases.push('With multiple workpieces, Fixture automatically selects an enabled workpiece and filters disabled entries; re-enable restores the second workpiece to the selector');
 assert.deepEqual(report.errors,[]);report.passed=true;report.originalPlacement=before.placements[0];
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
