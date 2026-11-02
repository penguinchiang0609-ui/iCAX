import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {frameInReference} from './workpieceFrame.mjs';

// Coordinate axes are displayed scene references. Convert the visible handles
// back into source-workpiece coordinates, including a pending frame preview.
export function alignmentCoordinateTargets(view,d){
  const viewport=view.viewport,object=viewport?.sceneObjects.get(d.placementId);if(!object?.visible)return [];
  object.updateWorldMatrix(true,false);const inverse=object.matrixWorld.clone().invert(),targets=[];
  if(!object.geometry.boundingBox)object.geometry.computeBoundingBox();
  const length=Math.max(1,object.geometry.boundingBox.getSize(new THREE.Vector3()).length()*.15);
  const add=(frame,name,worldOrigin,worldEnds)=>{
    const point=worldOrigin.clone().applyMatrix4(inverse).toArray();
    targets.push({mode:'datum-point',coordinateFrame:frame,featureId:`coordinate:${frame}:origin`,label:name+'原点',point,direction:[0,0,0],worldPoint:worldOrigin,
      query:{mode:'datum-point',frame:'Workpiece',coordinateFrame:frame}});
    for(const [axis,end] of Object.entries(worldEnds)){
      const localEnd=end.clone().applyMatrix4(inverse),direction=localEnd.clone().sub(new THREE.Vector3(...point)).normalize().toArray();
      targets.push({mode:'direction',coordinateFrame:frame,featureId:`coordinate:${frame}:${axis}`,label:`${name} ${axis.toUpperCase()} 轴`,point,direction,lineEnds:[point,localEnd.toArray()],worldPoint:worldOrigin,worldEnd:end,
        query:{mode:'direction',frame:'Workpiece',coordinateFrame:frame,axis}});
    }
  };
  const frame=frameInReference(d.frame,d.savedFrame),origin=new THREE.Vector3(...frame.origin).applyMatrix4(object.matrixWorld),ends={};
  for(const axis of ['x','y','z'])ends[axis]=new THREE.Vector3(...frame.origin).addScaledVector(new THREE.Vector3(...frame[axis]),length).applyMatrix4(object.matrixWorld);
  add('current','当前坐标系',origin,ends);
  const world=viewport.scene.getObjectByName('iCAX World XYZ');
  if(world?.visible&&world.parent){world.updateWorldMatrix(true,false);const origin=world.localToWorld(new THREE.Vector3()),length=viewport.options.worldAxisLength??120,ends={};
    for(const [i,axis] of ['x','y','z'].entries())ends[axis]=world.localToWorld(new THREE.Vector3().setComponent(i,length));
    add('world','世界坐标系',origin,ends);
  }
  return targets;
}
export function alignmentCoordinateCandidate(view,d,event){
  if(!Number.isFinite(event?.clientX))return null;
  const viewport=view.viewport,rect=viewport.renderer.domElement.getBoundingClientRect(),x=event.clientX,y=event.clientY;
  if(x<rect.left||x>rect.right||y<rect.top||y>rect.bottom)return null;
  viewport.camera.updateMatrixWorld(true);
  const project=p=>{const q=p.clone().project(viewport.camera);return {x:rect.left+(q.x+1)*rect.width/2,y:rect.top+(1-q.y)*rect.height/2,z:q.z};};
  const choices=[];
  for(const target of alignmentCoordinateTargets(view,d)){
    if((d.selecting==='origin')!==(target.mode==='datum-point'))continue;
    const a=project(target.worldPoint);if(a.z< -1||a.z>1)continue;
    let gap;
    if(target.mode==='datum-point')gap=Math.hypot(a.x-x,a.y-y);
    else{const b=project(target.worldEnd),dx=b.x-a.x,dy=b.y-a.y,n=dx*dx+dy*dy;if(n<64||b.z< -1||b.z>1)continue;
      const t=Math.max(.18,Math.min(1,((x-a.x)*dx+(y-a.y)*dy)/n));gap=Math.hypot(a.x+t*dx-x,a.y+t*dy-y);}
    if(gap<=9)choices.push({target,gap});
  }
  choices.sort((a,b)=>a.gap-b.gap);
  const target=choices[0]?.target;if(!target)return null;
  const {worldPoint,worldEnd,...result}=target;
  return {objectId:d.placementId,result,coordinateFrame:true,resolve:()=>Promise.resolve(result)};
}
