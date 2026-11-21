import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const tube = path.join(repo, 'TestData/Tube');
const args = new Map();
for (let i = 2; i < process.argv.length; i += 2) {
  const key = process.argv[i], value = process.argv[i + 1];
  if (!['--native', '--models', '--workers', '--timeout', '--out'].includes(key) || !value)
    throw Error('Unknown or incomplete option: ' + key);
  args.set(key, value);
}
if (!args.has('--native')) throw Error('Pass --native with the directory containing TubeStageProbe.exe');
const native = path.resolve(args.get('--native'));
const output = path.resolve(args.get('--out') ?? path.join(tube, 'SectionPartition'));
const workers = Number(args.get('--workers') ?? 8), timeout = Number(args.get('--timeout') ?? 120);
if (!Number.isInteger(workers) || workers < 1 || workers > 8 || !Number.isFinite(timeout) || timeout <= 0)
  throw Error('Invalid worker count or timeout');

const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const cacheFile = path.join(tube, 'PathAnalysis/cache-manifest.json');
const cache = read(cacheFile).models;
const selected = new Set((args.get('--models') ?? '').split(',').filter(Boolean));
const models = selected.size ? cache.filter(row => selected.has(String(row.id))) : cache;
if (!models.length || (selected.size && selected.size !== models.length)) throw Error('Invalid model selection');
const startedAt = new Date().toISOString();
const runId = startedAt.replaceAll(':', '-') + '-' + crypto.randomUUID().slice(0, 8);
const run = path.join(output, 'runs', runId);
const regionViewer = path.join(tube, 'PathReview-Full-20261119/RegionPartition-Shared/index.html');
const nativeFiles = ['TubeStageProbe.exe', 'ExtrusionRecognition.dll', 'OpenCascadeResourceImport.dll'];
const producer = Object.fromEntries(nativeFiles.map(name => [name, hash(path.join(native, name))]));
const runtime = path.join(repo, 'src/x64/Debug');
const env = {...process.env, PATH: [native, runtime, process.env.PATH].join(path.delimiter)};
const checkpointFiles = ['performance.json', 'split.icaxbrep', 'machining.icaxbrep',
  'stock.icaxbrep', 'section-curves.icaxbrep', 'section-domains.json'];

function artifactUrl(file) {
  if (!file || !fs.statSync(file, {throwIfNoEntry: false})?.isFile()) return undefined;
  const relative = path.relative(output, file);
  if (!relative || path.isAbsolute(relative) || relative.split(path.sep)[0] === '..')
    throw Error('Unexpected artifact path ' + file);
  return relative.split(path.sep).map(encodeURIComponent).join('/');
}

function write(file, data) {
  const temp = file + '-' + crypto.randomUUID() + '.tmp';
  fs.writeFileSync(temp, JSON.stringify(data, null, 2) + '\n');
  fs.renameSync(temp, file);
}

function execute(command, log) {
  return new Promise((resolve, reject) => {
    const descriptor = fs.openSync(log, 'w');
    const began = Date.now();
    const child = spawn(path.join(native, 'TubeStageProbe.exe'), command,
      {cwd: native, env, windowsHide: true, stdio: ['ignore', descriptor, descriptor]});
    let timedOut = false, error;
    const timer = setTimeout(() => { timedOut = true; child.kill(); }, timeout * 1000);
    child.once('error', cause => { error = cause; });
    child.once('close', code => {
      clearTimeout(timer);
      fs.closeSync(descriptor);
      if (error) reject(error);
      else resolve({code, timedOut, seconds: (Date.now() - began) / 1000});
    });
  });
}

