import assert from 'node:assert/strict';
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { resolve, sep } from 'node:path';
import { spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { minimalGrilleDiagramStocks } from '../../apps/tube-designer/webpage/minimalGrilleDiagram.mjs';

const sourceRoot = fileURLToPath(new URL('../../', import.meta.url));
const evidence = resolve(process.env.ICAX_GRILLE_VISUAL_EVIDENCE || 'output/tests/minimal-protective-grille-20261026/visual-update');
mkdirSync(evidence, { recursive: true });
const python = process.env.ICAX_PYTHON || 'C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe';
const oracle = spawnSync(python, ['-X', 'utf8', '-c', `
import json, sys
from pathlib import Path
from itertools import product
root=Path(sys.argv[1])
sys.path.insert(0,str(root/'iCAX-Engine/framework/TemplateRuntime/python'))
from icax_template_worker import _load_template
folder=root/'apps/tube-designer/templates/product/minimal_protective_grille'
descriptor=json.loads((folder/'template.json').read_text(encoding='utf-8'))
template=_load_template(str(folder/'template.py'),'grille-diagram-oracle')
defaults={f['key']:f['defaultValue'] for f in descriptor['parameters']}
cases=[]
changes=[dict(frameType=f,handleEnabled=e,handleSide=s,handleClosure=c,barLayoutMode=m)
 for f,e,s,c,m in product(('two_vertical','closed_frame','bottom_rail'),('yes','no'),
 ('left','right','center_double'),('closed','open_slot'),('maximum_gap','fixed_count'))]
changes += [dict(firstBarTopOffset=67,lastBarBottomOffset=73,maximumClearGap=70),
 dict(handleSide='center_double',handleCenterOffset=25),
 dict(handleBottom=420,handleHeight=210,handleWidth=190),
 dict(frameWidth=40,innerWidth=20,frameDepth=30,innerDepth=10,handleWidth=150)]
for change in changes:
 p={**defaults,**change};state=template.layout(p)
 cases.append(dict(values=p,stocks=[dict(key=m['key'],role=m['role'],width=m['profile'].width,
 start=[m['start'][0],m['start'][2]],end=[m['end'][0],m['end'][2]]) for m in state['members']]))
print(json.dumps(cases))
`, sourceRoot], { encoding: 'utf8', maxBuffer: 8 * 1024 * 1024 });
assert.equal(oracle.status, 0, oracle.stderr);
const cases = JSON.parse(oracle.stdout);
const descriptor = JSON.parse(readFileSync(resolve(sourceRoot, 'apps/tube-designer/templates/product/minimal_protective_grille/template.json')));
for (const test of cases) {
  const actual = minimalGrilleDiagramStocks(descriptor.extensions.productDiagram, test.values).stocks;
  assert.equal(actual.length, test.stocks.length);
  for (let i = 0; i < actual.length; i++) {
    assert.equal(actual[i].key, test.stocks[i].key);
    assert.equal(actual[i].role, test.stocks[i].role);
    for (const field of ['start', 'end']) for (let axis = 0; axis < 2; axis++)
      assert(Math.abs(actual[i][field][axis] - test.stocks[i][field][axis]) < 1e-7, `${actual[i].key} ${field}`);
    assert.equal(actual[i].width, test.stocks[i].width);
  }
}

const require = createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const { chromium } = require('playwright');
const browser = await chromium.launch({ headless: true, channel: 'msedge' });
try {
  const page = await browser.newPage({ viewport: { width: 1300, height: 800 } });
  page.on('requestfailed', request => console.error('Module request failed:', request.url(), request.failure()?.errorText));
  await page.route('http://grille.test/**', route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><body></body>' });
    const target = resolve(sourceRoot, path.slice('/src/'.length));
    if (!path.startsWith('/src/') || !target.startsWith(sourceRoot.replace(/[\\/]$/, '') + sep) || !/\.m?js$/.test(target)) return route.abort();
    return route.fulfill({ contentType: 'text/javascript', body: readFileSync(target, 'utf8') });
  });
  await page.goto('http://grille.test/');
  const result = await page.evaluate(async ({ raw, reference }) => {
    const { catalogText } = await import('/src/apps/tube-designer/webpage/productCatalog.mjs');
    const { renderDesignerAddDialog } = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const { handleDesignerAreaAction } = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const { bindProductParameterDiagrams, renderProductParameterDiagram } = await import('/src/apps/tube-designer/webpage/productParameterDiagram.mjs');
    const { tubeDesignerCss } = await import('/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
    const display = raw.display;
    const template = { ...raw, display, available: true, name: catalogText(raw.displayName),
      groups: raw.groups.map(g => ({ ...g, displayName: catalogText(g.displayName) })),
      parameters: raw.parameters.map(p => ({ ...p, groupKey: p.group, type: { enum: 'select', string: 'text' }[p.valueType] || p.valueType,
        displayName: catalogText(p.displayName), options: p.choices?.map(c => ({ ...c, label: catalogText(c.displayName) })) })) };
    const defaults = Object.fromEntries(raw.parameters.map(f => [f.key, f.defaultValue]));
    const view = { scene: { tubeDesigner: { templates: [template] } }, tubeDesignerAddTemplateId: raw.id,
      tubeDesignerAddDialogOpen: true, tubeDesignerAddDraft: defaults };
    document.body.innerHTML = `<style>*{box-sizing:border-box}body{margin:0;font-family:Arial,'Microsoft YaHei'}${tubeDesignerCss}</style><div class="tube-designer-workspace"></div>`;
    const mount = document.querySelector('.tube-designer-workspace'), context = { mount };
    let fullRenders = 0;
    const ops = { renderProject() { fullRenders++; mount.innerHTML = renderDesignerAddDialog(view.scene.tubeDesigner, view); bindProductParameterDiagrams(mount); } };
    ops.renderProject();
    const initialRenders = fullRenders;
    const left = mount.querySelector('.tube-designer-template-pane'), right = mount.querySelector('.tube-designer-config-parameters');
    // Supply enough genuine scroll range on both independent panes.
    left.style.height = '300px'; left.style.overflow = 'auto';
    const spacer = document.createElement('div'); spacer.style.height = '700px'; left.append(spacer);
    right.style.height = '290px'; right.style.overflow = 'auto';
    left.scrollTop = 45; right.scrollTop = 60;
    if (right.querySelectorAll('[data-tube-designer-parameter]').length !== 4 || right.querySelector('input[data-tube-designer-parameter]'))
      throw new Error('Creation must expose only the four style choices');
    const untouched = mount.querySelector('[data-tube-designer-parameter="frameType"]');
    let lastAction;
    mount.addEventListener('change', event => {
      lastAction = handleDesignerAreaAction(context, view, event.target.dataset.camChangeAction, event.target, ops);
    });
    const changes = [];
    for (const [key, value] of [['frameType', 'closed_frame'], ['handleSide', 'right'], ['handleClosure', 'open_slot'],
      ['handleSide', 'center_double'], ['handleEnabled', 'no'], ['handleEnabled', 'yes'], ['frameType', 'bottom_rail']]) {
      const input = mount.querySelector(`[data-tube-designer-parameter="${key}"]`);
      input.focus({ preventScroll: true }); input.value = value;
      const leftScroll = left.scrollTop, rightScroll = right.scrollTop;
      input.dispatchEvent(new Event('change', { bubbles: true })); await lastAction;
      if (view.tubeDesignerAddDraft[key] !== value) throw new Error(`Draft did not update: ${key}`);
      const current = mount.querySelector(`[data-tube-designer-parameter="${key}"]`);
      if (input !== current || document.activeElement !== current) throw new Error(`Lost editor/focus: ${key}`);
      const expectedRight = Math.min(rightScroll, right.scrollHeight - right.clientHeight);
      if (left.scrollTop !== leftScroll || Math.abs(right.scrollTop - expectedRight) > 1)
        throw new Error(`Lost scroll: ${key} ${rightScroll} -> ${right.scrollTop}, expected ${expectedRight}`);
      const svg = mount.querySelector('[data-minimal-grille-diagram]');
      if (!svg || svg.querySelectorAll('[data-grille-stock]').length === 0) throw new Error('Missing live stocks');
      if (Boolean(svg.querySelector('[data-grille-stock="frame.top.0001"]')) !== (view.tubeDesignerAddDraft.frameType === 'closed_frame'))
        throw new Error('Frame artwork did not follow the selected style');
      const expectedClearances = view.tubeDesignerAddDraft.handleEnabled === 'no' ? 0 : view.tubeDesignerAddDraft.handleSide === 'center_double' ? 2 : 1;
      if (svg.querySelectorAll('.minimal-grille-clearance').length !== expectedClearances)
        throw new Error('Handle artwork did not follow the selected style');
      changes.push({ key, value, label: svg.getAttribute('aria-label'), members: svg.querySelectorAll('[data-grille-stock]').length });
    }
    if (fullRenders !== initialRenders) throw new Error('Whole dialog was replaced');
    if (untouched !== mount.querySelector('[data-tube-designer-parameter="frameType"]')) throw new Error('Unrelated input replaced');
    // Keep a focused input/selection through another local preview patch.
    const textProbe = document.createElement('input'); textProbe.value = 'selection-probe'; left.append(textProbe);
    textProbe.focus({ preventScroll: true }); textProbe.setSelectionRange(0, 2);
    const other = mount.querySelector('[data-tube-designer-parameter="handleSide"]'); other.value = 'left';
    other.dispatchEvent(new Event('change', { bubbles: true })); await lastAction;
    if (document.activeElement !== textProbe || textProbe.selectionStart !== 0 || textProbe.selectionEnd !== 2)
      throw new Error('Unrelated text selection was lost');
    const referenceMount = document.createElement('div'); referenceMount.style.width = '420px';
    referenceMount.innerHTML = renderProductParameterDiagram(reference,
      Object.fromEntries(reference.parameters.map(f => [f.key, f.defaultValue])), { mode: 'add', compact: true, hideDimensionCards: true });
    document.body.append(referenceMount);
    const previewHeight = mount.querySelector('.tube-designer-product-diagram-art').getBoundingClientRect().height;
    const referenceHeight = referenceMount.querySelector('.tube-designer-product-diagram-art').getBoundingClientRect().height;
    referenceMount.remove();
    if (previewHeight !== 96 || referenceHeight !== previewHeight) throw new Error('Preview size differs from security window');
    return { changes, fullRenders, previewHeight, referenceHeight, passed: true };
  }, { raw: { ...descriptor, display: JSON.parse(readFileSync(resolve(sourceRoot, 'apps/tube-designer/templates/product/minimal_protective_grille/display.json'))) },
    reference: JSON.parse(readFileSync(resolve(sourceRoot, 'apps/tube-designer/templates/product/single_face_security_window/template.json'))) });
  await page.screenshot({ path: resolve(evidence, 'add-live-diagram.png') });
  writeFileSync(resolve(evidence, 'preview-browser-report.json'), JSON.stringify({ ...result, geometryOracleCases: cases.length,
    nativeBusinessMocked: true, actualBrowserEvents: true, standaloneNative: false }, null, 2));
  console.log(JSON.stringify({ passed: true, actualGeometryOracleCases: cases.length, changes: result.changes.length }));
} finally { await browser.close(); }
