import * as THREE from '../ThirdParty/three/three.module.js';
import {MeshBVH,acceleratedRaycast} from '../ThirdParty/three-mesh-bvh/index.module.js';

const indexes=new WeakMap();
const groupsKey=geometry=>geometry.groups.map(g=>`${g.start}:${g.count}:${g.materialIndex}`).join('|');
const matches=(geometry,entry)=>geometry.boundsTree===entry.tree
  &&geometry.attributes.position===entry.position&&geometry.attributes.position.version===entry.positionVersion
  &&geometry.index===entry.index&&geometry.index.version===entry.indexVersion
  &&geometry.drawRange.start===entry.start&&geometry.drawRange.count===entry.count&&groupsKey(geometry)===entry.groups;

function indexedRaycast(raycaster,hits){
  const entry=indexes.get(this.geometry);
  // Changed buffers/ranges use Three's current geometry until republished.
  // Never pick against a stale tree or rebuild a large tree on pointerdown.
  // The indirect BVH in the bundled version does not preserve material-group
  // semantics for material arrays. Keep Three's exact behavior for those meshes.
  if(entry&&!Array.isArray(this.material)&&matches(this.geometry,entry))acceleratedRaycast.call(this,raycaster,hits);
  else THREE.Mesh.prototype.raycast.call(this,raycaster,hits);
}

// CAD face mappings refer to the original triangle order. Indirect BVHs retain
// that order, share one index per geometry, and do not patch Three prototypes.
export function prepareMeshPicking(mesh,{minimumTriangles=50000}={}){
  const geometry=mesh?.geometry;
  if(!mesh?.isMesh||mesh.isSkinnedMesh||Array.isArray(mesh.material)||!geometry?.index||!geometry.attributes.position
     ||geometry.morphAttributes.position?.length||geometry.index.count/3<minimumTriangles)return false;
  let entry=indexes.get(geometry);
  if(!entry||!matches(geometry,entry)){
    entry?.dispose();
    const tree=new MeshBVH(geometry,{indirect:true,maxLeafTris:10});
    geometry.boundsTree=tree;
    entry={tree,position:geometry.attributes.position,positionVersion:geometry.attributes.position.version,
      index:geometry.index,indexVersion:geometry.index.version,start:geometry.drawRange.start,count:geometry.drawRange.count,groups:groupsKey(geometry)};
    entry.dispose=()=>{if(geometry.boundsTree===tree)geometry.boundsTree=null;geometry.removeEventListener('dispose',entry.dispose);indexes.delete(geometry);};
    indexes.set(geometry,entry);geometry.addEventListener('dispose',entry.dispose);
  }
  mesh.raycast=indexedRaycast;return true;
}
