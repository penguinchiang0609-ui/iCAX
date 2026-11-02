import * as THREE from '../ThirdParty/three/three.module.js';

const cache = new WeakMap();
const visible = object => { for (let node = object; node; node = node.parent) if (!node.visible) return false; return true; };

// Screen-space tolerance only locates a display triangle. Consumers must resolve
// the returned hit against their exact geometry before storing a CAD reference.
export function nearbySurfaceHit(object, camera, rect, clientX, clientY, radius = 12, occluders = []) {
  if (!object?.isMesh || !visible(object) || !rect.width || !rect.height) return null;
  const x = clientX - rect.left, y = clientY - rect.top;
  if (x < 0 || y < 0 || x > rect.width || y > rect.height) return null;
  object.updateWorldMatrix(true, false); camera.updateMatrixWorld();
  const matrix = new THREE.Matrix4().multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse).multiply(object.matrixWorld);
  const geometry = object.geometry, positions = geometry.getAttribute('position');
  const key = [...matrix.elements, rect.width, rect.height, positions.version, geometry.index?.version].join(',');
  let projected = cache.get(object);
  if (projected?.key !== key || projected.geometry !== geometry || projected.positions !== positions || projected.index !== geometry.index) {
    const vertices = [], p = new THREE.Vector4();
    for (let i = 0; i < positions.count; i++) {
      p.set(positions.getX(i), positions.getY(i), positions.getZ(i), 1).applyMatrix4(matrix);
      vertices.push(p.w > 0 && Math.abs(p.z) <= p.w
        ? [(p.x / p.w + 1) * rect.width / 2, (1 - p.y / p.w) * rect.height / 2, p.w] : null);
    }
    projected = { key, geometry, positions, index: geometry.index, vertices }; cache.set(object, projected);
  }
  const index = geometry.index, count = index?.count ?? positions.count, candidates = [];
  for (let i = 0; i < count; i += 3) {
    const ids = [0, 1, 2].map(j => index ? index.getX(i + j) : i + j);
    const points = ids.map(j => projected.vertices[j]);
    if (points.some(p => !p)) continue;
    if (x < Math.min(...points.map(p => p[0])) - radius || x > Math.max(...points.map(p => p[0])) + radius
      || y < Math.min(...points.map(p => p[1])) - radius || y > Math.max(...points.map(p => p[1])) + radius) continue;
    for (let j = 0; j < 3; j++) {
      const a = points[j], b = points[(j + 1) % 3], dx = b[0] - a[0], dy = b[1] - a[1];
      const t = Math.max(0, Math.min(1, ((x - a[0]) * dx + (y - a[1]) * dy) / (dx * dx + dy * dy || 1)));
      const px = a[0] + t * dx, py = a[1] + t * dy, distance = Math.hypot(px - x, py - y);
      if (distance > radius) continue;
      // Perspective-correct interpolation preserves the candidate's depth.
      const u = t * a[2] / ((1 - t) * b[2] + t * a[2]);
      const world = new THREE.Vector3().fromBufferAttribute(positions, ids[j]).lerp(new THREE.Vector3().fromBufferAttribute(positions, ids[(j + 1) % 3]), u).applyMatrix4(object.matrixWorld);
      candidates.push({ x: px, y: py, distance, world, center: points.reduce((p, q) => [p[0] + q[0] / 3, p[1] + q[1] / 3], [0, 0]) });
    }
  }
  candidates.sort((a, b) => a.distance - b.distance || a.world.distanceToSquared(camera.position) - b.world.distanceToSquared(camera.position));
  const ray = new THREE.Raycaster();
  const objects = [...new Set([object, ...occluders])].filter(visible);
  objects.forEach(item => item.updateWorldMatrix(true, false));
  for (const candidate of candidates) {
    // Move just inside the triangle to avoid cracks at coincident mesh edges.
    const dx = candidate.center[0] - candidate.x, dy = candidate.center[1] - candidate.y;
    const scale = Math.min(1, .02 / (Math.hypot(dx, dy) || 1));
    ray.setFromCamera(new THREE.Vector2((candidate.x + dx * scale) / rect.width * 2 - 1, 1 - (candidate.y + dy * scale) / rect.height * 2), camera);
    const hit = ray.intersectObjects(objects, false)[0];
    if (!hit || hit.object !== object) continue;
    const depth = candidate.world.clone().project(camera).z;
    const pixel = new THREE.Vector3((candidate.x + 1) / rect.width * 2 - 1, 1 - candidate.y / rect.height * 2, depth).unproject(camera);
    if (hit.point.distanceTo(candidate.world) > Math.max(pixel.distanceTo(candidate.world) * .2, 1e-6)) continue;
    return hit;
  }
  return null;
}
