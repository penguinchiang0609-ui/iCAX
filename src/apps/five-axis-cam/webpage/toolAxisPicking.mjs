import {supportsToolAxis} from './toolAxisEligibility.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';

const distanceToSegment=(p,a,b)=>{
  const x=b.x-a.x,y=b.y-a.y,n=x*x+y*y,t=n?Math.max(0,Math.min(1,((p.x-a.x)*x+(p.y-a.y)*y)/n)):0;
  return Math.hypot(p.x-a.x-x*t,p.y-a.y-y*t);
};
const before=(a,b)=>!b||a.distance<b.distance-1e-7||Math.abs(a.distance-b.distance)<=1e-7&&(a.depth<b.depth-1e-9||Math.abs(a.depth-b.depth)<=1e-9&&(a.intentId<b.intentId||a.intentId===b.intentId&&a.sampleIndex<b.sampleIndex));

// Match the rendered CSS-pixel glyph, not an invisible 3D line extending along
// the tool direction. Selection never queries CAD or changes the camera.
export function pickDisplayedToolAxis(view,event){
  const display=view.fiveToolAxisDisplay,viewport=view.viewport,camera=viewport?.camera,canvas=viewport?.renderer?.domElement;
  if(!display?.visible||!display.group?.visible||!camera||!canvas||!Number.isFinite(event?.clientX)||!Number.isFinite(event?.clientY)||view.fiveAxisArea!=='toolpath'||view.fiveCamPage==='rapid')return null;
  const rect=canvas.getBoundingClientRect();if(!rect.width||!rect.height||event.clientX<rect.left||event.clientX>rect.right||event.clientY<rect.top||event.clientY>rect.bottom)return null;
  const size=viewport.renderer.getSize(new THREE.Vector2()),scaleX=rect.width/Math.max(1,size.x),scaleY=rect.height/Math.max(1,size.y),pointer={x:(event.clientX-rect.left)/scaleX,y:(event.clientY-rect.top)/scaleY};
  const enabled=new Set((view.scene?.intentToolpaths||[]).filter(c=>c.enabled!==false&&supportsToolAxis(c)).map(c=>c.id)),mvp=new THREE.Matrix4();let best=null;
  for(const group of display.group.children){const intentId=group.userData.intentId;if(!group.visible||!enabled.has(intentId))continue;
    const mesh=group.children.find(m=>m.visible&&m.userData.kind==='axis');if(!mesh)continue;
    const geometry=mesh.geometry,shown=geometry.getAttribute('instanceShown'),points=geometry.getAttribute('instancePoint'),directions=geometry.getAttribute('instanceDirection'),samples=mesh.userData.toolAxisSamples||[];
    if(!shown||!points||!directions)continue;mvp.multiplyMatrices(camera.projectionMatrix,camera.matrixWorldInverse).multiply(mesh.matrixWorld);const m=mvp.elements;
    const projected=(x,y,z)=>{const w=m[3]*x+m[7]*y+m[11]*z+m[15];if(Math.abs(w)<=1e-12)return null;
      return {x:((m[0]*x+m[4]*y+m[8]*z+m[12])/w+1)*size.x/2,y:(1-(m[1]*x+m[5]*y+m[9]*z+m[13])/w)*size.y/2,z:(m[2]*x+m[6]*y+m[10]*z+m[14])/w,w};};
    for(let i=0;i<Math.min(geometry.instanceCount,samples.length);++i){if(shown.getX(i)<.5)continue;
      const x=points.getX(i),y=points.getY(i),z=points.getZ(i),anchor=projected(x,y,z),next=projected(x+directions.getX(i),y+directions.getY(i),z+directions.getZ(i));
      if(!anchor||!next||anchor.w<=0||anchor.z<-1||anchor.z>1)continue;
      const dx=next.x-anchor.x,dy=next.y-anchor.y,length=Math.hypot(dx,dy);let distance;
      if(length<.001)distance=Math.max(0,Math.abs(Math.hypot(pointer.x-anchor.x,pointer.y-anchor.y)-4.7)-.5);
      else{const ax=dx/length,ay=dy/length,at=(a,b)=>({x:anchor.x+ax*a-ay*b,y:anchor.y+ay*a+ax*b});
        distance=Math.max(0,Math.min(distanceToSegment(pointer,at(0,0),at(22,0)),distanceToSegment(pointer,at(16,3.5),at(22,0)),distanceToSegment(pointer,at(16,-3.5),at(22,0)))-.575);
      }
      if(distance>10)continue;const sample=samples[i];const hit={intentId,placementId:group.userData.placementId,sampleIndex:i,
        sample:{anchor:sample.anchor,point:sample.point,direction:sample.direction,surfaceNormal:sample.surfaceNormal,keypointIds:sample.keypointIds},distance,depth:(anchor.z+1)/2};if(before(hit,best))best=hit;
    }
  }
  return best?{...best,sample:{anchor:{...best.sample.anchor},point:[...best.sample.point],direction:[...best.sample.direction],surfaceNormal:[...best.sample.surfaceNormal],keypointIds:[...best.sample.keypointIds]}}:null;
}

export { syncToolAxisPickHighlight,setToolAxisDisplayVisible } from './toolAxisDisplay.mjs';
