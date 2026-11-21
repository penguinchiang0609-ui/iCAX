import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const work = path.join(root, 'TestData/Tube/SectionRegionWork-20261118/contours');
const toolDir = path.join(root, 'output/tests/section-regions-native-20261113/bin');
const partitionTool = path.join(toolDir, 'RegionOverlapPartition.exe');
const checkTool = path.join(toolDir, 'RegionVerification.exe');
const arg = name => process.argv.find(value => value.startsWith(`--${name}=`))?.slice(name.length + 3);
const chosen = arg('names')?.split('|').filter(Boolean);
const concurrency = Number(arg('workers') || 2);
const timeoutMs = Number(arg('timeout-ms') || 120000);
if (!Number.isInteger(concurrency) || concurrency < 1 || !Number.isFinite(timeoutMs) || timeoutMs <= 0)
  throw new Error('Invalid workers or timeout');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const write = (file, value) => fs.writeFileSync(file, JSON.stringify(value, null, 2));
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const env = {...process.env,
  PATH: [toolDir, path.join(root, 'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bind'), process.env.PATH].join(';')};
for (const file of [partitionTool, checkTool]) if (!fs.existsSync(file)) throw new Error(`Missing native tool: ${file}`);
const parent = read(path.join(work, 'manifest.json'));
const records = parent.records.filter(record => !chosen || chosen.includes(record.name));
if (!records.length) throw new Error('No matching region results');
const outputFile = path.join(work, chosen ? 'overlap-manifest-selected.json' : 'overlap-manifest.json');
fs.copyFileSync(path.join(root, 'src/tools/testing/region_overlap_viewer.html'),
  path.join(path.dirname(work), 'overlap.html'));
const results = [];
function manifest(allFinished) {
  write(outputFile, {schema: 1, stage: 'RegionOverlapPartition', source: 'SectionRegions',
    algorithm: 'native-overlap-membership-partition', sourceSolverSha256: parent.solverSha256,
    executableSha256: sha(partitionTool), inputCount: records.length, allFinished,
    records: [...results].sort((a, b) => a.name.localeCompare(b.name))});
}
manifest(false);

function run(exe, args, log) {
  return new Promise(resolve => {
    const child = spawn(exe, args, {cwd: root, env, windowsHide: true});
    let stdout = '', stderr = '', finished = false, timedOut = false;
    child.stdout.on('data', chunk => stdout += chunk);
    child.stderr.on('data', chunk => stderr += chunk);
    const timer = setTimeout(() => {timedOut = true; child.kill();}, timeoutMs);
    const finish = (code, error) => {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      if (error) stderr += error.message;
      fs.writeFileSync(log, stdout + (stderr ? '\n' + stderr : ''));
      resolve({code, timedOut, message: stderr.trim() || stdout.trim()});
    };
    child.once('error', error => finish(-1, error));
    child.once('close', code => finish(code));
  });
}

async function one(source) {
  const dir = path.join(work, source.id);
  const oldBrep = path.join(dir, 'regions.brep');
  const oldJson = path.join(dir, 'regions.json');
  const output = path.join(dir, 'regions-disjoint');
  const record = {id: source.id, name: source.name, sourceRegionCount: source.regionCount,
    sourceBrepSha256: fs.existsSync(oldBrep) ? sha(oldBrep) : null,
    status: 'running', brep: null, result: null};
  const started = Date.now();
  for (const suffix of ['.brep', '.json']) fs.rmSync(output + suffix, {force: true});
  fs.rmSync(path.join(dir, 'overlap-work.json'), {force: true});
  try {
    if (source.status !== 'support-regions') throw new Error('Source region Work did not pass');
    const original = read(oldJson);
    const native = await run(partitionTool, [oldBrep, output, String(source.regionCount),
      String(original.nativeTolerance)], path.join(dir, 'overlap-native.log'));
    if (native.code !== 0) throw new Error(native.timedOut ? 'Native overlap partition timed out' : native.message);
    const data = read(output + '.json');
    const reread = await run(checkTool, ['check', output + '.brep'], path.join(dir, 'overlap-check.log'));
    if (reread.code !== 0) throw new Error('Partition BREP failed native reread: ' + reread.message);
    const checked = read(path.join(dir, 'overlap-check.log'));
    if (!checked.valid || checked.regionComponents.length !== data.cellCount
      || checked.regionComponents.some(count => count !== 1))
      throw new Error('Partition BREP cells are invalid or disconnected');
    if (!data.coverageValid || data.status !== 'disjoint-regions'
      || data.remainingOverlapArea > data.areaTolerance)
      throw new Error('Native partition coverage or disjointness check failed');
    data.originalDisplayWires = original.originalDisplayWires;
    data.sourceRegionAreas = original.regions.map(region => ({id: region.id, area: region.area}));
    write(output + '.json', data);
    record.status = 'disjoint-regions';
    record.brep = `${source.id}/regions-disjoint.brep`;
    record.result = `${source.id}/regions-disjoint.json`;
    record.cellCount = data.cellCount;
    record.sharedCellCount = data.cells.filter(cell => cell.kind === 'shared').length;
    record.sharedArea = data.sharedArea;
    record.remainingOverlapArea = data.remainingOverlapArea;
    record.missingArea = data.missingArea;
    record.excessArea = data.excessArea;
  } catch (error) {
    record.status = 'failed';
    record.reason = error.message;
  }
  record.sourceBrepUnchanged = record.sourceBrepSha256 === (fs.existsSync(oldBrep) ? sha(oldBrep) : null);
  record.elapsedMs = Date.now() - started;
  write(path.join(dir, 'overlap-work.json'), record);
  results.push(record);
  manifest(results.length === records.length);
  console.log(JSON.stringify({name: record.name, status: record.status, cells: record.cellCount,
    shared: record.sharedCellCount, reason: record.reason}));
}

let cursor = 0;
await Promise.all(Array.from({length: Math.min(concurrency, records.length)}, async () => {
  while (cursor < records.length) await one(records[cursor++]);
}));
manifest(true);
const failed = results.filter(record => record.status !== 'disjoint-regions');
console.log(JSON.stringify({total: results.length, passed: results.length - failed.length, failed: failed.length}, null, 2));
if (failed.length) process.exitCode = 1;
