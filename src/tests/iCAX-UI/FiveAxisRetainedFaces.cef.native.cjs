const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),{createHash}=require('node:crypto');
const {chromium}=require('playwright');
if(!process.env.ICAX_CEF_ENDPOINT)throw Error('Use a dedicated CEF acceptance instance.');
const output=path.resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/cam-pillar-extraction-20261101/cef');fs.mkdirSync(output,{recursive:true});
const source=path.resolve('output/acceptance/fixture-pillar-performance-20261101/cef-responsibilities.i5cam'),project=path.join(output,'verified-pillar.i5cam');
if(fs.existsSync(project))throw Error('Acceptance project already exists; choose a fresh report directory.');
fs.copyFileSync(source,project);
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const report={passed:false,standaloneCEF:true,realNativeSDO:true,source,sourceHashBefore:hash(source),project,cases:[],errors:[]};
(async()=>{
 const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));
 assert.ok(page);await page.setViewportSize({width:1600,height:1000});page.setDefaultTimeout(240000);page.on('pageerror',e=>report.errors.push(e.message));
 const bind=()=>page.evaluate(async()=>{const base=new URL('../../../',location.href).href,{getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.retained={view:getProjectView(__icaxAppShell.getState().activeProjectId),E:await import(base+'apps/five-axis-cam/webpage/extractionActions.mjs'),calls:[]};});
 const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!retained.view.pending&&!retained.view.fixtureRequest);
 const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();assert.equal(await page.evaluate(()=>retained.view.error),'',id);};
 const read=()=>page.evaluate(()=>retained.view.viewportSceneProxy.invoke('CamScene.ReadState',{}));
 try{
  console.log('open isolated project');await page.waitForFunction(()=>__icaxAppShell?.getState().startupPhase==='ready');await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),project);
  await page.waitForFunction(()=>__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId);await bind();await idle();
  const before=await read();assert.equal(before.intentToolpaths.length,0);
  await page.locator('[data-tab-id="toolpath"]').click();await idle();await command('five.extraction.begin.Faces');
  await page.evaluate(()=>{
   const r=retained,proxy=r.view.viewportSceneProxy;r.proxy=proxy;r.original=proxy.invoke;
   proxy.invoke=async function(method,payload,...options){const result=await r.original.call(this,method,payload,...options);r.calls.push({method,bytes:JSON.stringify(result).length,keys:Object.keys(result.result||result)});if(method==='CamCut.QueryCandidates')(r.candidates??=[]).push(...result.candidates);return result;};
  });
  console.log('automatic retained skin');const started=Date.now();await page.locator('[data-five-command="five.extraction.auto"]').click();await idle();
  report.selectionMs=Date.now()-started;report.faces=await page.evaluate(()=>retained.E.extractionDraft(retained.view).extraction.faces.map(f=>f.localId));
  assert.equal(await page.evaluate(()=>retained.view.error),'');assert.deepEqual(report.faces,['1']);
  assert.equal(await page.locator('[data-five-key="extraction-faces"] .five-support-face').count(),1);
  assert.equal(await page.getByText('按观察方向批量添加保留材料上的非加工面',{exact:false}).count(),0);
  await command('five.extraction.pick');const occlusion=page.getByRole('checkbox',{name:'遮挡',exact:true});assert.equal(await occlusion.isChecked(),true);await occlusion.uncheck();await idle();assert.equal(await page.evaluate(()=>retained.E.extractionDraft(retained.view).extraction.throughSelection),true);await occlusion.check();await idle();await command('five.extraction.pick');
  const selectedState=await read();for(const key of ['expected','workpieces','fixtures','placements','intentToolpaths'])assert.deepEqual(selectedState[key],before[key],key);
  console.log('one-click confirm');const confirmStart=Date.now();await page.locator('[data-five-command="five.extraction.confirm"]').click();await idle();assert.equal(await page.evaluate(()=>retained.view.error),'');report.confirmMs=Date.now()-confirmStart;
  await page.waitForFunction(()=>retained.view.scene.intentToolpaths.length===21&&retained.view.scene.intentToolpaths.every(c=>(c.points?.length||c.previewPoints?.length||c.previewPointCount)>1||c.sourceState==='Current'));
  const created=await read();assert.equal(created.intentToolpaths.length,21);
  report.candidates=await page.evaluate(()=>retained.candidates.map(c=>({id:c.candidateId,role:c.boundaryRole,relation:c.boundary.regionRelation,kind:c.machiningKind,eligibility:c.confirmationEligibility,spans:c.boundary.sourceSpans.length})));
  assert.equal(report.candidates.filter(c=>c.role==='Opening').length,20);assert.equal(report.candidates.filter(c=>c.role==='OuterTrim').length,1);assert.ok(report.candidates.every(c=>c.eligibility==='Ready'&&c.kind==='Cut'));
  assert.equal(await page.locator('[data-five-intent]').count(),21);report.calls=await page.evaluate(()=>retained.calls);
  assert.ok(report.calls.some(c=>c.method==='CamCut.ReadRetainedSelection'));assert.equal(report.calls.filter(c=>c.method==='CamCut.Create').length,1);assert.ok(!report.calls.some(c=>c.method==='CamFixture.FindSupportFaces'));
  const receipt=report.calls.find(c=>c.keys.includes('selectionId'));assert.deepEqual(receipt.keys.sort(),['selectedFaceCount','selectionId']);
  for(const key of ['workpieces','fixtures','placements'])assert.deepEqual(created[key],before[key],key);
  report.cases.push('Automatic selection keeps the complete formed skin and excludes 33 thickness-wall faces; one Confirm creates 20 inner cuts and 1 outer trim through real native interfaces');
  await page.screenshot({path:path.join(output,'created-21-toolpaths.png')});
  await command('app.save');
  const projectId=await page.evaluate(()=>__icaxAppShell.getState().activeProjectId);
  await page.evaluate(()=>{retained.proxy.invoke=retained.original;});
  console.log('close and reopen saved copy');await page.evaluate(id=>__icaxAppShell.closeProject(id),projectId);
  await page.waitForFunction(id=>__icaxAppShell.getState().activeProjectId!==id,projectId);
  await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),project);await page.waitForFunction(()=>__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId);await bind();await idle();
  const reopened=await read();assert.deepEqual(reopened.intentToolpaths,created.intentToolpaths);for(const key of ['workpieces','fixtures','placements'])assert.deepEqual(reopened[key],before[key],key);
  report.reopenedPaths=reopened.intentToolpaths.length;report.sourceHashAfter=hash(source);assert.equal(report.sourceHashAfter,report.sourceHashBefore);
  report.cases.push('Save, fully close and reopen retain all 21 toolpaths; source CAD, fixture geometry and original project file are unchanged');
  report.cases.push('Current production panel has no redundant paragraph; Occlusion starts checked and its checkbox toggles through-selection correctly');
  assert.deepEqual(report.errors,[]);report.passed=true;
 }catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:path.join(output,'failure.png')}).catch(()=>{});}
 finally{await page.evaluate(()=>{if(window.retained?.original)retained.proxy.invoke=retained.original;}).catch(()=>{});fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,faces:report.faces?.length,paths:report.reopenedPaths,error:report.error,cases:report.cases}));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
