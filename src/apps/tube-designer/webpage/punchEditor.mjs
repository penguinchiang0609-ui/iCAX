import {
  installPunchCatalogue,
  getPunchWizardPayload,
  normalizePunchFeature,
  resolvePunchDistribution,
  stripPunchLayoutDerivedFields,
  validatePunchFeature,
  validatePunchWizard,
} from "./punchWizard.mjs";
import { attachViewCube, renderViewCube, stopViewCubeAnimation } from "../../_shared/workbench/viewport/viewCube.mjs";
import { rememberPunchDom } from "./punchDomPatch.mjs";
import { attachPunchParameterWindow } from "./punchParameterWindow.mjs";
import { bindDiagramDragging } from "./floatingParameterDiagram.mjs";
import { bindProfileParameterDiagrams } from "./profileParameterDiagram.mjs";
import { attachPunchModalBoundary } from "./punchModal.mjs";

const controllers = new WeakMap();
const presentations = new WeakMap();
const previewFlights = new WeakMap();
const inputBindings = new WeakMap();
const wizardWindowBindings = new WeakMap();
const regionResizeBindings = new WeakMap();
const sheetResizeBindings = new WeakMap();
const continuityNodes = new WeakMap();
const paint = () => typeof requestAnimationFrame === "function" ? new Promise(resolve => requestAnimationFrame(()=>requestAnimationFrame(resolve))) : Promise.resolve();
export function setPunchPreviewView(mount,name) {
  const viewport=controllers.get(mount)?.viewport;
  if(!viewport)return false;
  if(name==="fit")viewport.fitViewToViewport();else if(["iso","top","bottom","front","back","left","right"].includes(name))viewport.setStandardView(name);else return false;
  return true;
}

function retainPunchHorizontalFrame(mount, changeLayout) {
  const controller=controllers.get(mount),viewport=controller?.viewport;
  const before=viewport?.camera?.isOrthographicCamera
    ? {width:viewport.camera.right-viewport.camera.left,state:viewport.getCameraState()}:null;
  changeLayout();
  if(!before||controllers.get(mount)!==controller)return;
  viewport.resize();
  const width=viewport.camera.right-viewport.camera.left;
  if(before.width>0&&width>0&&Math.abs(width-before.width)>.001) {
    // Changing a pane's aspect must not magnify the stock until its ends leave
    // the view. Preserve the user's horizontal framing, target and direction.
    viewport.setCameraState({...before.state,radius:before.state.radius*before.width/width});
  }
}

export function beginPunchOperation(context, view, title) {
  const operation = { kind: "punch", title, message: "正在准备刀具和基准实体", phaseLabel: "准备中" };
  operation.finished=new Promise(resolve=>{operation.resolveFinished=resolve;});
  view.tubeDesignerOperation = operation;
  view.pending = true;
  return { timeoutMs: 180000, onReport(report) {
    if (view.tubeDesignerOperation !== operation) return;
    const data = report?.payload ?? report?.data ?? report;
    operation.message = String(data?.message ?? operation.message);
    const done = Number(data?.completed ?? data?.current ?? 0), total = Number(data?.total ?? 0);
    operation.phaseLabel = total>0 ? done+" / "+total : "正在计算";
    operation.completed=Number.isFinite(done)?done:0;
    operation.total=Number.isFinite(total)&&total>0?total:null;
    const mount = context.mount;
    const message = mount?.querySelector?.("[data-tube-designer-operation-message]");
    const phase = mount?.querySelector?.("[data-tube-designer-operation-phase]");
    if(message)message.textContent = operation.message;
    if(phase)phase.textContent = operation.phaseLabel;
    const track = mount?.querySelector?.(".tube-designer-operation-wait .tube-designer-export-progress-track");
    track?.setAttribute?.("aria-valuetext",operation.phaseLabel);
    if(track && total>0) {
      track.classList.remove("is-indeterminate");
      track.setAttribute?.("aria-valuemin","0");track.setAttribute?.("aria-valuemax",String(total));
      track.setAttribute?.("aria-valuenow",String(Math.max(0,Math.min(total,Number.isFinite(done)?done:0))));
      const bar=track.querySelector("i"); if(bar)bar.style.width = Math.max(2,Math.min(100,100*done/total))+"%";
    }
  }};
}
export function finishPunchOperation(view, expectedOperation = null) {
  if(expectedOperation&&view.tubeDesignerOperation!==expectedOperation)return false;
  const operation=expectedOperation??view.tubeDesignerOperation;
  if(operation?.renderHolds>0) {
    operation.finishRequested=true;
    view.pending=true;
    return false;
  }
  if(view.tubeDesignerOperation?.kind==="punch")view.tubeDesignerOperation=null;
  view.pending=false;
  operation?.resolveFinished?.();
  return true;
}
export function waitForPunchPreview(view) {
  return previewFlights.get(view)?.finished??Promise.resolve();
}
function punchPreviewPayload(view, part, includeDraft) {
  const state = view.tubeDesignerPunchWizard;
  const payload = getPunchWizardPayload(view);
  if (!includeDraft || !state?.draft) return payload;
  const draft = resolvePunchDistribution(state.draft,state.baseLength??part?.length);
  const error = validatePunchFeature({ ...part, length: state.baseLength ?? part?.length }, draft);
  if (error) throw new Error(error);
  const serializedDraft=stripPunchLayoutDerivedFields(draft);
  const editingIndex = payload.features.findIndex((feature) => feature.id === state.editingId);
  if (editingIndex >= 0) payload.features[editingIndex] = { ...serializedDraft, id: payload.features[editingIndex].id };
  else payload.features.push(serializedDraft);
  return payload;
}

export function buildPunchPreviewRows(preview = {}, mode = "tools") {
  const localToWorldMatrix = centeredPreviewMatrix(preview);
  if(mode === "result" && preview.previewComputed === false)return [];
  if (mode !== "result" && preview.baseGeometry?.url) {
    const individualTools=(Array.isArray(preview.toolPreviews)?preview.toolPreviews:[])
      .filter(item=>item.geometry?.url).map(item=>({
        entityId:"punch-preview-tool:"+item.target+":"+item.key,data:{geometry:item.geometry,material:preview.toolMaterial,
          geometryKind:1,renderClass:5,renderOrder:10,visible:true,selectable:false,localToWorldMatrix},
      }));
    const toolRows=individualTools.length?individualTools:(preview.toolGeometry?.url ? [{ entityId: "punch-preview-tools", data: {
      geometry: preview.toolGeometry, material: preview.toolMaterial,
      geometryKind: 1, renderClass: 5, renderOrder: 10, visible: true, selectable: false, localToWorldMatrix,
    } }] : []);
    return [{ entityId: "punch-preview-blank", data: {
      geometry: preview.baseGeometry, material: preview.baseMaterial,
      geometryKind: 1, renderClass: 1, visible: true, selectable: false, localToWorldMatrix,
    } }, ...toolRows];
  }
  return preview.geometry?.url ? [{ entityId: "punch-preview", data: {
    geometry: preview.geometry, geometryKind: 1, renderClass: 1, visible: true, selectable: false, localToWorldMatrix,
  } }] : [];
}

