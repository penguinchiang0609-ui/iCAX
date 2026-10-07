import assert from 'node:assert/strict';

export async function inspectManufacturedShapes(native, expectFrozen = false) {
  const snapshot = await native.invoke('InspectNativeGeometry');
  assert.ok(snapshot.geometryChecks.length > 0);
  assert.ok(snapshot.geometryChecks.every(check => check.valid && check.solids === 1
    && check.volume > 0 && check.resourceBytes > 0 && check.resourceHash));
  if (expectFrozen) {
    assert.ok(snapshot.canonicalStockResources.length > 0, 'Actual assembly must freeze canonical stock BReps');
    const stocks = new Map();
    for (const resource of snapshot.canonicalStockResources) {
      assert.ok(resource.resourceBytes > 0 && resource.resourceHash);
      const entry = stocks.get(resource.stockId) || {};
      entry[resource.kind] = resource; stocks.set(resource.stockId, entry);
    }
    assert.ok([...stocks.values()].every(stock => stock.baseGeometry && stock.processedGeometry));
    assert.ok([...stocks.values()].some(stock => stock.baseGeometry.resourceHash !== stock.processedGeometry.resourceHash),
      'At least one frame part must contain real machining beyond its initial tube');
  }
  return snapshot;
}

const geometryRoot = item => {
  const reference = item.geometry || item.representations.result;
  return typeof reference === 'string' ? reference
    : reference.instanceKey || (reference.placement ? `instance.${item.key}.result` : reference.resource);
};

function classifierReuseEvidence(checks) {
  const sampled = checks.filter(check => check.pointChecks?.length);
  const available = sampled.length > 0 && sampled.every(check =>
    Number.isInteger(check.classificationReuseControls));
  if (process.env.ICAX_NATIVE_CLASSIFIER_REUSE_REQUIRED === '1')
    assert.ok(available, 'The optimized bridge must prove fresh versus reused classification');
  if (!available) return { available: false };
  for (const check of sampled) {
    const states = new Set(check.pointChecks.map(point => point.inside ? 'inside'
      : point.outside ? 'outside' : point.boundary ? 'boundary' : 'unknown'));
    assert.equal(check.classificationReuseControls, states.size,
      `${check.geometryKey || check.key} fresh comparison for every encountered classification state`);
  }
  return { available: true, solids: sampled.length,
    points: sampled.reduce((sum, check) => sum + check.pointChecks.length, 0),
    controls: sampled.reduce((sum, check) => sum + check.classificationReuseControls, 0),
    everyEncounteredStateComparedWithFreshClassifier: true };
}

async function inspectNeutralChunks(native, model, keys, options = {}) {
  const checks = [];
  // Keep every root and every classification point, with bounded native work
  // per request for curved profiles and deep machining Boolean graphs.
  for (let first = 0; first < keys.length; first += 20) {
    const geometryKeys = keys.slice(first, first + 20), included = new Set(geometryKeys);
    const payload = { ...options, model, geometryKeys };
    if (options.points) payload.points = options.points.filter(point => included.has(point.geometryKey));
    const rows = (await native.invoke('InspectNeutralModel', payload)).geometryChecks;
    assert.deepEqual(rows.map(row => row.geometryKey), geometryKeys);
    if (options.manufacturingCoordinates) {
      assert.ok(rows.every(row => row.coordinateSpace === 'manufacturing-local'));
    }
    checks.push(...rows);
  }
  assert.equal(checks.length, keys.length);
  return checks;
}

