import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { resolve, sep } from 'node:path';
import { tubeDesignerCss } from '../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs';
const descriptor = JSON.parse(readFileSync(new URL('../../apps/tube-designer/templates/profile/rect/profile.json', import.meta.url)));
const presentationDescriptors = Object.fromEntries(['round-bar','polygon-bar','rect-bar','open-tube'].map(id => {
  const item = JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/profile/${id}/profile.json`, import.meta.url)));
  item.display = JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/profile/${id}/display.json`, import.meta.url)));
  return [id,item];
}));
const tool = JSON.parse(readFileSync(new URL('../../apps/tube-designer/templates/mold/v-notch-sharp/tool.json', import.meta.url)));
tool.defaultParameters = Object.fromEntries(tool.parameters.map(field => [field.key, field.defaultValue]));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: 'msedge' });
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
  const sourceRoot = fileURLToPath(new URL('../../', import.meta.url));
  await page.route('http://diagram-dock.test/**', route => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === '/') return route.fulfill({ contentType: 'text/html', body: '<body></body>' });
    const requested = pathname.replace(/^\/src\//, '');
    const mapped = process.env.ICAX_TEST_RUNTIME_WEBPAGE && requested.startsWith('apps/tube-designer/webpage/')
      ? requested.replace('apps/tube-designer/webpage/', 'x64/Debug/apps/tube-designer/webpage/') : requested;
    const path = resolve(sourceRoot, mapped);
    if (!pathname.startsWith('/src/') || !path.startsWith(sourceRoot.replace(/[\\/]$/, '') + sep) || !/\.m?js$/.test(path)) return route.abort();
    return route.fulfill({ contentType: 'text/javascript', body: readFileSync(path, 'utf8') });
  });
  await page.goto('http://diagram-dock.test/');
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.addStyleTag({ content: `${tubeDesignerCss} *{box-sizing:border-box}body{margin:0} .cam-workbench{display:grid;grid-template-columns:180px 700px 360px;height:700px}.cam-context-pane,.cam-info-pane{overflow:auto;min-height:0}.cam-viewport{position:relative;min-width:0;background:#14272e}.tube-profile-library-editor,.tube-tool-library-editor{height:100%}.left-list{height:1800px}.nested-scroll{height:80px;overflow:auto}.nested-scroll > div{height:600px}` });
  const results = await page.evaluate(async ({ descriptor, presentationDescriptors, tool, areas }) => {
    const profiles = await import('/src/apps/tube-designer/webpage/profileLibrary.mjs');
    const tools = await import('/src/apps/tube-designer/webpage/toolLibrary.mjs');
    const { bindProfileParameterDiagrams } = await import('/src/apps/tube-designer/webpage/profileParameterDiagram.mjs');
    const { bindToolParameterDiagrams } = await import('/src/apps/tube-designer/webpage/toolParameterDiagram.mjs');
    const { patchLibraryDom, rememberLibraryDom } = await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    const { bindDiagramDragging } = await import('/src/apps/tube-designer/webpage/floatingParameterDiagram.mjs');
    const parameterValues = descriptor => Object.fromEntries(descriptor.parameters.map(field => [field.key,field.defaultValue]));
    const presentationProfiles = [
      ['round-bar',{kind:'circle',center:[0,0],radius:15}],
      ['polygon-bar',{kind:'polygon',points:[[-15,-8],[0,-16],[15,-8],[15,8],[0,16],[-15,8]]}],
      ['rect-bar',{kind:'polygon',points:[[-20,-4],[20,-4],[20,4],[-20,4]]}],
      ['open-tube',{kind:'path',closed:true,segments:[{kind:'line',start:[-20,-2],end:[20,-2]},{kind:'line',start:[20,-2],end:[20,2]},{kind:'line',start:[20,2],end:[-20,2]},{kind:'line',start:[-20,2],end:[-20,-2]}]}],
    ].map(([id,contour]) => ({id,name:presentationDescriptors[id].displayName,profileForm:'parametric',profileType:'profile-package',libraryScope:'system',descriptor:presentationDescriptors[id],defaultParameters:parameterValues(presentationDescriptors[id]),previewProfile:{name:presentationDescriptors[id].displayName,contours:[contour]}}));
    const presentationView = {activeAreaId:'profiles',pending:false,tubeDesignerSystemProfiles:presentationProfiles,tubeDesignerUserData:{profiles:[]},tubeDesignerProfileLibrary:{scope:'system'}};
    document.body.innerHTML = profiles.renderProfileLibraryLeftPane({},presentationView);
    const solidPreviews = [...document.querySelectorAll('.tube-profile-library-card-preview.is-solid')];
    if(solidPreviews.length!==3)throw Error('round, polygon and flat bars must use solid library previews');
    if(solidPreviews.some(node=>getComputedStyle(node.querySelector('.outer')).fill==='none'))throw Error('solid bar library previews must be filled');
    const expectedGroups = {
      'round-bar':['dimensions'],
      'polygon-bar':['dimensions'],
      'rect-bar':['dimensions','advanced'],
      'open-tube':['dimensions','advanced'],
    };
    for(const profile of presentationProfiles){
      presentationView.tubeDesignerSelectedProfileId=`system:${profile.id}`;
      document.body.innerHTML=profiles.renderProfileLibraryRightPane({},presentationView);
      const groups=[...document.querySelectorAll('[data-profile-display-group]')].map(node=>node.dataset.profileDisplayGroup);
      if(JSON.stringify(groups)!==JSON.stringify(expectedGroups[profile.id]))throw Error(`${profile.id} parameter groups: ${groups.join(',')}`);
      if(document.body.textContent.includes('轮廓与模型'))throw Error(`${profile.id} must not show the legacy contour/model group`);
      if(profile.id==='polygon-bar'){
        const keys=[...document.querySelectorAll('[data-profile-parameter-key]')].map(node=>node.dataset.profileParameterKey);
        if(JSON.stringify(keys)!==JSON.stringify(['radius','sideCount','cornerRadius']))throw Error(`polygon-bar fields: ${keys.join(',')}`);
        if(document.querySelector('details[data-parameter-advanced]'))throw Error('polygon-bar must not render advanced settings');
      }
    }
    const values = Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue]));
    descriptor.parameters.find(d=>d.key==='innerRadius4').presentation={visible:false};
    descriptor.parameters.push(
      {key:'draftNote',displayName:'草稿备注',valueType:'string',defaultValue:'draft',presentation:{advanced:true}},
      {key:'expertWhenWide',displayName:'条件高级字段',valueType:'string',defaultValue:'kept',presentation:{advanced:true},visibleWhen:{op:'eq',parameter:'width',value:70}});
    tool.parameters.find(d=>d.key==='angle').presentation={advanced:true};
    tool.parameters.find(d=>d.key==='leaveBottom').presentation={visible:false};
    const snapshot = { name: '方管', width: 60, depth: 40, parameters: values,
      contours: [{ kind: 'polygon', points: [[-30,-20],[30,-20],[30,20],[-30,20]] }],
      parameterDiagram: { schemaVersion: 1, annotations: [{ parameter: 'width', kind: 'linear', axis: 'x', side: 'top', from: [-30,20], to: [30,20] },
        {parameter:'draftNote',kind:'leader',side:'right',point:[30,20]},
        {parameter:'innerRadius4',kind:'leader',side:'left',point:[-30,20]}] } };
    const profile = { id: 'rect', name: '方管', profileForm: 'parametric', profileType: 'profile-package', descriptor, defaultParameters: values, previewProfile: snapshot };
    const outcomes = [];
    for (const area of areas) {
      const view = { activeAreaId: area, pending: false, tubeDesignerSystemProfiles: [profile], tubeDesignerSystemPunchTools: [tool], tubeDesignerUserData: { profiles: [] },
        tubeDesignerSelectedProfileId: 'system:rect', tubeDesignerProfileLibrary: { scope: 'system' }, tubeDesignerToolLibrary: { scope: 'system', selectedKey: 'system::v-notch-sharp' } };
      const library = area === 'profiles' ? profiles : tools;
      const right = () => area === 'profiles' ? profiles.renderProfileLibraryRightPane({}, view) : tools.renderToolLibraryRightPane({}, view);
      const overlay = () => area === 'profiles' ? profiles.renderProfileLibraryViewportOverlay({}, view) : tools.renderToolLibraryViewportOverlay({}, view);
      const left = '<div class="left-list">资源列表</div>';
      document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport"><div class="cam-render-viewport-shell"><div data-cam-render-viewport><div class="icax-three-viewport" data-canonical-viewport><canvas class="icax-three-viewport-canvas"></canvas></div></div></div><div class="cube"></div>${overlay()}</div><aside class="cam-info-pane">${right()}</aside></div></main>`;
      const mount = document.querySelector('main'), context = { mount };
      if(mount.querySelector('[data-profile-parameter-key="innerRadius4"], [data-profile-annotation-key="innerRadius4"], [data-tool-parameter-key="leaveBottom"], [data-tool-annotation-key="leaveBottom"]'))throw Error('hidden fields must not appear in editors or diagrams');
      bindDiagramDragging(mount,view);
      const floatingLayer=mount.querySelector(':scope .cam-workbench > [data-floating-parameter-diagram-layer]');
      const floatingDock=mount.querySelector('.tube-library-diagram-dock');
      if(!floatingLayer||floatingDock.parentElement!==floatingLayer||mount.querySelector('.cam-viewport .tube-library-diagram-dock'))throw Error('diagram must be a direct child of the workbench floating layer');
      let annotations=0,cleared=0;
      const canonicalRoot=mount.querySelector('[data-canonical-viewport]');
      const canonicalCanvas=canonicalRoot.querySelector('canvas');
      view.viewport={
        root:canonicalRoot,
        renderer:{domElement:canonicalCanvas},
        mount(host){host.replaceChildren(canonicalRoot);},
        setSpecificationAnnotations(){annotations++;},
        clearSpecificationAnnotations(){cleared++;},
        resize(){view.viewportResizeCalls=(view.viewportResizeCalls??0)+1;},
      };
      profiles.bindProfileSpecificationAnnotations(mount,view);
      if(annotations!==0||cleared!==1||mount.querySelector('[data-tube-designer-specification-tree]'))throw Error('library scenes must remain unannotated');
      const canvas = canonicalCanvas, cube = mount.querySelector('.cube');
      const input = mount.querySelector('[data-profile-parameter-key="draftNote"]');
      const advanced = input.closest('details[data-parameter-advanced]');
      if (!advanced || advanced.open) throw Error('advanced corner definitions must start collapsed');
      advanced.open = true;
      input.focus({ preventScroll: true }); input.value = 'draft'; input.setSelectionRange(1,3);
      const leftPane = mount.querySelector('.cam-context-pane'), body = mount.querySelector(area === 'profiles' ? '.tube-profile-library-editor-body' : '.tube-tool-library-editor-body');
      if(area==='profiles'){
        const probe=document.createElement('div');probe.style.height='1200px';probe.style.minHeight='1200px';body.append(probe);
        if(body.scrollHeight<=body.clientHeight)throw Error('profile parameter body must own the overflow above its fixed footer');
        body.scrollTop=60;
        if(body.scrollTop<=0)throw Error('profile parameter body must expose a working vertical scrollbar');
        probe.remove();body.scrollTop=0;
      }
      leftPane.scrollTop = 300; body.scrollTop = 130;
      bindProfileParameterDiagrams(mount); bindToolParameterDiagrams(mount);
      const dock = mount.querySelector('.tube-library-diagram-dock');
      const toggle = kind => area === 'profiles' ? mount.querySelector('.cam-info-pane [data-profile-diagram-toggle]') : mount.querySelector(`.cam-info-pane [data-tube-tool-library-diagram="${kind}"]`);
      if(dock.querySelector('[data-profile-diagram-toggle],[data-tube-tool-library-diagram]'))throw Error('viewport must not contain diagram toggle buttons');
      const action = async kind => {
        const target = kind === 'close' ? dock.querySelector('[data-library-diagram-close]') : toggle(kind);
        await (area === 'profiles' ? profiles.handleProfileLibraryAction : tools.handleToolLibraryAction)(context, view, target.dataset.camAction, target, { renderProject() { throw Error('toggle must be local'); } });
      };
      const visibleCount = () => [...dock.querySelectorAll('.tube-library-diagram-content')].filter(node => !node.hidden).length;
      if (visibleCount() !== (area==='profiles'?1:0)) throw Error('profile diagram should be open by default; tool tabs start hidden');
      if(area==='tools')await action('profile');
      if (visibleCount() !== 1) throw Error('one profile panel expected');
      const infoBox = mount.querySelector('.cam-info-pane').getBoundingClientRect();
      const dockBox = dock.getBoundingClientRect();
      const profileOwner = area === 'profiles' ? 'profile-library:system:rect' : 'tool-library-profile:main';
      const legend = [...dock.querySelectorAll('[data-parameter-diagram-for]')].find(node => node.dataset.parameterDiagramFor === profileOwner).querySelector('[data-profile-annotation-key="width"]');
      legend.dispatchEvent(new MouseEvent('click', { bubbles: true }));
      if (document.activeElement?.dataset.profileParameterKey !== 'width') throw Error('external profile diagram did not locate pane control');
      if (area === 'tools') {
        await action('tool');
        if (visibleCount() !== 1 || !dock.querySelector('[data-tool-scene-diagram="profile"]').hidden) throw Error('tabs must be mutually exclusive');
        const glyph=dock.querySelector('svg [data-tool-annotation-key="angle"]');
        if(getComputedStyle(glyph).display!=='none')throw Error('advanced SVG annotations must start hidden');
        const expert=dock.querySelector('[data-tool-scene-diagram="tool"] [data-parameter-advanced]');
        expert.open=true;await new Promise(r=>setTimeout(r,0));
        if(getComputedStyle(glyph).display==='none')throw Error('opening advanced must reveal SVG annotations');
        expert.open=false;await new Promise(r=>setTimeout(r,0));
        if(getComputedStyle(glyph).display!=='none')throw Error('closing advanced must hide SVG annotations');
        expert.open=true;await new Promise(r=>setTimeout(r,0));
        dock.querySelector('.tube-tool-library-diagram-row[data-tool-annotation-key="angle"]').click();
        if (document.activeElement?.dataset.toolParameterKey !== 'angle') throw Error('external mould diagram did not locate pane control');
        if(!document.activeElement.closest('details[data-parameter-advanced]')?.open)throw Error('diagram must reveal advanced controls');
        await action('close');
      } else await action('close');
      if (visibleCount() !== 0) throw Error('active tab should hide all panels');
      await action('profile');
      const diagram = dock.querySelector('.tube-library-diagram-content:not([hidden])');
      diagram.insertAdjacentHTML('beforeend', '<div class="nested-scroll"><div></div></div>');
      const nested = diagram.querySelector('.nested-scroll');
      input.focus({ preventScroll: true }); input.setSelectionRange(1,3); leftPane.scrollTop = 320; body.scrollTop = 170; nested.scrollTop = 90; diagram.scrollTop = 70;
      let clicks = 0; input.addEventListener('click', () => clicks++);
      rememberLibraryDom(view, mount, '');
      // While a preview response is pending, continue editing and scrolling.
      await new Promise(done => setTimeout(done, 20));
      input.value = 'latest'; input.setSelectionRange(2,5); leftPane.scrollTop = 350; body.scrollTop = 200; nested.scrollTop = 120; diagram.scrollTop = 85;
      if (area === 'profiles') view.tubeDesignerProfileDrafts = { 'system:rect': { parameters: { ...values, width:70, draftNote: 'latest' } } };
      else view.tubeDesignerToolLibrary.profileDrafts = { 'system:rect': { ...values, width:70, draftNote: 'latest' } };
      const positions = [leftPane.scrollTop, body.scrollTop, nested.scrollTop, diagram.scrollTop];
      // Keep the nested test container within the same diagram for a keyed patch.
      const template = document.createElement('template'); template.innerHTML = overlay();
      template.content.querySelector('.tube-library-diagram-content:not([hidden])').insertAdjacentHTML('beforeend', '<div class="nested-scroll"><div></div></div>');
      if (!patchLibraryDom(view, mount, { left, right: right(), overlay: template.innerHTML, suffix: '' })) throw Error('local patch was not used');
      if(!mount.querySelector('.cam-info-pane [data-profile-parameter-key="expertWhenWide"]'))throw Error('conditional advanced field must appear');
      if(!advanced.open)throw Error('advanced disclosure must stay expanded during patch');
      bindProfileParameterDiagrams(mount); bindToolParameterDiagrams(mount);
      input.dispatchEvent(new MouseEvent('click', { bubbles: true }));
      outcomes.push({ area, tabCount: mount.querySelectorAll('.cam-info-pane [data-profile-diagram-toggle],.cam-info-pane [data-tube-tool-library-diagram]').length,
        rightHasDiagram: !!mount.querySelector('.cam-info-pane [data-profile-parameter-diagram],.cam-info-pane [data-tool-parameter-diagram]'),
        startsInRightPane: dockBox.left>=infoBox.left-1&&dockBox.right<=infoBox.right+1,
        defaultSize: Math.abs(dockBox.width-Math.min(360,infoBox.width-24))<1&&Math.abs(dockBox.height-Math.min(405,infoBox.height-24))<1,
        canvasStable: canvas === mount.querySelector('canvas') && cube === mount.querySelector('.cube'),
        inputStable: input === mount.querySelector('[data-profile-parameter-key="draftNote"]'),
        focused: document.activeElement === input, selection: [input.selectionStart,input.selectionEnd],
        scrollStable: JSON.stringify(positions) === JSON.stringify([leftPane.scrollTop,body.scrollTop,nested.scrollTop,diagram.scrollTop]), clicks });
      window.dockFixture={mount,view,library,right,overlay,left,patchLibraryDom,input,canvas,cube,area};
    }
    return outcomes;
  }, { descriptor, presentationDescriptors, tool, areas:process.env.ICAX_DIAGRAM_TEST_AREA ? [process.env.ICAX_DIAGRAM_TEST_AREA] : ['profiles','tools'] });
  const dock=page.locator('.tube-library-diagram-dock'),before=await dock.boundingBox();
  const viewportBefore=await page.evaluate(()=>{
    const rect=node=>{const r=node.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};};
    return {shell:rect(document.querySelector('.cam-render-viewport-shell')),host:rect(document.querySelector('[data-cam-render-viewport]')),
      roots:document.querySelectorAll('.icax-three-viewport').length,canvases:document.querySelectorAll('.icax-three-viewport-canvas').length};
  });
  const header=await dock.locator('[data-floating-diagram-drag]').boundingBox();
  await page.mouse.move(header.x+60,header.y+header.height/2);await page.mouse.down();
  await page.mouse.move(header.x-390,header.y+header.height/2+50,{steps:8});await page.mouse.up();
  const dragged=await dock.boundingBox();
  const infoLeft=await page.locator('.cam-info-pane').evaluate(node=>node.getBoundingClientRect().left);
  assert.ok(dragged.x<infoLeft-100 && dragged.x<before.x-350 && dragged.y>before.y+20,JSON.stringify({before,header,dragged,infoLeft}));
  const grip=await dock.locator('[data-diagram-resize="nw"]').boundingBox();
  await page.mouse.move(grip.x+6,grip.y+6);await page.mouse.down();
  await page.mouse.move(grip.x-44,grip.y-44,{steps:8});await page.mouse.up();
  const resized=await dock.boundingBox();assert.ok(resized.width>dragged.width+25 && resized.height>dragged.height+25);
  const movedLayout=await page.evaluate(()=>{
    const rect=node=>{const r=node.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};};
    const panel=document.querySelector('.tube-library-diagram-dock');
    return {shell:rect(document.querySelector('.cam-render-viewport-shell')),host:rect(document.querySelector('[data-cam-render-viewport]')),
      roots:document.querySelectorAll('.icax-three-viewport').length,canvases:document.querySelectorAll('.icax-three-viewport-canvas').length,
      inlineLeft:panel.style.left,inlineTop:panel.style.top,transform:getComputedStyle(panel).transform};
  });
  assert.deepEqual(movedLayout.shell,viewportBefore.shell,'拖动示意图不能移动或缩放三维视口外壳');
  assert.deepEqual(movedLayout.host,viewportBefore.host,'拖动示意图不能移动或缩放三维视口宿主');
  assert.equal(movedLayout.roots,1);assert.equal(movedLayout.canvases,1);
  assert.equal(movedLayout.inlineLeft,'0px');assert.equal(movedLayout.inlineTop,'0px');
  assert.notEqual(movedLayout.transform,'none','浮窗必须通过独立合成层移动，不能用 left/top 反复重排 WebGL 上层');
  const stable=await page.evaluate(()=>{
    const f=window.dockFixture, position={...f.view.tubeDesignerLibraryDiagramPositions[f.area]};
    const scroll=f.mount.querySelector('.tube-library-diagram-content:not([hidden])').scrollTop;
    f.patchLibraryDom(f.view,f.mount,{left:f.left,right:f.right(),overlay:f.overlay(),suffix:''});
    return {position,scroll,canvas:f.canvas===f.mount.querySelector('canvas'),input:f.input===document.activeElement,
      selection:[f.input.selectionStart,f.input.selectionEnd]};
  });
  const afterPatch=await dock.boundingBox();assert.ok(Math.abs(afterPatch.x-resized.x)<1 && Math.abs(afterPatch.y-resized.y)<1);
  assert.equal(stable.canvas,true);assert.equal(stable.input,true);assert.deepEqual(stable.selection,[2,5]);
  await page.evaluate(async()=>{
    const {handleDesignerAreaAction}=await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const f=window.dockFixture;
    f.mount.addEventListener('click',event=>{
      const button=event.target.closest('[data-profile-diagram-toggle],[data-tube-tool-library-diagram],[data-library-diagram-close]');
      if(button)handleDesignerAreaAction({mount:f.mount},f.view,button.dataset.camAction,button,
        {renderProject(){throw Error('diagram toggle must not rebuild the scene');}}).catch(error=>{throw error;});
    });
  });
  const controls=page.locator('.cam-info-pane [data-profile-diagram-toggle],.cam-info-pane [data-tube-tool-library-diagram]');
  await dock.locator('[data-library-diagram-close]').click();
  await dock.waitFor({state:'hidden'});
  assert.equal(await page.locator('.cam-viewport [data-profile-diagram-toggle],.cam-viewport [data-tube-tool-library-diagram]').count(),0,'隐藏后场景不留下开关按钮');
  await controls.first().click();
  await dock.waitFor({state:'visible'});
  await controls.first().click();
  assert.equal(await dock.isVisible(),true,'参数面板按钮只负责打开，不应再次点击就关闭');
  assert.equal(await dock.locator('.tube-library-diagram-content:not([hidden]) svg').count()>0,true,'真实按钮必须显示示意图正文');
  const reopened=await dock.boundingBox();assert.ok(Math.abs(reopened.width-resized.width)<1 && Math.abs(reopened.height-resized.height)<1,'尺寸必须跨刷新和收起保留');
  await page.screenshot({ path: 'tmp/library-diagram-dock.png' });
  for (const result of results) {
    assert.equal(result.tabCount, result.area === 'profiles' ? 1 : 2);
    assert.equal(result.rightHasDiagram, false);
    for (const key of ['startsInRightPane','defaultSize','canvasStable','inputStable','focused','scrollStable']) assert.equal(result[key], true, JSON.stringify(result));
    assert.deepEqual(result.selection, [2,5]);
    assert.equal(result.clicks, 1);
  }
  assert.deepEqual(errors, []);
  console.log('Library diagrams passed: pane open buttons, floating close buttons, drag/resize persistence, canvas/control/listener identity, latest focus/selection and nested scroll.');
} finally { await browser.close(); }
