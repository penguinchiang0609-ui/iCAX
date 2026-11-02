import {selectModelFile} from './fixtures/fiveAxisFilePicker.mjs';
import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,writeFileSync,readFileSync,copyFileSync,unlinkSync} from 'node:fs';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY),base=pathToFileURL(resolve(process.env.ICAX_CEF_RUNTIME)+'/').href;
mkdirSync(output,{recursive:true});
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${process.env.ICAX_CEF_PORT||9476}`);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');assert.ok(page);page.setDefaultTimeout(45000);
const reopen=process.env.ICAX_FIXTURE_LIBRARY_REOPEN==='1';
const report=reopen?JSON.parse(readFileSync(resolve(output,'report.json'),'utf8')):{passed:false,standaloneCef:true,realNativeQueue:true,cases:[],errors:[]};
page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fixtureRequest&&!fixture.view.fiveAxisImportActive,null,{timeout:240000});
const check=async()=>{await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await check();};
const click=async id=>{if(id.startsWith('five.fixture.library.use:')&&await page.locator('[data-five-key="fixture-library"]').count())await page.locator('[data-five-library-select="'+id.split(':')[1]+'"]').click();await page.locator(`[data-five-command="${id}"]`).first().click();await check();};
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
const library=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamFixture.LibraryList',{}, {timeoutMs:120000}));
const tab=async id=>{await page.locator(`[data-tab-id="${id}"]`).click();await check();};
const bind=async()=>{await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);await idle();};
const create=async name=>{const created=await page.evaluate(({name,path})=>__icaxAppShell.createProject({projectName:name,projectPath:path,productId:'icax.five-axis-cam'}),{name,path:resolve(output,name+'.i5cam')});await page.waitForFunction(id=>__icaxWorkbench.currentProjectId===id&&__icaxAppShell.getState().activeProjectId===id,created.activeProjectId);await bind();};
const importPart=async path=>{await page.evaluate(p=>__icaxWorkbench.importWorkpiece(p),resolve(path));await check();};
const save=async path=>{await page.evaluate(path=>{(globalThis.icax??globalThis.icaxNativeBridge).saveFileDialog=async()=>path;},path);await command('app.save');};
const openLibrary=()=>command('five.fixture.library.open');
const renameLibrary=async(entry,name)=>{await page.locator(`[data-five-library-select="${entry.id}"]`).dblclick();const input=page.locator(`[data-five-library-field="${entry.id}"]`);await input.fill(name);await input.press('Enter');await check();return (await library()).entries.find(e=>e.id===entry.id);};
const importLibrary=async(path,name)=>{await openLibrary();const before=new Set((await library()).entries.map(e=>e.id));const importing=command('five.fixture.library.import');await selectModelFile(page,path,{preview:true});await importing;const entry=(await library()).entries.find(e=>!before.has(e.id));assert.ok(entry,'Visible library import creates an entry');assert.equal(entry.source,'Imported');assert.ok(entry.partCount>0);return renameLibrary(entry,name);};
const cover='TestData/FA/刀路/刀路/整理测试刀路/多槽圆盖-3-7b202.igs';
const other='TestData/FA/刀路/刀路/整理测试刀路/组合件-多孔矮圆饼-带孔圆环.STEP';
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 if(!reopen){
  await create('工装库来源');await importPart(cover);await tab('workpiece');
  const initialState=await read(),sourcePlacement=initialState.placements.find(p=>p.setupId===initialState.activeSetupId);
  const sample=resolve('samples/structural-steel/vendors-angle-channel/tekspan-60300-cranked-t-plate.step');
  const fixturePath=resolve(output,'导入底座.step');copyFileSync(sample,fixturePath);
  const single=await importLibrary(fixturePath,'固定底座');assert.equal(single.partCount,1);unlinkSync(fixturePath);
  await click(`five.fixture.library.default:${single.id}`);await click(`five.fixture.library.use:${single.id}`);
  let state=await read();const initial=state.fixtures.find(f=>f.setupId===initialState.activeSetupId);assert.ok(initial);
  await page.locator('[data-five-fixture-field="x"]').fill('300');await click('five.fixture.apply');
  await click('five.setup.create');state=await read();
  assert.equal(state.fixtures.length,single.partCount*2);
  assert.equal(state.fixtures.find(f=>f.id===initial.id).transform[3],300);
  assert.ok(state.fixtures.filter(f=>f.setupId===state.activeSetupId).every(f=>f.transform[3]===0));
  await openLibrary();await click(`five.fixture.library.default:${single.id}`);await click('five.fixture.library.close');
  await click(`five.setup.activate:${sourcePlacement.id}`);
  const secondPath=resolve(output,'导入备用底座.step');copyFileSync(sample,secondPath);
  const managed=await importLibrary(secondPath,'备用底座');unlinkSync(secondPath);
  await click(`five.fixture.library.default:${managed.id}`);
  await page.screenshot({path:resolve(output,'library.png')});
  report.cases.push('Visible library import prepares two embedded entries; rename/default management survives removal of original STEP files, and a new setup receives one default at its imported position independently of a moved instance');

  // Actual asynchronous library response, with input and scrolling continuing
  // while it is in flight. Rendering must keep keyed input/canvas nodes.
  await page.locator(`[data-five-library-select="${managed.id}"]`).dblclick();
  await page.addStyleTag({content:'.library-scroll-test .cam-context-pane::after,.library-scroll-test .cam-info-pane::after{content:"";display:block;height:1600px}'});
  await page.evaluate(()=>{
   document.body.classList.add('library-scroll-test');const proxy=fixture.view.viewportSceneProxy,invoke=proxy.invoke.bind(proxy);
   fixture.originalInvoke=invoke;proxy.invoke=async(method,...args)=>{const result=await invoke(method,...args);if(method==='CamFixture.LibraryList'){await new Promise(r=>fixture.releaseLibrary=r);}return result;};
  });
  await page.locator('[data-five-command="five.fixture.library.refresh"]').click();
  await page.waitForFunction(()=>fixture.releaseLibrary);
  const keep=await page.evaluate(id=>{
   const input=document.querySelector(`[data-five-library-field="${id}"]`);fixture.keptInput=input;fixture.keptCanvas=fixture.view.viewport.renderer.domElement;fixture.listenerCalls=0;
   input.addEventListener('retention-probe',()=>fixture.listenerCalls++);input.focus({preventScroll:true});input.setSelectionRange(1,3);
   const scroll=['.cam-context-pane','.cam-info-pane'].map((selector,i)=>{const node=document.querySelector(selector);node.scrollTop=40+i*60;return node.scrollTop;});
   fixture.releaseLibrary();return scroll;
  },managed.id);
  await check();
  assert.equal(await page.evaluate(()=>{const e=fixture.keptInput;e.dispatchEvent(new Event('retention-probe'));return e.isConnected&&document.activeElement===e&&e.selectionStart===1&&e.selectionEnd===3&&fixture.listenerCalls===1&&fixture.keptCanvas===fixture.view.viewport.renderer.domElement;}),true);
  assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane'].map(s=>document.querySelector(s).scrollTop)),keep);
  await page.evaluate(()=>{fixture.view.viewportSceneProxy.invoke=fixture.originalInvoke;document.body.classList.remove('library-scroll-test');});
  report.cases.push('Delayed real library read preserves input identity/listener, focus, selection, viewport and both sidebar scroll positions');
  await save(resolve(output,'来源.i5cam'));
  await create('复用工装的新工件');await importPart(other);await tab('workpiece');
  state=await read();assert.equal(state.fixtures.length,managed.partCount);const before=structuredClone(state.fixtures);
  await openLibrary();await click(`five.fixture.library.use:${single.id}`);assert.equal((await read()).fixtures.length,managed.partCount+single.partCount);
  await openLibrary();await click(`five.fixture.library.delete:${single.id}`);assert.equal((await read()).fixtures.length,managed.partCount+single.partCount);
  await click('five.fixture.library.close');await click(`five.preparation.unit:${state.fixtureUnits.find(u=>u.partIds.includes(before[0].id)).id}`);
  await page.locator('[data-five-fixture-field="x"]').fill('450');await click('five.fixture.apply');
  await page.screenshot({path:resolve(output,'reused-fixtures.png')});
  const path=resolve(output,'复用.i5cam');await save(path);
  writeFileSync(resolve(output,'expected.json'),JSON.stringify({path,state:await read(),defaultId:managed.id,defaultPartCount:managed.partCount,entryIds:(await library()).entries.map(e=>e.id).sort(),before},null,2));
  report.cases.push('A different real workpiece automatically receives the imported default; manual library use, library deletion and instance-only position editing work independently');
 }else{
  const expected=JSON.parse(readFileSync(resolve(output,'expected.json'),'utf8'));
  await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),expected.path);await page.waitForFunction(()=>__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId);await bind();
  assert.deepEqual((await read()).fixtures,expected.state.fixtures);
  const saved=await library();assert.equal(saved.defaultId,expected.defaultId);assert.deepEqual(saved.entries.map(e=>e.id).sort(),expected.entryIds);
  await create('重启后默认工装');await importPart(cover);await tab('workpiece');
  const state=await read();assert.equal(state.fixtures.length,expected.defaultPartCount);
  const original=expected.before.map(f=>f.transform).sort((a,b)=>a[3]-b[3]);assert.deepEqual(state.fixtures.map(f=>f.transform).sort((a,b)=>a[3]-b[3]),original);
  await openLibrary();await page.screenshot({path:resolve(output,'reopened-library.png')});
  report.cases.push('Fresh application process restores user library/default, reopens all embedded project fixtures after a library deletion, and applies the unmodified default positions to another new project');
 }
 assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}

