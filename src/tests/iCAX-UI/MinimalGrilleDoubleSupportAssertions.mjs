import assert from 'node:assert/strict';

const near = (a, b, label, tolerance = 1e-5) => assert(Math.abs(a - b) <= tolerance,
  `${label}: ${a} != ${b}`);
const member = item => item.properties['assemblyFrame.member'];
const profile = item => item.properties['tubeDesigner.profile'];
const role = item => item.properties['tubeDesigner.productRule']?.role;

export function assertSolidFlatStocks(scripts, parameters) {
  const members = scripts.display.items.filter(item => item.properties?.['assemblyFrame.member']);
  assert(members.length > 0);
  for (const item of members) {
    const group = role(item) === 'frame.support' ? 'support' : role(item)?.startsWith('frame.') ? 'frame' : 'inner';
    const p = profile(item);
    assert.equal(p.kind, 'rect-bar', `${item.key}: default material must be solid rectangular bar`);
    assert.equal(p.hollow, false, `${item.key}: default flat stock must not contain a tube lumen`);
    near(p.width, parameters[group + 'Width'], `${item.key}: material width`);
    near(p.depth, parameters[group + 'Depth'], `${item.key}: material thickness`);
  }
  return { actualSolidFlatStocks: members.length, allHollowFalse: true, kind: 'rect-bar' };
}

// The descriptor-default case includes handle openings and variable row counts.
// Check every actual ordinary B source against every pair without assuming a fixed-count grid.
export function ordinaryDoubleGapAssertions(scripts, parameters) {
  const members = scripts.display.items.filter(item => item.properties?.['assemblyFrame.member']);
  const supports = members.filter(item => role(item) === 'frame.support')
    .sort((a, b) => member(a).start[0] - member(b).start[0]);
  assert(supports.length > 0 && supports.length % 2 === 0);
  assert.equal(members.filter(item => role(item) === 'inner.support_link').length,
    (parameters.supportLinkCount || 0) * supports.length / 2, 'Default zero-count must create no independent middle links');
  const rows = members.filter(item => role(item)?.startsWith('inner.') && role(item) !== 'inner.support_link' &&
    Math.abs(member(item).start[1]) < 1e-6 && Math.abs(member(item).end[1]) < 1e-6 &&
    Math.abs(member(item).end[0] - member(item).start[0]) > 1e-6);
  const witnesses = [], pairs = [];
  for (let index = 0; index < supports.length; index += 2) {
    const left = supports[index], right = supports[index + 1];
    const lx = member(left).start[0], rx = member(right).start[0];
    const outerLeft = lx - profile(left).width / 2, outerRight = rx + profile(right).width / 2;
    const gapLeft = lx + profile(left).width / 2, gapRight = rx - profile(right).width / 2;
    near(gapRight - gapLeft, parameters.supportPairClearGap, 'Descriptor-default actual pair gap');
    for (const row of rows) {
      assert(member(row).end[0] <= outerLeft + 1e-5 || member(row).start[0] >= outerRight - 1e-5,
        `${row.key}: an ordinary B stock still spans the paired C opening`);
      const p = profile(row), z = member(row).start[2] + (p.hollow ? p.width / 2 - p.wallThickness / 2 : 0);
      for (const x of [gapLeft + 0.25, (gapLeft + gapRight) / 2, gapRight - 0.25])
        witnesses.push({ sourceKey: row.key, point: [x, 0, z], inside: false,
          purpose: 'Descriptor-default ordinary B stock is absent from the paired C opening' });
    }
    pairs.push({ leftKey: left.key, rightKey: right.key, gapLeft, gapRight });
  }
  return { pairCount: pairs.length, ordinaryHorizontalStocks: rows.length, pairs, witnesses };
}

