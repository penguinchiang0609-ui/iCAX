// Selection filtering is independent of any transform or alignment operation.
export const geometryPickModes=[['auto','自动识别'],['plane','平面／轮廓基准面'],['line','直线'],['direction','特征方向'],['center','圆心'],['sphere-center','球心'],['datum-point','特征中心'],['axis','特征轴'],['endpoint','端点'],['midpoint','中点'],['point','面上点']];
export function renderGeometryPickControls({active,mode='auto',disabled=false}){
  if(!active)return '';
  return `<div class="geometry-pick-controls" data-five-key="geometry-picking" role="group" aria-label="几何拾取"><label>拾取<select aria-label="几何拾取方式" data-geometry-pick-mode ${disabled?'disabled':''}>${geometryPickModes.map(([v,t])=>`<option value="${v}" ${mode===v?'selected':''}>${t}</option>`).join('')}</select></label></div>`;
}
