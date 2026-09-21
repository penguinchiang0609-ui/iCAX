import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { renderToolLibraryRightPane, handleToolLibraryAction } from "../../apps/tube-designer/webpage/toolLibrary.mjs";
const collapseView = {
  tubeDesignerSystemPunchTools: [{ id: "test-mold", displayName: "测试模具", kind: "programmatic", target: "part", category: "槽口", inputs: [{ key: "targetSection", valueType: "profile", required: true }], parameters: [] }],
  tubeDesignerToolLibrary: { scope: "system", selectedKey: "system::test-mold", previewLength: 500 },
};
const collapsedPane = renderToolLibraryRightPane({}, collapseView);
await handleToolLibraryAction({}, collapseView, "tube-designer-tool-library-toggle-main-tube", {}, { renderProject() {} });
const expandedPane = renderToolLibraryRightPane({}, collapseView);
const read=path=>readFileSync(new URL(path,import.meta.url),"utf8");
const data=source=>"data:text/javascript;charset=utf-8,"+encodeURIComponent(source);
const patchUrl=data(read("../../apps/tube-designer/webpage/punchDomPatch.mjs"));
const floatingUrl=data('export function floatingParameterDiagramHost(){return null} export function moveFloatingParameterDiagramsToWorkspace(){return null}');
const libraryUrl=data(read("../../apps/tube-designer/webpage/libraryDomPatch.mjs")
  .replace('"./punchDomPatch.mjs"',JSON.stringify(patchUrl))
  .replace('"./floatingParameterDiagram.mjs"',JSON.stringify(floatingUrl)));
const stateUrl=data(read("../../apps/_shared/workbench/utils/paneInteractionState.mjs"));
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser=await chromium.launch({headless:true,channel:"msedge"});
try {
  const page=await browser.newPage();
  const result=await page.evaluate(async ({libraryUrl,stateUrl,expandedPane,collapsedPane})=>{
    const {patchLibraryDom,rememberLibraryDom}=await import(libraryUrl);
    const {capturePaneInteraction}=await import(stateUrl);
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
    return results;
  },{libraryUrl,stateUrl,expandedPane,collapsedPane});
  assert.ok(result.every(Boolean),JSON.stringify(result));
  console.log("Part-process/profile/assembly local patch keeps canvas, cube, controls/listeners, focus and scrolling; status updates without remount.");
} finally {await browser.close();}
