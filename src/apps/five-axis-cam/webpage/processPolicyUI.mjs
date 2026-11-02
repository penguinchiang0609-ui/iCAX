import { button, section, hint, field, choices } from './workspaceControls.mjs';

export function renderLeadFields(d) {
  const leadFields = (prefix, title) => field(d,`${prefix}Kind`,title,choices([['None','无'],['Line','直线'],['Arc','圆弧']]))
    + (d[`${prefix}Kind`] !== 'None' ? field(d,`${prefix}Length`,'长度（mm）') + field(d,`${prefix}Angle`,'角度（°）')
      + (d[`${prefix}Kind`] === 'Arc' ? field(d,`${prefix}Radius`,'半径（mm）') : '') : '');
  return field(d,'cutLeadPlacement','引线位置',choices([['ScrapSide','废料侧'],['OutsideStock','板外']]))
    + leadFields('cutLead','引入')
    + field(d,'cutExitPlacement','引出位置',choices([['ScrapSide','废料侧'],['OutsideStock','板外']])) + leadFields('cutExit','引出');
}
export function renderKerfFields(d) {
  return field(d,'cutKerf','切缝宽度（mm）')
    + field(d,'cutKerfOwner','补偿执行方',choices([['Cam','CAM'],['Executor','执行设备']]))
    + field(d,'cutMeasurement','切缝测量截面',choices([['Surface','表面'],['BeamNormalSection','光束法截面'],['ThicknessSection','厚度截面']]))
    + field(d,'cutCorner','拐角',choices([['Miter','尖角'],['Round','圆角'],['Bevel','斜角']])) + field(d,'cutMiterLimit','尖角上限');
}
export function renderPoseFields(d) {
  return field(d,'cutPoseInitialization','方向基准',choices([['SurfaceNormal','表面法向'],['TangentInSurface','面内切向'],['CrossTangentInSurface','面内横向'],['FixedDirection','固定 IJK']]))
    + (d.cutPoseInitialization==='FixedDirection'?field(d,'cutAxisI','I（工件坐标系）')+field(d,'cutAxisJ','J')+field(d,'cutAxisK','K'):field(d,'cutLeadTilt','前倾角') + field(d,'cutSideTilt','侧倾角')) + field(d,'cutLeadTolerance','前倾偏差上限')
    + field(d,'cutSideTolerance','侧倾偏差上限') + field(d,'cutAngularTolerance','总角偏差上限')
    + field(d,'cutNormalPreference','法向优先权重') + field(d,'cutTransition','过渡分配',choices([['ArcLength','按弧长'],['DirectionChange','按方向变化']]));
}
export function renderCutPolicyDraft(d, view, selectedCount, retry = false) {
  return section('cut-process', '确认工艺', field(d,'cutProcessName','名称',{text:true})
    + hint('以下工艺值由设备工艺表提供。允许先保存几何定义，缺项时不能生成完整加工计划。')
    + field(d,'cutMaterial','材料',{text:true}) + field(d,'cutThickness','厚度（mm）') + field(d,'cutGas','辅助气体',{text:true})
    + field(d,'cutFeed','速度（mm/s）') + field(d,'cutPower','功率（W）') + field(d,'cutPressure','气压（bar）')
    + field(d,'cutStandOff','跟随高度（mm）') + field(d,'cutFocus','焦点偏置（mm）') + field(d,'cutPierceTime','穿孔时间（s）') + field(d,'cutPierceRadius','穿孔影响半径（mm）'))
    + section('cut-compensation','补偿与引线',renderKerfFields(d) + renderLeadFields(d) + field(d,'cutOvercut','闭合过切（mm）'))
    + section('cut-pose','连续姿态要求',renderPoseFields(d),'°')
    + section('cut-confirm','加入加工任务',hint(`已选 ${selectedCount} 项候选。创建切割定义后仍需生成刀路、配置机床并验证。`)
      + button('five.extraction.create',retry?'重试上次确认':`创建 ${selectedCount} 项切割定义`,view.pending || (!selectedCount && !retry)?'disabled':'class="five-primary"')
      + (retry?hint('上次确认尚未收到成功回执。重试使用当时提交的参数，当前编辑的草稿仍保留。'):''));
}

