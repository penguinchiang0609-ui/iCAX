import * as THREE from '../../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { nearbySurfaceHit } from '../../../../iCAX-UI/SDK/Viewport/surfaceProximity.mjs';

// Screen-space picking only. Callers supply allowed geometry and resolve exact CAD references.
const radius = 12;
export function geometrySnapCandidate({viewport, objectId, mode, expected, setupId, previous}, hit, event) {
  if (!objectId || !viewport || !Number.isFinite(event?.clientX) || !Number.isFinite(event?.clientY)) return null;
  const object = viewport.sceneObjects.get(objectId);
  if (!object?.visible) return null;
  const rect = viewport.renderer.domElement.getBoundingClientRect();
  if (hit && hit.object !== object) return null;
  hit ??= nearbySurfaceHit(object, viewport.camera, rect, event.clientX, event.clientY, radius, [...viewport.sceneObjects.values()]);
  if (!hit?.point || !Number.isInteger(hit.faceIndex)) return null;
  const near = object.worldToLocal(hit.point.clone()), ndc = hit.point.clone().project(viewport.camera);
  const dx = new THREE.Vector3(ndc.x + radius * 2 / rect.width, ndc.y, ndc.z).unproject(viewport.camera);
  const dy = new THREE.Vector3(ndc.x, ndc.y + radius * 2 / rect.height, ndc.z).unproject(viewport.camera);
  const maximumDistanceMm = Math.min(100, Math.max(1e-5, object.worldToLocal(dx).distanceTo(near), object.worldToLocal(dy).distanceTo(near)) * 1.5);
  const query = { mode: mode, frame: 'Workpiece', triangleIndex: String(hit.faceIndex), nearPoint: near.toArray(), maximumDistanceMm };
  const request = { expected, objectId, setupId, query };
  // Capture the camera used for this pointer. Later navigation must not change
  // the acceptance radius of an in-flight exact query.
  const matrix = new THREE.Matrix4().multiplyMatrices(viewport.camera.projectionMatrix, viewport.camera.matrixWorldInverse).multiply(object.matrixWorld);
  const project = point => {
    const p = new THREE.Vector3(...point).applyMatrix4(matrix);
    return { x: rect.left + (p.x + 1) * rect.width / 2, y: rect.top + (1 - p.y) * rect.height / 2, z: p.z };
  };
  const accepts = result => {
    const p = project(result.point);
    if (p.z < -1 || p.z > 1) return false;
    if (result.query.mode === 'line' && result.lineEnds?.length === 2) {
      const [a, b] = result.lineEnds.map(project), dx = b.x - a.x, dy = b.y - a.y;
      const t = Math.max(0, Math.min(1, ((event.clientX - a.x) * dx + (event.clientY - a.y) * dy) / (dx * dx + dy * dy || 1)));
      return Math.hypot(a.x + dx * t - event.clientX, a.y + dy * t - event.clientY) <= radius;
    }
    if (['center', 'midpoint', 'axis'].includes(result.query.mode)) return true; // Feature anchors may lie away from the picked surface.
    return Math.hypot(p.x - event.clientX, p.y - event.clientY) <= radius;
  };
  const positionKey = JSON.stringify([request, event.clientX, event.clientY, matrix.elements]);
  if(previous?.positionKey === positionKey)return previous;
  return {request, positionKey, accepts};
}
