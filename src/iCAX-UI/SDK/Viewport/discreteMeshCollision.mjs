import * as THREE from '../ThirdParty/three/three.module.js';
import { MeshBVH, ExtendedTriangle } from '../ThirdParty/three-mesh-bvh/index.module.js';

const cache=new WeakMap();
const epsilon=1e-6;

// Geometry-only discrete queries. No trajectories, scene policy or persistence;
// renderer buffers and face/index order remain untouched.
export function prepareMeshCollider(source) {
  const position=source.getAttribute('position'),index=source.getIndex();
  const prior=cache.get(source);
  if(prior&&prior.position===position&&prior.positionVersion===position.version&&prior.index===index&&prior.indexVersion===index?.version)return prior.collider;
  if(!position||position.itemSize!==3||position.count<3)throw Error('碰撞模型缺少三角面。');
  const geometry=new THREE.BufferGeometry();
  geometry.setAttribute('position',position);
  if(index)geometry.setIndex(index.clone());
  const topology=meshTopology(position,index);
  geometry.boundsTree=new MeshBVH(geometry,{maxLeafTris:8});
  const collider={geometry,box:geometry.boundingBox,closed:topology.closed,probes:topology.probes};
  cache.set(source,{position,positionVersion:position.version,index,indexVersion:index?.version,collider});
  return collider;
}

function meshTopology(position,index) {
  const ids=new Map(),vertices=[],parents=[],edges=new Map();
  const root=i=>{while(parents[i]!==i){parents[i]=parents[parents[i]];i=parents[i];}return i;};
  const vertex=i=>{
    const p=new THREE.Vector3().fromBufferAttribute(position,i),key=p.toArray().map(v=>Math.round(v/epsilon)).join(',');
    let id=ids.get(key);if(id==null){id=vertices.length;ids.set(key,id);vertices.push(p);parents.push(id);}return id;
  };
  const count=index?.count??position.count;
  for(let i=0;i+2<count;i+=3){
    const v=[0,1,2].map(j=>vertex(index?index.getX(i+j):i+j));
    if(new Set(v).size<3)continue;
    for(let j=0;j<3;++j){const a=v[j],b=v[(j+1)%3];parents[root(b)]=root(a);
      const key=a<b?`${a}:${b}`:`${b}:${a}`,edge=edges.get(key)??{count:0,orientation:0};
      ++edge.count;edge.orientation+=a<b?1:-1;edges.set(key,edge);}
  }
  const components=new Map();vertices.forEach((p,i)=>components.set(root(i),p));
  return {closed:edges.size>0&&[...edges.values()].every(e=>e.count===2&&e.orientation===0),probes:[...components.values()]};
}

const rayDirections=[new THREE.Vector3(1,.371,.127).normalize(),new THREE.Vector3(.173,1,.419).normalize(),new THREE.Vector3(.293,.157,1).normalize()];
function contains(collider,point) {
  if(!collider.closed||!collider.box.containsPoint(point))return false;
  // Majority parity avoids a ray grazing a shared triangle edge. Open surfaces
  // never acquire an invented interior.
  let inside=0;
  for(const direction of rayDirections){
    const hits=collider.geometry.boundsTree.raycast(new THREE.Ray(point,direction),THREE.DoubleSide).sort((a,b)=>a.distance-b.distance);
    let crossings=0,previous=-Infinity;
    for(const hit of hits)if(hit.distance-previous>epsilon){++crossings;previous=hit.distance;}
    if(crossings%2)++inside;
  }
  return inside>=2;
}

export function meshesCollide(fixed,moving,movingToFixed,clearance=0) {
  if(!Number.isFinite(clearance)||clearance<0)throw Error('碰撞阈值无效。');
  const limit=clearance+epsilon,box=moving.box.clone().applyMatrix4(movingToFixed);
  if(!fixed.box.clone().expandByScalar(limit).intersectsBox(box))return false;
  if(fixed.geometry.boundsTree.intersectsGeometry(moving.geometry,movingToFixed))return true;
  const hit=fixed.geometry.boundsTree.closestPointToGeometry(moving.geometry,movingToFixed,{},null,limit,limit+epsilon);
  if(hit&&hit.distance<=limit)return true;
  for(const point of moving.probes)if(contains(fixed,point.clone().applyMatrix4(movingToFixed)))return true;
  if(moving.closed){const inverse=movingToFixed.clone().invert();for(const point of fixed.probes)if(contains(moving,point.clone().applyMatrix4(inverse)))return true;}
  return false;
}

