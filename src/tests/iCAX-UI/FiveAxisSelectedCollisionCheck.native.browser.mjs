import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {copyFileSync,existsSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import {browserAssetPath,browserReportDirectory} from './browserPackageRuntime.mjs';
const output=browserReportDirectory('selected-collision-20261102');
const native=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||'src/x64/Debug');
const source=resolve(process.env.ICAX_SELECTED_CHECK_PROJECT||'output/acceptance/local-update-20261102/Pillar-现场本条检测.i5cam');
const project=resolve(output,'Pillar-test-copy.i5cam');copyFileSync(source,project);if(existsSync(source+'.log'))copyFileSync(source+'.log',project+'.log');
const child=spawn(resolve(native,'FiveAxisCAMTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:native,windowsHide:true,stdio:['pipe','pipe','pipe']});
let serial=0,browser,page,stage='startup',exited=false;const waiting=new Map(),calls=[],errors=[],log=[];
const report={passed:false,source,project,cases:[],coverage:{native:true,headlessBrowser:true,standaloneCef:false,originalProjectModified:false}};
child.stderr.on('data',b=>log.push(String(b)));
child.stdin.on('error',()=>{}); // The exit handler rejects outstanding RPCs.
child.on('exit',code=>{exited=true;for(const p of waiting.values()){clearTimeout(p.timer);p.reject(Error('Native exited '+code));}waiting.clear();});
createInterface({input:child.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__')){log.push(line);return;}const r=JSON.parse(line.slice(12)),p=waiting.get(r.id);if(!p)return;waiting.delete(r.id);clearTimeout(p.timer);r.ok?p.resolve(r.result):p.reject(Error(r.error));});
const rpc=m=>new Promise((resolve,reject)=>{if(exited)return reject(Error('Native bridge exited'));const id=++serial;calls.push({id,...m});const timer=setTimeout(()=>{waiting.delete(id);reject(Error('Timeout '+m.action+' '+(m.method||'')));},240000);waiting.set(id,{resolve,reject,timer});child.stdin.write(JSON.stringify({id,...m,...(m.action==='invoke'?{payload:undefined,payloadText:serializeVariantText(m.payload||{})}:{})})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
const task=async(method,payload)=>{let t=await invoke(method,payload),lastLog=Date.now();while(t.status==='Running'){await new Promise(r=>setTimeout(r,100));t=await invoke(method==='CamPlan.Generate'?'CamPlan.QueryStatus':'CamSimulation.QueryTask',{epoch:t.epoch,taskId:t.taskId});if(Date.now()-lastLog>15000){console.log(JSON.stringify({stage,progress:t.collisionProgress??t.progress,status:t.status}));lastLog=Date.now();}}assert.equal(t.status,'Completed',JSON.stringify(t));return t;};
const progress=setInterval(()=>console.log(JSON.stringify({stage,last:calls.at(-1)?.method||calls.at(-1)?.action})),15000);
try{
 const {chromium}=await import('playwright');browser=await chromium.launch({channel:'msedge',headless:true});page=await browser.newPage({viewport:{width:1440,height:900}});page.setDefaultTimeout(120000);page.on('pageerror',e=>errors.push(e.message));
 await page.exposeFunction('nativeRpc',rpc);
 await page.route('**/*',route=>{const path=decodeURIComponent(new URL(route.request().url()).pathname);if(path==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><html lang="zh-CN"><meta charset="utf-8"><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"><body style="margin:0"><div id="app" style="height:100vh"></div></body></html>'});if(!path.startsWith('/src/'))return route.abort();try{return route.fulfill({contentType:path.endsWith('.css')?'text/css':'text/javascript',body:readFileSync(browserAssetPath(path.slice(5)))});}catch{return route.abort();}});
 await page.goto('http://localhost/');
 const mount=async info=>page.evaluate(async info=>{
  const entry=await import('/src/apps/five-axis-cam/webpage/entry.mjs'),{ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs'),{PDOStore}=await import('/src/iCAX-UI/SDK/PDO/pdoStore.mjs'),{getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
  const view=getProjectView(info.projectId),f=window.fixture={entry,view,calls:[],hold:false,held:false};
  const sceneProxy={sceneId:info.sceneId,state:{sceneId:info.sceneId},pdoStore:new PDOStore(),subscribe(){return()=>{};},pdo:{enabled:true,async withReadDescriptor(d,reader){const r=await nativeRpc({action:'pdo-read',slotId:String(d.id),arenaName:d.arenaName});return reader(Uint8Array.from(r.bytes).buffer,{dataVersion:r.dataVersion});}},async invoke(method,payload={}){const call={method,payload:structuredClone(payload)};f.calls.push(call);const result=await nativeRpc({action:'invoke',scope:'scene',method,payload});if(f.hold&&method===f.holdMethod){f.hold=false;f.held=true;await new Promise(r=>f.release=r);f.held=false;}return result;},async getState(){return this.state;}};
  const resource=async(url,options,head)=>{const version=new Headers(options?.headers).get('ICAX-Resource-Version')??String(options?.version??'');const r=await nativeRpc({action:'resource',url,version,head,encoding:'base64'});return new Response(head?null:Uint8Array.from(atob(r.base64),c=>c.charCodeAt(0)),{headers:{'Content-Type':'application/vnd.icax.flatbuffer','ICAX-Resource-Version':r.version}});};
  sceneProxy.resources={get:(url,opts)=>resource(url,opts,false),head:(url,opts)=>resource(url,opts,true)};sceneProxy.views=new ViewClient(sceneProxy);
  const context=f.context={mount:document.querySelector('#app'),sceneProxy,project:{projectId:info.projectId,projectName:'Pillar 单条碰撞验证'},activeRibbonTabId:'workpiece',productProxy:{invoke:(method,payload={})=>nativeRpc({action:'invoke',scope:'product',method,payload})},isCurrentProject:()=>true,actions:{refreshProductRibbon(){},appendProjectLog(){},async selectRibbonTab(id){context.activeRibbonTabId=id;entry.mountProject(context);}}};entry.mountProject(context);
 },info);
 const idle=()=>page.waitForFunction(()=>fixture.view.scene?.domain&&!fixture.view.pending&&!fixture.view.fiveAxisPointEdit?.loading&&!fixture.view.fiveAxisPointEdit?.request&&!fixture.view.fiveAxisPointSaving&&!fixture.view.fiveToolAxisDisplay?.loading);
 const command=async id=>{await page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
 await mount(await rpc({action:'info'}));await idle();assert.deepEqual(errors,[]);report.cases.push('Production workbench starts with the native empty scene');
 await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
 stage='open real project';await rpc({action:'open-project',path:project,replayJournal:true});let state=await rpc({action:'read-state'}),setup=state.setups.find(s=>s.id===state.activeSetupId);
 report.initial={cuts:setup.generatedGeometry.operations.length,geometry:setup.generatedGeometry.status,collision:setup.simulationCheck.validity};assert.equal(report.initial.cuts,21);console.log(JSON.stringify(report.initial));
 // Establish a checked baseline only inside this disposable project copy.
 if(setup.generatedGeometry.status!=='Current'){stage='prepare baseline cuts';await task('CamPlan.Generate',{commandId:crypto.randomUUID(),expected:state.expected,setupId:setup.id});state=await rpc({action:'read-state'});setup=state.setups.find(s=>s.id===state.activeSetupId);}
 if(setup.simulationCheck.validity!=='Current'){stage='baseline check of unchanged cut 19';const unchanged=setup.generatedGeometry.operations.find(o=>o.order==='19');assert(unchanged);await task('CamSimulation.CheckCollision',{commandId:crypto.randomUUID(),expected:state.expected,setupId:setup.id,intentId:unchanged.intentId});}
 if(process.env.ICAX_EXPORT_CHECKED_BASELINE==='1'){report.baselineProject=resolve(output,'Pillar-before-edit.i5cam');await rpc({action:'export-project',path:report.baselineProject});}
 await mount(await rpc({action:'info'}));await idle();assert.deepEqual(errors,[]);report.cases.push('Production workbench opens the 21-cut Pillar project');
 const initial=await page.evaluate(()=>{const d=fixture.view.scene.domain,s=d.setups.find(s=>s.id===d.activeSetupId);return {state:structuredClone(d),setupId:s.id,id:s.generatedGeometry.operations.find(o=>o.order==='18').intentId};});
 writeFileSync(resolve(output,'before.json'),JSON.stringify(initial));
 assert.equal(initial.state.setups.find(s=>s.id===initial.setupId).simulationCheck.validity,'Current');
 await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await command('five.cam-page.cutting');await page.locator(`[data-five-intent="${initial.id}"]`).click();await idle();await command('five.cam-inspector.points');
 const field=page.getByRole('textbox',{name:'关键点 欧拉角 X',exact:true});await field.waitFor();const begin=await page.evaluate(()=>fixture.calls.length);
 stage='manual endpoint edits';
 await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{height:400px!important;overflow:auto!important}.cam-context-pane::after,.cam-info-pane::after,.five-axis-keypoints::after{content:"";display:block;min-height:1600px;flex:0 0 1600px}.five-axis-keypoints{max-height:300px;overflow:auto}'});
 await page.evaluate(()=>{fixture.nodes={input:document.querySelector('[aria-label="关键点 欧拉角 X"]'),canvas:fixture.view.viewport.renderer.domElement};fixture.events=0;fixture.nodes.input.addEventListener('probe',()=>fixture.events++);});
 for(const endpoint of ['start','end']){
  await page.locator('[data-five-point-field="point"]').selectOption(endpoint);await idle();
  await page.evaluate(()=>{fixture.hold=true;fixture.holdMethod='CamCut.EditToolAxisPoint';});
  await field.fill(String(Number(await field.inputValue())+.2));await field.press('Enter');await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const n=fixture.nodes.input;n.focus({preventScroll:true});n.setSelectionRange(0,2);fixture.selection=[n.selectionStart,n.selectionEnd];return ['.cam-context-pane','.cam-info-pane','.five-axis-keypoints'].map((s,i)=>{const p=document.querySelector(s);p.scrollTop=100+i*40;return p.scrollTop;});});assert(scroll.every(n=>n>0));
  await page.evaluate(()=>fixture.release());await idle();
  const interaction=await page.evaluate(()=>{fixture.nodes.input.dispatchEvent(new Event('probe'));return {input:fixture.nodes.input===document.querySelector('[aria-label="关键点 欧拉角 X"]'),canvas:fixture.nodes.canvas===fixture.view.viewport.renderer.domElement,focus:document.activeElement===fixture.nodes.input,selection:[fixture.nodes.input.selectionStart,fixture.nodes.input.selectionEnd],expectedSelection:fixture.selection,scroll:['.cam-context-pane','.cam-info-pane','.five-axis-keypoints'].map(s=>document.querySelector(s).scrollTop),error:fixture.view.fiveAxisPointEdit.error};});
  assert.equal(interaction.input,true);assert.equal(interaction.canvas,true);assert.equal(interaction.focus,true);assert.deepEqual(interaction.selection,interaction.expectedSelection);assert.deepEqual(interaction.scroll,scroll);assert.equal(interaction.error,'');report.interaction=interaction;
 }
 await page.waitForTimeout(1100);
 const calculations=()=>page.evaluate(begin=>fixture.calls.slice(begin).filter(c=>/\.Generate$|CamSimulation\.(CheckCollision|AutoRepairCollision)$/.test(c.method)),begin);
 assert.deepEqual(await calculations(),[]);await command('five.cam-inspector.collision');assert.deepEqual(await calculations(),[]);assert.equal(await page.getByText('刀路已过期，请重新生成。',{exact:true}).count(),0);
 report.cases.push('Both endpoint edits preserve nodes, focus, selection and latest sidebar/nested scroll; editing and opening collision starts no calculation');
 stage='explicit selected check';await page.locator('[data-five-command="five.simulation.collision-check-path"]').click();await page.waitForFunction(()=>!fixture.view.fiveSimulation.checking,undefined,{timeout:240000});await idle();
 const after=await page.evaluate(()=>{const v=fixture.view;return {state:structuredClone(v.scene.domain),message:v.fiveSimulation.message,preparation:v.fiveSimulation.selectedPreparation,calls:fixture.calls};});
 writeFileSync(resolve(output,'after.json'),JSON.stringify(after));
 assert.equal(after.message,'');const beforeSetup=initial.state.setups.find(s=>s.id===initial.setupId),afterSetup=after.state.setups.find(s=>s.id===initial.setupId),r=afterSetup.simulationCheck;
 assert.equal(r.validity,'Current');assert.equal(r.checkMode,'SelectedPath');assert.equal(r.checkedIntentId,initial.id);assert.equal(Number(after.preparation.calculatedCount),1);assert.equal(Number(after.preparation.reusedCount),20);
 const generated=after.calls.slice(begin).filter(c=>c.method==='CamPlan.Generate'),checked=after.calls.slice(begin).filter(c=>c.method==='CamSimulation.CheckCollision');assert.equal(generated.length,1);assert.equal(generated[0].payload.intentId,initial.id);assert.equal(checked.length,1);assert.equal(checked[0].payload.intentId,initial.id);assert(!(await calculations()).some(c=>c.method==='CamRapid.Generate'||c.method==='CamSimulation.AutoRepairCollision'));
 for(const op of beforeSetup.generatedGeometry.operations.filter(o=>o.intentId!==initial.id)){const current=afterSetup.generatedGeometry.operations.find(o=>o.intentId===op.intentId);assert.deepEqual(current.parts,op.parts);assert.deepEqual(current.curveRevision,op.curveRevision);}
 // Adjacent input signatures may track the edited endpoints; their stored
 // rapid geometry must remain untouched by a selected cut check.
 assert.equal(afterSetup.generatedGeometry.transitions.length,beforeSetup.generatedGeometry.transitions.length);
 for(const transition of beforeSetup.generatedGeometry.transitions){const current=afterSetup.generatedGeometry.transitions.find(t=>t.from===transition.from&&t.to===transition.to);assert(current);const {inputKey:oldKey,...beforeRapid}=transition,{inputKey:newKey,...afterRapid}=current;assert.deepEqual(afterRapid,beforeRapid);}
 report.unchangedRapidGeometries=beforeSetup.generatedGeometry.transitions.length;
 for(const key of ['workpieces','placements','fixtures','fixtureUnits'])assert.deepEqual(after.state[key],initial.state[key],key);
 const beforeReport=beforeSetup.simulationCheck,unrelated=[];
 const ownerRecords=report=>{const owners=new Map();for(const segment of report.segments){const i=Number(segment.index),a=report.frames[i],b=report.frames[i+1];if(a?.role==='Rapid'||b?.role==='Rapid'||a?.intentId!==b?.intentId||a?.intentId===initial.id)continue;const {index,...result}=segment;let rows=owners.get(a.intentId);if(!rows)owners.set(a.intentId,rows=[]);rows.push({a,b,result});}return owners;};
 const currentRecords=ownerRecords(r);for(const [id,rows] of ownerRecords(beforeReport)){assert.deepEqual(currentRecords.get(id),rows,'Unrelated path frames and collision records '+id);unrelated.push(...rows);}
 const verified=unrelated.filter(row=>row.result.status!=='Unchecked');assert(verified.length>0,'Preserve actual checked records of the unrelated cut, not just pending placeholders');report.check={checked:r.checkedSegmentCount,reused:r.reusedSegmentCount,total:r.segments.length,unchangedCutRecords:unrelated.length,unchangedVerifiedCutRecords:verified.length};report.preparation=after.preparation;report.cases.push('Explicit recheck prepares only one cut, preserves 20 other cut geometries and their collision records (including checked cut 19), and generates no rapid or repair');
 await page.screenshot({path:resolve(output,'selected-check.png')});assert.deepEqual(errors,[]);report.passed=true;console.log(JSON.stringify({passed:true,check:report.check}));
}catch(e){report.stage=stage;report.error=e.stack;if(page){report.ui=await page.evaluate(()=>({error:window.fixture?.view.error,text:document.body.innerText.slice(-2500)})).catch(()=>null);await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}console.error(e);process.exitCode=1;}
finally{clearInterval(progress);writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));writeFileSync(resolve(output,'native.log'),log.join('\n'));writeFileSync(resolve(output,'calls.json'),JSON.stringify(calls.filter(c=>!['resource','pdo-read'].includes(c.action)),null,2));await browser?.close();if(!exited)await rpc({action:'exit'}).catch(()=>{});child.stdin.end();}
