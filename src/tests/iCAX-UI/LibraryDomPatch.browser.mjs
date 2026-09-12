import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
const read=path=>readFileSync(new URL(path,import.meta.url),"utf8");
const data=source=>"data:text/javascript;charset=utf-8,"+encodeURIComponent(source);
const patchUrl=data(read("../../apps/tube-designer/webpage/punchDomPatch.mjs"));
const libraryUrl=data(read("../../apps/tube-designer/webpage/libraryDomPatch.mjs").replace('"./punchDomPatch.mjs"',JSON.stringify(patchUrl)));
const stateUrl=data(read("../../apps/_shared/workbench/utils/paneInteractionState.mjs"));
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser=await chromium.launch({headless:true,channel:"msedge"});
try {
  const page=await browser.newPage();
  const result=await page.evaluate(async ({libraryUrl,stateUrl})=>{
    const {patchLibraryDom,rememberLibraryDom}=await import(libraryUrl);
    const {capturePaneInteraction}=await import(stateUrl);
    const results=[];
    for(const area of ["tools","profiles"]) {
      const hud=area==="tools"?"tube-tool-library-hud":"tube-profile-library-preview-hud";
      const left='<div class="list" style="height:1200px"><button data-cam-action="select">item</button></div>';
      const right=value=>'<div style="height:500px"></div><label><input data-parameter="width" value="'+value+'"></label><div style="height:500px"></div>';
      document.body.innerHTML='<main><div class="cam-workbench"><aside class="cam-context-pane" style="height:200px;overflow:auto">'+left+
        '</aside><div class="cam-viewport"><canvas></canvas><div class="cube"></div><div class="'+hud+'">old</div></div>'+
        '<aside class="cam-info-pane" style="height:200px;overflow:auto">'+right("10")+'</aside></div></main>';
      const mount=document.querySelector("main"),view={activeAreaId:area};
      const canvas=mount.querySelector("canvas"),cube=mount.querySelector(".cube");
      const input=mount.querySelector("input"),leftPane=mount.querySelector(".cam-context-pane"),rightPane=mount.querySelector(".cam-info-pane");
      rememberLibraryDom(view,mount,"");
      input.focus({preventScroll:true});input.value="123";input.setSelectionRange(1,2);
      leftPane.scrollTop=280;rightPane.scrollTop=450;
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
        mount.querySelector("."+hud).textContent==="new status" && mount.querySelector(".cam-status.error").textContent==="preview error");
      results.push(!patchLibraryDom({...view,activeAreaId:"components"},mount,{suffix:""}));
      results.push(!patchLibraryDom(view,mount,{suffix:"new dialog"}));
    }
    return results;
  },{libraryUrl,stateUrl});
  assert.ok(result.every(Boolean),JSON.stringify(result));
  console.log("Tools/profiles local patch keeps canvas, cube, controls/listeners, focus and scrolling; status updates without remount.");
} finally {await browser.close();}
