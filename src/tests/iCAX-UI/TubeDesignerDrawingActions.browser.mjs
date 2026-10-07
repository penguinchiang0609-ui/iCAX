// Production drawing renderer, actions and DOM patch in a real browser.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, importBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
const { tubeDesignerCss }=await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const profile=JSON.parse(readFileSync('output/tests/product-profile-labels-20261006/system-profiles.json')).find(p=>p.id==='rect');
assert(profile?.previewProfile?.parameterDiagram);
const { chromium }=await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output=browserReportDirectory('drawing-bottom-actions-20261007/browser');
const browser=await chromium.launch({headless:true,channel:'msedge'});
const report={passed:false,layouts:[],errors:[]};
try {
  const page=await browser.newPage();
  page.on('pageerror',error=>report.errors.push(error.message));
  await page.route('http://drawing-actions.test/**',serveBrowserAsset);
  await page.goto('http://drawing-actions.test/');
  await page.addStyleTag({content:'*{box-sizing:border-box}body{margin:0}'+tubeDesignerCss});
  await page.evaluate(async profile=>{
    const drawing=await import('/src/apps/tube-designer/webpage/partDrawing.mjs');
    const model=await import('/src/apps/tube-designer/webpage/partDrawingModel.mjs');
    const dom=await import('/src/apps/tube-designer/webpage/partDrawingDom.mjs');
    const floats=await import('/src/apps/tube-designer/webpage/floatingParameterDiagram.mjs');
    const diagrams=await import('/src/apps/tube-designer/webpage/profileParameterDiagram.mjs');
    const mount=document.createElement('main'); mount.className='tube-designer-workspace';document.body.append(mount);
    const view={activeAreaId:'nesting',pending:false,scene:{tubeDesigner:{nestingGroups:[]}},tubeDesignerSystemProfiles:[profile],tubeDesignerSelectedProfileId:'system:rect',tubeDesignerUserData:{profiles:[]}};
    const f=window.fixture={view,drawing,model,mount,patches:0,calls:[]};
    f.render=()=>{
      const html=drawing.renderPartDrawingDialog(view);
      if(!dom.patchPartDrawingDom(view,mount,html)){mount.innerHTML=html;dom.rememberPartDrawingDom(view,mount);}else f.patches++;
      floats.bindDiagramDragging(mount,view);diagrams.bindProfileParameterDiagrams(mount);
    };
    f.context={mount,sceneProxy:{async invoke(method,payload){
      f.calls.push({method,payload});
      if(method!=='TubeDesigner.AddPartDrawing')throw Error('Unexpected scene method '+method);
      return {partEntityId:'saved-ui-fixture',tubeDesigner:{nestingGroups:[]}};
    }}};
    f.ops={renderProject:f.render,showNotice(){}};
    f.reset=()=>{
      drawing.openPartDrawing(view);model.installDrawingCatalogue(view.tubeDesignerPartDrawing.state,{tools:[]});
      view.tubeDesignerPartDrawing.state.features=Array.from({length:60},(_,i)=>model.normalizeDrawingFeature({id:'fixture-'+i,station:i*5,type:'round'}));
      f.render();
    };
    mount.addEventListener('click',event=>{
      const button=event.target.closest('[data-cam-action]');
      if(button)f.job=drawing.handlePartDrawingAction(f.context,view,button.dataset.camAction,button,f.ops);
    });
    f.reset();
  },profile);
  const editor=page.locator('.td-draw-workbench');
  const footer=editor.locator('.td-draw-actions');
  const diagram=editor.locator('[data-library-floating-diagram="drawing-section"]');
  for(const viewport of [{width:1600,height:1000},{width:1024,height:768},{width:850,height:600}]) {
    await page.setViewportSize(viewport);
    await page.evaluate(()=>fixture.reset());
    await editor.locator('details').filter({has:page.locator('summary').filter({hasText:'零件信息'})}).evaluate(node=>node.open=true);
    const layout=await editor.evaluate(node=>{
      const box=n=>{const r=n.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,bottom:r.bottom};};
      const pane=node.querySelector('.td-draw-parameters'),footer=pane.querySelector('.td-draw-actions'),scroll=pane.querySelector('.td-draw-property-scroll');
      return {pane:box(pane),footer:box(footer),scroll:box(scroll),overflow:scroll.scrollHeight-scroll.clientHeight,
        labels:[...footer.querySelectorAll('button')].map(n=>n.textContent),icons:footer.querySelectorAll('svg,.command-icon').length,
        headerButtons:node.querySelectorAll(':scope > header button').length,ribbonActions:node.querySelectorAll('.td-draw-ribbon [data-cam-action$="-apply"],.td-draw-ribbon [data-cam-action$="-cancel"]').length};
    });
    assert.deepEqual(layout.labels,['取消','确定']);assert.equal(layout.icons,0);assert.equal(layout.headerButtons,0);assert.equal(layout.ribbonActions,0);
    assert(Math.abs(layout.pane.bottom-layout.footer.bottom)<1,JSON.stringify(layout));
    assert(layout.scroll.bottom<=layout.footer.y+1);assert(layout.footer.bottom<=viewport.height);
    await page.evaluate(()=>{
      const f=fixture;
      f.left=f.mount.querySelector('[role="tree"]');f.right=f.mount.querySelector('.td-draw-property-scroll');
      f.left.scrollTop=250;f.right.scrollTop=90;
      f.input=f.mount.querySelector('[data-drawing-field="name"]');f.input.value='测试光标保留';f.input.focus({preventScroll:true});f.input.setSelectionRange(2,5);
      f.canvas=f.mount.querySelector('[data-part-drawing-viewport]');f.listenerCount=0;f.input.addEventListener('input',()=>f.listenerCount++);
      f.left.scrollTop=370;f.right.scrollTop=150;
      f.expected={left:f.left.scrollTop,right:f.right.scrollTop};
      // Async preview completion uses the same render/patch boundary as production.
      f.refreshJob=new Promise(resolve=>{f.finishRefresh=()=>{f.view.tubeDesignerPartDrawing.state.previewPending=false;f.render();resolve();};});
    });
    await page.evaluate(()=>{fixture.left.scrollTop=420;fixture.right.scrollTop=170;fixture.expected={left:fixture.left.scrollTop,right:fixture.right.scrollTop};fixture.finishRefresh();});
    await page.evaluate(()=>fixture.refreshJob);
    const preserved=await page.evaluate(()=>{
      const f=fixture;f.input.dispatchEvent(new Event('input',{bubbles:true}));
      return {left:f.left.scrollTop,right:f.right.scrollTop,expected:f.expected,focus:document.activeElement===f.input,selection:[f.input.selectionStart,f.input.selectionEnd],inputSame:f.mount.querySelector('[data-drawing-field="name"]')===f.input,canvasSame:f.mount.querySelector('[data-part-drawing-viewport]')===f.canvas,listeners:f.listenerCount};
    });
    assert.equal(preserved.left,preserved.expected.left);assert.equal(preserved.right,preserved.expected.right);
    assert.equal(preserved.focus,true);assert.deepEqual(preserved.selection,[2,5]);assert(preserved.inputSame&&preserved.canvasSame);assert.equal(preserved.listeners,1);
    const before=await footer.boundingBox();
    await page.evaluate(()=>{fixture.right.scrollTop=fixture.right.scrollHeight;});
    assert.deepEqual(await footer.boundingBox(),before);
    // Insert/remove actual condition-controlled profile fields without replacing the canvas/inputs.
    await page.evaluate(()=>{fixture.view.tubeDesignerPartDrawing.state.drawing.section.parameters.useOuterRadii=true;fixture.render();});
    assert.equal(await editor.locator('[data-drawing-parameter="outerRadius1"]').count(),1);
    await page.evaluate(()=>{fixture.view.tubeDesignerPartDrawing.state.drawing.section.parameters.useOuterRadii=false;fixture.render();});
    assert.equal(await editor.locator('[data-drawing-parameter="outerRadius1"]').count(),0);
    await page.evaluate(()=>fixture.right.scrollTop=0);
    const toggle=editor.locator('[data-cam-action="tube-designer-drawing-section-diagram"]');
    const labelBox=await editor.locator('.td-draw-section-label').boundingBox(),toggleBox=await toggle.boundingBox();
    assert(toggleBox.y>=labelBox.y&&toggleBox.y+toggleBox.height<=labelBox.y+labelBox.height+1);
    assert.equal(await editor.locator('.td-draw-property-scroll [data-profile-parameter-diagram]').count(),0);
    await toggle.click(); await page.evaluate(()=>fixture.job);await diagram.waitFor();
    assert.equal(await toggle.getAttribute('aria-expanded'),'true');
    assert.equal(await diagram.locator('svg').count(),1);
    assert.equal(await diagram.locator('xpath=..').getAttribute('data-floating-parameter-diagram-layer'),'');
    const header=await diagram.locator(':scope > header').boundingBox(),windowBefore=await diagram.boundingBox();
    await page.mouse.move(header.x+80,header.y+22);await page.mouse.down();await page.mouse.move(header.x+40,header.y+47,{steps:5});await page.mouse.up();
    const moved=await diagram.boundingBox();assert(Math.abs(moved.x-windowBefore.x+40)<2);assert(Math.abs(moved.y-windowBefore.y-25)<2);
    const resize=await diagram.locator('[data-diagram-resize="se"]').boundingBox();
    await page.mouse.move(resize.x+5,resize.y+5);await page.mouse.down();await page.mouse.move(resize.x-30,resize.y-25,{steps:5});await page.mouse.up();
    const resized=await diagram.boundingBox();assert(resized.width<moved.width-20&&resized.height<moved.height-20);
    await page.evaluate(()=>fixture.render());assert.deepEqual(await diagram.boundingBox(),resized);
    // Shared annotation ownership still returns focus to its matching control.
    await diagram.locator('[data-profile-annotation-key="width"] .td-profile-dimension-label').first().click();
    assert.equal(await editor.locator('[data-drawing-parameter="width"]').evaluate(node=>document.activeElement===node),true);
    await page.screenshot({path:resolve(output,`diagram-${viewport.width}.png`)});
    await diagram.getByRole('button',{name:'关闭示意图',exact:true}).click();await page.evaluate(()=>fixture.job);
    assert.equal(await diagram.count(),0);assert.equal(await editor.count(),1);
    await page.evaluate(()=>{fixture.view.pending=true;fixture.render();});
    assert.equal(await footer.locator('button:disabled').count(),2);
    await page.evaluate(()=>{fixture.view.pending=false;fixture.render();});
    await footer.getByRole('button',{name:'确定',exact:true}).focus();
    assert.equal(await footer.getByRole('button',{name:'确定',exact:true}).evaluate(node=>document.activeElement===node),true);
    await page.screenshot({path:resolve(output,`actions-${viewport.width}.png`)});
    await footer.getByRole('button',{name:'取消',exact:true}).click();await page.evaluate(()=>fixture.job);assert.equal(await editor.count(),0);
    report.layouts.push({viewport,layout,preserved,fixedFooter:true,diagramDragResizeRetained:true});
  }
  // Exercise the moved confirm action through the real handler and scene channel.
  await page.evaluate(()=>{fixture.reset();fixture.view.tubeDesignerPartDrawing.state.features=[];fixture.render();});
  await footer.getByRole('button',{name:'确定',exact:true}).click();await page.evaluate(()=>fixture.job);
  assert.equal(await editor.count(),0);
  const calls=await page.evaluate(()=>fixture.calls);assert.equal(calls.length,1);assert.equal(calls[0].method,'TubeDesigner.AddPartDrawing');
  assert.deepEqual(report.errors,[]);report.passed=true;
  console.log('Drawing: fixed text actions, scoped floating diagrams and preserved live interaction passed at 3 sizes.');
} catch(error) {
  report.error=error.stack;throw error;
} finally {
  writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await browser.close();
}
