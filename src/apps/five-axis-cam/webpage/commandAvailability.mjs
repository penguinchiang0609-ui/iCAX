import {canAdjustMachinePart,canRotateMachinePart,selectedMachineJoint} from './machineIK.mjs';
import {preparationKind,workspaceState,activeTool,TOOLS} from './workspaceModel.mjs';
import {activeWorkpiece} from './workpieceState.mjs';
import {workpieceSelectionBusy} from './workpieceSelection.mjs';
import {camPreparationReason} from './workflow.mjs';
import {supportsToolAxis} from './toolAxisEligibility.mjs';
import {operationUnavailableReason} from './operationFeedback.mjs';
import {rapidListModel} from './rapidListModel.mjs';

const result=reason=>({disabled:!!reason,unavailableReason:reason||''});
export function commandAvailability(view,id){
  if(id==='five.simulation.collision-check')id='five.tool.collision';
  const d=view.scene?.domain,p=d?.placements?.find(p=>p.id===view.scene.activePlacementId)||d?.placements?.find(p=>p.setupId===d.activeSetupId),w=activeWorkpiece(view),s=d?.setups?.find(s=>s.id===p?.setupId),kind=preparationKind(view);
  const unit=d?.fixtureUnits?.find(u=>u.setupId===s?.id&&u.id===view.fiveAxisFixtures?.selected);
  const root=!!p&&kind==='workpiece',pose=!!p&&!!s&&kind==='setup',fixture=!!unit&&kind==='unit'&&!view.fiveFixtureDesignOpen;
  const busy=workpieceSelectionBusy(view)||view.viewport?.transformManipulator?.pointerId!=null,wait=busy?(operationUnavailableReason(view)||'请先结束当前拖动或确认正在编辑的坐标。'):'';
  const displayReason=view.viewport?.sceneObjects?.get(p?.id)?.geometry?'':'工件显示尚未就绪，请稍候。';
  const enabledReason=!w||!p?'请先选择工件。':w.enabled===false?'请先启用当前工件。':s?.enabled===false?'请先启用当前装夹姿态。':'';
  const rootReason=!root?'请先选择工件。':w?.enabled===false?'请先启用当前工件。':w?.locked?'请先解锁当前工件。':'';
  const poseReason=!pose?'请先选择装夹姿态。':enabledReason;
  const fixtureReason=!fixture?'请先选择夹具单元。':enabledReason;
  if(id.startsWith('five.extraction.begin'))return result(wait||camPreparationReason(view)||displayReason);
  if(id.startsWith('five.machine-ik.')){const m=view.scene?.machines?.find(m=>m.entityId===view.fiveMachine?.machineId);return result(wait||view.pending||view.fiveMachine?.busy||view.fiveMachineIK?.inflight||view.fiveMachineJog?.running?'请先完成当前操作。':!m||m.enabled===false?'请选择已启用的机床实例。':id==='five.machine-ik.fk'&&!selectedMachineJoint(view)?'请选择具有独立运动关节的节点。':id!=='five.machine-ik.close'&&!canAdjustMachinePart(view)?'请选择有运动关节的部件。':id==='five.machine-ik.rotate'&&!canRotateMachinePart(view)?'当前节点到根之间没有可用的旋转关节。':'');}
  if(id.startsWith('five.machine-panel.')){
    const action=id.slice(19),state=view.fiveMachine||{},machine=view.scene?.machines?.find(m=>m.entityId===state.machineId),setup=d?.setups?.find(s=>s.id===d.activeSetupId),report=state.report;
    if(view.pending||state.busy)return result('请先完成当前机床操作。');
    if(action==='import'||action==='instantiate')return result('');
    if(action==='delete'){
      const definition=view.scene?.machineDefinitions?.find(d=>d.id===state.definitionId)||view.scene?.machineDefinitions?.[0];
      return result(!view.fiveMachineDefinitionsOpen||!definition?'请在管理窗口中选择机床定义。':'');
    }
    if(['joints','colliders','post'].includes(action))return result(machine?'':'请先创建并选择机床实例。');
    if(action==='mount')return result(!machine||machine.enabled===false?'请选择已启用的机床实例。':!setup?'请先选择装夹作业。':setup.locked?'请先解锁当前装夹。':'');
    if(action==='pause')return result(state.playing?'':'仿真尚未开始。');
    if(action==='reset')return result(report?'':'请先生成仿真。');
    const configuration=setup?.machineSetup;
    if(!configuration?.machineId||state.machineId!==configuration.machineId||state.dirty)return result('请先选择作业并应用机床与工件摆放。');
    if(action==='build')return result('');
    if(state.postDirty)return result('请先保存实例后置并重新检查。');
    if(action==='play')return result(report?.playbackAllowed&&!state.playing?'':'请先检查生成可播放的仿真。');
    if(action==='export'||action==='program')return result(report?.status==='Ready'&&report?.collisionStatus==='Clear'&&machine?.postprocessor?.name?'':'请先完成仿真检查并配置实例后置。');
  }
  switch(id){
    case 'workpiece.import':return result(wait);
    case 'workpiece.remove':return result(wait||(!root?'请先选择工件。':w.locked?'请先解锁当前工件。':''));
    case 'five.preparation.copy:workpiece':return result(wait||(!root?'请先选择工件。':enabledReason));
    case 'five.tool.scale':case 'five.tool.align':return result(wait||rootReason||displayReason);
    case 'five.setup.create':return result(wait||(!root&&!pose?'请先选择工件或装夹姿态。':enabledReason));
    case 'five.setup.remove':return result(wait||(!pose?'请先选择装夹姿态。':s.locked?'请先解锁当前装夹姿态。':''));
    case 'five.preparation.copy:setup':return result(wait||poseReason);
    case 'five.tool.translate':case 'five.tool.rotate':return result(wait||poseReason||(s?.locked?'请先解锁当前装夹姿态。':'')||displayReason);
    case 'five.tool.registration':return result(wait||poseReason||displayReason);
    case 'five.fixture.add':case 'five.fixture.generate':return result(wait||(!pose&&!fixture?'请先选择装夹姿态。':enabledReason)||(s?.locked?'请先解锁当前装夹姿态。':'')||displayReason);
    case 'five.fixture.remove':return result(wait||(!fixture?'请先选择夹具单元。':unit.locked?'请先解锁当前夹具单元。':'')||(s?.locked?'请先解锁所属装夹姿态。':''));
    case 'five.preparation.copy:fixture':return result(wait||fixtureReason||(s?.locked?'请先解锁所属装夹姿态。':''));
    case 'five.fixture.library.open':return result(wait);
    case 'five.job.open':return result(wait||(!d?.workpieces?.length?'请先导入工件。':''));
    case 'five.fixture.focus.check':return result(wait||(!pose&&!fixture?'请先选择装夹姿态或夹具单元。':enabledReason)||(!(d?.fixtureUnits||[]).some(u=>u.setupId===s?.id&&u.enabled!==false)?'当前装夹没有已启用的夹具单元。':'')||displayReason);
  }
  const rawTool=id.startsWith('five.tool.')?id.slice(10):null;
  const tool=['manual-order-start','manual-order-current'].includes(rawTool)?'manual-order':rawTool;
  if(TOOLS[tool]?.area==='simulation')return result(wait||(view.fiveMachine?.busy?'请先完成当前机床操作。':''));

  const machining=TOOLS[tool]?.area==='toolpath'||id==='five.path.merge'||id==='five.leads.settings'||['five.spatial-leads','five.axes.reverse-all','five.pose-select.toggle','five.points.whole','five.points.reverse','five.points.add','five.points.edit','five.points.delete','five.points.delete-point'].includes(id);
  if(!machining)return null;
  const readiness=camPreparationReason(view);if(readiness)return result(readiness);
  const ui=workspaceState(view),ids=ui.selectedIntentIds||[ui.selectedIntentId],cuts=(view.scene?.intentToolpaths||[]).filter(c=>c.setupId===s?.id),selected=cuts.filter(c=>ids.includes(c.id));
  const editable=selected.length>0&&selected.every(c=>!c.locked&&c.enabled!==false),single=editable&&selected.length===1;
  const camBusy=!!(view.pending||view.fiveTreeRequest||view.fiveToolAxisRequest||view.fiveAxisPointLookup||view.fiveWorkpieceSwitching||view.fiveJobRequest||view.fixtureRequest||view.fiveGeneration?.request||view.fiveRapid?.request||view.fiveStart?.request||view.fiveLeads?.saving);
  if(camBusy)return result(operationUnavailableReason(view)||'正在保存刀路修改，保存完成后即可继续。');
  if(rawTool==='manual-order-current')return result(activeTool(view)==='manual-order'&&view.fiveOrder?.open&&view.fiveOrder.mode==='after'||selected.length===1&&selected[0].enabled!==false?'':'请先选择当前装夹中的一条已启用刀路。');
  if(tool==='leads'||id==='five.leads.settings')return result(cuts.some(c=>c.machiningKind==='Cut'&&c.enabled!==false&&!c.locked)?'':'当前装夹没有可编辑的切割刀路。');
  if(['projection','extract','manual-extract','settings','export'].includes(tool))return result('');
  if(id==='five.path.merge')return result(editable&&selected.length>=2?'':'请选择至少两条已启用、未锁定的刀路。');
  if(['split','path-translate','path-rotate','path-start','leads','overcut','compensation','adjust','reverse','clear','path-hole'].includes(tool)||id==='five.spatial-leads')
    return result((['path-start','compensation','adjust','path-hole'].includes(tool)?single:editable)?'':'请选择适用且已启用、未锁定的刀路。');
  if(['generate','manual-order','auto-order'].includes(tool))return result(cuts.some(c=>c.enabled!==false)?'':'请先建立并启用刀路。');
  if(['simulation','collision'].includes(tool))return result(cuts.some(c=>c.enabled!==false)?'':'请先建立并启用刀路。');
  if(tool==='rapid')return result(rapidListModel(view).transitions.length?'':'当前装夹还没有空移连接。');
  if(id==='five.pose-select.toggle')return result(view.fivePoseSelection?.active||cuts.some(c=>c.enabled!==false&&supportsToolAxis(c))?'':'当前装夹没有可选的刀轴姿态。');
  if(id==='five.axes.reverse-all')return result(cuts.some(c=>c.enabled!==false&&!c.locked&&supportsToolAxis(c))?'':'当前装夹没有可编辑的刀轴姿态。');
  if(id.startsWith('five.points.')||tool==='keypoint'){
    if(!single||!supportsToolAxis(selected[0]))return result('请先选择一条已启用、未锁定的刀路。');
    const point=view.fiveAxisPointEdit;
    if(point?.loading||point?.stale||point?.rotation?.gesture)return result('请先完成当前姿态操作。');
    if(id==='five.points.reverse'&&(activeTool(view)!=='keypoint'||point?.id!==selected[0].id||!point?.axis))return result('请先选择要反向的姿态。');
  }
  return result('');
}
