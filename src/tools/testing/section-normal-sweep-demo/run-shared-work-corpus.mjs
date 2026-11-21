import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { checkSharedSectionTools, runSharedSection } from './run-shared-work.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../../../..');
const inputRoot = path.join(root, 'TestData/Tube/SectionRegionWork-20261118/contours');
const destination = path.join(root, 'TestData/Tube/PathReview-20261119/SectionSharedCoverage');
const workers = Number(process.argv[2] ?? '3');
if (!Number.isInteger(workers) || workers < 1 || workers > 4)
  throw new Error('Worker count must be between 1 and 4.');
checkSharedSectionTools();

const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
function write(file, value) {
  const pending = file + '.tmp';
  fs.writeFileSync(pending, JSON.stringify(value, null, 2) + '\n');
  fs.renameSync(pending, file);
}
const parentManifest = path.join(inputRoot, 'manifest.json');
const input = read(parentManifest);
if (!input.allFinished || input.records.length !== input.inputCount)
  throw new Error('Input contour batch is incomplete.');
const records = input.records.map(record => ({
  id: record.id, name: record.name, source: record.input, sourceSha256: record.inputSha256,
  sourceMetadataSha256: record.sourceMetadataSha256,
  status: 'not-run', reason: '等待壁区共享覆盖 Work',
}));
fs.mkdirSync(destination, { recursive: true });
const manifestPath = path.join(destination, 'manifest.json');
const startedAt = new Date().toISOString();
const manifest = { schema: 1, work: 'SectionSharedCoverage', inputCount: records.length, startedAt,
  inputManifest: path.relative(root, parentManifest).split(path.sep).join('/'),
  inputManifestSha256: sha(parentManifest), workers, records };
function reportProgress(allFinished) {
  const statuses = ['complete', 'unresolved', 'unsupported', 'failed', 'not-run'];
  manifest.counts = Object.fromEntries(statuses.map(status => [status, records.filter(row => row.status === status).length]));
  manifest.completedCount = records.length - manifest.counts['not-run'];
  manifest.allFinished = allFinished;
  manifest.batchStatus = allFinished ? 'completed' : 'running';
  if (allFinished) manifest.completedAt = new Date().toISOString();
  write(manifestPath, manifest);
}
reportProgress(false);

let next = 0;
await Promise.all(Array.from({ length: workers }, async () => {
  while (next < input.records.length) {
    const index = next++;
    const record = input.records[index];
    const row = records[index];
    const at = Date.now();
    try {
      const result = record.inputUnchanged && record.sourceMetadataUnchanged
        ? await runSharedSection(record, inputRoot, destination)
        : { status: 'unresolved', reason: '缺少通过验收的精确输入轮廓' };
      Object.assign(row, { reason: result.reason ?? '' }, result);
    } catch (error) {
      Object.assign(row, { status: 'failed', reason: error.message });
    }
    row.elapsedMs = Date.now() - at;
    reportProgress(false);
    if (manifest.completedCount % 20 === 0 || row.status === 'failed') {
      console.log(`${manifest.completedCount}/${manifest.inputCount} ${JSON.stringify(manifest.counts)}${row.status === 'failed' ? ` | ${row.id} ${row.reason}` : ''}`);
    }
  }
}));
reportProgress(true);
console.log(JSON.stringify({ output: manifestPath, completedAt: manifest.completedAt, counts: manifest.counts }));
