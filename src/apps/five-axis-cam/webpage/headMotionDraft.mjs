import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {machineForward} from './machineKinematics.mjs';
import {hasHeadWorkspacePreview,headWorkspaceSelection} from './headWorkspaceSelection.mjs';

export const headStructures=[['HH','双摆头'],['HW','摆头＋转台'],['WW','双转台／摇篮'],['H','单摆头'],['W','单转台'],['','无旋转轴']];
const matrix=a=>new THREE.Matrix4().set(...a),row=m=>[0,4,8,12,1,5,9,13,2,6,10,14,3,7,11,15].map(i=>m.elements[i]);
const number=v=>{if(!String(v).trim()||!Number.isFinite(Number(v)))throw Error('请输入有效的标定数值。');return Number(v);};
export function axisDraft(a){return {...structuredClone(a),name:a.name||'A',target:a.target||'Head',velocityDegS:String(a.velocityDegS??60),axis:a.axis.map(String),position:[a.origin[3],a.origin[7],a.origin[11]].map(String),rotation:new THREE.Euler().setFromRotationMatrix(matrix(a.origin),'ZYX').toArray().slice(0,3).map(x=>String(Number(THREE.MathUtils.radToDeg(x).toFixed(6)))),zeroOffsetDeg:String(a.zeroOffsetDeg),sign:String(a.sign),lowerDeg:String(a.lowerDeg),upperDeg:String(a.upperDeg)};}
export function axisInput(a){
 const origin=a.originDirty?row(new THREE.Matrix4().makeRotationFromEuler(new THREE.Euler(...a.rotation.map(number).map(THREE.MathUtils.degToRad),'ZYX')).setPosition(...a.position.map(number))):a.origin;
 const result={index:Number(a.index),name:a.name.trim(),target:a.target,origin,axis:a.axis.map(number),velocityDegS:number(a.velocityDegS),zeroOffsetDeg:number(a.zeroOffsetDeg),sign:number(a.sign),lowerDeg:number(a.lowerDeg),upperDeg:number(a.upperDeg)};
 if(!result.name||Math.hypot(...result.axis)<1e-9||result.lowerDeg>result.upperDeg||result.velocityDegS<=0)throw Error('请核对轴名称、轴向、速度与限位。');return result;
}
export const structureOf=axes=>'H'.repeat(axes.filter(a=>a.target!=='Workpiece').length)+'W'.repeat(axes.filter(a=>a.target==='Workpiece').length);
export function changeHeadStructure(d,model,kind){
 const physical=model.assembly?.nodes||[],available=physical.map((n,index)=>({n,index})).filter(({n})=>n.type!=='fixed');
 const old=[...d.axes],used=new Set(),names=new Set();
 d.axes=[...kind].map((role,i)=>{
  const target=role==='W'?'Workpiece':'Head',prior=(kind==='HW'&&target==='Head'?old.find(a=>a.target===target&&a.name==='A'):null)||old.find(a=>a.target===target&&!used.has(a.index));
  if(prior){used.add(prior.index);names.add(prior.name);return prior;}
  const candidate=target==='Head'?available.find(({index})=>!used.has(index)):null;
  let index=candidate?.index??physical.length;while(used.has(index))index++;
  let name=candidate?.n.joint||'ABC'[i]||'R';if(names.has(name))name=['A','B','C','U','V','W'].find(n=>!names.has(n));
  used.add(index);names.add(name);
  const axis=target==='Workpiece'?(kind==='HW'?[0,0,1]:kind==='WW'&&i===1?[0,0,1]:[1,0,0]):candidate?.n.axis||[0,1,0];
  return axisDraft({index,name,target,velocityDegS:60,origin:candidate?.n.origin||row(new THREE.Matrix4()),axis,zeroOffsetDeg:0,sign:1,lowerDeg:-180,upperDeg:180});
 });
 // A retained virtual axis must not occupy a newly assigned physical index.
 const all=new Set();for(const a of d.axes){while(all.has(a.index))a.index++;all.add(a.index);}
}
export function headPreviewDraft(view){
 if(!hasHeadWorkspacePreview(view))return view.fiveHeadEditor?.draft;
 const selection=headWorkspaceSelection(view),parameters=selection.head?.instance?.parameters;
 if(!parameters)return null;
 return selection.draft??={...structuredClone(parameters),tcp:parameters.tcp.map(String),beam:parameters.beam.map(String),gaugeOrigin:parameters.gaugeOrigin.map(String),lengthMm:String(parameters.lengthMm),axes:parameters.axes.map(axisDraft)};
}
export const previewAxes=view=>headPreviewDraft(view)?.axes||[];
export function draftHeadMotion(head,draft){
 if(!draft)return head.motion;
 const source=head.instance?.model||{...head,assembly:head.sourceAssembly||head.assembly},physical=source.assembly?.nodes||[],axes=draft.axes.map(axisInput),nodes=[],headNodes=[];
 const add=(parent,type,origin=new THREE.Matrix4(),axis=[0,0,1],extra={})=>{const i=nodes.length;nodes.push({parent,type,origin:row(origin),axis,position:0,lower:0,upper:0,...extra});return i;};
 add(-1,'fixed');let parent=0;
 for(let k=0;k<3;k++)parent=add(parent,'prismatic',new THREE.Matrix4(),[0,1,2].map(i=>i===k?1:0),{lower:-1e6,upper:1e6});
 const axisNode=(a,parent,parameterIndex)=>{
  const axis=new THREE.Vector3(...a.axis).normalize(),origin=matrix(a.origin).multiply(new THREE.Matrix4().makeRotationAxis(axis,-a.sign*THREE.MathUtils.degToRad(a.zeroOffsetDeg)));
  return add(parent,'revolute',origin,axis.multiplyScalar(a.sign).toArray(),{lower:THREE.MathUtils.degToRad(a.lowerDeg),upper:THREE.MathUtils.degToRad(a.upperDeg),parameterIndex,name:a.name});
 };
 axes.forEach((a,i)=>{if(a.target==='Head'&&a.index>=physical.length)parent=axisNode(a,parent,i);});
 physical.forEach((n,i)=>{
  const p=n.parent<0?parent:headNodes[n.parent],a=axes.find(a=>a.index===i&&a.target==='Head');
  headNodes.push(a?axisNode(a,p,axes.indexOf(a)):add(p,'fixed',matrix(n.origin),n.axis));
 });
 const tool=physical.length?headNodes[source.assembly.tool]:add(parent,'fixed');if(!physical.length)headNodes.push(tool);
 parent=0;axes.forEach((a,i)=>{if(a.target==='Workpiece')parent=axisNode(a,parent,i);});const support=add(parent,'fixed');
 const beam=new THREE.Vector3(...draft.beam.map(number)).normalize(),tcp=new THREE.Vector3(...(draft.tcpMode==='Gauge'?draft.gaugeOrigin:draft.tcp).map(number));if(draft.tcpMode==='Gauge')tcp.addScaledVector(beam,number(draft.lengthMm));
 const motion={nodes,tool,support,headNodes,tcp:tcp.toArray(),beam:beam.toArray()};
 const positions=nodes.map(n=>THREE.MathUtils.clamp(0,n.lower,n.upper)),world=machineForward(motion,positions);
 nodes[3].position=80-tcp.clone().applyMatrix4(world[tool]).z;
 return motion;
}
