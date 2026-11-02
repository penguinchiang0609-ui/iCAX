import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import {verifyHeadColliders} from './FiveAxisHeadColliders.native.browserCase.mjs';
import {verifyHeadChoiceImport} from './FiveAxisHeadChoiceImport.native.browserCase.mjs';
import {serveBrowserAsset,browserAssetPath} from './browserPackageRuntime.mjs';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/head-unified-20261101/browser');mkdirSync(output,{recursive:true});
const runtime=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||'output/run/five-axis-cam-unified-head-20261101');
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
 browser=await chromium.launch({channel:'chrome',headless:true,args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
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
 const field=key=>key==='instanceId'?page.locator('[data-five-simulation-field="head-choice"]'):page.locator(`[data-five-head-instance-field="${key}"]`),button=key=>page.locator(`[data-five-command="five.head-instance.${key}"]`).last();
 const open=async key=>{if(key==='head-configuration-library'){if(!await page.locator('[data-five-key="head-configuration-library"]').count()){const menu=page.locator('[data-five-key="head-instance-actions"]');if(!await menu.evaluate(e=>e.open))await menu.locator('summary').click();await button('library-open').click();}return;}const el=page.locator(`[data-five-key="${key}"]`);if(!await el.evaluate(e=>e.open))await el.locator(':scope > summary').click();};
 const chooseInstance=async id=>page.evaluate(id=>f.I.headInstanceField(f.context,f.view,'instanceId',id,f.ops),id);
 const idle=()=>page.waitForFunction(()=>!f.view.pending&&!f.view.fiveHeadEditor?.busy&&!f.view.fiveSimulation.busy&&!f.view.fiveSimulation.headChoiceBusy);
 const physicalReady=()=>page.waitForFunction(()=>f.view.fiveHeadPreview?.phase==='ready',null,{timeout:60000});
 await field('instanceId').selectOption('builtin:'+heads.find(h=>h.kind==='AB').id);await idle();await physicalReady();
 if(process.env.ICAX_HEAD_COLLIDER_ONLY){await verifyHeadColliders({page,invoke,rpc,report,output,heads});report.project=resolve(output,'碰撞盒编辑验证.i5cam');await rpc({action:'export-project',path:report.project});assert.deepEqual(errors,[]);report.passed=true;}else{
 assert.equal(await field('name').count(),0);assert.equal(await page.locator('[role=tablist]').count(),0);
 const selection=page.locator('[data-five-key="head-selection"]'),builtin=field('instanceId');
 assert.equal(await selection.locator('select').count(),1);assert.equal(await builtin.isVisible(),true);assert.equal(await builtin.locator('option').last().getAttribute('value'),'import');assert.equal(await page.locator('[data-five-command="five.simulation.head-import"]').count(),0);
 assert.equal(await page.locator('[data-five-key="head-motion"]').evaluate(e=>e.open),false);
 const selectionBox=await selection.boundingBox(),previewBox=await page.locator('[data-five-key="head-preview"]').boundingBox();assert.ok(selectionBox.y+selectionBox.height<=previewBox.y);
 assert.equal(await field('tcp.0').isVisible(),true);await page.screenshot({path:resolve(output,'打开即可换头.png')});
 const menu=async()=>{const e=page.locator('[data-five-key="head-instance-actions"]');if(!await e.evaluate(e=>e.open))await e.locator('summary').click();};
 const rename=async name=>{await menu();await button('rename-open').click();await field('name').fill(name);};
 const original=await invoke('CamScene.ReadState'),first=original.setups.find(s=>s.id===original.activeSetupId).headInstanceId;
 await menu();await button('rename-open').click();await field('name').fill('取消的名称');await button('rename-cancel').click();assert.equal(await field('name').count(),0);assert.deepEqual(await invoke('CamScene.ReadState'),original);
 assert.equal(await page.locator('[data-five-key="head-model-source"]').count(),0);
 await open('head-configuration-library');await button('refresh-profiles').click();await page.waitForFunction(()=>f.view.fiveHeadEditor.libraryLoaded);
 assert.equal(await field('profileName').isVisible(),false);assert.equal(await page.locator('[data-five-key="head-effective-parameters"]').count(),0);
 assert.equal(await field('tcp.0').isVisible(),true);assert.equal(await page.locator('.five-head-settings').evaluate(e=>e.scrollWidth<=e.clientWidth+1),true);
 checkpoint('One cutting-head combobox above preview lists available heads and ends with Import; rotary setup is folded separately');
 await page.evaluate(()=>{window.previewModel=f.view.fiveHeadPreview.model;window.previewCanvas=f.view.fiveHeadPreview.canvas;window.previewCamera=f.view.fiveHeadPreview.camera.position.toArray();});
 await rename('客户甲 AB');await button('rename').click();await idle();
 assert.equal(await page.evaluate(()=>f.head().instance.name),'客户甲 AB');assert.equal(await page.evaluate(()=>previewModel===f.view.fiveHeadPreview.model&&previewCanvas===f.view.fiveHeadPreview.canvas),true);
 assert.deepEqual(await page.evaluate(()=>f.view.fiveHeadPreview.camera.position.toArray()),await page.evaluate(()=>previewCamera));
 await menu();await button('clone').click();await idle();await physicalReady();const second=await page.evaluate(()=>f.head().instance.id);assert.notEqual(second,first);
 await button('library-close').click();await field('tcp.0').fill('12');await chooseInstance(first);await idle();assert.equal(await field('tcp.0').inputValue(),String(original.headInstances.find(i=>i.id===first).parameters.tcp[0]));await chooseInstance(second);await idle();assert.equal(await field('tcp.0').inputValue(),'12');
 report.cases.push('Instance rename retains physical preview; clone owns independent parameters and switching retains unapplied drafts');
 // Calibrate from an explicit installation datum and gauge length; untouched
 // origin matrices must stay equal (JSON normalizes equivalent signed zero).
 const untouched=await page.evaluate(()=>f.head().instance.parameters.axes.map(a=>a.origin));
 await field('tcpMode').selectOption('Gauge');await field('gaugeOrigin.0').fill('1');await field('gaugeOrigin.1').fill('2');await field('gaugeOrigin.2').fill('3');await field('lengthMm').fill('225');
 await page.evaluate(()=>f.holdRead=true);await button('apply').click();await page.waitForFunction(()=>f.held);
 // Continue editing while the earlier commit is awaiting its native refresh.
 await field('lengthMm').fill('226');
 const interactionBefore=await page.evaluate(()=>{const input=document.querySelector('[data-five-head-instance-field="lengthMm"]'),left=document.querySelector('.left'),right=document.querySelector('.right'),settings=document.querySelector('.five-head-settings');input.focus({preventScroll:true});input.setSelectionRange(0,2);window.kept={input,left,right,settings,canvas:f.view.viewport.renderer.domElement,preview:f.view.fiveHeadPreview.canvas,events:0};input.addEventListener('probe',()=>kept.events++);left.scrollTop=140;right.scrollTop=180;settings.scrollTop=40;document.querySelectorAll('.nested').forEach(n=>n.scrollTop=35);left.scrollTop=190;right.scrollTop=220;return [left.scrollTop,right.scrollTop,settings.scrollTop];});
 await page.evaluate(()=>f.release());await idle();await physicalReady();
 const interaction=await page.evaluate(()=>{kept.input.dispatchEvent(new Event('probe'));return {input:kept.input===document.querySelector('[data-five-head-instance-field="lengthMm"]'),canvas:kept.canvas===f.view.viewport.renderer.domElement,preview:kept.preview===f.view.fiveHeadPreview.canvas,focus:document.activeElement===kept.input,selection:[kept.input.selectionStart,kept.input.selectionEnd],scroll:[kept.left.scrollTop,kept.right.scrollTop,kept.settings.scrollTop],nested:[...document.querySelectorAll('.nested')].map(n=>n.scrollTop),events:kept.events,draft:kept.input.value,dirty:f.view.fiveHeadEditor.dirty};});
 assert.deepEqual(interaction,{input:true,canvas:true,preview:true,focus:true,selection:[0,2],scroll:interactionBefore,nested:[35,35],events:1,draft:'226',dirty:true});report.interaction=interaction;
 assert.deepEqual(await page.evaluate(()=>f.head().tcpLocalPosition),[1,2,-222]);assert.equal(JSON.stringify(await page.evaluate(()=>f.head().instance.parameters.axes.map(a=>a.origin))),JSON.stringify(untouched));
 await button('apply').click();await idle();assert.deepEqual(await page.evaluate(()=>f.head().tcpLocalPosition),[1,2,-223]);assert.equal(await page.evaluate(()=>f.view.fiveHeadEditor.dirty),false);
 report.cases.push('Explicit datum plus gauge length reaches effective TCP; newer in-flight draft, focus, selection, event listeners, both panes, nested scrolling and canvas survive');
 // Change the mode so conditional field replacement is exercised in the real DOM.
 await field('tcpMode').selectOption('Coordinates');assert.equal(await field('lengthMm').count(),0);assert.equal(await field('tcp.0').count(),1);await field('tcpMode').selectOption('Gauge');assert.equal(await field('lengthMm').inputValue(),'226');
 await open('head-motion');await button('axis:0').click();await open('head-axis-advanced-'+await page.evaluate(()=>f.view.fiveHeadEditor.draft.axes[0].index));await field('axes.0.zeroOffsetDeg').fill('7');await field('axes.0.sign').selectOption('-1');await button('apply').click();await idle();
 assert.equal(await page.evaluate(()=>f.head().instance.parameters.axes[0].zeroOffsetDeg),7);assert.equal(await page.evaluate(()=>f.head().instance.parameters.axes[0].sign),-1);
 await open('head-configuration-library');await button('refresh-profiles').click();await page.waitForFunction(()=>f.view.fiveHeadEditor.libraryLoaded);await page.locator('[data-five-key="head-save-configuration"] > summary').click();await field('profileName').fill('客户甲 AB 标定');await button('save-profile').click();await idle();
 const entries=await invoke('CamHead.Configurations',{action:'list'}),entry=entries.items.find(e=>e.name==='客户甲 AB 标定');assert.ok(entry);await field('entryId').selectOption(entry.id);await button('use-profile').click();await idle();await physicalReady();
 const loaded=await page.evaluate(()=>f.head().instance.id);assert.notEqual(loaded,second);assert.deepEqual(await page.evaluate(()=>f.head().tcpLocalPosition),[1,2,-223]);
 await field('entryId').selectOption(entry.id);await button('delete-profile').click();await idle();assert.equal((await invoke('CamHead.Configurations',{action:'list'})).items.length,0);assert.deepEqual(await page.evaluate(()=>f.head().tcpLocalPosition),[1,2,-223]);
 await button('library-close').click();report.cases.push('Profile save, load as an independent embedded instance and library deletion preserve project parameters');
 // A missing response must retry the original receipt, then honor the latest
 // queued selection without accidentally duplicating the operation.
 await rename('回执重试');await page.evaluate(()=>f.loseReceipt=true);await button('rename').click();await idle();assert.match(await page.locator('[role="alert"]').innerText(),/acknowledgement lost/);
 await chooseInstance(first);await chooseInstance(second);await button('retry').click();await idle();assert.equal(await page.evaluate(()=>f.head().instance.id),second);
 const afterRetry=await invoke('CamScene.ReadState');assert.equal(afterRetry.headInstances.find(i=>i.id===loaded).name,'回执重试');assert.equal(afterRetry.headInstances.length,3);
 for(const key of ['workpieces','placements','intentToolpaths','fixtures','fixtureUnits'])assert.deepEqual(afterRetry[key],original[key],key+' changed during head edits');
 assert.equal(await field('structure').count(),0);assert.equal(await page.locator('[data-five-head-instance-field$=".target"]').count(),0);
 assert.equal(await page.evaluate(()=>f.view.fiveHeadPreview.workpiece),null);
 await page.evaluate(()=>{window.motionKept={canvas:f.view.fiveHeadPreview.canvas,model:f.view.fiveHeadPreview.model,camera:f.view.fiveHeadPreview.camera.position.toArray(),head:f.view.fiveHeadPreview.model.children.map(o=>o.matrix.toArray())};});
 const sliders=page.locator('[data-five-head-joint]');assert.equal(await sliders.count(),2);
 for(const slider of await sliders.all())assert.equal(await slider.isVisible(),true);
 const slider=sliders.first();await slider.fill('35');await slider.dispatchEvent('input');
 await sliders.nth(1).fill('20');await sliders.nth(1).dispatchEvent('input');
 await page.evaluate(()=>{window.jointRows=[...document.querySelectorAll('[data-five-head-joint]')];jointRows[1].focus({preventScroll:true});f.ops.renderProject();});
 assert.deepEqual(await sliders.evaluateAll(rows=>rows.map((r,i)=>({same:r===jointRows[i],value:r.value,focused:r===document.activeElement}))),[{same:true,value:'35',focused:false},{same:true,value:'20',focused:true}]);
 assert.notDeepEqual(await page.evaluate(()=>f.view.fiveHeadPreview.model.children.map(o=>o.matrix.toArray())),await page.evaluate(()=>motionKept.head));
 assert.deepEqual(await page.evaluate(()=>f.view.fiveHeadPreview.camera.position.toArray()),await page.evaluate(()=>motionKept.camera));
 assert.equal(await page.evaluate(()=>motionKept.canvas===f.view.fiveHeadPreview.canvas&&motionKept.model===f.view.fiveHeadPreview.model),true);
 report.cases.push('Head dialog shows head joints only, with no machine layout, workpiece axes or workpiece geometry; joint preview preserves canvas, loaded head and camera');
 // Use only the always-visible controls to change model and return to an
 // existing head. Verify native state and the production 3D preview together.
 for(const kind of ['A','Fixed']){
  const chosen=heads.find(h=>h.kind===kind);assert.ok(chosen);await builtin.selectOption('builtin:'+chosen.id);await idle();await physicalReady();
  assert.equal(await page.evaluate(()=>f.head().instance.parameters.axes.every(a=>a.target==='Head')),true);
  assert.equal(await page.evaluate(()=>f.head().builtinDefinitionId),chosen.id,JSON.stringify(await page.evaluate(()=>({error:f.view.fiveSimulation.headError,request:f.view.fiveSimulation.request,queued:f.view.fiveSimulation.queuedHeadChoice}))));
  assert.equal(await page.evaluate(()=>f.view.fiveHeadPreview.head.builtinDefinitionId),chosen.id);
  const saved=await invoke('CamScene.ReadState');assert.equal(saved.setups.find(s=>s.id===saved.activeSetupId).headModel.builtinDefinitionId,chosen.id);
  await chooseInstance(second);await idle();await physicalReady();assert.equal(await page.evaluate(()=>f.head().instance.id),second);
 }
 report.cases.push('Top controls switch AB to single-axis and fixed heads and back to an existing head; native active head and loaded 3D preview agree');
 if(process.env.ICAX_HEAD_COLLIDER_TESTS){await verifyHeadColliders({page,invoke,rpc,report,output,heads});await chooseInstance(second);await idle();await physicalReady();}
 await verifyHeadChoiceImport({page,invoke,rpc,report,output});
 await chooseInstance(second);await idle();await physicalReady();
 const currentState=await invoke('CamScene.ReadState');const payload={commandId:crypto.randomUUID(),expected:currentState.expected,placementId:currentState.placements[0].id,action:'create',name:'共用刀头的位姿'};await invoke('CamSetup.Manage',payload);await page.evaluate(()=>f.refresh());
 const shared=await invoke('CamScene.ReadState');assert.equal(shared.setups.filter(s=>s.headInstanceId===second).length,2);
 const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.headInstances,shared.headInstances);for(const key of ['workpieces','placements','intentToolpaths','fixtures','fixtureUnits'])assert.deepEqual(reopened[key],shared[key],key+' changed on reopen');
 report.project=resolve(output,'刀头实例验证.i5cam');await rpc({action:'export-project',path:report.project});
 report.cases.push('Unknown receipt retry is idempotent, queued selection uses the latest request, setups share references, and embedded instances survive project reopen with CAD and toolpaths unchanged');
 await page.evaluate(()=>f.refresh());
 await page.evaluate(()=>{document.querySelector('.left').scrollTop=0;document.querySelector('.right').scrollTop=0;document.querySelector('.five-head-settings').scrollTop=0;});await page.screenshot({path:resolve(output,'刀头实例与配置.png')});
 assert.deepEqual(errors,[]);report.passed=true;
}
}catch(error){report.error=error.stack;if(page)await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}finally{
 writeFileSync(resolve(output,'report.json'),JSON.stringify({...report,errors},null,2));writeFileSync(resolve(output,'native.log'),nativeLog.join('\n'));await browser?.close();try{await rpc({action:'exit'});}catch{}child.stdin.end();child.kill();
}
console.log(JSON.stringify(report));
