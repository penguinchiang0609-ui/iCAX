import { parameterVisible } from "./parameterConditions.mjs";
import { isTubeCadDocumentSnapshot, isTubeBoundaryGeometry } from "./tubeCadModel.mjs";
import { escapeAttr as esc, escapeText } from "../../_shared/workbench/utils/format.mjs";
import { libraryProfiles, profileRef, profileSelectionKey, renderProfileSvg,
  renderProfileParameterControl, renderProfileParameterGroups } from "./profileLibrary.mjs";
import { renderProfileParameterDiagram } from "./profileParameterDiagram.mjs";
import { libraryDiagramPositionStyle, renderDiagramResizeHandles } from "./floatingParameterDiagram.mjs";
import { restoreSavedNestingTask } from "./nestingWorkflow.mjs";
import { renderPartDrawingWorkbench } from "./partDrawingView.mjs";
import { bindPartDrawingLayout } from "./partDrawingLayout.mjs";
import { createDrawingPreviewKey, invokeDrawingPreview, invokeTubeCadDocumentPreview, releaseDrawingPreviewResources } from "./partDrawingSession.mjs";
import { createDrawingState, checkpointDrawing, getDrawingPayload, validateDrawing,
  createDrawingCadState, captureDrawingCadBaseline, getDrawingCadPayload,
  installDrawingCatalogue, drawingToolDescriptor, isDrawingToolReadOnly, updateDrawingField,
  addDrawingFeature, editDrawingFeature, normalizeDrawingFeature, selectDrawingTool, setDrawingArrayDimension,
  updateSelectedDrawingBevel, removeSelectedDrawingBevel, reselectDrawingBevel } from "./partDrawingModel.mjs";

const PREFIX="tube-designer-drawing-";
export const NEW_DRAWING_PART_ID="__new_drawn_part__";
const clone=value=>structuredClone(value);
const text=value=>typeof value==="object"?(value?.["zh-CN"]??value?.["en-US"]??""):String(value??"");
const profileName=p=>text(p?.name??p?.descriptor?.displayName??p?.previewProfile?.name)||"未命名截面";
const snapshot=p=>p?.previewProfile??p?.profile??(p?.contours?p:null);
const hasDrawingPreview=result=>result?.toolsOnly!==true && (!!result?.geometry?.url
  || !!result?.resultError && !!result?.topology?.revision
    && !!(result.cutBoundaryGeometry?.url || result.baseGeometry?.url));
const drawingState=view=>view?.tubeDesignerPartDrawing?.state;
const drawingOwned=(context,view,state)=>drawingState(view)===state&&!view.disposed&&context.isCurrentProject?.()!==false
  &&(!state.cadSession||view.scene===state.cadSession.scene&&context.sceneProxy===state.cadSession.sceneProxy);
const withDrawingState=(view,state)=>({...view,tubeDesignerPartDrawing:{...view.tubeDesignerPartDrawing,state}});
const paint=()=>typeof requestAnimationFrame==="function"?new Promise(resolve=>{
  let settled=false;const finish=()=>{if(!settled){settled=true;clearTimeout(timer);resolve();}};
  const timer=setTimeout(finish,100);requestAnimationFrame(()=>requestAnimationFrame(finish));
}):Promise.resolve();
export function beginDrawingOperation(context,view,title) {
  const operation={kind:"part-drawing",title,message:title,phaseLabel:"准备中"};
  view.tubeDesignerOperation=operation;view.pending=true;
  return {timeoutMs:180000,onReport(report) {
    if(view.tubeDesignerOperation!==operation)return;
    const data=report?.payload??report?.data??report;
    operation.message=String(data?.message??operation.message);
    const completed=Number(data?.completed??data?.current),total=Number(data?.total);
    operation.completed=Number.isFinite(completed)?completed:0;
    operation.total=Number.isFinite(total)&&total>0?total:null;
    operation.phaseLabel=operation.total?`${operation.completed} / ${operation.total}`:"正在计算";
    const mount=context.mount;
    const message=mount?.querySelector?.("[data-tube-designer-operation-message]");
    const phase=mount?.querySelector?.("[data-tube-designer-operation-phase]");
    if(message)message.textContent=operation.message;
    if(phase)phase.textContent=operation.phaseLabel;
    const track=mount?.querySelector?.(".tube-designer-operation-wait .tube-designer-export-progress-track");
    track?.setAttribute?.("aria-valuetext",operation.phaseLabel);
    if(track&&operation.total) {
      track.classList.remove("is-indeterminate");
      track.setAttribute("aria-valuemin","0");track.setAttribute("aria-valuemax",String(operation.total));
      const current=Math.max(0,Math.min(operation.total,operation.completed));
      track.setAttribute("aria-valuenow",String(current));
      const bar=track.querySelector("i");if(bar)bar.style.width=`${100*current/operation.total}%`;
    } else if(track) {
      track.classList.add("is-indeterminate");
      for(const name of ["aria-valuenow","aria-valuemin","aria-valuemax"])track.removeAttribute(name);
      const bar=track.querySelector("i");if(bar)bar.style.width="36%";
    }
  }};
}
export function finishDrawingOperation(view,expectedOperation=null) {
  if(expectedOperation&&view.tubeDesignerOperation!==expectedOperation)return false;
  const operation=expectedOperation??view.tubeDesignerOperation;
  if(operation?.renderHolds>0){operation.finishRequested=true;view.pending=true;return false;}
  if(operation?.kind==="part-drawing")view.tubeDesignerOperation=null;
  view.pending=false;return true;
}

