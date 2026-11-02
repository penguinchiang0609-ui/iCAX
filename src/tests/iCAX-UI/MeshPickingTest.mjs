import assert from 'node:assert/strict';
import {test} from 'node:test';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {prepareMeshPicking} from '../../iCAX-UI/SDK/Viewport/meshPicking.mjs';
import {sampleOrbitPivot} from '../../iCAX-UI/SDK/Viewport/orbitPivot.mjs';

const originalRaycast=THREE.Mesh.prototype.raycast;
function mesh(geometry){const object=new THREE.Mesh(geometry,new THREE.MeshBasicMaterial({side:THREE.DoubleSide}));object.updateMatrixWorld(true);return object;}
function hits(object,raycaster,legacy=false){const result=[];(legacy?originalRaycast:object.raycast).call(object,raycaster,result);return result.sort((a,b)=>a.distance-b.distance||a.faceIndex-b.faceIndex);}
function compare(actual,expected){assert.equal(actual.length,expected.length);for(let i=0;i<actual.length;++i){assert.equal(actual[i].faceIndex,expected[i].faceIndex);assert.equal(actual[i].face.materialIndex,expected[i].face.materialIndex);assert.ok(actual[i].point.distanceTo(expected[i].point)<1e-7);assert.ok(Math.abs(actual[i].distance-expected[i].distance)<1e-7);}}
function rays(){return [-2.3,-.7,.4,2.1].map(x=>new THREE.Raycaster(new THREE.Vector3(x,.31,30),new THREE.Vector3(0,0,-1),.01,100));}

test('dense indexed picking preserves all triangles, face IDs and transformed hits',()=>{
 const geometry=new THREE.PlaneGeometry(12,12,190,170),object=mesh(geometry),index=geometry.index,array=index.array.slice();
 object.position.set(.7,-.3,2);object.rotation.set(.1,.25,.3);object.scale.set(1.3,.7,1.9);object.updateMatrixWorld(true);
 const expected=rays().map(r=>hits(object,r,true));assert.equal(prepareMeshPicking(object),true);
 assert.equal(geometry.index,index);assert.deepEqual(index.array,array);assert.equal(THREE.Mesh.prototype.raycast,originalRaycast);
 rays().forEach((r,i)=>compare(hits(object,r),expected[i]));
 for(const raycaster of rays()){raycaster.firstHitOnly=true;compare(hits(object,raycaster),hits(object,raycaster,true).slice(0,1));}
 geometry.dispose();assert.equal(geometry.boundsTree,null);
});

test('shared geometry reuses its index; disposal releases it without global patches',()=>{
 const geometry=new THREE.PlaneGeometry(8,8,170,170),a=mesh(geometry),b=mesh(geometry);
 prepareMeshPicking(a);const tree=geometry.boundsTree;prepareMeshPicking(b);assert.equal(geometry.boundsTree,tree);
 b.position.z=5;b.updateMatrixWorld(true);for(const r of rays())compare(hits(b,r),hits(b,r,true));
 geometry.dispose();assert.equal(geometry.boundsTree,null);assert.equal(THREE.Mesh.prototype.raycast,originalRaycast);
});

test('draw ranges, material groups and changed buffers retain current Three picking semantics',()=>{
 const geometry=new THREE.BoxGeometry(8,8,4,4,4,4),object=mesh(geometry);
 prepareMeshPicking(object,{minimumTriangles:0});
 for(const r of rays())compare(hits(object,r),hits(object,r,true));
 geometry.setDrawRange(0,48);for(const r of rays())compare(hits(object,r),hits(object,r,true));
 geometry.setDrawRange(0,Infinity);geometry.groups[0].count=6;for(const r of rays())compare(hits(object,r),hits(object,r,true));
 const positions=geometry.attributes.position;for(let i=0;i<positions.count;++i)positions.setZ(i,positions.getZ(i)+3);positions.needsUpdate=true;geometry.computeBoundingBox();geometry.computeBoundingSphere();
 for(const r of rays())compare(hits(object,r),hits(object,r,true));prepareMeshPicking(object,{minimumTriangles:0});for(const r of rays())compare(hits(object,r),hits(object,r,true));geometry.dispose();
});

test('material arrays preserve Three group and side behavior before and after preparation',()=>{
 const geometry=new THREE.BoxGeometry(8,8,4,4,4,4),object=mesh(geometry);
 const materials=Array.from({length:6},(_,i)=>new THREE.MeshBasicMaterial({side:i===4?THREE.BackSide:THREE.DoubleSide}));
 object.material=materials;
 assert.equal(prepareMeshPicking(object,{minimumTriangles:0}),false);
 for(const r of rays())compare(hits(object,r),hits(object,r,true));
 object.material=new THREE.MeshBasicMaterial({side:THREE.DoubleSide});
 assert.equal(prepareMeshPicking(object,{minimumTriangles:0}),true);
 object.material=materials;
 for(const r of rays())compare(hits(object,r),hits(object,r,true));
 geometry.dispose();
});

test('small and nonindexed meshes keep their existing raycast',()=>{
 for(const geometry of [new THREE.PlaneGeometry(8,8),new THREE.PlaneGeometry(8,8,170,170).toNonIndexed()]){const object=mesh(geometry),raycast=object.raycast;assert.equal(prepareMeshPicking(object),false);assert.equal(object.raycast,raycast);geometry.dispose();}
});

test('orbit area samples retain their nearest visible surface and pivot',()=>{
 const camera=new THREE.PerspectiveCamera(45,4/3,.01,1000);camera.position.set(0,0,20);camera.lookAt(0,0,0);camera.updateMatrixWorld(true);
 const object=mesh(new THREE.PlaneGeometry(12,12,180,180)),options={camera,objects:[object],rect:{left:0,top:0,width:800,height:600},clientX:400,clientY:300,radiusPx:6};
 const before=sampleOrbitPivot(options);prepareMeshPicking(object);const after=sampleOrbitPivot(options);assert.equal(after.hitCount,before.hitCount);assert.ok(new THREE.Vector3(...after.point).distanceTo(new THREE.Vector3(...before.point))<1e-7);object.geometry.dispose();
});

test('accelerated orbit sampling skips clipped front surfaces in both camera projections',()=>{
 for(const camera of [new THREE.PerspectiveCamera(45,4/3,10,100),new THREE.OrthographicCamera(-8,8,6,-6,10,100)]){
  camera.position.set(0,0,20);camera.lookAt(0,0,0);camera.updateMatrixWorld(true);
  const object=mesh(new THREE.BoxGeometry(12,12,24,30,30,30));
  const options={camera,objects:[object],rect:{left:0,top:0,width:800,height:600},clientX:400,clientY:300,radiusPx:6};
  const before=sampleOrbitPivot(options);assert.equal(before.hitCount,9);assert.ok(Math.abs(before.point[2]+12)<1e-7);
  prepareMeshPicking(object,{minimumTriangles:0});const after=sampleOrbitPivot(options);
  assert.equal(after.hitCount,before.hitCount);assert.ok(new THREE.Vector3(...after.point).distanceTo(new THREE.Vector3(...before.point))<1e-7);
  object.geometry.dispose();
 }
});
