import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import readline from 'node:readline';
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';

const runtime = path.resolve(process.env.ICAX_NATIVE_RUNTIME_ROOT ?? '');
const bridge = path.resolve(process.env.ICAX_NATIVE_SCOPE_BRIDGE ?? '');
assert(process.env.ICAX_NATIVE_RUNTIME_ROOT && process.env.ICAX_NATIVE_SCOPE_BRIDGE,
  'Set runtime and BatchExcelAutomationScopeBridge paths');
const repo = path.resolve(import.meta.dirname, '../../..');
const fixture = process.env.ICAX_AUTOMATION_WORKBOOK ?? path.join(repo, 'output/防盗窗款式批量导入测试.xlsx');
const reportRoot = path.resolve(process.env.ICAX_AUTOMATION_OUTPUT_ROOT ??
  path.join(repo, 'output/tests/excel-temp-directory-20261006'));
await fs.mkdir(reportRoot, { recursive: true });
const root = await fs.mkdtemp(path.join(reportRoot, 'native-regression-'));
const input = path.join(root, 'in');
const temporary = path.join(root, 'temp');
const output = path.join(root, 'out');
const userData = path.join(root, 'user-data');
await Promise.all([input, temporary, output, userData].map(value => fs.mkdir(value, { recursive: true })));
const calls = [];
const processes = new Set();
const pause = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
const exists = value => fs.access(value).then(() => true, () => false);
async function tempFiles(directory = temporary) {
  const entries = await fs.readdir(directory, { withFileTypes: true });
  const children = await Promise.all(entries.map(entry => entry.isDirectory()
    ? tempFiles(path.join(directory, entry.name)) : [path.join(directory, entry.name)]));
  return children.flat().filter(value => path.extname(value).toLowerCase() === '.xlsx').sort();
}
async function assertMoved(claim, originalSource, bytes, tempRoot = temporary) {
  assert.equal(await exists(originalSource), false, `Processed input must leave in: ${originalSource}`);
  assert.equal(path.dirname(path.dirname(claim.sourcePath)), tempRoot,
    'The working file must be inside a job under the configured temp directory');
  assert.equal(path.basename(claim.sourcePath), path.basename(originalSource), 'Preserve the input filename');
  assert.equal(digest(await fs.readFile(claim.sourcePath)), digest(bytes), 'MOVE must preserve exact original bytes');
}
async function assertRetained(claim, bytes) {
  assert.equal(digest(await fs.readFile(claim.sourcePath)), digest(bytes), 'Retain the original in temp after completion');
}
async function observeStable(instance, source) {
  await instance.invoke('ScanBatchExcelAutomation');
  await pause(2200);
  return (await instance.invoke('ScanBatchExcelAutomation')).files.find(value => value.sourcePath === source);
}
async function lockFile(source) {
  // A writer that has not shared its file must prevent the native claim from moving it.
  const quotedSource = source.replaceAll("'", "''");
  const command = `$fileLock = [System.IO.File]::Open('${quotedSource}', [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None); try { [Console]::WriteLine('LOCKED'); [Console]::Out.Flush(); [Console]::In.ReadLine() | Out-Null } finally { $fileLock.Dispose() }`;
  const child = spawn('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', command], { windowsHide: true });
  processes.add(child);
  child.on('exit', () => processes.delete(child));
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => { child.kill(); reject(new Error('Timed out acquiring test file lock')); }, 10000);
    readline.createInterface({ input: child.stdout }).on('line', line => {
      if (line === 'LOCKED') { clearTimeout(timer); resolve(); }
    });
    child.once('error', error => { clearTimeout(timer); reject(error); });
    child.once('exit', code => { clearTimeout(timer); reject(new Error(`Test file lock exited early: ${code}`)); });
  });
  return async () => {
    const ended = new Promise(resolve => child.once('exit', resolve));
    child.stdin.end('\n'); await ended;
  };
}
function host() {
  const child = spawn(bridge, [], { cwd: runtime, windowsHide: true,
    env: { ...process.env, PATH: `${runtime};${process.env.PATH}`, ICAX_AUTOMATION_USER_DATA: userData } });
  processes.add(child);
  const pending = new Map();
  let counter = 0;
  let stderr = '';
  child.stderr.on('data', bytes => { stderr += bytes.toString(); });
  readline.createInterface({ input: child.stdout }).on('line', line => {
    let message;
    try { message = JSON.parse(line); } catch { return; }
    const item = pending.get(message.id);
    if (!item) return;
    pending.delete(message.id); clearTimeout(item.timer);
    calls.push({ process: child.pid, ...item.request, response: message });
    if (message.ok) item.resolve(message.result);
    else item.reject(new Error(message.error));
  });
  child.on('exit', (code, signal) => {
    processes.delete(child);
    for (const item of pending.values()) {
      clearTimeout(item.timer); item.reject(new Error(`Native bridge exit ${code}/${signal}: ${stderr}`));
    }
    pending.clear();
  });
  return {
    child,
    invoke(method, payload = {}, scope = 'product') {
      const request = { id: ++counter, scope, method, payload };
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => { pending.delete(request.id); reject(new Error(`${method} timed out`)); }, 180000);
        pending.set(request.id, { request, resolve, reject, timer });
        child.stdin.write(`${JSON.stringify(request)}\n`);
      });
    },
    async close() {
      const ended = new Promise(resolve => child.once('exit', resolve));
      child.stdin.end(); await ended;
    },
  };
}
const checks = [];
let crossDriveJob = null;
try {
  let first = host();
  const modules = (await first.invoke('GetRuntimeModules', {}, 'inspection')).modules;
  assert(Object.values(modules).every(value => path.dirname(path.resolve(value)) === runtime));
  assert.equal((await first.invoke('GetBatchExcelAutomationSettings')).settings.enabled, false);
  await assert.rejects(first.invoke('SaveBatchExcelAutomationSettings', {
    settings: { enabled: true, inputDirectory: input, outputDirectory: output },
  }));
  const settings = { enabled: true, inputDirectory: input, tempDirectory: temporary, outputDirectory: output };
  await Promise.all([
    path.join(input, 'temp'), path.join(temporary, 'in'), path.join(input, 'out'),
    path.join(output, 'in'), path.join(temporary, 'out'), path.join(output, 'temp'),
  ].map(value => fs.mkdir(value, { recursive: true })));
  for (const invalidSettings of [
    { ...settings, inputDirectory: 'relative-input' },
    { ...settings, tempDirectory: 'relative-temp' },
    { ...settings, outputDirectory: 'relative-output' },
    { ...settings, tempDirectory: input },
    { ...settings, outputDirectory: input },
    { ...settings, outputDirectory: temporary },
    { ...settings, tempDirectory: path.join(input, 'temp') },
    { ...settings, inputDirectory: path.join(temporary, 'in') },
    { ...settings, outputDirectory: path.join(input, 'out') },
    { ...settings, inputDirectory: path.join(output, 'in') },
    { ...settings, outputDirectory: path.join(temporary, 'out') },
    { ...settings, tempDirectory: path.join(output, 'temp') },
  ]) await assert.rejects(first.invoke('SaveBatchExcelAutomationSettings', { settings: invalidSettings }));
  assert.deepEqual((await first.invoke('SaveBatchExcelAutomationSettings', { settings })).settings, settings);
  checks.push('Enabled settings require configurable temp; all directories must be absolute, distinct and nonoverlapping');

  const workbookBytes = await fs.readFile(fixture);
  const source = path.join(input, '批量防盗窗.xlsx');
  await fs.writeFile(source, workbookBytes);
  await fs.writeFile(path.join(input, '~$批量防盗窗.xlsx'), workbookBytes);
  await fs.mkdir(path.join(input, 'nested'));
  await fs.writeFile(path.join(input, 'nested/ignored.xlsx'), workbookBytes);
  assert.equal((await first.invoke('ScanBatchExcelAutomation')).files.length, 0);
  await pause(2200);
  const scan = await first.invoke('ScanBatchExcelAutomation');
  assert.equal(scan.files.length, 1);
  const file = scan.files[0];
  assert.equal(file.fileToken, digest(workbookBytes));
  const second = host();
  const claims = await Promise.all([
    first.invoke('ClaimBatchExcelAutomation', file),
    second.invoke('ClaimBatchExcelAutomation', file),
  ]);
  assert.equal(claims.filter(value => value.claimed).length, 1);
  const owner = claims[0].claimed ? first : second;
  const other = claims[0].claimed ? second : first;
  const claim = claims.find(value => value.claimed);
  await assertMoved(claim, source, workbookBytes);
  assert.equal(path.dirname(claim.targetDirectory), output);
  assert((await owner.invoke('ReadBatchExcelImport', { sourcePath: claim.sourcePath })).rows.length > 0);
  await assert.rejects(other.invoke('CompleteBatchExcelAutomation', {
    sourcePath: claim.originalSourcePath, fileToken: file.fileToken, status: 'succeeded', outputs: [],
  }), /当前进程/);
  await owner.invoke('CompleteBatchExcelAutomation', {
    sourcePath: claim.originalSourcePath, fileToken: file.fileToken, status: 'succeeded', outputs: [],
  });
  await assertRetained(claim, workbookBytes);
  assert.equal((await first.invoke('ScanBatchExcelAutomation')).files.length, 0);
  checks.push('Nonrecursive XLSX filtering and stable observations; real workbook validation happens after MOVE; concurrent claims have one owner');
  checks.push('Successful completion leaves in clean and preserves the original bytes in the configured temp job');
  await second.close();
  await first.close();
  first = host();
  assert.deepEqual((await first.invoke('GetBatchExcelAutomationSettings')).settings, settings);
  assert.equal((await first.invoke('ScanBatchExcelAutomation')).files.length, 0);
  await assertRetained(claim, workbookBytes);
  checks.push('Restart preserves all three configured directories, successful-file fingerprint and original temp file');

  const beforeDuplicate = await tempFiles();
  await fs.writeFile(source, workbookBytes);
  assert.equal(await observeStable(first, source), undefined);
  assert.equal(await exists(source), false, 'A repeated completed input must also leave in');
  const duplicateFiles = (await tempFiles()).filter(value => !beforeDuplicate.includes(value));
  assert.equal(duplicateFiles.length, 1);
  assert.equal(digest(await fs.readFile(duplicateFiles[0])), digest(workbookBytes));
  checks.push('Repeated completed fingerprint is archived by MOVE without another candidate or product creation');

  const disappeared = path.join(input, '已撤回.xlsx');
  await fs.writeFile(disappeared, workbookBytes);
  const disappearedFile = await observeStable(first, disappeared);
  assert(disappearedFile);
  await fs.unlink(disappeared);
  assert.equal((await first.invoke('ClaimBatchExcelAutomation', disappearedFile)).claimed, false);
  checks.push('A candidate withdrawn before Claim safely returns claimed:false');

  const lockedSource = path.join(input, '写入锁定.xlsx');
  await fs.writeFile(lockedSource, workbookBytes);
  const lockedFile = await observeStable(first, lockedSource);
  assert(lockedFile);
  const beforeLocked = await tempFiles();
  const unlock = await lockFile(lockedSource);
  try {
    const lockedClaim = await first.invoke('ClaimBatchExcelAutomation', lockedFile);
    assert.equal(lockedClaim.claimed, false);
    assert(lockedClaim.error);
    assert.equal(await exists(lockedSource), true, 'A file still being written must remain in in');
    assert.deepEqual(await tempFiles(), beforeLocked, 'A failed move must not create a working original');
  } finally { await unlock(); }
  assert.equal(digest(await fs.readFile(lockedSource)), digest(workbookBytes));
  const unlockedClaim = await first.invoke('ClaimBatchExcelAutomation', lockedFile);
  assert(unlockedClaim.claimed, unlockedClaim.error);
  await assertMoved(unlockedClaim, lockedSource, workbookBytes);
  await first.invoke('CompleteBatchExcelAutomation', {
    sourcePath: unlockedClaim.originalSourcePath, fileToken: lockedFile.fileToken,
    status: 'failed', error: 'test completion failure after unlocking', outputs: [],
  });
  await assertRetained(unlockedClaim, workbookBytes);
  checks.push('An exclusively open input is preserved without a temp original; releasing the lock permits the same candidate to be moved and completed');

  const invalid = path.join(input, '错误.xlsx');
  await fs.writeFile(invalid, 'not an import workbook');
  await first.invoke('ScanBatchExcelAutomation');
  await pause(2200);
  const invalidFile = (await first.invoke('ScanBatchExcelAutomation')).files.find(value => value.sourcePath === invalid);
  assert(invalidFile);
  const rejected = await first.invoke('ClaimBatchExcelAutomation', invalidFile);
  assert.equal(rejected.claimed, false); assert(rejected.error);
  await assertMoved(rejected, invalid, Buffer.from('not an import workbook'));
  assert.equal((await first.invoke('ScanBatchExcelAutomation')).files.length, 0);
  await assertRetained(rejected, Buffer.from('not an import workbook'));
  checks.push('Invalid workbook moves out of in before validation, stays intact in temp and is not repeatedly retried');

  const interruptedSource = path.join(input, '中断.xlsx');
  await fs.writeFile(interruptedSource, workbookBytes);
  await first.invoke('ScanBatchExcelAutomation');
  await pause(2200);
  const interruptedFile = (await first.invoke('ScanBatchExcelAutomation')).files.find(value => value.sourcePath === interruptedSource);
  const interruptedClaim = await first.invoke('ClaimBatchExcelAutomation', interruptedFile);
  assert(interruptedClaim.claimed);
  await assertMoved(interruptedClaim, interruptedSource, workbookBytes);
  const killed = new Promise(resolve => first.child.once('exit', resolve));
  first.child.kill(); await killed;
  first = host();
  await first.invoke('GetBatchExcelAutomationSettings');
  const recoveredScan = await first.invoke('ScanBatchExcelAutomation');
  assert.equal(recoveredScan.files.length, 0);
  assert.equal(recoveredScan.errors.length, 1);
  assert.match(recoveredScan.errors[0].error, /被中断/);
  assert.equal((await first.invoke('ScanBatchExcelAutomation')).errors.length, 0);
  const statePath = path.join(userData, 'icax.tube-designer/batch-excel-automation/state.json');
  const state = JSON.parse(await fs.readFile(statePath, 'utf8'));
  const interruptedRecord = Object.values(state.records).find(value => value.originalSourcePath === interruptedSource);
  assert(interruptedRecord);
  assert.equal(interruptedRecord.sourcePath, interruptedClaim.sourcePath);
  assert.equal(interruptedRecord.status, 'failed'); assert.match(interruptedRecord.error, /被中断/);
  await assertRetained(interruptedClaim, workbookBytes);
  assert.equal(await exists(interruptedSource), false);
  checks.push('Interrupted claim becomes failed after restart; original remains in temp and in stays clean');

  // A copy still changing between scans must never enter the ready queue.
  const changing = path.join(input, '复制中.xlsx');
  await fs.writeFile(changing, workbookBytes.subarray(0, 100));
  await first.invoke('ScanBatchExcelAutomation');
  await pause(2200);
  await fs.writeFile(changing, workbookBytes);
  assert.equal((await first.invoke('ScanBatchExcelAutomation')).files.length, 0);
  await pause(2200);
  const stableChanging = (await first.invoke('ScanBatchExcelAutomation')).files.find(value => value.sourcePath === changing);
  assert(stableChanging);
  const changedClaim = await first.invoke('ClaimBatchExcelAutomation', stableChanging);
  assert(changedClaim.claimed);
  await assertMoved(changedClaim, changing, workbookBytes);
  assert.notEqual(changedClaim.targetDirectory, claim.targetDirectory);
  await first.invoke('CompleteBatchExcelAutomation', {
    sourcePath: changedClaim.originalSourcePath, fileToken: stableChanging.fileToken,
    status: 'failed', error: 'test scene failure', outputs: [],
  });
  await assertRetained(changedClaim, workbookBytes);
  assert.equal((await first.invoke('ScanBatchExcelAutomation')).files.length, 0);
  checks.push('Unstable copy remains in in until stable; failed completion preserves temp original and unique output job');

  // A ZIP comment changes the file bytes while preserving workbook contents.
  const endOfZip = workbookBytes.lastIndexOf(Buffer.from([0x50, 0x4b, 0x05, 0x06]));
  assert(endOfZip >= 0);
  const zipPrefix = Buffer.from(workbookBytes.subarray(0, endOfZip + 22));
  const zipComment = Buffer.from('automation-content-change-test');
  zipPrefix.writeUInt16LE(zipComment.length, endOfZip + 20);
  const revisedWorkbookBytes = Buffer.concat([zipPrefix, zipComment]);
  await fs.writeFile(changing, revisedWorkbookBytes);
  await first.invoke('ScanBatchExcelAutomation');
  await pause(2200);
  const revision = (await first.invoke('ScanBatchExcelAutomation')).files.find(value => value.sourcePath === changing);
  assert(revision); assert.notEqual(revision.fileToken, stableChanging.fileToken);
  const revisionClaim = await first.invoke('ClaimBatchExcelAutomation', revision);
  assert(revisionClaim.claimed, revisionClaim.error);
  await assertMoved(revisionClaim, changing, revisedWorkbookBytes);
  assert.notEqual(revisionClaim.sourcePath, changedClaim.sourcePath, 'Repeated filename must receive a fresh temp job');
  await assertRetained(changedClaim, workbookBytes);
  await first.invoke('CompleteBatchExcelAutomation', {
    sourcePath: revisionClaim.originalSourcePath, fileToken: revision.fileToken,
    status: 'succeeded', outputs: [],
  });
  await assertRetained(revisionClaim, revisedWorkbookBytes);
  await fs.writeFile(changing, workbookBytes);
  await first.invoke('ScanBatchExcelAutomation');
  await pause(2200);
  assert.equal((await first.invoke('ScanBatchExcelAutomation')).files.length, 0);
  assert.equal(await exists(changing), false, 'Repeated failed fingerprint must also leave in');
  await assertRetained(changedClaim, workbookBytes);
  await assertRetained(revisionClaim, revisedWorkbookBytes);
  checks.push('Changed content creates a new job without overwriting previous temp originals; repeated failed fingerprint is archived without retry');

  if (process.env.TEMP && path.parse(path.resolve(process.env.TEMP)).root.toLowerCase() !== path.parse(input).root.toLowerCase()) {
    const crossTemp = await fs.mkdtemp(path.join(process.env.TEMP, 'icax-excel-temp-native-'));
    await first.invoke('SaveBatchExcelAutomationSettings', { settings: { ...settings, tempDirectory: crossTemp } });
    const crossSource = path.join(input, '跨盘临时目录.xlsx');
    await fs.writeFile(crossSource, workbookBytes);
    const crossFile = await observeStable(first, crossSource);
    assert(crossFile);
    const crossClaim = await first.invoke('ClaimBatchExcelAutomation', crossFile);
    assert(crossClaim.claimed, crossClaim.error);
    await assertMoved(crossClaim, crossSource, workbookBytes, crossTemp);
    await first.invoke('CompleteBatchExcelAutomation', {
      sourcePath: crossClaim.originalSourcePath, fileToken: crossFile.fileToken, status: 'succeeded', outputs: [],
    });
    await assertRetained(crossClaim, workbookBytes);
    crossDriveJob = { tempDirectory: crossTemp, sourcePath: crossClaim.sourcePath, fileToken: digest(workbookBytes) };
    await first.invoke('SaveBatchExcelAutomationSettings', { settings });
    checks.push('Configurable temp on another drive moves the input successfully and retains its exact original bytes');
  }

  const remainingInputs = (await fs.readdir(input, { withFileTypes: true }))
    .filter(entry => entry.isFile() && entry.name.toLowerCase().endsWith('.xlsx') && !entry.name.startsWith('~$'));
  assert.deepEqual(remainingInputs, [], 'No attempted, completed or repeated workbook may remain in in');
  checks.push('After all success, failure, interruption and duplicate cases, in contains no handled workbooks');

  await first.invoke('SaveBatchExcelAutomationSettings', { settings: { ...settings, enabled: false } });
  const disabledSource = path.join(input, '停用时保留.xlsx');
  const beforeDisabled = await tempFiles();
  await fs.writeFile(disabledSource, workbookBytes);
  assert.equal((await first.invoke('ScanBatchExcelAutomation')).files.length, 0);
  await pause(2200);
  assert.equal((await first.invoke('ScanBatchExcelAutomation')).files.length, 0);
  assert.equal(digest(await fs.readFile(disabledSource)), digest(workbookBytes));
  assert.deepEqual(await tempFiles(), beforeDisabled);
  checks.push('Disabled automation leaves incoming originals in in and creates no temp job');
  await first.close();
  await fs.writeFile(path.join(root, 'report.json'), JSON.stringify({ passed: true, checks, modules, fixture,
    directories: { input, temp: temporary, output }, tempOriginals: await tempFiles(),
    inputFiles: await fs.readdir(input), crossDriveJob,
    coverage: 'Real registered native SDO settings, scanning, claims, workbook validation and completion; scene creation/export and file picker are outside this suite', calls }, null, 2));
  console.log(`PASS ${checks.length} native automation checks; report ${path.join(root, 'report.json')}`);
} catch (error) {
  await fs.writeFile(path.join(root, 'report.json'), JSON.stringify({ passed: false, checks,
    directories: { input, temp: temporary, output }, fixture, error: error.stack, calls }, null, 2));
  throw error;
} finally {
  for (const child of processes) child.kill();
  await fs.writeFile(path.join(root, 'native-calls.json'), JSON.stringify(calls, null, 2));
}
