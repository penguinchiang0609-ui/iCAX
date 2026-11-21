import {canEnterArea} from './workflow.mjs';
import { AREAS, TOOLS, activeTool, preparationKind, workspaceState } from './workspaceModel.mjs';
import { findProjectView } from '../../_shared/workbench/state/projectViewStore.mjs';
import { cutTreeAvailability } from './cutTree.mjs';
import {activeWorkpiece} from './workpieceState.mjs';
import {selectedFixture} from './fixtureActions.mjs';
import {workpieceSelectionBusy} from './workpieceSelection.mjs';
import {commandAvailability} from './commandAvailability.mjs';
import {workspaceAlerts} from './alerts.mjs';
import {camPage,camRibbonWorkspace} from './camWorkspace.mjs';
import {extractionDraft} from './extractionActions.mjs';
import {orderOptions} from './orderParameters.mjs';
import {manualOrderAnchor} from './toolpathOrdering.mjs';
import {collisionDetailKind} from './workspaceModel.mjs';
const cmd = (id, title, iconName, options = {}) => ({ id, title, iconName, ...options });
const group = (title, commands,options={}) => ({ title, commands,...options });
export function getRibbonDefinition(context = {}) {
  const view = findProjectView(context.project?.projectId) ?? {};
  const tree = cutTreeAvailability(view);
  const extractionMode=view.fiveAxisExtractionOpen&&['extract','manual-extract'].includes(activeTool(view))?extractionDraft(view).extraction.mode:null;
  const open = (id, iconName, options = {}) => cmd(`five.tool.${id}`, TOOLS[id].title, iconName, { active: activeTool(view, TOOLS[id].area) === id, ...options });
  const busy=workpieceSelectionBusy(view)||view.viewport?.transformManipulator?.pointerId!=null;
  const creating=view.fiveAxisArea==='operation'&&!!view.fiveOperationCreateDialog;
  const kind=preparationKind(view),workpiece=activeWorkpiece(view),setup=creating?null:view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId),unit=selectedFixture(view),hasPlacement=!!view.scene?.activePlacementId;
  const root=hasPlacement&&kind==='workpiece',pose=hasPlacement&&kind==='setup',fixture=hasPlacement&&kind==='unit'&&!!unit,enabled=workpiece?.enabled!==false&&setup?.enabled!==false;
  const tableSide=['HeadTable','TableTable'].includes(setup?.machineType?.machineType);
  const fixtureCreate=[cmd('five.fixture.add',tableSide?'添加工装':'新增夹具','cam-fixture-add',{size:'large',disabled:!setup||!!busy||!!setup?.locked||!enabled}),cmd('five.fixture.generate',tableSide?'生成承托':'新增支撑','cam-support-add',{size:'large',disabled:!setup||!!busy||!!setup?.locked||!enabled})];
  const groups = {
    workpiece: [group('工件',[cmd('workpiece.import','新增','cam-workpiece-add',{size:'large',disabled:!!busy}),cmd('workpiece.remove','删除','cam-workpiece-remove',{disabled:!!busy||!root||workpiece?.locked}),cmd('five.preparation.copy:workpiece','复制','cam-workpiece-copy',{disabled:!!busy||!root||!enabled})]),
      group('几何',[open('scale','scale',{title:'缩放',size:'large',disabled:!root||!!busy||!!workpiece?.locked||!enabled})]),
      group('自身坐标系',[open('align','align',{title:'设置坐标系',size:'large',disabled:!root||!!busy||!!workpiece?.locked||workpiece?.enabled===false})])],
    operation: [group('工序',[cmd('five.operation.create','新增','cam-setup-add',{disabled:!!busy||!workpiece||workpiece.enabled===false}),cmd('five.setup.remove','删除','cam-setup-remove',{disabled:!setup||!!busy||!!setup?.locked}),cmd('five.preparation.copy:setup','复制','cam-setup-copy',{disabled:!setup||!!busy||!enabled})]),
      group('机型',[open('machine-type','machine',{title:'机型信息',size:'large',disabled:!setup||!!busy})]),
      group('位姿装夹',[open('translate','move',{title:'放置',active:['translate','rotate'].includes(activeTool(view,'operation')),size:'large',disabled:!setup||!!busy||!!setup?.locked||!enabled}),cmd('five.tool.registration','三点定位','cam-three-point',{disabled:!setup||!!busy||!enabled})]),
      group('夹具',[...(tableSide?fixtureCreate:[...fixtureCreate].reverse()),cmd('five.fixture.remove','删除','cam-fixture-remove',{disabled:!setup||!fixture||!!busy||!!unit?.locked}),cmd('five.preparation.copy:fixture','复制','cam-fixture-copy',{disabled:!setup||!fixture||!!setup?.locked||!!busy||!enabled}),cmd('five.fixture.library.open','工装库','cam-fixture-library',{disabled:!!busy||creating})])],
    toolpath: [group('刀路管理',[
      cmd('five.extraction.begin.Faces','批量识别','cam-cut-batch',{disabled:!tree.add,active:extractionMode==='Faces',tooltip:'选择非加工面集合，识别其边界生成刀路候选；可设置加工观察方向。'}),
      cmd('five.extraction.begin.Edges','选边','cam-cut-edges',{disabled:!tree.add,active:extractionMode==='Edges',tooltip:'逐条选择模型边线，组成闭合切割轮廓；再次点击可取消选择。'}),
      cmd('five.extraction.begin.Region','选加工面','cam-cut-faces',{disabled:!tree.add,active:extractionMode==='Region',tooltip:'选择连续的加工面，提取该区域的外边界或内孔；公共边不参与切割。'}),
      cmd('five.tree.Delete','删除','cam-cut-remove',{disabled:!tree.remove,tooltip:'删除选中的刀路或刀路组；可撤销，CAD 模型保持原样。'})]),
      group('刀路编辑', [open('split', 'cut'), cmd('five.path.merge','合并','merge',{disabled:!!view.pending||(workspaceState(view).selectedIntentIds||[]).length<2,tooltip:'按当前容差合并至少两条选中的开放刀路。右侧箭头可设置端点连接容差。',inputPopover:{id:'five.path.merge-tolerance',label:'合并容差',unit:'mm',value:view.fiveMergeTolerance??'0.1',disabled:!!busy,min:0,max:1000,step:'any'}}),open('path-translate','move'),open('path-rotate','rotate')]),
      group('工艺', [open('path-start', 'select'), open('leads', 'sketch-line',{menuItems:[cmd('five.leads.settings','引线设置','parameters')]}), cmd('five.spatial-leads', '空间引线', 'sketch-line'), open('overcut', 'cut'), open('compensation', 'measure'), cmd('five.tool.reverse','反向','undo',{disabled:!!view.pending||(!tree.remove&&!view.fiveReverseRequest)}), open('clear', 'delete')]),
      group('排序', [cmd('five.tool.manual-order-start','从头开始','order-from-start',{disabled:!tree.order,active:activeTool(view)==='manual-order'&&!!view.fiveOrder?.open&&view.fiveOrder.mode!=='after',tooltip:'从第一条开始依次点选刀路；Enter 或再次点击保存，Esc 退出。'}),cmd('five.tool.manual-order-current','从当前开始','order-from-current',{disabled:!tree.order||!manualOrderAnchor(view)&&!(activeTool(view)==='manual-order'&&view.fiveOrder?.open&&view.fiveOrder.mode==='after'),active:activeTool(view)==='manual-order'&&!!view.fiveOrder?.open&&view.fiveOrder.mode==='after',tooltip:'保留当前刀路及之前的顺序，继续点选后续刀路；刀路组整体保留。Enter 或再次点击保存。'}),open('auto-order','sort',{disabled:!tree.order||!!view.fiveOrder?.request,active:!!view.fiveOrder?.running&&activeTool(view)==='auto-order',tooltip:'按空移粗估直接排序，使用左侧的高度差惩罚系数。',inputBefore:{id:'five.order.height-penalty',label:'高度差惩罚系数',caption:'高度差惩罚',value:orderOptions(view).heightPenaltyWeight,disabled:!!busy||!!view.fiveOrder?.request}})]),
      group('当前工序检查', [open('simulation', 'simulate'), cmd('five.simulation.collision-check','碰撞与限位检测','collision')]),
      group('整个加工任务', [cmd('five.job.open','工序与输出','export')]),
      group('切割头', [cmd('five.simulation.head-open','设置切割头','machine',{size:'large',active:!!view.fiveSimulation?.headOpen,disabled:!!busy||!!view.fiveSimulation?.busy||!setup})])],
    simulation: [
      group('机床定义', [cmd('five.tool.machine-definitions','管理','machine',{size:'large'})]),
      group('机床实例', [cmd('five.machine-panel.instantiate','创建实例','add-instance'),open('machine-instances','parameters',{title:'实例参数',size:'large',menuItems:[cmd('five.machine-panel.joints','点动控制','move'),cmd('five.machine-panel.colliders','碰撞盒','collision'),cmd('five.machine-panel.post','后置配置','parameters')]})]),
      group('作业', [open('machine-simulation','machine',{title:'工件摆放',size:'large'}),cmd('five.machine-panel.mount','放到工作台','move'),cmd('five.machine-panel.build','规划运动','collision',{size:'large'}),cmd('five.machine-panel.play','开始仿真','simulate'),cmd('five.machine-panel.pause','暂停','pause'),cmd('five.machine-panel.reset','复位','undo'),cmd('five.machine-panel.program','查看指令','report'),cmd('five.machine-panel.export','导出指令','export',{size:'large'})]),
    ],
    help: [group('支持', [open('manual', 'support'), open('diagnostics', 'report'), open('logs', 'report'), open('about', 'display')]), group('界面', [cmd('view.reset-layout', '重置布局', 'display')])],
  };
  const originalCam=groups.toolpath,mode=camPage(view),rapid=view.fiveRapid,hasCuts=!!tree.order;
  const pathChecks=group('检测与修复',[cmd('five.simulation.collision-check','碰撞与限位检测','collision',{disabled:!!busy||!hasCuts,menuItems:[
    cmd('five.simulation.collision-manual-scope','手动修复','keypoint-edit'),cmd('five.simulation.collision-auto-scope','自动修复','parameters')
  ]})]);
  const poseCommands=[cmd('five.points.whole','整体姿态','pose-axis'),cmd('five.points.edit','关键点','keypoint-edit',{menuItems:[cmd('five.points.add','新增关键点','keypoint-add'),cmd('five.points.delete','删除关键点','keypoint-delete'),cmd('five.pose-select.toggle','姿态选择','pose-select'),cmd('five.axes.reverse-all','全部姿态反向','undo')]})];
  groups.toolpath=mode==='rapid'?[
    group('生成',[open('settings','parameters',{title:'生成参数'}),open('generate','regenerate',{title:'生成全部',disabled:!hasCuts}),cmd('five.rapid.safe-generate','单段安全生成','sort',{disabled:!!busy||!rapid?.id||!!rapid?.settings})]),
    group('路径编辑',[cmd('five.rapid.path-edit','编辑路径','keypoint-edit',{disabled:!!busy||!rapid?.id||!!rapid?.settings}),cmd('five.rapid.path-automatic','恢复自动','undo',{disabled:!!busy||!rapid?.id||!!rapid?.settings})]),
    pathChecks,
  ]:mode==='process'?[
    group('切割参数',[cmd('five.cam-process.parameters','切割参数','parameters')]),
    group('进出刀',originalCam[2].commands.filter(c=>['five.tool.path-start','five.tool.reverse','five.tool.leads','five.spatial-leads'].includes(c.id))),
    group('补偿与过切',[...originalCam[2].commands.filter(c=>['five.tool.compensation','five.tool.overcut','five.tool.clear'].includes(c.id)),open('side','select',{title:'阴阳切'})]),
    group('指令',[cmd('five.cam-process.commands','前后指令','report')]),
  ]:mode==='order'?[group('手动排序',originalCam[3].commands.slice(0,2)),group('自动排序',originalCam[3].commands.slice(2))]:mode==='collision'?[
    group('检测',['Cutting','Rapid'].map(kind=>cmd('five.simulation.collision-check:'+kind,kind==='Cutting'?'检测切割':'检测空移','collision',{disabled:!!busy||!hasCuts}))),
    group('详情',[['cutting','切割','cam-cut-path'],['rapid','空移','cam-rapid-path'],['all','全部','report']].map(([kind,title,icon])=>cmd('five.collision-details.'+kind,title,icon,{active:collisionDetailKind(view)===kind,disabled:!!busy}))),
    group('自动修复',['Cutting','Rapid'].map(kind=>cmd('five.simulation.collision-auto-scope:'+kind,kind==='Cutting'?'修复切割':'修复空移','parameters',{disabled:!!busy||!hasCuts}))),
  ]:mode==='head'?[
    group('切割头',[cmd('five.simulation.head-import','导入','import',{size:'large',disabled:!!busy||!setup||!!setup?.machineSetup?.machineId||setup?.headModel?.collisionSource==='MachineInstance'||!!view.fiveHeadEditor?.importPrompt}),cmd('five.head-instance.library-open','我的刀头','machine',{disabled:!!busy})]),
  ]:mode==='simulation'?[
    group('播放',[cmd('five.simulation.play','播放','simulate',{disabled:!!busy||!!view.fiveSimulation?.playing||!hasCuts}),cmd('five.simulation.pause','暂停','pause',{disabled:!view.fiveSimulation?.playing}),cmd('five.simulation.previous','上一步','undo',{disabled:!!busy||!hasCuts}),cmd('five.simulation.next','下一步','redo',{disabled:!!busy||!hasCuts})]),
    group('显示',[cmd('five.simulation.head-toggle','切割头','machine',{active:view.fiveSimulation?.showHead!==false}),cmd('five.simulation.trace-clear','清除加工轨迹','delete')]),
  ]:[
    originalCam[0],originalCam[1],group('刀轴',poseCommands),pathChecks,
  ];
  const describe=(command,parent)=>{
    const availability=commandAvailability(view,command.id);
    const described=availability?{...command,...availability,disabled:!!command.disabled||availability.disabled}:{...command};
    if(parent?.disabled||parent?.authorizationRequired){described.disabled=true;described.unavailableReason||=parent.unavailableReason;}
    if(command.menuItems)described.menuItems=command.menuItems.map(item=>describe(item,described));
    return described;
  };
  return { alerts:workspaceAlerts(view), tabs: AREAS.filter(area=>canEnterArea(view,area.id)).map(area => ({ id: area.id, title: area.title,workspace:camRibbonWorkspace(view,area.id), groups: groups[area.id].map(group=>({...group,commands:group.commands.map(command=>describe(command))})) })) };
}

