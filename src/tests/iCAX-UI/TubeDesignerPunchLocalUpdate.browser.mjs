// Component regression: actual DOM reconciliation + WebGL snapshot hydration.
// Native construction is tested separately; these synthetic bytes test identity.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const root = fileURLToPath(new URL("../../", import.meta.url)).replace(/[\\/]$/, "");
const browser = await chromium.launch({ headless:true,...(process.env.ICAX_BROWSER_CHANNEL?{channel:process.env.ICAX_BROWSER_CHANNEL}:{}) });
try {
  const page=await browser.newPage({viewport:{width:1100,height:760}}), errors=[];
  page.on("pageerror", error=>errors.push(error.message));
  await page.route("**/*", route=>{
    const url=new URL(route.request().url());
    if(url.origin!=="http://punch-local.test")return route.abort();
    if(url.pathname==="/")return route.fulfill({contentType:"text/html",body:'<div id="app"><div id="untouched-workbench">Workbench</div></div>'});
    const file=resolve(root,url.pathname.replace(/^\/src\//,""));
    if(!url.pathname.startsWith("/src/")||!file.startsWith(root+sep)||!/\.(mjs|js)$/.test(file))return route.abort();
    return route.fulfill({contentType:"text/javascript",body:readFileSync(file,"utf8")});
  });
  await page.goto("http://punch-local.test");
  const report=await page.evaluate(async()=>{
    const editor=await import("/src/apps/tube-designer/webpage/punchEditor.mjs");
    const wizard=await import("/src/apps/tube-designer/webpage/punchWizard.mjs");
    const dom=await import("/src/apps/tube-designer/webpage/punchDomPatch.mjs");
    const {ThreeRenderViewport}=await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const THREE=await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const {encodeNestingGeometry,encodePreviewMaterial}=await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const check=(value,message)=>{if(!value)throw new Error(message);};
    const wait=async test=>{for(let n=0;n<400&&!test();n++)await new Promise(r=>setTimeout(r,5));check(test(),"display timed out");};
    const mount=document.querySelector("#app"), untouched=document.querySelector("#untouched-workbench");
    const state=wizard.createPunchWizardState({entityId:"local-edit",length:1000});
    Object.assign(state,{catalogueStatus:"ready",revision:1,features:[{id:"A",diameter:10},{id:"B",diameter:10}]});
    const view={activeAreaId:"nesting",scene:{tubeDesigner:{}},tubeDesignerPunchWizard:state};
    const ref=(url,version=1)=>({url,version});
    const recipe=()=>({partEntityId:"local-edit",features:structuredClone(state.features),ends:{}});
    const preview=version=>({toolsOnly:true,baseGeometry:ref("base"),baseMaterial:ref("base-material"),toolMaterial:ref("tool-material"),
      toolPreviews:[{target:"side",key:"A",geometry:ref("tool-A",version)},{target:"side",key:"B",geometry:ref("tool-B")}],
      baseBounds:{min:[0,-40,-40],max:[1000,40,40]},length:1000,revision:version,includesDraft:false});
    state.preview=preview(1);state.previewRecipe=recipe();
    let viewport, mounts=0, hold=false, release;
    const reads=[];
    const originalMount=ThreeRenderViewport.prototype.mount;
    ThreeRenderViewport.prototype.mount=function(...args){viewport=this;mounts++;return originalMount.apply(this,args);};
    const resources={async get(url,options){
      const version=options?.headers?.get("ICAX-Resource-Version");reads.push([url,version]);
      if(hold&&url==="tool-A")await new Promise(r=>release=r);
      if(url.includes("material"))return new Response(encodePreviewMaterial(0x61a8bbff));
      const box=new THREE.BoxGeometry(url==="base"?1000:Number(version)*10,30,30).toNonIndexed();
      box.translate(url==="tool-B"?650:500,0,url==="base"?0:25);
      const bytes=encodeNestingGeometry({positions:[...box.attributes.position.array],indices:Array.from({length:box.attributes.position.count},(_,i)=>i)});
      box.dispose();return new Response(bytes);
    }};
    const context={mount,sceneProxy:{resources}};
    let text="ten", groups=["g1","g2"];
    const html=()=>'<div class="tube-designer-punch-backdrop"><div class="tube-designer-punch-dialog">'
      +'<div data-tube-designer-punch-viewport style="position:relative;width:1000px;height:450px"></div>'
      +'<div class="tube-designer-punch-sheet-scroll" style="height:60px;overflow:auto"><div style="height:300px">'
      +'<input data-tube-designer-punch-field="name" value="'+text+'">'
      +groups.map(id=>'<section data-array-group-id="'+id+'"><input data-tube-designer-punch-array-group="'+id+'" data-tube-designer-punch-array-field="count" value="2"></section>').join("")
      +'</div></div></div></div>';
    const ops={renderProject(){if(!dom.patchPunchDom(view,mount,html()))mount.insertAdjacentHTML("beforeend",html());editor.attachPunchEditor(context,view,mount,ops);}};
    ops.renderProject();await wait(()=>!view.pending&&!!viewport);
    const host=mount.querySelector("[data-tube-designer-punch-viewport]"),canvas=viewport.renderer.domElement;
    const base=viewport.sceneObjects.get("punch-preview-blank"), other=viewport.sceneObjects.get("punch-preview-tool:side:B");
    const baseGeometry=viewport.geometryObjects.get("base"),otherGeometry=viewport.geometryObjects.get("tool-B");
    const initialCamera=viewport.getCameraState();
    check(initialCamera.projectionMode==="orthographic"&&Math.abs(initialCamera.theta+Math.PI/2)<1e-9&&Math.abs(initialCamera.phi-Math.PI/2)<1e-9,"Default camera is not exact orthographic side view");
    const xs=[];
    const position=base.geometry.getAttribute("position");
    for(let i=0;i<position.count;i++)xs.push(new THREE.Vector3(position.getX(i),position.getY(i),position.getZ(i)).applyMatrix4(base.matrix).project(viewport.camera).x);
    const initialWidthFraction=(Math.max(...xs)-Math.min(...xs))/2;
    check(Math.abs(initialWidthFraction-0.9)<1e-6,"Default stock does not occupy 90% of canvas width");
    editor.setPunchPreviewView(mount,"iso");
    viewport.setCameraState({...viewport.getCameraState(),radius:viewport.getCameraState().radius*1.4});
    const oldToolGeometry=viewport.geometryObjects.get("tool-A");
    const input=mount.querySelector('[data-tube-designer-punch-field="name"]');
    const second=mount.querySelector('[data-array-group-id="g2"]');
    const scroll=mount.querySelector(".tube-designer-punch-sheet-scroll");
    input.focus();input.setSelectionRange(1,2);scroll.scrollTop=90;
    await new Promise(r=>requestAnimationFrame(r));
    const camera=JSON.stringify(viewport.getCameraState());
    state.features[0].diameter=20;state.revision=2;state.pendingPreviewRecipe=recipe();state.previewPending=true;
    ops.renderProject();
    check(base.visible&&other.visible,"Editing A hid unchanged base/B");
    check(!viewport.sceneObjects.get("punch-preview-tool:side:A").visible,"Editing A kept stale A visible");
    hold=true;state.preview=preview(2);state.previewRecipe=recipe();state.previewPending=false;ops.renderProject();
    await wait(()=>!!release);
    check(base.visible&&other.visible,"Loading A hid unchanged base/B");
    hold=false;release();await wait(()=>!view.pending);
    check(mount.querySelector("[data-tube-designer-punch-viewport]")===host,"Viewport host replaced");
    check(viewport.renderer.domElement===canvas&&mounts===1,"Canvas remounted");
    check(viewport.sceneObjects.get("punch-preview-blank")===base&&viewport.geometryObjects.get("base")===baseGeometry,"Base object/geometry rebuilt");
    check(viewport.sceneObjects.get("punch-preview-tool:side:B")===other&&viewport.geometryObjects.get("tool-B")===otherGeometry,"Other tool object/geometry rebuilt");
    check(viewport.geometryObjects.get("tool-A")!==oldToolGeometry,"Changed tool geometry was not updated");
    check(document.activeElement===input&&input.selectionStart===1&&input.selectionEnd===2,"Active input/caret changed");
    check(scroll.scrollTop===90,"Table scroll changed");
    check(JSON.stringify(viewport.getCameraState())===camera,"Camera changed");
    check(reads.filter(([url])=>url==="base").length===1&&reads.filter(([url])=>url==="tool-B").length===1,"Unchanged geometry was fetched again");
    // Validation can stop a request before pendingPreviewRecipe is replaced.
    // Comparing it to the previous response would incorrectly leave stale A.
    state.previewSourceRecipe={blank:JSON.stringify([state.partId,state.resourceId,state.resourceVersion,state.baseLength,undefined,null]),
      features:structuredClone(state.features),ends:structuredClone(state.ends)};
    state.features[0].diameter=NaN;state.revision=3;
    state.previewComputeError={revision:3,message:"Invalid diameter"};ops.renderProject();
    check(base.visible&&other.visible,"Invalid A edit hid unchanged objects");
    check(!viewport.sceneObjects.get("punch-preview-tool:side:A").visible,"Invalid A edit displayed stale A from the last successful request");
    groups=["g2","g3"];ops.renderProject();await wait(()=>!view.pending);
    check(mount.querySelector('[data-array-group-id="g2"]')===second,"Unchanged keyed group was replaced");
    check(!mount.querySelector('[data-array-group-id="g1"]')&&mount.querySelector('[data-array-group-id="g3"]'),"Keyed insert/delete failed");
    check(document.querySelector("#untouched-workbench")===untouched,"Surrounding workbench replaced");
    const drawingView={...view,tubeDesignerPartDrawing:{}};
    check(dom.patchPunchDom(drawingView,mount,html())===false,"Punch patch accepted independent 3D drawing");
    return {mounts,reads,initialWidthFraction,orthographicSideView:true,unchangedBase:true,unchangedOtherTool:true,retainedInput:true,retainedUserCamera:true,isolatedDrawing:true};
  });
  assert.deepEqual(errors,[]);console.log(JSON.stringify(report));
}finally{await browser.close();}