export function defaultDrawingProfile(view) {
  const profiles=libraryProfiles(view).filter(profile=>snapshot(profile)?.contours?.length);
  return profiles.find(profile=>profile.libraryScope==="system"&&(profile.id==="round"||profile.descriptor?.id==="round"))
    ?? profiles.find(profile=>profile.libraryScope==="system")??profiles[0];
}
function sectionFromLibrary(p,view) {
  return {source:"library",key:profileSelectionKey(p),ref:profileRef(p),name:profileName(p),
    parameters:clone(snapshot(p)?.parameters??p.defaultParameters??{}),profile:clone(snapshot(p))};
}
function virtualPart(view) {
  const s=drawingState(view),d=s.drawing;
  return {entityId:s.partId,name:d.name,length:Number(d.length),profile:d.section?.profile};
}
function mainLocked(s) {
  if(s.cadSession)return s.cadSession.mainReadOnly;
  return [...s.features,...Object.values(s.ends)].some(f=>isDrawingToolReadOnly(s,f));
}
function drawingNodeIds(s) {
  return ["main",...["start","end"].filter(end=>s.ends?.[end]?.type!=="keep"),...s.features.map(feature=>feature.id),...(s.drawing.bevels??[]).map(bevel=>bevel.id)];
}
function adjacentDrawingNode(s,node) {
  const ids=drawingNodeIds(s),index=ids.indexOf(node);
  return ids[index+1]??ids[index-1]??"main";
}
function selectDrawingNode(view,node="main") {
  const s=drawingState(view),m=view.tubeDesignerPartDrawing;
  s.reselectBevelId="";
  if(node==="main") {
    m.mode="main";m.selected="main";s.editingId="";return true;
  }
  if(["start","end"].includes(node)) {
    if(s.ends[node]?.type==="keep")return false;
    m.mode=node;m.selected=node;s.draft=s.ends[node];s.editingId="";return true;
  }
  const feature=s.features.find(item=>item.id===node);
  if(!feature) {
    const preserved=s.cadSession?.readOnlyNodes.find(item=>item.nodeId===node);
    if(preserved){m.mode="preserved";m.selected=node;s.editingId="";return true;}
    const bevel=s.drawing.bevels?.find(item=>item.id===node);if(!bevel)return false;
    m.mode="bevel";m.selected=node;s.editingId="";
    s.topologySelection={kind:"edge",id:bevel.edgeIds[0],edgeIds:clone(bevel.edgeIds),
      topologyRevision:bevel.topologyRevision,adjacentFaceIds:bevel.referenceFaceId?[bevel.referenceFaceId]:[]};
    return true;
  }
  m.mode="feature";m.selected=node;s.draft=feature;s.editingId="";return true;
}
function getSection(view,which) {
  const s=drawingState(view);
  if(which==="main")return s.drawing.section;
  if(which==="start"||which==="end")return s.ends?.[which]?.section;
  return s.draft.section;
}
function setSection(view,which,section,shouldCheckpoint=true) {
  const s=drawingState(view);if(shouldCheckpoint)checkpointDrawing(s);
  if(which==="main")s.drawing.section=section;
  else if(which==="start"||which==="end")s.ends[which].section=section;
  else s.draft.section=section;
}
function sectionLocked(view,which) {
  const s=drawingState(view);
  if(which==="main")return mainLocked(s);
  if(which==="start"||which==="end")return isDrawingToolReadOnly(s,s.ends?.[which]);
  return isDrawingToolReadOnly(s,s.draft);
}
function drawingSectionParameters(current,section) {
  return {...Object.fromEntries((current?.descriptor?.parameters??[]).map(d=>[d.key,d.defaultValue])),
    ...current?.defaultParameters,...(section.pendingParameters??section.parameters)};
}
function drawingSectionDiagram(view,which) {
  const section=getSection(view,which)??{},current=libraryProfiles(view).find(p=>profileSelectionKey(p)===section.key);
  const definitions=current?.descriptor?.parameters??[];
  const parameters=drawingSectionParameters(current,section);
  const available=section.source!=="dxf" && !!section.profile && !!(definitions.length||section.profile.editableParameters);
  return {section,definitions,parameters,available};
}
function renderDrawingSectionDiagram(view) {
  const which=view.tubeDesignerPartDrawing?.sectionDiagram;
  if(!which)return "";
  const {section,definitions,parameters,available}=drawingSectionDiagram(view,which);
  if(!available)return "";
  const title=(which==="main"?"主管":which==="start"||which==="end"?"端部":which==="side"?"切除":"支管")+"参数示意图";
  return `<aside class="tube-library-diagram-dock" role="dialog" aria-modal="false" aria-label="${title}" data-library-floating-diagram="drawing-section" style="${libraryDiagramPositionStyle(view,"drawing-section")}">
    ${renderDiagramResizeHandles()}<header data-floating-diagram-drag><strong>${title}</strong><button type="button" data-cam-action="${PREFIX}section-diagram-close" aria-label="关闭示意图">×</button></header>
    <div class="tube-library-diagram-content" data-parameter-diagram-for="drawing-section:${esc(which)}">${renderProfileParameterDiagram(section.profile,{definitions:definitions.length?definitions:undefined,parameters,compact:true,title})}</div></aside>`;
}
export function renderDrawingSection(view,which) {
  const section=getSection(view,which)??{},profiles=libraryProfiles(view);
  const current=profiles.find(p=>profileSelectionKey(p)===section.key);
  const defs=current?.descriptor?.parameters??[];
  const values=drawingSectionParameters(current,section);
  const pending=!!(view.pending||sectionLocked(view,which)),disabled=pending?"disabled":"";
  const attrs=`data-drawing-section="${which}" ${disabled}`;
  const key=current?section.key:section.profile?"__snapshot__":"";
  const params=current?renderProfileParameterGroups(current,defs.filter(d=>parameterVisible(d,values)),values,pending,{
    keyPrefix:`drawing-section-${which}`,
    renderControl:(definition,parameters,locked)=>renderProfileParameterControl(definition,parameters,locked,{
      changeAction:`${PREFIX}section-parameter`,
      attributes:{"data-drawing-section":which,"data-drawing-parameter":definition.key},
    }),
  }):"";
  const sectionLabel=which==="main"?"主管":(which==="start"||which==="end"?"端部":which==="side"?"切除":"支管");
  const diagramAvailable=drawingSectionDiagram(view,which).available;
  return `<section class="tube-drawing-section${which==="start"||which==="end"?" is-end-section":""}" data-profile-parameter-scope data-parameter-diagram-owner="drawing-section:${esc(which)}"><div class="tube-designer-punch-field-grid">
    <div class="wide td-draw-section-choice"><div class="td-draw-section-label"><label for="td-draw-section-${which}">${sectionLabel}截面</label>${diagramAvailable?`<button type="button" data-cam-action="${PREFIX}section-diagram" data-drawing-section="${which}" aria-expanded="${view.tubeDesignerPartDrawing.sectionDiagram===which}" ${view.pending?"disabled":""}>示意图</button>`:""}</div><select id="td-draw-section-${which}" data-cam-change-action="${PREFIX}section-select" ${attrs}>
    <option value="">请选择截面</option>${section.profile?`<option value="__snapshot__" ${key==="__snapshot__"?"selected":""}>已保存截面 · ${escapeText(section.name??"本地 DXF")}</option>`:""}
    ${[["system","系统内置"],["template","模板自带"],["user","我的"]].map(([scope,title])=>`<optgroup label="${title}">${profiles.filter(p=>p.libraryScope===scope).map(p=>`<option value="${esc(profileSelectionKey(p))}" ${key===profileSelectionKey(p)?"selected":""}>${escapeText(profileName(p))}${p.templateName?" · "+escapeText(p.templateName):""}</option>`).join("")}</optgroup>`).join("")}
    <option value="__dxf__">外部 DXF…</option></select></div></div>${params?`<div class="td-draw-section-parameters">${params}</div>`:""}
    ${section.pendingParameters?'<small class="tube-drawing-warning">正在更新截面参数…</small>':""}
    ${section.profile&&!diagramAvailable?`<div class="tube-drawing-section-preview">${renderProfileSvg(section.profile)}</div>`:""}
    </section>`;
}
export function renderPartDrawingDialog(view) {
  return renderPartDrawingWorkbench(view,renderDrawingSection,renderDrawingSectionDiagram);
}
export function openPartDrawing(view) {
  const previous=drawingState(view);if(previous)void releaseDrawingPreviewResources(previous);
  view.tubeDesignerPartDrawing={mainApplied:true,mode:"main",selected:"main"};
  const profiles=libraryProfiles(view),p=profiles.find(p=>profileSelectionKey(p)===view.tubeDesignerSelectedProfileId)
    ??defaultDrawingProfile(view)??profiles[0];
  view.tubeDesignerPartDrawing.state=createDrawingState({entityId:NEW_DRAWING_PART_ID,length:500});
  const s=drawingState(view);
  s.previewMode="finished";
  s.previewResourceKey=createDrawingPreviewKey();
  s.drawing={length:500,name:"三维绘制零件",quantity:1,material:"",section:p?sectionFromLibrary(p,view):{}};
  if(!s.drawing.section?.profile?.contours?.length&&p)s.drawing.section=sectionFromLibrary(p,view);
  s.baseLength=Number(s.drawing.length);
}

