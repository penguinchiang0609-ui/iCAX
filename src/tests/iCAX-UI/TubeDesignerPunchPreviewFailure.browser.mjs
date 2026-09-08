// Production handlers, DOM patch and WebGL; deterministic construction/resources.
// No intermediate boolean is simulated. Only final Apply can reject a cut.
import assert from "node:assert/strict";
import { mkdirSync, readFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const root = fileURLToPath(new URL("../../", import.meta.url)).replace(/[\\/]$/, "");
const artifacts = resolve(process.env.ICAX_ARTIFACT_DIR || "tmp/punch-preview-failure-browser");
mkdirSync(artifacts, { recursive: true });
const browser = await chromium.launch({ headless:true,...(process.env.ICAX_BROWSER_CHANNEL?{channel:process.env.ICAX_BROWSER_CHANNEL}:{}) });
try {
  const page=await browser.newPage({viewport:{width:1280,height:800}}),pageErrors=[],externalRequests=[];
  page.setDefaultTimeout(10000);page.on("pageerror",error=>pageErrors.push(error.message));
  await page.route("**/*",route=>{
    const url=new URL(route.request().url());
    if(url.origin!=="http://punch-failure.test"){externalRequests.push(url.href);return route.abort();}
    if(url.pathname==="/")return route.fulfill({contentType:"text/html",body:'<div id="app" class="tube-designer-workspace"></div>'});
    const file=resolve(root,url.pathname.replace(/^\/src\//,""));
    if(!url.pathname.startsWith("/src/")||!file.startsWith(root+sep)||!/\.(mjs|js)$/.test(file))return route.abort();
    return route.fulfill({contentType:"text/javascript",body:readFileSync(file,"utf8")});
  });
  await page.goto("http://punch-failure.test");
  await page.addStyleTag({content:"*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,Arial,sans-serif}"+
    readFileSync(new URL("../../apps/_shared/workbench/styles/laser3dcam.css",import.meta.url),"utf8")+tubeDesignerCss});
  await page.evaluate(async()=>{
    const wizard=await import("/src/apps/tube-designer/webpage/punchWizard.mjs");
    const editor=await import("/src/apps/tube-designer/webpage/punchEditor.mjs");
    const parts=await import("/src/apps/tube-designer/webpage/partsArea.mjs");
    const dom=await import("/src/apps/tube-designer/webpage/punchDomPatch.mjs");
    const {renderDesignerOperationOverlay}=await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const {ThreeRenderViewport}=await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const THREE=await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const {encodeNestingGeometry,encodePreviewMaterial}=await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const part={entityId:"failure-acceptance",length:1000,independentNesting:true,name:"工具显示失败验收",
      profile:{kind:"rect",width:80,depth:80},properties:{"manufacturing.partKind":"tube"}};
    const state=wizard.createPunchWizardState(part);state.catalogueStatus="ready";
    state.features=[200,700].map((station,i)=>wizard.normalizePunchFeature({id:i?"B":"A",recordKind:"tool",type:"circle",face:"top",diameter:10,
      layoutDatum:"base",reference:"start",station,arrayCount:1,arrayPitch:50}));
    state.draft=wizard.normalizePunchFeature({recordKind:"tool",type:"circle",enabled:false,station:0});
    const view={pending:false,tubeDesignerPunchWizard:state,activeAreaId:"nesting",scene:{tubeDesigner:{nestingGroups:[{parts:[part]}]}}};
    const mount=document.querySelector("#app"),buffers=new Map();
    const f=window.fixture={state,view,part,scenario:"success",resourceFailure:false,pending:0,requests:[],applies:[],errors:[],reads:[],holds:[],holdNative:false,mounts:0};
    const originalMount=ThreeRenderViewport.prototype.mount;
    ThreeRenderViewport.prototype.mount=function(...args){f.viewport=this;f.mounts++;return originalMount.apply(this,args);};
    const encodeBox=(length,width,height,x,y,z)=>{const box=new THREE.BoxGeometry(length,width,height).toNonIndexed();box.translate(x,y,z);
      const bytes=encodeNestingGeometry({positions:[...box.attributes.position.array],indices:Array.from({length:box.attributes.position.count},(_,i)=>i)});box.dispose();return bytes;};
    buffers.set("base",encodeBox(1000,80,80,500,0,0));buffers.set("base-material",encodePreviewMaterial(0x66acc380));buffers.set("tool-material",encodePreviewMaterial(0xffab4088));
    const ref=url=>({url,version:1});
    const context=f.context={mount,sceneProxy:{
      resources:{async get(url){f.reads.push(url);if(f.resourceFailure&&url.startsWith("tool-A-"))return new Response("fixture tool resource failure",{status:503});const bytes=buffers.get(url);return new Response(bytes??"missing",{status:bytes?200:404});}},
      async invoke(method,payload){
        if(method==="TubeDesigner.ApplyPunchWizard"){f.applies.push(structuredClone(payload));throw new Error("最终切割产生多个独立实体，请调整刀具后重试。");}
        if(method!=="TubeDesigner.PreviewPunchWizard"||payload.toolsOnly!==true)throw new Error("Unexpected construction API/contract: "+method);
        f.requests.push(structuredClone(payload));if(f.holdNative)await new Promise(resolve=>f.holds.push(resolve));
        if(f.scenario==="throw")throw new Error("当前刀具构造失败：无法生成所选刀具体。");
        const toolPreviews=payload.features.filter(item=>item.enabled!==false).map(item=>{
          const x=item.reference==="center"?500+item.station:item.reference==="end"?1000-item.station:item.station;
          const url="tool-"+item.id+"-"+x+"-"+item.diameter;buffers.set(url,encodeBox(item.diameter,item.diameter,140,x,0,25));return {target:"side",key:item.id,geometry:ref(url)};});
        return {toolsOnly:true,baseGeometry:ref("base"),baseMaterial:ref("base-material"),toolMaterial:ref("tool-material"),toolPreviews,
          baseBounds:{min:[0,-40,-40],max:[1000,40,40]},length:1000,toolCount:toolPreviews.length};
      },
    }};
    const ops=f.ops={renderProject(){const html=wizard.renderPunchWizardDialog(part,view,{tableMode:true,showEnds:false})+renderDesignerOperationOverlay(context,view);
      if(!dom.patchPunchDom(view,mount,html)){mount.innerHTML=html;dom.rememberPunchDom(view,mount);}editor.attachPunchEditor(context,view,mount,ops);}};
    f.dispatch=async(action,target={})=>{f.pending++;try{await parts.handlePartsAreaAction(context,view,action,target,ops);}catch(error){f.errors.push(error.message);}finally{f.pending--;}};
    document.addEventListener("change",event=>{if(event.target.dataset.camChangeAction)void f.dispatch(event.target.dataset.camChangeAction,event.target);});
    document.addEventListener("click",event=>{const button=event.target.closest("button[data-cam-action]");if(button&&!button.disabled)void f.dispatch(button.dataset.camAction,button);});
    f.snapshot=()=>{const host=mount.querySelector("[data-tube-designer-punch-viewport]"),vp=f.viewport;
      const objects=[...(vp?.sceneObjects??[])].map(([id,object])=>{const points=object.geometry?.getAttribute("position"),x=[];
        for(let i=0;i<(points?.count??0);i++)x.push(new THREE.Vector3(points.getX(i),points.getY(i),points.getZ(i)).applyMatrix4(object.matrix).x);
        return {id,url:object.userData?.geometryId,visible:object.visible,x:[...new Set(x)].sort((a,b)=>a-b)};});
      return {objects,ready:host?.dataset.punchPreviewReady,notice:host?.querySelector('[data-punch-preview-notice]')?.textContent??"",error:state.error,
        applyDisabled:mount.querySelector('[data-cam-action="tube-designer-punch-apply"]')?.disabled,preview:state.preview,
        stableBase:vp?.sceneObjects.get("punch-preview-blank")===f.base,stableB:vp?.sceneObjects.get("punch-preview-tool:side:B")===f.other,
        stableCanvas:vp?.renderer.domElement===f.canvas,stableCamera:JSON.stringify(vp?.getCameraState())===f.camera};};
    ops.renderProject();await editor.previewPunch(context,view,part,ops,null,{includeDraft:false});
    f.base=f.viewport.sceneObjects.get("punch-preview-blank");f.other=f.viewport.sceneObjects.get("punch-preview-tool:side:B");f.canvas=f.viewport.renderer.domElement;f.camera=JSON.stringify(f.viewport.getCameraState());
  });
  const idle=()=>page.waitForFunction(()=>!window.fixture.pending&&!window.fixture.view.pending&&!window.fixture.state.uiPendingClick);
  const snap=()=>page.evaluate(()=>window.fixture.snapshot());
  const row=page.locator('[data-tube-designer-punch-row="0"]'),popup=page.locator('[data-punch-parameter-dialog]');
  const station=()=>popup.locator('[data-tube-designer-punch-field="station"]');
  const checkUnchanged=s=>{assert.equal(s.objects.find(o=>o.id==="punch-preview-blank")?.visible,true,"Unchanged main tube remains visible");
    assert.equal(s.objects.find(o=>o.id==="punch-preview-tool:side:B")?.visible,true,"Unrelated tool B remains visible");
    assert.equal(s.stableBase,true);assert.equal(s.stableB,true);assert.equal(s.stableCanvas,true);assert.equal(s.stableCamera,true);};
  await idle();let s=await snap();assert.equal(s.ready,"true");assert.deepEqual(s.objects.find(o=>o.id==="punch-preview-tool:side:A")?.x,[-305,-295]);checkUnchanged(s);
  assert.equal(await page.locator('[data-tube-designer-punch-preview-mode="result"]').count(),0);
  await page.screenshot({path:resolve(artifacts,"01-tools-current.png")});
  await row.getByRole("button",{name:"编辑位置 / 姿态",exact:true}).click();await idle();
  await page.evaluate(()=>{const f=window.fixture;f.scenario="throw";f.holdNative=true;});
  await station().fill("100");await station().press("Tab");await page.waitForFunction(()=>window.fixture.holds.length>0);
  s=await snap();checkUnchanged(s);assert.equal(s.objects.find(o=>o.id==="punch-preview-tool:side:A")?.visible,false,"Pending edit hides A's older location");assert.notEqual(s.ready,"true");
  await page.evaluate(()=>{const f=window.fixture;f.holdNative=false;f.holds.splice(0).forEach(resolve=>resolve());});await idle();
  s=await snap();checkUnchanged(s);assert.equal(s.objects.find(o=>o.id==="punch-preview-tool:side:A")?.visible,false);assert.match(s.error,/刀具构造失败/);assert.match(s.notice,/失败/);
  await page.screenshot({path:resolve(artifacts,"02-construction-failure.png")});
  const beforeInvalid=await page.evaluate(()=>window.fixture.requests.length);
  await station().fill("");await station().press("Tab");await idle();s=await snap();checkUnchanged(s);
  assert.equal(s.objects.find(o=>o.id==="punch-preview-tool:side:A")?.visible,false);assert.notEqual(s.ready,"true");assert.ok(s.error);
  assert.equal(await page.evaluate(()=>window.fixture.requests.length),beforeInvalid,"Invalid numeric input never reaches native construction");
  await page.evaluate(()=>{window.fixture.scenario="success";});
  await station().fill("150");await station().press("Tab");await idle();s=await snap();checkUnchanged(s);assert.equal(s.ready,"true");assert.equal(s.error,"");assert.deepEqual(s.objects.find(o=>o.id==="punch-preview-tool:side:A")?.x,[-355,-345]);
  await page.evaluate(()=>{window.fixture.resourceFailure=true;});
  await station().fill("180");await station().press("Tab");await idle();s=await snap();checkUnchanged(s);assert.match(s.error,/503/);assert.notEqual(s.ready,"true");
  assert.equal(s.objects.find(o=>o.id==="punch-preview-tool:side:A")?.visible,false,"Failed A resources do not expose old or partial A: "+JSON.stringify(s));
  const beforeRetry=await page.evaluate(()=>{window.fixture.resourceFailure=false;return window.fixture.requests.length;});
  // Parameter windows no longer have a second preview action. Close the
  // live-edit window, then use the scene-level refresh to retry failed
  // resources through the normal preview pipeline.
  await popup.locator('[data-cam-action$="parameters-cancel"]').click();await idle();assert.equal(await popup.count(),0);
  await page.locator('[data-cam-action="tube-designer-punch-preview"]').click();await idle();s=await snap();checkUnchanged(s);assert.equal(s.ready,"true");assert.deepEqual(s.objects.find(o=>o.id==="punch-preview-tool:side:A")?.x,[-325,-315]);
  assert.equal(await page.evaluate(()=>window.fixture.requests.length),beforeRetry+1,"Scene refresh retries the failed resource preview");
  await page.screenshot({path:resolve(artifacts,"03-resources-recovered.png")});
  await page.locator('.tube-designer-punch-footer [data-cam-action="tube-designer-punch-apply"]').click();await idle();
  assert.equal(await page.evaluate(()=>window.fixture.applies.length),1,"Final Apply runs only on whole-wizard confirmation");
  assert.equal(await page.evaluate(()=>window.fixture.applies[0].toolsOnly),undefined,"Final manufacturing is not display-only");
  assert.equal(await page.evaluate(()=>!!window.fixture.view.tubeDesignerPunchWizard),true);assert.equal(await page.evaluate(()=>window.fixture.view.pending),false);
  assert.match((await snap()).error,/最终切割产生多个独立实体/);assert.equal(await row.getByRole("button",{name:"编辑位置 / 姿态",exact:true}).isEnabled(),true);
  await page.screenshot({path:resolve(artifacts,"04-final-apply-failure-editable.png")});
  await row.getByRole("button",{name:"编辑位置 / 姿态",exact:true}).click();await idle();await station().fill("190");await station().press("Tab");await idle();
  await popup.locator('[data-cam-action$="parameters-cancel"]').click();await idle();s=await snap();checkUnchanged(s);assert.equal(s.error,"");assert.equal(s.ready,"true");assert.equal(s.applyDisabled,false);
  assert.equal(await page.evaluate(()=>window.fixture.requests.every(r=>r.toolsOnly===true)),true);assert.equal(await page.evaluate(()=>window.fixture.applies.length),1);
  assert.deepEqual(await page.evaluate(()=>window.fixture.errors),["最终切割产生多个独立实体，请调整刀具后重试。"]);assert.deepEqual(pageErrors,[]);assert.deepEqual(externalRequests,[]);
  assert.equal(await page.evaluate(()=>window.fixture.mounts),1);
  console.log("Tool preview failures passed: pending/native-invalid/numeric-invalid A hidden while base/B/camera/canvas persist; resource-only retry; final Apply rejection remains editable.");
  console.log("Screenshots: "+artifacts);
}catch(error){await browser.contexts()[0]?.pages()[0]?.screenshot({path:resolve(artifacts,"failure.png")}).catch(()=>{});throw error;}
finally{await browser.close();}
