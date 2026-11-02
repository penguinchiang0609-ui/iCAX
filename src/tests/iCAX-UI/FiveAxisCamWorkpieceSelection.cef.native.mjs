import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {mkdirSync,copyFileSync,writeFileSync} from 'node:fs';
import {chromium} from 'playwright';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/cam-workpiece-selection-20261026');
mkdirSync(output,{recursive:true});
const project=resolve(output,'CAM工件不可选验收.i5cam');
copyFileSync(process.env.ICAX_CAM_SELECTION_PROJECT||'output/tests/fixtures/path-edit-fixture.i5cam',project);
const base=pathToFileURL(resolve(process.env.ICAX_CAM_SELECTION_ASSET_ROOT||'src')+'/').href;
const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT||'http://127.0.0.1:9597');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/iCAX-UI/SDK/AppShell/index.html'));
assert.ok(page,'standalone CAM AppShell');page.setDefaultTimeout(45000);
const report={passed:false,standaloneCef:true,realNativeQueue:true,cases:[],errors:[]};
page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!!fixture.view.scene?.domain);
const settled=async()=>{
  await page.evaluate(()=>fixture.stability=null);
  await page.waitForFunction(()=>{
    const v=fixture.view,vp=v.viewport;if(v.pending||!vp)return false;
    const r=vp.renderer.domElement.getBoundingClientRect(),key=JSON.stringify([vp.getCameraState(),r.x,r.y,r.width,r.height,vp.appliedViewRevision]);
    if(fixture.stability?.key!==key){fixture.stability={key,since:performance.now()};return false;}
    return performance.now()-fixture.stability.since>=300;
  });
};
const area=async id=>{await page.locator(`[data-tab-id="${id}"]`).click();await idle();await settled();};
const unselected=async()=>assert.deepEqual(await page.evaluate(()=>({id:fixture.view.selectedSceneObjectId,ids:[...fixture.view.viewport.selectedObjectIds]})),{id:'',ids:[]});
const state=()=>page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
try{
  await page.waitForFunction(()=>__icaxAppShell?.getState().startupPhase==='ready');
  await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),project);
  await page.evaluate(async base=>{
    const store=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');
    window.fixture={base};Object.defineProperty(fixture,'view',{get:()=>store.getProjectView(__icaxAppShell.getState().activeProjectId)});
  },base);await idle();
  await page.waitForFunction(()=>!!fixture.view.viewport?.sceneObjects.get(fixture.view.scene.activePlacementId)?.geometry);
  const before=await state();
  await area('workpiece');
  // Exercise an inherited selection without depending on how a prior page selected it.
  await page.evaluate(()=>{const v=fixture.view;fixture.canvas=v.viewport.renderer.domElement;v.selectedSceneObjectId=v.scene.activePlacementId;v.viewport.setSelectedObjectIds([v.selectedSceneObjectId]);});
  await area('toolpath');await unselected();
  assert.equal(await page.evaluate(()=>fixture.canvas===fixture.view.viewport.renderer.domElement),true);
  report.cases.push('Entering CAM clears a pre-existing whole-workpiece selection and retains the viewport canvas');

  await page.evaluate(async()=>{
    fixture.model=await import(fixture.base+'apps/five-axis-cam/webpage/workspaceModel.mjs');
    const {nearestCutLine}=await import(fixture.base+'apps/five-axis-cam/webpage/cutSelection.mjs');
    fixture.findSpots=()=>{
      const view=fixture.view,vp=view.viewport,box=vp.renderer.domElement.getBoundingClientRect();
      vp.scene.updateMatrixWorld(true);vp.camera.updateMatrixWorld(true);
      const mesh=vp.sceneObjects.get(view.scene.activePlacementId);let solid=null,path=null;
      for(let y=.18;y<.84;y+=.012)for(let x=.18;x<.84;x+=.012){
        const e={clientX:box.left+x*box.width,clientY:box.top+y*box.height},cut=nearestCutLine(view,e);
        if(document.elementFromPoint(e.clientX,e.clientY)!==vp.renderer.domElement)continue;
        if(cut)path??={x:e.clientX,y:e.clientY,id:cut.intentId};
        else if(!solid){vp.raycaster.setFromCamera({x:x*2-1,y:1-y*2},vp.camera);if(vp.raycaster.intersectObject(mesh,false).length)solid={x:e.clientX,y:e.clientY};}
        if(path&&solid)return {solid,path};
      }
      return {solid,path};
    };
    const vp=fixture.view.viewport;vp.setViewDirection([1,-1,.8]);vp.fitView();
  });
  await settled();
  const spots=await page.evaluate(()=>fixture.findSpots());assert.ok(spots.solid);assert.ok(spots.path);report.spots=spots;
  // Capture real delivered mesh hits as well as the resulting selection.
  await page.evaluate(()=>{const vp=fixture.view.viewport,original=vp.options.onPick;vp.options.onPick=(...args)=>{fixture.lastHit=args[0]?.objectId||'';return original(...args);};});
  await page.mouse.click(spots.path.x,spots.path.y);await idle();await settled();await unselected();
  assert.equal(await page.evaluate(()=>fixture.model.workspaceState(fixture.view).selectedIntentId),spots.path.id);
  await page.screenshot({path:resolve(output,'刀路可选-工件无高亮.png')});
  for(const modifier of ['Control','Shift']){
    spots.solid=await page.evaluate(()=>fixture.findSpots().solid);assert.ok(spots.solid);
    await page.keyboard.down(modifier);
    try{await page.mouse.click(spots.solid.x,spots.solid.y);}finally{await page.keyboard.up(modifier);}
    await idle();await unselected();
    assert.equal(await page.evaluate(()=>fixture.model.workspaceState(fixture.view).selectedIntentId),spots.path.id);
  }
  await settled();spots.solid=await page.evaluate(()=>fixture.findSpots().solid);assert.ok(spots.solid);
  // A panel refresh may replace onPick after selecting a path. Observe this
  // model click directly instead of relying on a wrapper from an earlier render.
  await page.evaluate(()=>{const vp=fixture.view.viewport,original=vp.options.onPick;fixture.lastHit=null;vp.options.onPick=(...args)=>{fixture.lastHit=args[0]?.objectId||'';return original(...args);};});
  await page.mouse.click(spots.solid.x,spots.solid.y);await idle();await unselected();
  assert.equal(await page.evaluate(()=>fixture.lastHit),before.placements.find(p=>p.setupId===before.activeSetupId).id);
  assert.equal(await page.evaluate(()=>fixture.model.workspaceState(fixture.view).selectedIntentId),'');
  report.cases.push('Real clicks select a path; plain/Ctrl/Shift clicks on the solid never select or highlight the workpiece; plain model clicks clear the path selection');

  // A busy path handler must not allow the shared object-selection fallback either.
  await page.evaluate(()=>fixture.view.fiveToolAxisRequest=true);
  try{await page.mouse.click(spots.solid.x,spots.solid.y);await unselected();}
  finally{await page.evaluate(()=>fixture.view.fiveToolAxisRequest=false);}
  await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));await idle();await unselected();
  report.cases.push('Busy path handlers and a real native scene refresh cannot restore whole-workpiece selection');

  await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.tool.extract'));await idle();
  await page.locator('[data-five-command="five.extraction.mode.Region"]').click();await idle();
  await page.addStyleTag({content:'.five-position-dialog{left:8px!important;top:8px!important;width:320px!important;transform:none!important}'});
  await page.mouse.click(spots.solid.x,spots.solid.y);await idle();await unselected();
  const faces=await page.evaluate(()=>fixture.view.fiveAxisExtractionPick?.faces?.map(f=>f.localId));
  assert.ok(faces?.length,'real native face resolution still works');report.selectedFaces=faces;
  await page.screenshot({path:resolve(output,'手动选面仍可用.png')});
  await page.getByRole('dialog',{name:'添加刀路',exact:true}).getByRole('button',{name:'取消',exact:true}).click();await idle();
  report.cases.push('Manual machining-face picking still resolves actual native topology without selecting the whole workpiece');

  await area('workpiece');await idle();
  await page.locator('[data-five-command^="five.setup.activate:"]').first().click();await idle();
  assert.equal(await page.evaluate(()=>!!fixture.view.viewport.transformManipulator),true);
  await area('toolpath');await unselected();
  const after=await state();
  assert.deepEqual(after.placements,before.placements);assert.deepEqual(after.intentToolpaths,before.intentToolpaths);
  assert.deepEqual(after.workpieces,before.workpieces);
  await page.screenshot({path:resolve(output,'CAM工件不可选.png')});
  report.cases.push('Workpiece setup editing retains its manipulator; returning to CAM remains unselected and the native geometry/path definitions stay unchanged');
  assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