function centeredPreviewMatrix(preview) {
  const bounds=preview?.baseBounds??preview?.bounds??{};
  const min=Array.isArray(bounds.min)?bounds.min.map(Number):null;
  const max=Array.isArray(bounds.max)?bounds.max.map(Number):null;
  const width=Number(bounds.width??preview?.length??0);
  const centerX=min&&max&&Number.isFinite(min[0])&&Number.isFinite(max[0])
    ? (min[0]+max[0])/2 : Number.isFinite(width)&&width>0 ? width/2 : 0;
  const centerY=min&&max&&Number.isFinite(min[1])&&Number.isFinite(max[1]) ? (min[1]+max[1])/2 : 0;
  const centerZ=min&&max&&Number.isFinite(min[2])&&Number.isFinite(max[2]) ? (min[2]+max[2])/2 : 0;
  return [1,0,0,-centerX, 0,1,0,-centerY, 0,0,1,-centerZ, 0,0,0,1];
}

function initializePunchCamera(viewport,preview,baseLength) {
  viewport.setProjectionMode("orthographic");
  viewport.setStandardView("front");
  if(!viewport.fitViewToViewport(1.1))return false;
  const bounds=preview.baseBounds;
  const length=bounds?.min&&bounds?.max?Number(bounds.max[0])-Number(bounds.min[0]):Number(baseLength??preview.length);
  const visibleWidth=Number(viewport.camera.right)-Number(viewport.camera.left);
  if(Number.isFinite(length)&&length>0&&Number.isFinite(visibleWidth)&&visibleWidth>0) {
    const camera=viewport.getCameraState();
    // Orthographic width scales linearly with radius. Fit the stock itself,
    // not an oversized or off-stock tool, to 90% of the actual canvas width.
    const widthRadius=camera.radius*length/(visibleWidth*0.9);
    const height=bounds?.min&&bounds?.max?Number(bounds.max[2])-Number(bounds.min[2]):0;
    const visibleHeight=Number(viewport.camera.top)-Number(viewport.camera.bottom);
    // A very short, wide section cannot fill 90% horizontally without being
    // cropped vertically. Keep the whole stock visible in that edge case.
    const heightRadius=height>0&&visibleHeight>0?camera.radius*height*1.1/visibleHeight:0;
    viewport.setCameraState({...camera,target:{x:0,y:0,z:0},radius:Math.max(widthRadius,heightRadius)});
  }
  return true;
}

function previewIsCurrent(state) {
  return !!state?.preview && (state.preview.revision === state.revision
    && (state.requestedPreviewIncludesDraft===undefined||state.preview.includesDraft===state.requestedPreviewIncludesDraft)
    || (state.preview.isOriginal === true && state.revision === 0));
}
function displaySourceRecipe(state,includesDraft) {
  const input=state.creationInput,draft=input?.draft;
  const features=structuredClone(state.features??[]);
  if(includesDraft&&state.draft) {
    const index=features.findIndex(feature=>feature.id===state.editingId);
    if(index>=0)features[index]={...structuredClone(state.draft),id:features[index].id};
    else features.push(structuredClone(state.draft));
  }
  return {blank:JSON.stringify([state.partId,state.resourceId,state.resourceVersion,state.baseLength,
    draft?.profileKey,draft?input.profileParameters?.[draft.profileKey]:null]),features,ends:structuredClone(state.ends??{})};
}
function previewReferences(preview) {
  return [preview.geometry,preview.baseGeometry,preview.toolGeometry,preview.baseMaterial,preview.toolMaterial,
    ...(preview.toolPreviews??[]).map(item=>item.geometry)].filter(Boolean);
}
function unchangedPreviewEntities(state, mode) {
  if(mode!=="tools" || !state?.preview?.baseGeometry?.url)return [];
  // Compare live source records, including invalid edits that never reach a
  // request. A previous successful payload must not keep a stale changed tool.
  const previous=state.previewSourceRecipe??state.previewRecipe;
  const next=state.previewSourceRecipe?displaySourceRecipe(state,state.requestedPreviewIncludesDraft??state.preview.includesDraft):state.pendingPreviewRecipe;
  if(!previous||!next)return [];
  const blank=recipe=>recipe.blank??JSON.stringify([recipe.partEntityId,recipe.resourceId,recipe.resourceVersion,
    recipe.profileRef,recipe.parameters,recipe.length,recipe.drawing]);
  if(blank(previous)!==blank(next))return [];
  const ids=["punch-preview-blank"];
  for(const item of state.preview.toolPreviews??[]) {
    const find=recipe=>item.target==="end"?recipe.ends?.[item.key]
      :recipe.features?.find((feature,index)=>String(feature.id??index)===String(item.key));
    const old=find(previous),current=find(next);
    if(old&&current&&JSON.stringify(old)===JSON.stringify(current))ids.push("punch-preview-tool:"+item.target+":"+item.key);
  }
  return ids;
}
function loadedPreviewEntities(viewport,preview,mode,allowed=null) {
  if(!viewport)return [];
  const allow=allowed?new Set(allowed):null;
  return buildPunchPreviewRows(preview,mode).filter(row=>{
    if(allow&&!allow.has(row.entityId))return false;
    const reference=row.data.geometry,object=viewport.sceneObjects.get(row.entityId);
    return object?.userData.geometryId===reference.url
      &&viewport.geometryPayloads.get(reference.url)?.viewResourceVersion===String(reference.version??0)
      &&object.geometry===viewport.geometryObjects.get(reference.url);
  }).map(row=>row.entityId);
}
function previewUnavailableReason(state, mode) {
  if(state?.previewRenderError)return "三维资源显示失败；未显示旧画面。请点击“更新刀具体”重试。";
  if(!previewIsCurrent(state))return state?.previewPending
    ? "正在计算当前参数，旧预览已隐藏"
    : state?.previewComputeError?.revision === state?.revision
      ? "当前参数预览失败，旧预览已隐藏。" + state.previewComputeError.message
      : "参数已改变，旧刀具体已隐藏；请点击“更新刀具体”。";
  if(mode === "result" && !state.preview.geometry?.url)
    return state.preview.previewComputed===true&&state.preview.solidCount===0
      ? "当前切割结果为空，主管已全部切除。可以继续调整刀具。"
      : "当前切割尚未完成。可切换“主管 + 刀具体”检查已生成的刀具。";
  return "";
}
function renderPreviewNotice(host, message, blocking = true) {
  host?.querySelector("[data-punch-preview-notice]")?.remove();
  if(!host || !message)return;
  const notice=host.ownerDocument.createElement("div");
  notice.dataset.punchPreviewNotice=blocking?"blocked":"warning";
  notice.className="tube-designer-punch-scene-notice"+(blocking?" is-blocking":"");
  notice.setAttribute("role",blocking?"status":"note");
  notice.textContent=message;host.append(notice);
}
function recordPreviewFailure(state, message, includesDraft) {
  state.error=message;
  state.previewComputeError={revision:state.revision,includesDraft,message};
}

