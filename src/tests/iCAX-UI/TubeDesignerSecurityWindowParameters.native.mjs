// Complete public-parameter acceptance: native descriptor/preview + solid graph oracle.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createHash } from 'node:crypto';
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parameterVisible, parameterEnabled, availableParameterChoices } from '../../apps/tube-designer/webpage/parameterConditions.mjs';
import { buildGeometryCases } from './securityWindowParameterCases/geometry.mjs';
import { buildMaterialCases } from './securityWindowParameterCases/materials.mjs';
import { buildProcessCases } from './securityWindowParameterCases/process.mjs';
import { assertHorizontalNativeEndFit, assertNativeMiterContact } from './securityWindowParameterNativeAssertions.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const packageDirectory = resolve(root, 'src/apps/tube-designer/templates/product/single_face_security_window');
const source = JSON.parse(readFileSync(resolve(packageDirectory, 'template.json'), 'utf8'));
const templateId = source.id;
const definitions = new Map(source.parameters.map(field => [field.key, field]));
const defaults = Object.fromEntries(source.parameters.map(field => [field.key, field.defaultValue]));
const output = resolve(process.env.ICAX_SECURITY_PARAMETER_REPORT || resolve(root, 'output/tests/security-window-parameters'));
const filter = process.env.ICAX_SECURITY_PARAMETER_FILTER ? new RegExp(process.env.ICAX_SECURITY_PARAMETER_FILTER) : null;
const jobs = Number(process.env.ICAX_SECURITY_PARAMETER_JOBS || 2);
assert.ok(Number.isInteger(jobs) && jobs >= 1 && jobs <= 4, 'Worker count must be 1..4');
mkdirSync(output, { recursive: true });

