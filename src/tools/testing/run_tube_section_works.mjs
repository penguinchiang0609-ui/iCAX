import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {checkSharedSectionTools, runSharedSection} from './section-normal-sweep-demo/run-shared-work.mjs';

// Contour owns the exact section and its bounded analytic fitting. An explicit
// Classify input enables the normal-sweep region Work on that exact section.
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const solverDir = path.join(root, 'output/tests/section-regions-native-20261113');
const arg = name => process.argv.find(value => value.startsWith(`--${name}=`))?.slice(name.length + 3);
const inputDir = path.resolve(arg('input') || path.join(root, 'TestData/Tube/Coutour'));
const historicalOutput = path.join(root, 'TestData/Tube/SectionRegionWork-20261118');
const outputArg = arg('output');
const outputDir = path.resolve(outputArg || historicalOutput);
const classifyManifestArg = arg('classify-manifest');
const selected = arg('names')?.split('|').filter(Boolean);
const timeoutMs = Number(arg('timeout-ms') || 60000);
const concurrency = Number(arg('workers') || 3);
const solver = path.join(solverDir, 'bin/SectionRegionsQuasi.exe');
const search = path.join(solverDir, 'solve-carrier.mjs');
const verifier = path.join(solverDir, 'bin/RegionVerification.exe');
const sharedAlgorithm = 'normal-sweep-shared-coverage';
const env = {
  ...process.env,
  PATH: path.join(root, 'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bind') + ';' + process.env.PATH,
};
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const sha = file => digest(fs.readFileSync(file));
const rel = file => path.relative(root, file).replaceAll('\\', '/');
const parse = text => JSON.parse(text.replace(/^\uFEFF/, ''));
const read = file => parse(fs.readFileSync(file, 'utf8'));
const write = (file, value) => fs.writeFileSync(file, JSON.stringify(value, null, 2));
const fileKey = filename => {
  if (!filename) return null;
  const resolved = path.resolve(filename);
  return process.platform === 'win32' ? resolved.toLowerCase() : resolved;
};
const sameFile = (a, b) => !!a && !!b && fileKey(a) === fileKey(b);

function loadClassifyManifest(filename) {
  const file = path.resolve(filename);
  const bytes = fs.readFileSync(file);
  const {models} = parse(bytes.toString('utf8'));
  if (!Array.isArray(models) || !models.length) throw new Error('Classify manifest has no model records');
  const byContour = new Map();
  for (const model of models) {
    const contour = fileKey(model.works?.contour?.provenance?.contourPath);
    if (!contour || !model.works.classify) throw new Error(`Missing contour or Classify binding for ${model.name}`);
    if (byContour.has(contour)) throw new Error(`Duplicate Classify binding for contour ${contour}`);
    byContour.set(contour, model);
  }
  return {file: rel(file), sha256: digest(bytes), byContour};
}

