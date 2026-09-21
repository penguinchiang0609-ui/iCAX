import assert from 'node:assert/strict';
import {readFileSync,readdirSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {resolve,sep} from 'node:path';
import {tubeDesignerCss} from '../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs';
const root=fileURLToPath(new URL('../../',import.meta.url)).replace(/[\\/]$/,'');
const templates=new URL('../../apps/tube-designer/templates/',import.meta.url);
const read=p=>JSON.parse(readFileSync(new URL(p,templates)));
const descriptors=readdirSync(new URL('mold/',templates)).filter(id=>id.startsWith('end-')).map(id=>read(`mold/${id}/tool.json`));
const profiles=['rect','round'].map(id=>read(`profile/${id}/profile.json`));
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.launch({headless:true,channel:'msedge'});
try {
  const page=await browser.newPage({viewport:{width:1200,height:800}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.route('http://end-library.test/**',route=>{
    const path=new URL(route.request().url()).pathname;
    if(path==='/')return route.fulfill({contentType:'text/html',body:'<body></body>'});
    const file=resolve(root,path.replace(/^\/src\//,''));
    if(!path.startsWith('/src/')||!file.startsWith(root+sep)||!/\.m?js$/.test(file))return route.abort();
    return route.fulfill({contentType:'text/javascript',body:readFileSync(file,'utf8')});
  });
  await page.goto('http://end-library.test/');
  await page.addStyleTag({content:tubeDesignerCss+`*{box-sizing:border-box}body{margin:0}.cam-workbench{display:grid;grid-template-columns:220px 500px 400px;height:740px}.cam-context-pane,.cam-info-pane{height:500px;overflow:auto}.cam-viewport{background:#14272e}.tube-tool-library-editor{height:760px}.tube-tool-library-editor-body{height:330px;overflow:auto}`});
  const result=await page.evaluate(async({descriptors,profiles})=>{
    const lib=await import('/src/apps/tube-designer/webpage/toolLibrary.mjs');
    const {rememberLibraryDom,patchLibraryDom}=await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    const {capturePaneInteraction}=await import('/src/apps/_shared/workbench/utils/paneInteractionState.mjs');
    const defaults=d=>Object.fromEntries(d.parameters.map(p=>[p.key,p.defaultValue]));
    const check=(x,message)=>{if(!x)throw Error(message);},results=[];
    for(const descriptor of descriptors){
      const view={activeAreaId:'tools',tubeDesignerSystemPunchTools:[{...descriptor,defaultParameters:defaults(descriptor)}],
        tubeDesignerSystemProfiles:profiles.map(d=>({id:d.id,name:d.displayName,libraryScope:'system',profileForm:'parametric',profileType:'profile-package',descriptor:d,defaultParameters:defaults(d),previewProfile:{contours:[{kind:'circle',radius:d.id==='round'?20:30}]}})),
        tubeDesignerToolLibrary:{scope:'system',selectedKey:`system::${descriptor.id}`,mainTubeCollapsed:false}};
      const mount=document.createElement('div');document.body.replaceChildren(mount);
      const left=()=>lib.renderToolLibraryLeftPane({},view)+'<div style="height:1200px"></div>';
      const right=()=>lib.renderToolLibraryRightPane({},view);
      mount.innerHTML=`<div class="cam-workbench"><aside class="cam-context-pane">${left()}</aside><main class="cam-viewport"><canvas></canvas></main><aside class="cam-info-pane">${right()}</aside></div>`;
      const tool=lib.libraryTools(view)[0],state=lib.toolLibraryState(view);
      const pane=mount.querySelector('.cam-info-pane'),lp=mount.querySelector('.cam-context-pane'),body=pane.querySelector('.tube-tool-library-editor-body'),canvas=mount.querySelector('canvas');
      for(const detail of pane.querySelectorAll('details'))detail.open=true;
      const input=pane.querySelector('[data-tube-tool-library-operation-parameter="trim"]');
      check(input,`missing trim: ${descriptor.id}`);
      let calls=0;input.addEventListener('click',()=>calls++);
      view.viewport={async applyViewSnapshot(s){return {applied:true,entityIds:s.rows.map(r=>r.entityId)};}};
      rememberLibraryDom(view,mount,'');
      const render=()=>{const restore=capturePaneInteraction(mount);check(patchLibraryDom(view,mount,{left:left(),right:right(),overlay:'',suffix:''}),'local patch');restore();};
      const key=lib.toolPreviewKey(view,tool),request=state.previewRequest={};
      let release;const pending=new Promise(r=>release=r);
      const response=pending.then(()=>lib.applyToolLibraryPreview({},view,tool,{baseGeometry:{url:'resource:blank',version:1},toolPreviews:[{key:'start',target:'end',geometry:{url:'resource:tool',version:1}}],previewToolsComplete:true},key,request)).then(render);
      input.focus({preventScroll:true});lp.scrollTop=200;pane.scrollTop=80;body.scrollTop=30;
      await new Promise(r=>setTimeout(r,15));lp.scrollTop=290;pane.scrollTop=130;body.scrollTop=70;
      const scroll=[lp.scrollTop,pane.scrollTop,body.scrollTop];check(scroll.every(x=>x>0),'scroll fixture');
      release();await response;
      check(input===document.activeElement && input===pane.querySelector('[data-tube-tool-library-operation-parameter="trim"]'),'focus/control identity');
      check(JSON.stringify(scroll)===JSON.stringify([lp.scrollTop,pane.scrollTop,body.scrollTop]),'latest scrolling');
      input.click();check(calls===1,'listener identity');
      input.value='17';await lib.handleToolLibraryAction({},view,'tube-designer-tool-library-operation-parameter-change',input,{renderProject:render});
      check(lib.buildToolLibraryPreviewPayload(view,tool).ends.start.trim===17,'edited pose reaches generation');
      if(descriptor.requiresSection){
        const selector=pane.querySelector('select[data-tube-tool-library-profile-role="branch"]');
        check(selector,'independent cutter selector');
        const main=lib.buildToolLibraryPreviewPayload(view,tool).profileRef.id;
        selector.value=main==='rect'?'system:round':'system:rect';
        await lib.handleToolLibraryAction({},view,'tube-designer-tool-library-profile-change',selector,{renderProject:render});
        const payload=lib.buildToolLibraryPreviewPayload(view,tool);
        check(payload.profileRef.id===main && payload.ends.start.section.profileRef.id!==main,'independent sections');
      }
      const gender=pane.querySelector('[data-tube-tool-library-parameter="gender"]');
      if(gender){
        const search=lp.querySelector('input');search.focus({preventScroll:true});search.value=descriptor.displayName;state.search=search.value;search.setSelectionRange(0,1);
        gender.value='female';await lib.handleToolLibraryAction({},view,'tube-designer-tool-library-parameter-change',gender,{renderProject:render});
        check(pane.querySelector('[data-tube-tool-library-parameter="sideClearance"]'),'female clearance condition');
        check(search===document.activeElement&&search.selectionStart===0&&search.selectionEnd===1,'selection on conditional update');
      }
      check(input.isConnected&&canvas===mount.querySelector('canvas'),'no remount');results.push(descriptor.id);
    }
    return results;
  },{descriptors,profiles});
  assert.equal(result.length,5);assert.deepEqual(errors,[]);
  console.log('Five end browser regressions passed: editable poses, independent sections, conditional fields, canvas/control/listener identity and latest focus/selection/scroll.');
} finally {await browser.close();}
