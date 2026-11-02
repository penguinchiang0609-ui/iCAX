import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';

const finite=n=>typeof n==='number'&&Number.isFinite(n);
const number=n=>finite(n)?String(Number(n.toFixed(4))):'未提供';
const vector=v=>Array.isArray(v)&&v.length===3&&v.every(finite)?v.map(number).join(' / '):'未提供';
const row=(id,label,value)=>`<div data-five-key="head-parameter-${attr(id)}"><dt>${esc(label)}</dt><dd data-five-head-parameter="${attr(id)}">${esc(value)}</dd></div>`;

// These are projections of the effective head snapshot, not another editable
// calibration store. In particular TCP Z is not an independently defined gauge
// length, and a saved joint position is not a calibrated encoder zero offset.
export function renderCuttingHeadParameters(head){
 if(!head)return '';
 const assembly=head.assembly,nodes=assembly?.nodes||[],joints=nodes.filter(n=>['revolute','continuous','prismatic'].includes(n.type)),parameters=head.instance?.parameters;
 const tool=nodes[assembly?.tool],tcp=assembly?assembly.tcp:head.tcpLocalPosition,beam=assembly?assembly.beam:head.beamLocalDirection;
 const source=head.collisionSource==='MachineInstance'?'绑定机床实例':head.builtinDefinitionId?'内置模型定义':head.kind==='Imported'?'导入模型与当前设置':'内置示意模型';
 const chain=joints.map(n=>n.joint||n.name||'未命名轴').join(' → ')||'刚性刀头';
 const axes=joints.map((n,i)=>{
  const matrix=n.origin,valid=Array.isArray(matrix)&&matrix.length===16&&matrix.every(finite);
  const position=valid?[matrix[3],matrix[7],matrix[11]]:null;
  const rotation=valid?new THREE.Euler().setFromRotationMatrix(new THREE.Matrix4().set(...matrix),'ZYX').toArray().slice(0,3).map(THREE.MathUtils.radToDeg):null;
  const rotary=n.type!=='prismatic',scale=rotary?180/Math.PI:1,unit=rotary?'°':'mm';
  const limits=finite(n.lower)&&finite(n.upper)?`${number(n.lower*scale)} ～ ${number(n.upper*scale)} ${unit}`:'未提供';
  return `<section class="five-head-axis-parameters" data-five-key="head-parameter-axis-${i}"><strong>${esc(n.joint||n.name||'未命名轴')}</strong><dl>
   ${row('axis-'+i+'-position','零位位置 · mm',vector(position))}
   ${row('axis-'+i+'-rotation','零位旋转 · °',vector(rotation))}
   ${row('axis-'+i+'-direction','局部轴向 XYZ',vector(n.axis))}
   ${row('axis-'+i+'-limits','允许范围',limits)}
   ${row('axis-'+i+'-position-value','模型保存位置',finite(n.position)?`${number(n.position*scale)} ${unit}`:'未提供')}
   ${row('axis-'+i+'-zero-offset','零偏修正',finite(n.zeroOffsetDeg)?number(n.zeroOffsetDeg)+' °':head.collisionSource==='MachineInstance'?'由机床有效轴系管理':'未单独声明')}
  </dl></section>`;
 }).join('');
 return `<section class="five-head-parameters" data-five-key="head-parameters" aria-label="机构与 TCP 参数"><h3>机构与 TCP 参数</h3><dl>
  ${row('source','参数来源',source)}${row('chain','实际串接顺序',chain)}
  ${assembly&&Array.isArray(assembly.workpieceAxes)?row('workpiece-axes','工作台回转轴',assembly.workpieceAxes.join(' / ')||'无（模型声明）'):''}
  ${row('reference','TCP 坐标基准',tool?.name?tool.name+' · 局部坐标系':'刀具部件局部坐标系')}
  ${row('tcp','TCP XYZ · mm',vector(tcp))}${row('beam','光束方向 XYZ',vector(beam))}
  ${row('gauge-length','刀长定义',parameters?.tcpMode==='Gauge'?number(parameters.lengthMm)+' mm · 安装基准 '+vector(parameters.gaugeOrigin):parameters?'按局部 TCP 坐标定义':'未定义测量基准')}
 </dl>${joints.length?`<details data-five-key="head-axis-parameters"><summary>轴心、方向与限位</summary><p class="five-head-parameter-note">零位位置和旋转相对父部件；旋转 XYZ 按 ZYX 组合。局部轴向相对关节零位坐标系。模型保存位置与实测零偏是不同参数。</p>${axes}</details>`:''}
 <p class="five-head-parameter-note">当前展示有效参数${head.instance?' · 工程实例 '+esc(head.instance.name):''}。实机测量记录与验收独立管理；速度与加速度约束、工件坐标和输出补偿方式由整机与加工设置管理。</p></section>`;
}
