import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import {serveBrowserAsset,browserAssetPath} from './browserPackageRuntime.mjs';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/head-choice-delete-20261102');mkdirSync(output,{recursive:true});
const runtime=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||'src/x64/Debug');
const child=spawn(resolve(runtime,process.env.ICAX_FIVE_AXIS_NATIVE_TEST_EXECUTABLE||'FiveAxisHeadInstanceTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:runtime,windowsHide:true,stdio:['pipe','pipe','pipe']});
const waiting=new Map(),errors=[],nativeLog=[];let serial=0,browser,page;
const fail=error=>{for(const p of waiting.values()){clearTimeout(p.timer);p.reject(error);}waiting.clear();};child.on('error',fail);child.on('exit',code=>{if(waiting.size)fail(Error('Native bridge exited '+code));});child.stderr.on('data',d=>nativeLog.push(String(d)));
createInterface({input:child.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__')){nativeLog.push(line);return;}const r=JSON.parse(line.slice(12)),p=waiting.get(r.id);if(!p)return;waiting.delete(r.id);clearTimeout(p.timer);r.ok?p.resolve(r.result):p.reject(Error(r.error));});
const rpc=message=>new Promise((resolve,reject)=>{const id=++serial,timer=setTimeout(()=>{waiting.delete(id);reject(Error('Native timeout '+message.action));},60000);waiting.set(id,{resolve,reject,timer});child.stdin.write(JSON.stringify({id,...message,...(message.action==='invoke'?{payload:undefined,payloadText:serializeVariantText(message.payload||{})}:{})})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
const report={passed:false,realNativeSDO:true,productionWebGL:true,standaloneCef:false,cases:[]};
const checkpoint=text=>{report.cases.push(text);console.log(text);};
try{
 const domain=await rpc({action:'prepare-axis-test'}),heads=(await invoke('CamHead.ListBuiltins')).items;
 browser=await chromium.launch({channel:'msedge',headless:true,args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 page=await browser.newPage({viewport:{width:1600,height:1000}});page.on('pageerror',e=>errors.push(e.message));await page.exposeFunction('headInstanceRpc',rpc);
 await page.route('http://127.0.0.1:49189/**',route=>{const path=new URL(route.request().url()).pathname;return path.endsWith('.css')?route.fulfill({contentType:'text/css',body:readFileSync(browserAssetPath(path.slice(5)))}):serveBrowserAsset(route);});await page.goto('http://127.0.0.1:49189/');
 await page.evaluate(async({domain,heads})=>{
  const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),S=await import('/src/apps/five-axis-cam/webpage/trajectorySimulation.mjs'),H=await import('/src/apps/five-axis-cam/webpage/cuttingHeadColliders.mjs'),P=await import('/src/apps/five-axis-cam/webpage/cuttingHeadPreview.mjs'),I=await import('/src/apps/five-axis-cam/webpage/cuttingHeadInstances.mjs');
  const {createThreeViewport}=await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs'),{patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs');
  const css=document.createElement('link');css.rel='stylesheet';css.href='/src/apps/five-axis-cam/webpage/workspace.css';document.head.append(css);await new Promise(r=>css.onload=r);
  document.body.innerHTML='<style>body{margin:0;font:14px Arial;background:#e4ebef}main{display:grid;grid-template-columns:230px 1100px 270px;height:1000px}.left,.right{height:980px;overflow:auto;padding:12px;box-sizing:border-box}.spacer{height:1300px}.nested{height:65px;overflow:auto}#vp{height:980px;position:relative}.five-overlay{position:fixed;inset:0;pointer-events:none}.five-cam-head-dialog{pointer-events:auto;position:fixed!important;left:280px!important;top:70px!important;transform:none!important}.probe{display:block;margin:12px 0}</style><main class="five-workspace"><aside class="left cam-context-pane"></aside><div id="vp"></div><aside class="right cam-info-pane"></aside><div class="five-overlay" data-five-overlay></div></main>';
  const viewport=createThreeViewport({continuousRender:false,showGrid:false}).mount(document.querySelector('#vp'));
  const view={pending:false,fiveAxisArea:'toolpath',fiveAxisUI:{tools:{toolpath:'extract'}},fiveSimulation:{headOpen:true,builtinHeads:heads,cursor:0,speed:1,showHead:true,playing:false,frames:[]},viewport,scene:{domain}};
  const f=window.f={view,S,P,I,H,THREE,held:false,holdRead:false,saves:0,loseReceipt:false};
  const context={mount:document.body,isCurrentProject:()=>true,projectProxy:{state:{projectPath:'C:/tests/head-instances.i5cam'},async autosave(){f.saves++;return {saved:true};}}};
  context.sceneProxy={async invoke(method,payload={}){
   if(method==='CamScene.ReadState'&&f.holdRead){f.holdRead=false;f.held=true;await new Promise(r=>f.release=r);f.held=false;}
   let result;try{result=await headInstanceRpc({action:'invoke',scope:'scene',method,payload});}catch(error){f.lastNativeError={method,message:error.message};throw error;}
   if(method==='CamHead.Instance'&&f.loseReceipt){f.loseReceipt=false;throw Error('Transport acknowledgement lost');}return result;
  },resources:{async get(url,options){const version=new Headers(options?.headers).get('ICAX-Resource-Version')||'',r=await headInstanceRpc({action:'resource',url,version,head:false,encoding:'base64'});return new Response(Uint8Array.from(atob(r.base64),x=>x.charCodeAt(0)),{headers:{'Content-Type':'application/vnd.icax.flatbuffer','ICAX-Resource-Version':r.version}});}}};
  context.sceneProxy.pdo={enabled:true,async withReadDescriptor(descriptor,reader){const result=await headInstanceRpc({action:'pdo-read',slotId:String(descriptor.id),arenaName:descriptor.arenaName});return reader(Uint8Array.from(result.bytes).buffer,{dataVersion:result.dataVersion});}};
  const extra=side=>`<input id="${side}-draft" class="probe" data-five-key="${side}-draft" value="保留焦点与选区"><div class="nested" data-five-key="${side}-nested"><div style="height:600px">嵌套滚动</div></div><div class="spacer" data-five-key="${side}-spacer"></div>`;
  const ops={renderProject(){patchWorkspacePanels(document.body,[['.left',S.renderCamHeadCard(view)+extra('left')],['.right',extra('right')],['[data-five-overlay]',S.renderHeadSettings(view)]]);P.syncCuttingHeadPreview(context,view,ops);},appendProjectLog(){}};
  Object.assign(f,{context,ops});f.head=()=>view.scene.domain.setups.find(s=>s.id===view.scene.domain.activeSetupId).headModel;f.refresh=async()=>{view.scene.domain=await context.sceneProxy.invoke('CamScene.ReadState');ops.renderProject();};
  ops.renderProject();document.body.addEventListener('change',event=>{if(event.target.dataset.fiveHeadColliderField){if(event.target.tagName==='SELECT')H.headColliderField(context,view,event.target.dataset.fiveHeadColliderField,event.target.value,ops);return;}const name=event.target.dataset.fiveSimulationField,field=event.target.dataset.fiveHeadInstanceField;if(name)S.simulationField(context,view,name,event.target.value,ops);if(field&&event.target.tagName==='SELECT'){I.headInstanceField(context,view,field,event.target.value,ops);ops.renderProject();}});
  document.body.addEventListener('input',event=>{if(event.target.dataset.fiveHeadColliderField){if(event.target.tagName!=='SELECT')H.headColliderField(context,view,event.target.dataset.fiveHeadColliderField,event.target.type==='checkbox'?event.target.checked:event.target.value,ops);return;}const field=event.target.dataset.fiveHeadInstanceField;if(field&&event.target.tagName!=='SELECT'){I.headInstanceField(context,view,field,event.target.value,ops);ops.renderProject();}});
  document.body.addEventListener('click',event=>{const cmd=event.target.closest('[data-five-command]')?.dataset.fiveCommand;if(cmd?.startsWith('five.head-collider.'))void H.headColliderCommand(context,view,cmd.slice('five.head-collider.'.length),ops);if(cmd?.startsWith('five.head-instance.'))void I.headInstanceCommand(context,view,cmd.slice('five.head-instance.'.length),ops);});
 },{domain,heads});
 const combo=page.locator('[data-five-simulation-field="head-choice"]'),idle=()=>page.waitForFunction(()=>!f.view.pending&&!f.view.fiveHeadEditor?.busy&&!f.view.fiveSimulation.busy&&!f.view.fiveSimulation.headChoiceBusy);
 await combo.selectOption('builtin:'+heads.find(h=>h.kind==='AB').id);await idle();await page.waitForFunction(()=>f.view.fiveHeadPreview?.phase==='ready');
 const save=async()=>{const d=await invoke('CamScene.ReadState');return invoke('CamHead.Configurations',{action:'save',commandId:crypto.randomUUID(),expected:d.expected,setupId:d.activeSetupId,name:'同名刀头'});};
 const a=await save(),b=await save();assert.notEqual(a.id,b.id);await page.evaluate(()=>f.I.headInstanceCommand(f.context,f.view,'refresh-profiles',f.ops));
 await combo.selectOption('profile:'+a.id);await idle();await page.waitForFunction(()=>f.view.fiveHeadPreview?.phase==='ready');
 const before=await invoke('CamScene.ReadState'),headBefore=await page.evaluate(()=>structuredClone(f.head()));
 await combo.click();const popup=page.getByRole('dialog',{name:'选择切割头',exact:true});await popup.waitFor();
 assert.equal(await popup.locator('section[aria-label="系统"] .five-head-choice-delete').count(),0);assert.equal(await popup.locator('section[aria-label="我的"] .five-head-choice-delete').count(),2);
 await page.screenshot({path:resolve(output,'my-head-delete-buttons.png')});
 // Delete a nonselected row with a duplicate label. Hold its asynchronous list
 // readback, then continue scrolling/editing and change a conditional field.
 await page.evaluate(()=>{const invoke=f.context.sceneProxy.invoke;f.context.sceneProxy.invoke=async function(method,payload){if(method==='CamHead.Configurations'&&payload.action==='list'&&f.holdProfiles){f.heldProfiles=true;await new Promise(r=>f.releaseProfiles=r);f.holdProfiles=false;}return invoke.call(this,method,payload);};f.holdProfiles=true;});
 await popup.locator(`[data-five-key="head-choice-profile:${b.id}"] .five-head-choice-delete`).click();await page.waitForFunction(()=>f.heldProfiles);
 const interactionBefore=await page.evaluate(()=>{const input=document.querySelector('#right-draft'),left=document.querySelector('.left'),right=document.querySelector('.right');input.focus({preventScroll:true});input.setSelectionRange(1,5);window.kept={input,left,right,canvas:f.view.viewport.renderer.domElement,preview:f.view.fiveHeadPreview.canvas,events:0};input.addEventListener('probe',()=>kept.events++);left.scrollTop=110;right.scrollTop=150;document.querySelectorAll('.nested').forEach(n=>n.scrollTop=20);f.I.headInstanceField(f.context,f.view,'tcpMode','Gauge',f.ops);f.ops.renderProject();left.scrollTop=190;right.scrollTop=220;document.querySelectorAll('.nested').forEach(n=>n.scrollTop=35);return [left.scrollTop,right.scrollTop];});
 await page.evaluate(()=>f.releaseProfiles());await idle();
 const interaction=await page.evaluate(()=>{kept.input.dispatchEvent(new Event('probe'));return {input:kept.input===document.querySelector('#right-draft'),canvas:kept.canvas===f.view.viewport.renderer.domElement,preview:kept.preview===f.view.fiveHeadPreview.canvas,focus:document.activeElement===kept.input,selection:[kept.input.selectionStart,kept.input.selectionEnd],scroll:[kept.left.scrollTop,kept.right.scrollTop],nested:[...document.querySelectorAll('.nested')].map(n=>n.scrollTop),events:kept.events,conditional:!!document.querySelector('[data-five-head-instance-field="lengthMm"]')};});
 assert.deepEqual(interaction,{input:true,canvas:true,preview:true,focus:true,selection:[1,5],scroll:interactionBefore,nested:[35,35],events:1,conditional:true});report.interaction=interaction;
 assert.deepEqual((await invoke('CamHead.Configurations',{action:'list'})).items.map(e=>e.id),[a.id]);assert.equal(await page.evaluate(()=>f.head().instance.id),headBefore.instance.id,'Deleting another row must not select it');
 const afterOther=await invoke('CamScene.ReadState');assert.deepEqual(afterOther.headInstances,before.headInstances);report.cases.push('Per-row native deletion uses exact record ID even with duplicate names; other rows and current project head remain unchanged');
 await combo.press('ArrowDown');await popup.waitFor();assert.equal(await popup.locator(`[data-five-key="head-choice-profile:${b.id}"]`).count(),0);
 await popup.locator(`[data-five-key="head-choice-profile:${a.id}"] .five-head-choice-delete`).click();await idle();assert.equal((await invoke('CamHead.Configurations',{action:'list'})).items.length,0);
 const after=await invoke('CamScene.ReadState');assert.deepEqual(after.headInstances,before.headInstances);assert.deepEqual(await page.evaluate(()=>f.head().instance.parameters),headBefore.instance.parameters);
 await combo.press('ArrowDown');await popup.waitFor();assert.equal(await popup.locator('section[aria-label="我的"]').count(),0,'Deleting the selected library record cannot recreate the same row as an internal snapshot');
 await popup.press('Escape');assert.equal(await popup.count(),0);assert.equal(await combo.evaluate(e=>document.activeElement===e),true);
 const actions=page.locator('[data-five-key="head-instance-actions"]');await actions.locator('summary').click();assert.equal(await actions.getByRole('button',{name:'管理刀头库',exact:true}).count(),1);assert.equal(await actions.locator('button').count(),1);assert.equal(await page.locator('[data-five-command="five.head-instance.rename-open"],[data-five-command="five.head-instance.clone"],[data-five-command="five.head-instance.references"]').count(),0);
 await actions.locator('summary').click();await combo.press('ArrowDown');await popup.waitFor();await popup.locator(`[data-head-choice-value="builtin:${heads.find(h=>h.kind==='AC').id}"]`).click();await idle();assert.equal(await page.evaluate(()=>f.head().builtinDefinitionId),heads.find(h=>h.kind==='AC').id);
 report.cases.push('Selected library deletion preserves embedded project parameters and removes My row; system entries stay selectable through the enhanced dropdown; removed actions are absent; keyboard Escape restores focus');
 await page.screenshot({path:resolve(output,'head-picker-after-delete.png')});
 const obj=resolve(output,'temporary-head.obj');writeFileSync(obj,'v 0 0 0\nv 20 0 0\nv 0 30 40\nf 1 2 3\n');
 const rawBefore=await invoke('CamScene.ReadState');let task=await invoke('CamHead.Import',{commandId:crypto.randomUUID(),expected:rawBefore.expected,setupId:rawBefore.activeSetupId,sourcePath:obj});while(task.status==='Running'){await new Promise(r=>setTimeout(r,50));task=await invoke('CamHead.QueryTask',{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',task.error);
 await page.evaluate(()=>f.refresh());const rawId=await page.evaluate(()=>f.head().instance.id);await combo.click();await popup.waitFor();await popup.locator(`[data-five-key="head-choice-${rawId}"] .five-head-choice-delete`).click();await idle();
 const rawAfter=await invoke('CamScene.ReadState');assert.equal(rawAfter.headInstances.some(i=>i.id===rawId),false);assert.equal(!!rawAfter.setups.find(s=>s.id===rawAfter.activeSetupId).headModel?.instance,false);report.cases.push('An unsaved imported project head can also be deleted from My: only its current setup is detached, and the unused embedded instance is removed through native commands');
 report.passed=true;assert.deepEqual(errors,[]);
}catch(error){report.error=error.stack;if(page)await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}finally{
 writeFileSync(resolve(output,'report.json'),JSON.stringify({...report,errors},null,2));writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));await browser?.close();try{await rpc({action:'exit'});}catch{}child.stdin.end();child.kill();
}
console.log(JSON.stringify(report));
