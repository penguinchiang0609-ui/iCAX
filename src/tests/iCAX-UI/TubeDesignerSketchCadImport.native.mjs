// Real native file reader / isolated DWG process -> production DXF parser.
import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync, copyFileSync, existsSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
import { mapSketchCadDocument } from '../../apps/tube-designer/webpage/sketchCadExchange.mjs';
import { curvePoint, editableSegments } from '../../apps/tube-designer/webpage/sketchGeometry.mjs';
import { repositoryRoot, startSideSketchNativeBridge } from './fixtures/sideSketchNativeBridge.mjs';

const output = resolve(process.env.ICAX_ARTIFACT_DIR || resolve(repositoryRoot, 'output/tests/sketch-cad-import/native'));
const fixtures = resolve(repositoryRoot, 'src/tests/iCAX-UI/fixtures/cad-import');
const serviceOnly = process.env.ICAX_CAD_IMPORT_SCOPE === 'service';
mkdirSync(output, { recursive: true });
const bridge = await startSideSketchNativeBridge(output);
const cases = [];
const read = async sourcePath => {
  const result = serviceOnly
    ? await bridge.rpc({ action: 'read-cad-document', payload: { sourcePath } })
    : await bridge.invoke('ReadSketchCad', { sourcePath }, 'product');
  assert.ok(result.document?.sections, 'Native service must return decoded neutral CAD data');
  assert.equal(result.contentBase64, undefined, 'Business response must not send an encoded file for the UI to load');
  const parsed = mapSketchCadDocument(result.document);
  return { result, parsed };
};
const describe = entities => entities.map(entity => ({ kind: entity.kind, closed: Boolean(entity.closed),
  segments: editableSegments(entity).map(segment => ({ kind: segment.kind, samples: [0, .25, .5, .75, 1].map(t => curvePoint(segment, t)) })) }));
