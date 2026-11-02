import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';

export function focusShortcutAllowed(event,view,mount){
  return !event.defaultPrevented&&!event.isComposing&&!event.repeat&&!event.ctrlKey&&!event.metaKey&&!event.altKey&&!event.shiftKey&&event.key?.toLowerCase()==='f'
    &&!view.disposed&&!view.pending&&!view.fixtureRequest&&mount.contains(event.target)
    &&!event.target.closest?.('input,textarea,select,[contenteditable]:not([contenteditable="false"]),[role="textbox"]');
}

// Frame the target in the current camera basis. Both projections use radius
// to define the visible height; only perspective needs depth added to the fit.
export function focusWorldBounds(view,box){
  const vp=view.viewport;if(!vp||box.isEmpty())return false;
  const center=box.getCenter(new THREE.Vector3());
  if(![...box.min.toArray(),...box.max.toArray()].every(Number.isFinite))return false;
  const rect=vp.renderer.domElement.getBoundingClientRect();if(!rect.width||!rect.height)return false;
  vp.camera.updateMatrixWorld(true);
  const camera=vp.getCameraState(),inverse=vp.camera.quaternion.clone().invert();let width=0,height=0,depth=0;
  for(const x of [box.min.x,box.max.x])for(const y of [box.min.y,box.max.y])for(const z of [box.min.z,box.max.z]){
    const q=new THREE.Vector3(x,y,z).sub(center).applyQuaternion(inverse);
    width=Math.max(width,Math.abs(q.x));height=Math.max(height,Math.abs(q.y));depth=Math.max(depth,Math.abs(q.z));
  }
  const tangent=Math.tan(THREE.MathUtils.degToRad(vp.perspectiveFov*.5));
  const fit=1.25*Math.max(height/tangent,width/(tangent*rect.width/rect.height));
  const radius=Math.max(.05,vp.camera.isPerspectiveCamera?fit+depth:Math.max(fit,depth+.05));
  vp.camera.near=Math.max(.001,Math.min(radius/10000,(radius-depth)/2));
  vp.camera.far=Math.max(vp.camera.far,(radius+depth)*2);
  vp.setCameraState({...camera,target:{x:center.x,y:center.y,z:center.z},radius});return true;
}
