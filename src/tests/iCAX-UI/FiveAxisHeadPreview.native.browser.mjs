import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import {serveBrowserAsset,browserAssetPath,browserReportDirectory} from './browserPackageRuntime.mjs';
import {verifyHeadPreviewLimits} from './FiveAxisHeadLimits.native.browserCase.mjs';

const output=browserReportDirectory('head-preview-20261101');
const runtime=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||'output/tests/gantry-heads-20261101/runtime');
const executable=resolve(runtime,process.env.ICAX_FIVE_AXIS_NATIVE_TEST_EXECUTABLE||'FiveAxisCAMTest.exe');
const child=spawn(executable,['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:runtime,windowsHide:true,stdio:['pipe','pipe','pipe']});
const waiting=new Map(),errors=[],nativeLog=[];let serial=0,browser,page;
const fail=error=>{for(const p of waiting.values()){clearTimeout(p.timer);p.reject(error);}waiting.clear();};child.on('error',fail);child.on('exit',code=>{if(waiting.size)fail(Error('Native bridge exited '+code));});child.stderr.on('data',d=>nativeLog.push(String(d)));
createInterface({input:child.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__')){nativeLog.push(line);return;}const r=JSON.parse(line.slice(12)),p=waiting.get(r.id);if(!p)return;waiting.delete(r.id);clearTimeout(p.timer);r.ok?p.resolve(r.result):p.reject(Error(r.error));});
const rpc=message=>new Promise((resolve,reject)=>{const id=++serial,timer=setTimeout(()=>{waiting.delete(id);reject(Error('Native timeout '+message.action));},60000);waiting.set(id,{resolve,reject,timer});child.stdin.write(JSON.stringify({id,...message,...(message.action==='invoke'?{payload:undefined,payloadText:serializeVariantText(message.payload||{})}:{})})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
const report={passed:false,realNativeResources:true,productionWebGL:true,standaloneCef:false,runtime,executable,heads:[],cases:[]};
try{
 const domain=await rpc({action:'prepare-axis-test'}),heads=(await invoke('CamHead.ListBuiltins')).items;
 browser=await chromium.launch({channel:'msedge',headless:true,args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 page=await browser.newPage({viewport:{width:1600,height:1000}});page.on('pageerror',e=>errors.push(e.message));await page.exposeFunction('headPreviewRpc',rpc);
 await page.route('http://127.0.0.1:49188/**',route=>{const path=new URL(route.request().url()).pathname;return path.endsWith('.css')?route.fulfill({contentType:'text/css',body:readFileSync(browserAssetPath(path.slice(5)))}):serveBrowserAsset(route);});await page.goto('http://127.0.0.1:49188/');
 await page.evaluate(async({domain,heads})=>{
  const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),S=await import('/src/apps/five-axis-cam/webpage/trajectorySimulation.mjs'),P=await import('/src/apps/five-axis-cam/webpage/cuttingHeadPreview.mjs'),I=await import('/src/apps/five-axis-cam/webpage/cuttingHeadInstances.mjs');
  const {createThreeViewport}=await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs'),{patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs');
  const css=document.createElement('link');css.rel='stylesheet';css.href='/src/apps/five-axis-cam/webpage/workspace.css';document.head.append(css);await new Promise(r=>css.onload=r);
  document.body.innerHTML='<style>body{margin:0;font:14px Arial;background:#e4ebef}main{display:grid;grid-template-columns:230px 1100px 270px;height:1000px}.left,.right{height:980px;overflow:auto;padding:12px;box-sizing:border-box}.spacer{height:1300px}.nested{height:65px;overflow:auto}#vp{height:980px;position:relative}.five-overlay{position:fixed;inset:0}.five-cam-head-dialog{position:fixed!important;left:280px!important;top:100px!important;transform:none!important}.probe{display:block;margin:12px 0}</style><main class="five-workspace"><aside class="left cam-context-pane"></aside><div id="vp"></div><aside class="right cam-info-pane"></aside><div class="five-overlay" data-five-overlay></div></main>';
  const viewport=createThreeViewport({continuousRender:false,showGrid:false}).mount(document.querySelector('#vp'));
  const view={pending:false,fiveAxisArea:'toolpath',fiveAxisUI:{tools:{toolpath:'extract'}},fiveSimulation:{headOpen:true,builtinHeads:heads,cursor:0,speed:1,showHead:true,playing:false,frames:[]},viewport,scene:{domain}};
  const f=window.f={view,S,P,THREE,held:false,holdRead:false,holdResource:false};
  const context={mount:document.body,isCurrentProject:()=>true,projectProxy:{state:{projectPath:'C:/tests/head-preview.i5cam'},async autosave(){return {saved:true};}}};
  context.sceneProxy={async invoke(method,payload={}){if(method==='CamScene.ReadState'&&f.holdRead){f.holdRead=false;f.held=true;await new Promise(r=>f.release=r);f.held=false;}return headPreviewRpc({action:'invoke',scope:'scene',method,payload});},resources:{async get(url,options){
   if(url==='resource://broken')return new Response('',{status:404});
   if(f.holdResource){f.holdResource=false;f.resourceHeld=true;await new Promise(r=>f.releaseResource=r);f.resourceHeld=false;}
   const version=new Headers(options?.headers).get('ICAX-Resource-Version')||'',r=await headPreviewRpc({action:'resource',url,version,head:false,encoding:'base64'});return new Response(Uint8Array.from(atob(r.base64),x=>x.charCodeAt(0)),{headers:{'Content-Type':'application/vnd.icax.flatbuffer','ICAX-Resource-Version':r.version}});
  }}};
  const extra=side=>`<input id="${side}-draft" class="probe" data-five-key="${side}-draft" value="保留焦点与选区"><div class="nested" data-five-key="${side}-nested"><div style="height:600px">嵌套滚动</div></div><div class="spacer" data-five-key="${side}-spacer"></div>`;
  const ops={renderProject(){patchWorkspacePanels(document.body,[['.left',extra('left')],['.right',extra('right')],['[data-five-overlay]',S.renderHeadSettings(view)]]);P.syncCuttingHeadPreview(context,view);},appendProjectLog(){}};
  f.context=context;f.ops=ops;f.head=()=>view.scene.domain.setups.find(s=>s.id===view.scene.domain.activeSetupId).headModel;f.mainCamera=()=>JSON.stringify(viewport.getCameraState());
  ops.renderProject();document.body.addEventListener('change',event=>{const name=event.target.dataset.fiveSimulationField;if(name)S.simulationField(context,view,name,event.target.value,ops);});
  document.body.addEventListener('click',event=>{const cmd=event.target.closest('[data-five-command]')?.dataset.fiveCommand;if(cmd?.startsWith('five.simulation.'))void S.simulationCommand(context,view,cmd.slice(16),ops);if(cmd?.startsWith('five.head-instance.'))void I.headInstanceCommand(context,view,cmd.slice('five.head-instance.'.length),ops);});
 },{domain,heads});
 const selector=page.locator('[data-five-simulation-field="builtin-head"]'),ready=()=>page.waitForFunction(()=>!f.view.pending&&f.view.fiveHeadPreview?.phase==='ready',null,{timeout:60000});
 for(let i=0;i<heads.length;i++){
  if(await page.locator('[data-five-command="five.head-instance.tab.model"]').count())await page.locator('[data-five-command="five.head-instance.tab.model"]').click();
  const head=heads[i];if(i===0)await page.evaluate(()=>f.holdRead=true);
  await selector.selectOption(head.id);
  if(i===0){
   await page.waitForFunction(()=>f.held);const scrolls=await page.evaluate(()=>{const input=document.querySelector('#right-draft'),left=document.querySelector('.left'),right=document.querySelector('.right');input.focus({preventScroll:true});input.setSelectionRange(1,5);left.scrollTop=120;right.scrollTop=160;document.querySelectorAll('.nested').forEach(n=>n.scrollTop=35);window.saved={input,left,right,preview:document.querySelector('[data-five-head-preview]'),canvas:f.view.viewport.renderer.domElement};return [left.scrollTop,right.scrollTop];});
   await page.evaluate(()=>f.release());await ready();
   const interaction=await page.evaluate(()=>({input:saved.input===document.querySelector('#right-draft'),preview:saved.preview===document.querySelector('[data-five-head-preview]'),canvas:saved.canvas===f.view.viewport.renderer.domElement,focus:document.activeElement===saved.input,selection:[saved.input.selectionStart,saved.input.selectionEnd],scrolls:[saved.left.scrollTop,saved.right.scrollTop],nested:[...document.querySelectorAll('.nested')].map(n=>n.scrollTop)}));
   assert.deepEqual(interaction,{input:true,preview:true,canvas:true,focus:true,selection:[1,5],scrolls,nested:[35,35]});report.interaction=interaction;
  }
  await ready();const shape=await page.evaluate(()=>{const p=f.view.fiveHeadPreview,box=new f.THREE.Box3().setFromObject(p.model);let vertices=0;p.model.traverse(m=>vertices+=m.geometry?.attributes.position?.count||0);return {name:f.head().name,kind:f.head().assembly?.kind||'Nozzle',vertices,size:box.getSize(new f.THREE.Vector3()).toArray(),parts:f.head().assembly?.nodes.length||1,canvas:[p.canvas.width,p.canvas.height],color:getComputedStyle(document.querySelector('.five-cam-head-dialog')).color};});
  assert.ok(shape.vertices>0);assert.ok(shape.size.every(x=>x>0));assert.equal(shape.color,'rgb(32, 54, 64)');
  const dialogBox=await page.locator('[data-five-key="head-popover"]').boundingBox(),settingsBox=await page.locator('.five-head-settings').boundingBox();assert.ok(settingsBox.x+settingsBox.width<=dialogBox.x+dialogBox.width,'Settings must fit inside the dialog');assert.ok(shape.canvas[0]>250);
  const instanceMode=await page.evaluate(()=>!!f.head().instance),effective=page.locator('details[data-five-key="head-effective-parameters"]');
  if(instanceMode)await page.locator('[data-five-command="five.head-instance.tab.axes"]').click();
  if(instanceMode){assert.equal(await effective.count(),1);if(!await effective.evaluate(node=>node.open))await effective.locator(':scope > summary').click();}
  const parameters=instanceMode?effective.locator('.five-head-parameters'):page.locator('.five-head-parameters');
  assert.equal(await parameters.locator('[data-five-head-parameter="gauge-length"]').isVisible(),true);
  assert.match(await parameters.locator('[data-five-head-parameter="gauge-length"]').innerText(),instanceMode?/按局部 TCP 坐标定义/:/未定义测量基准/);
  if(head.kind==='AB'){
   const engineering=await invoke('CamScene.ReadState');
   assert.equal(await parameters.locator('[data-five-head-parameter="chain"]').innerText(),'B → A');
   assert.match(await parameters.locator('[data-five-head-parameter="tcp"]').innerText(),/-220/);
   const detail=parameters.locator('details[data-five-key="head-axis-parameters"]');if(!await detail.evaluate(node=>node.open))await detail.locator(':scope > summary').click();
   assert.match(await parameters.locator('[data-five-head-parameter="axis-1-position"]').innerText(),/-75/);
   assert.equal(await parameters.locator('[data-five-head-parameter="axis-1-zero-offset"]').isVisible(),true);
   assert.equal(await parameters.locator('[data-five-head-parameter="axis-1-zero-offset"]').innerText(),instanceMode?'0 °':'未单独声明');
   assert.equal(await parameters.locator('input').count(),0);
   await parameters.screenshot({path:resolve(output,'AB-机构与TCP参数.png')});
   await detail.locator('summary').click();assert.deepEqual(await invoke('CamScene.ReadState'),engineering);
   report.cases.push('AB displays the real B-to-A chain, separated pivot, local TCP and angle limits; instance mode shows local-TCP coordinates and zero calibration offsets in expanded effective parameters; inspecting them does not change engineering state');
  }
  if(instanceMode)await effective.locator(':scope > summary').click();
  if(head.kind==='AC'&&instanceMode)report.finiteJointLimits=await verifyHeadPreviewLimits({page,invoke,output});
  if(head.kind==='AB'){
   const snapshot=()=>page.evaluate(()=>({matrices:f.view.fiveHeadPreview.model.children.map(m=>m.matrix.toArray()),positions:f.view.fiveHeadPreview.previewPositions,saved:f.head().assembly.nodes.map(n=>n.position)}));
   const before=await snapshot(),native=await invoke('CamScene.ReadState');assert.equal(shape.parts,3);assert.equal(await page.locator('[data-five-head-joint]').count(),2);
   assert.match(await page.locator('[data-five-head-joint="2"]').getAttribute('aria-label'),/A.*X/);assert.match(await page.locator('[data-five-head-joint="1"]').getAttribute('aria-label'),/B.*Y/);
   await page.locator('[data-five-head-joint="2"]').evaluate(input=>{input.value='30';input.dispatchEvent(new Event('input',{bubbles:true}));});
   const a=await snapshot();assert.deepEqual(a.matrices.slice(0,2),before.matrices.slice(0,2));assert.notDeepEqual(a.matrices[2],before.matrices[2]);assert.deepEqual(a.saved,before.saved);
   await page.locator('[data-five-key="head-popover"]').screenshot({path:resolve(output,'AB-A30.png')});await page.locator('[data-five-head-joints-reset]').click();
   await page.locator('[data-five-head-joint="1"]').evaluate(input=>{input.value='30';input.dispatchEvent(new Event('input',{bubbles:true}));});
   const b=await snapshot();assert.deepEqual(b.matrices[0],before.matrices[0]);assert.notDeepEqual(b.matrices[1],before.matrices[1]);assert.notDeepEqual(b.matrices[2],before.matrices[2]);assert.deepEqual(b.saved,before.saved);
   await page.locator('[data-five-key="head-popover"]').screenshot({path:resolve(output,'AB-B30.png')});await page.locator('[data-five-head-joints-reset]').click();assert.deepEqual((await snapshot()).matrices,before.matrices);
   assert.deepEqual(await invoke('CamScene.ReadState'),native);report.abJointControls={parts:3,A:'X; tool only',B:'Y; carrier and tool',fixedMountUnchanged:true,savedSceneUnchanged:true};
  }
  if(head.kind)await page.locator('[data-five-key="head-popover"]').screenshot({path:resolve(output,head.kind.replace('+','plus')+'.png')});report.heads.push({id:head.id,...shape});
 }
 report.cases.push('All nine real native head models load without trajectory playback; dark readable dialog text, saved part transforms, TCP and beam markers');
 const beforeNative=await invoke('CamScene.ReadState'),before=await page.evaluate(()=>({main:f.mainCamera(),preview:f.view.fiveHeadPreview.camera.position.toArray(),rotation:f.view.fiveHeadPreview.camera.quaternion.toArray(),target:f.view.fiveHeadPreview.target.toArray()}));
 const canvas=page.locator('[data-five-head-preview]'),box=await canvas.boundingBox();await page.mouse.move(box.x+box.width*.5,box.y+box.height*.5);await page.mouse.down();await page.mouse.move(box.x+box.width*.7,box.y+box.height*.6,{steps:8});await page.mouse.up();
 assert.deepEqual(await page.evaluate(()=>f.view.fiveHeadPreview.camera.position.toArray()),before.preview,'Left drag must not navigate the preview');
 await page.mouse.move(box.x+box.width*.5,box.y+box.height*.5);await page.mouse.down({button:'right'});await page.mouse.move(box.x+box.width*.7,box.y+box.height*.6,{steps:8});await page.mouse.up({button:'right'});await page.mouse.wheel(0,160);
 const after=await page.evaluate(()=>({main:f.mainCamera(),preview:f.view.fiveHeadPreview.camera.position.toArray(),rotation:f.view.fiveHeadPreview.camera.quaternion.toArray(),target:f.view.fiveHeadPreview.target.toArray()}));assert.equal(after.main,before.main);assert.notDeepEqual(after.preview,before.preview);assert.notDeepEqual(after.rotation,before.rotation,'Right drag must rotate');assert.deepEqual(after.target,before.target,'Right drag must not pan');assert.deepEqual(await invoke('CamScene.ReadState'),beforeNative);
 await page.evaluate(()=>{f.retained=f.view.fiveHeadPreview;f.model=f.retained.model;f.ops.renderProject();});assert.equal(await page.evaluate(()=>f.retained===f.view.fiveHeadPreview&&f.model===f.view.fiveHeadPreview.model),true);assert.deepEqual(await page.evaluate(()=>f.view.fiveHeadPreview.camera.position.toArray()),after.preview);
 await page.locator('[data-five-head-preview-reset]').click();assert.deepEqual(await page.evaluate(()=>f.view.fiveHeadPreview.camera.position.toArray()),before.preview);
 report.cases.push('Right drag rotates, left drag does not navigate, zoom and reset preserve main camera and authoritative CAD/CAM state; async patches retain canvas, renderer, model and camera');
 // Complete an older resource request after a newer selection is already visible.
 await page.locator('[data-five-command="five.head-instance.tab.model"]').click();await page.evaluate(()=>f.holdResource=true);await selector.selectOption(heads[0].id);await page.waitForFunction(()=>f.resourceHeld);
 await page.waitForFunction(()=>!f.view.pending);await selector.selectOption(heads[1].id);await ready();await page.evaluate(()=>{f.latest=f.view.fiveHeadPreview.model;f.releaseResource();});await page.waitForFunction(()=>!f.resourceHeld);await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
 assert.equal(await page.evaluate(()=>f.latest===f.view.fiveHeadPreview.model),true);
 await page.locator('[data-five-command="five.simulation.head-close"]').click();assert.equal(await page.evaluate(()=>f.view.fiveHeadPreview===null),true);
 await page.evaluate(()=>{f.view.fiveSimulation.headOpen=true;f.ops.renderProject();});await ready();
 report.cases.push('Stale model response cannot replace a newer head; closing releases the renderer and reopening recreates a working preview');
 await page.evaluate(()=>{f.head().assembly=null;f.head().previewRef={url:'resource://broken',version:'1'};f.head().collisionSource='';f.ops.renderProject();});await page.waitForFunction(()=>f.view.fiveHeadPreview?.phase==='error');assert.match(await page.locator('.five-head-preview-status').innerText(),/预览加载失败/);assert.equal(await page.evaluate(()=>f.view.fiveHeadPreview.model===null),true);
 report.cases.push('Missing geometry reports a visible error and clears the previous model');
 assert.deepEqual(errors,[]);report.passed=true;
}catch(error){report.error=error.stack;if(page)await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}finally{
 writeFileSync(resolve(output,'report.json'),JSON.stringify({...report,errors},null,2));writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));await browser?.close();try{await rpc({action:'exit'});}catch{}child.stdin.end();child.kill();
}
console.log(JSON.stringify(report));
