import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {browserReportDirectory,serveBrowserAsset,browserAssetPath} from './browserPackageRuntime.mjs';

const output=browserReportDirectory('face-selection-20261031');
const report={passed:false,native:!!process.env.ICAX_CEF_ENDPOINT,standaloneCef:false,cases:[],errors:[]};
let connection,nativePage,browser,page,before,snapshot;
try{
 if(process.env.ICAX_CEF_ENDPOINT){
  connection=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT);
  nativePage=connection.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/SDK/AppShell/index.html'));
  snapshot=await nativePage.evaluate(async()=>{
   const base=new URL('../../../',location.href).href,{getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');
   const v=getProjectView(__icaxAppShell.getState().activeProjectId),p=v.scene.domain.placements.find(p=>p.id===v.scene.activePlacementId),o=v.viewport.sceneObjects.get(p.id);o.updateWorldMatrix(true,false);
   const scene={model:v.scene.model,activePlacementId:p.id,intentToolpaths:[],domain:structuredClone(v.scene.domain)};
   return {projectId:__icaxAppShell.getState().activeProjectId,scene,positions:Array.from(o.geometry.attributes.position.array),indices:o.geometry.index&&Array.from(o.geometry.index.array),matrix:o.matrixWorld.toArray(),camera:v.viewport.getCameraState()};
  });
  before=await nativePage.evaluate(async id=>{const {getProjectView}=await import(new URL('../../../',location.href).href+'apps/_shared/workbench/state/projectViewStore.mjs');return getProjectView(id).viewportSceneProxy.invoke('CamScene.ReadState',{});},snapshot.projectId);
 }
 browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'msedge'});
 page=await browser.newPage({viewport:{width:1450,height:900},deviceScaleFactor:2});
 page.on('pageerror',e=>report.errors.push(e.message));
 page.on('console',m=>{if(m.type()==='error'&&/Shader Error|VALIDATE_STATUS|GL_INVALID_OPERATION/.test(m.text()))report.errors.push(m.text());});
 await page.route('http://localhost:17757/**',route=>{
  const path=new URL(route.request().url()).pathname;
  if(path==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><meta charset="utf-8"><style>body{margin:0}.test-shell{display:grid;grid-template-columns:170px 880px 400px;height:900px}.cam-info-pane,.cam-context-pane{overflow:auto;height:900px}.cam-info-pane>div{min-height:1100px}#viewport{width:880px;height:800px}.cam-context-pane>div{height:1600px}.five-position-dialog{height:700px!important}.five-position-body{flex:1}</style><main class="five-workspace test-shell"><aside class="cam-context-pane"><input aria-label="左侧草稿" value="draft"><div></div></aside><div id="viewport"></div><aside class="cam-info-pane"><div id="panel"></div></aside></main>'});
  if(path.endsWith('.css'))return route.fulfill({contentType:'text/css',body:readFileSync(browserAssetPath(path.slice(5)),'utf8')});
  return serveBrowserAsset(route);
 });
 if(snapshot)await page.exposeFunction('nativeInvoke',async(method,payload)=>{
  assert.ok(['CamScene.ReadState','CamCut.QuerySources','CamPlacement.ReadReferenceFaces','CamCut.ResolveFace','CamCut.FindRetainedFaces','CamCut.ReadRetainedSelection','CamPlan.QueryStatus','CamPlan.CancelTask'].includes(method),method);
  return nativePage.evaluate(async({projectId,method,payload})=>{const {getProjectView}=await import(new URL('../../../',location.href).href+'apps/_shared/workbench/state/projectViewStore.mjs');return getProjectView(projectId).viewportSceneProxy.invoke(method,payload,{timeoutMs:300000});},{projectId:snapshot.projectId,method,payload});
 });
 await page.goto('http://localhost:17757/');await page.addStyleTag({url:'http://localhost:17757/src/apps/five-axis-cam/webpage/workspace.css'});
 await page.evaluate(async snapshot=>{
  const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
  const {createThreeViewport}=await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs'),{prepareMeshPicking}=await import('/src/iCAX-UI/SDK/Viewport/meshPicking.mjs');
  const actions=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs'),{renderExtraction}=await import('/src/apps/five-axis-cam/webpage/extractionViews.mjs');
  const {patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs'),{updateCutPreview}=await import('/src/apps/five-axis-cam/webpage/cutPreview.mjs');
  const {handleExtractionRectangleSelect,extractionRectangleSelectionKey}=await import('/src/apps/five-axis-cam/webpage/extractionSelection.mjs');
  const {syncSelectionFeedback,handleSelectionFeedbackHover}=await import('/src/apps/five-axis-cam/webpage/selectionFeedback.mjs');
  const {attachFaceListFocus,faceFocusShortcut}=await import('/src/apps/five-axis-cam/webpage/faceSelectionFocus.mjs');
  const s=window.faceSelection={T,actions,calls:[],held:false,current:true,rectangles:0};
  let geometry,scene,syntheticFaces;
  if(snapshot){scene=snapshot.scene;geometry=new T.BufferGeometry();geometry.setAttribute('position',new T.Float32BufferAttribute(snapshot.positions,3));if(snapshot.indices)geometry.setIndex(snapshot.indices);}
  else{
   geometry=new T.BufferGeometry();const points=[];
   for(const z of [20,0])points.push(-100,-100,z,100,-100,z,100,100,z,-100,-100,z,100,100,z,-100,100,z);
   geometry.setAttribute('position',new T.Float32BufferAttribute(points,3));geometry.setIndex(Array.from({length:12},(_,i)=>i));
   syntheticFaces=[{faceId:'front',first:0,count:2},{faceId:'back',first:2,count:2}];
   scene={model:{isLoaded:true,geometryRef:{id:'geometry',revision:'1'}},activePlacementId:'placement',intentToolpaths:[],domain:{expected:{epoch:'epoch',mutationSequence:'0'},activeSetupId:'setup',placements:[{id:'placement',setupId:'setup',workpieceId:'stock',transform:new T.Matrix4().elements}],workpieces:[{id:'stock'}],setups:[{id:'setup',planningProfileRef:{revision:'1'}}],fixtureUnits:[],fixtures:[],cuts:[]}};
  }
  const v=s.view={scene,fiveAxisArea:'toolpath',fiveAxisExtractionOpen:true};
  const context=s.context={mount:document.querySelector('main'),isCurrentProject:()=>s.current,sceneProxy:{invoke:async(method,payload)=>{
   s.calls.push({method,payload});let result;
   if(snapshot)result=await window.nativeInvoke(method,payload);
   else{
    const faceUses=syntheticFaces.map(f=>({kind:'Face',localId:f.faceId}));
    if(method==='CamCut.QuerySources')result={faceUses};
    else if(method==='CamPlacement.ReadReferenceFaces')result={faces:syntheticFaces};
    else if(method==='CamCut.FindRetainedFaces')result={status:'Completed',epoch:'epoch',taskId:'selection',result:{selectionId:'selection',selectedFaceCount:1}};
    else if(method==='CamCut.ReadRetainedSelection')result={faceIds:['front']};
    else if(method==='CamCut.ResolveFace'){const f=syntheticFaces.find(f=>+payload.query.triangleIndex>=f.first&&+payload.query.triangleIndex<f.first+f.count);result={faceUse:faceUses.find(u=>u.localId===f.faceId),triangleRanges:[f]};}
    else throw Error('Unexpected method '+method);
   }
   if(s.hold===method){s.held=true;await new Promise(r=>s.release=()=>{s.hold=null;s.held=false;r();});}
   return result;
  }}};
  const ops=s.ops={appendProjectLog(){},renderProject(){
   patchWorkspacePanels(document.querySelector('main'),[['#panel',renderExtraction(v)]]);updateCutPreview(v);syncSelectionFeedback(context,v);
  }};
  const vp=v.viewport=createThreeViewport({continuousRender:true,showGrid:false,hoverDetail:'surface',onHover:(...args)=>handleSelectionFeedbackHover(context,v,...args),
   onPick:(...args)=>actions.handleExtractionPick(context,v,...args,ops),getRectangleSelectionKey:()=>extractionRectangleSelectionKey(context,v),
   onRectangleSelect:(rectangle,event)=>{s.rectangles++;s.rectangleFlight=handleExtractionRectangleSelect(context,v,rectangle,event,ops);}});
  vp.mount(document.querySelector('#viewport'));vp.setCameraState(snapshot?.camera||{target:{x:0,y:0,z:0},radius:550,theta:0,phi:0});
  const mesh=s.mesh=new T.Mesh(geometry,new T.MeshBasicMaterial({color:0x69abc1,side:T.DoubleSide}));mesh.userData.objectId=scene.activePlacementId;prepareMeshPicking(mesh,{minimumTriangles:0});
  if(snapshot){mesh.matrixAutoUpdate=false;mesh.matrix.fromArray(snapshot.matrix);}
  vp.content.add(mesh);vp.sceneObjects.set(scene.activePlacementId,mesh);
  s.state=actions.extractionDraft(v).extraction;v.fiveAxisUI.tools.toolpath='extract';s.run=async(action,arg)=>{await actions.extractionCommand(context,v,action,arg,ops);ops.renderProject();};
  document.querySelector('main').addEventListener('click',event=>{const b=event.target.closest('[data-five-command]');if(!b||b.disabled)return;const [,,a,id]=b.dataset.fiveCommand.split('.');s.flight=s.run(a,id);});
  attachFaceListFocus(document.querySelector('main'),id=>{const [,,action,argument]=id.split('.');s.flight=s.run(action,argument);});
  document.addEventListener('keydown',event=>{const id=faceFocusShortcut(event,v,document.querySelector('main'));if(id){event.preventDefault();const [,,action,argument]=id.split('.');s.flight=s.run(action,argument);}});
  ops.renderProject();
 },snapshot);
 const idle=()=>page.waitForFunction(()=>!faceSelection.view.pending&&!faceSelection.state.faceRequest,null,{timeout:300000});
 const command=async(action,arg)=>{await page.evaluate(async({action,arg})=>faceSelection.run(action,arg),{action,arg});await idle();};
 assert.equal(await page.locator('[data-five-command="five.extraction.auto"]').isEnabled(),true);
 await page.locator('[data-five-command="five.extraction.auto"]').click();await idle();
 report.automatic=await page.evaluate(()=>({faces:faceSelection.state.faces.length,error:faceSelection.view.error,manual:!!faceSelection.view.fiveAxisExtractionPick,calls:faceSelection.calls.map(c=>c.method)}));
 assert.equal(report.automatic.error,'');assert.ok(report.automatic.faces>0);assert.equal(report.automatic.manual,false);
 assert.ok(report.automatic.calls.includes('CamCut.ReadRetainedSelection'));
 assert.ok(report.automatic.calls.includes('CamPlacement.ReadReferenceFaces'));
 assert.equal(await page.locator('.five-support-face').count(),report.automatic.faces);
 assert.equal(await page.locator('.five-support-face>button').first().innerText(),'面 1');
 report.cases.push('Empty selection has an enabled batch-add action; native envelope selection fills editable face rows and complete triangle ranges without creating geometry or toolpaths');
 const ids=await page.evaluate(()=>faceSelection.state.faces.map(f=>f.localId));
 await page.locator('.five-support-face>button').first().click();
 assert.equal(await page.evaluate(()=>faceSelection.state.selectedFace),ids[0]);
 const highlight=await page.evaluate(()=>{const m=faceSelection.view.fiveCutPreview.group.children.find(o=>o.userData.selectedMachiningFaces),active=m.children.find(o=>o.userData.activeSelectionFace!=null);return {active:active.material.color.getHex(),depthTest:active.material.depthTest,order:active.renderOrder,ordinaryColor:Array.from(m.geometry.attributes.color.array.slice(0,3)),opacity:m.material.opacity};});
 assert.equal(highlight.active,0xe83eff);assert.equal(highlight.depthTest,false);assert.ok(highlight.order>65);assert.equal(highlight.opacity,.5);
 report.membershipColor=highlight.ordinaryColor;
 await page.screenshot({path:resolve(output,'automatic-face-list.png')});
 await page.locator('.five-support-face>button:last-child').first().click();await idle();
 assert.equal(await page.evaluate(()=>faceSelection.state.faces.some(f=>f.localId===faceSelection.state.selectedFace)),false);
 await command('auto');await command('pick');assert.equal(await page.locator('[data-five-command="five.extraction.pick"]').getAttribute('aria-pressed'),'true');
 const occlusion=page.getByRole('checkbox',{name:'遮挡',exact:true});assert.equal(await occlusion.isChecked(),true);
 assert.equal(await page.getByText('按观察方向批量添加保留材料上的非加工面',{exact:false}).count(),0);
 assert.equal(await page.getByRole('button',{name:'穿透选择',exact:true}).count(),0);
 await occlusion.locator('..').click();await idle();assert.equal(await occlusion.isChecked(),false);
 assert.equal(await page.evaluate(()=>faceSelection.state.throughSelection),true);
 await occlusion.focus();await page.keyboard.press('Space');await idle();assert.equal(await occlusion.isChecked(),true);
 assert.equal(await page.evaluate(()=>faceSelection.state.throughSelection),false);
 const pixel=await page.evaluate(()=>{
  const {T,mesh,view:v,state}=faceSelection,vp=v.viewport,g=mesh.geometry,p=g.attributes.position,index=g.index,r=vp.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster();
  vp.scene.updateMatrixWorld(true);vp.camera.updateMatrixWorld(true);
  for(const face of state.faces)for(const range of state.faceRanges[face.localId]||[])for(let i=+range.first;i<+range.first+ +range.count;i+=Math.max(1,Math.floor(+range.count/200))){
   const q=new T.Vector3();for(let j=0;j<3;j++)q.add(new T.Vector3().fromBufferAttribute(p,index?index.getX(i*3+j):i*3+j));q.multiplyScalar(1/3).applyMatrix4(mesh.matrixWorld).project(vp.camera);
   if(Math.abs(q.x)>.9||Math.abs(q.y)>.9||Math.abs(q.z)>1)continue;
   ray.setFromCamera(q,vp.camera);const hit=ray.intersectObject(mesh,false)[0];
   if(hit&&state.faceRanges[face.localId].some(f=>hit.faceIndex>=+f.first&&hit.faceIndex<+f.first+ +f.count))return {x:r.x+(q.x+1)*r.width/2,y:r.y+(1-q.y)*r.height/2,id:face.localId};
  }throw Error('No visible suggested face');
 });
 await page.mouse.click(pixel.x,pixel.y);await idle();assert.equal(await page.evaluate(id=>faceSelection.state.faces.some(f=>f.localId===id),pixel.id),false);
 await page.mouse.click(pixel.x,pixel.y);await idle();assert.equal(await page.evaluate(id=>faceSelection.state.faces.some(f=>f.localId===id),pixel.id),true);
 await command('pick');assert.equal(await page.locator('[data-five-command="five.extraction.pick"]').getAttribute('aria-pressed'),'false');
 const revision=await page.evaluate(()=>faceSelection.state.revision);await page.mouse.click(pixel.x,pixel.y);await idle();
 assert.equal(await page.evaluate(()=>faceSelection.state.revision),revision);assert.equal(await page.evaluate(()=>faceSelection.state.selectedFace),pixel.id);
 report.cases.push('Face rows use support presentation, sequential labels, a purple depth-independent active item, unchanged teal set membership, individual removal and a true manual-picking toggle');
 // Hold a topology response while focus, caret and both sidebar scrolls change.
 await page.addStyleTag({content:'.five-position-body::after{content:"";display:block;height:1000px}'});
 await page.evaluate(()=>{faceSelection.hold='CamCut.QuerySources';document.querySelector('.five-extraction-direction-menu').open=true;document.querySelector('.cam-info-pane>div').style.minHeight='1600px';});
 await page.locator('[data-five-command="five.extraction.auto"]').click();await page.waitForFunction(()=>faceSelection.held);
 const positions=await page.evaluate(()=>{
  const s=faceSelection,input=document.querySelector('[aria-label="左侧草稿"]');s.input=input;s.canvas=s.view.viewport.renderer.domElement;s.list=document.querySelector('[data-five-key="extraction-faces"]');s.probes=0;input.addEventListener('probe',()=>s.probes++);
  input.focus({preventScroll:true});input.setSelectionRange(0,1);
  return ['.cam-context-pane','.cam-info-pane','.five-position-body'].map((selector,i)=>{const e=document.querySelector(selector);if(selector==='.five-position-body'){e.style.maxHeight='260px';e.style.minHeight='260px';}e.scrollTop=40+i*10;return e.scrollTop;});
 });
 // Continue scrolling after the request began; restoration must use these values.
 const latest=await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-position-body'].map((selector,i)=>{const e=document.querySelector(selector);e.scrollTop=55+i*10;return e.scrollTop;}));
 assert.ok(positions[0]>0&&positions[1]>0);await page.evaluate(()=>faceSelection.release());await idle();
 const retained=await page.evaluate(()=>{const s=faceSelection;s.input.dispatchEvent(new Event('probe'));return {input:s.input===document.querySelector('[aria-label="左侧草稿"]'),focus:document.activeElement===s.input,selection:[s.input.selectionStart,s.input.selectionEnd],canvas:s.canvas===s.view.viewport.renderer.domElement,list:s.list===document.querySelector('[data-five-key="extraction-faces"]'),probes:s.probes,scroll:['.cam-context-pane','.cam-info-pane','.five-position-body'].map(selector=>document.querySelector(selector).scrollTop)};});
 assert.deepEqual(retained,{input:true,focus:true,selection:[0,1],canvas:true,list:true,probes:1,scroll:latest});report.retained=retained;
 await page.evaluate(()=>{faceSelection.direction=document.querySelector('[data-five-extraction-direction="0"]');faceSelection.direction.focus({preventScroll:true});faceSelection.state.mode='Region';faceSelection.ops.renderProject();});
 assert.equal(await page.locator('[data-five-extraction-direction]').count(),0);assert.equal(await page.evaluate(()=>document.activeElement===faceSelection.direction),false);
 await page.evaluate(()=>{faceSelection.state.mode='Faces';faceSelection.ops.renderProject();document.querySelector('.cam-info-pane').scrollTop=0;document.querySelector('.five-extraction-direction-menu').open=false;});
 report.cases.push('Delayed automatic selection preserves latest left/right/nested scrolls, input/canvas/list identity, focus, caret and listeners; removing conditional direction fields does not focus another field');
 await command('clear');await command('pick');
 const box=await page.locator('#viewport canvas').first().boundingBox();
 const drag=async(through=false,modifier=null)=>{
  await occlusion.setChecked(!through);await idle();
  if(modifier)await page.keyboard.down(modifier);
  await page.mouse.move(box.x+box.width-10,box.y+box.height-10);await page.mouse.down();await page.mouse.move(box.x+10,box.y+10,{steps:8});await page.mouse.up();
  if(modifier)await page.keyboard.up(modifier);await idle();return page.evaluate(()=>faceSelection.state.faces.map(f=>f.localId));
 };
 const visible=await drag(false);assert.deepEqual(await drag(false),visible,'Selected-face overlays must not occlude the next gesture');
 const through=await drag(true);
 assert.equal(await occlusion.isChecked(),false);
 await page.evaluate(()=>faceSelection.ops.renderProject());assert.equal(await occlusion.isChecked(),false,'Refresh must retain the chosen occlusion setting');
 assert.ok(visible.length>0);assert.ok(through.length>visible.length,'Depth-filtered selection must omit hidden/back faces');
 if(!snapshot){assert.deepEqual(visible,['front']);assert.deepEqual(through,['front','back']);}
 await drag(true,'Alt');assert.equal(await page.evaluate(()=>faceSelection.state.faces.length),0);
 const added=await drag(false,'Control');assert.ok(added.length>0);
 report.rectangle={visible:visible.length,through:through.length,realGestures:await page.evaluate(()=>faceSelection.rectangles)};
 await page.screenshot({path:resolve(output,'visible-face-box-selection.png')});
 report.cases.push('Occlusion checkbox starts checked; label clicks and Space toggle it; checked excludes hidden faces, unchecked selects through them; real rectangle gestures and refresh retain the chosen behavior');
 // Choose an item which the real depth-filtered gesture could not see.
 await drag(true);const hiddenId=through.find(id=>!visible.includes(id));assert.ok(hiddenId);
 const row=page.locator(`[data-five-command="five.extraction.select-face.${hiddenId}"]`);
 await row.click();await idle();
 const inspected=await page.evaluate(()=>({camera:faceSelection.view.viewport.getCameraState(),faces:faceSelection.state.faces.map(f=>f.localId),revision:faceSelection.state.revision}));
 report.occludedHighlight=await page.evaluate(()=>{
  const {view:v,state:s,T}=faceSelection,vp=v.viewport,base=v.fiveCutPreview.group.children.find(o=>o.userData.selectedMachiningFaces),active=base.children.find(o=>o.userData.activeSelectionFace!=null);
  const renderer=vp.renderer,target=new T.WebGLRenderTarget(440,400),previous=renderer.getRenderTarget(),rect=renderer.getViewport(new T.Vector4());
  const count=()=>{renderer.setRenderTarget(target);renderer.render(vp.scene,vp.camera);const p=new Uint8Array(440*400*4);renderer.readRenderTargetPixels(target,0,0,440,400,p);let n=0;for(let i=0;i<p.length;i+=4)if(p[i]>p[i+1]*1.8&&p[i+2]>p[i+1]*1.8&&p[i+2]>80)n++;return n;};
  try{
   const through=count();active.material.depthTest=true;active.children.forEach(o=>o.material.depthTest=true);const depthTested=count();
   active.material.depthTest=false;active.children.forEach(o=>o.material.depthTest=false);
   return {through,depthTested,activeId:active.userData.activeSelectionFace,ordinaryColor:Array.from(base.geometry.attributes.color.array.slice(0,3)),faces:s.faces.length};
  }finally{renderer.setViewport(rect);renderer.setRenderTarget(previous);target.dispose();}
 });
 assert.deepEqual(report.occludedHighlight.ordinaryColor,report.membershipColor);
 assert.ok(report.occludedHighlight.through>report.occludedHighlight.depthTested,'Purple active face is visible through real occluding geometry');
 await page.screenshot({path:resolve(output,'purple-occluded-active-face.png')});
 await row.dblclick();await idle();
 const focused=await page.evaluate(()=>{
  const {view:v,state:s,T,mesh}=faceSelection,vp=v.viewport,p=mesh.geometry.attributes.position,index=mesh.geometry.index,box=new T.Box3(),projected=[];
  for(const range of s.faceRanges[s.selectedFace])for(let n=+range.first*3;n<(+range.first+ +range.count)*3;n++){const q=new T.Vector3().fromBufferAttribute(p,index?index.getX(n):n).applyMatrix4(mesh.matrixWorld);box.expandByPoint(q);projected.push(q.project(vp.camera));}
  return {camera:vp.getCameraState(),center:box.getCenter(new T.Vector3()).toArray(),maxNdc:projected.reduce((n,p)=>Math.max(n,Math.abs(p.x),Math.abs(p.y)),0),faces:s.faces.map(f=>f.localId),revision:s.revision};
 });
 assert.notDeepEqual(focused.camera,inspected.camera);assert.deepEqual(focused.faces,inspected.faces);assert.equal(focused.revision,inspected.revision);
 assert.ok(focused.maxNdc<.95);for(const [i,k] of ['x','y','z'].entries())assert.ok(Math.abs(focused.camera.target[k]-focused.center[i])<1e-6);
 const sameDirection=(actual,expected)=>{for(const key of ['projectionMode','theta','phi'])assert.equal(actual[key],expected[key],key);};
 sameDirection(focused.camera,inspected.camera);
 await page.screenshot({path:resolve(output,'focused-active-face.png')});
 await page.evaluate(camera=>faceSelection.view.viewport.setCameraState(camera),inspected.camera);
 await page.locator(`[data-five-command="five.extraction.focus-face.${hiddenId}"]`).click();await idle();
 const buttonCamera=await page.evaluate(()=>faceSelection.view.viewport.getCameraState());assert.notDeepEqual(buttonCamera,inspected.camera);sameDirection(buttonCamera,inspected.camera);
 await page.evaluate(camera=>faceSelection.view.viewport.setCameraState(camera),inspected.camera);await row.focus();await page.keyboard.press('f');await idle();
 const keyCamera=await page.evaluate(()=>faceSelection.view.viewport.getCameraState());assert.notDeepEqual(keyCamera,inspected.camera);sameDirection(keyCamera,inspected.camera);
 const input=page.getByLabel('左侧草稿');await input.focus();const typingCamera=await page.evaluate(()=>faceSelection.view.viewport.getCameraState());await page.keyboard.press('f');
 assert.deepEqual(await page.evaluate(()=>faceSelection.view.viewport.getCameraState()),typingCamera);
 report.cases.push('An occluded active item renders purple through the model without changing retained-face colours; real double-click, locate button and F frame the complete face while preserving projection, membership and draft revision; typing F leaves the camera alone');
 // Support uses the same active-item presentation and focus calculation.
 report.supportFocus=await page.evaluate(async id=>{
  const {designDraft,designCommand,highlightDesign,disposeFixtureDesign}=await import('/src/apps/five-axis-cam/webpage/fixtureDesign.mjs');
  const {view:v,state:s}=faceSelection,ref={scene:v.scene,viewport:v.viewport,fiveAxisArea:'workpiece',fiveFixtureDesignOpen:true,fixtureDesignPlacement:v.scene.activePlacementId,
   fixtureDesignFaces:s.faceRanges[id].map(r=>({...r,faceId:id})),fiveAxisUI:{tools:{workpiece:'fixture'},drafts:{}}};
  const d=designDraft(ref);d.supportFaces=[id];d.dirty=true;
  const camera=v.viewport.getCameraState();
  await designCommand(faceSelection.context,ref,'focus-face:'+id,{renderProject(){highlightDesign(ref);}});
  for(const key of ['projectionMode','theta','phi'])if(camera[key]!==v.viewport.getCameraState()[key])throw Error('Support focus changed '+key);
  const active=ref.fixtureDesignOverlay.children.find(o=>o.userData.activeSelectionFace!=null),result={selected:ref.fixtureDesignSelectedFace,color:active.material.color.getHex(),depthTest:active.material.depthTest,faces:[...d.supportFaces]};
  disposeFixtureDesign(ref);return result;
 },hiddenId);
 assert.deepEqual(report.supportFocus,{selected:hiddenId,color:0xe83eff,depthTest:false,faces:[hiddenId]});
 // Use an oblique view in both projection modes and a rolled-over free orbit.
 report.directionFits=[];
 for(const projectionMode of ['perspective','orthographic'])for(const phi of [.7,3.8]){
  const result=await page.evaluate(async({id,projectionMode,phi})=>{
   const {view:v,state:s,T,mesh}=faceSelection,vp=v.viewport,{focusSelectedFace}=await import('/src/apps/five-axis-cam/webpage/faceSelectionFocus.mjs');
   vp.options.constrainOrbit=false;vp.setCameraState({...vp.getCameraState(),projectionMode,theta:-.63,phi,radius:1700});
   const before=vp.getCameraState(),quaternion=vp.camera.quaternion.clone(),ranges=s.faceRanges[id];
   if(!focusSelectedFace(v,mesh,ranges))throw Error('Face focus failed');
   const after=vp.getCameraState(),p=mesh.geometry.attributes.position,index=mesh.geometry.index;let maxNdc=0,maxDepth=0;
   for(const r of ranges)for(let n=+r.first*3;n<(+r.first+ +r.count)*3;n++){
    const q=new T.Vector3().fromBufferAttribute(p,index?index.getX(n):n).applyMatrix4(mesh.matrixWorld).project(vp.camera);
    maxNdc=Math.max(maxNdc,Math.abs(q.x),Math.abs(q.y));maxDepth=Math.max(maxDepth,Math.abs(q.z));
   }
   focusSelectedFace(v,mesh,ranges);
   return {before,after,maxNdc,maxDepth,quaternionDot:Math.abs(quaternion.dot(vp.camera.quaternion)),repeat:vp.getCameraState()};
  },{id:hiddenId,projectionMode,phi});
  sameDirection(result.after,result.before);assert.ok(result.quaternionDot>1-1e-12);assert.ok(result.maxNdc<.95);assert.ok(result.maxDepth<1);sameDirection(result.repeat,result.after);assert.deepEqual(result.repeat.target,result.after.target);assert.ok(Math.abs(result.repeat.radius-result.after.radius)<1e-9);report.directionFits.push(result);
 }
 report.cases.push('Face focus preserves theta, phi and quaternion at oblique and rolled-over angles in perspective and orthographic views; all face vertices fit and repeated focus is stable');
 if(!snapshot){
  // Real scene clicks with the active face placed at either end of a long list.
  await page.evaluate(()=>{
   const s=faceSelection;s.view.fiveAxisExtractionPick=null;s.state.faces=[{localId:'back'},...Array.from({length:38},(_,i)=>({localId:'padding-'+i})),{localId:'front'}];
   s.state.faceRanges={front:[{first:0,count:2}],back:[{first:2,count:2}]};s.state.selectedFace='back';
   s.view.viewport.setCameraState({target:{x:0,y:0,z:0},radius:550,theta:0,phi:0});s.ops.renderProject();
   const input=document.querySelector('[aria-label="左侧草稿"]');input.focus({preventScroll:true});input.setSelectionRange(1,3);
   s.scrollInput=input;s.scrollCanvas=s.view.viewport.renderer.domElement;s.scrollList=document.querySelector('[data-five-key="extraction-faces"]');
   s.scrollList.scrollTop=0;s.scrollList.scrollLeft=0;
   s.outerScroll=['.cam-context-pane','.cam-info-pane','.five-position-body'].map((selector,i)=>{const e=document.querySelector(selector);e.scrollTop=30+i*10;return e.scrollTop;});
  });
  const sceneClick=async()=>{const canvas=await page.locator('#viewport canvas').first().boundingBox();await page.mouse.click(canvas.x+canvas.width*.53,canvas.y+canvas.height*.48);await idle();};
  const checkReveal=()=>page.evaluate(()=>{
   const s=faceSelection,list=s.scrollList,row=list.querySelector('.is-selected'),r=row.getBoundingClientRect(),p=list.getBoundingClientRect();
   return {selected:s.state.selectedFace,visible:r.top>=p.top+list.clientTop-.5&&r.bottom<=p.top+list.clientTop+list.clientHeight+.5,scroll:list.scrollTop,
    outer:['.cam-context-pane','.cam-info-pane','.five-position-body'].map(selector=>document.querySelector(selector).scrollTop),expectedOuter:s.outerScroll,
    input:s.scrollInput===document.querySelector('[aria-label="左侧草稿"]'),canvas:s.scrollCanvas===s.view.viewport.renderer.domElement,list:s.scrollList===document.querySelector('[data-five-key="extraction-faces"]'),
    focus:document.activeElement===s.scrollInput,selection:[s.scrollInput.selectionStart,s.scrollInput.selectionEnd]};
  });
  await sceneClick();let revealed=await checkReveal();assert.equal(revealed.selected,'front');assert.equal(revealed.visible,true);assert.ok(revealed.scroll>0);assert.deepEqual(revealed.outer,revealed.expectedOuter);assert.ok(revealed.input&&revealed.canvas&&revealed.list);assert.deepEqual(revealed.selection,[1,3]);
  // Pointer focus belongs to the viewport; revealing a row must not steal it.
  assert.equal(await page.evaluate(()=>document.activeElement.closest?.('.five-support-face-list')!=null),false);
  const outer=await page.evaluate(()=>faceSelection.outerScroll);report.sceneReveal={down:revealed};
  await page.evaluate(()=>{const s=faceSelection;s.state.faces.reverse();s.ops.renderProject();s.scrollList.scrollTop=s.scrollList.scrollHeight;});
  await sceneClick();revealed=await checkReveal();assert.equal(revealed.selected,'front');assert.equal(revealed.visible,true);assert.equal(revealed.scroll,0);assert.deepEqual(revealed.outer,outer);report.sceneReveal.up=revealed;
  await page.evaluate(()=>{const s=faceSelection;s.scrollList.scrollTop=s.scrollList.scrollHeight;});
  await sceneClick();assert.equal((await checkReveal()).visible,true,'Clicking the same scene face must reveal it again');
  await page.evaluate(()=>{const s=faceSelection;s.scrollInput.focus({preventScroll:true});s.scrollInput.setSelectionRange(1,3);s.scrollList.scrollTop=s.scrollList.scrollHeight;const hit={faceIndex:0};s.actions.handleExtractionPick(s.context,s.view,s.mesh.userData,hit,{},[],s.ops);});
  revealed=await checkReveal();assert.equal(revealed.focus,true);assert.deepEqual(revealed.selection,[1,3]);assert.deepEqual(revealed.outer,outer);
  await page.evaluate(()=>{const s=faceSelection;s.scrollList.scrollTop=s.scrollList.scrollHeight;s.manualScroll=s.scrollList.scrollTop;s.ops.renderProject();});
  assert.equal(await page.evaluate(()=>faceSelection.scrollList.scrollTop===faceSelection.manualScroll),true,'Unrelated refresh must retain manual face-list scrolling');
  await page.evaluate(()=>{const s=faceSelection;s.state.faces=s.state.faces.filter(f=>f.localId!=='front');delete s.state.faceRanges.front;s.state.selectedFace='back';s.view.fiveAxisExtractionPick=s.state;s.hold='CamCut.ResolveFace';s.ops.renderProject();});
  const canvas=await page.locator('#viewport canvas').first().boundingBox();await page.mouse.click(canvas.x+canvas.width*.53,canvas.y+canvas.height*.48);await page.waitForFunction(()=>faceSelection.held);
  await page.evaluate(()=>{const s=faceSelection;s.scrollInput.focus({preventScroll:true});s.scrollInput.setSelectionRange(0,2);s.outerScroll=['.cam-context-pane','.cam-info-pane','.five-position-body'].map((selector,i)=>{const e=document.querySelector(selector);e.scrollTop=65+i*10;return e.scrollTop;});s.release();});await idle();
  revealed=await checkReveal();assert.equal(revealed.selected,'front');assert.equal(revealed.visible,true);assert.equal(revealed.focus,true);assert.deepEqual(revealed.selection,[0,2]);assert.deepEqual(revealed.outer,revealed.expectedOuter);assert.ok(revealed.input&&revealed.canvas&&revealed.list);report.sceneReveal.asyncAdded=revealed;
  await page.screenshot({path:resolve(output,'scene-face-list-revealed.png')});
  report.cases.push('Scene face clicks reveal the selected row upward/downward and on repeated selection; unrelated panes, DOM nodes, input focus/selection and ordinary refresh scrolling are retained');
 }
 if(snapshot){
  const after=await nativePage.evaluate(async id=>{const {getProjectView}=await import(new URL('../../../',location.href).href+'apps/_shared/workbench/state/projectViewStore.mjs');return getProjectView(id).viewportSceneProxy.invoke('CamScene.ReadState',{});},snapshot.projectId);
  for(const k of ['expected','workpieces','placements','fixtures','intentToolpaths'])assert.deepEqual(after[k],before[k],k);report.nativeUnchanged=true;
 }
 assert.deepEqual(report.errors,[]);report.passed=true;
}catch(e){report.failure=e.stack;if(page)await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw e;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser?.close();await connection?.close();}
