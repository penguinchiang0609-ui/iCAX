export function toolAxisErrorText(error){
  const message=String(error?.message??error??'');
  if(/LEAD_FRAME_DEGENERATE|LEAD_BOUNDARY_MISMATCH|LEAD_SOURCE_MISSING/.test(message))return '轮廓的切向或板面方向读取失败，本次路径计算未完成。';
  if(/AUTHORED_POSE_KERF_UNSUPPORTED|MANUAL_KERF_UNSUPPORTED/.test(message))return '非零切缝补偿暂不支持手动关键点姿态。请先将切缝宽度设为 0，再编辑局部刀轴。';
  if(/NEEDS_REBIND_CONNECTOR_BOUNDARY/.test(message))return '断点位于补偿连接段，请重新绑定到原始切割轮廓后再修改该片段的工艺。';
  if(/SELECTION_PROJECTION_NEEDS_REBIND/.test(message))return '所选片段无法与原始轮廓精确对应，请重新绑定后再修改工艺。';
  if(/LEAD_POSE_IS_ENDPOINT_DERIVED/.test(message))return '引线姿态跟随切削端点，请编辑相邻切削端点的刀轴。';
  return message;
}