// A shell remount can replace the visible ribbon while the product's last
// definition signature stays unchanged. Reconcile permission attributes on the
// current nodes as well, without replacing a focused editor or side panel.
export function syncRibbonCommandAvailability(ribbon,definition){
  if(!ribbon)return false;
  const tabId=ribbon.querySelector('.ribbon-tab.active')?.dataset.tabId,tab=definition.tabs?.find(tab=>tab.id===tabId);
  if(!tab)return false;
  const commands=new Map();
  const collect=(command,parent)=>{
    const inherited=parent?.disabled||parent?.authorizationRequired,disabled=!!(inherited||command.disabled||command.authorizationRequired);
    const item={...command,disabled,unavailableReason:command.unavailableReason||(inherited?parent.unavailableReason:'')};
    commands.set(command.id,item);
    for(const child of command.menuItems||[])collect(child,item);
  };
  for(const command of [...tab.groups.flatMap(group=>group.commands),...(tab.workspace?.pages||[]),...(tab.workspace?.actions||[]),...(tab.workspace?.checks||[])])collect(command);
  let changed=false;
  for(const button of ribbon.querySelectorAll('.ribbon-command[data-command-id],.ribbon-command-menu-toggle[data-command-id],.ribbon-command-menu-item[data-command-id]')){
    const command=commands.get(button.dataset.commandId);if(!command)continue;
    const disabled=!!(command.disabled||command.authorizationRequired),reason=String(command.unavailableReason||'');
    const menu=button.classList.contains('ribbon-command-menu-toggle'),effectiveDisabled=menu&&command.inputPopover?!!command.inputPopover.disabled:disabled;
    if(button.disabled!==effectiveDisabled){button.disabled=effectiveDisabled;changed=true;}
    const tip=menu&&command.inputPopover?`设置${command.inputPopover.label}，单位 ${command.inputPopover.unit||'mm'}。`:reason||command.tooltip||'';
    if((button.dataset.toolbarTooltip||'')!==tip){button.dataset.toolbarTooltip=tip;changed=true;}
  }
  return changed;
}
