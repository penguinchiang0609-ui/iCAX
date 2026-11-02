// A draft is editable UI state. Saved inputs and fit reports come from the Scene.
export const registrationTool = method => method === 'PairedPoints' ? 'fine' : ['Points', 'Lines', 'Plane'].includes(method) ? 'rough' : null;
export const registrationMethodName = method => ({ Points: '选点定位', Lines: '选直线定位', Plane: '选面定位', PairedPoints: '实测点配对' })[method] || '尚未定位';
export function populateRegistrationDraft(draft, report) {
  const input = report.input;
  const put = (prefix, values) => values.forEach((v, i) => { draft[`${prefix}${'XYZ'[i]}`] = String(v); });
  draft.referenceQueries = structuredClone(input.referenceQueries ?? {});
  draft.referenceResults = {};
  draft.referenceFaces = {};
  draft.registrationPreview = null;
  if (input.method === 'PairedPoints') {
    draft.pointCount = String(input.modelPoints.length);
    draft.registrationTolerance = String(input.maximumResidualMm);
    draft.weightMode = input.weights ? 'custom' : 'equal';
    draft.weightSource = input.weightSource ?? '';
    input.modelPoints.forEach((p, i) => put(`model${i+1}`, p));
    input.measuredPoints.forEach((p, i) => put(`measured${i+1}`, p));
    input.modelPoints.forEach((_, i) => { draft[`weight${i+1}`] = String(input.weights?.[i] ?? 1); });
  } else {
    draft.roughMethod = ({ Points: 'point', Lines: 'line', Plane: 'face' })[input.method];
    put('origin', input.origin);
    put('axisX', input.method === 'Lines' ? input.xDirection : input.xPoint);
    put('plane', input.method === 'Points' ? input.planePoint : input.method === 'Lines' ? input.planeDirection : input.normal);
  }
}
export function registrationError(error) {
  const message = error?.message ?? String(error);
  const descriptions = [
    [/REGISTRATION_DEGENERATE_POINTS/, '定位点过于接近或接近一条直线，请选择分布更开的点。'],
    [/REGISTRATION_(COLLINEAR|PARALLEL|ZERO)_/, '定位方向退化：请选择非平行的基准，且 X 方向不能垂直于所选平面。'],
    [/REGISTRATION_(INVALID_TOLERANCE|RESIDUAL_EXCEEDED)/, '定位误差超过允许值，或误差限无效，请检查点位配对、实测坐标和误差限。'],
    [/REGISTRATION_INVALID_WEIGHT/, '每个测量点的权重必须是大于 0 的数。'],
    [/REGISTRATION_POINT_COUNT/, '模型点与实测点须一一配对，且至少有 3 对。'],
    [/REGISTRATION_INVALID_COORDINATE/, '坐标须为有限数值，且绝对值不超过 10 亿毫米。'],
    [/CAD_REFERENCE_REQUIRES_PLANE/, '请拾取平面，曲面不能用作平面定位基准。'],
    [/CAD_REFERENCE_(NOT_FOUND|TOO_FAR)/, '附近没有找到所选基准，请靠近所需的点、直线或圆边重新拾取。'],
    [/CAD_REFERENCE_AMBIGUOUS/, '附近有多个同样接近的基准，请换一个更明确的位置拾取。'],
    [/CAD_REFERENCE_INVALID_DISTANCE/, '捕捉距离应大于 0 且不超过 100 mm。'],
    [/STALE|EPOCH|REVISION|SOURCE_CHANGED/, '工程或定位来源已变化，请读取当前工程状态后重新计算。'],
  ];
  return descriptions.find(([pattern]) => pattern.test(message))?.[1] ?? message;
}
