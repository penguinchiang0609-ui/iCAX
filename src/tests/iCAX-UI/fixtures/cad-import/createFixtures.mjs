// Deterministic drawing sources; converter is used only to prepare test data.
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';

const target = dirname(fileURLToPath(import.meta.url));
mkdirSync(target, { recursive: true });
const values = [];
const pair = (code, value) => values.push(String(code), String(value));
const record = (kind, pairs) => { pair(0, kind); for (const [code, value] of pairs) pair(code, value); };
record('SECTION', [[2, 'HEADER']]);
pair(9, '$ACADVER'); pair(1, 'AC1027');
pair(9, '$INSUNITS'); pair(70, 4);
pair(9, '$DWGCODEPAGE'); pair(3, 'UTF-8');
record('ENDSEC', []);
record('SECTION', [[2, 'TABLES']]);
record('TABLE', [[2, 'LAYER'], [70, 2]]);
for (const layer of ['0', 'CUT']) record('LAYER', [[100, 'AcDbSymbolTableRecord'], [100, 'AcDbLayerTableRecord'], [2, layer], [70, 0], [62, 7], [6, 'CONTINUOUS']]);
record('ENDTAB', []); record('ENDSEC', []);
record('SECTION', [[2, 'BLOCKS']]);
record('BLOCK', [[8, '0'], [2, 'SHAPES'], [70, 0], [10, 0], [20, 0], [30, 0], [3, 'SHAPES'], [1, '']]);
record('LWPOLYLINE', [[8, '0'], [90, 4], [70, 1], [10, 0], [20, 0], [10, 8], [20, 0], [10, 8], [20, 12], [10, 0], [20, 12]]);
record('CIRCLE', [[8, '0'], [10, 16], [20, 0], [30, 0], [40, 3]]);
record('ELLIPSE', [[8, '0'], [10, 28], [20, 0], [30, 0], [11, 0], [21, 5], [31, 0], [40, .6], [41, 0], [42, Math.PI * 2]]);
record('SPLINE', [[8, '0'], [70, 8], [71, 3], [72, 8], [73, 4], [74, 0], ...[0, 0, 0, 0, 1, 1, 1, 1].map(x => [40, x]), ...[[0, 24], [4, 20], [8, 29], [12, 24]].flatMap(([x, y]) => [[10, x], [20, y], [30, 0]])]);
record('ENDBLK', [[8, '0']]); record('ENDSEC', []);
record('SECTION', [[2, 'ENTITIES']]);
record('INSERT', [[8, 'CUT'], [2, 'SHAPES'], [10, 24], [20, 85], [30, 0], [41, 1], [42, 1], [43, 1], [50, 0]]);
record('TEXT', [[8, '0'], [10, 10], [20, 20], [30, 0], [40, 2], [1, 'Imported cutting contours'], [7, 'STANDARD']]);
record('ENDSEC', []); record('EOF', []);
const ascii = resolve(target, 'block-contours.dxf');
writeFileSync(ascii, values.join('\r\n') + '\r\n');

// Binary DXF 2000+: little endian group code and typed values. This is built
// independently of the production binary reader, from the same DXF records.
const chunks = [Buffer.from('AutoCAD Binary DXF\r\n\x1a\0', 'binary')];
for (let i = 0; i < values.length; i += 2) {
  const code = Number(values[i]), value = values[i + 1];
  const head = Buffer.alloc(2); head.writeUInt16LE(code); chunks.push(head);
  if ((code >= 10 && code <= 59) || (code >= 110 && code <= 149) || (code >= 210 && code <= 239)) {
    const item = Buffer.alloc(8); item.writeDoubleLE(Number(value)); chunks.push(item);
  } else if (code >= 60 && code <= 79) {
    const item = Buffer.alloc(2); item.writeInt16LE(Number(value)); chunks.push(item);
  } else if (code >= 90 && code <= 99) {
    const item = Buffer.alloc(4); item.writeInt32LE(Number(value)); chunks.push(item);
  } else chunks.push(Buffer.from(value + '\0', 'utf8'));
}
writeFileSync(resolve(target, 'block-contours-binary.dxf'), Buffer.concat(chunks));

const writer = process.env.ICAX_DXF2DWG;
if (writer) {
  const dwg = resolve(target, 'block-contours.dwg');
  // A DWG writer requires the complete ownership/handle tables. Author those
  // independently with ezdxf rather than supplying our minimal parser fixture.
  const authoringRoot = fileURLToPath(new URL('../../../../../output/tests/sketch-cad-import/authoring/', import.meta.url));
  mkdirSync(authoringRoot, { recursive: true });
  const prepared = spawnSync(process.env.ICAX_FIXTURE_PYTHON || 'python', [resolve(target, 'createDwgSource.py'), authoringRoot, process.env.ICAX_FIXTURE_PYTHON_PACKAGES || ''], { windowsHide: true, encoding: 'utf8' });
  if (prepared.error || prepared.status !== 0) throw new Error(`DWG authoring source failed: ${prepared.error || prepared.stderr}`);
  const result = spawnSync(writer, ['--as', 'r2000', '-y', '-o', dwg, resolve(authoringRoot, 'dwg-source.dxf')], { windowsHide: true, encoding: 'utf8' });
  if (result.error || result.status !== 0) throw new Error(`DWG fixture preparation failed: ${result.error || result.stderr || result.stdout}`);
  if (readFileSync(dwg).subarray(0, 6).toString('ascii') !== 'AC1015') throw new Error('Fixture was not written as a real DWG 2000 file');
}
for (const file of ['block-contours.dxf', 'block-contours-binary.dxf', ...(writer ? ['block-contours.dwg'] : [])]) {
  console.log(file + ' SHA256 ' + createHash('sha256').update(readFileSync(resolve(target, file))).digest('hex'));
}
const workflowRoot = fileURLToPath(new URL('../../../../../output/tests/sketch-cad-import/', import.meta.url));
mkdirSync(workflowRoot, { recursive: true });
writeFileSync(resolve(workflowRoot, 'fixtures.json'), JSON.stringify({ cases: ['block-contours.dxf', 'block-contours-binary.dxf', 'block-contours.dwg'].map(file => ({
  id: file, sourcePath: resolve(target, file), expectedEntityCount: 4,
  expectedCoordinates: { circle: { cx: 40, cy: 85, radius: 3 }, ellipse: { cx: 52, cy: 85 } },
})) }, null, 2));
