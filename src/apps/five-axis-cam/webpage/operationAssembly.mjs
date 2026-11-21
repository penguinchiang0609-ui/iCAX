import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
export function operationAssemblyMatrix(view,setupId){
 const transform=view.scene?.domain?.setups?.find(s=>s.id===setupId)?.assemblyTransform;
 return transform?new THREE.Matrix4().set(...transform):new THREE.Matrix4();
}
export function operationPlacementMatrix(view,placement){return operationAssemblyMatrix(view,placement?.setupId).multiply(new THREE.Matrix4().set(...placement.transform));}
export function placementMovesAssembly(view,setupId){
 const domain=view.scene?.domain,type=domain?.setups?.find(s=>s.id===setupId)?.machineType?.machineType;
 return ['HeadTable','TableTable'].includes(type)&&!!(domain?.fixtureUnits?.some(f=>f.setupId===setupId)||domain?.fixtures?.some(f=>f.setupId===setupId));
}
