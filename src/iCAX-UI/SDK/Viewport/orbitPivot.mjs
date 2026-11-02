import * as THREE from '../ThirdParty/three/three.module.js';

// One nearest surface hit per ray avoids weighting by triangle density or by
// the number of back surfaces. The fixed CSS footprint also bridges small holes.
export function sampleOrbitPivot({camera,objects,rect,clientX,clientY,radiusPx=6}){
  if(!rect.width||!rect.height||!objects.length)return null;
  const raycaster=new THREE.Raycaster();raycaster.firstHitOnly=true;
  const forward=camera.getWorldDirection(new THREE.Vector3());
  const diagonal=Math.SQRT1_2;
  const offsets=[[0,0],[1,0],[-1,0],[0,1],[0,-1],[diagonal,diagonal],[-diagonal,diagonal],[diagonal,-diagonal],[-diagonal,-diagonal]];
  const points=[];
  for(const [dx,dy] of offsets){
    const x=clientX+dx*radiusPx,y=clientY+dy*radiusPx;
    if(x<rect.left||x>rect.left+rect.width||y<rect.top||y>rect.top+rect.height)continue;
    raycaster.setFromCamera(new THREE.Vector2((x-rect.left)/rect.width*2-1,1-(y-rect.top)/rect.height*2),camera);
    // Limit the accelerated nearest-hit query to the visible camera interval.
    // Otherwise a clipped front surface can hide a visible back surface.
    const cosine=raycaster.ray.direction.dot(forward);
    raycaster.near=Math.max(0,camera.near/cosine);raycaster.far=camera.far/cosine;
    const hit=raycaster.intersectObjects(objects,false).find(hit=>{
      const p=hit.point.clone().project(camera);return p.z>=-1&&p.z<=1;
    });
    if(hit)points.push(hit.point.toArray());
  }
  if(!points.length)return null;
  const center=new THREE.Vector3();for(const point of points)center.add(new THREE.Vector3(...point));center.multiplyScalar(1/points.length);
  return {point:center.toArray(),source:'mesh',hitCount:points.length,samples:points};
}

const orientation=(theta,phi,constrained)=>{
  const sin=Math.sin(phi),eye=new THREE.Vector3(sin*Math.cos(theta),sin*Math.sin(theta),Math.cos(phi));
  const up=constrained?new THREE.Vector3(0,0,1):new THREE.Vector3(-Math.cos(phi)*Math.cos(theta),-Math.cos(phi)*Math.sin(theta),sin);
  return new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().lookAt(eye,new THREE.Vector3(),up));
};

// Rotate the eye and look-at target together about the chosen pivot. Merely
// changing the look-at target would jump/reframe the view on pointerdown.
export function orbitCameraState(start,angles,pivot,constrained=false){
  const next={...start,...angles};if(!pivot)return next;
  const rotation=orientation(next.theta,next.phi,constrained).multiply(orientation(start.theta,start.phi,constrained).invert());
  const origin=new THREE.Vector3(...pivot),target=new THREE.Vector3(start.target.x,start.target.y,start.target.z).sub(origin).applyQuaternion(rotation).add(origin);
  next.target={x:target.x,y:target.y,z:target.z};return next;
}
