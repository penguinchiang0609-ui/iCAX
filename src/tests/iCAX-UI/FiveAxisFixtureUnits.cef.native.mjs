import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,writeFileSync} from 'node:fs';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY),base=pathToFileURL(resolve(process.env.ICAX_CEF_RUNTIME)+'/').href;
mkdirSync(output,{recursive:true});
const {chromium}=await import('playwright'),browser=await chromium.connectOverCDP('http://127.0.0.1:'+process.env.ICAX_CEF_PORT);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');page.setDefaultTimeout(45000);
const report={passed:false,standaloneCef:true,cases:[],errors:[]};page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fixtureRequest);
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
const click=async id=>{await page.locator('[data-five-command="'+id+'"]').first().click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),resolve('output/tests/fixture-units/standalone/来源.i5cam'));
 await page.waitForFunction(()=>__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId);
 await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);
 await page.locator('[data-tab-id="workpiece"]').click();await idle();
 const before=await read(),unit=before.fixtureUnits.find(u=>u.setupId===before.activeSetupId&&u.kind==='Generated'),other=before.fixtureUnits.find(u=>u.setupId===before.activeSetupId&&u.kind==='Imported');assert.ok(unit&&other);assert.equal(unit.partIds.length,4);
 await click('five.preparation.unit:'+unit.id);
 assert.equal(await page.locator('[data-five-key="unit-generation-info"]').count(),1);assert.equal(await page.locator('[data-five-key="unit-import-info"]').count(),0);
 assert.equal(await page.locator('[data-five-key^="fixture-"].five-setup-row').count(),before.fixtureUnits.length);
 const drag=await page.evaluate(async base=>{
   const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),vp=fixture.view.viewport;vp.setStandardView('front');vp.fitView(1.3);await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);
   const m=vp.transformManipulator,r=vp.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster();m.helper.updateMatrixWorld(true);
   for(let y=40;y<r.height-40;y+=4)for(let x=40;x<r.width-40;x+=4){ray.setFromCamera(new T.Vector2(x/r.width*2-1,1-y/r.height*2),vp.camera);const h=ray.intersectObjects(m.control._gizmo.picker.translate.children,false).find(h=>h.object.visible);if(h?.object.name==='X'&&document.elementFromPoint(r.x+x,r.y+y)===vp.renderer.domElement)return{x:r.x+x,y:r.y+y};}throw Error('No exposed unit X handle');
 },base);
 await page.mouse.move(drag.x,drag.y);await page.mouse.down();await page.mouse.move(drag.x+50,drag.y,{steps:10});await page.mouse.up();await idle();
 let state=await read();const moved=state.fixtureUnits.find(u=>u.id===unit.id);assert.notDeepEqual(moved.transform,unit.transform);
 for(const id of unit.partIds)assert.deepEqual(state.fixtures.find(p=>p.id===id).transform,moved.transform);
 for(const id of other.partIds)assert.deepEqual(state.fixtures.find(p=>p.id===id).transform,before.fixtures.find(p=>p.id===id).transform);
 await page.screenshot({path:resolve(output,'whole-unit-drag.png')});
 await click('five.preparation.unit-action:lock:'+unit.id);assert.equal(await page.evaluate(()=>!!fixture.view.fiveFixtureGizmo),false);assert.equal(await page.locator('[data-five-command="five.preparation.unit-action:remove:'+unit.id+'"]').isDisabled(),true);
 await click('five.preparation.unit-action:enable:'+unit.id);state=await read();assert.ok(state.fixtures.filter(p=>unit.partIds.includes(p.id)).every(p=>p.enabled===false));
 await click('five.preparation.unit-action:enable:'+unit.id);await click('five.preparation.unit-action:lock:'+unit.id);
 await click('five.fixture.design.breakdown');assert.ok((await read()).fixtureUnits.find(u=>u.id===unit.id).design.breakdown);
 await click('five.preparation.unit:'+other.id);assert.equal(await page.locator('[data-five-key="unit-import-info"]').count(),1);assert.equal(await page.locator('[data-five-key="unit-generation-info"]').count(),0);
 await click('five.preparation.unit:'+unit.id);await page.locator('.cam-info-pane').evaluate(e=>e.scrollTop=0);await page.screenshot({path:resolve(output,'generated-unit-information.png')});
 await click('five.preparation.unit-action:remove:'+unit.id);state=await read();assert.equal(state.fixtureUnits.filter(u=>u.setupId===state.activeSetupId).length,1);assert.ok(state.fixtures.every(p=>!unit.partIds.includes(p.id)));
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('edit.undo'));await idle();state=await read();assert.ok(state.fixtureUnits.some(u=>u.id===unit.id));assert.ok(unit.partIds.every(id=>state.fixtures.some(p=>p.id===id)));
 assert.deepEqual(report.errors,[]);report.passed=true;report.cases.push('Generated four-part assembly occupies one row; a real mouse drag moves all four rigidly and leaves the imported unit unchanged','Unit lock removes the gizmo and blocks deletion; disable applies to every part; whole-unit deletion and Undo restore the unit and all parts','Selection switches between the imported unit information and the generated recipe/parts; moved generated unit can still be broken down');
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