async function compareWorldExecutionShapes(native, baseline, actualDocument, onStage) {
  const byKey = new Map(actualDocument.items.map(item => [item.key, item]));
  assert.deepEqual([...byKey.keys()].sort(), baseline.items.map(item => item.key).sort());
  const referenceKeys = baseline.items.map(geometryRoot);
  const actualKeys = baseline.items.map(item => geometryRoot(byKey.get(item.key)));
  const reference = await inspectNeutralChunks(native, baseline, referenceKeys);
  const current = await inspectNeutralChunks(native, actualDocument, actualKeys);
  const referencePoints = [], actualPoints = [], differences = [];
  const fractions = [0.017, 0.05, 0.25, 0.5, 0.75, 0.95, 0.983];
  baseline.items.forEach((item, index) => {
    const previous = reference[index], next = current[index];
    assert.ok(previous.valid && next.valid, `${item.key} world shape validity`);
    assert.equal(next.solids, previous.solids, `${item.key} world solid count`);
    if (Math.abs(next.volume - previous.volume) > Math.max(1e-4, previous.volume * 1e-7))
      differences.push({ key: item.key, kind: 'world-volume', current: next.volume, previous: previous.volume });
    next.bounds.forEach((coordinate, axis) => {
      if (Math.abs(coordinate - previous.bounds[axis]) > 1e-5)
        differences.push({ key: item.key, kind: 'world-bounds', axis, current: coordinate, previous: previous.bounds[axis] });
    });
    for (const x of fractions) for (const y of fractions) for (const z of fractions) {
      const point = [x, y, z].map((fraction, axis) => previous.bounds[axis * 2]
        + fraction * (previous.bounds[axis * 2 + 1] - previous.bounds[axis * 2]));
      referencePoints.push({ geometryKey: referenceKeys[index], point });
      actualPoints.push({ geometryKey: actualKeys[index], point });
    }
  });
  const previousPoints = await inspectNeutralChunks(native, baseline, referenceKeys, { points: referencePoints });
  const nextPoints = await inspectNeutralChunks(native, actualDocument, actualKeys, { points: actualPoints });
  const classificationReuseControls = { baseline: classifierReuseEvidence(previousPoints),
    actual: classifierReuseEvidence(nextPoints) };
  await onStage?.({ stage: 'world-point-results', baselineGeometry: reference, actualGeometry: current,
    baselinePointChecks: previousPoints, actualPointChecks: nextPoints, classificationReuseControls });
  let checkedPoints = 0, interiorPoints = 0;
  baseline.items.forEach((item, index) => {
    const previous = previousPoints[index].pointChecks, current = nextPoints[index].pointChecks;
    assert.equal(previous.length, current.length);
    previous.forEach((point, pointIndex) => {
      if (point.boundary || current[pointIndex].boundary) return;
      if (point.inside !== current[pointIndex].inside || point.outside !== current[pointIndex].outside)
        differences.push({ key: item.key, kind: 'world-classification', point: point.point,
          current: current[pointIndex], previous: point });
      checkedPoints += 1;
      if (point.inside) interiorPoints += 1;
    });
  });
  assert.ok(checkedPoints > 0 && interiorPoints > 0);
  if (differences.length) {
    const error = new Error(`World execution shape differs: ${JSON.stringify(differences[0])}`);
    error.comparison = { differences, current, previous: reference, checkedPoints, interiorPoints };
    throw error;
  }
  return { parts: baseline.items.length, volumesAndBoundsEqual: true, solidClassificationsEqual: true,
    checkedPoints, interiorPoints, coordinateSpace: 'product-world', classificationReuseControls };
}

async function inspectNativePointChunks(native, nativePoints, expectedGeometry, onStage, phase) {
  const entityIds = [...new Set(nativePoints.map(point => point.entityId))];
  const expectedById = new Map(expectedGeometry.map(check => [check.entityId, check]));
  const checks = [];
  for (let first = 0; first < entityIds.length; first += 5) {
    const requestedIds = entityIds.slice(first, first + 5), included = new Set(requestedIds);
    const points = nativePoints.filter(point => included.has(point.entityId));
    const reply = await native.invoke('InspectNativeGeometry', { entityIds: requestedIds, points });
    assert.deepEqual(reply.geometryChecks.map(check => check.entityId), requestedIds);
    for (const check of reply.geometryChecks) {
      assert.equal(check.pointChecks.length, nativePoints.filter(point => point.entityId === check.entityId).length);
      const expected = expectedById.get(check.entityId);
      for (const field of ['key', 'resourceHash', 'resourceBytes', 'valid', 'solids', 'volume', 'bounds']) {
        assert.deepEqual(check[field], expected[field], `Native point inspection changed ${check.entityId}: ${field}`);
      }
    }
    checks.push(...reply.geometryChecks);
    await onStage?.({ stage: `${phase}.native-points.${first / 5}`,
      request: { entityIds: requestedIds, points }, geometryChecks: reply.geometryChecks });
  }
  assert.deepEqual(checks.map(check => check.entityId), entityIds);
  return checks;
}