export function independentSupportLinkAssertions(scripts, parts, parameters) {
  const members = scripts.display.items.filter(item => item.properties?.['assemblyFrame.member']);
  const facts = members[0].properties['tubeDesigner.minimalGrille'];
  const supports = members.filter(item => role(item) === 'frame.support')
    .sort((a, b) => member(a).start[0] - member(b).start[0]);
  const links = members.filter(item => role(item) === 'inner.support_link');
  const ordinary = members.filter(item => role(item)?.startsWith('inner.') && role(item) !== 'inner.support_link' &&
    Math.abs(member(item).start[1]) < 1e-6 && Math.abs(member(item).end[1]) < 1e-6 &&
    Math.abs(member(item).end[0] - member(item).start[0]) > 1e-6);
  const count = parameters.supportLinkCount, expectedZ = Array.from({ length: count }, (_, index) => count === 1
    ? parameters.supportLinkBottomOffset
    : parameters.supportLinkBottomOffset + index * (parameters.height - parameters.supportLinkTopOffset - parameters.supportLinkBottomOffset) / (count - 1));
  assert.equal(links.length, supports.length / 2 * count, 'Independent link physical stock count');
  assert.equal(facts.supportLinkCenters.length, count);
  for (const [index, z] of expectedZ.entries()) near(facts.supportLinkCenters[index], z, 'Independent link Z datum');
  const witnesses = [], groups = [];
  for (let index = 0; index < supports.length; index += 2) {
    const left = supports[index], right = supports[index + 1];
    const startX = member(left).start[0] + profile(left).width / 2;
    const endX = member(right).start[0] - profile(right).width / 2;
    const center = (startX + endX) / 2;
    const actual = links.filter(link => Math.abs((member(link).start[0] + member(link).end[0]) / 2 - center) < 1e-5)
      .sort((a, b) => member(a).start[2] - member(b).start[2]);
    assert.equal(actual.length, count);
    for (const [row, link] of actual.entries()) {
      const a = member(link).start, b = member(link).end, p = profile(link), z = expectedZ[row];
      near(a[0], startX, 'Short link starts on the inner face of left C');
      near(b[0], endX, 'Short link ends on the inner face of right C');
      near(a[1], 0, 'Short link front plane'); near(b[1], 0, 'Short link front plane');
      near(a[2], z, 'Short link independent lower/top positioning'); near(b[2], z, 'Short link horizontal');
      const owners = parts.flatMap(part => (part.properties['manufacturing.sourceMembers'] || []).filter(source => source.itemKey === link.key));
      assert.equal(owners.length, 1); assert.equal(owners[0].spans.length, 1);
      assert.deepEqual(owners[0].spans[0].sourceRange, [0, 1], 'Each independent short link remains its own complete stock');
      const materialZ = z + (p.hollow ? p.width / 2 - p.wallThickness / 2 : 0);
      for (const x of [startX + 0.25, center, endX - 0.25]) witnesses.push({ sourceKey: link.key,
        point: [x, 0, materialZ], inside: true, purpose: 'Independent short link has material throughout its own span' });
      for (const x of [startX - 0.25, endX + 0.25]) witnesses.push({ sourceKey: link.key,
        point: [x, 0, materialZ], inside: false, purpose: 'Independent link terminates at C and does not drill through it' });
      for (const post of [left, right]) {
        const section = profile(post), skin = section.hollow ? Math.min(0.25, section.wallThickness / 2) : 0.25;
        for (const sign of [-1, 1]) witnesses.push({ sourceKey: post.key,
          point: [member(post).start[0] + sign * (section.width / 2 - skin), 0, z], inside: true,
          purpose: 'C retains both sides at the short-link butt joint' });
      }
      for (const stock of ordinary) witnesses.push({ sourceKey: stock.key, point: [center, 0, materialZ], inside: false,
        purpose: 'The middle link is independent of every ordinary horizontal B source' });
    }
    groups.push({ leftKey: left.key, rightKey: right.key, linkKeys: actual.map(link => link.key), centers: expectedZ });
  }
  return { linksPerGroup: count, physicalLinkStocks: links.length, groupCount: groups.length,
    independentCenters: expectedZ, groups, ordinaryBRemainsSeparate: true, witnesses };
}

