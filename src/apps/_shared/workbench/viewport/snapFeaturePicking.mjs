import * as THREE from '../../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {nearbySurfaceHit} from '../../../../iCAX-UI/SDK/Viewport/surfaceProximity.mjs';
const projections=new WeakMap();

// The catalog is produced by a BRep service. This layer only projects and ranks
// existing features; it cannot invent CAD centres, normals or topology.
export function snapFeatureCandidate({viewport,objectId,catalog,mode='auto',allowedModes=null,radius=12},hit,event){
  const object=viewport.sceneObjects.get(objectId);if(!object?.visible||!catalog||!Number.isFinite(event?.clientX))return null;
  const rect=viewport.renderer.domElement.getBoundingClientRect();object.updateMatrixWorld(true);
  if(hit&&hit.object!==object)return null;
  hit??=nearbySurfaceHit(object,viewport.camera,rect,event.clientX,event.clientY,radius,[...viewport.sceneObjects.values()]);
  const face=catalog.faces.find(f=>hit&&hit.faceIndex>=+f.first&&hit.faceIndex<+f.first+ +f.count)?.faceId;
  const matrix=new THREE.Matrix4().multiplyMatrices(viewport.camera.projectionMatrix,viewport.camera.matrixWorldInverse).multiply(object.matrixWorld);
  const project=point=>{const p=new THREE.Vector3(...point).applyMatrix4(matrix);return {x:rect.left+(p.x+1)*rect.width/2,y:rect.top+(1-p.y)*rect.height/2,z:p.z};};
  const projectionKey=JSON.stringify([matrix.elements,rect.left,rect.top,rect.width,rect.height]);
  let projected=projections.get(catalog);
  if(projected?.key!==projectionKey){projected={key:projectionKey,points:new Map(),edges:new Map()};
    for(const f of catalog.features){projected.points.set(f.featureId,project(f.point));if(f.edgeSamples.length)projected.edges.set(f.featureId,f.edgeSamples.map(project));}
    projections.set(catalog,projected);
  }
  const gap=p=>p.z< -1||p.z>1?Infinity:Math.hypot(p.x-event.clientX,p.y-event.clientY);
  const edgeGaps=new Map(),ownEdgeGaps=new Map(),edgeSamples=new Map();
  for(const f of catalog.features)if(f.edgeSamples.length){
    const points=projected.edges.get(f.featureId);let distance=Infinity;
    for(let i=1;i<points.length;++i){const a=points[i-1],b=points[i];if(a.z< -1||a.z>1||b.z< -1||b.z>1)continue;
      const dx=b.x-a.x,dy=b.y-a.y,t=Math.max(0,Math.min(1,((event.clientX-a.x)*dx+(event.clientY-a.y)*dy)/(dx*dx+dy*dy||1)));
      distance=Math.min(distance,Math.hypot(a.x+t*dx-event.clientX,a.y+t*dy-event.clientY));}
    ownEdgeGaps.set(f.featureId,distance);if(f.edgeId!=='0')edgeGaps.set(f.edgeId,distance);edgeSamples.set(f.featureId,f.edgeSamples);
  }
  const priorities={endpoint:0,line:1,center:2,'sphere-center':3,'datum-point':4,plane:5,axis:6,direction:6,midpoint:7,edge:8};
  const choices=[];
  for(const f of catalog.features){
    if(allowedModes&&!allowedModes.includes(f.mode))continue;
    if(mode!=='auto'&&mode!==f.mode||mode==='auto'&&['midpoint','edge'].includes(f.mode))continue;
    const onFace=!!face&&f.faceIds.includes(face),pointGap=gap(projected.points.get(f.featureId));let distance=pointGap;
    const supportGap=Math.min(edgeGaps.get(f.edgeId)??Infinity,...f.edgeIds.map(id=>edgeGaps.get(id)??Infinity));
    if(['line','edge'].includes(f.mode))distance=ownEdgeGaps.get(f.featureId)??supportGap;
    if(f.mode==='center')distance=Math.min(distance,edgeGaps.get(f.edgeId)??Infinity);
    if(f.mode==='plane')distance=f.edgeIds.length?supportGap:onFace?0:Infinity;
    if(['axis','direction'].includes(f.mode))distance=f.edgeIds.length?supportGap:onFace?0:Infinity;
    if(f.mode==='datum-point')distance=Math.min(pointGap,supportGap);
    if(f.mode==='sphere-center'&&onFace)distance=0;
    if(distance>radius)continue;
    // Ordinary points/edges behind another face are excluded. Explicit centre
    // anchors can be away from the surface that established them.
    if(face&&!onFace&&!f.boxKind&& !['center','sphere-center','datum-point'].includes(f.mode))continue;
    // Prefer the explicit plane of a circular edge near that edge; away from
    // boundaries, the face itself remains the plane candidate.
    const planePriority=f.mode==='plane'?(f.featureId.startsWith('curve-plane:')?0:f.featureId.startsWith('wire-plane:')?.5:1)*radius:0;
    choices.push({f,distance,score:(mode==='auto'?priorities[f.mode]*radius:0)+distance+planePriority});
  }
  choices.sort((a,b)=>a.score-b.score||a.f.featureId.localeCompare(b.f.featureId));
  if(!choices.length)return null;
  const f=choices[0].f,query={mode:f.mode,frame:'Workpiece',featureId:f.featureId},samples=edgeSamples.get(f.featureId)||[];
  const result={...f,query,faceId:onFaceId(f,face),distanceMm:0,geometryRef:catalog.geometryRef,
    triangleRanges:catalog.faces.filter(r=>f.faceIds.includes(r.faceId)),lineEnds:samples.length?[samples[0],samples.at(-1)]:[]};
  return {objectId,request:{query},resolve:()=>Promise.resolve(result),result};
}
const onFaceId=(f,face)=>f.faceIds.includes(face)?face:f.faceIds[0]||'0';

export function loadSnapFeatureCatalog(cache,key,load){
  let entry=cache.get(key);if(entry)return entry;
  entry={data:null,error:null};cache.set(key,entry);
  entry.promise=Promise.resolve().then(load).then(data=>{entry.data=data;return data;}).catch(error=>{entry.error=error;throw error;});
  return entry;
}
