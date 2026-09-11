import { escapeAttr as esc, escapeText } from "../../_shared/workbench/utils/format.mjs";
import { libraryProfiles, profileRef, profileSelectionKey, renderProfileSvg } from "./profileLibrary.mjs";
import { renderProfileParameterDiagram } from "./profileParameterDiagram.mjs";
import { listNestingParts } from "./partsArea.mjs";
import { restoreSavedNestingTask } from "./nestingWorkflow.mjs";
import { renderPartDrawingWorkbench } from "./partDrawingView.mjs";
import { createDrawingState, checkpointDrawing, getDrawingPayload, validateDrawing,
  installDrawingCatalogue, drawingToolDescriptor, isDrawingToolReadOnly, updateDrawingField,
  addDrawingFeature, editDrawingFeature, normalizeDrawingFeature, selectDrawingTool } from "./partDrawingModel.mjs";

const PREFIX="tube-designer-drawing-";
export const NEW_DRAWING_PART_ID="__new_drawn_part__";
const clone=value=>structuredClone(value);
const text=value=>typeof value==="object"?(value?.["zh-CN"]??value?.["en-US"]??""):String(value??"");
const profileName=p=>text(p?.name??p?.descriptor?.displayName??p?.previewProfile?.name)||"未命名截面";
const snapshot=p=>p?.previewProfile??p?.profile??(p?.contours?p:null);
const hasDrawingToolPreview=result=>result?.toolsOnly===true && !!result?.baseGeometry?.url
  && !result.resultError && result.previewToolsComplete!==false;