// The moving body remains an oriented box primitive. Only obstacle triangles
// enter the BVH; no rendered cutting-head geometry or box triangulation is used.
export function prepareBoxCollider(box) {
  const vector=value=>{
    if(!Array.isArray(value)||value.length!==3||!value.every(Number.isFinite))throw Error('碰撞盒数据无效。');
    return new THREE.Vector3(...value);
  };
  const center=vector(box.center),half=vector(box.halfExtents);
  if(half.x<=0||half.y<=0||half.z<=0||!Array.isArray(box.axes)||box.axes.length!==3)throw Error('碰撞盒尺寸或姿态无效。');
  const axes=box.axes.map(vector),local=new THREE.Matrix4().makeBasis(...axes).setPosition(center);
  if(axes.some(a=>Math.abs(a.lengthSq()-1)>epsilon)||Math.abs(axes[0].dot(axes[1]))+Math.abs(axes[0].dot(axes[2]))+Math.abs(axes[1].dot(axes[2]))>epsilon)throw Error('碰撞盒姿态无效。');
  const bounds=new THREE.Box3(half.clone().negate(),half.clone());
  const points=Array.from({length:8},(_,i)=>new THREE.Vector3(i&1?half.x:-half.x,i&2?half.y:-half.y,i&4?half.z:-half.z));
  const edges=[];for(let i=0;i<8;++i)for(const bit of [1,2,4])if(!(i&bit))edges.push(new THREE.Line3(points[i],points[i|bit]));
  return {local,bounds,edges};
}

export function boxCollidesMesh(fixed,moving,movingToFixed,clearance=0) {
  if(!Number.isFinite(clearance)||clearance<0)throw Error('碰撞阈值无效。');
  const transform=movingToFixed.clone().multiply(moving.local),elements=transform.elements;
  if(!elements.every(Number.isFinite)||Math.abs(elements[3])+Math.abs(elements[7])+Math.abs(elements[11])+Math.abs(elements[15]-1)>epsilon)throw Error('碰撞盒变换无效。');
  const basis=[0,1,2].map(i=>new THREE.Vector3().setFromMatrixColumn(transform,i));
  if(basis.some(a=>Math.abs(a.lengthSq()-1)>epsilon)||Math.abs(basis[0].dot(basis[1]))+Math.abs(basis[0].dot(basis[2]))+Math.abs(basis[1].dot(basis[2]))>epsilon)throw Error('碰撞盒需要刚体变换。');
  const limit=clearance+epsilon,inverse=transform.clone().invert(),{bounds,edges}=moving;
  const broad=bounds.clone().applyMatrix4(transform).expandByScalar(limit);
  if(!fixed.box.intersectsBox(broad))return false;
  const triangle=new ExtendedTriangle();
  const intersects=fixed.geometry.boundsTree.shapecast({
    intersectsBounds:node=>node.intersectsBox(broad),
    intersectsTriangle:source=>{
      triangle.a.copy(source.a).applyMatrix4(inverse);triangle.b.copy(source.b).applyMatrix4(inverse);triangle.c.copy(source.c).applyMatrix4(inverse);triangle.needsUpdate=true;
      if(bounds.intersectsTriangle(triangle))return true;
      // A separated convex triangle/box pair reaches its minimum at a
      // triangle vertex/box face, box vertex/triangle face, or edge pair.
      if(triangle.points.some(point=>bounds.distanceToPoint(point)<=limit))return true;
      return edges.some(edge=>triangle.closestPointToSegment(edge)<=limit);
    }
  });
  if(intersects)return true;
  // With no boundary intersection, a closed obstacle can still enclose the
  // whole box. Open obstacle surfaces do not acquire an invented interior.
  return contains(fixed,new THREE.Vector3().setFromMatrixPosition(transform));
}
