import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { gizmoDriver } from './FiveAxisPlacementGizmo.native.browser.mjs';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';
import { verifyCompactViewCube } from './FiveAxisCompactViewCube.native.browser.mjs';

export async function verifyContextTransform({page,command,read,rpc,refresh,base,output}) {
  const cases=[],{pick,drag,numeric}=gizmoDriver(page),idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveWorkpieceSwitching);
  const toolbar=()=>page.getByRole('toolbar',{name:'工件变换'});
  const mode=()=>page.evaluate(()=>fixture.view.viewport.transformManipulator?.control.mode??null);
  const focusCanvas=()=>page.locator('.icax-three-viewport canvas').first().focus();
  const press=async key=>{await focusCanvas();await page.keyboard.press(key);};
  const choose=async title=>{await toolbar().getByRole('button',{name:title,exact:true}).click();};
  const near=(a,b)=>{assert.equal(a.length,b.length);a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-6,`${a} != ${b}`));};
  const initial=await read(),first=initial.placements[0].id;
  await toolbar().waitFor();assert.equal(await mode(),'translate');
  const unhighlighted=()=>page.evaluate(()=>{const v=fixture.view.viewport,o=v.sceneObjects.get(fixture.view.scene.activePlacementId);let tinted=false;o.traverse(n=>{if(n.userData.selectionMaterialState)tinted=true;});return v.selectedObjectIds.size===0&&!tinted;});
  assert.equal(await unhighlighted(),true);
  cases.push(...await verifyCompactViewCube({page,read,output}));
  const clickMesh=async()=>{
    const point=await page.evaluate(async()=>{
      const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view.viewport,c=v.renderer.domElement,r=c.getBoundingClientRect(),object=v.sceneObjects.get(fixture.view.scene.activePlacementId),ray=new T.Raycaster();
      for(let y=r.top+100;y<r.bottom-85;y+=10)for(let x=r.left+25;x<r.right-25;x+=10){
        if(document.elementFromPoint(x,y)!==c)continue;
        ray.setFromCamera({x:(x-r.left)/r.width*2-1,y:1-(y-r.top)/r.height*2},v.camera);
        if(ray.intersectObject(object,true).length)return {x,y};
      }
      throw Error('No visible workpiece surface');
    });await page.mouse.click(point.x,point.y);
  };
  await clickMesh();await toolbar().waitFor();assert.equal(await mode(),'translate');assert.equal(await unhighlighted(),true);assert.deepEqual(await read(),initial);
  assert.equal(await toolbar().getByRole('button').count(),3);assert.equal(await toolbar().getByRole('button',{name:'平移'}).getAttribute('aria-pressed'),'true');
  await choose('旋转');assert.equal(await mode(),'rotate');await choose('缩放');assert.equal(await mode(),'scale');await choose('平移');assert.equal(await mode(),'translate');
  assert.deepEqual(await read(),initial);
  cases.push('The loaded workpiece is already active with no whole-solid highlight; surface clicks preserve it and nearby buttons switch modes without native writes');

  const blank=await page.evaluate(()=>{const c=fixture.view.viewport.renderer.domElement,r=c.getBoundingClientRect();return {x:r.left+35,y:r.top+r.height-85};});
  await choose('旋转');
  for(const button of ['left','right']){await page.mouse.click(blank.x,blank.y,{button});assert.equal(await mode(),'rotate');assert.equal(await toolbar().count(),1);assert.equal(await unhighlighted(),true);}
  await press('Escape');assert.equal(await mode(),'rotate');assert.equal(await toolbar().count(),1);assert.deepEqual(await read(),initial);
  await choose('平移');
  cases.push('Blank left/right clicks and Escape retain the active workpiece, current transform mode and toolbar without highlighting it');

  for(const [key,expected] of [['e','rotate'],['r','scale'],['w','translate']]){await press(key);assert.equal(await mode(),expected);}
  for(const key of ['Control+e','Alt+r','Shift+e','Meta+r']){await press(key);assert.equal(await mode(),'translate');}
  await page.evaluate(()=>fixture.view.viewport.renderer.domElement.dispatchEvent(new KeyboardEvent('keydown',{key:'e',bubbles:true,isComposing:true})));
  assert.equal(await mode(),'translate');
  await page.evaluate(()=>{const e=new KeyboardEvent('keydown',{key:'r',bubbles:true,cancelable:true});e.preventDefault();fixture.view.viewport.renderer.domElement.dispatchEvent(e);});assert.equal(await mode(),'translate');
  await pick('X');await numeric().fill('1');await numeric().press('e');assert.equal(await numeric().inputValue(),'1e');assert.equal(await mode(),'translate');assert.deepEqual(await read(),initial);
  await numeric().fill('7');await choose('平移');assert.equal(await numeric().inputValue(),'7');
  await choose('旋转');assert.equal(await numeric().count(),0);assert.deepEqual(await read(),initial);
  cases.push('W/E/R match Unity; modifiers, composition, handled keys and typing are excluded; the current-mode button preserves drafts and changing mode cancels unsubmitted input');

  for(const projection of ['perspective','orthographic'])for(const width of [1024,1440]){
    await page.setViewportSize({width,height:900});await page.locator(`button[data-projection-mode="${projection}"]`).click();await press('w');
    const check=()=>page.evaluate(()=>{const v=fixture.view.viewport,c=v.renderer.domElement.getBoundingClientRect(),t=document.querySelector('.five-placement-tools').getBoundingClientRect(),m=v.transformManipulator,p=m.proxy.position.clone().project(v.camera);return {inside:t.left>=c.left&&t.right<=c.right&&t.top>=c.top&&t.bottom<=c.bottom,width:t.width,height:t.height,distance:Math.hypot(t.x+t.width/2-(c.x+(p.x+1)*c.width/2),t.y-(c.y+(1-p.y)*c.height/2))};});
    let layout=await check();assert.ok(layout.inside&&layout.width<270&&layout.height<45&&layout.distance<180,JSON.stringify(layout));
    const r=await page.locator('.icax-three-viewport canvas').first().boundingBox();
    await page.mouse.move(r.x+r.width*.35,r.y+130);await page.mouse.down({button:'middle'});await page.mouse.move(r.x+r.width*.35+35,r.y+145,{steps:8});await page.mouse.up({button:'middle'});
    await page.keyboard.down('Control');await page.mouse.down({button:'middle'});await page.mouse.move(r.x+r.width*.35+55,r.y+160,{steps:8});await page.mouse.up({button:'middle'});await page.keyboard.up('Control');
    layout=await check();assert.ok(layout.inside&&layout.distance<180,JSON.stringify(layout));assert.deepEqual(await read(),initial);
  }
  await page.setViewportSize({width:1440,height:900});await page.locator('button[data-projection-mode="perspective"]').click();await page.evaluate(()=>{fixture.view.viewport.setStandardView('top-front-right');fixture.view.viewport.fitView();});
  await page.screenshot({path:resolve(output,'workpiece-context-move.png')});await choose('旋转');await page.screenshot({path:resolve(output,'workpiece-context-rotate.png')});
  cases.push('The compact toolbar follows the object under orbit/pan and stays inside narrow/wide viewports in both projections');

  await press('w');await drag('X',15,false);await page.keyboard.press('e');assert.equal(await mode(),'translate');await page.keyboard.press('Escape');await page.mouse.up();assert.deepEqual(await read(),initial);
  await page.keyboard.press('Escape');assert.equal(await toolbar().count(),1);assert.equal(await mode(),'translate');
  await page.locator(`[data-five-workpiece="${first}"]`).click();assert.equal(await mode(),'translate');
  cases.push('Mode shortcuts cannot interrupt a held left drag; Escape cancels the gesture while keeping Move active');

  await pick('X');await numeric().fill('4');await page.evaluate(()=>{fixture.holdAfterApply=true;});await numeric().press('Enter');await page.waitForFunction(()=>fixture.held);
  const held=await mode();await press('r');assert.equal(await mode(),held);assert.equal(await toolbar().locator('button:disabled').count(),3);
  await page.evaluate(()=>fixture.release());await idle();const moved=[...base];moved[3]+=4;near((await read()).placements[0].transform,moved);
  await command('five.tool.select');await rpc({action:'undo'});await refresh();near((await read()).placements[0].transform,base);await rpc({action:'redo'});await refresh();near((await read()).placements[0].transform,moved);
  cases.push('Pending save disables switches and ignores shortcuts; Enter saves once and native undo/redo remain exact');

  await page.locator(`[data-five-workpiece="${first}"]`).click();await choose('旋转');
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await idle();await focusCanvas();await page.keyboard.press('r');assert.equal(await toolbar().count(),0);assert.equal(await mode(),null);
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('workpiece'));await idle();
  await toolbar().waitFor();assert.equal(await mode(),'rotate');assert.equal(await unhighlighted(),true);
  cases.push('The workpiece toolbar and shortcuts do not enter CAM or interfere with other editing areas');

  if(process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL){
    await selectWorkpieceFile(page,process.env.ICAX_FIVE_AXIS_PLACEMENT_MODEL);await idle();const two=await read(),second=two.placements.find(p=>p.id!==first);assert.ok(second);
    await toolbar().waitFor();assert.equal(await mode(),'translate');assert.equal(await page.evaluate(()=>fixture.view.fivePlacement.placementId),second.id);await choose('缩放');
    await page.locator(`[data-five-workpiece="${first}"]`).click();await idle();assert.equal(await mode(),'translate');assert.equal(await page.evaluate(()=>fixture.view.fivePlacement.placementId),first);
    near((await read()).placements.find(p=>p.id===first).transform,moved);assert.deepEqual((await read()).placements.find(p=>p.id===second.id),second);
    cases.push('Switching between two imported workpieces opens Move on the chosen object and preserves both independent placements');
  }
  const saved=await read();await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.placements,saved.placements);
  await page.evaluate(async()=>{const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);});
  await idle();await toolbar().waitFor();assert.equal(await mode(),'translate');assert.equal(await unhighlighted(),true);
  await page.screenshot({path:resolve(output,'reopened-active-workpiece.png')});
  cases.push('Save/reopen preserves exact placements and automatically restores Move plus the toolbar without whole-solid highlighting');
  return cases;
}
