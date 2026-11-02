import {geometrySnapCandidate} from '../../_shared/workbench/viewport/geometrySnapping.mjs';
import {snapFeatureCandidate,loadSnapFeatureCatalog} from '../../_shared/workbench/viewport/snapFeaturePicking.mjs';
import {SnapFeatureScene,planeNormal} from '../../_shared/workbench/viewport/snapFeatureScene.mjs';

function parseCatalog(buffer){
  const v=new DataView(buffer),bad=()=>{throw Error('几何吸附特征资源不完整，请重试。');};
  const range=(p,n)=>{if(p<0||n<0||p+n>v.byteLength)bad();};
  range(0,8);if(String.fromCharCode(...new Uint8Array(buffer,4,4))!=='ICSC')bad();
  const root=v.getUint32(0,true);range(root,4);const vt=root-v.getInt32(root,true);range(vt,8);
  if(v.getUint16(vt,true)<8)bad();
  const versionOffset=v.getUint16(vt+4,true),jsonOffset=v.getUint16(vt+6,true);
  if(!versionOffset||!jsonOffset)bad();range(root+versionOffset,4);if(v.getUint32(root+versionOffset,true)!==1)bad();
  const field=root+jsonOffset;range(field,4);const start=field+v.getUint32(field,true);range(start,4);
  const n=v.getUint32(start,true);range(start+4,n+1);if(v.getUint8(start+4+n)!==0)bad();
  return JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(new Uint8Array(buffer,start+4,n)));
}
async function readCatalog(proxy,request){
  const descriptor=await proxy.invoke('CamScene.ReadSnapFeatures',{...request,transport:'resource'},{timeoutMs:120000});
  const ref=descriptor.catalogRef;
  if(!ref?.url||!proxy.resources?.get)throw Error('几何吸附特征资源不可用，请重试。');
  const response=await proxy.resources.get(ref.url,{headers:{'ICAX-Resource-Version':String(ref.version)}});
  if(!response.ok||response.headers.get('ICAX-Resource-Version')!==String(ref.version))throw Error('几何吸附特征读取失败或版本已变化，请重试。');
  const data=parseCatalog(await response.arrayBuffer());
  if(!Array.isArray(data.features)||!Array.isArray(data.faces)||data.features.length!==Number(descriptor.featureCount)
    ||JSON.stringify(data.geometryRef)!==JSON.stringify(descriptor.geometryRef))throw Error('几何吸附特征资源不完整，请重试。');
  return data;
}
export function retrySceneSnapping(view,objectId){
  const state=view.sceneSnapping;if(!state||state.pending.has(objectId))return;
  view.snapFeatureCatalogs?.delete(state.keys.get(objectId));state.keys.delete(objectId);state.errors?.delete(objectId);
}
export function sceneSnapStatus(view,objectId){
  const state=view.sceneSnapping;
  if(state?.errors?.has(objectId))return `吸附特征读取失败：${state.errors.get(objectId).message}`;
  if(!state?.catalogs.has(objectId))return '正在读取几何吸附特征，请稍候…';
  return '';
}
export function clearSceneSnapPresentation(view){view.sceneSnapping?.presentation?.layer?.dispose();if(view.sceneSnapping)view.sceneSnapping.presentation=null;}
export function disposeSceneSnapping(view){clearSceneSnapPresentation(view);view.snapFeatureCatalogs?.clear();view.sceneSnapping=null;}
export function presentSceneSnaps(context,view,{objectId,allowedModes,anchor=null,inference=false,candidateDisplay=null}={}){
  const state=view.sceneSnapping;if(!state)return;
  const key=JSON.stringify([objectId,view.viewport.sceneObjects.get(objectId)?.geometry.uuid,allowedModes,anchor?.query,inference,candidateDisplay]);
  if(state.presentation?.key!==key){clearSceneSnapPresentation(view);state.presentation={key,objectId,allowedModes,anchor,inference,candidateDisplay,extras:[]};}
  const p=state.presentation,source=state.catalogs.get(objectId),object=view.viewport.sceneObjects.get(objectId);
  if(!object||!source)return;
  const catalog=source;
  if(!p.layer||p.layer.object!==object||p.layer.catalog!==catalog){p.layer?.dispose();p.layer=new SnapFeatureScene(object,catalog,p);}
  if(anchor&&!p.membersRequested){
    p.membersRequested=true;p.membersPending=true;
    const kinds=allowedModes.includes('direction')?['PlaneU','PlaneV']:allowedModes.includes('datum-point')?['PlaneCenter']:[];
    void Promise.all(kinds.map(kind=>constructSceneSnap(context,view,objectId,kind,[anchor]))).then(extras=>{
      if(view.sceneSnapping!==state||state.presentation!==p||view.disposed||context.isCurrentProject?.()===false)return;
      p.extras=extras;p.layer?.dispose();p.layer=new SnapFeatureScene(object,catalog,p);
    }).catch(error=>{if(state.presentation===p){p.error=error;state.onError?.(error);}}).finally(()=>{p.membersPending=false;});
  }
}

