import { escapeAttr as esc, escapeText as txt } from "../../_shared/workbench/utils/format.mjs";
import { renderRibbonCommandIcon as icon } from "../../../iCAX-UI/SDK/AppShell/app/ribbonIcons.mjs";
import { fieldControl, parameterFields } from "./partDrawingParameters.mjs";
import { drawingToolDescriptor, isDrawingToolReadOnly, hasFrozenDrawingTool } from "./partDrawingModel.mjs";

const action=name=>"tube-designer-drawing-"+name;
const label=t=>typeof t?.displayName==="string"?t.displayName:t?.displayName?.["zh-CN"]??t?.toolLabel??t?.type??"刀具";
const btn=(name,title,extra="",symbol="")=>`<button type="button" data-cam-action="${action(name)}" ${extra}>${symbol==="close"?'<span class="command-icon" aria-hidden="true">×</span>':symbol?icon(symbol):""}<span>${txt(title)}</span></button>`;
const input=(key,title,value,unit="",end="")=>fieldControl(action,key,title,value,{unit},end);
const select=(key,title,value,options,end="")=>fieldControl(action,key,title,value,{options:options.map(([value,label])=>({value,label}))},end);
const group=(title,body,open=true)=>`<details class="td-draw-properties" ${open?"open":""}><summary>${title}</summary><div class="tube-designer-punch-field-grid">${body}</div></details>`;
const mainField=(key,title,value,type="number")=>`<label><span>${title}</span><input type="${type}" value="${esc(value)}" step="any" data-cam-change-action="${action("main-change")}" data-drawing-field="${key}"></label>`;

