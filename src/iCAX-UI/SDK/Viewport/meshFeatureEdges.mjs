import * as THREE from "../ThirdParty/three/three.module.js";

// Native BRep meshes supply surface normals for each face vertex. A coarse
// curved face can have long, narrow triangles whose facet normals suggest a
// crease even though the underlying surface is smooth. Compare the normals
// at the same edge endpoints instead of comparing those triangle planes.
export function createMeshFeatureEdgesGeometry(source, thresholdAngle = 22) {
  const position = source.getAttribute("position"), normal = source.getAttribute("normal");
  if (!source.userData.surfaceNormals || !normal || normal.count !== position?.count)
    return new THREE.EdgesGeometry(source, thresholdAngle);
  const threshold = Math.cos(thresholdAngle * Math.PI / 180);
  const edges = new Map(), index = source.getIndex();
  const count = index?.count ?? position.count;
  const key = p => p.map(value => Math.round(value * 1e4)).join(",");
  const at = (attribute, i) => [attribute.getX(i), attribute.getY(i), attribute.getZ(i)];
  const unit = n => { const length = Math.hypot(...n); return length > 1e-12 && Number.isFinite(length) ? n.map(value => value / length) : null; };
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  for (let i = 0; i + 2 < count; i += 3) {
    const vertices = [0, 1, 2].map(j => index ? index.getX(i + j) : i + j);
    const points = vertices.map(vertex => at(position, vertex)), keys = points.map(key);
    if (new Set(keys).size < 3) continue;
    const normals = vertices.map(vertex => unit(at(normal, vertex)));
    if (normals.some(value => !value)) return new THREE.EdgesGeometry(source, thresholdAngle);
    for (let j = 0; j < 3; j++) {
      const next = (j + 1) % 3, forward = keys[j] < keys[next];
      const a = forward ? j : next, b = forward ? next : j;
      const edgeKey = `${keys[a]}|${keys[b]}`;
      const value = { a: points[a], b: points[b], normalA: normals[a], normalB: normals[b] };
      const shared = edges.get(edgeKey);
      if (shared) shared.push(value); else edges.set(edgeKey, [value]);
    }
  }
  const positions = [];
  for (const shared of edges.values()) {
    let visible = shared.length === 1;
    for (let i = 0; !visible && i < shared.length; i++)
      for (let j = i + 1; !visible && j < shared.length; j++)
        visible = dot(shared[i].normalA, shared[j].normalA) < threshold
          || dot(shared[i].normalB, shared[j].normalB) < threshold;
    if (visible) positions.push(...shared[0].a, ...shared[0].b);
  }
  return new THREE.BufferGeometry().setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
}
