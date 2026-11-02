import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,writeFileSync,readFileSync,existsSync,statSync} from 'node:fs';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/setup-redesign/standalone');mkdirSync(output,{recursive:true});
const runtime=resolve(process.env.ICAX_CEF_RUNTIME),base=pathToFileURL(runtime+'/').href;
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${process.env.ICAX_CEF_PORT||9451}`);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');assert.ok(page);page.setDefaultTimeout(45000);
const report={passed:false,standaloneCef:true,realNativeQueue:true,realResourceBridge:true,cases:[],errors:[],fileDialogs:'paths supplied to native dialog bridge for deterministic acceptance'};page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fiveAxisImportActive);
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
const tab=async id=>{await page.locator(`[data-tab-id="${id}"]`).click();await idle();};
const command=async id=>{await page.locator(`[data-command-id="${id}"]`).click();await idle();};
const click=async id=>{await page.locator(`[data-five-command="${id}"]`).last().click();await idle();};
const projectPath=resolve(output,'round-cover-setups.i5cam');
const bind=async()=>{await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');const view=getProjectView(__icaxAppShell.getState().activeProjectId);window.fixture={view,context:{sceneProxy:view.viewportSceneProxy}};},base);await idle();};
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 const created=await page.evaluate(p=>__icaxAppShell.createProject({projectName:'圆盖两次装夹验收',projectPath:p,productId:'icax.five-axis-cam'}),projectPath);
 await page.waitForFunction(id=>window.__icaxWorkbench?.currentProjectId===id&&__icaxAppShell.getState().activeProjectId===id,created.activeProjectId);await bind();
 await page.evaluate(p=>__icaxWorkbench.importWorkpiece(p),resolve('TestData/FA/刀路/刀路/整理测试刀路/多槽圆盖-3-7b202.igs'));await idle();
 assert.ok(await page.locator('[data-command-id="five.tool.scale"]').isVisible());assert.equal(await page.locator('[data-command-id="five.tool.translate"]').count(),0);
 assert.equal(await page.locator('[data-tab-id="toolpath"]').count(),1);
 const first=(await read()).placements[0];await tab('workpiece');assert.ok(await page.locator('[data-tab-id="toolpath"]').isVisible());await page.locator('.five-placement-tools').waitFor();
 assert.equal(await page.locator('.five-placement-tools button').count(),2);assert.equal(await page.locator('[data-command-id="five.tool.fine"]').count(),0);
 await command('five.tool.setup');await click('five.setup.create');assert.equal(await page.locator('[data-tab-id="toolpath"]').count(),0);
 assert.equal(await page.locator('.five-draft-note,[data-five-command="five.apply"]').count(),0);
 let state=await read();const second=state.placements.find(p=>p.id!==first.id);assert.equal(state.workpieces.length,1);assert.equal(state.setups.length,2);
 await page.locator('[data-five-field="setupName"]').fill('翻面装夹');await click('five.setup.rename');assert.equal((await read()).setups.find(s=>s.id===second.setupId).name,'翻面装夹');
 await command('five.tool.translate');await page.locator('[data-five-transform-field="position-X"]').fill('28.5');await page.locator('[data-five-transform-field="position-X"]').press('Enter');await idle();
 assert.equal((await read()).placements.find(p=>p.id===second.id).transform[3],28.5);assert.deepEqual((await read()).placements.find(p=>p.id===first.id).transform,first.transform);
 await click('workpiece.lock');await click(`five.setup.activate:${first.id}`);await command('five.tool.translate');assert.equal(await page.locator('[data-five-transform-field="position-X"]').getAttribute('readonly'),null);
 await tab('workpiece');assert.equal(await page.locator('[data-five-workpiece]').count(),1);assert.ok(await page.locator('[data-command-id="five.tool.scale"]').isDisabled());
 report.cases.push('Actual CEF ribbon separates geometry and setup placement; one round-cover model has two named setups with independent poses and pose locks');
 await tab('workpiece');await click(`five.setup.activate:${second.id}`);await click('workpiece.lock');await tab('workpiece');await command('five.tool.scale');
 const geometry=(await read()).workpieces[0].geometryRef;await page.locator('[data-five-transform-field="scale-X"]').fill('1.05');await page.locator('[data-five-transform-field="scale-X"]').press('Enter');await idle();assert.notDeepEqual((await read()).workpieces[0].geometryRef,geometry);
 await tab('workpiece');await command('five.tool.registration');await click('five.locating.pick:0');await page.waitForFunction(()=>fixture.view.sceneSnapping?.catalogs.has(fixture.view.scene.activePlacementId));
 assert.equal(await page.locator('.five-draft-note,[data-five-command="five.apply"]').count(),0);
 for(let index=0;index<3;++index){
  let found;for(const eye of [[0,0,1],[0,0,-1],[1,-1,1],[-1,1,1]]){
   found=await page.evaluate(async({base,eye})=>{
    const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),{sceneSnapCandidate}=await import(base+'apps/five-axis-cam/webpage/sceneSnapping.mjs');
    const v=fixture.view,vp=v.viewport,id=v.scene.activePlacementId,o=vp.sceneObjects.get(id),catalog=v.sceneSnapping.catalogs.get(id),selected=v.fiveLocating.points.filter(Boolean).map(p=>new T.Vector3(...p.point));
    vp.setViewDirection(eye);vp.fitView(1.3);vp.scene.updateMatrixWorld(true);const r=vp.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster();
    for(const f of catalog.features.filter(f=>['endpoint','center','datum-point','sphere-center'].includes(f.mode))){
      const s=new T.Vector3(...f.point).applyMatrix4(o.matrixWorld).project(vp.camera),x=r.left+(s.x+1)*r.width/2,y=r.top+(1-s.y)*r.height/2;if(document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;
      ray.setFromCamera({x:(x-r.left)/r.width*2-1,y:1-(y-r.top)/r.height*2},vp.camera);const hit=ray.intersectObject(o,false)[0];
      const c=sceneSnapCandidate(fixture.context,v,{objectId:id,allowedModes:['endpoint','midpoint','center','sphere-center','datum-point','point']},hit,{clientX:x,clientY:y});if(!c?.result)continue;const p=new T.Vector3(...c.result.point);
      if(selected.some(q=>q.distanceTo(p)<5)||selected.length===2&&selected[1].clone().sub(selected[0]).cross(p.clone().sub(selected[0])).length()<10)continue;return {x,y};
    }return null;
   },{base,eye});if(found)break;
  }assert.ok(found);await page.mouse.move(found.x,found.y);await page.mouse.click(found.x,found.y);await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
 }
 await click('five.locating.save');state=await read();assert.equal(state.setups.find(s=>s.id===second.setupId).registrationPlan.status,'Prepared');
 const exportPath=resolve(output,'round-cover-locating.json');await page.evaluate(path=>{const bridge=globalThis.icax??globalThis.icaxNativeBridge;fixture.originalDialog=bridge.saveFileDialog;bridge.saveFileDialog=async()=>path;},exportPath);
 await click('five.locating.export');assert.ok(existsSync(exportPath));const document=JSON.parse(readFileSync(exportPath,'utf8'));assert.equal(document.coordinateFrame,'Setup');assert.equal(document.machineStatus,'Unmeasured');assert.equal(document.points.length,3);
 await page.screenshot({path:resolve(output,'round-cover-setup.png')});report.cases.push('Actual CEF native queue and resource bridge support shared BRep scaling, mouse-picked snap features, persistent three-point plan and standard JSON export');
 await page.evaluate(path=>{(globalThis.icax??globalThis.icaxNativeBridge).saveFileDialog=async()=>path;},projectPath);
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('app.save'));await idle();assert.ok(existsSync(projectPath));assert.ok(statSync(projectPath).size>1000);
 const before=await read();writeFileSync(resolve(output,'expected-state.json'),JSON.stringify({state:before,setupId:second.setupId,projectPath},null,2));
 report.cases.push('Actual standalone Save writes the full i5cam file; reopening is checked in a fresh application process');
 assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
