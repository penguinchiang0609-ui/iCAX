import {selectModelFile} from './fixtures/fiveAxisFilePicker.mjs';
import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,readFileSync,writeFileSync,existsSync} from 'node:fs';
import {verifyLocating} from './FiveAxisLocating.interactions.mjs';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY),base=pathToFileURL(resolve(process.env.ICAX_CEF_RUNTIME)+'/').href;
mkdirSync(output,{recursive:true});
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${process.env.ICAX_CEF_PORT||9473}`);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');
assert.ok(page);page.setDefaultTimeout(45000);
const reopen=process.env.ICAX_LOCATING_REOPEN==='1';
const report=reopen?JSON.parse(readFileSync(resolve(output,'report.json'),'utf8')):{passed:false,standaloneCef:true,realCup:true,cases:[],errors:[]};
page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fiveAxisImportActive);
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();};
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
const bind=async()=>{
  await page.evaluate(async base=>{
    const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');
    window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};
  },base);await idle();
};
try{
  await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
  if(!reopen){
    const projectPath=resolve(output,'光滑杯子-三点定位.i5cam');
    const created=await page.evaluate(p=>__icaxAppShell.createProject({projectName:'光滑杯子-三点定位',projectPath:p,productId:'icax.five-axis-cam'}),projectPath);
    await page.waitForFunction(id=>window.__icaxWorkbench?.currentProjectId===id&&__icaxAppShell.getState().activeProjectId===id,created.activeProjectId);await bind();
    await page.evaluate(p=>__icaxWorkbench.importWorkpiece(p),resolve('TestData/FA/刀路/刀路/AI构造刀路/光滑杯子-杯壁3方位孔-下载Glasses.step'));await idle();
    await page.locator('[data-tab-id="fixture"]').click();await idle();
    const click=async id=>{await page.locator('[data-five-command="'+id+'"]').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
    
    const first=(await read()).placements[0];
    const nameButton=page.locator('[data-five-command="five.setup.activate:'+first.id+'"]'),beforeName=await nameButton.boundingBox();
    await nameButton.dblclick();
    const edit=page.locator('[data-five-setup-name]'),editBounds=await edit.boundingBox();
    assert.equal(await edit.evaluate(e=>e.parentElement.dataset.fiveKey),'setup-'+first.id);
    assert.ok(Math.abs(editBounds.x-beforeName.x)<2&&Math.abs(editBounds.y-beforeName.y)<2,'Name editor replaces the label in the same row');
    assert.equal(await page.locator('[data-five-command="five.setup.rename"]').count(),0);
    await edit.fill('取消的名称');await edit.press('Escape');await idle();assert.notEqual((await read()).setups[0].name,'取消的名称');
    await nameButton.dblclick();
    await page.screenshot({path:resolve(output,'inline-rename.png')});
    await page.locator('[data-five-setup-name]').fill('正面装夹');await page.locator('[data-five-setup-name]').press('Enter');await idle();
    assert.equal((await read()).setups[0].name,'正面装夹');assert.equal(await page.getByRole('button',{name:'重命名',exact:true}).count(),0);
    await click('five.setup.create');
    const second=(await read()).placements.find(p=>p.id!==first.id);
    await click('five.setup.lock:'+first.id);
    let rows=(await read()).setups;assert.equal(rows.find(s=>s.id===first.setupId).locked,true);assert.equal(rows.find(s=>s.id===second.setupId).locked,false);
    assert.equal(await page.locator('[data-five-command="five.setup.remove:'+first.id+'"]').isDisabled(),true);
    await click('five.setup.lock:'+first.id);
    await click('five.setup.toggle:'+second.id);
    rows=(await read()).setups;assert.equal(rows.find(s=>s.id===first.setupId).enabled,true);assert.equal(rows.find(s=>s.id===second.setupId).enabled,false);
    assert.equal((await read()).workpieces[0].enabled,true);
    await click('five.setup.toggle:'+second.id);
    await click('five.setup.remove:'+second.id);assert.equal((await read()).setups.length,1);
    await click('five.setup.create');
    const skipped=(await read()).placements.find(p=>p.id!==first.id);await click('five.setup.toggle:'+skipped.id);
    await click('five.setup.activate:'+first.id);
    assert.equal(await page.locator('[data-command-id="five.tool.translate"]').count(),0);
    assert.equal(await page.locator('.cam-info-pane [data-five-command="five.tool.registration"]').count(),1);
    const plus=await page.locator('[data-five-command="five.setup.create"]').boundingBox(),list=await page.locator('[data-five-key="setup-selector"]').boundingBox();assert.ok(plus.x>list.x+list.width*.7);
    report.cases.push('Real row controls independently lock, disable, re-enable and delete setups; add is below the list on the right; placement and locating are in the right pane');
    report.cases.push(...await verifyLocating({page,command,read,output,base}));
    // Import into product library without adding scene fixtures, then select a copy.
    await click('five.locating.close');
    await page.locator('[data-five-key="fixture-add"]>summary').click();
    await page.locator('[aria-label="工装选项"]').click();await click('five.fixture.library.open');
    const fixturePath=resolve('samples/structural-steel/vendors-angle-channel/tekspan-60300-cranked-t-plate.step');
    await page.evaluate(path=>{(globalThis.icax??globalThis.icaxNativeBridge).openFileDialog=async()=>path;},fixturePath);
    const count=(await read()).fixtures.length;const importing=click('five.fixture.library.import');await selectModelFile(page,fixturePath,{preview:true});await importing;assert.equal((await read()).fixtures.length,count);
    const library=await page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamFixture.LibraryList',{})),entry=library.entries.at(-1);
    await click('five.fixture.library.close');await click('five.fixture.library.choose');
    assert.equal(await page.locator('[data-five-key="fixture-choices"] button').last().innerText(),'导入');
    await click('five.fixture.library.use:'+entry.id);const placed=(await read()).fixtures.at(-1);assert.ok(placed);
    const placedUnit=(await read()).fixtureUnits.find(u=>u.partIds.includes(placed.id));await click('five.fixture.select:'+placedUnit.id);
    await page.locator('[data-five-command="five.fixture.select:'+placed.id+'"]').focus();await page.keyboard.press('e');await idle();
    assert.equal(await page.evaluate(()=>fixture.view.viewport.transformManipulator.control.mode),'rotate');
    await page.keyboard.press('w');await idle();assert.equal(await page.evaluate(()=>fixture.view.viewport.transformManipulator.control.mode),'translate');
    const drag=await page.evaluate(async base=>{
      const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),vp=fixture.view.viewport;
      vp.fitView(1.3);await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);
      const m=vp.transformManipulator,r=vp.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster();m.helper.updateMatrixWorld(true);
      for(let y=40;y<r.height-40;y+=4)for(let x=40;x<r.width-40;x+=4){ray.setFromCamera(new T.Vector2(x/r.width*2-1,1-y/r.height*2),vp.camera);const hit=ray.intersectObjects(m.control._gizmo.picker.translate.children,false).find(h=>h.object.visible);if(hit?.object.name==='X'&&document.elementFromPoint(r.x+x,r.y+y)===vp.renderer.domElement)return {x:r.x+x,y:r.y+y};}throw Error('No exposed X handle');
    },base);
    await page.mouse.move(drag.x,drag.y);await page.mouse.down();await page.mouse.move(drag.x+60,drag.y,{steps:10});await page.mouse.up();await idle();
    const moved=(await read()).fixtures.find(f=>f.id===placed.id);assert.notDeepEqual(moved.transform,placed.transform);
    await click('five.fixture.row.lock:'+placedUnit.id);assert.equal((await read()).fixtures.find(f=>f.id===placed.id).locked,true);
    assert.equal(await page.locator('[data-five-command="five.fixture.row.remove:'+placed.id+'"]').isDisabled(),true);
    await click('five.fixture.row.enable:'+placedUnit.id);assert.equal((await read()).fixtures.find(f=>f.id===placed.id).enabled,false);
    await click('five.fixture.row.enable:'+placed.id);await click('five.fixture.row.lock:'+placed.id);
    await page.screenshot({path:resolve(output,'fixture-list.png')});
    report.cases.push('Product-library import leaves scene unchanged; dropdown selection creates a copy; real mouse drag persists fixture position; row lock and enable controls work');
    await click('five.tool.registration');
    const exportPath=resolve(output,'三点定位.json');
    await page.evaluate(path=>{(globalThis.icax??globalThis.icaxNativeBridge).saveFileDialog=async()=>path;},exportPath);
    await page.locator('[data-five-command="five.locating.export"]').click();await idle();
    const state=await read(),plan=state.setups.find(s=>s.id===state.activeSetupId).registrationPlan;
    assert.deepEqual(JSON.parse(readFileSync(exportPath,'utf8')).points,plan.points);
    await page.evaluate(path=>{(globalThis.icax??globalThis.icaxNativeBridge).saveFileDialog=async()=>path;},projectPath);
    await command('app.save');assert.ok(existsSync(projectPath));
    writeFileSync(resolve(output,'expected-state.json'),JSON.stringify({projectPath,state},null,2));
  }else{
    const expected=JSON.parse(readFileSync(resolve(output,'expected-state.json'),'utf8'));
    await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),expected.projectPath);
    await page.waitForFunction(()=>window.__icaxWorkbench?.currentProjectId===__icaxAppShell.getState().activeProjectId);await bind();
    const state=JSON.parse(JSON.stringify(await read()));
    assert.deepEqual(state.setups,expected.state.setups);assert.deepEqual(state.fixtures,expected.state.fixtures);assert.deepEqual(state.fixtureUnits,expected.state.fixtureUnits);
    await command('five.tool.registration');
    assert.equal(await page.getByRole('dialog',{name:'三点定位',exact:true}).count(),1);
    assert.equal(await page.evaluate(()=>fixture.view.fiveLocating.points.length),3);
    await page.screenshot({path:resolve(output,'reopened-three-centres.png')});
    report.cases.push('Standalone native export and fresh-process save/reopen preserve three cylinder-centre references and their Setup coordinates');
  }
  assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'cef-failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
