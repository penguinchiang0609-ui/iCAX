import { escapeAttr as esc, escapeText, formatNumber } from "../../_shared/workbench/utils/format.mjs";
import { capturePaneInteraction } from "../../_shared/workbench/utils/paneInteractionState.mjs";
import { patchDomNode } from "./punchDomPatch.mjs";
import { tubeCadNode, tubeCadRegion, tubeCadPlacement, tubeCadKindLabels, tubeCadLoopDimensions,
  tubeCadEditableNodes, tubeCadCurveFields, tubeCadValue, setTubeCadSectionDimension, editTubeCadCurve,
  setTubeCadLocation, setTubeCadInterval, setTubeCadBlindDepth, addTubeCadHole, tubeCadRotation, setTubeCadRotation, mergeTubeCadDraft, isTubeBoundaryGeometry, isTubeCadDocumentSnapshot, tubeCadCoreSectionNode } from "./tubeCadModel.mjs";
import { createDrawingPreviewKey, invokeTubeCadDocumentPreview, retainDrawingPreviewRenderer, releaseDrawingPreviewResources } from "./partDrawingSession.mjs";
import { buildPartDrawingPreviewRows } from "./partDrawingPreview.mjs";

const PREFIX="tube-designer-cad-";
const renderSessions=new WeakMap(),viewports=new WeakMap();
const clone=value=>structuredClone(value);
export function hasTubeCadDocument(part) {return !!part?.properties?.["tubeDesigner.cadDocument"]?.documentResourceId;}
function selectedNode(state) {
  if(state?.coreOnly&&state.geometry)return tubeCadCoreSectionNode(state.geometry);
  return state?.construction?.SolidNodes.find(node=>node.ID===state.selected);
}
function draftGeometry(state) {return state.coreOnly?state.geometry:state.construction;}
function setDraftGeometry(state,value) {if(state.coreOnly)state.geometry=value;else state.construction=value;}
function snapshotDraft(state,result) {return state.coreOnly?result.geometry:result.construction;}
function acceptDocument(state,result) {
  state.geometry=clone(result.geometry);state.construction=clone(result.construction);state.placement=clone(result.placement);
}
function editPayload(state,value=draftGeometry(state)) {
  return {[state.coreOnly?"boundaryGeometry":"construction"]:clone(value)};
}
function selection(state,id) {
  state.selected=id;
  const region=tubeCadRegion(selectedNode(state));state.loopId=region?.Boundaries?.[0]?.ID??"";
  state.curveId=region?.Boundaries?.[0]?.Curves?.[0]?.ID??"";
}
function eligible(state) {return tubeCadEditableNodes(state.coreOnly?state.geometry:state.construction,"punch");}
function capabilities(state,node) {return state.nodes?.find(item=>item.nodeId===node?.ID);}
function nodeTitle(state,node) {
  if(node.ID==="section")return "管材截面";
  if(node.ID===state.construction.BaseNodeID)return "原材";
  if(/[\u4e00-\u9fff]/u.test(node.Label??""))return node.Label;
  const shape=tubeCadLoopDimensions(node,tubeCadRegion(node)?.Boundaries?.[0]);
  const type=node.Data.type,index=state.construction.SolidNodes.filter(item=>item.Data.type===type).indexOf(node)+1;
  const title=type==="SExtrudedRegionNode"?(shape.kind==="circle"?"圆孔":shape.kind==="rectangle"?"矩形孔":"轮廓切除"):
    type==="SHalfSpaceNode"?"端部截切":type==="SOpeningProfileSweepNode"?"开口剖面":tubeCadKindLabels[type]??"保留构造";
  return `${title} ${index}`;
}
function owned(context,view,state) {return view.tubeDesignerCadEditor===state&&context.sceneProxy===state.sceneProxy
  &&view.scene===state.scene&&!view.disposed&&context.isCurrentProject?.()!==false;}
