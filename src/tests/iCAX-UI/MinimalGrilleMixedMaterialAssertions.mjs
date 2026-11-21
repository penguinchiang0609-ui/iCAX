import assert from 'node:assert/strict';

const near = (a, b, label, tolerance = 1e-5) => assert(Math.abs(a - b) <= tolerance, `${label}: ${a} != ${b}`);
const member = item => item.properties['assemblyFrame.member'];
const profile = item => item.properties['tubeDesigner.profile'];
const verticalKeys = ['frame.rear_left.0001', 'frame.rear_right.0001'];
const horizontalKeys = ['frame.rear_top.0001', 'frame.rear_bottom.0001',
  'frame.return_left.bottom', 'frame.return_left.top', 'frame.return_right.bottom', 'frame.return_right.top'];

export function mixedMaterialGroup(item) {
  if (item.key.startsWith('frame.drying.')) return 'drying';
  if (verticalKeys.includes(item.key)) return 'projectionVertical';
  if (horizontalKeys.includes(item.key)) return 'projectionHorizontal';
  const role = item.properties['tubeDesigner.productRule']?.role;
  return role === 'frame.support' ? 'support' : role?.startsWith('frame.') ? 'frame' : 'inner';
}

export function inspectMixedMaterialGroups(scripts, parameters) {
  for (const group of ['frame', 'inner', 'support'])
    assert.equal(parameters[group + 'ProfileType'], 'rect-bar', `${group}: this acceptance keeps the confirmed front/internal flat stock`);
  const members = scripts.display.items.filter(item => item.properties?.['assemblyFrame.member']);
  const groups = Object.fromEntries(['frame', 'inner', 'support', 'projectionVertical', 'projectionHorizontal', 'drying'].map(key => [key, []]));
  for (const item of members) {
    const group = mixedMaterialGroup(item), p = profile(item), kind = parameters[group + 'ProfileType'];
    assert.equal(p.kind, kind, `${item.key}: wrong actual profile material`);
    assert.equal(p.hollow, kind !== 'rect-bar', `${item.key}: solid/hollow mismatch`);
    near(p.width, parameters[group + 'Width'], `${item.key}: actual section width`);
    near(p.depth, kind === 'round' ? parameters[group + 'Width'] : parameters[group + 'Depth'], `${item.key}: actual section depth`);
    if (p.hollow) near(p.wallThickness, parameters[group + 'WallThickness'], `${item.key}: actual wall thickness`);
    else near(p.wallThickness, 0, `${item.key}: solid flat stock must have no fabricated wall thickness`);
    if (kind === 'rect') near(p.cornerRadius, parameters[group + 'CornerRadius'], `${item.key}: actual corner radius`);
    assert.equal(p.contours.length, p.hollow ? 2 : 1, `${item.key}: actual section contour count`);
    assert.equal(item.properties.categoryKey, 'minimal-grille.' + group, `${item.key}: material editor category`);
    groups[group].push(item.key);
  }
  assert.deepEqual(groups.projectionHorizontal.slice().sort(), horizontalKeys.slice().sort());
  assert.deepEqual(groups.projectionVertical.slice().sort(), parameters.layoutStyle === 'five_face' ? verticalKeys.slice().sort() : []);
  const preservedFrontKeys = members.filter(item => !mixedMaterialGroup(item).startsWith('projection') &&
    Math.abs(member(item).start[1]) < 1e-6 && Math.abs(member(item).end[1]) < 1e-6).map(item => item.key);
  assert(preservedFrontKeys.length > 0);
  return { groups, physicalMemberCount: members.length, preservedFrontKeys,
    frontABCRemainSolid: members.filter(item => ['frame', 'support'].includes(mixedMaterialGroup(item)) || mixedMaterialGroup(item) === 'inner')
      .every(item => !profile(item).hollow),
    actualSections: members.map(item => ({ key: item.key, group: mixedMaterialGroup(item), kind: profile(item).kind,
      hollow: profile(item).hollow, width: profile(item).width, depth: profile(item).depth,
      wallThickness: profile(item).wallThickness, cornerRadius: profile(item).cornerRadius })) };
}

export function mixedMaterialOccupancyWitnesses(scripts) {
  const members = scripts.display.items.filter(item => item.properties?.['assemblyFrame.member']);
  const witnesses = [];
  for (const item of members) {
    const p = profile(item), axis = member(item), center = axis.start.map((a, i) => (a + axis.end[i]) / 2);
    witnesses.push({ sourceKey: item.key, point: center, inside: !p.hollow,
      purpose: p.hollow ? 'Actual projection tube has an empty bore at its middle' : 'Actual front/internal flat stock contains solid centre material' });
    if (p.hollow) {
      const x = axis.sectionFrame.xAxis, reach = p.width / 2 - p.wallThickness / 2;
      witnesses.push({ sourceKey: item.key, point: center.map((value, i) => value + x[i] * reach), inside: true,
        purpose: 'Actual hollow projection stock retains the declared tube wall' });
    }
  }
  return witnesses;
}

const envelope = entries => {
  assert(entries.length > 0);
  return [Math.min(...entries.map(e => e.bounds[0])), Math.max(...entries.map(e => e.bounds[1])),
    Math.min(...entries.map(e => e.bounds[2])), Math.max(...entries.map(e => e.bounds[3])),
    Math.min(...entries.map(e => e.bounds[4])), Math.max(...entries.map(e => e.bounds[5]))];
};

