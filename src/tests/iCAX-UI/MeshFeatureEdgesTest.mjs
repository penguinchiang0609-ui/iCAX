import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { createMeshFeatureEdgesGeometry } from '../../iCAX-UI/SDK/Viewport/meshFeatureEdges.mjs';

function pair(normals, surfaceNormals = true) {
  // Two coarse triangles of a smooth curved surface have a >22 degree facet
  // angle. Shared endpoint surface normals are nevertheless identical.
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute([0,0,0, 1000,0,0, 0,1,0, 1000,0,0, 0,0,0, 0,-1,.8], 3));
  geometry.setAttribute('normal', new THREE.Float32BufferAttribute(normals, 3));
  geometry.userData.surfaceNormals = surfaceNormals;
  return geometry;
}
const smooth = pair(Array(6).fill([0,0,1]).flat());
const old = new THREE.EdgesGeometry(smooth, 22), filtered = createMeshFeatureEdgesGeometry(smooth);
assert.equal(old.getAttribute('position').count, 10, 'Coarse facet extraction exposes the spurious shared long edge');
assert.equal(filtered.getAttribute('position').count, 8, 'Continuous surface suppresses it while preserving all four open boundaries');
const sharp = pair([...Array(3).fill([0,0,1]).flat(), ...Array(3).fill([0,1,0]).flat()]);
const crease = createMeshFeatureEdgesGeometry(sharp);
assert.equal(crease.getAttribute('position').count, 10, 'True sharp surface normals preserve the crease');
const invalid = pair(Array(18).fill(0));
assert.equal(createMeshFeatureEdgesGeometry(invalid).getAttribute('position').count, 10, 'Invalid surface normals retain facet edge extraction');
smooth.userData.surfaceNormals = false;
assert.equal(createMeshFeatureEdgesGeometry(smooth).getAttribute('position').count, 10, 'Meshes without native surface normals retain their existing boundaries');
const sourcePoints = new Set(Array.from({length:6},(_,i)=>[smooth.attributes.position.getX(i),smooth.attributes.position.getY(i),smooth.attributes.position.getZ(i)].join(',')));
const points = filtered.attributes.position;
for(let i=0;i<points.count;i++) assert.ok(sourcePoints.has([points.getX(i),points.getY(i),points.getZ(i)].join(',')));
for(const geometry of [smooth,old,filtered,sharp,crease,invalid])geometry.dispose();
console.log('Mesh feature edges: curved facet suppression, sharp/open boundaries and native endpoint preservation passed');
