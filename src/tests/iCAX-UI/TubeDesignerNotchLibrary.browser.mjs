import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { resolve, sep } from 'node:path';
import { tubeDesignerCss } from '../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs';
const ids=['edge-arc-groove','embedded-arc-notch','segmented-bend','v-notch-sharp'];
const read=path=>JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/${path}`,import.meta.url)));
const descriptors=ids.map(id=>read(`mold/${id}/tool.json`));
const profileDescriptor=read('profile/rect/profile.json');
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser=await chromium.launch({headless:true,channel:'msedge'});
try {
  const page=await browser.newPage({viewport:{width:1300,height:780}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  const root=fileURLToPath(new URL('../../',import.meta.url));
  await page.route('http://notch.test/**',route=>{
    const url=new URL(route.request().url());
    if(url.pathname==='/')return route.fulfill({contentType:'text/html',body:'<body></body>'});
    const file=resolve(root,url.pathname.replace(/^\/src\//,''));
    if(!url.pathname.startsWith('/src/')||!file.startsWith(root.replace(/[\\/]$/,'')+sep)||!/\.m?js$/.test(file))return route.abort();
    return route.fulfill({contentType:'text/javascript',body:readFileSync(file,'utf8')});
  });
  await page.goto('http://notch.test/');
  await page.addStyleTag({content:`${tubeDesignerCss} *{box-sizing:border-box}body{margin:0}.cam-workbench{display:grid;grid-template-columns:220px 680px 360px;height:700px}.cam-context-pane,.cam-info-pane{overflow:auto;min-height:0}.cam-viewport{position:relative;background:#14272e}.tube-tool-library-editor-body{height:380px;flex:none;overflow:auto}.cam-info-pane{height:500px}.tube-tool-library-editor{height:720px}`});
  const results=await page.evaluate(async({descriptors,profileDescriptor})=>{
    const lib=await import('/src/apps/tube-designer/webpage/toolLibrary.mjs');
    const {rememberLibraryDom,patchLibraryDom}=await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    const {capturePaneInteraction}=await import('/src/apps/_shared/workbench/utils/paneInteractionState.mjs');
    const defaults=d=>Object.fromEntries(d.parameters.map(p=>[p.key,p.defaultValue]));
    const results=[];
    for(const descriptor of descriptors){
      const view={activeAreaId:'tools',tubeDesignerSystemPunchTools:[{...descriptor,defaultParameters:defaults(descriptor)}],
        tubeDesignerSystemProfiles:[{id:'rect',name:'方管',libraryScope:'system',profileForm:'parametric',profileType:'profile-package',
          descriptor:profileDescriptor,defaultParameters:defaults(profileDescriptor),previewProfile:{contours:[{kind:'polygon',points:[[-30,-20],[30,-20],[30,20],[-30,20]]}]}}],
        tubeDesignerToolLibrary:{scope:'system',selectedKey:`system::${descriptor.id}`,mainTubeCollapsed:false}};
      const mount=document.createElement('div');document.body.replaceChildren(mount);
      const left=()=>lib.renderToolLibraryLeftPane({},view)+'<div style="height:900px"></div>';
      const right=()=>lib.renderToolLibraryRightPane({},view);
      const overlay=()=>lib.renderToolLibraryViewportOverlay({},view);
      mount.innerHTML=`<div class="cam-workbench"><aside class="cam-context-pane">${left()}</aside><main class="cam-viewport"><div class="cam-render-viewport-shell"><canvas></canvas></div>${overlay()}</main><aside class="cam-info-pane">${right()}</aside></div>`;
      const state=lib.toolLibraryState(view),tool=lib.libraryTools(view)[0];
      const leftPane=mount.querySelector('.cam-context-pane'),rightPane=mount.querySelector('.cam-info-pane');
      const body=mount.querySelector('.tube-tool-library-editor-body'),canvas=mount.querySelector('canvas');
      for(const details of rightPane.querySelectorAll('details'))details.open=true;
      // Real control bounds, including selectors inside advanced wrappers.
      // Narrow/default/wide panes must never squeeze a label behind a select.
      const workbench=mount.querySelector('.cam-workbench');
      for(const width of [320,400,520]) {
        workbench.style.gridTemplateColumns=`220px 500px ${width}px`;
        for(const select of rightPane.querySelectorAll('.tube-profile-library-field-grid select')) {
          if(!select.getClientRects().length)continue;
          const field=select.closest('.tube-designer-field');
          const grid=field.closest('.tube-profile-library-field-grid');
          const f=field.getBoundingClientRect(),g=grid.getBoundingClientRect();
          const label=field.querySelector('span').getBoundingClientRect(),control=select.getBoundingClientRect();
          const style=getComputedStyle(grid);
          const innerWidth=g.width-parseFloat(style.paddingLeft)-parseFloat(style.paddingRight);
          if(Math.abs(f.width-innerWidth)>2)throw Error(`choice must span the full row: ${descriptor.id}/${width}`);
          if(label.width<64||control.width<120||label.right>control.left+1||control.right>f.right+1)
            throw Error(`choice overlaps or squeezes its label: ${descriptor.id}/${width}`);
          if(field.scrollWidth>Math.ceil(f.width))throw Error('choice overflows its field');
        }
        const numbers=[...rightPane.querySelectorAll('.tube-profile-library-field-grid > .is-number')].filter(f=>f.getClientRects().length);
        if(numbers.some(f=>f.getBoundingClientRect().width>f.parentElement.getBoundingClientRect().width*0.55))
          throw Error('numeric fields should retain the compact two-column layout');
      }
      workbench.style.gridTemplateColumns='220px 680px 360px';
      const keyField=descriptor.id==='embedded-arc-notch'?'arcRadius':descriptor.id==='v-notch-sharp'?'leaveBottom':'angle';
      const input=rightPane.querySelector(`[data-tube-tool-library-parameter="${keyField}"]`);
      const wall=()=>rightPane.querySelector('[data-tube-tool-library-parameter="wallThickness"]');
      if(!wall()?.disabled || wall().value!=='')throw Error('measurement must start empty/read-only');
      let calls=0;input.addEventListener('click',()=>calls++);
      const snapshots=[];
      view.viewport={async applyViewSnapshot(s){snapshots.push(s);return {applied:true,entityIds:s.rows.map(r=>r.entityId)};}};
      rememberLibraryDom(view,mount,'');
      const render=()=>{
        const restore=capturePaneInteraction(mount);
        if(!patchLibraryDom(view,mount,{left:left(),right:right(),overlay:overlay(),suffix:''}))throw Error('local patch required');
        restore();
      };
      const request=state.previewRequest={},key=lib.toolPreviewKey(view,tool);
      let release;const pending=new Promise(done=>{release=done;});
      const refresh=pending.then(()=>lib.applyToolLibraryPreview({},view,tool,{
        baseGeometry:{url:'resource:base',version:1},toolPreviews:[{key:'library-preview',target:'feature',geometry:{url:'resource:cut',version:1}}],
        sectionAnalyses:[{applicable:true,parameters:{wallThickness:1.9999999999999998}}],previewToolsComplete:true,
      },key,request)).then(render);
      // Continue manipulating both sidebars while the response is pending.
      input.focus({preventScroll:true});leftPane.scrollTop=270;rightPane.scrollTop=80;body.scrollTop=60;
      await new Promise(done=>setTimeout(done,20));
      leftPane.scrollTop=310;rightPane.scrollTop=100;body.scrollTop=100;
      const positions=[leftPane.scrollTop,rightPane.scrollTop,body.scrollTop];
      if(positions.some(p=>p<=0))throw Error(`fixture must scroll both panes and nested body: ${positions}`);
      release();await refresh;
      if(wall().value!=='2'||!wall().disabled)throw Error('measured thickness must hide floating-point noise');
      if(state.preview.response.sectionAnalyses[0].parameters.wallThickness!==1.9999999999999998)throw Error('measurement precision changed');
      if(input!==document.activeElement||input!==rightPane.querySelector(`[data-tube-tool-library-parameter="${keyField}"]`))throw Error('input identity/focus lost');
      if(JSON.stringify(positions)!==JSON.stringify([leftPane.scrollTop,rightPane.scrollTop,body.scrollTop]))throw Error('async response changed latest scroll');
      input.click();if(calls!==1)throw Error('listener lost');
      // Field conditions use the normal action path and preserve sibling nodes.
      const [toggleKey,childKey]=descriptor.id==='edge-arc-groove'?['bottomCut','bottomCutWidth']:
        descriptor.id==='embedded-arc-notch'?['maleFemale','maleFemaleSize']:
        descriptor.id==='segmented-bend'?['bendCompensation','kFactor']:['maleFemale','maleFemaleSize'];
      const toggle=rightPane.querySelector(`[data-tube-tool-library-parameter="${toggleKey}"]`);
      if(rightPane.querySelector(`[data-tube-tool-library-parameter="${childKey}"]`))throw Error('child should initially be hidden');
      const search=leftPane.querySelector('input[type="search"],input[type="text"]');
      search.focus({preventScroll:true});search.value=descriptor.displayName;state.search=search.value;search.setSelectionRange(0,1);
      toggle.checked=true;
      await lib.handleToolLibraryAction({mount},view,'tube-designer-tool-library-parameter-change',toggle,{renderProject:render});
      const child=rightPane.querySelector(`[data-tube-tool-library-parameter="${childKey}"]`);
      if(!child)throw Error('child did not appear');
      if(['embedded-arc-notch','v-notch-sharp'].includes(descriptor.id)&&child.value!=='2')throw Error(`measured wall thickness was not visibly filled: ${descriptor.id}/${child.value}`);
      if(['embedded-arc-notch','v-notch-sharp'].includes(descriptor.id)) {
        child.value='3';
        await lib.handleToolLibraryAction({mount},view,'tube-designer-tool-library-parameter-change',child,{renderProject:render});
        let currentToggle=rightPane.querySelector(`[data-tube-tool-library-parameter="${toggleKey}"]`);
        currentToggle.checked=false;
        await lib.handleToolLibraryAction({mount},view,'tube-designer-tool-library-parameter-change',currentToggle,{renderProject:render});
        currentToggle=rightPane.querySelector(`[data-tube-tool-library-parameter="${toggleKey}"]`);
        currentToggle.checked=true;
        await lib.handleToolLibraryAction({mount},view,'tube-designer-tool-library-parameter-change',currentToggle,{renderProject:render});
        if(rightPane.querySelector(`[data-tube-tool-library-parameter="${childKey}"]`)?.value!=='3')throw Error(`manual公母尺寸 was overwritten: ${descriptor.id}`);
      }
      if(search!==document.activeElement||search.selectionStart!==0||search.selectionEnd!==1)throw Error('condition patch lost current text selection');
      if(canvas!==mount.querySelector('canvas')||input!==rightPane.querySelector(`[data-tube-tool-library-parameter="${keyField}"]`))throw Error(`condition patch rebuilt scene/control: ${descriptor.id}/${keyField}, canvas=${canvas===mount.querySelector('canvas')}, inputConnected=${input.isConnected}`);
      // A failed response clears all old/partial cutters, keeps only this blank.
      const failedKey=lib.toolPreviewKey(view,tool);
      try{await lib.applyToolLibraryPreview({},view,tool,{baseGeometry:{url:'resource:base2',version:2},resultError:'截面缺少平直壁'},failedKey,request);throw Error('failure not reported');}
      catch(e){if(e.message!=='截面缺少平直壁')throw e;}
      render();
      if(snapshots.at(-1).rows.some(r=>r.entityId!=='punch-preview-blank'))throw Error('stale cutter remains');
      results.push(descriptor.id);
    }
    return results;
  },{descriptors,profileDescriptor});
  assert.deepEqual(results,ids);assert.deepEqual(errors,[]);
  console.log('Four notch browser regressions passed: full-row choices at 320/400/520px, compact numbers, measurement display, conditional edits, node/listener/canvas identity, pending-response focus/selection/scroll, failure cleanup.');
} finally {await browser.close();}