function verifyClassify(input, caseInput) {
  const model = input.byContour.get(fileKey(caseInput.file));
  if (!model) throw new Error(`No verified Classify record for ${caseInput.file}`);
  const {import: imported, contour, classify} = model.works;
  const binding = contour.provenance;
  const prov = classify.provenance;
  if (!['passed', 'failed', 'not-run'].includes(classify.status))
    throw new Error(`Missing native Classify status for ${model.name}`);
  if (!caseInput.metadataSha256 || binding.contourSha256 !== caseInput.sha256 ||
      binding.contourMetadataSha256 !== caseInput.metadataSha256 || !sameFile(binding.source, prov?.source) ||
      binding.sourceSha256 !== prov.sourceSha256 || sha(binding.source) !== binding.sourceSha256)
    throw new Error(`Contour and Classify do not match the current CAD and contour files for ${model.name}`);
  const evidence = {status: classify.status, modelId: model.id, model: model.name,
    source: binding.source, sourceSha256: binding.sourceSha256, contourSha256: caseInput.sha256,
    contourMetadataSha256: caseInput.metadataSha256, runKind: prov.runKind, nativeComputedThisRun: false};
  if (classify.status !== 'passed') {
    const report = prov.report;
    if (!report?.sha256 || sha(report.sourcePath) !== report.sha256)
      throw new Error(`Native Classify failure report changed for ${model.name}`);
    const native = read(report.sourcePath);
    if (native.stages?.classify?.status === 'passed')
      throw new Error(`Classify status contradicts its native report for ${model.name}`);
    const failedStage = [classify, contour.views?.native, model.works.align, model.works.heal, imported]
      .find(stage => stage?.status === 'failed' && stage.reason);
    return {...evidence, report, blockedStage: native.stage, reason: native.error || failedStage?.reason || classify.reason};
  }

  const checkpoint = prov.checkpoint;
  if (!checkpoint?.directory || prov.inputUnchanged !== true) throw new Error(`Missing Classify checkpoint for ${model.name}`);
  for (const name of ['split.icaxbrep', 'machining.icaxbrep', 'stock.icaxbrep',
    'section-curves.icaxbrep', 'section-domains.json', 'performance.json']) {
    const record = checkpoint.files?.[name];
    if (!record?.sha256 || !sameFile(record.path, path.join(checkpoint.directory, name)) || sha(record.path) !== record.sha256)
      throw new Error(`Classify checkpoint file changed for ${model.name}: ${name}`);
  }
  const proof = read(checkpoint.files['performance.json'].path);
  const neutral = imported.previewEvidence;
  const report = {sourcePath: path.join(checkpoint.directory, 'native-report.json'), sha256: prov.report?.sha256};
  if (proof.status !== 'passed' || proof.sourceNeutralUnchanged !== true ||
      !['heal', 'align', 'contour', 'classify'].every(stage => proof.stages?.[stage]?.passed === true) ||
      !sameFile(proof.source, neutral?.sourcePath) ||
      sha(path.join(checkpoint.directory, 'imported-neutral.icaxbrep')) !== neutral.sourceSha256 ||
      !report.sha256 || sha(report.sourcePath) !== report.sha256 || read(report.sourcePath).stages?.classify?.status !== 'passed')
    throw new Error(`Classify checkpoint did not certify the imported geometry for ${model.name}`);
  return {...evidence, checkpoint, report};
}

if (!Number.isFinite(timeoutMs) || timeoutMs <= 0 || !Number.isInteger(concurrency) || concurrency < 1)
  throw new Error('Invalid timeout or worker count');
if (classifyManifestArg !== undefined && (!classifyManifestArg || !outputArg || sameFile(outputDir, historicalOutput)))
  throw new Error('Use --classify-manifest=<verified manifest> with a separate --output directory');
const classifyInput = classifyManifestArg ? loadClassifyManifest(classifyManifestArg) : null;
if (classifyInput) checkSharedSectionTools();
for (const file of classifyInput ? [solver] : [solver, search, verifier])
  if (!fs.existsSync(file)) throw new Error(`Missing tool: ${file}`);
if (!fs.statSync(inputDir).isDirectory()) throw new Error(`Missing contour directory: ${inputDir}`);
fs.mkdirSync(path.join(outputDir, 'contours'), {recursive: true});
for (const name of classifyInput ? [] : ['index.html', 'fitting.html']) {
  const source = path.join(root, 'TestData/Tube/SectionRegionsQuasi-20261117', name);
  if (!fs.existsSync(source)) continue;
  let page = fs.readFileSync(source, 'utf8')
    .replace(/<option value="fixtures">[^<]*<\/option>/g, '')
    .replaceAll("location.hash.startsWith('#fixtures/')?'fixtures':'contours'", "'contours'");
  if (name === 'index.html') page = page
    .replaceAll('曲线缺料区支撑检查', '截面分区 Work 检查')
    .replace("link('运行记录',dir+'/'+r.id+'/run.json');",
      "link('外轮廓 Work',dir+'/'+r.id+'/contour-work.json');link('简化曲线',dir+'/'+r.id+'/contour-fitting.json');link('分区 Work',dir+'/'+r.id+'/region-work.json');link('运行记录',dir+'/'+r.id+'/run.json');");
  else page = page.replaceAll('regions-fitting.json', 'contour-fitting.json');
  fs.writeFileSync(path.join(outputDir, name), page);
}

