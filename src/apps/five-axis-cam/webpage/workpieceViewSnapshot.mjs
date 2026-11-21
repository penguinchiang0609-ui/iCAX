import {machineViewportFrame} from './machinePlaybackContext.mjs';
import {operationAssemblyMatrix} from './operationAssembly.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
const cameraFrame=(frame,areaId)=>frame==='setup'&&areaId==='workpiece'?'workpiece':frame;
// Keep the native machine hierarchy intact. CAD/CAM render their own setup
// transforms, projected with distinct aliases under the shared View contract.
export function workpieceViewSnapshot(_context,view,areaId,snapshot){
  const frame=machineViewportFrame(view);
  const cameraKey=cameraFrame(frame,areaId);
  if(view.fiveViewportFrame!==cameraKey){
    const cameras=view.fiveFrameCameras??={};
    if(view.fiveViewportFrame&&view.viewport?.getCameraState)cameras[view.fiveViewportFrame]=view.viewport.getCameraState();
    view.fiveViewportFrame=cameraKey;view.fiveFrameCameraPending={frame:cameraKey,camera:cameras[cameraKey]};
  }
  if(frame!=='setup')return snapshot;
  return {...snapshot,rows:(snapshot.rows??[]).map(row=>{
    const data=row.data??{},matrix=data.setupPlacementTransform??data.setupFixtureTransform;
    if(!matrix)return row;
    if(!Array.isArray(matrix)||matrix.length!==16||!matrix.every(Number.isFinite))
      throw Error('装夹显示矩阵无效。');
    const {setupPlacementTransform,setupFixtureTransform,...renderData}=data;
    const part=[...(view.scene?.domain?.placements||[]),...(view.scene?.domain?.fixtures||[])].find(p=>p.id===row.entityId);
    const effective=operationAssemblyMatrix(view,part?.setupId).multiply(new THREE.Matrix4().set(...matrix)).transpose().toArray();
    return {...row,data:{...renderData,localToWorldMatrix:effective}};
  })};
}
export function workpieceViewSnapshotApplied(_context,view,areaId){
  const frame=cameraFrame(machineViewportFrame(view),areaId),pending=view.fiveFrameCameraPending;
  if(!pending||pending.frame!==frame||view.activeAreaId!==areaId)return;
  if(pending.camera)view.viewport.setCameraState(pending.camera);
  else if(!view.viewport.fitViewToViewport())return;
  view.fiveFrameCameraPending=null;
}
