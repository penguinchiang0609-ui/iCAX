import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {verifyResume,verifyFrameReset,verifyReopenedFrame} from './FiveAxisAlignmentResume.native.browser.mjs';

export async function verifyAlignment({page,command,read,rpc,invoke,refresh,base,output}){
  const cases=[],initial=await read(),id=initial.placements[0].id;
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAlignment?.picking&&!fixture.view.fiveAlignment?.solving);
  const action=async a=>{await page.locator(`[data-five-command="five.align.${a}"]`).last().click();await idle();};
  const state=()=>page.evaluate(()=>{const d=fixture.view.fiveAlignment;return {current:d.current,steps:d.steps,report:d.report,valid:d.valid,error:d.error};});
  const transform=()=>page.evaluate(()=>fixture.view.viewport.sceneObjects.get(fixture.view.scene.activePlacementId).matrix.clone().transpose().toArray());
  const map=(m,p,direction=false)=>[0,1,2].map(i=>(direction?0:m[i*4+3])+p.reduce((sum,v,j)=>sum+v*m[i*4+j],0));
  const near=(a,b)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-5,`${a} != ${b}`));
  const open=async(wait=true)=>{await page.locator(`[data-five-workpiece="${id}"]`).click();await page.getByRole('toolbar',{name:'工件变换'}).getByRole('button',{name:'对齐',exact:true}).click();await page.getByRole('dialog',{name:'对齐',exact:true}).waitFor();if(wait)await page.waitForFunction(()=>fixture.view.sceneSnapping.catalogs?.has(fixture.view.scene.activePlacementId));};
  const faceCamera=async()=>{await page.evaluate(()=>{const v=fixture.view.viewport,o=v.sceneObjects.get(fixture.view.scene.activePlacementId),p=o.position.clone().set(0,0,1).transformDirection(o.matrix);v.setViewDirection(p.toArray());v.fitView(1.7);});};
  const screen=point=>page.evaluate(point=>{const v=fixture.view.viewport,o=v.sceneObjects.get(fixture.view.scene.activePlacementId),p=o.position.clone().set(...point).applyMatrix4(o.matrixWorld).project(v.camera),r=v.renderer.domElement.getBoundingClientRect();return {x:r.x+(p.x+1)*r.width/2,y:r.y+(1-p.y)*r.height/2};},point);
  const click=async point=>{const p=await screen(point);await page.mouse.move(p.x,p.y);await page.mouse.click(p.x,p.y);await idle();const d=await state();assert.equal(d.error,'');assert.equal(d.valid,true);return d;};
  await open();await faceCamera();
  const calls=await page.evaluate(()=>fixture.snapCalls);
  for(const point of [[10,20,20],[20,30,20],[30,40,20]]){const p=await screen(point);await page.mouse.move(p.x,p.y);}
  await page.waitForTimeout(90);assert.equal(await page.evaluate(()=>fixture.snapCalls),calls);
  cases.push('After the BRep feature catalog loads, pointer motion performs local picking without additional native geometry queries');
  let s=await click([20,30,20]);assert.equal(s.current.kind,'plane');assert.deepEqual(await read(),initial);
  const pivot=map(base,[20,30,10]);await action('target:Y');near(map(await transform(),[0,0,1],true),[0,1,0]);near(map(await transform(),[20,30,10]),pivot);
  await action('flip');near(map(await transform(),[0,0,1],true),[0,-1,0]);await action('flip');
  await action('quarter');near(map(await transform(),[0,0,1],true),[0,1,0]);near(map(await transform(),[20,30,10]),pivot);
  await action('contact');assert.ok(Math.abs(map(await transform(),[20,30,20])[1])<1e-5);
  await page.screenshot({path:resolve(output,'plane-align-preview.png')});
  await action('close');near(await transform(),base);assert.deepEqual(await read(),initial);
  cases.push('Actual automatic face pick previews immediately; plane target, flip and 90 degree turn keep the pivot; explicit contact snaps to target plane; cancel restores native and displayed state');

  await open();await faceCamera();await click([20,30,20]);await action('target:Y');await action('next');await faceCamera();
  s=await click([20,.15,20]);assert.equal(s.current.kind,'direction');near(map(await transform(),[0,0,1],true),[0,1,0]);near(map(await transform(),[1,0,0],true),[1,0,0]);
  await action('target:Y');s=await state();assert.equal(s.valid,false);assert.match(s.error,/冲突/);assert.equal(await page.locator('[data-five-command="five.align.apply"]').isDisabled(),true);
  await action('target:X');await action('next');await faceCamera();s=await click([.1,.1,20]);assert.equal(s.current.kind,'point');
  near(map(await transform(),[0,0,20]),[0,0,0]);near(map(await transform(),[0,0,1],true),[0,1,0]);near(map(await transform(),[1,0,0],true),[1,0,0]);
  assert.deepEqual(await read(),initial);await page.screenshot({path:resolve(output,'progressive-align.png')});
  const preview=await transform();await action('apply');near((await read()).placements[0].transform,preview);
  const afterApply=await page.evaluate(()=>({tool:fixture.view.fiveAxisUI.tools.workpiece,placementOpen:fixture.view.fivePlacement?.open,placementMode:fixture.view.fivePlacement?.mode,manipulator:fixture.view.viewport.transformManipulator?.control.mode,toolbar:!!document.querySelector('.five-placement-tools'),error:fixture.view.error}));
  assert.deepEqual(afterApply,{tool:'translate',placementOpen:true,placementMode:'translate',manipulator:'translate',toolbar:true,error:''});
  assert.equal((await read()).placements[0].registrationMethod,'Alignment');
  await rpc({action:'undo'});await refresh();near((await read()).placements[0].transform,base);
  await rpc({action:'redo'});await refresh();near((await read()).placements[0].transform,preview);
  cases.push('Face, edge and point can progressively constrain placement; conflicting target is rejected; a single confirm saves all steps and one undo/redo restores the exact placement');
  await verifyResume({page,read,invoke,rpc,refresh,open,action});
  cases.push('Immediately after confirm: real gizmo drag, undo, repeat alignment, and native revision invalidation all restore usable Move handles');

  // A later stale preview cannot publish over a cancelled or replaced editor.
  await page.evaluate(()=>{fixture.view.snapFeatureCatalogs?.clear();fixture.view.sceneSnapping=null;fixture.holdMethod='CamScene.ReadSnapFeatures';fixture.hold=true;});await open(false);await page.waitForFunction(()=>fixture.held);
  await action('close');await page.evaluate(()=>fixture.release());await idle();assert.equal(await page.locator('.five-align-panel').count(),0);near(await transform(),preview);
  await open();await faceCamera();await click([20,30,20]);
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1700px}'});
  await page.evaluate(()=>{fixture.holdMethod='CamPlacement.SolveRegistration';fixture.hold=true;});
  await page.locator('[data-five-command="five.align.quarter"]').click();await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const input=document.querySelector('[data-five-transform-field="position-X"]');fixture.alignNodes={canvas:document.querySelector('canvas'),input};input.focus({preventScroll:true});input.setSelectionRange(0,1);return ['.cam-context-pane','.cam-info-pane'].map((s,i)=>{const p=document.querySelector(s);p.scrollTop=80+i*30;return p.scrollTop;});});
  assert.ok(scroll.every(n=>n>0));await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>{const n=fixture.alignNodes;return n.canvas===document.querySelector('canvas')&&n.input===document.querySelector('[data-five-transform-field="position-X"]')&&document.activeElement===n.input;}),true);
  assert.deepEqual(await page.locator('[data-five-transform-field="position-X"]').evaluate(n=>[n.selectionStart,n.selectionEnd]),[0,1]);
  assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane'].map(s=>document.querySelector(s).scrollTop)),scroll);await stress.evaluate(n=>n.remove());
  cases.push('Asynchronous alignment previews preserve canvas and parameter nodes, input focus/selection, and subsequent scroll positions in both sidebars');
  const draft=await state(),saved=await read();const payload={expected:saved.expected,placementId:id,setupId:saved.activeSetupId,registration:{method:'Alignment',modelFrame:'Workpiece',targetFrame:'Setup',unit:'mm',steps:[draft.current]}};
  const solved=await invoke('CamPlacement.SolveRegistration',payload);assert.equal(solved.qualified,true);
  const corrupted=structuredClone(payload);corrupted.registration.steps[0].source.featureId='plane:999999999';await assert.rejects(()=>invoke('CamPlacement.SolveRegistration',corrupted),/FEATURE_MISSING/);
  await action('close');assert.deepEqual(await read(),saved);
  cases.push('Cancelled asynchronous picks cannot reopen the editor or alter geometry; native solving re-resolves exact references and rejects fabricated face identities');

  await open();await faceCamera();await click([20,30,20]);await action('target:X');
  assert.equal(await page.locator('[data-five-command="five.transform.lock"]').isDisabled(),true);
  const retryPreview=await transform();await page.evaluate(()=>{fixture.dropApply=true;fixture.dropQuery=true;});await action('apply');
  assert.equal(await page.getByRole('button',{name:'核对结果',exact:true}).count(),1);
  assert.equal(await page.getByRole('button',{name:'取消',exact:true}).isDisabled(),true);
  const uncertain=await read();near(uncertain.placements[0].transform,retryPreview);await action('apply');assert.deepEqual(await read(),uncertain);
  await rpc({action:'undo'});await refresh();near((await read()).placements[0].transform,preview);
  await rpc({action:'redo'});await refresh();near((await read()).placements[0].transform,retryPreview);
  cases.push('Lost apply and receipt responses lock editing until the original command is checked; retry is idempotent and one undo restores the previous placement');

  await open();await faceCamera();await click([20,30,20]);await action('target:Y');
  await verifyFrameReset({page,read,invoke,rpc,refresh,action});
  cases.push('Explicit frame reset preserves every displayed world vertex and normal, rebases exact snap features, sets identity, and restores geometry plus placement in one undo/redo');
  await verifyReopenedFrame({page,rpc,read});
  cases.push('Save and reopen preserve rebased geometry and identity placement, and restore active Move handles');
  return cases;
}
