import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { loadRenderResource } from '../../../iCAX-UI/SDK/Viewport/renderResource.mjs';
import { escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';

const keyOf=ref=>JSON.stringify([ref?.url,String(ref?.version??0)]);
export function workpieceThumbnailPreview(view,workpiece){
  const entry=view.fiveThumbnails?.cache.get(keyOf(workpiece?.previewRef));
  return {image:entry?.image||'',imageKey:keyOf(workpiece?.previewRef),imageLabel:!workpiece?.previewRef?.url?'暂无预览':entry?.failed?'预览不可用':'加载预览…'};
}
export function workpieceThumbnail(view,placement) {
  const workpiece=view.scene.domain.workpieces.find(w=>w.id===placement.workpieceId),ref=workpiece?.previewRef;
  const key=keyOf(ref),entry=view.fiveThumbnails?.cache.get(key),src=entry?.image;
  return `<span class="five-workpiece-thumbnail" data-five-key="thumbnail-${attr(placement.id)}" data-five-thumbnail="${attr(key)}"><img alt="${attr(workpiece?.name||'工件')}缩略图" ${src?`src="${attr(src)}"`:'hidden'}><span ${src?'hidden':''}>${!ref?.url?'暂无预览':entry?.failed?'预览不可用':'加载预览…'}</span></span>`;
}

// Rasterize the complete neutral mesh with depth testing, independently of the
// main viewport's camera, highlights and machining overlays.
function renderMesh(state,mesh) {
  if(mesh.kind!=='mesh'||!mesh.positions?.length)throw Error('No mesh preview');
  const renderer=state.renderer??=new THREE.WebGLRenderer({antialias:true,alpha:false});
  renderer.setSize(192,128,false);renderer.setClearColor(0xeaf1f3,1);
  const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(mesh.positions,3));
  if(mesh.indices?.length)geometry.setIndex(new THREE.BufferAttribute(new Uint32Array(mesh.indices),1));
  if(mesh.normals?.length===mesh.positions.length)geometry.setAttribute('normal',new THREE.Float32BufferAttribute(mesh.normals,3));else geometry.computeVertexNormals();
  const material=new THREE.MeshPhongMaterial({color:0x74959e,side:THREE.DoubleSide,shininess:22});
  try {
    geometry.computeBoundingBox();const box=geometry.boundingBox,center=box.getCenter(new THREE.Vector3()),radius=Math.max(box.getSize(new THREE.Vector3()).length(),.001);
    const scene=new THREE.Scene(),camera=new THREE.OrthographicCamera(-1.5,1.5,1,-1,radius*.001,radius*10);
    camera.up.set(0,0,1);camera.position.copy(center).add(new THREE.Vector3(1,-1,.9).normalize().multiplyScalar(radius*3));camera.lookAt(center);camera.updateMatrixWorld(true);
    const bounds=new THREE.Box3();
    for(const x of [box.min.x,box.max.x])for(const y of [box.min.y,box.max.y])for(const z of [box.min.z,box.max.z])bounds.expandByPoint(new THREE.Vector3(x,y,z).applyMatrix4(camera.matrixWorldInverse));
    const half=Math.max((bounds.max.y-bounds.min.y)/2,(bounds.max.x-bounds.min.x)/3,.001)*1.12;
    camera.left=-half*1.5;camera.right=half*1.5;camera.top=half;camera.bottom=-half;camera.updateProjectionMatrix();
    scene.add(new THREE.Mesh(geometry,material),new THREE.HemisphereLight(0xffffff,0x72848b,2));
    const light=new THREE.DirectionalLight(0xffffff,2.2);light.position.copy(camera.position);scene.add(light);
    renderer.render(scene,camera);return renderer.domElement.toDataURL('image/png');
  }finally{geometry.dispose();material.dispose();}
}

export function hydrateWorkpieceThumbnails(context,view) {
  const state=view.fiveThumbnails??={cache:new Map(),running:false,dead:false};
  state.context=context;
  for(const workpiece of view.scene?.domain?.workpieces||[]){
    const ref=workpiece.previewRef;if(!ref?.url)continue;
    const key=keyOf(ref);if(!state.cache.has(key))state.cache.set(key,{ref});
  }
  const paint=()=>{
    if(state.dead||view.disposed||state.context.isCurrentProject?.()===false)return;
    for(const element of state.context.mount.querySelectorAll('[data-five-thumbnail],[data-ribbon-thumbnail]')){
      const entry=state.cache.get(element.dataset.fiveThumbnail||element.dataset.ribbonThumbnail);if(!entry)continue;
      const image=element.querySelector('img'),label=element.querySelector('span,small');
      if(entry.image){if(image.getAttribute('src')!==entry.image)image.src=entry.image;image.hidden=false;label.hidden=true;}
      else if(entry.failed)label.textContent='预览不可用';
    }
  };
  paint();if(state.running)return;
  state.running=true;
  void (async()=>{
    try {
      for(const [,entry] of state.cache){
        if(state.dead||view.disposed||state.context.isCurrentProject?.()===false)break;
        if(entry.image||entry.failed)continue;
        try {
          const resource=await loadRenderResource(state.context.sceneProxy.resources,entry.ref);
          if(state.dead||view.disposed)break;
          entry.image=renderMesh(state,resource.data);
        }catch{entry.failed=true;}
        paint();await new Promise(resolve=>setTimeout(resolve,0));
        if(!state.dead&&!view.disposed&&state.context.isCurrentProject?.()!==false&&['toolpath','simulation'].includes(view.fiveAxisArea))state.context.actions?.refreshProductRibbon?.();
      }
      const used=new Set((view.scene?.domain?.workpieces||[]).map(w=>keyOf(w.previewRef)));
      for(const key of state.cache.keys())if(!used.has(key))state.cache.delete(key);
    }finally{
      state.running=false;
      state.renderer?.dispose();state.renderer?.forceContextLoss();state.renderer=null;
    }
  })();
}
export function disposeWorkpieceThumbnails(view) {
  const state=view.fiveThumbnails;if(!state)return;
  state.dead=true;state.cache.clear();state.renderer?.dispose();state.renderer?.forceContextLoss();view.fiveThumbnails=null;
}

export function workpieceChooserKey(event) {
  const chooser=event.target.closest?.('[data-five-workpiece-chooser], [data-five-setup-chooser]');if(!chooser)return false;
  const summary=chooser.querySelector('summary'),buttons=[...chooser.querySelectorAll('[data-five-workpiece]:not(:disabled), [data-five-command^="five.setup.activate:"]:not(:disabled)')];
  if(event.key==='Escape'&&chooser.open){event.preventDefault();chooser.open=false;summary.focus({preventScroll:true});return true;}
  if(!['ArrowDown','ArrowUp','Home','End'].includes(event.key)||summary.getAttribute('aria-disabled')==='true')return false;
  event.preventDefault();chooser.open=true;
  const index=buttons.indexOf(event.target),next=event.key==='Home'?0:event.key==='End'?buttons.length-1:event.key==='ArrowDown'?(index+1)%buttons.length:index<0?buttons.length-1:(index+buttons.length-1)%buttons.length;
  buttons[next]?.focus({preventScroll:true});buttons[next]?.scrollIntoView({block:'nearest'});return true;
}
