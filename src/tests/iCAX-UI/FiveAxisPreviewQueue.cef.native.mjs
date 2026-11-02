import assert from 'node:assert/strict';
import {copyFileSync,mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';

// Open a native catalog without activating a UI tab. Keep the user's original
// workpiece and viewport untouched while exercising the actual scene queue.
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/cam-sdo-errors-20261026');
mkdirSync(output,{recursive:true});
const project=resolve(output,`preview-queue-${Date.now()}.i5cam`);
copyFileSync('output/tests/fixtures/path-edit-fixture.i5cam',project);
const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT||'http://127.0.0.1:9586');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));
const report={passed:false,standaloneCef:true,realNativeQueue:true,uiProjectPreserved:false};
try{
 assert.ok(page);
 const result=await page.evaluate(async project=>{
  const base=new URL('../../../',location.href).href,{findProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');
  const original=__icaxAppShell.getState(),view=findProjectView(original.activeProjectId);
  const {pathPreview}=await import(base+'apps/five-axis-cam/webpage/pathPreviewTask.mjs');
  const product=view.viewportSceneProxy.project.product;
  const opened=await product.appProxy.openProjectFile(project),scene=opened.sceneProxy;
  const result={calls:[],outputs:[],originalProjectId:original.activeProjectId};
  try{
   const before=await scene.invoke('CamScene.ReadState');
   const undoBefore=await scene.getUndoRedoState();
   if(before.intentToolpaths.length!==3)throw Error('Expected the original three-path native regression project');
   for(const intent of before.intentToolpaths){
    const start=performance.now();
    const compensation=pathPreview(scene,'Compensation',{expected:before.expected,intentIds:[intent.id]});
    const history=scene.getUndoRedoState().then(state=>({ms:performance.now()-start,state}));
    const preview=await compensation;
    const previewMs=performance.now()-start;
    const undo=await history;
    result.calls.push({method:'CamProcess.PreviewCompensation',intentId:intent.id,ms:previewMs,historyMs:undo.ms});
    result.outputs.push(preview);
   }
   const start=performance.now();
   const leads=pathPreview(scene,'Leads',{expected:before.expected,intentIds:before.intentToolpaths.map(i=>i.id)});
   const history=scene.getUndoRedoState().then(()=>performance.now()-start);
   const preview=await leads;
   result.calls.push({method:'CamProcess.PreviewLeads',ms:performance.now()-start,historyMs:await history,itemCount:preview.items.length});
   const cacheStart=performance.now();
   const cached=await scene.invoke('CamProcess.BeginPreview',{expected:before.expected,intentIds:[before.intentToolpaths[1].id],kind:'Compensation'});
   result.completedPreviewReuseMs=performance.now()-cacheStart;
   result.completedPreviewReused=cached.status==='Completed'&&JSON.stringify(cached.result)===JSON.stringify(result.outputs[1]);
   const cancel=await scene.invoke('CamProcess.BeginPreview',{expected:before.expected,intentIds:[before.intentToolpaths[1].id,before.intentToolpaths[0].id],kind:'Compensation'});
   try{await scene.invoke('CamProcess.QueryPreview',{epoch:'obsolete-preview-epoch',taskId:cancel.taskId});}
   catch(error){result.staleEpochRejected=/CAM_STALE_EPOCH/.test(error.message);}
   const released=await scene.invoke('CamProcess.QueryPreview',{epoch:cancel.epoch,taskId:cancel.taskId,release:true});
   result.previewCancelled=released.status==='Cancelled';
   try{await scene.invoke('CamProcess.QueryPreview',{epoch:cancel.epoch,taskId:cancel.taskId});}
   catch(error){result.cancelledResultUnavailable=/CAM_PREVIEW_NOT_FOUND/.test(error.message);}
   const after=await scene.invoke('CamScene.ReadState');
   result.nativeStateUnchanged=['expected','workpieces','placements'].every(key=>JSON.stringify(before[key])===JSON.stringify(after[key]))
     &&JSON.stringify(undoBefore)===JSON.stringify(await scene.getUndoRedoState());
   const references=s=>s.intentToolpaths.map(({id,curveRef,featureRef,processRef,poseRef})=>({id,curveRef,featureRef,processRef,poseRef}));
   result.pathReferencesUnchanged=JSON.stringify(references(before))===JSON.stringify(references(after));
   result.before=before;result.after=after;
  }catch(error){result.error=error.message;}
  finally{try{await product.closeProjectCatalog(opened.catalog.catalogId);}catch(error){result.closeError=error.message;}}
  result.activeProjectId=__icaxAppShell.getState().activeProjectId;
  return result;
 },project);
 report.result=result;
 assert.equal(result.error,undefined);assert.equal(result.closeError,undefined);
 assert.equal(result.activeProjectId,result.originalProjectId);report.uiProjectPreserved=true;
 assert.equal(result.nativeStateUnchanged,true);
 assert.equal(result.pathReferencesUnchanged,true);
 for(const key of ['completedPreviewReused','staleEpochRejected','previewCancelled','cancelledResultUnavailable'])assert.equal(result[key],true,key);
 for(const call of result.calls)assert.ok(call.historyMs<5000,`${call.method} blocked history for ${call.historyMs} ms`);
 for(const output of result.outputs)for(const item of output.items)assert.equal(item.error,undefined);
 const baseline=JSON.parse(readFileSync(resolve(output,'single-compensation.json')));
 const matched=result.outputs.find(o=>o.items[0].intentId===baseline.intent.id);
 assert.deepEqual(matched,baseline.result);report.sameCompensatedGeometry=true;
 if(process.env.ICAX_PREVIEW_BASELINE){
  const previous=JSON.parse(readFileSync(resolve(process.env.ICAX_PREVIEW_BASELINE)));
  assert.deepEqual(result.outputs,previous.result.outputs);report.sameAllCompensatedGeometry=true;
  const slowId=baseline.intent.id;
  report.performance={intentId:slowId,
   beforeMs:previous.result.calls.find(c=>c.intentId===slowId).ms,
   afterMs:result.calls.find(c=>c.intentId===slowId).ms};
 }
 report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;}
finally{writeFileSync(resolve(output,'preview-queue-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,error:report.error,calls:report.result?.calls,sameCompensatedGeometry:report.sameCompensatedGeometry,uiProjectPreserved:report.uiProjectPreserved},null,2));await browser.close();}
