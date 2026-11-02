import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,readFileSync,writeFileSync,copyFileSync} from 'node:fs';
import {chromium} from 'playwright';
import {selectModelFile} from './fixtures/fiveAxisFilePicker.mjs';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY),base=pathToFileURL(resolve(process.env.ICAX_CEF_RUNTIME)+'/').href;
mkdirSync(output,{recursive:true});
const reopen=process.env.ICAX_PREPARATION_REOPEN==='1';
const report=reopen?JSON.parse(readFileSync(resolve(output,'report.json'),'utf8')):{passed:false,standaloneCef:true,realNativeQueue:true,cases:[],errors:[]};
const browser=await chromium.connectOverCDP('http://127.0.0.1:'+process.env.ICAX_CEF_PORT);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');page.setDefaultTimeout(45000);
page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fixtureRequest&&!fixture.view.fiveSetupRequest&&!fixture.view.fiveWorkpieceSwitching&&!fixture.view.fiveAxisImportActive,null,{timeout:240000});
const check=async()=>{await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await check();};
const click=async id=>{await page.locator('[data-five-command="'+id+'"]').first().click();await check();};
const ribbonClick=async id=>{await page.locator('[data-command-id="'+id+'"]').click();await check();};
const cleanTree=async()=>{
 assert.equal(await page.locator('[data-five-key="preparation-tree"] [data-five-command="workpiece.import"],[data-five-command^="five.preparation.add-setup:"]').count(),0);
 assert.equal(await page.locator('[data-five-key="preparation-tree"] button,[data-five-key="preparation-tree"] summary').evaluateAll(es=>es.some(e=>e.textContent.trim()==='＋')),false);
 assert.equal(await page.locator('[data-command-id="workpiece.import"]').innerText(),'导入工件');
 assert.equal(await page.locator('[data-command-id="five.setup.create"]').innerText(),'新增装夹');
};
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
const bind=async()=>{await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);await idle();};
const open=async path=>{await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),path);await bind();await page.locator('[data-tab-id="workpiece"]').click();await check();};
const save=async path=>{await page.evaluate(p=>{(globalThis.icax??globalThis.icaxNativeBridge).saveFileDialog=async()=>p;},path);await command('app.save');};
const shot=name=>page.screenshot({path:resolve(output,name+'.png')});
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 if(!reopen){
  const project=resolve(output,'三级树-'+Date.now()+'.i5cam');copyFileSync(resolve('output/tests/fixture-units/standalone/来源.i5cam'),project);await open(project);
  assert.equal(await page.locator('[data-tab-id="fixture"]').count(),0);
  let state=await read();const workpiece=state.workpieces[0],placements=state.placements.filter(p=>p.workpieceId===workpiece.id);
  for(const p of placements){const subtree=page.locator('[data-five-key="prep-setup-'+p.id+'"]');assert.equal(await subtree.count(),1);for(const unit of state.fixtureUnits.filter(u=>u.setupId===p.setupId))assert.equal(await subtree.locator('[data-five-key="fixture-'+unit.id+'"]').count(),1);}
  await click('five.preparation.workpiece:'+workpiece.id);
  await cleanTree();
  assert.equal(await page.locator('[data-five-key="workpiece-parameters"]').count(),1);assert.equal(await page.locator('[data-five-transform-field="position-X"]').count(),0);
  await shot('workpiece');
  const generated=state.fixtureUnits.find(u=>u.kind==='Generated'),source=state.placements.find(p=>p.setupId===generated.setupId),other=placements.find(p=>p.id!==source.id);
  await click('five.setup.activate:'+other.id);
  assert.equal(await page.locator('[data-five-transform-field="position-X"]').count(),1);assert.equal(await page.locator('[data-five-key="workpiece-parameters"]').count(),0);
  await click('five.tool.registration');assert.equal(await page.locator('.cam-info-pane [data-five-key="locating-dialog"]').count(),1);
  await page.locator('[data-five-locating-field="tolerance"]').fill('0.2');
  await click('five.setup.activate:'+source.id);await click('five.tool.registration');
  assert.equal(await page.locator('[data-five-locating-field="tolerance"]').inputValue(),'0.1');
  await click('five.preparation.unit:'+generated.id);
  await page.locator('[data-five-key="unit-generation-info"]').waitFor({state:'visible'});
  assert.equal((await read()).activeSetupId,generated.setupId);assert.equal(await page.locator('[data-five-key="unit-generation-info"]').count(),1);
  const before=await read();await page.locator('[data-five-fixture-field="x"]').fill('25');await click('five.fixture.apply');state=await read();
  for(const p of state.fixtures)assert.equal(p.transform[3],before.fixtures.find(f=>f.id===p.id).transform[3]+(generated.partIds.includes(p.id)?25:0));
  await shot('generated-unit');
  const imported=state.fixtureUnits.find(u=>u.setupId===other.setupId);await click('five.preparation.unit:'+imported.id);
  assert.equal((await read()).activeSetupId,other.setupId);assert.equal(await page.locator('[data-five-key="unit-import-info"]').count(),1);assert.equal(await page.locator('[data-five-key="unit-generation-info"]').count(),0);
  await click('five.preparation.workpiece:'+workpiece.id);
  await page.locator('[data-five-command="five.preparation.toggle:setup:'+source.id+'"]').click();await idle();
  assert.equal(await page.locator('[data-five-key="prep-units-'+source.id+'"]').isVisible(),false);
  // Hold a real scene response while editing and scrolling both panes.
  await page.addStyleTag({content:'.prep-scroll-test .cam-context-pane::after,.prep-scroll-test .cam-info-pane::after{content:"";display:block;height:1100px}'});
  await page.evaluate(()=>{document.body.classList.add('prep-scroll-test');const proxy=fixture.view.viewportSceneProxy,invoke=proxy.invoke.bind(proxy);fixture.originalInvoke=invoke;proxy.invoke=async(method,...args)=>{const result=await invoke(method,...args);if(method==='CamScene.ReadState')await new Promise(r=>fixture.releaseRead=r);return result;};fixture.refreshing=__icaxAppShell.executeRibbonCommand('five.refresh');});
  await page.waitForFunction(()=>fixture.releaseRead);
  const keep=await page.evaluate(()=>{const input=document.querySelector('[data-five-transform-field="scale-X"]');fixture.keptInput=input;fixture.keptCanvas=fixture.view.viewport.renderer.domElement;fixture.listenerCalls=0;input.addEventListener('retention-probe',()=>fixture.listenerCalls++);input.focus({preventScroll:true});input.setSelectionRange(0,1);const scrolls=['.cam-context-pane','.cam-info-pane'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=65+i*20;return n.scrollTop;});fixture.releaseRead();return scrolls;});
  await page.evaluate(()=>fixture.refreshing);await check();
  assert.equal(await page.evaluate(()=>{const e=fixture.keptInput;e.dispatchEvent(new Event('retention-probe'));return e.isConnected&&document.activeElement===e&&e.selectionStart===0&&e.selectionEnd===1&&fixture.listenerCalls===1&&fixture.keptCanvas===fixture.view.viewport.renderer.domElement;}),true);
  assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane'].map(s=>document.querySelector(s).scrollTop)),keep);
  assert.equal(await page.locator('[data-five-key="prep-units-'+source.id+'"]').isVisible(),false);
  await page.evaluate(()=>{fixture.view.viewportSceneProxy.invoke=fixture.originalInvoke;document.body.classList.remove('prep-scroll-test');});
  report.cases.push('One workpiece page renders the three-level native hierarchy; each selection shows its own parameters, cross-setup unit selection activates the correct placement, and a whole-unit move leaves all other units unchanged','Three-point drafts stay with their setup; a delayed native refresh retains the tree collapse state, input/canvas/listener identity, caret and both pane scroll positions');
  await ribbonClick('five.setup.create');state=await read();const added=state.placements.find(p=>!placements.some(old=>old.id===p.id));assert.ok(added);assert.equal(added.workpieceId,workpiece.id);
  assert.equal(await page.locator('[data-five-transform-field="position-X"]').count(),1);
  await page.locator('[data-five-key="fixture-add"]>summary').click();await click('five.fixture.library.choose');
  const importing=page.locator('[data-five-command="five.fixture.library.import"]').click();await selectModelFile(page,resolve('samples/structural-steel/vendors-angle-channel/tekspan-60300-cranked-t-plate.step'),{preview:true});await importing;await check();
  state=await read();const addedUnit=state.fixtureUnits.find(u=>u.setupId===added.setupId);assert.ok(addedUnit);
  assert.equal(await page.evaluate(()=>fixture.view.fiveAxisFixtures.selected),addedUnit.id);
  assert.equal(await page.locator('[data-five-key="prep-setup-'+added.id+'"] [data-five-key="fixture-'+addedUnit.id+'"]').count(),1);
  await click('five.setup.activate:'+added.id);assert.ok((await read()).setups.find(s=>s.id===added.setupId));
  await save(project);report.fixtureProject=project;report.fixtureState=await read();
  report.cases.push('Import and new setup use the top ribbon, with no anonymous + controls in the preparation tree; the labeled fixture-unit menu imports through the shared preview picker, selects the new unit and completes setup');

  const camProject=resolve(output,'双装夹CAM-'+Date.now()+'.i5cam');copyFileSync(resolve('output/tests/job-sequence/standalone/高脚杯-双装夹工序.i5cam'),camProject);await open(camProject);
  state=await read();const setups=state.setups.filter(s=>state.intentToolpaths.some(c=>c.setupId===s.id));assert.equal(setups.length,2);
  const p1=state.placements.find(p=>p.setupId===setups[0].id),p2=state.placements.find(p=>p.setupId===setups[1].id);
  await click('five.setup.activate:'+p1.id);await page.locator('[data-tab-id="toolpath"]').click();await check();await command('five.cam-page.cutting');
  const cut=state.intentToolpaths.find(c=>c.setupId===p1.setupId),unmodified=state.intentToolpaths.filter(c=>c.setupId===p2.setupId);
  assert.deepEqual(await page.locator('[data-five-intent]').evaluateAll(es=>es.map(e=>e.dataset.fiveIntent)),state.intentToolpaths.filter(c=>c.setupId===p1.setupId).map(c=>c.id));
  await page.locator('[data-five-intent="'+cut.id+'"]').click();await check();
  await page.locator('[data-five-cut-name]').fill('第一装夹专用刀路');await page.locator('[data-five-cut-name]').press('Tab');await check();
  await command('five.axes.reverse-all');state=await read();
  assert.equal(state.intentToolpaths.find(c=>c.id===cut.id).name,'第一装夹专用刀路');assert.deepEqual(state.intentToolpaths.filter(c=>c.setupId===p2.setupId),unmodified);
  await click('five.setup.activate:'+p2.id);await command('five.cam-page.cutting');
  assert.deepEqual(await page.locator('[data-five-intent]').evaluateAll(es=>es.map(e=>e.dataset.fiveIntent)),unmodified.map(c=>c.id));
  await shot('cam-setup-scope');await page.locator('[data-tab-id="workpiece"]').click();await check();await click('five.setup.activate:'+p2.id);await shot('setup-tree');
  await save(camProject);report.camProject=camProject;report.camState=await read();
  report.cases.push('CAM still filters by the active native setup; renaming and reversing tool axes in the first setup leave the second setup cuts and poses byte-for-byte unchanged');
 }else{
  for(const [path,expected] of [[report.fixtureProject,report.fixtureState],[report.camProject,report.camState]]){
   await open(path);const actual=JSON.parse(JSON.stringify(await read()));assert.deepEqual(actual.workpieces,expected.workpieces);assert.deepEqual(actual.placements,expected.placements);assert.deepEqual(actual.fixtureUnits,expected.fixtureUnits);assert.deepEqual(actual.intentToolpaths,expected.intentToolpaths);assert.equal(await page.locator('[data-tab-id="fixture"]').count(),0);
   if(path===report.fixtureProject){
    const unit=actual.fixtureUnits.find(u=>u.kind==='Generated');await click('five.preparation.unit:'+unit.id);
    const drag=await page.evaluate(async base=>{
     const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),vp=fixture.view.viewport;vp.setStandardView('front');vp.fitView(1.3);await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);
     const m=vp.transformManipulator,r=vp.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster();m.helper.updateMatrixWorld(true);
     for(let y=40;y<r.height-40;y+=4)for(let x=40;x<r.width-40;x+=4){ray.setFromCamera(new T.Vector2(x/r.width*2-1,1-y/r.height*2),vp.camera);const h=ray.intersectObjects(m.control._gizmo.picker.translate.children,false).find(h=>h.object.visible);if(h?.object.name==='X'&&document.elementFromPoint(r.x+x,r.y+y)===vp.renderer.domElement)return{x:r.x+x,y:r.y+y};}throw Error('No exposed unit X handle');
    },base);
    await page.mouse.move(drag.x,drag.y);await page.mouse.down();await page.mouse.move(drag.x+45,drag.y,{steps:10});await page.mouse.up();await check();
    const moved=await read(),delta=moved.fixtureUnits.find(u=>u.id===unit.id).transform[3]-unit.transform[3];assert.ok(Math.abs(delta)>0.01);
    for(const part of moved.fixtures)assert.ok(Math.abs(part.transform[3]-actual.fixtures.find(p=>p.id===part.id).transform[3]-(unit.partIds.includes(part.id)?delta:0))<1e-7);
    await click('five.preparation.unit-action:lock:'+unit.id);assert.equal(await page.evaluate(()=>!!fixture.view.fiveFixtureGizmo),false);
    await click('five.preparation.unit-action:lock:'+unit.id);
    await shot('tree-unit-drag');
    const placement=actual.placements.find(p=>p.setupId===unit.setupId),sibling=actual.placements.find(p=>p.workpieceId===placement.workpieceId&&p.id!==placement.id);
    await click('five.setup.lock:'+placement.id);
    assert.equal(await page.evaluate(()=>fixture.view.fivePreparationKind),'setup');
    assert.equal(await page.locator('[data-five-transform-field="position-X"]').isEditable(),false);
    await click('five.preparation.workpiece:'+placement.workpieceId);assert.equal(await page.locator('[data-five-transform-field="scale-X"]').isEditable(),false);
    await click('five.setup.activate:'+sibling.id);assert.equal(await page.locator('[data-five-transform-field="position-X"]').isEditable(),true);
    await click('five.setup.lock:'+placement.id);
    await click('five.tool.registration');await click('five.locating.close');assert.equal(await page.locator('[data-five-transform-field="position-X"]').count(),1);
    await command('five.fixture.focus.design');assert.equal(await page.locator('.cam-info-pane [data-five-key="fixture-design"]').count(),1);await click('five.fixture.design.close');
    report.cases.push('An actual mouse drag in the merged page moves every plate of the selected unit rigidly; the lock action removes its gizmo and leaves other setups and units unchanged');
   }
  }
  report.cases.push('A fresh standalone process reopens the saved hierarchy, unit placements, setup completion and independent CAM edits');report.freshProcessReopen=true;
  const before=await read();
  const importing=page.locator('[data-command-id="workpiece.import"]').click();
  await selectModelFile(page,resolve('TestData/FA/刀路/刀路/AI构造刀路/光滑奖杯-杯身3方位孔-下载高脚杯.step'),{preview:true});await importing;await check();
  let multiple=await read();assert.equal(multiple.workpieces.length,before.workpieces.length+1);
  for(const old of before.workpieces)assert.equal(await page.locator('[data-five-key="prep-workpiece-'+old.id+'"]').count(),1);
  const added=multiple.workpieces.find(w=>!before.workpieces.some(old=>old.id===w.id));assert.ok(added);
  await page.locator('[data-five-transform-field="scale-X"]').fill('1.1');await page.locator('[data-five-transform-field="scale-X"]').press('Tab');await check();
  multiple=await read();assert.notDeepEqual(multiple.workpieces.find(w=>w.id===added.id).geometryRef,added.geometryRef);
  for(const old of before.workpieces)assert.deepEqual(multiple.workpieces.find(w=>w.id===old.id),old);
  assert.deepEqual(multiple.intentToolpaths,before.intentToolpaths);
  await shot('multiple-workpieces');report.cases.push('A second real workpiece imports into its own tree root; scaling its shared geometry leaves the existing workpiece and all earlier setup toolpaths unchanged');
  const selected=before.workpieces[0];await click('five.preparation.workpiece:'+selected.id);await cleanTree();
  await page.locator('[data-five-command="five.preparation.toggle:workpiece:'+selected.id+'"]').click();await idle();
  assert.equal(await page.locator('[data-five-key="prep-setups-'+selected.id+'"]').isVisible(),false);
  const previous=await read();await ribbonClick('five.setup.create');const created=await read(),newPlacement=created.placements.find(p=>!previous.placements.some(old=>old.id===p.id));assert.ok(newPlacement);
  assert.equal(newPlacement.workpieceId,selected.id);assert.equal(created.activeSetupId,newPlacement.setupId);
  assert.deepEqual(created.placements.filter(p=>p.workpieceId===added.id),previous.placements.filter(p=>p.workpieceId===added.id));
  assert.deepEqual(created.intentToolpaths,previous.intentToolpaths);
  assert.equal(await page.locator('[data-five-key="prep-setups-'+selected.id+'"]').isVisible(),true);
  assert.equal(await page.locator('[data-five-key="prep-setup-'+newPlacement.id+'"] .five-setup-row.selected').count(),1);
  const sourcePlacement=previous.placements.find(p=>p.workpieceId===added.id);
  await click('five.preparation.add-unit:'+sourcePlacement.id);
  assert.equal((await read()).activeSetupId,sourcePlacement.setupId);assert.equal(await page.locator('[data-five-key="prep-setup-'+sourcePlacement.id+'"] [data-five-key="fixture-add"]').getAttribute('open'),'');
  await shot('fixture-menu');await page.keyboard.press('Escape');await check();
  await click('five.setup.activate:'+newPlacement.id);await cleanTree();await shot('preparation-menu');
  report.cases.push('With two workpieces, the actual top-ribbon button adds a setup only to the selected workpiece and expands/selects it; a labeled fixture-unit entry activates its own parent setup before opening its menu');
 }
 assert.deepEqual(report.errors,[]);delete report.error;report.passed=true;
}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;await shot('failure').catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({...report,fixtureState:undefined,camState:undefined},null,2));await browser.close();}
