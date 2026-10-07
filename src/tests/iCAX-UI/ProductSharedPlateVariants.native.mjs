// Check real product branches against their original design geometry, using one affected plate per case.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { createInterface } from 'node:readline';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repository = fileURLToPath(new URL('../../../', import.meta.url));
const binaryRoot = resolve(repository, 'src/x64/Debug');
const runtimeRoot = resolve(process.env.ICAX_NATIVE_PURE_RUNTIME_ROOT || binaryRoot);
const output = resolve(repository, 'output/tests/assembly-process');
const prepareOnly = process.argv.includes('--prepare-only');
const scenarios = [
  { name: 'glass-continuous', directory: 'modular_guardrail_glass-straight',
    changes: { pathMode: 'continuous' }, outline: true },
  { name: 'stair-bracket-equal-width', directory: 'straight_steel_staircase',
    changes: { bracketType: 'plate', bracketThickness: 30 }, thickness: 30, width: 30 },
  { name: 'stair-bracket-wider-than-width', directory: 'straight_steel_staircase',
    changes: { bracketType: 'plate', bracketThickness: 12, treadWidth: 10 }, thickness: 12, width: 10 },
];
const selected = scenarios.filter(scenario => !process.env.ICAX_SHARED_PLATE_CASE
  || process.env.ICAX_SHARED_PLATE_CASE === scenario.name);
assert.ok(selected.length > 0, 'Unknown ICAX_SHARED_PLATE_CASE');
mkdirSync(output, { recursive: true });