export async function openPartDrawingCad(context,view,part,ops) {
  if(!part?.properties?.["tubeDesigner.cadDocument"]?.documentResourceId)
    throw new Error("此零件没有可编辑的管材 CAD 文档。");
  if(!context.sceneProxy?.invoke)throw new Error("当前项目未连接，无法打开三维编辑。");
  const previous=drawingState(view);if(previous)void releaseDrawingPreviewResources(previous);
  const scene=view.scene,sceneProxy=context.sceneProxy;
  const state=createDrawingState(part);
  state.drawing={length:part.length,name:part.name,quantity:part.quantity??1,material:part.material??"",section:{}};
  state.catalogueStatus="loading";state.previewResourceKey=createDrawingPreviewKey();
  const meta={mainApplied:true,mode:"main",selected:"main",state,loading:true,savedPartId:part.entityId};
  view.tubeDesignerPartDrawing=meta;
  const options=beginDrawingOperation(context,view,"正在读取三维 CAD 文档"),operation=view.tubeDesignerOperation;
  renderDrawing(context,view,ops);await paint();
  const owned=()=>view.tubeDesignerPartDrawing===meta&&view.scene===scene&&context.sceneProxy===sceneProxy
    &&!view.disposed&&context.isCurrentProject?.()!==false;
  try {
    const document=await sceneProxy.invoke("TubeDesigner.GetTubeCadDocument",{partEntityId:part.entityId,
      resourceVersion:part.manufacturingGeometryResourceVersion,authoringView:"drawing"},options);
    if(!owned())return;
    if(!isTubeCadDocumentSnapshot(document))throw new Error("没有返回有效的管材边界文档。");
    meta.state=createDrawingCadState(part,document);
    meta.state.previewResourceKey=state.previewResourceKey;
    Object.assign(meta.state.cadSession,{scene,sceneProxy,geometry:clone(document.geometry),
      construction:clone(document.construction),placement:clone(document.placement)});
  } catch(error) {
    if(!owned())return;
    meta.loadError=true;state.catalogueStatus="error";state.error=error?.message??String(error);
    throw error;
  } finally {
    meta.loading=false;finishDrawingOperation(view,operation);
    if(owned())renderDrawing(context,view,ops);
  }
  return meta;
}

