import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,copyFileSync,writeFileSync} from 'node:fs';
import {chromium} from 'playwright';
import {frameAlignmentDriver} from './FiveAxisFrameAlignment.native.browser.mjs';
const output=resolve('output/tests/selection-feedback'),base=pathToFileURL(resolve('output/tests/job-sequence/cef-runtime')+'/').href;
mkdirSync(output,{recursive:true});
const browser=await chromium.connectOverCDP('http://127.0.0.1:9476'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');
page.setDefaultTimeout(30000);await page.setViewportSize({width:1600,height:950});
const report={passed:false,standaloneCef:true,realMouse:true,cases:[],errors:[]};page.on('pageerror',e=>report.errors.push(e.message));
page.on('console',message=>{if(message.type()==='error'&&/Shader Error|VALIDATE_STATUS|GL_INVALID_OPERATION/.test(message.text()))report.errors.push(message.text());});
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fixtureRequest&&!fixture.view.fiveSetupRequest&&!fixture.view.fiveAlignment?.picking&&!fixture.view.fiveAlignment?.solving);
const command=async id=>{console.log('Command '+id);await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
const frame=()=>page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
const canvas=page.locator('.cam-render-viewport canvas').first();
const cursor=async expected=>assert.match(await canvas.evaluate(e=>getComputedStyle(e).cursor),new RegExp(expected+'-pick-cursor.svg'));
const open=async source=>{const path=resolve(output,source.split('/').at(-1));copyFileSync(resolve(source),path);await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),path);await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);await idle();await page.locator('[data-tab-id="workpiece"]').click();await idle();};
const blank=async()=>{const box=await canvas.boundingBox();await page.mouse.move(box.x+30,box.y+110);await frame();};
const target=()=>page.evaluate(()=>fixture.view.fiveSelectionFeedback?.target);
const moved=async p=>{await page.mouse.move(p.x,p.y);await frame();};
async function facePixels(){
 return page.evaluate(async base=>{
  const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view,vp=v.viewport,placement=v.scene.activePlacementId,mesh=vp.sceneObjects.get(placement),g=mesh.geometry,p=g.attributes.position,r=vp.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster();
  const faces=v.fixtureDesignFaces||((await v.viewportSceneProxy.invoke('CamPlacement.ReadReferenceFaces',{expected:v.scene.domain.expected,placementId:placement,setupId:v.scene.domain.activeSetupId})).faces);
  mesh.updateWorldMatrix(true,true);vp.camera.updateMatrixWorld(true);const found=[],seen=new Set();
  for(const face of [...faces].sort((a,b)=>+b.count- +a.count)){
   if(seen.has(String(face.faceId)))continue;
   for(let triangle=+face.first;triangle<+face.first+ +face.count;triangle++){
    const q=new T.Vector3();for(let j=0;j<3;j++){const i=g.index?g.index.getX(triangle*3+j):triangle*3+j;q.add(new T.Vector3().fromBufferAttribute(p,i));}q.multiplyScalar(1/3).applyMatrix4(mesh.matrixWorld).project(vp.camera);
    if(Math.abs(q.x)>.9||Math.abs(q.y)>.85||Math.abs(q.z)>1)continue;
    const x=r.x+(q.x+1)/2*r.width,y=r.y+(1-q.y)/2*r.height;if(document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;
    ray.setFromCamera({x:q.x,y:q.y},vp.camera);const h=ray.intersectObjects([...vp.sceneObjects.values()].filter(o=>o.visible),false)[0];
    if(h?.object!==mesh)continue;
    const actual=faces.find(f=>h.faceIndex>=+f.first&&h.faceIndex<+f.first+ +f.count);if(String(actual?.faceId)!==String(face.faceId))continue;
    found.push({x,y,faceId:String(face.faceId),triangles:faces.filter(f=>String(f.faceId)===String(face.faceId)).reduce((n,f)=>n+ +f.count,0)});seen.add(String(face.faceId));break;
   }if(found.length>=6)break;
  }return found;
 },base);
}
async function linePixel(){
 return page.evaluate(async base=>{
  const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),{nearestCutLine}=await import(base+'apps/five-axis-cam/webpage/cutSelection.mjs'),v=fixture.view,vp=v.viewport,r=vp.renderer.domElement.getBoundingClientRect();
  for(const c of v.scene.intentToolpaths.filter(c=>!c.locked&&c.enabled!==false)){
   const source=vp.sceneObjects.get(c.placementId);if(!source)continue;
   for(let i=1;i<c.previewPoints.length;i++){
    const q=new T.Vector3(...c.previewPoints[i-1]).lerp(new T.Vector3(...c.previewPoints[i]),.5).applyMatrix4(source.matrixWorld).project(vp.camera);
    const x=r.x+(q.x+1)*r.width/2,y=r.y+(1-q.y)*r.height/2;
    if(Math.abs(q.z)>1||document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;
    if(nearestCutLine(v,{clientX:x,clientY:y})?.intentId===c.id)return {x,y,intentId:c.id};
   }
  }throw Error('No visible editable cutting line');
 },base);
}
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await open('output/tests/preparation-tree/four-groups/夹具复制.i5cam');
 await command('five.fixture.focus.design');await command('five.fixture.design.pick');await cursor('face');
 const faces=await facePixels();assert.ok(faces.length>=2,'At least two real visible native faces');
 await moved(faces[0]);await page.waitForFunction(id=>fixture.view.fiveSelectionFeedback?.target?.faceId===id,faces[0].faceId);
 assert.equal(await page.evaluate(()=>fixture.view.fiveSelectionFeedback.group.children[0].geometry.attributes.position.count),faces[0].triangles*3);
 const before=await page.evaluate(()=>[...fixture.view.fixtureDesignDraft.supportFaces]);await page.mouse.click(faces[0].x,faces[0].y);await idle();
 assert.equal(await page.evaluate(id=>fixture.view.fixtureDesignDraft.supportFaces.includes(id),faces[0].faceId),!before.includes(faces[0].faceId));
 await moved(faces[1]);await page.waitForFunction(id=>fixture.view.fiveSelectionFeedback?.target?.faceId===id,faces[1].faceId);
 assert.notEqual(await page.evaluate(()=>fixture.view.fiveSelectionFeedback.group.children[0].material.color.getHex()),await page.evaluate(()=>fixture.view.fixtureDesignOverlay.material.color.getHex()));
 await page.screenshot({path:resolve(output,'fixture-face-hover.png')});
 await page.mouse.move(1500,230);await frame();assert.equal(await target(),null);
 await moved(faces[1]);await page.mouse.down({button:'middle'});await frame();assert.equal(await target(),null);await page.mouse.up({button:'middle'});
 await moved(faces[0]);await command('five.fixture.design.pick');assert.equal(await target(),null);assert.equal(await canvas.getAttribute('data-five-pick'),null);
 await command('five.fixture.design.pick');await page.keyboard.press('Escape');await idle();assert.equal(await target(),null);assert.equal(await canvas.getAttribute('data-five-pick'),null);
 await command('five.fixture.design.close');
 report.cases.push('Fixture picking highlights complete native faces including all triangle ranges, differs from selection, clicks toggle exactly the hovered face; leave, orbit, finish and Escape clear hover');

 await command('five.tool.registration');await command('five.locating.pick:0');await cursor('point');
 await page.waitForFunction(()=>fixture.view.sceneSnapping?.catalogs.has(fixture.view.scene.activePlacementId));
 const point=await page.evaluate(async base=>{
  const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),{sceneSnapCandidate}=await import(base+'apps/five-axis-cam/webpage/sceneSnapping.mjs'),v=fixture.view,vp=v.viewport,id=v.scene.activePlacementId,object=vp.sceneObjects.get(id),r=vp.renderer.domElement.getBoundingClientRect();
  for(const f of v.sceneSnapping.catalogs.get(id).features.filter(f=>['endpoint','midpoint','center','sphere-center','datum-point'].includes(f.mode))){
   const q=new T.Vector3(...f.point).applyMatrix4(object.matrixWorld).project(vp.camera),x=r.x+(q.x+1)*r.width/2,y=r.y+(1-q.y)*r.height/2;if(Math.abs(q.z)>1||document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;
   if(v.fiveLocating.points.some(p=>{if(!p?.point)return false;const q=new T.Vector3(...p.point).applyMatrix4(object.matrixWorld).project(vp.camera);return Math.hypot(r.x+(q.x+1)*r.width/2-x,r.y+(1-q.y)*r.height/2-y)<40;}))continue;
   const c=sceneSnapCandidate({sceneProxy:v.viewportSceneProxy},v,{objectId:id,allowedModes:['endpoint','midpoint','center','sphere-center','datum-point','point']},null,{clientX:x,clientY:y});if(c?.result)return {x,y,query:c.result.query};
  }throw Error('No actual point snap visible');
 },base);
 await page.addStyleTag({content:'.hover-interaction-test .cam-context-pane::after,.hover-interaction-test .cam-info-pane::after{content:"";display:block;height:1100px}.hover-interaction-test .five-locating-body{max-height:240px!important}'});
 const positions=await page.evaluate(()=>{document.body.classList.add('hover-interaction-test');const e=document.querySelector('[data-five-locating-field="tolerance"]');fixture.editor=e;fixture.canvas=fixture.view.viewport.renderer.domElement;e.focus({preventScroll:true});e.setSelectionRange(0,2);return ['.cam-context-pane','.cam-info-pane','.five-locating-body'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=30+i*10;return n.scrollTop;});});
 await moved(point);await page.waitForFunction(()=>!!fixture.view.fiveLocating?.hover);const query=await page.evaluate(()=>fixture.view.fiveLocating.hover.query);
 assert.equal(await page.evaluate(()=>fixture.editor===document.activeElement&&fixture.editor.isConnected&&fixture.editor.selectionStart===0&&fixture.editor.selectionEnd===2&&fixture.canvas===fixture.view.viewport.renderer.domElement),true);
 assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-locating-body'].map(s=>document.querySelector(s).scrollTop)),positions);
 await page.evaluate(()=>document.body.classList.remove('hover-interaction-test'));await page.screenshot({path:resolve(output,'point-hover.png')});
 await page.mouse.move(1500,230);await frame();assert.equal(await page.evaluate(()=>fixture.view.fiveLocating.hover),null);
 await moved(point);await page.mouse.click(point.x,point.y);await idle();assert.deepEqual(await page.evaluate(()=>fixture.view.fiveLocating.points[0].query),query);
 await page.keyboard.press('Escape');await idle();assert.equal(await canvas.getAttribute('data-five-pick'),null);await command('five.locating.close');
 report.cases.push('Three-point locating uses the point cursor and real feature hover, clears it on leaving the canvas and stores the exact same query on click');

 await command('five.tool.align');await cursor('face');await page.waitForFunction(()=>fixture.view.sceneSnapping?.catalogs.has(fixture.view.fiveAlignment?.placementId));
 const driver=await frameAlignmentDriver(page,base);await driver.pick('plane',{screenshot:resolve(output,'alignment-face-hover.png')});assert.equal(await canvas.getAttribute('data-five-pick'),null);
 await driver.action('next');await cursor('axis');await driver.pick('direction',{construction:'PlaneU',screenshot:resolve(output,'alignment-axis-hover.png')});
 await driver.action('next');await cursor('point');
 await page.waitForFunction(()=>!fixture.view.sceneSnapping?.presentation?.membersPending);
 const origin=await page.evaluate(async base=>{const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view,vp=v.viewport,p=v.sceneSnapping.presentation.layer.records.find(r=>r.type==='point').feature.point,o=vp.sceneObjects.get(v.fiveAlignment.placementId),q=new T.Vector3(...p).applyMatrix4(o.matrixWorld).project(vp.camera),r=vp.renderer.domElement.getBoundingClientRect();return {x:r.x+(q.x+1)*r.width/2,y:r.y+(1-q.y)*r.height/2};},base);
 await moved(origin);await page.waitForFunction(()=>!!fixture.view.fiveAlignment.hover);await page.screenshot({path:resolve(output,'alignment-point-hover.png')});await page.mouse.click(origin.x,origin.y);await idle();
 assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.valid),true);await command('five.align.close');
 report.cases.push('Coordinate-frame alignment switches face/axis/point cursors by stage, prehighlights actual inferred native references and solves the clicked frame; hover preserves input node/focus/caret and both sidebar/nested scroll positions');

 await page.locator('[data-tab-id="toolpath"]').click();await idle();
 await command('five.tool.manual-extract');await command('five.extraction.mode.Faces');await cursor('face');
 const extractionFaces=await facePixels();await moved(extractionFaces[0]);await page.waitForFunction(()=>fixture.view.fiveSelectionFeedback?.target?.kind==='face');const hovered=await target();
 await page.mouse.click(extractionFaces[0].x,extractionFaces[0].y);await idle();assert.equal(await page.evaluate(id=>fixture.view.fiveAxisExtractionPick.faces.some(f=>String(f.localId)===id),hovered.faceId),true);
 await command('five.extraction.mode.Edges');await cursor('line');
 // Scan visible face pixels using real native ResolveEdge; no synthetic topology.
 let edgePixel;
 for(const p of extractionFaces){await moved(p);try{await page.waitForFunction(()=>fixture.view.fiveSelectionFeedback?.target?.edgeId!=null,null,{timeout:2000});edgePixel=p;break;}catch{}}
 assert.ok(edgePixel,'A real visible CAD edge resolves');const edge=await target();await page.screenshot({path:resolve(output,'edge-hover.png')});
 await page.mouse.click(edgePixel.x,edgePixel.y);await idle();assert.equal(await page.evaluate(id=>fixture.view.fiveAxisExtractionPick.edges.some(e=>e.localId===id),edge.edgeId),true);
 await blank();assert.equal(await target(),null);
 await command('five.extraction.mode.Faces');
 await page.evaluate(()=>{const proxy=fixture.view.viewportSceneProxy;fixture.invoke=proxy.invoke.bind(proxy);proxy.invoke=async(method,...args)=>{const result=await fixture.invoke(method,...args);if(method==='CamCut.ResolveFace')await new Promise(r=>fixture.releaseHover=r);return result;};});
 await moved(extractionFaces[0]);await page.waitForFunction(()=>!!fixture.releaseHover);await command('five.extraction.close');
 await page.evaluate(()=>{fixture.view.viewportSceneProxy.invoke=fixture.invoke;fixture.releaseHover();fixture.releaseHover=null;});await frame();assert.equal(await target(),null);
 report.cases.push('CAM face and edge extraction share exact read-only native requests with clicks, retain selected topology, and use matching face/line cursors');

 await open('output/tests/preparation-tree/four-groups/工序顺序与复制.i5cam');
 const configured=await page.evaluate(()=>{const v=fixture.view,s=v.scene.domain.setups.find(s=>s.generatedGeometry?.operations?.length);return v.scene.domain.placements.find(p=>p.setupId===s.id).id;});await command('five.setup.activate:'+configured);
 await page.locator('[data-tab-id="toolpath"]').click();await idle();
 let line=await linePixel();await cursor('line');await moved(line);await page.waitForFunction(()=>fixture.view.fiveSelectionFeedback?.target?.kind==='line');assert.equal((await target()).intentId,line.intentId);
 await page.mouse.click(line.x,line.y);await idle();assert.equal(await page.evaluate(()=>fixture.view.fiveAxisUI.selectedIntentId),line.intentId);await moved(line);await page.screenshot({path:resolve(output,'line-hover.png')});
 await command('five.tool.split');await cursor('point');line=await linePixel();await moved(line);await page.waitForFunction(()=>fixture.view.fiveSelectionFeedback?.target?.kind==='point');const split=await target();await page.screenshot({path:resolve(output,'curve-point-hover.png')});
 await page.mouse.click(line.x,line.y);await idle();assert.deepEqual(await page.evaluate(()=>fixture.view.fiveSplit.points.at(-1).anchor),split.anchor);await command('five.split.close');
 await command('five.pose-select.toggle');await page.waitForFunction(()=>fixture.view.fiveToolAxisDisplay?.visible&&!fixture.view.fiveToolAxisDisplay?.loading);await cursor('axis');
 const axis=await page.evaluate(async base=>{
  const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),{pickDisplayedToolAxis}=await import(base+'apps/five-axis-cam/webpage/toolAxisPicking.mjs'),v=fixture.view,vp=v.viewport,r=vp.renderer.domElement.getBoundingClientRect();
  for(const group of v.fiveToolAxisDisplay.group.children){const mesh=group.children.find(m=>m.userData.kind==='axis');if(!mesh)continue;const shown=mesh.geometry.attributes.instanceShown;
   for(let i=0;i<mesh.userData.toolAxisSamples.length;i++){if(shown.getX(i)<.5)continue;const s=mesh.userData.toolAxisSamples[i],q=new T.Vector3(...s.point).applyMatrix4(mesh.matrixWorld).project(vp.camera),b=new T.Vector3(...s.point).add(new T.Vector3(...s.direction)).applyMatrix4(mesh.matrixWorld).project(vp.camera),dx=(b.x-q.x)*r.width/2,dy=(q.y-b.y)*r.height/2,n=Math.hypot(dx,dy);if(Math.abs(q.z)>1||n<.01)continue;
    const x=r.x+(q.x+1)*r.width/2+dx/n*12,y=r.y+(1-q.y)*r.height/2+dy/n*12;if(document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;
    const pick=pickDisplayedToolAxis(v,{clientX:x,clientY:y});if(pick)return {x,y,pick};
   }
  }throw Error('No rendered tool axis available');
 },base);
 await moved(axis);await page.waitForFunction(()=>!!fixture.view.fivePoseSelection.hover);assert.equal(await page.evaluate(()=>fixture.view.fivePoseSelection.hover.intentId),axis.pick.intentId);await page.screenshot({path:resolve(output,'axis-hover.png')});
 await page.mouse.click(axis.x,axis.y);await idle();assert.deepEqual(await page.evaluate(()=>fixture.view.fivePoseSelection.selected.sample.anchor),axis.pick.sample.anchor);await page.mouse.move(1500,230);await frame();assert.equal(await page.evaluate(()=>fixture.view.fivePoseSelection.hover),null);
 await command('five.pose-select.exit');
 report.cases.push('Actual toolpath lines, native curve points and rendered tool axes prehighlight the same ID/anchor accepted by actual mouse clicks; four cursors load and restore');
 assert.deepEqual(report.errors,[]);report.passed=true;writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
}catch(error){report.failure=error.stack;writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await page.screenshot({path:resolve(output,'failure.png')});throw error;}
finally{await browser.close();}
