// Real registered native template rules and cutter construction for every tool.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdirSync, mkdtempSync, writeFileSync } from 'node:fs';
import { delimiter, join, resolve } from 'node:path';
import { createInterface } from 'node:readline';
import { buildToolLibraryPreviewPayload, libraryTools, toolLibraryState } from '../../apps/tube-designer/webpage/toolLibrary.mjs';

assert(process.env.ICAX_NATIVE_RUNTIME_ROOT && process.env.ICAX_NATIVE_SCOPE_BRIDGE,
  'Set ICAX_NATIVE_RUNTIME_ROOT and ICAX_NATIVE_SCOPE_BRIDGE');
const runtime = resolve(process.env.ICAX_NATIVE_RUNTIME_ROOT);
const root = resolve(process.env.ICAX_TOOL_APPLICABILITY_REPORT_ROOT || 'output/tests/tool-applicability-20261006');
mkdirSync(root, { recursive: true });
const scenario = mkdtempSync(join(root, 'native-'));
const userData = join(scenario, 'user-data');
mkdirSync(userData);
const env = { ...process.env, ICAX_AUTOMATION_USER_DATA: userData };
const oldPath = Object.entries(env).find(([key]) => key.toLowerCase() === 'path')?.[1] || '';
for (const key of Object.keys(env)) if (key.toLowerCase() === 'path') delete env[key];
env.Path = runtime + delimiter + oldPath;
const child = spawn(resolve(process.env.ICAX_NATIVE_SCOPE_BRIDGE), [], { cwd: runtime, env, windowsHide: true });
let sequence = 0, stderr = '';
const waiting = new Map();
const calls = [];
const report = { status: 'running', runtime, scenario, nativeBusinessResponsesMocked: false, tools: [], checks: [] };
child.stderr.on('data', bytes => stderr += bytes.toString());
createInterface({ input: child.stdout }).on('line', line => {
  let response; try { response = JSON.parse(line); } catch { return; }
  const job = waiting.get(response.id); if (!job) return;
  waiting.delete(response.id); clearTimeout(job.timer);
  calls.push({ request: job.request, response, milliseconds: Date.now() - job.started });
  response.ok ? job.resolve(response.result) : job.reject(new Error(response.error));
});
const rejectPending = error => { for (const job of waiting.values()) { clearTimeout(job.timer); job.reject(error); } waiting.clear(); };
child.on('error', rejectPending);
child.on('exit', code => { if (waiting.size) rejectPending(new Error(`Native exit ${code}: ${stderr}`)); });
function invoke(method, payload = {}, scope = 'scene') {
  const request = { id: ++sequence, scope, method, payload };
  return new Promise((resolveCall, reject) => {
    const timer = setTimeout(() => { waiting.delete(request.id); reject(new Error(`Timeout ${method}`)); }, 180000);
    waiting.set(request.id, { request, resolve: resolveCall, reject, timer, started: Date.now() });
    child.stdin.write(JSON.stringify(request) + '\n');
  });
}
function assertPreview(preview, id) {
  assert.equal(preview.toolsOnly, true, `${id}: only the cutter preview is constructed`);
  assert.equal(preview.previewToolsComplete, true, `${id}: complete actual cutter construction`);
  assert.ok(preview.baseGeometry?.url && preview.baseGeometry.version > 0, `${id}: actual blank mesh`);
  assert.ok(preview.toolPreviews?.length, `${id}: actual cutter mesh`);
  assert.equal(preview.resultError, undefined, `${id}: no hidden failure`);
}
function assertProcessUnchanged(before, after, id) {
  assert.deepEqual(after.features.map(item => item.toolParameters), before.features.map(item => item.toolParameters), `${id}: process parameters retained`);
  for (const key of ['start', 'end']) assert.deepEqual(after.ends[key].toolParameters, before.ends[key].toolParameters, `${id}: end parameters retained`);
}
let catalogue, profiles;
async function fixture(id) {
  const descriptor = catalogue.find(item => item.id === id);
  assert(descriptor);
  const view = { activeAreaId: 'tools', tubeDesignerSystemPunchTools: [descriptor],
    tubeDesignerSystemProfiles: profiles, tubeDesignerToolLibrary: { scope: 'system', selectedKey: `system::${id}` } };
  const tool = libraryTools(view)[0];
  return { view, tool, state: toolLibraryState(view) };
}
async function payloadFor(view, tool) {
  const draft = buildToolLibraryPreviewPayload(view, tool);
  const main = await invoke('EvaluateProfilePackage', { profileRef: draft.profileRef, parameters: draft.parameters });
  const section = draft.features[0]?.section || draft.ends.start.section;
  const branch = section?.profileRef ? await invoke('EvaluateProfilePackage', { profileRef: section.profileRef, parameters: section.parameters }) : null;
  return buildToolLibraryPreviewPayload(view, tool, { profileSnapshot: main.profile, branchProfileSnapshot: branch?.profile });
}
try {
  report.modules = (await invoke('GetRuntimeModules', {}, 'inspection')).modules;
  catalogue = (await invoke('GetPunchTools')).tools.filter(item => item.libraryScope === 'system');
  profiles = (await invoke('ListSystemProfiles')).systemProfiles.map(item => ({ ...item, libraryScope: 'system' }));
  assert.equal(catalogue.length, 24, 'Every current built-in tool is covered');
  {
    const { view, tool, state } = await fixture('edge-arc-groove');
    buildToolLibraryPreviewPayload(view, tool);
    state.profileKey = 'system:round';
    state.profileDrafts['system:round'] = { width: 40, wallThickness: 2 };
    const payload = await payloadFor(view, tool);
    const check = await invoke('CheckPunchToolApplicability', payload);
    assert.equal(check.applicable, false, 'Edge arc groove must reject a round target without flat supports');
    const recommendation = await invoke('GetPunchToolProfileRecommendation', payload);
    assert.equal(recommendation.available, true, recommendation.reason);
    assert.deepEqual(Object.keys(recommendation.profiles), ['main']);
    assertProcessUnchanged(payload, recommendation.previewPayload, tool.id);
    const choice = recommendation.profiles.main;
    state.profileKey = `${choice.profileRef.scope}:${choice.profileRef.id}`;
    state.profileDrafts[state.profileKey] = choice.parameters;
    const rebuilt = await payloadFor(view, tool);
    assert.deepEqual(recommendation.previewPayload.features[0].section.profile,
      rebuilt.features[0].section.profile,
      'Main-only suggestion must return the same part-section snapshot as applying it in the UI');
    const reapplied = await invoke('CheckPunchToolApplicability', rebuilt);
    assert.equal(reapplied.applicable, true, reapplied.reason);
    assertPreview(reapplied.preview, tool.id);
    report.checks.push('Main-only round-to-rect suggestion replaces the part-section snapshot exactly as the UI rebuild does');
  }
  for (const descriptor of catalogue) {
    assert.ok(descriptor.preview?.profileRef, `${descriptor.id}: descriptor-owned recommendation`);
    if (descriptor.requiresSection) assert.ok(descriptor.preview.section?.profileRef);
    const { view, tool } = await fixture(descriptor.id);
    const payload = await payloadFor(view, tool);
    const check = await invoke('CheckPunchToolApplicability', payload);
    assert.equal(check.applicable, true, `${tool.id}: ${check.reason}`);
    assertPreview(check.preview, tool.id);
    const recommendation = await invoke('GetPunchToolProfileRecommendation', payload);
    assert.equal(recommendation.available, true, `${tool.id}: ${recommendation.reason}`);
    assertProcessUnchanged(payload, recommendation.previewPayload, tool.id);
    assertPreview(recommendation.preview, tool.id);
    const actual = await invoke('CheckPunchToolApplicability', recommendation.previewPayload);
    assert.equal(actual.applicable, true, `${tool.id}: returned recommendation must really work`);
    report.tools.push({ id: tool.id, profileRef: payload.profileRef,
      sectionRef: payload.features[0]?.section?.profileRef || payload.ends.start.section?.profileRef,
      cutterCount: check.preview.toolPreviews.length, recommendationRoles: Object.keys(recommendation.profiles) });
    console.log(`PASS native applicability and verified recommendation: ${tool.id}`);
  }
  for (const id of ['contact-outline-mark', 'curve-pocket']) {
    const { view, tool } = await fixture(id);
    const payload = await payloadFor(view, tool);
    assert.ok(payload.features[0].section.profile.contours.length, `${id}: side input receives complete contours`);
    const missing = structuredClone(payload); delete missing.features[0].section;
    const check = await invoke('CheckPunchToolApplicability', missing);
    assert.equal(check.applicable, false);
    assert.match(check.reason, id === 'curve-pocket' ? /闭合切除轮廓/ : /支管截面/);
    const recommendation = await invoke('GetPunchToolProfileRecommendation', missing);
    assert.equal(recommendation.available, true, recommendation.reason);
    assert.deepEqual(recommendation.previewPayload.profileRef, payload.profileRef);
    assert.deepEqual(recommendation.previewPayload.parameters, payload.parameters);
    assert.deepEqual(Object.keys(recommendation.profiles), ['branch']);
    assertProcessUnchanged(payload, recommendation.previewPayload, id);
    report.checks.push(`${id}: complete side contours, genuine missing-input error, verified section-only repair`);
  }
  for (const id of ['paired-side-slots', 'paired-end-tabs']) {
    const { view, tool, state } = await fixture(id);
    buildToolLibraryPreviewPayload(view, tool);
    state.profileDrafts['system:rect'] = { width: 72, depth: 50, wallThickness: 2 };
    state.branchProfileKey = 'system:round';
    state.branchProfileDrafts['system:round'] = { width: 20, wallThickness: 2 };
    state.parameterDrafts[tool.libraryKey] = { pairCount: 4, tabWidth: 10, tabLength: 12 };
    const payload = await payloadFor(view, tool);
    const check = await invoke('CheckPunchToolApplicability', payload);
    assert.equal(check.applicable, false);
    assert.match(check.reason, /平直壁面/);
    const recommendation = await invoke('GetPunchToolProfileRecommendation', payload);
    assert.equal(recommendation.available, true, recommendation.reason);
    assert.deepEqual(Object.keys(recommendation.profiles), ['branch'], `${id}: keep a valid custom target tube`);
    assert.deepEqual(recommendation.previewPayload.parameters, payload.parameters);
    assertProcessUnchanged(payload, recommendation.previewPayload, id);
    assertPreview(recommendation.preview, id);
    report.checks.push(`${id}: circular partner rejected, only partner replaced, 72x50 target and four 10mm tabs retained`);
    const invalid = structuredClone(payload);
    const operation = invalid.features[0] || invalid.ends.start;
    operation.toolParameters.tabWidth = 10001;
    const invalidCheck = await invoke('CheckPunchToolApplicability', invalid);
    assert.equal(invalidCheck.applicable, false);
    assert.match(invalidCheck.reason, /超出允许范围/);
    const invalidRecommendation = await invoke('GetPunchToolProfileRecommendation', invalid);
    assert.equal(invalidRecommendation.available, false, 'An invalid parameter is not hidden by changing profiles');
    assert.match(invalidRecommendation.reason, /超出允许范围/);
  }
  const { view, tool } = await fixture('circle');
  const payload = await payloadFor(view, tool);
  await assert.rejects(invoke('CheckPunchToolApplicability', payload, 'product'), /场景上下文/);
  await assert.rejects(invoke('GetPunchToolProfileRecommendation', payload, 'product'), /场景上下文/);
  report.checks.push('Product channel rejected; real scene channel used throughout');
  report.status = 'passed';
} catch (error) { report.status = 'failed'; report.error = error.stack; throw error; }
finally {
  writeFileSync(join(scenario, 'report.json'), JSON.stringify(report, null, 2));
  writeFileSync(join(scenario, 'calls.json'), JSON.stringify(calls, null, 2));
  writeFileSync(join(scenario, 'stderr.txt'), stderr);
  child.stdin.end(); child.kill();
  console.log(`Native evidence: ${scenario}`);
}
