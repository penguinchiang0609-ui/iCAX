import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {serveBrowserAsset,browserReportDirectory} from './browserPackageRuntime.mjs';
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const output=browserReportDirectory('five-axis-machine-mount-manual');
const browser=await chromium.launch({channel:'msedge',headless:true});
try{
 const page=await browser.newPage({viewport:{width:1440,height:950}});
 await page.route('https://machine-mount-manual.test/**',route=>{
  if(new URL(route.request().url()).pathname==='/src/apps/five-axis-cam/webpage/entry.mjs'){
   const source=readFileSync(resolve('src/apps/five-axis-cam/webpage/entry.mjs'),'utf8');
   return route.fulfill({contentType:'text/javascript',body:source+'\nexport {command as testEntryCommand};\n'});
  }
  return serveBrowserAsset(route);
 });
 await page.goto('https://machine-mount-manual.test/');
 await page.evaluate(async()=>{
  const [{machineField,renderMachineRight},{selectTool},{patchWorkspacePanels},{testEntryCommand}]=await Promise.all([
   import('/src/apps/five-axis-cam/webpage/machineSimulation.mjs'),
   import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs'),
   import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs'),
   import('/src/apps/five-axis-cam/webpage/entry.mjs')]);
  const nativeDomain={jobId:'job',activeSetupId:'setup',expected:{epoch:'e',mutationSequence:'1'},setups:[{id:'setup',name:'装夹',rapidSettings:{followHeightMm:1}}],workpieces:[],placements:[],fixtures:[],fixtureUnits:[],intentToolpaths:[]};
  const machine={entityId:'machine',enabled:true,name:'机床',elements:[{entityId:'table',kind:'link',name:'工作台'},{entityId:'tool',kind:'link',name:'刀头'}],joints:[],collisions:[],postprocessor:{name:'轴运动',format:'JointJSON',axes:[]}};
  document.body.innerHTML='<main id="mount"><aside class="left" style="height:100px;overflow:auto"><div style="height:400px"><div class="nested" style="height:70px;overflow:auto"><div style="height:300px"><input id="other" value="abcdef"></div></div></div></aside><aside class="cam-info-pane" style="height:150px;overflow:auto"></aside><canvas></canvas></main>';
  const view={fiveAxisArea:'simulation',scene:{domain:structuredClone(nativeDomain),machines:[machine]}},calls=[];selectTool(view,'machine-simulation');
  let releaseConfigure,releaseMount,saves=0,hold=false,holdMount=false,failMount=false,acceptConfigure=false;
  const context={mount:document.querySelector('#mount'),project:{projectId:'p'},isCurrentProject:()=>true,projectProxy:{state:{projectPath:'C:/test/manual-mount.i5cam'},async autosave(){saves++;}},actions:{async refreshNativeProjectHistory(){},refreshProjectHistoryControls(){}},sceneProxy:{async invoke(method,request){
   calls.push({method,request:structuredClone(request)});
   if(method==='CamSimulation.ConfigureMachine'){
    if(hold){hold=false;await new Promise(resolve=>releaseConfigure=resolve);}
    if(acceptConfigure){acceptConfigure=false;nativeDomain.setups[0].machineSetup=structuredClone(request.configuration);nativeDomain.expected.mutationSequence=String(Number(nativeDomain.expected.mutationSequence)+1);return {configuration:request.configuration};}
    throw Error('CAM_MACHINE_PLACEMENT_BELOW_TABLE: 当前草稿尚未放到台面');
   }
   if(method==='CamSimulation.MountMachine'){
    if(holdMount){holdMount=false;await new Promise(resolve=>releaseMount=resolve);}
    if(failMount){failMount=false;throw Error('模拟放置失败');}
    const config=request.configuration||nativeDomain.setups[0].machineSetup;
    if(!config?.toolId||!config?.supportId)throw Error('CAM_MACHINE_ENDPOINT_METADATA_REQUIRED: 请手动选择工作台和刀具部件。');
    nativeDomain.setups[0].machineSetup=structuredClone(config);nativeDomain.expected.mutationSequence=String(Number(nativeDomain.expected.mutationSequence)+1);
    return {configuration:structuredClone(config),mounting:{setupBounds:{min:[-10,-10,-10],max:[10,10,10]},surfaceZMm:5}};
   }
   if(method==='CamScene.ReadState')return structuredClone(nativeDomain);if(method==='Machine.List')return {machines:[machine]};throw Error(method);
  }}};
  const ops={renderProject(){patchWorkspacePanels(context.mount,[['.cam-info-pane',renderMachineRight(view)]]);},async refreshSceneState(){view.scene.domain=structuredClone(nativeDomain);},async fitViewAfterRenderPublish(){},appendProjectLog(_c,_level,message){window.mountLastLog=message;}};
  context.mount.addEventListener('change',event=>{const key=event.target.dataset.fiveMachineField;if(key)machineField(context,view,key,event.target.value,ops);});
  context.mount.addEventListener('input',event=>{const key=event.target.dataset.fiveMachineField;if(key&&event.target.tagName==='INPUT')machineField(context,view,key,event.target.value,ops);});
  context.mount.addEventListener('click',event=>{if(event.target.closest('[data-five-command="five.machine.mount"]'))window.mountTask=testEntryCommand(context,view,'five.machine.mount',ops).catch(e=>{window.mountError=e.message;});});
  window.mountTest={view,calls,runEntry:id=>testEntryCommand(context,view,id,ops),hold(){hold=true;},acceptNextConfigure(){acceptConfigure=true;},release(){releaseConfigure?.();releaseConfigure=null;},get held(){return !!releaseConfigure;},holdMount(){holdMount=true;},releaseMount(){releaseMount?.();releaseMount=null;},get mountHeld(){return !!releaseMount;},failNextMount(){failMount=true;},get saves(){return saves;},get native(){return structuredClone(nativeDomain);}};ops.renderProject();
 });
 // No endpoints: the native endpoint error remains visible and no setup is saved.
 await page.locator('[data-five-command="five.machine.mount"]').click();await page.evaluate(()=>window.mountTask);
 assert.match(await page.evaluate(()=>window.mountTest.view.fiveMachine.error),/手动选择/);
 assert.equal(await page.evaluate(()=>window.mountTest.native.setups[0].machineSetup),undefined);
 // Half-selected draft must not submit a native mount.
 await page.locator('[data-five-machine-field="supportId"]').selectOption('table');
 const countBefore=await page.evaluate(()=>window.mountTest.calls.filter(c=>c.method==='CamSimulation.MountMachine').length);
 await page.locator('[data-five-command="five.machine.mount"]').click();await page.evaluate(()=>window.mountTask);
 assert.match(await page.evaluate(()=>window.mountTest.view.fiveMachine.error),/请选择机床、工作台和刀具部件/);
 assert.equal(await page.evaluate(()=>window.mountTest.calls.filter(c=>c.method==='CamSimulation.MountMachine').length),countBefore);
 // A previous automatic setup request is already in flight when a newer TCP
 // is entered. It fails for a below-table draft; mount must use the new input.
 await page.evaluate(()=>window.mountTest.hold());
 await page.locator('[data-five-machine-field="toolId"]').selectOption('tool');
 await page.waitForFunction(()=>window.mountTest.held);
 await page.locator('[data-five-machine-field="tcp2"]').fill('-220');
 await page.evaluate(()=>window.mountTest.holdMount());
 await page.locator('[data-five-command="five.machine.mount"]').click();
 assert.equal(await page.evaluate(()=>window.mountTest.calls.filter(c=>c.method==='CamSimulation.MountMachine').length),countBefore,'mount must wait for the in-flight configuration');
 await page.evaluate(()=>window.mountTest.release());await page.waitForFunction(()=>window.mountTest.mountHeld);
 await page.evaluate(()=>{const left=document.querySelector('.left'),right=document.querySelector('.cam-info-pane'),nested=document.querySelector('.nested'),input=document.querySelector('#other');left.scrollTop=40;right.scrollTop=55;nested.scrollTop=30;input.focus({preventScroll:true});input.setSelectionRange(1,4);});
 const uiBefore=await page.evaluate(()=>({left:document.querySelector('.left').scrollTop,right:document.querySelector('.cam-info-pane').scrollTop,nested:document.querySelector('.nested').scrollTop,active:document.activeElement.id,start:document.activeElement.selectionStart,end:document.activeElement.selectionEnd}));
 await page.evaluate(()=>window.mountTest.releaseMount());await page.evaluate(()=>window.mountTask);
 const uiAfter=await page.evaluate(()=>({left:document.querySelector('.left').scrollTop,right:document.querySelector('.cam-info-pane').scrollTop,nested:document.querySelector('.nested').scrollTop,active:document.activeElement.id,start:document.activeElement.selectionStart,end:document.activeElement.selectionEnd}));
 assert.deepEqual(uiAfter,uiBefore,'asynchronous mount must retain later focus, selection and pane scrolling');
 const result=await page.evaluate(()=>({calls:window.mountTest.calls,native:window.mountTest.native,saves:window.mountTest.saves,error:window.mountTest.view.fiveMachine.error}));
 const config=result.calls.findLast(c=>c.method==='CamSimulation.MountMachine').request.configuration;
 assert.equal(config.supportId,'table');assert.equal(config.toolId,'tool');assert.equal(config.tcp[2],-220);
 assert.deepEqual(result.native.setups[0].machineSetup,config);
 assert.equal(result.calls.filter(c=>c.method==='CamSimulation.MountMachine').length,countBefore+1);
 assert.equal(result.error,'');assert.ok(result.saves>=1);
 // An already failed setup save is recoverable via atomic mount too.
 await page.locator('[data-five-machine-field="tcp2"]').fill('-221');
 await page.waitForFunction(()=>window.mountTest.view.fiveMachine.error.includes('BELOW_TABLE'));
 assert.match(await page.evaluate(async()=>{try{await window.mountTest.runEntry('five.machine.reset');return '';}catch(e){return e.message;}}),/BELOW_TABLE/,'other machine commands must still flush and respect failed drafts');
 await page.locator('[data-five-command="five.machine.mount"]').click();await page.evaluate(()=>window.mountTask);
 const afterFailedSave=await page.evaluate(()=>window.mountTest.calls.findLast(c=>c.method==='CamSimulation.MountMachine').request.configuration);
 assert.equal(afterFailedSave.tcp[2],-221);
 // Failure must preserve the pending draft so the user can correct or retry.
 await page.locator('[data-five-machine-field="tcp2"]').fill('-222');await page.evaluate(()=>window.mountTest.failNextMount());
 await page.locator('[data-five-command="five.machine.mount"]').click();await page.evaluate(()=>window.mountTask);
 assert.match(await page.evaluate(()=>window.mountTest.view.fiveMachine.error),/模拟放置失败/);
 assert.equal(await page.locator('[data-five-machine-field="tcp2"]').inputValue(),'-222');
 assert.equal(await page.evaluate(()=>window.mountTest.view.fiveMachine.dirty),true);
 await page.locator('[data-five-command="five.machine.mount"]').click();await page.evaluate(()=>window.mountTask);
 assert.equal(await page.evaluate(()=>window.mountTest.calls.findLast(c=>c.method==='CamSimulation.MountMachine').request.configuration.tcp[2]),-222);
 // Successful older acknowledgement must not reset the newer draft either.
 await page.evaluate(()=>{window.mountTest.hold();window.mountTest.acceptNextConfigure();});
 await page.locator('[data-five-machine-field="tcp2"]').fill('-223');await page.waitForFunction(()=>window.mountTest.held);
 await page.locator('[data-five-machine-field="tcp2"]').fill('-224');
 await page.locator('[data-five-command="five.machine.mount"]').click();await page.evaluate(()=>window.mountTest.release());await page.evaluate(()=>window.mountTask);
 assert.equal(await page.evaluate(()=>window.mountTest.calls.findLast(c=>c.method==='CamSimulation.MountMachine').request.configuration.tcp[2]),-224);
 assert.equal(await page.evaluate(()=>window.mountTest.native.setups[0].machineSetup.tcp[2]),-224);
 writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,cases:['missing endpoint error','half selection blocks mount','in-flight stale save rejected and new TCP mounted','focus selection and nested panes preserved','failed setup save followed by mount','failed mount preserves retry draft','successful old acknowledgement cannot replace newer TCP'],result},null,2));
 console.log('PASS endpoint validation, stale and failed setup, atomic mount, retry and interaction state');
}finally{await browser.close();}