const allFiles = fs.readdirSync(inputDir).filter(name => name.toLowerCase().endsWith('.step')).sort();
const files = allFiles.filter(name => !selected || selected.includes(path.parse(name).name));
if (!files.length) throw new Error('No matching contour STEP inputs');
const solverSha256 = sha(solver);
const startedAt = new Date().toISOString();
const records = [];
const manifestFile = classifyInput ? path.join(outputDir, 'manifest.json')
  : path.join(outputDir, 'contours', selected ? 'manifest-selected.json' : 'manifest.json');
function manifest(allFinished) {
  write(manifestFile, {
    schema: 2,
    pipeline: ['Coutour (original plus simplify)', ...(classifyInput ? ['Classify'] : []), 'SectionRegions'],
    ...(classifyInput ? {classifyInput: {path: classifyInput.file, sha256: classifyInput.sha256},
      work: 'RegionPartition', algorithm: sharedAlgorithm, counts: sharedCounts()} : {}),
    fittingToleranceMm: 0.001,
    source: rel(inputDir),
    solverSha256,
    startedAt,
    allFinished,
    inputCount: files.length,
    records: [...records].sort((a, b) => a.name.localeCompare(b.name)),
  });
}

function sharedCounts() {
  return Object.fromEntries(['complete', 'unresolved', 'unsupported', 'failed', 'not-run']
    .map(status => [status, records.filter(record => record.status === status).length]));
}
manifest(false);

function execute(exe, args, log) {
  return new Promise(resolve => {
    let stdout = '', stderr = '', settled = false, timedOut = false;
    const child = spawn(exe, args, {cwd: root, env, windowsHide: true});
    const timer = setTimeout(() => {timedOut = true; child.kill();}, timeoutMs);
    child.stdout.on('data', data => {stdout += data;});
    child.stderr.on('data', data => {stderr += data;});
    const finish = (code, error) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      if (error) stderr += error.message;
      fs.writeFileSync(log, stdout + (stderr ? '\n' + stderr : ''));
      resolve({code, timedOut, stderr: stderr.trim()});
    };
    child.once('error', error => finish(-1, error));
    child.once('close', code => finish(code));
  });
}

