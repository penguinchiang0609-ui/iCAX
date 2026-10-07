// Native descriptor/previews enforce one actual outer-frame section across every face.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { copyFileSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const runtime = resolve(process.env.ICAX_MATERIAL_SEPARATION_RUNTIME || resolve(root, 'src/x64/Debug'));
const deployed = process.env.ICAX_MATERIAL_SEPARATION_DEPLOYED === '1';
const caseFilter = process.env.ICAX_MATERIAL_SEPARATION_CASE || '';
const output = resolve(process.env.ICAX_MATERIAL_SEPARATION_OUTPUT
  || resolve(root, `output/tests/product-material-separation/${deployed ? 'deployed' : 'source'}-native.json`));
const bridgeDirectory = resolve(dirname(output), 'material-separation-bridge');
mkdirSync(bridgeDirectory, { recursive: true });
assert.deepEqual(readdirSync(bridgeDirectory).filter(name => /\.dll$/i.test(name)), [], 'No staged DLLs');
const bridge = resolve(bridgeDirectory, 'SecurityWindowFramesBridge.exe');
copyFileSync(resolve(process.env.ICAX_MATERIAL_SEPARATION_BRIDGE
  || resolve(root, 'output/tests/product-tab-connections/bridge-build/SecurityWindowFramesBridge.exe')), bridge);
const templates = resolve(deployed ? runtime : resolve(root, 'src'), 'apps/tube-designer/templates');
const hash = value => createHash('sha256').update(value).digest('hex');
const descriptorPath = resolve(templates, 'product/single_face_security_window/template.json');
const report = { passed: false, deployed, runtime, templates, descriptorSha256: hash(readFileSync(descriptorPath)), cases: [] };
const save = () => writeFileSync(output, JSON.stringify(report, null, 2) + '\n');
save();
const child = spawn(bridge, [], { cwd: deployed ? runtime : root, windowsHide: true,
  env: { ...process.env, PATH: `${runtime};${process.env.PATH || ''}` }, stdio: ['pipe', 'pipe', 'pipe'] });
let sequence = 0, stderr = '';
const pending = new Map();
const fail = error => {
  for (const request of pending.values()) { clearTimeout(request.timer); request.reject(error); }
  pending.clear();
};
child.stderr.on('data', data => { stderr = (stderr + data).slice(-10000); });
child.on('error', fail);
child.on('exit', code => fail(new Error(`Native exited ${code}: ${stderr}`)));
createInterface({ input: child.stdout }).on('line', line => {
  let response;
  try { response = JSON.parse(line); } catch { return fail(new Error(`Invalid native response: ${line.slice(0, 500)}`)); }
  const request = pending.get(response.id);
  if (!request) return;
  pending.delete(response.id); clearTimeout(request.timer);
  response.ok ? request.resolve(response.result) : request.reject(new Error(`${request.method}: ${response.error}`));
});
const invoke = (method, payload = {}) => new Promise((resolveRequest, reject) => {
  const id = ++sequence;
  const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Timeout ${method}: ${stderr}`)); }, 300000);
  pending.set(id, { method, resolve: resolveRequest, reject, timer });
  child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
});
// Resources have per-request cache URLs; record physical placement and measured bounds.
const physicalItems = preview => preview.items.map(item => ({ key: item.stableKey || item.key,
  bounds: item.bounds, transform: item.transform })).sort((a, b) => a.key.localeCompare(b.key));

try {
  const templateId = 'single-face-security-window';
  const template = (await invoke('GetTemplateDescriptor', { templateId })).template;
  const source = JSON.parse(readFileSync(descriptorPath, 'utf8'));
  assert.deepEqual(template.parameters.map(field => field.key), source.parameters.map(field => field.key));
  assert.deepEqual(template.extensions.productControls, source.extensions.productControls);
  const defaults = Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue]));
  const removed = ['outerFramePostMaterial', 'foldedPostProfileType', 'foldedPostWidth',
    'foldedPostDepth', 'foldedPostCornerRadius', 'foldedPostWallThickness'];
  for (const key of removed) {
    assert.equal(Object.hasOwn(defaults, key), false);
    assert.equal(JSON.stringify(source).includes(key), false, `Removed material field remains: ${key}`);
  }
  assert.equal(JSON.stringify(source).includes('folded_post_profile'), false);
  assert.equal(Object.keys(defaults).length, 102);
  for (const face of ['single', 'two', 'three', 'five']) {
    if (caseFilter && caseFilter !== face) continue;
    let baseline;
    for (const [mode, joint] of [['segment_weld', 'weld'], ['segment_weld', 'tabs'], ['plane_v_notch', 'tabs']]) {
      const parameters = { ...defaults, faceType: face, accessDoorEnabled: false,
        frameWidth: 42, frameDepth: 46, frameWallThickness: 1.5, frameCornerRadius: 3,
        frameManufacturingMode: mode, foldedPostJoint: joint };
      const frozen = structuredClone(parameters);
      const entry = { face, mode, joint, passed: false };
      report.cases.push(entry); save();
      const preview = await invoke('GenerateProductTemplatePreview', { templateId, parameters });
      assert.deepEqual(parameters, frozen, 'Request parameters mutated');
      assert.deepEqual(preview.parameters, frozen, 'Native must echo every public key/value exactly');
      assert.ok(preview.items.length > 0);
      for (const item of preview.items) {
        assert.ok(item.geometry?.url, `${item.key}: missing solid resource`);
        assert.equal(item.transform.length, 16);
        assert.ok(item.transform.every(Number.isFinite));
        for (const dimension of ['width', 'height', 'depth'])
          assert.ok(Number.isFinite(item.bounds[dimension]) && item.bounds[dimension] >= 0);
      }
      const physical = physicalItems(preview);
      if (baseline) assert.deepEqual(physical, baseline,
        'Manufacturing route or end joint changed the displayed physical placement/bounds');
      else baseline = physical;
      Object.assign(entry, { passed: true, itemCount: preview.items.length,
        physicalPlacementSha256: hash(JSON.stringify(physical)), exactParameterEcho: true,
        inputUnchanged: true, displayUnchanged: true, uniformFrameSection: {
          width: parameters.frameWidth, depth: parameters.frameDepth,
          wallThickness: parameters.frameWallThickness, cornerRadius: parameters.frameCornerRadius,
        } });
      save();
    }
    console.log(JSON.stringify({ face, nativePreviews: 3, passed: true }));
  }
  assert.ok(report.cases.length > 0, 'The selected native case must exist');
  report.passed = true; save();
  console.log(`Security window native uniform outer-frame materials: ${report.cases.length} previews passed`);
} catch (error) { report.error = String(error.stack || error); save(); throw error; }
finally { child.stdin.end(); child.kill(); }
