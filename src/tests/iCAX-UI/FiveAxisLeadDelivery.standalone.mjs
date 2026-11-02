import assert from 'node:assert/strict';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/lead-compensation-20261026/delivery');mkdirSync(output,{recursive:true});
const project=resolve(process.env.ICAX_LEAD_PROJECT||'output/tests/lead-compensation-20261026/standalone/连续引线与补偿.i5cam');
const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT||'http://127.0.0.1:9568'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));page.setDefaultTimeout(60000);
const report={passed:false,project,coverage:{native:true,standaloneCef:true,viewport:{width:1600,height:1000}},errors:[]};page.on('pageerror',e=>report.errors.push(e.message));
const invoke=(method,payload={})=>page.evaluate(({method,payload})=>fixture.view.viewportSceneProxy.invoke(method,payload,{timeoutMs:120000}),{method,payload});
try{
 await page.setViewportSize({width:1600,height:1000});await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
 await page.waitForFunction(()=>__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId);
 await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};});
 await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.viewport?.sceneObjects.size>0);
 await page.locator('[data-tab-id="toolpath"]').click();await page.waitForFunction(()=>!fixture.view.pending);
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.cam-page.cutting'));
 await page.waitForFunction(()=>!fixture.view.fiveCompensation?.loading&&fixture.view.fiveCompensation?.items.length>0);
 const state=await invoke('CamScene.ReadState'),ids=state.intentToolpaths.filter(c=>c.machiningKind==='Cut').map(c=>c.id);
 const saved=await invoke('CamProcess.PreviewLeads',{expected:state.expected,intentIds:ids});assert.equal(saved.items.length,3);assert.ok(saved.items.every(i=>i.leads.length===2));
 const id=await page.evaluate(()=>fixture.view.fiveCompensation.items[0].intentId);
 const exact=(await invoke('CamProcess.PreviewCompensation',{expected:state.expected,intentIds:[id]})).items[0];assert.ok(!exact.error,exact.error);
 const bounds=ps=>[0,1,2].map(i=>[Math.min(...ps.map(p=>p[i])),Math.max(...ps.map(p=>p[i]))]);
 assert.notDeepEqual(bounds(exact.nominalPoints),bounds(exact.parts.find(p=>p.role==='Cut').points));
 report.display=await page.evaluate(id=>{const v=fixture.view,source=v.fiveCutPreview.group.children.find(n=>n.userData.cuttingLineId===id),parts=[];v.fiveCompensationOverlay.group.traverse(n=>{if(n.isLine&&n.userData.compensatedIntentId===id)parts.push({role:n.userData.role,count:n.geometry.attributes.position.count,opacity:n.material.opacity,visible:n.visible});});return {originalOpacity:source.material.opacity,originalPath:source.userData.originalPath,parts};},id);
 assert.equal(report.display.originalPath,true);assert.ok(report.display.originalOpacity<1);for(const role of ['Cut','LeadIn','LeadOut'])assert.ok(report.display.parts.some(p=>p.role===role&&p.count>1&&p.opacity===1&&p.visible));
 await page.locator(`[data-five-intent="${id}"]`).click();await page.waitForFunction(()=>!fixture.view.pending);await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.tool.compensation'));
 await page.evaluate(id=>{const v=fixture.view,vp=v.viewport,c=v.scene.intentToolpaths.find(c=>c.id===id),o=vp.sceneObjects.get(c.placementId);o.updateWorldMatrix(true,false);const p=vp.camera.position.clone().set(...c.startPoint).applyMatrix4(o.matrixWorld);vp.setViewDirection([0,0,1]);vp.setCameraState({...vp.getCameraState(),target:{x:p.x,y:p.y,z:p.z},radius:24});},id);
 await page.screenshot({path:resolve(output,'最终引线补偿.png')});
 report.startup=await page.evaluate(()=>__icaxAppShell.getState());assert.equal(report.startup.error,'');assert.equal(report.startup.bridgeStatus,'Connected');assert.deepEqual(report.errors,[]);
 report.nominalBounds=bounds(exact.nominalPoints);report.compensatedBounds=bounds(exact.parts.find(p=>p.role==='Cut').points);report.materialSides=state.intentToolpaths.map(c=>({id:c.id,side:c.retainedSide}));report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,error:report.error,startup:report.startup?.startupPhase}));await browser.close();}
