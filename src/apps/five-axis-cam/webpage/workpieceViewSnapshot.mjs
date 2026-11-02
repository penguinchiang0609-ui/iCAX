import {machineViewportFrame} from './machinePlaybackContext.mjs';
// Keep the native machine hierarchy intact. CAD/CAM render their own setup
// transforms, projected with distinct aliases under the shared View contract.
export function workpieceViewSnapshot(_context,view,areaId,snapshot){
  const frame=machineViewportFrame(view);
  if(view.fiveViewportFrame!==frame){
    const cameras=view.fiveFrameCameras??={};
    if(view.fiveViewportFrame&&view.viewport?.getCameraState)cameras[view.fiveViewportFrame]=view.viewport.getCameraState();
    view.fiveViewportFrame=frame;view.fiveFrameCameraPending={frame,camera:cameras[frame]};
  }
  if(frame!=='setup')return snapshot;
  return {...snapshot,rows:(snapshot.rows??[]).map(row=>{
    const data=row.data??{},matrix=data.setupPlacementTransform??data.setupFixtureTransform;
    if(!matrix)return row;
    if(!Array.isArray(matrix)||matrix.length!==16||!matrix.every(Number.isFinite))
      throw Error('装夹显示矩阵无效。');
    const {setupPlacementTransform,setupFixtureTransform,...renderData}=data;
    return {...row,data:{...renderData,localToWorldMatrix:matrix}};
  })};
}
export function workpieceViewSnapshotApplied(_context,view,areaId){
  const frame=machineViewportFrame(view),pending=view.fiveFrameCameraPending;
  if(!pending||pending.frame!==frame||view.activeAreaId!==areaId)return;
  if(pending.camera)view.viewport.setCameraState(pending.camera);
  else if(!view.viewport.fitViewToViewport())return;
  view.fiveFrameCameraPending=null;
}
