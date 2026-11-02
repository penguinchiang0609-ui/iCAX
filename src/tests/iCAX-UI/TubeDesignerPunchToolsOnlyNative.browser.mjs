// Real production punch UI and local DOM patching -> native SDO -> persisted
// geometry. Only workbench navigation/file picker and JSON bridge are adapters.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { mkdirSync, readFileSync, writeFileSync, readdirSync, copyFileSync, existsSync, statSync } from "node:fs";
import { resolve, sep } from "node:path";
import { createHash } from "node:crypto";
import { fileURLToPath } from "node:url";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const root=fileURLToPath(new URL("../../../",import.meta.url)),sourceRoot=resolve(root,"src");
const artifacts=resolve(root,"tmp/punch-tools-only-native-browser"),runtime=resolve(artifacts,"native");
mkdirSync(runtime,{recursive:true});
// Never hold the DLL being relinked by the native build agent.
const sourceDlls=process.env.ICAX_PUNCH_NATIVE_DLL_DIR||resolve(root,"src/x64/Debug");
for(const file of readdirSync(sourceDlls).filter(file=>/\.dll$/i.test(file)))copyFileSync(resolve(sourceDlls,file),resolve(runtime,file));
copyFileSync(process.env.ICAX_PUNCH_NATIVE_BRIDGE||resolve(root,"output/tests/unified-cad-document/native/SideSketchAcceptanceBridge.exe"),resolve(runtime,"PunchAcceptanceBridge.exe"));
const nativeDll={source:resolve(sourceDlls,"TubeDesigner.dll"),bytes:statSync(resolve(runtime,"TubeDesigner.dll")).size,
  modified:statSync(resolve(sourceDlls,"TubeDesigner.dll")).mtime.toISOString(),
  sha256:createHash("sha256").update(readFileSync(resolve(runtime,"TubeDesigner.dll"))).digest("hex")};
const bridge=spawn(resolve(runtime,"PunchAcceptanceBridge.exe"),[],{cwd:root,windowsHide:true,
  env:{...process.env,PATH:runtime+";"+resolve(root,"src/x64/Debug")+";"+process.env.PATH},stdio:["pipe","pipe","pipe"]});
