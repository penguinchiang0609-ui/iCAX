// Real browser rendering and interaction regression. Native geometry is deliberately mocked.
import assert from 'node:assert/strict';
import {readFileSync,mkdirSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {resolve} from 'node:path';
import {importBrowserAsset,readBrowserAsset,serveBrowserAsset} from './browserPackageRuntime.mjs';

const {tubeDesignerCss}=await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const actualProfile=JSON.parse(execFileSync(process.env.ICAX_PYTHON||'python',['-c',`import importlib.util,json,pathlib
p=pathlib.Path('src/apps/tube-designer/templates/_shared/profile_package_runtime.py')
s=importlib.util.spec_from_file_location('r',p);r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
profiles=r.generate({'action':'list-system','profileRoot':'src/apps/tube-designer/templates/profile'}, {})['systemProfiles']
p=next(p for p in profiles if p['descriptor']['id']=='rect')
print(json.dumps({k:p[k] for k in ('name','descriptor','defaultParameters','previewProfile')}))`],{encoding:'utf8',maxBuffer:8*1024*1024}));
assert.equal(actualProfile.descriptor.display.schema,'icax.template-display');
const branchTool=JSON.parse(readFileSync(new URL('../../apps/tube-designer/templates/mold/branch-profile/tool.json',import.meta.url),'utf8'));
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'msedge'});
const artifacts=process.env.ICAX_ARTIFACT_DIR;
try {
  const page=await browser.newPage({viewport:{width:1600,height:950}}),errors=[];
  page.on('pageerror',error=>errors.push(error.message));page.setDefaultTimeout(15000);
  await page.route('http://drawing-layout.test/**',route=>{
    const path=new URL(route.request().url()).pathname;
    if(path==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><body><div id="app"></div></body>'});
    return serveBrowserAsset(route);
  });
  await page.goto('http://drawing-layout.test/');
  await page.addStyleTag({content:'*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,sans-serif}'+readBrowserAsset('apps/_shared/workbench/styles/laser3dcam.css')+tubeDesignerCss+`
    .fixture-nested-scroll{height:75px;overflow:auto}.fixture-nested-scroll>div{height:500px}.fixture-scroll-space{height:800px}
  `});
  await page.evaluate(async ({actualProfile,branchTool})=>{
    const drawing=await import('/src/apps/tube-designer/webpage/partDrawing.mjs');
    const model=await import('/src/apps/tube-designer/webpage/partDrawingModel.mjs');
    const preview=await import('/src/apps/tube-designer/webpage/partDrawingPreview.mjs');
    const dom=await import('/src/apps/tube-designer/webpage/partDrawingDom.mjs');
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const {createThreeViewport}=await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const {encodeNestingGeometry,encodePreviewMaterial}=await import('/src/apps/tube-designer/webpage/nestingPreview.mjs');
    const profile={...actualProfile,id:'rect',libraryScope:'system'};
    const view={activeAreaId:'nesting',pending:false,tubeDesignerSystemProfiles:[profile]};
    drawing.openPartDrawing(view);
    const state=view.tubeDesignerPartDrawing.state;
    branchTool.digest='browser-fixture';model.installDrawingCatalogue(state,{tools:[branchTool]});
    // Actual long tree makes independent left and right scrolling meaningful.
    state.features=Array.from({length:48},(_,index)=>{
      const feature=model.normalizeDrawingFeature({id:'fixture-'+index,station:index*5,enabled:false,section:structuredClone(state.drawing.section)});
      model.selectDrawingTool(state,feature,'branch-profile');return feature;
    });
    const geometry=new THREE.BoxGeometry(500,60,40);geometry.translate(250,0,0);
    const geometryBytes=encodeNestingGeometry({positions:Array.from(geometry.attributes.position.array),indices:Array.from(geometry.index.array)});geometry.dispose();
    const resource={url:'fixture://layout/base',version:1},material={url:'fixture://layout/material',version:1};
    let fullRenders=0,patches=0,viewport,release;
    const calls=[],fixture={view,drawing,model,profile,calls,resourceReads:0,get state(){return state;},get viewport(){return viewport;},get fullRenders(){return fullRenders;},get patches(){return patches;},holdPreview:false,waiting:false,
      release(){const done=release;release=null;done?.();}};
    const context={mount:document.querySelector('#app'),productProxy:{async invoke(method,payload){
      calls.push({method,payload:structuredClone(payload)});
      if(method!=='TubeDesigner.EvaluateProfilePackage')throw Error('Unexpected product API: '+method);
      return {profile:{...structuredClone(profile.previewProfile),parameters:structuredClone(payload.parameters)}};
    }},sceneProxy:{async invoke(method,payload){
      calls.push({method,payload:structuredClone(payload)});
      if(method!=='TubeDesigner.PreviewPartDrawing'||payload.toolsOnly!==true)throw Error('Unexpected scene API: '+method);
      if(fixture.holdPreview){fixture.waiting=true;await new Promise(resolve=>release=resolve);fixture.waiting=false;fixture.holdPreview=false;}
      return {toolsOnly:true,previewComputed:true,baseGeometry:resource,baseMaterial:material,toolMaterial:material,toolPreviews:[],baseBounds:{min:[0,-30,-20],max:[500,30,20]},length:500};
    },resources:{async get(url){fixture.resourceReads++;return new Response(url===resource.url?geometryBytes:encodePreviewMaterial(0xabc4ceff),{status:200});}}}};
    const ops={createPartDrawingViewport(options){return viewport=createThreeViewport(options);},renderProject(){
      let html=drawing.renderPartDrawingDialog(view);
      // Keep test scroll content inside the actual inspector's scroll container.
      if(fixture.addScrollFixture)html=html.replace('</fieldset></div>\n    <footer class="td-draw-actions">','</fieldset><div id="fixture-nested" class="fixture-nested-scroll"><div></div></div><div class="fixture-scroll-space"></div></div>\n    <footer class="td-draw-actions">');
      if(dom.patchPartDrawingDom(view,context.mount,html))patches++;
      else{fullRenders++;context.mount.innerHTML='<main id="main-workbench">既有工作台</main>'+html;dom.rememberPartDrawingDom(view,context.mount);}
      preview.attachPartDrawingPreview(context,view,context.mount,ops);drawing.attachPartDrawingEditor(context,view,context.mount,ops);
    }};
    fixture.ops=ops;fixture.context=context;window.fixture=fixture;
    document.addEventListener('change',async event=>{
      const action=event.target.dataset.camChangeAction;if(action)await drawing.handlePartDrawingAction(context,view,action,event.target,ops);
    });
    document.addEventListener('click',async event=>{
      const target=event.target.closest('[data-cam-action]');if(target)await drawing.handlePartDrawingAction(context,view,target.dataset.camAction,target,ops);
    });
    ops.renderProject();
  },{actualProfile,branchTool});
  const parameter=key=>page.locator('[data-drawing-section="main"][data-drawing-parameter="'+key+'"]');
  const arrayCommand=page.locator('.td-draw-ribbon [data-cam-action="tube-designer-drawing-array"]');
  const closeButton=page.getByRole('button',{name:'关闭三维绘制零件',exact:true});
  const arrayField=key=>page.locator('[data-tube-designer-punch-field="'+key+'"]');
  const ready=async()=>{
    await page.waitForFunction(()=>{const {state,view}=window.fixture;return state.preview?.revision===state.revision&&!view.pending&&!state.previewPending&&!state.previewRenderPending;});
    await page.locator('[data-part-drawing-preview-ready=true] .icax-three-viewport-canvas').waitFor();
  };
  const screenshot=async name=>{if(artifacts){mkdirSync(artifacts,{recursive:true});await page.screenshot({path:resolve(artifacts,name+'.png')});}};
  await ready();
  for(const size of [{width:1600,height:950},{width:1024,height:768}]){
    await page.setViewportSize(size);
    assert.equal(await arrayCommand.count(),0,'Array configuration belongs only in the inspector');
    const closePosition=await closeButton.evaluate(button=>{
      const bounds=button.getBoundingClientRect(),header=button.closest('.td-draw-title').getBoundingClientRect();
      return {rightAligned:bounds.right>=header.right-25&&bounds.right<=header.right,insideTitle:bounds.top>=header.top&&bounds.bottom<=header.bottom,width:bounds.width,height:bounds.height};
    });
    assert.equal(closePosition.rightAligned,true,JSON.stringify({size,closePosition}));assert.equal(closePosition.insideTitle,true);assert.ok(closePosition.width>=24&&closePosition.height>=24);
    const layout=await page.evaluate(()=>{
      const group=document.querySelector('[data-profile-display-group="dimensions"]'),width=group.querySelector('[data-drawing-parameter="width"]'),depth=group.querySelector('[data-drawing-parameter="depth"]');
      const fields=[...group.querySelectorAll('[data-drawing-parameter]')].map(input=>input.dataset.drawingParameter);
      return {title:group.querySelector('summary').textContent,fields,advanced:!!group.querySelector('[data-parameter-advanced]'),advancedOpen:group.querySelector('[data-parameter-advanced]').open,
        widthTitle:width.closest('label').querySelector('span').textContent,compact:Math.abs(width.getBoundingClientRect().top-depth.getBoundingClientRect().top)<2,
        labelLayout:getComputedStyle(width.closest('label')).display,inputWidth:width.getBoundingClientRect().width,
        overflow:[...document.querySelectorAll('.td-draw-section-parameters,.tube-profile-library-field-grid,.td-draw-property-scroll')].filter(node=>node.scrollWidth>node.clientWidth+2).map(node=>node.className)};
    });
    assert.equal(layout.title,'基本尺寸');assert.equal(layout.widthTitle,'宽');assert.equal(layout.compact,true);assert.equal(layout.labelLayout,'flex');
    assert.deepEqual(layout.fields,['width','depth','wallThickness','cornerRadius','innerRadius','useOuterRadii','useInnerRadii','innerOffsetX','innerOffsetY']);
    assert.equal(layout.advanced,true);assert.equal(layout.advancedOpen,false);assert.ok(layout.inputWidth<=84);assert.deepEqual(layout.overflow,[]);
    await screenshot('rect-template-layout-'+size.width);
  }
  // Every section consumer reads the same descriptor/defaults/right-display contract.
  const sections=await page.evaluate(()=>{
    const f=window.fixture,s=f.state,section={...structuredClone(s.drawing.section),parameters:{width:75},pendingParameters:{width:78}};
    s.draft.section=structuredClone(section);s.ends.start={type:'fixture',section:structuredClone(section)};s.ends.end={type:'fixture',section:structuredClone(section)};
    const result=['main','branch','start','end'].map(which=>{
      if(which==='main')s.drawing.section=structuredClone(section);
      const wrapper=document.createElement('section');wrapper.className='td-draw-workbench';wrapper.style.cssText='position:fixed;left:-10000px;width:380px';wrapper.innerHTML=f.drawing.renderDrawingSection(f.view,which);document.body.append(wrapper);
      const group=wrapper.querySelector('[data-profile-display-group="dimensions"]');
      const value=key=>wrapper.querySelector('[data-drawing-parameter="'+key+'"]')?.value;
      const advanced=wrapper.querySelector('[data-parameter-advanced]');advanced.open=true;
      const fullField=wrapper.querySelector('[data-drawing-parameter="useOuterRadii"]').closest('label');
      const result={which,id:group.id,title:group.querySelector('summary').textContent,width:value('width'),depth:value('depth'),outer:!!wrapper.querySelector('[data-drawing-parameter="outerRadius1"]'),full:fullField.classList.contains('is-line-full'),fullWidth:Math.abs(fullField.getBoundingClientRect().width-fullField.parentElement.parentElement.getBoundingClientRect().width)<2};
      wrapper.remove();return result;
    });
    s.drawing.section={...section,parameters:structuredClone(f.profile.defaultParameters)};delete s.drawing.section.pendingParameters;
    s.ends={start:{type:'keep'},end:{type:'keep'}};return result;
  });
  for(const result of sections)assert.deepEqual(result,{which:result.which,id:'tube-drawing-section-'+result.which+'-display-dimensions',title:'基本尺寸',width:'78',depth:'40',outer:false,full:true,fullWidth:true});
  await page.setViewportSize({width:1600,height:950});
  // Extend a test-only copy so hidden/enabled/order/width and conditional text identity
  // are checked without changing the shipped profile descriptor.
  await page.evaluate(()=>{
    const f=window.fixture,d=f.profile.descriptor;
    d.parameters.push(
      {key:'fixtureGate',displayName:'descriptor fallback',valueType:'boolean',defaultValue:false},
      {key:'fixtureConditional',displayName:'conditional fallback',valueType:'number',defaultValue:11,visibleWhen:{op:'eq',parameter:'fixtureGate',value:true}},
      {key:'fixtureText',displayName:'text fallback',valueType:'string',defaultValue:'retained text'},
      {key:'fixtureEnabled',displayName:'enabled fallback',valueType:'number',defaultValue:9,enabledWhen:{op:'eq',parameter:'fixtureGate',value:true}},
      {key:'fixtureHidden',displayName:'hidden fallback',valueType:'number',defaultValue:88,presentation:{visible:false}},
      {key:'fixtureAdvancedText',displayName:'advanced text',valueType:'string',defaultValue:'advanced retained',presentation:{advanced:true}}
    );
    const display=d.display.views.right;display.groups.fixture={title:{'zh-CN':'测试模板分组'},order:5,defaultOpen:true};
    Object.assign(display.fields,{fixtureGate:{group:'fixture',order:10,line:'full',title:{'zh-CN':'测试条件'}},fixtureConditional:{group:'fixture',order:20,title:{'zh-CN':'条件尺寸'}},fixtureText:{group:'fixture',order:30,line:'full',title:{'zh-CN':'保留文本'}},fixtureEnabled:{group:'fixture',order:40,title:{'zh-CN':'启用尺寸'},width:{min:48,preferred:64,max:68}},fixtureHidden:{group:'fixture',order:50},fixtureAdvancedText:{group:'dimensions',order:170}});
    f.state.drawing.section.parameters={...f.profile.defaultParameters,fixtureGate:false,fixtureText:'retained text',fixtureAdvancedText:'advanced retained'};
    f.addScrollFixture=true;f.ops.renderProject();
  });
  await ready();
  assert.deepEqual(await page.locator('.td-draw-section-parameters > [data-profile-display-group]').evaluateAll(groups=>groups.map(group=>group.dataset.profileDisplayGroup)),['fixture','dimensions']);
  assert.equal(await parameter('fixtureHidden').count(),0);assert.equal(await parameter('fixtureConditional').count(),0);assert.equal(await parameter('fixtureEnabled').isDisabled(),true);
  assert.equal(await parameter('fixtureText').locator('..').locator('span').innerText(),'保留文本');
  const width=await parameter('fixtureEnabled').evaluate(input=>({width:input.getBoundingClientRect().width,preferred:input.closest('label').style.getPropertyValue('--tube-designer-control-preferred-width'),max:input.closest('label').style.getPropertyValue('--tube-designer-control-max-width')}));
  assert.equal(width.preferred,'64px');assert.equal(width.max,'68px');assert.ok(width.width<=68&&width.width>=40,JSON.stringify(width));
  await page.evaluate(()=>{
    const f=window.fixture,advanced=document.querySelector('[data-parameter-advanced]');advanced.open=true;
    f.canvas=document.querySelector('.icax-three-viewport-canvas');f.input=document.querySelector('[data-drawing-parameter="fixtureText"]');f.advancedInput=document.querySelector('[data-drawing-parameter="fixtureAdvancedText"]');
    f.left=document.querySelector('.td-draw-tree [role="tree"]');f.right=document.querySelector('.td-draw-property-scroll');f.nested=document.querySelector('#fixture-nested');
    f.inputEvents=0;f.canvasEvents=0;f.input.addEventListener('fixture-retained',()=>f.inputEvents++);f.canvas.addEventListener('fixture-retained',()=>f.canvasEvents++);
    f.input.focus({preventScroll:true});f.input.value='draft text';f.input.setSelectionRange(2,7);f.left.scrollTop=90;f.right.scrollTop=35;f.nested.scrollTop=60;
    f.holdPreview=true;const name=document.querySelector('[data-drawing-field="name"]');name.value='async layout preview';name.dispatchEvent(new Event('change',{bubbles:true}));
  });
  await page.waitForFunction(()=>window.fixture.waiting);
  await page.evaluate(()=>{
    const f=window.fixture;f.input.focus({preventScroll:true});f.input.setSelectionRange(2,7);f.left.scrollTop=240;f.right.scrollTop=120;f.nested.scrollTop=125;
    f.latestScroll=[f.left.scrollTop,f.right.scrollTop,f.nested.scrollTop];f.release();
  });
  await ready();
  const interaction=await page.evaluate(()=>{
    const f=window.fixture;f.input.dispatchEvent(new Event('fixture-retained'));f.canvas.dispatchEvent(new Event('fixture-retained'));
    return {canvas:f.canvas===document.querySelector('.icax-three-viewport-canvas'),input:f.input===document.querySelector('[data-drawing-parameter="fixtureText"]'),focused:document.activeElement===f.input,selection:[f.input.selectionStart,f.input.selectionEnd],value:f.input.value,scroll:[f.left.scrollTop,f.right.scrollTop,f.nested.scrollTop],expectedScroll:f.latestScroll,nested:f.nested===document.querySelector('#fixture-nested'),listeners:[f.inputEvents,f.canvasEvents],fullRenders:f.fullRenders};
  });
  assert.equal(interaction.canvas,true);assert.equal(interaction.input,true);assert.equal(interaction.focused,true);assert.deepEqual(interaction.selection,[2,7]);assert.equal(interaction.value,'draft text');
  assert.deepEqual(interaction.scroll,interaction.expectedScroll);assert.ok(interaction.scroll.every(value=>value>0));assert.equal(interaction.nested,true);assert.deepEqual(interaction.listeners,[1,1]);assert.equal(interaction.fullRenders,1);
  // Conditional siblings before both retained controls must not replace them or their listeners.
  let listenerCount=1;
  for(const gate of [true,false]){
    await page.evaluate(gate=>{
      const f=window.fixture;f.input.focus({preventScroll:true});f.input.setSelectionRange(1,6);f.state.drawing.section.parameters.fixtureGate=gate;f.state.drawing.section.parameters.useOuterRadii=gate;f.ops.renderProject();
    },gate);
    const result=await page.evaluate(()=>{
      const f=window.fixture;f.input.dispatchEvent(new Event('fixture-retained'));
      return {input:f.input===document.querySelector('[data-drawing-parameter="fixtureText"]'),advancedInput:f.advancedInput===document.querySelector('[data-drawing-parameter="fixtureAdvancedText"]'),canvas:f.canvas===document.querySelector('.icax-three-viewport-canvas'),focused:document.activeElement===f.input,active:document.activeElement?.outerHTML?.slice(0,240),connected:f.input.isConnected,disabled:f.input.disabled,selection:[f.input.selectionStart,f.input.selectionEnd],scroll:[f.left.scrollTop,f.right.scrollTop,f.nested.scrollTop],expectedScroll:f.latestScroll,listeners:f.inputEvents,groupOpen:document.querySelector('[data-parameter-advanced]').open};
    });
    assert.equal(result.input,true,'Conditional insertion/removal keeps the later regular input node');assert.equal(result.advancedInput,true,'Conditional insertion/removal keeps the later advanced input node');assert.equal(result.canvas,true);assert.equal(result.focused,true,JSON.stringify({gate,result}));assert.deepEqual(result.selection,[1,6]);assert.deepEqual(result.scroll,result.expectedScroll);assert.equal(result.groupOpen,true);assert.equal(result.listeners,++listenerCount);
    assert.equal(await parameter('fixtureConditional').count(),gate?1:0);assert.equal(await parameter('outerRadius1').count(),gate?1:0);assert.equal(await parameter('fixtureEnabled').isDisabled(),!gate);
  }
  for(const gate of [true,false]){
    const result=await page.evaluate(gate=>{
      const f=window.fixture;f.advancedInput.focus({preventScroll:true});f.advancedInput.setSelectionRange(3,9);
      f.state.drawing.section.parameters.useOuterRadii=gate;f.ops.renderProject();
      return {input:f.advancedInput===document.querySelector('[data-drawing-parameter="fixtureAdvancedText"]'),focused:document.activeElement===f.advancedInput,selection:[f.advancedInput.selectionStart,f.advancedInput.selectionEnd],scroll:[f.left.scrollTop,f.right.scrollTop,f.nested.scrollTop],expectedScroll:f.latestScroll};
    },gate);
    assert.equal(result.input,true);assert.equal(result.focused,true);assert.deepEqual(result.selection,[3,9]);assert.deepEqual(result.scroll,result.expectedScroll);
  }
  const removedFocusedField=await page.evaluate(()=>{
    const f=window.fixture;f.state.drawing.section.parameters.fixtureGate=true;f.ops.renderProject();
    const input=document.querySelector('[data-drawing-parameter="fixtureConditional"]');input.focus({preventScroll:true});
    f.state.drawing.section.parameters.fixtureGate=false;f.ops.renderProject();
    return {removed:!input.isConnected,focusAssignedToAnotherField:document.activeElement?.matches?.('[data-drawing-parameter]')??false};
  });
  assert.deepEqual(removedFocusedField,{removed:true,focusAssignedToAnotherField:false});
  await screenshot('conditional-fields-and-preserved-workbench');
  // Drag both pane boundaries with a live WebGL scene and a focused text draft.
  // A pane resize updates the renderer's dimensions, never its model or camera.
  const splitter=side=>page.locator('[data-drawing-splitter="'+side+'"]');
  const paneWidths=()=>page.evaluate(()=>['.td-draw-sidebar','.td-draw-canvas','.td-draw-parameters'].map(selector=>document.querySelector(selector).getBoundingClientRect().width));
  const dragPane=async(side,dx)=>{
    const box=await splitter(side).boundingBox();assert.ok(box&&box.width>=4,side+' splitter has a usable mouse target');
    await page.mouse.move(box.x+box.width/2,box.y+Math.min(120,box.height/2));await page.mouse.down();
    await page.mouse.move(box.x+box.width/2+dx,box.y+Math.min(120,box.height/2),{steps:8});await page.mouse.up();
    await page.waitForFunction(()=>{
      const f=window.fixture,box=f.viewport.host.getBoundingClientRect();
      return Math.abs(f.viewport.renderer.domElement.width-Math.floor(box.width)*devicePixelRatio)<2;
    });
  };
  await page.evaluate(()=>{
    const f=window.fixture,v=f.viewport,state=v.getCameraState();v.setCameraState({...state,radius:state.radius*1.23,theta:state.theta+0.17});
    f.input.focus({preventScroll:true});f.input.setSelectionRange(2,7);f.left.scrollTop=200;f.right.scrollTop=90;f.nested.scrollTop=115;
    f.resizeBaseline={camera:v.camera,state:JSON.stringify(v.getCameraState()),meshes:[...v.sceneObjects],geometries:[...v.geometryObjects],listeners:[...v.domListeners],reads:f.resourceReads};
    f.resizeScroll=[f.left.scrollTop,f.right.scrollTop,f.nested.scrollTop];
  });
  const initialWidths=await paneWidths();await dragPane('left',110);const leftResized=await paneWidths();
  assert.ok(Math.abs(leftResized[0]-initialWidths[0]-110)<3);assert.ok(Math.abs(leftResized[2]-initialWidths[2])<3);
  await dragPane('right',-85);const bothResized=await paneWidths();
  assert.ok(Math.abs(bothResized[2]-leftResized[2]-85)<3);assert.ok(Math.abs(bothResized[0]-leftResized[0])<3);assert.ok(bothResized[1]>=280);
  const resizeState=await page.evaluate(()=>{
    const f=window.fixture,v=f.viewport,b=f.resizeBaseline;
    return {camera:v.camera===b.camera,state:JSON.stringify(v.getCameraState())===b.state,
      meshes:b.meshes.every(([key,value])=>v.sceneObjects.get(key)===value),geometries:b.geometries.every(([key,value])=>v.geometryObjects.get(key)===value),
      listeners:v.domListeners.length===b.listeners.length&&b.listeners.every((value,i)=>v.domListeners[i]===value),
      canvas:f.canvas===document.querySelector('.icax-three-viewport-canvas'),input:f.input===document.querySelector('[data-drawing-parameter="fixtureText"]'),
      focus:document.activeElement===f.input,selection:[f.input.selectionStart,f.input.selectionEnd],scroll:[f.left.scrollTop,f.right.scrollTop,f.nested.scrollTop],expectedScroll:f.resizeScroll,
      reads:f.resourceReads,expectedReads:b.reads,fullRenders:f.fullRenders};
  });
  for(const key of ['camera','state','meshes','geometries','listeners','canvas','input','focus'])assert.equal(resizeState[key],true,key+' survives splitter drag');
  assert.deepEqual(resizeState.selection,[2,7]);assert.deepEqual(resizeState.scroll,resizeState.expectedScroll);assert.equal(resizeState.reads,resizeState.expectedReads);assert.equal(resizeState.fullRenders,1);
  // The response must retain widths selected while that response was pending.
  await page.evaluate(()=>{
    const f=window.fixture;f.holdPreview=true;const name=document.querySelector('[data-drawing-field="name"]');name.value='resize during async preview';name.dispatchEvent(new Event('change',{bubbles:true}));
  });
  await page.waitForFunction(()=>window.fixture.waiting);await dragPane('left',24);
  const activeDragBox=await splitter('right').boundingBox(),activeDragY=activeDragBox.y+120;
  await page.evaluate(()=>{
    const f=window.fixture;f.heldSeparator=document.querySelector('[data-drawing-splitter="right"]');
    f.heldSeparator.addEventListener('pointerdown',event=>f.heldPointerId=event.pointerId,{once:true});
  });
  await page.mouse.move(activeDragBox.x+activeDragBox.width/2,activeDragY);await page.mouse.down();
  await page.mouse.move(activeDragBox.x+activeDragBox.width/2-12,activeDragY,{steps:3});
  const pendingWidths=await paneWidths();
  await page.evaluate(()=>{
    const f=window.fixture;f.input.focus({preventScroll:true});f.input.setSelectionRange(3,8);f.left.scrollTop=260;f.right.scrollTop=100;f.nested.scrollTop=135;
    f.resizeScroll=[f.left.scrollTop,f.right.scrollTop,f.nested.scrollTop];f.release();
  });
  await ready();assert.deepEqual(await paneWidths(),pendingWidths,'Async preview keeps the latest dragged widths');
  assert.equal(await page.evaluate(()=>{const f=window.fixture;return f.heldSeparator===document.querySelector('[data-drawing-splitter="right"]')&&f.heldSeparator.hasPointerCapture(f.heldPointerId);}),true,'Async preview retains an active separator drag and pointer capture');
  await page.mouse.move(activeDragBox.x+activeDragBox.width/2-24,activeDragY,{steps:3});await page.mouse.up();
  assert.ok(Math.abs((await paneWidths())[2]-pendingWidths[2]-12)<3,'An active drag continues after the preview response');
  const asyncResize=await page.evaluate(()=>{
    const f=window.fixture;return {state:JSON.stringify(f.viewport.getCameraState())===f.resizeBaseline.state,focus:document.activeElement===f.input,
      selection:[f.input.selectionStart,f.input.selectionEnd],scroll:[f.left.scrollTop,f.right.scrollTop,f.nested.scrollTop],expectedScroll:f.resizeScroll,fullRenders:f.fullRenders};
  });
  assert.equal(asyncResize.state,true);assert.equal(asyncResize.focus,true);assert.deepEqual(asyncResize.selection,[3,8]);assert.deepEqual(asyncResize.scroll,asyncResize.expectedScroll);assert.equal(asyncResize.fullRenders,1);
  await splitter('left').focus();const beforeKeyboard=await paneWidths();await splitter('left').press('ArrowRight');
  assert.ok((await paneWidths())[0]>beforeKeyboard[0]+8,'Keyboard changes the left width');
  await splitter('right').focus();const beforeRightKeyboard=await paneWidths();await splitter('right').press('ArrowLeft');
  assert.ok((await paneWidths())[2]>beforeRightKeyboard[2]+8,'Keyboard changes the right width');
  await page.setViewportSize({width:1024,height:768});await dragPane('left',900);let limited=await paneWidths();
  assert.ok(limited[0]>=120&&limited[1]>=279&&limited[2]>=259,'Widths keep all three regions usable in a smaller window');
  await dragPane('right',-900);limited=await paneWidths();assert.ok(limited[0]>=120&&limited[1]>=279&&limited[2]>=259);
  assert.equal(await page.locator('.td-draw-body').evaluate(body=>body.scrollWidth<=body.clientWidth+2),true,'Pane width limits prevent outer horizontal overflow');
  await page.setViewportSize({width:1600,height:950});await screenshot('resizable-drawing-regions');
  const rememberedWidths=await paneWidths();
  // Dimension changes retain the existing native recipe and inactive drafts.
  await page.locator('[data-drawing-command="branch"]').click();await ready();
  assert.deepEqual(await paneWidths(),rememberedWidths,'Selecting a different node keeps the chosen pane widths');
  const arrayBefore=await page.evaluate(()=>{
    const f=window.fixture,m=f.view.tubeDesignerPartDrawing;f.arrayFeatureId=m.selected;
    f.left.scrollTop=320;
    f.arrayBefore={payload:JSON.stringify(f.drawing.getPartDrawingPayload(f.view)),count:f.state.features.length,history:f.state.history.length,left:f.left.scrollTop};return f.arrayBefore;
  });
  assert.equal(await arrayField('arrayDimension').inputValue(),'none');
  assert.equal(await arrayField('arrayCount').count(),0);assert.equal(await arrayField('rowCount').count(),0);
  await arrayField('arrayDimension').selectOption('one');await ready();
  assert.equal(await page.locator('[data-drawing-array-dimension]').count(),1);
  await page.evaluate(()=>window.fixture.firstDimensionInput=document.querySelector('[data-tube-designer-punch-field="arrayCount"]'));
  await arrayField('arrayDimension').selectOption('two');await ready();
  assert.equal(await page.locator('[data-drawing-array-dimension]').count(),2);
  assert.equal(await page.evaluate(()=>window.fixture.firstDimensionInput===document.querySelector('[data-tube-designer-punch-field="arrayCount"]')),true,'Inserting dimension two retains dimension one and its listeners');
  assert.equal(await arrayCommand.count(),0);
  await page.evaluate(()=>{const f=window.fixture;f.view.pending=true;f.ops.renderProject();});
  assert.equal(await closeButton.isDisabled(),true);assert.equal(await arrayField('arrayDimension').isDisabled(),true);
  await page.evaluate(()=>{const f=window.fixture;f.view.pending=false;f.ops.renderProject();});
  await page.evaluate(()=>window.fixture.holdPreview=true);
  await arrayField('arrayCount').fill('3');await arrayField('arrayCount').press('Tab');
  await page.waitForFunction(()=>window.fixture.waiting);
  await arrayField('rowCount').fill('2');await arrayField('rowCount').press('Tab');
  await arrayField('arrayDirection').selectOption('negative');await arrayField('rowDirection').selectOption('negative');
  await page.evaluate(()=>{
    const f=window.fixture,input=document.querySelector('[data-tube-designer-punch-field="rowCount"]');
    f.arrayInput=input;input.focus({preventScroll:true});f.right.scrollTop+=25;f.left.scrollTop=360;f.nested.scrollTop=140;
    f.arrayLatestScroll=[f.left.scrollTop,f.right.scrollTop,f.nested.scrollTop];f.release();
  });
  await ready();
  const arrayEdited=await page.evaluate(()=>{
    const f=window.fixture,call=f.calls.filter(call=>call.method==='TubeDesigner.PreviewPartDrawing').at(-1),feature=call.payload.features.find(feature=>feature.id===f.arrayFeatureId);
    return {feature:{id:feature.id,arrayCount:feature.arrayCount,rowCount:feature.rowCount,arrayPitch:feature.arrayPitch,rowPitch:feature.rowPitch},count:f.state.features.length,
      open:document.querySelector('details[data-drawing-array]').open,canvas:f.canvas===document.querySelector('.icax-three-viewport-canvas'),input:f.arrayInput===document.querySelector('[data-tube-designer-punch-field="rowCount"]'),focused:document.activeElement===f.arrayInput,
      scroll:[f.left.scrollTop,f.right.scrollTop,f.nested.scrollTop],expectedScroll:f.arrayLatestScroll,fullRenders:f.fullRenders};
  });
  assert.equal(arrayEdited.feature.arrayCount,3);assert.equal(arrayEdited.feature.rowCount,2);assert.ok(arrayEdited.feature.arrayPitch<0&&arrayEdited.feature.rowPitch<0);
  assert.equal(arrayEdited.count,arrayBefore.count);assert.equal(arrayEdited.open,true);assert.equal(arrayEdited.canvas,true);assert.equal(arrayEdited.input,true);assert.equal(arrayEdited.focused,true);assert.deepEqual(arrayEdited.scroll,arrayEdited.expectedScroll);assert.equal(arrayEdited.fullRenders,1);
  await arrayField('drawingArrayMode').selectOption('round');await ready();
  await arrayField('arrayDimension').selectOption('one');await ready();
  assert.equal(await arrayField('rowCount').count(),0);
  assert.equal(await page.evaluate(()=>window.fixture.calls.filter(c=>c.method==='TubeDesigner.PreviewPartDrawing').at(-1).payload.features.at(-1).rowCount),1);
  await arrayField('arrayDimension').selectOption('none');await ready();
  assert.equal(await page.locator('[data-drawing-array-dimension]').count(),0);
  assert.deepEqual(await page.evaluate(()=>{const f=window.fixture.calls.filter(c=>c.method==='TubeDesigner.PreviewPartDrawing').at(-1).payload.features.at(-1);return [f.arrayCount,f.rowCount];}),[1,1]);
  await arrayField('arrayDimension').selectOption('two');await ready();
  assert.deepEqual(await page.evaluate(()=>{const f=window.fixture.calls.filter(c=>c.method==='TubeDesigner.PreviewPartDrawing').at(-1).payload.features.at(-1);return [f.arrayCount,f.rowCount,f.face,f.rowPitch,!!f.arrayEditing];}),[3,2,'round',-20,false]);
  await page.evaluate(()=>{const f=window.fixture;f.addScrollFixture=false;f.ops.renderProject();f.right.scrollTop=f.right.scrollHeight;});
  await screenshot('two-dimensional-array-inspector');
  await closeButton.click();
  await page.waitForFunction(()=>window.fixture.view.tubeDesignerPartDrawing===null);
  assert.equal(await page.locator('.td-draw-workbench').count(),0);
  assert.deepEqual(await page.evaluate(()=>window.fixture.calls.filter(call=>['TubeDesigner.AddPartDrawing','TubeDesigner.ApplyPartDrawing'].includes(call.method))),[],'Closing the editor does not save or create a part');
  assert.deepEqual(errors,[]);
  assert.ok(await page.evaluate(()=>window.fixture.calls.some(call=>call.method==='TubeDesigner.PreviewPartDrawing')));
  console.log('Drawing section template layout passed: actual rect display, hidden/enabled conditions, stable canvas and inputs, latest pane/nested scroll and focus/selection through async changes. Array inspector passed: no ribbon entry, none/one/two dimensions, retained inactive counts, signed 3 x 2 circular payload and unchanged native schema. Both pane splitters and header close passed. Browser rendering only; native geometry mocked.');
} finally {await browser.close();}
