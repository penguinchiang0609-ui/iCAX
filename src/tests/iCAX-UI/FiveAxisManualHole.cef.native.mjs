import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,existsSync,unlinkSync,writeFileSync,readFileSync} from 'node:fs';
import {chromium} from 'playwright';
import {verifyManualHole} from './FiveAxisPathEditing.native.browser.mjs';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/manual-hole-cef');mkdirSync(output,{recursive:true});
const workingFile=resolve(output,'manual-hole-accepted.i5cam'),reopen=!!process.env.ICAX_MANUAL_HOLE_REOPEN;
if(!reopen&&existsSync(workingFile))unlinkSync(workingFile);
const assetBase=pathToFileURL(resolve('src')+'/').href;
const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT||'http://127.0.0.1:9628');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/iCAX-UI/SDK/AppShell/index.html'));assert.ok(page);page.setDefaultTimeout(120000);
const errors=[];page.on('pageerror',e=>errors.push(e.message));
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 if(reopen)await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),workingFile);
 else await page.evaluate(p=>__icaxAppShell.createProject({productId:'icax.five-axis-cam',projectName:'曲面孔手动指定',projectPath:p}),workingFile);
 await page.evaluate(async base=>{const store=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={entry:{handleRibbonCommand:(_context,id)=>__icaxAppShell.executeRibbonCommand(id)},context:{actions:{selectRibbonTab:id=>__icaxAppShell.selectRibbonTab(id)}}};Object.defineProperty(fixture,'view',{get:()=>store.getProjectView(__icaxAppShell.getState().activeProjectId)});},assetBase);
 await page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view?.pending&&!!fixture.view?.scene?.domain);
 const read=()=>page.evaluate(async base=>{const {readMachiningState}=await import(base+'apps/five-axis-cam/webpage/domainActions.mjs');return readMachiningState({sceneProxy:fixture.view.viewportSceneProxy});},assetBase);
 const rpc=async request=>{
  if(request.action==='read-state')return read();
  if(request.action==='invoke')return page.evaluate(({method,payload})=>fixture.view.viewportSceneProxy.invoke(method,payload,{timeoutMs:120000}),request);
  if(request.action==='undo'||request.action==='redo')return page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),'edit.'+request.action);
  throw Error('Unsupported manual-hole acceptance request '+request.action);
 };
 if(reopen){
  const expected=JSON.parse(readFileSync(resolve(output,'saved-state.json'),'utf8')),actual=JSON.parse(JSON.stringify(await read()));
  for(const key of ['intentToolpaths','workpieces','placements'])assert.deepEqual(actual[key],expected[key],key+' persisted across a fresh process');
  writeFileSync(resolve(output,'reopen-report.json'),JSON.stringify({passed:true,freshProcess:true,cadAndPlacementPreserved:true},null,2));
 }else{
  const invoke=(method,payload)=>rpc({action:'invoke',method,payload});
  const wait=async task=>{for(let i=0;task.status==='Running'&&i<600;i++){await new Promise(resolve=>setTimeout(resolve,50));task=await invoke('CamWorkpiece.QueryTask',{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',JSON.stringify(task));return task;};
  let state=await read();await wait(await invoke('CamWorkpiece.Import',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,sourcePath:resolve(process.env.ICAX_MANUAL_HOLE_STEP)}));
  state=await read();assert.ok(state.placements[0]);const setup=await invoke('CamSetup.Manage',{commandId:crypto.randomUUID(),expected:state.expected,placementId:state.placements[0].id,action:'create',name:'曲面孔验收装夹'});
  state=await read();const placement=state.placements.find(p=>p.id===setup.placementId);assert.ok(placement);
  const sources=await invoke('CamCut.QuerySources',{expected:state.expected,setupId:setup.setupId,placementId:placement.id});
  const extraction={commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,placementId:placement.id,sourceGeometry:sources.sourceGeometry,extractionRuleRef:sources.extractionRuleRef,selection:{mode:'Faces',faceUses:sources.faceUses},representation:'SkinSurface',workpieceFamily:'SheetSurface',side:{outwardSign:1},requestedKinds:['GeneralContour','Circle','Ellipse','Rectangle','Slot'],draftId:crypto.randomUUID(),draftRevision:'1',draftDigest:'manual-hole-cef',geometryOnly:true};
  const terminal=await wait(await invoke('CamCut.ExtractCandidates',extraction)),pageOfCandidates=await invoke('CamCut.QueryCandidates',{epoch:terminal.epoch,candidateSet:terminal.result.candidateSet});
  const data=value=>({revision:crypto.randomUUID(),value:{...value,schemaVersion:'1'}}),none={kind:'None',placement:'ScrapSide',lengthMm:0,angleRad:0};
  const library=data({schema:'cam.process-library',name:'曲面孔验收',productionQualified:false});
  const processPolicy=data({schema:'cam.process-assignment',libraryData:library,overrides:[],kerf:{owner:'Cam',widthMm:0,measurementSection:'BeamNormalSection',cornerMode:'Miter',miterLimit:4},leadIn:none,leadOut:none,overcutLengthMm:0,gapLengthMm:0,layerKey:'cut'});
  const pose=data({schema:'cam.pose-policy',mode:'Continuous',initialization:'SurfaceNormal',transitionMetric:'ArcLength',leadAngleRad:0,sideAngleRad:0,leadDeviationLimitRad:.5,sideDeviationLimitRad:.5,angularToleranceRad:.75,surfaceNormalPreferenceWeight:.5,keypoints:[]});
  state=await read();await invoke('CamCut.Create',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,placementId:placement.id,draftDigest:extraction.draftDigest,candidateSet:terminal.result.candidateSet,candidateIds:pageOfCandidates.candidates.filter(c=>c.confirmationEligibility==='Ready').map(c=>c.candidateId),decisions:[],process:processPolicy,pose});
  for(let i=0;i<400;i++){state=await read();if(state.intentToolpaths.length&&state.intentToolpaths.every(c=>c.sourceState==='Current'))break;assert.equal(state.intentGenerationErrors?.length||0,0,JSON.stringify(state.intentGenerationErrors));await new Promise(resolve=>setTimeout(resolve,50));}
  await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));
  const cut=state.intentToolpaths.find(c=>!c.split&&!c.standardHole?.kind&&Math.abs(c.startPoint[2])>19);assert.ok(cut,'actual unrecognized hole on a cylindrical face');
  await verifyManualHole({page,rpc,output,cut,withKerf:true});
  assert.equal((await page.evaluate(path=>fixture.view.viewportSceneProxy.project.save(path),workingFile)).saved,true);
  writeFileSync(resolve(output,'saved-state.json'),JSON.stringify(await read(),null,2));
 }
 assert.deepEqual(errors,[]);await page.screenshot({path:resolve(output,reopen?'reopened.png':'standalone-accepted.png')});
 console.log('PASS standalone CEF manual standard hole'+(reopen?' fresh-process reopen':' on a curved surface'));
}catch(error){await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});writeFileSync(resolve(output,'failure.json'),JSON.stringify({error:error.stack,errors,state:await page.evaluate(()=>({text:document.body.innerText,error:fixture.view?.error})).catch(()=>null)},null,2));throw error;}
finally{await browser.close();}
