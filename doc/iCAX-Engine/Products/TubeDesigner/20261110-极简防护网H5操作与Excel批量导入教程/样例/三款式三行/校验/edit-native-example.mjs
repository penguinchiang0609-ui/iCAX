import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { FileBlob, SpreadsheetFile } from '@oai/artifact-tool';

const [phase, sourceArgument, targetArgument] = process.argv.slice(2);
assert(['inspect', 'edit', 'help'].includes(phase), 'Use inspect, edit or help phase');
assert(sourceArgument, 'Use the actual H5-exported native workbook');
const source = resolve(sourceArgument);
const directory = dirname(fileURLToPath(import.meta.url));
const sourceBytes = await fs.readFile(source);
const sourceSha256 = createHash('sha256').update(sourceBytes).digest('hex');
const workbook = await SpreadsheetFile.importXlsx(await FileBlob.load(source));
if (phase === 'help') {
  console.log(workbook.help('worksheet.visibility', { include: 'index,examples,notes', maxChars: 3000 }).ndjson);
  process.exit(0);
}
const sheet = workbook.worksheets.getItem('批量导入');
const metadata = workbook.worksheets.getItem('__iCAX_列定义').getRange('A1').values[0][0];
const contract = JSON.parse(metadata);
assert.equal(contract.schema, 'icax.tube-designer.batch-excel');
assert.equal(contract.templateId, 'minimal-protective-grille');
assert.equal(contract.templateVersion, '2.5.0');
const expectedKeys = ['__instanceName', '__instanceQuantity', 'layoutStyle', 'width', 'height', 'barCenterSpacing'];
assert.deepEqual(contract.columns.map(column => column.key).sort(), [...expectedKeys].sort());
assert.deepEqual(sheet.getRangeByIndexes(1, 0, 1, 6).values[0], contract.columns.map(column => column.title));
assert(sheet.getRange('A3:F5').values.every(row => row.every(value => value === null || value === '')),
  'The source must be the blank native template');

if (phase === 'inspect') {
  const overview = await workbook.inspect({ kind: 'workbook,sheet,table', maxChars: 4500,
    tableMaxRows: 5, tableMaxCols: 6, tableMaxCellChars: 60 });
  await fs.writeFile(resolve(directory, 'native-template-inspection.txt'), overview.ndjson);
  const preview = await workbook.render({ sheetName: '批量导入', range: 'A1:F5', scale: 1.5, format: 'png' });
  await fs.writeFile(resolve(directory, 'native-template-preview.png'), new Uint8Array(await preview.arrayBuffer()));
  await fs.writeFile(resolve(directory, 'native-template-contract.json'), JSON.stringify({ source, sourceSha256, contract }, null, 2));
  console.log(JSON.stringify({ phase, source, sourceSha256, columns: contract.columns.map(column => column.title),
    preview: resolve(directory, 'native-template-preview.png') }));
} else {
  assert(targetArgument, 'Choose a separate filled-example output');
  const target = resolve(targetArgument);
  assert.notEqual(target.toLowerCase(), source.toLowerCase(), 'Preserve original native template');
  await assert.rejects(fs.access(target), 'Do not overwrite an earlier example');
  const rows = [
    { __instanceName: 'A01-平面', __instanceQuantity: 1, layoutStyle: '平面款', width: 568, height: 1250, barCenterSpacing: 115 },
    { __instanceName: 'A02-五面', __instanceQuantity: 2, layoutStyle: '五面飘网', width: 800, height: 1400, barCenterSpacing: 115 },
    { __instanceName: 'A03-阳台', __instanceQuantity: 3, layoutStyle: '阳台上下收口', width: 1000, height: 1200, barCenterSpacing: 100 },
  ];
  sheet.getRangeByIndexes(2, 0, 3, 6).values = rows.map(row => contract.columns.map(column => row[column.key]));
  workbook.recalculate();
  const values = sheet.getRangeByIndexes(2, 0, 3, 6).values;
  assert.deepEqual(values, rows.map(row => contract.columns.map(column => row[column.key])));
  assert.equal(workbook.worksheets.getItem('__iCAX_列定义').getRange('A1').values[0][0], metadata);
  const view = await workbook.inspect({ kind: 'table', range: "'批量导入'!A1:F5", include: 'values,formulas',
    tableMaxRows: 5, tableMaxCols: 6, maxChars: 4000 });
  const errors = await workbook.inspect({ kind: 'match', searchTerm: '#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!|#SPILL!|#CALC!',
    options: { useRegex: true, maxResults: 20 }, summary: 'Final sample formula-error scan' });
  await fs.writeFile(resolve(directory, 'sample-inspection.txt'), view.ndjson + '\n' + errors.ndjson);
  const preview = await workbook.render({ sheetName: '批量导入', range: 'A1:F5', scale: 1.5, format: 'png' });
  const previewPath = resolve(dirname(target), '极简防护网_三款式表格预览.png');
  await fs.writeFile(previewPath, new Uint8Array(await preview.arrayBuffer()));
  const output = await SpreadsheetFile.exportXlsx(workbook);
  await output.save(target);
  assert.equal(createHash('sha256').update(await fs.readFile(source)).digest('hex'), sourceSha256);
  const targetBytes = await fs.readFile(target);
  const tableDataPath = resolve(dirname(target), '极简防护网_三款式表格数据.json');
  await fs.writeFile(tableDataPath, JSON.stringify({ templateId: contract.templateId, templateVersion: contract.templateVersion,
    sheet: '批量导入', headerRow: 2, firstDataRow: 3, columns: contract.columns.map(column => ({ key: column.key, title: column.title })),
    rows, values, expectedProductInstanceCount: 3, expectedProductionQuantity: 6 }, null, 2));
  const report = { phase, source, sourceSha256, target,
    targetSha256: createHash('sha256').update(targetBytes).digest('hex'), rows, rowCount: 3,
    expectedProductInstanceCount: 3, expectedProductionQuantity: 6,
    templateId: contract.templateId, templateVersion: contract.templateVersion,
    dataAuthoring: '@oai/artifact-tool', nativeImportCreationVerified: false,
    pending: 'ZIP metadata/validation preservation and root actual H5 import',
    preview: previewPath, tableData: tableDataPath };
  await fs.writeFile(resolve(directory, 'sample-edit-report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report));
}