function toolbar(view) {
  const m=view.tubeDesignerPartDrawing,s=m.state;
  const busy=view.pending?"disabled":"",ready=(!m.mainApplied||s.catalogueStatus!=="ready"||view.pending)?"disabled":"";
  const f=s.features.find(f=>f.id===m.selected),locked=f&&isDrawingToolReadOnly(s,f);
  const active=m.mode!=="feature"?m.mode:drawingToolDescriptor(s,s.draft)?.requiresSection?"branch":s.draft.toolTarget==="part"?"v-notch":"hole";
  const selected=(mode)=>`aria-pressed="${active===mode}"`;
  const tool=(kind,title,symbol,extra="")=>btn("command",title,`${ready} data-drawing-command="${kind}" ${selected(kind)} ${extra}`,symbol);
  return `<nav class="td-draw-ribbon" aria-label="三维建模工具">
    <div class="td-draw-ribbon-group">${btn("command","主管",`${busy} data-drawing-command="main" ${selected("main")}`,"base")}${tool("branch","支管相贯","branch")}${tool("v-notch","V 槽","bevel")}<small>建模</small></div>
    <div class="td-draw-ribbon-group">${tool("start","起点切断","cut")}${tool("end","终点切断","cut")}<small>端部</small></div>
    <div class="td-draw-ribbon-group td-draw-edit-tools">${btn("undo","撤销",`${busy} ${!s.history?.length?"disabled":""}`,"undo")}${btn("redo","重做",`${busy} ${!s.future?.length?"disabled":""}`,"redo")}${btn("selected-copy","复制",`${busy} ${!f||locked?"disabled":""}`,"merge")}${btn("selected-toggle",f?.enabled===false?"启用":"停用",`${busy} ${!f||locked?"disabled":""}`,"display")}${btn("selected-remove","删除",`${busy} ${!f&&!["start","end"].includes(m.selected)?"disabled":""}`,"delete")}<small>编辑特征</small></div>
    <div class="td-draw-ribbon-group td-draw-complete">${btn("apply","确定",`${busy} ${m.mode||!m.mainApplied?"disabled":""} title="${m.part?"计算并保存零件":"计算并生成零件"}"`,"apply")}${btn("cancel","取消",busy,"close")}<small>完成</small></div>
  </nav>`;
}
function tree(view) {
  const m=view.tubeDesignerPartDrawing,s=m.state;
  const row=(id,title,symbol,sub="",muted=false)=>`<button type="button" role="treeitem" aria-selected="${m.selected===id}" class="td-draw-tree-node ${muted?"is-muted":""}" data-cam-action="${action("select-node")}" data-drawing-node="${esc(id)}" ${view.pending?"disabled":""}>${icon(symbol)}<span>${txt(title)}${sub?`<small>${txt(sub)}</small>`:""}</span>${muted?'<small>停用</small>':""}</button>`;
  return `<section class="td-draw-tree" aria-label="建模历史"><header><strong>特征树</strong><span>${s.features.length+1+Object.values(s.ends).filter(e=>e.type!=="keep").length} 项</span></header><div role="tree" aria-label="零件特征">
    ${row("main","主管 · 拉伸基体","base",`${s.drawing.section?.name??"请选择截面"} · ${s.drawing.length} mm`)}
    ${["start","end"].filter(e=>s.ends[e]?.type!=="keep").map(e=>row(e,(e==="start"?"起点":"终点")+" · "+label(drawingToolDescriptor(s,s.ends[e])??s.ends[e]),"cut")).join("")}
    ${s.features.map((f,i)=>row(f.id,`${i+1}. ${label(drawingToolDescriptor(s,f)??f)}`,f.toolTarget==="part"?(f.section?"branch":"bevel"):"hole",isDrawingToolReadOnly(s,f)?"定式 · 仅可删除":`${f.station} mm${f.arrayCount>1||f.rowCount>1?` · 阵列 ${f.arrayCount} × ${f.rowCount}`:""}`,f.enabled===false)).join("")}
    ${!s.features.length?'<p>应用主管后，从上方添加支管、V 槽或端部切割。</p>':""}</div></section>`;
}
function featureParameters(view,renderSection) {
  const m=view.tubeDesignerPartDrawing,s=m.state,f=s.draft,t=drawingToolDescriptor(s,f);
  const locked=isDrawingToolReadOnly(s,f),isPart=f.toolTarget==="part";
  const type=t?.requiresSection?renderSection(view,"branch"):
    select("tool",isPart?"槽口类型":"孔形",f.toolRef?.id??f.type,s.tools.filter(t=>t.target===(isPart?"part":"side")&&!t.requiresSection).map(t=>[t.id,label(t)]));
  if(locked)return `<div class="td-draw-readonly"><strong>${hasFrozenDrawingTool(f)?"已退化为定式刀具":"缺少原版刀具"}</strong><p>保留已保存的几何，仅可删除此节点。</p><small>${txt(f.toolRef?.id)} @ ${txt(f.toolRef?.version)}</small></div>`;
  const params=(keys)=>parameterFields(action,{...t,parameters:t?.parameters?.filter(p=>keys.includes(p.key))},f);
  const extraGrooveKeys=["rootRadius","rootWidth","reliefDiameter","reliefLift"];
  const geometryFields=t?.id==="v-notch"
    ? parameterFields(action,{...t,parameters:t.parameters.filter(p=>!extraGrooveKeys.includes(p.key))},f)
    : parameterFields(action,t,f);
  const extraGrooveFields=t?.id==="v-notch"?params(extraGrooveKeys):"";
  const position=input("station","沿主管位置",f.station,"mm")+select("reference","定位基准",f.reference,[["start","距起点"],["end","距终点"],["center","距中心"]]);
  // Reuse the persisted placement fields: columns are X, rows are Y/Z or rotation about X.
  const arrayModes=[["top","沿 Y 轴多排"],["left","沿 Z 轴多排"],["round","绕 X 轴圆周多排"]];
  const axialSign=(f.reference==="end"?-1:1)*f.arrayPitch<0?"negative":"positive";
  const rowDirections=f.face==="round"?[["positive","正转（绕 +X）"],["negative","反转（绕 −X）"]]
    :f.face==="left"||f.face==="right"?[["positive","向上（+Z）"],["negative","向下（−Z）"]]
    :[["positive","Y 正向（+Y）"],["negative","Y 负向（−Y）"]];
  const axialArray=input("arrayCount","X 向数量",f.arrayCount)+input("arraySpacing","X 向间距",Math.abs(f.arrayPitch),"mm")
    +`<div class="wide">${select("arrayDirection","X 向排列方向",axialSign,[["positive","X 正向（起点 → 终点）"],["negative","X 负向（终点 → 起点）"]])}</div>`;
  const array=axialArray+(isPart?`<div class="wide">${select("drawingArrayMode","多排方向",f.face,arrayModes)}</div>`:"")
    +input("rowCount",f.face==="round"?"圆周排数":"横向排数",f.rowCount)
    +input("rowSpacing",f.face==="round"?"角度间隔":"排间距",Math.abs(f.rowPitch),f.face==="round"?"°":"mm")
    +`<div class="wide">${select("rowDirection","多排排列方向",f.rowPitch<0?"negative":"positive",rowDirections)}</div>`
    +`<small class="wide">数量包含原刀具，共 ${f.arrayCount} × ${f.rowCount} = ${f.arrayCount*f.rowCount} 个位置。${f.face==="round"?"从 X 正端看向原点：正转为逆时针。":""}</small>`;
  return `<fieldset ${view.pending?"disabled":""} class="tube-designer-punch-controls">
    ${group(t?.requiresSection?"截面形状":isPart?"槽口类型":"开孔形状",`<div class="wide">${type}</div>`)}
    ${t?.requiresSection?group("定位",position+params(["offsetY","offsetZ"]))+group("旋转",params(["angle","azimuth","roll"]))+group("拉伸切除",params(["direction","length"])):
      group("几何参数",geometryFields)+(extraGrooveFields?group("附加槽底处理",extraGrooveFields,extraGrooveKeys.some(key=>Number(f.toolParameters?.[key])>0)):"")+group("定位",position+(isPart?"":select("face","加工方向",f.face,[["top","上方 +Z"],["bottom","下方 −Z"],["left","左方 −Y"],["right","右方 +Y"],["round","周向"]])+input("offset",f.face==="round"?"周向角度":"横向偏移",f.offset,f.face==="round"?"°":"mm")+input("rotation","面内旋转",f.rotation,"°")+select("endDatum","端面基准",f.endDatum,[["long","长点"],["center","中心"],["short","短点"]])))}
    ${group("阵列",array,f.arrayCount>1||f.rowCount>1)}
    ${!isPart?group("切除选项",[ ["through","贯穿管材"],["opposite","对侧同孔"] ].map(([key,title])=>fieldControl(action,key,title,f[key],{valueType:"boolean"})).join(""),false):""}
  </fieldset>`;
}
function inspector(view,renderSection) {
  const m=view.tubeDesignerPartDrawing,s=m.state;
  let title="选择特征",body='<p class="td-draw-inspector-hint">从上方选择建模工具，或在特征树中选择已有操作。</p>',canApply=!!m.mode;
  if(m.mode==="main") {
    title=m.mainApplied?"编辑主管":"建立主管";
    const locked=[...s.features,...Object.values(s.ends)].some(f=>isDrawingToolReadOnly(s,f));
    canApply=!locked;
    body=`<fieldset class="tube-designer-punch-controls" ${view.pending||locked?"disabled":""}>${group("主管截面",`<div class="wide">${renderSection(view,"main")}</div>`)}${group("拉伸长度",mainField("length","管长（mm）",s.drawing.length))}
      ${!m.part?group("零件信息",mainField("name","名称",s.drawing.name,"text")+mainField("quantity","数量",s.drawing.quantity)+mainField("material","材料",s.drawing.material,"text"),false):""}</fieldset>${locked?'<p class="td-draw-readonly">有退化定式刀具，主管截面和长度已锁定。</p>':""}`;
  } else if(m.mode==="feature") {
    title=(s.editingId?"编辑 · ":"添加 · ")+label(drawingToolDescriptor(s,s.draft)??s.draft);
    body=featureParameters(view,renderSection);canApply=!isDrawingToolReadOnly(s,s.draft);
  } else if(["start","end"].includes(m.mode)) {
    const end=m.mode,e=s.ends[end],t=drawingToolDescriptor(s,e),locked=isDrawingToolReadOnly(s,e);
    title=end==="start"?"起点端部":"终点端部";canApply=!locked;
    body=locked?'<p class="td-draw-readonly">该端部刀具已退化为定式，仅可删除节点。</p>':`<fieldset class="tube-designer-punch-controls" ${view.pending?"disabled":""}>${group("切割方式",select("tool","端部刀具",e.toolRef?.id??"keep",[["keep","保留原端面"],...s.tools.filter(t=>t.target==="end").map(t=>[t.id,label(t)])],end))}${e.type!=="keep"?(t?.requiresSection?renderSection(view,end):"")+group("形状",parameterFields(action,t,e,end))+group("定位",input("trim","向内修剪",e.trim??0,"mm",end)+input("rotation","绕主管旋转",e.rotation??0,"°",end)+select("datum","尺寸基准",e.datum??"long",[["long","长点"],["center","中心"],["short","短点"]],end)):""}</fieldset>`;
  }
  return `<section class="td-draw-inspector" aria-label="特征参数"><header><strong>${txt(title)}</strong>${m.mode?'<small>仅更新当前特征预览</small>':""}</header><div class="td-draw-property-scroll">${body}</div>${m.mode?`<footer>${canApply?btn("commit-operation",m.mode==="main"?"应用主管":"加入特征树",view.pending||s.previewPending?"disabled":"","apply"):""}${btn("cancel-operation","取消本次",view.pending?"disabled":"")}</footer>`:""}</section>`;
}
export function renderPartDrawingWorkbench(view,renderSection) {
  const m=view.tubeDesignerPartDrawing,s=m?.state;
  if(!m||!s)return "";
  const current=s.previewPending?"正在更新模型…":s.preview?.revision===s.revision?"预览已更新":s.preview?.isOriginal?"已保存的实体":"等待预览";
  return `<div class="tube-designer-modal-backdrop td-draw-backdrop"><section class="td-draw-workbench" role="dialog" aria-modal="true" aria-label="三维绘制零件">
    <header class="td-draw-title">${icon("edit3d")}<strong>三维绘制零件</strong><span>${txt(s.drawing.name??m.part?.name??"未命名零件")}</span><small>单位：mm</small></header>
    ${toolbar(view)}
    <div class="td-draw-body"><aside class="td-draw-sidebar" aria-label="特征树">${tree(view)}</aside>
      <main class="td-draw-canvas"><div class="td-draw-view-toolbar"><strong>主管与特征预览</strong><span>${s.drawing.length} mm · ${txt(s.drawing.section?.name??"主管")}</span></div>
        <div class="td-draw-preview-host" data-part-drawing-viewport></div>
        ${s.error?`<div class="td-draw-error" role="alert">${txt(s.error)}</div>`:""}
        <div class="td-draw-canvas-status" data-punch-preview-status>${txt(current)} · 编辑过程中不切除主管</div>
      </main><aside class="td-draw-parameters" aria-label="右侧参数面板">${inspector(view,renderSection)}</aside></div>
    <footer class="td-draw-status"><span>${m.mode?"设置参数 → 加入特征树 → 继续建模":"选择特征可继续编辑"} · 最后点击“确定”统一计算实体</span><span>拖动旋转 · 滚轮缩放 · 主管轴 X</span></footer>
  </section></div>`;
}
