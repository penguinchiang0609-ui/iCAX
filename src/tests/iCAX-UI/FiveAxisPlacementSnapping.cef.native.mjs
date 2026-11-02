import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,writeFileSync} from 'node:fs';
import {chromium} from 'playwright';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
const output=resolve('output/tests/placement-snapping'),base=pathToFileURL(resolve('output/tests/job-sequence/cef-runtime')+'/').href;
mkdirSync(output,{recursive:true});
const browser=await chromium.connectOverCDP('http://127.0.0.1:9476'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');
assert.ok(page);page.setDefaultTimeout(45000);await page.setViewportSize({width:1600,height:950});
const report={passed:false,standaloneCef:true,realMouse:true,cases:[],errors:[]};page.on('pageerror',error=>report.errors.push(error.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fiveAxisImportActive&&!fixture.view.fixtureRequest);
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}));
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
const invoke=(method,payload)=>page.evaluate(({method,payload})=>fixture.view.viewportSceneProxy.invoke(method,payload),{method,payload});
const matrix=()=>page.evaluate(()=>fixture.view.viewport.sceneObjects.get(fixture.view.scene.activePlacementId).matrix.clone().transpose().toArray());
const snapshot=()=>page.evaluate(()=>{const m=fixture.view.viewport.transformManipulator;return {snap:m.snap,marker:m.snapMarker.visible,angle:m.control.rotationAngle,axis:m.control.axis,pointer:m.pointerId,proxy:m.proxy.position.toArray(),start:m.start?.position.toArray(),options:m.snapOptions,draft:fixture.view.fivePlacement?.value};});
const near=(a,b)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-5,`${a} != ${b}`));
let placement;
async function reset(position=[23,17,4],local=false){
 const r=Math.SQRT1_2,m=local?[r,-r,0,position[0],r,r,0,position[1],0,0,1,position[2],0,0,0,1]:[1,0,0,position[0],0,1,0,position[1],0,0,1,position[2],0,0,0,1];
 await invoke('CamPlacement.SetTransform',{commandId:crypto.randomUUID(),expected:(await read()).expected,placementId:placement.id,setupId:placement.setupId,transform:m});await command('five.refresh');await command('five.setup.activate:'+placement.id);await command('five.tool.translate');
 await page.locator('button[data-transform-space="'+(local?'local':'world')+'"]').click();await idle();
}
const driver=gizmoDriver(page);
async function commitDrag(){
 const wanted=await matrix();await page.mouse.up();
 await page.waitForFunction(wanted=>!fixture.view.pending&&fixture.view.scene.domain.placements[0].transform.every((v,i)=>Math.abs(v-wanted[i])<1e-5),wanted);await idle();
}
async function translate(axis,offset){
 const handle=await driver.locate(axis);
 await page.mouse.move(handle.x,handle.y);await page.mouse.down();
 const target=await page.evaluate(offset=>{const vp=fixture.view.viewport,m=vp.transformManipulator,r=vp.renderer.domElement.getBoundingClientRect(),p=m.control.pointStart.clone().add(m.start.position).add(m.proxy.position.clone().set(...offset)).project(vp.camera);return {x:r.left+(p.x+1)*r.width/2,y:r.top+(1-p.y)*r.height/2};},offset);
 await page.mouse.move(target.x,target.y,{steps:12});return target;
}
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(path=>__icaxAppShell.createProject({projectName:'放置吸附验证',projectPath:path,productId:'icax.five-axis-cam'}),resolve(output,Date.now()+'-放置吸附.i5cam'));
 await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);
 await page.waitForFunction(()=>fixture.view.scene?.domain?.expected&&__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId&&!__icaxAppShell.getState().pendingCount);await idle();
 await page.evaluate(path=>__icaxWorkbench.importWorkpiece(path),resolve('TestData/FA/刀路/刀路/AI构造刀路/光滑奖杯-杯身3方位孔-下载高脚杯.step'));await idle();
 await page.waitForFunction(()=>fixture.view.viewport?.sceneObjects.get(fixture.view.scene.activePlacementId)?.geometry);
 placement=(await read()).placements[0];assert.equal(await page.evaluate(()=>fixture.view.viewport.getGridState().spacing),50);
 for(const projection of ['perspective','orthographic']){
  await page.locator('button[data-projection-mode="'+projection+'"]').click();await reset();
  const before=(await read()).placements[0].transform;
  const target=await translate('X',[26.4,0,0]);let s=await snapshot();assert.equal(s.snap?.kind,'grid',JSON.stringify(s));assert.ok(s.marker);near((await matrix()).filter((_,i)=>[3,7,11].includes(i)),[50,17,4]);near((await read()).placements[0].transform,before);
  await page.keyboard.down('Alt');await page.mouse.move(target.x+.3,target.y+.3);assert.equal((await snapshot()).snap,null);assert.equal((await snapshot()).marker,false);assert.ok(Math.abs((await matrix())[3]-50)>1e-4);
  await page.keyboard.up('Alt');await page.mouse.move(target.x,target.y);await page.waitForFunction(()=>fixture.view.viewport.transformManipulator.snap?.kind==='grid');
 await commitDrag();near((await read()).placements[0].transform.filter((_,i)=>[3,7,11].includes(i)),[50,17,4]);await page.screenshot({path:resolve(output,projection+'-grid-saved.png')});
  await reset([23,0,0]);await translate('X',[-22.4,0,0]);s=await snapshot();assert.equal(s.snap?.kind,'origin',JSON.stringify(s));await commitDrag();near((await read()).placements[0].transform.filter((_,i)=>[3,7,11].includes(i)),[0,0,0]);
  await reset();await translate('XY',[26.4,33.6,0]);s=await snapshot();assert.equal(s.snap?.kind,'grid',JSON.stringify(s));await commitDrag();near((await read()).placements[0].transform.filter((_,i)=>[3,7,11].includes(i)),[50,50,4]);
  report.cases.push(projection+': actual axis/plane drags snap the model origin to the rendered 50 mm grid and world origin; previews remain transient; Alt releases snapping; mouse-up persists exact native transforms');
 }
 await reset([23,17,4],true);await translate('X',[26.4,26.4,0]);let moved=await matrix();assert.ok(Math.abs(moved[3]-50)<1e-5);assert.ok(Math.abs((moved[3]-23)-(moved[7]-17))<1e-5);assert.equal(moved[11],4);await page.keyboard.press('Escape');await commitDrag();near((await read()).placements[0].transform.filter((_,i)=>[3,7,11].includes(i)),[23,17,4]);
 report.cases.push('A rotated local-axis drag reaches the world grid without leaving its constrained local direction; Escape cancels snapping without changing saved state');
 await reset([23,17,4]);await command('five.tool.rotate');
 const handle=await driver.locate('E'),center=await page.evaluate(()=>{const vp=fixture.view.viewport,p=vp.transformManipulator.proxy.position.clone().project(vp.camera),r=vp.renderer.domElement.getBoundingClientRect();return {x:r.left+(p.x+1)*r.width/2,y:r.top+(1-p.y)*r.height/2};});
 const x=handle.x-center.x,y=handle.y-center.y;await page.mouse.move(handle.x,handle.y);await page.mouse.down();
 for(let i=1;i<=15;i++){const a=89*Math.PI/180*i/15;await page.mouse.move(center.x+x*Math.cos(a)-y*Math.sin(a),center.y+x*Math.sin(a)+y*Math.cos(a));}
 let s=await snapshot();assert.equal(s.snap?.kind,'angle',JSON.stringify(s));assert.ok(Math.abs(Math.abs(s.angle)-Math.PI/2)<1e-8);await page.screenshot({path:resolve(output,'rotation-snap.png')});const preview=await matrix();await commitDrag();near((await read()).placements[0].transform,preview);
 await command('five.tool.translate');const numericBefore=(await read()).placements[0].transform;await driver.pick('X');await driver.numeric().fill('7.25');await driver.numeric().press('Enter');await idle();assert.equal((await snapshot()).snap,null);near((await read()).placements[0].transform,numericBefore.map((v,i)=>i===3?v+7.25:v));
 report.cases.push('Actual screen-ring rotation snaps near 90 degrees and commits the exact preview; typed 7.25 mm remains unquantized');
 let task=await invoke('CamFixture.Import',{commandId:crypto.randomUUID(),expected:(await read()).expected,setupId:placement.setupId,sourcePath:resolve('TestData/automotive-stamping/mercedes-benz-t-node/Test-support-block.step')});
 while(task.status==='Running'){await new Promise(r=>setTimeout(r,50));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',JSON.stringify(task));
 await command('five.refresh');const unit=(await read()).fixtureUnits[0];assert.ok(unit);
 await invoke('CamFixture.UnitUpdate',{commandId:crypto.randomUUID(),expected:(await read()).expected,setupId:placement.setupId,unitId:unit.id,transform:[1,0,0,23,0,1,0,17,0,0,1,4,0,0,0,1]});await command('five.refresh');await command('five.preparation.unit:'+unit.id);
 await translate('X',[26.4,0,0]);assert.equal((await snapshot()).snap?.kind,'grid');await page.mouse.up();
 await page.waitForFunction(id=>!fixture.view.pending&&Math.abs(fixture.view.scene.domain.fixtureUnits.find(u=>u.id===id).transform[3]-50)<1e-5,unit.id);
 near((await read()).fixtureUnits.find(u=>u.id===unit.id).transform.filter((_,i)=>[3,7,11].includes(i)),[50,17,4]);
 await command('five.fixture.move-mode:rotate');const fixtureRing=await driver.locate('E'),fixtureCenter=await page.evaluate(()=>{const vp=fixture.view.viewport,p=vp.transformManipulator.proxy.position.clone().project(vp.camera),r=vp.renderer.domElement.getBoundingClientRect();return{x:r.left+(p.x+1)*r.width/2,y:r.top+(1-p.y)*r.height/2};});
 const fx=fixtureRing.x-fixtureCenter.x,fy=fixtureRing.y-fixtureCenter.y;await page.mouse.move(fixtureRing.x,fixtureRing.y);await page.mouse.down();for(let i=1;i<=15;i++){const a=89*Math.PI/180*i/15;await page.mouse.move(fixtureCenter.x+fx*Math.cos(a)-fy*Math.sin(a),fixtureCenter.y+fx*Math.sin(a)+fy*Math.cos(a));}
 assert.equal((await snapshot()).snap?.kind,'angle');const fixturePreview=await page.evaluate(()=>fixture.view.fiveFixtureGizmo.matrix.clone().transpose().toArray());await page.mouse.up();
 await page.waitForFunction(({id,wanted})=>!fixture.view.pending&&fixture.view.scene.domain.fixtureUnits.find(u=>u.id===id).transform.every((v,i)=>Math.abs(v-wanted[i])<1e-5),{id:unit.id,wanted:fixturePreview});
 near((await read()).fixtureUnits.find(u=>u.id===unit.id).transform,fixturePreview);
 await invoke('CamFixture.UnitUpdate',{commandId:crypto.randomUUID(),expected:(await read()).expected,setupId:placement.setupId,unitId:unit.id,transform:[1,0,0,50,0,1,0,17,0,0,1,4,0,0,0,1]});await command('five.refresh');await command('five.preparation.unit:'+unit.id);await command('five.fixture.move-mode:rotate');
 await driver.drag('Z',50,false);const axisPreview=await page.evaluate(()=>fixture.view.fiveFixtureGizmo.matrix.clone().transpose().toArray());near([axisPreview[2],axisPreview[6],axisPreview[8],axisPreview[9],axisPreview[10]],[0,0,0,0,1]);assert.ok(Math.abs(axisPreview[1])>.01);await page.mouse.up();
 await page.waitForFunction(({id,wanted})=>!fixture.view.pending&&fixture.view.scene.domain.fixtureUnits.find(u=>u.id===id).transform.every((v,i)=>Math.abs(v-wanted[i])<1e-5),{id:unit.id,wanted:axisPreview});
 report.cases.push('An actual imported fixture unit shares grid/origin and angular snapping; native persistence moves/rotates its complete assembly, and the Z ring preserves the selected world axis');
 await page.evaluate(path=>{(globalThis.icax??globalThis.icaxNativeBridge).saveFileDialog=async()=>path;},resolve(output,'放置吸附.i5cam'));await command('app.save');
 writeFileSync(resolve(output,'expected-state.json'),JSON.stringify({projectPath:resolve(output,'放置吸附.i5cam'),state:await read()},null,2));
 assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.failure=error.stack;process.exitCode=1;await page.mouse.up().catch(()=>{});await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
