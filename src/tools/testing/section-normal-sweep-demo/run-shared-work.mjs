import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { fileURLToPath } from 'node:url';
import { planSharedCoverage } from './section-normal-sweep.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../../../..');
const output = path.join(root, 'TestData/Tube/SectionNormalSweepDemo');
const contourWork = path.join(root, 'TestData/Tube/SectionRegionWork-20261118/contours');
const bin = path.join(root, 'output/tests/section-regions-native-20261113/bin');
const exporter = path.join(bin, 'SectionNormalSweepExport.exe');
const builder = path.join(bin, 'SectionSharedRegions.exe');
const overlay = path.join(bin, 'RegionOverlapPartition.exe');
const verifier = path.join(bin, 'RegionVerification.exe');
const env = { ...process.env, PATH: [
  bin, path.join(root, 'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bind'), process.env.PATH,
].join(path.delimiter) };
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const write = (file, data) => fs.writeFileSync(file, JSON.stringify(data, null, 2) + '\n');
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const execute = promisify(execFile);

export async function runSharedNative(exe, args) {
  try {
    const { stdout } = await execute(exe, args, { cwd: root, env, encoding: 'utf8', windowsHide: true, timeout: 90000 });
    return stdout;
  } catch (failure) {
    throw new Error(`${path.basename(exe)}: ${failure.stderr?.trim() || failure.stdout?.trim() || failure.message}`);
  }
}

function writePlan(file, plan) {
  const lines = ['ICAX_SHARED_COVERAGE_V1', String(plan.walls.length)];
  for (const { polygon } of plan.walls) {
    lines.push(String(polygon.length));
    lines.push(...polygon.map(point => point.join(' ')));
  }
  lines.push(String(plan.patchRules.length));
  for (const rule of plan.patchRules)
    lines.push([rule.curveId, rule.ownerWallIds.length, ...rule.ownerWallIds].join(' '));
  fs.writeFileSync(file, lines.join('\n') + '\n');
}

function sharedSectionArguments(sample, contourRecords, contourRoot = contourWork) {
  const source = path.join(root, sample.source);
  if (path.extname(source).toLowerCase() !== '.step') return { source, extras: [] };
  const record = contourRecords.find(row => row.input === sample.source);
  if (!record || sha(source) !== record.inputSha256) throw new Error('原始 STEP 与轮廓 Work 的记录不一致');
  const metadataPath = source.replace(/\.step$/i, '.json');
  if (record.sourceMetadataSha256 && sha(metadataPath) !== record.sourceMetadataSha256)
    throw new Error('原始 STEP 的轮廓元数据与 Work 的记录不一致');
  const meta = read(metadataPath);
  const gap = Math.max(...meta.wires.map(wire => wire.maxGapMm || 0));
  const edges = record.stages?.Coutour?.originalSourceEdges ?? `${record.id}/contour-source-edges.txt`;
  const edgesFile = path.resolve(contourRoot, edges);
  if (!edgesFile.startsWith(contourRoot + path.sep)) throw new Error('Unexpected contour edge identity path.');
  return { source, extras: [String(meta.endX), String(gap), edgesFile] };
}

function organizeCells(plan, data) {
  const junctions = plan.junctions.map(junction => ({ ...junction, cellIds: [] }));
  const walls = plan.walls.map(({ id, label, guideIds, supportIds }) => (
    { id, label, guideIds, supportIds, cellIds: [], area: 0 }
  ));
  const cells = data.cells.map(({ id, area, displayWires, sourceRegionIds: ownerWallIds }) => {
    let junctionId = null;
    if (ownerWallIds.length > 1) {
      const matches = junctions.filter(junction => junction.wallIds.length === ownerWallIds.length &&
        ownerWallIds.every(id => junction.wallIds.includes(id)));
      if (matches.length !== 1) throw new Error('共享材料无法唯一关联到对应交界');
      junctionId = matches[0].id;
      matches[0].cellIds.push(id);
    }
    for (const wallId of ownerWallIds) {
      walls[wallId - 1].cellIds.push(id);
      walls[wallId - 1].area += area;
    }
    return { id, area, displayWires, ownerWallIds, junctionId };
  });
  if (junctions.some(j => !j.cellIds.length) || walls.some(w => !w.cellIds.length))
    throw new Error('壁区或交界没有对应的材料块');
  return { walls, cells, junctions };
}