const drawingState=view=>view?.tubeDesignerPartDrawing?.state;
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
  const s=drawingState(view),d=s.drawing,meta=view.tubeDesignerPartDrawing;
  return {...meta.part,entityId:meta.part?.entityId??NEW_DRAWING_PART_ID,name:d.name,length:Number(d.length),profile:d.section?.profile};
}
function mainLocked(s) {
  return [...s.features,...Object.values(s.ends)].some(f=>isDrawingToolReadOnly(s,f));
}
function drawingNodeIds(s) {
  return ["main",...["start","end"].filter(end=>s.ends?.[end]?.type!=="keep"),...s.features.map(feature=>feature.id)];
}
function adjacentDrawingNode(s,node) {
  const ids=drawingNodeIds(s),index=ids.indexOf(node);
  return ids[index+1]??ids[index-1]??"main";
}
function selectDrawingNode(view,node="main") {
  const s=drawingState(view),m=view.tubeDesignerPartDrawing;
  if(node==="main") {
    m.mode="main";m.selected="main";s.editingId="";return true;
  }
  if(["start","end"].includes(node)) {
    if(s.ends[node]?.type==="keep")return false;
    m.mode=node;m.selected=node;s.draft=s.ends[node];s.editingId="";s.previewMode="tools";return true;
  }
  const feature=s.features.find(item=>item.id===node);
  if(!feature)return false;
  m.mode="feature";m.selected=node;s.draft=feature;s.editingId="";s.previewMode="tools";return true;
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
function visibleParameter(d,values) {
  const test=c=>!c?true:c.conditions? (c.op==="any"?c.conditions.some(test):c.conditions.every(test))
    :c.all?c.all.every(test):c.any?c.any.some(test):c.op==="eq"?values[c.parameter??c.key]===c.value:c.op==="ne"?values[c.parameter??c.key]!==c.value:true;
  return test(d.visibleWhen);
}
export function renderDrawingSection(view,which) {
  const section=getSection(view,which)??{},profiles=libraryProfiles(view);
  const current=profiles.find(p=>profileSelectionKey(p)===section.key),values=section.pendingParameters??section.parameters??{};
  const disabled=view.pending||sectionLocked(view,which)?"disabled":"";
  const attrs=`data-drawing-section="${which}" ${disabled}`;
  const key=current?section.key:section.profile?"__snapshot__":"";
  const defs=current?.descriptor?.parameters??[];
  const params=defs.filter(d=>visibleParameter(d,values)).map(d=>{
    const value=values[d.key]??d.defaultValue;
    const a=`data-cam-change-action="${PREFIX}section-parameter" ${attrs} data-drawing-parameter="${esc(d.key)}" data-profile-parameter-key="${esc(d.key)}"`;
    const options=d.options??[];
    const control=options.length?`<select ${a}>${options.map(o=>{const v=typeof o==="object"?o.value:o;return `<option value="${esc(v)}" ${String(v)===String(value)?"selected":""}>${escapeText(text(o.displayName??o.label??v))}</option>`;}).join("")}</select>`
      :`<input ${a} type="${d.valueType==="boolean"?"checkbox":d.valueType==="string"?"text":"number"}" ${d.valueType==="boolean"?(value?"checked":""):`value="${esc(value)}"`} step="${d.valueType==="integer"?1:"any"}"/>`;
    return `<label><span>${escapeText(text(d.displayName??d.key))}</span>${control}</label>`;
  }).join("");
  const diagram = section.source !== "dxf" && (defs.length || section.profile?.editableParameters)
    ? renderProfileParameterDiagram(section.profile, { definitions: defs.length ? defs : undefined, parameters: values, compact: true, title: `${which === "main" ? "主管" : "支管"}参数示意图` }) : "";
  const sectionLabel=which==="main"?"主管":(which==="start"||which==="end"?"端部":"支管");
  return `<section class="tube-drawing-section${which==="start"||which==="end"?" is-end-section":""}" data-profile-parameter-scope><div class="tube-designer-punch-field-grid">
    <label class="wide"><span>${sectionLabel}截面</span><select data-cam-change-action="${PREFIX}section-select" ${attrs}>
    <option value="">请选择截面</option>${section.profile?`<option value="__snapshot__" ${key==="__snapshot__"?"selected":""}>已保存截面 · ${escapeText(section.name??"本地 DXF")}</option>`:""}
    ${[["system","系统内置"],["template","模板自带"],["user","我的"]].map(([scope,title])=>`<optgroup label="${title}">${profiles.filter(p=>p.libraryScope===scope).map(p=>`<option value="${esc(profileSelectionKey(p))}" ${key===profileSelectionKey(p)?"selected":""}>${escapeText(profileName(p))}${p.templateName?" · "+escapeText(p.templateName):""}</option>`).join("")}</optgroup>`).join("")}
    <option value="__dxf__">外部 DXF…</option></select></label>${params}</div>
    ${section.pendingParameters?'<small class="tube-drawing-warning">正在更新截面参数…</small>':""}
    ${section.profile?`${diagram?`<details class="td-draw-section-diagram"><summary>截面参数示意图</summary>${diagram}</details>`:""}<div class="tube-drawing-section-preview">${diagram ? "" : renderProfileSvg(section.profile)}<span>${escapeText(section.name)} · ${escapeText(section.profile.specification??"")}<small>截面快照随图纸保存，不修改管型库。</small></span></div>`:'<small>可直接从管型库选取，或导入当前零件使用的 DXF。</small>'}
    </section>`;
}
export function renderPartDrawingDialog(view) {
  return renderPartDrawingWorkbench(view,renderDrawingSection);
}
export function openPartDrawing(view,part=null) {
  const recipe=part?.properties?.["tubeDesigner.partDrawing"]??part?.properties?.["tubeDesigner.punchWizard"];
  const saved=recipe?.drawing;
  if(part&&!saved)throw new Error("此零件没有三维绘制定义，请使用冲孔向导。");
  // A drawing always starts with a real editable main tube. There is no
  // separate "apply main" transaction anymore; the right panel edits the
  // current model directly and the history is the only rollback mechanism.
  view.tubeDesignerPartDrawing={part:part?clone(part):null,mainApplied:true,mode:"main",selected:"main"};
  const profiles=libraryProfiles(view),p=profiles.find(p=>profileSelectionKey(p)===view.tubeDesignerSelectedProfileId)
    ??defaultDrawingProfile(view)??profiles[0];
  view.tubeDesignerPartDrawing.state=createDrawingState(part??{entityId:NEW_DRAWING_PART_ID,length:500});
  const s=drawingState(view);
  s.previewMode="tools";
  s.drawing=saved?{...clone(saved),name:part.name}:{length:500,name:"三维绘制零件",quantity:1,material:"",section:p?sectionFromLibrary(p,view):{}};
  if(!s.drawing.section?.profile?.contours?.length&&p)s.drawing.section=sectionFromLibrary(p,view);
  s.baseLength=Number(s.drawing.length);
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
  payload.drawing={schemaVersion:1,length,section:savedSection(d.section)};
  for(const f of payload.features)if(f.section)f.section=savedSection(f.section);
  if(!view.tubeDesignerPartDrawing.part) {
    delete payload.partEntityId;delete payload.resourceId;delete payload.resourceVersion;
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
  const current=getSection(view,which)??{};
  if(key==="__snapshot__") {const restored=clone(current);delete restored.pendingParameters;delete restored.key;setSection(view,which,restored);return;}
  const options=beginDrawingOperation(context,view,"正在读取精确截面");renderDrawing(context,view,ops);await paint();
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
    savedSection(section);setSection(view,which,section,shouldCheckpoint);
  } finally {finishDrawingOperation(view);}
}
async function applyDrawing(context,view,ops) {
  const payload=getPartDrawingPayload(view),meta=view.tubeDesignerPartDrawing;
  const options=beginDrawingOperation(context,view,meta.part?"正在更新三维零件":"正在生成三维零件");renderDrawing(context,view,ops);await paint();
  try {
    const result=await context.sceneProxy.invoke(meta.part?"TubeDesigner.ApplyPartDrawing":"TubeDesigner.AddPartDrawing",payload,options);
    const id=String(result.partEntityId??meta.part?.entityId??"");
    if(!result.tubeDesigner||!id)throw new Error("没有收到保存后的零件记录。");
    view.scene??={};view.scene.tubeDesigner=result.tubeDesigner;restoreSavedNestingTask(view,context);
    view.tubeDesignerNestingSelectedPartIds=[id];view.tubeDesignerActivePartId=id;view.tubeDesignerActiveNestingPartId=id;
    view.tubeDesignerNestingSelectionKind="part";view.tubeDesignerActiveNestingPlacementId="";
    view.tubeDesignerPartMeasurementState=null;view.tubeDesignerPartViewportKey="";view.tubeDesignerNestingSettingsSourceSignature="";
    view.tubeDesignerPartDrawing=null;
    await context.actions?.refreshActiveSceneState?.();
    ops.showNotice?.(context,view,"三维零件已保存到下料区，可继续编辑、排样或导出。");
  } finally {finishDrawingOperation(view);}
}
const previewQueues=new WeakMap();
function renderDrawing(context,view,ops) {
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
  return {...getPartDrawingPayload(view),toolsOnly:true};
}
// One native evaluation at a time. Coalesce edits and never display a stale response.
export function attachPartDrawingEditor(context,view,mount,ops) {
  attachDrawingInspector(view,mount);
  const s=drawingState(view),m=view.tubeDesignerPartDrawing;
  if(!s||!m||!context.sceneProxy?.invoke)return;
  if(s.catalogueStatus==="idle"&&!view.pending) {
    s.catalogueStatus="loading";
    const options=beginDrawingOperation(context,view,"正在加载三维绘制工具"),operation=view.tubeDesignerOperation;
    renderDrawing(context,view,ops);
    void paint().then(()=>context.sceneProxy.invoke("TubeDesigner.GetPartDrawingTools",{},options)).then(result=>{
      if(drawingState(view)===s)installDrawingCatalogue(s,result);
    }).catch(error=>{
      if(drawingState(view)===s){s.catalogueStatus="error";s.error=error?.message??String(error);}
    }).finally(()=>{
      finishDrawingOperation(view,operation);
      if(drawingState(view)===s)renderDrawing(context,view,ops);
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
  s.pendingPreviewRecipe=clone(payload);
  clearTimeout(q.timer);
  const run=async()=>{
    if(q.running)return;
    if(drawingState(view)!==s||view.tubeDesignerPartDrawing!==m||view.pending||!q.payload)return;
    const current=q.signature,body=clone(q.payload),revision=s.revision;
    q.running=true;s.previewPending=true;s.error="";s.previewComputeError=null;s.previewPhase="正在更新主管与刀具";
    const options={timeoutMs:180000,onReport(report) {
      if(drawingState(view)!==s||q.signature!==current)return;
      const data=report?.payload??report?.data??report;
      s.previewPhase=String(data?.message??s.previewPhase);
      const label=context.mount?.querySelector?.("[data-part-drawing-preview-notice] > span");
      if(label)label.textContent=s.previewPhase;
      context.mount?.querySelector?.(".td-draw-preview-progress")?.setAttribute?.("aria-valuetext",s.previewPhase);
    }};
    renderDrawing(context,view,ops);
    try {
      await paint();
      const result=await context.sceneProxy.invoke("TubeDesigner.PreviewPartDrawing",body,options);
      if(!hasDrawingToolPreview(result))throw new Error(result?.resultError||"未返回可显示的主管与刀具预览。");
      if(drawingState(view)===s&&q.signature===current&&s.revision===revision) {
        s.preview={...result,revision,includesDraft:false};m.validSignature=current;
        s.previewRecipe=clone(body);
      }
    } catch(error) {
      if(drawingState(view)===s&&q.signature===current) {
        s.error=error?.message??String(error);s.previewComputeError={revision,message:s.error};
      }
    } finally {
      q.running=false;
      if(drawingState(view)===s) {
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
  // Kept as a compatibility action for stale DOM events. Live edits are not
  // rolled back here; undo/redo is the only rollback mechanism.
  s.editingId="";m.mode="";s.error="";s.previewMode="tools";
}
function beginOperation(view,command,node=null) {
  const s=drawingState(view),m=view.tubeDesignerPartDrawing;
  if(!s||!m)throw new Error("三维绘制未打开。");
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
    m.mode=command;m.selected=command;s.draft=end;s.editingId="";s.previewMode="tools";return;
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
  s.draft=s.features.at(-1);m.mode="feature";m.selected=s.draft.id;s.previewMode="tools";
}
async function commitOperation(context,view,ops) {
  // The old right-panel commit button is no longer rendered. Keep this action
  // harmless for a stale click from an already-mounted page.
  const s=drawingState(view),m=view.tubeDesignerPartDrawing;
  if(!s||!m)return;
  s.editingId="";s.error="";
  if(m.mode==="feature")s.draft=s.features.find(feature=>feature.id===m.selected)??s.draft;
}
export async function handlePartDrawingAction(context,view,action,target,ops) {
  if(!action.startsWith(PREFIX))return {handled:false};
  if(view.pending)return {handled:true};
  const suffix=action.slice(PREFIX.length);
  try {
    if(suffix==="open") {
      const id=target?.dataset?.tubeDesignerPartId;
      const part=id?listNestingParts(view.scene?.tubeDesigner??{}).find(p=>String(p.entityId)===String(id)):null;
      if(id&&!part)throw new Error("零件记录不存在。");
      openPartDrawing(view,part);
    } else if(suffix==="cancel") {view.tubeDesignerPartDrawing=null;}
    else if(view.tubeDesignerPartDrawing) {
      const s=drawingState(view),m=view.tubeDesignerPartDrawing,which=target?.dataset?.drawingSection;
      if(suffix==="command")beginOperation(view,target?.dataset?.drawingCommand);
      else if(suffix==="select-node") {
        const node=target?.dataset?.drawingNode;beginOperation(view,["main","start","end"].includes(node)?node:"feature",node);
      } else if(suffix==="cancel-operation")cancelOperation(view);
      else if(suffix==="commit-operation")await commitOperation(context,view,ops);
      else if(suffix.startsWith("selected-")) {
        const kind=suffix.slice(9);
        if(kind==="remove"&&m.selected==="main")throw new Error("主管不可删除。");
        const index=s.features.findIndex(f=>f.id===m.selected);
        if(index>=0) {
          const selected=s.features[index];
          const nextAfterRemove=kind==="remove"?adjacentDrawingNode(s,selected.id):null;
          if(kind==="copy") {
            if(isDrawingToolReadOnly(s,selected))throw new Error("退化定式刀具仅可删除。");
            const copy=normalizeDrawingFeature({...clone(selected),id:undefined,station:selected.station+(selected.arrayPitch||50),enabled:true});
            checkpointDrawing(s);s.features=[...s.features,copy];s.draft=s.features.at(-1);s.editingId="";
            m.mode="feature";m.selected=s.draft.id;s.previewMode="tools";
          } else if(editDrawingFeature(s,kind,index)) {
            if(kind==="remove") {
              if(!selectDrawingNode(view,nextAfterRemove))selectDrawingNode(view,"main");
              s.editingId="";
            }
            else {m.mode="feature";m.selected=selected.id;s.draft=s.features.find(f=>f.id===selected.id)??s.draft;s.editingId="";}
            s.previewMode="tools";
          }
        } else if(kind==="remove"&&["start","end"].includes(m.selected)) {
          const next=adjacentDrawingNode(s,m.selected);
          editDrawingFeature(s,"remove-end",m.selected);
          if(!selectDrawingNode(view,next))selectDrawingNode(view,"main");
          s.editingId="";s.previewMode="tools";
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
        if(["drawingArrayMode","arrayDirection","rowDirection","arraySpacing","rowSpacing"].includes(field)) {
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
        if(mode==="tools")s.previewMode=mode;
      }
      else if(suffix==="add") {
        // Legacy action: a feature is added when its command is selected.
        if(s.draft&&!s.features.some(feature=>feature.id===s.draft.id))activateLiveFeature(view,s.draft);
      } else if(["edit","copy","toggle","remove","undo","redo","remove-end"].includes(suffix)) {
        if(["undo","redo"].includes(suffix)) {
          cancelOperation(view);
          if(editDrawingFeature(s,suffix,target?.dataset?.tubeDesignerPunchIndex)) {
            m.mode="main";m.selected="main";s.editingId="";s.previewMode="tools";
          }
        } else if(editDrawingFeature(s,suffix,target?.dataset?.tubeDesignerPunchIndex)) {
          if(suffix==="remove-end") {m.mode="main";m.selected="main";}
          else {m.mode="feature";const feature=s.features.find(item=>item.id===m.selected);if(feature)s.draft=feature;}
          s.editingId="";s.previewMode="tools";
        }
      }
      else if(suffix==="preview") {
        // Refresh the drawing-only tool preview through its existing serial
        // queue. Never route this editor into machining's Boolean preview.
        s.previewRenderError="";s.error="";m.validSignature="";
        if(s.catalogueStatus==="error")s.catalogueStatus="idle";
        const q=previewQueues.get(view);
        if(q)q.signature="";
      } else if(suffix==="apply")await applyDrawing(context,view,ops);
    }
  } catch(error) {
    if(drawingState(view))drawingState(view).error=error?.message??String(error);else throw error;
  } finally {renderDrawing(context,view,ops);}
  return {handled:true};
}
export async function handlePartDrawingRibbonCommand(context,view,command,ops) {
  if(command!=="nesting.draw-part")return false;
  await handlePartDrawingAction(context,view,PREFIX+"open",null,ops);return true;
}
