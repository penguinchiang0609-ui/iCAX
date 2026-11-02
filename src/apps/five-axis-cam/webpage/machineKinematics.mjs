import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
export const machineMatrix=values=>new THREE.Matrix4().set(...values);
export function machineForward(snapshot,positions){
 const world=[];
 for(let i=0;i<snapshot.nodes.length;i++){
  const n=snapshot.nodes[i],axis=new THREE.Vector3(...n.axis).normalize(),q=Number(positions[i]);
  const joint=n.type==='prismatic'?new THREE.Matrix4().makeTranslation(...axis.multiplyScalar(q).toArray()):n.type==='fixed'?new THREE.Matrix4():new THREE.Matrix4().makeRotationAxis(axis,q);
  const transform=machineMatrix(n.origin).multiply(joint);if(n.parent>=0)transform.premultiply(world[n.parent]);world.push(transform);
 }
 return world;
}
