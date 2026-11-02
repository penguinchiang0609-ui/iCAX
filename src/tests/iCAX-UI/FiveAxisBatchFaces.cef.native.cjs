const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),{createHash}=require('node:crypto');
const {chromium}=require('playwright');
// Run only against a dedicated acceptance instance: this opens a test project.
const endpoint=process.env.ICAX_CEF_ENDPOINT;
if(!endpoint)throw Error('Set ICAX_CEF_ENDPOINT to an isolated acceptance instance.');
const project=path.resolve(process.env.ICAX_FIXTURE_TEST_PROJECT||'output/acceptance/fixture-pillar-performance-20261101/cef-responsibilities.i5cam');
const output=path.resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/cam-batch-face-query-20261101/cef');
fs.mkdirSync(output,{recursive:true});
const hash=filename=>createHash('sha256').update(fs.readFileSync(filename)).digest('hex');
const report={passed:false,standaloneCEF:true,realNativeSDO:true,project,projectHashBefore:hash(project),cases:[],errors:[]};
(async()=>{
 const browser=await chromium.connectOverCDP(endpoint),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));
 assert.ok(page);await page.setViewportSize({width:1600,height:1000});page.setDefaultTimeout(120000);page.on('pageerror',e=>report.errors.push(e.message));
 try{
  await page.waitForFunction(()=>__icaxAppShell?.getState().startupPhase==='ready');
  await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),project);
  await page.waitForFunction(()=>__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId);
  await page.evaluate(async()=>{
   const base=new URL('../../../',location.href).href,{getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');
   const actions=await import(base+'apps/five-axis-cam/webpage/extractionActions.mjs');
   window.batchFaces={view:getProjectView(__icaxAppShell.getState().activeProjectId),actions,calls:[]};
  });
  const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!batchFaces.view.pending&&!batchFaces.view.fixtureRequest);
  const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();assert.equal(await page.evaluate(()=>batchFaces.view.error),'');};
  await idle();await page.locator('[data-tab-id="toolpath"]').click();await idle();await command('five.cam-page.cutting');
  await page.waitForFunction(()=>batchFaces.view.viewport.sceneObjects.get(batchFaces.view.scene.activePlacementId)?.geometry);
  await page.evaluate(()=>{
   const f=batchFaces,proxy=f.view.viewportSceneProxy;f.proxy=proxy;f.original=proxy.invoke;
   proxy.invoke=async function(method,payload,...options){
    const result=await f.original.call(this,method,payload,...options);
    f.calls.push({method,bytes:JSON.stringify(result).length,keys:Object.keys(result.result||result),taskId:result.taskId,epoch:result.epoch});
    return result;
   };
  });
  const read=()=>page.evaluate(()=>batchFaces.proxy.invoke('CamScene.ReadState',{})),before=await read();
  await command('five.extraction.begin.Faces');
  const button=page.locator('[data-five-command="five.extraction.auto"]');
  await button.click();await idle();
  report.selection=await page.evaluate(()=>{const s=batchFaces.actions.extractionDraft(batchFaces.view).extraction;return {error:batchFaces.view.error,ids:s.faces.map(f=>f.localId),ranges:s.faceRanges,calls:batchFaces.calls};});
  assert.equal(report.selection.error,'');assert.ok(report.selection.ids.length>0);
  assert.equal(await page.locator('[data-five-key="extraction-faces"] .five-support-face').count(),report.selection.ids.length);
  for(const id of report.selection.ids)assert.ok(report.selection.ranges[id]?.some(r=>+r.count>0),id);
  for(const method of ['CamCut.ReadRetainedSelection','CamCut.QuerySources','CamPlacement.ReadReferenceFaces'])assert.ok(report.selection.calls.some(c=>c.method===method),method);
  const receipt=report.selection.calls.filter(c=>['CamCut.FindRetainedFaces','CamPlan.QueryStatus'].includes(c.method)).findLast(c=>c.keys.includes('selectionId'));
  assert.ok(receipt);assert.deepEqual(receipt.keys.sort(),['selectedFaceCount','selectionId']);report.receiptBytes=receipt.bytes;
  report.cases.push('Real Pillar batch-add reads a compact task receipt, then selection, topology and triangle ranges independently');
  await button.click();await idle();assert.equal(await page.evaluate(()=>batchFaces.view.error),'');
  assert.deepEqual(await page.evaluate(()=>batchFaces.actions.extractionDraft(batchFaces.view).extraction.faces.map(f=>f.localId)),report.selection.ids);
  const rows=page.locator('[data-five-command^="five.extraction.select-face."]');await rows.first().click();await rows.first().focus();
  if(report.selection.ids.length>1){await page.keyboard.press('ArrowDown');assert.equal(await page.evaluate(()=>batchFaces.actions.extractionDraft(batchFaces.view).extraction.selectedFace),report.selection.ids[1]);await page.keyboard.press('ArrowUp');}
  const camera=await page.evaluate(()=>batchFaces.view.viewport.getCameraState());
  await page.keyboard.press('f');await idle();await rows.first().dblclick();await idle();
  const focused=await page.evaluate(()=>batchFaces.view.viewport.getCameraState());assert.equal(focused.theta,camera.theta);assert.equal(focused.phi,camera.phi);
  report.cases.push('Repeated batch-add preserves membership; arrow selection, F and double-click work with unchanged camera angles');
  await page.screenshot({path:path.join(output,'batch-faces-fixed.png')});
  const after=await read();for(const key of ['expected','workpieces','placements','fixtures','intentToolpaths'])assert.deepEqual(after[key],before[key],key);
  report.projectHashAfter=hash(project);assert.equal(report.projectHashAfter,report.projectHashBefore);
  report.cases.push('Batch selection and inspection leave native CAD, fixture, toolpath and saved project data unchanged');
  assert.deepEqual(report.errors,[]);report.passed=true;
 }catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:path.join(output,'failure.png')}).catch(()=>{});}
 finally{
  await page.evaluate(()=>{if(window.batchFaces?.original)batchFaces.proxy.invoke=batchFaces.original;}).catch(()=>{});
  fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(report,null,2));
  console.log(JSON.stringify({passed:report.passed,faces:report.selection?.ids.length,receiptBytes:report.receiptBytes,error:report.error,cases:report.cases}));await browser.close();
 }
})().catch(error=>{console.error(error);process.exitCode=1;});