async function runOne(file) {
  const name = path.parse(file).name;
  const id = String(allFiles.indexOf(file) + 1).padStart(3, '0');
  const dir = path.join(outputDir, 'contours', id);
  fs.mkdirSync(dir, {recursive: true});
  for (const name of [
    'contour-fitting.json', 'contour-work.json', 'classify-work.json', 'regions-carrier.json',
    'regions-fitting.json', 'regions-polygons.txt', 'regions-polygons.txt.json',
    'regions-polygons.txt.diagnostics.json', 'regions.json', 'regions.brep',
    'region-work.json', 'region-carrier.log', 'region-search.log', 'region-native.log', 'region-check.log',
  ]) fs.rmSync(path.join(dir, name), {force: true});
  if (classifyInput) {
    for (const name of ['view.json', 'cells.brep', 'shared-work.json'])
      fs.rmSync(path.join(outputDir, 'shared', id, name), {force: true});
  }
  const source = path.join(inputDir, file);
  // OCCT's STEP reader accepts a narrow path. Use an ASCII working filename
  // for non-ASCII source names and verify the original input separately.
  const localStep = path.join(dir, 'input.step');
  const metadataFile = path.join(inputDir, name + '.json');
  const originalSha256 = sha(source);
  const result = {
    id, name, input: rel(source), inputSha256: originalSha256,
    sourceMetadataSha256: null,
    solverSha256, stages: {}, status: 'running', result: null, brep: null,
  };
  const started = Date.now();
  let activeStage = 'Coutour';
  try {
    fs.copyFileSync(source, localStep);
    if (sha(localStep) !== originalSha256) throw new Error('Contour working copy differs from source');
    const metadata = fs.readFileSync(metadataFile);
    result.sourceMetadataSha256 = digest(metadata);
    const meta = parse(metadata.toString('utf8'));
    if (!Array.isArray(meta.wires) || !meta.wires.length || !meta.wires.every(w => w.closed && w.valid))
      throw new Error('Contour Work did not certify complete closed wires');
    const gap = Math.max(...meta.wires.map(w => w.maxGapMm || 0));
    const identities = path.join(dir, 'contour-source-edges.txt');
    const lines = meta.wires.flatMap((wire, wi) => wire.edges.map((edge, ei) => [
      JSON.stringify(String(edge.sourceEdgeId)), wi, ei,
      ...(edge.exactStart || edge.start), ...(edge.exactEnd || edge.end),
    ].join(' ')));
    fs.writeFileSync(identities, String(meta.overlayRoundTrip?.planeErrorMm || 0) + '\n' + lines.join('\n') + '\n');
    const common = [localStep, path.join(dir, 'regions'), String(meta.endX), String(gap), identities];

    const contourStart = Date.now();
    const fittingPrefix = path.join(dir, 'contour');
    const simplified = await execute(solver, [localStep, fittingPrefix, String(meta.endX), String(gap), identities, 'simplify'], path.join(dir, 'contour.log'));
    if (simplified.code !== 0) throw new Error(`Contour simplify failed: ${simplified.stderr || simplified.code}`);
    const fitting = read(fittingPrefix + '-fitting.json');
    if (fitting.toleranceMm !== 0.001 || fitting.curves.some(curve => curve.deviationUpperBoundMm > 0.001))
      throw new Error('Contour fitting exceeds the declared 0.001 mm bound');
    fs.renameSync(fittingPrefix + '-fitting.json', path.join(dir, 'contour-fitting.json'));
    const contourArtifactDir = classifyInput ? `contours/${id}` : id;
    result.stages.Coutour = {
      status: 'passed', inputEdgeCount: meta.wires.reduce((sum, wire) => sum + wire.edges.length, 0),
      simplifiedCurveCount: fitting.curves.length,
      fittingToleranceMm: fitting.toleranceMm,
      fitting: `${contourArtifactDir}/contour-fitting.json`,
      originalSourceEdges: `${contourArtifactDir}/contour-source-edges.txt`,
      sourceGeometryChanged: false,
      elapsedMs: Date.now() - contourStart,
    };
    write(path.join(dir, 'contour-work.json'), result.stages.Coutour);

    if (classifyInput) {
      activeStage = 'Classify';
      const classification = verifyClassify(classifyInput, {
        file: source, sha256: originalSha256, metadataSha256: result.sourceMetadataSha256,
      });
      result.stages.Classify = classification;
      write(path.join(dir, 'classify-work.json'), classification);
      if (classification.status !== 'passed') throw new Error(classification.reason || 'Classify 未完成');
    }

    activeStage = 'SectionRegions';
    const regionStart = Date.now();
    if (classifyInput) {
      const region = await runSharedSection(result, outputDir, outputDir);
      const {status, ...details} = region;
      Object.assign(result, details, {status, result: region.dataUrl ?? null, brep: region.brepUrl ?? null});
      result.stages.SectionRegions = {
        ...details, resultStatus: status, elapsedMs: Date.now() - regionStart,
        status: {complete: 'passed', unresolved: 'partial', unsupported: 'unsupported'}[status],
      };
    } else {
      const carrier = await execute(solver, [...common, 'carrier'], path.join(dir, 'region-carrier.log'));
      if (carrier.code !== 0) throw new Error(`Region carrier failed: ${carrier.stderr || carrier.code}`);
      const polygons = path.join(dir, 'regions-polygons.txt');
      const searched = await execute(process.execPath, [search, path.join(dir, 'regions-carrier.json'), polygons], path.join(dir, 'region-search.log'));
      if (searched.code !== 0) throw new Error(`Region search failed: ${searched.stderr || searched.code}`);
      const built = await execute(solver, [...common, 'support', polygons], path.join(dir, 'region-native.log'));
      if (built.code !== 0) throw new Error(`Native region construction failed: ${built.stderr || built.code}`);
      const regions = read(path.join(dir, 'regions.json'));
      if (regions.status !== 'support-regions' || !regions.coverageValid || !regions.inputUnchanged)
        throw new Error(`Region coverage failed: ${regions.status}`);
      const checked = await execute(verifier, ['check', path.join(dir, 'regions.brep')], path.join(dir, 'region-check.log'));
      if (checked.code !== 0) throw new Error(`Saved region BREP failed native reread: ${checked.stderr || checked.code}`);
      const check = read(path.join(dir, 'region-check.log'));
      if (!check.valid || check.faces !== regions.regions.length || check.regionComponents.some(count => count !== 1))
        throw new Error('Saved region BREP face count, connectivity or validity mismatch');
      result.stages.SectionRegions = {
        status: 'passed', regionCount: regions.regions.length,
        nativeToleranceMm: regions.nativeTolerance,
        missingAreaMm2: regions.missingArea,
        excessAreaMm2: regions.excessArea,
        overlapAreaMm2: regions.overlapArea,
        carrier: `${id}/regions-carrier.json`,
        brep: `${id}/regions.brep`, elapsedMs: Date.now() - regionStart,
      };
      write(path.join(dir, 'region-work.json'), result.stages.SectionRegions);
      result.status = 'support-regions';
      result.result = `${id}/regions.json`;
      result.brep = `${id}/regions.brep`;
      result.regionCount = regions.regions.length;
      result.coverageValid = true;
    }
  } catch (error) {
    result.reason = error.message;
    const blocked = activeStage === 'Classify' && result.stages.Classify && result.stages.Classify.status !== 'passed';
    result.status = blocked ? 'not-run' : 'failed';
    result.stages[activeStage] ??= {status: 'failed', reason: error.message};
    if (classifyInput && activeStage !== 'SectionRegions') {
      if (activeStage === 'Coutour') result.stages.Classify = {status: 'not-run', reason: error.message};
      result.stages.SectionRegions = {status: 'not-run', reason: error.message};
      write(path.join(dir, 'classify-work.json'), result.stages.Classify);
    }
  }
  if (classifyInput) {
    result.stages.SectionRegions.algorithm = sharedAlgorithm;
    result.source = result.input;
    result.sourceSha256 = result.inputSha256;
    result.fittingUrl = result.stages.Coutour.fitting ?? null;
    result.regionWorkUrl = `contours/${id}/region-work.json`;
    write(path.join(dir, 'region-work.json'), result.stages.SectionRegions);
  }
  fs.rmSync(localStep, {force: true});
  result.inputUnchanged = sha(source) === originalSha256;
  result.sourceMetadataUnchanged = result.sourceMetadataSha256 === (fs.existsSync(metadataFile) ? sha(metadataFile) : null);
  result.elapsedMs = Date.now() - started;
  write(path.join(dir, 'run.json'), result);
  records.push(result);
  manifest(records.length === files.length);
  console.log(JSON.stringify({name, status: result.status, regions: result.regionCount, reason: result.reason}));
}

let cursor = 0;
await Promise.all(Array.from({length: Math.min(concurrency, files.length)}, async () => {
  while (cursor < files.length) await runOne(files[cursor++]);
}));
manifest(true);
const counts = classifyInput ? sharedCounts() : {
  passed: records.filter(record => record.status === 'support-regions').length,
  failed: records.filter(record => record.status === 'failed').length,
};
console.log(JSON.stringify({total: records.length, ...counts, output: outputDir}, null, 2));
if ((counts.complete ?? counts.passed) !== records.length) process.exitCode = 1;
