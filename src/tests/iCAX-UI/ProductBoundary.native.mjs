import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawn, spawnSync } from 'node:child_process';
import { createInterface } from 'node:readline';
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { makeProductToolBinding, productToolParameterUI } from '../../apps/tube-designer/webpage/productResourceBindings.mjs';
import { parameterVisible } from '../../apps/tube-designer/webpage/parameterConditions.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const runtime = resolve(process.env.ICAX_PRODUCT_BOUNDARY_RUNTIME || resolve(root, 'src/x64/Debug'));
const output = resolve(process.env.ICAX_PRODUCT_BOUNDARY_OUTPUT || resolve(root, 'output/tests/product-ui-boundary/native.json'));
const bridgeDirectory = resolve(dirname(output), 'bridge');
mkdirSync(bridgeDirectory, { recursive: true });
const bridge = resolve(bridgeDirectory, 'SecurityWindowFramesBridge.exe');
copyFileSync(resolve(process.env.ICAX_PRODUCT_BOUNDARY_BRIDGE
  || resolve(root, 'tmp/security-window-frames-native/SecurityWindowFramesBridge.exe')), bridge);
const deployed = process.env.ICAX_PRODUCT_BOUNDARY_DEPLOYED === '1';
const templates = deployed ? resolve(runtime, 'apps/tube-designer/templates') : resolve(root, 'src/apps/tube-designer/templates');
const hash = path => createHash('sha256').update(readFileSync(path)).digest('hex');
const read = path => JSON.parse(readFileSync(path, 'utf8'));
const report = { passed: false, runtime, bridge, deployed, templates, cases: [] };
const save = () => writeFileSync(output, JSON.stringify(report, null, 2) + '\n');

