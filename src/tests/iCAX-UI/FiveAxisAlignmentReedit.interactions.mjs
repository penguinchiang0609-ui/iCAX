import assert from 'node:assert/strict';
import {resolve} from 'node:path';

export async function verifyAlignmentReedit({page,driver,read,output}){
  const cases=[],snapshot=()=>page.evaluate(()=>{const d=fixture.view.fiveAlignment;return structuredClone({plane:d.plane,x:d.x,origin:d.origin});});
  const original=await snapshot(),scene=await read();
  for(const stage of ['plane','normal','x','origin','x','plane','origin']){
    await driver.action(`stage.${stage}`);
    const shown=await page.evaluate(()=>{const d=fixture.view.fiveAlignment,a=d.feedback.children.filter(g=>g.userData.alignmentActive);return {stage:d.stage,selecting:d.selecting,label:document.querySelector('[data-align-binding-label]').textContent,active:a.map(g=>({role:g.userData.alignmentRole,id:g.userData.alignmentSourceId,sizes:g.children.filter(o=>o.isPoints).map(o=>o.material.size),labels:g.children.filter(o=>o.isSprite).length}))};});
    assert.equal(shown.stage,stage);assert.equal(shown.selecting,null);assert.equal(shown.active.length,1);assert.equal(shown.active[0].role,stage);assert.ok(shown.active[0].sizes.includes(13));assert.equal(shown.active[0].labels,1);
    const selected=stage==='plane'||stage==='normal'?original.plane:original[stage];assert.equal(shown.active[0].id,selected.sourceResult.featureId);assert.ok(shown.label.includes(selected.sourceResult.label));
    assert.deepEqual(await snapshot(),original);
  }
  cases.push('All four step buttons directly revisit saved selections without clearing references; panel identity, active scene geometry and a readable scene label identify the same selected object');
  await driver.action('stage.normal');await driver.action('flip');let revised=await snapshot();assert.equal(revised.plane.flip,!original.plane.flip);assert.deepEqual(revised.x,original.x);assert.deepEqual(revised.origin,original.origin);await driver.action('flip');
  await driver.action('stage.x');await driver.action('flip');revised=await snapshot();assert.equal(revised.x.flip,!original.x.flip);assert.deepEqual(revised.plane,original.plane);assert.deepEqual(revised.origin,original.origin);await driver.action('flip');
  cases.push('Z and X direction reversals retain the chosen plane, the other direction and the origin');

  const conflictingPlane=await page.evaluate(()=>{const d=fixture.view.fiveAlignment,c=fixture.view.sceneSnapping.catalogs.get(d.placementId);return c.features.find(f=>f.boxKind==='OBB'&&f.mode==='plane'&&Math.abs(f.direction.reduce((s,n,i)=>s+n*d.x.sourceResult.direction[i],0))>.999).featureId;});
  await driver.action('stage.plane');await driver.action('reselect');await driver.pick('plane',{prefix:conflictingPlane,allowError:true});
  const conflict=await page.evaluate(()=>({valid:fixture.view.fiveAlignment.valid,error:fixture.view.fiveAlignment.error,report:fixture.view.fiveAlignment.report}));
  assert.equal(conflict.valid,false);assert.equal(conflict.report,null);assert.match(conflict.error,/X 正方向/);assert.ok(await page.locator('[data-five-command="five.align.apply"]').isDisabled());
  revised=await snapshot();assert.deepEqual(revised.x,original.x);assert.deepEqual(revised.origin,original.origin);
  await driver.action('stage.x');await driver.action('reselect');await driver.pick('line',{prefix:'bounds:OBB:edge:',exclude:[original.x.sourceResult.featureId]});
  assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.valid),true);assert.deepEqual((await snapshot()).origin,original.origin);
  await driver.action('stage.origin');await driver.action('reselect');await driver.pick('datum-point',{prefix:'bounds:OBB:face-center:'});
  const complete=await snapshot();assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.valid),true);
  cases.push('Changing only the base plane preserves X and origin, exposes a perpendicular-direction conflict and disables commit; editing X repairs it, and changing origin retains both directions');

  await driver.action('stage.plane');await driver.action('three-points');await driver.pick('endpoint',{prefix:'bounds:OBB:corner:'});
  assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.points.length),1);await driver.action('stage.origin');
  assert.deepEqual(await snapshot(),complete);assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.construction),null);
  const unchanged=await read();assert.deepEqual(unchanged.workpieces,scene.workpieces);assert.deepEqual(unchanged.placements,scene.placements);
  await driver.action('stage.normal');
  const style=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1500px}.five-align-panel{max-height:370px!important}'});
  await page.evaluate(()=>{
    const proxy=fixture.view.fiveAlignment.context.sceneProxy,original=proxy.invoke;fixture.reeditProxy=proxy;fixture.reeditInvoke=original;
    proxy.invoke=async function(method,...args){const result=await original.call(this,method,...args);if(method==='CamPlacement.SolveRegistration'&&fixture.reeditHold){fixture.reeditHold=false;fixture.reeditHeld=true;await new Promise(r=>fixture.reeditRelease=r);fixture.reeditHeld=false;}return result;};fixture.reeditHold=true;
    for(const s of ['.cam-context-pane','.cam-info-pane','.five-align-panel'])document.querySelector(s).scrollTop=25;
  });
  const pending=driver.action('flip');await page.waitForFunction(()=>fixture.reeditHeld);
  const interaction=await page.evaluate(()=>{const input=document.querySelector('[data-five-key="align-close-header"]');input.focus({preventScroll:true});fixture.reeditNodes={input,canvas:fixture.view.viewport.renderer.domElement,panel:document.querySelector('.five-align-panel')};return ['.cam-context-pane','.cam-info-pane','.five-align-panel'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=45+i*10;return n.scrollTop;});});
  await page.evaluate(()=>fixture.reeditRelease());await pending;
  const restored=await page.evaluate(()=>({same:fixture.reeditNodes.input===document.querySelector('[data-five-key="align-close-header"]')&&fixture.reeditNodes.canvas===fixture.view.viewport.renderer.domElement&&fixture.reeditNodes.panel===document.querySelector('.five-align-panel'),focus:document.activeElement===fixture.reeditNodes.input,scroll:['.cam-context-pane','.cam-info-pane','.five-align-panel'].map(s=>document.querySelector(s).scrollTop)}));
  assert.equal(restored.same,true);assert.equal(restored.focus,true);assert.deepEqual(restored.scroll,interaction);
  await page.evaluate(()=>{fixture.reeditProxy.invoke=fixture.reeditInvoke;for(const s of ['.cam-context-pane','.cam-info-pane','.five-align-panel'])document.querySelector(s).scrollTop=0;});await style.evaluate(n=>n.remove());await driver.action('flip');await driver.action('stage.origin');
  cases.push('A delayed real native preview keeps the same canvas, panel and focused close button, plus both sidebars and nested panel scroll positions changed while the response is pending');
  await page.screenshot({path:resolve(output,'revisited-origin.png')});
  cases.push('Leaving an unfinished three-point replacement discards only those temporary picks; all saved choices and the native model remain unchanged');
  return cases;
}
