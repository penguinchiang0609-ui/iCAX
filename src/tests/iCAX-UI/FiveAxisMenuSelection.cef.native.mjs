import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,copyFileSync,writeFileSync} from 'node:fs';
import {chromium} from 'playwright';
const output=resolve('output/tests/menu-selection'),base=pathToFileURL(resolve('output/tests/job-sequence/cef-runtime')+'/').href;
mkdirSync(output,{recursive:true});
const browser=await chromium.connectOverCDP('http://127.0.0.1:9476'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');await page.setViewportSize({width:1600,height:950});page.setDefaultTimeout(20000);
const report={passed:false,standaloneCef:true,cases:[],errors:[]};page.on('pageerror',e=>report.errors.push(e.message));
page.on('crash',()=>writeFileSync(resolve(output,'renderer-crash.json'),JSON.stringify({at:Date.now()})));
page.on('close',()=>writeFileSync(resolve(output,'browser-close.json'),JSON.stringify({connected:browser.isConnected(),pages:browser.contexts().flatMap(c=>c.pages()).map(p=>p.url())})));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fixtureRequest&&!fixture.view.fiveSetupRequest&&(!fixture.view.scene?.model?.isLoaded||fixture.view.viewport?.sceneObjects.get(fixture.view.scene.activePlacementId)?.geometry));
const command=async id=>{console.log(id);await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
const button=id=>page.locator('[data-command-id="'+id+'"]');
const click=async id=>{console.log('Click '+id);await page.locator('[data-five-command="'+id+'"]').first().click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
const enabled=async(...ids)=>{for(const id of ids)assert.equal(await button(id).isEnabled(),true,id+' enabled');};
const disabled=async(...ids)=>{for(const id of ids)assert.equal(await button(id).isDisabled(),true,id+' disabled');};
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}));
async function open(source){const path=resolve(output,Date.now()+'-'+source.split('/').at(-1));copyFileSync(resolve(source),path);await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),path);await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);await idle();await page.locator('[data-tab-id="workpiece"]').click();await idle();}
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');await open('output/tests/preparation-tree/four-groups/夹具复制.i5cam');
 let state=await read();const active=await page.evaluate(()=>fixture.view.scene.activePlacementId),workpiece=state.workpieces[0],p=state.placements.find(p=>p.id===active),unit=state.fixtureUnits.find(u=>u.setupId===p.setupId);
 await click('five.preparation.workpiece:'+workpiece.id);
 await enabled('workpiece.import','workpiece.remove','five.preparation.copy:workpiece','five.tool.scale','five.tool.align','five.setup.create','five.fixture.library.open');
 await disabled('five.setup.remove','five.preparation.copy:setup','five.tool.translate','five.tool.registration','five.fixture.add','five.fixture.remove','five.preparation.copy:fixture','five.fixture.focus.check');
 const before=await read();await command('five.tool.rotate');await command('five.setup.remove');await command('five.fixture.remove');assert.equal(await page.evaluate(()=>fixture.view.fivePreparationKind),'workpiece');assert.deepEqual((await read()).setups,before.setups);assert.deepEqual((await read()).fixtureUnits,before.fixtureUnits);
 await page.screenshot({path:resolve(output,'workpiece-selected.png')});
 await click('five.setup.activate:'+p.id);await enabled('five.tool.translate','five.tool.registration','five.setup.create','five.fixture.add','five.fixture.focus.check');await disabled('workpiece.remove','five.tool.scale','five.tool.align','five.fixture.remove','five.preparation.copy:fixture');
 await click('five.setup.lock:'+p.id);await disabled('five.tool.translate','five.fixture.add');await enabled('five.tool.registration');await click('five.setup.lock:'+p.id);
 await click('five.preparation.unit:'+unit.id);await enabled('five.fixture.add','five.fixture.remove','five.preparation.copy:fixture','five.fixture.focus.check');await disabled('workpiece.remove','five.tool.scale','five.tool.align','five.setup.create','five.setup.remove','five.tool.translate','five.tool.registration');
 await page.screenshot({path:resolve(output,'fixture-selected.png')});
 await click('five.preparation.unit-action:lock:'+unit.id);await disabled('five.fixture.remove');await click('five.preparation.unit-action:lock:'+unit.id);
 await click('five.preparation.workpiece-action:lock:'+workpiece.id);await disabled('workpiece.remove','five.tool.scale','five.tool.align');await click('five.preparation.workpiece-action:lock:'+workpiece.id);
 await click('five.preparation.workpiece-action:enable:'+workpiece.id);await disabled('five.tool.scale','five.tool.align','five.setup.create','five.preparation.copy:workpiece');await page.locator('[data-tab-id="toolpath"]').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.fiveAxisArea),'toolpath');await disabled('five.tool.projection','five.tool.split','five.tool.path-start','five.axes.reverse-all');assert.match(await page.locator('[data-five-key="cam-preparation"]').textContent(),/已禁用/);
 await page.locator('[data-tab-id="workpiece"]').click();await idle();await click('five.preparation.workpiece-action:enable:'+workpiece.id);
 report.cases.push('Actual tree selection links preparation menus to workpiece/setup/unit; row lock and enable states update immediately, and calling disabled commands directly cannot mutate another level');

 await click('five.setup.activate:'+p.id);await button('five.setup.create').click();await idle();
 assert.equal(await page.locator('[data-tab-id="toolpath"]').isVisible(),true);await page.locator('[data-tab-id="toolpath"]').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.fiveAxisArea),'toolpath');await enabled('five.tool.projection');await disabled('five.tool.split','five.tool.path-start','five.tool.simulation');assert.equal(await page.locator('[data-five-key="cam-preparation"]').count(),0);
 await button('five.tool.projection').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.fiveAxisArea),'toolpath');await page.screenshot({path:resolve(output,'cam-visible-new-setup.png')});
 report.cases.push('New setups immediately permit CAM preparation; path-editing and simulation commands remain disabled until their actual inputs exist');

 await open('output/tests/preparation-tree/four-groups/工序顺序与复制.i5cam');state=await read();const ready=state.setups.find(s=>s.generatedGeometry?.operations?.length),rp=state.placements.find(p=>p.setupId===ready.id);await click('five.setup.activate:'+rp.id);await page.locator('[data-tab-id="toolpath"]').click();await idle();
 await disabled('five.tool.path-start','five.tool.leads','five.tool.compensation','five.points.whole','five.points.add');await enabled('five.pose-select.toggle','five.axes.reverse-all');
 const cut=await page.evaluate(()=>fixture.view.scene.intentToolpaths.find(c=>!c.locked&&c.enabled!==false).id);await page.locator('[data-five-intent="'+cut+'"]').click();await idle();await enabled('five.tool.path-start','five.tool.leads','five.tool.compensation','five.points.whole','five.points.add');await page.screenshot({path:resolve(output,'cam-cut-selected.png')});
 await command('five.tool.compensation');await idle();assert.ok(await page.locator('.cam-info-pane input').count());
 await page.addStyleTag({content:'.menu-scroll-test .cam-context-pane::after,.menu-scroll-test .cam-info-pane::after{content:"";display:block;height:1000px}'});
 await page.evaluate(()=>{const proxy=fixture.view.viewportSceneProxy;fixture.invoke=proxy.invoke.bind(proxy);proxy.invoke=async(method,...args)=>{const result=await fixture.invoke(method,...args);if(method==='CamScene.ReadState')await new Promise(r=>fixture.release=r);return result;};});
 const pending=command('five.refresh');await page.waitForFunction(()=>!!fixture.release);await disabled('five.tool.projection','five.tool.path-start','five.points.add');
 const positions=await page.evaluate(()=>{document.body.classList.add('menu-scroll-test');const e=document.querySelector('.cam-info-pane input:not([readonly])');fixture.editor=e;fixture.canvas=fixture.view.viewport.renderer.domElement;e.focus({preventScroll:true});e.setSelectionRange(0,1);return ['.cam-context-pane','.cam-info-pane'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=40+i*10;return n.scrollTop;});});
 await page.evaluate(()=>{fixture.view.viewportSceneProxy.invoke=fixture.invoke;fixture.release();});await pending;assert.equal(await page.evaluate(()=>fixture.editor===document.activeElement&&fixture.editor.isConnected&&fixture.editor.selectionStart===0&&fixture.editor.selectionEnd===1&&fixture.canvas===fixture.view.viewport.renderer.domElement),true);assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane'].map(s=>document.querySelector(s).scrollTop)),positions);await enabled('five.tool.path-start','five.points.add');
 report.cases.push('CAM edits require an enabled unlocked selected cut; global pose selection remains available; delayed real refresh disables busy commands and restores them without replacing the focused input/canvas or losing caret/scroll positions');
 await page.evaluate(path=>__icaxAppShell.createProject({projectName:'空项目菜单检查',projectPath:path,productId:'icax.five-axis-cam'}),resolve(output,Date.now()+'-empty.i5cam'));
 await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);await idle();await page.locator('[data-tab-id="toolpath"]').click();await idle();await disabled('five.tool.projection','five.tool.split','five.points.add','five.job.open');assert.match(await page.locator('[data-five-key="cam-preparation"]').textContent(),/导入工件/);
 report.cases.push('A new empty project also keeps CAM visible and accessible, with unavailable actions disabled and an import prompt');
 assert.deepEqual(report.errors,[]);report.passed=true;writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
}catch(error){report.failure=error.stack;writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{await browser.close();}