export async function previewPunch(context, view, part, ops, creation = null, previewOptions = {}) {
  const state = view.tubeDesignerPunchWizard;
  // A shared batch hole definition is not an instance on the selected part.
  // Only its assigned feature rows belong in the stock preview.
  const includeDraft = !view.tubeDesignerPunchBatch && (state?.parameterEditor
    ? state.parameterEditor.index==="draft"&&!state.parameterEditor.end : previewOptions.includeDraft === true);
  if(!state || view.pending || previewFlights.has(view))return false;
  state.requestedPreviewIncludesDraft=includeDraft;
  // Retry the displayed recipe, not an unrelated unfinished new-row draft.
  // This is a resource-only operation and does not accept or save that draft.
  if(state.previewRenderError&&(state.preview?.revision===state.revision
    ||(state.preview?.isOriginal===true&&state.revision===0))) {
    state.requestedPreviewIncludesDraft=state.preview.includesDraft;
    const controller=presentations.get(state);
    if(controller?.state===state)controller.failedKey=null;
    state.previewRenderError="";state.error="";
    ops.renderProject(context,view);
    await presentations.get(state)?.flight;
    return !state.previewRenderError;
  }
  const validationView = { ...view, tubeDesignerPunchWizard: { ...state, parameterEditor: null,
    previewRenderError:"", editingId: includeDraft ? "" : state.editingId } };
  const error=validatePunchWizard(validationView,part);
  if(error){recordPreviewFailure(state,error,includeDraft);ops.renderProject(context,view);return false;}
  const revision=state.revision;
  const sourceRecipe=displaySourceRecipe(state,includeDraft);
  let payload;
  try {
    payload=punchPreviewPayload(view,part,includeDraft);
  } catch(error) {
    recordPreviewFailure(state,error?.message??String(error),includeDraft);ops.renderProject(context,view);return false;
  }
  if(creation){delete payload.partEntityId;delete payload.resourceId;delete payload.resourceVersion;Object.assign(payload,creation);}
  // Editing only constructs/places display tools. Manufacturing subtraction is
  // deliberately reserved for the final Add/Apply action.
  payload.toolsOnly=true;
  payload.individualToolPreviews=true;
  state.pendingPreviewRecipe=structuredClone(payload);
  if(previewOptions.quiet===true&&!state.error&&!state.previewPending&&state.preview?.toolsOnly===true&&state.preview?.baseGeometry?.url
    &&state.preview.includesDraft===includeDraft&&(state.preview.revision===revision
      ||view.tubeDesignerPunchBatch&&JSON.stringify(state.previewRecipe)===JSON.stringify(payload))) {
    // Editing/selecting an unused shared definition changes UI revision only;
    // the selected part has exactly the same validated geometry request.
    state.preview={...state.preview,revision};
    state.previewSourceRecipe=sourceRecipe;
    ops.renderProject(context,view);
    await presentations.get(state)?.flight;
    return !state.previewRenderError;
  }
  const requestId=(state.previewRequestId??0)+1;
  state.previewRequestId=requestId;
  // Batch previews are serialized by the creation queue, independently of the
  // exclusive save/catalogue operation. Editing does not wait for the scene.
  const background=previewOptions.background===true;
  const options=background?{timeoutMs:180000}:beginPunchOperation(context,view,"正在更新冲孔刀具体");
  const operation=background?{background:true}:view.tubeDesignerOperation;
  if(background)operation.finished=new Promise(resolve=>{operation.resolveFinished=resolve;});
  previewFlights.set(view,operation);
  state.previewPending=true;
  state.previewRenderError="";
  state.previewComputeError=null;
  state.error="";
  try {
    ops.renderProject(context,view);
    await paint();
    if(view.tubeDesignerPunchWizard!==state||state.revision!==revision||state.previewRequestId!==requestId
      ||state.requestedPreviewIncludesDraft!==includeDraft||background&&view.pending)return false;
    const result=await context.sceneProxy.invoke("TubeDesigner.PreviewPunchWizard",payload,options);
    if(result?.toolsOnly!==true||!result?.baseGeometry?.url||result?.geometry?.url)
      throw new Error("刀具体预览接口未返回独立主管与刀具，请更新后端组件后重试。");
    if((!view.pending||!background)&&view.tubeDesignerPunchWizard===state && state.revision===revision && state.previewRequestId===requestId
      &&state.requestedPreviewIncludesDraft===includeDraft) {
      state.preview={...result,revision,includesDraft:includeDraft};
      state.previewRecipe=structuredClone(payload);
      state.previewSourceRecipe=sourceRecipe;
      state.previewMode="tools";
      ops.renderProject(context,view);
      await presentations.get(state)?.flight;
      if(state.previewRenderError)throw new Error(state.previewRenderError);
    }
    return true;
  } catch(error) {
    if((!view.pending||!background)&&view.tubeDesignerPunchWizard===state && state.revision===revision && state.previewRequestId===requestId
      &&state.requestedPreviewIncludesDraft===includeDraft)
      recordPreviewFailure(state,error?.message??String(error),includeDraft);
    return false;
  } finally {
    if(state.previewRequestId===requestId)state.previewPending=false;
    if(previewFlights.get(view)===operation)previewFlights.delete(view);
    if(background)operation.resolveFinished();else finishPunchOperation(view,operation);
    ops.renderProject(context,view);
  }
}
export function attachPunchEditor(context,view,mount,ops) {
  const state=view.tubeDesignerPunchWizard;
  rememberPunchDom(view,mount);
  attachPunchRegionResize(view,mount);
  attachPunchWizardWindow(view,mount);
  attachPunchInputContinuity(state,mount,view);
  // Restore the edited control before the modal's initial-focus fallback can
  // focus the first field and overwrite the retained editing position.
  attachPunchModalBoundary(view,mount);
  attachPunchBatchCells(context,view,mount,ops);
  attachPunchParameterWindow(mount,state);
  bindDiagramDragging(mount,view);
  bindProfileParameterDiagrams(mount);
  const scroll=mount?.querySelector?.('.tube-designer-punch-sheet-scroll');
  const previousSheet=sheetResizeBindings.get(mount);
  if(previousSheet?.scroll!==scroll) {previousSheet?.observer.disconnect();sheetResizeBindings.delete(mount);}
  if(scroll) {
    scroll.style.setProperty('--punch-table-visible-width',scroll.clientWidth+'px');
    // A full-row shape editor follows the visible sheet width while the many
    // parameter columns scroll horizontally within this region only.
    if(!sheetResizeBindings.has(mount)) {
      const observer=new scroll.ownerDocument.defaultView.ResizeObserver(()=>{
        scroll.style.setProperty('--punch-table-visible-width',scroll.clientWidth+'px');
      });observer.observe(scroll);sheetResizeBindings.set(mount,{scroll,observer});
    }
  }
  for(const cell of mount?.querySelectorAll?.('[data-punch-cell-activate]')??[]) {
    cell.onkeydown=event=>{
      if(event.target===cell&&['Enter',' '].includes(event.key)){event.preventDefault();cell.click();}
    };
  }
  if(state?.scrollToFeatureIndex!==undefined) {
    mount?.querySelector?.('[data-tube-designer-punch-row="'+state.scrollToFeatureIndex+'"]')?.scrollIntoView?.({block:"nearest",inline:"nearest"});
    delete state.scrollToFeatureIndex;
  }
  const parameterDialog=[...mount?.querySelectorAll?.("[data-punch-inline-editor],[data-punch-parameter-dialog]")??[]]
    .find(node=>!node.closest('[data-punch-batch-definition-editor]'));
  if(parameterDialog&&state?.parameterEditor) {
    if(!state.parameterEditor.focused){parameterDialog.querySelector("select,input")?.focus({preventScroll:true});state.parameterEditor.focused=true;}
    parameterDialog.onkeydown=event=>{
      if(event.key==="Escape"){event.preventDefault();event.stopPropagation();parameterDialog.querySelector('[data-cam-action$="parameters-close"]')?.click();}
    };
  }
  if(state?.catalogueStatus==="idle" && !view.pending && context.sceneProxy?.invoke) {
    state.catalogueStatus="loading";
    // Mark before rendering: afterProjectRender may run again synchronously.
    beginPunchOperation(context,view,"正在加载刀具模板");
    const catalogueOperation=view.tubeDesignerOperation;
    ops.renderProject(context,view);
    void paint().then(()=>context.sceneProxy.invoke("TubeDesigner.GetPunchTools",{}, {timeoutMs:30000})).then(result=>{
      if(view.tubeDesignerPunchWizard===state)installPunchCatalogue(state,result);
    }).catch(error=>{
      if(view.tubeDesignerPunchWizard===state){state.catalogueStatus="error";state.error=error?.message??String(error);}
    }).finally(()=>{finishPunchOperation(view,catalogueOperation);ops.renderProject(context,view);});
  }
  // A saved thumbnail is only the initial fallback. Opening the wizard restores
  // its independent blank and tool bodies after the catalogue/display hold.
  if(state?.initialToolsPreviewPart&&state.catalogueStatus==="ready"&&!view.pending&&!state.initialToolsPreviewQueued) {
    state.initialToolsPreviewQueued=true;
    queueMicrotask(()=>{
      state.initialToolsPreviewQueued=false;
      if(view.tubeDesignerPunchWizard!==state||view.pending)return;
      const part=state.initialToolsPreviewPart;
      delete state.initialToolsPreviewPart;
      void previewPunch(context,view,part,ops,null,{quiet:true,includeDraft:false});
    });
  }
  void hydrate(context,view,mount,ops);
}

