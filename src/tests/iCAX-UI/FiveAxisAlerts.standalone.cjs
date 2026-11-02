// Operates only an explicitly supplied disposable CEF project through CDP.
// Engineering calls below are read oracles; detection starts via its real button.
'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const {chromium}=require(process.env.ICAX_PLAYWRIGHT_MODULE||'C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const args=process.argv.slice(2),option=name=>{const index=args.indexOf(name);return index<0?null:args[index+1];};
for(const name of ['--cdp','--project','--output'])assert.ok(option(name)&&!option(name).startsWith('--'),'Explicit '+name+' required');
const cdp=new URL(option('--cdp'));assert.notEqual(cdp.port,'9488','The original user app must never be touched');
const project=path.resolve(option('--project')).replaceAll('\\','/'),output=path.resolve(option('--output'));
assert.ok(fs.existsSync(project)&&project.endsWith('.i5cam'));assert.match(output,/^[cC]:[\\/]/);fs.mkdirSync(output,{recursive:true});
const layoutOnly=process.env.ICAX_ALERTS_LAYOUT_ONLY==='1',hash=value=>crypto.createHash('sha256').update(JSON.stringify(value)).digest('hex');
const same=(first,second,message)=>assert.equal(hash(first),hash(second),message);
const report={passed:false,project,cdp:cdp.href,startedAt:new Date().toISOString(),layoutOnly,cases:[],samples:[],
  coverage:{standaloneCef:true,explicitTarget:true,offscreenCdpOnly:true,realMouse:true,renderedDetectionButton:!layoutOnly,nativeReadOracles:true,repairOrHistoryWorkflow:false}};
