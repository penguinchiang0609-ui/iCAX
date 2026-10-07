import assert from 'node:assert/strict';

const near = (actual, expected, label) => assert.ok(Math.abs(actual - expected) < 0.002,
  `${label}: ${actual} differs from ${expected}`);
const dot = (left, right) => left.reduce((sum, value, index) => sum + value * right[index], 0);

function sourcePlacement(part, key) {
  const source = part.properties['manufacturing.sourceMembers'].find(row => row.itemKey === key);
  assert.ok(source, `${key}: final part lost its actual source member`);
  assert.equal(source.spans.length, 1, `${key}: expected one straight source span`);
  return source.spans[0].placement;
}

function toLocal(placement, world) {
  return placement.origin.map((value, index) => value + world[0] * placement.xAxis[index]
    + world[1] * placement.yAxis[index] + world[2] * placement.zAxis[index]);
}

function toWorld(placement, local) {
  const translated = local.map((value, index) => value - placement.origin[index]);
  return [placement.xAxis, placement.yAxis, placement.zAxis].map(axis => dot(axis, translated));
}

function worldBounds(placement, bounds) {
  const corners = [];
  for (const x of [bounds[0], bounds[1]]) for (const y of [bounds[2], bounds[3]])
    for (const z of [bounds[4], bounds[5]]) corners.push(toWorld(placement, [x, y, z]));
  return [0, 1, 2].flatMap(axis => [Math.min(...corners.map(point => point[axis])),
    Math.max(...corners.map(point => point[axis]))]);
}

export async function assertHorizontalNativeEndFit(native, disassembled, values) {
  const parts = disassembled.tubeDesigner.parts;
  const keys = ['main_grid.horizontal.0001', 'outer_frame.left.0001', 'outer_frame.right.0001'];
  const selected = keys.map(key => {
    const part = parts.find(row => row.properties['manufacturing.sourceMembers'].some(source => source.itemKey === key));
    assert.ok(part, `${key}: missing actual disassembled part`);
    return { key, part, placement: sourcePlacement(part, key) };
  });
  const snapshot = await native.invoke('InspectNativeGeometry', { entityIds: selected.map(row => row.part.entityId) });
  for (const row of selected) {
    row.shape = snapshot.geometryChecks.find(check => check.entityId === row.part.entityId);
    assert.ok(row.shape?.valid && row.shape.solids === 1, `${row.key}: invalid final native solid`);
    row.world = worldBounds(row.placement, row.shape.bounds);
  }
  const [branch, left, right] = selected;
  const nearPlanes = [left.world[1], right.world[0]];
  const expectedLength = nearPlanes[1] - nearPlanes[0] + 2 * values.horizontalBranchReserve;
  const axialLength = branch.shape.bounds[1] - branch.shape.bounds[0];
  near(axialLength, expectedLength, 'Actual horizontal BRep length from actual receiver near faces');
  near(branch.part.length, axialLength, 'Reported horizontal length follows actual BRep');
  const bounds = branch.shape.bounds;
  const y = (bounds[2] + bounds[3]) / 2;
  const z = bounds[5] - branch.part.profile.wallThickness / 2;
  const probes = [];
  const add = (row, point, expected, role) => probes.push({ entityId: row.part.entityId, point, expected, role });
  const endpoints = [];
  for (const [index, host] of [left, right].entries()) {
    const direction = index === 0 ? 1 : -1;
    const endpoint = [bounds[index], y, z];
    const world = toWorld(branch.placement, endpoint);
    near(world[0], nearPlanes[index] - direction * values.horizontalBranchReserve,
      `${host.key}: actual horizontal endpoint follows the actual near face`);
    add(branch, endpoint, 'boundary', `${host.key}: actual branch end plane`);
    add(branch, [endpoint[0] + direction * 0.05, y, z], 'inside', `${host.key}: retained end material`);
    add(branch, [endpoint[0] - direction * 0.05, y, z], 'outside', `${host.key}: no material beyond actual end`);
    const nearWall = [...world];
    nearWall[0] = nearPlanes[index] - direction * host.part.profile.wallThickness / 2;
    add(branch, toLocal(branch.placement, nearWall), values.horizontalBranchReserve === 0 ? 'outside' : 'inside',
      `${host.key}: branch at actual receiver near-wall midpoint`);
    // The selected insert route retains its clearance aperture even at zero
    // insertion. Probe both the real slot and adjacent retained receiver skin.
    add(host, toLocal(host.placement, nearWall), 'outside', `${host.key}: actual receiver aperture`);
    const besideSlot = [...nearWall];
    besideSlot[1] = branch.world[3] + values.assemblyClearance + 0.2;
    add(host, toLocal(host.placement, besideSlot), 'inside', `${host.key}: receiver skin beside actual aperture`);
    add(branch, toLocal(branch.placement, besideSlot), 'outside', `${host.key}: no branch outside its actual section`);
    const farWall = [...nearWall];
    farWall[0] = (index === 0 ? host.world[0] : host.world[1]) + direction * host.part.profile.wallThickness / 2;
    add(host, toLocal(host.placement, farWall), 'inside', `${host.key}: opposite wall remains`);
    add(branch, toLocal(branch.placement, farWall), 'outside', `${host.key}: branch does not reach opposite wall`);
    endpoints.push({ receiverKey: host.key, actualNearPlane: nearPlanes[index], actualEndpoint: world });
  }
  const classified = await native.invoke('InspectNativeGeometry', {
    entityIds: selected.map(row => row.part.entityId),
    points: probes.map(({ entityId, point }) => ({ entityId, point })),
  });
  for (const row of selected) {
    const check = classified.geometryChecks.find(candidate => candidate.entityId === row.part.entityId);
    const expected = probes.filter(probe => probe.entityId === row.part.entityId);
    assert.equal(check.pointChecks.length, expected.length);
    for (const [index, probe] of expected.entries())
      assert.equal(check.pointChecks[index][probe.expected], true, probe.role);
  }
  return { axialLength, endpoints, pointChecks: probes.length, actualPersistedSolidsChecked: true,
    ...(values.horizontalBranchReserve === 0 ? { zeroDepthBranchDoesNotCrossReceiverWall: true } : {}) };
}

