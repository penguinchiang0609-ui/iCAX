// Real dedicated viewport / local DOM regression; native cutter shapes are tested in C++.
import assert from "node:assert/strict";
import {readFileSync,readdirSync,mkdirSync} from "node:fs";
import {resolve,sep} from "node:path";
import {fileURLToPath} from "node:url";
import {tubeDesignerCss} from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||"playwright");
const root=fileURLToPath(new URL("../../",import.meta.url));
const toolsRoot=new URL("../../apps/tube-designer/templates/mold/",import.meta.url);
const tools=readdirSync(toolsRoot,{withFileTypes:true}).filter(d=>d.isDirectory()).map(d=>JSON.parse(readFileSync(new URL(d.name+"/tool.json",toolsRoot)))).map(t=>({...t,digest:"fixture",defaultParameters:Object.fromEntries(t.parameters.map(p=>[p.key,p.defaultValue]))}));
const browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||"msedge"});
try {
 const page=await browser.newPage(),errors=[];page.on("pageerror",e=>errors.push(e.message));page.setDefaultTimeout(15000);
 await page.route("http://drawing.test/**",route=>{
  const path=new URL(route.request().url()).pathname;
  if(path==="/")return route.fulfill({contentType:"text/html",body:"<!doctype html><body><div id='app'></div></body>"});
  const file=resolve(root,path.replace(/^\/src\//,""));
  if(!path.startsWith("/src/")||!file.startsWith(root.replace(/[\\/]$/,"")+sep)||!(/\.(mjs|js)$/.test(file)))return route.abort();
  return route.fulfill({contentType:"text/javascript",body:readFileSync(file,"utf8")});
 });
 await page.goto("http://drawing.test/");
 const common=readFileSync(new URL("../../apps/_shared/workbench/styles/laser3dcam.css",import.meta.url),"utf8");
 await page.addStyleTag({content:"*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,sans-serif}"+common+tubeDesignerCss});
 await page.evaluate(async tools=>{
  const drawing=await import("/src/apps/tube-designer/webpage/partDrawing.mjs");
  const preview=await import("/src/apps/tube-designer/webpage/partDrawingPreview.mjs");
  const dom=await import("/src/apps/tube-designer/webpage/partDrawingDom.mjs");
  const THREE=await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
  const {createThreeViewport}=await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
  const {encodeNestingGeometry,encodePreviewMaterial}=await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
  const profile={schema:"icax.imported-tube-profile",schemaVersion:1,kind:"imported-dxf",name:"圆管",width:40,depth:40,specification:"Φ40 × 2",contours:[{kind:"circle",radius:20,center:[0,0]},{kind:"circle",radius:18,center:[0,0]}]};
  const view={activeAreaId:"nesting",pending:false,tubeDesignerSystemProfiles:[{id:"round",name:"圆管",previewProfile:profile,defaultParameters:{diameter:40},descriptor:{parameters:[{key:"diameter",displayName:"直径",valueType:"number",defaultValue:40}]}}]};
  const resources=new Map(),resourceCache=new Map(),reads=new Map(),calls=[];
  let sequence=0,fullRenders=0,localPatches=0,viewport;
  const put=(kind,key,build)=>{const k=kind+":"+key;if(resourceCache.has(k))return resourceCache.get(k);const ref={url:"fixture://"+kind+"/"+(++sequence),version:1};resources.set(ref.url,build());resourceCache.set(k,ref);return ref;};
  const encode=g=>{try{return encodeNestingGeometry({positions:Array.from(g.attributes.position.array),indices:g.index?Array.from(g.index.array):Array.from({length:g.attributes.position.count},(_,i)=>i)});}finally{g.dispose();}};
  const baseMaterial=put("blank-material","fixed",()=>encodePreviewMaterial(0xabc4cebb));
  const toolMaterial=put("tool-material","fixed",()=>encodePreviewMaterial(0xeeaa4499));
  const context={mount:document.querySelector("#app"),productProxy:{async invoke(){return {profile:structuredClone(profile)};}},sceneProxy:{
   async invoke(method,payload){
    calls.push(structuredClone({method,payload}));
    if(method==="TubeDesigner.GetPartDrawingTools")return {tools};
    if(method!=="TubeDesigner.PreviewPartDrawing"||payload.toolsOnly!==true)throw new Error("Unexpected drawing API/mode: "+method);
    const main=put("main",JSON.stringify(payload.drawing),()=>{
     const shape=new THREE.Shape();shape.absarc(0,0,20,0,Math.PI*2,false);
     const hole=new THREE.Path();hole.absarc(0,0,18,0,Math.PI*2,true);shape.holes.push(hole);
     const tube=new THREE.ExtrudeGeometry(shape,{depth:payload.drawing.length,steps:1,bevelEnabled:false,curveSegments:32});tube.rotateY(Math.PI/2);return encode(tube);
    });
    const toolPreviews=(payload.features??[]).filter(f=>f.enabled!==false).map(f=>({target:"feature",key:f.id,geometry:put("tool",JSON.stringify(f),()=>{
     let cutter;
     if(f.toolRef?.id==="v-notch") {
      const p=f.toolParameters??{},shape=new THREE.Shape();
      const left=p.style==="asymmetric_v"?40*Math.tan(p.leftAngle*Math.PI/180):40,right=p.style==="asymmetric_v"?40*Math.tan(p.rightAngle*Math.PI/180):40;
      shape.moveTo(-left,21);shape.lineTo(p.style==="flat_v"?-p.flatWidth/2:0,-19);
      if(p.style==="flat_v")shape.lineTo(p.flatWidth/2,-19);
      shape.lineTo(right,21);shape.closePath();
      cutter=new THREE.ExtrudeGeometry(shape,{depth:44,steps:1,bevelEnabled:false});cutter.rotateX(Math.PI/2);cutter.translate(f.station??250,22,0);
     } else {cutter=new THREE.CylinderGeometry(20,20,100,32);cutter.rotateX(Math.PI/2);cutter.translate(f.station??250,0,0);}
     return encode(cutter);
    })}));
    return {toolsOnly:true,previewComputed:true,baseGeometry:main,baseMaterial,toolMaterial,toolPreviews,baseBounds:{min:[0,-20,-20],max:[payload.drawing.length,20,20]},length:payload.drawing.length};
   },resources:{async get(url){reads.set(url,(reads.get(url)??0)+1);return new Response(resources.get(url),{status:resources.has(url)?200:404});}}
  }};
  const ops={createPartDrawingViewport(options){return viewport=createThreeViewport(options);},renderProject(){
   const html=drawing.renderPartDrawingDialog(view);
   if(dom.patchPartDrawingDom(view,context.mount,html))localPatches++;
   else{fullRenders++;context.mount.innerHTML="<main id='main-workbench'>既有主工作台</main>"+html;dom.rememberPartDrawingDom(view,context.mount);}
   preview.attachPartDrawingPreview(context,view,context.mount,ops);drawing.attachPartDrawingEditor(context,view,context.mount,ops);
  }};
  drawing.openPartDrawing(view);
  window.fixture={view,drawing,ops,calls,reads,get state(){return view.tubeDesignerPartDrawing.state;},get fullRenders(){return fullRenders;},get localPatches(){return localPatches;},get viewport(){return viewport;}};
  document.addEventListener("change",async e=>{await drawing.handlePartDrawingAction(context,view,e.target.dataset.camChangeAction??"",e.target,ops);});
  document.addEventListener("click",async e=>{const t=e.target.closest("[data-cam-action]");if(t)await drawing.handlePartDrawingAction(context,view,t.dataset.camAction,t,ops);});
  ops.renderProject();
 },tools);
 const command=name=>page.locator('[data-drawing-command="'+name+'"]');
 const act=name=>page.locator('[data-cam-action=tube-designer-drawing-'+name+']');
 const control=key=>page.locator('[data-tube-designer-punch-field="'+key+'"]');
 const parameter=key=>page.locator('[data-tube-designer-punch-parameter="'+key+'"]');
 const ready=async()=>{
  try{await page.waitForFunction(()=>{const {state,view}=window.fixture;return state.preview?.revision===state.revision&&!view.pending&&!state.previewPending&&!state.previewRenderPending;});await page.locator('[data-part-drawing-preview-ready=true] .icax-three-viewport-canvas').waitFor();}
  catch(error){throw new Error(error.message+"\nDrawing state: "+await page.evaluate(()=>JSON.stringify({pending:window.fixture.view.pending,state:window.fixture.state,alerts:[...document.querySelectorAll('[role=alert]')].map(e=>e.textContent)})),{cause:error});}
 };
 const screenshot=async name=>{if(process.env.ICAX_ARTIFACT_DIR){mkdirSync(process.env.ICAX_ARTIFACT_DIR,{recursive:true});await page.screenshot({path:resolve(process.env.ICAX_ARTIFACT_DIR,name+".png")});}};
 const checkIdentity=async()=>assert.deepEqual(await page.evaluate(()=>({canvas:window.fixture.canvas===document.querySelector(".icax-three-viewport-canvas"),workbench:window.fixture.workbench===document.querySelector("#main-workbench"),baseGeometry:window.fixture.baseGeometry===window.fixture.viewport.geometryObjects.get(window.fixture.baseUrl),baseObject:window.fixture.baseObject===window.fixture.viewport.sceneObjects.get("drawing:base"),camera:JSON.stringify(window.fixture.camera)===JSON.stringify(window.fixture.viewport.getCameraState()),fullRenders:window.fixture.fullRenders})),{canvas:true,workbench:true,baseGeometry:true,baseObject:true,camera:true,fullRenders:1});
 await ready();assert.equal(await command("branch").isEnabled(),true);assert.equal(await act("commit-operation").count(),0);assert.equal(await act("cancel-operation").count(),0);
 await page.evaluate(()=>{const f=window.fixture;f.canvas=document.querySelector(".icax-three-viewport-canvas");f.workbench=document.querySelector("#main-workbench");f.baseUrl=f.state.preview.baseGeometry.url;f.baseGeometry=f.viewport.geometryObjects.get(f.baseUrl);f.baseObject=f.viewport.sceneObjects.get("drawing:base");f.camera=f.viewport.getCameraState();});
 for(const size of [{width:1600,height:1000},{width:1024,height:768},{width:780,height:820}]) {
  await page.setViewportSize(size);await command("branch").click();await ready();
  // A deliberate fit after resizing is allowed; subsequent field edits must not move the camera.
  await page.locator("[data-part-drawing-preview-controls] button").click();
  await page.evaluate(()=>window.fixture.camera=window.fixture.viewport.getCameraState());
  assert.equal(await page.locator('[data-cam-change-action=tube-designer-drawing-section-select][data-drawing-section=branch]').inputValue(),"system:round","A new branch has a usable default section");
  assert.ok(await page.evaluate(()=>window.fixture.state.draft.section?.profile?.contours?.length)>0);
  await control("station").fill("180");await control("station").press("Tab");await page.waitForFunction(()=>window.fixture.calls.at(-1)?.payload.features.at(-1)?.station===180);await ready();
  assert.equal(await page.evaluate(()=>window.fixture.state.features.length),1,"Selecting a tool immediately updates the live feature tree");await checkIdentity();
  const dims=await page.evaluate(()=>{
   const rect=s=>{const r=document.querySelector(s).getBoundingClientRect();return {left:r.left,right:r.right,top:r.top,bottom:r.bottom,width:r.width,height:r.height};};
   return {dialog:rect(".td-draw-workbench"),tree:rect(".td-draw-sidebar"),canvas:rect("[data-part-drawing-viewport]"),parameters:rect(".td-draw-parameters"),overflow:[...document.querySelectorAll(".td-draw-sidebar,.td-draw-body,.td-draw-parameters,.td-draw-inspector,.tube-designer-punch-field-grid")].filter(e=>e.scrollWidth>e.clientWidth+2).map(e=>e.className)};
  });
  assert.deepEqual(dims.overflow,[],JSON.stringify({size,dims}));
  assert.ok(dims.dialog.right<=size.width+1&&dims.dialog.bottom<=size.height+1,JSON.stringify({size,dims}));
  assert.ok(dims.tree.right<=dims.canvas.left+1&&dims.canvas.right<=dims.parameters.left+1,JSON.stringify({size,dims}));
  assert.ok(dims.canvas.width>=280&&dims.canvas.height>size.height*.55,JSON.stringify({size,dims}));
  await page.locator(".td-draw-property-scroll").evaluate(e=>e.scrollTop=0);await screenshot("part-drawing-branch-"+size.width);
  const arrays=page.locator("details").filter({has:page.locator("summary",{hasText:/^阵列$/})});
  if(!await arrays.evaluate(e=>e.open))await arrays.locator("summary").click();
  await control("arraySpacing").fill("60");await control("arraySpacing").press("Tab");await ready();
  assert.equal(await arrays.evaluate(e=>e.open),true,"Local patch preserves expanded groups");
  assert.ok(await page.locator(".td-draw-property-scroll").evaluate(e=>e.scrollTop)>0,"Local patch preserves inspector scroll");
  await control("arrayCount").fill("3");await control("arrayCount").press("Tab");await control("rowCount").fill("2");await control("rowCount").press("Tab");
  for(const mode of ["top","left","round"]) {
   await control("drawingArrayMode").selectOption(mode);await control("rowDirection").selectOption("negative");
   await page.waitForFunction(mode=>{const f=window.fixture.calls.at(-1)?.payload.features.at(-1);return f?.face===mode&&f?.rowPitch<0&&f.arrayCount*f.rowCount===6;},mode);await ready();
   assert.match(await page.locator(".td-draw-inspector").innerText(),mode==="round"?/角度间隔/:/排间距/);
  }
  await control("arrayDirection").selectOption("negative");await control("reference").selectOption("end");await page.waitForFunction(()=>window.fixture.calls.at(-1)?.payload.features.at(-1)?.arrayPitch===60);await ready();
  assert.equal(await control("arrayDirection").inputValue(),"negative");assert.equal(await arrays.evaluate(e=>e.scrollWidth<=e.clientWidth+2),true);
  await checkIdentity();await screenshot("part-drawing-array-"+size.width);
  await control("reference").selectOption("start");await control("drawingArrayMode").selectOption("top");await control("rowCount").fill("1");await control("rowCount").press("Tab");await ready();
  assert.equal(await page.locator('[role=treeitem]').count(),2);
  await page.locator('[role=treeitem]').nth(1).click();assert.equal(await control("station").inputValue(),"180");
  await control("station").fill("210");await control("station").press("Tab");await ready();assert.equal(await page.evaluate(()=>window.fixture.state.features[0].station),210);
  await page.evaluate(()=>{const f=window.fixture;f.keptToolUrl=f.state.preview.toolPreviews[0].geometry.url;f.keptTool=f.viewport.geometryObjects.get(f.keptToolUrl);});
  await command("v-notch").click();await ready();assert.equal(await page.locator('[data-drawing-section=branch]').count(),0);
  assert.equal(await parameter("style").locator("option").count(),7);assert.equal(await parameter("rootRadius").count(),1);assert.equal(await parameter("curveRadius").count(),0);
  for(const style of ["asymmetric_v","rounded_v","left_arc","right_arc","flat_v","relief_v","sharp_v"]) {
   await parameter("style").selectOption(style);await ready();
   assert.equal(await parameter("leftAngle").count(),style==="asymmetric_v"?1:0);assert.equal(await parameter("rightAngle").count(),style==="asymmetric_v"?1:0);
   assert.equal(await parameter("curveRadius").count(),style==="rounded_v"?1:0);assert.equal(await parameter("flatWidth").count(),style==="flat_v"?1:0);assert.equal(await parameter("holeDiameter").count(),style==="relief_v"?1:0);
   if(style==="asymmetric_v")assert.equal(await parameter("angle").count(),0);await checkIdentity();
   assert.equal(await page.evaluate(()=>window.fixture.keptTool===window.fixture.viewport.geometryObjects.get(window.fixture.keptToolUrl)),true,"Updating a V slot keeps the existing branch GPU geometry");
  }
  await parameter("style").selectOption("rounded_v");await parameter("curveRadius").fill("6");await parameter("curveRadius").press("Tab");await ready();
  await parameter("style").selectOption("asymmetric_v");await ready();await page.locator(".td-draw-property-scroll").evaluate(e=>e.scrollTop=0);await screenshot("part-drawing-v-notch-"+size.width);
  await act("selected-remove").click();await ready();
  await page.locator('[role=treeitem]').nth(1).click();await act("selected-remove").click();await ready();assert.equal(await page.locator('[role=treeitem]').count(),1);
  await command("start").click();assert.equal(await page.locator('[data-tube-designer-punch-end=start]').count()>0,true);
  await act("selected-remove").click();await ready();assert.equal(await page.locator('[role=treeitem]').count(),1);
 }
 await checkIdentity();const calls=await page.evaluate(()=>window.fixture.calls);
 assert.ok(calls.some(c=>c.method==="TubeDesigner.GetPartDrawingTools"));assert.ok(calls.filter(c=>c.method==="TubeDesigner.PreviewPartDrawing").every(c=>c.payload.toolsOnly===true));
 assert.ok(calls.every(c=>!["TubeDesigner.GetPunchTools","TubeDesigner.PreviewPunchPart"].includes(c.method)));
 assert.deepEqual(await page.evaluate(()=>[...window.fixture.reads].filter(([url])=>url.startsWith("fixture://main/")).map(([,count])=>count)),[1],"Unchanged main geometry is fetched once through all parameter edits");
 assert.deepEqual(errors,[]);console.log("Dedicated drawing: 3 left-tree / canvas / right-inspector layouts, default main tube, live feature/section edits, 7 V types, stable main resource + canvas + outer workbench passed.");
} finally {await browser.close();}
