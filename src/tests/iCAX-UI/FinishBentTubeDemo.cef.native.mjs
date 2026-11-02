import assert from 'node:assert/strict';import {resolve} from 'node:path';import {pathToFileURL} from 'node:url';import {readFileSync,writeFileSync} from 'node:fs';import {chromium} from 'playwright';
const output=process.env.ICAX_DEMO_REPORT,demo=process.env.ICAX_DEMO_ROOT,base=pathToFileURL(resolve('output/tests/job-sequence/cef-runtime')+'/').href;
const spec=JSON.parse(readFileSync(resolve(demo,'demo-spec.json'),'utf8')),example=spec.examples[0],expected=JSON.parse(readFileSync(resolve(output,'expected-2.json'),'utf8'));
const browser=await chromium.connectOverCDP('http://127.0.0.1:9476'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');page.setDefaultTimeout(60000);await page.setViewportSize({width:1600,height:950});
const invoke=(method,payload={})=>page.evaluate(({method,payload})=>fixture.view.viewportSceneProxy.invoke(method,payload,{timeoutMs:120000}),{method,payload});const read=()=>invoke('CamScene.ReadState');
const request=async extra=>({commandId:crypto.randomUUID(),expected:(await read()).expected,...extra});
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fiveWorkpieceSwitching);
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
async function wait(task){while(task.status==='Running'){await new Promise(r=>setTimeout(r,100));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',JSON.stringify(task));}
try{
 await page.waitForFunction(()=>__icaxAppShell?.getState().startupPhase==='ready');await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),example.project);
 await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);await idle();
 for(const built of expected.built){
   let state=await read();built.intentIds.forEach((id,j)=>assert.equal(state.intentToolpaths.find(c=>c.id===id).sourceKey,built.contours[j].sourceKey));
   await invoke('CamPlan.SetOrder',await request({setupId:built.setupId,mode:'Manual',intentIds:built.intentIds}));await wait(await invoke('CamPlan.Generate',await request({setupId:built.setupId})));
   let setup=(await read()).setups.find(s=>s.id===built.setupId);await wait(await invoke('CamRapid.Generate',await request({setupId:built.setupId,geometryRevision:setup.generatedGeometry.revision})));
   setup=(await read()).setups.find(s=>s.id===built.setupId);assert.deepEqual(setup.generatedGeometry.operations.map(op=>op.intentId),built.intentIds);assert.equal(setup.generatedGeometry.transitions.every(t=>t.geometryStatus==='Generated'&&t.validation==='Clear'),true);
 }
 await invoke('CamSetup.ExportProgram',{expected:(await read()).expected,destination:example.program});await command('five.refresh');await command('five.area.workpiece');await command('five.setup.activate:'+expected.built[0].placementId);await command('five.preparation.workpiece:'+(await read()).workpieces[0].id);await command('app.save');
 expected.state=await read();writeFileSync(resolve(output,'expected-2.json'),JSON.stringify(expected,null,2));
 await page.setViewportSize({width:1600,height:1600});await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
 const rect=await page.evaluate(()=>{const vp=fixture.view.viewport;vp.setProjectionMode('orthographic');vp.setDisplayMode('shaded');vp.setViewDirection([.15,-1,.08]);vp.fitView(1.08);return vp.renderer.domElement.getBoundingClientRect().toJSON();});
 const clip={x:Math.round(rect.x+rect.width/2-300),y:Math.ceil(rect.y+60),width:600,height:Math.floor(rect.height-70)};
 await page.screenshot({path:resolve(demo,'model-preview.png'),clip});
 await page.evaluate(()=>{const vp=fixture.view.viewport;const c=vp.getCameraState();vp.setCameraState({...c,target:{x:-280,y:0,z:2350},radius:350,theta:-1.1,phi:1.32});});
 await page.screenshot({path:resolve(demo,'end-detail.png'),clip:{x:clip.x,y:Math.round(rect.y+rect.height/2-300),width:600,height:600}});
 console.log('Manual order, clear local rapids, source identities, updated saved project and native previews verified.');
}finally{await browser.close();}
