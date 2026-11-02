export const processDraftDefaults = {
  cutProcessName: '自定义切割工艺', cutMaterial: '', cutThickness: '', cutGas: '',
  cutFeed: '', cutPower: '', cutPressure: '', cutStandOff: '', cutFocus: '', cutPierceTime: '', cutPierceRadius: '',
  cutKerf: '0', cutKerfOwner: 'Cam', cutMeasurement: 'Surface', cutCorner: 'Miter', cutMiterLimit: '4',
  cutLeadKind: 'None', cutLeadPlacement: 'ScrapSide', cutLeadLength: '0', cutLeadAngle: '0', cutLeadRadius: '',
  cutExitKind: 'None', cutExitPlacement: 'ScrapSide', cutExitLength: '0', cutExitAngle: '0', cutExitRadius: '', cutOvercut: '0', cutGap: '0',
  cutPoseInitialization: 'SurfaceNormal', cutTransition: 'ArcLength', cutLeadTilt: '0', cutSideTilt: '0',
  cutAxisI:'0',cutAxisJ:'0',cutAxisK:'1',
  cutLeadTolerance: '0', cutSideTolerance: '0', cutAngularTolerance: '0', cutNormalPreference: '0.5',
};
const numeric = (d, key, label, min = -Infinity, strict = false) => {
  const number = Number(d[key]);
  if (!String(d[key] ?? '').trim() || !Number.isFinite(number) || number < min || (strict && number === min))
    throw Error(`${label}需要填写${strict && min === 0 ? '正数' : min === 0 ? '非负数' : '有效数值'}。`);
  return number;
};
const angle = (d, key, label, tolerance = false) => {
  const number = numeric(d, key, label, tolerance ? 0 : -180);
  if (number > 180) throw Error(`${label}不能超过 180°。`);
  return number * Math.PI / 180;
};
export const CUT_PARAMETER_FIELDS=Object.freeze([
  ['cutFeed','feedMmPerSecond','速度（mm/s）',0,true],['cutPower','laserPowerW','功率（W）',0,true],
  ['cutPressure','gasPressureBar','气压（bar）',0,false],['cutStandOff','standOffMm','跟随高度（mm）',0,false],
  ['cutFocus','focusOffsetMm','焦点偏置（mm）',-Infinity,false],['cutPierceTime','pierceTimeSeconds','穿孔时间（s）',0,false],
  ['cutPierceRadius','pierceFootprintRadiusMm','穿孔影响半径（mm）',0,false],
]);
export function parameterOverrides(d,assignment,dirtyFields){
  const values=new Map((assignment.overrides||[]).map(item=>[item.parameterId,item]));
  for(const [name,id,label,min,strict] of CUT_PARAMETER_FIELDS){
    if(!dirtyFields.includes(name))continue;
    if(!String(d[name]??'').trim())values.delete(id);
    else values.set(id,{parameterId:id,value:numeric(d,name,label,min,strict)});
  }
  return [...values.values()];
}
export function cutPolicyDraft(d,{includePose=true}={}) {
  const name = d.cutProcessName.trim(); if (!name) throw Error('请填写工艺名称。');
  const library = { name, parameters: {} };
  if (d.cutMaterial.trim()) library.material = d.cutMaterial.trim();
  if (d.cutGas.trim()) library.gas = d.cutGas.trim();
  if (d.cutThickness.trim()) library.thicknessMm = numeric(d, 'cutThickness', '材料厚度', 0, true);
  for (const [key, id, label, min, strict] of [
    ['cutFeed','feedMmPerSecond','切割速度',0,true], ['cutPower','laserPowerW','激光功率',0,true],
    ['cutPressure','gasPressureBar','气压',0,false], ['cutStandOff','standOffMm','跟随高度',0,false],
    ['cutFocus','focusOffsetMm','焦点偏置',-Infinity,false], ['cutPierceTime','pierceTimeSeconds','穿孔时间',0,false],
    ['cutPierceRadius','pierceFootprintRadiusMm','穿孔影响半径',0,false],
  ]) if (d[key].trim()) library.parameters[id] = numeric(d,key,label,min,strict);
  const lead = (exit = false) => {
    const prefix = exit ? 'cutExit' : 'cutLead', kind = d[`${prefix}Kind`];
    const value = { kind, placement: d[`${prefix}Placement`], lengthMm: 0, angleRad: 0 };
    if (kind !== 'None') {
      value.lengthMm = numeric(d,`${prefix}Length`,exit?'引出长度':'引入长度',0,true);
      value.angleRad = angle(d,`${prefix}Angle`,exit?'引出角度':'引入角度');
      if (kind === 'Arc') value.radiusMm = numeric(d,`${prefix}Radius`,'引线半径',0,true);
    }
    return value;
  };
  const weight = includePose?numeric(d,'cutNormalPreference','法向优先权重',0):0;
  if (weight > 1) throw Error('法向优先权重须在 0 到 1 之间。');
  return { library,
    assignment: { overrides: [], kerf: { owner: d.cutKerfOwner, widthMm: numeric(d,'cutKerf','切缝宽度',0),
      measurementSection: d.cutMeasurement, cornerMode: d.cutCorner, miterLimit: numeric(d,'cutMiterLimit','尖角上限',1) },
      leadIn: lead(), leadOut: lead(true), overcutLengthMm: numeric(d,'cutOvercut','过切长度',0), gapLengthMm: numeric(d,'cutGap','缺口长度',0), layerKey: 'cut' },
    pose: includePose?{ mode: 'Continuous', initialization: d.cutPoseInitialization, transitionMetric: d.cutTransition,
      leadAngleRad: d.cutPoseInitialization==='FixedDirection'?0:angle(d,'cutLeadTilt','前倾角'),
      sideAngleRad: d.cutPoseInitialization==='FixedDirection'?0:angle(d,'cutSideTilt','侧倾角'),
      leadDeviationLimitRad: angle(d,'cutLeadTolerance','前倾偏差',true), sideDeviationLimitRad: angle(d,'cutSideTolerance','侧倾偏差',true),
      angularToleranceRad: angle(d,'cutAngularTolerance','总角偏差',true), surfaceNormalPreferenceWeight: weight, keypoints: [],
      ...(d.cutPoseInitialization==='FixedDirection'?{direction:(()=>{const v=['cutAxisI','cutAxisJ','cutAxisK'].map(k=>numeric(d,k,'刀轴 IJK'));const n=Math.hypot(...v);if(n<1e-12)throw Error('刀轴 IJK 不能为零向量。');return v.map(x=>x/n);})()}:{}),
    }:undefined };
}

