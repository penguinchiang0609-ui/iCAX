import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {chromium} from 'playwright';

const output=resolve('output/tests/setup-auto-ready'),runtime=resolve('output/tests/job-sequence/cef-runtime'),base=pathToFileURL(runtime+'/').href;
mkdirSync(output,{recursive:true});
const reopen=process.env.ICAX_SETUP_READY_REOPEN==='1';
const browser=await chromium.connectOverCDP('http://127.0.0.1:9476');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');
assert.ok(page);page.setDefaultTimeout(45000);await page.setViewportSize({width:1600,height:950});
const report=reopen?JSON.parse(readFileSync(resolve(output,'report.json'),'utf8')):{passed:false,standaloneCef:true,realMouse:true,cases:[],errors:[]};
page.on('pageerror',error=>report.errors.push(error.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fiveSetupRequest&&!fixture.view.fiveAxisImportActive);
const bind=()=>page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
const click=async id=>{await page.locator('[data-five-command="'+id+'"]').first().click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
const button=id=>page.locator('[data-command-id="'+id+'"]');
const tab=async id=>{assert.ok(await page.locator('[data-tab-id="'+id+'"]').isVisible());await page.locator('[data-tab-id="'+id+'"]').click();await idle();};
async function canQuery(placement){
 const state=await read();
 const sources=await page.evaluate(payload=>fixture.view.viewportSceneProxy.invoke('CamCut.QuerySources',payload),{expected:state.expected,setupId:placement.setupId,placementId:placement.id});
 assert.ok(sources.faceUses.length);
}
async function noConfirmation(){
 assert.equal(await page.locator('[data-command-id="five.setup.complete"],[data-five-command="five.setup.complete"]').count(),0);
 assert.doesNotMatch(await page.locator('[data-five-key="preparation-tree"]').textContent(),/待设置|完成装夹/);
}
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 if(!reopen){
  const projectPath=resolve(output,Date.now()+'-直接编辑装夹.i5cam');
  await page.evaluate(path=>__icaxAppShell.createProject({projectName:'直接编辑装夹',projectPath:path,productId:'icax.five-axis-cam'}),projectPath);await bind();await idle();
  await tab('toolpath');assert.equal(await button('five.tool.projection').isDisabled(),true);
  await tab('simulation');assert.equal(await button('five.tool.machine-simulation').isDisabled(),true);
  await tab('workpiece');
  await page.evaluate(path=>__icaxWorkbench.importWorkpiece(path),resolve('output/demos/sculptural-s-tube-distinct-ends-20261021/艺术S弯管-A端叶形波浪-B端菱窗圆孔斜口.step'));await idle();
  await page.waitForFunction(()=>fixture.view.scene?.domain?.placements.length===1&&fixture.view.viewport?.sceneObjects.get(fixture.view.scene.activePlacementId)?.geometry);
  let state=await read();const first=state.placements[0];
  assert.equal('configured' in state.setups[0],false);await click('five.setup.activate:'+first.id);await noConfirmation();await canQuery(first);
  assert.equal(await button('five.tool.translate').innerText(),'放置');assert.equal(await button('five.tool.rotate').count(),0);
  await button('five.tool.translate').click();await idle();
  await page.locator('[data-five-command="five.tool.rotate"]').click();await idle();
  assert.equal(await page.locator('[data-five-command="five.tool.rotate"]').getAttribute('aria-pressed'),'true');
  assert.equal(await button('five.tool.translate').innerText(),'放置');
  await page.locator('[data-five-transform-value="rotation-Y"]').fill('180');await page.locator('[data-five-transform-value="rotation-Y"]').press('Enter');await idle();
  await page.locator('[data-five-command="five.tool.translate"]').click();await idle();
  const field=page.locator('[data-five-transform-value="position-X"]');await field.fill('125.5');await field.press('Enter');await idle();
  state=await read();assert.ok(Math.abs(state.placements[0].transform[3]-125.5)<1e-8);assert.ok(Math.abs(state.placements[0].transform[0]+1)<1e-8);
  await tab('toolpath');assert.ok(await button('five.tool.projection').isEnabled());await button('five.tool.projection').click();await idle();await canQuery(first);
  report.cases.push('Actual STEP import immediately permits native CAM source queries and CAM editing; one placement ribbon entry contains working translation/rotation controls, whose numeric edits commit without setup confirmation');
  await tab('workpiece');await click('five.setup.activate:'+first.id);await button('five.setup.create').click();await idle();
  state=await read();const second=state.placements.find(p=>p.id!==first.id);assert.ok(second);await canQuery(second);
  await tab('toolpath');assert.ok(await button('five.tool.projection').isEnabled());
  await tab('workpiece');await click('five.setup.activate:'+second.id);await button('five.preparation.copy:setup').click();await idle();
  state=await read();const third=state.placements.find(p=>p.id!==first.id&&p.id!==second.id);assert.ok(third);await canQuery(third);await noConfirmation();
  await click('five.setup.lock:'+third.id);assert.ok(await button('five.tool.translate').isDisabled());await canQuery(third);
  await tab('toolpath');assert.ok(await button('five.tool.projection').isEnabled());
  await tab('workpiece');await click('five.setup.toggle:'+third.id);await tab('toolpath');assert.ok(await button('five.tool.projection').isDisabled());assert.match(await button('five.tool.projection').getAttribute('title'),/已禁用/);
  await tab('simulation');assert.ok(await button('five.tool.machine-simulation').isDisabled());
  await tab('workpiece');await click('five.setup.toggle:'+third.id);await click('five.setup.lock:'+third.id);
  report.cases.push('New and copied setup postures immediately permit CAM; locking still protects placement while CAM remains editable; disabled setups keep both tabs visible and disable dependent commands');
  await command('five.job.open');assert.ok(await page.locator('[data-five-command="five.job.simulate"]').isDisabled());assert.ok(await page.locator('[data-five-command="five.job.export"]').isDisabled());
  assert.doesNotMatch(await page.getByRole('dialog',{name:'装夹工序与输出'}).innerText(),/尚未完成装夹/);await click('five.job.close');
  await page.evaluate(path=>{(globalThis.icax??globalThis.icaxNativeBridge).saveFileDialog=async()=>path;},projectPath);await command('app.save');
  state=await read();writeFileSync(resolve(output,'expected-state.json'),JSON.stringify({projectPath,state},null,2));await page.screenshot({path:resolve(output,'placement-and-tree.png')});
  report.cases.push('Empty paths and missing actual machining prerequisites still disable sequential simulation/output; the three postures and independent transforms save successfully');
 }else{
  const expected=JSON.parse(readFileSync(resolve(output,'expected-state.json'),'utf8'));
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),expected.projectPath);await bind();await idle();
  const state=JSON.parse(JSON.stringify(await read()));assert.deepEqual(state.placements,expected.state.placements);assert.deepEqual(state.workpieces,expected.state.workpieces);assert.deepEqual(state.setups.map(s=>[s.id,s.sequence,s.enabled,s.locked,s.registrationPlan]),expected.state.setups.map(s=>[s.id,s.sequence,s.enabled,s.locked,s.registrationPlan]));
  await tab('workpiece');await noConfirmation();for(const p of state.placements)await canQuery(p);
  await tab('toolpath');assert.ok(await button('five.tool.projection').isEnabled());await page.screenshot({path:resolve(output,'reopened-cam.png')});
  report.cases.push('A fresh standalone process reopens exact transforms, order and locks, and all three saved postures allow native CAM source queries without a completion action');
 }
 assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.passed=false;report.failure=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
