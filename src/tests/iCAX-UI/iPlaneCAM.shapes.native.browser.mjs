import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const source = resolve(fileURLToPath(new URL("../../", import.meta.url))), root = resolve(source, ".."), output = resolve(process.env.ICAX_PLANE_TEST_OUTPUT || resolve(root, "output/tests/iplane-cam-worktable/source-shapes"));mkdirSync(output, { recursive: true });
assert.ok(process.env.ICAX_PLANE_NATIVE_BRIDGE, "Select the isolated native acceptance bridge");
const report = { pass: false, scope: "Two independent Node/native bridge/Chromium processes verify actual left drawing-group menus, seven standalone exact shapes plus the existing arc, numeric/pointer cooperation, invalid-input correction, owned CAD sketches, framework history and source-free save/reopen. All business writes use real UI controls; native calls only read exact geometry/resources. Existing user desktop projects and processes are untouched.", assetRoot: resolve(process.env.ICAX_PLANE_ASSET_ROOT || source), executable: resolve(process.env.ICAX_PLANE_NATIVE_BRIDGE), phases: [], checks: [], assets: {}, binaries: {}, pageErrors: [], failures: [] };
const script = fileURLToPath(new URL("./iPlaneCAM.shapes.native.browserPhase.mjs", import.meta.url)), manifest = resolve(output, "manifest.json");
report.testSources = Object.fromEntries(["iPlaneCAM.shapes.native.browser.mjs", "iPlaneCAM.shapes.native.browserPhase.mjs", "iPlaneCAM.shapes.native.browserCase.mjs", "iPlaneCAM.native.browserHarness.mjs", "iPlaneCAM.browserCommands.mjs"].map(name => { const path = "tests/iCAX-UI/" + name;return [path, createHash("sha256").update(readFileSync(resolve(source, path))).digest("hex")]; }));
const merge = (target, values, kind) => { for (const [name, hash] of Object.entries(values || {})) { assert.match(hash, /^[a-f0-9]{64}$/, kind + " missing: " + name);if (name in target) assert.equal(target[name], hash, kind + " changed across independent processes: " + name);target[name] = hash; } };
const alive = pid => { try { process.kill(pid, 0);return true; } catch (error) { if (error.code === "ESRCH") return false;throw error; } };
async function runPhase(name) {
  const phaseOutput = resolve(output, name);mkdirSync(phaseOutput, { recursive: true });const child = spawn(process.execPath, [script], { cwd: root, windowsHide: true, stdio: ["ignore", "pipe", "pipe"], env: { ...process.env, ICAX_PLANE_TEST_OUTPUT: phaseOutput, ICAX_PLANE_SHAPES_PHASE: name, ICAX_PLANE_SHAPES_MANIFEST: manifest } }), stdout = [], stderr = [];
  child.stdout.on("data", data => { stdout.push(data);process.stdout.write(data); });child.stderr.on("data", data => { stderr.push(data);process.stderr.write(data); });const exit = await new Promise((done, reject) => { child.once("error", reject);child.once("close", (code, signal) => done({ code, signal })); });
  const path = resolve(phaseOutput, "shapes-phase-native-browser-report.json"), result = existsSync(path) ? JSON.parse(readFileSync(path, "utf8")) : null, entry = { name, pid: child.pid, exit, report: path, nativeProcess: result?.nativeProcess, checks: result?.checks?.length || 0, pass: Boolean(exit.code === 0 && result?.pass), stdout: Buffer.concat(stdout).toString(), stderr: Buffer.concat(stderr).toString() };report.phases.push(entry);
  if (result) { merge(report.assets, result.assets, "Asset");merge(report.binaries, result.binaries, "Native binary");report.pageErrors.push(...result.pageErrors || []);report.failures.push(...result.failures || []); }
  assert.equal(entry.nativeProcess?.closed, true);assert.equal(entry.nativeProcess.dataDirectoryExistedBeforeStart, false);assert.equal(alive(entry.nativeProcess.pid), false, "Old native bridge is still alive");for (const previous of report.phases.slice(0, -1)) { assert.notEqual(previous.pid, entry.pid);assert.notEqual(previous.nativeProcess.pid, entry.nativeProcess.pid);assert.notEqual(previous.nativeProcess.dataDirectory, entry.nativeProcess.dataDirectory); }
  assert.equal(entry.pass, true, result?.error || entry.stderr || name + " failed");return result;
}
try {
  const prepared = await runPhase("prepare"), reopened = await runPhase("reopen");assert.equal(prepared.savedShapesProject.sha256, reopened.fileBeforeOpen.sha256);assert.equal(reopened.fileAfterOpen.sha256, prepared.savedShapesProject.sha256);assert.deepEqual(report.pageErrors, []);assert.deepEqual(report.failures, []);assert.ok(Object.keys(report.assets).length > 0);
  report.checks.push("Real grouped palette and numeric/pointer UI produce exact standalone shapes, validate correction/cancellation, edit owned CAD sketches and retain framework history", "A completely fresh native/browser process renders all saved standalone shapes and retrieves identical exact geometry/resources after original DXF deletion");report.pass = true;
} catch (error) { report.error = error.stack || String(error);process.exitCode = 1; }
finally { writeFileSync(resolve(output, "shapes-native-browser-report.json"), JSON.stringify(report, null, 2)); }
console.log(JSON.stringify({ pass: report.pass, checks: report.checks.length, phases: report.phases.length, output }));
