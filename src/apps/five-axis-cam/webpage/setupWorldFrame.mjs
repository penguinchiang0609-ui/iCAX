import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';

// CAD/CAM work in setup coordinates. Physical support and registration are
// display parents only inside machine simulation.
export function setupWorldFrame(view,setupId){
  if(view.fiveAxisArea!=='simulation')return new THREE.Matrix4();
  const configuration=view.scene?.domain?.setups?.find(s=>s.id===setupId)?.machineSetup;
  if(!configuration?.supportId)return new THREE.Matrix4();
  const viewport=view.viewport,object=viewport?.sceneObjects.get(configuration.supportId);
  const payload=viewport?.transformPreviews?.get(configuration.supportId)??viewport?.transformPayloads?.get(configuration.supportId);
  const support=object?.matrix?.clone()??(payload?.localToWorld?new THREE.Matrix4().fromArray(payload.localToWorld):null);
  if(!support){
    // Workpiece-only views omit the machine. Recover the setup frame from an
    // authoritative child transform; preview matrices may contain live edits.
    const parts=[...(view.scene?.domain?.placements||[]),...(view.scene?.domain?.fixtures||[])];
    for(const part of parts){
      if(part.setupId!==setupId||!part.transform)continue;
      const world=viewport?.transformPayloads?.get(part.id)?.localToWorld;
      if(!world)continue;
      const local=new THREE.Matrix4().set(...part.transform),frame=new THREE.Matrix4().fromArray(world);
      if(!frame.elements.every(Number.isFinite)||!local.elements.every(Number.isFinite)||Math.abs(local.determinant())<1e-12)continue;
      frame.multiply(local.invert());
      if(Math.abs(frame.determinant())>=1e-12)return frame;
    }
  }
  if(!support||!configuration.placement||!support.elements.every(Number.isFinite)||Math.abs(support.determinant())<1e-12)
    throw Error('工作台显示尚未就绪，请稍后重试。');
  return support.multiply(new THREE.Matrix4().set(...configuration.placement));
}

export function setupTransformToWorld(view,setupId,matrix){
  return setupWorldFrame(view,setupId).multiply(matrix);
}
