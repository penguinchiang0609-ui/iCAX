import assert from 'node:assert/strict';
import {chromium} from 'playwright';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

const output=resolve('output/tests/pending-rapid-and-head-size-20261101');mkdirSync(output,{recursive:true});
const report={passed:false,standaloneCef:true,cases:[]};
const browser=await chromium.connectOverCDP(process.env.ICAX_PENDING_RAPID_CDP||'http://127.0.0.1:9631');let page;
try{
 page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('AppShell'));assert.ok(page);page.setDefaultTimeout(30000);
 const activePath=await page.evaluate(()=>__icaxAppShell.getState().activeProjectPath);
 assert.ok(!activePath||activePath.includes('pending-rapid-and-head-size-20261101')&&activePath.includes('head-size-audit'),'Only an owned audit project may be used');
 const project=activePath||resolve(output,'head-size-audit-1793546881380.i5cam');report.project=project;
 // Load the current production modules into this isolated acceptance window.
 await page.reload();await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
 await page.waitForFunction(async path=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.pendingRapidView=getProjectView(__icaxAppShell.getState().activeProjectId);const d=pendingRapidView?.scene?.domain;return __icaxAppShell.getState().activeProjectPath===path&&d?.setups?.some(s=>s.id===d.activeSetupId)&&pendingRapidView.scene.intentToolpaths?.length>1&&!pendingRapidView.pending;},project,{timeout:120000});
 const read=()=>page.evaluate(()=>pendingRapidView.viewportSceneProxy.invoke('CamScene.ReadState',{})),before=await read(),setup=before.setups.find(s=>s.id===before.activeSetupId);
 assert.equal(setup.generatedGeometry.status,'Missing');report.nativeGeometryStatus='Missing';
 await page.locator('[data-tab-id="toolpath"]').click();await page.locator('[data-command-id="five.cam-page.rapid"]').click();
 const model=await page.evaluate(async()=>{const {rapidListModel}=await import('../../../apps/five-axis-cam/webpage/rapidListModel.mjs');return rapidListModel(pendingRapidView);});
 assert.ok(model.transitions.length>0);assert.equal(model.transitions.length,model.operations.length-1);
 const rows=page.locator('[data-five-rapid-id]');assert.equal(await rows.count(),model.transitions.length);
 for(const text of await rows.allTextContents())assert.match(text,/尚未生成|未生成/);
 const selected=model.transitions.at(-1),id=`${selected.from}/${selected.to}`;
 await page.locator(`[data-five-rapid-id="${id}"]`).click();assert.equal(await page.locator('[data-five-rapid-field="connection"]').inputValue(),id);
 assert.equal(await page.locator(`[data-five-rapid-id="${id}"]`).getAttribute('aria-pressed'),'true');
 assert.match(await page.locator('.cam-info-pane').innerText(),/未生成/);assert.equal(await page.locator('[data-five-command="five.rapid.path-edit"]').count(),0);
 report.pendingConnections={operations:model.operations.length,rows:model.transitions.length,selected:id};report.cases.push('Real native ungenerated toolpath definitions produce selectable adjacent rapid rows and a pending status in production CEF');
 // Read the production motion snapshot and load its immutable display resources.
 report.headDisplay=await page.evaluate(async()=>{
  const v=pendingRapidView,s=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId),head=s.headModel;
  const snapshot=await v.viewportSceneProxy.invoke('CamSimulation.ReadMachine',{setupId:s.id});
  const THREE=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js'),{machineForward}=await import('../../../apps/five-axis-cam/webpage/machineKinematics.mjs'),{loadCuttingHeadPreview}=await import('../../../apps/five-axis-cam/webpage/cuttingHeadPreview.mjs');
  const world=machineForward(snapshot,snapshot.positions),tcp=new THREE.Vector3(...snapshot.configuration.tcp).applyMatrix4(world[snapshot.tool]);
  const loaded=await loadCuttingHeadPreview(v.viewportSceneProxy.resources,head);assertGroup(loaded);
  function assertGroup(result){if(!result?.group)throw Error('Head display resource did not load');}
  const box=new THREE.Box3().setFromObject(loaded.group),result={model:head.name,sizeMm:box.getSize(new THREE.Vector3()).toArray(),scale:loaded.group.scale.toArray(),initialTcpMm:tcp.toArray(),placement:snapshot.configuration.placement};
  loaded.group.traverse(o=>{o.geometry?.dispose();for(const m of Array.isArray(o.material)?o.material:[o.material])m?.dispose();});return result;
 });
 assert.deepEqual(report.headDisplay.scale,[1,1,1]);assert.ok(Math.abs(report.headDisplay.initialTcpMm[2]-500)<1e-7);
 report.cases.push('Production display resources retain unit scale; the native independent-head initial TCP is at Z=500 mm');
 const after=await read();assert.deepEqual(after,before);report.cases.push('Opening and selecting pending rows and reading head geometry do not change CAD, toolpaths, calibration or native engineering state');
 await page.screenshot({path:resolve(output,'未生成空移连接-独立程序.png')});report.passed=true;
}catch(error){report.error=error.stack;if(page)await page.screenshot({path:resolve(output,'pending-cef-failure.png')}).catch(()=>{});throw error;}
finally{writeFileSync(resolve(output,'pending-cef-report.json'),JSON.stringify(report,null,2));await browser.close();}
console.log(JSON.stringify(report,null,2));
