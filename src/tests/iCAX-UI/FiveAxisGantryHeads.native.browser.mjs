import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {writeFileSync,readFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import {serveBrowserAsset,browserAssetPath} from './browserPackageRuntime.mjs';
import {verifyHeadImport} from './FiveAxisHeadImport.native.browserCase.mjs';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/gantry-heads-20261101/browser');mkdirSync(output,{recursive:true});
const runtime=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||'output/tests/gantry-heads-20261101/runtime');
const child=spawn(resolve(runtime,process.env.ICAX_FIVE_AXIS_NATIVE_TEST_EXECUTABLE||'FiveAxisCAMTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:runtime,windowsHide:true,stdio:['pipe','pipe','pipe']});
const waiting=new Map(),errors=[],nativeLog=[];let serial=0,browser;
const fail=error=>{for(const p of waiting.values()){clearTimeout(p.timer);p.reject(error);}waiting.clear();};child.on('error',fail);child.on('exit',code=>{if(waiting.size)fail(Error('Native bridge exited '+code));});child.stderr.on('data',d=>nativeLog.push(String(d)));
createInterface({input:child.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__')){nativeLog.push(line);return;}const r=JSON.parse(line.slice(12)),p=waiting.get(r.id);if(!p)return;waiting.delete(r.id);clearTimeout(p.timer);r.ok?p.resolve(r.result):p.reject(Error(r.error));});
const rpc=message=>new Promise((resolve,reject)=>{const id=++serial,timer=setTimeout(()=>{waiting.delete(id);reject(Error('Native timeout '+message.action));},60000);waiting.set(id,{resolve,reject,timer});child.stdin.write(JSON.stringify({id,...message,...(message.action==='invoke'?{payload:undefined,payloadText:serializeVariantText(message.payload||{})}:{})})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
const report={passed:false,realNativeSDO:true,productionWebGL:true,standaloneCef:false,cases:[]};
try{
 const domain=await rpc({action:'prepare-axis-test'}),builtins=(await invoke('CamHead.ListBuiltins')).items;
 assert.equal(builtins.length,9);const heads=builtins.filter(h=>h.kind);assert.equal(heads.length,5);
 const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');browser=await chromium.launch({channel:'msedge',headless:true,args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 const page=await browser.newPage({viewport:{width:1540,height:980}});page.on('pageerror',e=>errors.push(e.message));await page.exposeFunction('headNativeRpc',rpc);
 await page.route('http://127.0.0.1:49187/**',route=>{const path=new URL(route.request().url()).pathname;return path.endsWith('.css')?route.fulfill({contentType:'text/css',body:readFileSync(browserAssetPath(path.slice(5)))}):serveBrowserAsset(route);});await page.goto('http://127.0.0.1:49187/');
 await page.evaluate(async({domain,builtins})=>{
  const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),S=await import('/src/apps/five-axis-cam/webpage/trajectorySimulation.mjs'),H=await import('/src/apps/five-axis-cam/webpage/cuttingHeadAssembly.mjs'),I=await import('/src/apps/five-axis-cam/webpage/cuttingHeadInstances.mjs');
  const {createThreeViewport}=await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs'),{patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs');
  const css=document.createElement('link');css.rel='stylesheet';css.href='/src/apps/five-axis-cam/webpage/workspace.css';document.head.append(css);
  document.body.innerHTML='<style>body{margin:0;font:14px Arial;color:#19303c;background:#e4ebef}main{position:relative;display:grid;grid-template-columns:250px 950px 340px;height:980px}.left,.right{overflow:auto;height:950px;padding:12px;box-sizing:border-box}.spacer{height:1300px}.nested{height:65px;overflow:auto}.five-cam-head-dialog{position:relative!important;left:0!important;top:0!important;max-width:310px!important;width:310px!important;transform:none!important;max-height:750px!important}#vp{height:950px;width:950px;background:#263a46}.probe{display:block;margin:12px 0}</style><main class="five-workspace"><aside class="left cam-context-pane"></aside><div id="vp"></div><aside class="right cam-info-pane"></aside></main>';
  const viewport=createThreeViewport({continuousRender:true,showGrid:false,showWorldAxes:true}).mount(document.querySelector('#vp'));
  const view={pending:false,fiveAxisArea:'toolpath',fiveAxisUI:{tools:{toolpath:'simulation'}},fiveSimulation:{headOpen:true,builtinHeads:builtins,cursor:0,speed:1,showHead:true,playing:false,frames:[]},viewport,scene:{domain}};
  const f=window.f={view,S,H,I,THREE,held:false,hold:false,release:null,saves:0};
  const context={mount:document.body,isCurrentProject:()=>true,projectProxy:{state:{projectPath:'C:/tests/gantry-heads.i5cam'},async autosave(){f.saves++;return {saved:true};}}};
  context.sceneProxy={async invoke(method,payload={}){if(method==='CamScene.ReadState'&&f.hold){f.hold=false;f.held=true;await new Promise(r=>f.release=r);f.held=false;}return headNativeRpc({action:'invoke',scope:'scene',method,payload});},resources:{async get(url,options){const version=new Headers(options?.headers).get('ICAX-Resource-Version')||'',r=await headNativeRpc({action:'resource',url,version,head:false,encoding:'base64'});return new Response(Uint8Array.from(atob(r.base64),x=>x.charCodeAt(0)),{headers:{'Content-Type':'application/vnd.icax.flatbuffer','ICAX-Resource-Version':r.version}});}}};
  context.sceneProxy.pdo={enabled:true,async withReadDescriptor(descriptor,reader){const result=await headNativeRpc({action:'pdo-read',slotId:String(descriptor.id),arenaName:descriptor.arenaName});return reader(Uint8Array.from(result.bytes).buffer,{dataVersion:result.dataVersion});}};
  const extra=side=>`<input class="probe" id="${side}-draft" data-five-key="${side}-draft" value="保留输入与选区"><div class="nested" data-five-key="${side}-nested"><div style="height:600px">嵌套滚动</div></div><div class="spacer" data-five-key="${side}-spacer"></div>`;
  const ops={renderProject(){patchWorkspacePanels(document.body,[['.left',S.renderCamHeadCard(view)+extra('left')],['.right',S.renderHeadSettings(view)+extra('right')]]);S.syncSimulation(context,view);},appendProjectLog(){}};
  f.context=context;f.ops=ops;ops.renderProject();document.body.addEventListener('change',event=>{const name=event.target.dataset.fiveSimulationField,instanceField=event.target.dataset.fiveHeadInstanceField;if(name)S.simulationField(context,view,name,event.target.value,ops);if(instanceField&&event.target.tagName==='SELECT'){I.headInstanceField(context,view,instanceField,event.target.value,ops);ops.renderProject();}});
  document.body.addEventListener('input',event=>{const field=event.target.dataset.fiveHeadInstanceField;if(field&&event.target.tagName!=='SELECT'){I.headInstanceField(context,view,field,event.target.value,ops);ops.renderProject();}});
  f.show=()=>{const s=view.fiveSimulation,box=new THREE.Box3().setFromObject(s.group),center=box.getCenter(new THREE.Vector3()),size=box.getSize(new THREE.Vector3()).length();viewport.setCameraState({...viewport.getCameraState(),target:{x:center.x,y:center.y,z:center.z},theta:.7,phi:1.1,radius:size*1.5});};
 },{domain,builtins});
 const selector=page.locator('[data-five-simulation-field="builtin-head"]');
 for(let i=0;i<heads.length;i++){
  const head=heads[i];if(i===0)await page.evaluate(()=>f.hold=true);
  await selector.selectOption(head.id);
  if(i===0){await page.waitForFunction(()=>f.held);const before=await page.evaluate(()=>{const left=document.querySelector('.left'),right=document.querySelector('.right'),input=document.querySelector('#right-draft'),canvas=f.view.viewport.renderer.domElement;input.focus({preventScroll:true});input.setSelectionRange(1,5);let events=0;input.addEventListener('probe',()=>events++);left.scrollTop=45;right.scrollTop=60;document.querySelectorAll('.nested').forEach(n=>n.scrollTop=35);left.scrollTop=130;right.scrollTop=170;window.saved={left,right,input,canvas,get events(){return events;}};return [left.scrollTop,right.scrollTop];});
   await page.evaluate(()=>f.release());await page.waitForFunction(()=>!f.view.pending&&!f.view.fiveSimulation.busy);
   const interaction=await page.evaluate(()=>{saved.input.dispatchEvent(new Event('probe'));return {input:saved.input===document.querySelector('#right-draft'),canvas:saved.canvas===f.view.viewport.renderer.domElement,focus:document.activeElement===saved.input,selection:[saved.input.selectionStart,saved.input.selectionEnd],scroll:[saved.left.scrollTop,saved.right.scrollTop],nested:[...document.querySelectorAll('.nested')].map(n=>n.scrollTop),events:saved.events};});
   assert.deepEqual(interaction,{input:true,canvas:true,focus:true,selection:[1,5],scroll:before,nested:[35,35],events:1});report.interaction=interaction;
  }
  await page.waitForFunction(id=>!f.view.pending&&!f.view.fiveSimulation.busy&&f.view.scene.domain.setups.find(s=>s.id===f.view.scene.domain.activeSetupId)?.headModel.builtinDefinitionId===id&&!!f.view.fiveSimulation.headAssembly,head.id);
  const check=await page.evaluate(kind=>{const s=f.view.fiveSimulation,a=s.headAssembly.userData.assembly,A=.25,B=.3,C=kind==='4+1'?0:.4;let d;
   if(kind==='3+2')d=[0,0,1];else if(kind==='AB')d=[Math.sin(B)*Math.cos(A),-Math.sin(A),Math.cos(B)*Math.cos(A)];else if(kind==='BC')d=[Math.cos(C)*Math.sin(B),Math.sin(C)*Math.sin(B),Math.cos(B)];else d=[Math.sin(C)*Math.sin(A),-Math.cos(C)*Math.sin(A),Math.cos(A)];
   const pose={rotation:new f.THREE.Quaternion().setFromUnitVectors(new f.THREE.Vector3(0,0,1),new f.THREE.Vector3(...d))};const result=f.H.paintHeadAssembly(s.headAssembly,pose);s.group.quaternion.copy(pose.rotation);s.group.updateMatrixWorld(true);f.show();
   const tcp=new f.THREE.Vector3(...a.tcp).applyMatrix4(s.headAssembly.children[a.tool].matrix).applyQuaternion(pose.rotation);
   const unavailable=f.H.headAssemblyPose(a,[0,0,-1]);document.querySelector('.left').scrollTop=0;document.querySelector('.right').scrollTop=0;
   return {valid:result.valid,parts:s.headAssembly.children.length,nodeCount:a.nodes.length,angles:result.angles,tcpError:tcp.length(),unreachable:unavailable.valid,headAxes:a.headAxes,workpieceAxes:a.workpieceAxes,status:f.view.scene.domain.setups[0].headModel.collisionModel.status};
  },head.kind);
  assert.equal(check.valid,true,head.kind);assert.ok(check.tcpError<1e-7);assert.deepEqual(check.headAxes,head.headAxes);assert.deepEqual(check.workpieceAxes,head.workpieceAxes);assert.equal(check.status,'MachineBindingRequired');assert.equal(check.parts,check.nodeCount);
  assert.equal(await page.locator('[data-five-key="head-popover"] [data-five-command^="five.machine.instantiate:"]').count(),0);
  await page.waitForTimeout(120);await page.screenshot({path:resolve(output,head.kind.replace('+','plus')+'.png')});
  report.cases.push({kind:head.kind,...check});
 }
 if(process.env.ICAX_HEAD_IMPORT_TESTS)await verifyHeadImport({page,invoke,rpc,report});
 const prior=await invoke('CamScene.ReadState');const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.setups[0].headModel,prior.setups[0].headModel);report.embeddedSaveReopen=true;
 assert.deepEqual(errors,[]);report.passed=true;
}catch(error){report.error=error.stack;throw error;}finally{
 writeFileSync(resolve(output,'report.json'),JSON.stringify({...report,errors},null,2));writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));await browser?.close();try{await rpc({action:'exit'});}catch{}child.stdin.end();child.kill();
}
console.log(JSON.stringify(report));
