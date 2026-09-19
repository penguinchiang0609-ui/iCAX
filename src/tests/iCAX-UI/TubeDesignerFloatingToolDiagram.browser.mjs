import assert from 'node:assert/strict';
import { readFileSync, mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { resolve, sep } from 'node:path';
import { tubeDesignerCss } from '../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs';
const json=path=>JSON.parse(readFileSync(new URL(path,import.meta.url),'utf8'));
const raw=json('../../apps/tube-designer/templates/product/single_face_security_window/template.json');
const display=json('../../apps/tube-designer/templates/product/single_face_security_window/display.json');
const tool=json('../../apps/tube-designer/templates/mold/edge-arc-groove/tool.json');
tool.parameters.find(p=>p.key==='angle').presentation={advanced:true};
tool.parameters.find(p=>p.key==='bridge').presentation={visible:false};
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser=await chromium.launch({headless:true,channel:'msedge'});
try{
  const page=await browser.newPage({viewport:{width:1380,height:850}}), errors=[];
  page.on('pageerror',error=>errors.push(error.message));
  const source=fileURLToPath(new URL('../../',import.meta.url));
  await page.route('http://floating-diagram.test/**',route=>{
    const path=new URL(route.request().url()).pathname;
    if(path==='/')return route.fulfill({contentType:'text/html',body:'<body></body>'});
    const requested=path.replace(/^\/src\//,'');
    const mapped=process.env.ICAX_TEST_RUNTIME_WEBPAGE&&requested.startsWith('apps/tube-designer/webpage/')
      ? requested.replace('apps/tube-designer/webpage/','x64/Debug/apps/tube-designer/webpage/'):requested;
    const file=resolve(source,mapped);
    if(!path.startsWith('/src/')||!file.startsWith(source.replace(/[\\/]$/,'')+sep)||! /\.m?js$/.test(file))return route.abort();
    return route.fulfill({contentType:'text/javascript',body:readFileSync(file,'utf8')});
  });
  await page.goto('http://floating-diagram.test/');
  await page.addStyleTag({content:`${tubeDesignerCss}*{box-sizing:border-box}body{margin:0}.cam-workbench{display:grid;grid-template-columns:170px 700px 510px;height:800px}.cam-viewport{position:relative;background:#14272e;min-width:0}.cam-context-pane{overflow:auto}.left-list{height:1800px}.cam-info-pane{height:800px}.tube-floating-parameter-diagram-body{max-height:150px}`});
  await page.evaluate(async({raw,display,tool})=>{
    const views=await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const {catalogText}=await import('/src/apps/tube-designer/webpage/productCatalog.mjs');
    const {makeProductToolBinding,productToolRole}=await import('/src/apps/tube-designer/webpage/productResourceBindings.mjs');
    const {bindToolParameterDiagrams}=await import('/src/apps/tube-designer/webpage/toolParameterDiagram.mjs');
    const floating=await import('/src/apps/tube-designer/webpage/floatingParameterDiagram.mjs');
    const groups=raw.groups.map(g=>({...g,displayName:catalogText(g.displayName)}));
    const template={...raw,display,available:true,name:catalogText(raw.displayName),groups,
      parameters:raw.parameters.map(p=>({...p,displayName:catalogText(p.displayName),
        type:({enum:'select',string:'text'}[p.valueType]??p.valueType),groupKey:p.group,
        group:groups.find(g=>g.key===p.group)?.displayName??p.group,
        options:p.choices?.map(c=>({...c,label:catalogText(c.displayName)}))}))};
    const parameters={...Object.fromEntries(raw.parameters.map(p=>[p.key,p.defaultValue])),
      accessDoorEnabled:true,frameManufacturingMode:'spatial_v_notch',faceType:'three',
      doorFrameJoinType:'v_groove_90:tool_library',tubeDesignerToolBindings:{}};
    for(const key of ['outerFrameGrooveTool','doorFrameGrooveTool']){
      const field=template.parameters.find(p=>p.key===key);if(!field)continue;
      const binding=makeProductToolBinding(field,tool,null,template);
      parameters[key]=binding.selectionKey;parameters.tubeDesignerToolBindings[productToolRole(field)]=binding;
    }
    const product={entityId:'floating-product',templateId:raw.id,name:'防盗窗',quantity:1,parameters};
    const view={activeAreaId:'view',scene:{tubeDesigner:{templates:[template],product,activeProductId:product.entityId,members:[],joints:[],parts:[]}},
      tubeDesignerParameterPanelProductId:product.entityId,tubeDesignerExpandedParameterGroups:['section:process','section:materials',...groups.map(g=>g.key)],
      tubeDesignerParameterDisclosureState:{fixture:true},tubeDesignerSystemPunchTools:[tool]};
    document.body.innerHTML=`<main><div class="cam-workbench"><aside class="cam-context-pane"><div class="left-list">产品列表</div></aside><div class="cam-viewport"><canvas></canvas>${views.renderDesignerViewportOverlay({},view)}</div><aside class="cam-info-pane">${views.renderDesignerRightPane({},view)}</aside></div></main>`;
    const mount=document.querySelector('main');mount.querySelectorAll('details:not([data-parameter-advanced])').forEach(d=>d.open=true);
    bindToolParameterDiagrams(mount);floating.bindFloatingParameterDiagram(mount,view,views.renderDesignerToolDiagramDock);
    window.fixture={views,floating,view,mount,bindToolParameterDiagrams};
  },{raw,display,tool});
  assert.equal(await page.locator('[data-floating-parameter-diagram]').count(),0);
  await page.locator('[data-product-tool-diagram-open="doorFrameGrooveTool"]').click();
  const panel=page.locator('[data-floating-parameter-diagram]');
  assert.equal(await panel.count(),1);
  assert.equal(await page.evaluate(()=>{
    const layer=document.querySelector('.cam-workbench > [data-floating-parameter-diagram-layer]');
    const panel=document.querySelector('[data-floating-parameter-diagram]');
    const box=panel?.getBoundingClientRect(),info=document.querySelector('.cam-info-pane')?.getBoundingClientRect();
    return !!layer&&panel?.parentElement===layer&&!document.querySelector('.cam-viewport [data-floating-parameter-diagram]')
      &&box.left>=info.left-1&&box.right<=info.right+1&&Math.abs(box.width-360)<1&&Math.abs(box.height-405)<1;
  }),true,'product tool diagram must live in the workbench floating layer');
  assert.equal(await panel.locator('[data-tool-annotation-key="angle"]').isVisible(),false);
  const angleDetails=page.locator('.cam-info-pane [data-tool-parameter-key="angle"]').first().locator('xpath=ancestor::details[@data-parameter-advanced][1]');
  await angleDetails.locator('summary').click();
  await panel.locator('[data-tool-annotation-key="angle"]').waitFor({state:'visible'});
  await panel.locator('[data-tool-annotation-key="angle"] .tool-diagram-parameter-badge').click();
  assert.equal(await page.evaluate(()=>document.activeElement?.dataset.tubeDesignerToolField),'doorFrameGrooveTool');
  assert.equal(await page.evaluate(()=>document.activeElement?.dataset.toolParameterKey),'angle');
  assert.equal(await page.evaluate(()=>document.activeElement?.closest('details[data-parameter-advanced]')?.open),true);
  assert.equal(await page.locator('[data-tool-parameter-key="bridge"], [data-tool-annotation-key="bridge"]').count(),0);
  assert.equal(await panel.locator('[data-tool-annotation-key="angle"].is-active').count(),1);
  const before=await panel.boundingBox(), header=await panel.locator('header').boundingBox();
  await page.mouse.move(header.x+80,header.y+12);await page.mouse.down();
  await page.mouse.move(header.x-220,header.y+92,{steps:8});await page.mouse.up();
  const after=await panel.boundingBox();assert.ok(after.x<before.x-200 && after.y>before.y+60);
  const grip=await panel.locator('[data-diagram-resize="nw"]').boundingBox();
  await page.mouse.move(grip.x+6,grip.y+6);await page.mouse.down();
  await page.mouse.move(grip.x-54,grip.y-54,{steps:8});await page.mouse.up();
  const resized=await panel.boundingBox();
  assert.ok(resized.width>after.width+30 && resized.height>after.height+30,'borders must resize the floating window');
  const result=await page.evaluate(async()=>{
    const {mount,view,views,floating,bindToolParameterDiagrams}=window.fixture;
    const {captureDesignerScrollState,restoreDesignerScrollState}=await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const left=mount.querySelector('.cam-context-pane'),right=mount.querySelector('[data-tube-designer-parameter-scroll]');
    const diagram=mount.querySelector('[data-floating-diagram-scroll]'),canvas=mount.querySelector('canvas');
    const input=mount.querySelector('[data-tube-designer-parameter="productCode"]');
    input.value='latest draft';input.focus({preventScroll:true});input.setSelectionRange(2,7,'backward');
    left.scrollTop=200;right.scrollTop=140;diagram.scrollTop=35;
    // Continue typing/scrolling while a response is in flight, then capture the latest state.
    await new Promise(done=>setTimeout(done,20));
    input.value='latest edit';input.setSelectionRange(3,8,'backward');left.scrollTop=260;right.scrollTop=190;diagram.scrollTop=55;
    const positions=[left.scrollTop,right.scrollTop,diagram.scrollTop];
    captureDesignerScrollState({mount},view);
    const field=view.scene.tubeDesigner.templates[0].parameters.find(p=>p.key==='doorFrameGrooveTool');
    const binding=view.scene.tubeDesigner.product.parameters.tubeDesignerToolBindings[field.presentation.resourceRole];
    binding.parameters.angle=80;
    const oldPanel=mount.querySelector('[data-floating-parameter-diagram]');
    floating.refreshFloatingParameterDiagram(mount,view,views.renderDesignerToolDiagramDock);
    restoreDesignerScrollState({mount},view);bindToolParameterDiagrams(mount);
    const unchanged=oldPanel===mount.querySelector('[data-floating-parameter-diagram]');
    return {positions,now:[left.scrollTop,right.scrollTop,diagram.scrollTop],unchanged,
      canvas:canvas===mount.querySelector('canvas'),input:input===document.activeElement,
      selection:[input.selectionStart,input.selectionEnd,input.selectionDirection],
      updated:oldPanel.querySelector('[data-tool-annotation-key="angle"]').getAttribute('aria-label'),
      location:{...view.tubeDesignerFloatingToolDiagram}};
  });
  assert.deepEqual(result.now,result.positions);assert.equal(result.unchanged,true);
  assert.equal(result.canvas,true);assert.equal(result.input,true);assert.deepEqual(result.selection,[3,8,'backward']);
  assert.match(result.updated,/80/);
  const persisted=await panel.boundingBox();assert.ok(Math.abs(persisted.x-resized.x)<1 && Math.abs(persisted.y-resized.y)<1);
  assert.ok(Math.abs(persisted.width-resized.width)<1 && Math.abs(persisted.height-resized.height)<1);
  const refreshed=await page.evaluate(async()=>{
    const {mount,view,views,floating,bindToolParameterDiagrams}=window.fixture;
    const {captureDesignerScrollState,restoreDesignerScrollState}=await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const designer=view.scene.tubeDesigner, field=designer.templates[0].parameters.find(p=>p.key==='doorFrameGrooveTool');
    const binding=designer.product.parameters.tubeDesignerToolBindings[field.presentation.resourceRole];
    const input=mount.querySelector('[data-tube-designer-parameter="productCode"]');
    input.value='latest edit';input.focus({preventScroll:true});input.setSelectionRange(3,8,'backward');
    view.tubeDesignerRightDraft={...designer.product.parameters,productCode:'latest edit'};
    binding.parameters.bottomCut=true;
    const left=mount.querySelector('.cam-context-pane'),scroller=mount.querySelector('[data-tube-designer-parameter-scroll]');
    left.scrollTop=300;scroller.scrollTop=210;
    await new Promise(done=>setTimeout(done,20));left.scrollTop=340;scroller.scrollTop=240;
    captureDesignerScrollState({mount},view);
    const positions=[left.scrollTop,scroller.scrollTop];
    scroller.innerHTML=views.renderDesignerRightParameterContent(designer,view).scrollContent;
    floating.refreshFloatingParameterDiagram(mount,view,views.renderDesignerToolDiagramDock);
    restoreDesignerScrollState({mount},view);bindToolParameterDiagrams(mount);
    await new Promise(done=>requestAnimationFrame(()=>requestAnimationFrame(done)));
    const focused=document.activeElement;
    return {positions,now:[left.scrollTop,scroller.scrollTop],key:focused.dataset.tubeDesignerParameter,
      selection:[focused.selectionStart,focused.selectionEnd,focused.selectionDirection],
      conditional:!!mount.querySelector('[data-tube-designer-tool-field="doorFrameGrooveTool"][data-tool-parameter-key="bottomCutWidth"]'),
      annotation:!!mount.querySelector('[data-floating-parameter-diagram] [data-tool-annotation-key="bottomCutWidth"]')};
  });
  assert.deepEqual(refreshed.now,refreshed.positions);assert.equal(refreshed.key,'productCode');
  assert.deepEqual(refreshed.selection,[3,8,'backward']);assert.equal(refreshed.conditional,true);assert.equal(refreshed.annotation,true);
  await panel.locator('[data-floating-diagram-close]').click();assert.equal(await panel.count(),0);
  await page.locator('[data-product-tool-diagram-open="doorFrameGrooveTool"]').click();
  const reopened=await panel.boundingBox();assert.ok(Math.abs(reopened.x-resized.x)<1 && Math.abs(reopened.width-resized.width)<1 && Math.abs(reopened.height-resized.height)<1);
  mkdirSync(new URL('../../../tmp/floating-tool-diagram/',import.meta.url),{recursive:true});
  await page.addStyleTag({content:'.tube-floating-parameter-diagram-body{max-height:380px}'});
  await panel.screenshot({path:fileURLToPath(new URL('../../../tmp/floating-tool-diagram/window.png',import.meta.url))});
  await page.evaluate(()=>{const f=window.fixture;f.view.scene.tubeDesigner.product={...f.view.scene.tubeDesigner.product,entityId:'another-product'};f.floating.refreshFloatingParameterDiagram(f.mount,f.view,f.views.renderDesignerToolDiagramDock);});
  assert.equal(await panel.count(),0,'切换产品不携带旧产品的窗口');assert.deepEqual(errors,[]);
  console.log('Floating mould diagram: drag, close/reopen, owner highlight, live values, focus/selection/nested scroll and product isolation passed');
}finally{await browser.close();}