export function inspectMixedOutsideDimensions(scripts, parameters, intersections) {
  const members = scripts.display.items.filter(item => item.properties?.['assemblyFrame.member']);
  const measured = intersections.stockGeometry;
  assert(Array.isArray(measured), 'Build the mixed-material native bridge with actual world-space stock bounds');
  assert.equal(measured.length, members.length);
  const byKey = new Map(measured.map(stock => [stock.key, stock]));
  assert.equal(byKey.size, measured.length);
  for (const item of members) {
    const stock = byKey.get(item.key);
    assert(stock, `Missing actual finished stock ${item.key}`);
    assert.equal(stock.valid, true); assert.equal(stock.solids, 1);
  }
  const balcony = parameters.layoutStyle === 'balcony_closure';
  const topDepth = balcony ? parameters.balconyTopDepth : parameters.projectionDepth;
  const bottomDepth = balcony ? parameters.balconyBottomDepth : parameters.projectionDepth;
  const drop = balcony ? parameters.balconyTopDrop : parameters.topDrop;
  const frontOuterY = -parameters.frameDepth / 2;
  const topRearY = frontOuterY + topDepth, bottomRearY = frontOuterY + bottomDepth;
  const all = envelope(measured);
  for (const [index, expected, label] of [[0, 0, 'left outside'], [1, parameters.width, 'right outside'],
    [2, frontOuterY, 'front outside'], [3, Math.max(topRearY, bottomRearY), 'rear outside'],
    [4, 0, 'bottom outside'], [5, parameters.height + drop, 'rear roof outside']]) near(all[index], expected, label);
  const front = envelope(members.filter(item => mixedMaterialGroup(item) === 'frame').map(item => byKey.get(item.key)));
  near(front[4], 0, 'Actual front frame lower outside'); near(front[5], parameters.height, 'Actual front frame height');
  near(front[2], frontOuterY, 'Actual front material outside datum');
  const rearTop = byKey.get('frame.rear_top.0001'), rearBottom = byKey.get('frame.rear_bottom.0001');
  near(rearTop.bounds[3], topRearY, 'Actual upper depth outside');
  near(rearBottom.bounds[3], bottomRearY, 'Actual lower depth outside');
  near(rearTop.bounds[5] - front[5], drop, 'Actual roof outside drop');
  near(rearBottom.bounds[4], 0, 'Rear lower frame bottom is level');
  for (const key of ['frame.return_left.bottom', 'frame.return_right.bottom']) near(byKey.get(key).bounds[4], 0, `${key}: actual bottom outside`);
  if (!balcony) for (const key of verticalKeys) {
    near(byKey.get(key).bounds[4], 0, `${key}: rear post lower outside`);
    near(byKey.get(key).bounds[5], parameters.height + drop, `${key}: rear post upper outside`);
    near(byKey.get(key).bounds[3], topRearY, `${key}: rear post rear outside`);
  }
  const bottoms = members.filter(item => item.key.endsWith('.bottom') || item.properties['tubeDesigner.productRule']?.role?.includes('bottom'));
  for (const item of bottoms) near(member(item).start[2], member(item).end[2], `${item.key}: bottom stock must remain horizontal`);
  const roof = members.filter(item => item.key.endsWith('.top') || item.properties['tubeDesigner.productRule']?.role === 'inner.top_return');
  const slopes = roof.map(item => (member(item).end[2] - member(item).start[2]) / (member(item).end[1] - member(item).start[1]));
  assert(slopes.length > 2 && slopes.every(Number.isFinite));
  for (const slope of slopes) {
    near(slope, slopes[0], 'Roof inner flat bars and boundary tubes share the same drainage plane slope');
    if (drop === 0) near(slope, 0, 'Zero-drop roof'); else assert(slope > 0, 'Roof drains toward the front');
  }
  const actualRowCenterSpacing = {};
  if (parameters.barLayoutMode === 'center_spacing') {
    for (const role of ['inner.full', 'inner.upper', 'inner.lower']) {
      const rows = members.filter(item => item.properties['tubeDesigner.productRule']?.role === role &&
        Math.abs(member(item).start[1]) < 1e-6 && Math.abs(member(item).end[1]) < 1e-6);
      const centers = [...new Set(rows.map(item => {
        const bounds = byKey.get(item.key).bounds;
        return Number(((bounds[4] + bounds[5]) / 2).toFixed(8));
      }))].sort((a, b) => a - b);
      if (centers.length) {
        for (let i = 1; i < centers.length; i++) near(centers[i] - centers[i - 1], parameters.barCenterSpacing,
          `${role}: actual finished front B centre spacing`);
        actualRowCenterSpacing[role] = centers;
      }
    }
    assert(Object.keys(actualRowCenterSpacing).length > 0, 'No actual B rows found for centre-spacing acceptance');
  }
  return { actualWorldBounds: all, actualFrontBounds: front, frontOuterY, upperRearOutsideY: rearTop.bounds[3],
    lowerRearOutsideY: rearBottom.bounds[3], measuredUpperDepth: rearTop.bounds[3] - frontOuterY,
    measuredLowerDepth: rearBottom.bounds[3] - frontOuterY, measuredFrontHeight: front[5] - front[4],
    measuredRoofOutsideDrop: rearTop.bounds[5] - front[5], bottomHorizontal: true,
    roofSlope: slopes[0], actualFinishedStockBoundsChecked: measured.length, actualRowCenterSpacing };
}
