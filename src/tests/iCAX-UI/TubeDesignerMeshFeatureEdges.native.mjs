// Native rounded profile/cut mesh, including differently split face seams.
import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { parseRenderGeometryResource } from '../../iCAX-UI/SDK/Viewport/renderResource.mjs';
import { createMeshFeatureEdgesGeometry } from '../../iCAX-UI/SDK/Viewport/meshFeatureEdges.mjs';
import { repositoryRoot, startSideSketchNativeBridge } from './fixtures/sideSketchNativeBridge.mjs';

const artifacts = resolve(process.env.ICAX_ARTIFACT_DIR || resolve(repositoryRoot, 'output/tests/mesh-feature-edges-native'));
mkdirSync(artifacts, { recursive: true });
const bridge = await startSideSketchNativeBridge(artifacts);
try {
  const descriptor = JSON.parse(readFileSync(resolve(repositoryRoot, 'src/apps/tube-designer/templates/profile/rect/profile.json'), 'utf8'));
  const parameters = { ...Object.fromEntries(descriptor.parameters.map(item => [item.key, item.defaultValue])), width: 60, depth: 40, wallThickness: 2, cornerRadius: 3, innerRadius: 1 };
  const payload = { profileRef: { scope: 'system', id: 'rect' }, parameters, length: 1000, previewResourceKey: 'rounded-cut-feature-edges' };
  const blank = await bridge.invoke('PreviewNestingSideSketchPart', payload);
  const period = blank.unfolding.surfaces.find(surface => !surface.inner).uPeriod;
  const sketch = { schema: 'icax.tube-sketch', schemaVersion: 1, kind: 'side', unit: 'mm', coordinateSpace: 'arc-length-axial', length: 1000, faceHeight: period, perimeter: period, trajectoryWidth: 1,
    entities: [{ id: 'corner-hole', kind: 'circle', cx: 54, cy: 200, radius: 20, closed: true },
      { id: 'period-ellipse', kind: 'ellipse', cx: period - 5, cy: 140, radiusX: 25, radiusY: 40, closed: true },
      { id: 'rectangle-hole', kind: 'rectangle', x: period + 10, y: 200, width: 50, height: 25, closed: true }] };
  const report = await bridge.invoke('PreviewNestingSideSketchPart', { ...payload, sketch });
  assert.equal(report.available, true);
  const reference = report.preview.baseGeometry;
  const resource = await bridge.rpc({ action: 'resource', payload: { url: reference.url, version: Number(reference.version || reference.dataVersion || 0) } });
  const bytes = Buffer.from(resource.base64, 'base64');
  const mesh = parseRenderGeometryResource(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.BufferAttribute(mesh.positions, 3));
  assert.equal(mesh.normals.length, mesh.positions.length, 'Native BRep vertex surface normals must accompany the cut mesh');
  geometry.setAttribute('normal', new THREE.BufferAttribute(mesh.normals, 3));
  geometry.userData.surfaceNormals = true;
  geometry.setIndex(new THREE.BufferAttribute(mesh.indices, 1));
  const legacy = new THREE.EdgesGeometry(geometry, 22), edges = createMeshFeatureEdgesGeometry(geometry, 22);
  const longEdges = source => {
    const p = source.getAttribute('position'), result = [];
    for (let i = 0; i < p.count; i += 2) {
      const a = new THREE.Vector3().fromBufferAttribute(p, i), b = new THREE.Vector3().fromBufferAttribute(p, i + 1);
      if (a.distanceTo(b) > 250) result.push({ start: a.toArray(), end: b.toArray(), length: a.distanceTo(b) });
    }
    return result;
  };
  const before = longEdges(legacy), after = longEdges(edges);
  assert.ok(before.length >= 4, 'The crossed-period circle/rectangle/ellipse fixture reproduces false facet creases');
  assert.deepEqual(after, [], 'Smooth inner rounded surfaces must not display long triangulation lines');
  assert.ok(edges.attributes.position.count > 200, 'End and cut boundaries remain visible');
  const reportData = { passed: true, dllHash: bridge.dllHash, importerHash: bridge.importerHash, meshVertices: mesh.positions.length / 3, normalCount: mesh.normals.length / 3,
    triangles: mesh.indices.length / 3, edgeSegments: edges.attributes.position.count / 2, falseLongEdgesBefore: before, falseLongEdgesAfter: after, reference };
  writeFileSync(resolve(artifacts, 'report.json'), JSON.stringify(reportData, null, 2));
  console.log(JSON.stringify({ passed: true, falseLongEdgesBefore: before.length, falseLongEdgesAfter: after.length, edgeSegments: reportData.edgeSegments }));
  legacy.dispose(); edges.dispose(); geometry.dispose();
} finally { await bridge.close(); }
