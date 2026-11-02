import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';

function screenBounds(objects,camera){
 const box=new THREE.Box3(),local=new THREE.Box3(),point=new THREE.Vector3();
 for(const object of objects){const geometry=object.geometry;if(!geometry)continue;if(!geometry.boundingBox)geometry.computeBoundingBox();local.copy(geometry.boundingBox).applyMatrix4(object.matrixWorld);box.union(local);}
 if(box.isEmpty())return null;
 let left=Infinity,right=-Infinity,bottom=Infinity,top=-Infinity,crossesEye=false;
 for(const x of [box.min.x,box.max.x])for(const y of [box.min.y,box.max.y])for(const z of [box.min.z,box.max.z]){
  point.set(x,y,z);if(point.clone().applyMatrix4(camera.matrixWorldInverse).z>=0)crossesEye=true;
  point.project(camera);left=Math.min(left,point.x);right=Math.max(right,point.x);bottom=Math.min(bottom,point.y);top=Math.max(top,point.y);
 }
 if(crossesEye)return {left:-1,right:1,bottom:-1,top:1};
 left=Math.max(-1,left);right=Math.min(1,right);bottom=Math.max(-1,bottom);top=Math.min(1,top);
 return right>=left&&top>=bottom?{left,right,bottom,top}:null;
}

// Probe the worktable's actual visible surface, not its centre or a filled box.
// A body fades only when it is closer than that surface on the same camera ray.
export function findTableOccluders({camera,tables,candidates,width=800,height=600}){
 const faded=new Set();if(!tables.length||!candidates.length)return faded;
 camera.updateMatrixWorld(true);
 for(const object of [...tables,...candidates])object.updateWorldMatrix(true,false);
 const bounds=screenBounds(tables,camera);if(!bounds)return faded;
 const raycaster=new THREE.Raycaster(),forward=camera.getWorldDirection(new THREE.Vector3());raycaster.firstHitOnly=true;
 const probe=(x,y)=>{
  raycaster.setFromCamera(new THREE.Vector2(x,y),camera);
  const cosine=raycaster.ray.direction.dot(forward);if(cosine<=0)return;
  raycaster.near=Math.max(0,camera.near/cosine);raycaster.far=camera.far/cosine;
  const surface=raycaster.intersectObjects(tables,false)[0];if(!surface)return;
  raycaster.far=surface.distance-Math.max(1e-5,surface.distance*1e-7);
  if(raycaster.far<=raycaster.near)return;
  for(const hit of raycaster.intersectObjects(candidates,false))faded.add(hit.object);
 };
 const grid=(rect,columns,rows,jitter=false)=>{
  for(let row=0;row<rows;row++)for(let column=0;column<columns;column++){
   const u=(column+(jitter ? .2+.6*(row*.61803398875%1) : .5))/columns;
   const v=(row+(jitter ? .2+.6*(column*.41421356237%1) : .5))/rows;
   probe(rect.left+(rect.right-rect.left)*u,rect.bottom+(rect.top-rect.bottom)*v);
  }
 };
 const columns=Math.min(25,Math.max(17,Math.ceil((bounds.right-bounds.left)*width/24)));
 const rows=Math.min(25,Math.max(11,Math.ceil((bounds.top-bounds.bottom)*height/24)));
 grid(bounds,columns,rows,true);probe((bounds.left+bounds.right)/2,(bounds.bottom+bounds.top)/2);
 // Extra probes over each projected part prevent a thin post near a table edge
 // from falling entirely between the regular table-area samples.
 for(const object of candidates){
  const part=screenBounds([object],camera);if(!part)continue;
  const overlap={left:Math.max(bounds.left,part.left),right:Math.min(bounds.right,part.right),bottom:Math.max(bounds.bottom,part.bottom),top:Math.min(bounds.top,part.top)};
  if(overlap.right>=overlap.left&&overlap.top>=overlap.bottom)grid(overlap,3,3);
 }
 return faded;
}
