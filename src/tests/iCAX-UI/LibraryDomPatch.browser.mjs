import assert from "node:assert/strict";
import { importBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
const { renderToolLibraryRightPane, handleToolLibraryAction } = await importBrowserAsset('apps/tube-designer/webpage/toolLibrary.mjs');
const collapseView = {
  tubeDesignerSystemPunchTools: [{ id: "test-mold", displayName: "测试模具", kind: "programmatic", target: "part", category: "槽口", inputs: [{ key: "targetSection", valueType: "profile", required: true }], parameters: [] }],
  tubeDesignerToolLibrary: { scope: "system", selectedKey: "system::test-mold", previewLength: 500 },
};
const collapsedPane = renderToolLibraryRightPane({}, collapseView);
await handleToolLibraryAction({}, collapseView, "tube-designer-tool-library-toggle-main-tube", {}, { renderProject() {} });
const expandedPane = renderToolLibraryRightPane({}, collapseView);
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser=await chromium.launch({headless:true,channel:"msedge"});
try {
  const page=await browser.newPage();
  await page.route('http://library-dom-patch.test/**', serveBrowserAsset);
  await page.goto('http://library-dom-patch.test/');
  const result=await page.evaluate(async ({expandedPane,collapsedPane})=>{
    const {patchLibraryDom,rememberLibraryDom}=await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    const {capturePaneInteraction}=await import('/src/apps/_shared/workbench/utils/paneInteractionState.mjs');
    const results=[];
    for(const area of ["tools","profiles","assemblies"]) {
      const hud=area==="tools"?"tube-tool-library-hud":area==="assemblies"?"tube-connection-library-hud":"tube-profile-library-preview-hud";
      const left='<div class="list" style="height:1200px"><button data-cam-action="select">item</button></div>';
      const right=value=>'<div style="height:500px"></div><label><input data-parameter="width" value="'+value+'"></label>'+
        (value==='10'?'<label data-condition-field="extra"><input data-parameter="inactive" value="9"></label>':'')+
        '<div class="nested-scroll" style="height:100px;overflow:auto"><div style="height:700px"></div></div><div style="height:500px"></div>';
      document.body.innerHTML='<main><div class="cam-workbench"><aside class="cam-context-pane" style="height:200px;overflow:auto">'+left+
        '</aside><div class="cam-viewport"><canvas></canvas><div class="cube"></div><div class="'+hud+'">old</div></div>'+
        '<aside class="cam-info-pane" style="height:200px;overflow:auto">'+right("10")+'</aside></div></main>';
      const mount=document.querySelector("main"),view={activeAreaId:area};
      const canvas=mount.querySelector("canvas"),cube=mount.querySelector(".cube");
      const input=mount.querySelector("input"),leftPane=mount.querySelector(".cam-context-pane"),rightPane=mount.querySelector(".cam-info-pane");
      rememberLibraryDom(view,mount,"");
      input.focus({preventScroll:true});input.value="123";input.setSelectionRange(1,2);
      leftPane.scrollTop=280;rightPane.scrollTop=450;
      mount.querySelector('.nested-scroll').scrollTop=180;
      let clicks=0;input.addEventListener("click",()=>clicks++);
      // A delayed response must retain the latest edit and scroll.
      await new Promise(resolve=>setTimeout(resolve,5));
      leftPane.scrollTop=340;
      const restore=capturePaneInteraction(mount);
      view.error="preview error";
      const patched=patchLibraryDom(view,mount,{left,right:right("12"),
        overlay:'<div class="'+hud+'">new status</div>',suffix:""});
      restore();
      input.click();
      results.push(patched && canvas===mount.querySelector("canvas") && cube===mount.querySelector(".cube") &&
        input===mount.querySelector("input") && clicks===1 && document.activeElement===input &&
        input.value==="123" && input.selectionStart===1 && input.selectionEnd===2 &&
        leftPane.scrollTop===340 && rightPane.scrollTop===450 &&
        mount.querySelector('.nested-scroll').scrollTop===180 && !mount.querySelector('[data-condition-field="extra"]') &&
        mount.querySelector("."+hud).textContent==="new status" && mount.querySelector(".cam-status.error").textContent==="preview error");
      // The shared node patch itself must protect a focused draft even when
      // the caller does not perform a complete pane replacement/restoration.
      // Keep typing and scrolling after a response has already been queued.
      let release;
      const response=new Promise(resolve=>{release=resolve;}).then(()=>patchLibraryDom(view,mount,{left,right:right("12"),
        overlay:'<div class="'+hud+'">late response</div>',suffix:""}));
      input.value="12345";input.setSelectionRange(2,4,"backward");
      leftPane.scrollTop=380;rightPane.scrollTop=470;mount.querySelector('.nested-scroll').scrollTop=220;
      release();const latePatched=await response;
      results.push(latePatched && input===mount.querySelector("input") && document.activeElement===input &&
        input.value==="12345" && input.selectionStart===2 && input.selectionEnd===4 && input.selectionDirection==="backward" &&
        leftPane.scrollTop===380 && rightPane.scrollTop===470 && mount.querySelector('.nested-scroll').scrollTop===220 &&
        canvas===mount.querySelector("canvas") && cube===mount.querySelector(".cube"));
      // A new authoritative value must not be mistaken for the stale default.
      results.push(patchLibraryDom(view,mount,{left,right:right("20"),overlay:'<div class="'+hud+'">new model</div>',suffix:""}) &&
        input.value==="20" && input.defaultValue==="20" && document.activeElement===input);
      input.value="unsubmitted";input.setSelectionRange(2,5);
      const replacement=right("20").replace('data-parameter="width"','data-parameter="another-resource-width"');
      results.push(patchLibraryDom(view,mount,{left,right:replacement,overlay:'<div class="'+hud+'">new resource</div>',suffix:""}) &&
        !input.isConnected && mount.querySelector('[data-parameter="another-resource-width"]').value==="20" && document.activeElement!==input);
      results.push(!patchLibraryDom({...view,activeAreaId:"components"},mount,{suffix:""}));
      results.push(!patchLibraryDom(view,mount,{suffix:"new dialog"}));
    }
    document.body.innerHTML='<main><div class="cam-workbench"><aside class="cam-context-pane" style="height:200px;overflow:auto"><div style="height:1600px">left</div></aside><div class="cam-viewport"><canvas></canvas></div><aside class="cam-info-pane" style="height:200px;overflow:auto">'+expandedPane+'<div style="height:1600px"></div></aside></div></main>';
    const mount=document.querySelector('main'),view={activeAreaId:'tools'};
    const selector=mount.querySelector('[data-cam-change-action="tube-designer-tool-library-profile-change"]');
    const toggle=mount.querySelector('[data-cam-action="tube-designer-tool-library-toggle-main-tube"]');
    const content=mount.querySelector('#tube-tool-library-main-tube-content');
    const canvas=mount.querySelector('canvas');
    const left=mount.querySelector('.cam-context-pane'),right=mount.querySelector('.cam-info-pane');
    let clicks=0;toggle.addEventListener('click',()=>clicks++);
    rememberLibraryDom(view,mount,'');
    toggle.focus({preventScroll:true});left.scrollTop=300;right.scrollTop=350;
    for(const [html,hidden] of [[collapsedPane,true],[collapsedPane,true],[expandedPane,false]]) {
      await new Promise(resolve=>setTimeout(resolve,5));
      left.scrollTop=340;right.scrollTop=400;
      const restore=capturePaneInteraction(mount);
      const patched=patchLibraryDom(view,mount,{left:'<div style="height:1600px">left</div>',right:html+'<div style="height:1600px"></div>',overlay:'',suffix:''});
      restore();toggle.click();
      results.push(patched && content.hidden===hidden && selector.isConnected &&
        selector===mount.querySelector('[data-cam-change-action="tube-designer-tool-library-profile-change"]') &&
        toggle===document.activeElement && toggle.getAttribute('aria-expanded')===String(!hidden) &&
        canvas===mount.querySelector('canvas') && left.scrollTop===340 && right.scrollTop===400);
    }
    results.push(clicks===3);
    // Disassembly progress belongs to the product workspace, not the model
    // lifecycle. Adding, updating and removing it must preserve both editors.
    const productLeft='<div class="nested-left" style="height:120px;overflow:auto"><input data-note value="draft"><div style="height:1400px"></div></div><div style="height:1400px"></div>';
    const productRight='<input data-parameter="code" value="original"><div class="nested-right" style="height:120px;overflow:auto"><div style="height:1400px"></div></div><div style="height:1400px"></div>';
    document.body.innerHTML='<main><div class="cam-workbench"><aside class="cam-context-pane" style="height:200px;overflow:auto">'+productLeft+'</aside><div class="cam-viewport"><canvas></canvas><button class="cube">view</button></div><aside class="cam-info-pane" style="height:200px;overflow:auto">'+productRight+'</aside></div></main>';
    const productMount=document.querySelector('main');
    const productView={activeAreaId:'view',scene:{tubeDesigner:{product:{entityId:'product',templateId:'window'},members:[{entityId:'member',previewGeometryResourceId:'geometry',previewGeometryResourceVersion:1,transform:[1,0,0]}]}}};
    const productCanvas=productMount.querySelector('canvas'),productCube=productMount.querySelector('.cube');
    const productInput=productMount.querySelector('[data-parameter="code"]');
    const scrolls=['.cam-context-pane','.cam-info-pane','.nested-left','.nested-right'].map(selector=>productMount.querySelector(selector));
    let productClicks=0;productInput.addEventListener('click',()=>productClicks++);
    rememberLibraryDom(productView,productMount,'');
    productInput.focus({preventScroll:true});productInput.value='uncommitted';productInput.setSelectionRange(2,6);
    for(const [index,stage] of ['start','machining',''].entries()) {
      scrolls.forEach((node,i)=>node.scrollTop=180+i*30);
      await new Promise(resolve=>setTimeout(resolve,5));
      scrolls.forEach((node,i)=>node.scrollTop=240+i*30+index);
      productInput.value='uncommitted-'+index;productInput.setSelectionRange(3,8,'backward');
      const before=scrolls.map(node=>node.scrollTop);
      const suffix=stage?'<div data-tube-designer-operation-wait>'+stage+'</div>':'';
      const patched=patchLibraryDom(productView,productMount,{left:productLeft,right:productRight,overlay:'',suffix});
      productInput.click();
      const progress=productMount.querySelector('[data-tube-designer-operation-wait]');
      results.push(patched && productCanvas===productMount.querySelector('canvas') && productCube===productMount.querySelector('.cube') &&
        productInput===productMount.querySelector('[data-parameter="code"]') && document.activeElement===productInput &&
        productInput.value==='uncommitted-'+index && productInput.selectionStart===3 && productInput.selectionEnd===8 && productInput.selectionDirection==='backward' &&
        scrolls.every((node,i)=>node.scrollTop===before[i]) && (stage?progress?.textContent===stage:!progress));
    }
    results.push(productClicks===3);
    productView.scene.tubeDesigner.members[0].previewGeometryResourceVersion=2;
    results.push(!patchLibraryDom(productView,productMount,{left:productLeft,right:productRight,overlay:'',suffix:''}));
    return results;
  },{expandedPane,collapsedPane});
  assert.ok(result.every(Boolean),JSON.stringify(result));
  console.log("Resource editors and product disassembly progress preserve canvas, controls/listeners, focus, selection and current nested scrolling; changed geometry retains its viewport lifecycle.");
} finally {await browser.close();}
