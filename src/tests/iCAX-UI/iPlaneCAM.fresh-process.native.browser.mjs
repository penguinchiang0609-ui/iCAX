import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const source = resolve(fileURLToPath(new URL("../../", import.meta.url))), root = resolve(source, "..");
const output = resolve(process.env.ICAX_PLANE_TEST_OUTPUT || resolve(root, "output/tests/iplane-cam-worktable/source-fresh-process"));
mkdirSync(output, { recursive: true });
const report = { pass: false, scope: "Four independent Node child processes each start and fully terminate their own native bridge plus real Chromium AppShell/SDK. Real GUI discard and actual native process crash are tested separately. Each fresh bridge uses a unique empty data directory; legitimate journal recovery is retained. Native resource GETs, exact source curves, typed snapshots, renderer pixels/UV/materials and missing original input files are verified. No backend/cache mocking or journal suppression. Desktop CEF and OS dialogs are outside scope.", assetRoot: resolve(process.env.ICAX_PLANE_ASSET_ROOT || source), executable: resolve(process.env.ICAX_PLANE_NATIVE_BRIDGE), phases: [], checks: [], assets: {}, binaries: {}, pageErrors: [], failures: [] };
const script = fileURLToPath(new URL("./iPlaneCAM.fresh-process.native.browserPhase.mjs", import.meta.url));
report.testSources = Object.fromEntries(["iPlaneCAM.fresh-process.native.browser.mjs", "iPlaneCAM.fresh-process.native.browserPhase.mjs", "iPlaneCAM.native.browserHarness.mjs", "iPlaneCAM.worktable.native.browserCase.mjs"].map(name => { const path = "tests/iCAX-UI/" + name;return [path, createHash("sha256").update(readFileSync(resolve(source, path))).digest("hex")]; }));
const alive = pid => { try { process.kill(pid, 0);return true; } catch (error) { if (error.code === "ESRCH") return false;throw error; } };
const mergeHashes = (target, incoming, kind) => { for (const [name, hash] of Object.entries(incoming || {})) { assert.match(hash, /^[a-f0-9]{64}$/, kind + " missing: " + name);if (name in target) assert.equal(target[name], hash, kind + " changed between independent processes: " + name);target[name] = hash; } };
async function phase(name, scenario) {
  const phaseOutput = resolve(output, scenario, name.startsWith("prepare") ? "prepare" : "reopen"), manifest = resolve(output, scenario, "manifest.json");mkdirSync(phaseOutput, { recursive: true });
  const startedAt = new Date().toISOString(), child = spawn(process.execPath, [script], { cwd: root, windowsHide: true, stdio: ["ignore", "pipe", "pipe"], env: { ...process.env, ICAX_PLANE_TEST_OUTPUT: phaseOutput, ICAX_PLANE_FRESH_PHASE: name, ICAX_PLANE_FRESH_MANIFEST: manifest } });
  const stdout = [], stderr = [];child.stdout.on("data", data => { stdout.push(data);process.stdout.write(data); });child.stderr.on("data", data => { stderr.push(data);process.stderr.write(data); });
  const exit = await new Promise((done, reject) => { child.once("error", reject);child.once("close", (code, signal) => done({ code, signal })); });
  const path = resolve(phaseOutput, "fresh-process-phase-report.json"), result = existsSync(path) ? JSON.parse(readFileSync(path, "utf8")) : null;
  const entry = { name, scenario, pid: child.pid, startedAt, closedAt: new Date().toISOString(), exit, report: path, nativeProcess: result?.nativeProcess, pass: Boolean(exit.code === 0 && result?.pass), stdout: Buffer.concat(stdout).toString(), stderr: Buffer.concat(stderr).toString() };report.phases.push(entry);
  if (result) { mergeHashes(report.assets, result.assets, "Loaded asset");mergeHashes(report.binaries, result.binaries, "Native binary");report.pageErrors.push(...result.pageErrors || []);report.failures.push(...result.failures || []); }
  assert.equal(entry.nativeProcess?.closed, true, "Native bridge must have actually closed before another process starts");assert.equal(alive(entry.nativeProcess.pid), false, "Old native PID is still alive");
  assert.equal(entry.nativeProcess.dataDirectoryExistedBeforeStart, false, "Each phase must start with a fresh native user-data directory");
  for (const previous of report.phases.slice(0, -1)) { assert.notEqual(previous.pid, entry.pid);assert.notEqual(previous.nativeProcess.pid, entry.nativeProcess.pid);assert.notEqual(previous.nativeProcess.dataDirectory, entry.nativeProcess.dataDirectory); }
  assert.equal(entry.pass, true, result?.error || entry.stderr || name + " failed");return result;
}
try {
  const scenarioErrors = [];
  for (const scenario of ["discard", "recovery"]) { try {
    const prepared = await phase("prepare-" + scenario, scenario), reopened = await phase("reopen-" + scenario, scenario);
    assert.equal(reopened.filesBeforeOpen.project.sha256, prepared.projectFiles.project.sha256, "Main saved project changed between process termination and fresh open");
    assert.equal(reopened.filesAfterOpen.project.sha256, prepared.projectFiles.project.sha256, "Read-only recovery unexpectedly changed the saved project file");
    assert.equal(prepared.nativeProcess.requestedExitMode, scenario === "recovery" ? "crash" : "graceful");
    if (scenario === "recovery") assert.ok(prepared.nativeProcess.terminationRequestedAt, "Recovery must follow a real forced native process termination");
    else assert.equal(prepared.discardDialogObserved, true);
    report.checks.push(scenario === "discard" ? "Explicit GUI discard followed by completely independent native/browser processes restores the exact saved snapshot with all native resources and rendered pixels" : "A real native process crash followed by completely independent native/browser processes recovers unsaved ECS and every generated resource, source curve and rendered texture");
  } catch (error) { scenarioErrors.push({ scenario, error: error.stack || String(error) }); } }
  report.scenarioErrors = scenarioErrors;assert.equal(scenarioErrors.length, 0, "Independent-process persistence scenarios failed; inspect scenarioErrors and phase reports");
  assert.ok(Object.keys(report.assets).length > 0);assert.deepEqual(report.pageErrors, []);assert.deepEqual(report.failures, []);report.pass = true;
} catch (error) { report.error = error.stack || String(error);process.exitCode = 1; }
finally { writeFileSync(resolve(output, "fresh-process-native-browser-report.json"), JSON.stringify(report, null, 2)); }
console.log(JSON.stringify({ pass: report.pass, checks: report.checks.length, phases: report.phases.length, output }));
