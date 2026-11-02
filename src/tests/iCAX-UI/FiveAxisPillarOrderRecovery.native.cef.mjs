import assert from 'node:assert/strict';
import {copyFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {restoreNativeCefViewport} from './nativeCefViewport.mjs';

const root=resolve('output/tests/order-pillar-recovery-20261101');
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||resolve(root,'cef'));mkdirSync(output,{recursive:true});
const project=resolve(output,`pillar-${Date.now()}.i5cam`);
copyFileSync(resolve(root,'baseline.i5cam'),project);
copyFileSync(resolve(root,'baseline.i5cam.log'),project+'.log');
const report={passed:false,standaloneCef:true,realProject:true,machineValidation:false};
const browser=await chromium.connectOverCDP(process.env.ICAX_ORDER_CDP_URL||'http://127.0.0.1:9628');
const page=browser.contexts()[0].pages().find(p=>p.url().includes('AppShell'));page.setDefaultTimeout(30000);
try{
 await restoreNativeCefViewport(page);
 await page.waitForFunction(()=>__icaxAppShell.getState().startupPhase==='ready');
 if(await page.evaluate(output=>String(__icaxAppShell.getState().activeProjectPath||'').replaceAll('\\','/').startsWith(output.replaceAll('\\','/')+'/'),output)){
  await page.evaluate(()=>{window.pillarOrderClosing=__icaxAppShell.closeProject(__icaxAppShell.getState().activeProjectId);});
  const discard=page.locator('[data-project-close-choice="discard"]');
  await discard.waitFor({timeout:1500}).catch(()=>{});if(await discard.isVisible())await discard.click();
  await page.evaluate(()=>pillarOrderClosing);
 }
 await page.evaluate(project=>__icaxAppShell.openProject(project,'icax.five-axis-cam'),project);
 await page.waitForFunction(async()=>{
  const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');
  window.pillarOrderView=getProjectView(__icaxAppShell.getState().activeProjectId);
  return pillarOrderView.scene?.intentToolpaths?.length===21&&!pillarOrderView.pending;
 },null,{timeout:180000});
 console.log('Opened 21-cut project');
 await page.evaluate(()=>{
  const proxy=pillarOrderView.viewportSceneProxy.sdoClient,own=Object.getOwnPropertyDescriptor(proxy,'invoke'),invoke=proxy.invoke;
  const probe=window.pillarOrderProbe={calls:[],proxy,own,started:performance.now()};
  proxy.invoke=async function(channel,method,...args){const started=performance.now();const call={method,payload:structuredClone(args[0]),requested:started-probe.started};probe.calls.push(call);
   try{return await invoke.call(this,channel,method,...args);}catch(e){call.error=e.message;throw e;}
   finally{call.milliseconds=performance.now()-started;}
  };
 });
 const read=()=>page.evaluate(()=>pillarOrderView.viewportSceneProxy.invoke('CamScene.ReadState',{},{timeoutMs:180000}));
 const before=await read();report.project=project;report.cutCount=before.intentToolpaths.length;
 await page.locator('[data-tab-id="toolpath"]').click();
 await page.locator('[data-command-id="five.cam-page.order"]').click();
 const automatic=page.locator('.product-ribbon .ribbon-command[data-command-id="five.tool.auto-order"]'),toggle=page.locator('.ribbon-command-menu-toggle[data-command-id="five.tool.auto-order"]'),weight=page.locator('[data-ribbon-input="five.order.height-penalty"]');
 const idle=()=>page.waitForFunction(()=>!pillarOrderView.pending&&!pillarOrderView.fiveOrder?.running&&!pillarOrderView.fiveOrder?.request&&!pillarOrderView.fiveTreeRequest,null,{timeout:120000});
 const order=async()=>{const d=await read();return d.setups.find(s=>s.id===d.activeSetupId).definitionOrder;};
 const button=(action,id)=>page.locator('[data-five-command="five.tree.'+action+':'+id+'"]');
 const setPage=async id=>{await page.locator('[data-command-id="five.cam-page.'+id+'"]').click();await idle();};
 const verifyLabels=async()=>{
  const state=await page.evaluate(()=>{
   const v=pillarOrderView,cuts=v.scene.intentToolpaths,setup=v.scene.domain.setups.find(s=>s.id===v.scene.domain.activeSetupId),byId=new Map(cuts.map(c=>[c.id,c])),enabled=setup.definitionOrder.filter(id=>byId.get(id).enabled!==false);
   return {rows:setup.definitionOrder.map(id=>{const c=byId.get(id);return {id,name:c.name,number:c.number,sequence:enabled.includes(id)?String(enabled.indexOf(id)+1):'',label:document.querySelector(`[data-five-intent="${id}"] .five-cut-name`).textContent};}),scene:[...document.querySelectorAll('.five-path-number')].map(el=>({id:el.dataset.fiveIntentId,text:el.textContent})),selected:v.fiveAxisUI.selectedIntentId,header:document.querySelector('.five-cut-parameters>header')?.textContent};
  });
  for(const row of state.rows)assert.equal(row.label,row.name);
  assert.ok(state.scene.length>0);for(const row of state.scene)assert.equal(row.text,state.rows.find(c=>c.id===row.id).sequence);
  const selected=state.rows.find(c=>c.id===state.selected);if(selected){assert.equal(state.header,selected.label);assert.ok((await page.locator('.five-cut-parameters').textContent()).includes('固定编号 #'+selected.number));}
  report.namesWithoutNumberSuffix=true;
 };
 const verifyPages=async()=>{
  for(const id of ['cutting','rapid','process','order']){
   await setPage(id);
   assert.equal(await page.locator('.five-cut-move').count(),id==='order'?21*4:0);
   assert.equal(await page.locator('.five-cut-row-controls button:not(.five-cut-move)').count(),id==='cutting'?21*2:0);
   assert.equal(await page.locator('[data-command-id="five.tree.Group"],[data-command-id="five.tree.Ungroup"]').count(),0);
   assert.equal(await page.getByText('组织',{exact:true}).count(),0);
  }
  report.pageSpecificControls=true;
 };
 const act=async(action,id)=>{
  const previous=await page.evaluate(()=>JSON.stringify(pillarOrderView.scene.domain.expected)),start=Date.now(),callCount=await page.evaluate(()=>pillarOrderProbe.calls.length);
  await button(action,id).click();await page.waitForFunction(previous=>JSON.stringify(pillarOrderView.scene.domain.expected)!==previous,previous,{timeout:120000});await idle();
  if(action.startsWith('Row')){(report.rowMoveMilliseconds??=[]).push({action,milliseconds:Date.now()-start});const calls=await page.evaluate(i=>pillarOrderProbe.calls.slice(i),callCount);assert.equal(calls.filter(c=>c.method==='CamCut.ManageTree').length,1);assert.equal(calls.filter(c=>c.method==='CamPlan.QueryOrder').length,1);assert.equal(calls.filter(c=>c.method==='CamScene.ReadState').length,0);}
  await verifyLabels();
 };
 assert.equal(await page.locator('[data-five-key="auto-order-panel"],[data-five-key="order-inspector-tabs"]').count(),0);
 await toggle.click();assert.equal(await weight.inputValue(),'3');await weight.fill('12.5');await page.keyboard.press('Enter');
 await page.waitForFunction(()=>!pillarOrderView.fiveToolAxisDisplay?.loading,null,{timeout:60000});
 await page.waitForFunction(()=>{const c=pillarOrderView.viewport.camera,k=JSON.stringify([c.position.toArray(),c.quaternion.toArray()]),p=pillarOrderProbe;if(p.cameraReadyKey!==k){p.cameraReadyKey=k;p.cameraReadyAt=performance.now();return false;}return performance.now()-p.cameraReadyAt>300;});
 await page.evaluate(()=>{pillarOrderProbe.canvas=pillarOrderView.viewport.renderer.domElement;pillarOrderProbe.camera={position:pillarOrderView.viewport.camera.position.toArray(),quaternion:pillarOrderView.viewport.camera.quaternion.toArray()};});
 const started=Date.now();await automatic.click();await idle();report.sortMilliseconds=Date.now()-started;
 assert.equal(await page.evaluate(()=>pillarOrderView.error||''),'');
 let after=await read();for(const key of ['workpieces','placements','fixtures','fixtureUnits','intentToolpaths'])assert.deepEqual(after[key],before[key],key+' changed');
 assert.equal(after.setups.find(s=>s.id===after.activeSetupId).generatedGeometry.ordering.options.heightPenaltyWeight,12.5);
 assert.equal(await page.evaluate(()=>pillarOrderProbe.canvas===pillarOrderView.viewport.renderer.domElement),true);
 assert.deepEqual(await page.evaluate(()=>({position:pillarOrderView.viewport.camera.position.toArray(),quaternion:pillarOrderView.viewport.camera.quaternion.toArray()})),await page.evaluate(()=>pillarOrderProbe.camera));
 assert.equal(await page.evaluate(()=>pillarOrderProbe.calls.some(c=>c.method==='CamPlan.Generate')),false);
 await setPage('cutting');assert.equal(await page.locator('.five-cut-row-controls button:not(.five-cut-move)').count(),42);
 assert.equal(await page.locator('.five-cut-row-controls button:not(.five-cut-move)').evaluateAll(es=>es.every(e=>!e.disabled)),true);
 await verifyPages();
 report.directSorting=true;report.canvasAndCameraPreserved=true;
 // Every action targets its own row, including a row that is not selected.
 let ids=await order();const moved=ids[5],initial=ids;
 await page.locator('[data-five-intent="'+initial[0]+'"]').click();await verifyLabels();
 await act('RowUp',moved);ids=[...ids];[ids[4],ids[5]]=[ids[5],ids[4]];assert.deepEqual(await order(),ids);
 await act('RowDown',moved);assert.deepEqual(await order(),initial);
 await act('RowTop',moved);ids=[moved,...initial.filter(id=>id!==moved)];assert.deepEqual(await order(),ids);assert.equal(await button('RowTop',moved).isDisabled(),true);assert.equal(await button('RowUp',moved).isDisabled(),true);
 await act('RowBottom',moved);ids=[...initial.filter(id=>id!==moved),moved];assert.deepEqual(await order(),ids);assert.equal(await button('RowBottom',moved).isDisabled(),true);assert.equal(await button('RowDown',moved).isDisabled(),true);
 report.rowMoves=true;
 const lockId=ids[2];await setPage('cutting');await act('ToggleLocked',lockId);await setPage('order');
 for(const action of ['RowUp','RowDown','RowTop','RowBottom'])assert.equal(await button(action,lockId).isDisabled(),true);
 const blocked=ids[3];assert.equal(await button('RowUp',blocked).isDisabled(),true);assert.equal(await button('RowTop',blocked).isDisabled(),true);
 // Top stops after the locked row; it cannot cross or move the anchor.
 const bounded=ids[6];await act('RowTop',bounded);ids=ids.filter(id=>id!==bounded);ids.splice(3,0,bounded);assert.deepEqual(await order(),ids);assert.equal((await order())[2],lockId);
 await setPage('cutting');await act('ToggleLocked',lockId);await setPage('order');assert.equal(await button('RowUp',blocked).isEnabled(),true);await setPage('cutting');
 await act('ToggleEnabled',blocked);assert.equal((await read()).intentToolpaths.find(c=>c.id===blocked).enabled,false);
 await act('ToggleEnabled',blocked);assert.equal((await read()).intentToolpaths.find(c=>c.id===blocked).enabled,true);
 report.enableAndLockRecovered=true;report.lockBoundaryPreserved=true;
 after=await read();for(const key of ['workpieces','placements','fixtures','fixtureUnits','intentToolpaths'])assert.deepEqual(after[key],before[key],key+' changed after row moves');
 report.geometryAndStartsPreserved=true;
 await setPage('order');
 await page.screenshot({path:resolve(output,'row-controls.png')});
 await page.evaluate(()=>{window.orderClosing=__icaxAppShell.closeProject(__icaxAppShell.getState().activeProjectId);});
 const saveChoice=page.locator('[data-project-close-choice="save"]');await saveChoice.waitFor({timeout:2000}).catch(()=>{});if(await saveChoice.isVisible())await saveChoice.click();await page.evaluate(()=>orderClosing);
 await page.evaluate(project=>__icaxAppShell.openProject(project,'icax.five-axis-cam'),project);
 await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.pillarOrderView=getProjectView(__icaxAppShell.getState().activeProjectId);return pillarOrderView.scene?.intentToolpaths?.length===21&&!pillarOrderView.pending;},null,{timeout:180000});
 const reopened=await read();assert.deepEqual(reopened.setups.find(s=>s.id===reopened.activeSetupId).definitionOrder,ids);
 for(const key of ['workpieces','placements','fixtures','fixtureUnits','intentToolpaths'])assert.deepEqual(reopened[key],before[key],key+' changed after reopen');
 report.savedAndReopened=true;report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{
 report.calls=await page.evaluate(()=>{const p=window.pillarOrderProbe;if(!p)return [];if(p.own)Object.defineProperty(p.proxy,'invoke',p.own);else delete p.proxy.invoke;return p.calls;}).catch(()=>[]);
 await restoreNativeCefViewport(page).catch(()=>{});writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();
}
