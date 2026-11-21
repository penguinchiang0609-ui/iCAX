import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const work = path.join(root, 'TestData/Tube/SectionRegionWork-20261118/contours');
const baseline = path.join(root, 'TestData/Tube/SectionRegionsQuasi-20261117/contours');
const verifier = path.join(root, 'output/tests/section-regions-native-20261113/bin/RegionVerification.exe');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const manifest = read(path.join(work, 'manifest.json'));
const old = new Map(read(path.join(baseline, 'manifest.json')).records.map(record => [record.name, record]));
const failures = [];
const check = (condition, name, message) => {if (!condition) failures.push({name, message});};
let maximumFitDeviationMm = 0;
let maximumMissingAreaMm2 = 0;
let maximumExcessAreaMm2 = 0;
let fittedLine = 0, fittedCircle = 0, fittedEllipse = 0, fittedSpline = 0;
check(manifest.allFinished && manifest.inputCount === 270 && manifest.records.length === 270, 'batch', 'Expected all 270 contour inputs');
for (const record of manifest.records) {
  const dir = path.join(work, record.id);
  const source = path.join(root, record.input);
  const metadata = path.join(path.dirname(source), record.name + '.json');
  check(record.inputUnchanged && hash(source) === record.inputSha256, record.name, 'STEP input changed');
  check(record.sourceMetadataUnchanged && hash(metadata) === record.sourceMetadataSha256, record.name, 'Contour metadata changed');
  check(record.status === 'support-regions' && record.stages.Coutour?.status === 'passed'
    && record.stages.SectionRegions?.status === 'passed', record.name, record.reason || 'A stage failed');
  if (record.status !== 'support-regions') continue;
  const fitting = read(path.join(dir, 'contour-fitting.json'));
  const regions = read(path.join(dir, 'regions.json'));
  const saved = read(path.join(dir, 'region-check.log'));
  check(fitting.toleranceMm === 0.001 && fitting.originalGeometryChanged === false, record.name, 'Fitting contract failed');
  check(fitting.curves.length === record.stages.Coutour.simplifiedCurveCount, record.name, 'Fitting count mismatch');
  check(fitting.curves.every(curve => curve.deviationUpperBoundMm <= 0.001), record.name, 'Fitting bound exceeded');
  check(regions.status === 'support-regions' && regions.coverageValid && regions.inputUnchanged
    && !regions.curveGeometrySnapped, record.name, 'Native material coverage failed');
  check(saved.valid && saved.faces === regions.regions.length, record.name, 'Saved BREP reread failed');
  check(record.regionCount === regions.regions.length && record.regionCount === old.get(record.name)?.regionCount,
    record.name, 'Region count changed from verified baseline');
  maximumMissingAreaMm2 = Math.max(maximumMissingAreaMm2, regions.missingArea);
  maximumExcessAreaMm2 = Math.max(maximumExcessAreaMm2, regions.excessArea);
  for (const curve of fitting.curves) {
    maximumFitDeviationMm = Math.max(maximumFitDeviationMm, curve.deviationUpperBoundMm);
    if (curve.type === 'line') fittedLine++;
    else if (curve.type === 'circle') fittedCircle++;
    else if (curve.type === 'ellipse') fittedEllipse++;
    else fittedSpline++;
  }
}

const env = {...process.env, PATH: path.join(root, 'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bind') + ';' + process.env.PATH};
const comparisons = [];
for (const name of ['002', '053', '1_p_30', '左辊压梁']) {
  const current = manifest.records.find(record => record.name === name);
  const previous = old.get(name);
  if (!current || !previous || !current.brep || !previous.brep) {
    check(false, name, 'Missing region BREP for comparison');
    continue;
  }
  const currentData = read(path.join(work, current.result));
  const areaTolerance = 20 * currentData.nativeTolerance * Math.sqrt(currentData.inputArea);
  const run = spawnSync(verifier, ['compare', path.join(work, current.brep), path.join(baseline, previous.brep),
    String(currentData.nativeTolerance), String(areaTolerance), 'identity'], {env, windowsHide: true, encoding: 'utf8'});
  let result;
  try {result = JSON.parse(run.stdout);} catch {result = {equivalent: false, error: run.stderr || run.stdout};}
  comparisons.push({name, ...result});
  check(run.status === 0 && result.equivalent, name, 'Region BREP differs from verified baseline');
}

const report = {
  schema: 1, inputs: manifest.records.length, passed: failures.length === 0,
  fittingToleranceMm: 0.001, maximumFitDeviationMm,
  fittedCurveCounts: {line: fittedLine, circle: fittedCircle, ellipse: fittedEllipse, spline: fittedSpline},
  maximumMissingAreaMm2, maximumExcessAreaMm2, comparisons, failures,
};
fs.writeFileSync(path.join(path.dirname(work), 'verification.json'), JSON.stringify(report, null, 2));
console.log(JSON.stringify(report, null, 2));
if (failures.length) process.exitCode = 1;