export async function assertNativeMiterContact(native, resources) {
  const persisted = resources.manufacturingExecution.document.extensions['tubeDesigner.assemblyProcessNative'];
  const members = resources.display.document.items.filter(item => item.key.startsWith('outer_frame.top.'));
  const pair = members.flatMap((first, index) => members.slice(index + 1).map(second => ({ first, second })))
    .find(({ first, second }) => {
      const a = first.properties['assemblyFrame.member'], b = second.properties['assemblyFrame.member'];
      return ['start', 'end'].some(left => ['start', 'end'].some(right =>
        Math.hypot(...a[left].map((value, axis) => value - b[right][axis])) < 1e-6));
    });
  assert.ok(pair, 'Miter verification needs two actually adjacent top rails');
  assert.ok(persisted.stocks[pair.first.key] && persisted.stocks[pair.second.key],
    'Miter verification must inspect actual persisted manufacturing stocks');
  const result = await native.invoke('InspectNativeContact', {
    firstStockId: pair.first.key, secondStockId: pair.second.key,
  });
  assert.equal(result.scope, 'actual-persisted-processed-stock-world-matrices');
  assert.ok(result.first.valid && result.second.valid, 'Contact must use two valid persisted native BReps');
  assert.equal(result.first.solids, 1); assert.equal(result.second.solids, 1);
  assert.ok(result.contactAreaMm2 > 1, 'Actually processed miter faces must share material beyond a numerical edge touch');
  assert.ok(result.intersectionVolumeMm3 <= 1e-7, 'Actually processed miter rails must not penetrate each other');
  assert.ok(result.nearestDistanceMm <= 1e-6, 'Actually processed miter rails must touch');
  return result;
}
