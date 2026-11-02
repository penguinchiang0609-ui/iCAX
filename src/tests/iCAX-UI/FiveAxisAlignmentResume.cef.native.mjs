// Owned standalone application: production AppShell, SDO polling and resources.
// File paths are supplied to the production import/save APIs; all geometry picks,
// alignment confirms, gizmo drags and workpiece switches use actual pointer input.
import assert from 'node:assert/strict';
import {writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {frameAlignmentDriver} from './FiveAxisFrameAlignment.native.browser.mjs';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
import {activeHandles,worldGeometry,identity} from './FiveAxisAlignmentResume.native.browser.mjs';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/alignment-resume/standalone');mkdirSync(output,{recursive:true});
const runtime=resolve(process.env.ICAX_CEF_RUNTIME||'output/tests/alignment-resume/cef-runtime'),base=pathToFileURL(runtime+'/').href;
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${process.env.ICAX_CEF_PORT||9438}`),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');assert.ok(page,'Owned AppShell tab required');page.setDefaultTimeout(30000);
const report={passed:false,standaloneCef:true,realNativeQueue:true,realResourceBridge:true,cases:[],errors:[]};page.on('pageerror',e=>report.errors.push(e.stack));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fiveAlignment?.solving&&!fixture.view.fiveAlignment?.picking);
const read=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}));
async function history(name,expected,preserveInteraction=false){
  let stress;
  if(preserveInteraction){
    stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1700px}'});
    await page.evaluate(()=>{
      fixture.historyNodes={canvas:fixture.view.viewport.renderer.domElement,input:document.querySelector('[data-five-transform-field="position-X"]')};
      const proxy=fixture.context.sceneProxy,invoke=proxy.invoke;
      proxy.invoke=async function(method,...args){
        const result=await invoke.call(this,method,...args);
        if(method==='CamScene.ReadState'){proxy.invoke=invoke;await new Promise(resolve=>fixture.releaseHistory=resolve);}
        return result;
      };
    });
  }
  await page.getByRole('button',{name,exact:true}).click();
  let interaction;
  if(preserveInteraction){
    await page.waitForFunction(()=>fixture.releaseHistory);
    interaction=await page.evaluate(()=>{
      const n=fixture.historyNodes;n.input.focus({preventScroll:true});n.input.setSelectionRange(0,1);
      return ['.cam-context-pane','.cam-info-pane'].map((s,i)=>{const p=document.querySelector(s);p.scrollTop=80+i*30;return p.scrollTop;});
    });
    await page.evaluate(()=>fixture.releaseHistory());
  }
  // A real toolbar click starts asynchronously; an idle sample before its
  // command dispatch is not evidence that the history operation completed.
  await page.waitForFunction(async expected=>{
    if(__icaxAppShell.getState().pendingCount||fixture.view.pending)return false;
    const state=await fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{});
    return JSON.stringify(state.placements)===JSON.stringify(expected.placements)
      &&JSON.stringify(state.workpieces)===JSON.stringify(expected.workpieces);
  },expected);
  await idle();await activeHandles(page);
  if(preserveInteraction){
    assert.equal(await page.evaluate(()=>{const n=fixture.historyNodes;return n.canvas===fixture.view.viewport.renderer.domElement&&n.input===document.querySelector('[data-five-transform-field="position-X"]')&&document.activeElement===n.input;}),true);
    assert.deepEqual(await page.locator('[data-five-transform-field="position-X"]').evaluate(n=>[n.selectionStart,n.selectionEnd]),[0,1]);
    assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane'].map(s=>document.querySelector(s).scrollTop)),interaction);
    await stress.evaluate(n=>n.remove());
    report.cases.push({name:'Native AppShell history refresh preserves canvas/input nodes, focus, caret selection and both sidebar scroll positions changed during its response'});
  }
}
const near=(a,b,t=5e-5)=>{assert.equal(a.length,b.length);a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<t,`${v} != ${b[i]} at ${i}`));};
const bind=()=>page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);
const action=async a=>{await page.locator(`[data-five-command="five.align.${a}"]`).last().click();await idle();};
async function align(curved=true){
  const fixtureId=await page.evaluate(()=>fixture.view.scene.activePlacementId);
  const driver=await frameAlignmentDriver(page,base);await driver.open();
  await driver.pick('plane',curved?{prefix:'curve-plane:',helperType:'plane',empty:true,screenshot:resolve(output,'direct-plane.png')}:{prefix:'plane:'});
  await driver.action('next');
  if(!curved){await driver.pick('direction',{construction:'PlaneNormal',helperType:'direction',screenshot:resolve(output,'direct-normal.png')});await driver.action('reselect');}
  await driver.action('basis-u');await driver.action('next');
  await driver.pick('datum-point',{construction:'PlaneCenter',screenshot:resolve(output,'direct-centre.png')});
  await page.evaluate(()=>fixture.context=fixture.view.fiveAlignment.context);
  const world=await worldGeometry(page),began=Date.now();await action('apply');await activeHandles(page);
  near((await read()).placements.find(p=>p.id===fixtureId).transform,identity);
  report.cases.push({name:'Reset object frame from the ribbon; four steps and unconditional identity placement',milliseconds:Date.now()-began});
  near((await worldGeometry(page)).points,world.points);await page.waitForTimeout(800);await activeHandles(page);
  await page.evaluate(()=>{fixture.view.viewport.setStandardView('top-front-right');fixture.view.viewport.fitView(1.25);});
}

try{
  await page.setViewportSize({width:1600,height:952});
  await page.waitForFunction(()=>window.__icaxAppShell?.createProject);
  await page.evaluate(p=>__icaxAppShell.createProject({projectName:'摆放与自身坐标系重置验收',projectPath:p,productId:'icax.five-axis-cam'}),resolve(output,`placement-frame-${Date.now()}.i5cam`));
  await page.waitForFunction(()=>window.__icaxWorkbench?.importWorkpiece&&__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId);
  await bind();const modelRoot=resolve('TestData/FA/刀路/刀路/整理测试刀路');
  await page.waitForFunction(()=>!!fixture.view.scene?.domain&&!fixture.view.pending);
  for(const name of ['摆放','自身坐标系重置'])assert.equal(await page.getByRole('button',{name,exact:true}).isDisabled(),true);
  await page.evaluate(p=>__icaxWorkbench.importWorkpiece(p),resolve(modelRoot,'多槽圆盖-3-7b202.igs'));await idle();await activeHandles(page);
  await page.evaluate(()=>{fixture.view.viewport.setStandardView('top-front-right');fixture.view.viewport.fitView(1.25);});
  const imported=await read();
  for(const name of ['摆放','自身坐标系重置'])assert.equal(await page.getByRole('button',{name,exact:true}).isDisabled(),false);
  assert.equal(await page.getByRole('toolbar',{name:'工件变换'}).getByRole('button').count(),3);
  await page.getByRole('button',{name:'旋转',exact:true}).click();
  await page.getByRole('button',{name:'摆放',exact:true}).click();await activeHandles(page);
  const menuDriver=await frameAlignmentDriver(page,base);await menuDriver.open();
  await page.getByRole('button',{name:'摆放',exact:true}).click();await activeHandles(page);
  assert.deepEqual(await read(),imported);
  await page.screenshot({path:resolve(output,'placement-and-frame-reset.png')});
  report.cases.push({name:'Two real ribbon entries, disabled without a workpiece; Placement opens Move with exactly three nearby modes; switching from reset to Placement cancels a read-only draft'});
  const cover=await page.evaluate(()=>fixture.view.scene.activePlacementId);
  await align();const beforeDrag=await read();await gizmoDriver(page).drag('X',20);await idle();const dragged=await read();assert.notDeepEqual(dragged.placements,beforeDrag.placements);assert.deepEqual(dragged.workpieces,beforeDrag.workpieces);
  await history('撤销',beforeDrag,true);
  report.cases.push({name:'Immediate real mouse drag after confirm and AppShell undo'});
  const beforeReset=await read();await align();const coverFrame=await read();near(coverFrame.placements.find(p=>p.id===cover).transform,identity);
  await history('撤销',beforeReset);
  await history('重做',coverFrame);
  await page.evaluate(p=>__icaxWorkbench.importWorkpiece(p),resolve(modelRoot,'多种斜孔-多种边-206.STEP'));await idle();await activeHandles(page);
  const second=await page.evaluate(()=>fixture.view.scene.activePlacementId);await align(false);const two=await read();near(two.placements.find(p=>p.id===second).transform,identity);
  await page.locator(`[data-five-workpiece="${cover}"]`).click();await idle();await activeHandles(page);
  assert.equal(await page.evaluate(()=>fixture.view.fivePlacement.placementId),cover);
  assert.deepEqual((await read()).placements,two.placements);report.cases.push({name:'Two real imported workpieces retain independent frames and active handles when switching'});
  await page.getByRole('button',{name:'自身坐标系重置',exact:true}).click();await page.evaluate(()=>fixture.context=fixture.view.fiveAlignment.context);await action('close');
  const saved=await read(),world=await worldGeometry(page),projectPath=resolve(output,`aligned-real-parts-${saved.jobId}.i5cam`);report.projectPath=projectPath;
  const savedResult=await page.evaluate(p=>fixture.context.projectProxy.save(p),projectPath);assert.equal(savedResult.saved,true);
  const closing=page.evaluate(()=>__icaxAppShell.closeProject(__icaxAppShell.getState().activeProjectId));await page.locator('[data-project-close-choice="discard"]').click();await closing;
  const opened=await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),projectPath);assert.equal(opened.error,'');await bind();await idle();await activeHandles(page);
  await page.locator(`[data-five-workpiece="${cover}"]`).click();await idle();await activeHandles(page);
  const reopened=await read();assert.deepEqual(reopened.placements,saved.placements);assert.deepEqual(reopened.workpieces,saved.workpieces);near((await worldGeometry(page)).points,world.points);
  await page.getByRole('button',{name:'自身坐标系重置',exact:true}).click();await action('close');await activeHandles(page);
  report.cases.push({name:'Native project save, actual AppShell close/reopen, world geometry unchanged and repeat alignment works'});
  await page.screenshot({path:resolve(output,'reopened-aligned-workpiece.png')});assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
