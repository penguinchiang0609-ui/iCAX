// Production artwork and real annotation actions; host commits remain held fixtures.
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { browserReportDirectory, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
import { installProductInteractionFixture } from './productInteractionFixture.mjs';
import { minimalGrilleDiagramStocks } from '../../apps/tube-designer/webpage/minimalGrilleDiagram.mjs';

const output = browserReportDirectory('minimal-grille-drying-rod-20261110/browser');
const temporary = resolve(output, 'temp'); mkdirSync(temporary, { recursive: true });
const sourceRoot = fileURLToPath(new URL('../../', import.meta.url));
const folder = 'apps/tube-designer/templates/product/minimal_protective_grille';
const descriptor = { ...JSON.parse(readBrowserAsset(`${folder}/template.json`)),
  display: JSON.parse(readBrowserAsset(`${folder}/display.json`)) };
const defaults = Object.fromEntries(descriptor.parameters.map(f => [f.key, f.defaultValue]));
const python = process.env.ICAX_PYTHON || 'C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe';
const oracle = spawnSync(python, ['-X', 'utf8', '-c', `
import json,sys
from pathlib import Path
from itertools import product
root=Path(sys.argv[1]);sys.path.insert(0,str(root/'iCAX-Engine/framework/TemplateRuntime/python'))
from icax_template_worker import _load_template
folder=root/'apps/tube-designer/templates/product/minimal_protective_grille'
d=json.loads((folder/'template.json').read_text(encoding='utf-8'))
t=_load_template(str(folder/'template.py'),'drying-diagram-oracle')
defaults={f['key']:f['defaultValue'] for f in d['parameters']};cases=[]
for style,kind,change in product(('five_face','balcony_closure'),('rect','rect-bar','round'),
 ({},{'dryingRodRearOffset':40,'dryingRodDrop':120},
  {'width':1600,'projectionDepth':300,'balconyTopDepth':300,'topDrop':60,'balconyTopDrop':60,
   'dryingWidth':20,'dryingDepth':10,'dryingRodDrop':100})):
 p={**defaults,'layoutStyle':style,'dryingRodEnabled':'yes','dryingProfileType':kind,**change}
 state=t.layout(p)
 cases.append({'values':p,'facts':state['facts']['dryingRod'],'members':[
  {'key':m['key'],'role':m['role'],'profileRole':m.get('profileRole'),'start':m['start'],'end':m['end'],
   'width':m['profile'].width} for m in state['members']]})
print(json.dumps(cases))
`, sourceRoot], { encoding: 'utf8', maxBuffer: 8 * 1024 * 1024,
  env: { ...process.env, TEMP: temporary, TMP: temporary, TMPDIR: temporary } });
assert.equal(oracle.status, 0, oracle.stderr);
const cases = JSON.parse(oracle.stdout);
const near = (a, b, label) => assert.ok(Math.abs(a - b) < 1e-7, `${label}: ${a} != ${b}`);
for (const test of cases) {
  const actual = minimalGrilleDiagramStocks(descriptor.extensions.productDiagram, test.values).spatial;
  const stocks = actual.members.filter(m => m.profileRole === 'drying');
  const expected = test.members.filter(m => m.profileRole === 'drying');
  assert.equal(stocks.length, 3);
  assert.deepEqual(stocks.map(m => m.key).sort(), expected.map(m => m.key).sort());
  near(actual.dryingRod.rodY, test.facts.y, 'rear datum');
  near(actual.dryingRod.rodZ, test.facts.z, 'top datum');
  for (const stock of stocks) {
    const member = expected.find(m => m.key === stock.key);
    assert.equal(stock.role, member.role); assert.equal(stock.width, member.width);
    stock.start.forEach((v, axis) => near(v, member.start[axis], `${stock.key} start ${axis}`));
    if (stock.key.includes('.rod.')) stock.end.forEach((v, axis) => near(v, member.end[axis], `${stock.key} end ${axis}`));
    else {
      const contact = test.facts.topContacts.find(c => c.armKey === stock.key);
      const pictureContact = actual.dryingRod.topContacts.find(c => c.armKey === stock.key);
      assert.equal(pictureContact.hostKey, contact.hostKey, 'The picture must attach to the same nearest real stock');
      const host = test.members.find(m => m.key === contact.hostKey);
      const hostAxisZ = host.start[2] + (host.end[2] - host.start[2]) * (test.facts.y - host.start[1]) / (host.end[1] - host.start[1]);
      near(stock.end[2], hostAxisZ, 'artwork receiving stock axis');
      near(stock.end[0], member.end[0], 'hanger x'); near(stock.end[1], member.end[1], 'hanger y');
      assert.ok(stock.end[2] >= member.end[2] - 1e-7 && stock.end[2] - member.end[2] <= host.width,
        'Artwork axis must contain the true receiving surface; final trimming belongs to native geometry');
    }
  }
}
const report = { passed: false, pythonPositionCases: cases.length, physicalParameterCount: descriptor.parameters.length,
  diagramEndConvention: 'source axes; exact receiving-surface trims remain in the Python/native pipeline',
  nativeBusinessMocked: true, standaloneNative: false, cases: [] };
writeFileSync(resolve(output, 'python-position-oracle.json'), JSON.stringify(cases, null, 2));
const require = createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const { chromium } = require('playwright');
const browser = await chromium.launch({ headless: true, channel: 'msedge' });
try {
  const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  const errors = []; page.on('pageerror', error => errors.push(error.message));
  await page.route('http://drying-rod.test/**', serveBrowserAsset);
  await page.goto('http://drying-rod.test/');
  await page.addStyleTag({ content: `*{box-sizing:border-box}body{margin:0;font-family:Arial,'Microsoft YaHei'}
    .cam-workbench{display:flex;gap:8px}.cam-context-pane,.cam-info-pane{height:280px;overflow:auto;width:300px;flex:none}
    .cam-viewport{width:920px;height:600px;position:relative;flex:none}.nested-left{height:150px;overflow:auto}
    .nested-left>div{height:1600px}.cam-context-pane:after,.cam-info-pane:after{content:'';display:block;height:1600px}
    .tube-designer-parameter-scroll{height:180px!important;overflow:auto!important}
    .tube-designer-parameter-scroll:after{content:'';display:block;height:1600px}
    #drying-artwork{width:700px;height:360px;margin:20px;background:#14393e;padding:16px}#drying-artwork svg{width:100%;height:100%}` });
  await page.addStyleTag({ content: await page.evaluate(async () => (await import('/src/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs')).tubeDesignerCss) });
  const anchors = ['dryingRodEnabled', 'dryingRodRearOffset', 'dryingRodDrop'].map((key, i) => ({
    id: `drying-fixture:${key}`, parameter: key, kind: key === 'dryingRodEnabled' ? 'parameter' : 'linear',
    start: [0, 0, 1200 - i * 150], end: [150, 0, 1200 - i * 150], generatedValue: defaults[key] }));
  await installProductInteractionFixture(page, descriptor, { specificationAnnotations: anchors },
    { initialParameters: { ...defaults, layoutStyle: 'five_face' } });
  await page.evaluate(async () => {
    const f = window.productFixture;
    const diagram = await import('/src/apps/tube-designer/webpage/productParameterDiagram.mjs');
    const artwork = await import('/src/apps/tube-designer/webpage/minimalGrilleDiagram.mjs');
    const host = document.createElement('div'); host.id = 'drying-artwork'; document.body.append(host);
    f.refreshDrying = () => { diagram.bindProductSpecificationAnnotations(f.mount, f.view);
      host.innerHTML = artwork.renderMinimalGrilleArtwork(f.template, f.template.extensions.productDiagram,
        f.view.scene.tubeDesigner.product.parameters); };
    f.operations = [];
    for (const eventName of ['click', 'change']) f.mount.addEventListener(eventName, event => {
      const node = event.target.closest?.(eventName === 'click'
        ? '[data-cam-action="tube-designer-edit-scene-specification"]' : '[data-tube-designer-scene-specification-input]');
      if (node) f.operations.push(f.actions.handleDesignerAreaAction(f.context, f.view,
        eventName === 'click' ? node.dataset.camAction : node.dataset.camChangeAction, node, f.ops));
    });
    f.refreshDrying();
  });
  for (const [index, [key, value]] of [['dryingRodEnabled', 'yes'], ['dryingRodRearOffset', 40],
    ['dryingRodDrop', 120], ['dryingRodEnabled', 'no'], ['dryingRodEnabled', 'yes']].entries()) {
    const hit = await page.locator(`button[data-tube-designer-parameter-key="${key}"]`).boundingBox();
    assert.ok(hit, `Missing annotation ${key}`); await page.mouse.dblclick(hit.x + hit.width / 2, hit.y + hit.height / 2);
    const editor = page.locator(`[data-tube-designer-scene-specification-input="${key}"]`);
    await editor.waitFor({ state: 'visible' });
    if (key === 'dryingRodEnabled') await editor.selectOption(value);
    else { await editor.fill(String(value)); await editor.press('Enter'); }
    await page.waitForFunction(() => window.productFixture.pending.length === 1);
    report.cases.push(await page.evaluate(async ({ key, value, side }) => {
      const f = window.productFixture; await f.completeHeldChange(Promise.all(f.operations), side);
      f.refreshDrying(); f.assertPhysicalCommits();
      const values = f.view.scene.tubeDesigner.product.parameters;
      if (values[key] !== value) throw new Error('Lost annotation value');
      const stocks = [...document.querySelectorAll('#drying-artwork [data-grille-profile-role="drying"]')];
      if (stocks.length !== (values.dryingRodEnabled === 'yes' ? 3 : 0)) throw new Error('Artwork does not follow drying state');
      if (values.dryingRodEnabled === 'yes' && !document.querySelector('#drying-artwork svg').getAttribute('aria-label').includes('带晾衣杆'))
        throw new Error('Style label missing drying rod');
      return { key, value, rods: stocks.map(node => node.dataset.grilleStock), retainedInteraction: true };
    }, { key, value, side: index % 2 ? 'left' : 'right' }));
  }
  assert.deepEqual(errors, []);
  await page.evaluate(async raw => {
    const { renderMinimalGrilleArtwork } = await import('/src/apps/tube-designer/webpage/minimalGrilleDiagram.mjs');
    const defaults = Object.fromEntries(raw.parameters.map(f => [f.key, f.defaultValue]));
    document.body.innerHTML = `<style>body{margin:0;padding:24px;background:#eff4f5;font-family:Arial,'Microsoft YaHei'}main{display:grid;grid-template-columns:1fr 1fr;gap:20px}article{padding:18px;background:white}svg{width:100%;height:420px;background:#14393e}p{line-height:1.6;color:#345}h1{font-size:24px}h2{font-size:20px}</style><h1>极简防护网 · 可选晾衣杆</h1><main>` +
      ['five_face', 'balcony_closure'].map(layoutStyle => {
        const p = { ...defaults, layoutStyle, width: 1600, projectionDepth: 300, balconyTopDepth: 300,
          balconyBottomDepth: 150, dryingRodEnabled: 'yes', handleEnabled: 'no' };
        return `<article><h2>${layoutStyle === 'five_face' ? '五面飘网' : '阳台上下收口'}</h2>${renderMinimalGrilleArtwork(raw, raw.extensions.productDiagram, p)}<p>金色：一根通长晾衣横杆 + 左右两根吊臂<br>杆中心距后侧外表面 ${p.dryingRodRearOffset} mm；距正面顶部 ${p.dryingRodDrop} mm<br>吊臂接上方最近的实际侧杆；图中表示构件轴线，切合端部由实体计算。</p></article>`;
      }).join('') + '</main>';
  }, descriptor);
  await page.screenshot({ path: resolve(output, 'drying-rod-dynamic-artwork.png'), fullPage: true });
  Object.assign(report, { passed: true, actualBrowserEvents: true, webgl: true });
  console.log(JSON.stringify({ passed: true, pythonPositionCases: cases.length, annotationEdits: report.cases.length }));
} catch (error) { report.error = error.stack; throw error; }
finally { writeFileSync(resolve(output, 'report.json'), JSON.stringify(report, null, 2)); await browser.close(); }