// Keep the sheet movable without coupling a drag to scene preview or CSG work.
// The position is UI-only and is restored on every incremental render.
function attachPunchWizardWindow(view,mount) {
  if(!mount?.querySelector)return;
  const state=view?.tubeDesignerPunchBatch??view?.tubeDesignerPunchWizard;
  const dialog=mount.querySelector("[data-tube-designer-punch-window-drag]")?.closest?.(".tube-designer-punch-dialog");
  const previous=wizardWindowBindings.get(mount);
  if(previous&&previous.dialog===dialog&&previous.state===state){previous.keepReachable();return;}
  previous?.abort.abort();
  wizardWindowBindings.delete(mount);
  if(!dialog||!state)return;
  const handle=dialog.querySelector("[data-tube-designer-punch-window-drag]");
  const win=dialog.ownerDocument?.defaultView;
  if(!handle||!win)return;
  const abort=new AbortController();
  const options={signal:abort.signal};
  const margin=16;
  let drag=null;
  const place=(left,top)=>{
    const box=dialog.getBoundingClientRect();
    const horizontalMargin=Math.min(margin,Math.max(0,(win.innerWidth-box.width)/2));
    const verticalMargin=Math.min(margin,Math.max(0,(win.innerHeight-box.height)/2));
    left=Math.max(horizontalMargin,Math.min(Number(left)||0,win.innerWidth-box.width-horizontalMargin));
    top=Math.max(verticalMargin,Math.min(Number(top)||0,win.innerHeight-box.height-verticalMargin));
    state.wizardWindowPosition={left,top};
    Object.assign(dialog.style,{position:"fixed",left:left+"px",top:top+"px",margin:"0px"});
  };
  const keepReachable=()=>{
    if(dialog.isConnected&&state.wizardWindowPosition)
      place(state.wizardWindowPosition.left,state.wizardWindowPosition.top);
  };
  const stop=event=>{
    if(!drag||event.pointerId!==drag.id)return;
    drag=null;
    delete handle.dataset.dragging;
    if(handle.hasPointerCapture?.(event.pointerId))handle.releasePointerCapture(event.pointerId);
  };
  handle.addEventListener("pointerdown",event=>{
    if(event.button!==0||event.isPrimary===false||event.target.closest?.("button,input,select,textarea,a"))return;
    event.preventDefault();
    const box=dialog.getBoundingClientRect();
    drag={id:event.pointerId,x:event.clientX-box.left,y:event.clientY-box.top};
    handle.dataset.dragging="true";
    handle.setPointerCapture?.(event.pointerId);
  },options);
  handle.addEventListener("pointermove",event=>{
    if(drag?.id===event.pointerId){event.preventDefault();place(event.clientX-drag.x,event.clientY-drag.y);}
  },options);
  for(const name of ["pointerup","pointercancel","lostpointercapture"])
    handle.addEventListener(name,stop,options);
  win.addEventListener("resize",keepReachable,options);
  let observer=null;
  if(typeof win.ResizeObserver==="function"){
    observer=new win.ResizeObserver(keepReachable);
    observer.observe(dialog);
  }
  abort.signal.addEventListener("abort",()=>{
    observer?.disconnect();
    if(drag&&handle.hasPointerCapture?.(drag.id))handle.releasePointerCapture(drag.id);
    delete handle.dataset.dragging;
  },{once:true});
  wizardWindowBindings.set(mount,{dialog,state,abort,keepReachable});
  keepReachable();
}