const write=()=>fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(report,null,2));
const pass=(name,details={})=>{report.cases.push({name,...details});write();console.log('PASS '+name);};
let browser,page,hooked=false,target;
(async()=>{
  browser=await chromium.connectOverCDP(cdp.href);
  page=browser.contexts().flatMap(context=>context.pages()).find(candidate=>/iCAX-UI\/SDK\/AppShell\/index\.html/i.test(candidate.url().replaceAll('\\','/')));
  assert.ok(page,'Explicit CEF app page required');page.setDefaultTimeout(60000);
  await page.setViewportSize({width:1600,height:1000});
  const wait=(predicate,arg,timeout=180000)=>page.waitForFunction(predicate,arg,{timeout});
  await wait(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
  const activePath=await page.evaluate(()=>__icaxAppShell.getState().activeProjectPath||'');
  if(!activePath)await page.evaluate(project=>__icaxAppShell.openProject(project,'icax.five-axis-cam'),project);
  else assert.equal(activePath.replaceAll('\\','/'),project,'Refuse to switch away from a different active project');
  await wait(expected=>{const app=window.__icaxAppShell?.getState();return app?.startupPhase==='ready'&&app.activeProjectPath?.replaceAll('\\','/')===expected&&!app.pendingCount;},project);
  await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.fiveAlertsAudit=getProjectView(__icaxAppShell.getState().activeProjectId);});
  await wait(()=>!!window.fiveAlertsAudit?.scene?.domain&&!fiveAlertsAudit.pending&&!!fiveAlertsAudit.viewportSceneProxy&&!!fiveAlertsAudit.viewport);
  target=await page.evaluate(()=>__icaxAppShell.getState().activeProjectId);
  const idle=()=>wait(()=>{const v=fiveAlertsAudit,s=v.fiveSimulation;return !v.pending&&!s?.checking&&!s?.repairing&&!v.fiveHistoryBusy&&!__icaxAppShell.getState().pendingCount;});
  const identity=async()=>assert.equal(await page.evaluate(()=>__icaxAppShell.getState().activeProjectId),target,'Only the supplied project may be operated');
  const camera=()=>page.evaluate(()=>fiveAlertsAudit.viewport.getCameraState());
  const read=()=>page.evaluate(async()=>{const {hydratePathSamples}=await import('../../../apps/five-axis-cam/webpage/pathSampleResource.mjs'),proxy=fiveAlertsAudit.viewportSceneProxy;return hydratePathSamples(await proxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}),proxy);});
  const setup=state=>state.setups.find(value=>value.id===state.activeSetupId);
  const immutable=state=>({workpieces:state.workpieces,placements:state.placements,fixtures:state.fixtures,fixtureUnits:state.fixtureUnits,cuts:state.intentToolpaths,
    setups:state.setups.map(value=>({id:value.id,headModel:value.headModel,rapidSettings:value.rapidSettings,definitionOrder:value.definitionOrder,cutGroups:value.cutGroups,machineSetup:value.machineSetup,
      generatedGeometry:(({sampleGeometryRef,...geometry})=>geometry)(value.generatedGeometry)}))});
  const shot=name=>page.screenshot({path:path.join(output,name+'.png')});
  const panel=page.locator('[data-five-alert-panel]'),toggle=page.locator('[data-ribbon-alerts] .ribbon-alerts-heading[data-command-id="five.alerts.toggle"]');
  const panelFacts=()=>page.evaluate(()=>{
    const panel=document.querySelector('[data-five-alert-panel]'),anchor=document.querySelector('[data-ribbon-alerts]'),ribbon=anchor?.closest('.product-ribbon');
    if(!panel||!anchor||!ribbon)return null;
    const p=panel.getBoundingClientRect(),a=anchor.getBoundingClientRect(),r=ribbon.getBoundingClientRect(),style=getComputedStyle(panel);
    return {width:p.width,top:p.top,right:innerWidth-p.right,expectedTop:Math.min(r.bottom+6,Math.max(12,innerHeight-180)),expectedRight:Math.max(12,innerWidth-a.right+4),position:style.position,
      background:style.backgroundColor,modal:panel.getAttribute('aria-modal'),floating:panel.hasAttribute('data-five-floating'),text:panel.textContent,summary:anchor.textContent,
      issueCount:panel.querySelectorAll('.five-collision-issue').length,dialogCount:document.querySelectorAll('[data-five-alert-panel]').length};
  });
  const ensureOpen=async()=>{if(!await panel.count())await toggle.click();await panel.waitFor();};
  await idle();await identity();
  const before=await read(),baseline=setup(before).simulationCheck;assert.equal(baseline.validity,'Current');assert.ok(Number(baseline.collisionCount)>0,'A real remaining collision is required');
  report.initial={collisionCount:baseline.collisionCount,uncheckedCount:baseline.uncheckedCount,segmentCount:baseline.segments.length};
  fs.writeFileSync(path.join(output,'before-native.json'),JSON.stringify(before,null,2));
  const beforeOpen=await camera();await ensureOpen();await idle();same(await camera(),beforeOpen,'Opening alerts must not change the camera');
  const facts=await panelFacts();assert.ok(facts);assert.equal(facts.width,900);assert.equal(facts.position,'fixed');assert.equal(facts.dialogCount,1);assert.equal(facts.floating,false);assert.notEqual(facts.modal,'true');
  assert.ok(Math.abs(facts.top-facts.expectedTop)<1&&Math.abs(facts.right-facts.expectedRight)<1,JSON.stringify(facts));assert.match(facts.background,/rgba\([^)]*,\s*0\./);
  assert.match(facts.text,new RegExp(`${baseline.collisionCount} 段碰撞`));assert.ok(facts.issueCount>0);assert.match(facts.summary,/告警/);
  await shot('01-alerts-open');pass('Real ribbon opens one 900px translucent right anchored nonmodal panel without changing camera',facts);
  await panel.getByRole('button',{name:'收起告警'}).click();await panel.waitFor({state:'detached'});same(await camera(),beforeOpen,'Collapsing alerts must not change the camera');
  assert.equal(await page.locator('[aria-modal="true"],.modal-backdrop').count(),0);await ensureOpen();same(await camera(),beforeOpen,'Reopening alerts must not change camera');
  pass('Collapse and reopen use rendered controls, leave no modal backdrop and preserve camera');
  const scenePoint=()=>page.evaluate(()=>{
    const canvas=fiveAlertsAudit.viewport.renderer.domElement,r=canvas.getBoundingClientRect();
    for(const fy of [.9,.8,.7,.5,.3])for(const fx of [.08,.18,.3,.5,.7]){
      const point={x:r.x+r.width*fx,y:r.y+r.height*fy};
      if(point.x<12||point.y<12||point.x+90>innerWidth-12||point.y+45>innerHeight-12)continue;
      if([point,{x:point.x+80,y:point.y+35}].every(p=>document.elementFromPoint(p.x,p.y)===canvas))return point;
    }
    throw Error('No visible canvas area outside the alert panel');
  });
  const drag=async button=>{const p=await scenePoint();await page.mouse.move(p.x,p.y);await page.mouse.down({button});await page.mouse.move(p.x+80,p.y+35,{steps:10});await page.mouse.up({button});await page.waitForTimeout(120);};
  const initialCamera=await camera();await drag('right');const rotated=await camera();assert.notEqual(hash(rotated),hash(initialCamera),'Real right drag must rotate the viewport');
  await drag('middle');const panned=await camera();assert.notEqual(hash(panned),hash(rotated),'Real middle drag must pan the viewport');
  const wheelPoint=await scenePoint();await page.mouse.move(wheelPoint.x,wheelPoint.y);await page.mouse.wheel(0,-200);await page.waitForTimeout(120);const zoomed=await camera();assert.notEqual(hash(zoomed),hash(panned),'Real wheel must zoom the viewport');
  same(immutable(await read()),immutable(before),'Camera gestures cannot change source geometry or machining definitions');
  pass('Real panel-external rotation, pan and zoom work through the canvas and preserve native geometry',{initialCamera,rotated,panned,zoomed});
  const first=baseline.segments.find(segment=>segment.status==='Collision'&&segment.witness);assert.ok(first,'A native collision witness is required');
  await page.locator(`[data-five-command="five.simulation.collision-result:${first.index}"]`).click();await idle();
  const selected=await page.evaluate(()=>({cursor:fiveAlertsAudit.fiveSimulation.cursor,report:fiveAlertsAudit.fiveSimulation.collisionReport,diagnosis:document.querySelector('[data-five-key="collision-diagnosis"]')?.textContent,
    selected:document.querySelector('.five-collision-issue[aria-pressed="true"]')?.dataset.fiveCommand}));
  assert.equal(Math.floor(selected.cursor),Number(first.index));assert.match(selected.diagnosis||'',/碰撞盒|间隙/);same(selected.report,baseline,'Visible results must use the authoritative native report');
  await shot('02-selected-native-collision');pass('Real row click selects and deliberately focuses the corresponding native collision and witness',{segmentIndex:first.index,witness:first.witness,selected:selected.selected,camera:await camera()});
  if(layoutOnly){report.passed=true;report.detectionDeferred=true;return;}
  await identity();await idle();
  const checkButton=panel.locator('[data-five-command="five.simulation.collision-check"]');assert.match(await checkButton.textContent(),Number(baseline.uncheckedCount)?/继续检测/:/重新检测/);
  await page.evaluate(()=>{
    const proxy=fiveAlertsAudit.viewportSceneProxy;window.fiveAlertsObserver={proxy,original:proxy.invoke,replies:[],cameras:[]};
    proxy.invoke=async function(method,...args){const result=await fiveAlertsObserver.original.call(this,method,...args);
      if(['CamSimulation.CheckCollision','CamSimulation.QueryTask','CamSimulation.CancelTask'].includes(method))fiveAlertsObserver.replies.push({method,reply:structuredClone(result)});
      return result;
    };
  });hooked=true;
  const detectionCamera=await camera(),started=Date.now();await checkButton.click();
  await wait(()=>!!fiveAlertsAudit.fiveSimulation?.checking);
  assert.equal(await toggle.isEnabled(),true,'Shared busy state must leave the alert toggle enabled');
  await toggle.click();await panel.waitFor({state:'detached'});
  const closedRunning=await page.evaluate(()=>({checking:!!fiveAlertsAudit.fiveSimulation?.checking,task:fiveAlertsAudit.fiveSimulation?.collisionTask?.taskId,cancelled:!!fiveAlertsAudit.fiveSimulation?.collisionCancelRequested}));
  assert.equal(closedRunning.checking,true,'Collapsing during a check must not end it');assert.equal(closedRunning.cancelled,false);
  same(await camera(),detectionCamera,'Collapsing while checking must not change camera');
  assert.equal(await toggle.isEnabled(),true);await toggle.click();await panel.waitFor();
  const openedRunning=await page.evaluate(()=>({checking:!!fiveAlertsAudit.fiveSimulation?.checking,task:fiveAlertsAudit.fiveSimulation?.collisionTask?.taskId,cancelled:!!fiveAlertsAudit.fiveSimulation?.collisionCancelRequested}));
  assert.equal(openedRunning.checking,true);assert.equal(openedRunning.cancelled,false);if(closedRunning.task)assert.equal(openedRunning.task,closedRunning.task,'Reopen must retain the exact running native task');
  pass('A real detection continues through busy-state collapse and reopen without cancellation or camera changes',{closedRunning,openedRunning});
  let completed=false,lastLog=0;
  while(Date.now()-started<900000){
    const sample=await page.evaluate(()=>{const v=fiveAlertsAudit,s=v.fiveSimulation;return {checking:!!s?.checking,pending:!!v.pending,stage:s?.checkStage,taskStatus:s?.collisionTask?.status,
      taskId:s?.collisionTask?.taskId,progress:s?.checkExactProgress?structuredClone(s.checkExactProgress):null,camera:v.viewport.getCameraState(),error:v.error||v.fiveWorkpieceSaveError||s?.message||''};});
    report.samples.push(sample);same(sample.camera,detectionCamera,'Asynchronous detection progress and completion must preserve the user camera');
    if(!sample.checking&&!sample.pending){assert.equal(sample.taskStatus,'Completed',JSON.stringify(sample));assert.equal(sample.error,'');completed=true;break;}
    if(Date.now()-lastLog>10000){console.log(`Detection ${Date.now()-started}ms ${sample.stage||''} ${sample.taskStatus||''}`);lastLog=Date.now();write();}
    await new Promise(resolve=>setTimeout(resolve,100));
  }
  assert.equal(completed,true,'Detection must finish within 15 minutes');await idle();same(await camera(),detectionCamera,'Final asynchronous result must not auto focus the camera');
  const after=await read(),final=setup(after).simulationCheck,visible=await page.evaluate(()=>fiveAlertsAudit.fiveSimulation.collisionReport);
  assert.equal(final.validity,'Current');same(visible,final,'Visible final collision report must equal native authority');same(immutable(after),immutable(before),'Detection preserves original geometry and machining definitions');
  report.observer=await page.evaluate(()=>fiveAlertsObserver.replies);assert.ok(report.observer.some(value=>value.method==='CamSimulation.CheckCollision'));assert.equal(report.observer.filter(value=>value.method==='CamSimulation.CancelTask').length,0);
  assert.equal(final.checkOrder,'KeypointsThenIntervals');assert.equal(final.parallelWorkers,'2');
  const finalFacts=await panelFacts();assert.match(finalFacts.text,new RegExp(`${final.collisionCount} 段碰撞`));
  fs.writeFileSync(path.join(output,'after-native.json'),JSON.stringify(after,null,2));await shot('03-detection-completed');
  pass('Continuing detection finishes with camera preserved and rendered summary/report equal to native authority',{taskMs:Date.now()-started,parallelWorkers:final.parallelWorkers,keypointCheckCount:final.keypointCheckCount,collisionCount:final.collisionCount,uncheckedCount:final.uncheckedCount});
  report.passed=true;
})().catch(async error=>{
  report.error=error.message;report.stack=error.stack;console.error(error.stack);if(page)await page.screenshot({path:path.join(output,'failure.png')}).catch(()=>{});
}).finally(async()=>{
  if(hooked&&page)await page.evaluate(()=>{const observer=window.fiveAlertsObserver;if(observer){observer.proxy.invoke=observer.original;delete window.fiveAlertsObserver;}}).catch(()=>{});
  report.finishedAt=new Date().toISOString();write();if(browser)await browser.close();console.log('Report: '+path.join(output,'report.json'));process.exitCode=report.passed?0:1;
});
