import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {sceneColors} from './scenePalette.mjs';
import {applySetupDisplayFrame} from './setupWorldFrame.mjs';

const point=(frame,rapid)=>rapid?frame.position:frame.target;
export function rapidSegmentDistances(frames){
  const distances=new Map();let length=0;
  for(let i=0;i<frames.length-1;i++){
    const a=frames[i],b=frames[i+1];
    if(a.role!=='Rapid'||b.breakBefore){length=0;continue;}
    const end=length+a.position.distanceTo(b.position);distances.set(i,[length,end]);length=end;
  }
  return distances;
}
const lines=trace=>[trace,trace.rapid,trace.collisionCut,trace.collisionRapid].filter(Boolean);
function disposeLine(track){track.line.removeFromParent();track.line.geometry.dispose();track.line.material.dispose();}
export function disposeSimulationTrace(state) {
  if(state.trace)lines(state.trace).forEach(disposeLine);
  state.trace=null;
}

function createLine(viewport,frames,segments,rapid,color,name,order,distanceMap) {
  const points=segments.flatMap(i=>[...point(frames[i],rapid).toArray(),...point(frames[i+1],rapid).toArray()]);
  const geometry=new THREE.BufferGeometry();
  geometry.setAttribute('position',new THREE.Float32BufferAttribute(points,3).setUsage(THREE.DynamicDrawUsage));
  geometry.setDrawRange(0,0);
  const options={color,depthTest:false,depthWrite:false};
  const line=new THREE.LineSegments(geometry,rapid?new THREE.LineDashedMaterial({...options,dashSize:3,gapSize:2}):new THREE.LineBasicMaterial(options));
  const sourceDistances=rapid?(distanceMap??rapidSegmentDistances(frames)):null;
  if(rapid)geometry.setAttribute('lineDistance',new THREE.Float32BufferAttribute(segments.flatMap(i=>sourceDistances.get(i)),1).setUsage(THREE.DynamicDrawUsage));
  line.name=name;line.renderOrder=order;line.frustumCulled=false;line.raycast=()=>{};
  viewport.scene.add(line);
  return {line,segments,isRapid:rapid,sourceDistances,distances:rapid?geometry.getAttribute('lineDistance').array.slice():null,first:-1,last:-1};
}
const lowerBound=(segments,value)=>{let lo=0,hi=segments.length;while(lo<hi){const mid=(lo+hi)>>>1;if(segments[mid]<value)lo=mid+1;else hi=mid;}return lo;};
function clipLine(track,frames,start,cursor) {
  const {line,segments,isRapid:rapid,distances}=track,attribute=line.geometry.getAttribute('position'),distance=line.geometry.getAttribute('lineDistance');
  const set=(vertex,p,fraction=vertex%2)=>{
    attribute.setXYZ(vertex,p.x,p.y,p.z);attribute.addUpdateRange(vertex*3,3);
    if(distance){const pair=vertex-vertex%2;distance.setX(vertex,distances[pair]+(distances[pair+1]-distances[pair])*fraction);distance.addUpdateRange(vertex,1);}
  };
  for(const index of new Set([track.first,track.last]))if(index>=0){
    const i=segments[index];set(index*2,point(frames[i],rapid));set(index*2+1,point(frames[i+1],rapid));
  }
  const first=lowerBound(segments,Math.floor(start)),end=lowerBound(segments,cursor);
  track.first=track.last=-1;
  if(end>first&&cursor>start){
    const a=segments[first],b=segments[end-1],from=Math.max(0,start-a),to=Math.min(1,cursor-b);
    set(first*2,point(frames[a],rapid).clone().lerp(point(frames[a+1],rapid),from),from);
    set((end-1)*2+1,point(frames[b],rapid).clone().lerp(point(frames[b+1],rapid),to),to);
    track.first=first;track.last=end-1;
  }
  line.geometry.setDrawRange(first*2,track.last<0?0:(end-first)*2);
  attribute.needsUpdate=true;if(distance)distance.needsUpdate=true;
}

// Cuts follow the beam contact points; rapids follow nozzle TCPs. Immutable
// buffers are reused while playback clips only the first and last endpoints.
export function paintSimulationTrace(view,state) {
  const viewport=view.viewport,frames=state.frames;
  if(!viewport||!frames?.length){disposeSimulationTrace(state);return;}
  if(state.trace?.frames!==frames||state.trace?.viewport!==viewport){
    disposeSimulationTrace(state);
    const cuts=[],rapids=[];
    for(let i=0;i<frames.length-1;++i){
      const a=frames[i],b=frames[i+1],rapid=a.role==='Rapid';
      if(b.breakBefore||!rapid&&(!a.laser||a.intentId!==b.intentId))continue;
      const from=point(a,rapid),to=point(b,rapid);
      if(!from||!to||from.distanceToSquared(to)<1e-20)continue;
      (rapid?rapids:cuts).push(i);
    }
    state.trace={...createLine(viewport,frames,cuts,false,sceneColors.simulationCut,'five-simulation-cut-trace',50),frames,viewport,
      rapid:createLine(viewport,frames,rapids,true,sceneColors.simulationRapid,'five-simulation-rapid-trace',50),contacts:new Set(),hitsVersion:0};
    state.trace.rapidLine=state.trace.rapid.line;state.trace.rapidSegments=rapids;
  }
  const trace=state.trace,cursor=Math.max(0,Math.min(frames.length-1,state.cursor));
  const start=state.traceStart=Math.min(state.traceStart??0,cursor);
  if(trace.contactContext!==state.traceContactKey){trace.contacts.clear();trace.hitsVersion++;trace.contactContext=state.traceContactKey;}
  // A discrete pose hit colors its traversed incoming interval for display.
  // It never creates or changes a native continuous collision certificate.
  const contact=state.frameContact,index=Math.ceil(cursor)-1;
  if(contact?.status==='Collision'&&contact.cursor===cursor&&cursor>start
    &&index>=0&&!frames[index+1].breakBefore&&!trace.contacts.has(index)){
    trace.contacts.add(index);trace.hitsVersion++;
  }
  if(trace.report!==state.collisionReport||trace.paintedHitsVersion!==trace.hitsVersion){
    const hits=new Set(trace.contacts);
    for(const segment of state.collisionReport?.validity==='Current'?state.collisionReport.segments??[]:[])if(segment.status==='Collision')hits.add(Number(segment.index));
    for(const [key,source,rapid,name]of[['collisionCut',trace,false,'five-simulation-cut-collisions'],['collisionRapid',trace.rapid,true,'five-simulation-rapid-collisions']]){
      if(trace[key])disposeLine(trace[key]);
      trace[key]=createLine(viewport,frames,source.segments.filter(i=>hits.has(i)),rapid,sceneColors.collision,name,80,source.sourceDistances);
      trace[key].line.userData.source='CollisionReportOrDiscretePlaybackHit';
    }
    trace.report=state.collisionReport;trace.paintedHitsVersion=trace.hitsVersion;trace.cursor=null;
  }
  lines(trace).forEach(track=>applySetupDisplayFrame(view,track.line));
  if(trace.cursor===cursor&&trace.start===start)return;
  lines(trace).forEach(track=>clipLine(track,frames,start,cursor));
  trace.cursor=cursor;trace.start=start;
}

export function clearSimulationTrace(view,state) {
  state.traceStart=state.cursor;
  if(state.trace){state.trace.contacts.clear();state.trace.hitsVersion++;}
  paintSimulationTrace(view,state);
}
