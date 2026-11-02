import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {browserReportDirectory,serveBrowserAsset} from './browserPackageRuntime.mjs';

// Real pointer events and the production viewport/feedback modules. An optional
// native CEF connection supplies a copy of the open workpiece and read-only face
// resolution; the user's canvas, drafts and project are never changed.
const output=browserReportDirectory('face-hover-20261031');
const report={passed:false,realMouse:true,nativeTopology:!!process.env.ICAX_CEF_ENDPOINT,cases:[],errors:[]};
let nativeBrowser,nativePage,browser,page;
try{
 let snapshot;
 if(process.env.ICAX_CEF_ENDPOINT){
  nativeBrowser=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT);
  nativePage=nativeBrowser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/SDK/AppShell/index.html'));
  assert.ok(nativePage,'Open native CAM project');
  snapshot=await nativePage.evaluate(async()=>{
   const base=new URL('../../../',location.href).href,{getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');
   const v=getProjectView(__icaxAppShell.getState().activeProjectId),vp=v.viewport,p=v.scene.domain.placements.find(p=>p.id===v.scene.activePlacementId);
   const mesh=vp.sceneObjects.get(p.id);mesh.updateWorldMatrix(true,false);
   const faces=(await v.viewportSceneProxy.invoke('CamPlacement.ReadReferenceFaces',{expected:v.scene.domain.expected,placementId:p.id,setupId:p.setupId})).faces;
   return {projectId:__icaxAppShell.getState().activeProjectId,scene:{activePlacementId:p.id,model:{geometryRef:v.scene.model.geometryRef},intentToolpaths:[],
    domain:{expected:v.scene.domain.expected,activeSetupId:p.setupId,placements:[p],workpieces:v.scene.domain.workpieces}},
    positions:Array.from(mesh.geometry.attributes.position.array),indices:mesh.geometry.index?Array.from(mesh.geometry.index.array):null,
    matrix:mesh.matrixWorld.toArray(),camera:vp.getCameraState(),faces};
  });
 }
 browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'msedge'});
 page=await browser.newPage({viewport:{width:1200,height:850}});
 page.on('pageerror',e=>report.errors.push(e.message));
 await page.route('http://localhost:17756/**',route=>new URL(route.request().url()).pathname==='/'?route.fulfill({contentType:'text/html',body:
  '<!doctype html><meta charset="utf-8"><style>body{margin:0;background:#152830}#viewport{width:1100px;height:750px}#outside{height:100px}</style><div id="viewport"></div><div id="outside"></div>'}):serveBrowserAsset(route));
 if(snapshot)await page.exposeFunction('nativeResolve',request=>nativePage.evaluate(async({request,projectId})=>{
  const base=new URL('../../../',location.href).href,{getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');
  return getProjectView(projectId).viewportSceneProxy.invoke('CamCut.ResolveFace',request);
 },{request,projectId:snapshot.projectId}));
 await page.goto('http://localhost:17756/');
 report.geometry=await page.evaluate(async snapshot=>{
  const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
  const {createThreeViewport}=await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
  const {prepareMeshPicking}=await import('/src/iCAX-UI/SDK/Viewport/meshPicking.mjs');
  const {syncSelectionFeedback,handleSelectionFeedbackHover}=await import('/src/apps/five-axis-cam/webpage/selectionFeedback.mjs');
  const {extractionDraft}=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');
  const s=window.faceHover={calls:0,delay:110,seen:[],events:[],current:true};
  let geometry,faces,scene;
  if(snapshot){geometry=new T.BufferGeometry();geometry.setAttribute('position',new T.Float32BufferAttribute(snapshot.positions,3));if(snapshot.indices)geometry.setIndex(snapshot.indices);faces=snapshot.faces;scene=snapshot.scene;}
  else{
   geometry=new T.PlaneGeometry(400,240,20,12);faces=[];
   const p=geometry.attributes.position,g=geometry.index;
   for(let i=0;i<g.count/3;i++){
    const x=[0,1,2].reduce((sum,j)=>sum+p.getX(g.getX(i*3+j)),0),id=x<0?'left':'right',last=faces.at(-1);
    if(last?.faceId===id)last.count++;else faces.push({faceId:id,first:i,count:1});
   }
   scene={activePlacementId:'part',model:{geometryRef:{id:'geometry',revision:'1'}},intentToolpaths:[],domain:{expected:{epoch:'epoch',mutationSequence:'0'},activeSetupId:'setup',placements:[{id:'part',setupId:'setup',workpieceId:'stock'}],workpieces:[{id:'stock'}]}};
  }
  const v=s.view={fiveAxisArea:'toolpath',scene},context=s.context={isCurrentProject:()=>s.current,sceneProxy:{invoke:async(method,request)=>{
   if(method!=='CamCut.ResolveFace')throw Error('Unexpected method '+method);
   s.calls++;const delay=s.delay;
   const result=snapshot?await window.nativeResolve(request):(()=>{const face=faces.find(f=>+request.query.triangleIndex>=+f.first&&+request.query.triangleIndex<+f.first+ +f.count);return {faceUse:{localId:face.faceId},triangleRanges:faces.filter(f=>f.faceId===face.faceId)};})();
   await new Promise(r=>setTimeout(r,delay));return result;
  }}};
  const vp=v.viewport=createThreeViewport({continuousRender:true,showGrid:false,hoverEnabled:true,hoverDetail:'surface',onHover(...args){
   handleSelectionFeedbackHover(context,v,...args);const h=args[1],f=h&&faces.find(f=>h.faceIndex>=+f.first&&h.faceIndex<+f.first+ +f.count);
   s.events.push({triangle:h?.faceIndex,face:f&&String(f.faceId),target:v.fiveSelectionFeedback?.target?.faceId,group:v.fiveSelectionFeedback?.group?.uuid});
  }});vp.mount(document.querySelector('#viewport'));
  vp.setCameraState(snapshot?.camera||{target:{x:0,y:0,z:0},radius:600,theta:0,phi:.15});
  const mesh=s.mesh=new T.Mesh(geometry,new T.MeshBasicMaterial({color:0x69abc1,side:T.DoubleSide}));mesh.userData.objectId=scene.activePlacementId;
  prepareMeshPicking(mesh,{minimumTriangles:0});
  if(snapshot){mesh.matrixAutoUpdate=false;mesh.matrix.fromArray(snapshot.matrix);}
  vp.content.add(mesh);vp.sceneObjects.set(scene.activePlacementId,mesh);
  v.fiveAxisExtractionPick=extractionDraft(v).extraction;syncSelectionFeedback(context,v);s.sync=()=>syncSelectionFeedback(context,v);
  const sample=()=>{s.seen.push({time:performance.now(),target:v.fiveSelectionFeedback?.target?.faceId,group:v.fiveSelectionFeedback?.group?.uuid});if(!s.stop)requestAnimationFrame(sample);};requestAnimationFrame(sample);
  vp.scene.updateMatrixWorld(true);vp.camera.updateMatrixWorld(true);
  const p=geometry.attributes.position,g=geometry.index,r=vp.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster(),groups=[];
  for(const id of [...new Set([...faces].sort((a,b)=>+b.count- +a.count).map(f=>String(f.faceId)))]){
   const pixels=[];
   for(const range of faces.filter(f=>String(f.faceId)===id)){
    for(let i=+range.first;i<+range.first+ +range.count;i+=Math.max(1,Math.floor(+range.count/500))){
     const q=new T.Vector3();for(let j=0;j<3;j++)q.add(new T.Vector3().fromBufferAttribute(p,g?g.getX(i*3+j):i*3+j));q.multiplyScalar(1/3).applyMatrix4(mesh.matrixWorld).project(vp.camera);
     if(Math.abs(q.x)>.85||Math.abs(q.y)>.8||Math.abs(q.z)>1)continue;
     ray.setFromCamera(q,vp.camera);const h=ray.intersectObject(mesh,false)[0];if(h?.faceIndex!==i)continue;
     const pixel={x:r.x+(q.x+1)*r.width/2,y:r.y+(1-q.y)*r.height/2,triangle:i};
     if(pixels.every(p=>Math.hypot(pixel.x-p.x,pixel.y-p.y)>2))pixels.push(pixel);
     if(pixels.length>=12)break;
    }if(pixels.length>=12)break;
   }
   if(pixels.length>=3)groups.push({id,pixels});if(groups.length>=2)break;
  }
  s.groups=groups;return {triangles:(g?.count||p.count)/3,faces:new Set(faces.map(f=>String(f.faceId))).size,visible:groups};
 },snapshot);
 const [a,b]=report.geometry.visible;assert.ok(a&&b,'Two visible multi-triangle faces');
 const move=p=>page.mouse.move(p.x,p.y);
 const target=id=>page.waitForFunction(id=>faceHover.view.fiveSelectionFeedback?.target?.faceId===id,id);
 const blank=async()=>{await page.mouse.move(1150,800);await page.waitForTimeout(40);assert.equal(await page.evaluate(()=>faceHover.view.fiveSelectionFeedback.target),null);};
 // Continuous motion must resolve before the pointer stops, even with delayed RPC.
 await move(a.pixels[0]);
 for(let i=0;i<24;i++){await move(a.pixels[i%a.pixels.length]);await page.waitForTimeout(15);}
 assert.ok(await page.evaluate(id=>faceHover.seen.some(s=>s.target===id),a.id),'Cold face resolves during continuous movement');
 await target(a.id);
 const warm=await page.evaluate(()=>{faceHover.events=[];faceHover.seen=[];return {calls:faceHover.calls,group:faceHover.view.fiveSelectionFeedback.group.uuid};});
 for(let i=0;i<36;i++){await move(a.pixels[i%a.pixels.length]);await page.waitForTimeout(15);}
 const stable=await page.evaluate(()=>({calls:faceHover.calls,events:faceHover.events,frames:faceHover.seen}));
 assert.equal(stable.calls,warm.calls,'Known face needs no repeated native lookups');assert.ok(stable.events.length>=12);
 assert.ok(new Set(stable.events.map(e=>e.triangle)).size>=3);
 assert.ok(stable.frames.every(f=>f.target===a.id&&f.group===warm.group),'Every frame retains the same complete face overlay');
 report.stability={pointerEvents:stable.events.length,distinctTriangles:new Set(stable.events.map(e=>e.triangle)).size,renderedFrames:stable.frames.length,
  blankFrames:stable.frames.filter(f=>!f.target).length,extraResolveRequests:stable.calls-warm.calls};
 report.cases.push('Continuous cold hover resolves without waiting for rest; crossing many triangles of the resolved face retains one overlay with zero new requests and zero blank frames');
 // Return to A while B is still resolving. B must never reappear afterwards.
 await page.evaluate(()=>faceHover.delay=250);await move(b.pixels[0]);await page.waitForTimeout(65);await move(a.pixels[0]);await target(a.id);
 await page.evaluate(()=>faceHover.seen=[]);await page.waitForTimeout(400);
 assert.ok(await page.evaluate(id=>faceHover.seen.every(f=>f.target===id),a.id));
 await move(b.pixels[0]);await target(b.id);await page.screenshot({path:resolve(output,'face-hover.png')});
 report.cases.push('Delayed response cannot replace the face under the current pointer; crossing a genuine face boundary selects the new face');
 await blank();await move(a.pixels[0]);await page.waitForTimeout(65);await blank();await page.waitForTimeout(350);
 assert.equal(await page.evaluate(()=>faceHover.view.fiveSelectionFeedback.target),null);
 await move(a.pixels[0]);await target(a.id);await page.mouse.down({button:'middle'});await page.waitForTimeout(40);
 assert.equal(await page.evaluate(()=>faceHover.view.fiveSelectionFeedback.target),null);await page.mouse.up({button:'middle'});
 await move(a.pixels[1]);await page.waitForTimeout(65);
 await page.evaluate(()=>{faceHover.view.fiveAxisExtractionPick=null;faceHover.sync();});await page.waitForTimeout(350);
 assert.equal(await page.evaluate(()=>faceHover.view.fiveSelectionFeedback.target),null);
 assert.equal(await page.locator('canvas').first().getAttribute('data-five-pick'),null);
 report.cases.push('Leaving canvas, camera gesture and ending picking clear feedback; late responses cannot restore it');
 // Even an identical source mesh/triangle must reject an older scene's result.
 await blank();await page.evaluate(async()=>{const {extractionDraft}=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');faceHover.view.fiveAxisExtractionPick=extractionDraft(faceHover.view).extraction;faceHover.sync();});
 await move(a.pixels[0]);await page.waitForTimeout(65);
 await page.evaluate(()=>{faceHover.view.scene.domain.expected.mutationSequence='99';faceHover.sync();faceHover.delay=600;});
 await move(a.pixels[1]);await page.waitForTimeout(350);
 assert.equal(await page.evaluate(()=>faceHover.view.fiveSelectionFeedback.target),null,'Old scene response must not be accepted for a new request on the same triangle ranges');
 await page.evaluate(()=>{faceHover.stop=true;faceHover.view.disposed=true;faceHover.sync();});
 report.cases.push('Scene version change rejects delayed old topology even when the mesh and face triangle ranges coincide');
 assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.failure=error.stack;if(page)await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser?.close();await nativeBrowser?.close();}