function savedSection(section) {
  if(!section?.profile?.contours?.length)throw new Error("请先选择有效截面。");
  if(section.pendingParameters)throw new Error("截面参数正在更新，请等待更新完成。");
  const {pendingParameters,...saved}=section;return clone(saved);
}
export function getPartDrawingPayload(view,{includeDraft=false}={}) {
  const activeId=view.tubeDesignerPartDrawing?.selected;
  const activeAlreadyPersisted=activeId&&drawingState(view)?.features?.some(feature=>feature.id===activeId);
  if(includeDraft && view.tubeDesignerPartDrawing?.mode==="feature"&&!activeAlreadyPersisted) {
    const s=drawingState(view),f=normalizeDrawingFeature(s.draft);
    if(f.section)f.section=savedSection(f.section);
    const i=s.features.findIndex(item=>item.id===s.editingId);
    const features=i<0?[...s.features,f]:s.features.map((item,n)=>n===i?{...f,id:item.id}:item);
    return getPartDrawingPayload(withDrawingState(view,{...s,features,editingId:""}));
  }
  const s=drawingState(view),d=s.drawing,length=Number(d.length);
  if(!Number.isFinite(length)||length<1||length>100000)throw new Error("主管长度须为 1 至 100000 mm。");
  const error=validateDrawing(s);if(error)throw new Error(error);
  const payload=getDrawingPayload(s);
  payload.drawing={...clone(d),schemaVersion:1,length,section:savedSection(d.section)};
  for(const key of ["name","quantity","material"])delete payload.drawing[key];
  for(const f of payload.features)if(f.section)f.section=savedSection(f.section);
  {
    const name=String(d.name??"").trim(),quantity=Number(d.quantity),material=String(d.material??"").trim();
    if(!name||name.length>160)throw new Error("请填写零件名称（最多 160 个字符）。");
    if(!Number.isSafeInteger(quantity)||quantity<1||quantity>1000000)throw new Error("数量须为 1 至 1000000 的整数。");
    if(material.length>240)throw new Error("材料名称不能超过 240 个字符。");
    Object.assign(payload,{name,quantity,material});
  }
  return payload;
}
async function resolveSection(context,view,which,key,ops,shouldCheckpoint=true) {
  if(sectionLocked(view,which))throw new Error("退化定式刀具仅可删除，不能修改其截面或主管。");
  const current=getSection(view,which)??{},state=drawingState(view);
  if(key==="__snapshot__") {const restored=clone(current);delete restored.pendingParameters;delete restored.key;setSection(view,which,restored);return;}
  const options=beginDrawingOperation(context,view,"正在读取精确截面"),operation=view.tubeDesignerOperation;
  renderDrawing(context,view,ops);await paint();
  try {
    let section;
    if(key==="__dxf__") {
      const bridge=context.appProxy?.bridge??context.productProxy?.bridge??context.sceneProxy?.bridge;
      if(!bridge?.openFileDialog)throw new Error("当前宿主没有提供文件选择能力。");
      const sourcePath=String(await bridge.openFileDialog({title:which==="main"?"选择主管截面":"选择端部截面",filters:[{name:"DXF 二维截面",extensions:["dxf"]}]})??"").trim();
      if(!sourcePath)return;
      if(!/\.dxf$/i.test(sourcePath))throw new Error("请选择 DXF 截面文件。");
      const result=await context.productProxy.invoke("TubeDesigner.ImportProfileDxf",{sourcePath},options);
      section={source:"dxf",name:result.profile?.name??sourcePath.split(/[\\/]/).at(-1),profile:result.profile};
    } else {
      const p=libraryProfiles(view).find(p=>profileSelectionKey(p)===key);
      if(!p)throw new Error("该管型当前不可用，请选择已保存截面或重新导入 DXF。");
      const parameters=key===current.key?(current.pendingParameters??current.parameters??p.defaultParameters??{}):(p.defaultParameters??{});
      const result=await context.productProxy.invoke("TubeDesigner.EvaluateProfilePackage",{profileRef:profileRef(p),parameters},options);
      section={...sectionFromLibrary(p,view),parameters:clone(parameters),profile:result.profile};
    }
    if(!drawingOwned(context,view,state))return;
    savedSection(section);setSection(view,which,section,shouldCheckpoint);
  } catch(error) {
    if(drawingOwned(context,view,state))throw error;
  } finally {finishDrawingOperation(view,operation);}
}
async function applyDrawing(context,view,ops) {
  const meta=view.tubeDesignerPartDrawing,state=meta.state,cad=state.cadSession;
  if(meta.savedPartId&&!cad)throw new Error("当前零件的三维 CAD 文档尚未读取完成。");
  const payload=cad?getDrawingCadPayload(state):getPartDrawingPayload(view);
  const options=beginDrawingOperation(context,view,cad?"正在保存三维 CAD 修改":"正在生成三维零件"),operation=view.tubeDesignerOperation;
  renderDrawing(context,view,ops);await paint();
  try {
    const result=await context.sceneProxy.invoke(cad?"TubeDesigner.ApplyTubeCadDocument":"TubeDesigner.AddPartDrawing",payload,options);
    if(view.tubeDesignerPartDrawing!==meta||cad&&(view.scene!==cad.scene||context.sceneProxy!==cad.sceneProxy||view.disposed||context.isCurrentProject?.()===false))return;
    if(cad&&!isTubeCadDocumentSnapshot(result))throw new Error("三维修改没有返回有效的管材边界文档。");
    const id=String(cad?state.partId:result.partEntityId??"");
    const snapshot=cad?result.snapshot?.tubeDesigner:result.tubeDesigner;
    if(!snapshot||!id)throw new Error("没有收到保存后的零件记录。");
    view.scene??={};view.scene.tubeDesigner=snapshot;await restoreSavedNestingTask(view,context);
    if(!drawingOwned(context,view,state))return;
    view.tubeDesignerNestingSelectedPartIds=[id];view.tubeDesignerActivePartId=id;view.tubeDesignerActiveNestingPartId=id;
    view.tubeDesignerNestingSelectionKind="part";view.tubeDesignerActiveNestingPlacementId="";
    view.tubeDesignerPartMeasurementState=null;view.tubeDesignerPartViewportKey="";view.tubeDesignerNestingSettingsSourceSignature="";
    view.tubeDesignerPartDrawing=null;
    void releaseDrawingPreviewResources(meta.state);
    await context.actions?.refreshActiveSceneState?.();
    ops.showNotice?.(context,view,cad?"三维 CAD 修改已保存。":"三维零件已保存到下料区，可继续编辑、排样或导出。");
  } catch(error) {
    if(drawingOwned(context,view,state))throw error;
  } finally {finishDrawingOperation(view,operation);}
}
const previewQueues=new WeakMap();
function renderDrawing(context,view,ops) {
  if(view.disposed||context.isCurrentProject?.()===false)return;
  context.mount?.querySelector?.(".td-draw-property-scroll")?._captureDrawingLayout?.();
  ops.renderProject(context,view);
}
function attachDrawingInspector(view,mount) {
  const m=view.tubeDesignerPartDrawing,s=drawingState(view);
  const root=mount?.querySelector?.(".td-draw-property-scroll");
  if(!m||!s||!root)return;
  const key=m.mode+":"+(s.editingId||s.draft?.toolRef?.id||"new");
  if(root._drawingBinding?.meta===m&&root._drawingBinding.key===key)return;
  root._drawingBinding?.abort.abort();
  const Controller=root.ownerDocument?.defaultView?.AbortController??AbortController;
  const abort=new Controller();
  root._drawingBinding={meta:m,key,abort};
  m.panels??={};const panel=m.panels[key]??={scroll:0,groups:{}};
  for(const detail of root.querySelectorAll("details")) {
    const name=detail.querySelector("summary")?.textContent??"";
    if(name in panel.groups)detail.open=panel.groups[name];
  }
  root.addEventListener("toggle",event=>{
    const detail=event.target;
    if(root.isConnected&&detail?.matches?.("details"))
      panel.groups[detail.querySelector("summary")?.textContent??""]=detail.open;
  },{capture:true,signal:abort.signal});
  root.scrollTop=panel.scroll;
  root._captureDrawingLayout=()=>{
    panel.scroll=root.scrollTop;
    for(const detail of root.querySelectorAll("details"))panel.groups[detail.querySelector("summary")?.textContent??""]=detail.open;
  };
  root.addEventListener("scroll",()=>{if(root.isConnected)panel.scroll=root.scrollTop;},{passive:true,signal:abort.signal});
}
function drawingPreviewPayload(view) {
  // Features are committed to the local drawing state as soon as they are
  // created or edited. Preview therefore always consumes the same complete
  // recipe that the final save will use; there is no draft-only preview path.
  return {...(drawingState(view).cadSession?getDrawingCadPayload(drawingState(view)):getPartDrawingPayload(view)),toolsOnly:false,individualToolPreviews:true,includeTopology:true,
    previewResourceKey:drawingState(view).previewResourceKey};
}
// One native evaluation at a time. Coalesce edits and never display a stale response.
export function attachPartDrawingEditor(context,view,mount,ops) {
  bindPartDrawingLayout(view,mount);
  attachDrawingInspector(view,mount);
  const s=drawingState(view),m=view.tubeDesignerPartDrawing;
  if(!s||!m||!context.sceneProxy?.invoke)return;
  if(!drawingOwned(context,view,s))return;
  if(s.catalogueStatus==="idle"&&!view.pending) {
    s.catalogueStatus="loading";
    const options=beginDrawingOperation(context,view,"正在加载三维绘制工具"),operation=view.tubeDesignerOperation;
    renderDrawing(context,view,ops);
    void paint().then(()=>context.sceneProxy.invoke("TubeDesigner.GetPartDrawingTools",{},options)).then(result=>{
      if(drawingOwned(context,view,s)){
        installDrawingCatalogue(s,result);
        if(s.cadSession&&!s.cadSession.baselineCatalogueReady){captureDrawingCadBaseline(s);s.cadSession.baselineCatalogueReady=true;}
      }
    }).catch(error=>{
      if(drawingOwned(context,view,s)){s.catalogueStatus="error";s.error=error?.message??String(error);}
    }).finally(()=>{
      finishDrawingOperation(view,operation);
      if(drawingOwned(context,view,s))renderDrawing(context,view,ops);
    });
    return;
  }
  if((view.pending&&!s.previewPending)||s.catalogueStatus!=="ready")return;
  let q=previewQueues.get(view);
  if(!q||q.state!==s){q={state:s,signature:"",running:false,timer:null};previewQueues.set(view,q);}
  let payload;
  try {payload=drawingPreviewPayload(view);} catch(error) {
    clearTimeout(q.timer);q.signature="";q.payload=null;
    s.pendingPreviewRecipe=null;
    const message=error?.message??String(error);
    s.previewComputeError={revision:s.revision,message};
    if(s.error!==message){s.error=message;renderDrawing(context,view,ops);}
    return;
  }
  const signature=JSON.stringify(payload);
  if(q.signature===signature) {
    if(!q.running&&m.validSignature===signature&&s.preview&&s.preview.revision!==s.revision) {
      s.preview.revision=s.revision;renderDrawing(context,view,ops);
    }
    return;
  }
  q.signature=signature;q.payload=payload;
  q.presentationRecipe=s.cadSession?{...clone(payload),drawing:clone(s.drawing),features:clone(s.features),ends:clone(s.ends)}:clone(payload);
  s.pendingPreviewRecipe=clone(q.presentationRecipe);
  clearTimeout(q.timer);
  const run=async()=>{
    if(q.running)return;
    if(!drawingOwned(context,view,s)||view.tubeDesignerPartDrawing!==m||view.pending||!q.payload)return;
    const current=q.signature,body=clone(q.payload),presentationRecipe=clone(q.presentationRecipe),revision=s.revision;
    q.running=true;s.previewPending=true;s.error="";s.previewComputeError=null;s.previewPhase="正在更新三维成品";
    const options={timeoutMs:180000,onReport(report) {
      if(!drawingOwned(context,view,s)||q.signature!==current)return;
      const data=report?.payload??report?.data??report;
      s.previewPhase=String(data?.message??s.previewPhase);
      const label=context.mount?.querySelector?.("[data-part-drawing-preview-notice] > span");
      if(label)label.textContent=s.previewPhase;
      context.mount?.querySelector?.(".td-draw-preview-progress")?.setAttribute?.("aria-valuetext",s.previewPhase);
    }};
    renderDrawing(context,view,ops);
    try {
      await paint();
      const result=await (s.cadSession?invokeTubeCadDocumentPreview:invokeDrawingPreview)(context.sceneProxy,s,body,options);
      if(!hasDrawingPreview(result))throw new Error(result?.resultError||"未返回可显示的三维成品预览。");
      if(s.cadSession&&(!isTubeBoundaryGeometry(result.documentGeometry)||!Array.isArray(result.documentConstruction?.SolidNodes)))
        throw new Error("三维预览没有返回有效的管材边界文档。");
      if(drawingOwned(context,view,s)&&q.signature===current&&s.revision===revision
        &&body.previewResourceKey===s.previewResourceKey) {
        s.preview={...result,revision,includesDraft:false};m.validSignature=current;
        if(result.resultError){s.error=result.resultError;s.previewComputeError={revision,message:result.resultError};}
        s.previewRecipe=presentationRecipe;
      }
    } catch(error) {
      if(drawingOwned(context,view,s)&&q.signature===current) {
        s.error=error?.message??String(error);s.previewComputeError={revision,message:s.error};
      }
    } finally {
      q.running=false;
      if(drawingOwned(context,view,s)) {
        s.previewPending=false;renderDrawing(context,view,ops);
        if(q.payload&&q.signature!==current)q.timer=setTimeout(run,180);
      }
    }
  };
  q.timer=setTimeout(run,240);
}
function cancelOperation(view) {
  const s=drawingState(view),m=view.tubeDesignerPartDrawing;
  if(!s||!m)return;
  s.editingId="";s.reselectBevelId="";m.mode="";s.error="";
}
function beginOperation(view,command,node=null) {
  const s=drawingState(view),m=view.tubeDesignerPartDrawing;
  if(!s||!m)throw new Error("三维绘制未打开。");
  s.reselectBevelId="";
  if(command!=="main"&&!m.mainApplied)throw new Error("请先建立主管。");
  if(node) {
    if(!selectDrawingNode(view,node))throw new Error("该特征已不存在。");
    return;
  }
  if(command==="main"){m.mode="main";m.selected="main";s.editingId="";return;}
  if(["start","end"].includes(command)) {
    const end=s.ends[command]??{type:"keep"};
    if(end.type==="keep") {
      if(!s.tools.some(tool=>tool.id==="end-miter"))throw new Error("该刀具尚未加载，请稍后重试。");
      checkpointDrawing(s);selectDrawingTool(s,end,"end-miter");
      Object.assign(end,{trim:0,rotation:0,datum:"long"});s.ends[command]=end;
    }
    m.mode=command;m.selected=command;s.draft=end;s.editingId="";return;
  }
  const feature=normalizeDrawingFeature({station:s.baseLength/2});
  const id=command==="branch"?"branch-profile":command==="hole"?s.tools.find(t=>t.target==="side")?.id:command==="part"?s.tools.find(t=>t.target==="part"&&!t.requiresSection)?.id:command;
  if(!s.tools.some(tool=>tool.id===id))throw new Error("该刀具尚未加载，请稍后重试。");
  selectDrawingTool(s,feature,id);
  if(command==="branch") {
    const profile=defaultDrawingProfile(view);
    if(profile)feature.section=sectionFromLibrary(profile,view);
  }
  s.draft=feature;s.editingId="";
  if(!addDrawingFeature(s,virtualPart(view))) {
    if(feature.recordKind!=="branch"||!/支管截面/.test(s.error))throw new Error(s.error);
    // Keep an incomplete branch editable when the profile catalogue is empty;
    // selecting a section in the inspector will make the live recipe valid.
    s.error="";checkpointDrawing(s);s.features=[...s.features,clone(feature)];
  }
  s.draft=s.features.at(-1);m.mode="feature";m.selected=s.draft.id;
}
export async function handlePartDrawingAction(context,view,action,target,ops) {
  if(!action.startsWith(PREFIX))return {handled:false};
  if(view.pending)return {handled:true};
  const suffix=action.slice(PREFIX.length);
  const initialMeta=view.tubeDesignerPartDrawing,scene=view.scene,sceneProxy=context.sceneProxy;
  const savedOperation=suffix==="open"?!!target?.dataset?.tubeDesignerPartId:!!(initialMeta?.savedPartId||initialMeta?.state?.cadSession);
  let superseded=false;
  const owned=()=>!superseded&&(!savedOperation||view.scene===scene&&context.sceneProxy===sceneProxy
    &&(suffix==="open"||view.tubeDesignerPartDrawing===initialMeta||view.tubeDesignerPartDrawing===null));
  try {
    if(suffix==="open") {
      const partId=target?.dataset?.tubeDesignerPartId;
      if(partId) {
        const part=view.scene?.tubeDesigner?.nestingGroups?.flatMap(group=>group.parts??[]).find(item=>String(item.entityId)===String(partId));
        if(!part)throw new Error("当前下料零件已不存在，请重新选择。");
        if(!await openPartDrawingCad(context,view,part,ops))superseded=true;
      } else openPartDrawing(view);
    } else if(suffix==="cancel") {
      const previous=drawingState(view);view.tubeDesignerPartDrawing=null;
      if(previous)void releaseDrawingPreviewResources(previous);
    }
    else if(view.tubeDesignerPartDrawing) {
      const s=drawingState(view),m=view.tubeDesignerPartDrawing,which=target?.dataset?.drawingSection;
      if(s.cadSession&&!s.cadSession.bevelEditable&&suffix.startsWith("bevel-"))throw new Error("当前三维编辑暂不支持新增或修改坡口，原有坡口已保留。");
      if(suffix==="section-diagram")m.sectionDiagram=m.sectionDiagram===which?"":which;
      else if(suffix==="section-diagram-close")m.sectionDiagram="";
      else if(suffix==="command") {m.sectionDiagram="";beginOperation(view,target?.dataset?.drawingCommand);}
      else if(suffix==="select-node") {
        m.sectionDiagram="";
        const node=target?.dataset?.drawingNode;beginOperation(view,["main","start","end"].includes(node)?node:"feature",node);
      }
      else if(suffix.startsWith("selected-")) {
        const kind=suffix.slice(9);
        if(kind==="remove"&&m.selected==="main")throw new Error("主管不可删除。");
        const index=s.features.findIndex(f=>f.id===m.selected);
        const selectedBevel=s.drawing.bevels?.find(item=>item.id===m.selected);
        if(selectedBevel&&kind==="remove") {
          checkpointDrawing(s);s.drawing.bevels=s.drawing.bevels.filter(item=>item.id!==selectedBevel.id);
          s.topologySelection=null;selectDrawingNode(view,"main");
        }
        else if(index>=0) {
          const selected=s.features[index];
          const nextAfterRemove=kind==="remove"?adjacentDrawingNode(s,selected.id):null;
          if(kind==="copy") {
            if(isDrawingToolReadOnly(s,selected))throw new Error("退化定式刀具仅可删除。");
            const copy=normalizeDrawingFeature({...clone(selected),id:undefined,station:selected.station+(selected.arrayPitch||50),enabled:true});delete copy.cadNodeId;
            checkpointDrawing(s);s.features=[...s.features,copy];s.draft=s.features.at(-1);s.editingId="";
            m.mode="feature";m.selected=s.draft.id;
          } else if(editDrawingFeature(s,kind,index)) {
            if(kind==="remove") {
              if(!selectDrawingNode(view,nextAfterRemove))selectDrawingNode(view,"main");
              s.editingId="";
            }
            else {m.mode="feature";m.selected=selected.id;s.draft=s.features.find(f=>f.id===selected.id)??s.draft;s.editingId="";}
          }
        } else if(kind==="remove"&&["start","end"].includes(m.selected)) {
          const next=adjacentDrawingNode(s,m.selected);
          editDrawingFeature(s,"remove-end",m.selected);
          if(!selectDrawingNode(view,next))selectDrawingNode(view,"main");
          s.editingId="";
        } else if(kind==="remove") {
          selectDrawingNode(view,"main");
        }
      }
      else if(suffix==="section-select"||suffix==="section-resolve") {
        await resolveSection(context,view,which,suffix==="section-select"?String(target.value):getSection(view,which)?.key,ops);
      }
      else if(suffix==="section-parameter") {
        if(sectionLocked(view,which))throw new Error("该截面只读。");
        const section=getSection(view,which),p=libraryProfiles(view).find(p=>profileSelectionKey(p)===section?.key);
        const key=target.dataset.drawingParameter,def=p?.descriptor?.parameters?.find(d=>d.key===key);
        if(def){checkpointDrawing(s);section.pendingParameters??=clone(section.parameters);section.pendingParameters[key]=def.valueType==="string"?String(target.value):def.valueType==="boolean"?!!target.checked:target.value===""?NaN:Number(target.value);await resolveSection(context,view,which,section.key,ops,false);}
      } else if(suffix==="main-change") {
        const key=target?.dataset?.drawingField;
        if(["length","name","quantity","material"].includes(key)) {
          if(key==="length"&&mainLocked(s))throw new Error("存在退化定式刀具，不能修改主管长度。");
          checkpointDrawing(s);s.drawing[key]=target.value;if(key==="length")s.baseLength=Number(target.value);
        }
      } else if(suffix==="field-change") {
        const field=target?.dataset?.tubeDesignerPunchField;
        if(m.mode!=="feature"&&!['start','end'].includes(m.mode))throw new Error("请先选择要编辑的特征。");
        if(["start","end"].includes(m.mode))s.draft=s.ends[m.mode];
        if(field==="arrayDimension") {
          if(isDrawingToolReadOnly(s,s.draft))throw new Error("退化定式刀具仅可删除，不能修改阵列。");
          if(!["none","one","two"].includes(target.value))throw new Error("请选择无、一维阵列或二维阵列。");
          checkpointDrawing(s);setDrawingArrayDimension(s.draft,target.value);
        } else if(["drawingArrayMode","arrayDirection","rowDirection","arraySpacing","rowSpacing"].includes(field)) {
          if(isDrawingToolReadOnly(s,s.draft))throw new Error("退化定式刀具仅可删除，不能修改阵列。");
          if(field==="drawingArrayMode"&&(s.draft.toolTarget!=="part"||!["top","left","round"].includes(target.value)))throw new Error("请选择 Y、Z 或绕 X 轴圆周方向。");
          if(field.endsWith("Direction")&&!["positive","negative"].includes(target.value))throw new Error("请选择正向或反向。");
          const spacing=Number(target.value);
          if(field.endsWith("Spacing")&&(!Number.isFinite(spacing)||spacing<=0))throw new Error("间距须为正数，反向排列请选择方向。");
          checkpointDrawing(s);
          const f=s.draft,sign=target.value==="negative"?-1:1;
          if(field==="drawingArrayMode")f.face=target.value;
          else if(field==="arrayDirection")f.arrayPitch=(Math.abs(f.arrayPitch)||50)*sign*(f.reference==="end"?-1:1);
          else if(field==="rowDirection")f.rowPitch=(Math.abs(f.rowPitch)||20)*sign;
          else if(field==="arraySpacing")f.arrayPitch=spacing*(f.arrayPitch<0?-1:1);
          else if(field==="rowSpacing")f.rowPitch=spacing*(f.rowPitch<0?-1:1);
        } else {
          const oldReference=s.draft.reference;
          if(updateDrawingField(s,target)) {
            // UI directions are absolute axes, independent of the position datum.
            if(field==="reference"&&(oldReference==="end")!==(s.draft.reference==="end"))s.draft.arrayPitch=-s.draft.arrayPitch;
          }
        }
      }
      else if(suffix==="preview-mode") {
        const mode=target?.dataset?.drawingPreviewMode;
        if(["finished","base","tools"].includes(mode))s.previewMode=mode;
      }
      else if(suffix==="selection-mode") {
        if(["edge","face"].includes(target?.value)){s.selectionMode=target.value;s.topologySelection=null;}
      }
      else if(suffix==="clear-topology")s.topologySelection=null;
      else if(suffix==="bevel-reselect") {
        const definition=s.drawing.bevels?.find(item=>item.id===m.selected);
        if(definition){s.reselectBevelId=definition.id;s.topologySelection=null;}
      }
      else if(suffix==="bevel-use-selection") {
        const id=s.reselectBevelId;
        if(reselectDrawingBevel(s)){m.mode="bevel";m.selected=id;s.reselectBevelId="";}
      }
      else if(suffix==="bevel-cancel-reselect") {
        const id=s.reselectBevelId;s.reselectBevelId="";selectDrawingNode(view,id);
      }
      else if(suffix==="bevel-remove") {
        if(removeSelectedDrawingBevel(s)){s.topologySelection=null;selectDrawingNode(view,"main");}
      }
      else if(suffix==="bevel-reference-face")updateSelectedDrawingBevel(s,"referenceFaceId",target);
      else if(suffix==="bevel-change") {
        const field=target?.dataset?.cadBevelField;
        if(["enabled","side","angleDegrees","rootFace"].includes(field))updateSelectedDrawingBevel(s,field,target);
      }
      else if(["edit","copy","toggle","remove","undo","redo","remove-end"].includes(suffix)) {
        if(["undo","redo"].includes(suffix)) {
          cancelOperation(view);
          if(editDrawingFeature(s,suffix,target?.dataset?.tubeDesignerPunchIndex)) {
            m.mode="main";m.selected="main";s.editingId="";s.topologySelection=null;
          }
        } else if(editDrawingFeature(s,suffix,target?.dataset?.tubeDesignerPunchIndex)) {
          if(suffix==="remove-end") {m.mode="main";m.selected="main";}
          else {m.mode="feature";const feature=s.features.find(item=>item.id===m.selected);if(feature)s.draft=feature;}
          s.editingId="";
        }
      }
      else if(suffix==="preview") {
        // Re-evaluate the CAD recipe through this editor's own serial queue.
        s.previewRenderError="";s.error="";m.validSignature="";
        if(s.catalogueStatus==="error")s.catalogueStatus="idle";
        const q=previewQueues.get(view);
        if(q)q.signature="";
      } else if(suffix==="apply")await applyDrawing(context,view,ops);
    }
  } catch(error) {
    if(owned()) {
      if(drawingState(view))drawingState(view).error=error?.message??String(error);else throw error;
    }
  } finally {
    if(owned())renderDrawing(context,view,ops);
  }
  return {handled:true};
}
export async function handlePartDrawingRibbonCommand(context,view,command,ops) {
  if(command!=="nesting.draw-part")return false;
  await handlePartDrawingAction(context,view,PREFIX+"open",null,ops);return true;
}
