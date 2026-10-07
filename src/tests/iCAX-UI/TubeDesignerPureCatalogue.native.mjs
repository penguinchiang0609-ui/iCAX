// Validate every active product descriptor through the real native preview API.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { readFileSync, readdirSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const workingRoot = process.env.ICAX_NATIVE_PURE_RUNTIME_ROOT || root;
const deployed = resolve(workingRoot) !== resolve(root);
const directory = resolve(root, 'src/apps/tube-designer/templates/product');
const bridge = spawn(resolve(root, 'tmp/security-window-frames-native/SecurityWindowFramesBridge.exe'), [], {
  cwd: workingRoot, windowsHide: true,
  env: { ...process.env, PATH: resolve(root, 'src/x64/Debug') + ';' + process.env.PATH },
  stdio: ['pipe', 'pipe', 'pipe'],
});
let sequence = 0, stderr = '';
const pending = new Map();
const fail = error => { for (const request of pending.values()) { clearTimeout(request.timer); request.reject(error); } pending.clear(); };
bridge.stderr.on('data', value => stderr += value);
bridge.on('error', fail);
bridge.on('exit', code => { if (pending.size) fail(new Error(`Native process exited ${code}: ${stderr}`)); });
createInterface({ input: bridge.stdout }).on('line', line => {
  let response; try { response = JSON.parse(line); } catch { return fail(new Error(line)); }
  const request = pending.get(response.id); if (!request) return;
  pending.delete(response.id); clearTimeout(request.timer);
  response.ok ? request.resolve(response.result) : request.reject(new Error(response.error));
});
const invoke = (method, payload = {}) => new Promise((resolve, reject) => {
  const id = ++sequence;
  pending.set(id, { resolve, reject, timer: setTimeout(() => {
    pending.delete(id); reject(new Error(`Timeout ${method}: ${payload.templateId}`));
  }, 240000) });
  bridge.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
});
const evidence = [];
try {
  for (const entry of readdirSync(directory, { withFileTypes: true }).filter(entry => entry.isDirectory() && !entry.name.startsWith('_') &&
      existsSync(resolve(directory, entry.name, 'template.json')))) {
    const source = JSON.parse(readFileSync(resolve(directory, entry.name, 'template.json'), 'utf8'));
    const templateId = source.id;
    const descriptor = (await invoke('GetTemplateDescriptor', { templateId })).template;
    const values = Object.fromEntries(descriptor.parameters.map(parameter => [parameter.key, parameter.defaultValue]));
    const preview = await invoke('GenerateProductTemplatePreview', { templateId, parameters: values });
    assert.equal(preview.templateId, templateId);
    assert.deepEqual(preview.parameters, values, `${templateId}: normalized public parameters must be echoed exactly`);
    assert.ok(preview.items.length, `${templateId}: native model has no items`);
    evidence.push({ templateId, itemCount: preview.items.length, parameterCount: Object.keys(values).length });
    console.log(`native descriptor+preview ${templateId}: ${preview.items.length} items; exact ${Object.keys(values).length} parameter echo`);
  }
  const output = resolve(root, 'output/tests/assembly-process'); mkdirSync(output, { recursive: true });
  writeFileSync(resolve(output, `pure-catalogue-native-v4${deployed ? '-deployed' : ''}.json`), JSON.stringify(evidence, null, 2));
  console.log(`Pure product catalogue native acceptance passed: ${evidence.length} templates`);
} finally { bridge.stdin.end(); bridge.kill(); }
