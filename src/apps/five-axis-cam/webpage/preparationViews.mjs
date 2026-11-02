import { section, hint, facts, disabled, choices, field } from './workspaceControls.mjs';

// Editors describe user input. Only native scene results may supply geometry,
// manufactured fixture parts, machine calibration or a verification report.
export function renderPreparation(id, d, context, view) {
  switch (id) {
    case 'projection': return section('projection-plane', '工作平面', field(d, 'plane', '参考平面', choices([['xy', 'XY 平面'], ['xz', 'XZ 平面'], ['yz', 'YZ 平面'], ['picked', '从模型选面']]))
      + (d.plane === 'picked' ? disabled('选择模型平面') : '') + field(d, 'planeOffset', '平面偏移'), 'mm')
      + section('projection-sketch', '平面轮廓尺寸', field(d, 'sketchShape', '轮廓', choices([['circle', '圆孔'], ['rectangle', '矩形孔'], ['slot', '腰型孔']]))
        + (d.sketchShape === 'circle' ? field(d, 'sketchDiameter', '孔径') : field(d, 'sketchWidth', '长度') + field(d, 'sketchHeight', '宽度'))
        + field(d, 'pitchX', '横向孔距') + field(d, 'pitchY', '纵向孔距'), 'mm')
      + section('projection-array', '孔组', field(d, 'countX', '横向数量') + field(d, 'countY', '纵向数量'))
      + section('projection-target', '曲面与方向', facts([['目标曲面', '尚未选择']]) + disabled('选择目标面')
        + field(d, 'projectionDirection', '投影方向', choices([['plane-normal', '工作平面法向'], ['reference', '指定参考方向']]))
        + (d.projectionDirection === 'reference' ? disabled('选择方向参考') : '')
        + field(d, 'designAxis', '孔设计轴向', choices([['projection', '采用投影方向'], ['independent', '单独指定安装方向']]))
        + (d.designAxis === 'independent' ? disabled('选择安装方向') : '')
        + hint('草图绘制与曲面投影待接入；尺寸草稿未形成刀路。多层命中必须确认目标，切割姿态另在工艺中设置。'));
    default: return null;
  }
}
