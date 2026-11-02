import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyFixtureProgress({page,rpc,info,output,modelPath}){
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest,undefined,{timeout:180000});
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const field=name=>page.locator(`[data-five-fixture-field="design:${name}"]`);
  const dialog=page.locator('dialog.five-blocking-progress[open]');
  await selectWorkpieceFile(page,modelPath||info.sourcePath);await idle();
  await page.waitForFunction(()=>!fixture.view.fiveAxisImportActive&&fixture.view.viewport?.sceneObjects.size===1);
  await command('five.setup.create');await idle();await command('five.fixture.generate');await idle();
  const before=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  assert.equal(await field('windowMode').inputValue(),'None');
  await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{overflow:auto!important;max-height:400px!important}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;min-height:1400px}.five-design-body{max-height:300px!important}'});
  async function begin(action){
    await field('baseDrop').focus();
    await page.evaluate(action=>{
      const input=document.querySelector('[data-five-fixture-field="design:baseDrop"]');input.setSelectionRange(0,1);
      const panes=['.cam-context-pane','.cam-info-pane','.five-design-body'].map(s=>document.querySelector(s));panes.forEach((p,i)=>p.scrollTop=65+i*15);
      fixture.progressProbe={input,panes,scroll:panes.map(p=>p.scrollTop),canvas:fixture.view.viewport.renderer.domElement,events:0};
      input.addEventListener('progress-retention-probe',()=>fixture.progressProbe.events++,{once:true});
      fixture.holdMethod=action==='auto'?'CamFixture.FindSupportFaces':'CamFixture.Generate';fixture.hold=true;
      void fixture.entry.handleRibbonCommand(fixture.context,`five.fixture.design.${action}`);
    },action);
    await page.waitForFunction(()=>fixture.held);
    assert.equal(await dialog.evaluate(n=>n.matches(':modal')),true);
    assert.deepEqual(await dialog.boundingBox(),{x:0,y:0,width:1440,height:900});
    const card=await dialog.locator('section').boundingBox();
    assert.ok(Math.abs(card.x+card.width/2-720)<1&&Math.abs(card.y+card.height/2-450)<1);
    assert.equal(await page.getByRole('progressbar').count(),1);assert.equal(await page.locator('.five-design-progress').count(),0);
    const bar=dialog.getByRole('progressbar');assert.equal(await bar.getAttribute('aria-valuenow'),null);
    assert.equal(await bar.getAttribute('aria-label'),action==='auto'?'正在识别承托面…':'正在计算支撑…');
    assert.equal(await bar.locator('i').evaluate(n=>getComputedStyle(n).animationName),'five-blocking-progress-move');
    assert.equal(await page.evaluate(()=>fixture.view.viewport.continuousRendering),false);
    const frame=await page.evaluate(()=>fixture.view.viewport.renderer.info.render.frame);
    for(const selector of ['.five-design-header h2','.cam-context-pane','canvas']){
      const box=await page.locator(selector).first().boundingBox();if(box)await page.mouse.click(box.x+4,box.y+4);
    }
    await page.keyboard.press('Escape');await page.keyboard.press('Delete');await page.keyboard.press('Control+z');await page.keyboard.press('Tab');
    await page.mouse.move(1400,300);await page.mouse.wheel(0,500);await page.waitForTimeout(150);
    assert.equal(await dialog.count(),1);
    assert.deepEqual(await page.evaluate(()=>fixture.progressProbe.panes.map(p=>p.scrollTop)),await page.evaluate(()=>fixture.progressProbe.scroll));
    assert.equal(await page.evaluate(()=>fixture.view.viewport.renderer.info.render.frame),frame);
    await page.screenshot({path:resolve(output,`${action}-centered-progress.png`)});
  }
  async function finish(){
    await page.evaluate(()=>fixture.release());await idle();assert.equal(await dialog.count(),0);
    assert.equal(await page.evaluate(()=>fixture.view.viewport.continuousRendering),true);
    const retained=await page.evaluate(()=>{const p=fixture.progressProbe;p.input.dispatchEvent(new Event('progress-retention-probe'));return {input:p.input===document.querySelector('[data-five-fixture-field="design:baseDrop"]'),canvas:p.canvas===fixture.view.viewport.renderer.domElement,focus:document.activeElement===p.input,selection:[p.input.selectionStart,p.input.selectionEnd],scroll:p.panes.map(p=>p.scrollTop),events:p.events};});
    assert.deepEqual(retained,{input:true,canvas:true,focus:true,selection:[0,1],scroll:await page.evaluate(()=>fixture.progressProbe.scroll),events:1});
  }
  await begin('auto');await finish();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  assert.ok((await page.evaluate(()=>fixture.view.fixtureDesignDraft.supportFaces.length))>0);
  await begin('generate');await finish();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const candidate=await page.evaluate(()=>fixture.view.fiveFixtureGenerated.design.candidate);
  assert.equal(candidate.supportCheck.status,'Supported');for(const p of candidate.parts)assert.equal(p.windowHoleIndices.length,0);
  await page.screenshot({path:resolve(output,'complete-no-windows.png')});
  await field('spacing').fill('1');await begin('generate');await finish();
  assert.match(await page.evaluate(()=>fixture.view.error),/间距/);assert.equal(await field('spacing').isEnabled(),true);
  await field('spacing').fill('220');
  // Hold real task polling: acknowledging cancel must not end the command.
  await page.evaluate(()=>{fixture.holdMethod='CamFixture.QueryTask';fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.fixture.design.generate');});
  await page.waitForFunction(()=>fixture.held&&fixture.view.fiveAxisFixtureTask?.status==='Running');
  const cancelCount=await page.evaluate(()=>fixture.methods.filter(c=>c.method==='CamFixture.CancelTask').length);
  await dialog.locator('[data-progress-cancel]').click();
  await page.waitForFunction(before=>fixture.methods.filter(c=>c.method==='CamFixture.CancelTask').length>before,cancelCount);
  assert.equal(await dialog.count(),1);assert.equal(await dialog.locator('[data-progress-cancel]').isDisabled(),true);
  assert.equal(await page.evaluate(()=>fixture.view.fiveFixtureDesignOpen),true);
  await page.evaluate(()=>fixture.release());await idle();assert.equal(await dialog.count(),0);
  assert.equal(await page.evaluate(()=>fixture.view.fiveFixtureDesignOpen),true);
  assert.equal(await page.evaluate(()=>fixture.view.viewport.continuousRendering),true);
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  assert.equal(await page.evaluate(()=>fixture.view.fiveFixtureGenerated),null);
  const after=await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  for(const key of ['workpieces','placements','fixtures','fixtureUnits'])assert.deepEqual(after[key],before[key]);
  return {cases:['Real native recognition and generation use one centered full-document modal through final readback; clicks, wheel and shortcuts cannot reach the scene or sidebars',
    'Masked 3D redraw stops while progress animates; completion and failure resume drawing and retain canvas/input/listeners, text selection, both sidebars and nested scrolling',
    'New supports have no automatic windows; invalid spacing restores controls, and native cancellation waits for termination while keeping the editor open and CAD unchanged'],partCount:candidate.parts.length};
}
