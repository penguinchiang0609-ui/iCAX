const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {chromium}=require('playwright');
const out=path.resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/fixture-part-list-cef');
fs.mkdirSync(out,{recursive:true});
let savedCamera;
const report={passed:false,standaloneCEF:true,realNativeSDO:true,cases:[],errors:[],fits:[]};
(async()=>{
 const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT||'http://127.0.0.1:9620'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));await page.setViewportSize({width:1600,height:1000});page.setDefaultTimeout(30000);page.on('pageerror',e=>report.errors.push(e.message));
 const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fixtureRequest);
 const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
 const inspect=()=>page.evaluate(async()=>{
  const T=await import(new URL('../../../',location.href).href+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view,vp=v.viewport,id=v.fiveFixtureSelectedPart,source=vp.sceneObjects.get(id),box=new T.Box3();
  if(source){source.updateWorldMatrix(true,true);box.setFromObject(source);}vp.camera.updateMatrixWorld(true);const points=[];
  if(!box.isEmpty())for(const x of [box.min.x,box.max.x])for(const y of [box.min.y,box.max.y])for(const z of [box.min.z,box.max.z])points.push(new T.Vector3(x,y,z).project(vp.camera).toArray());
  return {id,camera:vp.getCameraState(),quaternion:vp.camera.quaternion.toArray(),maxNdc:Math.max(0,...points.flatMap(p=>[Math.abs(p[0]),Math.abs(p[1])]))};
 });
 const fit=(before,after)=>{assert.equal(after.camera.theta,before.camera.theta);assert.equal(after.camera.phi,before.camera.phi);assert.ok(after.quaternion.every((n,i)=>Math.abs(n-before.quaternion[i])<1e-12));assert.ok(after.maxNdc<.95,JSON.stringify(after));};
 try{
  console.log("stage reload");await page.reload();await page.waitForFunction(()=>__icaxAppShell?.getState().startupPhase==='ready');
  console.log('stage open');await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),path.resolve(process.env.ICAX_FIXTURE_TEST_PROJECT||'output/acceptance/fixture-pillar-performance-20261101/cef-responsibilities.i5cam'));await page.waitForFunction(()=>__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId);
  await page.evaluate(async()=>{const {getProjectView}=await import(new URL('../../../',location.href).href+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};});await idle();await page.locator('[data-tab-id="workpiece"]').click();await idle();
  console.log('stage select');const unit=await page.evaluate(()=>fixture.view.scene.domain.fixtureUnits[0].id);await command('five.fixture.select:'+unit);await page.waitForFunction(()=>document.querySelectorAll('[data-five-fixture-part]').length===8);
  savedCamera=await page.evaluate(()=>({projectId:__icaxAppShell.getState().activeProjectId,camera:fixture.view.viewport.getCameraState()}));console.log('stage disclosure');const disclosure=page.locator('[data-five-key="unit-generation-info"] details');assert.equal(await disclosure.evaluate(n=>n.open),false);
  assert.equal(await page.locator('[data-five-command="five.fixture.edit:'+unit+'"]').isVisible(),false);assert.equal(await page.locator('[data-five-key="unit-generated-parts"]').isVisible(),true);
  await disclosure.locator('summary').click();assert.equal(await disclosure.evaluate(n=>n.open),true);assert.equal(await page.locator('[data-five-command="five.fixture.edit:'+unit+'"]').isVisible(),true);
  console.log('stage refresh');await command('five.refresh');assert.equal(await disclosure.evaluate(n=>n.open),true);
  report.cases.push('Generation parameters start collapsed; the title expands them; refresh keeps the chosen open state and the name/part list stay visible');
  console.log('stage keyboard');const rows=page.locator('[data-five-fixture-part]');await rows.first().click();const original=await inspect();
  await rows.first().focus();await page.keyboard.press('ArrowDown');assert.equal((await inspect()).id,await rows.nth(1).getAttribute('data-five-fixture-part'));assert.deepEqual((await inspect()).camera,original.camera);
  await page.keyboard.press('ArrowUp');assert.equal((await inspect()).id,await rows.first().getAttribute('data-five-fixture-part'));await page.keyboard.press('Enter');fit(original,await inspect());
  report.cases.push('Up/down traverse and highlight real plate rows without moving the camera; Enter focuses the selected plate');
  console.log('stage focus');for(const projectionMode of ['perspective','orthographic'])for(const [theta,phi] of [[.4,1.1],[-.7,.6]]){
   await page.evaluate(({projectionMode,theta,phi})=>{const vp=fixture.view.viewport;vp.setCameraState({...vp.getCameraState(),projectionMode,theta,phi,radius:2200,target:{x:-600,y:30,z:-100}});},{projectionMode,theta,phi});
   await rows.nth(1).click();let before=await inspect();await page.keyboard.press('f');let after=await inspect();fit(before,after);
   before=after;await rows.nth(2).dblclick();after=await inspect();fit(before,after);report.fits.push({projectionMode,theta,phi,radius:after.camera.radius,maxNdc:after.maxNdc});
  }
  report.cases.push('F and double-click frame the actual plate in perspective and orthographic views while preserving camera angles and quaternion');
  const input=page.locator('[data-five-fixture-field="name"]');await input.focus();const before=await inspect();await input.evaluate(n=>n.dispatchEvent(new KeyboardEvent('keydown',{key:'f',bubbles:true,cancelable:true})));assert.deepEqual((await inspect()).camera,before.camera);
  await rows.last().focus();await page.keyboard.press('ArrowDown');assert.equal((await inspect()).id,await rows.last().getAttribute('data-five-fixture-part'));await rows.first().focus();await page.keyboard.press('ArrowUp');assert.equal((await inspect()).id,await rows.first().getAttribute('data-five-fixture-part'));
  report.cases.push('F in the name field does not move the camera; arrow traversal stays within the first and last rows');
  await disclosure.locator('summary').click();await page.screenshot({path:path.join(out,'cef-fixture-list-controls.png')});assert.deepEqual(report.errors,[]);report.passed=true;
 }catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:path.join(out,'cef-fixture-list-controls-failure.png')}).catch(()=>{});}
 finally{if(savedCamera)await page.evaluate(saved=>{if(__icaxAppShell.getState().activeProjectId===saved.projectId)fixture.view.viewport.setCameraState(saved.camera);},savedCamera).catch(()=>{});fs.writeFileSync(path.join(out,'cef-fixture-list-controls-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
