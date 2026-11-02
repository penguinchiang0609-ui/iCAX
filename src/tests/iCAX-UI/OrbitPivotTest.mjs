import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {sampleOrbitPivot,orbitCameraState} from '../../iCAX-UI/SDK/Viewport/orbitPivot.mjs';
const rect={left:0,top:0,width:800,height:600},camera=new THREE.PerspectiveCamera(45,4/3,.01,1000);
camera.position.set(0,0,20);camera.lookAt(0,0,0);camera.updateMatrixWorld(true);
const mesh=(geometry,z=0)=>{const m=new THREE.Mesh(geometry,new THREE.MeshBasicMaterial({side:THREE.DoubleSide}));m.position.z=z;m.updateMatrixWorld(true);return m;};
const sample=(objects,extra={})=>sampleOrbitPivot({camera,objects,rect,clientX:400,clientY:300,radiusPx:6,...extra});
const near=(a,b)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-7,`${a} != ${b}`));
const front=mesh(new THREE.PlaneGeometry(20,20,16,16),2),back=mesh(new THREE.PlaneGeometry(20,20),-2);
const hits=sample([back,front]);assert.equal(hits.hitCount,9);near(hits.point,[0,0,2]);
const coarse=sample([mesh(new THREE.PlaneGeometry(20,20),2)]);near(coarse.point,hits.point);
const ring=mesh(new THREE.RingGeometry(.3,2,80));assert.equal(sample([ring],{radiusPx:0}),null);const aroundHole=sample([ring],{radiusPx:20});assert.equal(aroundHole.hitCount,8);near(aroundHole.point,[0,0,0]);
assert.equal(sample([]),null);assert.equal(sample([mesh(new THREE.PlaneGeometry(1,1),2)],{clientX:0,clientY:0,radiusPx:0}),null);
const pose=state=>{const s=Math.sin(state.phi),position=new THREE.Vector3(state.target.x+s*state.radius*Math.cos(state.theta),state.target.y+s*state.radius*Math.sin(state.theta),state.target.z+Math.cos(state.phi)*state.radius),c=camera.clone();c.position.copy(position);c.up.set(-Math.cos(state.phi)*Math.cos(state.theta),-Math.cos(state.phi)*Math.sin(state.theta),s);c.lookAt(state.target.x,state.target.y,state.target.z);c.updateMatrixWorld(true);return c;};
const start={target:{x:3,y:-4,z:2},radius:40,theta:.7,phi:1.1},pivot=[-6,9,7];
for(const angles of [{theta:1.3,phi:1.7},{theta:-2,phi:Math.PI*2+.2},{theta:3,phi:-.4}]){
 const next=orbitCameraState(start,angles,pivot),a=pose(start),b=pose(next),p=new THREE.Vector3(...pivot);
 assert.ok(Math.abs(a.position.distanceTo(p)-b.position.distanceTo(p))<1e-7);near(p.clone().project(a).toArray(),p.clone().project(b).toArray());assert.equal(next.radius,start.radius);
 const restored=orbitCameraState(next,{theta:start.theta,phi:start.phi},pivot);near(Object.values(restored.target),Object.values(start.target));
}
assert.deepEqual(orbitCameraState(start,{theta:2,phi:1},null).target,start.target);
console.log('Orbit pivot: nearest visible surfaces, tessellation independence, area hits around a hole, miss handling, fixed pivot projection/distance and pole-crossing rotations passed');
