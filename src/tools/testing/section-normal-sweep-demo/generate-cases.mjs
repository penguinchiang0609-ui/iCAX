import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../../../..');
const work = 'output/tests/section-regions-native-20261113';
const contourWork = 'TestData/Tube/SectionRegionWork-20261118/contours';
const exporter = path.join(root, work, 'bin/SectionNormalSweepExport.exe');
const cascade = 'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bind';
const read = name => JSON.parse(fs.readFileSync(name, 'utf8'));

const samples = [
  { id: '002', title: '002 · 圆角方管', contour: '007' },
  { id: '005', title: '005 · 异形圆角管', contour: '013' },
  { id: '012', title: '012 · 直角方管', contour: '021' },
  { id: 'flat-t', title: 'T 型截面 · 直角', fixture: 'flat-t' },
  { id: 'rounded-t', title: 'T 型截面 · 圆角', fixture: 'rounded-t' },
];

function endpoint(curve, end) {
  if (curve.kind === 'line') return end ? curve.b : curve.a;
  const angle = curve.a0 + (end ? curve.sweep : 0);
  return [curve.center[0] + curve.radius * Math.cos(angle), curve.center[1] + curve.radius * Math.sin(angle)];
}

if (!fs.existsSync(exporter)) {
  throw new Error('Build SectionNormalSweepExport.vcxproj (Debug x64) before generating cases.json.');
}
const scratch = fs.mkdtempSync(path.join(root, work, 'normal-sweep-export-'));
const exported = path.join(scratch, 'curves.json');
const cases = [];
try {
  for (const sample of samples) {
    let input, args = [];
    if (sample.contour) {
      const record = read(path.join(root, contourWork, sample.contour, 'run.json'));
      input = record.input;
      const metadata = read(path.join(root, input.replace(/\.step$/i, '.json')));
      args = [String(metadata.endX), String(Math.max(...metadata.wires.map(wire => wire.maxGapMm || 0))),
        path.join(root, contourWork, sample.contour, 'contour-source-edges.txt')];
    } else {
      input = path.posix.join(work, 'quasi-fixtures', sample.fixture + '.brep');
    }
    const processResult = spawnSync(exporter, [path.join(root, input), exported, ...args], {
      cwd: root,
      env: { ...process.env, PATH: [path.join(root, cascade), process.env.PATH].join(path.delimiter) },
      encoding: 'utf8',
      windowsHide: true,
    });
    if (processResult.status !== 0) {
      throw new Error(`${sample.id}: ${processResult.error || processResult.stderr || processResult.status}`);
    }
    const geometry = read(exported);
    let maxClosingGapMm = 0;
    let maxFitBoundMm = 0;
    for (const { curves } of geometry.rings) {
      for (let i = 0; i < curves.length; ++i) {
        const curve = curves[i];
        const end = endpoint(curve, true);
        const start = endpoint(curves[(i + 1) % curves.length], false);
        maxClosingGapMm = Math.max(maxClosingGapMm, Math.hypot(end[0] - start[0], end[1] - start[1]));
        maxFitBoundMm = Math.max(maxFitBoundMm, curve.fitBoundMm);
      }
    }
    cases.push({ id: sample.id, title: sample.title, source: input, ...geometry,
      maxClosingGapMm, maxFitBoundMm });
    console.log(`${sample.id}: ${geometry.rings.map(ring => ring.curves.length).join('+')} curves; closure ${maxClosingGapMm} mm`);
  }
  fs.writeFileSync(path.join(here, 'cases.json'), JSON.stringify({ cases }, null, 2) + '\n');
} finally {
  fs.rmSync(exported, { force: true });
  fs.rmdirSync(scratch);
}
