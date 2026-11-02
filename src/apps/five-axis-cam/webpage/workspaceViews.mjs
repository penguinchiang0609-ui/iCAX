import {renderMachineColliderHud} from './machineColliders.mjs';
import {renderMachinePlanIssue} from './machinePlanIssue.mjs';
import {renderToolpathLayers} from './toolpathLayers.mjs';
import {pendingFeedbackTitle} from '../../_shared/workbench/layout/commonViews.mjs';
import {renderBlockingOperation} from './operationFeedback.mjs';
import {renderMachineIKTransform} from './machineIK.mjs';
import {renderFixturePlacement} from './fixturePlacement.mjs';
import {renderRapidPathTransform,renderRapidPathEditor} from './rapidPathEditing.mjs';
import {renderFixtureLibrary} from './fixtureLibrary.mjs';
import {renderSetupSelector,renderSetupManager,renderPoseLock} from './setupManagement.mjs';
import {renderJobExecution} from './jobExecution.mjs';
import {renderFixturePlateEditor} from './fixturePlateEditor.mjs';
import {renderFixtureBreakdown} from './fixtureBreakdown.mjs';
import {renderLocating} from './setupLocating.mjs';
import { floatingDialogStyle } from './floatingDialogs.mjs';
import { renderRibbonCommandIcon } from '../../../iCAX-UI/SDK/AppShell/app/ribbonIcons.mjs';
import { renderWorkpieces } from './workpieceSelection.mjs';
import { renderCutParameters, renderCollisionPoseEditor } from './cutParameters.mjs';
import { renderCutTreeTools, renderCutTree, orderedCuts } from './cutTree.mjs';
import { renderOrdering, renderOrderMenu, orderingPreview } from './toolpathOrdering.mjs';
import { renderRapid, renderRapidTree, rapidKinds } from './rapidPlanning.mjs';
import {rapidListModel} from './rapidListModel.mjs';
import {rapidDefaultStrategy,renderRapidSafety} from './rapidDefaultStrategy.mjs';
import { renderSimulation,renderCollisionCheck,renderHeadSettings } from './trajectorySimulation.mjs';
import {renderMachineLeft,renderMachineRight,renderMachineDock,renderMachineDefinitionManager} from './machineSimulation.mjs';
import {camMachinePlayback} from './machinePlaybackContext.mjs';
import { renderToolAxisDisplayControl } from './toolAxisDisplay.mjs';
import { renderToolAxisPointRotation } from './toolAxisPointRotation.mjs';
import { sceneColors, sceneColorCss } from './scenePalette.mjs';
import { currentCompensation } from './cutCompensation.mjs';
import { placementTools, renderPlacement, renderTransformParameters } from './workpiecePlacement.mjs';
import {renderAlignmentHint} from './workpieceAlignment.mjs';
import { renderCutClearing } from './cutClearing.mjs';
import { renderGeneration, generatedGeometry } from './toolpathGeneration.mjs';
import { renderLeads } from './cutLeads.mjs';
import { escapeText as esc, escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import { AREAS, TOOLS, normalizeArea, activeTool, workspaceState, currentDraft, sceneItems, outputChecks, definitionStatus } from './workspaceModel.mjs';
import { button, open, section, hint, empty, facts, badge, disabled, choices, steps, field } from './workspaceControls.mjs';
import { renderPreparation } from './preparationViews.mjs';
import { renderRegistration } from './registrationViews.mjs';
import { selectedPlacement } from './registrationActions.mjs';
import { renderExtraction, renderExtractionCandidates } from './extractionViews.mjs';
import { renderCutStarting } from './cutStarting.mjs';
import { renderCutSplitting } from './cutSplitting.mjs';
import { renderPathEditing } from './pathEditing.mjs';
import {renderPathTransform} from './pathTransform.mjs';
import { renderStandardHoles } from './standardHoles.mjs';
import { hasProcessEditor, renderProcessEditor } from './processEditing.mjs';
import { hasCutEditor, renderCutEditor } from './cutEditing.mjs';
import { renderFixtureEditor, renderFixtureList } from './fixtureViews.mjs';
import { fixtures } from './fixtureActions.mjs';
import {renderPreparationTree,renderWorkpieceParameters} from './preparationTree.mjs';
import {preparationKind} from './workspaceModel.mjs';
import {enabledWorkpiecesOnly} from './workpieceState.mjs';
import {camPreparationReason} from './workflow.mjs';
import {commandAvailability} from './commandAvailability.mjs';
import {camPage,renderCamInspectorTabs,renderProcessTabs} from './camWorkspace.mjs';
function availableOpen(view,id,label){
  const command=`five.tool.${id}`,state=commandAvailability(view,command);
  return button(command,label||TOOLS[id].title,state?.disabled?`disabled title="${attr(state.unavailableReason)}"`:'');
}
function treeGroup(id, title, rows, count) {
  return `<section class="five-tree-group" data-five-key="tree-${id}"><h3>${esc(title)}<span>${esc(count)}</span></h3><div class="five-tree-rows" data-five-key="rows-${id}">${rows}</div></section>`;
}
function renderRapidTools(view){
  const tools=[['five.tool.settings','空移策略参数','parameters'],['five.tool.generate','更新刀路与空移','regenerate']];
  const strategy=rapidDefaultStrategy(view);
  return `<div class="five-cut-tree-tools five-rapid-tools" role="toolbar" aria-label="空移管理">${tools.map(([id,title,icon])=>`<button type="button" data-five-key="${id}" data-five-command="${id}" title="${title}" aria-label="${title}" ${view.pending||view.fiveRapid?.request?'disabled':''}>${renderRibbonCommandIcon(icon)}</button>`).join('')}<select data-five-default-strategy aria-label="当前空移策略" title="选择下次生成使用的空移策略；点击更新刀路与空移后生效" ${view.pending||view.fiveRapid?.request?'disabled':''}>${rapidKinds.map(([id,title])=>`<option value="${id}" ${strategy===id?'selected':''}>${title}</option>`).join('')}</select>${renderRapidSafety(view)}</div>`;
}
export function renderLeft(context, view) {
  if(view.fiveAxisArea==='simulation')return renderMachineLeft(view);
  if(view.fiveAxisArea==='workpiece')return renderPreparationTree(view);
  if(['toolpath','simulation'].includes(view.fiveAxisArea)&&camPreparationReason(view))return `${renderWorkpieces(view,true)}${renderSetupSelector(view)}${section('cam-preparation',view.fiveAxisArea==='simulation'?'孪生准备':'CAM 准备',hint(camPreparationReason(view))+button('five.area.workpiece','前往工件页'))}`;
  if(enabledWorkpiecesOnly(view)&&!view.scene?.model?.isLoaded)return `${renderWorkpieces(view,true)}<div class="five-empty-row" data-five-key="no-enabled-workpiece">暂无已启用工件</div><div class="five-tree-footer">${button('five.area.workpiece','前往工件页')}</div>`;
  const state = workspaceState(view), items = sceneItems(view), search = state.search.toLocaleLowerCase();
  const name = item => item.name || item.label || item.sourcePath?.split(/[\\/]/).at(-1) || item.entityId || item.id || '未命名对象';
  const rows = (values, kind) => values.filter(item => name(item).toLocaleLowerCase().includes(search)).map(item => {
    const id = item.entityId ?? item.id;
    const selected = kind === 'intent' ? state.selectedIntentId === id : view.selectedMachineInstanceId === id;
    return `<button class="five-tree-row ${selected ? 'selected' : ''}" type="button" data-five-key="${kind}-${attr(id)}" ${kind === 'intent' ? `data-five-intent="${attr(id)}"` : `data-cam-machine-instance-id="${attr(id)}"`} aria-pressed="${selected}"><span class="five-object-icon ${kind}" aria-hidden="true">${kind === 'intent' ? '⌁' : '▤'}</span><span><strong>${esc(name(item))}</strong><small>${kind === 'intent' ? esc(definitionStatus(view, item)) : '设备实例'}</small></span></button>`;
  }).join('') || empty(search ? '没有匹配的对象' : '尚未添加');
  items.intents=orderedCuts(view);
  const generated=orderingPreview(view,generatedGeometry(view)), orderedView=['generate','manual-order','auto-order','simulation'].includes(activeTool(view));
  const definitions = treeGroup('intents', '加工任务', rows(items.intents, 'intent'), items.intents.length);
  if (view.fiveAxisArea==='toolpath') {
    const mode=camPage(view),rapid=mode==='rapid';
    const heading=`<header class="five-cam-list-heading" data-five-key="cam-list-heading"><strong>${rapid?'空移连接':mode==='order'?'加工顺序':'刀路列表'}</strong><span>${rapid?rapidListModel(view).transitions.length:items.intents.length} ${rapid?'段':'条'}</span></header>`;
    const searchBox=`<label class="five-search" data-five-key="cam-search"><input data-five-search data-five-key="cam-search-input" aria-label="查找刀路" value="${attr(state.search)}" placeholder="名称 / 序号 / 固定编号"></label>`;
    return `${heading}${searchBox}<div id="five-path-page" role="tabpanel" aria-label="${rapid?'空移刀路':'切割刀路'}" data-five-key="path-page-${rapid?'rapid':'cutting'}">${rapid?`<div class="five-path-list" data-five-key="path-list-rapid">${renderRapidTree(view,{heading:false})}</div>`:`<div class="five-cam-tree-actions">${renderCutTreeTools(view)}</div><div class="five-path-list" data-five-key="path-list-cutting">${renderExtractionCandidates(view)}${renderCutTree(view,items.intents)}</div>`}</div>`;
  }
  return `<div class="five-pane-heading" data-five-key="left-heading"><span class="five-eyebrow">工程对象</span><h2>${esc(context.project?.projectName || '未命名工程')}</h2><p>工件 · 装夹 · 加工任务</p></div>
    <label class="five-search" data-five-key="search-row"><span aria-hidden="true">⌕</span><input data-five-search data-five-key="search" type="search" aria-label="搜索工程对象" placeholder="搜索机床或刀路" value="${attr(state.search)}"></label>
    ${definitions}
    ${renderWorkpieces(view)}
    ${treeGroup('machines', '机床', rows(items.machines, 'machine'), items.machines.length)}
    ${treeGroup('fixtures', '工装', renderFixtureList(view), fixtures(view).length)}
    ${section('setup', '坐标与定位', facts([['编程坐标', selectedPlacement(view)?.applicationMode === 'AppliedInCAM' ? '已建立' : '待建立'], ['实测校准', selectedPlacement(view)?.registrationMethod === 'PairedPoints' ? '已应用' : '尚未应用'], ['现场定位计划', '尚未建立']]))}
    ${section('tree-results', '加工状态', facts([['切割定义', `${items.intents.length} 项`], ['完整刀路', '尚未生成'], ['整机验证', '未验证']]) + open('generate', '查看 CAM 任务 →'))}
    <div class="five-tree-footer" data-five-key="tree-footer">${button('five.refresh', '刷新', view.pending ? 'disabled' : '')}</div>`;
}
function poseEditor(d) {
  return section('pose-mode', '目标方向', field(d, 'poseMode', '初始化方向', choices([['normal', '沿板面法向'], ['tilted', '指定前倾 / 侧倾'], ['in-plane', '指定板面内方向']]))
    + (d.poseMode === 'tilted' ? field(d, 'forward', '目标前倾角') + field(d, 'lateral', '目标侧倾角') : d.poseMode === 'in-plane' ? disabled('选择面内方向参考') : ''), '°')
    + section('pose-tolerance', '允许调整', field(d, 'forwardTolerance', '前倾允许误差') + field(d, 'lateralTolerance', '侧倾允许误差') + field(d, 'totalTolerance', '总姿态误差上限') + hint('各方向偏差和总姿态偏差同时受限，自动调姿不扩大允许范围。'), '°')
    + section('pose-normal', '台阶法向偏好', field(d, 'normalPreference', '偏离强度') + hint('0—1，越大越倾向板面法向；不覆盖关键点硬约束。'));
}
const transitionField = d => field(d, 'transition', '向量过渡', choices([['length', '按弧长分配'], ['direction', '按方向变化分配']]));
function operationEditor(id, d, context, view) {
  if (hasCutEditor(view)) return renderCutEditor(view,d);
  if (hasProcessEditor(view)) return renderProcessEditor(view,d);
  const items = sceneItems(view), selected = items.intents.find(item => (item.entityId ?? item.id) === workspaceState(view).selectedIntentId);
  if (['rough', 'fine'].includes(id)) return renderRegistration(id, d, view);
  if (id === 'registration') return renderLocating(view);
  if(id==='setup')return renderSetupManager(view);
  const preparation = renderPreparation(id, d, context, view);
  if (preparation !== null) return preparation;
  switch (id) {
    case 'extract': case 'manual-extract': return renderExtraction(view);
    case 'adjust': return poseEditor(d) + section('adjust-keypoints', '局部例外', open('keypoint', '进入关键点编辑'));
    case 'keypoint': return ''; // The selected path owns the live editor.
    case 'compensation': return section('compensation', '切缝补偿', field(d, 'compensationMode', '补偿方式', choices([['auto', '按材料保留侧'], ['none', '不补偿']]))
      + (d.compensationMode !== 'none' ? field(d, 'kerf', '切缝宽度') + field(d, 'corner', '拐角方式', choices([['round', '圆角连接'], ['sharp', '尖角连接']])) : ''), 'mm');
    case 'export': return section('output-purpose', '交付用途', field(d, 'purpose', '用途', choices([['production', '生产加工'], ['acceptance', '设备验收']])) + hint('两种用途均须经过有效计划与输出重放验证。'))
      + section('output-profile', '程序配置', facts([['目标设备', '尚未确认'], ['后处理', '尚未选择'], ['实际文件重放', '尚未执行']]))
      + section('output-package', '交付内容', steps(['设备加工程序', '装夹与定位计划 / 已应用定位记录', '工艺摘要、验证报告与输出记录']))
      + `<div class="five-apply" data-five-key="output-apply">${button('five.export', '生成程序并重放', 'class="five-primary" disabled')}<span>查看下方检查项，当前尚不具备输出资格</span></div>`;
    case 'manual': return section('manual-flow', '按加工任务组织操作', steps(['工件：导入模型、缩放和重设自身坐标系', '工件树：建立装夹、摆放、准备三点定位资料和夹具单元', 'CAM：刀路提取、工艺设置和切割头轨迹仿真', '孪生：真实机型的数字孪生与整机验证', 'CAM：准备现场定位并导出加工程序']))
      + section('manual-files', '工程文件', hint('新建、打开、保存和另存为统一使用 iCAX 的文件入口。'))
      + section('manual-registration', '坐标与定位', hint('工件页设置自身几何坐标；工件树的装夹保存本次姿态和三点定位资料。现场测量对应三点求机床变换，不在 CAM 设置加工零点。'));
    case 'diagnostics': return section('diagnostics', '诊断范围', facts([['工程', context.project?.projectName || '未命名'], ['最近错误', view.error || '无']]) + disabled('导出诊断包'));
    case 'logs': return section('logs', '运行日志', hint('实时记录显示在窗口底部的公共日志区。') + facts([['日期筛选', '待接入'], ['详细级别 / 消息类别', '待接入']]) + open('diagnostics', '查看诊断导出'));
    case 'about': return section('about', '产品信息', facts([['产品', '三维五轴 CAM'], ['界面版本', '0.1 · 操作逻辑预览'], ['框架', 'iCAX AppShell / CAM 工作台']]) + hint('参考 FACAM 汽车覆盖件版的操作逻辑；工程格式、参数语义和设备接口由本产品定义。'));
    default: return '';
  }
}
export function renderRight(context,view){
  if(view.fiveAxisArea==='toolpath'&&camPreparationReason(view))return section('cam-preparation-state','当前装夹',hint(camPreparationReason(view)));
  if(view.fiveAxisArea==='simulation')return renderMachineRight(view);
  if(enabledWorkpiecesOnly(view)&&!view.scene?.model?.isLoaded)return '';
  if(view.fiveAxisArea==='workpiece'){
    const kind=preparationKind(view);
    if(kind==='workpiece')return renderWorkpieceParameters(view);
    if(kind==='unit')return renderFixtureEditor(view);
    return activeTool(view)==='registration'&&view.fiveLocatingOpen!==false?renderLocating(view):renderTransformParameters(view);
  }
  if(view.fiveAxisArea==='toolpath'&&view.fiveCamPage==='rapid')return renderCamInspectorTabs(view)+(view.fiveCamInspector==='collision'?renderCollisionCheck(view)+renderRapidPathEditor(view,view.pending):`<div class="five-cam-operation-panel" data-five-key="cam-rapid-operation">${renderRapid(view,true)}</div>`);
  if(view.fiveAxisArea==='toolpath'&&camPage(view)==='process'){
    const tab=view.fiveCamProcessTab||'parameters';
    const editor=renderToolEditor(context,view);
    return renderProcessTabs(view)+(tab==='commands'?renderCutParameters(view,{panel:'commands'}):tab==='parameters'
      ?hasProcessEditor(view)?renderProcessEditor(view,currentDraft(view)):section('process-selection','切割参数',hint('请选择一条已启用、未锁定的刀路。引线、补偿和过切可从上方工具进入。'))
      :editor?`<div class="five-cam-operation-panel" data-five-key="cam-process-operation">${editor}</div>`:section('process-geometry','引线与补偿',`<div class="five-button-row">${availableOpen(view,'leads','引线')}${availableOpen(view,'compensation','补偿')}${availableOpen(view,'overcut','过切')}${availableOpen(view,'side','阴阳切')}${availableOpen(view,'clear','清除覆盖')}</div>`));
  }
  if(view.fiveAxisArea==='toolpath'&&(view.fiveCamInspector==='collision'||activeTool(view)==='keypoint'))return renderCamInspectorTabs(view)+renderCollisionCheck(view)+renderCollisionPoseEditor(view);
  if(view.fiveAxisArea==='toolpath'&&camPage(view)==='order')return renderCamInspectorTabs(view)+renderCutParameters(view,{panel:view.fiveCamInspector||'properties'});
  if(view.fiveAxisArea==='toolpath'&&view.fiveAxisExtractionOpen&&['extract','manual-extract'].includes(activeTool(view)))return `<div class="five-cam-operation-panel" data-five-key="cam-extraction-operation">${renderExtraction(view)}</div>`;
  const editor=renderToolEditor(context,view);
  if(['path-translate','path-rotate'].includes(activeTool(view)))return editor+renderCutParameters(view,{panel:'properties'});
  if(view.fiveAxisArea==='toolpath')return renderCamInspectorTabs(view)+renderCutParameters(view,{panel:'properties'})+(editor&&!editor.startsWith('<div class="five-position-dialog"')&&!['select','keypoint'].includes(activeTool(view))?editor:'');
  return (normalizeArea(context.activeRibbonTabId)==='toolpath'?renderCutParameters(view):'')+(activeTool(view)!=='registration'&&editor.startsWith('<div class="five-position-dialog"')?'':editor);
}
function renderToolEditor(context, view) {
  const area = normalizeArea(context.activeRibbonTabId), id = activeTool(view, area), tool = TOOLS[id], draft = currentDraft(view, id);
  if (id==='select'||id==='align'||placementTools.includes(id)) return '';
  if (id==='registration') return renderLocating(view);
  if(['rapid','settings'].includes(id))return renderRapid(view);
  if(id==='simulation')return '';
  if(id==='collision')return '';
  if (['rough', 'fine'].includes(id)) return view.fiveAxisRegistrationOpen ? renderRegistration(id, draft, view) : '';
  if (id === 'fixture') return renderFixtureEditor(view);
  if (['extract','manual-extract'].includes(id)) return '';
  if (id==='clear') return view.fiveAxisClearOpen?renderCutClearing(view):'';
  if (['manual-order','auto-order'].includes(id)) return renderOrdering(view);
  if (id==='generate') return view.fiveGenerationOpen?renderGeneration(view):'';
  if (id==='split') return view.fiveAxisSplitOpen?renderCutSplitting(view):'';
  if(['path-translate','path-rotate'].includes(id))return renderPathEditing(view);
  if (['leads','overcut'].includes(id)) return view.fiveAxisLeadsOpen?renderLeads(view):'';
  if (id==='path-start'||id==='keypoint') return '';
  if (id==='path-hole') return view.fiveAxisHoleOpen?renderStandardHoles(view):'';
  const selected = sceneItems(view).intents.find(item => (item.entityId ?? item.id) === workspaceState(view).selectedIntentId);
  const editable = !['setup','registration', 'manual', 'about', 'diagnostics', 'logs', 'machine-simulation', 'machine-definitions', 'machine-instances'].includes(id);
  const positioning = ['rough', 'fine', 'extract', 'manual-extract'].includes(id) || hasProcessEditor(view) || hasCutEditor(view);
  const sourceOperation = ['split', 'path-hole', 'path-start', 'adjust', 'keypoint', 'reverse', 'leads', 'side', 'overcut', 'compensation', 'clear'].includes(id);
  return `<div class="five-pane-heading" data-five-key="right-heading" data-five-tool="${id}"><span class="five-eyebrow">${esc(AREAS.find(item => item.id === area).title)} / ${esc(tool.target)}</span><h2>${esc(tool.title)}</h2><p>${esc(tool.purpose)}</p></div>
    ${sourceOperation ? section('operation-source', '关联对象', facts([['切割定义', selected?.name || selected?.label || '请在左侧选择'], ['生成刀路', '尚未生成']])) : ''}
    ${editable && !positioning ? '<div class="five-draft-note" data-five-key="draft-note">界面参数草稿 · 仅本次会话，未应用到工程</div>' : ''}
    ${operationEditor(id, draft, context, view)}
    ${section('operation-effect', '操作结果', hint(tool.effect))}
    ${editable && !['simulation', 'export'].includes(id) ? section('draft-note-editor', '方案备注', field(draft, 'note', '备注', { text: true, placeholder: '记录当前操作方案' })) + (positioning ? '' : `<div class="five-apply" data-five-key="apply">${button('five.apply', id === 'generate' ? '生成完整刀路' : '应用到工程', 'class="five-primary" disabled')}<span>参数提交与计算能力待接入</span></div>`) : ''}`;
}
export function renderSummary(context, view) {
  return '';
}
function renderSceneLegend(view){
  const item=(kind,title,color)=>`<span><i class="${kind}" style="--five-legend-color:${sceneColorCss(color)}" aria-hidden="true"></i>${title}</span>`;
  if(camMachinePlayback(view))return `<div class="five-legend" data-five-key="legend">${item('cut','已切割',sceneColors.simulationCut)}${item('rapid','已空移',sceneColors.simulationRapid)}</div>`;
  const axes=view.fiveCamPage!=='rapid'&&view.fiveToolAxisDisplay?.visible!==false;
  const marking=view.scene?.intentToolpaths?.some(c=>c.enabled!==false&&c.machiningKind==='Mark');
  const layered=!!view.scene?.domain?.layers;
  const original=!layered&&currentCompensation(view).length>0?item('cut','原刀路',sceneColors.original):'';
  const paths=layered?'<span>刀路与引线：按图层配色</span>':`${original}${item('cut','切割路径',sceneColors.cutting)}${item('cut','引入线',sceneColors.leadIn)}${item('cut','引出线',sceneColors.leadOut)}`;
  const simulation=activeTool(view)==='simulation';
  return `<div class="five-legend" data-five-key="legend" style="--five-legend-text:${sceneColorCss(sceneColors.reference)}">${paths}${marking?item('mark','刻线',sceneColors.cutting):''}${item('selected','选中',sceneColors.selected)}${axes?item('axis','刀轴',sceneColors.toolAxis)+item('reference','法向参考',sceneColors.reference):''}${item('rapid','空移',sceneColors.reference)}${simulation?item('cut','已切割',sceneColors.simulationCut)+item('rapid','已空移',sceneColors.simulationRapid)+item('cut','碰撞',sceneColors.collision):''}${item('stale','过期',sceneColors.stale)}</div>`;
}
export function renderOverlay(context, view) {
  const pendingTitle=pendingFeedbackTitle(context,view);
  if (view.fiveAxisImportActive) return '';
  if(enabledWorkpiecesOnly(view)&&!view.scene?.model?.isLoaded&&!activeTool(view).startsWith('machine-'))return '';
  const items = sceneItems(view), hasGeometry = view.scene?.model?.isLoaded || items.machines.length;
  const area = normalizeArea(context.activeRibbonTabId), twin = area === 'simulation';
  const id=activeTool(view),editor=id==='rapid'?'':renderToolEditor(context,view);
  // Generation owns the page while it runs. Use the shared centered progress
  // layer rather than a movable viewport editor or its remembered position.
  const orderStatus=area==='toolpath'&&camPage(view)==='order'?renderOrdering(view):'';
  const sidebarOperation=area==='toolpath'&&['rapid','process','order'].includes(camPage(view));
  const dialog=!sidebarOperation&&id!=='registration'&&editor.startsWith('<div class="five-position-dialog"')?id==='generate'
    ?`<div class="cam-progress-backdrop" data-five-key="generation-modal">${editor.replace('class="five-position-dialog"','class="five-position-dialog five-centered-operation-dialog"')}</div>`
    :editor.replace('class="five-position-dialog"',`class="five-position-dialog five-floating-dialog" data-five-floating="${id}" ${floatingDialogStyle(view,id)}`).replace('class="five-position-title"','class="five-position-title" data-five-dialog-drag'):'';
  const poseHint=activeTool(view)==='keypoint'?`<div class="five-point-scene-hint" data-five-key="point-scene-hint" role="status">${view.fivePoseSelection?.active?'姿态选择中 · 点击紫色箭头 · 右键拖动旋转 / 中键拖动平移 / 滚轮缩放 · Esc 退出':view.fiveAxisPointInsert?'连续新增关键点 · 点击刀路依次添加并自动保存 · 再次点击按钮或 Esc 退出':view.fiveAxisPointDelete?'连续删除关键点 · 点击手动关键点依次删除并自动保存 · 系统关键点不可删除 · 再次点击按钮或 Esc 退出':'点选关键点后拖动旋转环 · 点环可输入角度'}</div>`:'';
  const poseControls=area==='toolpath'&&!camMachinePlayback(view)&&view.fiveCamPage!=='rapid'?`<div class="five-pose-controls" data-five-key="pose-display" role="group" aria-label="姿态显示">${renderToolAxisDisplayControl(view)}</div>${poseHint?`<div class="five-pose-dock" data-five-key="pose-dock">${poseHint}</div>`:''}`:'';
  return `${renderMachinePlanIssue(view)}${renderHeadSettings(view)}${renderMachineDefinitionManager(view)}${renderJobExecution(view)}${renderFixtureLibrary(view)}${dialog}${orderStatus}${activeTool(view)==='leads'&&!view.fiveAxisLeadsOpen?`<div class="five-lead-tool-hint five-point-scene-hint" data-five-key="lead-tool-hint" role="status">${view.fiveLeads?.saving?'正在应用引线…':esc(view.error||'连续设置引线 · 点击切割刀路应用 · 再次点击引线或 Esc 退出')}${view.fiveLeads?.request&&!view.fiveLeads.saving?button('five.leads.retry','重试'):''}</div>`:''}${renderFixturePlateEditor(view)}${renderFixtureBreakdown(view)}${renderAlignmentHint(view)}${renderOrderMenu(view)}${renderCutStarting(view)}${placementTools.includes(activeTool(view))?renderPlacement(view):''}${renderMachineColliderHud(view)}${renderMachineIKTransform(view)}${renderFixturePlacement(view)}${renderToolAxisPointRotation(view)}${renderRapidPathTransform(view)}${poseControls}
    ${renderPathTransform(view)}${camMachinePlayback(view)?'':renderToolpathLayers(view)}${area==='toolpath'&&!camMachinePlayback(view)?button('five.display.numbers',`序号：${workspaceState(view).showPathNumbers!==false?'显示':'隐藏'}`,`class="five-path-number-toggle" data-five-key="path-number-toggle" aria-label="刀路序号" aria-pressed="${workspaceState(view).showPathNumbers!==false}" title="显示或隐藏场景中的刀路序号"`):''}
    ${twin ? `` : !hasGeometry ? `<div class="five-start-card" data-five-key="start-card"><svg viewBox="0 0 180 115" aria-hidden="true"><path d="M24 76 90 106 156 76 90 46zM24 76V35l66-27 66 27v41M90 106V65M24 35l66 30 66-30"/></svg><span class="five-eyebrow">导入加工</span><h2>从工件与装夹开始</h2><p>导入已有模型，选择装夹姿态后进入 CAM 编程。</p>${button('workpiece.import', '导入模型', view.pending ? 'disabled' : '')}<small>STEP / STP / IGES / IGS</small></div>` : ''}
    ${area === 'toolpath' ? renderSceneLegend(view) : ''}${renderBlockingOperation(view)}
    ${pendingTitle && !view.error && !(activeTool(view)==='fixture'&&view.fiveFixtureDesignOpen) && !view.fivePlacement?.open && !view.fiveSimulation?.checking && !view.fiveAxisStartOpen && !view.fiveAxisRegistrationOpen && !view.fiveAxisExtractionOpen && !view.fiveAxisSplitOpen && !view.fiveAxisHoleOpen && !view.fiveAxisLeadsOpen && !view.fiveAxisReverseOpen && !(activeTool(view)==='clear'&&view.fiveAxisClearOpen)&&!(activeTool(view)==='generate'&&view.fiveGenerationOpen)&&!(['manual-order','auto-order'].includes(activeTool(view))&&view.fiveOrder?.open) ? `<div class="five-feedback" data-five-key="feedback" role="status">${view.fiveAxisFixtureTask?.status === 'Running' ? `${esc(view.progress?.title)} ${button('five.fixture.cancel','取消')}` : esc(pendingTitle)}</div>` : ''}`;
}
export function renderDock(context, view) {
  if(view.fiveAxisArea==='simulation')return renderMachineDock(view);
  if(enabledWorkpiecesOnly(view)&&!view.scene?.model?.isLoaded)return '';
  const area = normalizeArea(context.activeRibbonTabId);
  if(activeTool(view)==='simulation')return renderSimulation(view);
  if (area==='workpiece') return '';
  const state = workspaceState(view), issues = state.dock === 'issues';
  const items = sceneItems(view);
  return `<div class="five-dock-head" data-five-key="dock-head"><div role="group" aria-label="底部面板">${button('five.dock.sequence', '加工顺序', `aria-pressed="${!issues}"`)}${button('five.dock.issues', '检查与问题', `aria-pressed="${issues}"`)}</div><span>${issues ? '尚未形成验证报告' : '完整计划生成后显示实际加工顺序'}</span></div>
    <div class="five-dock-content" data-five-key="dock-content">${issues ? `<div class="five-gates">${outputChecks(view).map((check, index) => `<button type="button" data-five-command="five.tool.${check.tool}" class="five-gate" data-five-key="gate-${index}"><span>${String(index + 1).padStart(2, '0')}</span><div><strong>${check.title}</strong><small>${check.detail}</small></div><b>→</b></button>`).join('')}</div>` : `<div class="five-sequence-empty"><span class="five-sequence-symbol">⌁</span><div><strong>${items.intents.length ? `${items.intents.length} 项切割定义，等待生成刀路` : '先建立刀路'}</strong><p>排序、切割姿态和空移由“生成刀路”统一规划。</p></div>${open('extract', '查看刀路 →')}</div>`}</div>`;
}
