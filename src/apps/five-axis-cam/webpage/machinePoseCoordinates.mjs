import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';

function payloadMatrix(values){
 if(values?.length!==16||!Array.from(values).every(Number.isFinite))return null;
 return new THREE.Matrix4().fromArray(values);
}

// Render objects are flat siblings. The machine hierarchy defines the parent
// frame, and accepted interaction previews can include nodes with no geometry.
export function machinePoseFrame(view,m,tip,space){
 if(space==='world')return new THREE.Matrix4();
 if(space!=='local')throw new TypeError('Unknown transform space');
 const node=m?.elements?.find(element=>element.entityId===tip);
 if(!node)return null;
 const parentId=node.parentEntityId;
 if(!parentId)return new THREE.Matrix4();
 const viewport=view?.viewport,preview=viewport?.transformPreviews?.get(parentId);
 if(preview)return payloadMatrix(preview.localToWorld);
 const object=viewport?.sceneObjects?.get(parentId);
 if(object){
  object.updateWorldMatrix(true,false);
  return payloadMatrix(object.matrixWorld.elements);
 }
 return payloadMatrix(viewport?.transformPayloads?.get(parentId)?.localToWorld);
}

export function machinePoseValues(worldMatrix,frame=new THREE.Matrix4()){
 const local=frame.clone().invert().multiply(worldMatrix),position=new THREE.Vector3(),rotation=new THREE.Quaternion();
 local.decompose(position,rotation,new THREE.Vector3());
 const euler=new THREE.Euler().setFromQuaternion(rotation,'ZYX');
 return [...position.toArray(),euler.x*180/Math.PI,euler.y*180/Math.PI,euler.z*180/Math.PI].map(String);
}

export function machinePoseTarget(values,frame=new THREE.Matrix4()){
 if(values?.length!==6||values.some(value=>String(value).trim()===''||!Number.isFinite(Number(value))))throw new TypeError('机床目标位姿需要六个有效数值。');
 const numbers=values.map(Number),rotation=new THREE.Quaternion().setFromEuler(new THREE.Euler(...numbers.slice(3).map(value=>value*Math.PI/180),'ZYX'));
 const local=new THREE.Matrix4().compose(new THREE.Vector3(...numbers.slice(0,3)),rotation,new THREE.Vector3(1,1,1));
 return frame.clone().multiply(local);
}
