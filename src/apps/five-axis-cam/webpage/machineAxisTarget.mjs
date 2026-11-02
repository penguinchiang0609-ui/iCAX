import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';

// The caller captures the chosen axis in world space once for this edit.
export function machineAxisTarget(baseWorld,direction,mode,value){
 if(!baseWorld?.isMatrix4||!baseWorld.elements.every(Number.isFinite))throw new TypeError('部件位姿无效。');
 if(!['translate','rotate'].includes(mode))throw new TypeError('不支持的变换类型。');
 if(!['number','string'].includes(typeof value)||String(value).trim()===''||!Number.isFinite(Number(value)))throw new TypeError('请输入有效的位移或角度。');
 if(direction?.length!==3||!Array.from(direction).every(Number.isFinite))throw new TypeError('运动轴方向无效。');
 const scale=Math.max(...Array.from(direction,Math.abs));if(!(scale>0))throw new TypeError('运动轴方向无效。');
 const axis=new THREE.Vector3(direction[0]/scale,direction[1]/scale,direction[2]/scale).normalize(),amount=Number(value),target=baseWorld.clone(),position=new THREE.Vector3().setFromMatrixPosition(baseWorld);
 if(mode==='translate')target.setPosition(position.addScaledVector(axis,amount));
 else{
  target.premultiply(new THREE.Matrix4().makeRotationAxis(axis,(amount%360)*Math.PI/180));
  target.setPosition(position);
 }
 if(!target.elements.every(Number.isFinite))throw new TypeError('数值超出范围。');
 return target;
}

// Report motion actually accepted by the machine in the same frozen axis frame.
export function machineAxisValue(baseWorld,direction,mode,actualWorld,reference=0){
 const axis=new THREE.Vector3(...direction).normalize();
 if(mode==='translate')return new THREE.Vector3().setFromMatrixPosition(actualWorld).sub(new THREE.Vector3().setFromMatrixPosition(baseWorld)).dot(axis);
 const base=new THREE.Quaternion().setFromRotationMatrix(baseWorld),actual=new THREE.Quaternion().setFromRotationMatrix(actualWorld),delta=actual.multiply(base.invert()).normalize();
 const projected=delta.x*axis.x+delta.y*axis.y+delta.z*axis.z;
 if(Math.hypot(projected,delta.w)<1e-12)return 0;
 const degrees=2*Math.atan2(projected,delta.w)*180/Math.PI;
 return degrees+360*Math.round((Number(reference)-degrees)/360);
}
