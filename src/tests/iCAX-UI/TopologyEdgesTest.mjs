import assert from 'node:assert/strict';
import { test } from 'node:test';
import { parseTopologyEdges } from '../../iCAX-UI/SDK/Viewport/renderResource.mjs';

test('CAD polylines stay separate and smooth seams are independently selectable for display', () => {
  const data = parseTopologyEdges(new Float32Array([0,0,0, 1,0,0, 1,1,1, 9,9,9, 10,9,9]),
    new Uint32Array([0,3,1, 3,2,0]));
  assert.equal(data.topologyEdgeCount, 2);
  assert.equal(data.topologyEdges.length, 18);
  assert.deepEqual([...data.topologyFeatureEdges], [0,0,0,1,0,0,1,0,0,1,1,1]);
  assert.deepEqual([...data.topologyEdges.slice(12)], [9,9,9,10,9,9]);
});
test('invalid edge ranges and nonfinite samples cannot create WebGL geometry', () => {
  for (const range of [[1,2,1], [0,3,1], [0,1,1], [0,2,2], [0,2]]) {
    assert.throws(() => parseTopologyEdges(new Float32Array(6), new Uint32Array(range)));
  }
  assert.throws(() => parseTopologyEdges(new Float32Array([0,0,0,NaN,0,0]),new Uint32Array([0,2,1])));
  assert.throws(() => parseTopologyEdges(new Float32Array(6),new Uint32Array()));
  assert.equal(parseTopologyEdges(new Float32Array(),new Uint32Array()).topologyEdgeCount,0);
});
