import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,copyFileSync,writeFileSync} from 'node:fs';
import {chromium} from 'playwright';
import {PNG} from 'pngjs';
const output=resolve('output/tests/locating-marker'),base=pathToFileURL(resolve('output/tests/job-sequence/cef-runtime')+'/').href;
mkdirSync(output,{recursive:true});
const browser=await chromium.connectOverCDP('http://127.0.0.1:9476'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');
page.setDefaultTimeout(15000);await page.setViewportSize({width:1600,height:950});
const report={passed:false,standaloneCef:true,realMouse:true,measurements:[],errors:[]};page.on('pageerror',e=>report.errors.push(e.message));
const frame=()=>page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fixtureRequest);
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 const path=resolve(output,'定位点缩放.i5cam');copyFileSync(resolve('output/tests/preparation-tree/four-groups/夹具复制.i5cam'),path);await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),path);
 await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);await idle();await page.locator('[data-tab-id="workpiece"]').click();await idle();
 await command('five.tool.registration');await command('five.locating.pick:0');await page.waitForFunction(()=>fixture.view.sceneSnapping?.catalogs.has(fixture.view.scene.activePlacementId),null,{timeout:60000});
 const first=await page.evaluate(async base=>{
  const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),{sceneSnapCandidate}=await import(base+'apps/five-axis-cam/webpage/sceneSnapping.mjs'),v=fixture.view,vp=v.viewport,id=v.scene.activePlacementId,o=vp.sceneObjects.get(id),r=vp.renderer.domElement.getBoundingClientRect();
  for(const f of v.sceneSnapping.catalogs.get(id).features.filter(f=>['endpoint','midpoint','center','datum-point'].includes(f.mode))){
   const q=new T.Vector3(...f.point).applyMatrix4(o.matrixWorld).project(vp.camera),x=r.x+(q.x+1)*r.width/2,y=r.y+(1-q.y)*r.height/2;
   if(Math.abs(q.z)>1||document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;
   if(v.fiveLocating.points.some(p=>{if(!p?.point)return false;const q=new T.Vector3(...p.point).applyMatrix4(o.matrixWorld).project(vp.camera);return Math.hypot(r.x+(q.x+1)*r.width/2-x,r.y+(1-q.y)*r.height/2-y)<40;}))continue;
   const c=sceneSnapCandidate({sceneProxy:v.viewportSceneProxy},v,{objectId:id,allowedModes:['endpoint','midpoint','center','sphere-center','datum-point','point']},null,{clientX:x,clientY:y});if(c?.result)return {x,y};
  }throw Error('No visible unlabelled native snap point');
 },base);
 await page.mouse.move(first.x,first.y);await page.waitForFunction(()=>!!fixture.view.fiveLocating.hover);
 await page.evaluate(()=>{fixture.reference=structuredClone(fixture.view.fiveLocating.hover);fixture.camera=fixture.view.viewport.getCameraState();});
 for(const projectionMode of ['perspective','orthographic'])for(const zoom of [1,.08,.01]){
  const p=await page.evaluate(async({base,projectionMode,zoom})=>{const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view,vp=v.viewport,o=vp.sceneObjects.get(v.scene.activePlacementId),point=new T.Vector3(...fixture.reference.point).applyMatrix4(o.matrixWorld);vp.setCameraState({...fixture.camera,target:{x:point.x,y:point.y,z:point.z},radius:fixture.camera.radius*zoom,projectionMode});const q=point.project(vp.camera),r=vp.renderer.domElement.getBoundingClientRect();return {x:r.x+(q.x+1)*r.width/2,y:r.y+(1-q.y)*r.height/2};},{base,projectionMode,zoom});
  await frame();await page.mouse.move(p.x+22,p.y+22);await frame();await page.mouse.move(p.x,p.y);await page.waitForFunction(()=>!!fixture.view.fiveLocating.hover);await frame();
  assert.deepEqual(await page.evaluate(()=>fixture.view.fiveLocating.hover.query),await page.evaluate(()=>fixture.reference.query));
  const at=await page.evaluate(async base=>{const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view,m=v.fiveLocating.layer.children.find(o=>o.userData.locatingPoint==='hover'),q=new T.Vector3().fromBufferAttribute(m.geometry.attributes.position,0).applyMatrix4(m.matrixWorld).project(v.viewport.camera),r=v.viewport.renderer.domElement.getBoundingClientRect();return {x:r.x+(q.x+1)*r.width/2,y:r.y+(1-q.y)*r.height/2};},base);
  const clip={x:Math.round(at.x)-22,y:Math.round(at.y)-22,width:44,height:44},file=resolve(output,projectionMode+'-'+zoom+'.png'),png=PNG.sync.read(await page.screenshot({path:file,clip,scale:'css'})),hits=[];
  for(let y=0;y<png.height;y++)for(let x=0;x<png.width;x++){const i=(y*png.width+x)*4;if(Math.abs(png.data[i]-96)<18&&Math.abs(png.data[i+1]-223)<18&&Math.abs(png.data[i+2]-204)<18)hits.push({x,y});}
  assert.ok(hits.length>10,'Visible turquoise ring');const width=Math.max(...hits.map(p=>p.x))-Math.min(...hits.map(p=>p.x))+1,height=Math.max(...hits.map(p=>p.y))-Math.min(...hits.map(p=>p.y))+1;
  assert.ok(width>=9&&width<=14&&height>=9&&height<=14,`${projectionMode}/${zoom}: actual rendered ring ${width} x ${height} CSS pixels`);
  assert.equal(hits.some(h=>Math.abs(h.x-22)<2&&Math.abs(h.y-22)<2),false,'Ring leaves the CAD point visible');
  report.measurements.push({projectionMode,zoom,width,height});console.log(JSON.stringify(report.measurements.at(-1)));
 }
 await page.screenshot({path:resolve(output,'close-zoom-small-marker.png')});
 const query=await page.evaluate(()=>fixture.view.fiveLocating.hover.query),p=await page.evaluate(()=>{const r=fixture.view.viewport.renderer.domElement.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};});
 await page.mouse.click(p.x,p.y);await idle();assert.deepEqual(await page.evaluate(()=>fixture.view.fiveLocating.points[0].query),query);
 assert.equal(await page.evaluate(()=>fixture.view.fiveLocating.layer.children.every(o=>o.isPoints&&o.material.size<=12&&!o.material.sizeAttenuation)),true);
 await page.keyboard.press('Escape');await idle();assert.equal(await page.evaluate(()=>fixture.view.fiveLocating.hover),null);await command('five.locating.close');assert.equal(await page.evaluate(()=>fixture.view.fiveLocating.layer),null);
 assert.deepEqual(report.errors,[]);report.passed=true;report.clickMatchesHover=true;report.markersRemovedOnClose=true;writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log('Native locating marker zoom and click checks passed');
}catch(error){report.failure=error.stack;writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await page.screenshot({path:resolve(output,'failure.png')});throw error;}
finally{await browser.close();}