export function singleSolidWeldAssertions(scripts, parts, parameters) {
  assert.equal(parameters.supportStyle, 'single');
  assert.equal(parameters.supportProfileType, 'rect-bar');
  const members = scripts.display.items.filter(item => item.properties?.['assemblyFrame.member']);
  const supports = members.filter(item => role(item) === 'frame.support')
    .sort((a, b) => member(a).start[0] - member(b).start[0]);
  assert(supports.length > 0);
  assert.equal(members[0].properties['tubeDesigner.minimalGrille'].continuousHorizontalRows, false);
  const rows = members.filter(item => role(item)?.startsWith('inner.') && role(item) !== 'inner.support_link' &&
    Math.abs(member(item).start[1]) < 1e-6 && Math.abs(member(item).end[1]) < 1e-6 &&
    Math.abs(member(item).end[0] - member(item).start[0]) > 1e-6);
  const levels = [...new Set(rows.map(item => member(item).start[2]))].sort((a, b) => a - b);
  assert.equal(levels.length, parameters.barCount);
  assert.equal(rows.length, parameters.barCount * (supports.length + 1));
  const groups = levels.map(z => rows.filter(item => Math.abs(member(item).start[2] - z) < 1e-5)
    .sort((a, b) => member(a).start[0] - member(b).start[0]));
  const witnesses = [];
  for (const [index, post] of supports.entries()) {
    const x = member(post).start[0], half = profile(post).width / 2;
    assert.equal(profile(post).hollow, false);
    const sources = parts.flatMap(part => (part.properties['manufacturing.sourceMembers'] || []).filter(source => source.itemKey === post.key));
    assert.equal(sources.length, 1); assert.equal(sources[0].spans.length, 1);
    assert.deepEqual(sources[0].spans[0].sourceRange, [0, 1], 'Solid C must remain one whole physical stock');
    for (const group of groups) {
      assert.equal(group.length, supports.length + 1);
      const left = group[index], right = group[index + 1], z = member(left).start[2];
      near(member(left).end[0], x - half, 'Left B terminates on the left surface of solid C');
      near(member(right).start[0], x + half, 'Right B starts on the right surface of solid C');
      for (const [stock, insideX, outsideX] of [[left, x - half - 0.25, x - half + 0.25],
        [right, x + half + 0.25, x + half - 0.25]]) {
        witnesses.push({ sourceKey: stock.key, point: [insideX, 0, z], inside: true,
          purpose: 'Independent ordinary B span reaches its weld end at solid C' });
        witnesses.push({ sourceKey: stock.key, point: [outsideX, 0, z], inside: false,
          purpose: 'Ordinary B is cut back and cannot continue inside solid C' });
      }
      for (const keepX of [x - half + 0.25, x, x + half - 0.25]) witnesses.push({ sourceKey: post.key,
        point: [keepX, 0, z], inside: true, purpose: 'Solid C has intact material through both surfaces and its centre at every B weld' });
      for (const stock of group) witnesses.push({ sourceKey: stock.key, point: [x, 0, z], inside: false,
        purpose: 'Every B source is separate from the unperforated solid C' });
    }
  }
  for (const prefix of ['frame.top.', 'frame.bottom.'])
    assert.equal(members.filter(item => item.key.startsWith(prefix)).length, 1, 'Weld segmentation must not split the A boundary frames');
  return { supportCount: supports.length, horizontalRows: levels.length, independentHorizontalStocks: rows.length,
    solidSupportUnperforatedAndWhole: true, horizontalStocksCutAndWeldedAtC: true, witnesses };
}

