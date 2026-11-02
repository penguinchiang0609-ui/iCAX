import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { nearbySurfaceHit } from '../../iCAX-UI/SDK/Viewport/surfaceProximity.mjs';

const rect = { left: 30, top: 40, width: 800, height: 600 };
let cases = 0;
for (const perspective of [false, true]) {
  const camera = perspective ? new THREE.PerspectiveCamera(45, 4 / 3, .1, 1000) : new THREE.OrthographicCamera(-60, 60, 45, -45, .1, 1000);
  camera.position.set(0, 0, 160); camera.lookAt(0, 0, 0); camera.updateMatrixWorld();
  const mesh = new THREE.Mesh(new THREE.BoxGeometry(40, 60, 20), new THREE.MeshBasicMaterial());
  const parent = new THREE.Group(); parent.add(mesh);
  const project = value => {
    const p = mesh.localToWorld(new THREE.Vector3(...value)).project(camera);
    return { x: rect.left + (p.x + 1) * rect.width / 2, y: rect.top + (1 - p.y) * rect.height / 2 };
  };
  for (const transformed of [false, true]) {
    if (transformed) { parent.rotation.z = .3; parent.position.set(5, -3, 4); parent.scale.setScalar(1.2); }
    parent.updateMatrixWorld(true);
    const center = project([0, 0, 10]), edge = project([20, 0, 10]);
    const dx = edge.x - center.x, dy = edge.y - center.y, length = Math.hypot(dx, dy);
    const pick = distance => nearbySurfaceHit(mesh, camera, rect, edge.x + dx / length * distance, edge.y + dy / length * distance);
    const hit = pick(8); assert.ok(hit);
    assert.ok(Math.abs(mesh.worldToLocal(hit.point.clone()).z - 10) < 1e-6, 'do not snap through the front to a rear triangle');
    assert.equal(pick(24), null);
    mesh.visible = false; assert.equal(pick(8), null); mesh.visible = true;
    parent.visible = false; assert.equal(pick(8), null); parent.visible = true;
    if (!transformed) {
      const occluder = new THREE.Mesh(new THREE.BoxGeometry(44, 64, 1), new THREE.MeshBasicMaterial());
      occluder.position.z = 20;
      assert.equal(nearbySurfaceHit(mesh, camera, rect, edge.x + dx / length * 8, edge.y + dy / length * 8, 12, [occluder]), null, 'do not snap to the workpiece through another object');
      occluder.geometry.dispose(); occluder.material.dispose();
    }
    cases++;
  }
  parent.position.z = 300; parent.updateMatrixWorld(true);
  assert.equal(nearbySurfaceHit(mesh, camera, rect, 400, 300), null, 'behind-camera geometry is not a candidate'); cases++;
  mesh.geometry.dispose(); mesh.material.dispose();
  const ring = new THREE.Mesh(new THREE.RingGeometry(10, 20, 64), new THREE.MeshBasicMaterial({ side: THREE.DoubleSide }));
  assert.equal(nearbySurfaceHit(ring, camera, rect, rect.left + rect.width / 2, rect.top + rect.height / 2), null, 'a hole is not filled by a bounding-box pick'); cases++;
  ring.geometry.dispose(); ring.material.dispose();
}
console.log(JSON.stringify({ passed: true, cases }));
