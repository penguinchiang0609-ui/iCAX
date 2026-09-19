import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {resolve,sep} from 'node:path';
import {execFileSync} from 'node:child_process';
import {tubeDesignerCss} from '../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs';
const packages=JSON.parse(execFileSync(process.env.ICAX_PYTHON || 'python',['-c',`import importlib.util,json,pathlib
p=pathlib.Path('src/apps/tube-designer/templates/_shared/profile_package_runtime.py')
s=importlib.util.spec_from_file_location('r',p);r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
packages=r.generate({'action':'list-system','profileRoot':'src/apps/tube-designer/templates/profile'}, {})['systemProfiles']
print(json.dumps([{k:p[k] for k in ('name','descriptor','defaultParameters','previewProfile')} for p in packages]))`],{encoding:'utf8',maxBuffer:8*1024*1024}));
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser=await chromium.launch({headless:true,channel:'msedge'});
try {
  const page=await browser.newPage({viewport:{width:1300,height:800}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  const source=fileURLToPath(new URL('../../',import.meta.url));
  await page.route('http://profile-display.test/**',route=>{
    const p=new URL(route.request().url()).pathname;
    if(p==='/')return route.fulfill({contentType:'text/html',body:'<body></body>'});
    const file=resolve(source,p.replace(/^\/src\//,''));
    if(!file.startsWith(source.replace(/[\\/]$/,'')+sep))return route.abort();
    return route.fulfill({contentType:'text/javascript',body:readFileSync(file,'utf8')});
  });
  await page.goto('http://profile-display.test/');
  await page.addStyleTag({content:tubeDesignerCss+`body{margin:0}.cam-workbench{display:grid;grid-template-columns:190px 720px 350px;height:700px}.cam-context-pane,.cam-info-pane{overflow:auto;min-height:0}.cam-viewport{position:relative;background:#14272e}.left-list{height:2200px}.nested-scroll{height:80px;overflow:auto}.nested-scroll div{height:600px}`});
  const result=await page.evaluate(async packages=>{
    const lib=await import('/src/apps/tube-designer/webpage/profileLibrary.mjs');
    const {patchLibraryDom,rememberLibraryDom}=await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    let annotations=[],applied={revision:'',entityIds:[]},release;
    const view={activeAreaId:'profiles',tubeDesignerSystemProfiles:packages,tubeDesignerUserData:{profiles:[]},tubeDesignerProfileLibrary:{scope:'system'},viewport:{
      setSpecificationAnnotations:a=>annotations=a,clearSpecificationAnnotations:()=>annotations=[],setVisibleEntityIds(){},setSelectedObjectIds(){},fitViewForRevision(){},setStandardView(){},getAppliedViewState:()=>applied,
      async applyViewSnapshot(snapshot){await new Promise(r=>release=r);applied={revision:snapshot.revision,entityIds:snapshot.rows.map(r=>r.entityId)};return {applied:true,entityIds:applied.entityIds};}}};
    for(const profile of packages){
      profile.id=profile.descriptor.id;profile.libraryScope='system';
      view.tubeDesignerSelectedProfileId='system:'+profile.id;
      const right=lib.renderProfileLibraryRightPane({},view),overlay=lib.renderProfileLibraryViewportOverlay({},view);
      if(!/data-profile-diagram-toggle/.test(right)||!/tube-profile-library-parameter-section/.test(right)||/data-profile-diagram-toggle/.test(overlay)||!/data-tube-profile-diagram-dock/.test(overlay))throw Error('Diagram toggle belongs in the parameter pane: '+profile.id);
      if(lib.bindProfileSpecificationAnnotations(null,view).length || annotations.length || /data-tube-designer-specification-tree/.test(overlay))throw Error('Resource page must not annotate the model');
      if(!/data-profile-annotation-key/.test(overlay))throw Error('Floating diagram has no parameter links: '+profile.id);
    }
    const profile=packages.find(p=>p.id==='rect');view.tubeDesignerSelectedProfileId='system:rect';
    if(profile.descriptor.parameters.some(p=>['innerWidth','innerDepth'].includes(p.key)))throw Error('内宽和内高只能是派生尺寸，不能是模板参数');
    const actual=document.createElement('div');
    actual.innerHTML=lib.renderProfileLibraryRightPane({},view)+lib.renderProfileLibraryViewportOverlay({},view);
    const exposed=[...actual.querySelectorAll('[data-profile-parameter-key]')].map(n=>n.dataset.profileParameterKey).sort();
    const expectedExposed=['cornerRadius','depth','innerOffsetX','innerOffsetY','innerRadius','useInnerRadii','useOuterRadii','wallThickness','width'];
    if(JSON.stringify(exposed)!==JSON.stringify(expectedExposed))throw Error('方管的基本参数和高级参数显示不完整');
    if(actual.querySelector('.td-profile-parameter-legend, .td-profile-parameter-legend-row, .td-profile-parameter-advanced-divider'))throw Error('示意图不应再显示参数列表');
    for(const key of ['useOuterRadii','useInnerRadii']){
      if(actual.querySelector(`[data-profile-annotation-key="${key}"]`))throw Error(`${key} 不应显示在示意图上`);
    }
    if(actual.textContent.includes('名义壁厚')||!actual.querySelector('[data-parameter-advanced]'))throw Error('方管高级设置未正确显示');
    if(actual.querySelector('[data-profile-advanced-toggle]'))throw Error('参数面板不应重复提供高级设置按钮');
    if(actual.textContent.includes('系统内置管型不可重命名或删除')||actual.querySelector('[data-tube-profile-preview-length]'))throw Error('右侧不应显示系统说明或预览长度参数');
    if(!actual.querySelector('.tube-profile-library-preview-hud')?.textContent.includes('版本'))throw Error('场景缺少管型版本信息');
    if(actual.querySelector('.tube-profile-library-identity'))throw Error('右侧不应重复显示管型名称和版本');
    if(actual.textContent.includes('截面尺寸：mm')||actual.textContent.includes('参数已更改'))throw Error('示意图不应显示多余提示语');
    for(const key of ['useOuterRadii','useInnerRadii']){
      if(!actual.querySelector(`[data-profile-parameter-key="${key}"]`)?.closest('.tube-designer-field.is-line-full'))throw Error(`${key} 应独占一行`);
    }
    if(!actual.querySelector('[data-profile-display-group="dimensions"] [data-profile-parameter-key="cornerRadius"]'))throw Error('R（外）必须属于基本尺寸');
    const omega=packages.find(p=>p.id==='omega');
    view.tubeDesignerSelectedProfileId='system:omega';
    const omegaActual=document.createElement('div');
    omegaActual.innerHTML=lib.renderProfileLibraryRightPane({},view);
    const advancedGroup=omegaActual.querySelector('[data-profile-display-group="advanced"]');
    if(!advancedGroup?.matches('details[data-parameter-advanced]')||advancedGroup.querySelector('details[data-parameter-advanced]'))throw Error('显式高级设置分组必须自身承担折叠，不得再嵌套第二层高级设置');
    view.tubeDesignerSelectedProfileId='system:rect';
    // A test-only advanced text field retains the selection/async patch regression;
    // the shipped template above must hide this internal parameter entirely.
    profile.descriptor.parameters.push({key:'testAdvancedText',displayName:'测试高级文本',valueType:'string',defaultValue:'',presentation:{advanced:true}});
    document.body.innerHTML='<main><div class="cam-workbench"><aside class="cam-context-pane"><div class="left-list"></div></aside><div class="cam-viewport"><canvas></canvas>'+lib.renderProfileLibraryViewportOverlay({},view)+'</div><aside class="cam-info-pane">'+lib.renderProfileLibraryRightPane({},view)+'</aside></div></main>';
    const mount=document.querySelector('main'),calls=[];
    const context={mount,sceneProxy:{resources:{get(){}},async invoke(method,payload){calls.push({method,payload});return {profile:{...profile.previewProfile,parameters:payload.parameters},length:payload.length,geometryResourceId:'geometry',geometryResourceVersion:2};}}};
    rememberLibraryDom(view,mount,'');
    const ops={renderProject(){patchLibraryDom(view,mount,{left:'<div class="left-list"></div>',right:lib.renderProfileLibraryRightPane(context,view),overlay:lib.renderProfileLibraryViewportOverlay(context,view),suffix:''});lib.bindProfileSpecificationAnnotations(mount,view);}};
    const canvas=mount.querySelector('canvas'),width=mount.querySelector('[data-profile-parameter-key="width"]'),depth=mount.querySelector('[data-profile-parameter-key="depth"]');
    const compact=Math.abs(width.getBoundingClientRect().top-depth.getBoundingClientRect().top)<2;
    width.value='70';
    await lib.handleProfileLibraryAction(context,view,'tube-designer-profile-preview-change',width,ops);
    if(annotations.length)throw Error('Parameter editing added scene annotations');
    if(view.tubeDesignerProfileDrafts['system:rect'].parameters.width!==70)throw Error('Missing edited draft');
    if(mount.querySelector('[data-tube-profile-diagram-dock]').textContent.includes('上次成功生成'))throw Error('示意图不应显示旧截面提示语');
    while(!release)await new Promise(r=>setTimeout(r,0));
    mount.querySelector('[data-profile-display-group="dimensions"]').open=true;
    const input=mount.querySelector('[data-profile-parameter-key="testAdvancedText"]');
    const advanced=input.closest('details[data-parameter-advanced]');
    if(!advanced||advanced.open)throw Error('Advanced fields must start folded');
    advanced.open=true;input.focus({preventScroll:true});input.value='   ';input.setSelectionRange(1,2);
    const left=mount.querySelector('.cam-context-pane'),right=mount.querySelector('.tube-profile-library-editor-body');
    right.insertAdjacentHTML('beforeend','<div class="nested-scroll"><div></div></div><div style="height:900px"></div>');
    const nested=right.querySelector('.nested-scroll');left.scrollTop=310;right.scrollTop=130;nested.scrollTop=90;
    await new Promise(r=>setTimeout(r,15));left.scrollTop=440;right.scrollTop=210;nested.scrollTop=130;
    const positions=[left.scrollTop,right.scrollTop,nested.scrollTop];release();
    while(view.tubeDesignerProfilePreviewRequest)await new Promise(r=>setTimeout(r,0));
    return {count:packages.length,calls:calls.length,compact,canvas:canvas===mount.querySelector('canvas'),input:input===mount.querySelector('[data-profile-parameter-key="testAdvancedText"]'),focused:document.activeElement===input,selection:[input.selectionStart,input.selectionEnd],scroll:JSON.stringify(positions)===JSON.stringify([left.scrollTop,right.scrollTop,nested.scrollTop]),clean:!annotations.some(a=>a.pending)};
  },packages);
  await page.screenshot({path:'tmp/profile-display-layout.png'});
  assert.equal(result.count,21);assert.equal(result.calls,1);
  for(const key of ['compact','canvas','input','scroll','clean'])assert.equal(result[key],true,JSON.stringify(result));
  assert.deepEqual(result.selection,[1,2]);assert.deepEqual(errors,[]);
  console.log('21 profile displays passed: floating diagrams without scene annotations, single-level advanced groups, live drafts, explicit generation and asynchronous focus/selection/scroll preservation.');
} finally {await browser.close();}
