// Drive only isolated harness processes. Timeouts are reported; no process is killed.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createHash, randomUUID } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const variant = process.env.ICAX_CEF_SHUTDOWN_VARIANT || 'baseline';
assert.ok(['baseline', 'fixed'].includes(variant));
const binaries = resolve(process.env.ICAX_CEF_SHUTDOWN_BINARIES || resolve(root, 'output/tests/cef-shutdown', variant));
const executable = resolve(binaries, 'CefShutdownHarness.exe');
const cases = (process.env.ICAX_CEF_SHUTDOWN_CASES || (variant === 'baseline' ? 'window' : 'window,stop,immediate,popup')).split(',');
const output = resolve(root, 'output/tests/cef-shutdown', `${variant}-native.json`);
const protectedPids = (process.env.ICAX_CEF_PROTECTED_PIDS || '').split(',').filter(Boolean).map(Number);
assert.ok(protectedPids.every(pid => Number.isSafeInteger(pid) && pid > 0));
const protectedSnapshot = () => {
  if (!protectedPids.length) return [];
  const result = spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command',
    `Get-Process -Id ${protectedPids.join(',')} -ErrorAction SilentlyContinue | Select-Object Id,StartTime,Path | ConvertTo-Json -Compress`],
  { windowsHide: true, encoding: 'utf8', timeout: 10000 });
  assert.equal(result.status, 0, result.stderr);
  return result.stdout.trim() ? [JSON.parse(result.stdout)].flat().sort((a, b) => a.Id - b.Id) : [];
};
const report = { passed: false, variant, executable, binaries, noForcedTermination: true,
  isolatedProfiles: true, protectedBefore: protectedSnapshot(), cases: [],
  binaryManifest: JSON.parse(readFileSync(resolve(binaries, 'binary-manifest.json'), 'utf8').replace(/^\uFEFF/, '')),
  executableSha256: createHash('sha256').update(readFileSync(executable)).digest('hex') };
const save = () => writeFileSync(output, JSON.stringify(report, null, 2) + '\n');
save();
try {
  for (const mode of cases) {
    const directory = resolve(binaries, 'fixtures', `${mode}-${randomUUID()}`);
    mkdirSync(directory, { recursive: true });
    const start = Date.now();
    const child = spawn(executable, [mode, directory, ...(mode === 'popup' ? ['--disable-popup-blocking'] : [])],
      { cwd: binaries, windowsHide: true, env: { ...process.env, TEMP: directory, TMP: directory }, stdio: ['ignore', 'pipe', 'pipe'] });
    assert.ok(!protectedPids.includes(child.pid));
    const entry = { mode, directory, hostPid: child.pid, events: [], stderr: '', completed: false };
    report.cases.push(entry); save();
    child.stderr.on('data', data => { entry.stderr = (entry.stderr + data).slice(-12000); });
    createInterface({ input: child.stdout }).on('line', line => {
      try { const event = JSON.parse(line); entry.events.push(event); }
      catch { entry.stderr = (entry.stderr + '\n' + line).slice(-12000); }
      save();
    });
    const result = await new Promise((resolveCase, reject) => {
      const timer = setTimeout(() => {
        entry.timedOut = true; save();
        // Keep this owned process observable for a cooperative fix; never force termination.
        reject(new Error(`Owned harness ${child.pid} timed out; it was not killed`));
      }, 60000);
      child.on('error', error => { clearTimeout(timer); reject(error); });
      child.on('exit', (code, signal) => { clearTimeout(timer); resolveCase({ code, signal }); });
    });
    entry.exit = result; entry.milliseconds = Date.now() - start; entry.completed = true; save();
    assert.equal(result.code, 0, JSON.stringify(entry));
    assert.equal(result.signal, null);
    const event = name => entry.events.find(item => item.event === name);
    assert.ok(event('passed'));
    assert.ok(event('shutdown_enter') && event('shutdown_return'));
    assert.equal(event('shutdown_enter').threadId, event('start_enter').threadId);
    assert.ok(entry.events.filter(item => item.event === 'child_exit').every(item => !item.detail.endsWith(':259')));
    if (mode === 'window') assert.ok(event('wait_for_exit_return').milliseconds <= event('stop_enter').milliseconds);
    if (variant === 'fixed') {
      assert.ok(event('windows_destroyed'));
      assert.ok(event('wrong_thread_shutdown_rejected'));
      assert.notEqual(event('wrong_thread_shutdown_rejected').threadId, event('start_enter').threadId);
    } else {
      assert.ok(event('baseline_shutdown_not_invoked'));
      entry.isolatedOmissionEvidence = { childrenAliveBeforeExplicitCleanup: Number(event('children_before_shutdown').detail),
        explicitCleanupOnlyAfterStop: event('shutdown_enter').milliseconds >= event('stop_return').milliseconds,
        interpretation: 'Isolated Stop omits runtime shutdown; this does not reproduce the user process exit duration.' };
    }
    save();
    console.log(JSON.stringify({ variant, mode, completed: entry.completed, milliseconds: entry.milliseconds,
      children: entry.events.filter(item => item.event === 'owned_child_observed').length, isolatedOmissionEvidence: entry.isolatedOmissionEvidence }));
  }
  report.protectedAfter = protectedSnapshot();
  assert.deepEqual(report.protectedAfter, report.protectedBefore, 'User processes must keep their original identities');
  report.passed = true;
} catch (error) { report.error = String(error.stack || error); process.exitCode = 1; }
finally { save(); console.log(JSON.stringify({ passed: report.passed, output, error: report.error })); }