const requests=[],waits=new Map(),cases=[];let nextId=0,stderr="",browser,page;
bridge.stderr.on("data",data=>{stderr+=data;});
const failAll=error=>{for(const wait of waits.values()){clearTimeout(wait.timer);wait.reject(error);}waits.clear();};
bridge.on("error",failAll);bridge.on("exit",code=>{if(waits.size)failAll(new Error("Native bridge exited "+code+"\n"+stderr));});
createInterface({input:bridge.stdout}).on("line",line=>{
  let response;try{response=JSON.parse(line);}catch{failAll(new Error("Invalid bridge output: "+line));return;}
  const wait=waits.get(response.id);if(!wait)return;waits.delete(response.id);clearTimeout(wait.timer);
  if(response.ok)wait.resolve(response.result);else wait.reject(new Error(response.error));
});
const rpc=message=>new Promise((resolveResult,reject)=>{
  const id=++nextId,entry={id,...message,started:new Date().toISOString()};requests.push(entry);
  const timer=setTimeout(()=>{waits.delete(id);reject(new Error("Native timeout: "+message.action+"/"+message.method));},120000);
  waits.set(id,{timer,reject,resolve:result=>{entry.finished=new Date().toISOString();resolveResult(result);}});
  bridge.stdin.write(JSON.stringify({id,...message})+"\n");
});
const invoke=(method,payload={},scope="scene")=>rpc({action:"invoke",scope,method,payload});
try {
  await rpc({action:"reset"});
  const profiles=[];
  const profilesDir=resolve(sourceRoot,"apps/tube-designer/templates/profile");
  for(const directory of readdirSync(profilesDir,{withFileTypes:true}).filter(item=>item.isDirectory())) {
    if(!existsSync(resolve(profilesDir,directory.name,"profile.json")))continue;
    const descriptor=JSON.parse(readFileSync(resolve(profilesDir,directory.name,"profile.json"),"utf8"));
    const defaultParameters=Object.fromEntries(descriptor.parameters.map(p=>[p.key,p.defaultValue]));
    const evaluated=await invoke("EvaluateProfilePackage",{profileRef:{scope:"system",id:directory.name},parameters:defaultParameters},"product");
    profiles.push({id:directory.name,name:typeof descriptor.displayName==="string"?descriptor.displayName:descriptor.displayName["zh-CN"],profileType:"parametric-package",descriptor,defaultParameters,previewProfile:evaluated.profile});
  }
  assert.ok(profiles.some(p=>p.id==="polygon"));
  const scene=await invoke("List"),{chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||"file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs");
  browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||"msedge"});
  page=await browser.newPage({viewport:{width:1600,height:1000}});page.setDefaultTimeout(45000);
  const errors=[],messages=[],externalRequests=[];
  page.on("pageerror",error=>errors.push(error.message));
  page.on("console",message=>{if(["error","warning"].includes(message.type()))messages.push(message.text());});
  await page.exposeFunction("nativeRpc",rpc);
  await page.route("**/*",route=>{
    const url=new URL(route.request().url());
    if(url.origin!=="http://punch-tools-native.test"){externalRequests.push(url.href);return route.abort();}
    if(url.pathname==="/")return route.fulfill({contentType:"text/html",body:"<!doctype html><html lang='zh-CN'><meta charset='utf-8'><body><div id='app' class='tube-designer-workspace'></div></body></html>"});
    const file=resolve(sourceRoot,url.pathname.replace(/^\/src\//,""));
    if(!url.pathname.startsWith("/src/")||!file.startsWith(sourceRoot+sep)||!/\.(mjs|js)$/.test(file))return route.abort();
    return route.fulfill({contentType:"text/javascript",body:readFileSync(file,"utf8")});
  });
  await page.goto("http://punch-tools-native.test/");
  await page.addStyleTag({content:"*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,Arial,sans-serif}"+readFileSync(resolve(sourceRoot,"apps/_shared/workbench/styles/workbench.css"),"utf8")+tubeDesignerCss});
  await page.evaluate(async({profiles,scene,dxfPath})=>{
    const creation=await import("/src/apps/tube-designer/webpage/nestingPunchPart.mjs"),parts=await import("/src/apps/tube-designer/webpage/partsArea.mjs");
    const editor=await import("/src/apps/tube-designer/webpage/punchEditor.mjs"),wizard=await import("/src/apps/tube-designer/webpage/punchWizard.mjs");
    const batch=await import("/src/apps/tube-designer/webpage/punchBatch.mjs");
    const {renderDesignerOperationOverlay}=await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const {patchPunchDom}=await import("/src/apps/tube-designer/webpage/punchDomPatch.mjs");
    const {ThreeRenderViewport}=await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs"),THREE=await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const mount=document.querySelector("#app"),view={pending:false,activeAreaId:"nesting",scene,tubeDesignerSystemProfiles:profiles,
      tubeDesignerSelectedProfileId:"system:round",tubeDesignerUserData:{profiles:[]},tubeDesignerTemplateProfiles:[]};
    const f=window.fixture={view,wizard,batch,pending:0,calls:[],failures:[],resources:[],active:0,maximumActive:0,patches:0};
    const originalMount=ThreeRenderViewport.prototype.mount;
    ThreeRenderViewport.prototype.mount=function(...args){f.viewport=this;return originalMount.apply(this,args);};
    const invoke=async(method,payload,options,scope="scene")=>{
      const call={method,scope,payload:structuredClone(payload)};f.calls.push(call);f.active++;f.maximumActive=Math.max(f.maximumActive,f.active);
      options?.onReport?.({message:"真实原生计算中",completed:0,total:1});
      try{const result=await window.nativeRpc({action:"invoke",scope,method:method.replace(/^TubeDesigner\./,""),payload});call.result=result;return result;}
      catch(error){call.error=error.message;throw error;}finally{f.active--;}
    };
    const resources={async get(url,options){const version=Number(options?.headers?.get("ICAX-Resource-Version")||0);
      const result=await window.nativeRpc({action:"resource",payload:{url,version}});f.resources.push({url,version,bytes:result.bytes});
      return new Response(Uint8Array.from(atob(result.base64),c=>c.charCodeAt(0)));}};
    const context=f.context={mount,sceneProxy:{invoke,resources},productProxy:{invoke:(method,payload,options)=>invoke(method,payload,options,"product")},appProxy:{bridge:{async openFileDialog(){return dxfPath;}}}};
    f.parts=()=>view.scene.tubeDesigner?.nestingGroups?.flatMap(g=>g.parts??[])??[];
    const ops={renderProject(){
      const html=creation.renderNestingPunchPartDialog(view);
      const full=html+renderDesignerOperationOverlay(context,view);
      if(patchPunchDom(view,mount,full))f.patches++;
      else mount.innerHTML='<nav data-fixture-navigation><button data-cam-action="tube-designer-nesting-punch-create-open">新建冲孔件</button></nav>'+full;
      editor.attachPunchEditor(context,view,mount,ops);
    }};
    f.render=ops.renderProject;
    f.dispatch=async(action,target={})=>{if(!action?.startsWith("tube-designer-"))return;f.pending++;
      try{return action.startsWith("tube-designer-nesting-punch-create-")?await creation.handleNestingPunchPartAction(context,view,action,target,ops):await parts.handlePartsAreaAction(context,view,action,target,ops);}
      catch(error){f.failures.push({action,message:error.message});}finally{f.pending--;}};
    document.addEventListener("change",event=>void f.dispatch(event.target.dataset.camChangeAction,event.target));
    document.addEventListener("click",event=>{const target=event.target.closest("[data-cam-action]");if(target&&!target.disabled)void f.dispatch(target.dataset.camAction,target);});
    f.snapshot=()=>{const s=view.tubeDesignerPunchWizard;
      return {preview:s?.preview,error:s?.error,failures:f.failures,patches:f.patches,
        objects:[...f.viewport?.sceneObjects??[]].filter(([,object])=>object.visible).map(([id,object])=>{
          const points=object.geometry?.getAttribute("position"),coordinates=[];
          for(let i=0;i<(points?.count??0);i++)coordinates.push(new THREE.Vector3(points.getX(i),points.getY(i),points.getZ(i)).applyMatrix4(object.matrix).toArray());
          return {id,min:[0,1,2].map(axis=>Math.min(...coordinates.map(p=>p[axis]))),max:[0,1,2].map(axis=>Math.max(...coordinates.map(p=>p[axis]))),
            x:[...new Set(coordinates.map(p=>Math.round(p[0]*1e4)/1e4))].sort((a,b)=>a-b)};
        })};};
    f.render();
  },{profiles,scene,dxfPath:resolve(sourceRoot,"tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/PartDrawingSection.dxf")});
  const idle=async()=>{await page.waitForFunction(()=>{const f=window.fixture,s=f.view.tubeDesignerPunchWizard;return !f.pending&&!f.view.pending&&!s?.previewPending&&!s?.previewRenderPending&&!s?.uiPendingClick;});
    assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard?.previewRenderError??""),"");};
  const ready=async()=>{await idle();await page.waitForFunction(()=>document.querySelector("[data-tube-designer-punch-viewport]")?.dataset.punchPreviewReady==="true");};
  const input=async(locator,value)=>{await locator.fill(String(value));await locator.press("Tab");await ready();};
  const choose=async(locator,value)=>{await locator.selectOption(value);await ready();};
  const action=async suffix=>{await page.locator('[data-cam-action="tube-designer-nesting-punch-create-'+suffix+'"]').first().click();await ready();};
  const definition=()=>page.locator('[data-punch-batch-definition-editor]');
  const partField=name=>page.locator('[data-punch-batch-part-row]').first().locator('[data-punch-batch-field="'+name+'"]');
  const rowField=(name,index=0)=>page.locator('[data-tube-designer-punch-row="'+index+'"] [data-tube-designer-punch-field="'+name+'"]');
  const screenshot=label=>page.screenshot({path:resolve(artifacts,label+".png")});
  const assertToolsOnly=async expected=>{
    const snapshot=await page.evaluate(()=>window.fixture.snapshot());
    assert.equal(snapshot.preview.toolsOnly,true);assert.equal(snapshot.preview.geometry,undefined);assert.equal(snapshot.preview.toolCount,expected);
    assert.equal(snapshot.error,"");assert.ok(snapshot.objects.some(o=>o.id==="punch-preview-blank"));
    assert.ok(!snapshot.objects.some(o=>o.id==="punch-preview"));assert.equal(await page.locator('[data-tube-designer-punch-preview-mode="result"]').count(),0);
    return snapshot;
  };
  const reopen=async(file,id)=>{
    const beforeScene=await invoke("List"),beforePart=beforeScene.tubeDesigner.nestingGroups.flatMap(g=>g.parts??[]).find(p=>p.entityId===id);
    assert.ok(beforePart?.thumbnailGeometryResourceId);
    const document=await invoke("GetTubeCadDocument",{partEntityId:id});
    assert.ok(document.documentResourceId);assert.ok(document.geometry.SolidNodes.length);
    const evaluated=await rpc({action:"cad-document-geometry",payload:{partEntityId:id}});
    assert.equal(evaluated.valid,true);assert.ok(evaluated.directedDifferenceVolume<1e-3);
    const beforeGeometry=await rpc({action:"resource",payload:{url:beforePart.thumbnailGeometryResourceId,version:beforePart.thumbnailGeometryResourceVersion}});
    await rpc({action:"save",payload:{file}});const opened=await rpc({action:"open",payload:{file}});assert.equal(opened.opened,true);
    const afterPart=opened.snapshot.tubeDesigner.nestingGroups.flatMap(g=>g.parts??[]).find(p=>p.entityId===id);
    const afterGeometry=await rpc({action:"resource",payload:{url:afterPart.thumbnailGeometryResourceId,version:afterPart.thumbnailGeometryResourceVersion}});
    assert.equal(afterGeometry.base64,beforeGeometry.base64);
    assert.deepEqual(await invoke("GetTubeCadDocument",{partEntityId:id}),document,"Creation CAD graph survives native save/open");
    await page.evaluate(scene=>{const f=window.fixture;f.view.scene=scene;f.render();},opened.snapshot);
    return {documentResourceId:document.documentResourceId,differenceVolume:evaluated.directedDifferenceVolume};
  };
  const newCreation=async(profile="round")=>{
    await page.getByRole("button",{name:"新建冲孔件",exact:true}).click();await ready();
    await choose(page.locator('[data-tube-designer-nesting-punch-field="profileKey"]'),"system:"+profile);
  };
  const createFinal=async()=>{
    await page.locator('[data-cam-action="tube-designer-nesting-punch-create-apply"]').click();await idle();
    const call=await page.evaluate(()=>window.fixture.calls.findLast(c=>c.method.endsWith("AddNestingPunchPart")));
    assert.ok(call.result?.partEntityId,JSON.stringify(call));assert.equal(call.error,undefined);return call;
  };
  // Current batch cells drive all creation actions. Persisted editing belongs
  // to the shared neutral editor and is covered by the unified CAD suite.
  await newCreation();
  await page.evaluate(()=>{const f=window.fixture;f.metadataProbe={calls:f.calls.length,reads:f.resources.length,
    revision:f.view.tubeDesignerPunchWizard.revision,preview:f.view.tubeDesignerPunchWizard.preview,
    canvas:f.viewport.renderer.domElement,blank:f.viewport.sceneObjects.get("punch-preview-blank")};});
  await input(partField("quantity"),2);await input(partField("name"),"验收-零孔直管");await input(partField("material"),"Q235B");
  await assertToolsOnly(0);
  assert.deepEqual(await page.evaluate(()=>{const f=window.fixture,p=f.metadataProbe;return {calls:f.calls.length-p.calls,reads:f.resources.length-p.reads,
    revision:f.view.tubeDesignerPunchWizard.revision===p.revision,preview:f.view.tubeDesignerPunchWizard.preview===p.preview,
    canvas:f.viewport.renderer.domElement===p.canvas,blank:f.viewport.sceneObjects.get("punch-preview-blank")===p.blank};}),
    {calls:0,reads:0,revision:true,preview:true,canvas:true,blank:true});
  const straight=await createFinal();assert.equal(straight.payload.features.length,0);assert.equal(straight.payload.quantity,2);
  await screenshot("01-zero-hole-current-batch");
  cases.push({name:"Current zero-hole batch metadata preserves preview identity",...await reopen("tools-only-zero-hole.ictd",straight.result.partEntityId)});

  // A actual shared circle definition and actual Add Row action. The row's
  // station is edited through its table cell, retaining the real GPU canvas.
  await newCreation();
  await input(definition().locator('[data-tube-designer-punch-parameter="diameter"]'),4);
  await action("add");assert.equal(await page.locator('[data-tube-designer-punch-row="0"]').count(),1);
  await input(rowField("station"),400);
  await page.evaluate(async()=>{
    const f=window.fixture,s=f.view.tubeDesignerPunchWizard,record=s.features[0];
    record.arrayGroups=[{id:"layout-length",type:"linear",axis:"X",count:2,spacing:100,distributionMode:"pitch"},
      {id:"array-group-1",type:"linear",axis:"Y",count:2,spacing:6},
      {id:"array-group-2",type:"linear",axis:"Z",count:2,spacing:2},
      {id:"array-group-3",type:"polar",axis:"X",count:2,angleMode:"full-circle"}];
    f.batch.syncPunchBatch(f.view);f.render();f.identity={canvas:f.viewport.renderer.domElement};
  });
  // A definition edit causes the actual controller to serialize all arrays.
  await input(definition().locator('[data-tube-designer-punch-parameter="diameter"]'),4.2);
  const multi=await assertToolsOnly(16),multiPayload=await page.evaluate(()=>window.fixture.calls.findLast(c=>c.method.endsWith("PreviewPunchWizard")).payload.features[0]);
  assert.deepEqual(multiPayload.arrayGroups.map(g=>[g.type,g.axis,g.count]),[["linear","X",2],["linear","Y",2],["linear","Z",2],["polar","X",2]]);
  assert.equal(multiPayload.arrayCandidateCount,16);assert.equal(multiPayload.arrayTransforms.length,16);
  assert.equal(multiPayload.arrayCount,1);assert.equal(multiPayload.rowCount,1);
  const bounds=multi.objects.find(o=>o.id.startsWith("punch-preview-tool"));
  assert.ok(Math.abs(bounds.min[0]+102.1)<0.02&&Math.abs(bounds.max[0]-2.1)<0.02,JSON.stringify(bounds));
  assert(await page.evaluate(()=>window.fixture.identity.canvas===window.fixture.viewport.renderer.domElement));
  await screenshot("02-current-batch-xyz-polar-tools");
  cases.push({name:"Circle definition, actual Add Row/cell edit, XYZ/polar sixteen positioned preview tools"});
  await page.evaluate(()=>{const f=window.fixture,s=f.view.tubeDesignerPunchWizard;
    s.features[0].arrayGroups=s.features[0].arrayGroups.filter(group=>group.axis==="X");f.batch.syncPunchBatch(f.view);f.render();});
  await input(definition().locator('[data-tube-designer-punch-parameter="diameter"]'),4);
  await assertToolsOnly(4);const combined=await createFinal();
  assert.equal(combined.payload.features[0].arrayTransforms.length,4);
  cases.push({name:"Four connected axial/polar holes publish the same CAD graph after save/open",...await reopen("tools-only-current-xyz-polar.ictd",combined.result.partEntityId)});

  // Template branch geometry is seeded as a creation draft; its dimensions
  // and final creation are changed by current shared definition controls.
  await newCreation();
  await choose(definition().locator('[data-cam-change-action$="record-kind-change"]'),"branch");
  await choose(definition().locator('[data-cam-change-action$="profile-select"]'),"system:polygon");
  const octagonRadius=20/Math.cos(Math.PI/8);
  await input(definition().locator('[data-tube-designer-punch-profile-parameter="sideCount"]'),8);
  await input(definition().locator('[data-tube-designer-punch-profile-parameter="radius"]'),octagonRadius);
  await input(definition().locator('[data-tube-designer-punch-profile-parameter="outerRadius"]'),0);
  await action("add");
  await page.evaluate(()=>{
    const f=window.fixture,s=f.view.tubeDesignerPunchWizard,record=s.features[0];
    Object.assign(record,{reference:"center",station:100,arrayGroups:[{id:"layout-length",type:"linear",axis:"X",count:2,spacing:50,distributionMode:"pitch"}]});
    f.batch.syncPunchBatch(f.view);f.render();
  });
  await input(definition().locator('[data-tube-designer-punch-profile-parameter="radius"]'),octagonRadius+0.1);
  await input(definition().locator('[data-tube-designer-punch-profile-parameter="radius"]'),octagonRadius);
  const problem=await assertToolsOnly(2),tools=problem.objects.filter(o=>o.id.startsWith("punch-preview-tool"));
  assert.equal(tools.length,1);assert.ok(Math.abs(tools[0].min[0]-80)<0.02&&Math.abs(tools[0].max[0]-170)<0.02,JSON.stringify(tools));
  await page.locator('[data-cam-action="tube-designer-nesting-punch-create-apply"]').click();await idle();
  const rejected=await page.evaluate(()=>window.fixture.calls.findLast(c=>c.method.endsWith("AddNestingPunchPart")));
  assert.match(rejected.error,/3.*段|3.*实体|3.*solid/i);
  cases.push({name:"Polygon8 width40 tools preview succeeds; actual final creation rejects three disconnected pieces"});
  await input(definition().locator('[data-tube-designer-punch-profile-parameter="radius"]'),10);
  await assertToolsOnly(2);const reduced=await createFinal();
  assert.equal(reduced.payload.features[0].section.parameters.radius,10);assert.equal(reduced.payload.features[0].arrayGroups[0].count,2);
  await screenshot("03-current-batch-reduced-polygon-created");
  cases.push({name:"Current branch radius10 edit creates connected final stock",...await reopen("tools-only-current-polygon20.ictd",reduced.result.partEntityId)});

  // Near-end and fully outside candidates retain their authored positions.
  await newCreation();await input(definition().locator('[data-tube-designer-punch-parameter="diameter"]'),10);await action("add");
  await input(rowField("station"),992);
  await page.evaluate(()=>{
    const f=window.fixture,s=f.view.tubeDesignerPunchWizard;
    s.features[0].arrayGroups=[{id:"layout-length",type:"linear",axis:"X",count:3,spacing:10,distributionMode:"pitch"}];
    f.batch.syncPunchBatch(f.view);f.render();
  });
  await input(definition().locator('[data-tube-designer-punch-parameter="diameter"]'),10.1);await input(definition().locator('[data-tube-designer-punch-parameter="diameter"]'),10);
  await assertToolsOnly(3);const crossing=await createFinal();
  assert.deepEqual(crossing.payload.features[0].arrayTransforms.map(matrix=>matrix[3]),[0,10,20]);
  assert.equal(crossing.payload.features[0].station,992);
  cases.push({name:"Near-end/outside authored candidates survive strict creation",...await reopen("tools-only-current-cross-end.ictd",crossing.result.partEntityId)});

  assert.equal(await page.evaluate(()=>window.fixture.calls.filter(call=>/\.(SavePartSketch|ApplyPartDrawing|ApplyPunchWizard|GetPartDrawing|RecoverImportedPartDrawing)$/.test(call.method)).length),0);
  assert.deepEqual(errors,[]);assert.deepEqual(externalRequests,[]);
  assert.ok(!messages.some(message=>/context.*(lost|limit)|too many active/i.test(message)),messages.join("\n"));
  const metrics=await page.evaluate(()=>({maximumActive:window.fixture.maximumActive,patches:window.fixture.patches,resources:window.fixture.resources.length,failures:window.fixture.failures}));
  assert.equal(metrics.maximumActive,1);assert.ok(metrics.patches>20);assert.equal(metrics.failures.length,1);
  assert.equal(metrics.failures[0].action,"tube-designer-nesting-punch-create-apply");
  assert.match(metrics.failures[0].message,/3.*段|3.*实体|3.*solid/i);
  writeFileSync(resolve(artifacts,"result.json"),JSON.stringify({passed:true,nativeDll,cases,metrics,messages},null,2));
  console.log(JSON.stringify({passed:true,nativeDll,cases,metrics,artifacts},null,2));
} catch(error) {
  if(page){await page.screenshot({path:resolve(artifacts,"failure.png")}).catch(()=>{});
    const state=await page.evaluate(()=>{const f=window.fixture;return f?{view:f.view,calls:f.calls,failures:f.failures,pending:f.pending}:{};}).catch(()=>({}));
    writeFileSync(resolve(artifacts,"failure.json"),JSON.stringify({error:error.stack,state},null,2));}
  throw error;
} finally {
  writeFileSync(resolve(artifacts,"requests.json"),JSON.stringify(requests,null,2));writeFileSync(resolve(artifacts,"native-stderr.log"),stderr);
  if(browser)await browser.close();
  if(bridge.exitCode===null){await rpc({action:"exit"}).catch(()=>{});bridge.stdin.end();}
}
