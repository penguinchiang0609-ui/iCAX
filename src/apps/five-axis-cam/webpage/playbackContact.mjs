import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { prepareMeshCollider, prepareBoxCollider, boxCollidesMesh } from '../../../iCAX-UI/SDK/Viewport/discreteMeshCollision.mjs';

// Check the displayed interpolated pose locally, without requesting path
// segments, predicting the next frame or waiting for native CAD calculations.
export function checkPlaybackContact(view,s) {
  const domain=view.scene?.domain,setup=domain?.setups?.find(v=>v.id===domain.activeSetupId);
  const result={status:'Clear',cursor:s.cursor,index:Math.floor(s.cursor),fraction:s.cursor-Math.floor(s.cursor),mode:'DiscreteBoxes'};
  const unchecked=reason=>({...result,status:'Unchecked',reason});
  if(!setup)return unchecked('当前装夹尚未就绪');
  const head=setup.headModel,model=head?.collisionModel;
  const record=s.collisionReport?.validity==='Current'?s.collisionReport:s.nativeHeadMotion?.sourceKey===s.sourceKey?s.nativeHeadMotion?.report:null;
  if(head?.assembly&&head.collisionSource!=='MachineInstance'){
    const checked=record?.segments?.find(row=>Number(row.index)===Math.min(result.index,Math.max(0,s.frames.length-2)));
    return checked?{...result,...checked,mode:'NativeKeyposeInterpolation'}:unchecked('当前关键点区间尚未完成检测');
  }
  if(model?.status==='MachineBindingRequired')return unchecked('刀头结构预览；绑定机床后检查完整机构。');
  if(head?.assembly)return unchecked('刀头关节姿态预览；完整机构碰撞请在机床仿真中检查。');
  if(!s.group||head?.kind==='Imported'&&!head.calibrated)return unchecked('切割头模型尚未就绪');
  if(model?.status!=='Ready'||model.frame!=='CalibratedTCP'||!Array.isArray(model.boxes)||!model.boxes.length)return unchecked('切割头尚未配置有效碰撞盒');
  s.group.updateWorldMatrix(true,false);
  const obstacles=[...(domain.placements??[]).filter(p=>p.setupId===setup.id&&p.enabled!==false),...(domain.fixtures??[]).filter(f=>f.setupId===setup.id&&f.enabled!==false&&f.collisionEnabled!==false)];
  let missing=false;
  const started=performance.now();
  try{
    const boxes=model.boxes.map(prepareBoxCollider),clearance=Number(setup.rapidSettings.collisionThresholdMm);
    if(!Number.isFinite(clearance)||clearance<0)return unchecked('碰撞阈值无效。');
    const radius=Number(setup.rapidSettings.envelopeRadiusMm??0),length=Number(setup.rapidSettings.envelopeLengthMm??0);
    if(!Number.isFinite(radius)||!Number.isFinite(length)||radius<0||length<0)return unchecked('附加碰撞包络无效。');
    if(radius>0&&length>0)boxes.push(prepareBoxCollider({center:[0,0,length/2],halfExtents:[radius,radius,length/2],axes:[[1,0,0],[0,1,0],[0,0,1]]}));
    for(const obstacle of obstacles){
      const object=view.viewport?.sceneObjects.get(obstacle.id);
      if(!object?.isMesh){missing=true;continue;}
      object.updateWorldMatrix(true,false);
      const fixed=prepareMeshCollider(object.geometry),relative=new THREE.Matrix4().multiplyMatrices(object.matrixWorld.clone().invert(),s.group.matrixWorld);
      for(const box of boxes){
        if(boxCollidesMesh(fixed,box,relative,clearance)){result.status='Collision';result.obstacleId=obstacle.id;break;}
      }
      if(result.status==='Collision')break;
    }
    result.elapsedMs=performance.now()-started;
    if(missing&&result.status==='Clear')return unchecked('工件或夹具模型尚未就绪');
    return result;
  }catch(error){return unchecked(error.message);}
}
