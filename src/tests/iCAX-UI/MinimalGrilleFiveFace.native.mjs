// Real product/scene SDO acceptance, without starting a browser or desktop app.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { createInterface } from 'node:readline';
import { delimiter, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { batchExcelColumnsFromTemplate } from '../../apps/tube-designer/webpage/designerActions.mjs';
import { minimalGrilleFiveFaceCases } from './MinimalGrilleFiveFaceCases.mjs';
import { doubleSupportAssertions, compareDoubleSupportGap, assertSolidFlatStocks, ordinaryDoubleGapAssertions, independentSupportLinkAssertions, singleSolidWeldAssertions } from './MinimalGrilleDoubleSupportAssertions.mjs';
import { inspectMixedMaterialGroups, inspectMixedOutsideDimensions, mixedMaterialOccupancyWitnesses } from './MinimalGrilleMixedMaterialAssertions.mjs';
import { invisibleContactPairs, assertInvisibleNative } from './InvisibleGrilleNativeChecks.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const sourceRoot = resolve(root, 'src');
const runtime = resolve(process.env.ICAX_GRILLE_NATIVE_RUNTIME || resolve(root, 'src/x64/Debug'));
const installation = resolve(process.env.ICAX_GRILLE_INSTALLATION || sourceRoot);
const output = resolve(process.env.ICAX_GRILLE_REPORT || resolve(root, 'output/tests/minimal-grille-five-face-20261110/results'));
const bridge = resolve(process.env.ICAX_GRILLE_NATIVE_BRIDGE || resolve(root,
  'output/tests/minimal-grille-five-face-20261110/native/MinimalGrilleFiveFaceBridge.exe'));
const python = process.env.ICAX_PYTHON || resolve(process.env.USERPROFILE,
  '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe');
const templatePackage = process.env.ICAX_GRILLE_TEMPLATE_PACKAGE || 'minimal_protective_grille';
const descriptorPath = resolve(installation, 'apps/tube-designer/templates/product', templatePackage, 'template.json');
const source = JSON.parse(readFileSync(descriptorPath, 'utf8'));
const defaults = Object.fromEntries(source.parameters.map(field => [field.key, field.defaultValue]));
const normalized = input => Object.fromEntries(source.parameters.map(field => [field.key,
  field.readOnly || input[field.key] == null ? field.defaultValue : input[field.key]]));
mkdirSync(output, { recursive: true });
mkdirSync(resolve(output, 'user-data'), { recursive: true });
mkdirSync(resolve(output, 'scratch'), { recursive: true });
assert(existsSync(bridge), `Build the native bridge first: ${bridge}`);
const env = { ...process.env, ICAX_AUTOMATION_USER_DATA: resolve(output, 'user-data'),
  TEMP: resolve(output, 'scratch'), TMP: resolve(output, 'scratch') };
const priorPath = Object.entries(env).find(([key]) => key.toLowerCase() === 'path')?.[1] || '';
for (const key of Object.keys(env)) if (key.toLowerCase() === 'path') delete env[key];
env.Path = runtime + delimiter + priorPath;
const child = spawn(bridge, [], { cwd: installation, env, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
const pending = new Map(), calls = [], stderr = [], stdout = [];
let sequence = 0, exited = false;
const report = { status: 'running', startedAt: new Date().toISOString(), installation, nativeRuntime: runtime,
  bridge, nativeBusinessResponsesMocked: false, standaloneCefEndToEnd: false, browserFilePickerCovered: false,
  descriptor: { id: source.id, version: source.version }, cases: [], batches: [], stages: [] };
function saveReport() {
  writeFileSync(resolve(output, 'results.json'), JSON.stringify(report, null, 2));
  writeFileSync(resolve(output, 'native-calls.json'), JSON.stringify(calls, null, 2));
  writeFileSync(resolve(output, 'native-stderr.log'), stderr.join(''));
  writeFileSync(resolve(output, 'native-stdout.log'), stdout.join('\n'));
}
function fail(error) {
  for (const entry of pending.values()) { clearTimeout(entry.timer); entry.reject(error); }
  pending.clear();
}
child.on('error', fail);
child.on('exit', (code, signal) => { exited = true; fail(new Error(`Native bridge exited ${code}/${signal}: ${stderr.join('').slice(-4000)}`)); });
child.stderr.on('data', bytes => stderr.push(bytes.toString()));
createInterface({ input: child.stdout }).on('line', line => {
  let response;
  try { response = JSON.parse(line); } catch { stdout.push(line); return; }
  const entry = pending.get(response.id);
  if (!entry) return;
  pending.delete(response.id); clearTimeout(entry.timer);
  const milliseconds = performance.now() - entry.start;
  calls.push({ request: entry.request, response, milliseconds });
  report.stages.push({ scope: entry.request.scope, method: entry.request.method, milliseconds, ok: response.ok });
  response.ok ? entry.resolve(response.result) : entry.reject(new Error(response.error));
});
function invoke(scope, method, payload = {}) {
  const request = { id: ++sequence, scope, method, payload };
  return new Promise((resolveRequest, reject) => {
    if (exited) return reject(new Error('Native bridge is closed'));
    const timer = setTimeout(() => { pending.delete(request.id); reject(new Error(`Native timeout: ${scope}.${method}`)); },
      ['Disassemble', 'DisassembleSelected', 'InspectAssemblyIntersections'].includes(method) ? 900000 : 180000);
    pending.set(request.id, { request, resolve: resolveRequest, reject, timer, start: performance.now() });
    child.stdin.write(JSON.stringify(request) + '\n');
  });
}
function fileEvidence(path) {
  const bytes = readFileSync(path), stats = statSync(path);
  return { path, bytes: bytes.length, modifiedAt: stats.mtime.toISOString(), sha256: createHash('sha256').update(bytes).digest('hex') };
}
function saveArtifact(name, value) { writeFileSync(resolve(output, name), JSON.stringify(value, null, 2)); }
function assertParameters(actual, expected, label) { assert.deepEqual(actual, expected, `${label}: host parameters changed or leaked internal values`); }
function assertPreview(preview, expected) {
  assert.equal(preview.templateId, source.id);
  assert.equal(preview.templateVersion, source.version);
  assertParameters(preview.parameters, expected, 'Preview');
  assert(preview.items.length > 0, 'Empty native preview');
  assert.equal(new Set(preview.items.map(item => item.stableKey || item.key)).size, preview.items.length, 'Duplicate members');
  for (const item of preview.items) {
    assert(item.geometry?.url, `${item.key}: missing native geometry`);
    assert.equal(item.transform.length, 16);
    assert(item.transform.every(Number.isFinite));
    assert(item.bounds.min.every(Number.isFinite) && item.bounds.max.every(Number.isFinite));
  }
}
function geometrySummary(preview) {
  return preview.items.map(item => ({ key: item.stableKey || item.key, transform: item.transform,
    bounds: item.bounds, role: item.properties?.['tubeDesigner.productRule']?.role,
    length: item.properties?.length })).sort((a, b) => a.key.localeCompare(b.key));
}
function assertSolids(checks, label) {
  assert(checks.length > 0, `${label}: no final solids`);
  for (const check of checks) {
    assert.equal(check.valid, true, `${label}/${check.key}: invalid BRep`);
    assert.equal(check.solids, 1, `${label}/${check.key}: cut into separate solids`);
    assert(check.volume > 0 && Number.isFinite(check.volume), `${label}/${check.key}: invalid volume`);
  }
}
function assertManufacturingValidation(part) {
  // This verifies the executed geometry, not a persisted whole-product fit certificate.
  assert.equal(part.status, 'AwaitingAssemblyValidation');
  const validation = part.properties?.['tubeDesigner.manufacturingValidation'];
  assert(validation, `${part.entityId}: missing native manufacturing validation`);
  assert.equal(validation.schema, 'icax.manufacturing-geometry-validation');
  assert.equal(validation.schemaVersion, 1);
  assert.equal(validation.scope, 'executed-product-parts');
  assert.equal(validation.status, 'verified');
  assert.equal(validation.assemblyFitCertified, false);
  assert(part.manufacturingGeometryResourceId && part.manufacturingGeometryResourceVersion > 0);
  assert.equal(validation.resourceId, part.manufacturingGeometryResourceId);
  assert.equal(validation.resourceVersion, part.manufacturingGeometryResourceVersion);
}
const near = (actual, expected, label, tolerance = 1e-5) =>
  assert(Math.abs(actual - expected) <= tolerance, `${label}: ${actual} != ${expected}`);
function physicalGeometryChecks(scripts, parts, parameters, options = {}) {
  const items = scripts.display.items;
  const members = items.filter(item => item.properties?.['assemblyFrame.member']);
  const get = item => item.properties['assemblyFrame.member'];
  const rule = item => item.properties['tubeDesigner.productRule']?.role;
  const frontRows = members.filter(item => rule(item)?.startsWith('inner.') && rule(item) !== 'inner.support_link' &&
    Math.abs(get(item).start[1]) < 1e-6 && Math.abs(get(item).end[1]) < 1e-6 &&
    Math.abs(get(item).end[0] - get(item).start[0]) > 1e-6);
  // A whole physical source stock must appear in exactly one part, even when
  // equal parts are grouped into a single drawing with quantity > 1.
  const sourceCounts = new Map();
  for (const part of parts) for (const sourceMember of part.properties['manufacturing.sourceMembers'] || []) {
    sourceCounts.set(sourceMember.itemKey, (sourceCounts.get(sourceMember.itemKey) || 0) + 1);
    if (part.properties['manufacturing.partKind'] === 'tube') {
      assert.equal(sourceMember.spans.length, 1, `${sourceMember.itemKey}: unnecessary stock segmentation`);
      assert.deepEqual(sourceMember.spans[0].sourceRange, [0, 1], `${sourceMember.itemKey}: incomplete stock range`);
    }
  }
  for (const item of members) assert.equal(sourceCounts.get(item.key), 1, `${item.key}: source stock duplicated or lost`);
  if (!['five_face','balcony_closure'].includes(parameters.layoutStyle)) return { physicalMembers: members.length, sourceStocksRemainWhole: true };
  const balcony = parameters.layoutStyle === 'balcony_closure';
  const expectedDrop = balcony ? parameters.balconyTopDrop : parameters.topDrop;
  const frontTop = members.find(item => item.key === 'frame.top.0001');
  const rearTop = members.find(item => item.key === 'frame.rear_top.0001');
  assert(frontTop && rearTop, 'Five-face front/rear reference rails missing');
  const rearY = get(rearTop).start[1];
  assert(rearY > 0, 'Projection must occupy real Y space');
  if (!options.mixedMaterials)
    near(get(rearTop).start[2] - get(frontTop).start[2], expectedDrop, 'Top drainage height difference');
  if (balcony) {
    assert(!members.some(item => ['frame.rear_left.0001','frame.rear_right.0001'].includes(item.key)),
      'Balcony must not add full-height rear uprights');
    const rearBottom = members.find(item => item.key === 'frame.rear_bottom.0001');
    assert(rearBottom);
    near(rearY - get(rearBottom).start[1], parameters.balconyTopDepth - parameters.balconyBottomDepth,
      'Independent upper and lower projection depths');
    assert.equal(members.filter(item => (options.mixedMaterials
      ? ['frame.return_left', 'frame.return_right'] : ['inner.return_left','inner.return_right']).includes(rule(item))).length, 4,
      'Balcony side boundaries must not acquire extra side infill');
  }
  const bottomRows = members.filter(item => rule(item)?.includes('bottom'));
  assert(bottomRows.length > 0);
  for (const item of bottomRows) near(get(item).end[2], get(item).start[2], `${item.key}: bottom must stay horizontal`);
  const roof = members.filter(item => rule(item) === 'inner.top_return');
  assert(roof.length > 0, 'Missing roof face members');
  for (const item of roof) {
    const a = get(item).start, b = get(item).end;
    assert(b[1] > a[1]);
    if (!options.mixedMaterials)
      near((b[2] - a[2]) / (b[1] - a[1]), expectedDrop / rearY, `${item.key}: drainage direction`);
    else if (expectedDrop === 0) near(b[2], a[2], `${item.key}: level roof`);
    else assert(b[2] > a[2], `${item.key}: roof drainage must slope toward the front`);
  }
  const supports = members.filter(item => rule(item) === 'frame.support');
  const facts = members[0].properties['tubeDesigner.minimalGrille'];
  assert.equal(supports.length, facts.supportPositions.length);
  const sideLeft = members.find(item => item.key === 'frame.left.0001');
  const sideRight = members.find(item => item.key === 'frame.right.0001');
  const centers = [get(sideLeft).start[0], ...supports.map(item => get(item).start[0]), get(sideRight).start[0]].sort((a, b) => a - b);
  for (let i = 1; i < centers.length; i++) assert(centers[i] - centers[i - 1] <= parameters.supportMaxSpacing + 1e-5,
    'Support maximum centre spacing exceeded');
  if (parameters.handleEnabled === 'no' && parameters.barLayoutMode === 'fixed_count') {
    const expectedSegments = parameters.supportStyle === 'double' ? facts.supportGroupCenters.length + 1
      : facts.continuousHorizontalRows === false ? facts.supportPositions.length + 1 : 1;
    assert.equal(frontRows.length, parameters.barCount * expectedSegments,
      'Physical horizontal stocks must match the chosen single/paired support structure');
  }
  return { physicalMembers: members.length, sourceStocksRemainWhole: true, frontRowCount: frontRows.length,
    supportCount: supports.length, supportCenters: centers,
    actualTopDrop: options.mixedMaterials ? undefined : get(rearTop).start[2] - get(frontTop).start[2],
    topCenterDifference: get(rearTop).start[2] - get(frontTop).start[2],
    mountingPlaneY: rearY, bottomHorizontal: true, nonOrthogonalRoofCount: expectedDrop ? roof.length : 0 };
}
async function supportSolidProbes(scripts, parts) {
  const members = scripts.display.items.filter(item => item.properties?.['assemblyFrame.member']);
  const get = item => item.properties['assemblyFrame.member'];
  const supports = members.filter(item => item.properties['tubeDesigner.productRule']?.role === 'frame.support');
  if (!supports.length) return { checked: false, reason: 'No intermediate supports in this case' };
  const rows = members.filter(item => item.properties['tubeDesigner.productRule']?.role?.startsWith('inner.') &&
    item.properties['tubeDesigner.productRule']?.role !== 'inner.support_link' &&
    Math.abs(get(item).start[1]) < 1e-6 && Math.abs(get(item).end[1]) < 1e-6 &&
    Math.abs(get(item).end[0] - get(item).start[0]) > 1e-6);
  const points = [], expectations = [];
  function addPoint(item, world, inside, purpose) {
    const part = parts.find(part => part.properties['manufacturing.sourceMembers']?.some(source => source.itemKey === item.key));
    assert(part, `${item.key}: native manufactured part missing`);
    const source = part.properties['manufacturing.sourceMembers'].find(source => source.itemKey === item.key);
    const t = source.spans[0].placement;
    const local = t.origin.map((origin, i) => origin + t.xAxis[i] * world[0] + t.yAxis[i] * world[1] + t.zAxis[i] * world[2]);
    points.push({ entityId: part.entityId, point: local });
    expectations.push({ entityId: part.entityId, point: local, inside, purpose, source: item.key, world });
  }
  for (const support of supports) for (const row of rows) {
    const x = get(support).start[0], z = get(row).start[2];
    if (!(get(row).start[0] < x && x < get(row).end[0])) continue;
    const c = support.properties['tubeDesigner.profile'], b = row.properties['tubeDesigner.profile'];
    const rowMaterialZ = b.hollow ? z + b.width / 2 - b.wallThickness / 2 : z;
    const receiverSkin = c.hollow ? c.wallThickness / 2 : Math.min(0.25, c.width / 8);
    for (const sign of [-1, 1]) {
      addPoint(support, [x + sign * (c.width / 2 - receiverSkin), 0, z], false, 'Receiver aperture crosses each opposing wall');
      addPoint(support, [x + sign * (c.width / 2 - receiverSkin), 0, z + b.width / 2 + 2], true,
        'Receiver retains wall material beside aperture');
      addPoint(row, [x + sign * (c.width / 2 + 2), 0, rowMaterialZ], true,
        'Horizontal stock retains material on both sides of support');
    }
    addPoint(row, [x, 0, rowMaterialZ], true,
      'Horizontal stock remains continuous through support');
  }
  const result = await invoke('inspection', 'InspectNativeGeometry', { entityIds: [...new Set(points.map(point => point.entityId))], points });
  for (const expected of expectations) {
    const match = result.geometryChecks.find(check => check.entityId === expected.entityId).pointChecks.find(check =>
      check.point.every((value, index) => Math.abs(value - expected.point[index]) < 1e-8));
    assert(match, 'Native point result missing');
    assert.equal(match.inside, expected.inside, `${expected.source}: ${expected.purpose}`);
    if (!expected.inside) assert.equal(match.outside, true, `${expected.source}: hole sample lies on boundary`);
  }
  return { checked: true, sampleCount: expectations.length, expectations, results: result.geometryChecks };
}
async function worldMaterialWitnesses(parts, witnesses) {
  const points = witnesses.map(witness => {
    const part = parts.find(part => part.properties['manufacturing.sourceMembers']?.some(source => source.itemKey === witness.sourceKey));
    assert(part, `Missing witness source ${witness.sourceKey}`);
    const source = part.properties['manufacturing.sourceMembers'].find(source => source.itemKey === witness.sourceKey);
    const t = source.spans[0].placement;
    return { entityId: part.entityId, point: t.origin.map((origin, i) => origin +
      t.xAxis[i] * witness.point[0] + t.yAxis[i] * witness.point[1] + t.zAxis[i] * witness.point[2]) };
  });
  const result = await invoke('inspection', 'InspectNativeGeometry', { entityIds: [...new Set(points.map(p => p.entityId))], points });
  const checks = witnesses.map((witness, index) => {
    const expected = points[index];
    const actual = result.geometryChecks.find(check => check.entityId === expected.entityId).pointChecks.find(check =>
      check.point.every((value, i) => Math.abs(value - expected.point[i]) < 1e-8));
    assert(actual);
    return { ...witness, worldPoint: witness.point, ...actual, expectedInside: witness.inside };
  });
  return { checks, passed: checks.every(check => check.inside === check.expectedInside) };
}
const configuration = process.env.ICAX_GRILLE_CASES
  ? JSON.parse(readFileSync(resolve(process.env.ICAX_GRILLE_CASES), 'utf8'))
  : process.env.ICAX_GRILLE_SMOKE === '1'
    ? { cases: [{ id: 'planar-default', parameters: { layoutStyle: 'planar' } }], batches: [] }
    : minimalGrilleFiveFaceCases();

async function runCase(test) {
  const start = performance.now(), parameters = normalized(test.parameters || {});
  if (test.actualDescriptorDefaults) {
    assert.deepEqual(Object.keys(test.parameters).sort(), ['layoutStyle']);
    assert.equal(parameters.width, 568); assert.equal(parameters.height, 1250);
    assert.equal(parameters.handleEnabled, 'yes'); assert.equal(parameters.supportStyle, 'double');
    if ('supportLinkCount' in parameters) assert.equal(parameters.supportLinkCount, 0);
    if ('barCenterSpacing' in parameters) {
      assert.equal(parameters.barLayoutMode, 'center_spacing');
      assert.equal(parameters.barCenterSpacing, 115);
    }
  }
  if (test.expectSolidDefaultProfiles) for (const [key, value] of Object.entries({
    frameProfileType: 'rect-bar', innerProfileType: 'rect-bar', supportProfileType: 'rect-bar',
    frameWidth: 10, frameDepth: 5, innerWidth: 10, innerDepth: 5, supportWidth: 20, supportDepth: 5,
  })) assert.equal(defaults[key], value, `Default flat-stock parameter ${key}`);
  await invoke('inspection', 'ResetScene');
  const preview = await invoke('scene', 'GenerateProductTemplatePreview', { templateId: source.id, parameters: test.parameters || {} });
  assertPreview(preview, parameters);
  if (test.expectedSupportPairs !== undefined)
    assert(preview.specificationAnnotations.some(annotation => annotation.parameter === 'supportPairClearGap'),
      'Double-support clear gap must be a native scene dimension');
  if (test.verifyIndependentSupportLinks) for (const key of ['supportLinkCount', 'supportLinkBottomOffset', 'supportLinkTopOffset'])
    assert(preview.specificationAnnotations.some(annotation => annotation.parameter === key), `Missing native short-link dimension ${key}`);
  for (const expected of test.annotationExpectations || []) {
    const actual = preview.specificationAnnotations.find(annotation => annotation.parameter === expected.parameter);
    assert(actual, `Missing actual native annotation ${expected.parameter}`);
    near(actual.start[2], expected.startZ, `${expected.parameter}: datum`);
    near(actual.end[2], expected.endZ, `${expected.parameter}: hole position`);
  }
  saveArtifact(`${test.id}-preview.json`, preview);
  const row = { id: test.id, parameters, previewMembers: preview.items.length, geometry: geometrySummary(preview), status: 'running' };
  if (test.compareGeometryTo) {
    const previous = report.cases.find(entry => entry.id === test.compareGeometryTo);
    assert(previous, 'Reference case must run first');
    assert.deepEqual(row.geometry, previous.geometry, `${test.id}: inactive fields changed planar geometry`);
    row.geometryUnchangedFrom = previous.id;
  }
  report.cases.push(row); saveReport();
  if (test.create || test.disassemble || test.reopen) {
    const generated = await invoke('scene', 'GeneratePreview', { templateId: source.id, ...parameters,
      instanceName: test.id, instanceQuantity: 1 });
    assertParameters(generated.tubeDesigner.product.parameters, parameters, 'Created product');
    assert.equal(generated.tubeDesigner.instances.length, 1);
    saveArtifact(`${test.id}-created.json`, generated);
    row.createdMembers = generated.tubeDesigner.members.length;
    if (test.disassemble || test.reopen) {
      const plan = await invoke('scene', 'GetProductManufacturingPlan', { templateId: source.id, parameters });
      assertParameters(plan.parameters, parameters, 'Manufacturing plan');
      saveArtifact(`${test.id}-plan.json`, plan);
      let disassembled;
      try { disassembled = await invoke('scene', 'Disassemble'); }
      catch (error) {
        if (!test.expectedDisassemblyRejection) throw error;
        assert.match(error.message, new RegExp(test.expectedDisassemblyRejection));
        const afterRejection = await invoke('scene', 'List');
        assert.equal(afterRejection.tubeDesigner.parts.length, 0, 'Rejected machining left a partial part list');
        assert.equal(afterRejection.tubeDesigner.generationRun.entityId, generated.tubeDesigner.generationRun.entityId,
          'Rejected machining changed the design generation');
        row.nativeRejected = true; row.rejection = error.message; row.status = 'passed';
        row.milliseconds = performance.now() - start; saveReport();
        console.log(JSON.stringify({ case: row.id, status: row.status, nativeRejected: true, rejection: row.rejection }));
        return;
      }
      assert(!test.expectedDisassemblyRejection, `${test.id}: unsafe assembly was accepted by the native product`);
      assertParameters(disassembled.tubeDesigner.product.parameters, parameters, 'Disassembled product');
      saveArtifact(`${test.id}-disassembled.json`, disassembled);
      const inspected = await invoke('inspection', 'InspectNativeGeometry');
      assertSolids(inspected.geometryChecks, test.id);
      saveArtifact(`${test.id}-solids.json`, inspected);
      row.partCount = disassembled.tubeDesigner.parts.length;
      row.solidCount = inspected.geometryChecks.length;
      if (test.captureTubeGeometry || test.compareTubeGeometryTo || test.expectTubeGeometryDifferentFrom) {
        const tubeIds = new Set(disassembled.tubeDesigner.parts.filter(part => part.properties['manufacturing.partKind'] === 'tube').map(part => part.entityId));
        row.tubeSolids = inspected.geometryChecks.filter(check => tubeIds.has(check.entityId))
          .map(({ key, volume, bounds, resourceHash }) => ({ key, volume, bounds, resourceHash })).sort((a,b) => a.key.localeCompare(b.key));
        if (test.compareTubeGeometryTo) {
          const previous = report.cases.find(entry => entry.id === test.compareTubeGeometryTo);
          assert(previous?.tubeSolids, 'Reference tube solids are missing');
          const comparison = await invoke('inspection', 'CompareTubeGeometry', { reference: previous.id });
          saveArtifact(`${test.id}-tube-frame-equivalence.json`, comparison);
          assert.equal(comparison.equivalent, true, 'Expected equivalent final tube geometry');
          row.tubeFrameEquivalence = comparison;
          row.tubeFrameUnchangedFrom = previous.id;
        }
        if (test.captureTubeGeometry) {
          row.capturedTubeGeometry = await invoke('inspection', 'CaptureTubeGeometry', { reference: test.id });
        }
        if (test.expectTubeGeometryDifferentFrom) {
          const comparison = await invoke('inspection', 'CompareTubeGeometry', { reference: test.expectTubeGeometryDifferentFrom });
          saveArtifact(`${test.id}-tube-frame-negative-control.json`, comparison);
          assert.equal(comparison.equivalent, false, 'Exact geometry comparison missed the expected geometry change');
          row.tubeGeometryNegativeControl = comparison;
        }
      }
      const scripts = await invoke('inspection', 'InspectScriptResources');
      assertParameters(scripts.generatedParameters, parameters, 'Persisted generation');
      assertParameters(scripts.manufacturingParameters, parameters, 'Persisted manufacturing');
      saveArtifact(`${test.id}-scripts.json`, scripts);
      if (test.expectSolidDefaultProfiles) row.solidDefaultMaterial = assertSolidFlatStocks(scripts, parameters);
      if (test.mixedMaterials) row.mixedMaterialGroups = inspectMixedMaterialGroups(scripts, parameters);
      if (test.verifyMixedMaterialOccupancy) {
        const material = await worldMaterialWitnesses(disassembled.tubeDesigner.parts, mixedMaterialOccupancyWitnesses(scripts));
        assert(material.passed, 'Actual solid/hollow material differs from the declared material groups');
        for (const check of material.checks) if (!check.expectedInside) assert.equal(check.outside, true);
        saveArtifact(`${test.id}-actual-material-occupancy.json`, material);
        row.actualMaterialOccupancy = { passed: true, sampleCount: material.checks.length };
      }
      if (test.materialWitnesses) {
        const witnesses = await worldMaterialWitnesses(disassembled.tubeDesigner.parts, test.materialWitnesses);
        saveArtifact(`${test.id}-material-witnesses.json`, witnesses);
        assert(witnesses.passed, `${test.id}: free corner material was removed or receiver mapping changed: ${JSON.stringify(witnesses.checks)}`);
        row.materialWitnesses = witnesses.checks;
      }
      row.physicalGeometry = physicalGeometryChecks(scripts, disassembled.tubeDesigner.parts, parameters, test);
      if (test.verifyDefaultDoubleGaps) {
        const checked = ordinaryDoubleGapAssertions(scripts, parameters);
        const material = await worldMaterialWitnesses(disassembled.tubeDesigner.parts, checked.witnesses);
        assert(material.passed && material.checks.every(check => check.outside), 'Default ordinary B stocks remain in the paired C opening');
        saveArtifact(`${test.id}-default-double-gap-material.json`, material);
        row.defaultDoubleGaps = { ...checked, sampleCount: material.checks.length };
        delete row.defaultDoubleGaps.witnesses;
      }
      if (test.expectedSupportPairs !== undefined) {
        const checked = doubleSupportAssertions(scripts, disassembled.tubeDesigner.parts, parameters, test.expectedSupportPairs);
        const gapMaterial = await worldMaterialWitnesses(disassembled.tubeDesigner.parts, checked.witnesses);
        assert(gapMaterial.passed, 'Paired C stock endpoints or required empty gap do not match the actual solid material');
        for (const check of gapMaterial.checks) if (!check.expectedInside)
          assert.equal(check.outside, true, 'Clear-gap sample lies on a C boundary');
        saveArtifact(`${test.id}-double-support-material.json`, gapMaterial);
        row.doubleSupports = { ...checked, gapMaterialSampleCount: gapMaterial.checks.length };
        delete row.doubleSupports.witnesses;
        if (test.compareSupportGapTo) {
          compareDoubleSupportGap(row.doubleSupports,
            report.cases.find(entry => entry.id === test.compareSupportGapTo)?.doubleSupports);
          row.doubleSupports.comparedGapTo = test.compareSupportGapTo;
        }
      }
      if (test.verifyIndependentSupportLinks) {
        const checked = independentSupportLinkAssertions(scripts, disassembled.tubeDesigner.parts, parameters);
        const material = await worldMaterialWitnesses(disassembled.tubeDesigner.parts, checked.witnesses);
        assert(material.passed, 'Independent short links lost material, crossed C, or merged with ordinary B');
        for (const check of material.checks) if (!check.expectedInside) assert.equal(check.outside, true);
        saveArtifact(`${test.id}-independent-link-material.json`, material);
        row.independentSupportLinks = { ...checked, sampleCount: material.checks.length };
        delete row.independentSupportLinks.witnesses;
      }
      if (test.verifySingleSolidWeld) {
        const checked = singleSolidWeldAssertions(scripts, disassembled.tubeDesigner.parts, parameters);
        const material = await worldMaterialWitnesses(disassembled.tubeDesigner.parts, checked.witnesses);
        assert(material.passed, 'Solid C was perforated or B failed to terminate at its weld surfaces');
        for (const check of material.checks) if (!check.expectedInside) assert.equal(check.outside, true);
        saveArtifact(`${test.id}-single-solid-weld-material.json`, material);
        row.singleSolidWeld = { ...checked, sampleCount: material.checks.length };
        delete row.singleSolidWeld.witnesses;
      }
      const supportProbes = parameters.supportStyle === 'double' || test.verifySingleSolidWeld
        ? { checked: false, reason: 'Double supports use separate B spans; actual endpoints, retained C material and empty gaps checked in double-support probes' }
        : await supportSolidProbes(scripts, disassembled.tubeDesigner.parts);
      saveArtifact(`${test.id}-support-probes.json`, supportProbes);
      row.supportProbes = { checked: supportProbes.checked, sampleCount: supportProbes.sampleCount, reason: supportProbes.reason };
      const drying = test.verifyDryingRod ? scripts.display.items[0].properties['tubeDesigner.minimalGrille'].dryingRod : null;
      const contactPairs = drying?.enabled ? [
        ...drying.topContacts.map(contact => ({ first: contact.armKey, second: contact.hostKey })),
        ...drying.armKeys.map(key => ({ first: drying.rodKey, second: key })),
      ] : test.verifyInvisibleGrille ? invisibleContactPairs(parameters) : [];
      const intersections = await invoke('inspection', 'InspectAssemblyIntersections', { contactPairs });
      saveArtifact(`${test.id}-assembly-intersections.json`, intersections);
      row.assemblyIntersections = { physicalStocks: intersections.physicalStocks, totalPairs: intersections.totalPairs,
        booleanCandidates: intersections.booleanCandidates.length, collisions: intersections.collisions,
        positiveVolumeTolerance: intersections.positiveVolumeTolerance,
        scope: intersections.scope || 'actual-finished-stocks' };
      assert.equal(intersections.collisions.length, 0,
        `${test.id}: assembled stocks overlap: ${JSON.stringify(intersections.collisions)}`);
      if (test.verifyInvisibleGrille) row.invisibleGrille = assertInvisibleNative(scripts, disassembled.tubeDesigner.parts, parameters, intersections);
      if (drying) {
        assert.equal(drying.enabled, parameters.dryingRodEnabled === 'yes');
        if (drying.enabled) {
          const keys = [drying.rodKey, ...drying.armKeys];
          assert.equal(keys.length, 3);
          assert.equal(intersections.contacts.length, 4);
          for (const contact of intersections.contacts) near(contact.distanceMm, 0, 'Drying joint must physically touch', 1e-6);
          row.dryingRod = { ...drying, contacts: intersections.contacts };
        }
      }
      if (test.compareOriginalTo) {
        const sourceKeys = scripts.display.items.filter(item => !item.key.startsWith('frame.drying.')).map(item => item.key);
        const comparison = await invoke('inspection', 'CompareTubeGeometry', { reference: test.compareOriginalTo, sourceKeys });
        assert.equal(comparison.equivalent, true, 'Adding/moving drying stocks changed the original framework');
        saveArtifact(`${test.id}-original-framework-comparison.json`, comparison);
        row.originalFramework = comparison;
      }
      if (test.mixedMaterials) row.mixedOutsideDimensions = inspectMixedOutsideDimensions(scripts, parameters, intersections);
      if (test.reopen) {
        const targetPath = resolve(output, `${test.id}.ictd`);
        const reopened = await invoke('inspection', 'SaveAndReopen', { targetPath });
        assert(reopened.savedAndReopened);
        assertParameters(reopened.tubeDesigner.product.parameters, parameters, 'Reopened product');
        const after = await invoke('inspection', 'InspectNativeGeometry');
        assert.deepEqual(after, inspected, `${test.id}: final BRep changed after save/reopen`);
        row.savedProject = fileEvidence(targetPath);
        row.savedReopenedSolidsUnchanged = true;
      }
      if (test.verifyParameterOwnership) {
        const productEntityId = generated.tubeDesigner.product.entityId;
        await assert.rejects(invoke('scene', 'UpdateProductParameters', { productEntityId,
          parameters: { ...parameters, layoutStyle: 'planar' } }), /创建后固定.*layoutStyle/);
        const assembly = await invoke('scene', 'UpdateProductParameters', { productEntityId,
          parameters: { ...parameters, returnJointMode: 'male_female_half_hole' } });
        assert.equal(assembly.tubeDesigner.product.modelOutdated, false, 'Assembly-only edit expired the display model');
        assert.equal(assembly.tubeDesigner.instances.find(item => item.entityId === productEntityId).partsOutdated, true);
        const material = await invoke('scene', 'UpdateProductParameters', { productEntityId,
          parameters: { ...parameters, supportWidth: parameters.supportWidth + 1 } });
        assert.equal(material.tubeDesigner.product.modelOutdated, true, 'Support material edit must expire the model');
        const dimension = await invoke('scene', 'UpdateProductParameters', { productEntityId,
          parameters: { ...parameters, projectionDepth: parameters.projectionDepth + 10 } });
        assert.equal(dimension.tubeDesigner.product.modelOutdated, true, 'Projection remains an editable scene dimension');
        const restored = await invoke('scene', 'UpdateProductParameters', { productEntityId, parameters });
        assertParameters(restored.tubeDesigner.product.parameters, parameters, 'Parameter ownership restore');
        assert.equal(restored.tubeDesigner.generationRun.entityId, generated.tubeDesigner.generationRun.entityId);
        row.parameterOwnership = { structureLockedAfterCreation: true, dimensionsEditable: true,
          materialEditsExpireModel: true, assemblyOnlyExpiresParts: true };
      }
      if (test.verifySupportPairOwnership) {
        const productEntityId = generated.tubeDesigner.product.entityId;
        await assert.rejects(invoke('scene', 'UpdateProductParameters', { productEntityId,
          parameters: { ...parameters, supportStyle: 'single' } }), /创建后固定.*supportStyle/);
        const changed = await invoke('scene', 'UpdateProductParameters', { productEntityId,
          parameters: { ...parameters, supportPairClearGap: parameters.supportPairClearGap + 10 } });
        assert.equal(changed.tubeDesigner.product.modelOutdated, true, 'Editable pair gap must expire the scene model');
        const restored = await invoke('scene', 'UpdateProductParameters', { productEntityId, parameters });
        assertParameters(restored.tubeDesigner.product.parameters, parameters, 'Double-support parameter ownership restore');
        row.supportPairOwnership = { structureLockedAfterCreation: true, clearGapEditable: true, gapEditExpiresModel: true };
      }
      if (test.verifyIndependentSupportLinks) {
        const productEntityId = generated.tubeDesigner.product.entityId;
        for (const key of ['supportLinkCount', 'supportLinkBottomOffset', 'supportLinkTopOffset']) {
          const changed = await invoke('scene', 'UpdateProductParameters', { productEntityId,
            parameters: { ...parameters, [key]: parameters[key] + (key === 'supportLinkCount' ? 1 : 5) } });
          assert.equal(changed.tubeDesigner.product.modelOutdated, true, `${key}: dimension edit must expire the actual scene model`);
          const restored = await invoke('scene', 'UpdateProductParameters', { productEntityId, parameters });
          assertParameters(restored.tubeDesigner.product.parameters, parameters, 'Independent link parameter restore');
        }
        row.independentSupportLinks.numericEditsExpireModel = true;
      }
      if (test.mixedMaterialEdits) row.mixedMaterialEdits = await runMixedMaterialEdits(test, parameters,
        generated.tubeDesigner.product.entityId, scripts);
    }
  }
  row.milliseconds = performance.now() - start; row.status = 'passed'; saveReport();
  console.log(JSON.stringify({ case: row.id, status: row.status, milliseconds: row.milliseconds, parts: row.partCount,
    physicalGeometry: row.physicalGeometry, supportProbes: row.supportProbes }));
}

async function runMixedMaterialEdits(test, initialParameters, productEntityId, initialScripts) {
  let currentParameters = initialParameters, currentScripts = initialScripts;
  const results = [];
  for (const [index, edit] of test.mixedMaterialEdits.entries()) {
    const start = performance.now(), reference = `${test.id}-before-material-${index}`;
    const before = inspectMixedMaterialGroups(currentScripts, currentParameters);
    await invoke('inspection', 'CaptureTubeGeometry', { reference });
    const next = { ...currentParameters, ...edit.changes };
    const updated = await invoke('scene', 'UpdateProductParameters', { productEntityId, parameters: next });
    assert.equal(updated.tubeDesigner.product.modelOutdated, true, 'Material edit must expire the real scene model');
    assertParameters(updated.tubeDesigner.product.parameters, next, 'Material edit input');
    const regenerated = await invoke('scene', 'GeneratePreview', { templateId: source.id, ...next, productEntityId });
    assert.equal(regenerated.tubeDesigner.instances.length, 1, 'Material regeneration must replace the existing instance');
    assert.equal(regenerated.tubeDesigner.product.entityId, productEntityId);
    assert.equal(regenerated.tubeDesigner.product.modelOutdated, false);
    assertParameters(regenerated.tubeDesigner.product.parameters, next, 'Material regeneration');
    const disassembled = await invoke('scene', 'Disassemble');
    assertParameters(disassembled.tubeDesigner.product.parameters, next, 'Material redisassembly');
    const solids = await invoke('inspection', 'InspectNativeGeometry');
    assertSolids(solids.geometryChecks, `${test.id}/${edit.group}`);
    const scripts = await invoke('inspection', 'InspectScriptResources');
    assertParameters(scripts.generatedParameters, next, 'Regenerated material parameters');
    assertParameters(scripts.manufacturingParameters, next, 'Remanufactured material parameters');
    const groups = inspectMixedMaterialGroups(scripts, next);
    const intersections = await invoke('inspection', 'InspectAssemblyIntersections');
    assert.equal(intersections.collisions.length, 0, 'Material-edited product has a real stock collision');
    const outside = inspectMixedOutsideDimensions(scripts, next, intersections);
    const unchanged = await invoke('inspection', 'CompareTubeGeometry', { reference, sourceKeys: before.preservedFrontKeys });
    assert.equal(unchanged.equivalent, true, 'Projection-material edit changed unaffected front A/B/C or independent links');
    const changed = await invoke('inspection', 'CompareTubeGeometry', { reference, sourceKeys: before.groups[edit.group] });
    assert.equal(changed.equivalent, false, 'Requested projection stock group did not change');
    assert(changed.checks.every(check => check.addedVolumeMm3 + check.removedVolumeMm3 > 1e-6),
      'Every stock in the edited material group must reflect its changed section');
    const path = resolve(output, `${test.id}-${edit.group}-edited.ictd`);
    const reopened = await invoke('inspection', 'SaveAndReopen', { targetPath: path });
    assertParameters(reopened.tubeDesigner.product.parameters, next, 'Reopened material edit');
    const after = await invoke('inspection', 'InspectNativeGeometry');
    assert.deepEqual(after, solids, 'Material-edited final solids changed after save/reopen');
    saveArtifact(`${test.id}-${edit.group}-material-edit.json`, { changes: edit.changes, parameters: next,
      groups, outside, unchanged, changed, intersections, solids });
    results.push({ group: edit.group, changes: edit.changes, modelExpiredThenRegenerated: true,
      partCount: disassembled.tubeDesigner.parts.length, groups: groups.groups, outside,
      unchangedFrontSourceCount: unchanged.physicalTubeCount, unchangedFrontMaximumDifferenceMm3: unchanged.maximumSymmetricDifferenceMm3,
      changedGroupSourceCount: changed.physicalTubeCount, changedGroupMaximumDifferenceMm3: changed.maximumSymmetricDifferenceMm3,
      totalPairs: intersections.totalPairs, collisions: intersections.collisions,
      savedReopenedSolidsUnchanged: true, savedProject: fileEvidence(path), milliseconds: performance.now() - start });
    currentParameters = next; currentScripts = scripts;
  }
  return results;
}

async function fillWorkbook(sourcePath, targetPath, values) {
  assert(existsSync(python), 'A real Python executable is required to fill workbook rows');
  const valuesPath = resolve(output, 'workbook-row-input.json');
  saveArtifact('workbook-row-input.json', values);
  const code = `import json, sys, zipfile, xml.etree.ElementTree as ET\nfrom xml.sax.saxutils import escape\nsource,target,values_path=sys.argv[1:]\nvalues=json.load(open(values_path,encoding='utf-8'))\nns={'s':'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}\nwith zipfile.ZipFile(source) as book:\n metadata=ET.fromstring(book.read('xl/worksheets/sheet2.xml'))\n contract=json.loads(''.join(n.text or '' for n in metadata.findall('.//s:c[@r="A1"]//s:t',ns)))\n rows=[]\n for number,value in enumerate(values,3):\n  cells=[]\n  for index,column in enumerate(contract['columns'],1):\n   if column['key'] not in value: continue\n   letters,n='',index\n   while n:\n    n,r=divmod(n-1,26); letters=chr(65+r)+letters\n   cells.append('<c r="'+letters+str(number)+'" t="inlineStr"><is><t>'+escape(str(value[column['key']]))+'</t></is></c>')\n  rows.append('<row r="'+str(number)+'">'+''.join(cells)+'</row>')\n with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED) as result:\n  for item in book.infolist():\n   data=book.read(item.filename)\n   if item.filename=='xl/worksheets/sheet1.xml': data=data.decode('utf-8').replace('</sheetData>',''.join(rows)+'</sheetData>').encode('utf-8')\n   result.writestr(item,data)\n print(json.dumps({'rows':len(rows),'columns':len(contract['columns'])}))\n`;
  const result = spawnSync(python, ['-c', code, sourcePath, targetPath, valuesPath], { encoding: 'utf8', windowsHide: true });
  assert.equal(result.status, 0, result.stderr || result.error?.message);
  return JSON.parse(result.stdout);
}
async function runBatch(batch, descriptor) {
  const count = typeof batch === 'number' ? batch : batch.count;
  const start = performance.now();
  await invoke('inspection', 'ResetScene');
  const columns = batchExcelColumnsFromTemplate(descriptor);
  const templatePath = resolve(output, `batch-${count}-template.xlsx`), workbookPath = resolve(output, `batch-${count}.xlsx`);
  await invoke('product', 'ExportBatchExcelTemplate', { templateId: source.id, targetPath: templatePath, columns });
  const values = Array.from({ length: count }, (_, i) => ({ __instanceName: `五面-${count}-${i + 1}`, __instanceQuantity: '1',
    layoutStyle: 'five_face', width: String(1200 + i * 20), height: String(900 + (i % 3) * 50),
    projectionDepth: '100', topDrop: '30', supportMaxSpacing: '800', handleEnabled: 'no',
    ...(batch.parameters || {}) }));
  const filled = await fillWorkbook(templatePath, workbookPath, values);
  const imported = await invoke('product', 'ReadBatchExcelImport', { sourcePath: workbookPath });
  assert.equal(imported.templateId, source.id);
  assert.equal(imported.rows.length, count);
  const creates = [];
  for (const [index, row] of imported.rows.entries()) {
    const before = performance.now();
    const created = await invoke('scene', 'GeneratePreview', { templateId: imported.templateId, ...row.parameters,
      instanceName: row.instanceName, instanceQuantity: row.instanceQuantity });
    assert.equal(created.tubeDesigner.instances.length, index + 1);
    const instance = created.tubeDesigner.instances.find(item => item.name === row.instanceName);
    assert(instance, `Workbook row ${index + 1} was not persisted`);
    assertParameters(instance.parameters, normalized(row.parameters), `Excel row ${index + 1}`);
    assert.equal(instance.quantity, 1);
    creates.push({ name: row.instanceName, entityId: instance.entityId, milliseconds: performance.now() - before });
  }
  let listed = await invoke('scene', 'List');
  assert.equal(listed.tubeDesigner.instances.length, count);
  saveArtifact(`batch-${count}-scene.json`, listed);
  const result = { count, workbook: fileEvidence(workbookPath), filled, createdInstances: creates, milliseconds: performance.now() - start,
    actualNativeSceneCreation: true, browserFilePickerCovered: false, standaloneCovered: false };
  let manufacturedBefore;
  let manufacturedIds;
  if (batch.disassemble) {
    const disassemblyStart = performance.now();
    await invoke('scene', 'DisassembleSelected', { productEntityIds: creates.map(item => item.entityId) });
    listed = await invoke('scene', 'List');
    assert.equal(listed.tubeDesigner.manufacturingGroups.length, count);
    for (const instance of listed.tubeDesigner.instances) {
      assert.equal(instance.hasDisassembly, true, `${instance.name}: no persisted disassembly`);
      assert.equal(instance.partsOutdated, false);
      const group = listed.tubeDesigner.manufacturingGroups.find(group => group.productEntityId === instance.entityId);
      assert(group && group.parts.length > 0);
      assert.equal(group.parts.length, instance.partCount);
      for (const part of group.parts) {
        assertManufacturingValidation(part);
      }
    }
    manufacturedIds = listed.tubeDesigner.manufacturingGroups.flatMap(group => group.parts.map(part => part.entityId));
    manufacturedBefore = await invoke('inspection', 'InspectNativeGeometry', { entityIds: manufacturedIds });
    assertSolids(manufacturedBefore.geometryChecks, `Batch ${count}`);
    saveArtifact(`batch-${count}-manufactured-solids.json`, manufacturedBefore);
    saveArtifact(`batch-${count}-manufactured-scene.json`, listed);
    result.manufacturing = { groups: count, uniquePartRecords: manufacturedIds.length,
      allFinalSolidsValidAndConnected: true, executedManufacturingGeometryVerified: true,
      partStatus: 'AwaitingAssemblyValidation', assemblyFitCertified: false,
      milliseconds: performance.now() - disassemblyStart };
  }
  if (batch.reopen) {
    const targetPath = resolve(output, `batch-${count}.ictd`);
    const reopened = await invoke('inspection', 'SaveAndReopen', { targetPath });
    assert.equal(reopened.tubeDesigner.instances.length, count);
    for (const instance of listed.tubeDesigner.instances) {
      const match = reopened.tubeDesigner.instances.find(item => item.entityId === instance.entityId);
      assert(match); assert.deepEqual(match.parameters, instance.parameters);
      if (batch.disassemble) {
        assert.equal(match.hasDisassembly, true); assert.equal(match.partCount, instance.partCount);
      }
    }
    if (batch.disassemble) {
      assert.equal(reopened.tubeDesigner.manufacturingGroups.length, count);
      for (const group of reopened.tubeDesigner.manufacturingGroups)
        for (const part of group.parts) assertManufacturingValidation(part);
      const after = await invoke('inspection', 'InspectNativeGeometry', { entityIds: manufacturedIds });
      assert.deepEqual(after, manufacturedBefore, 'Batch final BReps changed after save/reopen');
      result.manufacturing.savedReopenedSolidsUnchanged = true;
    }
    result.savedProject = fileEvidence(targetPath);
  }
  if (batch.export) {
    assert(batch.disassemble, 'Batch export requires actual disassembly');
    const exportStart = performance.now();
    const exported = await invoke('scene', 'ExportSelected', {
      targetDirectory: resolve(output, `batch-${count}-export`), partEntityIds: manufacturedIds });
    assert.equal(exported.draftOnly, false, 'Manufactured geometry export must not require draft bypass');
    assert.equal(exported.exportedGroups.length, count);
    assert.equal(exported.exportedCount, manufacturedIds.length);
    assert.equal(exported.exportedFiles.length, manufacturedIds.length);
    const files = exported.exportedFiles.map(path => {
      const evidence = fileEvidence(path);
      const step = readFileSync(path, 'utf8');
      assert(step.includes('ISO-10303-21;') && step.includes('END-ISO-10303-21;'), `Incomplete STEP export: ${path}`);
      return evidence;
    });
    const workbook = fileEvidence(exported.partListFile);
    saveArtifact(`batch-${count}-export.json`, { ...exported, fileEvidence: files, workbook });
    result.manufacturing.export = { draftOnly: false, groups: exported.exportedGroups.length,
      exportedCount: exported.exportedCount, partListFile: workbook, milliseconds: performance.now() - exportStart };
  }
  result.milliseconds = performance.now() - start;
  report.batches.push(result); saveReport();
  console.log(JSON.stringify({ batch: count, created: creates.length, savedReopened: Boolean(result.savedProject), milliseconds: result.milliseconds }));
}

try {
  report.templateFiles = readdirSync(dirname(descriptorPath)).filter(name => /\.(py|json)$/.test(name))
    .map(name => fileEvidence(resolve(dirname(descriptorPath), name)));
  const modules = (await invoke('inspection', 'GetRuntimeModules')).modules;
  report.modules = Object.fromEntries(Object.entries(modules).map(([name, path]) => {
    assert.equal(resolve(dirname(path)).toLowerCase(), runtime.toLowerCase(), `Unexpected DLL location: ${name}`);
    return [name, fileEvidence(path)];
  }));
  const descriptor = (await invoke('product', 'GetTemplateDescriptor', { templateId: source.id })).template;
  assert.equal(descriptor.version, source.version);
  assert.deepEqual(descriptor.parameters.map(field => field.key).sort(), source.parameters.map(field => field.key).sort());
  await assert.rejects(invoke('product', 'GeneratePreview', {}), /requires a scene/);
  for (const test of configuration.cases) await runCase(test);
  for (const batch of configuration.batches || []) await runBatch(batch, descriptor);
  for (const original of report.templateFiles)
    assert.equal(fileEvidence(original.path).sha256, original.sha256, `Template changed during native acceptance: ${original.path}`);
  report.status = 'passed';
} catch (error) {
  report.status = 'failed'; report.error = { message: error.message, stack: error.stack };
  const activeCase = report.cases.findLast(test => test.status === 'running');
  if (activeCase) { activeCase.status = 'failed'; activeCase.error = error.message; }
  process.exitCode = 1;
} finally {
  report.finishedAt = new Date().toISOString(); saveReport();
  const ended = new Promise(resolveExit => child.once('exit', resolveExit));
  child.stdin.end();
  if (!exited) await Promise.race([ended, new Promise(resolveDelay => setTimeout(() => { if (!exited) child.kill(); resolveDelay(); }, 3000))]);
}
console.log(JSON.stringify({ status: report.status, cases: report.cases.length, batches: report.batches.length,
  passed: report.cases.filter(test => test.status === 'passed').length,
  failed: report.cases.filter(test => test.status === 'failed').length,
  expectedNativeRejections: report.cases.filter(test => test.nativeRejected).length,
  error: report.error?.message, output }));