function connection() {
  // Only the bridge executable is staged. All product DLLs come from the deployed runtime.
  const child = spawn(bridge, [], { cwd: deployed ? runtime : root, windowsHide: true,
    env: { ...process.env, PATH: `${runtime};${process.env.PATH || ''}` }, stdio: ['pipe', 'pipe', 'pipe'] });
  let sequence = 0, stderr = '';
  const pending = new Map();
  const fail = error => { for (const item of pending.values()) { clearTimeout(item.timer); item.reject(error); } pending.clear(); };
  child.stderr.on('data', data => { stderr = (stderr + data).slice(-10000); });
  child.on('error', fail);
  child.on('exit', code => fail(new Error(`Native exited ${code}: ${stderr}`)));
  createInterface({ input: child.stdout }).on('line', line => {
    let response;
    try { response = JSON.parse(line); } catch { return fail(new Error(`Invalid native response: ${line.slice(0, 500)}`)); }
    const item = pending.get(response.id);
    if (!item) return;
    pending.delete(response.id); clearTimeout(item.timer);
    response.ok ? item.resolve(response.result) : item.reject(new Error(`${item.method}: ${response.error}`));
  });
  return {
    close() { child.stdin.end(); child.kill(); },
    modules() {
      const observed = spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command',
        `(Get-Process -Id ${child.pid}).Modules | Where-Object { $_.ModuleName -in @('TemplateRuntime.dll','TubeDesigner.dll') } | Select-Object ModuleName,FileName | ConvertTo-Json -Compress`],
      { windowsHide: true, encoding: 'utf8', timeout: 30000 });
      assert.equal(observed.status, 0, observed.stderr);
      const parsed = JSON.parse(observed.stdout.trim());
      const modules = (Array.isArray(parsed) ? parsed : [parsed]).map(item => ({ name: item.ModuleName,
        path: item.FileName, sha256: hash(item.FileName) }));
      assert.ok(modules.some(item => item.name === 'TemplateRuntime.dll'));
      assert.ok(modules.every(item => item.path.toLowerCase().startsWith(runtime.toLowerCase() + '\\')));
      return modules;
    },
    invoke(method, payload) {
      return new Promise((resolveRequest, reject) => {
        const id = ++sequence;
        const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Timeout ${method}: ${stderr}`)); }, 300000);
        pending.set(id, { method, resolve: resolveRequest, reject, timer });
        child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
      });
    },
  };
}

const productRoot = resolve(templates, 'product');
const products = readdirSync(productRoot, { withFileTypes: true }).filter(item => item.isDirectory() && !item.name.startsWith('_')
  && existsSync(resolve(productRoot, item.name, 'template.json')))
  .map(item => ({ directory: item.name, descriptor: read(resolve(productRoot, item.name, 'template.json')) }));
const changed = products.filter(item => item.descriptor.parameters.some(field => field.key === 'assemblyPlanningMode'));
const defaults = descriptor => Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue]));
const native = connection();
try {
  assert.equal(changed.length, 4, 'All existing product sources must stay behind the product boundary');
  for (const { descriptor } of products) {
    assert.equal(descriptor.parameters.filter(field => field.presentation?.editor === 'tool-library').length, 0,
      `${descriptor.id} exposes a generic resource selector`);
  }
  for (const { directory, descriptor } of changed) {
    const values = defaults(descriptor);
    assert.equal(parameterVisible(descriptor.parameters.find(field => field.key === 'assemblyPlanningMode'), values), false);
    const started = Date.now();
    const nativeDescriptor = (await native.invoke('GetTemplateDescriptor', { templateId: descriptor.id })).template;
    assert.deepEqual(nativeDescriptor.parameters.map(field => field.key).sort(), descriptor.parameters.map(field => field.key).sort());
    assert.equal(nativeDescriptor.parameters.find(field => field.key === 'assemblyPlanningMode').presentation.visible, false);
    const input = { templateId: descriptor.id, parameters: values };
    const frozen = structuredClone(input);
    const preview = await native.invoke('GenerateProductTemplatePreview', input);
    assert.deepEqual(preview.parameters, values, 'Hidden inputs remain part of the unchanged host contract');
    assert.deepEqual(input, frozen);
    assert.ok(preview.items.length > 0);
    report.cases.push({ templateId: descriptor.id, passed: true, previewItems: preview.items.length,
      parametersEchoedUnchanged: true, descriptorSha256: hash(resolve(productRoot, directory, 'template.json')),
      milliseconds: Date.now() - started });
    save();
  }
  report.modules = native.modules();
  const { descriptor } = products.find(item => item.descriptor.id === 'single-face-security-window');
  const values = { ...defaults(descriptor), accessDoorEnabled: true, frameManufacturingMode: 'plane_v_notch',
    doorFrameJoinType: 'v_groove_90:tool_library', doorLeafFrameJoinType: 'v_groove_90:tool_library' };
  for (const key of ['assemblyClearance', 'horizontalBranchReserve', 'verticalBranchReserve'])
    assert.equal(parameterVisible(descriptor.parameters.find(field => field.key === key), values), false);
  values.tubeDesignerToolBindings = {};
  const tool = { ...read(resolve(templates, 'mold/edge-arc-groove/tool.json')), libraryScope: 'system' };
  const fields = descriptor.parameters.filter(field => field.presentation?.editor === 'product-option');
  assert.equal(fields.length, 3);
  for (const field of fields) {
    assert.deepEqual(field.presentation.productOptions.map(item => item.value), ['system:v-notch-sharp', 'system:edge-arc-groove']);
    // A saved generic draft may contain an old opening angle. The product view
    // neither exposes nor rewrites it; actual bends derive their angle from the frame.
    const binding = makeProductToolBinding(field, tool, null, descriptor);
    binding.parameters.angle = 45;
    const ui = productToolParameterUI(descriptor, field, binding, tool);
    assert.equal(ui.available, true); assert.equal(ui.fixedConflict, false); assert.deepEqual(ui.definitions, []);
    values[field.key] = binding.selectionKey;
    values.tubeDesignerToolBindings[binding.role] = binding;
  }
  const input = { templateId: descriptor.id, parameters: values }, frozen = structuredClone(input);
  const started = Date.now();
  const preview = await native.invoke('GenerateProductTemplatePreview', input);
  assert.deepEqual(preview.parameters, values); assert.deepEqual(input, frozen);
  const plan = await native.invoke('GetProductManufacturingPlan', input);
  assert.deepEqual(plan.parameters, values); assert.deepEqual(input, frozen); assert.ok(plan.partCount > 0);
  report.cases.push({ templateId: descriptor.id, scenario: 'all-three-arc-options-with-saved-hidden-drafts',
    passed: true, previewItems: preview.items.length, plannedParts: plan.partCount,
    parametersEchoedUnchanged: true, milliseconds: Date.now() - started });
  report.passed = true;
} catch (error) {
  report.error = String(error.stack || error);
  process.exitCode = 1;
} finally {
  native.close(); save(); console.log(JSON.stringify({ passed: report.passed, output, cases: report.cases.length, error: report.error }));
}