function attachPunchRegionResize(view,mount) {
  const workspace=mount?.querySelector?.('.tube-designer-punch-sheet-workspace');
  const state=view?.tubeDesignerPunchBatch??view?.tubeDesignerPunchWizard,previous=regionResizeBindings.get(mount);
  if(previous?.workspace===workspace&&previous.state===state)return;
  previous?.abort.abort();regionResizeBindings.delete(mount);
  const splitters=[...workspace?.querySelectorAll?.('[data-punch-region-splitter]')??[]];
  if(!state||!splitters.length)return;
  const win=workspace.ownerDocument.defaultView,abort=new AbortController(),options={signal:abort.signal};
  const regions=Object.fromEntries([...workspace.querySelectorAll('[data-punch-region]')].map(node=>[node.dataset.punchRegion,node]));
  const fixedRegions=Object.keys(regions).filter(name=>name!=="scene");
  const minimum=name=>parseFloat(win.getComputedStyle(regions[name]).getPropertyValue('--punch-region-min-height'))||96;
  const sizes=()=>Object.fromEntries(Object.entries(regions).map(([name,node])=>[name,node.getBoundingClientRect().height]));
  const place=values=>{
    retainPunchHorizontalFrame(mount,()=>{
      state.punchRegionSizes=values;
      for(const name of fixedRegions)workspace.style.setProperty('--punch-'+name+'-height',values[name]+'px');
      for(const splitter of splitters){const [before]=splitter.dataset.punchRegionSplitter.split(':');splitter.setAttribute('aria-valuenow',Math.round(values[before]));}
    });
  };
  const keepInside=()=>{
    if(!workspace.isConnected)return;
    const values=sizes(),available=workspace.clientHeight-splitters.reduce((sum,node)=>sum+node.getBoundingClientRect().height,0);
    const excess=Object.values(values).reduce((sum,height)=>sum+height,0)-available;
    if(excess<=.5)return;
    const capacity=fixedRegions.reduce((sum,name)=>sum+Math.max(0,values[name]-minimum(name)),0);
    if(capacity<=0)return;
    for(const name of fixedRegions)values[name]=Math.max(minimum(name),values[name]-Math.min(excess,capacity)*Math.max(0,values[name]-minimum(name))/capacity);
    place(values);
  };
  let drag=null;
  const resize=(pair,values,delta)=>{
    const [before,after]=pair,total=values[before]+values[after];
    const height=Math.max(minimum(before),Math.min(total-minimum(after),values[before]+delta));
    place({...values,[before]:height,[after]:total-height});
  };
  const stop=event=>{
    if(!drag||event.pointerId!==drag.id)return;
    if(drag.splitter.hasPointerCapture?.(drag.id))drag.splitter.releasePointerCapture(drag.id);
    drag=null;workspace.ownerDocument.body.classList.remove('tube-designer-punch-regions-resizing');
  };
  for(const splitter of splitters) {
    const pair=splitter.dataset.punchRegionSplitter.split(':');
    splitter.addEventListener('pointerdown',event=>{
      if(event.button!==0||event.isPrimary===false)return;
      event.preventDefault();drag={id:event.pointerId,y:event.clientY,values:sizes(),pair,splitter};
      splitter.setPointerCapture?.(event.pointerId);workspace.ownerDocument.body.classList.add('tube-designer-punch-regions-resizing');
    },options);
    splitter.addEventListener('keydown',event=>{
      if(!['ArrowUp','ArrowDown'].includes(event.key))return;
      event.preventDefault();resize(pair,sizes(),event.key==='ArrowDown'?20:-20);
    },options);
  }
  win.addEventListener('pointermove',event=>{if(drag?.id===event.pointerId){event.preventDefault();resize(drag.pair,drag.values,event.clientY-drag.y);}},options);
  win.addEventListener('pointerup',stop,options);win.addEventListener('pointercancel',stop,options);
  const observer=new win.ResizeObserver(keepInside);observer.observe(workspace);
  abort.signal.addEventListener('abort',()=>{observer.disconnect();workspace.ownerDocument.body.classList.remove('tube-designer-punch-regions-resizing');},{once:true});
  regionResizeBindings.set(mount,{workspace,state,abort});keepInside();
}
// The workbench replaces its DOM on each preview update. Retain the editing
// position locally so a Tab sequence behaves like a sheet, not a page reload.
function attachPunchInputContinuity(state,mount,view) {
  inputBindings.get(mount)?.abort();
  if(!state||!mount?.querySelectorAll)return;
  const binding=new AbortController();inputBindings.set(mount,binding);
  const capture={capture:true,signal:binding.signal};
  // Main-table edits can be committed by buttons in the header or footer, too.
  // Keep the whole active dialog in the same blur/click transaction boundary.
  const liveScope=()=>mount.querySelector("[data-punch-profile-advanced-dialog]")
    ??mount.querySelector("[data-punch-parameter-dialog]")
    ??mount.querySelector(".tube-designer-punch-dialog")??mount.querySelector(".tube-designer-punch-sheet");
  const scope=liveScope();
  if(!scope)return;
  const key=control=>JSON.stringify([control.closest?.('[data-punch-batch-current-part]')?.dataset.punchBatchCurrentPart,
    control.dataset.punchBatchPartId,control.dataset.punchBatchField,
    control.closest?.('[data-punch-batch-definition-editor]')?.dataset.punchBatchDefinitionEditor,
    control.dataset.tubeDesignerPunchField,control.dataset.tubeDesignerPunchParameter,
    control.dataset.tubeDesignerPunchProfileParameter,control.dataset.tubeDesignerPunchIndex,control.dataset.tubeDesignerPunchEnd,
    control.dataset.tubeDesignerNestingPunchField,control.dataset.tubeDesignerMainProfileParameter,
    control.dataset.tubeDesignerPunchArrayGroup,control.dataset.tubeDesignerPunchArrayField]);
  const inputs=[...scope.querySelectorAll("input,select,textarea")];
  const value=control=>control.type==="checkbox"?control.checked:control.value;
  const initialValues=new Map(inputs.map(control=>[control,value(control)]));
  const isDirty=control=>initialValues.has(control)&&initialValues.get(control)!==value(control);
  // Different buttons can share an action but pass different modes/options.
  const buttonKey=button=>JSON.stringify(Object.entries(button.dataset).sort(([a],[b])=>a.localeCompare(b)));
  const details=[...scope.querySelectorAll("details")];
  const detailEntries=[];
  const previousNodes=continuityNodes.get(state);
  state.uiDetails??={};
  for(const detail of details) {
    const input=detail.querySelector("[data-tube-designer-punch-field],[data-tube-designer-punch-profile-parameter],[data-tube-designer-punch-array-field]");
    const row=detail.closest("tr")?.dataset.tubeDesignerPunchRow
      ??["parameters",state.parameterEditor?.index,state.parameterEditor?.end];
    const id=JSON.stringify([row,detail.className,input?key(input):detail.querySelector("summary")?.textContent??""]);
    detailEntries.push([id,detail]);
    if(previousNodes?.details.get(id)===detail)state.uiDetails[id]=detail.open;
    else if(state.uiDetails[id]!==undefined)detail.open=state.uiDetails[id];
    detail.ontoggle=()=>{if(detail.isConnected)state.uiDetails[id]=detail.open;};
  }
  const scroll=mount.querySelector(".tube-designer-punch-sheet-scroll");
  if(scroll) {
    if(previousNodes?.scroll===scroll)state.uiScroll={top:scroll.scrollTop,left:scroll.scrollLeft};
    else if(state.uiScroll){scroll.scrollTop=state.uiScroll.top;scroll.scrollLeft=state.uiScroll.left;}
    scroll.onscroll=()=>{state.uiScroll={top:scroll.scrollTop,left:scroll.scrollLeft};};
  }
  continuityNodes.set(state,{scroll,details:new Map(detailEntries)});
  // toggle/scroll notifications are asynchronous; snapshot before the delegated
  // change handler can replace the DOM, even when the user edits immediately.
  scope.addEventListener("change",event=>{
    const active=scope.ownerDocument.activeElement;
    // A different control (for example a select picker) can change before the
    // previous text input blurs. Commit the old input before either DOM render.
    if(active!==event.target&&isDirty(active)) {
      state.uiFocus=key(event.target);active.blur();
    }
    for(const [id,detail] of detailEntries)if(detail.isConnected)state.uiDetails[id]=detail.open;
    if(scroll?.isConnected)state.uiScroll={top:scroll.scrollTop,left:scroll.scrollLeft};
  },capture);
  const actionSelector='button[data-cam-action],[data-punch-cell-activate]';
  const clickTarget=event=>event.target.closest?.(actionSelector+',input,select,textarea');
  scope.addEventListener("mousedown",event=>{
    const target=clickTarget(event),active=scope.ownerDocument.activeElement;
    if(target&&target!==active&&scope.contains(target)&&isDirty(active))event.preventDefault();
  },capture);
  scope.addEventListener("click",event=>{
    const target=clickTarget(event),active=scope.ownerDocument.activeElement;
    if(!target||target===active||!scope.contains(target)||!isDirty(active))return;
    // Preserve the intended action across the blur-triggered DOM replacement.
    // Otherwise the first click merely commits text and removes its own button.
    event.preventDefault();event.stopImmediatePropagation();
    const button=target.matches(actionSelector),identity=button?buttonKey(target):key(target);
    const cancelling=button&&target.dataset.camAction?.endsWith("-cancel");
    if(cancelling) {
      // Cancelling an uncommitted input must not start work only to discard it.
      if(active.type==="checkbox")active.checked=initialValues.get(active);
      else active.value=initialValues.get(active);
      delete state.uiPendingClick;
    }
    // One explicit button gesture may follow the edit it commits. Ordinary
    // controls are never queued, and a later wizard/parameter transaction may
    // not inherit this action. Disabled controls cannot create a new intent.
    const intent=button&&!cancelling&&!target.disabled&&!view.pending
      ?{identity,editor:state.parameterEditor}:null;
    if(intent)state.uiPendingClick=intent;
    if(button)delete state.uiFocus;else state.uiFocus=identity;
    active.blur();
    const current=liveScope(),selector=button?actionSelector:"input,select,textarea";
    const replacement=[...current?.querySelectorAll(selector)??[]].find(candidate=>(button?buttonKey(candidate):key(candidate))===identity);
    if(!replacement||replacement.disabled||view.pending)return;
    if(state.uiPendingClick===intent)delete state.uiPendingClick;
    if(button||replacement.type==="checkbox"||replacement.type==="radio")replacement.click();
    else {
      replacement.focus({preventScroll:true});
      if(replacement.matches("select")) {
        try { replacement.showPicker?.(); } catch { /* Focus remains usable when native pickers are unavailable. */ }
      }
    }
  },capture);
  for(const control of inputs) {
    control.onfocus=()=>{state.uiFocus=key(control);};
    control.oninput=()=>{
      if(control.dataset.tubeDesignerInteger!=="true")return;
      const integer=String(control.value??"").match(/^\d+/)?.[0]??"";
      if(control.value!==integer)control.value=integer;
    };
    control.onkeydown=event=>{
      if(event.key!=="Tab"||event.ctrlKey||event.altKey||event.metaKey)return;
      const visible=inputs.filter(el=>!el.disabled&&el.getClientRects().length);
      const next=visible[visible.indexOf(control)+(event.shiftKey?-1:1)];
      if(!next)return;
      event.preventDefault();event.stopPropagation();state.uiFocus=key(next);
      // Let the browser commit a dirty value exactly once. A synthetic change
      // leaves the native dirty flag set: replacing the DOM then fires another
      // change from blur and recursively enters the workbench render.
      control.blur();
      if(next.isConnected)next.focus();
      else {
        const currentScope=liveScope();
        [...currentScope?.querySelectorAll("input,select,textarea")??[]]
          .find(candidate=>key(candidate)===state.uiFocus&&!candidate.disabled)?.focus({preventScroll:true});
      }
    };
  }
  for(const button of scope.querySelectorAll("button"))button.addEventListener("focus",()=>{delete state.uiFocus;},{signal:binding.signal});
  // A remembered key is only a fallback after replacement/blur. A live input
  // in this retained dialog is newer than that key, especially while an
  // asynchronous preview returns after the user moved to another field.
  const active=scope.ownerDocument.activeElement;
  if(inputs.includes(active)&&!active.disabled)state.uiFocus=key(active);
  else if(state.uiFocus)inputs.find(control=>key(control)===state.uiFocus&&!control.disabled)?.focus({preventScroll:true});
  if(state.restoreParameterFocus&&!state.parameterEditor&&!view.pending) {
    const target=[...mount.querySelectorAll('[data-cam-action$="parameters-open"]')].find(button=>
      String(button.dataset.tubeDesignerPunchIndex)===state.restoreParameterFocus.index
      &&(button.dataset.tubeDesignerPunchEnd??"")===state.restoreParameterFocus.end
      &&(button.dataset.tubeDesignerPunchEditorMode??"shape")===(state.restoreParameterFocus.mode??"shape"));
    target?.focus({preventScroll:true});delete state.restoreParameterFocus;
  }
  const intent=state.uiPendingClick;
  if(intent&&!view.pending&&!intent.scheduled) {
    intent.scheduled=true;
    // A section response can immediately begin the 3D stage in the caller's
    // next microtask. Wait for that transition, not merely one idle render.
    setTimeout(()=>{
      intent.scheduled=false;
      if(state.uiPendingClick!==intent||view.pending)return;
      // A resource-only viewport failure is an inspection problem, not a
      // manufacturing validation failure.  Do not discard an Apply gesture
      // that was queued while the user was finishing a table cell in that
      // state; the native Apply operation can still calculate the saved recipe.
      const blockingError = state.error && state.error !== state.previewRenderError;
      if(view.tubeDesignerPunchWizard!==state||state.parameterEditor!==intent.editor||blockingError) {
        delete state.uiPendingClick;return;
      }
      const button=[...liveScope()?.querySelectorAll(actionSelector)??[]]
        .find(candidate=>buttonKey(candidate)===intent.identity&&!candidate.disabled);
      delete state.uiPendingClick;button?.click();
    },0);
  }
}
function attachPunchBatchCells(context,view,mount,ops) {
  const batch=view.tubeDesignerPunchBatch;
  const dialog=mount?.querySelector?.('.tube-designer-punch-batch-dialog');
  if(!batch||!dialog)return;
  const send=(control,suffix,value)=>{
    if(view.tubeDesignerPunchBatch!==batch||view.pending||!dialog.isConnected)return;
    const target=dialog.ownerDocument.createElement('input');
    target.type='hidden';target.value=value??'';
    target.dataset.punchBatchPartId=control.dataset.punchBatchPartId;
    target.dataset.punchBatchField=control.dataset.punchBatchField;
    target.dataset.camChangeAction=(control.dataset.camChangeAction??'').replace(/batch-change$/,suffix);
    dialog.append(target);
    target.dispatchEvent(new dialog.ownerDocument.defaultView.Event('change',{bubbles:true}));
    target.remove();
  };
  for(const control of dialog.querySelectorAll('[data-punch-batch-part-id][data-punch-batch-field]')) {
    const originalKeydown=control.onkeydown;
    control.onkeydown=event=>{
      if(event.key!=='Enter'||event.ctrlKey||event.altKey||event.metaKey||event.shiftKey){originalKeydown?.call(control,event);return;}
      if(view.pending)return;
      event.preventDefault();event.stopPropagation();control.blur();
      void Promise.resolve(view.activeAreaAction?.promise).then(()=>send(control,'batch-enter')).catch(()=>{});
    };
    control.onpaste=event=>{
      const text=event.clipboardData?.getData('text/plain')??'';
      if(!text.includes('\t')&&!/[\r\n]/.test(text))return;
      event.preventDefault();event.stopPropagation();send(control,'batch-paste',text);
    };
  }
  const focus=batch.focusCell;
  if(focus&&!view.pending) {
    const control=[...dialog.querySelectorAll('[data-punch-batch-part-id][data-punch-batch-field]')]
      .find(node=>node.dataset.punchBatchPartId===focus.partId&&node.dataset.punchBatchField===focus.field&&!node.disabled);
    if(control){control.focus({preventScroll:true});control.scrollIntoView({block:'nearest',inline:'nearest'});delete batch.focusCell;}
  }
}
function hydrate(context,view,mount,ops) {
  if(!mount || (typeof mount!=="object"&&typeof mount!=="function"))return Promise.resolve(false);
  const state=view.tubeDesignerPunchWizard;
  const host=mount.querySelector?.("[data-tube-designer-punch-viewport]");
  let controller=controllers.get(mount);
  const preview=state?.preview,mode=state?.previewMode??"tools";
  const owner=view.tubeDesignerPunchBatch??state;
  if(!host||!preview) {
    if(host&&controller?.owner===owner&&controller.viewport) {
      if(presentations.get(controller.state)===controller)presentations.delete(controller.state);
      controller.state=state;controller.displayKey=null;controller.failedKey=null;presentations.set(state,controller);
      controller.viewport.setVisibleEntityIds([]);controller.sceneHidden=true;
      delete host.dataset.punchPreviewReady;
      renderPreviewNotice(host,"正在准备当前零件预览");
      return controller.flight??Promise.resolve(false);
    }
    disposePunchController(controller);controllers.delete(mount);
    if(host)host.textContent=state?.creationMode==="main-tube-punch"
      ? "正在准备完整主管预览；添加孔位后可查看主管扣孔结果"
      : "点击“更新实体预览”检查实际刀具裁剪结果";
    return Promise.resolve(false);
  }
  if(controller?.state!==state) {
    if(controller?.owner===owner&&controller.viewport&&controller.host===host) {
      if(presentations.get(controller.state)===controller)presentations.delete(controller.state);
      controller.state=state;controller.displayKey=null;controller.failedKey=null;
    } else {
      disposePunchController(controller);
      controller={state,owner,host,viewport:null,ready:false,flight:null,displayKey:null,failedKey:null};
    }
    controllers.set(mount,controller);
    presentations.set(state,controller);
  }
  mountPunchController(controller,host,mount);
  const unavailable=previewUnavailableReason(state,mode);
  if(unavailable) {
    controller.viewport?.setVisibleEntityIds(loadedPreviewEntities(controller.viewport,preview,mode,unchangedPreviewEntities(state,mode)));
    controller.sceneHidden=true;
    delete host.dataset.punchPreviewReady;
    renderPreviewNotice(host,unavailable);
    return controller.flight??Promise.resolve(false);
  }
  const warning=preview.previewComputed===false
    ? (preview.previewToolsComplete===false?"刀具尚未全部生成，可继续修改参数。":"当前刀具已显示，切割计算未完成。")
    : preview.previewComputed===true&&preview.solidCount!==1
      ? "当前切割结果："+preview.solidCount+" 个实体。可继续编辑，确认时再检查。"
    : "";
  renderPreviewNotice(host,warning,false);
  const references=previewReferences(preview).map(reference=>reference.url+"@"+reference.version).join("|");
  const key="punch:"+mode+":"+references;
  if(controller.flight)return controller.flight;
  if(controller.displayKey===key&&controller.ready) {
    if(controller.sceneHidden)controller.viewport?.setVisibleEntityIds(controller.entityIds??null);
    controller.sceneHidden=false;
    host.dataset.punchPreviewReady="true";
    return Promise.resolve(true);
  }
  if(controller.failedKey===key)return Promise.resolve(false);
  // Resource loading participates in the preview flight, but a batch preview
  // must not take the exclusive form/save lock.
  if(view.pending&&!view.tubeDesignerOperation)return Promise.resolve(false);
  const background=!!view.tubeDesignerPunchBatch&&state.creationMode==="main-tube-punch";
  const ownsOperation=!view.tubeDesignerOperation&&!background;
  if(ownsOperation)beginPunchOperation(context,view,"正在显示主管与刀具体");
  const operation=view.tubeDesignerOperation??previewFlights.get(view)??{background:true};
  operation.renderHolds=(operation.renderHolds??0)+1;
  operation.message="刀具体已生成，正在加载对应网格和材质";
  operation.phaseLabel="显示刀具体";operation.completed=0;operation.total=null;
  state.previewRenderPending=true;state.previewRenderError="";
  delete host.dataset.punchPreviewReady;
  const mounted=()=>controllers.get(mount)===controller&&controller.state===state&&view.tubeDesignerPunchWizard===state;
  const current=()=>mounted()&&state.preview===preview&&previewIsCurrent(state);
  const hideObsolete=()=>{
    if(controllers.get(mount)!==controller)return;
    const live=view.tubeDesignerPunchWizard;
    controller.viewport?.setVisibleEntityIds(loadedPreviewEntities(controller.viewport,live?.preview,live?.previewMode??"tools",unchangedPreviewEntities(live,live?.previewMode??"tools")));
    controller.sceneHidden=true;delete controller.host.dataset.punchPreviewReady;
  };
  controller.flight=Promise.resolve().then(async()=>{
    const {createThreeViewport}=await import("../../../iCAX-UI/SDK/Viewport/threeViewport.mjs");
    if(!current())return false;
    if(!controller.viewport)controller.viewport=createThreeViewport({backgroundColor:0x13252d,continuousRender:false,constrainOrbit:false,projectionMode:"orthographic",
      showProjectionToggle:true,pickingEnabled:false,blankDoubleClickFitEnabled:true,antialias:true,pixelRatioCap:2});
    mountPunchController(controller,controller.host,mount);
    const viewport=controller.viewport;
    // Keep unchanged objects visible while the changed resources arrive.
    const desiredRows=buildPunchPreviewRows(preview,mode);
    viewport.setVisibleEntityIds(loadedPreviewEntities(viewport,preview,mode));
    const camera=controller.ready?viewport.getCameraState():null;
    const boundsKey=JSON.stringify(preview.baseBounds??preview.bounds??{length:state.baseLength??preview.length});
    viewport.retainViewResources(previewReferences(preview));
    const receipt=await viewport.applyViewSnapshot({revision:key,rows:desiredRows,isCurrent:current},context.sceneProxy?.resources);
    if(!current()){hideObsolete();return false;}
    if(!receipt?.applied||!receipt.entityIds?.length||receipt.missingGeometryEntityIds?.length)throw new Error("预览几何未完整进入视口。");
    viewport.retainViewResources(previewReferences(preview));
    // Keep an intentional camera for ordinary hole edits/mode switches, but
    // refit a changed blank. Never restore an uninitialized first-frame camera.
    if(camera&&controller.boundsKey===boundsKey)viewport.setCameraState(camera);
    else {
      const fitted=!camera?initializePunchCamera(viewport,preview,state.baseLength):viewport.fitViewToViewport(1.2);
      if(!fitted)throw new Error("预览几何无法适合窗口。");
    }
    controller.ready=true;controller.displayKey=key;controller.boundsKey=boundsKey;controller.failedKey=null;
    controller.entityIds=receipt.entityIds;controller.sceneHidden=false;
    // Wait for the mounted canvas to reach a displayed frame before unlocking.
    await paint();
    if(!current()){hideObsolete();return false;}
    if(previewUnavailableReason(state,mode)) {
      viewport.setVisibleEntityIds(loadedPreviewEntities(viewport,preview,mode,unchangedPreviewEntities(state,mode)));controller.sceneHidden=true;return false;
    }
    controller.host.dataset.punchPreviewReady="true";
    return true;
  }).catch(error=>{
    if(current()) {
      controller.failedKey=key;
      state.previewRenderError="三维预览显示失败，请点击“更新刀具体”重试。原因："+(error?.message??String(error));
      state.error=state.previewRenderError;
      controller.viewport?.setVisibleEntityIds(loadedPreviewEntities(controller.viewport,preview,mode,unchangedPreviewEntities(state,mode)));
      controller.sceneHidden=true;
      delete controller.host.dataset.punchPreviewReady;
    }
    return false;
  }).finally(()=>{
    controller.flight=null;
    operation.renderHolds=Math.max(0,(operation.renderHolds??1)-1);
    state.previewRenderPending=false;
    if(ownsOperation||operation.finishRequested)finishPunchOperation(view,operation);
    // A part/recipe may have changed during decode. The stable batch owner
    // re-renders the live state and starts its latest resource snapshot.
    if(controllers.get(mount)===controller&&(mounted()||view.tubeDesignerPunchBatch===owner))ops?.renderProject(context,view);
  });
  // The nested render only remounts this same controller and joins its flight.
  // Set the flight first so no DOM replacement starts a duplicate resource load.
  ops?.renderProject(context,view);
  return controller.flight;
}