function current(context,view,state) {return owned(context,view,state)&&view.activeAreaId==="nesting";}
export async function openTubeCadEditor(context,view,part,mode,ops) {
  if(mode!=="punch")throw new Error("二维与三维编辑必须从各自的绘图页面打开。");
  if(!hasTubeCadDocument(part))throw new Error("此零件没有可编辑的管材 CAD 文档。");
  if(typeof context.sceneProxy?.invoke!=="function")throw new Error("当前项目未连接，无法打开 CAD 编辑。");
  const prior=view.tubeDesignerCadEditor;
  if(prior?.part.entityId===part.entityId&&owned(context,view,prior)&&(prior.construction||prior.loading)) {
    if(!eligible(prior).some(node=>node.ID===prior.selected))selection(prior,eligible(prior)[0]?.ID);
    ops.renderProject(context,view);return prior;
  }
  if(prior)closeTubeCadEditor(view,context.mount);
  const state={part:clone(part),scene:view.scene,sceneProxy:context.sceneProxy,mode,loading:true,selected:"",geometry:null,construction:null,placement:null,coreOnly:false,
    draftRevision:0,dirty:false,error:"",history:[],previewResourceKey:createDrawingPreviewKey(),preview:null,
    newHole:{diameter:10,station:part.length/2,offset:0,face:"top"}};
  view.tubeDesignerCadEditor=state;ops.renderProject(context,view);
  try {
    const result=await context.sceneProxy.invoke("TubeDesigner.GetTubeCadDocument",{
      partEntityId:part.entityId,resourceVersion:part.manufacturingGeometryResourceVersion},{timeoutMs:180000});
    if(!owned(context,view,state))return;
    if(!isTubeCadDocumentSnapshot(result))throw new Error("没有返回有效的管材 CAD 文档。");
    Object.assign(state,{geometry:clone(result.geometry),construction:clone(result.construction),placement:clone(result.placement),coreOnly:result.construction.SolidNodes.length===0,revision:result.revision,resourceVersion:result.resourceVersion,
      nodes:result.nodes??[],documentResourceId:result.documentResourceId,documentResourceVersion:result.documentResourceVersion});
    selection(state,eligible(state)[0]?.ID??state.construction.BaseNodeID);
  } catch(error) {if(owned(context,view,state))state.error=error?.message??String(error);throw error;}
  finally {state.loading=false;if(current(context,view,state))ops.renderProject(context,view);}
  return state;
}
function numberControl(state,key,title,value,attributes="",disabled=false) {
  const identity=encodeURIComponent(JSON.stringify([state.selected,key,attributes]));
  const display=disabled&&typeof value==="number"&&Number.isFinite(value)?formatNumber(value,8):value;
  return `<label class="tube-designer-field" id="cad-field-${identity}"><span>${escapeText(title)}</span><input type="text" inputmode="decimal" value="${esc(display??"")}" aria-label="${esc(title)}" data-cam-change-action="${PREFIX}field" data-cad-field="${esc(key)}" data-cad-node="${esc(state.selected)}" ${attributes} ${disabled?"disabled":""}></label>`;
}
function dimensions(state,node) {
  const region=tubeCadRegion(node);if(!region)return "";
  return `<section class="tube-cad-property-section"><h3>${state.coreOnly?"截面尺寸":"孔轮廓尺寸"}</h3>${region.Boundaries.map((loop,index)=>{
    const shape=tubeCadLoopDimensions(node,loop),attrs=`data-cad-loop="${esc(loop.ID)}"`;
    return `<div class="tube-cad-loop-dimensions" id="cad-dim-${esc(loop.ID)}"><h4>${index?`内轮廓 ${index}`:"外轮廓"}</h4>
      ${shape.kind==="circle"?numberControl(state,"radius",state.coreOnly?"直径":"孔径",shape.radius*2,attrs)
        :shape.kind==="rectangle"?numberControl(state,"width","宽度",shape.width,attrs)+numberControl(state,"height","高度",shape.height,attrs)
        :'<p>按精确曲线编辑此轮廓。</p>'}</div>`;
  }).join("")}</section>`;
}
function curveControls(state,node) {
  const region=tubeCadRegion(node);if(!region)return `<p>此构造没有平面轮廓。</p>`;
  const loop=region.Boundaries.find(item=>item.ID===state.loopId)??region.Boundaries[0];
  const curve=loop?.Curves.find(item=>item.ID===state.curveId)??loop?.Curves[0];
  const fields=tubeCadCurveFields(curve);
  return `<section class="tube-cad-property-section"><h3>精确轮廓曲线</h3>
    <label class="tube-designer-field"><span>轮廓</span><select aria-label="选择轮廓" data-cam-change-action="${PREFIX}loop">${region.Boundaries.map((item,index)=>`<option value="${esc(item.ID)}" ${item.ID===loop?.ID?"selected":""}>${escapeText(item.Label??(index?`内轮廓 ${index}`:"外轮廓"))}</option>`).join("")}</select></label>
    <label class="tube-designer-field"><span>曲线</span><select aria-label="选择曲线" data-cam-change-action="${PREFIX}curve">${(loop?.Curves??[]).map((item,index)=>`<option value="${esc(item.ID)}" ${item.ID===curve?.ID?"selected":""}>${index+1} · ${curveName(item.Segment.Curve.type)}</option>`).join("")}</select></label>
    <div class="tube-cad-curve-fields" id="cad-curve-fields-${esc(curve?.ID??"")}">${fields.map(field=>numberControl(state,"curve",field.title,
      tubeCadValue(curve.Segment.Curve.value,field.path),`data-cad-loop="${esc(loop.ID)}" data-cad-curve="${esc(curve.ID)}" data-cad-path="${esc(JSON.stringify(field.path))}"`)).join("")
      ||'<p>当前曲线保留精确几何，暂不支持修改。</p>'}</div></section>`;
}
function curveName(type) {return {Segment2:"直线",Circle2:"圆",Arc2:"圆弧",Ellipse2:"椭圆",EllipseArc2:"椭圆弧",BSpline2:"B 样条",NURBS2:"有理样条",Bezier2:"贝塞尔",Polyline2:"折线"}[type]??"保留曲线";}
function properties(state) {
  const node=selectedNode(state);if(!node)return "<p>请在左侧选择特征。</p>";
  const capability=capabilities(state,node),editable=capability?.geometryEditable!==false;
  const frame=tubeCadPlacement(node),data=node.Data.value,extent=node.Relations?.Extent;
  const unsupported=!editable&&!tubeCadRegion(node);
  let html=`<h2>${escapeText(nodeTitle(state,node))}<small>尺寸单位：mm</small></h2>
    ${state.coreOnly?"":`<label class="tube-designer-field"><span>特征名称</span><input type="text" aria-label="特征名称" value="${esc(nodeTitle(state,node))}" data-cam-change-action="${PREFIX}label" data-cad-node="${esc(node.ID)}"></label>`}`;
  if(unsupported)return html+'<p class="tube-cad-preserved">此导入构造保留精确实体和原始关系，当前没有可安全编辑的几何参数。</p>';
  if(frame)html+=`<section class="tube-cad-property-section"><h3>零件坐标中的构造位置</h3>${["X","Y","Z"].map(axis=>numberControl(state,`location-${axis}`,axis==="X"?"轴向位置 X":`位置 ${axis}`,frame.Location[axis],"",!editable)).join("")}</section>`;
  if(frame&&node.ID!==state.construction.BaseNodeID) {
    const rotation=tubeCadRotation(node);
    html+=`<details class="tube-cad-property-section"><summary>构造方向（°）</summary>${["X","Y","Z"].map(axis=>numberControl(state,`rotation-${axis}`,`绕 ${axis} 旋转`,Number(rotation[axis].toFixed(6)),"",!editable)).join("")}</details>`;
  }
  html+=dimensions(state,node);
  if(Object.hasOwn(data,"First"))html+=`<section class="tube-cad-property-section"><h3>构造范围</h3>${numberControl(state,"First","起始位置",data.First,"",!!extent?.RecomputeAgainstHost)}${numberControl(state,"Last","结束位置",data.Last,"",!!extent?.RecomputeAgainstHost)}
    ${extent?.RecomputeAgainstHost?`<p>${["固定范围","贯穿全部宿主材料","贯穿选定材料边界","从宿主边界形成盲孔","封闭体内切除"][extent.Rule]??"宿主材料约束"}</p>`:""}
    ${extent?.Rule===3&&extent.Depth?numberControl(state,"depth","盲孔深度",extent.Depth.Value):""}
    ${Object.hasOwn(data,"FirstScale")?numberControl(state,"FirstScale","起端缩放比例",data.FirstScale)+numberControl(state,"LastScale","末端缩放比例",data.LastScale):""}</section>`;
  if(tubeCadRegion(node))html+=`<details class="tube-cad-property-section"><summary>精确轮廓曲线</summary>${curveControls(state,node)}</details>`;
  return html;
}
function holeControls(state) {
  if(state.coreOnly)return "";
  const data=state.newHole;
  return `<section class="tube-cad-add-hole"><h3>新增贯穿圆孔</h3><p>贯穿原材全部材料，包括此方向上遇到的各层管壁。轴向位置以原材构造起点为参考。</p>${[["diameter","孔径"],["station","轴向位置"],["offset","横向偏移"]].map(([key,title])=>
    `<label class="tube-designer-field"><span>${title}</span><input type="text" inputmode="decimal" value="${esc(data[key])}" aria-label="新增${title}" data-cam-change-action="${PREFIX}new-hole-field" data-cad-new-hole-field="${key}"></label>`).join("")}
    <label class="tube-designer-field"><span>构造方向</span><select aria-label="新增孔方向" data-cam-change-action="${PREFIX}new-hole-field" data-cad-new-hole-field="face">${[["top","从上向下"],["bottom","从下向上"],["front","从前向后"],["back","从后向前"]].map(([value,title])=>`<option value="${value}" ${data.face===value?"selected":""}>${title}</option>`).join("")}</select></label>
    <button type="button" class="tube-designer-primary" data-cam-action="${PREFIX}add-hole">添加圆孔</button></section>`;
}
export function renderTubeCadEditor(view) {
  const state=view.tubeDesignerCadEditor;if(!state||view.activeAreaId!=="nesting")return "";
  return `<div class="tube-cad-backdrop"><section class="tube-cad-editor" role="dialog" aria-modal="true" aria-label="冲孔编辑">
    <header><strong>${escapeText(state.part.name??"管材零件")} · 冲孔编辑</strong><nav><button type="button" data-cam-action="tube-designer-part-open-sketch" data-tube-designer-part-id="${esc(state.part.entityId)}">二维编辑</button><button type="button" data-cam-action="tube-designer-drawing-open" data-tube-designer-part-id="${esc(state.part.entityId)}">三维编辑</button></nav><button type="button" aria-label="关闭 CAD 编辑" data-cam-action="${PREFIX}close" ${state.saving?"disabled":""}>×</button></header>
    <div class="tube-cad-layout"><aside class="tube-cad-sidebar" aria-label="构造特征"><h2>孔与切除特征</h2>
      <div role="tree">${(draftGeometry(state)?eligible(state):[]).map(node=>`<button type="button" role="treeitem" aria-selected="${state.selected===node.ID}" data-cam-action="${PREFIX}select" data-cad-node="${esc(node.ID)}"><strong>${escapeText(nodeTitle(state,node))}</strong><small>${escapeText(tubeCadKindLabels[node.Data.type]??"保留构造")}</small></button>`).join("")}</div>${holeControls(state)}</aside>
      <div class="tube-cad-center"><div data-tube-cad-viewport data-side-sketch-viewport></div><p>${state.previewPending?"正在更新三维草稿预览…":state.preview?.draftRevision===state.draftRevision&&state.dirty?"三维视图显示当前 CAD 草稿；保存后写入零件文档。":state.dirty?"正在准备 CAD 草稿预览…":"三维视图显示当前 CAD 文档。"}</p></div>
      <aside class="tube-cad-properties" aria-label="几何参数">${draftGeometry(state)?properties(state):'<p>正在读取管材 CAD 文档…</p>'}</aside></div>
    <footer><span role="alert">${escapeText(state.error||"")}</span>${state.conflict?`<button type="button" data-cam-action="${PREFIX}reload">重新读取已保存文档</button>`:""}<button type="button" data-cam-action="${PREFIX}undo" ${!state.history.length||state.saving||state.conflict?"disabled":""}>撤销修改</button><button type="button" class="tube-designer-primary" data-cam-action="${PREFIX}save" ${!state.dirty||state.loading||state.saving||state.conflict?"disabled":""}>${state.saving?"正在保存…":"保存 CAD 修改"}</button><button type="button" data-cam-action="${PREFIX}close" ${state.saving?"disabled":""}>完成</button></footer>
  </section></div>`;
}
export function patchTubeCadEditor(view,mount) {
  const root=mount?.querySelector?.(".tube-cad-backdrop"),state=view.tubeDesignerCadEditor;
  if(!state) {
    if(root&&renderSessions.get(mount)){root.remove();disposeTubeCadEditor(mount);renderSessions.delete(mount);}
    return false;
  }
  if(!root||renderSessions.get(mount)!==state||view.activeAreaId!=="nesting")return false;
  const restore=capturePaneInteraction(mount,[".cam-context-pane",".cam-info-pane",".tube-cad-sidebar",".tube-cad-properties"]);
  const template=mount.ownerDocument.createElement("template");template.innerHTML=renderTubeCadEditor(view);
  state.domPatching=true;
  try {patchDomNode(root,template.content.firstElementChild);restore();}
  finally {state.domPatching=false;}
  return true;
}
export function disposeTubeCadEditor(mount) {
  const controller=viewports.get(mount);if(!controller)return;
  viewports.delete(mount);controller.viewport?.dispose();controller.canvas?.remove();
  const state=controller.state;
  // DOM remounts and mode switches retain the editor's document session. Only
  // closing that session releases native preview resources.
  Promise.resolve(controller.flight).finally(()=>controller.releaseRenderer?.());
  if(controller.closed) {
    clearTimeout(state.previewTimer);state.previewClosed=true;
    void releaseDrawingPreviewResources(state);
  }
}
export function closeTubeCadEditor(view,mount) {
  const state=view.tubeDesignerCadEditor,controller=viewports.get(mount);
  if(controller)controller.closed=true;
  if(state){clearTimeout(state.previewTimer);state.previewClosed=true;void releaseDrawingPreviewResources(state);}
  disposeTubeCadEditor(mount);
}
function changed(state) {state.dirty=true;state.draftRevision++;state.error="";}
export async function handleTubeCadEditorAction(context,view,action,target,ops) {
  const opening={"tube-designer-punch-open":"punch"}[action];
  const id=String(target?.dataset?.tubeDesignerPartId??"");
  if(opening&&id) {
    const part=view.scene?.tubeDesigner?.nestingGroups?.flatMap(group=>group.parts??[]).find(item=>String(item.entityId)===id);
    if(!part)throw new Error("当前下料零件已不存在，请重新选择。");
    await openTubeCadEditor(context,view,{...part,independentNesting:true},opening,ops);
    return {handled:true};
  }
  if(!action.startsWith(PREFIX))return {handled:false};
  const state=view.tubeDesignerCadEditor;if(!state)return {handled:true};
  const suffix=action.slice(PREFIX.length);
  // A replaced editor can emit blur/change while its old controls are being
  // removed. Those events do not belong to the newly loading session.
  if(!draftGeometry(state)&&!["close","reload"].includes(suffix))return {handled:true};
  if(["field","label"].includes(suffix)&&target.dataset.cadNode&&target.dataset.cadNode!==state.selected)return {handled:true};
  try {
    if(suffix==="close") {if(!state.saving){closeTubeCadEditor(view,context.mount);view.tubeDesignerCadEditor=null;}}
    else if(suffix==="reload"&&!state.saving) {
      closeTubeCadEditor(view,context.mount);view.tubeDesignerCadEditor=null;
      await openTubeCadEditor(context,view,state.part,state.mode,ops);
    }
    else if(draftGeometry(state)) {
      if(suffix==="select")selection(state,target.dataset.cadNode);
      else if(suffix==="loop") {state.loopId=target.value;state.curveId=tubeCadRegion(selectedNode(state)).Boundaries.find(loop=>loop.ID===target.value)?.Curves[0]?.ID??"";}
      else if(suffix==="curve")state.curveId=target.value;
      else if(suffix==="new-hole-field")state.newHole[target.dataset.cadNewHoleField]=target.dataset.cadNewHoleField==="face"?target.value:target.value.trim()===""?NaN:Number(target.value);
      else if(suffix==="undo"&&!state.saving&&state.history.length) {setDraftGeometry(state,state.history.pop());changed(state);if(!eligible(state).some(node=>node.ID===state.selected))selection(state,eligible(state)[0]?.ID);}
      else if(suffix==="save")await save(context,view,state,ops);
      else if(["field","label","add-hole"].includes(suffix)) {
        const before=clone(draftGeometry(state));
        try {
          if(suffix==="add-hole")selection(state,addTubeCadHole(state.construction,state.newHole));
          else if(suffix==="label")tubeCadNode(draftGeometry(state),target.dataset.cadNode).Label=String(target.value).slice(0,160);
          else {
            const key=target.dataset.cadField,value=target.value.trim()===""?NaN:Number(target.value);
            if(key==="curve")editTubeCadCurve(draftGeometry(state),state.selected,target.dataset.cadLoop,target.dataset.cadCurve,JSON.parse(target.dataset.cadPath),value);
            else if(["radius","width","height"].includes(key))setTubeCadSectionDimension(draftGeometry(state),state.selected,target.dataset.cadLoop,key,key==="radius"?value/2:value);
            else if(key.startsWith("location-"))setTubeCadLocation(state.construction,state.selected,key.slice(9),value);
            else if(key.startsWith("rotation-"))setTubeCadRotation(state.construction,state.selected,key.slice(9),value);
            else if(key==="depth")setTubeCadBlindDepth(state.construction,state.selected,value);
            else setTubeCadInterval(state.construction,state.selected,key,value);
          }
        } catch(error) {setDraftGeometry(state,before);throw error;}
        if(JSON.stringify(before)!==JSON.stringify(draftGeometry(state))) {
          state.history.push(before);state.history=state.history.slice(-50);changed(state);
        } else if(!state.error)return {handled:true};
      }
    }
  } catch(error) {state.error=error?.message??String(error);}
  if(view.activeAreaId==="nesting"&&(view.tubeDesignerCadEditor===state||suffix==="close")) {
    if(state.domPatching)queueMicrotask(()=>{if(current(context,view,state))ops.renderProject(context,view);});
    else ops.renderProject(context,view);
  }
  return {handled:true};
}
async function save(context,view,state,ops) {
  if(state.saving||state.conflict||!state.dirty)return;
  const submitted=state.draftRevision,geometry=clone(draftGeometry(state)),historyAtSubmission=state.history.length;state.saving=true;state.error="";ops.renderProject(context,view);
  try {
    const result=await state.sceneProxy.invoke("TubeDesigner.ApplyTubeCadDocument",{partEntityId:state.part.entityId,
      resourceVersion:state.resourceVersion,revision:state.revision,...editPayload(state,geometry)},{timeoutMs:180000});
    if(!owned(context,view,state))return;
    if(!isTubeCadDocumentSnapshot(result))throw new Error("CAD 修改未返回有效文档。");
    Object.assign(state,{revision:result.revision,resourceVersion:result.resourceVersion,nodes:result.nodes??state.nodes,
      documentResourceId:result.documentResourceId,documentResourceVersion:result.documentResourceVersion});
    if(result.snapshot?.tubeDesigner)view.scene.tubeDesigner=result.snapshot.tubeDesigner;
    const part=view.scene.tubeDesigner?.nestingGroups?.flatMap(group=>group.parts??[]).find(item=>item.entityId===state.part.entityId);
    if(part)state.part=clone(part);
    if(submitted===state.draftRevision){acceptDocument(state,result);state.dirty=false;state.history=[];}
    else {
      try {
        const merged=mergeTubeCadDraft(geometry,draftGeometry(state),snapshotDraft(state,result));
        const history=state.history.slice(historyAtSubmission).map(draft=>mergeTubeCadDraft(geometry,draft,snapshotDraft(state,result)));
        acceptDocument(state,result);setDraftGeometry(state,merged);state.history=history;
      } catch(error) {state.conflict=true;throw error;}
    }
    view.tubeDesignerPartViewportKey="";view.tubeDesignerPartMeasurementState=null;
    ops.appendProjectLog?.(context,"info","已保存管材 CAD 修改。");
  } finally {state.saving=false;}
}
export function attachTubeCadEditor(context,view,mount,ops) {
  const state=view.tubeDesignerCadEditor,host=mount?.querySelector?.("[data-tube-cad-viewport]");
  if(!state||!host){disposeTubeCadEditor(mount);return;}
  renderSessions.set(mount,state);
  let controller=viewports.get(mount);
  if(controller?.state!==state||controller.host!==host){disposeTubeCadEditor(mount);controller={state,host,mode:state.mode,releaseRenderer:retainDrawingPreviewRenderer(state)};viewports.set(mount,controller);}
  {
    schedulePreview(context,view,state,ops);
    if(!controller.loading&&!controller.creating)controller.flight=showSolid(controller,view,context,ops).catch(error=>{
    if(current(context,view,state)){state.error=error?.message??String(error);const message=mount.querySelector(".tube-cad-editor footer [role=alert]");if(message)message.textContent=state.error;}
    });
  }
}
function schedulePreview(context,view,state,ops) {
  if(!draftGeometry(state)||!state.dirty||state.saving||state.conflict||state.previewClosed)return;
  const signature=JSON.stringify([state.resourceVersion,state.revision,draftGeometry(state)]);
  if(signature===state.previewCompletedSignature||signature===state.previewSignature&&(state.previewRunning||state.previewTimer))return;
  state.previewSignature=signature;clearTimeout(state.previewTimer);
  const run=async()=>{
    state.previewTimer=null;
    if(state.previewRunning)return;
    if(!current(context,view,state)||state.saving||state.previewClosed)return;
    const currentSignature=state.previewSignature,revision=state.draftRevision,historyAtSubmission=state.history.length;
    const payload={partEntityId:state.part.entityId,resourceVersion:state.resourceVersion,revision:state.revision,
      ...editPayload(state),previewResourceKey:state.previewResourceKey};
    state.previewRunning=true;state.previewPending=true;ops.renderProject(context,view);
    try {
      const result=await invokeTubeCadDocumentPreview(state.sceneProxy,state,payload,{timeoutMs:180000});
      if(!owned(context,view,state)||state.previewClosed
        ||state.revision!==payload.revision||state.resourceVersion!==payload.resourceVersion)return;
      if(!result.geometry?.url)throw new Error("CAD 草稿没有返回可显示的三维预览。");
      if(state.draftRevision!==revision) {
        if(isTubeBoundaryGeometry(result.documentGeometry)&&Array.isArray(result.documentConstruction?.SolidNodes)) {
          try {
            const merged=mergeTubeCadDraft(payload[state.coreOnly?"boundaryGeometry":"construction"],draftGeometry(state),state.coreOnly?result.documentGeometry:result.documentConstruction);
            const history=state.history.slice(historyAtSubmission).map(draft=>mergeTubeCadDraft(payload[state.coreOnly?"boundaryGeometry":"construction"],draft,state.coreOnly?result.documentGeometry:result.documentConstruction));
            setDraftGeometry(state,merged);state.history.splice(historyAtSubmission,state.history.length-historyAtSubmission,...history);
          } catch(error) {state.error="预览约束与继续输入的字段发生冲突，已保留草稿并重新预览。";}
        }
        state.previewSignature=JSON.stringify([state.resourceVersion,state.revision,draftGeometry(state)]);
        return;
      }
      if(!isTubeBoundaryGeometry(result.documentGeometry)||!Array.isArray(result.documentConstruction?.SolidNodes))throw new Error("CAD 草稿没有返回有效的边界文档。");
      acceptDocument(state,{geometry:result.documentGeometry,construction:result.documentConstruction,placement:result.documentPlacement});
      state.preview={...result,draftRevision:revision};
      state.previewCompletedSignature=JSON.stringify([state.resourceVersion,state.revision,draftGeometry(state)]);
      state.previewSignature=state.previewCompletedSignature;
    } catch(error) {
      if(owned(context,view,state)&&state.previewSignature===currentSignature){state.error=error?.message??String(error);state.previewCompletedSignature=currentSignature;}
    } finally {
      state.previewRunning=false;state.previewPending=false;
      if(current(context,view,state))ops.renderProject(context,view);
      if(owned(context,view,state)&&!state.previewClosed&&state.previewSignature!==currentSignature&&state.previewSignature!==state.previewCompletedSignature)
        state.previewTimer=setTimeout(run,160);
    }
  };
  state.previewTimer=setTimeout(run,160);
}
async function showSolid(controller,view,context,ops) {
  if(!controller.viewport&&!controller.creating) {
    controller.creating=true;
    const create=ops.createPartDrawingViewport??(await import("../../../iCAX-UI/SDK/Viewport/threeViewport.mjs")).createThreeViewport;
    const viewport=await create({backgroundColor:0x13252d,continuousRender:false,constrainOrbit:false,projectionMode:"orthographic",showProjectionToggle:true,blankDoubleClickFitEnabled:true});
    controller.creating=false;
    if(viewports.get(context.mount)!==controller){viewport.dispose();return;}
    controller.viewport=viewport;viewport.mount(controller.host);
  }
  const viewport=controller.viewport;if(!viewport)return;
  const state=controller.state,part=state.part,preview=state.preview?.draftRevision===state.draftRevision&&state.dirty?state.preview:null;
  const key=preview?`preview:${preview.geometry.url}@${preview.geometry.version}`:`${part.thumbnailGeometryResourceId}@${part.thumbnailGeometryResourceVersion}`;
  if(controller.key===key||controller.loading)return;
  if(!part.thumbnailGeometryResourceId)throw new Error("当前零件缺少三维显示资源。");
  controller.loading=true;
  try {
    const rows=preview?buildPartDrawingPreviewRows(preview):[{entityId:part.entityId,data:{
      geometry:{url:part.thumbnailGeometryResourceId,version:part.thumbnailGeometryResourceVersion},geometryKind:1,renderClass:1,meshEdges:true,visible:true,selectable:false}}];
    const receipt=await viewport.applyViewSnapshot({revision:`tube-cad:${key}`,rows},context.sceneProxy.resources);
    if(viewports.get(context.mount)!==controller)return;
    if(!receipt?.applied)throw new Error("CAD 三维视图加载失败。");
    const initial=!controller.key;controller.key=key;
    if(initial)viewport.setStandardView?.("top-front");viewport.fitViewToViewport?.(1.15);
  } finally {
    controller.loading=false;
    const latest=controller.state.part,latestPreview=controller.state.preview?.draftRevision===controller.state.draftRevision&&controller.state.dirty?controller.state.preview:null;
    const desired=latestPreview?`preview:${latestPreview.geometry.url}@${latestPreview.geometry.version}`:`${latest.thumbnailGeometryResourceId}@${latest.thumbnailGeometryResourceVersion}`;
    if(viewports.get(context.mount)===controller&&controller.mode!=="sketch"
      &&desired!==key)
      await showSolid(controller,view,context,ops);
  }
}
