import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
const source=readFileSync(new URL("../../apps/_shared/workbench/utils/paneInteractionState.mjs",import.meta.url),"utf8");
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser=await chromium.launch({headless:true,channel:"msedge"});
try {
  const page=await browser.newPage();
  const results=await page.evaluate(async source=>{
    const {capturePaneInteraction}=await import("data:text/javascript;charset=utf-8,"+encodeURIComponent(source));
    document.body.innerHTML='<main></main>';
    const mount=document.querySelector("main");
    const html=extra=>`<aside class="cam-context-pane" style="height:180px;overflow:auto"><div style="height:1000px">left</div></aside>
      <aside class="cam-info-pane" style="height:200px;overflow:auto"><div class="nested" style="height:150px;overflow:auto">
      <div style="height:400px"></div>${extra}<input data-field="length" value="server"/><div style="height:400px"></div></div><div style="height:700px"></div></aside>`;
    mount.innerHTML=html("");
    const set=()=>{
      mount.querySelector("input").focus({preventScroll:true});
      mount.querySelector("input").value="12345";
      mount.querySelector("input").setSelectionRange(1,4,"backward");
      mount.querySelector(".cam-context-pane").scrollTop=230;
      mount.querySelector(".cam-info-pane").scrollTop=85;
      mount.querySelector(".nested").scrollTop=380;
    };
    set();
    // Simulate a pending response; user changes position while it is pending.
    const refresh=new Promise(resolve=>setTimeout(()=>{
      const restore=capturePaneInteraction(mount);
      mount.innerHTML=html('<p>new conditional field</p>');
      restore();resolve();
    },20));
    mount.querySelector(".cam-context-pane").scrollTop=310;
    await refresh;
    const input=mount.querySelector("input");
    const result={left:mount.querySelector(".cam-context-pane").scrollTop,
      right:mount.querySelector(".cam-info-pane").scrollTop,
      nested:mount.querySelector(".nested").scrollTop,
      focus:document.activeElement===input,value:input.value,
      selection:[input.selectionStart,input.selectionEnd,input.selectionDirection]};
    const restore=capturePaneInteraction(mount);
    mount.innerHTML=html("").replace('data-field="length"','data-field="different"');
    restore();
    result.noWrongFocus=document.activeElement!==mount.querySelector("input");
    return result;
  },source);
  assert.deepEqual(results,{left:310,right:85,nested:380,focus:true,value:"12345",
    selection:[1,4,"backward"],noWrongFocus:true});
  console.log("Pane refresh preserves both scroll positions, nested scroll, focus, text and selection; latest async state wins.");
} finally {await browser.close();}
