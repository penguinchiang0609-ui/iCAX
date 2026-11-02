import assert from 'node:assert/strict';
import {chromium} from 'playwright';
import {copyFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/rapid-settings-autosave-20261102');mkdirSync(output,{recursive:true});
const project=resolve(output,'默认空移策略自动保存-'+Date.now()+'.i5cam');copyFileSync(resolve('output/acceptance/head-colliders-20261102/browser-final/碰撞盒编辑验证.i5cam'),project);
const report={passed:false,standaloneCef:true,project,errors:[]},browser=await chromium.connectOverCDP(process.env.ICAX_CDP_URL||'http://127.0.0.1:9670');let page;
try{
 page=browser.contexts()[0].pages()[0];page.setDefaultTimeout(30000);page.on('pageerror',e=>report.errors.push(e.message));
 const close=async()=>{const closing=page.evaluate(()=>__icaxAppShell.closeProject(__icaxAppShell.getState().activeProjectId));await page.locator('[data-project-close-choice="save"]').click();await closing;};
 const previous=await page.evaluate(()=>__icaxAppShell.getState().activeProjectPath);if(previous){assert.ok(/head-colliders-20261102|rapid-settings-autosave-20261102/.test(previous),'Only close this test-owned project');await close();}
 await page.reload();await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 const open=async()=>{await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.ra=getProjectView(__icaxAppShell.getState().activeProjectId);return !!ra.scene?.domain&&!ra.pending;});await page.locator('[data-tab-id="toolpath"]').click();await page.getByRole('tab',{name:'空移刀路',exact:true}).click();await page.locator('[data-command-id="five.tool.settings"]').click();await page.waitForFunction(()=>ra.fiveRapid?.settings&&ra.fiveRapid.open);};
 await open();
 const dialog=page.locator('[data-five-key="rapid-dialog"][data-five-tool="settings"]'),field=name=>dialog.locator(`[data-five-rapid-field="${name}"]`);
 const settle=()=>page.waitForFunction(()=>!ra.pending&&!ra.fiveHistoryBusy&&!ra.fiveRapid?.saveTimer&&!ra.fiveRapid?.savingPath&&!ra.fiveRapid?.settingsChanged&&!ra.fiveRapid?.fileSaving,{},{timeout:60000});
 const read=()=>page.evaluate(async()=>{const d=await ra.viewportSceneProxy.invoke('CamScene.ReadState',{});return d.setups.find(s=>s.id===d.activeSetupId).rapidSettings;});
 assert.equal(await dialog.locator('footer,[data-five-command="five.rapid.save-settings"]').count(),0);
 await field('strategy').selectOption('Quick');await settle();const baseline=await read(),states=[baseline];
 await page.evaluate(()=>{window.raInput=document.querySelector('[data-five-rapid-field="normalRetractDistanceMm"]');window.raCanvas=ra.viewport.renderer.domElement;raInput.focus({preventScroll:true});raInput.setSelectionRange(0,1);});
 await field('normalRetractDistanceMm').fill(String(baseline.normalRetractDistanceMm+.25));await field('normalRetractDistanceMm').evaluate(n=>n.setSelectionRange(0,1));await settle();
 assert.equal(await page.evaluate(()=>document.activeElement===raInput&&raInput.selectionStart===0&&raInput.selectionEnd===1&&raCanvas===ra.viewport.renderer.domElement),true);states.push(await read());
 await field('sideRetractDistanceMm').fill(String(baseline.sideRetractDistanceMm+.5));await field('sideRetractDistanceMm').press('Tab');await settle();states.push(await read());
 await field('safeZMm').fill(String(baseline.safeZMm+5));await field('safeZMm').press('Enter');await settle();states.push(await read());
 await field('strategy').selectOption('Frog');await settle();assert.equal((await read()).strategy,'Frog');states.push(await read());
 for(let i=states.length-2;i>=0;i--){await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('edit.undo'));await settle();assert.deepEqual(await read(),states[i]);}
 for(let i=1;i<states.length;i++){await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('edit.redo'));await settle();assert.deepEqual(await read(),states[i]);}
 await field('strategy').selectOption('Quick');await settle();await field('safeZMm').fill(String(baseline.safeZMm+6));await dialog.locator('[data-five-command="five.rapid.close"]').click();await settle();assert.equal((await read()).safeZMm,baseline.safeZMm+6);
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.tool.settings'));await page.waitForFunction(()=>ra.fiveRapid?.settings&&ra.fiveRapid.open);await dialog.screenshot({path:resolve(output,'默认空移策略自动保存.png')});
 const expected=await read();await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('app.save'));await close();await open();await settle();assert.deepEqual(await read(),expected);
 report.noSaveButton=true;report.typingBlurEnterAndStrategyAutosave=true;report.closeFlushesLastEdit=true;report.focusSelectionCanvasRetained=true;report.undoSteps=states.length-1;report.redoSteps=states.length-1;report.actualProjectCloseReopen=true;assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;if(page)await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await browser.close();}
console.log(JSON.stringify(report));
