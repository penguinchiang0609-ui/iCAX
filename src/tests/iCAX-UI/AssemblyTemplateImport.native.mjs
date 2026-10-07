import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import readline from 'node:readline';
import { spawn, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';

const repo = path.resolve(import.meta.dirname, '../../..');
const runtime = path.resolve(process.env.ICAX_NATIVE_RUNTIME_ROOT ?? '');
const bridge = path.resolve(process.env.ICAX_NATIVE_SCOPE_BRIDGE ?? '');
assert(process.env.ICAX_NATIVE_RUNTIME_ROOT && process.env.ICAX_NATIVE_SCOPE_BRIDGE,
  'Set ICAX_NATIVE_RUNTIME_ROOT and ICAX_NATIVE_SCOPE_BRIDGE');
const python = process.env.ICAX_PYTHON ?? 'C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe';
const reportRoot = path.resolve(process.env.ICAX_ASSEMBLY_IMPORT_REPORT_ROOT ?? path.join(repo, 'output/tests/assembly-template-import-20261006'));
await fs.mkdir(reportRoot, { recursive: true });
const root = await fs.mkdtemp(path.join(reportRoot, 'native-'));
const fixtureRoot = path.join(root, 'packages');
const generated = spawnSync(python, [path.join(import.meta.dirname, 'assemblyTemplateImportFixtures.py'), repo, fixtureRoot],
  { encoding: 'utf8', windowsHide: true });
assert.equal(generated.status, 0, generated.stderr);
const userData = path.join(root, 'user-data');
await fs.mkdir(userData, { recursive: true });
const userAssembly = path.join(userData, 'icax.tube-designer/template/assembly');
const id = 'user-native-fastener';
const checks = [], calls = [], processes = new Set();
let modules = {};
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
function host() {
  const child = spawn(bridge, [], { cwd: runtime, windowsHide: true,
    env: { ...process.env, PATH: `${runtime};${process.env.PATH}`, ICAX_AUTOMATION_USER_DATA: userData } });
  processes.add(child);
  const pending = new Map(); let counter = 0, errors = '';
  child.stderr.on('data', value => { errors += value.toString(); });
  readline.createInterface({ input: child.stdout }).on('line', line => {
    let response; try { response = JSON.parse(line); } catch { return; }
    const item = pending.get(response.id); if (!item) return;
    pending.delete(response.id); clearTimeout(item.timer); calls.push({ ...item.request, response });
    if (response.ok) item.resolve(response.result); else item.reject(new Error(response.error));
  });
  child.on('exit', code => {
    processes.delete(child);
    for (const item of pending.values()) { clearTimeout(item.timer); item.reject(new Error(`Bridge exit ${code}: ${errors}`)); }
    pending.clear();
  });
  return {
    invoke(method, payload = {}, scope = 'scene') {
      const request = { id: ++counter, scope, method, payload };
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => { pending.delete(request.id); reject(new Error(`${method} timed out`)); }, 180000);
        pending.set(request.id, { request, resolve, reject, timer }); child.stdin.write(`${JSON.stringify(request)}\n`);
      });
    },
    async close() { const ended = new Promise(resolve => child.once('exit', resolve)); child.stdin.end(); await ended; },
  };
}
try {
  let api = host();
  const loaded = (await api.invoke('GetRuntimeModules', {}, 'inspection')).modules;
  for (const [name, file] of Object.entries(loaded)) {
    assert.equal(path.dirname(path.resolve(file)), runtime, `Actual native module directory: ${name}`);
    modules[name] = { path: file, sha256: sha(await fs.readFile(file)) };
  }
  const baseline = await api.invoke('GetAssemblyTemplates');
  assert.deepEqual(baseline.errors, []);
  assert(baseline.assemblies.length > 0 && baseline.assemblies.every(value => value.libraryScope === 'builtin'));
  const input = path.join(fixtureRoot, 'valid.itat');
  const inputSha = sha(await fs.readFile(input));
  const imported = await api.invoke('ImportAssemblyTemplatePackage', { sourcePath: input }, 'product');
  assert.equal(imported.template.id, id); assert.equal(imported.template.libraryScope, 'user');
  assert(!imported.template.previewScene.designParts);
  assert.equal(sha(await fs.readFile(input)), inputSha, 'Import preserves source package');
  const storedDescriptor = JSON.parse(await fs.readFile(path.join(userAssembly, id, 'assembly.json'), 'utf8'));
  assert.equal(storedDescriptor.id, id); assert(!storedDescriptor.previewScene.designParts);
  const catalogue = await api.invoke('GetAssemblyTemplates');
  assert.deepEqual(catalogue.errors, []);
  assert.equal(catalogue.assemblies.length, baseline.assemblies.length + 1);
  assert.equal(catalogue.assemblies.find(value => value.id === id).libraryScope, 'user');
  checks.push('native import stores one user assembly with unchanged descriptor and source');
  const descriptorSha = sha(await fs.readFile(path.join(userAssembly, id, 'assembly.json')));
  for (const name of ['valid.itat', 'builtin-duplicate.itat', 'missing-example.itat', 'invalid-schema.itat',
    'unsafe-path.itat', 'invalid-example.itat', 'unprotected.itat', 'corrupt.itat']) {
    await assert.rejects(api.invoke('ImportAssemblyTemplatePackage', { sourcePath: path.join(fixtureRoot, name) }, 'product'));
    const after = await api.invoke('GetAssemblyTemplates');
    assert.deepEqual(after.errors, []); assert.equal(after.assemblies.length, catalogue.assemblies.length);
    assert.equal(sha(await fs.readFile(path.join(userAssembly, id, 'assembly.json'))), descriptorSha);
  }
  assert.deepEqual((await fs.readdir(userAssembly)).sort(), [id]);
  assert(!await fs.access(path.join(root, 'escape.py')).then(() => true, () => false));
  checks.push('duplicate user or built-in ID, invalid schema, missing script, traversal, invalid example and bad/passwordless ZIP rejected without extra templates');
  await api.close(); api = host();
  const restarted = await api.invoke('GetAssemblyTemplates');
  assert.equal(restarted.assemblies.find(value => value.id === id).libraryScope, 'user');
  const parameters = { nominalDiameter: 12, holeClearance: 1, count: 3, pitch: 45, washer: 'both' };
  const example = await api.invoke('GetAssemblyTemplateExampleProduct', { templateId: id, parameters });
  assert.equal(example.templateId, id); assert.equal(example.finishedProduct.schema, 'icax.finished-product');
  const applicability = await api.invoke('CheckAssemblyTemplateApplicability', { templateId: id, parameters, finishedProduct: example.finishedProduct });
  assert.equal(applicability.applicable, true); assert.equal(applicability.reason, '');
  const plan = await api.invoke('ResolveAssemblyTemplatePreview', { templateId: id, parameters, finishedProduct: example.finishedProduct, processDrafts: {} });
  assert.equal(plan.schema, 'icax.assembly-preview-plan'); assert.equal(plan.templateId, id);
  assert.equal(plan.manufacturingParts.length, 2); assert(plan.resolvedWorkflow.partOperations.length > 0);
  assert.deepEqual(plan.finishedProduct, example.finishedProduct);
  const normalizedParameters = Object.fromEntries(imported.template.parameters.map(value => [value.key, value.defaultValue]));
  assert.deepEqual(plan.parameters, { ...normalizedParameters, ...parameters });
  for (const [index, part] of plan.manufacturingParts.entries()) {
    const geometry = await api.invoke('PreviewAssemblyManufacturingPart', {
      ...part.request, name: `外部装配下料件${index + 1}`, quantity: 1,
      previewResourceKey: `assembly-import-${index + 1}`,
    });
    assert.equal(geometry.previewComputed, true); assert.equal(geometry.resultValid, true);
    assert.equal(geometry.solidCount, 1); assert(geometry.geometry?.url);
  }
  checks.push('restart reloads the user assembly; real native example/applicability/plan resolve the imported directory and preserve product input');
  checks.push('both imported-template manufacturing requests generate valid native single-solid BRep preview resources');
  await api.close();
  const report = { status: 'passed', root, fixture: input, modules, nativeBusinessResponsesMocked: false, checks, calls };
  await fs.writeFile(path.join(root, 'report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ status: 'passed', root, checks }, null, 2));
} catch (error) {
  await fs.writeFile(path.join(root, 'report.json'), JSON.stringify({ status: 'failed', root, modules, checks, error: String(error.stack ?? error), calls }, null, 2));
  throw error;
} finally { for (const child of processes) child.kill(); }
