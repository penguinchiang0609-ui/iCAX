// Actual native preview anchors, then the production WebGL annotation layer.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { createInterface } from 'node:readline';
import { delimiter, resolve } from 'node:path';
import {
  browserAssetRoot, browserReportDirectory, importBrowserAsset, readBrowserAsset, serveBrowserAsset,
} from './browserPackageRuntime.mjs';

const runtime = resolve(process.env.ICAX_NATIVE_RUNTIME_ROOT || browserAssetRoot);
assert(process.env.ICAX_NATIVE_SCOPE_BRIDGE, 'Set ICAX_NATIVE_SCOPE_BRIDGE to the scope-aware native bridge');
const bridge = resolve(process.env.ICAX_NATIVE_SCOPE_BRIDGE);
const output = browserReportDirectory('security-hardware-removed-20261006');
const descriptor = JSON.parse(readBrowserAsset('apps/tube-designer/templates/product/single_face_security_window/template.json'));
descriptor.display = JSON.parse(readBrowserAsset('apps/tube-designer/templates/product/single_face_security_window/display.json'));
const defaults = Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue]));
assert(!JSON.stringify(descriptor).includes('doorHardwareClearance'), 'Removed field must leave the descriptor and display configuration');
const env = { ...process.env };
const path = Object.entries(env).find(([key]) => key.toLowerCase() === 'path')?.[1] || '';
for (const key of Object.keys(env)) if (key.toLowerCase() === 'path') delete env[key];
env.Path = runtime + delimiter + path;
const native = spawn(bridge, [], { cwd: runtime, env, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
const waiting = new Map(), calls = [], stderr = [];
let sequence = 0, browser;
const fail = error => {
  for (const job of waiting.values()) { clearTimeout(job.timer); job.reject(error); }
  waiting.clear();
};
native.on('error', fail);
native.on('exit', code => fail(new Error(`Native host exited ${code}: ${stderr.join('')}`)));
native.stderr.on('data', bytes => stderr.push(bytes.toString()));
createInterface({ input: native.stdout }).on('line', line => {
  let response;
  try { response = JSON.parse(line); } catch { return fail(new Error(`Invalid native output: ${line}`)); }
  const job = waiting.get(response.id);
  if (!job) return;
  waiting.delete(response.id); clearTimeout(job.timer);
  calls.push({ request: job.request, response, milliseconds: Date.now() - job.started });
  response.ok ? job.resolve(response.result) : job.reject(new Error(response.error));
});
function invoke(scope, method, payload = {}) {
  const request = { id: ++sequence, scope, method, payload };
  return new Promise((resolveRequest, reject) => {
    const timer = setTimeout(() => { waiting.delete(request.id); reject(new Error(`Native timeout: ${method}`)); }, 120000);
    waiting.set(request.id, { request, resolve: resolveRequest, reject, timer, started: Date.now() });
    native.stdin.write(JSON.stringify(request) + '\n');
  });
}
const cases = [
  { id: 'single', parameters: {} },
  { id: 'single-moved', parameters: { width: 1600, doorLeft: 300, doorBottom: 450 } },
  { id: 'single-width', parameters: { doorClearWidth: 700 } },
  { id: 'single-leaf-depth', parameters: { doorLeafFrameDepth: 24 } },
  { id: 'two-front', parameters: { faceType: 'two' } },
  { id: 'two-side', parameters: { faceType: 'two', sideWidth: 1600, accessDoorFace2: 'side' } },
  { id: 'three-left', parameters: { faceType: 'three', leftWidth: 1600, accessDoorFace3: 'left' } },
  { id: 'three-right', parameters: { faceType: 'three', rightWidth: 1600, accessDoorFace3: 'right' } },
  { id: 'five-front', parameters: { faceType: 'five' } },
  { id: 'five-bottom', parameters: { faceType: 'five', depth: 1600, accessDoorFace5: 'bottom', doorVOffset: 150 } },
  ...['single', 'two', 'three', 'five'].map(faceType => ({
    id: `${faceType}-disabled`, parameters: { faceType, accessDoorEnabled: false },
  })),
];
const report = { status: 'running', nativeRuntime: runtime, bridge,
  nativeBusinessResponsesMocked: false, standaloneCefEndToEnd: false, cases: [], browserCases: [] };
const previews = new Map();
try {
  const modules = (await invoke('inspection', 'GetRuntimeModules')).modules;
  report.modules = Object.fromEntries(Object.entries(modules).map(([name, path]) => {
    assert.equal(resolve(path).toLowerCase(), resolve(runtime, name).toLowerCase());
    return [name, { path, sha256: createHash('sha256').update(readFileSync(path)).digest('hex') }];
  }));
  for (const test of cases) {
    const input = structuredClone(test.parameters);
    const preview = await invoke('scene', 'GenerateProductTemplatePreview', { templateId: descriptor.id, parameters: input });
    assert.deepEqual(input, test.parameters, 'Caller inputs must stay unchanged');
    const normalized = Object.fromEntries(descriptor.parameters.map(field => [field.key,
      field.readOnly || input[field.key] == null ? field.defaultValue : input[field.key]]));
    assert.deepEqual(preview.parameters, normalized, 'Native return must match normalized host inputs');
    assert(!Object.hasOwn(preview.parameters, 'doorHardwareClearance'));
    assert(!preview.specificationAnnotations.some(a => a.parameter === 'doorHardwareClearance'));
    const anchors = preview.specificationAnnotations.filter(a => a.parameter === 'doorClearWidth');
    if (normalized.accessDoorEnabled) {
      assert.equal(anchors.length, 1, 'Opening width retains its geometry-owned editable annotation');
      const anchor = anchors[0];
      assert.match(anchor.id, /^security-window\.escape/);
      assert.equal(anchor.generatedValue, normalized.doorClearWidth);
      const frames = preview.items.filter(item => item.key.startsWith('access_door.fixed_frame.'));
      assert(frames.length, 'Native preview must include the actual escape-window frame');
      const direction = anchor.end.map((value, i) => Math.abs(value - anchor.start[i]));
      const axis = direction.indexOf(Math.max(...direction));
      const actualOutsideWidth = Math.max(...frames.map(item => item.bounds.max[axis]))
        - Math.min(...frames.map(item => item.bounds.min[axis]));
      const expectedOutsideWidth = normalized.doorClearWidth + normalized.doorLeafFrameDepth + 2 * normalized.doorFrameWidth;
      assert(Math.abs(actualOutsideWidth - expectedOutsideWidth) < 1e-5,
        `Actual fixed frame has extra width: ${actualOutsideWidth} versus ${expectedOutsideWidth}`);
      report.cases.push({ id: test.id, actualOutsideWidth, expectedOutsideWidth, anchor });
    } else {
      assert(!preview.items.some(item => item.key.startsWith('access_door.')), 'Disabled opening must have no actual escape-window geometry');
      report.cases.push({ id: test.id, openingItemCount: 0 });
    }
    previews.set(test.id, preview);
  }
  const anchorFor = id => previews.get(id).specificationAnnotations.find(a => a.parameter === 'doorClearWidth');
  assert.deepEqual(anchorFor('single-moved').start.map((value, i) => value - anchorFor('single').start[i]), [140, 0, 100],
    'Moving the actual escape window must move its width annotation, independently of overall width');

  // Export every exposed column through the real DLL, inspect the workbook's
  // hidden contract, then read a filled row through the real DLL again.
  const presentation = (await invoke('product', 'GetTemplateDescriptor', { templateId: descriptor.id })).template;
  const { batchExcelColumnsFromTemplate } = await importBrowserAsset('apps/tube-designer/webpage/designerActions.mjs');
  const columns = batchExcelColumnsFromTemplate(presentation);
  assert(!columns.some(column => column.key === 'doorHardwareClearance'));
  const templatePath = resolve(output, `native-template-${Date.now()}.xlsx`);
  const filledPath = resolve(output, 'filled.xlsx');
  await invoke('product', 'ExportBatchExcelTemplate', { templateId: descriptor.id, targetPath: templatePath, columns });
  await assert.rejects(invoke('product', 'ExportBatchExcelTemplate', { templateId: descriptor.id,
    targetPath: resolve(output, 'invalid-removed-field.xlsx'),
    columns: [{ key: 'doorHardwareClearance', title: '五金占宽预留', defaultValue: '10' }] }), /doorHardwareClearance/);
  assert(process.env.ICAX_NATIVE_TEST_PYTHON, 'Set ICAX_NATIVE_TEST_PYTHON for workbook contract inspection');
  const inspection = spawnSync(process.env.ICAX_NATIVE_TEST_PYTHON, ['-c', `
import json, sys, zipfile, xml.etree.ElementTree as ET
from xml.sax.saxutils import escape
source, target = sys.argv[1:]
ns = {'s':'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
with zipfile.ZipFile(source) as book:
    texts = [book.read(item).decode('utf-8') for item in book.namelist() if item.endswith('.xml')]
    assert all('doorHardwareClearance' not in text and '五金占宽预留' not in text for text in texts)
    metadata = ET.fromstring(book.read('xl/worksheets/sheet2.xml'))
    contract = json.loads(''.join(node.text or '' for node in metadata.findall('.//s:c[@r="A1"]//s:t', ns)))
    values = {'__instanceName':'参数移除导入测试','__instanceQuantity':'1','width':'1200','height':'1800'}
    cells = []
    for index, column in enumerate(contract['columns'], 1):
        if column['key'] not in values: continue
        letters, number = '', index
        while number:
            number, remainder = divmod(number - 1, 26)
            letters = chr(65 + remainder) + letters
        cells.append('<c r="'+letters+'3" t="inlineStr"><is><t>'+escape(values[column['key']])+'</t></is></c>')
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as result:
        for item in book.infolist():
            data = book.read(item.filename)
            if item.filename == 'xl/worksheets/sheet1.xml':
                data = data.decode('utf-8').replace('</sheetData>', '<row r="3">'+''.join(cells)+'</row></sheetData>').encode('utf-8')
            result.writestr(item, data)
    print(json.dumps({'columnCount':len(contract['columns']),'removedFieldAbsent':True}))
`, templatePath, filledPath], { encoding: 'utf8', windowsHide: true });
  assert.equal(inspection.status, 0, inspection.stderr || inspection.error?.message);
  const imported = await invoke('product', 'ReadBatchExcelImport', { sourcePath: filledPath });
  assert.equal(imported.templateId, descriptor.id);
  assert.equal(imported.rows.length, 1);
  assert.equal(imported.rows[0].parameters.width, 1200);
  assert.equal(imported.rows[0].parameters.height, 1800);
  assert(!Object.hasOwn(imported.rows[0].parameters, 'doorHardwareClearance'));
  report.excel = { ...JSON.parse(inspection.stdout), templatePath, filledPath, rows: imported.rows.length,
    templateId: imported.templateId, removedFieldExportRejected: true };
  const generated = await invoke('scene', 'GeneratePreview', {
    templateId: imported.templateId, ...imported.rows[0].parameters,
    instanceName: imported.rows[0].instanceName, instanceQuantity: imported.rows[0].instanceQuantity,
  });
  assert(!Object.hasOwn(generated.tubeDesigner.product.parameters, 'doorHardwareClearance'));
  const disassembled = await invoke('scene', 'Disassemble');
  assert.deepEqual(disassembled.tubeDesigner.product.parameters, generated.tubeDesigner.product.parameters);
  const topPart = disassembled.tubeDesigner.parts.find(part =>
    part.properties['manufacturing.sourceMembers']?.some(member => member.itemKey === 'access_door.fixed_frame.top.0001'));
  assert(topPart, 'Native disassembly must retain the actual fixed-frame top stock');
  const measured = await invoke('scene', 'MeasurePartGeometry', {
    partEntityId: topPart.entityId, resourceVersion: topPart.manufacturingGeometryResourceVersion,
  });
  assert.equal(measured.source, 'final-brep');
  assert(measured.available);
  assert(Math.abs(measured.length - 870) < 1e-5, `Final frame stock must be 870 mm, got ${measured.length}`);
  report.manufacturing = { parts: disassembled.tubeDesigner.parts.length,
    sourceMember: 'access_door.fixed_frame.top.0001', finalBRepLength: measured.length };

  const { chromium } = await import('playwright');
  browser = await chromium.launch({ channel: process.env.ICAX_BROWSER_CHANNEL || 'chrome', headless: true });
  for (const id of ['single', 'single-moved', 'two-side', 'five-bottom']) {
    const page = await browser.newPage({ viewport: { width: 1100, height: 760 } });
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.route('http://opening-clearance.test/**', serveBrowserAsset);
    await page.goto('http://opening-clearance.test/');
    const result = await page.evaluate(async ({ descriptor, preview }) => {
      const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
      const { resolveProductSpecificationAnnotations, resolveProductSpecificationAnnotationTree } = await import('/src/apps/tube-designer/webpage/productParameterDiagram.mjs');
      const { batchExcelColumnsFromTemplate } = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
      if (batchExcelColumnsFromTemplate(descriptor).some(column => column.key === 'doorHardwareClearance'))
        throw new Error('Removed field remains in Excel column configuration');
      document.body.style.margin = '0';
      document.body.innerHTML = '<div class="cam-viewport" style="width:1100px;height:760px;position:relative"><div id="host" style="width:100%;height:100%"></div><div class="cam-viewcube" style="position:absolute;right:10px;top:10px;width:100px;height:100px;background:#c88e18"></div></div>';
      const viewport = createThreeViewport({ continuousRender: false });
      viewport.mount(document.querySelector('#host'));
      const designer = { templates: [descriptor], product: { entityId: 'native-opening', templateId: descriptor.id,
        parameters: preview.parameters, modelOutdated: false }, specificationAnnotations: preview.specificationAnnotations };
      const annotations = resolveProductSpecificationAnnotations(designer, {});
      if (annotations.some(a => a.parameter === 'doorHardwareClearance')) throw new Error('Removed field remains in scene annotations');
      const opening = annotations.find(a => a.parameter === 'doorClearWidth');
      if (opening.label !== `逃生窗净宽 ${preview.parameters.doorClearWidth.toLocaleString('en-US')} mm` || opening.groupKey !== 'door_size')
        throw new Error('Incorrect label or escape-window grouping');
      const escape = resolveProductSpecificationAnnotationTree(designer, {}).children.find(group => group.key === 'section:escape_window');
      if (!escape?.children.some(group => group.key === opening.groupKey)) throw new Error('Opening callout is outside escape-window controls');
      const bounds = preview.items.map(item => item.bounds);
      const min = [0, 1, 2].map(i => Math.min(...bounds.map(b => b.min[i])));
      const max = [0, 1, 2].map(i => Math.max(...bounds.map(b => b.max[i])));
      const center = min.map((v, i) => (v + max[i]) / 2);
      const span = Math.max(...max.map((v, i) => v - min[i]));
      const checks = [];
      for (const [x, y, z, distance] of [[0, -1, .2, 1.5], [.7, -1, .4, 1.6], [0, -1, .2, 2.2]]) {
        viewport.camera.position.set(center[0] + x * span * distance, center[1] + y * span * distance, center[2] + z * span * distance);
        viewport.camera.lookAt(...center); viewport.camera.updateProjectionMatrix();
        viewport.setSpecificationAnnotations(annotations);
        viewport.renderer.render(viewport.scene, viewport.camera);
        const label = viewport.specificationLabels.find(label => label.element.querySelector('[data-tube-designer-parameter-key="doorClearWidth"]'));
        if (!label || label.element.hidden) throw new Error('Opening callout disappeared after view change');
        const projected = label.worldPoint.clone().project(viewport.camera);
        const canvas = viewport.renderer.domElement.getBoundingClientRect(), box = label.element.getBoundingClientRect();
        const distanceFromFrame = Math.hypot((box.left + box.right) / 2 - canvas.left - (projected.x * .5 + .5) * canvas.width,
          (box.top + box.bottom) / 2 - canvas.top - (-projected.y * .5 + .5) * canvas.height);
        // Production collision layout may move a label away from its initial
        // projected midpoint; retain the existing local caption bound.
        if (distanceFromFrame > 150) throw new Error(`Width label moved away from the escape window: ${distanceFromFrame}`);
        const cube = document.querySelector('.cam-viewcube').getBoundingClientRect();
        if (box.left < cube.right && box.right > cube.left && box.top < cube.bottom && box.bottom > cube.top)
          throw new Error('Opening caption overlaps orientation control');
        checks.push({ distanceFromFrame, label: label.element.textContent });
      }
      const canvas = viewport.renderer.domElement;
      viewport.setSpecificationAnnotations(annotations.map(a => a.parameter === opening.parameter ? { ...a, editing: true } : a));
      if (!viewport.focusSpecificationAnnotationEditor(opening.parameter)) throw new Error('Scene width editor cannot focus');
      const input = document.querySelector('.icax-three-specification-input');
      if (document.activeElement !== input || Number(input.value) !== preview.parameters.doorClearWidth)
        throw new Error('Opening width is not editable through its annotation');
      if (canvas !== viewport.renderer.domElement) throw new Error('Annotation editing replaced the canvas');
      return { label: opening.label, group: opening.groupKey, checks, editorFocused: true };
    }, { descriptor, preview: previews.get(id) });
    assert.deepEqual(errors, []);
    report.browserCases.push({ id, ...result });
    await page.screenshot({ path: resolve(output, `${id}.png`) });
    await page.close();
  }
  report.status = 'passed';
  console.log(`PASS ${cases.length} native opening dimensions and 4 production WebGL cases: removed allowance, actual frame width and remaining width editing`);
} catch (error) {
  report.status = 'failed'; report.failure = error.message;
  throw error;
} finally {
  writeFileSync(resolve(output, 'report.json'), JSON.stringify(report, null, 2));
  writeFileSync(resolve(output, 'native-calls.json'), JSON.stringify(calls, null, 2));
  await browser?.close();
  native.stdin.end(); native.kill();
}
