import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';

// Product-owned box face gesture. A face moves along its local normal while
// its opposite face stays fixed; the visual part is never transformed.
export function attachColliderBounds(mount,binding,hooks){
 let gesture=null;
 const state=view=>hooks.state?.(view)??view.fiveMachineColliders;
 const viewport=view=>hooks.viewport?.(view)??view.viewport;
 const listen=(type,callback,options)=>mount.addEventListener(type,callback,hooks.signal?{...(typeof options==='boolean'?{capture:options}:options),signal:hooks.signal}:options);
 const ray=(vp,event)=>{const r=vp.renderer.domElement.getBoundingClientRect(),caster=new THREE.Raycaster();caster.setFromCamera({x:2*(event.clientX-r.left)/r.width-1,y:1-2*(event.clientY-r.top)/r.height},vp.camera);return caster.ray;};
 const finish=cancel=>{
  const g=gesture;if(!g)return;gesture=null;g.state.boundsFinish=null;
  if(g.canvas.hasPointerCapture(g.pointerId))g.canvas.releasePointerCapture(g.pointerId);
  g.canvas.style.cursor='';hooks.end(g.binding,cancel);
 };
 listen('pointerdown',event=>{
  if(gesture){event.preventDefault();event.stopImmediatePropagation();return;}
  const {view,context}=binding,s=state(view),vp=viewport(view);
  const blocked=hooks.blocked?hooks.blocked(view):view.pending||view.fiveMachine?.busy||view.fiveMachineJog?.running;
  if(event.button!==0||event.target!==vp?.renderer.domElement||s?.mode!=='bounds'||!s.show||blocked||context.isCurrentProject?.()===false)return;
  const data=hooks.get(view);if(!data)return;const {box,object,parent}=data,r=vp.renderer.domElement.getBoundingClientRect();object.updateWorldMatrix(true,false);
  let hit=null;
  for(let i=0;i<6;i++){const p=new THREE.Vector3().setComponent(Math.floor(i/2),i%2?.5:-.5).applyMatrix4(object.matrixWorld),screen=p.clone().project(vp.camera),distance=Math.hypot(event.clientX-r.left-(screen.x+1)*r.width/2,event.clientY-r.top-(1-screen.y)*r.height/2);if(Math.abs(screen.z)<=1&&distance<=10&&(!hit||distance<hit.distance-1||Math.abs(distance-hit.distance)<=1&&screen.z<hit.depth))hit={i,p,distance,depth:screen.z};}
  if(!hit)return;
  const axis=Math.floor(hit.i/2),sign=hit.i%2?1:-1,rotation=new THREE.Quaternion().setFromEuler(new THREE.Euler(...box.rotation.map(v=>Number(v)*Math.PI/180),'ZYX'));
  const localAxis=new THREE.Vector3().setComponent(axis,1).applyQuaternion(rotation),worldAxis=localAxis.clone().applyMatrix3(new THREE.Matrix3().setFromMatrix4(parent.matrixWorld)),units=worldAxis.length();worldAxis.normalize();
  const normal=vp.camera.getWorldDirection(new THREE.Vector3());normal.addScaledVector(worldAxis,-normal.dot(worldAxis));if(normal.lengthSq()<1e-5)return;normal.normalize();
  const plane=new THREE.Plane().setFromNormalAndCoplanarPoint(normal,hit.p),origin=ray(vp,event).intersectPlane(plane,new THREE.Vector3());if(!origin)return;
  hooks.begin(binding);gesture={binding:{...binding},view,state:s,viewport:vp,canvas:event.target,pointerId:event.pointerId,box,original:structuredClone(box),axis,sign,localAxis,worldAxis,units,plane,origin};
  s.boundsFinish=finish;event.target.focus({preventScroll:true});event.target.setPointerCapture(event.pointerId);event.target.style.cursor='grabbing';event.preventDefault();event.stopImmediatePropagation();
 },true);
 listen('pointermove',event=>{
  const g=gesture;if(!g||event.pointerId!==g.pointerId)return;
  const point=ray(g.viewport,event).intersectPlane(g.plane,new THREE.Vector3());if(point){
   let delta=point.sub(g.origin).dot(g.worldAxis)/g.units;
   if(event.ctrlKey||event.metaKey)delta=Math.round(delta); // 1 mm incremental snapping
   const size=Math.max(.001,Number(g.original.size[g.axis])+g.sign*delta);delta=g.sign*(size-Number(g.original.size[g.axis]));
   g.box.size[g.axis]=size;g.box.position=g.original.position.map((v,i)=>Number(v)+g.localAxis.getComponent(i)*delta*.5);
   hooks.change(g.binding);
  }
  event.preventDefault();event.stopImmediatePropagation();
 },true);
 listen('pointerup',event=>{if(gesture&&event.pointerId===gesture.pointerId){finish(false);event.preventDefault();event.stopImmediatePropagation();}},true);
 listen('pointercancel',event=>{if(gesture&&event.pointerId===gesture.pointerId)finish(true);},true);
 listen('lostpointercapture',event=>{if(gesture&&event.pointerId===gesture.pointerId)finish(true);},true);
 listen('wheel',event=>{if(gesture){event.preventDefault();event.stopImmediatePropagation();}},{capture:true,passive:false});
 return ()=>finish(true);
}
