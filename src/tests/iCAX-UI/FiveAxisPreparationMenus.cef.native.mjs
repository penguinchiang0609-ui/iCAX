import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,copyFileSync,readFileSync,writeFileSync} from 'node:fs';
import {chromium} from 'playwright';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY),base=pathToFileURL(resolve(process.env.ICAX_CEF_RUNTIME)+'/').href,reopen=process.env.ICAX_PREPARATION_REOPEN==='1';
mkdirSync(output,{recursive:true});
const report=reopen?JSON.parse(readFileSync(resolve(output,'report.json'),'utf8')):{passed:false,standaloneCef:true,cases:[],errors:[]};
const browser=await chromium.connectOverCDP('http://127.0.0.1:'+process.env.ICAX_CEF_PORT),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');page.setDefaultTimeout(45000);await page.setViewportSize({width:1600,height:950});
page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fixtureRequest&&!fixture.view.fiveSetupRequest&&!fixture.view.fiveWorkpieceSwitching&&!fixture.view.fiveAxisImportActive,null,{timeout:180000});
const check=async()=>{await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await check();};
const click=async id=>{await page.locator('[data-five-command="'+id+'"]').first().click();await check();};
const ribbon=async id=>{await page.locator('[data-command-id="'+id+'"]').click();await check();};
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
const open=async path=>{await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),path);await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);await idle();await page.locator('[data-tab-id="workpiece"]').click();await check();};
const shot=name=>page.screenshot({path:resolve(output,name+'.png')});
const save=async path=>{await page.evaluate(p=>{(globalThis.icax??globalThis.icaxNativeBridge).saveFileDialog=async()=>p;},path);await command('app.save');};
// Packed display samples are rebuilt on open; their temporary resource IDs are
// not engineering state. Persisted curve/pose resources and every input compare.
const persistentState=state=>JSON.parse(JSON.stringify(state,(key,value)=>key==='sampleGeometryRef'?undefined:value));
const groups=async()=>{
 assert.deepEqual(await page.locator('.ribbon-group-title').allTextContents(),['工件','几何','装夹姿态','夹具']);
 assert.equal(await page.locator('[data-five-key="preparation-tree"] [data-five-key="fixture-add"],[data-five-key="preparation-tree"] .five-preparation-footer').count(),0);
 assert.equal(await page.locator('[data-five-key="preparation-tree"] button').evaluateAll(es=>es.some(e=>e.textContent.trim()==='＋')),false);
};
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 if(!reopen){
  report.jobProject=resolve(output,'工序顺序与复制.i5cam');copyFileSync(resolve('output/tests/job-sequence/standalone/高脚杯-双装夹工序.i5cam'),report.jobProject);await open(report.jobProject);await groups();
  let state=await read();assert.equal(state.setups.length,2);const originalCuts=structuredClone(state.intentToolpaths),[first,second]=state.setups,workpiece=state.workpieces[0],firstPlacement=state.placements.find(p=>p.setupId===first.id);
  for(const [i,setup] of state.setups.entries()){
   const placement=state.placements.find(p=>p.setupId===setup.id);await click('five.setup.activate:'+placement.id);await ribbon('five.tool.registration');
   for(const [k,point] of setup.registrationPlan.points.entries()){const measured=[100+i*400-point.point[1],200+point.point[0],300+point.point[2]];for(let j=0;j<3;j++)await page.getByRole('textbox',{name:`P${k+1} 机床 ${'XYZ'[j]}`,exact:true}).fill(String(measured[j]));}
   await page.getByRole('button',{name:'保存并计算定位',exact:true}).click();await check();assert.equal((await read()).setups.find(s=>s.id===setup.id).registrationPlan.machineStatus,'Qualified');await click('five.locating.close');
  }
  await click('five.preparation.workpiece:'+workpiece.id);
  // Hold a real committed sequence response; preserve the latest editor and scrolls.
  await page.addStyleTag({content:'.preserve-test .cam-context-pane::after,.preserve-test .cam-info-pane::after{content:"";display:block;height:1000px}'});
  await page.evaluate(()=>{document.body.classList.add('preserve-test');const proxy=fixture.view.viewportSceneProxy;fixture.invoke=proxy.invoke.bind(proxy);proxy.invoke=async(method,...args)=>{const result=await fixture.invoke(method,...args);if(method==='CamSetup.SetSequence')await new Promise(r=>fixture.releaseOrder=r);return result;};});
  await page.locator('[data-five-setup-drag="'+second.id+'"]').dragTo(page.locator('[data-five-setup-drag="'+first.id+'"]'),{targetPosition:{x:70,y:3}});
  await page.waitForFunction(()=>fixture.releaseOrder);
  const scrolls=await page.evaluate(()=>{const e=document.querySelector('[data-five-transform-field="scale-X"]');fixture.editor=e;fixture.canvas=fixture.view.viewport.renderer.domElement;e.focus({preventScroll:true});e.setSelectionRange(0,1);const positions=['.cam-context-pane','.cam-info-pane'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=40+i*20;return n.scrollTop;});fixture.releaseOrder();return positions;});
  await check();state=await read();assert.deepEqual(state.setups.map(s=>s.id),[second.id,first.id]);
  assert.equal(await page.evaluate(()=>fixture.editor===document.activeElement&&fixture.editor.isConnected&&fixture.editor.selectionStart===0&&fixture.editor.selectionEnd===1&&fixture.canvas===fixture.view.viewport.renderer.domElement),true);
  assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane'].map(s=>document.querySelector(s).scrollTop)),scrolls);
  await page.evaluate(()=>{fixture.view.viewportSceneProxy.invoke=fixture.invoke;document.body.classList.remove('preserve-test');});
  assert.deepEqual(state.intentToolpaths,originalCuts);
  await command('five.job.open');const cl=resolve(output,'按树顺序输出.cl');await page.evaluate(p=>{(globalThis.icax??globalThis.icaxNativeBridge).saveFileDialog=async()=>p;},cl);
  await click('five.job.export');assert.equal(await page.evaluate(()=>fixture.view.fiveJobError),'');const text=readFileSync(cl,'utf8');assert.ok(text.indexOf(second.id)<text.indexOf(first.id));assert.equal(text.split('\nM00\n').length-1,1);assert.equal(text.split('\nFROM/').length-1,2);
  await click('five.job.simulate');await page.waitForFunction(()=>fixture.view.fiveJobPlayback?.index===0&&fixture.view.fiveSimulation?.frames?.length>0&&!fixture.view.pending);await command('five.simulation.pause');assert.equal((await read()).activeSetupId,second.id);
  const last=await page.evaluate(()=>fixture.view.fiveSimulation.frames.length-1),seek=page.locator('[data-five-simulation-field="seek"]');await seek.fill(String(last-1));await seek.dispatchEvent('input');await command('five.simulation.next');await page.waitForFunction(()=>fixture.view.fiveJobPlayback?.boundary);
  await click('five.job.continue');await page.waitForFunction(()=>fixture.view.fiveJobPlayback?.index===1&&!fixture.view.pending);await command('five.simulation.pause');assert.equal((await read()).activeSetupId,first.id);await command('five.job.stop');
  report.cases.push('Actual tree drag persists the native setup sequence without replacing the active editor/canvas or losing selection and both scroll positions; actual CL output and two-stage M00 playback follow exactly that sequence');
  await page.locator('[data-tab-id="workpiece"]').click();await check();await click('five.setup.activate:'+firstPlacement.id);const original=await read();await ribbon('five.preparation.copy:setup');state=await read();const copiedSetup=state.setups.find(s=>!original.setups.some(old=>old.id===s.id));assert.ok(copiedSetup);assert.equal(copiedSetup.registrationPlan.status,'Missing');assert.equal(state.workpieces.length,1);
  assert.equal(state.intentToolpaths.filter(c=>c.setupId===copiedSetup.id).length,original.intentToolpaths.filter(c=>c.setupId===first.id).length);await ribbon('five.setup.remove');assert.equal((await read()).setups.length,2);
  await click('five.preparation.workpiece:'+workpiece.id);await ribbon('five.preparation.copy:workpiece');state=await read();const copiedWorkpiece=state.workpieces.find(w=>w.id!==workpiece.id);assert.ok(copiedWorkpiece);assert.equal(state.placements.filter(p=>p.workpieceId===copiedWorkpiece.id).length,2);assert.equal(await page.locator('[data-five-key="prep-workpiece-'+copiedWorkpiece.id+'"]').count(),1);
  for(const c of original.intentToolpaths)assert.deepEqual(state.intentToolpaths.find(x=>x.id===c.id),c);
  await click('five.preparation.workpiece-action:lock:'+copiedWorkpiece.id);assert.equal(await page.locator('[data-command-id="five.tool.scale"]').isDisabled(),true);
  await click('five.preparation.workpiece-action:lock:'+copiedWorkpiece.id);await click('five.preparation.workpiece-action:enable:'+copiedWorkpiece.id);assert.equal((await read()).workpieces.find(w=>w.id===copiedWorkpiece.id).enabled,false);await click('five.preparation.workpiece-action:enable:'+copiedWorkpiece.id);
  await groups();await shot('four-groups');await save(report.jobProject);report.jobState=await read();
  report.cases.push('Top menus copy a complete independent workpiece subtree or one setup with its cuts; new setups clear machine registration and computed plans; workpiece row lock/disable controls persist and protect geometry');
  report.fixtureProject=resolve(output,'夹具复制.i5cam');copyFileSync(resolve('output/tests/fixture-units/standalone/来源.i5cam'),report.fixtureProject);await open(report.fixtureProject);state=await read();
  for(const kind of ['Imported','Generated']){
   const unit=state.fixtureUnits.find(u=>u.kind===kind);assert.ok(unit);await click('five.preparation.unit:'+unit.id);await ribbon('five.preparation.copy:fixture');const after=await read(),copy=after.fixtureUnits.find(u=>!state.fixtureUnits.some(old=>old.id===u.id));assert.ok(copy);assert.equal(copy.setupId,unit.setupId);assert.equal(copy.partIds.length,unit.partIds.length);assert.equal(copy.kind,kind);
   assert.deepEqual(after.fixtureUnits.find(u=>u.id===unit.id),unit);assert.equal(await page.evaluate(()=>fixture.view.fiveAxisFixtures.selected),copy.id);
   const before=await read();await page.locator('[data-five-fixture-field="x"]').fill('30');await click('five.fixture.apply');const moved=await read();for(const old of before.fixtures.filter(f=>!copy.partIds.includes(f.id)))assert.deepEqual(moved.fixtures.find(f=>f.id===old.id),old);
   await click('five.preparation.unit-action:lock:'+copy.id);assert.equal(await page.locator('[data-command-id="five.fixture.remove"]').isDisabled(),true);await click('five.preparation.unit-action:lock:'+copy.id);
   if(kind==='Imported'){await ribbon('five.fixture.remove');assert.equal((await read()).fixtureUnits.some(u=>u.id===copy.id),false);}state=await read();
  }
  await ribbon('five.fixture.add');assert.equal(await page.getByRole('dialog',{name:'新增夹具',exact:true}).isVisible(),true);await click('five.fixture.library.choose');assert.equal(await page.getByRole('dialog',{name:'新增夹具',exact:true}).getByRole('button',{name:'导入',exact:true}).count(),1);await page.keyboard.press('Escape');await check();
  await groups();await shot('fixture-copy');await save(report.fixtureProject);report.fixtureState=await read();
  report.cases.push('Imported and generated assemblies each copy as one fixture unit with all plates; moving the copy leaves every original part unchanged, locks protect deletion, and the new top menu opens the existing library/import/generator chooser');
 }else{
  for(const [path,expected] of [[report.jobProject,report.jobState],[report.fixtureProject,report.fixtureState]]){await open(path);const actual=persistentState(await read()),saved=persistentState(expected);for(const key of ['workpieces','setups','placements','fixtureUnits','fixtures','intentToolpaths'])assert.deepEqual(actual[key],saved[key],key);await groups();assert.deepEqual(await page.locator('[data-five-setup-drag]').evaluateAll(es=>es.map(e=>e.dataset.fiveSetupDrag)),actual.setups.map(s=>s.id));}
  report.freshProcessReopen=true;report.cases.push('A fresh standalone process restores both saved trees, copied fixture snapshots, native setup order and independent cut ownership');
  await ribbon('five.fixture.add');assert.equal(await page.getByRole('dialog',{name:'新增夹具',exact:true}).isVisible(),true);await ribbon('five.tool.translate');assert.equal(await page.getByRole('dialog',{name:'新增夹具',exact:true}).count(),0);
 }
 assert.deepEqual(report.errors,[]);delete report.error;report.passed=true;
}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;await shot('failure').catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({...report,jobState:undefined,fixtureState:undefined},null,2));await browser.close();}
