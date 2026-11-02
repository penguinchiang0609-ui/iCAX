import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {copyFileSync,existsSync,readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
const {chromium}=createRequire(import.meta.url)('playwright'),out=resolve('output/tests/machine-jog-spinner-20261029');mkdirSync(out,{recursive:true});
const report={passed:false,errors:[],transport:'standalone CEF with real native scene commands and PDO jog'},browser=await chromium.connectOverCDP(process.env.MACHINE_CDP||'http://127.0.0.1:9605');let page;
try{
 page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));assert.ok(page);page.on('pageerror',e=>report.errors.push(e.message));
 const cdp=await page.context().newCDPSession(page);await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:true});
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 const open=async(source,name)=>{
  const project=resolve(out,name+'-'+Date.now()+'.i5cam');copyFileSync(source,project);if(existsSync(source+'.log'))copyFileSync(source+'.log',project+'.log');
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
  await page.waitForFunction(async path=>{if(__icaxAppShell.getState().activeProjectPath!==path)return false;const {getProjectView}=await import(new URL('../../../apps/_shared/workbench/state/projectViewStore.mjs',location.href));window.jogView=getProjectView(__icaxAppShell.getState().activeProjectId);return jogView?.scene?.machines?.length&&jogView.viewport?.sceneObjects?.size&&!jogView.pending&&!__icaxAppShell.getState().pendingCount;},project);
  await page.locator('[data-tab-id="simulation"]').click();
  await page.waitForFunction(()=>jogView.scene?.domain?.setups?.length&&!jogView.pending&&!jogView.fiveMachine?.busy);
  const bound=await page.evaluate(()=>jogView.scene.domain.setups.find(s=>s.id===jogView.scene.domain.activeSetupId)?.machineSetup?.machineId);
  if(bound)await page.locator('[data-five-machine-instance]').selectOption(bound);
  await page.waitForFunction(()=>!jogView.pending&&!jogView.fiveMachine?.busy);
  await page.locator('.ribbon-command[data-command-id="five.tool.machine-simulation"]').click();
  await page.locator('.five-machine-jog-input').first().waitFor({state:'attached'});return project;
 };
 const source=JSON.parse(readFileSync('output/tests/ir1600-mount-20261029/cef-report.json','utf8')).project;
 report.motionProject=await open(source,'native-spinner-motion');
 await page.locator('[data-five-machine-field="jogStep"]').selectOption('0.1');
 const joint=await page.evaluate(()=>jogView.scene.machines.find(m=>m.entityId===jogView.fiveMachine.machineId).joints.find(j=>j.jointName==='J1'));
 assert.ok(joint);const arrow=direction=>page.locator(`[data-five-command="five.machine.jog-${direction}:${joint.entityId}"]`);
 await page.evaluate(()=>{window.jogCalls=[];const p=jogView.viewportSceneProxy,invoke=p.invoke;p.invoke=async function(method,request,...rest){try{return await invoke.call(this,method,request,...rest);}finally{jogCalls.push({method,request});}};});
 await arrow('plus').click();await page.waitForFunction(()=>jogCalls.some(c=>c.method==='CamSimulation.MoveMachine')&&!jogView.fiveMachine.busy);
 report.short=await page.evaluate(id=>({position:jogView.scene.machines.flatMap(m=>m.joints).find(j=>j.entityId===id).position,error:jogView.fiveMachine.error,calls:jogCalls.map(c=>c.method)}),joint.entityId);
 assert.equal(report.short.error,'');assert.ok(Math.abs(report.short.position-joint.position-.1*Math.PI/180)<1e-8);assert.equal(report.short.calls.filter(m=>m==='CamSimulation.MoveMachine').length,1);
 await arrow('minus').click();await page.waitForFunction(()=>jogCalls.filter(c=>c.method==='CamSimulation.MoveMachine').length===2&&!jogView.fiveMachine.busy);
 await arrow('plus').scrollIntoViewIfNeeded();const r=await arrow('plus').boundingBox();await page.mouse.move(r.x+r.width/2,r.y+r.height/2);await page.mouse.down();await page.waitForFunction(()=>jogView.fiveMachineJog?.stream?.stats.results>2);await page.waitForTimeout(450);await page.mouse.up();await page.waitForFunction(()=>jogView.fiveMachineJog?.ended&&!jogView.fiveMachineJog.running);
 report.hold=await page.evaluate(()=>({result:jogView.fiveMachineJog.result,stats:jogView.fiveMachineJog.lastStreamStats,error:jogView.fiveMachine.error,moves:jogCalls.filter(c=>c.method==='CamSimulation.MoveMachine').length}));
 assert.equal(report.hold.error,'');assert.equal(report.hold.result.status,'Stopped');assert.equal(report.hold.moves,2);assert.ok(report.hold.stats.results>2);
 const final=await page.evaluate(async id=>(await jogView.viewportSceneProxy.invoke('Machine.GetElement',{entityId:id})).machineElement.joint.position,joint.entityId);await page.waitForTimeout(300);
 assert.equal(await page.evaluate(async id=>(await jogView.viewportSceneProxy.invoke('Machine.GetElement',{entityId:id})).machineElement.joint.position,joint.entityId),final);report.nativePositionAfterRelease=final;
 report.robotProject=report.motionProject;
 assert.equal(await page.locator('.five-machine-jog-input').count(),6);assert.equal(await page.getByRole('button',{name:/[−＋] 点动/}).count(),0);
 report.robotArrows=await page.evaluate(()=>[...document.querySelectorAll('.five-machine-jog-input')].map(n=>{const r=n.querySelector('input').getBoundingClientRect();return [...n.querySelectorAll('button')].every(b=>{const x=b.getBoundingClientRect();return x.left>=r.left&&x.right<=r.right&&x.top>=r.top&&x.bottom<=r.bottom;});}));assert.ok(report.robotArrows.every(Boolean));
 await page.setViewportSize({width:1600,height:1000});await page.evaluate(()=>{jogView.viewport.setViewDirection([1,-1,.7]);jogView.viewport.fitViewToViewport(1.1);const pane=document.querySelector('.cam-info-pane'),section=document.querySelector('[data-five-key="machine-joints"]');pane.scrollTop+=section.getBoundingClientRect().top-pane.getBoundingClientRect().top-8;});await page.waitForTimeout(300);await page.screenshot({path:resolve(out,'native-robot-spinner.png')});
 const legacy=await page.evaluate(()=>jogView.scene.machines.find(m=>m.name.startsWith('LC-3015')));assert.ok(legacy);
 await page.locator('[data-five-machine-instance]').selectOption(legacy.entityId);await page.waitForFunction(()=>!jogView.fiveMachine.busy);
 const toggle=page.getByRole('switch',{name:'机床实例启用状态'});
 if(await toggle.getAttribute('aria-checked')==='true'){await toggle.click();await page.waitForFunction(()=>!jogView.fiveMachine.busy);assert.equal(await toggle.getAttribute('aria-checked'),'false');}
 await toggle.click();await page.waitForFunction(()=>!jogView.fiveMachine.busy);assert.equal(await toggle.getAttribute('aria-checked'),'true');report.togglePassed=true;
 const cleared=await page.evaluate(()=>({tcp:jogView.fiveMachine.tcp,beam:jogView.fiveMachine.beam,tool:jogView.fiveMachine.toolId,support:jogView.fiveMachine.supportId}));assert.deepEqual(cleared.tcp,['0','0','0']);assert.deepEqual(cleared.beam,['0','0','-1']);report.switchedDraft=cleared;
 await page.locator('[data-five-command="five.machine.mount"]').click();await page.waitForFunction(()=>!jogView.fiveMachine.busy&&!jogView.pending);
 report.legacyMount=await page.evaluate(()=>{const s=jogView.scene.domain.setups.find(s=>s.id===jogView.scene.domain.activeSetupId);return {configuration:s.machineSetup,mounting:jogView.fiveMachine.mounting,error:jogView.fiveMachine.error};});assert.equal(report.legacyMount.error,'');assert.equal(report.legacyMount.configuration.machineId,legacy.entityId);assert.deepEqual(report.legacyMount.configuration.tcp,[0,0,-220]);assert.deepEqual(report.legacyMount.configuration.beam,[0,0,-1]);
 await page.evaluate(()=>{jogView.viewport.fitViewToViewport(1.1);document.querySelector('.cam-info-pane').scrollTop=0;});await page.screenshot({path:resolve(out,'native-lc3015-mount.png')});
 assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.failure=error.stack;process.exitCode=1;if(page)await page.screenshot({path:resolve(out,'native-failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(out,'native-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,short:report.short,holdStatus:report.hold?.result.status,holdStats:report.hold?.stats,robotArrows:report.robotArrows,failure:report.failure}));await browser.close();}
