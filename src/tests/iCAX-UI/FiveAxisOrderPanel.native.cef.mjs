import assert from 'node:assert/strict';
import {copyFileSync,existsSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {restoreNativeCefViewport} from './nativeCefViewport.mjs';
const output=resolve('output/acceptance/order-panel-20261101/native');mkdirSync(output,{recursive:true});
const project=resolve(output,`排序空移粗估验收-${Date.now()}.i5cam`);
const report={passed:false,coverage:{standaloneCef:true,nativeScene:true,realModel:true,machine:false},cases:[]};
const browser=await chromium.connectOverCDP(process.env.ICAX_ORDER_CDP_URL||'http://127.0.0.1:9589');let page;
try{
 page=browser.contexts()[0].pages().find(p=>p.url().includes('AppShell'))||browser.contexts()[0].pages()[0];page.setDefaultTimeout(30000);
 await restoreNativeCefViewport(page);
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 if(await page.evaluate(output=>String(__icaxAppShell.getState().activeProjectPath||'').replaceAll('\\','/').startsWith(output.replaceAll('\\','/')+'/'),output)){
  await page.evaluate(()=>{window.orderTestClosing=__icaxAppShell.closeProject(__icaxAppShell.getState().activeProjectId);});
  const discard=page.locator('[data-project-close-choice="discard"]');await discard.waitFor({timeout:1500}).catch(()=>{});if(await discard.isVisible())await discard.click();await page.evaluate(()=>orderTestClosing);
 }
 const source=resolve('output/projects/CAM-工作副本-20261101.i5cam');copyFileSync(source,project);
 // A quick-save journal belongs to the archive. A new filename prevents an
 // earlier acceptance run's journal from replaying over the fresh fixture.
 if(existsSync(source+'.log'))copyFileSync(source+'.log',project+'.log');
 await page.evaluate(project=>__icaxAppShell.openProject(project,'icax.five-axis-cam'),project);
 await page.waitForFunction(async project=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.orderNative=getProjectView(__icaxAppShell.getState().activeProjectId);return __icaxAppShell.getState().activeProjectPath===project&&orderNative.scene?.intentToolpaths?.length===31&&!orderNative.pending;},project,{timeout:180000});
 await page.locator('[data-tab-id="toolpath"]').click();
 await page.waitForFunction(()=>orderNative.fiveAxisArea==='toolpath'&&!!document.querySelector('[data-command-id="five.cam-page.order"]'));
 await page.locator('[data-command-id="five.cam-page.order"]').click();
 await page.locator('.product-ribbon [data-command-id="five.tool.auto-order"]').waitFor();
 const idle=()=>page.waitForFunction(()=>!orderNative.pending&&!orderNative.fiveOrder?.request&&!orderNative.fiveOrder?.running,null,{timeout:300000});
 const action=id=>page.locator('[data-five-command="five.order.'+id+'"]');
 const read=()=>page.evaluate(()=>orderNative.viewportSceneProxy.invoke('CamScene.ReadState',{}));
 const before=await read();const initial=before.setups.find(s=>s.id===before.activeSetupId);report.sourceCounts={cuts:before.intentToolpaths.length,groups:initial.cutGroups.length,generated:initial.generatedGeometry.status};
 const viewport=await page.locator('.cam-viewport').boundingBox();
 await page.locator('.ribbon-command-menu-toggle[data-command-id="five.tool.auto-order"]').click();
 const field=page.locator('[data-ribbon-input="five.order.height-penalty"]');assert.equal(await field.inputValue(),'3');await field.fill('1.5');await page.keyboard.press('Enter');
 assert.equal(await page.locator('[data-five-key="auto-order-panel"]').count(),0);
 await page.evaluate(()=>{orderNative.testCanvas=orderNative.viewport.renderer.domElement;});
 const started=Date.now();await page.locator('.product-ribbon .ribbon-command[data-command-id="five.tool.auto-order"]').click();await idle();report.sortMs=Date.now()-started;
 assert.equal(await page.evaluate(()=>orderNative.error||''),'');const state=await read();
 for(const key of ['workpieces','placements','fixtures','fixtureUnits','intentToolpaths'])assert.deepEqual(state[key],before[key],key+' changed');
 assert.equal(await page.evaluate(()=>orderNative.testCanvas===orderNative.viewport.renderer.domElement),true);assert.equal((await page.locator('.cam-viewport').boundingBox()).width,viewport.width);
 const setup=state.setups.find(s=>s.id===state.activeSetupId);assert.equal(setup.generatedGeometry.ordering.options.heightPenaltyWeight,1.5);assert.equal(setup.generatedGeometry.status,initial.generatedGeometry.status);
 const geometryById=g=>g.operations.map(({order,...op})=>op).sort((a,b)=>a.intentId.localeCompare(b.intentId));assert.deepEqual(geometryById(setup.generatedGeometry),geometryById(initial.generatedGeometry));
 report.after={cuts:state.intentToolpaths.length,generated:setup.generatedGeometry.status,rapidCount:setup.generatedGeometry.rapidGeneratedCount,clearCount:setup.generatedGeometry.rapidClearCount,options:setup.generatedGeometry.ordering.options};
 report.cases.push('One-click sorting uses the ribbon height parameter on the real model and preserves authored starts, machining paths, CAD, placements and fixtures');
 await page.screenshot({path:resolve(output,'应用后的排序参数.png')});report.project=project;report.passed=true;
}catch(error){report.error=error.stack;if(page)await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{
 try{if(page&&!page.isClosed())report.nativeViewport=await restoreNativeCefViewport(page);}
 catch(error){report.passed=false;report.viewportRestoreError=error.message;}
 writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,sortMs:report.sortMs,optimization:report.optimization,geometryComparison:report.geometryComparison,error:report.error?.split('\n')[0]}));await browser.close();
}
