import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,writeFileSync} from 'node:fs';
import {chromium} from 'playwright';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
import {verifyViewCubeDrag} from './FiveAxisViewCubeDrag.native.browser.mjs';
const output=resolve('output/tests/orbit-pivot'),base=pathToFileURL(resolve('output/tests/job-sequence/cef-runtime')+'/').href;
mkdirSync(output,{recursive:true});
const browser=await chromium.connectOverCDP('http://127.0.0.1:9476'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');
assert.ok(page);page.setDefaultTimeout(45000);await page.setViewportSize({width:1600,height:950});
const report={passed:false,standaloneCef:true,realMouse:true,cases:[],errors:[]};page.on('pageerror',error=>report.errors.push(error.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fiveAxisImportActive);
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}));
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
const invoke=(method,payload)=>page.evaluate(({method,payload})=>fixture.view.viewportSceneProxy.invoke(method,payload),{method,payload});
const near=(a,b,epsilon=1e-7)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<epsilon,`${a} != ${b}`));
const camera=()=>page.evaluate(()=>fixture.view.viewport.getCameraState());
const orbitPoint=()=>page.evaluate(()=>fixture.view.viewport.navigation.orbitPivot);
// Screen coordinates are X/Y. Adaptive clipping can change NDC depth while the
// same world point stays at exactly the same screen position and eye distance.
const view=point=>page.evaluate(async point=>{const T=await import(fixture.base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),vp=fixture.view.viewport,p=new T.Vector3(...point);return{pixel:p.clone().project(vp.camera).toArray().slice(0,2),distance:p.distanceTo(vp.camera.position),camera:vp.getCameraState()};},point);
async function create(){
 await page.evaluate(path=>__icaxAppShell.createProject({projectName:'视野旋转中心',projectPath:path,productId:'icax.five-axis-cam'}),resolve(output,Date.now()+'-视野旋转中心.i5cam'));
 await page.waitForFunction(()=>__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId&&!__icaxAppShell.getState().pendingCount);
 await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId),base};},base);await idle();
}
async function trace(){await page.evaluate(()=>{const vp=fixture.view.viewport,capture=vp.captureOrbitPivot.bind(vp);fixture.captures=[];vp.captureOrbitPivot=event=>{const start=performance.now(),pivot=capture(event);fixture.captures.push({pivot,ms:performance.now()-start});return pivot;};});}
async function target(hit){return page.evaluate(async hit=>{
 const T=await import(fixture.base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),vp=fixture.view.viewport,c=vp.renderer.domElement,r=c.getBoundingClientRect(),ray=new T.Raycaster(),objects=[...vp.sceneObjects.values()].filter(m=>m.visible&&m.isMesh),target=vp.cameraState.target;
 let best=null;for(let iy=1;iy<13;iy++)for(let ix=1;ix<17;ix++){
  const x=r.left+r.width*ix/17,y=r.top+r.height*iy/13;if(document.elementFromPoint(x,y)!==c)continue;
  ray.setFromCamera({x:(x-r.left)/r.width*2-1,y:1-(y-r.top)/r.height*2},vp.camera);const hits=ray.intersectObjects(objects,false);
  if(hit&&hits.length){const score=hits[0].point.distanceTo(target);if(!best||score>best.score)best={x,y,score};}
  if(!hit&&!hits.length&&x<r.left+r.width*.2&&y>r.top+r.height*.7)return{x,y};
 }return best;
},hit);}
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');await create();
 await page.evaluate(path=>__icaxWorkbench.importWorkpiece(path),resolve('TestData/FA/刀路/刀路/AI构造刀路/光滑奖杯-杯身3方位孔-下载高脚杯.step'));await idle();await page.waitForFunction(()=>fixture.view.viewport?.sceneObjects.get(fixture.view.scene.activePlacementId)?.geometry);
 await command('five.tool.select');const nativeBefore=await read();await trace();
 for(const projection of ['perspective','orthographic']){
  await page.locator('button[data-projection-mode="'+projection+'"]').click();await page.evaluate(()=>{fixture.view.viewport.fitViewToViewport();fixture.cameraStart=fixture.view.viewport.getCameraState();});
  const hit=await target(true);assert.ok(hit);const before=await camera(),count=await page.evaluate(()=>fixture.captures.length);await page.mouse.move(hit.x,hit.y);await page.mouse.down({button:'middle'});
  assert.deepEqual(await camera(),before,'press must not jump or reframe');const pivot=await orbitPoint();assert.equal(pivot.source,'mesh');assert.ok(pivot.hitCount>=1&&pivot.hitCount<=9);near(pivot.point,pivot.samples[0].map((_,i)=>pivot.samples.reduce((n,p)=>n+p[i],0)/pivot.hitCount));
  const initial=await view(pivot.point);
  for(const [dx,dy] of [[35,20],[95,40],[-65,-20]]){await page.mouse.move(hit.x+dx,hit.y+dy,{steps:8});assert.deepEqual(await orbitPoint(),pivot);const after=await view(pivot.point);near(after.pixel,initial.pixel);assert.ok(Math.abs(after.distance-initial.distance)<1e-6);assert.equal(after.camera.radius,before.radius);}
  assert.notEqual((await camera()).theta,before.theta);assert.equal(await page.evaluate(()=>fixture.captures.length),count+1);await page.screenshot({path:resolve(output,projection+'-mesh-orbit.png')});await page.mouse.up({button:'middle'});assert.equal(await orbitPoint(),null);
  const blank=await target(false);assert.ok(blank);const fallback=await page.evaluate(()=>fixture.view.viewport.options.getOrbitFallbackCenter()),beforeBlank=await camera();await page.mouse.move(blank.x,blank.y);await page.mouse.down({button:'middle'});
  const fallbackPivot=await orbitPoint();assert.equal(fallbackPivot.source,'workpiece');near(fallbackPivot.point,fallback);assert.deepEqual(await camera(),beforeBlank);const fallbackView=await view(fallback);
  await page.mouse.move(hit.x,hit.y,{steps:12});assert.deepEqual(await orbitPoint(),fallbackPivot);near((await view(fallback)).pixel,fallbackView.pixel);await page.mouse.up({button:'middle'});
  const panStart=await camera(),panCount=await page.evaluate(()=>fixture.captures.length);await page.keyboard.down('Control');await page.mouse.move(blank.x,blank.y);await page.mouse.down({button:'middle'});await page.mouse.move(blank.x+30,blank.y+20,{steps:8});await page.mouse.up({button:'middle'});await page.keyboard.up('Control');
  assert.equal(await page.evaluate(()=>fixture.captures.length),panCount);const panned=await camera();assert.notDeepEqual(panned.target,panStart.target);for(const key of ['theta','phi','radius'])assert.equal(panned[key],panStart[key]);
  report.cases.push(projection+': middle press captures the nearest-mesh ray-bundle centroid once; blank press uses workpiece center; neither press reframes; the pivot, its screen position and eye distance stay fixed throughout orbit; Ctrl-middle remains pan');
 }
 assert.deepEqual(await read(),nativeBefore);
 const count=await page.evaluate(()=>fixture.captures.length),cube=page.locator('.cam-viewcube'),bounds=await cube.boundingBox();assert.ok(bounds);const cubeStart=await camera();await page.mouse.move(bounds.x+bounds.width*.55,bounds.y+bounds.height*.5);await page.mouse.down();assert.deepEqual(await camera(),cubeStart);const captured=await page.evaluate(()=>fixture.captures.at(-1).pivot),projected=await view(captured.point);
 await page.mouse.move(bounds.x+bounds.width*.55+45,bounds.y+bounds.height*.5+22,{steps:8});near((await view(captured.point)).pixel,projected.pixel);assert.equal(await page.evaluate(()=>fixture.captures.length),count+1);await page.keyboard.press('Escape');await page.mouse.up();assert.deepEqual(await camera(),cubeStart);
 report.cases.push('View-cube left dragging also captures a fixed center once and Escape restores the complete original camera pose');
 report.cases.push(...await verifyViewCubeDrag({page,read}));
 await page.evaluate(()=>fixture.view.viewport.setCameraState({...fixture.view.viewport.getCameraState(),phi:1.05,theta:.4}));
 const ring=await page.locator('[data-cam-compass]').evaluate(n=>{const m=n.ownerSVGElement.getScreenCTM(),p=new DOMPoint(+n.dataset.cx,+n.dataset.cy).matrixTransform(m);return{x:p.x,y:p.y,rx:+n.dataset.rx*m.a,ry:+n.dataset.ry*m.d,sign:+n.dataset.sign};}),compassStart=await camera(),compassCount=await page.evaluate(()=>fixture.captures.length);
 await page.mouse.move(ring.x,ring.y+ring.ry);await page.mouse.down();const compassPivot=await page.evaluate(()=>fixture.captures.at(-1).pivot),compassProjection=await view(compassPivot.point);
 for(let i=1;i<=12;i++){const a=Math.PI/2+Math.PI/3*i/12;await page.mouse.move(ring.x+ring.rx*Math.cos(a),ring.y+ring.ry*Math.sin(a));}
 near((await view(compassPivot.point)).pixel,compassProjection.pixel);assert.equal((await camera()).phi,compassStart.phi);assert.equal((await camera()).radius,compassStart.radius);assert.equal(await page.evaluate(()=>fixture.captures.length),compassCount+1);assert.ok(Math.abs((await camera()).theta-compassStart.theta-ring.sign*Math.PI/3)<.035);await page.keyboard.press('Escape');await page.mouse.up();assert.deepEqual(await camera(),compassStart);
 report.cases.push('Compass dragging captures once, keeps the pivot screen position and pitch/zoom, and cancels to the original view');
 const placement=(await read()).placements[0];await command('five.setup.activate:'+placement.id);await command('five.tool.translate');const handle=await gizmoDriver(page).locate('X'),beforeHandle=await camera(),handleCount=await page.evaluate(()=>fixture.captures.length);await page.mouse.click(handle.x,handle.y);await idle();assert.deepEqual(await camera(),beforeHandle);assert.equal(await page.evaluate(()=>fixture.captures.length),handleCount);assert.deepEqual(await read(),nativeBefore);
 report.cases.push('Left handle selection remains object editing, performs no orbit sampling and preserves native machining data');
 const driver=gizmoDriver(page);await driver.pick('X');await driver.numeric().fill('7.25');const draft=await page.evaluate(()=>({matrix:fixture.view.viewport.sceneObjects.get(fixture.view.scene.activePlacementId).matrix.toArray(),value:fixture.view.fivePlacement.value})),draftCamera=await camera(),draftHit=await target(true);await page.mouse.move(draftHit.x,draftHit.y);await page.mouse.down({button:'middle'});await page.mouse.move(draftHit.x+40,draftHit.y+20,{steps:8});await page.mouse.up({button:'middle'});
 assert.notDeepEqual(await camera(),draftCamera);assert.deepEqual(await page.evaluate(()=>({matrix:fixture.view.viewport.sceneObjects.get(fixture.view.scene.activePlacementId).matrix.toArray(),value:fixture.view.fivePlacement.value})),draft);assert.deepEqual(await read(),nativeBefore);await command('five.tool.select');
 report.cases.push('Middle orbit preserves the live 7.25 mm transform draft and leaves native workpiece data unchanged');
 let imported=await invoke('CamFixture.Import',{commandId:crypto.randomUUID(),expected:(await read()).expected,setupId:placement.setupId,sourcePath:resolve('TestData/automotive-stamping/mercedes-benz-t-node/Test-support-block.step')});
 while(imported.status==='Running'){await new Promise(r=>setTimeout(r,50));imported=await invoke('CamPlan.QueryStatus',{epoch:imported.epoch,taskId:imported.taskId});}assert.equal(imported.status,'Completed');await command('five.refresh');const unit=(await read()).fixtureUnits[0];
 await invoke('CamFixture.UnitUpdate',{commandId:crypto.randomUUID(),expected:(await read()).expected,setupId:placement.setupId,unitId:unit.id,transform:[1,0,0,1000,0,1,0,0,0,0,1,0,0,0,0,1]});await command('five.refresh');await command('five.tool.select');
 const fallback=await page.evaluate(()=>fixture.view.viewport.options.getOrbitFallbackCenter()),blank=await target(false);assert.ok(blank);await page.mouse.move(blank.x,blank.y);await page.mouse.down({button:'middle'});assert.equal((await orbitPoint()).source,'workpiece');near((await orbitPoint()).point,fallback);await page.mouse.up({button:'middle'});report.cases.push('A distant imported fixture does not shift the workpiece-center fallback');
 await create();await trace();const canvas=await page.locator('.icax-three-viewport canvas').first().boundingBox(),empty=await camera();await page.mouse.move(canvas.x+canvas.width*.5,canvas.y+canvas.height*.3);await page.mouse.down({button:'middle'});assert.equal((await orbitPoint()).source,'view');await page.mouse.move(canvas.x+canvas.width*.5+30,canvas.y+canvas.height*.3+15,{steps:6});await page.mouse.up({button:'middle'});assert.ok(Number.isFinite((await camera()).theta));assert.equal((await camera()).radius,empty.radius);
 report.cases.push('An empty native project retains a finite view-center orbit without model-dependent failures');
 assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.failure=error.stack;process.exitCode=1;await page.mouse.up({button:'middle'}).catch(()=>{});await page.mouse.up().catch(()=>{});await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