function mountPunchController(controller,host,mount) {
  if(!controller.viewport){controller.host=host;return;}
  const viewport=controller.viewport;
  if(controller.host===host&&viewport.root.parentElement===host)return;
  stopViewCubeAnimation(controller.cubeView);
  if(controller.host&&controller.viewCubeClick)controller.host.removeEventListener("click",controller.viewCubeClick,true);
  controller.host=host;viewport.mount(host);
  host.insertAdjacentHTML("beforeend",renderViewCube());
  controller.cubeView={viewport};attachViewCube(controller.cubeView,host);
  controller.viewCubeClick=event=>{
    const target=event.target?.closest?.('[data-cam-viewcube] [data-cam-action="view-standard"]');
    if(!target||!host.contains(target))return;
    event.preventDefault();event.stopPropagation();event.stopImmediatePropagation?.();
    viewport.setStandardView(String(target.dataset.camView??"iso"));
  };
  host.addEventListener("click",controller.viewCubeClick,true);
}

function disposePunchController(controller) {
  if(!controller)return;
  if(presentations.get(controller.state)===controller)presentations.delete(controller.state);
  stopViewCubeAnimation(controller.cubeView);
  if(controller.host&&controller.viewCubeClick)controller.host.removeEventListener("click",controller.viewCubeClick,true);
  // A wizard owns only these two contexts; release them when it really closes.
  controller.viewport?.renderer?.forceContextLoss?.();
  controller.viewport?.axisRenderer?.forceContextLoss?.();
  controller.viewport?.dispose();
}