export async function publishSharedCase(sample, contourRecords, destination = output, sectionInput) {
  const plan = planSharedCoverage(sample);
  if (plan.status !== 'planned') return { id: sample.id, status: 'unresolved', reason: plan.reason };
  const dir = path.join(destination, 'shared', sample.id);
  fs.mkdirSync(dir, { recursive: true });
  const scratch = fs.mkdtempSync(path.join(dir, '.work-'));
  const planFile = path.join(scratch, 'plan.txt');
  const wallsPrefix = path.join(scratch, 'candidate-walls');
  const cellsPrefix = path.join(scratch, 'cells');
  try {
    const { source, extras, sourceCopy } = sectionInput ?? sharedSectionArguments(sample, contourRecords);
    const sourceSha256 = sha(source);
    if (sourceCopy && sha(sourceCopy) !== sourceSha256)
      throw new Error('工作副本与原始截面不一致');
    writePlan(planFile, plan);
    await runSharedNative(builder, [sourceCopy || source, planFile, wallsPrefix, ...extras]);
    const coverage = read(wallsPrefix + '.json');
    if (coverage.missingArea > coverage.areaTolerance || coverage.excessArea > coverage.areaTolerance ||
        Math.abs(coverage.materialArea - sample.nativeMaterialAreaMm2) > coverage.areaTolerance)
      throw new Error('原生壁区不能完整覆盖原截面');

    await runSharedNative(overlay, [wallsPrefix + '.brep', cellsPrefix, String(plan.walls.length), String(coverage.nativeTolerance)]);
    const partition = read(cellsPrefix + '.json');
    const reread = JSON.parse(await runSharedNative(verifier, ['check', cellsPrefix + '.brep']));
    if (!partition.coverageValid || !reread.valid || reread.regionComponents.length !== partition.cellCount ||
        reread.regionComponents.some(count => count !== 1) ||
        [partition.missingArea, partition.excessArea, partition.remainingOverlapArea, partition.worstMembershipResidualArea]
          .some(value => !Number.isFinite(value) || value > partition.areaTolerance) ||
        Math.abs(reread.area - coverage.materialArea) > coverage.areaTolerance)
      throw new Error('材料块原生重读、覆盖或归属校验失败');
    if (sha(source) !== sourceSha256) throw new Error('原生截面输入在处理时已发生变化');

    const geometry = organizeCells(plan, partition);
    if (geometry.walls.some((wall, i) => Math.abs(wall.area - coverage.regionAreas[i]) > coverage.areaTolerance))
      throw new Error('各壁区无法从材料块完整重建');
    const links = { brepUrl: `shared/${sample.id}/cells.brep`, evidenceUrl: `shared/${sample.id}/shared-work.json` };
    const result = { id: sample.id, status: 'complete', materialArea: coverage.materialArea, sharedArea: partition.sharedArea,
      ...geometry, ...links };
    const evidence = { schema: 1, work: 'SectionSharedCoverage', id: sample.id,
      source: sample.source, sourceSha256, plan, coverage, partition: { ...partition,
        cells: geometry.cells.map(({ displayWires: _, ...cell }) => cell) }, reread,
      walls: geometry.walls, junctions: geometry.junctions, ...links };
    write(path.join(scratch, 'shared-work.json'), evidence);
    fs.renameSync(cellsPrefix + '.brep', path.join(dir, 'cells.brep'));
    fs.renameSync(path.join(scratch, 'shared-work.json'), path.join(dir, 'shared-work.json'));
    return result;
  } finally {
    for (const name of ['plan.txt', 'candidate-walls.brep', 'candidate-walls.json', 'cells.brep', 'cells.json', 'shared-work.json'])
      fs.rmSync(path.join(scratch, name), { force: true });
    fs.rmdirSync(scratch);
  }
}

export function checkSharedNativeTools() {
  for (const exe of [builder, overlay, verifier]) if (!fs.existsSync(exe))
    throw new Error(`Missing native tool: ${exe}`);
}

