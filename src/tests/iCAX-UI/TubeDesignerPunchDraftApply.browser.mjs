// Real production UI/actions/WebGL. Geometry construction and persistence are
// deterministic doubles; no open user project or native service is contacted.
import assert from "node:assert/strict";
import { mkdirSync, readFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";
const { chromium }=await import(process.env.ICAX_PLAYWRIGHT_MODULE||"playwright");
const sourceRoot=fileURLToPath(new URL("../../",import.meta.url)).replace(/[\\/]$/,"");
const artifacts=resolve(process.env.ICAX_ARTIFACT_DIR||"tmp/punch-draft-apply-browser");
const catalogue=["circle","end-convex"].map(id=>JSON.parse(readFileSync(new URL("../../apps/tube-designer/templates/mold/"+id+"/tool.json",import.meta.url))))
  .map(tool=>({...tool,digest:"draft-apply-fixture",defaultParameters:Object.fromEntries(tool.parameters.map(p=>[p.key,p.defaultValue]))}));
mkdirSync(artifacts,{recursive:true});
const browser=await chromium.launch({headless:true,...(process.env.ICAX_BROWSER_CHANNEL?{channel:process.env.ICAX_BROWSER_CHANNEL}:{})});
try{
  const page=await browser.newPage({viewport:{width:1280,height:800}}),errors=[],external=[];
  page.setDefaultTimeout(12000);page.on("pageerror",error=>errors.push(error.message));
  await page.route("**/*",route=>{
    const url=new URL(route.request().url());
    if(url.origin!=="http://punch-draft.test"){external.push(url.href);return route.abort();}
    if(url.pathname==="/")return route.fulfill({contentType:"text/html",body:'<div id="app" class="tube-designer-workspace"></div>'});
    const file=resolve(sourceRoot,url.pathname.replace(/^\/src\//,""));
    if(!url.pathname.startsWith("/src/")||!file.startsWith(sourceRoot+sep)||!/\.(mjs|js)$/.test(file))return route.abort();
    return route.fulfill({contentType:"text/javascript",body:readFileSync(file,"utf8")});
  });
  await page.goto("http://punch-draft.test");
  await page.addStyleTag({content:"*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,Arial,sans-serif}"+readFileSync(new URL("../../apps/_shared/workbench/styles/laser3dcam.css",import.meta.url),"utf8")+tubeDesignerCss});
  await page.evaluate(async tools=>{
    const wizard=await import("/src/apps/tube-designer/webpage/punchWizard.mjs"),parts=await import("/src/apps/tube-designer/webpage/partsArea.mjs");
    const editor=await import("/src/apps/tube-designer/webpage/punchEditor.mjs"),dom=await import("/src/apps/tube-designer/webpage/punchDomPatch.mjs");
    const arrays=await import("/src/apps/tube-designer/webpage/punchArrayGroups.mjs");
    const thumbnails=await import("/src/apps/tube-designer/webpage/partThumbnail.mjs");
    const {renderDesignerOperationOverlay}=await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const THREE=await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const {encodeNestingGeometry,encodePreviewMaterial}=await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const cut=tools.find(tool=>tool.id==="end-convex");
    const end=()=>({type:cut.id,recordKind:"tool",toolRef:{id:cut.id,version:cut.version,digest:cut.digest},toolLabel:cut.displayName,toolParameters:structuredClone(cut.defaultParameters),datum:"long",trim:0,rotation:0});
    const original={entityId:"existing-punch-part",name:"已有零件 · 新增七孔直接应用",length:1000,quantity:1,independentNesting:true,profile:{kind:"rect",width:80,depth:80},
      manufacturingGeometryResourceId:"original-part",manufacturingGeometryResourceVersion:1,
      thumbnailGeometryResourceId:"thumbnail-original",thumbnailGeometryResourceVersion:1,
      properties:{"manufacturing.partKind":"tube","tubeDesigner.punchWizard":{features:[],ends:{start:end(),end:end()},baseLength:1000}}};
    const view={pending:false,activeAreaId:"nesting",tubeDesignerActivePartId:original.entityId,scene:{tubeDesigner:{nestingGroups:[{productEntityId:"standalone",name:"下料零件",parts:[original]}]}}};
    const mount=document.querySelector("#app"),buffers=new Map();
    const f=window.fixture={wizard,parts,editor,arrays,view,mount,original,pending:0,errors:[],requests:[],applies:[],refreshes:0,reads:[]};
    const box=(x,length=12,height=140)=>{const g=new THREE.BoxGeometry(length,12,height).toNonIndexed();g.translate(x,0,20);const values=[...g.attributes.position.array];g.dispose();return values;};
    const geometry=(url,positions)=>{buffers.set(url,encodeNestingGeometry({positions,indices:Array.from({length:positions.length/3},(_,i)=>i)}));return {url,version:1};};
    const base=new THREE.BoxGeometry(1000,80,80).toNonIndexed();base.translate(500,0,0);geometry("base",[...base.attributes.position.array]);geometry("thumbnail-original",[...base.attributes.position.array]);base.dispose();
    buffers.set("base-material",encodePreviewMaterial(0x66acc380));buffers.set("tool-material",encodePreviewMaterial(0xffab4088));
    const context=f.context={mount,actions:{async refreshActiveSceneState(){f.refreshes++;f.ops.renderProject();}},sceneProxy:{
      resources:{async get(url,options){f.reads.push({url,version:options?.headers?.get("ICAX-Resource-Version")??"0"});const bytes=buffers.get(url);return new Response(bytes??"missing",{status:bytes?200:404});}},
      async invoke(method,payload){
        if(method==="TubeDesigner.GetPunchTools")return {tools};
        if(method==="TubeDesigner.ApplyPunchWizard"){
          f.applies.push(structuredClone(payload));
          const current=parts.listNestingParts(view.scene.tubeDesigner).find(part=>part.entityId===payload.partEntityId);
          const version=current.manufacturingGeometryResourceVersion+1,thumbnailUrl="thumbnail-saved-"+version;
          geometry(thumbnailUrl,box(500,1000,30+payload.features.length*20));
          const saved={...structuredClone(current),manufacturingGeometryResourceId:"saved-part-"+version,manufacturingGeometryResourceVersion:version,
            thumbnailGeometryResourceId:thumbnailUrl,thumbnailGeometryResourceVersion:version,
            properties:{...structuredClone(current.properties),"tubeDesigner.punchWizard":{features:structuredClone(payload.features),ends:structuredClone(payload.ends),baseLength:1000}}};
          return {tubeDesigner:{...view.scene.tubeDesigner,nestingGroups:[{productEntityId:"standalone",name:"下料零件",parts:[saved]}]}};
        }
        if(method!=="TubeDesigner.PreviewPunchWizard"||payload.toolsOnly!==true)throw new Error("Unexpected service request: "+method);
        f.requests.push(structuredClone(payload));let count=0;
        const toolPreviews=payload.features.filter(item=>item.enabled!==false).map(item=>{
          const n=arrays.punchArrayGroupInstanceCount(item,1000);count+=n;
          return {target:"side",key:item.id,geometry:geometry("tools-"+f.requests.length+"-"+item.id,Array.from({length:n},(_,i)=>box(100+i*100)).flat())};
        });
        for(const key of ["start","end"])if(payload.ends?.[key]?.type!=="keep"){
          count++;toolPreviews.push({target:"end",key,geometry:geometry("end-"+key,box(key==="start"?0:1000,18,120))});
        }
        return {toolsOnly:true,baseGeometry:{url:"base",version:1},baseMaterial:{url:"base-material",version:1},toolMaterial:{url:"tool-material",version:1},toolPreviews,
          length:1000,baseBounds:{min:[0,-40,-40],max:[1000,40,40]},placedToolCount:count,toolCount:count};
      }
    }};
    const ops=f.ops={renderProject(){
      const html=parts.renderPunchWizardDialog(context,view)||'<section data-returned-parts>'+parts.renderNestingLeftPane(context,view)+'</section><button data-cam-action="tube-designer-punch-open" data-tube-designer-part-id="'+original.entityId+'">再次编辑已有零件</button>';
      const output=html+renderDesignerOperationOverlay(context,view);
      if(!dom.patchPunchDom(view,mount,output)){mount.innerHTML=output;dom.rememberPunchDom(view,mount);}editor.attachPunchEditor(context,view,mount,ops);
      if(!view.tubeDesignerPunchWizard)thumbnails.scheduleDesignerPartThumbnailHydration(context);
    }};
    f.dispatch=async(action,target={})=>{f.pending++;try{await parts.handlePartsAreaAction(context,view,action,target,ops);}catch(error){f.errors.push(error.message);}finally{f.pending--;}};
    document.addEventListener("change",event=>{if(event.target.dataset.camChangeAction)void f.dispatch(event.target.dataset.camChangeAction,event.target);});
    document.addEventListener("click",event=>{const target=event.target.closest("button[data-cam-action]");if(target&&!target.disabled)void f.dispatch(target.dataset.camAction,target);});
    ops.renderProject();
  },catalogue);
  const idle=()=>page.waitForFunction(()=>!window.fixture.pending&&!window.fixture.view.pending&&!window.fixture.view.tubeDesignerPunchWizard?.uiPendingClick);
  const ready=()=>page.waitForFunction(()=>document.querySelector('[data-tube-designer-punch-viewport]')?.dataset.punchPreviewReady==="true"&&!window.fixture.view.pending);
  const popup=page.locator('[data-punch-parameter-dialog]'),draft=page.locator('[data-tube-designer-punch-row="draft"]');
  const thumbnail=page.locator('[data-tube-designer-part-thumbnail]');
  const readyThumbnail=version=>page.waitForFunction(expected=>{const canvas=document.querySelector('[data-tube-designer-part-thumbnail]');return canvas?.dataset.tubeThumbnailSource==="resource"&&canvas.dataset.tubePreviewVersion===String(expected);},version);
  await readyThumbnail(1);const originalThumbnail=await thumbnail.evaluate(canvas=>canvas.toDataURL());
  await page.getByRole("button",{name:"再次编辑已有零件",exact:true}).click();
  await idle();await ready();
  await draft.getByRole("button",{name:"编辑形状",exact:true}).click();await idle();
  await popup.locator('[data-tube-designer-punch-field="tool"]').selectOption("circle");await idle();
  await popup.locator('[data-cam-action$="parameters-cancel"]').click();await idle();
  await draft.getByRole("button",{name:"编辑阵列",exact:true}).click();await idle();
  const count=popup.locator('[data-tube-designer-punch-array-field="count"]').first();
  await count.fill("7");await count.press("Tab");await idle();
  const spacing=popup.locator('[data-tube-designer-punch-array-field="spacing"]').first();
  await spacing.fill("100");await spacing.press("Tab");await idle();
  await popup.locator('[data-cam-action$="parameters-cancel"]').click();await idle();await ready();
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features.length),0,"The array is initially in the uncommitted new row");
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.preview.placedToolCount),9,"Seven pending hole tools and two saved end tools are visible");
  await page.screenshot({path:resolve(artifacts,"01-seven-pending-plus-two-ends.png")});
  // Auto-committing an unadded draft is not the current product contract.
  // Explicitly add the configured row, as in the user's failing workflow.
  await draft.getByRole("button",{name:"＋ 添加行",exact:true}).click();await idle();await ready();
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features.length),1);
  await page.locator('.tube-designer-punch-footer [data-cam-action="tube-designer-punch-apply"]').click();await idle();
  const result=await page.evaluate(()=>{const f=window.fixture;return {request:f.applies[0],calls:f.applies.length,part:f.parts.listNestingParts(f.view.scene.tubeDesigner)[0],open:!!f.view.tubeDesignerPunchWizard,refreshes:f.refreshes,errors:f.errors};});
  console.log("Direct Apply request diagnostic: "+JSON.stringify({calls:result.calls,features:result.request?.features?.length,returnedFeatures:result.part.properties["tubeDesigner.punchWizard"].features.length,open:result.open,errors:result.errors}));
  assert.equal(result.calls,1);assert.equal(result.request.features.length,1,"Final Apply must submit the row that was explicitly added");
  assert.equal(result.request.features[0].arrayGroups[0].count,7);assert.equal(result.request.features[0].arrayGroups[0].spacing,100);
  assert.equal(result.open,false);assert.equal(result.refreshes,1);assert.equal(result.part.manufacturingGeometryResourceVersion,2);
  assert.equal(result.part.manufacturingGeometryResourceId,"saved-part-2");
  assert.equal(result.part.properties["tubeDesigner.punchWizard"].features.length,1);assert.deepEqual(result.part.properties["tubeDesigner.punchWizard"].features,result.request.features);
  assert.match(await page.locator('[data-returned-parts]').textContent(),/已有零件 · 新增七孔直接应用/);
  await readyThumbnail(2);
  assert.equal(await thumbnail.getAttribute("data-tube-preview-url"),"thumbnail-saved-2");
  assert.notEqual(await thumbnail.evaluate(canvas=>canvas.toDataURL()),originalThumbnail,"The returned part's new thumbnail resource must redraw, not retain the old canvas pixels");
  assert.equal(await page.evaluate(()=>window.fixture.reads.some(read=>read.url==="thumbnail-saved-2"&&read.version==="2")),true,"Thumbnail hydration must request the returned URL and version");
  await page.screenshot({path:resolve(artifacts,"02-returned-existing-part.png")});
  await page.getByRole("button",{name:"再次编辑已有零件",exact:true}).click();await idle();await ready();
  assert.equal(await page.locator('[data-tube-designer-punch-row="0"]').count(),1);
  assert.match(await page.locator('[data-tube-designer-punch-row="0"] [data-punch-array-summary]').textContent(),/7 个/);
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features[0].arrayGroups[0].count),7);
  await page.screenshot({path:resolve(artifacts,"03-reopened-seven-saved-holes.png")});
  // The user's additional report also concerns rows that were explicitly added.
  // Add another independent three-hole row, then verify a second real Apply,
  // returned resource replacement and a fresh reopen with both stored rows.
  await draft.getByRole("button",{name:"编辑形状",exact:true}).click();await idle();
  await popup.locator('[data-tube-designer-punch-field="tool"]').selectOption("circle");await idle();
  await popup.locator('[data-cam-action$="parameters-cancel"]').click();await idle();
  await draft.getByRole("button",{name:"编辑阵列",exact:true}).click();await idle();
  await count.fill("3");await count.press("Tab");await idle();await spacing.fill("80");await spacing.press("Tab");await idle();
  await popup.locator('[data-cam-action$="parameters-cancel"]').click();await idle();
  await draft.getByRole("button",{name:"＋ 添加行",exact:true}).click();await idle();await ready();
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features.length),2);
  assert.equal(await page.locator('.tube-designer-punch-footer [data-cam-action="tube-designer-punch-apply"]').isEnabled(),true,"Completed previews unlock Apply for already added records");
  await page.locator('.tube-designer-punch-footer [data-cam-action="tube-designer-punch-apply"]').click();await idle();await readyThumbnail(3);
  assert.equal(await page.evaluate(()=>window.fixture.applies.length),2,"Already-added records trigger an actual final Apply request");
  assert.deepEqual(await page.evaluate(()=>window.fixture.applies[1].features.map(item=>item.arrayGroups[0].count)),[7,3]);
  assert.equal(await thumbnail.getAttribute("data-tube-preview-url"),"thumbnail-saved-3");
  assert.equal(await page.evaluate(()=>window.fixture.reads.some(read=>read.url==="thumbnail-saved-3"&&read.version==="3")),true);
  assert.equal(await page.evaluate(()=>window.fixture.parts.listNestingParts(window.fixture.view.scene.tubeDesigner)[0].manufacturingGeometryResourceId),"saved-part-3");
  await page.getByRole("button",{name:"再次编辑已有零件",exact:true}).click();await idle();await ready();
  assert.deepEqual(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features.map(item=>item.arrayGroups[0].count)),[7,3]);
  await page.screenshot({path:resolve(artifacts,"04-reopened-two-explicitly-saved-rows.png")});
  assert.deepEqual(result.errors,[]);assert.deepEqual(errors,[]);assert.deepEqual(external,[]);
  console.log("Apply passed: explicitly added rows persist once; returned geometry/thumbnail URLs and versioned bytes replace the list entry; fresh reopens restore seven then seven-plus-three holes.");
}catch(error){const page=browser.contexts()[0]?.pages()[0];console.log("Failure state: "+JSON.stringify(await page?.evaluate(()=>({canvases:[...document.querySelectorAll('[data-tube-designer-part-thumbnail]')].map(c=>({...c.dataset})),reads:window.fixture?.reads,errors:window.fixture?.errors})).catch(()=>null)));await page?.screenshot({path:resolve(artifacts,"failure.png")}).catch(()=>{});throw error;}
finally{await browser.close();}
