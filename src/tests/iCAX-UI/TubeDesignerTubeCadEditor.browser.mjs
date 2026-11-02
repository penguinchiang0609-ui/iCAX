import assert from "node:assert/strict";
import { mkdirSync,readFileSync,writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { serveBrowserAsset } from "./browserPackageRuntime.mjs";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||"file:///C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs");
const output=resolve("output/tests/tube-cad-editor-browser");mkdirSync(output,{recursive:true});
const browser=await chromium.launch({headless:true,channel:"msedge"});
const report={passed:false,editor:"punch",nativeTransport:"controlled neutral document protocol fixture",actualSdkRenderer:true,standaloneCefEndToEnd:false};
let page;
try {
  page=await browser.newPage({viewport:{width:1440,height:800}});const errors=[];page.on("pageerror",error=>errors.push(error.message));
  await page.route("http://tube-cad-editor.test/**",serveBrowserAsset);await page.goto("http://tube-cad-editor.test/");
  await page.addStyleTag({content:"*{box-sizing:border-box}body{margin:0;font-family:Segoe UI}main{height:800px;position:relative}.fixture-pane{height:110px;overflow:auto}.fixture-content{height:500px}"
    +readFileSync("src/apps/_shared/workbench/styles/workbench.css","utf8")+tubeDesignerCss+".tube-cad-properties{max-height:260px}.tube-cad-curve-fields{max-height:100px}"});
  await page.evaluate(async()=>{
    const cad=await import("/src/apps/tube-designer/webpage/tubeCadEditor.mjs"),model=await import("/src/apps/tube-designer/webpage/tubeCadModel.mjs");
    const THREE=await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const {createThreeViewport}=await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const {encodeNestingGeometry}=await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const p=(X,Y,Z)=>({X,Y,Z}),circle={ID:"base-loop",Curves:[{ID:"base-circle",Segment:{Curve:{type:"Circle2",value:{
      Placement:{Location:{X:0,Y:0},XDirection:{X:1,Y:0},YDirection:{X:0,Y:1}},Radius:20}},Range:{First:0,Last:Math.PI*2,Reversed:false}}}]};
    let construction={Version:1,BaseNodeID:"base",RootNodeID:"base",SolidNodes:[{ID:"base",Label:"原材",Data:{type:"SExtrudedRegionNode",value:{
      First:0,Last:300,Frame:{Location:p(-150,0,0),XDirection:p(0,1,0),YDirection:p(0,0,1),ZDirection:p(1,0,0)},
      Section:{FillRule:0,Boundaries:[circle]},SectionPrimitives:[],SideAtlases:[]}},Relations:null,RemovalSemantics:null,Metadata:{}}],UVFeatures:[],Metadata:{source:"keep"}};
    const inner=structuredClone(circle);inner.ID="inner-loop";inner.Curves[0].ID="inner-circle";inner.Curves[0].Segment.Curve.value.Radius=18;
    construction.SolidNodes[0].Data.value.Section.Boundaries.push(inner);
    for(let i=0;i<65;i++)model.addTubeCadHole(construction,{diameter:8,station:5+i*4});
    const firstHole=construction.SolidNodes.find(node=>node.ID!==construction.BaseNodeID&&model.tubeCadRegion(node));
    firstHole.Relations=null;
    const inside=structuredClone(firstHole.Data.value.Section.Boundaries[0]);inside.ID="hole-inner-loop";inside.Curves[0].ID="hole-inner-circle";
    inside.Curves[0].Segment.Curve.value.Radius=2;firstHole.Data.value.Section.Boundaries.push(inside);
    const profileCut=structuredClone(firstHole);profileCut.ID="cut-profile";profileCut.Label="轮廓切除";
    profileCut.Data.value.Section.Boundaries=[{ID:"cut-loop",Curves:[{ID:"cut-bezier",Segment:{Curve:{type:"Bezier2",value:{
      Poles:Array.from({length:22},(_,i)=>({X:i/10,Y:Math.sin(i)/10}))}},Range:{First:0,Last:1,Reversed:false}}}]}];
    construction.SolidNodes.push(profileCut);
    let revision=1,resourceVersion=1;const resources=new Map();
    const box=new THREE.BoxGeometry(300,40,40);box.translate(0,0,0);
    const bytes=encodeNestingGeometry({positions:Array.from(box.attributes.position.array),indices:Array.from(box.index.array)});box.dispose();
    const mesh=version=>{const url=`fixture://cad/mesh/${version}`;resources.set(url,bytes);return {url,version:1};};
    const part=()=>({entityId:"fixture-part",name:"共享管材 CAD",length:300,independentNesting:true,manufacturingGeometryResourceVersion:resourceVersion,
      thumbnailGeometryResourceId:mesh(resourceVersion).url,thumbnailGeometryResourceVersion:1,properties:{"manufacturing.partKind":"tube",
        "tubeDesigner.cadDocument":{documentResourceId:"doc",documentResourceVersion:revision}}});
    const scene={tubeDesigner:{nestingGroups:[{parts:[part()]}]}},view={activeAreaId:"nesting",scene,pending:false},mount=document.createElement("main");document.body.append(mount);
    const f=window.fixture={cad,model,view,mount,firstHoleId:firstHole.ID,calls:[],releaseCalls:[],resourcesRequested:[],applied:[],fullRenders:0};
    Object.defineProperty(f,"state",{get(){return view.tubeDesignerCadEditor;}});
    const placement={Location:p(0,0,0),XDirection:p(1,0,0),YDirection:p(0,1,0),ZDirection:p(0,0,1)};
    const evaluateBoundary=()=>({Section:structuredClone(construction.SolidNodes[0].Data.value.Section),
      MachiningFaces:{Faces:[{Id:1}],Metadata:{constructionNodes:construction.SolidNodes.length,revision}}});
    let boundary=evaluateBoundary();
    f.makeCoreOnly=()=>{construction={Version:1,BaseNodeID:"",RootNodeID:"",SolidNodes:[],UVFeatures:[],Metadata:{}};delete f.normalizeFirst;delete f.normalizePreviewFirst;};
    const read=partEntityId=>({partEntityId:partEntityId??"fixture-part",resourceVersion,revision:String(revision),documentResourceId:"doc",documentResourceVersion:revision,
      geometry:structuredClone(boundary),construction:structuredClone(construction),placement:structuredClone(placement),nodes:construction.SolidNodes.map(node=>({nodeId:node.ID,kind:node.Data.type,geometryEditable:!!model.tubeCadRegion(node),sketchEditable:!!model.tubeCadRegion(node)}))});
    const context=f.context={mount,sceneProxy:{async invoke(method,payload){
      f.calls.push({method,payload:structuredClone(payload)});
      if(method==="TubeDesigner.GetTubeCadDocument") {
        if(f.failRead){f.failRead=false;throw new Error("受控读取失败");}
        if(f.holdRead){f.holdRead=false;await new Promise(resolve=>f.releaseRead=resolve);}return read(payload.partEntityId);
      }
      if(method==="TubeDesigner.ApplyTubeCadDocument") {
        if(Number(payload.revision)!==revision||payload.resourceVersion!==resourceVersion)throw new Error("Stale revision");
        if(payload.construction){construction=structuredClone(payload.construction);}
        else if(payload.boundaryGeometry){boundary=structuredClone(payload.boundaryGeometry);}
        else throw new Error("Geometry mutations require construction or boundaryGeometry");
        revision++;resourceVersion++;
        construction.Metadata.normalized=`server-${revision}`;
        if(f.normalizeFirst)construction.SolidNodes.find(node=>node.ID===f.normalizeFirst).Data.value.First=-41;
        if(payload.construction)boundary=evaluateBoundary();
        const result={...read(),snapshot:{tubeDesigner:{nestingGroups:[{parts:[part()]}]}}};
        if(f.holdSave){f.holdSave=false;await new Promise(resolve=>f.releaseSave=resolve);}return result;
      }
      if(method==="TubeDesigner.PreviewTubeCadDocument") {
        const result={partEntityId:payload.partEntityId,revision:payload.revision,geometry:mesh(`preview-${f.calls.length}`),documentGeometry:structuredClone(payload.boundaryGeometry??boundary),documentConstruction:structuredClone(payload.construction??construction),documentPlacement:structuredClone(placement),
          baseBounds:{min:[-150,-20,-20],max:[150,20,20]}};
        if(f.normalizePreviewFirst!==undefined) {
          result.documentConstruction.SolidNodes.find(node=>node.ID===f.firstHoleId).Data.value.First=f.normalizePreviewFirst;
          delete f.normalizePreviewFirst;
        }
        if(f.holdPreview){f.holdPreview=false;await new Promise(resolve=>f.releasePreview=resolve);}return result;
      }
      if(method==="TubeDesigner.ReleaseTubeCadDocumentPreview"){f.releaseCalls.push(payload.previewResourceKey);return {};}
      throw new Error(`Unexpected ${method}`);
    },resources:{async get(url){f.resourcesRequested.push(url);if(f.holdResource===url){f.holdResource=null;await new Promise(resolve=>f.releaseResource=resolve);}return new Response(resources.get(url));}}}};
    const ops=f.ops={createPartDrawingViewport(options){const viewport=createThreeViewport(options);f.viewport=viewport;
      const apply=viewport.applyViewSnapshot.bind(viewport);viewport.applyViewSnapshot=async(snapshot,client)=>{const result=await apply(snapshot,client);if(result.applied)f.applied.push(snapshot.rows[0].data.geometry.url);return result;};return viewport;},
      appendProjectLog(){},renderProject(){
        if(!cad.patchTubeCadEditor(view,mount)) {
          f.fullRenders++;
          const previous=mount.querySelector(".fixture-shell");if(!previous)mount.innerHTML='<div class="fixture-shell"><aside class="cam-context-pane fixture-pane"><div class="fixture-content">左栏</div></aside><aside class="cam-info-pane fixture-pane"><div class="fixture-content">右栏</div></aside><input aria-label="其他页面输入" data-other-input></div>';
          mount.querySelector(".tube-cad-backdrop")?.remove();mount.insertAdjacentHTML("beforeend",cad.renderTubeCadEditor(view));
        }
        cad.attachTubeCadEditor(context,view,mount,ops);
      }};
    f.action=(suffix,target={})=>cad.handleTubeCadEditorAction(context,view,"tube-designer-cad-"+suffix,target,ops);
    f.open=mode=>cad.openTubeCadEditor(context,view,scene.tubeDesigner.nestingGroups[0].parts[0],mode,ops);
    f.openPart=(part,mode)=>cad.openTubeCadEditor(context,view,part,mode,ops);
    mount.addEventListener("click",event=>{const target=event.target.closest("[data-cam-action]");if(target&&!target.disabled)void cad.handleTubeCadEditorAction(context,view,target.dataset.camAction,target,ops);});
    mount.addEventListener("change",event=>{if(event.target.dataset.camChangeAction)void cad.handleTubeCadEditorAction(context,view,event.target.dataset.camChangeAction,event.target,ops);});
    await f.open("punch");
  });
  const ready=()=>page.waitForFunction(()=>fixture.state&&!fixture.state.loading&&!fixture.state.saving);
  const field=key=>page.locator(`[data-cad-field="${key}"]`);
  const input=async(locator,value)=>{await locator.fill(String(value));await locator.dispatchEvent("change");};
  await ready();
  await page.evaluate(()=>fixture.action("select",{dataset:{cadNode:"cut-profile"}}));
  await page.locator('.tube-cad-properties>details').getByText("精确轮廓曲线",{exact:true}).first().click();
  assert.equal(await page.evaluate(()=>{const ids=Array.from(document.querySelectorAll(".tube-cad-editor [id]"),node=>node.id);return new Set(ids).size===ids.length;}),true,
    "Each exact curve coordinate label must have a unique stable identity");
  await page.screenshot({path:resolve(output,"punch-exact-cut-profile.png")});
  await page.evaluate(()=>{
    fixture.canvas=document.querySelector("[data-tube-cad-viewport] canvas");
    fixture.input=document.querySelector('[aria-label="特征名称"]');fixture.input.value="剖面正在继续输入";
    fixture.input.dispatchEvent(new Event("change",{bubbles:true}));fixture.input.focus({preventScroll:true});fixture.input.setSelectionRange(2,6);
    fixture.listeners=0;fixture.input.addEventListener("input",()=>fixture.listeners++);
    fixture.holdSave=true;fixture.normalizeFirst="cut-profile";
    void fixture.action("save");
  });
  await page.waitForFunction(()=>typeof fixture.releaseSave==="function");
  await page.evaluate(()=>{
    fixture.input.value="剖面继续输入后的名称";fixture.input.dispatchEvent(new Event("change",{bubbles:true}));fixture.input.focus({preventScroll:true});fixture.input.setSelectionRange(3,7);
    const selectors=[".cam-context-pane",".cam-info-pane",".tube-cad-sidebar",".tube-cad-properties",".tube-cad-curve-fields"];
    fixture.scrolls=selectors.map((selector,index)=>{const node=document.querySelector(selector);node.scrollTop=80+index*20;return {selector,node,top:node.scrollTop};});
    fixture.releaseSave();
  });
  await ready();
  const interaction=await page.evaluate(()=>{
    fixture.input.dispatchEvent(new Event("input",{bubbles:true}));
    return {scrolls:fixture.scrolls.map(row=>[row.selector,row.top,row.node.scrollTop]),sameInput:fixture.input===document.querySelector('[aria-label="特征名称"]'),
      sameCanvas:fixture.canvas===document.querySelector("[data-tube-cad-viewport] canvas"),focused:document.activeElement===fixture.input,
      range:[fixture.input.selectionStart,fixture.input.selectionEnd],listeners:fixture.listeners,label:fixture.state.construction.SolidNodes.find(n=>n.ID==="cut-profile").Label,
      first:fixture.state.construction.SolidNodes.find(n=>n.ID==="cut-profile").Data.value.First,normalized:fixture.state.construction.Metadata.normalized,dirty:fixture.state.dirty};
  });
  for(const [selector,expected,actual] of interaction.scrolls){assert.ok(expected>0,selector+" actually scrolls");assert.equal(actual,expected,selector);}
  assert.equal(interaction.sameInput,true);assert.equal(interaction.sameCanvas,true);assert.equal(interaction.focused,true);assert.deepEqual(interaction.range,[3,7]);assert.equal(interaction.listeners,1);
  assert.equal(interaction.label,"剖面继续输入后的名称");assert.equal(interaction.first,-41);assert.equal(interaction.normalized,"server-2");assert.equal(interaction.dirty,true);
  report.interaction=interaction;
  // Replacing conditional fields must not transfer focus to a different field.
  await page.evaluate(()=>fixture.action("select",{dataset:{cadNode:fixture.firstHoleId}}));
  assert.equal(await page.locator('[data-cad-field="radius"]').count(),2);
  assert.equal(await page.evaluate(()=>{const ids=Array.from(document.querySelectorAll(".tube-cad-editor [id]"),node=>node.id);return new Set(ids).size===ids.length;}),true,
    "Outer and inner boundary dimensions must keep different stable label identities");
  assert.equal(await page.evaluate(()=>fixture.input.isConnected),false);
  assert.notEqual(await page.evaluate(()=>document.activeElement?.getAttribute("aria-label")),"圆心 X");
  await page.waitForFunction(()=>fixture.state.preview?.draftRevision===fixture.state.draftRevision&&!fixture.state.previewPending&&fixture.applied.at(-1)?.includes("preview"));
  await page.evaluate(()=>{
    fixture.holdPreview=true;fixture.normalizePreviewFirst=-51;fixture.authorityRevision=fixture.state.revision;
    void fixture.action("field",{dataset:{cadField:"Last"},value:"320"});
  });
  await page.waitForFunction(()=>typeof fixture.releasePreview==="function");
  await input(field("Last"),330);
  await page.evaluate(()=>fixture.releasePreview());
  await page.waitForFunction(()=>fixture.state.preview?.draftRevision===fixture.state.draftRevision&&!fixture.state.previewPending);
  assert.equal(await page.evaluate(()=>fixture.state.preview.documentConstruction.SolidNodes.find(n=>n.ID===fixture.firstHoleId).Data.value.Last),330);
  assert.equal(await page.evaluate(()=>fixture.state.construction.SolidNodes.find(n=>n.ID===fixture.firstHoleId).Data.value.First),-51,
    "Preview normalization from the earlier request must merge with later user changes");
  assert.equal(await page.evaluate(()=>fixture.state.revision===fixture.authorityRevision&&fixture.state.dirty&&fixture.state.history.length>0),true);
  const key=await page.evaluate(()=>fixture.state.previewResourceKey);
  await page.evaluate(()=>fixture.action("close"));
  await page.waitForFunction(key=>fixture.releaseCalls.includes(key),key);
  assert.equal(await page.locator(".tube-cad-backdrop").count(),0);
  // Read/save completion on another page keeps its focus and resets lifecycle.
  await page.evaluate(()=>{fixture.holdRead=true;void fixture.open("punch");});
  await page.waitForFunction(()=>fixture.state.loading&&typeof fixture.releaseRead==="function");
  await page.evaluate(()=>{fixture.view.activeAreaId="profiles";fixture.ops.renderProject();const node=document.querySelector("[data-other-input]");node.focus();node.value="其他页面输入";node.setSelectionRange(2,4);fixture.releaseRead();});
  await page.waitForFunction(()=>!fixture.state.loading);
  assert.equal(await page.evaluate(()=>document.activeElement.hasAttribute("data-other-input")),true);
  await page.evaluate(()=>{fixture.view.activeAreaId="nesting";return fixture.open("punch");});await ready();
  await input(field("Last"),350);
  await page.evaluate(()=>{fixture.holdSave=true;void fixture.action("save");});
  await page.waitForFunction(()=>fixture.state.saving&&typeof fixture.releaseSave==="function");
  await page.evaluate(()=>{fixture.view.activeAreaId="profiles";fixture.ops.renderProject();document.querySelector("[data-other-input]").focus();fixture.releaseSave();});
  await page.waitForFunction(()=>!fixture.state.saving);
  assert.equal(await page.evaluate(()=>document.activeElement.hasAttribute("data-other-input")),true);
  await page.evaluate(()=>{fixture.view.activeAreaId="nesting";return fixture.open("punch");});await ready();
  assert.equal(await field("Last").inputValue(),"350");
  await page.waitForFunction(()=>fixture.applied.at(-1)===fixture.state.part.thumbnailGeometryResourceId);
  // A delayed old mesh completes after a save; the queued latest version wins.
  await page.evaluate(()=>fixture.action("close"));
  await page.evaluate(()=>{fixture.holdResource=fixture.view.scene.tubeDesigner.nestingGroups[0].parts[0].thumbnailGeometryResourceId;return fixture.open("punch");});
  await page.waitForFunction(()=>typeof fixture.releaseResource==="function");
  await input(field("Last"),360);
  await page.evaluate(()=>fixture.action("save"));await ready();
  await page.evaluate(()=>fixture.releaseResource());
  await page.waitForFunction(()=>fixture.applied.at(-1)===fixture.state.part.thumbnailGeometryResourceId);
  await page.screenshot({path:resolve(output,"punch-editor.png")});
  const readonlyDisplay=await page.evaluate(async()=>{
    const {renderTubeCadEditor}=await import("/src/apps/tube-designer/webpage/tubeCadEditor.mjs");
    const construction=structuredClone(fixture.state.construction),node=construction.SolidNodes.find(item=>item.ID===fixture.firstHoleId);
    node.Data.value.First=0.009999999999999787;node.Data.value.Last=3.029999999999999;
    node.Relations={Extent:{Rule:2,RecomputeAgainstHost:true}};
    const state={...fixture.state,construction,selected:node.ID,mode:"punch"},before=JSON.stringify(construction);
    const host=document.createElement("div");host.innerHTML=renderTubeCadEditor({...fixture.view,tubeDesignerCadEditor:state});
    const read=key=>host.querySelector(`[data-cad-field="${key}"]`).value;
    const readonly=[read("First"),read("Last")],unaffected=JSON.stringify(construction)===before;delete node.Relations;
    host.innerHTML=renderTubeCadEditor({...fixture.view,tubeDesignerCadEditor:state});
    return {readonly,editable:[read("First"),read("Last")],exact:node.Data.value.First,unaffected};
  });
  assert.deepEqual(readonlyDisplay.readonly,["0.01","3.03"]);assert.equal(readonlyDisplay.unaffected,true);
  assert.equal(readonlyDisplay.editable[0],String(readonlyDisplay.exact));
  // Replacing a part waits for its pending preview before releasing its scope.
  await page.evaluate(()=>{delete fixture.releasePreview;fixture.holdPreview=true;void fixture.action("field",{dataset:{cadField:"location-X"},value:"14"});});
  await page.waitForFunction(()=>fixture.state.previewRunning&&typeof fixture.releasePreview==="function");
  const replacement=await page.evaluate(()=>{
    fixture.previousState=fixture.state;fixture.previousKey=fixture.state.previewResourceKey;
    const another={...fixture.state.part,entityId:"second-part",name:"另一管材"};
    return fixture.openPart(another,"punch").then(()=>({oldKey:fixture.previousKey,newKey:fixture.state.previewResourceKey,
      oldClosed:fixture.previousState.previewClosed,releasedBeforeResponse:fixture.releaseCalls.includes(fixture.previousKey)}));
  });
  assert.equal(replacement.oldClosed,true);assert.notEqual(replacement.oldKey,replacement.newKey);assert.equal(replacement.releasedBeforeResponse,false);
  await page.evaluate(()=>fixture.releasePreview());
  await page.waitForFunction(key=>fixture.releaseCalls.includes(key),replacement.oldKey);
  assert.equal(await page.evaluate(()=>fixture.state.part.entityId),"second-part");
  // A failed read replaced the old session; retry creates a clean new session.
  await input(field("Last"),370);
  await page.waitForFunction(()=>fixture.state.preview?.draftRevision===fixture.state.draftRevision&&!fixture.state.previewPending);
  const failedRead=await page.evaluate(async()=>{
    const prior=fixture.state;fixture.failRead=true;
    try {await fixture.open("punch");}catch(error) {fixture.readFailure=error.message;}
    fixture.failedState=fixture.state;
    return {oldKey:prior.previewResourceKey,closed:prior.previewClosed,error:fixture.state.error,loading:fixture.state.loading,failure:fixture.readFailure};
  });
  assert.equal(failedRead.closed,true);assert.equal(failedRead.error,"受控读取失败",JSON.stringify(failedRead));assert.equal(failedRead.loading,false);
  await page.waitForFunction(key=>fixture.releaseCalls.includes(key),failedRead.oldKey);
  await page.evaluate(()=>fixture.open("punch"));await ready();
  assert.equal(await page.evaluate(()=>fixture.failedState.previewClosed&&fixture.failedState!==fixture.state),true);
  const holesBefore=await page.evaluate(()=>fixture.state.construction.SolidNodes.filter(node=>node.Data.type==="SExtrudedRegionNode").length);
  await input(page.locator('[data-cad-new-hole-field=diameter]'),12);
  await input(page.locator('[data-cad-new-hole-field=station]'),150);
  await page.locator('[data-cam-action=tube-designer-cad-add-hole]').click();
  await page.waitForFunction(()=>fixture.state.preview?.draftRevision===fixture.state.draftRevision&&!fixture.state.previewPending);
  assert.equal(await page.evaluate(()=>fixture.state.construction.SolidNodes.filter(node=>node.Data.type==="SExtrudedRegionNode").length),holesBefore+1);
  assert.equal(await field("radius").inputValue(),"12");
  assert.equal(await page.evaluate(()=>fixture.state.construction.SolidNodes.find(n=>n.ID===fixture.state.selected).Relations.Extent.Rule),1);
  for(const mode of ["sketch","drawing"])assert.match(await page.evaluate(async mode=>{try {await fixture.open(mode);return "unexpected success";}catch(error){return error.message;}},mode),/各自的绘图页面/);
  report.newThroughAllHole=true;report.onlyPunchOpening=true;
  const constructionContract=await page.evaluate(()=>({
    core:fixture.model.isTubeBoundaryGeometry(fixture.state.geometry),
    separated:fixture.state.geometry.SolidNodes===undefined&&Array.isArray(fixture.state.construction.SolidNodes),
    updated:fixture.state.geometry.MachiningFaces.Metadata.revision>1,
    payloads:fixture.calls.filter(call=>call.method==="TubeDesigner.ApplyTubeCadDocument").every(call=>!!call.payload.construction&&!Object.hasOwn(call.payload,"geometry")&&!Object.hasOwn(call.payload,"boundaryGeometry")),
  }));
  assert.deepEqual(constructionContract,{core:true,separated:true,updated:true,payloads:true});
  report.constructionContract=constructionContract;
  await page.evaluate(()=>{fixture.action("close");fixture.makeCoreOnly();return fixture.open("punch");});await ready();
  assert.equal(await page.evaluate(()=>fixture.state.coreOnly&&fixture.state.selected==="section"),true);
  assert.equal(await page.locator('[data-cam-action=tube-designer-cad-add-hole]').count(),0);
  const coreRadiusBefore=await page.evaluate(()=>fixture.state.geometry.Section.Boundaries[0].Curves[0].Segment.Curve.value.Radius);
  await input(field("radius").first(),(coreRadiusBefore+1)*2);
  await page.waitForFunction(()=>fixture.state.preview?.draftRevision===fixture.state.draftRevision&&!fixture.state.previewPending);
  await page.evaluate(()=>fixture.action("save"));await ready();
  const coreContract=await page.evaluate(()=>{
    const last=fixture.calls.filter(call=>call.method==="TubeDesigner.ApplyTubeCadDocument").at(-1).payload;
    return {selected:fixture.state.selected,constructionEmpty:fixture.state.construction.SolidNodes.length===0,
      boundaryOnly:!!last.boundaryGeometry&&!Object.hasOwn(last,"construction")&&!Object.hasOwn(last,"geometry"),
      radius:fixture.state.geometry.Section.Boundaries[0].Curves[0].Segment.Curve.value.Radius,
      sameSection:fixture.model.tubeCadCoreSectionNode(fixture.state.geometry).Data.value.Section===fixture.state.geometry.Section,
      faceRetained:fixture.state.geometry.MachiningFaces.Faces[0].Id===1};
  });
  assert.deepEqual(coreContract,{selected:"section",constructionEmpty:true,boundaryOnly:true,radius:coreRadiusBefore+1,sameSection:true,faceRetained:true});
  report.coreContract=coreContract;
  await page.screenshot({path:resolve(output,"boundary-section-editor.png")});
  await page.evaluate(()=>fixture.action("close"));
  assert.deepEqual(errors,[]);report.passed=true;report.previewAndLifecycle=true;report.previewNormalizationAndLaterInput=true;
  report.partReplacementAndReadRetryRelease=true;report.uniqueControlIds=true;report.latestMeshVersion=true;report.errors=errors;
} catch(error) {report.error=error.stack;report.diagnostic=await page?.evaluate(()=>({error:fixture.state?.error,dirty:fixture.state?.dirty,saving:fixture.state?.saving,previewPending:fixture.state?.previewPending,
  part:fixture.state?.part,applied:fixture.applied,calls:fixture.calls.map(call=>({method:call.method,revision:call.payload.revision})),resources:fixture.resourcesRequested}));throw error;}
finally {writeFileSync(resolve(output,"report.json"),JSON.stringify(report,null,2));await browser.close();}
console.log("PASS punch CAD editor controlled browser interaction, neutral draft merge, preview lifecycle and latest mesh");
