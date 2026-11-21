import assert from 'node:assert/strict';

const near = (actual, expected, label, tolerance = 1e-6) =>
  assert(Math.abs(actual - expected) <= tolerance, `${label}: ${actual} != ${expected}`);
const vectorNear = (actual, expected, label) => expected.forEach((n, i) => near(actual[i], n, `${label}[${i}]`));

export function invisibleStockExpectations(parameters) {
  const p = parameters, wireCount = Math.floor((p.width - p.wireWidth) / p.wireCenterSpacing) + 1;
  const firstX = (p.width - (wireCount - 1) * p.wireCenterSpacing) / 2;
  const centers = [p.clampWidth / 2];
  for (let i = 0; i < p.clampCount; ++i) centers.push(p.clampCount === 1 ? p.clampBottomOffset
    : p.clampBottomOffset + i * (p.height - p.clampTopOffset - p.clampBottomOffset) / (p.clampCount - 1));
  centers.push(p.height - p.clampWidth / 2);
  const stocks = [];
  for (let i = 0; i < wireCount; ++i) {
    const x = firstX + i * p.wireCenterSpacing, r = p.wireWidth / 2;
    stocks.push({ key: `wire.vertical.${String(i + 1).padStart(4, '0')}`, profile: 'round-bar', role: 'wire',
      start: [x, 0, 0], end: [x, 0, p.height], length: p.height, width: p.wireWidth, depth: p.wireWidth,
      volume: Math.PI * r * r * p.height, bounds: [x-r, x+r, -r, r, 0, p.height] });
  }
  centers.forEach((z, i) => {
    const group = i === 0 ? 'bottom' : i === centers.length - 1 ? 'top' : `middle.${String(i).padStart(4, '0')}`;
    for (const [side, sign] of [['front', -1], ['back', 1]]) {
      const y = sign * (p.wireWidth + p.clampDepth) / 2;
      stocks.push({ key: `clamp.${group}.${side}`, profile: 'rect-bar', role: 'clamp',
        start: [0,y,z], end: [p.width,y,z], length: p.width, width: p.clampWidth, depth: p.clampDepth,
        volume: p.width * p.clampWidth * p.clampDepth,
        bounds: [0,p.width,y-p.clampDepth/2,y+p.clampDepth/2,z-p.clampWidth/2,z+p.clampWidth/2] });
    }
  });
  return { stocks, wireCount, centers, stripCount: 2 * centers.length };
}

export function invisibleContactPairs(parameters) {
  const { stocks } = invisibleStockExpectations(parameters);
  return stocks.filter(s => s.role === 'wire').flatMap(wire => stocks.filter(s => s.role === 'clamp')
    .map(strip => ({ first: wire.key, second: strip.key })));
}

export function assertInvisibleNative(scripts, parts, parameters, intersections) {
  const expected = invisibleStockExpectations(parameters), byKey = new Map(expected.stocks.map(s => [s.key, s]));
  assert.deepEqual(scripts.display.items.map(i => i.key), expected.stocks.map(s => s.key));
  for (const item of scripts.display.items) {
    const stock = byKey.get(item.key), p = item.properties, profile = p['tubeDesigner.profile'];
    assert.equal(profile.kind, stock.profile); assert.equal(profile.hollow, false); near(profile.wallThickness, 0, item.key);
    near(profile.width, stock.width, item.key); near(profile.depth, stock.depth, item.key);
    vectorNear(p['assemblyFrame.member'].start, stock.start, item.key);
    vectorNear(p['assemblyFrame.member'].end, stock.end, item.key);
    near(p.length, stock.length, item.key); assert.equal(p.quantity, 1);
    const facts = p['tubeDesigner.invisibleGrille'];
    assert.equal(facts.hasSideFrame, false); assert.equal(facts.fasteningVerified, false);
    assert.equal(facts.endAnchorageModeled, false); assert.equal(facts.continuousVerticalWires, true);
  }
  let quantity = 0; const sources = [];
  for (const part of parts) {
    const members = part.properties['manufacturing.sourceMembers'];
    assert.equal(part.quantity, members.length, 'Grouped quantities must equal physical stock sources');
    quantity += part.quantity;
    for (const member of members) {
      const stock = byKey.get(member.itemKey); assert(stock, 'Unexpected source stock'); sources.push(member.itemKey);
      assert.equal(member.spans.length, 1); const span = member.spans[0];
      assert.deepEqual(span.sourceRange, [0, 1]); near(span.startReserve, 0, stock.key); near(span.endReserve, 0, stock.key);
      vectorNear(span.start, stock.start, stock.key); vectorNear(span.end, stock.end, stock.key);
      near(part.length, stock.length, stock.key); near(part.properties['tubeDesigner.assemblyProcessStockLength'], stock.length, stock.key);
    }
  }
  assert.equal(quantity, expected.stocks.length);
  assert.deepEqual(sources.sort(), [...byKey.keys()].sort(), 'Each physical stock must be produced once');
  assert.equal(intersections.physicalStocks, expected.stocks.length);
  assert.equal(intersections.stockGeometry.length, expected.stocks.length);
  for (const actual of intersections.stockGeometry) {
    const stock = byKey.get(actual.key); assert(stock); assert.equal(actual.valid, true); assert.equal(actual.solids, 1);
    vectorNear(actual.bounds, stock.bounds, stock.key);
    near(actual.volume, stock.volume, stock.key + ' exact stock volume', Math.max(1e-6, stock.volume * 1e-9));
  }
  const expectedPairs = invisibleContactPairs(parameters), pairKeys = pairs => pairs.map(pair => `${pair.first}/${pair.second}`).sort();
  assert.deepEqual(pairKeys(intersections.contacts), pairKeys(expectedPairs));
  for (const contact of intersections.contacts) near(contact.distanceMm, 0, contact.first + '/' + contact.second);
  assert.equal(intersections.collisions.length, 0);
  return { wireCount: expected.wireCount, stripCount: expected.stripCount, clampGroupCenters: expected.centers,
    physicalStocks: expected.stocks.length, groupedPartCount: parts.length, quantity, fullLengthSources: sources.length,
    actualBrepStocksChecked: intersections.stockGeometry.length, exactTangencyPairs: intersections.contacts.length,
    maximumContactDistanceMm: Math.max(...intersections.contacts.map(c => c.distanceMm)),
    collisions: 0, fasteningVerified: false, endAnchorageModeled: false };
}
