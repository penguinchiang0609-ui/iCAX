import {renderToolAxisVector,changeToolAxisAngle,changeToolAxisNotation,validateToolAxisAngles} from './toolAxisAngles.mjs';

const number=n=>String(Number(n.toFixed(8)));
const locked=e=>!!e.construction&&e.selected===1;
export function rapidPointPose(e){
  const direction=e.directions[e.selected],key=JSON.stringify([e.selected,direction]);
  if(e.poseDraft?.source!==key)e.poseDraft={source:key,pointId:e.selected,frame:'Setup',values:direction.map(number),error:''};
  return e.poseDraft;
}
export function validateRapidPointPose(e){
  const d=rapidPointPose(e);validateToolAxisAngles(d);
  const v=d.values.map(x=>String(x).trim()?Number(x):NaN),length=Math.hypot(...v);
  if(!v.every(Number.isFinite)||!Number.isFinite(length)||length<1e-12)throw Error('请输入有效的 IJK，且不能为零向量。');
  return v.map(x=>x/length);
}
export function rapidPointPoseField(view,field,value){
  const e=view.fiveRapid?.pathEdit;
  if(!e||!['notation','0','1','2','angle-0','angle-1','angle-2'].includes(field))return false;
  const d=rapidPointPose(e);if(field==='notation'){changeToolAxisNotation(view,d,value);view.error=d.error;return true;}
  if(locked(e))return true;
  if(field.startsWith('angle-'))changeToolAxisAngle(d,field,value);else {d.values[Number(field)]=value;delete d.eulerInput;}
  try{e.directions[e.selected]=validateRapidPointPose(e);d.source=JSON.stringify([e.selected,e.directions[e.selected]]);d.error='';}
  catch(error){d.error=error.message;}
  view.error=d.error;return true;
}
export function renderRapidPointPose(view,busy){
  const e=view.fiveRapid.pathEdit,d=rapidPointPose(e);
  return `<section class="five-rapid-point-pose" data-five-key="rapid-point-pose"><strong>当前点姿态</strong>${renderToolAxisVector(view,d,{prefix:'rapid',scope:'空移姿态',readonly:!!busy||locked(e),busy:!!busy})}<small>方向由路径点指向刀头；修改只作用于当前空移。</small></section>`;
}
