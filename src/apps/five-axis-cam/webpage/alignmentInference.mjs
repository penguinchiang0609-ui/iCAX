import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {planeNormal} from '../../_shared/workbench/viewport/snapFeatureScene.mjs';
import {clipScreenSegment} from '../../_shared/workbench/viewport/screenSegment.mjs';

const caches=new WeakMap(),pointModes=new Set(['endpoint','midpoint','center','sphere-center','datum-point']);
const vector=p=>new THREE.Vector3(...p);
const identity=f=>JSON.stringify(f.query||{mode:f.mode,frame:'Workpiece',featureId:f.featureId});
const segment=(a,b,x,y)=>{
  if(a.z< -1||a.z>1||b.z< -1||b.z>1)return {gap:Infinity,z:Infinity};
  const dx=b.x-a.x,dy=b.y-a.y,t=Math.max(0,Math.min(1,((x-a.x)*dx+(y-a.y)*dy)/(dx*dx+dy*dy||1)));
  return {gap:Math.hypot(a.x+t*dx-x,a.y+t*dy-y),z:a.z+t*(b.z-a.z)};
};

// Rank only native features. Screen proximity, visibility and view angle
// express intent; property values are then validated by the native frame commit.
export function inferAlignmentCandidate(view,d,hit,event){
  const vp=view.viewport,object=vp?.sceneObjects.get(d.placementId),catalog=view.sceneSnapping?.catalogs.get(d.placementId);
  if(!object?.visible||!catalog||!Number.isFinite(event?.clientX))return null;
  // Explicit native-derived handles are drawn in the foreground. A fixture
  // seen through a hole must not block the visible plane-centre/U/V target.
  const helper=view.sceneSnapping?.presentation?.layer?.candidate(vp,event),construction=helper?.result.query?.construction?.kind;
  if((d.selecting==='origin'&&helper?.helperType==='point'&&construction==='PlaneCenter')
    ||(d.selecting!=='origin'&&helper?.helperType==='direction'&&['PlaneU','PlaneV'].includes(construction)))
    return {...helper,result:{...helper.result,inferenceReason:construction==='PlaneCenter'?'已选基准面的中心':'已选基准面的面内方向'}};
  object.updateWorldMatrix(true,false);vp.camera.updateMatrixWorld(true);
  const rect=vp.renderer.domElement.getBoundingClientRect(),x=event.clientX,y=event.clientY;
  if(x<rect.left||x>rect.right||y<rect.top||y>rect.bottom)return null;
  const matrix=new THREE.Matrix4().multiplyMatrices(vp.camera.projectionMatrix,vp.camera.matrixWorldInverse).multiply(object.matrixWorld);
  const frame=JSON.stringify([object.geometry.uuid,matrix.elements,rect.left,rect.top,rect.width,rect.height]);
  const project=p=>{const q=vector(p).applyMatrix4(matrix);return {x:rect.left+(q.x+1)*rect.width/2,y:rect.top+(1-q.y)*rect.height/2,z:q.z};};
  let cache=caches.get(catalog);
  if(cache?.frame!==frame){
    if(!object.geometry.boundingBox)object.geometry.computeBoundingBox();
    cache={frame,size:Math.max(1,object.geometry.boundingBox.getSize(new THREE.Vector3()).length()),points:new Map(),edges:[]};
    for(const f of catalog.features){
      cache.points.set(f.featureId,project(f.point));
      if(f.edgeSamples?.length){
        const segments=[];
        if(f.mode==='line'){
          const clips=f.edgeSamples.map(p=>new THREE.Vector4(...p,1).applyMatrix4(matrix));
          for(let i=1;i<clips.length;++i){const clipped=clipScreenSegment(clips[i-1],clips[i],rect);if(clipped)segments.push(clipped);}
        }else{const points=f.edgeSamples.map(project);for(let i=1;i<points.length;++i)segments.push([points[i-1],points[i]]);}
        cache.edges.push({f,segments});
      }
    }
    caches.set(catalog,cache);
  }
  const raycaster=new THREE.Raycaster();
  raycaster.setFromCamera({x:(x-rect.left)/rect.width*2-1,y:1-(y-rect.top)/rect.height*2},vp.camera);
  hit??=raycaster.intersectObjects([...vp.sceneObjects.values()].filter(o=>o.visible),false)[0];
  if(hit&&hit.object!==object)return null;
  const face=catalog.faces.find(f=>hit&&hit.faceIndex>=+f.first&&hit.faceIndex<+f.first+ +f.count)?.faceId;
  const hitZ=hit?hit.point.clone().project(vp.camera).z:Infinity;
  // A small depth allowance accommodates tessellation error and a twelve-pixel
  // neighbourhood, without making far-side edges selectable through the part.
  const eyeDistance=hit?hit.distance:object.getWorldPosition(new THREE.Vector3()).distanceTo(vp.camera.position);
  const worldPerPixel=vp.camera.isPerspectiveCamera?2*eyeDistance*Math.tan(THREE.MathUtils.degToRad(vp.camera.getEffectiveFOV()/2))/rect.height:(vp.camera.top-vp.camera.bottom)/vp.camera.zoom/rect.height;
  const depthAllowance=hit?Math.abs(hit.point.clone().addScaledVector(raycaster.ray.direction,worldPerPixel*2).project(vp.camera).z-hitZ):0;
  const visible=p=>p.z>=-1&&p.z<=1&&p.z<=hitZ+depthAllowance;
  const edges=new Map();
  for(const {f,segments} of cache.edges){
    let best={gap:Infinity,z:Infinity};
    for(const [a,b] of segments){const s=segment(a,b,x,y);if(s.gap<best.gap&&visible(s))best=s;}
    if(f.edgeId&&f.edgeId!=='0')edges.set(f.edgeId,best);
    edges.set(f.featureId,best);
  }
  const localRay=raycaster.ray.clone().applyMatrix4(object.matrixWorld.clone().invert());
  const candidates=[],extras=view.sceneSnapping?.presentation?.extras||[];
  const features=[...extras,...catalog.features];
  const add=(f,tier,score,reason,span=0)=>{
    const query=f.query||{mode:f.mode,frame:'Workpiece',featureId:f.featureId};
    const result={...f,label:f.label||({line:'直边',axis:'特征轴',direction:'方向',endpoint:'端点',midpoint:'中点',center:'圆心','sphere-center':'球心','datum-point':'特征中心'})[f.mode],query,geometryRef:catalog.geometryRef,inferenceReason:reason,
      triangleRanges:f.triangleRanges||catalog.faces.filter(r=>f.faceIds?.includes(r.faceId)),
      lineEnds:f.lineEnds||(f.edgeSamples?.length?[f.edgeSamples[0],f.edgeSamples.at(-1)]:[])};
    candidates.push({result,tier,score,span,key:identity(result)});
  };
  const planarHit=f=>{
    const n=vector(f.direction).normalize(),center=vector(f.point);
    const p=localRay.intersectPlane(new THREE.Plane().setFromNormalAndCoplanarPoint(n,center),new THREE.Vector3());
    if(!p)return null;
    if(f.planeCorners?.length===4){
      const [a,b,c,e]=f.planeCorners.map(vector);
      if(!new THREE.Triangle(a,b,c).containsPoint(p)&&!new THREE.Triangle(a,c,e).containsPoint(p))return null;
    }else if(p.distanceTo(center)>Math.max(f.radius||0,1e-8))return null;
    return p;
  };
  const stage=d.selecting==='origin'?'origin':'axis';
  for(const f of features){
    const p=cache.points.get(f.featureId)||project(f.point);
    if((p.z< -1||p.z>1)&&!(stage==='axis'&&f.mode==='line'))continue;
    const gap=Math.hypot(p.x-x,p.y-y),onFace=!!face&&f.faceIds?.includes(face);
    const support=Math.min(edges.get(f.edgeId)?.gap??Infinity,...(f.edgeIds||[]).map(id=>edges.get(id)?.gap??Infinity));
    if(stage==='axis'){
      if(f.mode==='plane'&&onFace&&f.featureId.startsWith('plane:'))add(planeNormal(f),2,0,'鼠标下平面的法向');
      if(f.mode==='plane'&&f.boxKind==='OBB'){
        const facing=-vector(f.direction).transformDirection(object.matrixWorld).dot(raycaster.ray.direction);
        if(facing>.08&&planarHit(f))add(planeNormal(f),3,(1-facing)*8,'按当前视角推断的包围面方向');
      }
      if(!['line','axis','direction'].includes(f.mode))continue;
      if(f.mode==='line'){
        const edge=edges.get(f.featureId);if(edge?.gap<=12){const samples=f.edgeSamples,span=samples?.length?vector(samples[0]).distanceTo(vector(samples.at(-1))):0;add(f,f.boxKind?2:0,edge.gap,'附近边的方向',span);}
      }else{
        const size=cache.size,length=Math.max(size*.1,Math.min(f.radius||0,size*.25));
        const end=project(vector(f.point).addScaledVector(vector(f.direction).normalize(),length).toArray());
        const s=segment(p,end,x,y),projectedLength=Math.hypot(end.x-p.x,end.y-p.y);
        const basis=['PlaneU','PlaneV'].includes(f.query?.construction?.kind);
        if(projectedLength>=8&&s.gap<=12&&(basis||onFace||visible(s)))add(f,basis?0:1,s.gap,basis?'已选基准面的面内方向':'附近特征轴的方向');
        else if(f.mode==='axis'&&onFace)add(f,2,4,'鼠标下圆柱面的轴向');
      }
    }else if(stage==='origin'&&pointModes.has(f.mode)){
      const basis=f.query?.construction?.kind==='PlaneCenter';
      const boxCenter=f.featureId?.endsWith(':center')&&f.boxKind;
      if(gap<=12&&(basis||boxCenter||onFace||visible(p)))add(f,f.boxKind?2:0,gap,basis?'已选基准面的中心':f.label||'附近几何特征点');
      else if(!f.boxKind&&['center','datum-point'].includes(f.mode)&&support<=8)add(f,1,support+4,'附近圆边或轮廓的中心');
    }
  }
  candidates.sort((a,b)=>a.tier-b.tier||a.score-b.score||a.key.localeCompare(b.key));
  const nearest=candidates[0];
  // Prefer a longer straight edge only inside the nearest quarter-pixel band;
  // a visibly closer short edge still wins and remains independently pickable.
  let best=nearest&&candidates.filter(c=>c.tier===nearest.tier&&c.score<=nearest.score+.25).sort((a,b)=>b.span-a.span||a.score-b.score||a.key.localeCompare(b.key))[0];
  const previous=d.inference;
  const scope=JSON.stringify([frame,stage,d.selecting]);
  if(best&&previous?.scope===scope&&Math.hypot(previous.x-x,previous.y-y)<=5){
    const retained=candidates.find(c=>c.key===previous.key);
    if(retained&&retained.tier===best.tier&&retained.score<=best.score+2)best=retained;
  }
  d.inference=best?{scope,x,y,key:best.key}:null;
  if(!best)return null;
  const result=best.result;
  return {objectId:d.placementId,result,request:{query:result.query},resolve:()=>Promise.resolve(result)};
}
