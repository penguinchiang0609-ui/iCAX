import { escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';

const number=n=>Math.abs(n)<1e-10?'0':Number(n.toFixed(8)).toString();
const parse=values=>values.map(value=>String(value).trim()?Number(value):NaN);
const draftKey=d=>JSON.stringify([d.values,d.frame,d.pointId]);
export const toolAxisNotation=view=>view.fiveToolAxisNotation||'Euler';

// For XYZ Euler angles, R = Rx * Ry * Rz. The tool axis is R * +Z;
// its direction contains no roll, so choose Z=0, Y in [-90,90]. At
// the +/-X singularity choose X=0. Never infer spindle roll from IJK.
export function directionToEuler(values){
  const [i,j,k]=parse(values),length=Math.hypot(i,j,k);
  if(!Number.isFinite(length)||length<1e-12)throw Error('请输入有效的 IJK，且不能为零向量。');
  const side=Math.hypot(j,k);
  return [side/length<1e-12?0:Math.atan2(-j,k)*180/Math.PI,Math.atan2(i,side)*180/Math.PI,0];
}
export function eulerToDirection(values){
  const [x,y,z]=parse(values);
  if(![x,y,z].every(Number.isFinite)||z!==0)throw Error('请输入有效的姿态角；刀轴自转角固定为 0。');
  const a=x%360*Math.PI/180,b=y%360*Math.PI/180;
  return [Math.sin(b),-Math.sin(a)*Math.cos(b),Math.cos(a)*Math.cos(b)];
}
function angles(d){
  const key=draftKey(d);
  if(d.eulerInput?.key!==key){
    try{d.eulerInput={key,values:directionToEuler(d.values).map(number),error:''};}
    catch(error){d.eulerInput={key,values:['','','0'],error:error.message};}
  }
  return d.eulerInput;
}
export function validateToolAxisAngles(d){if(d.eulerInput?.key===draftKey(d)&&d.eulerInput.error)throw Error(d.eulerInput.error);}
export function changeToolAxisNotation(view,d,value){
  if(!['Euler','IJK'].includes(value))return;
  try{validateToolAxisAngles(d);if(value==='Euler')directionToEuler(d.values);view.fiveToolAxisNotation=value;d.error='';}
  catch(error){d.error=error.message;}
}
export function changeToolAxisAngle(d,field,value){
  const index=Number(field.slice(6));if(![0,1].includes(index))return false;
  const input=angles(d);input.values[index]=value;
  try{d.values=eulerToDirection(input.values).map(number);input.key=draftKey(d);input.error='';}
  catch(error){input.error=error.message;}
  d.error=input.error;return true;
}
export function renderToolAxisVector(view,d,{prefix,scope,readonly,busy}){
  const euler=toolAxisNotation(view)==='Euler',values=euler?angles(d).values:d.values,axes=euler?['X','Y','Z']:['I','J','K'];
  return `<label>姿态表示<select data-five-${prefix}-field="notation" aria-label="姿态表示方式" ${busy?'disabled':''}><option value="Euler" ${euler?'selected':''}>欧拉角（° · XYZ）</option><option value="IJK" ${!euler?'selected':''}>方向向量（IJK）</option></select></label>
    <div class="five-axis-vector" data-five-key="${prefix}-vector">${axes.map((axis,i)=>`<label>${axis}<input type="text" inputmode="decimal" autocomplete="off" aria-label="${scope} ${euler?'欧拉角 ':''}${axis}" data-five-${prefix}-field="${euler?'angle-'+i:i}" data-five-key="${prefix}-${euler?'angle-':''}${axis}" value="${attr(values?.[i]??'')}" data-model-value-authoritative="true" ${readonly||euler&&i===2?'readonly':''} ${euler&&i===2?'title="刀轴方向不包含绕自身轴的自转，Z 角固定为 0"':''}></label>`).join('')}</div>
    ${euler?'<p>以 +Z 为刀轴基准，XYZ 欧拉角；刀轴自转角固定为 0。</p>':''}`;
}
export function syncToolAxisVectorFields(mount,view,d,prefix){
  const euler=toolAxisNotation(view)==='Euler',values=euler?angles(d).values:d.values;
  for(let i=0;i<3;i++){const node=mount.querySelector(`[data-five-${prefix}-field="${euler?'angle-'+i:i}"]`);if(node&&node.value!==values[i])node.value=values[i];}
}
