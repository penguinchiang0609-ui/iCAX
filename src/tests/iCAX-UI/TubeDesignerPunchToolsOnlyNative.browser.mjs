// Real production punch UI and local DOM patching -> native SDO -> persisted
// geometry. Only workbench navigation/file picker and JSON bridge are adapters.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { mkdirSync, readFileSync, writeFileSync, readdirSync, copyFileSync, existsSync, statSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const root=fileURLToPath(new URL("../../../",import.meta.url)),sourceRoot=resolve(root,"src");
const artifacts=resolve(root,"tmp/punch-tools-only-native-browser"),runtime=resolve(artifacts,"native");
mkdirSync(runtime,{recursive:true});
// Never hold the DLL being relinked by the native build agent.
const sourceDlls=process.env.ICAX_PUNCH_NATIVE_DLL_DIR||resolve(root,"src/x64/Debug");
for(const file of readdirSync(sourceDlls).filter(file=>/\.dll$/i.test(file)))copyFileSync(resolve(sourceDlls,file),resolve(runtime,file));
copyFileSync(process.env.ICAX_PUNCH_NATIVE_BRIDGE||resolve(root,"tmp/native-layout-tests/PunchAcceptanceBridge.exe"),resolve(runtime,"PunchAcceptanceBridge.exe"));
const nativeDll={source:resolve(sourceDlls,"TubeDesigner.dll"),bytes:statSync(resolve(runtime,"TubeDesigner.dll")).size,
  modified:statSync(resolve(sourceDlls,"TubeDesigner.dll")).mtime.toISOString()};
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
const invoke=(method,payload={})=>rpc({action:"invoke",method,payload});
try {
  await rpc({action:"reset"});
  const profiles=[];
  const profilesDir=resolve(sourceRoot,"apps/tube-designer/templates/_shared/profiles");
  for(const directory of readdirSync(profilesDir,{withFileTypes:true}).filter(item=>item.isDirectory())) {
    if(!existsSync(resolve(profilesDir,directory.name,"profile.json")))continue;
    const descriptor=JSON.parse(readFileSync(resolve(profilesDir,directory.name,"profile.json"),"utf8"));
    const defaultParameters=Object.fromEntries(descriptor.parameters.map(p=>[p.key,p.defaultValue]));
    const evaluated=await invoke("EvaluateProfilePackage",{profileRef:{scope:"system",id:directory.name},parameters:defaultParameters});
    profiles.push({id:directory.name,name:descriptor.displayName["zh-CN"],profileType:"parametric-package",descriptor,defaultParameters,previewProfile:evaluated.profile});
  }
  assert.ok(profiles.some(p=>p.id==="polygon"&&p.defaultParameters.sideCount===8));
  const scene=await invoke("List"),{chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||"playwright");
  browser=await chromium.launch({headless:true,...(process.env.ICAX_BROWSER_CHANNEL?{channel:process.env.ICAX_BROWSER_CHANNEL}:{})});
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
  await page.addStyleTag({content:"*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,Arial,sans-serif}"+readFileSync(resolve(sourceRoot,"apps/_shared/workbench/styles/laser3dcam.css"),"utf8")+tubeDesignerCss});
  await page.evaluate(async({profiles,scene,dxfPath})=>{
    const creation=await import("/src/apps/tube-designer/webpage/nestingPunchPart.mjs"),parts=await import("/src/apps/tube-designer/webpage/partsArea.mjs");
    const editor=await import("/src/apps/tube-designer/webpage/punchEditor.mjs"),wizard=await import("/src/apps/tube-designer/webpage/punchWizard.mjs");
    const {renderDesignerOperationOverlay}=await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const {patchPunchDom}=await import("/src/apps/tube-designer/webpage/punchDomPatch.mjs");
    const {ThreeRenderViewport}=await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs"),THREE=await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const mount=document.querySelector("#app"),view={pending:false,activeAreaId:"nesting",scene,tubeDesignerSystemProfiles:profiles,
      tubeDesignerSelectedProfileId:"system:round",tubeDesignerUserData:{profiles:[]},tubeDesignerTemplateProfiles:[]};
    const f=window.fixture={view,wizard,pending:0,calls:[],failures:[],resources:[],active:0,maximumActive:0,patches:0};
    const originalMount=ThreeRenderViewport.prototype.mount;
    ThreeRenderViewport.prototype.mount=function(...args){f.viewport=this;return originalMount.apply(this,args);};
    const invoke=async(method,payload,options)=>{
      const call={method,payload:structuredClone(payload)};f.calls.push(call);f.active++;f.maximumActive=Math.max(f.maximumActive,f.active);
      options?.onReport?.({message:"真实原生计算中",completed:0,total:1});
      try{const result=await window.nativeRpc({action:"invoke",method:method.replace(/^TubeDesigner\./,""),payload});call.result=result;return result;}
      catch(error){call.error=error.message;throw error;}finally{f.active--;}
    };
    const resources={async get(url,options){const version=Number(options?.headers?.get("ICAX-Resource-Version")||0);
      const result=await window.nativeRpc({action:"resource",payload:{url,version}});f.resources.push({url,version,bytes:result.bytes});
      return new Response(Uint8Array.from(atob(result.base64),c=>c.charCodeAt(0)));}};
    const context=f.context={mount,sceneProxy:{invoke,resources},productProxy:{invoke},appProxy:{bridge:{async openFileDialog(){return dxfPath;}}}};
    f.parts=()=>view.scene.tubeDesigner?.nestingGroups?.flatMap(g=>g.parts??[])??[];
    const ops={renderProject(){
      const html=creation.renderNestingPunchPartDialog(view)||parts.renderPunchWizardDialog(context,view);
      const full=html+renderDesignerOperationOverlay(context,view);
      if(patchPunchDom(view,mount,full))f.patches++;
      else mount.innerHTML='<nav data-fixture-navigation><button data-cam-action="tube-designer-nesting-punch-create-open">新建冲孔件</button>'
        +f.parts().map(p=>'<button data-cam-action="tube-designer-punch-open" data-tube-designer-part-id="'+p.entityId+'">打开 '+p.entityId+'</button>').join("")+"</nav>"+full;
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
  const popup=()=>page.locator("[data-punch-parameter-dialog]");
  const input=async(locator,value)=>{await locator.fill(String(value));await locator.press("Tab");await ready();};
  const choose=async(locator,value)=>{await locator.selectOption(value);await ready();};
  const open=async(mode,index="draft")=>{await page.locator('[data-tube-designer-punch-row="'+index+'"] [data-tube-designer-punch-editor-mode="'+mode+'"]').click();await ready();};
  const closePopup=async()=>{await popup().locator('[data-cam-action$="parameters-cancel"]').click();await ready();};
  const pfield=name=>popup().locator('[data-tube-designer-punch-field="'+name+'"]');
  const arrayfield=(name,id="legacy-length")=>popup().locator('[data-tube-designer-punch-array-group="'+id+'"][data-tube-designer-punch-array-field="'+name+'"]');
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
    assert.ok(beforePart?.thumbnailGeometryResourceId,"Saved part must expose its real rendered geometry resource.");
    // The bridge exposes render FlatBuffers. Manufacturing BRep byte equality
    // has a separate native persistence test; do not substitute one for the other.
    const beforeGeometry=await rpc({action:"resource",payload:{url:beforePart.thumbnailGeometryResourceId,version:beforePart.thumbnailGeometryResourceVersion}});
    await rpc({action:"save",payload:{file}});const opened=await rpc({action:"open",payload:{file}});assert.equal(opened.opened,true);
    const afterPart=opened.snapshot.tubeDesigner.nestingGroups.flatMap(g=>g.parts??[]).find(p=>p.entityId===id);
    const afterGeometry=await rpc({action:"resource",payload:{url:afterPart.thumbnailGeometryResourceId,version:afterPart.thumbnailGeometryResourceVersion}});
    assert.equal(afterGeometry.base64,beforeGeometry.base64,"Save/open must preserve actual final render geometry bytes, not just UI rules.");
    await page.evaluate(scene=>{const f=window.fixture;f.view.scene=scene;f.render();},opened.snapshot);
    await page.locator('[data-fixture-navigation] [data-tube-designer-part-id="'+id+'"]').click();
    await page.waitForFunction(()=>{const s=window.fixture.view.tubeDesignerPunchWizard;return s?.preview?.toolsOnly===true&&!s.initialToolsPreviewPart;});await ready();
  };

  // The reported problematic defaults are intentional: 40 x 40 octagonal
  // branch on a round diameter-40 stock, not a conveniently smaller fixture.
  await page.getByRole("button",{name:"新建冲孔件",exact:true}).click();await ready();
  await choose(page.locator('[data-tube-designer-nesting-punch-field="profileKey"]'),"system:round");
  await open("shape");
  await choose(popup().locator('[data-cam-change-action$="profile-select"]'),"system:polygon");
  assert.equal(await popup().locator('[data-tube-designer-punch-profile-parameter="sideCount"]').inputValue(),"8");
  assert.equal(await popup().locator('[data-tube-designer-punch-profile-parameter="width"]').inputValue(),"40");
  assert.equal(await popup().locator('[data-tube-designer-punch-profile-parameter="depth"]').inputValue(),"40");
  await closePopup();await open("pose");await choose(pfield("reference"),"center");await input(pfield("station"),100);await closePopup();
  await open("arrays");await input(arrayfield("count"),2);await input(arrayfield("spacing"),50);await closePopup();
  await page.locator('[data-tube-designer-punch-row="draft"] [data-cam-action$="-add"]').click();await ready();
  const problem=await assertToolsOnly(2);
  const tools=problem.objects.filter(o=>o.id.startsWith("punch-preview-tool"));assert.equal(tools.length,1);
  assert.ok(Math.abs(tools[0].min[0]-80)<0.01&&Math.abs(tools[0].max[0]-170)<0.01,JSON.stringify(tools));
  assert.ok(tools[0].x.some(x=>Math.abs(x-80)<0.01)&&tools[0].x.some(x=>Math.abs(x-130)<0.01));
  const beforeConfirm=await page.evaluate(()=>window.fixture.calls.filter(c=>c.method.endsWith("AddNestingPunchPart")).length);
  await screenshot("01-default-polygon40-two-tools-no-cut");
  await page.locator('[data-cam-action="tube-designer-nesting-punch-create-apply"]').click();await idle();
  const rejected=await page.evaluate(()=>window.fixture.calls.findLast(c=>c.method.endsWith("AddNestingPunchPart")));
  assert.match(rejected.error,/3.*段|3.*实体|3.*solid/i);
  assert.equal(await page.evaluate(()=>window.fixture.calls.filter(c=>c.method.endsWith("AddNestingPunchPart")).length),beforeConfirm+1);
  assert.match(await page.locator(".tube-designer-punch-backdrop").innerText(),/3/);
  await screenshot("02-final-confirm-reports-three-segments");
  cases.push("Full default polygon8 40 branch / round40 main: two positioned tools at centre+100,+150; final-only three-segment rejection");

  await open("shape","0");
  await input(popup().locator('[data-tube-designer-punch-profile-parameter="width"]'),20);
  await input(popup().locator('[data-tube-designer-punch-profile-parameter="depth"]'),20);
  await closePopup();await assertToolsOnly(2);await screenshot("03-polygon20-ready-to-confirm");
  await page.locator('[data-cam-action="tube-designer-nesting-punch-create-apply"]').click();await idle();
  const created=await page.evaluate(()=>window.fixture.calls.findLast(c=>c.method.endsWith("AddNestingPunchPart")));
  assert.ok(created.result?.partEntityId,JSON.stringify(created));assert.equal(created.error,undefined);
  await reopen("tools-only-polygon20.ictd",created.result.partEntityId);
  let saved=await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features[0]);
  assert.equal(saved.section.parameters.width,20);assert.equal(saved.section.parameters.depth,20);assert.equal(saved.arrayGroups[0].count,2);
  assert.equal(saved.reference,"center");assert.equal(saved.station,100);
  assert.equal(await page.evaluate(()=>[...window.fixture.viewport.transformPayloads.values()][0].localToWorld[12]),-500);
  await screenshot("04-real-saved-part-reopened");
  await page.locator('.tube-designer-punch-footer [data-cam-action="tube-designer-punch-cancel"]').click();await idle();
  cases.push("Reduce branch to20 through actual shape controls: strict success, native save/open, immutable stock centre and array recipe");

  // A separate one-record fixture combines X, Y, Z and polar groups. Small
  // transverse offsets keep this a connected blank at final confirmation.
  await page.getByRole("button",{name:"新建冲孔件",exact:true}).click();await ready();
  await choose(page.locator('[data-tube-designer-nesting-punch-field="profileKey"]'),"system:round");
  await open("shape");await choose(popup().locator('[data-cam-change-action$="record-kind-change"]'),"tool");
  await choose(popup().locator('[data-tube-designer-punch-field="tool"]'),"circle");
  await input(popup().locator('[data-tube-designer-punch-parameter="diameter"]'),4);await closePopup();
  await open("pose");await choose(pfield("face"),"top");await input(pfield("station"),400);await closePopup();
  await open("arrays");await input(arrayfield("count"),2);await input(arrayfield("spacing"),100);
  await popup().locator('[data-cam-action$="array-group-add"][data-tube-designer-punch-array-type="linear"][data-tube-designer-punch-array-axis="Y"]').click();await ready();
  await input(arrayfield("spacing","array-group-1"),6);
  await popup().locator('[data-cam-action$="array-group-add"][data-tube-designer-punch-array-type="linear"][data-tube-designer-punch-array-axis="Y"]').click();await ready();
  await choose(arrayfield("axis","array-group-2"),"Z");await input(arrayfield("spacing","array-group-2"),2);
  await popup().locator('[data-cam-action$="array-group-add"][data-tube-designer-punch-array-type="polar"][data-tube-designer-punch-array-axis="X"]').click();await ready();
  await input(arrayfield("count","array-group-3"),2);
  await closePopup();await page.locator('[data-tube-designer-punch-row="draft"] [data-cam-action$="-add"]').click();await ready();
  const multi=await assertToolsOnly(16);
  const multiPayload=await page.evaluate(()=>window.fixture.calls.findLast(c=>c.method.endsWith("PreviewPunchWizard")).payload.features[0]);
  assert.deepEqual(multiPayload.arrayGroups.map(g=>[g.type,g.axis,g.count]),[["linear","X",2],["linear","Y",2],["linear","Z",2],["polar","X",2]]);
  assert.equal(multiPayload.arrayCandidateCount,16);assert.equal(multiPayload.arrayTransforms.length,16);
  assert.equal(multiPayload.arrayCount,1);assert.equal(multiPayload.rowCount,1);
  const bounds=multi.objects.find(o=>o.id.startsWith("punch-preview-tool"));
  assert.ok(Math.abs(bounds.min[0]+102)<0.01&&Math.abs(bounds.max[0]-2)<0.01,JSON.stringify(bounds));
  assert.ok(bounds.min[1]<-7.9&&bounds.max[1]>7.9,JSON.stringify(bounds));
  await screenshot("05-one-record-independent-xyz-polar-groups");
  await page.locator('[data-cam-action="tube-designer-nesting-punch-create-apply"]').click();await idle();
  const multiCreated=await page.evaluate(()=>window.fixture.calls.findLast(c=>c.method.endsWith("AddNestingPunchPart")));
  assert.ok(multiCreated.result?.partEntityId,JSON.stringify(multiCreated));
  await reopen("tools-only-independent-groups.ictd",multiCreated.result.partEntityId);
  saved=await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features[0]);
  assert.deepEqual(saved.arrayGroups,multiCreated.payload.features[0].arrayGroups);assert.deepEqual(saved.arraySkips,multiCreated.payload.features[0].arraySkips);
  assert.equal(saved.arrayCandidateCount,16);assert.equal(saved.arrayTransforms.length,16);
  await assertToolsOnly(16);await open("arrays","0");
  await screenshot("06-independent-groups-persisted-and-reopened");await closePopup();
  cases.push("One record X/Y/Z/polar Cartesian16: visible native tool bounds, strict success, saved groups/transforms roundtrip and UI reopen");

  // Changing record A must preserve record B's resource and WebGL/DOM identity.
  await page.locator('[data-tube-designer-punch-row="0"] [data-cam-action$="-copy"]').click();await ready();
  await open("pose");await input(pfield("station"),700);await closePopup();
  await page.locator('[data-tube-designer-punch-row="draft"] [data-cam-action$="-add"]').click();await ready();await assertToolsOnly(32);
  await page.evaluate(()=>{const f=window.fixture,s=f.view.tubeDesignerPunchWizard,b=s.preview.toolPreviews.find(t=>t.key===s.features[1].id);
    f.stableProbe={canvas:f.viewport.renderer.domElement,row:document.querySelector('[data-tube-designer-punch-row="1"]'),
      object:f.viewport.sceneObjects.get("punch-preview-tool:"+b.target+":"+b.key),reference:JSON.stringify(b.geometry),id:b.key,
      url:b.geometry.url,reads:f.resources.filter(r=>r.url===b.geometry.url).length};});
  await open("pose","0");await input(pfield("station"),420);await closePopup();
  const unchanged=await page.evaluate(()=>{const f=window.fixture,p=f.stableProbe,s=f.view.tubeDesignerPunchWizard,b=s.preview.toolPreviews.find(t=>t.key===p.id);
    return {canvas:f.viewport.renderer.domElement===p.canvas,row:document.querySelector('[data-tube-designer-punch-row="1"]')===p.row,
      object:f.viewport.sceneObjects.get("punch-preview-tool:"+b.target+":"+b.key)===p.object,
      reference:JSON.stringify(b.geometry)===p.reference,reads:f.resources.filter(r=>r.url===p.url).length-p.reads,
      a:s.features[0].station,b:s.features[1].station};});
  assert.deepEqual(unchanged,{canvas:true,row:true,object:true,reference:true,reads:0,a:420,b:700});
  await assertToolsOnly(32);await screenshot("07-edit-a-keeps-b-resource-and-object");
  cases.push("Two records: editing A preserves B native resource/version, DOM row, Three object and canvas identity with zero B resource re-reads");

  // A disabled unfinished array is valid editable recipe data; native must not
  // reject its zero candidate metadata or require non-existent transforms.
  await page.locator('[data-tube-designer-punch-row="0"] [data-cam-action$="-copy"]').click();await ready();
  await page.locator('[data-tube-designer-punch-row="draft"] [data-tube-designer-punch-field="enabled"]').uncheck();await ready();
  await open("arrays");await input(arrayfield("count"),0);await closePopup();
  await page.locator('[data-tube-designer-punch-row="draft"] [data-cam-action$="-add"]').click();await ready();await assertToolsOnly(32);
  await page.locator('.tube-designer-punch-footer [data-cam-action="tube-designer-punch-apply"]').click();await idle();
  const disabledSave=await page.evaluate(()=>window.fixture.calls.findLast(c=>c.method.endsWith("ApplyPunchWizard")));
  assert.equal(disabledSave.error,undefined);assert.equal(disabledSave.payload.features[2].enabled,false);
  assert.equal(disabledSave.payload.features[2].arrayCandidateCount,0);assert.equal(disabledSave.payload.features[2].arrayTransforms,undefined);
  await reopen("tools-only-disabled-array.ictd",multiCreated.result.partEntityId);
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features[2].arrayGroups[0].count),0);
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features[2].enabled),false);
  await page.locator('.tube-designer-punch-footer [data-cam-action="tube-designer-punch-cancel"]').click();await idle();
  cases.push("Disabled unfinished array count0: native tools-only preview, strict Apply and native save/open keep its editable recipe");

  const newCreation=async()=>{await page.getByRole("button",{name:"新建冲孔件",exact:true}).click();await ready();};
  const closePart=async()=>{await page.locator('.tube-designer-punch-footer [data-cam-action="tube-designer-punch-cancel"]').click();await idle();};
  const main=name=>page.locator('[data-tube-designer-nesting-punch-field="'+name+'"]');
  const draftCircle=async diameter=>{
    await open("shape");await choose(popup().locator('[data-cam-change-action$="record-kind-change"]'),"tool");
    await choose(popup().locator('[data-tube-designer-punch-field="tool"]'),"circle");
    await input(popup().locator('[data-tube-designer-punch-parameter="diameter"]'),diameter);await closePopup();
  };
  const addDraft=async()=>{await page.locator('[data-tube-designer-punch-row="draft"] [data-cam-action$="-add"]').click();await ready();};
  const createFinal=async()=>{
    await page.locator('[data-cam-action="tube-designer-nesting-punch-create-apply"]').click();await idle();
    const call=await page.evaluate(()=>window.fixture.calls.findLast(c=>c.method.endsWith("AddNestingPunchPart")));
    assert.ok(call.result?.partEntityId,JSON.stringify(call));assert.equal(call.error,undefined);return call;
  };

  // Original acceptance A: zero-hole creation, quantity, metadata and true
  // native save/open remain supported with no artificial required-hole rule.
  await newCreation();await choose(main("profileKey"),"system:round");
  await page.evaluate(()=>{const f=window.fixture;f.metadataProbe={calls:f.calls.length,reads:f.resources.length,
    revision:f.view.tubeDesignerPunchWizard.revision,preview:f.view.tubeDesignerPunchWizard.preview,
    canvas:f.viewport.renderer.domElement,blank:f.viewport.sceneObjects.get("punch-preview-blank")};});
  await input(main("quantity"),2);await main("name").fill("验收-零孔直管");await main("name").press("Tab");await ready();
  await main("material").fill("Q235B");await main("material").press("Tab");await ready();await assertToolsOnly(0);
  assert.deepEqual(await page.evaluate(()=>{const f=window.fixture,p=f.metadataProbe;return {calls:f.calls.length-p.calls,reads:f.resources.length-p.reads,
    revision:f.view.tubeDesignerPunchWizard.revision===p.revision,preview:f.view.tubeDesignerPunchWizard.preview===p.preview,
    canvas:f.viewport.renderer.domElement===p.canvas,blank:f.viewport.sceneObjects.get("punch-preview-blank")===p.blank};}),
    {calls:0,reads:0,revision:true,preview:true,canvas:true,blank:true});
  const straight=await createFinal();assert.equal(straight.payload.features.length,0);assert.equal(straight.payload.quantity,2);
  assert.equal(straight.payload.name,"验收-零孔直管");await reopen("tools-only-zero-hole.ictd",straight.result.partEntityId);
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features.length),0);await assertToolsOnly(0);
  assert.equal(await page.evaluate(()=>[...window.fixture.viewport.transformPayloads.values()][0].localToWorld[12]),-500);
  await screenshot("08-zero-hole-save-reopen");await closePart();
  cases.push("Legacy A preserved: zero-hole round stock, quantity/name, strict create, native save/open and automatic centred tools-only scene");

  // Original acceptance B: actual R2 profile regeneration, fixed-end margins,
  // stock-length undo, left miter, and editing the saved array from5 to6 holes.
  await newCreation();await choose(main("profileKey"),"system:rect");
  await input(page.locator('[data-tube-designer-main-profile-parameter="width"]'),50);await draftCircle(8);
  await open("pose");await choose(pfield("face"),"top");await closePopup();
  await open("arrays");await input(arrayfield("count"),5);await choose(arrayfield("distributionMode"),"end-margins");
  await input(arrayfield("headMargin"),100);await input(arrayfield("tailMargin"),100);await closePopup();await addDraft();
  const positions=()=>page.evaluate(()=>{const f=window.fixture,s=f.view.tubeDesignerPunchWizard,r=f.wizard.resolvePunchDistribution(s.features[0],s.baseLength);
    return r.arrayGroupsSummary.groups[0].layoutSummary.positions;});
  assert.deepEqual(await positions(),[100,300,500,700,900]);
  await input(main("length"),1200);assert.deepEqual(await positions(),[100,350,600,850,1100]);
  await page.locator('[data-cam-action="tube-designer-nesting-punch-create-undo"]').click();await ready();
  assert.equal(await main("length").inputValue(),"1000");assert.deepEqual(await positions(),[100,300,500,700,900]);
  await choose(page.locator('[data-tube-designer-punch-end-row="start"] [data-tube-designer-punch-field="tool"]'),"end-miter");await closePopup();
  await screenshot("09-r2-fixed-end-margins-miter");
  const beforeView=await page.evaluate(()=>window.fixture.calls.length);
  // A front-orthographic cube displays only its front face and shared edges.
  // Reach the hidden TOP face through its visible top-front edge, as a user does.
  if(!await page.locator('[data-cam-viewcube] [data-cam-action="view-standard"][data-cam-view="top"]').count())
    await page.locator('[data-cam-viewcube] [data-cam-action="view-standard"][data-cam-view="top-front"]').click();
  await page.locator('[data-cam-viewcube] [data-cam-action="view-standard"][data-cam-view="top"]').click();
  await page.locator('.tube-punch-view-controls button').first().click();
  const direction=await page.evaluate(()=>window.fixture.viewport.getViewCubeState().direction);
  assert.ok(direction.z<-.999&&Math.abs(direction.x)<.001&&Math.abs(direction.y)<.001,JSON.stringify(direction));
  assert.equal(await page.evaluate(()=>window.fixture.calls.length),beforeView,"ViewCube and Fit cannot issue geometry operations.");
  const shaped=await createFinal();assert.equal(shaped.payload.parameters.width,50);assert.equal(shaped.payload.parameters.cornerRadius,2);
  assert.equal(shaped.payload.features[0].arrayGroups[0].count,5);assert.equal(shaped.payload.ends.start.toolRef.id,"end-miter");
  await reopen("tools-only-r2-array-miter.ictd",shaped.result.partEntityId);await open("arrays","0");await input(arrayfield("count"),6);await closePopup();
  await page.locator('.tube-designer-punch-footer [data-cam-action="tube-designer-punch-apply"]').click();await idle();
  const edited=await page.evaluate(()=>window.fixture.calls.findLast(c=>c.method.endsWith("ApplyPunchWizard")));
  assert.equal(edited.error,undefined);assert.equal(edited.payload.features[0].arrayGroups[0].count,6);
  await reopen("tools-only-r2-array-miter.ictd",shaped.result.partEntityId);assert.deepEqual(await positions(),[100,260,420,580,740,900]);
  assert.equal(await page.evaluate(()=>[...window.fixture.viewport.transformPayloads.values()][0].localToWorld[12]),-500);
  await screenshot("10-r2-six-holes-and-end-reopened");await closePart();
  cases.push("Legacy B preserved: R2 stock dimensions, both margins, five holes, length undo, miter, create/save/open/edit six/Apply/save/open");

  // Original acceptance C: near-end and fully outside candidates stay in the
  // recipe; preview never pretends these candidate counts are finished holes.
  await page.locator('[data-fixture-navigation] [data-tube-designer-part-id="'+straight.result.partEntityId+'"]').click();
  await page.waitForFunction(()=>window.fixture.view.tubeDesignerPunchWizard?.preview?.toolsOnly===true);await ready();
  await draftCircle(10);await open("pose");await choose(pfield("face"),"top");await input(pfield("station"),992);await closePopup();
  await open("arrays");await input(arrayfield("count"),3);await input(arrayfield("spacing"),10);await closePopup();await addDraft();
  const cross=await assertToolsOnly(3);assert.equal(cross.preview.candidateToolCount,3);assert.equal(cross.preview.toolCountExact,false);
  assert.doesNotMatch(await page.locator(".tube-designer-punch-review").innerText(),/实际相交孔刀|未接触主管，已略过/);
  await screenshot("11-cross-end-candidates");await page.locator('.tube-designer-punch-footer [data-cam-action="tube-designer-punch-apply"]').click();await idle();
  const crossed=await page.evaluate(()=>window.fixture.calls.findLast(c=>c.method.endsWith("ApplyPunchWizard")));
  assert.equal(crossed.error,undefined);assert.equal(crossed.payload.features[0].arrayCandidateCount,3);
  assert.deepEqual(crossed.payload.features[0].arrayTransforms.map(m=>992+m[3]),[992,1002,1012]);
  await reopen("tools-only-cross-end.ictd",straight.result.partEntityId);await assertToolsOnly(3);
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features[0].arrayGroups[0].count),3);await closePart();
  cases.push("Legacy C preserved: stations992/1002/1012 cross end without allow-open UI, strict native intersection, three-candidate recipe save/open");

  // Original acceptance D: library and actual local DXF both feed side tools
  // and independent end tools; neither uses a mock profile or mock final shape.
  await newCreation();await choose(main("profileKey"),"system:rect");await open("shape");
  await choose(popup().locator('[data-cam-change-action$="profile-select"]'),"system:round");
  await input(popup().locator('[data-tube-designer-punch-profile-parameter="width"]'),12);await closePopup();
  await open("pose");await input(pfield("station"),200);await closePopup();await addDraft();
  await open("shape");await choose(popup().locator('[data-cam-change-action$="record-kind-change"]'),"dxf");
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.draft.section.source),"dxf");
  await screenshot("12-local-dxf-real-profile-dialog");await closePopup();await open("pose");await input(pfield("station"),700);await closePopup();await addDraft();
  await choose(page.locator('[data-tube-designer-punch-end-row="start"] [data-cam-change-action$="record-kind-change"]'),"branch");
  await choose(popup().locator('[data-cam-change-action$="profile-select"]'),"system:round");await input(popup().locator('[data-tube-designer-punch-profile-parameter="width"]'),12);await closePopup();
  await choose(page.locator('[data-tube-designer-punch-end-row="end"] [data-cam-change-action$="record-kind-change"]'),"dxf");await closePopup();
  const bothEnds=await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.preview);
  assert.equal(bothEnds.toolsOnly,true);assert.equal(bothEnds.geometry,undefined);assert.equal(bothEnds.toolPreviews.filter(t=>t.target==="end").length,2);
  await screenshot("13-branch-dxf-side-and-both-ends");const profilesPart=await createFinal();
  assert.deepEqual(profilesPart.payload.features.map(f=>f.section.source),["library","dxf"]);
  assert.equal(profilesPart.payload.features[0].section.parameters.width,12);assert.equal(profilesPart.payload.ends.start.section.parameters.width,12);
  assert.equal(profilesPart.payload.ends.end.section.source,"dxf");await reopen("tools-only-branch-dxf-ends.ictd",profilesPart.result.partEntityId);
  assert.deepEqual(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features.map(f=>f.section.source)),["library","dxf"]);
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.ends.end.section.source),"dxf");
  assert.equal(await page.evaluate(()=>[...window.fixture.viewport.transformPayloads.values()][0].localToWorld[12]),-500);
  await screenshot("14-real-dxf-ends-and-branch-reopened");await closePart();
  cases.push("Legacy D preserved: library branch and local DXF, side holes and both end cuts, actual profile/import SDOS, final create/save/open");

  assert.deepEqual(errors,[]);assert.deepEqual(externalRequests,[]);
  assert.ok(!messages.some(message=>/context.*(lost|limit)|too many active/i.test(message)),messages.join("\n"));
  const metrics=await page.evaluate(()=>({maximumActive:window.fixture.maximumActive,patches:window.fixture.patches,resources:window.fixture.resources.length,failures:window.fixture.failures}));
  assert.equal(metrics.maximumActive,1);assert.ok(metrics.patches>30);assert.equal(metrics.failures.length,1);
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
