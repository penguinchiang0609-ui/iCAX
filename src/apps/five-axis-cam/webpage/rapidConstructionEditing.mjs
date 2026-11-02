const add=(a,b)=>a.map((v,i)=>v+b[i]);
const sub=(a,b)=>a.map((v,i)=>v-b[i]);
const scale=(a,n)=>a.map(v=>v*n);
const dot=(a,b)=>a.reduce((n,v,i)=>n+v*b[i],0);
const length=a=>Math.hypot(...a);
const unit=a=>{const n=length(a);if(!Number.isFinite(n)||n<1e-10)throw Error('两端姿态无法确定曲线控制方向。');return scale(a,1/n);};
const mix=(a,b,t)=>a.map((v,i)=>v+(b[i]-v)*t);
const point=p=>Array.isArray(p)&&p.length===3&&p.every(Number.isFinite);
const format=n=>String(Number(n.toFixed(6)));
const endpointOverrides=source=>structuredClone(source.endpointOverrides||Object.fromEntries(['start','end'].filter(k=>source.manualPath?.[k]).map(k=>[k,source.manualPath[k]])));

function direction(a,b,t){
 const angle=Math.acos(Math.max(-1,Math.min(1,dot(a,b))));
 if(angle>Math.PI-1e-6)throw Error('两端姿态相反，无法确定曲线控制方向。');
 return unit(angle<1e-7?mix(a,b,t):add(scale(a,Math.sin((1-t)*angle)),scale(b,Math.sin(t*angle))));
}
export function isConstructionEdit(edit){return !!edit?.construction;}
// Retract is constructed as exact straight spans. These are its vertices,
// unlike the adaptive display samples returned for Arch and Follow curves.
export function createRapidPathEdit(source,polyline=false){
 if(!polyline&&source.construction?.kind!=='Retract')return createConstructionEdit(source);
 if(!source.points?.length||source.points.length<2||source.points.length>2050||source.points.length!==source.directions?.length
  ||!source.points.every(point)||!source.directions.every(point))throw Error('折线控制点尚未就绪，请重新生成当前空移。');
 const selected=!polyline&&source.construction?.turnPoint&&source.points.length===5?2:source.points.length>2?1:0;
 return {points:structuredClone(source.points),directions:structuredClone(source.directions),selected,
  selectedPoints:[selected],endpointOverrides:endpointOverrides(source),revision:0,changed:false,inserting:false,deleting:false,pathKind:'Polyline'};
}
export function validateConstructionEdit(edit){
 const c=edit.construction;
 if(!c||!['Retract','Arch','Follow'].includes(c.kind)||!Number.isFinite(c.followHeightMm)||c.followHeightMm!==edit.lockedFollowHeightMm
  ||!Number.isFinite(c.liftHeightMm)||c.liftHeightMm<=0||c.liftHeightMm>1e6||!point(c.lateralOffset)||c.lateralOffset.some(n=>Math.abs(n)>1e6))throw Error('请输入有效的曲线高度和侧向引导位置。');
 if(c.turnPoint!==undefined&&(c.kind!=='Retract'||!point(c.turnPoint)||c.turnPoint.some(n=>Math.abs(n)>1e8)||c.lateralOffset.some(n=>n!==0)))throw Error('请输入有效的转向位置。');
 return c;
}
export function createConstructionEdit(source){
 if(!source.construction)return null;
 if(!point(source.shapeHandle?.point)||!point(source.shapeHandle?.direction)||!point(source.sourceStart?.point)||!point(source.sourceStart?.direction)
  ||!point(source.sourceEnd?.point)||!point(source.sourceEnd?.direction)||!source.points?.length||!source.directions?.length)throw Error('曲线控制数据尚未就绪，请重新生成当前空移。');
 const construction=structuredClone(source.construction),start=structuredClone(source.sourceStart),end=structuredClone(source.sourceEnd);
 const points=[structuredClone(source.points[0]),structuredClone(source.shapeHandle.point),structuredClone(source.points.at(-1))];
 const directions=[structuredClone(source.directions[0]),structuredClone(source.shapeHandle.direction),structuredClone(source.directions.at(-1))];
 const edit={construction,lockedFollowHeightMm:construction.followHeightMm,sourceStart:start,sourceEnd:end,points,directions,endpointOverrides:endpointOverrides(source),
  samplePoints:structuredClone(source.points),sampleDirections:structuredClone(source.directions),selected:1,selectedPoints:[1],values:points[1].map(format),revision:0,changed:false,inserting:false,deleting:false,
  reference:{construction:structuredClone(construction),handle:structuredClone(points[1]),samples:structuredClone(source.points),directions:structuredClone(source.directions)}};
 validateConstructionEdit(edit);return edit;
}
export function constructionHandleMoved(edit,position,before=edit){
 if(before.construction.turnPoint!==undefined){
  edit.construction={...structuredClone(before.construction),turnPoint:[...position]};edit.points[1]=[...position];delete edit.liftInput;refreshConstructionSamples(edit);return;
 }
 const c=structuredClone(before.construction),axis=unit(edit.directions[1]),delta=sub(position,before.points[1]),vertical=dot(delta,axis);
 const lateral=sub(delta,scale(axis,vertical));
 c.lateralOffset=add(c.lateralOffset,lateral);
 if(c.kind!=='Follow'){
  const factor=c.kind==='Retract'?dot(scale(add(edit.sourceStart.direction,edit.sourceEnd.direction),.5),axis):1;
  if(Math.abs(factor)<1e-8)throw Error('两端姿态无法确定抬升方向。');
  c.liftHeightMm=Math.max(1e-6,c.liftHeightMm+vertical/factor);
  const constrained=(c.liftHeightMm-before.construction.liftHeightMm)*factor;
  edit.points[1]=add(before.points[1],add(lateral,scale(axis,constrained)));
 }
 edit.construction=c;
 if(c.kind==='Follow')edit.points[1]=add(before.points[1],lateral);
 delete edit.liftInput;
 refreshConstructionSamples(edit);
}
export function constructionLiftChanged(edit,value){
 if(edit.construction.turnPoint!==undefined)return false;
 const n=String(value).trim()?Number(value):NaN;
 edit.liftInput=value;
 if(!Number.isFinite(n)||n<=0||n>1e6)return false;
 const old=edit.construction.liftHeightMm,axis=unit(edit.directions[1]);
 const factor=edit.construction.kind==='Retract'?dot(scale(add(edit.sourceStart.direction,edit.sourceEnd.direction),.5),axis):1;
 edit.construction.liftHeightMm=n;edit.points[1]=add(edit.points[1],scale(axis,(n-old)*factor));refreshConstructionSamples(edit);return true;
}
export function constructionManualPath(edit){
 validateConstructionEdit(edit);
 if(edit.liftInput!==undefined&&(!String(edit.liftInput).trim()||!Number.isFinite(Number(edit.liftInput))||Number(edit.liftInput)<=0||Number(edit.liftInput)>1e6))throw Error('请输入有效的曲线高度。');
 return {construction:structuredClone(edit.construction),...structuredClone(edit.endpointOverrides||{})};
}
export function refreshConstructionSamples(edit){
 const c=edit.construction;
 if(!Number.isFinite(c.liftHeightMm)||c.liftHeightMm<=0)return;
 const start=edit.sourceStart,end=edit.sourceEnd;
 if(c.kind==='Retract'){
  const a={point:add(start.point,scale(start.direction,c.followHeightMm)),direction:start.direction},b={point:add(end.point,scale(end.direction,c.followHeightMm)),direction:end.direction};
  const first=add(a.point,scale(a.direction,c.liftHeightMm)),last=add(b.point,scale(b.direction,c.liftHeightMm));
  const points=[a.point,first],directions=[a.direction,a.direction];
  if(c.turnPoint!==undefined){points.push([...c.turnPoint],last,b.point);directions.push(b.direction,b.direction,b.direction);edit.samplePoints=points;edit.sampleDirections=directions;return;}
  const axis=direction(a.direction,b.direction,.5),lateral=sub(c.lateralOffset,scale(axis,dot(c.lateralOffset,axis)));
  if(length(c.lateralOffset)>1e-10){points.push(add(mix(first,last,.5),lateral));directions.push(axis);}
  points.push(last,b.point);directions.push(b.direction,b.direction);edit.samplePoints=points;edit.sampleDirections=directions;return;
 }
 if(c.kind==='Arch'){
  const count=Math.min(512,Math.max(32,Math.ceil(length(sub(end.point,start.point))/2),Math.ceil(Math.sqrt(c.liftHeightMm/.02)))),points=[],directions=[];
  for(let i=0;i<=count;i++){
   const t=i/count,axis=direction(start.direction,end.direction,t),lateral=scale(sub(c.lateralOffset,scale(axis,dot(c.lateralOffset,axis))),16*t*t*(1-t)*(1-t));
   points.push(add(add(mix(start.point,end.point,t*t*(3-2*t)),lateral),scale(axis,c.followHeightMm+c.liftHeightMm*4*t*(1-t))));directions.push(axis);
  }
  edit.samplePoints=points;edit.sampleDirections=directions;return;
 }
 // The local gesture is an unqualified guide preview. Native projection and
 // construction replaces it after saving; sampled vertices never become
 // editable controls or the persisted definition.
 const delta=sub(c.lateralOffset,edit.reference.construction.lateralOffset),last=edit.reference.samples.length-1;
 const startDelta=sub(add(start.point,scale(start.direction,c.followHeightMm)),edit.reference.samples[0]),endDelta=sub(add(end.point,scale(end.direction,c.followHeightMm)),edit.reference.samples[last]);
 edit.samplePoints=edit.reference.samples.map((p,i)=>{const t=last?i/last:0,axis=edit.reference.directions[i];return add(add(p,mix(startDelta,endDelta,t)),scale(sub(delta,scale(axis,dot(delta,axis))),16*t*t*(1-t)*(1-t)));});
 edit.sampleDirections=structuredClone(edit.reference.directions);
 edit.sampleDirections[0]=[...start.direction];edit.sampleDirections[last]=[...end.direction];
}
export function adoptConstructionReadback(edit,source){
 const updated=createConstructionEdit(source);if(!updated)return false;
 const values=edit.values,liftInput=edit.liftInput,typing=edit.typing,selected=edit.selected,revision=edit.revision;
 Object.assign(edit,updated,{selected,selectedPoints:[selected],changed:false,revision});
 if(typing){edit.values=values;edit.typing=typing;if(liftInput!==undefined)edit.liftInput=liftInput;}
 else{delete edit.liftInput;edit.values=edit.points[selected].map(format);}
 return true;
}
