export const identityFrame=()=>({origin:[0,0,0],x:[1,0,0],y:[0,1,0],z:[0,0,1]});
export function persistedWorkpieceFrame(view){
  const domain=view.scene?.domain,placement=domain?.placements?.find(p=>p.id===view.scene.activePlacementId),workpiece=domain?.workpieces?.find(w=>w.id===placement?.workpieceId);
  return structuredClone(workpiece?.coordinateFrame||identityFrame());
}
const dot=(a,b)=>a.reduce((s,n,i)=>s+n*b[i],0);
const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
const clean=n=>Math.abs(n)<1e-12?0:n;
const unit=a=>{const length=Math.hypot(...a);if(!Number.isFinite(length)||length<1e-10)throw Error('轴方向不能为零向量。');return a.map(n=>clean(n/length));};
export const frameNumber=n=>String(Number(n.toPrecision(10)));
export const frameStrings=frame=>Object.fromEntries(Object.entries(frame).map(([key,v])=>[key,v.map(frameNumber)]));
export function frameVector(values){
  if(values.some(v=>!String(v).trim()||!Number.isFinite(Number(v))))throw Error('请输入完整的有限数值。');
  return values.map(Number);
}
// Rotate the complete orthonormal frame by the shortest rotation onto the
// edited axis. The other two axes stay orthogonal and keep a right-handed frame.
export function turnFrame(frame,axis,direction){
  const target=unit(direction),from=frame[axis],normal=cross(from,target),sin=Math.hypot(...normal),cos=Math.max(-1,Math.min(1,dot(from,target)));
  let rotate;
  if(sin<1e-12&&cos>0)rotate=v=>[...v];
  else{
    const n=sin<1e-12?frame[{x:'y',y:'z',z:'x'}[axis]]:unit(normal),s=sin<1e-12?0:sin;
    rotate=v=>{const t=cross(n,v),d=dot(n,v);return v.map((value,i)=>clean(value*cos+t[i]*s+n[i]*d*(1-cos)));};
  }
  const result={origin:[...frame.origin],x:unit(rotate(frame.x)),y:unit(rotate(frame.y)),z:unit(rotate(frame.z))};
  result[axis]=target;return result;
}
// Keep the first chosen axis exact; project the second direction onto its
// perpendicular plane. Y is always derived from Z cross X, never edited.
export function editFrameAxis(frame,axis,direction,firstAxis=axis){
  if(!['x','z'].includes(axis))throw Error('Y 轴由 X、Z 轴自动计算。');
  if(!firstAxis||firstAxis===axis)return turnFrame(frame,axis,direction);
  const target=unit(direction),fixed=frame[firstAxis],parallel=dot(target,fixed),perpendicular=target.map((n,i)=>n-parallel*fixed[i]);
  if(Math.hypot(...perpendicular)<1e-8)throw Error('X 轴与 Z 轴不能平行，请选择另一条边或方向。');
  const result={origin:[...frame.origin],x:[...frame.x],z:[...frame.z]};
  result[axis]=unit(perpendicular);result.y=unit(cross(result.z,result.x));return result;
}
export function frameTransform(frame){
  return [...frame.x,-dot(frame.x,frame.origin),...frame.y,-dot(frame.y,frame.origin),...frame.z,-dot(frame.z,frame.origin),0,0,0,1].map(clean);
}
export function frameChanged(frame){
  const initial=identityFrame();return Object.keys(initial).some(key=>frame[key].some((n,i)=>Math.abs(n-initial[key][i])>1e-10));
}
export function frameInReference(frame,reference=identityFrame()){
  const project=v=>['x','y','z'].map(axis=>clean(dot(reference[axis],v)));
  return {origin:project(frame.origin.map((n,i)=>n-reference.origin[i])),x:project(frame.x),y:project(frame.y),z:project(frame.z)};
}
export function frameReferenceVector(value,reference=identityFrame(),point=false){
  return [0,1,2].map(i=>clean((point?reference.origin[i]:0)+['x','y','z'].reduce((s,axis,j)=>s+reference[axis][i]*value[j],0)));
}