function connection() {
  const child = spawn(resolve(process.env.ICAX_NATIVE_PURE_BRIDGE
    || resolve(repository, 'tmp/security-window-frames-native/SecurityWindowFramesBridge.exe')), [], {
    cwd: runtimeRoot, windowsHide: true,
    env: { ...process.env, PATH: `${binaryRoot};${process.env.PATH}` }, stdio: ['pipe', 'pipe', 'pipe'],
  });
  const pending = new Map();
  let sequence = 0, stderr = '';
  const fail = error => {
    for (const request of pending.values()) { clearTimeout(request.timer); request.reject(error); }
    pending.clear();
  };
  child.stderr.on('data', data => { stderr += data; });
  child.on('error', fail);
  child.on('exit', code => { if (pending.size) fail(new Error(`Native exited ${code}: ${stderr}`)); });
  createInterface({ input: child.stdout }).on('line', line => {
    let response;
    try { response = JSON.parse(line); } catch { fail(new Error(line)); return; }
    const request = pending.get(response.id);
    if (!request) return;
    pending.delete(response.id); clearTimeout(request.timer);
    response.ok ? request.resolve(response.result) : request.reject(new Error(`${request.method}: ${response.error}`));
  });
  return {
    close() {
      return new Promise(resolveClosed => {
        if (child.exitCode !== null || child.signalCode !== null || !child.pid) return resolveClosed();
        child.once('exit', resolveClosed);
        child.stdin.end(); child.kill();
      });
    },
    invoke(method, payload = {}) {
      return new Promise((resolveRequest, reject) => {
        const id = ++sequence;
        const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Timeout ${method}: ${stderr}`)); }, 600000);
        pending.set(id, { resolve: resolveRequest, reject, timer });
        child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
      });
    },
  };
}

// The reference bypasses the new display adapter, while the current model uses both public entries.
// Shape models contain only the selected real item's transitive geometry dependencies.
const sourceReferencePython = String.raw`
from copy import deepcopy
import json, sys
from pathlib import Path
from unittest.mock import patch
repository, request_path, output_path = map(Path, sys.argv[1:])
sys.path.insert(0, str(repository / 'src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest'))
from WindowCatalogueTests import package
from icax_template_sdk import display_context, expand_resource_model
from icax_template_sdk.display import expand_display_model
from icax_template_sdk.manufacturing import compose_manufacturing_model
request = json.loads(request_path.read_text(encoding='utf-8'))
descriptor, _, template = package(request['directory'])
assert descriptor['id'] == request['templateId']
values = deepcopy(request['parameters'])
before = deepcopy(values)
with patch.object(template, 'display', side_effect=AssertionError('Reference cannot use the new public display')):
    original = template._generate_resource_document(values, display_context(template.__file__))
current = template.display(values)
manufacturing = template.manufacturing(values)
assert values == before and original['parameters'] == before
assert set(current) == {'schema','schemaVersion','coordinateSystem','lengthUnit','resources','items','roots','annotations'}
assert current['schemaVersion'] == 2
assert set(manufacturing) == {'schema','schemaVersion','connections','processes'}
assert manufacturing['schemaVersion'] == 4
compose_manufacturing_model(manufacturing, current)
if request['outline']:
    affected = [item for item in current['items'] if item.get('properties', {}).get('plate', {}).get('outline')]
else:
    affected = [item for item in current['items'] if item.get('properties', {}).get('categoryName') == '钢板支架']
assert affected
item = affected[0]
previous = next(entry for entry in original['items'] if entry['key'] == item['key'])
assert item['properties']['plate'] == previous['properties']['manufacturing.plate']

def minimal_shape(model, item_key, purpose):
    expanded = expand_resource_model(model)
    selected_item = deepcopy(next(entry for entry in expanded['items'] if entry['key'] == item_key))
    geometry_key = selected_item['representations'][purpose]
    assert isinstance(geometry_key, str)
    nodes = {node['key']: node for node in expanded['geometry']}
    required = set()
    def collect(key):
        assert key in nodes
        if key in required:
            return
        required.add(key)
        for dependency in nodes[key]['inputs']:
            collect(dependency)
    collect(geometry_key)
    result = deepcopy(expanded)
    result['geometry'] = [node for node in expanded['geometry'] if node['key'] in required]
    selected_item['representations'] = {'result': geometry_key}
    selected_item['children'] = []
    result['items'] = [selected_item]
    result['outputs'] = [{'key':'result','purpose':'result','items':[item_key],'properties':{}}]
    result['relationships'], result['tables'], result['diagnostics'], result['extensions'] = [], [], [], {}
    return {'model':result, 'geometryKey':geometry_key, 'geometryNodes':len(required)}

reference = {'templateId':request['templateId'],'parameters':before,'hostInputUnchanged':values == before,
    'itemKey':item['key'],'affectedItems':len(affected),'originalPlate':previous['properties']['manufacturing.plate'],
    'currentPlate':item['properties']['plate'],'display':current,'manufacturing':manufacturing,
    'originalShape':minimal_shape(original,item['key'],'result'),
    'currentShape':minimal_shape(expand_display_model(current),item['key'],'result')}
output_path.write_text(json.dumps(reference,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
print(json.dumps({'itemKey':item['key'],'affectedItems':len(affected),'inputsUnchanged':True}))
`;

function constructReference(scenario, descriptor, parameters) {
  const requestFile = resolve(output, `shared-plate-v4-${scenario.name}-host-input.json`);
  const referenceFile = resolve(output, `shared-plate-v4-${scenario.name}-source-reference.json`);
  writeFileSync(requestFile, JSON.stringify({ directory: scenario.directory, templateId: descriptor.id,
    parameters, outline: Boolean(scenario.outline) }, null, 2));
  const python = process.env.ICAX_TEST_PYTHON || resolve(process.env.USERPROFILE,
    '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe');
  const result = spawnSync(python, ['-c', sourceReferencePython, repository, requestFile, referenceFile], {
    cwd: repository, windowsHide: true, encoding: 'utf8', timeout: 300000, maxBuffer: 1024 * 1024,
    env: { ...process.env, PYTHONDONTWRITEBYTECODE: '1' },
  });
  assert.equal(result.status, 0, `Real product reference failed: ${result.error || result.stderr}`);
  const reference = JSON.parse(readFileSync(referenceFile, 'utf8'));
  assert.deepEqual(reference.parameters, parameters);
  assert.equal(reference.hostInputUnchanged, true);
  assert.deepEqual(reference.currentPlate, reference.originalPlate);
  return { ...reference, requestFile, referenceFile };
}

const near = (actual, expected, message, tolerance = 1e-5) => assert.ok(
  Math.abs(actual - expected) <= tolerance, `${message}: ${actual} vs ${expected}`);
const outlineArea = points => Math.abs(points.reduce((sum, a, index) => {
  const b = points[(index + 1) % points.length];
  return sum + a[0] * b[1] - b[0] * a[1];
}, 0)) / 2;

async function compareAffectedShape(native, reference, preview) {
  const inspect = (entry, points) => native.invoke('InspectNeutralModel', {
    model: entry.model, geometryKeys: [entry.geometryKey],
    ...(points ? { points: points.map(point => ({ geometryKey: entry.geometryKey, point })) } : {}),
  });
  const previous = (await inspect(reference.originalShape)).geometryChecks[0];
  const current = (await inspect(reference.currentShape)).geometryChecks[0];
  assert.ok(previous.valid && current.valid, 'The actual original/current plate solids must be valid');
  assert.equal(previous.solids, 1); assert.equal(current.solids, previous.solids);
  assert.ok(previous.volume > 0 && current.volume > 0);
  near(current.volume, previous.volume, 'Original/current world volume', Math.max(1e-4, previous.volume * 1e-7));
  current.bounds.forEach((value, axis) => near(value, previous.bounds[axis], `Original/current world bound ${axis}`));
  const previewItem = preview.items.find(item => item.key === reference.itemKey);
  assert.ok(previewItem, 'Actual host preview must contain the affected design plate');
  assert.ok(previewItem.geometry.url && Number.isFinite(previewItem.geometry.version));
  assert.equal(previewItem.bounds.min.length, 3); assert.equal(previewItem.bounds.max.length, 3);
  for (let axis = 0; axis < 3; axis += 1) {
    near(previewItem.bounds.min[axis], current.bounds[2 * axis], `Native preview world minimum ${axis}`);
    near(previewItem.bounds.max[axis], current.bounds[2 * axis + 1], `Native preview world maximum ${axis}`);
  }
  const area = reference.currentPlate.outline ? outlineArea(reference.currentPlate.outline)
    : reference.currentPlate.width * reference.currentPlate.height;
  near(reference.currentPlate.areaMm2, area, 'Actual outline/component area', Math.max(1e-8, area * 1e-8));
  near(current.volume, area * reference.currentPlate.thickness, 'Native display prism volume / actual plate area',
    Math.max(1e-4, current.volume * 1e-7));
  const fractions = [0.017, 0.05, 0.25, 0.5, 0.75, 0.95, 0.983], points = [];
  for (const x of fractions) for (const y of fractions) for (const z of fractions) {
    points.push([x, y, z].map((fraction, axis) => previous.bounds[2 * axis]
      + fraction * (previous.bounds[2 * axis + 1] - previous.bounds[2 * axis])));
  }
  const previousPoints = (await inspect(reference.originalShape, points)).geometryChecks[0].pointChecks;
  const currentPoints = (await inspect(reference.currentShape, points)).geometryChecks[0].pointChecks;
  assert.equal(previousPoints.length, points.length); assert.equal(currentPoints.length, points.length);
  let checkedPoints = 0, interiorPoints = 0;
  previousPoints.forEach((point, index) => {
    const next = currentPoints[index];
    if (point.boundary || next.boundary) return;
    assert.equal(next.inside, point.inside, `Plate world interior classification ${index}`);
    assert.equal(next.outside, point.outside, `Plate world exterior classification ${index}`);
    checkedPoints += 1;
    if (point.inside) interiorPoints += 1;
  });
  assert.ok(checkedPoints > 0 && interiorPoints > 0);
  return { parts: 1, itemKey: reference.itemKey, coordinateSpace: 'product-world', purpose: 'display',
    originalGeometryNodes: reference.originalShape.geometryNodes, currentGeometryNodes: reference.currentShape.geometryNodes,
    volume: current.volume, areaMm2: area, volumesEqual: true, boundsEqual: true, solidCountsEqual: true,
    nativePreviewBoundsEqual: true, solidClassificationsEqual: true, checkedPoints, interiorPoints };
}

const report = { runtimeRoot, representativeDesignPlatesOnly: true, cases: [], passed: false };
try {
  for (const scenario of selected) {
    const native = prepareOnly ? undefined : connection();
    const result = { name: scenario.name, passed: false };
    try {
      const sourceDescriptor = JSON.parse(readFileSync(resolve(repository,
        'src/apps/tube-designer/templates/product', scenario.directory, 'template.json'), 'utf8'));
      const descriptor = prepareOnly ? sourceDescriptor
        : (await native.invoke('GetTemplateDescriptor', { templateId: sourceDescriptor.id })).template;
      result.templateId = descriptor.id;
      const defaults = Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue]));
      const parameters = { ...defaults, ...scenario.changes }, frozen = structuredClone(parameters);
      const reference = constructReference(scenario, descriptor, parameters);
      if (scenario.outline) {
        assert.equal(reference.currentPlate.outline.length, 4);
        assert.ok(reference.currentPlate.areaMm2 < reference.currentPlate.width * reference.currentPlate.height,
          'A sloped panel has true polygon area, not its rectangular bounding area');
      } else {
        assert.equal(reference.currentPlate.thickness, scenario.thickness);
        assert.equal(reference.currentPlate.width, scenario.width);
        assert.ok(reference.currentPlate.thickness >= reference.currentPlate.width);
      }
      result.referenceFile = reference.referenceFile;
      result.hostInputFile = reference.requestFile;
      result.affectedDesignItems = reference.affectedItems;
      result.originalPlateFactsEqual = true;
      result.inputsUnchanged = true;
      if (prepareOnly) {
        result.prepared = true;
      } else {
        const preview = await native.invoke('GenerateProductTemplatePreview', { templateId: descriptor.id, parameters });
        assert.equal(preview.templateId, descriptor.id);
        assert.deepEqual(preview.parameters, frozen, 'Host normalized parameters must retain the entire actual input');
        assert.deepEqual(parameters, frozen);
        result.nativePreviewAccepted = true;
        result.hostParametersEqual = true;
        const request = { designModel: reference.display, manufacturingDefinition: reference.manufacturing };
        const frozenRequest = structuredClone(request);
        assert.deepEqual(Object.keys(request.manufacturingDefinition).sort(),
          ['schema', 'schemaVersion', 'connections', 'processes'].sort());
        const composed = await native.invoke('ValidateSharedManufacturing', request);
        assert.deepEqual(composed, { valid: true, designItemCount: reference.display.items.length,
          manufacturingPartCountKnown: false });
        assert.deepEqual(request, frozenRequest);
        result.manufacturingSharedReferences = true;
        result.manufacturingHasNoDuplicatedDesignObjects = true;
        result.shapeComparison = await compareAffectedShape(native, reference, preview);
        result.passed = true;
      }
      report.cases.push(result);
      if (!prepareOnly) writeFileSync(resolve(output, `product-shared-plate-variants-native-v4-${scenario.name}.json`),
        JSON.stringify(result, null, 2));
      console.log(JSON.stringify(result));
    } catch (error) {
      result.error = error.message;
      report.cases.push(result);
      if (!prepareOnly) writeFileSync(resolve(output, `product-shared-plate-variants-native-v4-${scenario.name}.json`),
        JSON.stringify(result, null, 2));
      throw error;
    } finally { await native?.close(); }
  }
  report.passed = !prepareOnly && report.cases.every(result => result.passed);
} finally {
  writeFileSync(resolve(output, prepareOnly ? 'product-shared-plate-variants-v4-prepared-inputs.json'
    : 'product-shared-plate-variants-native-v4.json'), JSON.stringify(report, null, 2));
}