async function comparePersistedShapes(native, baseline, inspectionOptions = {}, onStage, phase = 'local') {
  const originalSource = baseline.extensions?.['tubeDesigner.assemblyProcessSource']
    || baseline.processes?.find(process => process.kind === 'assembly-plan')?.definition;
  if (originalSource) {
    const currentSource = (await native.invoke('InspectScriptResources')).manufacturingExecution.document
      .extensions['tubeDesigner.assemblyProcessSource'];
    const currentStocks = new Map(currentSource.stocks.map(stock => [stock.id, stock]));
    const stocks = new Map(originalSource.stocks.map(stock => [stock.id, stock]));
    baseline = structuredClone(baseline);
    for (const item of baseline.items) {
      const matrix = stocks.get(item.key).matrix, currentMatrix = currentStocks.get(item.key).matrix;
      matrix.forEach((value, index) => assert.ok(Math.abs(value - currentMatrix[index]) <= 1e-7,
        `${item.key} product placement matrix ${index}`));
      const reference = item.geometry || item.representations.result;
      let resource = typeof reference === 'string' ? reference : reference.resource;
      const nodes = baseline.resources || baseline.geometry;
      if (reference.placement) {
        // Preserve the original instance alias: v1 compiled records can reference it.
        const placed = reference.instanceKey || `instance.${item.key}.result`;
        nodes.push({ key: placed, operator: 'transform', inputs: [resource],
          arguments: { placement: reference.placement } });
        resource = placed;
      }
      // Native manufacturing BReps are stored in each material's own coordinates.
      // Preserve and separately compare the product pose, then invert it for the baseline solid.
      const inverse = { xAxis: matrix.slice(0, 3), yAxis: matrix.slice(4, 7), zAxis: matrix.slice(8, 11),
        origin: [0, 1, 2].map(column => -[0, 1, 2].reduce((sum, row) =>
          sum + matrix[row * 4 + column] * matrix[row * 4 + 3], 0)) };
      const local = `acceptance.local.${item.key}`;
      nodes.push({ key: local, operator: 'transform', inputs: [resource], arguments: { placement: inverse } });
      if (item.geometry) item.geometry = { resource: local };
      else item.representations.result = baseline.resources ? { resource: local } : local;
    }
  }
  const keys = baseline.items.map(geometryRoot);
  const expected = await inspectNeutralChunks(native, baseline, keys, inspectionOptions);
  const actual = (await native.invoke('InspectNativeGeometry')).geometryChecks;
  assert.equal(actual.length, baseline.items.length);
  const byKey = new Map(actual.map(check => [check.key, check]));
  const neutralPoints = [], nativePoints = [];
  const differences = [];
  // Include thin wall locations and positions near both ends, where end cuts lie.
  const fractions = [0.017, 0.05, 0.25, 0.5, 0.75, 0.95, 0.983];
  for (let index = 0; index < baseline.items.length; index += 1) {
    const item = baseline.items[index], previous = expected[index], current = byKey.get(item.key);
    assert.ok(current, `Manufactured BRep missing for ${item.key}`);
    assert.ok(previous.valid && current.valid, item.key);
    assert.equal(current.solids, previous.solids, item.key);
    if (Math.abs(current.volume - previous.volume) > Math.max(1e-4, previous.volume * 1e-7))
      differences.push({ key: item.key, kind: 'volume', current: current.volume, previous: previous.volume });
    current.bounds.forEach((coordinate, axis) => {
      if (Math.abs(coordinate - previous.bounds[axis]) > 1e-5)
        differences.push({ key: item.key, kind: 'bounds', axis, current: coordinate, previous: previous.bounds[axis] });
    });
    for (const x of fractions) for (const y of fractions) for (const z of fractions) {
      const point = [x, y, z].map((fraction, axis) => previous.bounds[axis * 2]
        + fraction * (previous.bounds[axis * 2 + 1] - previous.bounds[axis * 2]));
      neutralPoints.push({ geometryKey: keys[index], point });
      nativePoints.push({ entityId: current.entityId, point });
    }
  }
  const referenceChecks = await inspectNeutralChunks(native, baseline, keys,
    { ...inspectionOptions, points: neutralPoints });
  const referenceClassifierControls = classifierReuseEvidence(referenceChecks);
  await onStage?.({ stage: `${phase}.neutral-points`, baseline, inspectionOptions,
    nativeGeometry: actual, neutralPoints, nativePoints, referenceChecks, referenceClassifierControls });
  const currentChecks = await inspectNativePointChunks(native, nativePoints, actual, onStage, phase);
  const classificationReuseControls = { baseline: referenceClassifierControls,
    actual: classifierReuseEvidence(currentChecks) };
  const currentByKey = new Map(currentChecks.map(check => [check.key, check]));
  let checkedPoints = 0, interiorPoints = 0;
  baseline.items.forEach((item, index) => {
    const previous = referenceChecks[index].pointChecks, current = currentByKey.get(item.key).pointChecks;
    assert.equal(current.length, previous.length);
    previous.forEach((point, pointIndex) => {
      // Boundary classifications are sensitive to OCC tolerances; compare samples away from the surface.
      if (point.boundary || current[pointIndex].boundary) return;
      if (current[pointIndex].inside !== point.inside || current[pointIndex].outside !== point.outside)
        differences.push({ key: item.key, kind: 'classification', point: point.point,
          current: current[pointIndex], previous: point });
      checkedPoints += 1;
      if (point.inside) interiorPoints += 1;
    });
  });
  assert.ok(checkedPoints > 0 && interiorPoints > 0);
  if (differences.length) {
    const error = new Error(`Baseline manufactured shape differs: ${JSON.stringify(differences[0])}`);
    error.comparison = { differences, current: actual, previous: expected, checkedPoints, interiorPoints };
    throw error;
  }
  return { parts: actual.length, volumesAndBoundsEqual: true, checkedPoints, interiorPoints,
    solidClassificationsEqual: true, classificationReuseControls };
}

