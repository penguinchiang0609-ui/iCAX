import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import {serveBrowserAsset,browserReportDirectory} from './browserPackageRuntime.mjs';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';

const output=browserReportDirectory('machine-assembly-placement'),nativeDirectory=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||'src/x64/Debug');
const child=spawn(resolve(nativeDirectory,'FiveAxisCAMTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:nativeDirectory,windowsHide:true,env:{...process.env,ICAX_FIVE_AXIS_BRIDGE_BOX:'1'}});
const pending=new Map(),calls=[],errors=[];let serial=0,browser;
createInterface({input:child.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__'))return;const r=JSON.parse(line.slice(12)),p=pending.get(r.id);if(!p)return;pending.delete(r.id);clearTimeout(p.timer);r.ok?p.resolve(r.result):p.reject(Error(r.error));});
child.stderr.on('data',data=>errors.push(String(data)));
const rpc=message=>new Promise((resolve,reject)=>{const id=++serial,timer=setTimeout(()=>{pending.delete(id);reject(Error('Native timeout '+message.action));},60000);pending.set(id,{resolve,reject,timer});calls.push({id,...message});child.stdin.write(JSON.stringify({id,...message,...(message.action==='invoke'?{payload:undefined,payloadText:serializeVariantText(message.payload||{})}:{})})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
const report={passed:false,transport:'real native SDO and production browser modules with WebGL',standaloneCef:false,cases:[]};
try{
 await rpc({action:'prepare-machine-placement-test'});
 const domain=await invoke('CamScene.ReadState'),machines=(await invoke('Machine.List')).machines;
 const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');browser=await chromium.launch({channel:'msedge',headless:true,args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 const page=await browser.newPage({viewport:{width:1280,height:900}});page.on('pageerror',e=>errors.push(e.message));
 await page.route('http://127.0.0.1:49199/**',serveBrowserAsset);await page.exposeFunction('nativeAssemblyRpc',rpc);await page.goto('http://127.0.0.1:49199/');
 await page.evaluate(async({domain,machines})=>{
  const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),A=await import('/src/apps/five-axis-cam/webpage/machineAssemblyPlacement.mjs');
  const {createThreeViewport}=await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs'),{selectTool}=await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs'),{patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs');
  document.body.style='margin:0;background:#13252d;color:white';document.body.innerHTML='<main class="five-workspace" style="display:grid;grid-template-columns:180px 840px 260px;height:900px"><aside class="left" style="height:700px;overflow:auto"><input id="left-input" value="draft"><div style="height:1800px">装夹组合</div></aside><div id="viewport" style="width:840px;height:800px"></div><aside class="right" style="height:700px;overflow:auto"></aside></main>';
  const vp=createThreeViewport({continuousRender:false,showGrid:true,showWorldAxes:true}).mount(document.querySelector('#viewport'));
  const view={fiveAxisArea:'simulation',scene:{domain,machines},viewport:vp,fiveMachine:{error:'',dirty:false,originals:new Map()}},context={mount:document.body,isCurrentProject:()=>true};selectTool(view,'machine-simulation');
  const matrix=values=>new THREE.Matrix4().set(...values),row=m=>m.clone().transpose().toArray();let saves=0,hold=false,release,checks=0,commits=0;
  context.sceneProxy={async invoke(method,payload){if(method==='CamSimulation.CheckMachinePlacement')checks++;if(method==='CamSimulation.SetMachinePlacement'){commits++;if(hold){hold=false;await new Promise(r=>release=r);release=null;}}const result=await nativeAssemblyRpc({action:'invoke',scope:'scene',method,payload});if(method==='CamSimulation.SetMachinePlacement')await project();return result;}};
  context.projectProxy={state:{projectPath:'C:/test/assembly.i5cam'},async autosave(){saves++;return {};}};
  const setup=()=>view.scene.domain.setups.find(s=>s.id===view.scene.domain.activeSetupId);
  const geometry=(size,center)=>new THREE.BoxGeometry(...size).translate(...center);
  const add=(id,g,color)=>{const object=new THREE.Mesh(g,new THREE.MeshBasicMaterial({color,transparent:true,opacity:.8}));object.matrixAutoUpdate=false;vp.sceneObjects.set(id,object);vp.content.add(object);return object;};
  add(domain.placements[0].id,geometry([40,60,20],[20,30,10]),0x33aaff);add(domain.fixtures[0].id,geometry([40,160,20],[0,0,-20]),0xf3ac42);
  add(setup().machineSetup.supportId,geometry([1000,1000,1000],[0,0,-495]),0x657e85);
  const saved=new Map(),actualPreview=vp.setTransformPreview.bind(vp),actualClear=vp.clearTransformPreview.bind(vp);
  // The native fixture supplies BRep dimensions and authoritative transforms;
  // the regular viewport owns the preview map and real pointer manipulator.
  vp.setTransformPreview=(id,e)=>{actualPreview(id,e);const o=vp.sceneObjects.get(id);if(o){o.matrix.fromArray(e);o.matrixWorldNeedsUpdate=true;}vp.scene.updateMatrixWorld(true);};
  vp.clearTransformPreview=id=>{actualClear(id);const o=vp.sceneObjects.get(id);if(o&&saved.has(id)){o.matrix.copy(saved.get(id));o.matrixWorldNeedsUpdate=true;}vp.scene.updateMatrixWorld(true);};
  const project=async()=>{const ids=[setup().machineSetup.supportId,...A.machineAssemblyParts(view).map(p=>p.id)],r=await nativeAssemblyRpc({action:'entity-transforms',entityIds:ids});for(const t of r.transforms){const o=vp.sceneObjects.get(t.id);o.matrix.copy(matrix(t.world));o.matrixWorldNeedsUpdate=true;saved.set(t.id,o.matrix.clone());}vp.scene.updateMatrixWorld(true);};
  const ops={async refreshSceneState(){throw Error('Assembly gestures must never refresh the complete Scene');},renderProject(){patchWorkspacePanels(document.body,[['.right',A.renderMachineAssemblyPlacement(view,'')+'<input data-five-key="draft" id="right-input" value="draft"><div style="height:1800px">整体平移与旋转</div>']]);A.syncMachineAssemblyPlacement(context,view,ops);},appendProjectLog(){}};
  document.body.addEventListener('click',event=>{const cmd=event.target.closest('[data-five-command]')?.dataset.fiveCommand;if(cmd?.startsWith('five.machine.assembly:'))A.machineAssemblyCommand(context,view,cmd.slice(13),ops);});
  await project();vp.camera.position.set(650,-850,700);vp.camera.up.set(0,0,1);vp.camera.lookAt(0,0,30);vp.camera.updateMatrixWorld(true);ops.renderProject();
  window.fixture={view,context,ops,A,THREE,row,setup,get saves(){return saves;},get checks(){return checks;},get commits(){return commits;},hold(){hold=true;},get held(){return !!release;},release(){release?.();},snapshot(){const parts=A.machineAssemblyParts(view);return {configuration:structuredClone(setup().machineSetup),parts:parts.map(p=>({id:p.id,transform:[...p.transform],world:row(vp.sceneObjects.get(p.id).matrix)}))};}};
 },{domain,machines});
 await page.locator('[data-five-command="five.machine.assembly:translate"]').click();
 const before=await page.evaluate(()=>fixture.snapshot());
 await gizmoDriver(page).drag('X',22);await page.waitForFunction(()=>!fixture.view.fiveMachineAssembly.saving&&!fixture.view.fiveMachineAssembly.gesture);
 const after=await page.evaluate(()=>fixture.snapshot());report.dragDebug=await page.evaluate(()=>({error:fixture.view.fiveMachine.error,gesture:!!fixture.view.fiveMachineAssembly.gesture,saves:fixture.saves}));assert.notDeepEqual(before.configuration.placement,after.configuration.placement);assert.deepEqual(before.parts.map(p=>p.transform),after.parts.map(p=>p.transform));
 const delta=after.parts[0].world[3]-before.parts[0].world[3];assert.ok(delta>0);assert.ok(Math.abs(after.parts[1].world[3]-before.parts[1].world[3]-delta)<1e-6);report.cases.push('real pointer translates workpiece and fixture together; native local geometry stays unchanged');
 await page.locator('[data-five-command="five.machine.assembly:rotate"]').click();
 await page.evaluate(()=>{const s=fixture.view.viewport.transformManipulator.options;s.onStart({axis:'Z',direction:[0,0,1]});s.onChange({angle:.2});s.onEnd({moved:true});});
 await page.waitForFunction(()=>!fixture.view.fiveMachineAssembly.saving&&!fixture.view.fiveMachineAssembly.gesture);
 const rotated=await page.evaluate(()=>fixture.snapshot());assert.notDeepEqual(rotated.configuration.placement,after.configuration.placement);assert.deepEqual(rotated.parts.map(p=>p.transform),before.parts.map(p=>p.transform));assert.equal(await page.evaluate(()=>fixture.checks),0);report.cases.push('rotation commits one rigid placement without implicit collision or a full Scene refresh');
 const safe=await page.evaluate(()=>fixture.snapshot());
 await page.locator('[data-five-command="five.machine.assembly:translate"]').click();
 await page.evaluate(()=>{const s=fixture.view.viewport.transformManipulator.options;s.onStart({axis:'Z',direction:[0,0,1]});s.onChange({translation:[0,0,-10]});s.onEnd({moved:true});});
 await page.waitForFunction(()=>!fixture.view.fiveMachineAssembly.saving&&!fixture.view.fiveMachineAssembly.gesture);assert.notDeepEqual((await page.evaluate(()=>fixture.snapshot())).configuration,safe.configuration);assert.equal(await page.evaluate(()=>fixture.view.fiveMachine.error),'');report.cases.push('editing placement is independent of collision qualification');
 // Late response plus continuous editing: preserve canvas, both pane nodes,
 // focus, selection, listeners and the latest nested scroll state.
 const commitsBefore=await page.evaluate(()=>fixture.commits);
 await page.evaluate(()=>{fixture.hold();const s=fixture.view.viewport.transformManipulator.options;s.onStart({axis:'X',direction:[1,0,0]});for(let i=1;i<=40;i++)s.onChange({translation:[i/8,0,0]});});
 assert.equal(await page.evaluate(()=>fixture.commits),commitsBefore);assert.equal(await page.evaluate(()=>fixture.checks),0);
 await page.evaluate(()=>fixture.view.viewport.transformManipulator.options.onEnd({moved:true}));await page.waitForFunction(()=>fixture.held);
 const focus=await page.evaluate(()=>{const input=document.querySelector('#right-input'),left=document.querySelector('.left'),right=document.querySelector('.right'),canvas=fixture.view.viewport.renderer.domElement;left.scrollTop=120;right.scrollTop=150;input.focus({preventScroll:true});input.value='12.345';input.setSelectionRange(1,4);let listener=0;input.addEventListener('probe',()=>listener++);left.scrollTop=220;right.scrollTop=250;window.probe={input,left,right,canvas,get listener(){return listener;}};fixture.view.viewport.transformManipulator.options.onChange({translation:[8,0,0]});fixture.release();return true;});
 await page.waitForFunction(()=>!fixture.view.fiveMachineAssembly.saving);assert.equal(await page.evaluate(()=>fixture.commits),commitsBefore+1);assert.equal(await page.evaluate(()=>fixture.checks),0);
 const interaction=await page.evaluate(()=>{probe.input.dispatchEvent(new Event('probe'));return {same:probe.input===document.querySelector('#right-input'),canvas:probe.canvas===fixture.view.viewport.renderer.domElement,focus:document.activeElement===probe.input,selection:[probe.input.selectionStart,probe.input.selectionEnd],scroll:[probe.left.scrollTop,probe.right.scrollTop],listener:probe.listener};});
 assert.deepEqual(interaction,{same:true,canvas:true,focus:true,selection:[1,4],scroll:[220,250],listener:1});assert.equal(focus,true);report.interaction=interaction;report.cases.push('asynchronous acceptance preserves both pane scrolls, focus, selection and canvas');
 const savedState=await invoke('CamScene.ReadState');await rpc({action:'save-reopen'});const reopened=await invoke('CamScene.ReadState');assert.deepEqual(reopened.setups[0].machineSetup,savedState.setups[0].machineSetup);assert.deepEqual(reopened.fixtures,savedState.fixtures);report.cases.push('assembly placement and fixture relative positions survive native save and reopen');
 await rpc({action:'export-project',path:resolve(output,'assembly.i5cam')});await page.evaluate(()=>{document.querySelector('.left').scrollTop=0;document.querySelector('.right').scrollTop=0;});await page.screenshot({path:resolve(output,'assembly.png')});assert.deepEqual(errors,[]);report.passed=true;report.saves=await page.evaluate(()=>fixture.saves);
}finally{writeFileSync(resolve(output,'report.json'),JSON.stringify({...report,errors,calls},null,2));await browser?.close();try{await rpc({action:'exit'});}finally{child.kill();}}
console.log(JSON.stringify(report));
