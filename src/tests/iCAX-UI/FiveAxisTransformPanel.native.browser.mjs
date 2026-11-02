import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { gizmoDriver } from './FiveAxisPlacementGizmo.native.browser.mjs';

export async function verifyTransformPanel({page,command,read,refresh,rpc,base,output}){
  const cases=[],{pick,drag,numeric}=gizmoDriver(page),idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const values=group=>page.locator(`[data-five-transform-value^="${group}-"]`).evaluateAll(ns=>ns.map(n=>Number(n.value)));
  const near=(a,b)=>{assert.equal(a.length,b.length);a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-5,`${a} != ${b}`));};
  const savedMatrix=async()=>{const s=await read();return s.placements.find(p=>p.id===s.placements[0].id).transform;};
  await command('five.tool.select');near(await values('position'),[10,20,30]);near(await values('rotation'),[0,0,90]);near(await values('scale'),[1,1,1]);near(await savedMatrix(),base);
  assert.equal(await page.locator('.five-transform-parameters input[readonly]').count(),0);
  assert.equal(await page.locator('[data-five-command="five.transform.lock"]').count(),0);
  assert.equal(await page.locator('[data-five-key="transform-matrix"],[data-five-transform-value^="matrix-"]').count(),0);
  for(const width of [1024,1440]){await page.setViewportSize({width,height:900});const rects=await page.evaluate(()=>{const box=s=>{const r=document.querySelector(s).getBoundingClientRect();return {left:r.left,right:r.right,width:r.width};};return {panel:box('.cam-info-pane'),canvas:box('canvas'),left:box('.cam-context-pane')};});assert.ok(rects.panel.width>150&&rects.panel.left>=rects.canvas.right-2&&rects.left.width>100);}
  cases.push('saved matrix decomposes to position, XYZ Euler degrees and transform scale; panel is docked at right at two widths');
  for(const projection of ['perspective','orthographic']){
    await page.locator(`[data-projection-mode="${projection}"]`).click();await page.locator('[data-transform-space="local"]').click();
    await command('five.tool.translate');await pick('X');const before=await read();await numeric().fill('7.5');near(await values('position'),[10,27.5,30]);assert.deepEqual(await read(),before);assert.equal(await page.locator('[data-five-transform-status]').textContent(),'预览中');
    const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1700px}'});
    await page.evaluate(()=>{fixture.transformNodes={canvas:document.querySelector('canvas'),input:document.querySelector('[data-five-placement-field]'),output:document.querySelector('[data-five-transform-value="position-X"]')};fixture.holdMethod='CamPlacement.SetTransform';fixture.hold=true;});
    await numeric().press('Enter');await page.waitForFunction(()=>fixture.held);
    const scroll=await page.evaluate(()=>{fixture.transformNodes.input.focus({preventScroll:true});fixture.transformNodes.input.setSelectionRange(1,3);return ['.cam-context-pane','.cam-info-pane'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=70+i*20;return n.scrollTop;});});assert.ok(scroll.every(n=>n>0));
    await page.evaluate(()=>fixture.release());await idle();near(await values('position'),[10,27.5,30]);
    assert.equal(await page.evaluate(()=>{const p=fixture.transformNodes;return p.canvas===document.querySelector('canvas')&&p.input===document.querySelector('[data-five-placement-field]')&&p.output===document.querySelector('[data-five-transform-value="position-X"]')&&document.activeElement===p.input;}),true);
    assert.deepEqual(await numeric().evaluate(n=>[n.selectionStart,n.selectionEnd]),[1,3]);assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane'].map(s=>document.querySelector(s).scrollTop)),scroll);
    await stress.evaluate(n=>n.remove());await command('five.tool.select');await rpc({action:'undo'});await refresh();near(await savedMatrix(),base);near(await values('position'),[10,20,30]);
    await page.evaluate(()=>fixture.view.viewport.fitView());await command('five.tool.rotate');await pick('Z');await numeric().fill('20');near(await values('rotation'),[0,0,110]);await numeric().press('Escape');near(await values('rotation'),[0,0,90]);
    await command('five.tool.scale');await pick('XYZ');await numeric().fill('1.5');near(await values('scale'),[1.5,1.5,1.5]);await numeric().press('Enter');await idle();near(await values('scale'),[1,1,1]);near(await savedMatrix(),base);
    await command('five.tool.select');await rpc({action:'undo'});await refresh();
    cases.push(`${projection}: translation preview and commit, rotation cancel, geometric scale preview/commit, undo; canvas/input/output identity, latest dual scroll, focus and selection preserved`);
  }
  cases.push(...await verifyTransformEdits({page,command,read,refresh,rpc,base,near,values}));
  await page.locator('[data-transform-space="world"]').click();await command('five.tool.translate');
  const original=await values('position');await drag('X',35,false);assert.notDeepEqual(await values('position'),original);await page.mouse.up();await idle();
  const saved=await read();await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.placements,saved.placements);
  await page.evaluate(async()=>{const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);});
  await idle();await page.waitForFunction(()=>fixture.view.viewport?.getDebugState().objectCount===1);await command('five.tool.select');assert.equal(await page.evaluate(()=>fixture.view.error),'');near(await values('position'),[reopened.placements[0].transform[3],reopened.placements[0].transform[7],reopened.placements[0].transform[11]]);
  await page.screenshot({path:resolve(output,'workpiece-transform-panel.png')});cases.push('actual pointer drag updates outputs before release; native transform persists through save/reopen');
  return cases;
}

