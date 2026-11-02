const assert=require('node:assert/strict');
const {mkdirSync,copyFileSync,writeFileSync}=require('node:fs');
const {resolve}=require('node:path');
const {chromium}=require('playwright');
const output=process.env.ICAX_BROWSER_REPORT_DIRECTORY||'C:/Users/pengu/AppData/Local/Temp/icax-blocking-progress-20261024/acceptance';
const endpoint=process.env.ICAX_CEF_ENDPOINT||'http://127.0.0.1:9504';
mkdirSync(output,{recursive:true});
const report={passed:false,standaloneCef:true,realNativeCalculation:true,cases:[],errors:[]};
(async()=>{
 const browser=await chromium.connectOverCDP(endpoint);
 const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));
 page.setDefaultTimeout(45000);page.on('pageerror',e=>report.errors.push(e.message));
 const wait=fn=>page.waitForFunction(fn,null,{timeout:240000});
 const idle=()=>wait(()=>!fixtureView.pending&&!fixtureView.fixtureRequest&&!__icaxAppShell.getState().pendingCount);
 const field=k=>page.locator(`[data-five-fixture-field="design:${k}"]`);
 try{
  await wait(()=>__icaxAppShell?.getState().startupPhase==='ready');
  const project=resolve(output,'支撑计算进度验收-'+Date.now()+'.i5cam');
  copyFileSync(resolve('output/research/video-stamped-panel-20261023/Video_HatRail_AlgorithmSupport.i5cam'),project);
  await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),project);
  await wait(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.fixtureView=getProjectView(__icaxAppShell.getState().activeProjectId);return !!fixtureView?.scene?.domain&&!fixtureView.pending;});
  await idle();await page.locator('[data-tab-id="workpiece"]').click();await idle();
  await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.fixture.focus.design'));await idle();
  report.initial=await page.evaluate(()=>({supportFaces:fixtureView.fixtureDesignDraft?.supportFaces?.length,model:fixtureView.scene.model?.name}));
  assert.ok(report.initial.supportFaces>0,'Saved model must have actual support faces');
  await field('spacing').fill('220');
  // Hold only delivery of a real native task receipt to make input checks
  // deterministic. No geometry result or task state is mocked.
  await page.evaluate(()=>{
   const proxy=fixtureView.viewportSceneProxy,invoke=proxy.invoke.bind(proxy);window.nativeInvoke=invoke;window.nativeCalls=[];
   proxy.invoke=async(method,...args)=>{
    nativeCalls.push(method);
    const result=await invoke(method,...args);
    if(method==='CamFixture.Generate'){window.nativeReceipt=result;await new Promise(r=>window.releaseReceipt=r);}
    return result;
   };
   window.keptViewport=fixtureView.viewport.renderer.domElement;
  });
  await page.locator('[data-five-command="five.fixture.design.generate"]').click();
  const dialog=page.locator('.five-blocking-progress');await dialog.waitFor();
  await wait(()=>!!window.releaseReceipt);
  assert.equal(await dialog.evaluate(n=>n.matches(':modal')),true);
  const box=await dialog.boundingBox(),viewport=await page.evaluate(()=>({width:innerWidth,height:innerHeight}));
  assert.deepEqual(box,{x:0,y:0,...viewport});
  report.viewport=viewport;report.receipt=await page.evaluate(()=>nativeReceipt);
  const before=await page.evaluate(()=>({project:__icaxAppShell.getState().activeProjectId,area:fixtureView.fiveAxisArea,spacing:fixtureView.fixtureDesignDraft.spacing,canvas:keptViewport===fixtureView.viewport.renderer.domElement}));
  const rect=await page.locator('[data-tab-id="simulation"]').boundingBox();if(rect)await page.mouse.click(rect.x+rect.width/2,rect.y+rect.height/2);
  await page.keyboard.press('Escape');await page.keyboard.press('Control+z');await page.keyboard.press('Delete');await page.keyboard.press('Tab');
  const after=await page.evaluate(()=>({project:__icaxAppShell.getState().activeProjectId,area:fixtureView.fiveAxisArea,spacing:fixtureView.fixtureDesignDraft.spacing,canvas:keptViewport===fixtureView.viewport.renderer.domElement}));
  assert.deepEqual(after,before);assert.equal(await dialog.evaluate(n=>n.open),true);
  await page.screenshot({path:resolve(output,'native-large-progress.png')});
  report.cases.push('User HatRail model, real support generation: full-page modal blocks ribbon, shortcuts, navigation and preserves viewport');
  await page.evaluate(()=>releaseReceipt());await idle();
  assert.equal(await dialog.count(),0);
  assert.equal(await page.evaluate(()=>fixtureView.error),'');
  assert.equal(await page.locator('[data-five-command="five.fixture.design.page:preview"]').getAttribute('aria-pressed'),'true');
  report.candidate=await page.evaluate(()=>{const d=fixtureView.scene.domain;const c=d.setups.find(s=>s.id===d.activeSetupId).fixtureDesign.candidate;return {parts:c.parts.length,validity:c.validity};});
  assert.ok(report.candidate.parts>0);assert.equal(report.candidate.validity,'Current');
  await page.locator('[data-five-command="five.fixture.design.page:parameters"]').first().click();await idle();
  assert.equal(await field('spacing').isEnabled(),true);
  await page.screenshot({path:resolve(output,'native-completed.png')});
  report.cases.push('Actual native generation completes; candidate preview is current, mask closes and parameter controls work again');
  await page.evaluate(()=>window.releaseReceipt=null);
  await page.locator('[data-five-command="five.fixture.design.generate"]').click();
  await wait(()=>!!window.releaseReceipt);await page.evaluate(()=>releaseReceipt());
  await dialog.locator('[data-progress-cancel]').waitFor();
  await dialog.locator('[data-progress-cancel]').click();await idle();
  assert.equal(await dialog.count(),0);
  assert.equal(await page.evaluate(()=>nativeCalls.includes('CamFixture.CancelTask')),true);
  report.cases.push('Central cancel button invokes the actual native cancellation and releases the mask only when the request ends');
  await page.evaluate(()=>fixtureView.viewportSceneProxy.invoke=nativeInvoke);
  await field('spacing').fill('-1');
  await page.locator('[data-five-command="five.fixture.design.generate"]').click();await idle();
  assert.equal(await dialog.count(),0);assert.ok(await page.evaluate(()=>fixtureView.error));
  assert.equal(await field('spacing').isEnabled(),true);
  report.cases.push('Real native invalid-spacing failure restores editable parameters and removes the modal');
  await field('spacing').fill('220');
  assert.deepEqual(report.errors,[]);report.passed=true;
 }catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
 finally{await page.evaluate(()=>{if(window.releaseReceipt)releaseReceipt();if(window.nativeInvoke&&window.fixtureView)fixtureView.viewportSceneProxy.invoke=nativeInvoke;}).catch(()=>{});writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