async function replay(model) {
  const id = String(model.id).padStart(3, '0');
  const row = {id, name: model.name, status: 'not-run', upstreamMode: model.available ? 'checkpoint' : 'source'};
  try {
    if (path.basename(model.name) !== model.name) throw Error('Invalid model filename');
    const original = path.join(tube, 'origin', model.name);
    const verified = [{path: original, sha256: model.originalCad.sha256}];
    for (const name of model.available ? checkpointFiles : []) {
      const file = model.files[name], expectedPath = path.join(model.directory, name);
      if (path.resolve(file.path).toLowerCase() !== path.resolve(expectedPath).toLowerCase())
        throw Error('Unexpected checkpoint file ' + name);
      verified.push(file);
    }
    for (const file of verified) if (hash(file.path) !== file.sha256) throw Error('Input changed: ' + file.path);

    const log = path.join(run, 'logs', id + '.log');
    const command = model.available ? ['--region-partition-checkpoint', model.directory]
      : ['--region-partition-source', original];
    const process = await execute([...command, run, model.name, 'part-1'], log);
    for (const file of verified) if (hash(file.path) !== file.sha256) throw Error('Input changed during Work: ' + file.path);
    const reportPath = path.join(run, 'Reports', model.name, 'part-1.json');
    const importPath = path.join(run, 'Imported', model.name, 'import.json');
    const evidenceUrl = artifactUrl(reportPath) ?? artifactUrl(importPath);
    Object.assign(row, {process, evidenceUrl, sourceUnchanged: true});
    if (process.timedOut) {
      const failedStage = fs.existsSync(reportPath) ? read(reportPath).stage : 'import';
      return {...row, status: 'timeout', failedStage, reason: `${failedStage} 超过 ${timeout} 秒`};
    }
    if (!evidenceUrl) throw Error('Native Work did not write a report');
    if (!model.available) {
      const imported = read(importPath);
      if (imported.status !== 'passed')
        return {...row, failedStage: 'import', reason: imported.error ?? 'Native import failed'};
      if (imported.sourceSha256?.toLowerCase() !== model.originalCad.sha256.toLowerCase())
        throw Error('Native import did not confirm the original CAD hash');
    }
    const report = read(reportPath), stage = report.stages?.regionPartition;
    if (!model.available && stage?.status === 'not-run')
      return {...row, failedStage: stage.blockedStage, reason: stage.reason};
    if (model.available ? !stage?.checkpointCadUnchanged :
      !stage?.sourceUnchanged || report.stages.classify?.status !== 'passed' || report.stages.contour?.status !== 'passed')
      throw Error(report.error ?? 'Native Work did not verify its Classify input');
    const data = read(stage.dataPath);
    if (data.stage !== 'regionPartition' || data.status !== stage.resultStatus)
      throw Error('Inconsistent native region output');
    if (stage.resultStatus === 'complete' &&
        (process.code !== 0 || !stage.brepRoundTripPassed || !data.reread?.valid || !artifactUrl(stage.brepPath)))
      throw Error('Native material output or BREP read-back failed');
    return {...row, status: stage.resultStatus, reason: data.reason, counts: data.counts, sharedArea: data.sharedArea,
      boundaryProvenanceComplete: data.boundaryProvenanceComplete,
      sourceSha256: model.originalCad.sha256, classifyStatus: 'passed', contourStatus: 'passed',
      dataUrl: artifactUrl(stage.dataPath), brepUrl: artifactUrl(stage.brepPath),
      contourUrl: artifactUrl(stage.contourPreviewPath)};
  } catch (error) {
    return {...row, status: 'failed', reason: error.message};
  }
}

fs.mkdirSync(path.join(run, 'logs'), {recursive: true});
const records = [];
let next = 0;
await Promise.all(Array.from({length: Math.min(workers, models.length)}, async () => {
  while (next < models.length) {
    const row = await replay(models[next++]);
    records.push(row);
    if (records.length % 20 === 0 || row.status === 'failed' || row.status === 'timeout')
      console.log(`${records.length}/${models.length} ${row.id} ${row.status} ${row.reason ?? ''}`);
  }
}));
for (const [name, digest] of Object.entries(producer))
  if (hash(path.join(native, name)) !== digest) throw Error('Native producer changed during the run: ' + name);
records.sort((a, b) => Number(a.id) - Number(b.id));
const counts = {};
for (const row of records) counts[row.status] = (counts[row.status] ?? 0) + 1;
const manifest = {schema: 1, status: 'completed', stage: 'regionPartition', runId, startedAt, producer,
  inputCount: records.length, counts, records};
write(path.join(run, 'summary.json'), manifest);
fs.copyFileSync(regionViewer, path.join(output, 'index.html'));
write(path.join(output, 'manifest.json'), manifest);
console.log(JSON.stringify({runId, counts, manifest: path.join(output, 'manifest.json')}));
if (records.some(row => row.status === 'failed' || row.status === 'timeout')) process.exitCode = 1;