export async function compareBaselineShapes(native, baseline, { onStage } = {}) {
  const originalSource = baseline.extensions?.['tubeDesigner.assemblyProcessSource']
    || baseline.processes?.find(process => process.kind === 'assembly-plan')?.definition;
  if (originalSource) return comparePersistedShapes(native, baseline, {}, onStage);
  const actualDocument = (await native.invoke('InspectScriptResources')).manufacturingExecution.document;
  await onStage?.({ stage: 'world-inputs', baseline, actualDocument });
  const worldExecutionShapes = await compareWorldExecutionShapes(native, baseline, actualDocument, onStage);
  await onStage?.({ stage: 'world-complete', worldExecutionShapes });
  const actualItems = new Map(actualDocument.items.map(item => [item.key, item]));
  const propertiesFor = model => Object.fromEntries(model.items.map(item =>
    [geometryRoot(item), actualItems.get(item.key).properties]));
  const baselineOptions = { manufacturingCoordinates: true, manufacturingProperties: propertiesFor(baseline) };
  const actualOptions = { manufacturingCoordinates: true, manufacturingProperties: propertiesFor(actualDocument) };
  const baselineLocal = await comparePersistedShapes(native, baseline, baselineOptions, onStage, 'legacy-local');
  await onStage?.({ stage: 'legacy-local-complete', baselineLocal });
  const actualExecutionLocal = await comparePersistedShapes(native, actualDocument, actualOptions, onStage, 'actual-local');
  await onStage?.({ stage: 'actual-local-complete', actualExecutionLocal });
  return { ...baselineLocal, worldExecutionShapes, actualExecutionLocal,
    sharedPublicNormalizerAppliedToBoth: true, normalizationHintsFromActualSavedExecution: true };
}
