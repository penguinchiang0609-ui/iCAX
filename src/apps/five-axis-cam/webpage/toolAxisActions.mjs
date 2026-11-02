import { supportsToolAxis } from './toolAxisEligibility.mjs';
import { activeTool, workspaceState } from './workspaceModel.mjs';
import { commandAvailability } from './commandAvailability.mjs';
import { renderRibbonCommandIcon } from '../../../iCAX-UI/SDK/AppShell/app/ribbonIcons.mjs';
import { escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';

export function toolAxisActions(view) {
  const ids=workspaceState(view).selectedIntentIds||[],poseId=view.fivePoseSelection?.active&&view.fivePoseSelection.selected?.intentId;
  const cut=view.scene?.intentToolpaths?.find(c=>c.id===(poseId||(ids.length===1?ids[0]:null))),point=view.fiveAxisPointEdit;
  const busy=!!(view.pending||view.fiveToolAxisRequest||point?.loading||point?.influenceLoading||point?.request||point?.stale||point?.rotation?.gesture);
  const canPose=!!view.scene?.intentToolpaths?.some(c=>c.enabled!==false&&!c.locked&&supportsToolAxis(c));
  const actions=[
    {id:'five.axes.reverse-all',title:'全部姿态反向',icon:'undo',disabled:busy||!canPose,global:true},
    {id:'five.pose-select.toggle',title:'姿态选择',icon:'pose-select',active:!!view.fivePoseSelection?.active,disabled:!view.fivePoseSelection?.active&&(busy||!view.scene?.intentToolpaths?.some(c=>c.enabled!==false&&supportsToolAxis(c)))},
    {id:'five.points.whole',title:'整条刀路',icon:'pose-axis',active:!!cut&&activeTool(view,'toolpath')!=='keypoint',disabled:busy||!cut},
    {id:'five.points.add',title:'新增关键点',icon:'keypoint-add',active:!!view.fiveAxisPointInsert,disabled:busy||!canPose},
    {id:'five.points.delete',title:'删除关键点',icon:'keypoint-delete',active:!!view.fiveAxisPointDelete,disabled:!!view.fivePoseSelection?.active||busy||!cut||cut.locked||!supportsToolAxis(cut)},
    {id:'five.points.edit',title:'编辑关键点',icon:'keypoint-edit',active:activeTool(view,'toolpath')==='keypoint'&&!view.fiveAxisPointInsert&&!view.fiveAxisPointDelete&&!view.fivePoseSelection?.active,disabled:busy||!canPose},
    {id:'five.points.reverse',title:'当前姿态反向',icon:'undo',disabled:busy||!cut||cut.locked||activeTool(view,'toolpath')!=='keypoint'||point?.id!==cut.id||!point?.axis},
  ];
  return actions.map(action=>{
    const availability=commandAvailability(view,action.id);
    if(action.id==='five.points.add'&&view.fiveAxisPointInsert)return {...action,disabled:false,unavailableReason:'连续新增已开启；再次点击或按 Esc 退出'};
    if(action.id==='five.points.delete'&&view.fiveAxisPointDelete)return {...action,disabled:false,unavailableReason:'连续删除已开启；再次点击或按 Esc 退出'};
    return {...action,disabled:!!action.disabled||!!availability?.disabled,unavailableReason:availability?.unavailableReason||''};
  });
}

export function renderToolAxisActions(view) {
  return `<section class="five-pose-editor-actions" data-five-key="pose-editor-actions"><h4>姿态调整</h4><div class="five-pose-action-grid" role="group" aria-label="姿态调整">${toolAxisActions(view).map(action=>`<button type="button" data-five-key="pose-action-${attr(action.id)}" data-five-command="${attr(action.id)}" title="${attr(action.unavailableReason||action.title)}" aria-label="${attr(action.title)}" ${action.active!==undefined?`aria-pressed="${action.active}"`:''} ${action.global?'class="five-pose-action-global"':''} ${action.disabled?'disabled':''}>${renderRibbonCommandIcon(action.icon)}<span>${action.title}</span></button>`).join('')}</div></section>`;
}