async function verifyTransformEdits({page,command,read,refresh,rpc,base,near,values}){
  const field=(group,axis)=>page.locator(`[data-five-transform-field="${group}-${axis}"]`),idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const matrix=async()=>(await read()).placements[0].transform;
  const undo=async()=>{await rpc({action:'undo'});await refresh();near(await matrix(),base);};
  await command('five.tool.select');assert.equal(await page.locator('[data-five-transform-field][readonly]').count(),0);
  const unchanged=await read();await field('position','X').fill('12.5');assert.deepEqual(await read(),unchanged);near(await values('position'),[12.5,20,30]);
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1700px}'});
  await page.evaluate(()=>{fixture.editNodes={canvas:document.querySelector('canvas'),input:document.querySelector('[data-five-transform-field="position-X"]')};fixture.holdMethod='CamPlacement.SetTransform';fixture.hold=true;});
  await field('position','X').press('Enter');await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const n=fixture.editNodes.input;n.focus({preventScroll:true});n.setSelectionRange(1,3);return ['.cam-context-pane','.cam-info-pane'].map((s,i)=>{const p=document.querySelector(s);p.scrollTop=75+i*25;return p.scrollTop;});});assert.ok(scroll.every(v=>v>0));
  await page.evaluate(()=>fixture.release());await idle();assert.equal(await page.evaluate(()=>{const n=fixture.editNodes;return n.canvas===document.querySelector('canvas')&&n.input===document.querySelector('[data-five-transform-field="position-X"]')&&document.activeElement===n.input;}),true);assert.deepEqual(await field('position','X').evaluate(n=>[n.selectionStart,n.selectionEnd]),[1,3]);assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane'].map(s=>document.querySelector(s).scrollTop)),scroll);
  await stress.evaluate(n=>n.remove());near(await matrix(),base.map((v,i)=>i===3?12.5:v));await undo();
  await field('rotation','X').fill('15');await field('rotation','X').press('Enter');await idle();const a=15*Math.PI/180;near(await matrix(),[0,-1,0,10,Math.cos(a),0,-Math.sin(a),20,Math.sin(a),0,Math.cos(a),30,0,0,0,1]);await undo();
  await field('position','Z').fill('42');await field('position','Z').press('Tab');await idle();near(await matrix(),base.map((v,i)=>i===11?42:v));await undo();
  await field('position','X').fill('abc');await field('position','X').press('Enter');await idle();near(await matrix(),base);assert.ok(await page.evaluate(()=>fixture.view.error));await field('position','X').press('Escape');near(await values('position'),[10,20,30]);
  const bounds=()=>page.evaluate(()=>{const o=[...fixture.view.viewport.sceneObjects.values()][0];o.geometry.computeBoundingBox();return o.geometry.boundingBox.getSize(o.position.clone()).toArray();});
  const size=await bounds();await field('scale','Y').fill('1.25');near(await values('scale'),[1.25,1.25,1.25]);await field('scale','Y').press('Enter');await idle();near(await bounds(),size.map(v=>v*1.25));near(await matrix(),base);near(await values('scale'),[1,1,1]);await undo();
  for(const invalid of ['0','-1','abc']){await field('scale','X').fill(invalid);await field('scale','X').press('Enter');await idle();near(await matrix(),base);near(await bounds(),size);assert.ok(await page.evaluate(()=>fixture.view.error));await field('scale','X').press('Escape');}
  await field('position','Y').fill('31');await field('position','Y').press('Enter');await idle();near(await matrix(),base.map((v,i)=>i===7?31:v));await undo();
  await page.getByRole('button',{name:'解锁参数编辑',exact:true}).click();await field('position','X').fill('14');await page.evaluate(()=>{fixture.dropApply=true;fixture.dropQuery=true;});await field('position','X').press('Enter');await idle();assert.ok(await page.evaluate(()=>fixture.view.fivePlacement?.request));const applied=await read();await field('position','X').press('Enter');await idle();assert.deepEqual(await read(),applied);await undo();
  await field('position','Y').fill('27');await page.evaluate(()=>fixture.entry.prepareProjectSave(fixture.context));await idle();near(await matrix(),base.map((v,i)=>i===7?27:v));await undo();
  return ['an unlocked workpiece exposes nine numeric fields; absolute position and XYZ rotation commit via native placement and undo; blur submits; invalid text and Escape leave native geometry unchanged','editing preserves canvas, input node, latest left/right scroll, focus and selection across delayed native response','linked XYZ scale changes exact native geometry, returns to the new geometry baseline, and rejects zero/negative/non-numeric factors','lost reply retries idempotently; project save flushes pending transform edits; the global workpiece lock is covered by the dedicated management acceptance'];
}