try {
  let baseline, baselineDocument;
  for (const filename of ['block-contours.dxf', 'block-contours-binary.dxf', 'block-contours.dwg']) {
    const sourcePath = resolve(fixtures, filename);
    const { result, parsed } = await read(sourcePath);
    if (!serviceOnly) {
      const independent = await bridge.rpc({ action: 'read-cad-document', payload: { sourcePath } });
      assert.deepEqual(independent, result, 'Registered neutral service works without a product or scene');
    }
    assert.equal(parsed.entities.length, 4, filename + ': all four block contours must import');
    assert.equal(result.format, filename.endsWith('.dwg') ? 'dwg' : 'dxf');
    assert.equal(parsed.unitScaleToMillimeter, 1);
    const actual = describe(parsed.entities);
    if (!baseline) { baseline = actual; baselineDocument = result.document; }
    else {
      assert.deepEqual(actual.map(item => [item.kind, item.closed, item.segments.map(segment => segment.kind)]), baseline.map(item => [item.kind, item.closed, item.segments.map(segment => segment.kind)]));
      for (let i = 0; i < actual.length; i++) for (let j = 0; j < actual[i].segments.length; j++) for (let k = 0; k < 5; k++) {
        const a = actual[i].segments[j].samples[k], b = baseline[i].segments[j].samples[k];
        assert.ok(Math.hypot(a[0] - b[0], a[1] - b[1]) < 1e-6, filename + ': coordinates agree with independent DXF source');
      }
    }
    cases.push({ id: filename, format: result.format, entities: actual, warnings: parsed.warnings, converterVersion: result.converterVersion });
  }
  const commentedSource = resolve(output, 'section-comments.dxf');
  const commentedDxf = readFileSync(resolve(fixtures, 'block-contours.dxf'), 'utf8')
    .replace(/(0\r?\nSECTION\r?\n2\r?\n[^\r\n]+\r?\n)/g, (_, prefix) => prefix + '999\nsection comment\n')
    .replace(/(9\r?\n\$(?:ACADVER|DWGCODEPAGE|INSUNITS)\r?\n)/g, (_, prefix) => prefix + '999\nheader comment\n')
    .replace(/(0\r?\nLWPOLYLINE\r?\n)/, (_, prefix) => prefix + '999\nentity comment\n');
  writeFileSync(commentedSource, '999\nfile comment\n' + commentedDxf + '999\ntrailing comment\n');
  const commented = await read(commentedSource);
  assert.deepEqual(commented.result.document, baselineDocument, 'Comments anywhere in the document must not alter neutral data');
  assert.deepEqual(describe(commented.parsed.entities), baseline, 'Commented DXF retains the exact cutting contours');
  cases.push({ id: 'comments before header and entity records', passed: true });
  const actualUnicodeSource = resolve(output, "中文 图纸 sample's.DWG");
  copyFileSync(resolve(fixtures, 'block-contours.dwg'), actualUnicodeSource);
  assert.equal((await read(actualUnicodeSource)).parsed.entities.length, 4);
  cases.push({ id: 'Chinese and spaced path with apostrophe', passed: true });

  const metadata = JSON.parse(readFileSync(resolve(fixtures, 'libredwg-samples.json'), 'utf8').replace(/^\uFEFF/, ''));
  const externalRoot = resolve(process.env.ICAX_DWG_SAMPLE_DIR || resolve(repositoryRoot, 'output/tests/sketch-cad-import/external'));
  for (const sample of metadata) {
    const sourcePath = resolve(externalRoot, sample.id + '.dwg');
    assert.ok(existsSync(sourcePath), 'Run prepare_sketch_cad_import_samples.ps1: ' + sourcePath);
    assert.equal(createHash('sha256').update(readFileSync(sourcePath)).digest('hex'), sample.sha256);
    const { result, parsed } = await read(sourcePath);
    assert.equal(parsed.entities.length, 1, sample.id + ': one model-space curve');
    assert.equal(parsed.unitScaleToMillimeter, 25.4, sample.id + ': inch drawing converted to mm');
    const segments = parsed.entities.flatMap(editableSegments);
    const kinds = new Set([...parsed.entities.map(item => item.kind), ...segments.map(item => item.kind)]);
    assert.ok(kinds.has(sample.expectedKind) || (sample.expectedKind === 'ellipse' && kinds.has('ellipseArc')), sample.id + ': original curve type must survive');
    assert.ok(segments.every(segment => [0, .5, 1].map(t => curvePoint(segment, t)).flat().every(Number.isFinite)));
    cases.push({ id: sample.id, format: result.format, fileVersion: sample.sourceFormatVersion, entities: parsed.entities.length, kinds: [...kinds] });
  }
  for (const [filename, content] of [['damaged.dwg', Buffer.from('AC1032not-a-drawing')], ['renamed.dwg', readFileSync(resolve(fixtures, 'block-contours.dxf'))], ['not-cad.txt', Buffer.from('0\nEOF\n')]]) {
    const sourcePath = resolve(output, filename); writeFileSync(sourcePath, content);
    await assert.rejects(() => read(sourcePath), undefined, filename + ' must fail explicitly');
    cases.push({ id: filename, rejected: true });
  }
  const converterFailure = resolve(output, 'damaged-converter.dwg');
  writeFileSync(converterFailure, Buffer.concat([Buffer.from('AC1032'), Buffer.alloc(256, 0xff)]));
  await assert.rejects(() => read(converterFailure), /DWG图纸损坏或包含无法读取的数据/, 'A valid signature must reach the converter and fail safely');
  assert.equal((await read(resolve(fixtures, 'block-contours.dwg'))).parsed.entities.length, 4, 'A converter failure must leave the host able to import again');
  cases.push({ id: 'isolated converter failure and successful next import', rejected: true, recovered: true });
  await assert.rejects(() => read('relative.dxf'));
  await assert.rejects(() => read(resolve(output, 'missing.dwg')));
  cases.push({ id: 'relative and missing paths', rejected: true });
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: true, dllHash: bridge.dllHash, importerHash: bridge.importerHash, cases,
    coverage: (serviceOnly ? 'Registered neutral service with no product/scene context; ' : 'Real product SDO file reading; ')
      + 'isolated DWG conversion, ASCII/binary DXF equivalence and upstream DWG 2000/2004/2007/2013/2018 curves; installed CEF/file selector outside scope.' }, null, 2));
  console.log(`CAD import native regression passed (${cases.length} scenarios).`);
} finally { await bridge.close(); }