// Read the actual persisted member placement and manufactured source maps.
// Pair assertions complement, rather than replace, solid point and Common checks.
export function doubleSupportAssertions(scripts, parts, parameters, expectedPairs) {
  assert.equal(parameters.supportStyle, 'double');
  const members = scripts.display.items.filter(item => item.properties?.['assemblyFrame.member']);
  const supports = members.filter(item => role(item) === 'frame.support')
    .sort((a, b) => member(a).start[0] - member(b).start[0]);
  assert.equal(supports.length, expectedPairs * 2, 'Double supports must contain two physical stocks per group');
  const facts = members[0].properties['tubeDesigner.minimalGrille'];
  assert.equal(facts.supportStyle, 'double');
  assert.equal(facts.continuousHorizontalRows, false, 'Paired supports must declare the requested segmented B structure');
  near(facts.supportPairClearGap, parameters.supportPairClearGap, 'Persisted pair clear gap');
  assert.equal(facts.supportPositions.length, supports.length);
  assert.equal(facts.supportGroupCenters.length, expectedPairs);
  assert(parts.every(part => !['plate', 'accessory', 'glass'].includes(part.properties['manufacturing.partKind'])),
    'Removed covering must not create plate/accessory parts');
  const rows = members.filter(item => role(item)?.startsWith('inner.') && role(item) !== 'inner.support_link' &&
    Math.abs(member(item).start[1]) < 1e-6 && Math.abs(member(item).end[1]) < 1e-6 &&
    Math.abs(member(item).end[0] - member(item).start[0]) > 1e-6);
  const rowLevels = [...new Set(rows.map(row => member(row).start[2]))].sort((a, b) => a - b);
  assert.equal(rowLevels.length, parameters.barCount, 'Horizontal row count changed');
  assert.equal(rows.length, parameters.barCount * (expectedPairs + 1), 'Each horizontal row must have one physical stock per clear span');
  const rowGroups = rowLevels.map(z => rows.filter(row => Math.abs(member(row).start[2] - z) < 1e-5)
    .sort((a, b) => member(a).start[0] - member(b).start[0]));
  for (const group of rowGroups) assert.equal(group.length, expectedPairs + 1);
  for (const prefix of ['frame.top.', 'frame.bottom.']) {
    const frames = members.filter(item => item.key.startsWith(prefix));
    assert.equal(frames.length, 1, 'Upper and lower A frames must each remain one full-width stock');
    assert(member(frames[0]).start[0] < member(supports[0]).start[0]);
    assert(member(frames[0]).end[0] > member(supports.at(-1)).end[0]);
  }
  const pairs = [], witnesses = [];
  for (let index = 0; index < supports.length; index += 2) {
    const left = supports[index], right = supports[index + 1];
    const a = member(left), b = member(right), pa = profile(left), pb = profile(right);
    const center = (a.start[0] + b.start[0]) / 2;
    const clearGap = b.start[0] - a.start[0] - (pa.width + pb.width) / 2;
    near(clearGap, parameters.supportPairClearGap, 'Actual clear gap between paired C surfaces');
    near(center, facts.supportGroupCenters[index / 2], 'Declared group centre');
    near(a.start[1], b.start[1], 'Columns must be side by side in the same front plane');
    near(a.end[1], b.end[1], 'Column front plane at upper end');
    near(a.start[2], b.start[2], 'Paired column lower ends');
    near(a.end[2], b.end[2], 'Paired column upper ends');
    for (const post of [left, right]) {
      assert.equal(profile(post).kind, parameters.supportProfileType, `${post.key}: actual C profile kind`);
      const owners = parts.flatMap(part => (part.properties['manufacturing.sourceMembers'] || [])
        .filter(source => source.itemKey === post.key).map(source => ({ part, source })));
      assert.equal(owners.length, 1, `${post.key}: duplicated or lost C stock`);
      assert.equal(owners[0].source.spans.length, 1, `${post.key}: C stock was split`);
      assert.deepEqual(owners[0].source.spans[0].sourceRange, [0, 1]);
      for (const roof of members.filter(item => ['inner.top_return', 'inner.bottom_return'].includes(role(item)))) {
        const separation = Math.abs(member(roof).start[0] - member(post).start[0]);
        assert(separation >= (profile(post).width + profile(roof).depth) / 2 - 1e-5,
          `${roof.key}: return enters the shoulder of actual support ${post.key}`);
      }
    }
    const outerLeft = a.start[0] - pa.width / 2, outerRight = b.start[0] + pb.width / 2;
    const gapLeft = a.start[0] + pa.width / 2, gapRight = b.start[0] - pb.width / 2;
    for (const group of rowGroups) {
      const leftSpan = group[index / 2], rightSpan = group[index / 2 + 1];
      near(member(leftSpan).end[0], outerLeft, 'Left B span ends at the outer face of left C');
      near(member(rightSpan).start[0], outerRight, 'Right B span starts at the outer face of right C');
      const p = profile(leftSpan), z = member(leftSpan).start[2] + (p.hollow ? p.width / 2 - p.wallThickness / 2 : 0);
      const delta = 0.25;
      for (const [stock, insideX, outsideX] of [
        [leftSpan, outerLeft - delta, outerLeft + delta],
        [rightSpan, outerRight + delta, outerRight - delta],
      ]) {
        witnesses.push({ sourceKey: stock.key, point: [insideX, 0, z], inside: true,
          purpose: 'Actual B material reaches its intended butt-weld endpoint' });
        witnesses.push({ sourceKey: stock.key, point: [outsideX, 0, z], inside: false,
          purpose: 'B does not continue through either C post' });
      }
      for (const gapX of [gapLeft + Math.min(0.25, clearGap / 4), center, gapRight - Math.min(0.25, clearGap / 4)]) {
        for (const stock of group) witnesses.push({ sourceKey: stock.key, point: [gapX, 0, z], inside: false,
          purpose: 'Ordinary horizontal B spans do not continue into the gap; independent short links are checked separately' });
      }
      for (const post of [left, right]) {
        const section = profile(post), skin = section.hollow ? Math.min(0.25, section.wallThickness / 2) : 0.25;
        for (const sign of [-1, 1]) witnesses.push({ sourceKey: post.key,
          point: [member(post).start[0] + sign * (section.width / 2 - skin), 0, member(leftSpan).start[2]], inside: true,
          purpose: 'Both sides of C retain material; double-support joints do not drill through C' });
      }
    }
    pairs.push({ leftKey: left.key, rightKey: right.key, leftX: a.start[0], rightX: b.start[0], center, clearGap });
  }
  const leftFrame = members.find(item => item.key === 'frame.left.0001');
  const rightFrame = members.find(item => item.key === 'frame.right.0001');
  const anchors = [member(leftFrame).start[0], ...pairs.map(pair => pair.center), member(rightFrame).start[0]];
  for (let index = 1; index < anchors.length; index++)
    assert(anchors[index] - anchors[index - 1] <= parameters.supportMaxSpacing + 1e-5,
      'Maximum spacing applies to group centres including the side frames');
  return { pairCount: pairs.length, supportCount: supports.length, pairs, horizontalRowCount: rowLevels.length,
    horizontalPhysicalSegments: rows.length, segmentsPerRow: expectedPairs + 1,
    horizontalStocksTerminateOutsidePairedColumns: true, gapHasNoOrdinaryBStock: true,
    upperAndLowerFramesRemainWhole: true, madeStockOnly: true, witnesses };
}

export function compareDoubleSupportGap(actual, previous) {
  assert(previous, 'Reference double support case must run first');
  assert.equal(actual.pairCount, previous.pairCount);
  let changed = false;
  for (const [index, pair] of actual.pairs.entries()) {
    const before = previous.pairs[index];
    near(pair.center, before.center, 'Clear-gap edit keeps the group centre');
    const delta = pair.clearGap - before.clearGap;
    near(pair.leftX - before.leftX, -delta / 2, 'Left column follows clear-gap edit');
    near(pair.rightX - before.rightX, delta / 2, 'Right column follows clear-gap edit');
    changed ||= Math.abs(delta) > 1e-5;
  }
  assert(changed, 'Gap-variation case must actually change the physical spacing');
}