class JsonProcess {
  constructor(executable, args, options) {
    this.sequence = 0; this.pending = new Map(); this.stderr = ''; this.closed = false;
    this.process = spawn(executable, args, { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'], ...options });
    this.process.stderr.on('data', data => { this.stderr = (this.stderr + data).slice(-12000); });
    const fail = error => {
      this.closed = true;
      for (const pending of this.pending.values()) { clearTimeout(pending.timer); pending.reject(error); }
      this.pending.clear();
    };
    this.process.on('error', fail);
    this.process.on('exit', code => fail(new Error(`Process exited ${code}: ${this.stderr}`)));
    createInterface({ input: this.process.stdout }).on('line', line => {
      let response;
      try { response = JSON.parse(line); } catch { return fail(new Error(`Non-JSON process output: ${line}`)); }
      const pending = this.pending.get(response.id);
      if (!pending) return;
      this.pending.delete(response.id); clearTimeout(pending.timer);
      response.ok ? pending.resolve(response.result) : pending.reject(new Error(response.error));
    });
  }
  invoke(method, payload) {
    return new Promise((resolveRequest, reject) => {
      if (this.closed) return reject(new Error(`Process unavailable: ${this.stderr}`));
      const id = ++this.sequence;
      this.pending.set(id, { resolve: resolveRequest, reject,
        timer: setTimeout(() => { this.pending.delete(id); this.stop(); reject(new Error(`Timeout: ${method}`)); }, 180000) });
      this.process.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
    });
  }
  stop() { this.process.stdin.end(); this.process.kill(); }
}

function prepareRuntime() {
  const executable = resolve(process.env.ICAX_SECURITY_WINDOW_BRIDGE || resolve(root, 'tmp/security-window-frames-native/SecurityWindowFramesBridge.exe'));
  assert.ok(existsSync(executable), `Build SecurityWindowFramesBridge first: ${executable}`);
  if (process.env.ICAX_SECURITY_WINDOW_RUNTIME) {
    const runtime = resolve(process.env.ICAX_SECURITY_WINDOW_RUNTIME);
    assert.equal(dirname(executable), runtime, 'The explicit bridge must be inside its isolated runtime');
    assert.ok(existsSync(resolve(runtime, 'TubeDesigner.dll')), 'The isolated native product module is required');
    return runtime;
  }
  const runtime = resolve(root, 'tmp/security-window-parameters-native/runtime');
  mkdirSync(runtime, { recursive: true });
  // The explicitly built bridge's DLLs take precedence over the workspace
  // copies. Reversing this order silently tested an older product module.
  for (const directory of [resolve(root, 'src/x64/Debug'), dirname(executable)]) {
    if (!existsSync(directory)) continue;
    for (const name of readdirSync(directory).filter(name => /\.dll$/i.test(name)))
      copyFileSync(resolve(directory, name), resolve(runtime, name));
  }
  copyFileSync(executable, resolve(runtime, 'SecurityWindowFramesBridge.exe'));
  return runtime;
}

function normalized(parameters = {}) {
  return Object.fromEntries(source.parameters.map(field => [field.key,
    field.readOnly || parameters[field.key] == null ? field.defaultValue : parameters[field.key]]));
}
const escaped = value => value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

// Prepare active host-boundary inputs with the shared condition evaluator.
// Inactive drafts intentionally retain values outside their editing bounds.
function activationParameters(field) {
  if (field.presentation?.visible === false || field.readOnly) return null;
  const candidates = new Map();
  const collect = condition => {
    if (!condition || typeof condition !== 'object') return;
    if (condition.parameter && condition.parameter !== field.key) {
      const definition = definitions.get(condition.parameter);
      const values = candidates.get(condition.parameter) || new Set([definition.defaultValue]);
      for (const value of condition.values || (Object.hasOwn(condition, 'value') ? [condition.value] : [])) values.add(value);
      for (const choice of definition.choices || []) values.add(choice.value);
      if (definition.valueType === 'boolean') { values.add(false); values.add(true); }
      candidates.set(condition.parameter, values);
    }
    for (const value of Object.values(condition)) {
      if (Array.isArray(value)) value.forEach(collect);
      else if (value && typeof value === 'object') collect(value);
    }
  };
  collect(field.visibleWhen); collect(field.enabledWhen);
  const entries = [...candidates];
  function search(index, values) {
    if (index === entries.length) return parameterVisible(field, values) && parameterEnabled(field, values)
      ? Object.fromEntries(entries.filter(([key]) => !Object.is(values[key], defaults[key])).map(([key]) => [key, values[key]])) : null;
    const [key, choices] = entries[index];
    for (const value of choices) {
      const result = search(index + 1, { ...values, [key]: value });
      if (result != null) return result;
    }
    return null;
  }
  return search(0, defaults);
}

function hostCases() {
  const cases = [{ id: 'host.defaults', keys: [...definitions.keys()], kind: 'valid', parameters: {},
    notes: 'Every public default is normalized and exercised by the native preview.' },
  { id: 'host.null-defaults', keys: [...definitions.keys()], kind: 'valid',
    parameters: Object.fromEntries(source.parameters.map(field => [field.key, null])), compareTo: {}, effect: 'none',
    notes: 'Missing/null fields use descriptor defaults without introducing internal parameters.' }];
  for (const field of source.parameters) {
    if (field.readOnly) {
      cases.push({ id: `host.${field.key}.readonly`, keys: [field.key], kind: 'valid',
        parameters: { [field.key]: '__ignored_readonly_input__' }, compareTo: {}, effect: 'none',
        notes: 'Host restores the fixed default before validating a supplied readonly value.' });
      continue;
    }
    const constraints = field.constraints || {};
    const reject = (suffix, value) => cases.push({ id: `host.${field.key}.${suffix}`,
      keys: [field.key], kind: 'rejection', parameters: { [field.key]: value }, reasonPattern: escaped(field.key),
      notes: 'Native descriptor validation must name the offending parameter.' });
    const boundary = (suffix, value) => {
      const activeByDefault = parameterVisible(field, defaults) && parameterEnabled(field, defaults);
      if (activeByDefault) { reject(suffix, value); return; }
      cases.push({ id: `host.${field.key}.${suffix}`, keys: [field.key], kind: 'inactive',
        parameters: { [field.key]: value }, compareTo: {}, effect: 'none',
        notes: 'An inactive draft keeps its editing-boundary value without changing the current design.' });
      const activation = activationParameters(field);
      if (activation != null) cases.push({ id: `host.${field.key}.${suffix}.active`, keys: [field.key], kind: 'rejection',
        parameters: { ...activation, [field.key]: value }, reasonPattern: escaped(field.key),
        notes: 'The same editing boundary must be enforced once the field is applicable.' });
    };
    reject('wrong-type', {});
    if (field.valueType === 'enum') reject('unknown-choice', '__not_a_choice__');
    if (field.valueType === 'number') {
      if (constraints.minimum != null) boundary('below-minimum', constraints.minimum - 1);
      if (constraints.maximum != null) boundary('above-maximum', constraints.maximum + 1);
    }
    if (field.valueType === 'string') {
      if (constraints.minimumLength > 0) boundary('below-minimum-length', '');
      if (constraints.maximumLength != null) boundary('above-maximum-length', 'X'.repeat(constraints.maximumLength + 1));
    }
  }
  cases.push({ id: 'host.unknown-field', keys: [], kind: 'valid', parameters: { unsupportedDraft: 1 },
    compareTo: {}, effect: 'none',
    notes: 'The SDO selects declared fields before codec normalization; an extra draft must not enter returned parameters or geometry.' });
  return cases;
}

function validateDescriptor(native) {
  assert.equal(native.id, templateId, 'Native template ID drift');
  assert.equal(native.version, source.version, 'Native template version drift');
  assert.deepEqual(native.parameters.map(field => field.key).sort(), [...definitions.keys()].sort());
  const nativeFields = new Map(native.parameters.map(field => [field.key, field]));
  for (const field of native.parameters) {
    const declared = definitions.get(field.key);
    assert.deepEqual(field.defaultValue, declared.defaultValue, `${field.key}: default drift`);
    assert.equal(field.valueType, declared.valueType, `${field.key}: type drift`);
    assert.equal(Boolean(field.readOnly), Boolean(declared.readOnly), `${field.key}: readonly drift`);
    if (declared.choices) assert.deepEqual(field.choices.map(choice => choice.value), declared.choices.map(choice => choice.value));
    for (const [sourceKey, nativeKey] of [['minimum', 'min'], ['maximum', 'max'], ['step', 'step']])
      if (declared.constraints?.[sourceKey] != null) assert.equal(field[nativeKey], declared.constraints[sourceKey], `${field.key}: ${sourceKey}`);
  }
  // Validate references and enum membership; evaluation itself uses the shared UI functions.
  function conditionReferences(condition) {
    if (!condition || typeof condition !== 'object') return;
    const key = condition.parameter ?? condition.key ?? condition.name;
    if (key) {
      assert.ok(definitions.has(key), `Condition references unknown parameter ${key}`);
      const field = definitions.get(key);
      if (field.valueType === 'enum') {
        const values = condition.values ?? (Object.hasOwn(condition, 'value') ? [condition.value] : []);
        for (const value of values) assert.ok(field.choices.some(choice => Object.is(choice.value, value)),
          `${key}: condition references unavailable enum value ${value}`);
      }
    }
    for (const value of Object.values(condition)) {
      if (Array.isArray(value)) value.forEach(conditionReferences);
      else if (value && typeof value === 'object') conditionReferences(value);
    }
  }
  for (const field of source.parameters) {
    conditionReferences(field.visibleWhen); conditionReferences(field.enabledWhen);
    for (const condition of Object.values(field.presentation?.choiceConditions || {})) conditionReferences(condition);
  }
  const matrix = [];
  for (const faceType of ['single', 'two', 'three', 'five']) for (const infillPattern of ['vertical', 'horizontal', 'grid'])
    for (const accessDoorEnabled of [false, true]) {
      const values = { ...defaults, faceType, infillPattern, accessDoorEnabled };
      for (const field of source.parameters) {
        const nativeField = nativeFields.get(field.key);
        assert.equal(parameterVisible(nativeField, values), parameterVisible(field, values), `${field.key}: native visibility drift`);
        assert.equal(parameterEnabled(nativeField, values), parameterEnabled(field, values), `${field.key}: native enabled condition drift`);
        assert.deepEqual(availableParameterChoices(nativeField, values).map(choice => choice.value ?? choice),
          availableParameterChoices(field, values).map(choice => choice.value ?? choice), `${field.key}: native choice condition drift`);
      }
      matrix.push({ faceType, infillPattern, accessDoorEnabled,
        parameters: Object.fromEntries(source.parameters.map(field => [field.key, {
          visible: parameterVisible(field, values), enabled: parameterEnabled(field, values),
          choices: availableParameterChoices(field, values).map(choice => choice.value ?? choice),
        }])) });
    }
  return matrix;
}

function validatePreview(preview, expected) {
  assert.equal(preview.templateId, templateId);
  assert.equal(preview.templateVersion, source.version);
  assert.deepEqual(preview.parameters, expected, 'Native returned parameters differ from host normalization');
  assert.ok(preview.items.length > 0, 'Preview has no parts');
  const keys = preview.items.map(item => item.stableKey || item.key);
  assert.equal(new Set(keys).size, keys.length, 'Duplicate native item keys');
  for (const item of preview.items) {
    assert.ok(item.geometry?.url, `${item.key}: missing solid preview resource`);
    assert.equal(item.transform.length, 16, `${item.key}: transform`);
    assert.ok(item.transform.every(Number.isFinite), `${item.key}: non-finite transform`);
    for (const dimension of ['width', 'height', 'depth']) assert.ok(Number.isFinite(item.bounds[dimension]) && item.bounds[dimension] >= 0,
      `${item.key}: invalid ${dimension}`);
  }
  return { itemCount: preview.items.length,
    outerFrameCount: keys.filter(key => key.startsWith('outer_frame.')).length };
}

const builders = [...buildGeometryCases(source), ...buildMaterialCases(source), ...buildProcessCases(source)];
const allCases = [...hostCases(), ...builders];
assert.equal(new Set(allCases.map(test => test.id)).size, allCases.length, 'Duplicate case IDs');
for (const test of allCases) {
  assert.ok(['valid', 'inactive', 'rejection'].includes(test.kind), `${test.id}: kind`);
  for (const key of test.keys) assert.ok(definitions.has(key), `${test.id}: unknown coverage key ${key}`);
}
// Defaults and generic invalid input do not count as a parameter-specific valid exercise.
for (const field of source.parameters) {
  assert.ok(builders.some(test => test.keys.includes(field.key) && test.kind === 'valid'),
    `No parameter-specific legal exercise: ${field.key}`);
  for (const choice of field.choices || []) assert.ok(builders.some(test => test.keys.includes(field.key)
    && test.kind === 'valid' && Object.is(test.parameters[field.key], choice.value)),
  `Untested public choice: ${field.key}=${choice.value}`);
}
const cases = allCases.filter(test => !filter || filter.test(test.id));
assert.ok(cases.length, 'No cases match the filter');
const report = { templateId, version: source.version, startedAt: new Date().toISOString(),
  caseDefinitionHash: createHash('sha256').update(JSON.stringify(allCases)).digest('hex'),
  parameterCount: source.parameters.length, fullRun: !filter, totalDefinedCases: allCases.length,
  selectedCases: cases.length, cases: [], coverage: [], conditions: [] };

function saveReport() {
  report.coverage = source.parameters.map(field => {
    const rows = report.cases.filter(row => row.keys.includes(field.key));
    return { key: field.key, label: typeof field.displayName === 'object'
      ? field.displayName['zh-CN'] || field.displayName.en || field.key : field.displayName || field.label || field.key,
      group: field.groupKey || field.group,
      valid: rows.filter(row => row.kind === 'valid' && !row.id.startsWith('host.')).length,
      inactive: rows.filter(row => row.kind === 'inactive').length,
      rejection: rows.filter(row => row.kind === 'rejection').length,
      passed: rows.filter(row => row.status === 'passed').length, failed: rows.filter(row => row.status === 'failed').length,
      failures: rows.filter(row => row.status === 'failed').map(row => row.id) };
  });
  report.passed = report.cases.filter(row => row.status === 'passed').length;
  report.failed = report.cases.filter(row => row.status === 'failed').length;
  writeFileSync(resolve(output, 'results.json'), JSON.stringify(report, null, 2));
  writeFileSync(resolve(output, 'failures.json'), JSON.stringify(report.cases.filter(row => row.status === 'failed'), null, 2));
  const html = value => String(value ?? '').replace(/[&<>"']/g, char => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[char]));
  writeFileSync(resolve(output, 'report.html'), `<!doctype html><meta charset="utf-8"><title>防盗窗全参数自动化测试</title>
<style>body{font:15px/1.6 system-ui;margin:32px auto;max-width:1200px;color:#213544;background:#f6f8fa}h1{font-size:26px}table{border-collapse:collapse;width:100%;background:white}th,td{padding:7px 10px;border:1px solid #dae0e5;text-align:left}th{background:#e9eef2}.bad{color:#b42318}.ok{color:#067647}details{margin:12px 0;padding:12px;background:white;border:1px solid #dae0e5}pre{white-space:pre-wrap;overflow-wrap:anywhere}small{color:#657584}</style>
<h1>防盗窗全参数自动化测试</h1><p>${html(templateId)} · ${html(source.version)} · ${report.parameterCount} 项参数</p>
<p>已完成 ${report.cases.length}/${report.selectedCases} 用例：<b class="ok">${report.passed} 通过</b>，<b class="bad">${report.failed} 失败</b>。${report.fullRun ? '完整运行' : '按筛选条件运行'}。</p>
<p>每项合法配置调用原生预览，核对完整宿主参数；另比较实体声明图。制造用例额外调用原生制造计划。隐藏草稿必须保持几何不变。失败保留配置与实际错误。</p>
${report.fatalError ? `<p class="bad">运行错误：${html(report.fatalError)}</p>` : ''}
<h2>逐参数覆盖</h2><table><tr><th>参数</th><th>标识</th><th>合法</th><th>隐藏联动</th><th>拒绝检查</th><th>失败</th></tr>${report.coverage.map(row => `<tr><td>${html(row.label)}</td><td>${html(row.key)}</td><td>${row.valid}</td><td>${row.inactive}</td><td>${row.rejection}</td><td class="${row.failed ? 'bad' : 'ok'}">${row.failed}</td></tr>`).join('')}</table>
<h2>失败复现</h2>${report.cases.filter(row => row.status === 'failed').map(row => `<details open><summary class="bad">${html(row.id)}</summary><p>${html(row.notes)}</p><pre>${html(row.error)}</pre><pre>${html(JSON.stringify({ parameters: row.parameters, compareTo: row.compareTo, expected: row.kind, effect: row.effect, purpose: row.purpose }, null, 2))}</pre></details>`).join('') || '<p>当前已完成用例无失败。</p>'}
<h2>全部用例</h2>${report.cases.map(row => `<details><summary class="${row.status === 'failed' ? 'bad' : 'ok'}">${html(row.status)} · ${html(row.id)}</summary><pre>${html(JSON.stringify(row, null, 2))}</pre></details>`).join('')}`);
}

const runtime = prepareRuntime();
const python = process.env.ICAX_PYTHON || resolve(process.env.USERPROFILE || '', '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe');
assert.ok(existsSync(python), `Set ICAX_PYTHON to a Python 3 executable: ${python}`);
const probePath = resolve(root, 'src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/SecurityWindowParameterProbe.py');
const workers = Array.from({ length: jobs }, () => ({
  native: new JsonProcess(resolve(runtime, 'SecurityWindowFramesBridge.exe'), [], { cwd: process.env.ICAX_SECURITY_WINDOW_RUNTIME ? runtime : root,
    env: { ...process.env, PATH: runtime + ';' + resolve(root, 'src/x64/Debug') + ';' + process.env.PATH } }),
  python: new JsonProcess(python, ['-u', probePath], { cwd: root, env: { ...process.env, PYTHONUTF8: '1' } }),
  cache: new Map(),
}));
let nextCase = 0;

async function evaluate(worker, parameters, purpose, options = {}) {
  const { nativeDisassembly = false, expectedProcessParameters = {}, componentPrefix = null,
    nativeHorizontalEndFit = false, nativeMiterContact = false } = options;
  const cacheKey = JSON.stringify({ parameters, purpose, nativeDisassembly, expectedProcessParameters, componentPrefix,
    nativeHorizontalEndFit, nativeMiterContact });
  if (worker.cache.has(cacheKey)) return worker.cache.get(cacheKey);
  const original = structuredClone(parameters);
  const expected = normalized(parameters);
  const preview = await worker.native.invoke('GenerateProductTemplatePreview', { templateId, parameters });
  const summary = validatePreview(preview, expected);
  let plan;
  let nativeSolid;
  if (purpose === 'manufacturing') {
    plan = await worker.native.invoke('GetProductManufacturingPlan', { templateId, parameters });
    assert.equal(plan.templateId, templateId);
    assert.equal(plan.templateVersion, source.version);
    assert.deepEqual(plan.parameters, expected, 'Native manufacturing plan changed host-normalized parameters');
  }
  if (nativeDisassembly) {
    assert.equal(purpose, 'manufacturing', 'Native disassembly requires a manufacturing case');
    const generated = await worker.native.invoke('GeneratePreview', { templateId, ...expected });
    assert.deepEqual(generated.tubeDesigner.product.parameters, expected, 'Native product changed host-normalized parameters');
    const disassembled = await worker.native.invoke('Disassemble', {});
    assert.deepEqual(disassembled.tubeDesigner.product.parameters, expected, 'Native disassembly changed host-normalized parameters');
    assert.ok(disassembled.geometryChecks.length, 'Native disassembly has no final solids');
    for (const check of disassembled.geometryChecks) {
      assert.equal(check.valid, true, `${check.key}: invalid native BRep`);
      assert.equal(check.solids, 1, `${check.key}: disconnected native solid`);
      assert.ok(Number.isFinite(check.volume) && check.volume > 0, `${check.key}: invalid native volume`);
    }
    const resources = await worker.native.invoke('InspectScriptResources', {});
    assert.deepEqual(resources.generatedParameters, expected, 'Persisted generation changed host-normalized parameters');
    assert.deepEqual(resources.manufacturingParameters, expected, 'Persisted manufacturing changed host-normalized parameters');
    const process = resources.manufacturing.document.processes.find(row => row.key === 'window.assembly');
    assert.ok(process, 'Native disassembly did not preserve its actual assembly declaration');
    for (const [key, value] of Object.entries(expectedProcessParameters))
      assert.deepEqual(process.definition.parameters[key], value, `${key}: native assembly used an inapplicable draft`);
    nativeSolid = { count: disassembled.geometryChecks.length,
      hash: createHash('sha256').update(JSON.stringify(disassembled.geometryChecks
        .map(check => ({ key: check.key, solids: check.solids, volume: Math.round(check.volume * 1e6) / 1e6 }))
        .sort((a, b) => a.key.localeCompare(b.key)))).digest('hex'),
      processParameters: Object.fromEntries(Object.keys(expectedProcessParameters)
        .map(key => [key, process.definition.parameters[key]])) };
    if (nativeHorizontalEndFit) nativeSolid.horizontalEndFit =
      await assertHorizontalNativeEndFit(worker.native, disassembled, expected);
    if (nativeMiterContact) nativeSolid.miterContact = await assertNativeMiterContact(worker.native, resources);
  }
  const neutral = await worker.python.invoke('Probe', { parameters: expected, purpose, componentPrefix });
  assert.deepEqual(parameters, original, 'Test input mutated');
  assert.deepEqual(neutral.parameters, expected);
  assert.equal(neutral.itemCount, summary.itemCount, 'Native/neutral item count differs');
  assert.equal(neutral.outerFrameCount, summary.outerFrameCount, 'Native/neutral frame decomposition differs');
  const result = { ...summary, geometryNodeCount: neutral.geometryNodeCount,
    hash: purpose === 'manufacturing' ? neutral.manufacturingHash : neutral.geometryHash,
    ...(plan ? { plannedParts: plan.partCount } : {}), ...(nativeSolid ? { nativeSolid } : {}),
    ...(componentPrefix ? { componentHash: neutral.componentHash } : {}) };
  worker.cache.set(cacheKey, result);
  return result;
}

async function runCase(worker, test) {
  const started = Date.now();
  const purpose = test.purpose || 'display';
  const row = { ...test, purpose, status: 'passed' };
  const values = normalized(test.parameters);
  row.presentation = Object.fromEntries(test.keys.map(key => [key, {
    visible: parameterVisible(definitions.get(key), values), enabled: parameterEnabled(definitions.get(key), values),
  }]));
  try {
    if (test.kind === 'rejection') {
      let failure;
      row.nativeStages = [];
      const check = async (method, payload) => {
        try {
          await worker.native.invoke(method, payload);
          row.nativeStages.push({ method, accepted: true });
        } catch (error) {
          row.nativeStages.push({ method, accepted: false });
          row.rejectionMethod = method;
          throw error;
        }
      };
      // Expected rejection must come from the native host, not the Python oracle.
      try {
        await check('GenerateProductTemplatePreview', { templateId, parameters: test.parameters });
        if (purpose === 'manufacturing') {
          await check('GetProductManufacturingPlan', { templateId, parameters: test.parameters });
          // The plan endpoint deliberately stops before executing cutter and
          // receiver-fit providers. Domain boundaries must reach Disassemble.
          if (!test.id.startsWith('host.')) {
            await check('GeneratePreview', { templateId, ...values });
            await check('Disassemble', {});
          }
        }
      } catch (error) { failure = error; }
      assert.ok(failure, 'Native accepted a configuration that must be rejected');
      assert.doesNotMatch(failure.message, /Process exited|Process unavailable|Timeout:|Non-JSON|TemplateRuntimeUnavailable|ModuleNotFoundError|ImportError|SyntaxError|std::bad_alloc|cannot load.*(?:module|DLL)/i,
        'Infrastructure failure is not a valid rejection');
      row.rejectionReason = failure.message;
      if (test.reasonPattern) assert.match(failure.message, new RegExp(test.reasonPattern, 'i'), 'Wrong rejection reason');
    } else {
      row.actual = await evaluate(worker, test.parameters, purpose, test);
      if (test.compareTo != null) {
        row.baseline = await evaluate(worker, test.compareTo, purpose, test);
        if (test.effect === 'geometry') assert.notEqual(row.actual.hash, row.baseline.hash, 'Active parameter failed to change the solid/process graph');
        if (test.effect === 'none' || test.kind === 'inactive') assert.equal(row.actual.hash, row.baseline.hash, 'Inactive/identity input changed the solid/process graph');
        if (test.nativeSolidEffect === 'geometry') assert.notEqual(row.actual.nativeSolid.hash, row.baseline.nativeSolid.hash,
          'Active parameter failed to change the committed native solids');
        if (test.nativeSolidEffect === 'none') assert.equal(row.actual.nativeSolid.hash, row.baseline.nativeSolid.hash,
          'Inapplicable draft changed the committed native solids');
        if (test.componentEffect === 'none') assert.equal(row.actual.componentHash, row.baseline.componentHash,
          `${test.componentPrefix}: another component's draft changed its actual geometry`);
      }
      if (test.expectedFrames != null) assert.equal(row.actual.outerFrameCount, test.expectedFrames);
    }
  } catch (error) { row.status = 'failed'; row.error = error.message; }
  row.seconds = (Date.now() - started) / 1000;
  report.cases.push(row);
  saveReport();
  if (row.status === 'failed') console.log(`FAIL ${test.id}: ${row.error.split('\n')[0]}`);
  if (report.cases.length % 25 === 0 || report.cases.length === cases.length)
    console.log(`${report.cases.length}/${cases.length}: ${report.passed} passed, ${report.failed} failed`);
}

try {
  const nativeDescriptor = (await workers[0].native.invoke('GetTemplateDescriptor', { templateId })).template;
  if (process.env.ICAX_SECURITY_WINDOW_RUNTIME) {
    const { modules } = await workers[0].native.invoke('GetRuntimeModules', {});
    report.nativeModules = Object.fromEntries(Object.entries(modules).map(([name, path]) => [name, {
      path, sha256: createHash('sha256').update(readFileSync(path)).digest('hex'),
    }]));
    for (const { path } of Object.values(report.nativeModules))
      assert.equal(dirname(path).toLowerCase(), runtime.toLowerCase(), 'Native acceptance must load the isolated runtime modules');
  }
  report.packageDigest = nativeDescriptor.packageDigest;
  report.conditions = validateDescriptor(nativeDescriptor);
  assert.deepEqual(nativeDescriptor.extensions.productDiagram.sceneBindings, source.extensions.productDiagram.sceneBindings,
    'Native descriptor scene bindings differ from the current source template');
  report.nativeDescriptorSceneBindingsExactlyMatchSource = true;
  writeFileSync(resolve(output, 'native-descriptor.json'), JSON.stringify(nativeDescriptor, null, 2));
  console.log(`Security window ${source.version}: ${source.parameters.length} public parameters, ${cases.length} cases, ${jobs} native workers`);
  await Promise.all(workers.map(async worker => {
    while (nextCase < cases.length) await runCase(worker, cases[nextCase++]);
  }));
  report.finishedAt = new Date().toISOString();
  saveReport();
  assert.equal(report.cases.length, cases.length);
  if (report.failed) process.exitCode = 1;
  console.log(`Report: ${resolve(output, 'report.html')}`);
} catch (error) {
  report.fatalError = error.stack;
  saveReport(); process.exitCode = 1;
  console.error(error.stack);
} finally {
  for (const worker of workers) { worker.native.stop(); worker.python.stop(); }
}
