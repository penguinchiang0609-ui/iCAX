import { toolAxisErrorText } from './toolAxisErrors.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { escapeAttr as attr, escapeText as esc } from '../../_shared/workbench/utils/format.mjs';
import { createToolAxisPointGlyph } from './toolAxisDisplay.mjs';
import { sceneColors } from './scenePalette.mjs';
import { sameToolAxisAnchor } from './toolAxisAnchors.mjs';

export const sameAxisAnchor=sameToolAxisAnchor;
const point=d=>[...(d.points||[]),...(d.newPoint?[d.newPoint]:[])].find(p=>p.id===d.pointId);
const saved=d=>point(d)?.influence;
const clamp=(x,a=0,b=1)=>Math.max(a,Math.min(b,x));
const fmt=x=>Number(x.toFixed(4)).toString();
const available=(view,d)=>d?.influence&&!d.loading&&!d.request&&!d.stale&&!view.pending&&!view.fivePoseSelection?.active&&!d.rotation?.gesture;
const sameStation=(a,b)=>sameAxisAnchor(a.anchor,b.anchor)||Math.abs(a.offset-b.offset)<1e-7;
export function resetAxisInfluence(d){
  const e=d.influence;if(!e)return;const activeIndex=e.activeIndex;
  const group=saved(d);e.referenceDirection=[...(group?.referenceDirection||d.axis.direction)];e.interpolation=group?.interpolation||'Smooth';
  for(const station of e.stations){const target=group?.targets.find(t=>sameAxisAnchor(t.anchor,station.anchor));station.weight=target?.weight??null;station.referenceDirection=[...(target?.referenceDirection||station.direction)];}
  e.activeIndex=e.stations[activeIndex]?.weight!==null&&e.stations[activeIndex]?activeIndex:e.stations.findIndex(s=>s.weight!==null);e.weightText=e.activeIndex<0?'':fmt(e.stations[e.activeIndex].weight*100);e.gesture=null;
}
export async function loadAxisInfluence(context,view,d,ops){
  if(!d?.axis)return;const token={key:d.key,pointId:d.pointId};d.influenceLoading=token;d.error='';ops.renderProject(context,view);
  const owns=()=>!view.disposed&&context.isCurrentProject?.()!==false&&view.fiveAxisPointEdit===d&&d.key===token.key&&d.pointId===token.pointId&&d.influenceLoading===token;
  try{
    const data=await context.sceneProxy.invoke('CamCut.ReadToolAxisSamples',{expected:structuredClone(view.scene.domain.expected),intentIds:[d.id]});if(!owns())return;
    const cut=data.cuts.find(c=>c.intentId===d.id);if(!cut)throw Error(data.errors?.[0]?.error||'无法读取当前刀路姿态。');
    const offsetAt=sample=>{let offset=sample.pathDistanceMm-d.axis.pathDistanceMm;if(cut.closed&&cut.lengthMm>0)offset=((offset+cut.lengthMm/2)%cut.lengthMm+cut.lengthMm)%cut.lengthMm-cut.lengthMm/2;return offset;};
    const at=point(d),stations=[],owners=(d.points||[]).filter(p=>p.influence&&!sameAxisAnchor(p.anchor,at.anchor)&&!cut.samples.some(s=>sameAxisAnchor(s.anchor,p.anchor)&&Math.abs(offsetAt(s))<1e-7));
    for(const sample of cut.samples){
      const offset=offsetAt(sample);
      if(Math.abs(offset)<1e-7||sameAxisAnchor(sample.anchor,at.anchor))continue;
      const station={...sample,offset,weight:null,referenceDirection:[...sample.direction]};if(stations.some(s=>sameStation(s,station)))continue;
      station.owner=owners.find(p=>sameAxisAnchor(p.anchor,sample.anchor)||p.influence.targets.some(t=>sameAxisAnchor(t.anchor,sample.anchor)))?.label||'';stations.push(station);
    }
    stations.sort((a,b)=>a.offset-b.offset);d.influence={stations,picking:false,rangeStart:null,rangeMode:false};resetAxisInfluence(d);
  }catch(error){if(owns())d.error=toolAxisErrorText(error);}
  finally{if(owns()){d.influenceLoading=null;ops.renderProject(context,view);}}
}
export function axisInfluenceRequest(d){
  const e=d.influence;if(!e)return null;
  if(e.weightError)throw Error(e.weightError);
  const targets=e.stations.filter(s=>s.weight!==null).map(s=>({anchor:s.anchor,weight:s.weight}));
  if(targets.length>256)throw Error('一次最多选择 256 个跟随姿态。');
  return {targets,interpolation:e.interpolation};
}
const graphNodes=d=>[...d.influence.stations.map((s,index)=>({x:s.offset,y:s.weight??0,index,selected:s.weight!==null})),{x:0,y:1,index:-1,selected:true}].sort((a,b)=>a.x-b.x);
function graphLayout(d){
  const nodes=graphNodes(d),lo=Math.min(-1,...nodes.map(n=>n.x)),hi=Math.max(1,...nodes.map(n=>n.x));
  const x=n=>36+(n-lo)/(hi-lo)*244,y=n=>124-n*98;const pts=[];
  for(let i=0;i<nodes.length-1;i++)for(let j=0;j<12;j++){const a=nodes[i],b=nodes[i+1],t=j/12,f=d.influence.interpolation==='Smooth'?t*t*(3-2*t):t;pts.push(`${x(a.x+(b.x-a.x)*t)},${y(a.y+(b.y-a.y)*f)}`);}
  const last=nodes.at(-1);pts.push(`${x(last.x)},${y(last.y)}`);return {nodes,x,y,path:pts.join(' ')};
}
export function renderAxisInfluence(view,d,readonly){
  const e=d?.influence,disabled=readonly||!!d?.influenceLoading;
  const button=(command,label,on=false)=>`<button type="button" data-five-command="five.points.influence-${command}" aria-pressed="${on}" ${disabled?'disabled':''}>${label}</button>`;
  if(!e)return `<section class="five-axis-influence" data-five-key="axis-influence"><div class="five-button-row">${button('open',d?.influenceLoading?'正在读取姿态…':'选择受影响姿态')}</div></section>`;
  const graph=graphLayout(d),count=e.stations.filter(s=>s.weight!==null).length,active=e.stations[e.activeIndex];
  return `<section class="five-axis-influence" data-five-key="axis-influence"><h4>影响范围与曲线</h4>
    <div class="five-button-row">${button('pick',e.picking?'结束选择':'三维点选',e.picking)}${button('range',e.rangeMode?'取消连续选择':'连续选择',e.rangeMode)}${button('all','全选')}${button('clear','清空')}</div>
    <p data-five-key="influence-hint">${e.rangeMode?(e.rangeStart===null?'依次点击范围首、末两个姿态。':'再点击范围末端姿态。'):e.picking?'点击箭头切换跟随；Shift 点击可连续选择。':'黄色为主姿态，橙色为跟随姿态，灰色保持原方向。'} 已选 ${count} 个。</p>
    <div class="five-influence-stations" data-five-key="influence-stations" role="group" aria-label="受影响姿态列表">${e.stations.map((s,i)=>`<button type="button" data-five-key="influence-station-${i}" data-five-command="five.points.influence-toggle:${i}" aria-label="跟随姿态 ${i+1}" aria-pressed="${s.weight!==null}" title="${attr(s.owner?'由'+s.owner+'控制':fmt(s.offset)+' mm')}" ${disabled||s.owner?'disabled':''}>${i+1}${s.weight!==null?' ✓':''}</button>`).join('')}</div>
    <div class="five-button-row five-influence-presets">${button('uniform','等量跟随')}${button('linear','线性衰减')}${button('smooth','平滑衰减')}</div>
    <svg data-five-key="influence-graph" class="five-influence-graph" viewBox="0 0 300 154" role="group" aria-label="姿态影响曲线" aria-disabled="${disabled}">
      <path data-five-key="grid" d="M36 26H280M36 75H280M36 124H280M36 26V124M${graph.x(0)} 26V124" class="five-influence-grid"/>
      <text x="1" y="30">100%</text><text x="14" y="128">0%</text><text x="65" y="148">沿刀路距离（mm）</text>
      <polyline data-five-key="influence-line" points="${graph.path}" class="five-influence-line"/>
      ${graph.nodes.slice().sort((a,b)=>Number(a.index===e.activeIndex)-Number(b.index===e.activeIndex)).map(n=>`<circle data-five-key="influence-dot-${n.index}" data-five-weight-index="${n.index}" cx="${graph.x(n.x)}" cy="${graph.y(n.y)}" r="${n.index===-1?6:n.selected?5:2}" class="${n.index===-1?'primary':n.selected?'affected':'fixed'} ${e.activeIndex===n.index?'active':''}" ${n.index>=0&&n.selected?`tabindex="${disabled?-1:0}" role="slider" aria-label="姿态 ${n.index+1} 跟随比例" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${fmt(n.y*100)}"`:''}/>`).join('')}
    </svg>
    <label>当前曲线点<select data-five-point-field="influence-active" aria-label="当前影响曲线点" ${disabled||!count?'disabled':''}>${!count?'<option value="-1">尚未选择跟随姿态</option>':e.stations.map((s,i)=>s.weight===null?'':`<option value="${i}" ${e.activeIndex===i?'selected':''}>姿态 ${i+1} · ${fmt(s.offset)} mm</option>`).join('')}</select></label>
    <label class="five-influence-weight-field" data-five-key="influence-weight-field"><span>${active?`姿态 ${e.activeIndex+1} 跟随比例`:'跟随比例'}</span><input type="text" inputmode="decimal" data-five-key="influence-weight" data-five-point-field="influence-weight" aria-label="跟随比例百分比" value="${attr(e.weightText||'')}" ${!active||active.weight===null?'disabled':disabled?'readonly':''}><span>%</span></label>
    <label>点间过渡<select data-five-point-field="influence-interpolation" aria-label="影响曲线过渡" ${disabled?'disabled':''}><option value="Smooth" ${e.interpolation==='Smooth'?'selected':''}>平滑</option><option value="Linear" ${e.interpolation==='Linear'?'selected':''}>线性</option></select></label>
    <p>上下拖动橙色曲线点，或输入跟随比例。100% 跟随主姿态的全部旋转；0% 保持原方向。${e.weightError?esc(e.weightError):''}</p></section>`;
}
function changed(d){d.dirty=true;d.error='';d.influence.weightError='';}
function activate(e,index){e.activeIndex=index;e.weightText=e.stations[index]?.weight===null?'':fmt((e.stations[index]?.weight||0)*100);}
export function toggleAxisInfluence(d,index,range=false){
  const e=d.influence,s=e?.stations[index];if(!s||s.owner)return;
  if(e.rangeMode&&e.rangeStart===null){e.rangeStart=index;activate(e,index);return;}
  const from=e.rangeMode?e.rangeStart:range?e.lastIndex:null;
  if(from!==null&&from!==undefined){for(let i=Math.min(from,index);i<=Math.max(from,index);i++)if(!e.stations[i].owner)e.stations[i].weight??=1;e.rangeStart=null;e.rangeMode=false;}
  else s.weight=s.weight===null?1:null;
  e.lastIndex=index;activate(e,index);changed(d);
}
export async function axisInfluenceCommand(context,view,d,action,ops){
  if(action==='open'){await loadAxisInfluence(context,view,d,ops);return;}
  if(!available(view,d))return;const e=d.influence;
  if(action.startsWith('toggle:'))toggleAxisInfluence(d,Number(action.slice(7)));
  else if(action==='pick'){e.picking=!e.picking;e.rangeMode=false;e.rangeStart=null;d.rotation=null;}
  else if(action==='range'){e.rangeMode=!e.rangeMode;e.picking=e.rangeMode;e.rangeStart=null;d.rotation=null;}
  else if(action==='all'||action==='clear'){for(const s of e.stations)if(!s.owner)s.weight=action==='all'?1:null;changed(d);activate(e,e.stations.findIndex(s=>s.weight!==null));}
  else if(['uniform','linear','smooth'].includes(action)){
    for(const side of [-1,1]){const chosen=e.stations.filter(s=>s.weight!==null&&Math.sign(s.offset)===side),far=Math.max(0,...chosen.map(s=>Math.abs(s.offset)));
      const beyond=e.stations.filter(s=>Math.sign(s.offset)===side&&Math.abs(s.offset)>far+1e-7).map(s=>Math.abs(s.offset));const radius=beyond.length?Math.min(...beyond):far*1.25;
      for(const s of chosen){const t=radius?clamp(Math.abs(s.offset)/radius):0;s.weight=action==='uniform'?1:action==='linear'?1-t:1-t*t*(3-2*t);}
    }e.interpolation=action==='linear'?'Linear':'Smooth';changed(d);activate(e,e.activeIndex);
  }ops.renderProject(context,view);
}
export function axisInfluenceField(view,d,field,value){
  if(!available(view,d))return;const e=d.influence;
  if(field==='influence-active'){const index=Number(value);if(e.stations[index]?.weight!==null&&e.stations[index])activate(e,index);}
  else if(field==='influence-interpolation'){if(!['Linear','Smooth'].includes(value))return;e.interpolation=value;changed(d);}
  else if(field==='influence-weight'){
    e.weightText=value;const n=String(value).trim()?Number(value):NaN;
    if(!Number.isFinite(n)||n<0||n>100){e.weightError='跟随比例应在 0%～100% 之间。';d.dirty=true;return;}
    const s=e.stations[e.activeIndex];if(s&&s.weight!==null){s.weight=n/100;changed(d);}
  }
}
// Same deterministic half-turn as the native field, including exactly opposed axes.
export function axisInfluenceRotation(from,to){
  const a=new THREE.Vector3(...from).normalize(),b=new THREE.Vector3(...to).normalize(),cross=new THREE.Vector3().crossVectors(a,b),dot=clamp(a.dot(b),-1,1);
  if(dot<-.999999){const length=cross.length();if(length>1e-12)return new THREE.Quaternion().setFromAxisAngle(cross.normalize(),Math.atan2(length,dot));
    const values=a.toArray(),index=values.reduce((best,n,i)=>Math.abs(n)<Math.abs(values[best])?i:best,0),basis=new THREE.Vector3();basis.setComponent(index,1);cross.crossVectors(a,basis).normalize();
    if(cross.toArray().find(n=>Math.abs(n)>1e-12)<0)cross.negate();return new THREE.Quaternion(cross.x,cross.y,cross.z,0);
  }return new THREE.Quaternion(cross.x,cross.y,cross.z,1+dot).normalize();
}
export function addAxisInfluenceGlyphs(view,d,root,direction){
  const e=d.influence;if(!e)return;const delta=axisInfluenceRotation(e.referenceDirection,direction),identity=new THREE.Quaternion();
  for(const s of e.stations){const affected=s.weight!==null,axis=affected?new THREE.Vector3(...s.referenceDirection).applyQuaternion(identity.clone().slerp(delta,s.weight)).toArray():s.direction;
    const glyph=createToolAxisPointGlyph(view,d.id,s,axis);glyph.material.uniforms.size.value=affected?1.2:.8;glyph.material.uniforms.color.value.setHex(affected?sceneColors.leadOut:sceneColors.reference);glyph.userData.influenceStation=s;root.add(glyph);
  }
}
export function axisInfluenceAtPointer(view,d,event){
  if(!available(view,d)||!d.influence.picking)return null;const vp=view.viewport,box=vp.renderer.domElement.getBoundingClientRect(),cut=view.scene.intentToolpaths.find(c=>c.id===d.id),p=view.scene.domain.placements.find(p=>p.id===cut.placementId),matrix=new THREE.Matrix4().set(...p.transform);let best=13,index=-1;
  for(let i=0;i<d.influence.stations.length;i++){const s=d.influence.stations[i];if(s.owner)continue;
    const q=new THREE.Vector3(...s.point).applyMatrix4(matrix).project(vp.camera);if(q.z<-1||q.z>1)continue;
    const glyph=view.fiveAxisPointPreview?.group.children.find(g=>g.userData.influenceStation===s),direction=glyph?.userData.toolAxisSamples[0].direction||s.direction;
    const tip=new THREE.Vector3(...s.point).add(new THREE.Vector3(...direction)).applyMatrix4(matrix).project(vp.camera);
    const x=box.left+(q.x+1)*box.width/2,y=box.top+(1-q.y)*box.height/2,dx=(tip.x-q.x)*box.width/2,dy=(q.y-tip.y)*box.height/2,len=Math.hypot(dx,dy),size=glyph?.material.uniforms.size.value||1;
    const ax=len>1e-6?dx/len*22*size:0,ay=len>1e-6?dy/len*22*size:0,t=len>1e-6?clamp(((event.clientX-x)*ax+(event.clientY-y)*ay)/(ax*ax+ay*ay)):0;
    const dist=Math.hypot(event.clientX-x-ax*t,event.clientY-y-ay*t);if(dist<best){best=dist;index=i;}
  }return index>=0?{index,station:d.influence.stations[index]}:null;
}
export function pickAxisInfluence(context,view,d,event,ops){
  if(!available(view,d)||!d.influence.picking)return false;
  const hit=axisInfluenceAtPointer(view,d,event);if(hit){toggleAxisInfluence(d,hit.index,event.shiftKey);ops.renderProject(context,view);}return true;
}
export function syncAxisInfluenceGraph(context,view,d,ops){
  const svg=context.mount.querySelector('.five-influence-graph');if(!svg||!d?.influence)return;const e=d.influence;
  const update=event=>{if(!e.gesture||!available(view,d))return;const box=svg.getBoundingClientRect(),weight=clamp((124-(event.clientY-box.top)/box.height*154)/98);e.stations[e.gesture.index].weight=weight;activate(e,e.gesture.index);changed(d);ops.renderProject(context,view);};
  svg.onpointerdown=event=>{if(event.button!==0||!available(view,d))return;const index=Number(event.target.getAttribute('data-five-weight-index'));if(!event.target.hasAttribute('data-five-weight-index')||index<0||e.stations[index]?.weight===null)return;event.preventDefault();event.stopPropagation();
    e.gesture={index,weight:e.stations[index].weight,dirty:d.dirty};activate(e,index);svg.setPointerCapture(event.pointerId);update(event);};
  svg.onpointermove=event=>{if(e.gesture){event.preventDefault();update(event);}};
  svg.onpointerup=event=>{if(!e.gesture)return;update(event);e.gesture=null;if(svg.hasPointerCapture(event.pointerId))svg.releasePointerCapture(event.pointerId);context.actions?.refreshProjectHistoryControls?.();};
  svg.onpointercancel=()=>{if(!e.gesture)return;e.stations[e.gesture.index].weight=e.gesture.weight;d.dirty=e.gesture.dirty;activate(e,e.gesture.index);e.gesture=null;ops.renderProject(context,view);};
  svg.onkeydown=event=>{if(!available(view,d))return;const index=Number(event.target.getAttribute('data-five-weight-index')),s=e.stations[index];if(!s||s.weight===null||!['ArrowUp','ArrowDown','Escape'].includes(event.key))return;event.preventDefault();event.stopPropagation();
    if(event.key==='Escape'){svg.onpointercancel();return;}s.weight=clamp(s.weight+(event.key==='ArrowUp'?1:-1)*(event.shiftKey?.1:.01));activate(e,index);changed(d);ops.renderProject(context,view);};
}
