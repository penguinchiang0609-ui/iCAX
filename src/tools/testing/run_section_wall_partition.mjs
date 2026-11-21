import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const work = path.join(root, 'TestData/Tube/SectionRegionWork-20261118');
const contours = path.join(work, 'contours');
const bin = path.join(root, 'output/tests/section-regions-native-20261113/bin');
const native = path.join(bin, 'SectionWallPartition.exe');
const verifier = path.join(bin, 'RegionVerification.exe');
const env = { ...process.env, PATH: [
  bin, path.join(root, 'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bind'), process.env.PATH,
].join(';') };

const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const manifest = read(path.join(contours, 'manifest.json'));

function run(exe, args) {
  return new Promise((resolve, reject) => {
    const child = spawn(exe, args, { cwd: root, env, windowsHide: true });
    let stdout = '', stderr = '', timedOut = false;
    child.stdout.on('data', chunk => { stdout += chunk; });
    child.stderr.on('data', chunk => { stderr += chunk; });
    const timer = setTimeout(() => { timedOut = true; child.kill(); }, 120000);
    child.once('error', error => { clearTimeout(timer); reject(error); });
    child.once('close', code => {
      clearTimeout(timer);
      if (timedOut) reject(new Error('Native section wall work timed out'));
      else resolve({ code, stdout, stderr: stderr.trim() });
    });
  });
}

function candidateRingSizes(fitting) {
  const rings = Map.groupBy(fitting.curves, curve => curve.wire);
  if (rings.size !== 2) return false;
  const [first, second] = [...rings.values()];
  return first.length === second.length && first.length >= 4 &&
    [first, second].every(ring => ring.every((curve, i) =>
      (curve.type === 'line') !== (ring[(i + 1) % ring.length].type === 'line')));
}

async function partition(source) {
  const record = { id: source.id, name: source.name, status: 'not-applicable' };
  const dir = path.join(contours, source.id);
  let staging;
  try {
    if (source.status !== 'support-regions') throw new Error('Section region source did not pass');
    const fitting = read(path.join(dir, 'contour-fitting.json'));
    if (!candidateRingSizes(fitting)) {
      record.reason = '内外轮廓没有可一一对应的直段和弯段';
      return record;
    }

    const input = path.join(root, source.input);
    const meta = read(input.replace(/\.step$/i, '.json'));
    const gap = Math.max(...meta.wires.map(wire => wire.maxGapMm || 0));
    staging = fs.mkdtempSync(path.join(dir, '.wall-'));
    const copy = path.join(staging, 'input.step');
    const output = path.join(staging, 'regions-wall');
    fs.copyFileSync(input, copy);
    if (sha(copy) !== source.inputSha256) throw new Error('Source STEP differs from contour Work');
    const split = await run(native, [copy, output, String(meta.endX), String(gap),
      path.join(dir, 'contour-source-edges.txt')]);
    if (split.code === 3) {
      record.reason = '未找到唯一的内外过渡点配对';
      return record;
    }
    if (split.code !== 0) throw new Error(split.stderr || `Native exit ${split.code}`);

    const data = read(output + '.json');
    const checked = await run(verifier, ['check', output + '.brep']);
    if (checked.code !== 0) throw new Error(checked.stderr || 'Saved BREP reread failed');
    const reread = JSON.parse(checked.stdout);
    if (!reread.valid || reread.faces !== data.cellCount ||
        reread.regionComponents.length !== data.cellCount ||
        reread.regionComponents.some(count => count !== 1) ||
        data.cells.length !== data.cellCount || data.cells.some(cell => cell.area <= 0) ||
        Math.abs(reread.area - data.partitionArea) > data.areaTolerance)
      throw new Error('Saved BREP faces, connectivity, or area differ from partition result');
    if (sha(input) !== source.inputSha256) throw new Error('Source STEP changed during wall Work');
    for (const suffix of ['.brep', '.json'])
      fs.renameSync(output + suffix, path.join(dir, 'regions-wall' + suffix));
    Object.assign(record, {
      status: 'wall-regions',
      result: `${source.id}/regions-wall.json`,
      brep: `${source.id}/regions-wall.brep`,
      cellCount: data.cellCount,
      curvedCellCount: data.cells.filter(cell => cell.kind === 'curved-wall').length,
      missingArea: data.missingArea,
      excessArea: data.excessArea,
      remainingOverlapArea: data.remainingOverlapArea,
      sourceBrepReread: reread.valid,
    });
  } catch (error) {
    record.status = 'failed';
    record.reason = error.message;
  } finally {
    if (staging) {
      for (const file of ['input.step', 'regions-wall.brep', 'regions-wall.json'])
        fs.rmSync(path.join(staging, file), { force: true });
      fs.rmdirSync(staging);
    }
  }
  return record;
}

let next = 0;
const results = new Array(manifest.records.length);
await Promise.all(Array.from({ length: 4 }, async () => {
  while (next < results.length) {
    const index = next++;
    results[index] = await partition(manifest.records[index]);
    if (results[index].status !== 'not-applicable')
      console.log(JSON.stringify(results[index]));
  }
}));
const counts = Object.fromEntries(['wall-regions', 'not-applicable', 'failed'].map(status =>
  [status, results.filter(record => record.status === status).length]));
const output = path.join(contours, 'wall-manifest.json');
const stage = output + '.' + process.pid;
fs.writeFileSync(stage, JSON.stringify({ schema: 1, stage: 'SectionWallPartition',
  inputCount: results.length, executableSha256: sha(native), counts, records: results }, null, 2));
fs.renameSync(stage, output);
fs.copyFileSync(path.join(root, 'src/tools/testing/region_overlap_viewer.html'), path.join(work, 'overlap.html'));
console.log(counts);
if (counts.failed) process.exitCode = 1;
