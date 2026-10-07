// Exercise product-owned choices through their actual UI mapping and native descriptor validation.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawn, spawnSync } from 'node:child_process';
import { createInterface } from 'node:readline';
import { copyFileSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { isDeepStrictEqual } from 'node:util';
import {
  applyProductControlChoice, productControlFields, productControlValue,
} from '../../apps/tube-designer/webpage/productControls.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const runtime = resolve(process.env.ICAX_PRODUCT_CONNECTIONS_RUNTIME || resolve(root, 'src/x64/Debug'));
const deployed = process.env.ICAX_PRODUCT_CONNECTIONS_DEPLOYED === '1';
const output = resolve(process.env.ICAX_PRODUCT_CONNECTIONS_OUTPUT
  || resolve(root, `output/tests/product-connection-choices/${deployed ? 'deployed' : 'source'}-native.json`));
const bridgeDirectory = resolve(dirname(output), 'connection-choices-bridge');
mkdirSync(bridgeDirectory, { recursive: true });
// A DLL beside this executable would defeat PATH selection. Never stage old product DLLs.
assert.deepEqual(readdirSync(bridgeDirectory).filter(name => /\.dll$/i.test(name)), [],
  'The isolated bridge directory must contain no DLLs');
const bridge = resolve(bridgeDirectory, 'SecurityWindowFramesBridge.exe');
copyFileSync(resolve(process.env.ICAX_PRODUCT_CONNECTIONS_BRIDGE
  || resolve(root, 'output/tests/product-tab-connections/bridge-build/SecurityWindowFramesBridge.exe')), bridge);
const templates = deployed ? resolve(runtime, 'apps/tube-designer/templates')
  : resolve(root, 'src/apps/tube-designer/templates');
const descriptorPath = resolve(templates, 'product/single_face_security_window/template.json');
const read = path => JSON.parse(readFileSync(path, 'utf8'));
const hash = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const report = { passed: false, deployed, runtime, templates, bridge, bridgeSha256: hash(bridge), cases: [] };
const save = () => writeFileSync(output, JSON.stringify(report, null, 2) + '\n');
save();

function connection() {
  const child = spawn(bridge, [], { cwd: deployed ? runtime : root, windowsHide: true,
    env: { ...process.env, PATH: `${runtime};${process.env.PATH || ''}` }, stdio: ['pipe', 'pipe', 'pipe'] });
  let sequence = 0, stderr = '', ended = false;
  const pending = new Map();
  const fail = error => {
    for (const request of pending.values()) { clearTimeout(request.timer); request.reject(error); }
    pending.clear();
  };
  child.stderr.on('data', data => { stderr = (stderr + data).slice(-10000); });
  child.on('error', fail);
  child.on('exit', code => { ended = true; fail(new Error(`Native exited ${code}: ${stderr}`)); });
  createInterface({ input: child.stdout }).on('line', line => {
    let response;
    try { response = JSON.parse(line); } catch { return fail(new Error(`Invalid native response: ${line.slice(0, 500)}`)); }
    const request = pending.get(response.id);
    if (!request) return;
    pending.delete(response.id); clearTimeout(request.timer);
    response.ok ? request.resolve(response.result)
      : request.reject(new Error(`${request.method}: ${response.error}`));
  });
  return {
    close() { child.stdin.end(); child.kill(); },
    modules() {
      const observed = spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command',
        `(Get-Process -Id ${child.pid}).Modules | Where-Object { $_.ModuleName -in @('TemplateRuntime.dll','TubeDesigner.dll') } | Select-Object ModuleName,FileName | ConvertTo-Json -Compress`],
      { windowsHide: true, encoding: 'utf8', timeout: 30000 });
      assert.equal(observed.status, 0, observed.stderr || 'Cannot inspect native modules');
      const parsed = JSON.parse(observed.stdout.trim());
      const modules = (Array.isArray(parsed) ? parsed : [parsed]).map(item => ({ name: item.ModuleName,
        path: item.FileName, sha256: hash(item.FileName) }));
      assert.ok(modules.some(item => item.name === 'TemplateRuntime.dll'));
      assert.ok(modules.some(item => item.name === 'TubeDesigner.dll'));
      assert.ok(modules.every(item => item.path.toLowerCase().startsWith(runtime.toLowerCase() + '\\')),
        'All observed product DLLs must load from the current deployed runtime');
      return modules;
    },
    invoke(method, payload = {}) {
      return new Promise((resolveRequest, reject) => {
        if (ended) return reject(new Error(`Native already exited: ${stderr}`));
        const id = ++sequence;
        const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Timeout ${method}: ${stderr}`)); }, 300000);
        pending.set(id, { method, resolve: resolveRequest, reject, timer });
        child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
      });
    },
  };
}

function loadTool(id) {
  const packageRoot = resolve(templates, 'mold', id), manifest = resolve(packageRoot, 'tool.json');
  const descriptor = read(manifest);
  const member = resolve(packageRoot, descriptor.kind === 'programmatic' ? 'tool.py' : 'geometry.json');
  const digest = createHash('sha256').update(readFileSync(manifest)).update(Buffer.from([0]))
    .update(readFileSync(member)).update(readFileSync(resolve(templates, '_shared/section_geometry.py'))).digest('hex');
  return { ...descriptor, libraryScope: 'system', digest,
    defaultParameters: Object.fromEntries((descriptor.parameters || []).map(field => [field.key, field.defaultValue])) };
}

const native = connection();
try {
  const templateId = 'single-face-security-window';
  const template = (await native.invoke('GetTemplateDescriptor', { templateId })).template;
  const source = read(descriptorPath);
  assert.deepEqual(template.extensions.productControls, source.extensions.productControls,
    'The native descriptor must return the actual product-owned controls');
  assert.deepEqual(template.parameters.map(field => field.key).sort(), source.parameters.map(field => field.key).sort());
  report.descriptorSha256 = hash(descriptorPath);
  report.scriptSha256 = hash(resolve(templates, 'product/single_face_security_window/template.py'));
  report.modules = native.modules();
  const controls = productControlFields(template);
  const controlIds = ['outerFrameConnection', 'doorFrameConnection', 'doorLeafFrameConnection', 'outerFrameFoldLayout',
    'horizontalEndConnectionChoice', 'verticalEndConnectionChoice'];
  assert.deepEqual(controls.map(control => control.key).sort(), [...controlIds].sort());
  report.controls = controls.map(control => ({ key: control.key, choices: control.choices.map(choice => choice.value) }));
  const control = key => { const found = controls.find(field => field.key === key); assert.ok(found, key); return found; };
  assert.equal(control('outerFrameConnection').choices.some(choice => choice.value === 'insert'), false);
  const view = { tubeDesignerSystemPunchTools: [loadTool('v-notch-sharp'), loadTool('edge-arc-groove')] };
  report.tools = view.tubeDesignerSystemPunchTools.map(tool => ({ id: tool.id, version: tool.version, digest: tool.digest }));
  const publicKeys = new Set(template.parameters.map(field => field.key));
  const allowedKeys = new Set([...publicKeys, 'tubeDesignerToolBindings', 'tubeDesignerProfileOverrides']);
  const defaults = () => Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue]));
  const manufacturingOnly = new Set(template.extensions.parameterDependencies.manufacturingOnly);
  const displayIdentity = state => state.members.map(member => ({ entityId: member.entityId, key: member.stableKey,
    resourceId: member.previewGeometryResourceId, version: member.previewGeometryResourceVersion,
    transform: member.transform })).sort((left, right) => left.key.localeCompare(right.key));
  let baselineKey, generatedBaseline, baselineParameters;
  const assertPublic = values => {
    for (const key of Object.keys(values)) assert.ok(allowedKeys.has(key), `Unexpected public parameter: ${key}`);
    for (const key of controlIds) assert.equal(Object.hasOwn(values, key), false, `UI control ${key} leaked into public input`);
  };
  const choose = (values, key, choice) => {
    const frozen = structuredClone(values);
    const result = applyProductControlChoice(view, template, control(key), values, choice);
    assert.deepEqual(values, frozen, `Choosing ${key}/${choice} mutated the previous values`);
    assertPublic(result);
    assert.equal(productControlValue(template, control(key), result), choice, `${key}/${choice} did not round-trip`);
    return result;
  };
  const roundTrip = async (name, values, expected = {}) => {
    const entry = { name, faceType: values.faceType, expectedChoices: expected, passed: false };
    report.cases.push(entry); save();
    const started = Date.now();
    try {
      assertPublic(values);
      for (const [key, choice] of Object.entries(expected))
        assert.equal(productControlValue(template, control(key), values), choice);
      const input = { templateId, parameters: values }, frozen = structuredClone(input);
      const preview = await native.invoke('GenerateProductTemplatePreview', input);
      assert.deepEqual(preview.parameters, frozen.parameters, 'Preview must echo the complete normalized public input exactly');
      assert.deepEqual(input, frozen, 'Preview must not mutate its request');
      assert.ok(preview.items.length > 0);
      const plan = await native.invoke('GetProductManufacturingPlan', input);
      assert.deepEqual(plan.parameters, frozen.parameters, 'Manufacturing plan must echo complete normalized public input exactly');
      assert.deepEqual(input, frozen, 'Manufacturing planning must not mutate its request');
      assert.ok(plan.partCount > 0);
      const displayParameters = Object.fromEntries(Object.entries(values).filter(([key]) => !manufacturingOnly.has(key)));
      const nextBaselineKey = JSON.stringify(displayParameters);
      if (baselineKey !== nextBaselineKey) {
        baselineParameters = { ...defaults(), ...displayParameters };
        generatedBaseline = (await native.invoke('GeneratePreview', { templateId, ...baselineParameters })).tubeDesigner;
        baselineKey = nextBaselineKey;
        assert.deepEqual(generatedBaseline.product.parameters, baselineParameters);
      }
      const identity = displayIdentity(generatedBaseline), runId = generatedBaseline.product.activeGenerationRunId;
      const updateRequest = { productEntityId: generatedBaseline.product.entityId, parameters: values };
      const updateFrozen = structuredClone(updateRequest);
      const updated = (await native.invoke('UpdateProductParameters', updateRequest)).tubeDesigner;
      assert.deepEqual(updateRequest, updateFrozen, 'Updating manufacturing choices mutated its request');
      assert.deepEqual(updated.product.parameters, values, 'Native update lost public manufacturing parameters');
      assert.equal(updated.product.modelOutdated, false, 'A current product became visually outdated after a manufacturing choice');
      assert.equal(updated.product.partsOutdated, !isDeepStrictEqual(values, baselineParameters));
      assert.equal(updated.product.activeGenerationRunId, runId, 'Manufacturing choices regenerated the displayed model');
      assert.deepEqual(displayIdentity(updated), identity, 'Manufacturing choices replaced display geometry resources or placement');
      const listed = (await native.invoke('List')).tubeDesigner;
      assert.equal(listed.product.modelOutdated, false);
      assert.equal(listed.product.partsOutdated, updated.product.partsOutdated);
      assert.equal(listed.product.activeGenerationRunId, runId);
      assert.deepEqual(displayIdentity(listed), identity);
      entry.previewItems = preview.items.length; entry.plannedParts = plan.partCount;
      entry.frameManufacturingMode = values.frameManufacturingMode;
      entry.selectedReferences = Object.fromEntries(Object.entries(values.tubeDesignerToolBindings || {})
        .map(([role, binding]) => [role, binding.ref]));
      entry.inputUnchanged = true; entry.parametersEchoedUnchanged = true; entry.noSyntheticControlKeys = true;
      entry.displayGenerationUnchanged = true; entry.displayResourcesUnchanged = true;
      entry.modelOutdated = updated.product.modelOutdated; entry.partsOutdated = updated.product.partsOutdated;
      entry.passed = true;
    } catch (error) { entry.error = String(error.stack || error); throw error; }
    finally { entry.milliseconds = Date.now() - started; save(); }
    console.log(JSON.stringify({ name, passed: entry.passed, previewItems: entry.previewItems, plannedParts: entry.plannedParts }));
  };

  // Different choices on the three frames must remain independent.
  for (const [outer, fixed, leaf] of [
    ['miter', 'butt', 'v-groove'], ['butt', 'v-groove', 'edge-arc'],
    ['v-groove', 'edge-arc', 'miter'], ['edge-arc', 'miter', 'butt'],
  ]) {
    let values = { ...defaults(), faceType: 'single', accessDoorEnabled: true };
    values = choose(values, 'outerFrameConnection', outer);
    values = choose(values, 'doorFrameConnection', fixed);
    values = choose(values, 'doorLeafFrameConnection', leaf);
    await roundTrip(`single-${outer}-mixed`, values,
      { outerFrameConnection: outer, doorFrameConnection: fixed, doorLeafFrameConnection: leaf });
  }
  for (const choice of ['tabs']) {
    let values = { ...defaults(), faceType: 'single', accessDoorEnabled: false };
    values = choose(values, 'outerFrameConnection', choice);
    assert.equal(values.frameManufacturingMode, 'segment_weld');
    assert.equal(values.foldedPostJoint, choice);
    await roundTrip(`single-${choice}`, values, { outerFrameConnection: choice });
  }
  for (const [horizontal, vertical] of [['insert', 'weld'], ['weld', 'tabs'], ['tabs', 'insert']]) {
    let values = { ...defaults(), faceType: 'single', accessDoorEnabled: false };
    values = choose(values, 'horizontalEndConnectionChoice', horizontal);
    values = choose(values, 'verticalEndConnectionChoice', vertical);
    assert.equal(values.horizontalEndConnection, horizontal);
    assert.equal(values.verticalEndConnection, vertical);
    await roundTrip(`single-branches-${horizontal}-${vertical}`, values,
      { horizontalEndConnectionChoice: horizontal, verticalEndConnectionChoice: vertical });
  }
  {
    let values = { ...defaults(), faceType: 'single', accessDoorEnabled: false };
    values = choose(values, 'outerFrameConnection', 'edge-arc');
    // Selecting a split welded frame must retain an older, now inactive tool draft.
    values.tubeDesignerToolBindings.outerFrameGroove.parameters.angle = 45;
    const savedBinding = structuredClone(values.tubeDesignerToolBindings.outerFrameGroove);
    values = choose(values, 'outerFrameConnection', 'miter');
    assert.deepEqual(values.tubeDesignerToolBindings.outerFrameGroove, savedBinding);
    await roundTrip('single-miter-retains-inactive-edge-draft', values, { outerFrameConnection: 'miter' });
  }
  for (const faceType of ['two', 'three']) {
    let values = { ...defaults(), faceType, accessDoorEnabled: false };
    values = choose(values, 'outerFrameConnection', 'v-groove');
    values = choose(values, 'outerFrameFoldLayout', 'plane');
    assert.equal(values.frameManufacturingMode, 'plane_v_notch');
    await roundTrip(`${faceType}-plane-v-groove`, values,
      { outerFrameConnection: 'v-groove', outerFrameFoldLayout: 'plane' });
    values = choose(values, 'outerFrameConnection', 'spatial');
    assert.equal(values.frameManufacturingMode, 'spatial_v_notch');
    await roundTrip(`${faceType}-spatial-v-groove`, values,
      { outerFrameConnection: 'spatial', outerFrameFoldLayout: '' });
    values = choose(values, 'outerFrameConnection', 'edge-arc');
    assert.equal(values.frameManufacturingMode, 'plane_v_notch', 'The edge-arc product choice selects planar frames');
    await roundTrip(`${faceType}-plane-edge-arc`, values,
      { outerFrameConnection: 'edge-arc', outerFrameFoldLayout: 'plane' });
    const savedBinding = structuredClone(values.tubeDesignerToolBindings.outerFrameGroove);
    values = choose(values, 'outerFrameConnection', 'spatial');
    assert.equal(values.frameManufacturingMode, 'spatial_v_notch');
    assert.deepEqual(values.tubeDesignerToolBindings.outerFrameGroove, savedBinding);
    await roundTrip(`${faceType}-spatial-retains-edge-arc`, values,
      { outerFrameConnection: 'spatial', outerFrameFoldLayout: '' });
    values = choose(values, 'outerFrameConnection', 'v-groove');
    assert.equal(values.frameManufacturingMode, 'plane_v_notch', 'The V-groove product choice selects planar frames');
    await roundTrip(`${faceType}-plane-v-groove-restored`, values,
      { outerFrameConnection: 'v-groove', outerFrameFoldLayout: 'plane' });
  }
  if (process.env.ICAX_PRODUCT_CONNECTIONS_MACHINING !== '0') {
    let values = { ...defaults(), faceType: 'single', accessDoorEnabled: false,
      verticalMaximumCenterSpacing: 600, horizontalMaximumCenterSpacing: 1000 };
    values = choose(values, 'outerFrameConnection', 'edge-arc');
    const entry = { name: 'single-edge-arc-native-machining', passed: false };
    report.cases.push(entry); save();
    const started = Date.now();
    try {
      const input = { templateId, ...values }, frozen = structuredClone(input);
      const generated = await native.invoke('GeneratePreview', input);
      assert.deepEqual(input, frozen);
      assert.deepEqual(generated.tubeDesigner.product.parameters, values);
      const result = await native.invoke('Disassemble', { productEntityIds: [generated.tubeDesigner.product.entityId] });
      const outer = result.geometryChecks.filter(check => check.key.startsWith('outer_frame.'));
      assert.equal(outer.length, 1);
      assert.equal(outer[0].valid, true); assert.equal(outer[0].solids, 1); assert.ok(outer[0].volume > 0);
      const snapshot = await native.invoke('InspectScriptResources');
      assert.deepEqual(snapshot.generatedParameters, values);
      assert.deepEqual(snapshot.manufacturingParameters, values);
      const refs = snapshot.manufacturingExecution.document.extensions['tubeDesigner.assemblyProcessSource']
        .instances.flatMap(instance => Object.values(instance.processInput.resources || {}).map(slot => slot.ref))
        .filter(ref => ref.id === 'edge-arc-groove');
      assert.ok(refs.length > 0, 'The actual machining must retain the selected edge-arc resource');
      for (const ref of refs) assert.deepEqual(ref, values.tubeDesignerToolBindings.outerFrameGroove.ref);
      entry.manufacturedParts = result.tubeDesigner.parts.length; entry.geometryChecks = outer;
      entry.executedReferenceCount = refs.length; entry.parametersEchoedUnchanged = true; entry.inputUnchanged = true;
      entry.passed = true;
    } catch (error) { entry.error = String(error.stack || error); throw error; }
    finally { entry.milliseconds = Date.now() - started; save(); }
    console.log(JSON.stringify({ name: entry.name, passed: entry.passed, manufacturedParts: entry.manufacturedParts }));
  }
  report.passed = true;
} catch (error) {
  report.error = String(error.stack || error); process.exitCode = 1;
} finally {
  native.close(); save();
  console.log(JSON.stringify({ passed: report.passed, output, cases: report.cases.length, error: report.error }));
}