// Scene/resource adapter for the neutral snap service. Any tool can consume
// these catalogs without depending on the alignment editor.
export function syncSceneSnapping(context,view,onError=()=>{}){
  const epoch=view.scene.domain.expected.epoch;
  if(view.sceneSnapping?.epoch!==epoch){clearSceneSnapPresentation(view);view.sceneSnapping={epoch,catalogs:new Map(),pending:new Set(),keys:new Map(),errors:new Map()};}
  const state=view.sceneSnapping;state.onError=onError;view.snapFeatureCatalogs??=new Map();
  for(const [id,mesh] of view.viewport.sceneObjects){
    if(!mesh.visible)continue;
    const key=JSON.stringify([epoch,mesh.geometry.uuid]);
    if(state.keys.get(id)===key&&(state.catalogs.has(id)||state.pending.has(id)||state.errors.has(id)))continue;
    state.keys.set(id,key);state.catalogs.delete(id);state.errors.delete(id);state.pending.add(id);
    const expected=structuredClone(view.scene.domain.expected),setupId=view.scene.domain.activeSetupId;
    const entry=loadSnapFeatureCatalog(view.snapFeatureCatalogs,key,()=>readCatalog(context.sceneProxy,{expected,setupId,objectId:id}));
    const valid=()=>view.sceneSnapping===state&&!view.disposed&&context.isCurrentProject?.()!==false&&state.keys.get(id)===key;
    if(entry.data){state.catalogs.set(id,entry.data);state.pending.delete(id);continue;}
    void entry.promise.then(data=>{if(valid()){state.catalogs.set(id,data);state.pending.delete(id);if(state.presentation)presentSceneSnaps(context,view,state.presentation);context.mount?.dispatchEvent(new CustomEvent('five-snap-state'));}}).catch(error=>{if(valid()){state.pending.delete(id);state.errors.set(id,error);onError(error);context.mount?.dispatchEvent(new CustomEvent('five-snap-state'));}});
  }
  return state;
}

export function sceneSnapCandidate(context,view,{objectId,mode='auto',allowedModes=null},hit,event){
  if(mode!=='point'){
    const state=view.sceneSnapping,p=state?.presentation?.objectId===objectId?state.presentation:null,layer=p?.layer;
    const catalog=state?.catalogs.get(objectId);
    const helper=mode==='auto'?layer?.candidate(view.viewport,event):null;
    const candidate=snapFeatureCandidate({viewport:view.viewport,objectId,mode,allowedModes,catalog},hit,event);
    if(helper&&(!allowedModes||allowedModes.includes(helper.result.mode))){
      // A real planar face and an auxiliary patch use the same surface pick.
      // Point/arrow handles remain precise foreground targets, as drawn.
      if(helper.helperType!=='plane'||!candidate||candidate.result.derived)return helper;
    }
    if(candidate)return candidate;
    if(mode==='auto'&&allowedModes?.includes('direction')){
      const plane=snapFeatureCandidate({viewport:view.viewport,objectId,mode:'plane',catalog},hit,event);
      if(plane){const result=planeNormal(plane.result);return {...plane,result,request:{query:result.query},resolve:()=>Promise.resolve(result)};}
    }
    if(mode!=='auto'||!allowedModes?.includes('point'))return null;
    mode='point';
  }
  if(allowedModes&&!allowedModes.includes('point'))return null;
  const c=geometrySnapCandidate({viewport:view.viewport,objectId,mode,expected:view.scene.domain.expected,setupId:view.scene.domain.activeSetupId},hit,event);
  if(!c)return null;c.objectId=objectId;
  c.resolve=()=>c.promise??=context.sceneProxy.invoke('CamScene.ResolvePickReference',c.request).then(r=>{if(!c.accepts(r))throw Error('CAD_REFERENCE_TOO_FAR');return r;});return c;
}

export async function constructSceneSnap(context,view,objectId,kind,sources){
  const mode=kind==='ThreePointPlane'?'plane':kind==='PlaneCenter'?'datum-point':'direction';
  const query={mode,frame:'Workpiece',construction:{kind,sources:sources.map(r=>r.query)}};
  return context.sceneProxy.invoke('CamScene.ResolvePickReference',{expected:structuredClone(view.scene.domain.expected),setupId:view.scene.domain.activeSetupId,objectId,query});
}