export function checkSharedSectionTools() {
  checkSharedNativeTools();
  if (!fs.existsSync(exporter)) throw new Error(`Missing native tool: ${exporter}`);
}

export async function runSharedSection(record, contourRoot, destination) {
  if (!/^[0-9]+$/.test(record.id)) throw new Error('Unexpected contour record ID.');
  if (record.stages?.Coutour?.status !== 'passed')
    return { status: 'unresolved', reason: '缺少通过验收的精确输入轮廓' };
  const { source, extras } = sharedSectionArguments({ source: record.input }, [record], contourRoot);
  const fittingPath = path.resolve(contourRoot, record.stages.Coutour.fitting);
  if (!fittingPath.startsWith(contourRoot + path.sep)) throw new Error('Unexpected input fitting path.');
  const fit = read(fittingPath);
  const unsupported = [...new Set(fit.curves.map(curve => curve.type).filter(type => !['line', 'circle'].includes(type)))];
  if (unsupported.length)
    return { status: 'unsupported', reason: '当前原生法向扫掠支持直线和圆弧。拟合轮廓含 ' + unsupported.join('、') };

  const dir = path.join(destination, 'shared', record.id);
  fs.mkdirSync(dir, { recursive: true });
  const scratch = fs.mkdtempSync(path.join(dir, '.export-'));
  const nativeInput = path.join(scratch, 'input.step');
  const analyticFile = path.join(scratch, 'curves.json');
  try {
    // OCCT's STEP reader requires an ASCII working filename on Windows.
    fs.copyFileSync(source, nativeInput);
    if (sha(nativeInput) !== record.inputSha256) throw new Error('截面工作副本与输入 SHA 不符');
    await runSharedNative(exporter, [nativeInput, analyticFile, ...extras]);
    const sample = { id: record.id, title: record.name, source: record.input, ...read(analyticFile) };
    const result = await publishSharedCase(sample, [record], destination, { source, extras, sourceCopy: nativeInput });
    if (result.status !== 'complete') return result;
    if (sha(source) !== record.inputSha256 || sha(source.replace(/\.step$/i, '.json')) !== record.sourceMetadataSha256)
      throw new Error('处理期间原始截面输入或元数据改变');
    const dataUrl = `shared/${record.id}/view.json`;
    write(path.join(destination, dataUrl), { ...result, name: record.name, source: record.input,
      sourceSha256: record.inputSha256, sourceMetadataSha256: record.sourceMetadataSha256 });
    return { status: 'complete', materialArea: result.materialArea, sharedArea: result.sharedArea,
      wallCount: result.walls.length, cellCount: result.cells.length, junctionCount: result.junctions.length,
      brepUrl: result.brepUrl, evidenceUrl: result.evidenceUrl, dataUrl };
  } finally {
    for (const file of [nativeInput, analyticFile]) fs.rmSync(file, { force: true });
    fs.rmdirSync(scratch);
  }
}

async function runDemo() {
  checkSharedNativeTools();
  const inputs = read(path.join(here, 'cases.json')).cases;
  const contourRecords = read(path.join(contourWork, 'manifest.json')).records;
  const cases = [];
  for (const sample of inputs) {
    try {
      const result = await publishSharedCase(sample, contourRecords);
      console.log(sample.id, result.status === 'complete' ?
        `${result.walls.length} walls, ${result.cells.length} cells, shared ${result.sharedArea} mm²` : result.reason);
      cases.push(result);
    } catch (error) {
      console.error(sample.id, error.message);
      cases.push({ id: sample.id, status: 'unresolved', reason: error.message });
    }
  }
  const data = { schema: 1, work: 'SectionSharedCoverage', inputCount: inputs.length, cases };
  write(path.join(here, 'shared-cases.json'), data);
  write(path.join(output, 'shared-manifest.json'), { ...data,
    cases: cases.map(sample => ({ ...sample, cells: sample.cells?.map(({ displayWires: _, ...cell }) => cell) })) });
  if (cases.some(result => result.status !== 'complete')) process.exitCode = 1;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) await runDemo();
