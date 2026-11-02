// Real production WebGL, annotation editors and update actions; the host EC
// response and initial annotation anchors are controlled browser fixtures.
import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, importBrowserAsset, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
import { installProductInteractionFixture } from './productInteractionFixture.mjs';

const output = browserReportDirectory('minimal-protective-grille-20261027/mounting-browser');
const folder = 'apps/tube-designer/templates/product/minimal_protective_grille';
const descriptor = { ...JSON.parse(readBrowserAsset(`${folder}/template.json`)),
  display: JSON.parse(readBrowserAsset(`${folder}/display.json`)) };
const defaults = Object.fromEntries(descriptor.parameters.map(field => [field.key, field.defaultValue]));
const anchorFixture = { specificationAnnotations: [
  { parameter: 'installHoleEnabled', kind: 'parameter', start: [180, 0, 1100], end: [180, 0, 1100] },
  { parameter: 'installHoleCountPerSide', kind: 'parameter', start: [440, 0, 1000], end: [440, 0, 1000] },
  { parameter: 'installHoleTopOffset', kind: 'linear', start: [500, 0, 1250], end: [500, 0, 1200], offset: [140, 0, 0] },
  { parameter: 'installHoleBottomOffset', kind: 'linear', start: [500, 0, 0], end: [500, 0, 50], offset: [140, 0, 0] },
].map(anchor => ({ ...anchor, id: `mounting-fixture:${anchor.parameter}`, generatedValue: defaults[anchor.parameter] })) };
const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
const report = { passed: false, standaloneNative: false, nativeBusinessMocked: true,
  anchorSource: 'controlled renderer fixture; native-generated anchor acceptance is separate', cases: [] };
try {
  const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://minimal-mounting.test/**', serveBrowserAsset);
  await page.goto('http://minimal-mounting.test/');
  await page.addStyleTag({ content: `${tubeDesignerCss}*{box-sizing:border-box}body{margin:0;font-family:Arial,'Microsoft YaHei'}
    .cam-workbench{display:flex;gap:8px}.cam-context-pane,.cam-info-pane{height:280px;overflow:auto;width:300px;flex:none}
    .cam-viewport{width:920px;height:600px;position:relative;flex:none}.nested-left{height:150px;overflow:auto}
    .nested-left>div{height:1600px}.cam-context-pane:after,.cam-info-pane:after{content:'';display:block;height:1600px}
    .tube-designer-parameter-scroll{height:180px!important;overflow:auto!important}
    .tube-designer-parameter-scroll:after{content:'';display:block;height:1600px}
    #mounting-artwork{width:500px;height:280px;margin:20px;background:#14393e;padding:16px}
    #mounting-artwork svg{width:100%;height:100%}` });
  await installProductInteractionFixture(page, descriptor, anchorFixture);
  await page.evaluate(async () => {
    const f = window.productFixture;
    const diagram = await import('/src/apps/tube-designer/webpage/productParameterDiagram.mjs');
    const artwork = await import('/src/apps/tube-designer/webpage/minimalGrilleDiagram.mjs');
    const { parameterVisible } = await import('/src/apps/tube-designer/webpage/parameterConditions.mjs');
    f.parameterVisible = parameterVisible;
    const host = document.createElement('div'); host.id = 'mounting-artwork'; document.body.append(host);
    f.bindAnnotations = () => diagram.bindProductSpecificationAnnotations(f.mount, f.view);
    f.refreshArtwork = () => { host.innerHTML = artwork.renderMinimalGrilleArtwork(
      f.template, f.template.extensions.productDiagram, f.view.scene.tubeDesigner.product.parameters); };
    f.refreshArtwork();
    f.annotationState = () => diagram.resolveProductSpecificationAnnotations(f.view.scene.tubeDesigner, f.view);
    f.operations = [];
    f.mount.addEventListener('click', event => {
      const node = event.target.closest?.('[data-cam-action="tube-designer-edit-scene-specification"]');
      if (node) f.operations.push(f.actions.handleDesignerAreaAction(f.context, f.view, node.dataset.camAction, node, f.ops));
    });
    f.mount.addEventListener('change', event => {
      const node = event.target.closest?.('[data-tube-designer-scene-specification-input], [data-tube-designer-parameter]');
      if (node) f.operations.push(f.actions.handleDesignerAreaAction(f.context, f.view, node.dataset.camChangeAction, node, f.ops));
    });
    if (!f.view.viewport.renderer.domElement.getContext('webgl2')) throw new Error('Production WebGL viewport missing');
    if (f.annotationState().length !== 1 || f.annotationState()[0].parameter !== 'installHoleEnabled')
      throw new Error('Disabled mounting layout exposes dependent annotations');
    if (document.querySelectorAll('[data-grille-install-hole]').length) throw new Error('Disabled mounting draws holes');
  });

  const operations = [
    ['installHoleEnabled', 'yes'],
    ...[2, 3, 4, 5, 4].map(value => ['installHoleCountPerSide', value]),
    ['installHoleOrientation', 'front'],
    ['installHoleTopOffset', 70], ['installHoleBottomOffset', 80],
    ['installHoleEnabled', 'no'], ['installHoleEnabled', 'yes'],
    ['installHoleOrientation', 'side'],
    ['jointMode', 'male_female_half_hole'], ['maleHeadLength', 3.25],
    ['maleCornerType', 'round'], ['maleCornerSize', 0.4],
    ['jointMode', 'male_female_through_hole'], ['maleCornerType', 'square'],
    ['maleCornerType', 'chamfer'], ['jointMode', 'butt_weld'],
    ['jointMode', 'male_female_through_hole'], ['jointMode', 'male_female_half_hole'],
    ['jointMode', 'male_female_through_hole'],
    ['maleTabCount','four'], ['maleTabAngle',27],
    ['maleEndForm','full_section'], ['fullHeadTotalClearance',0.25],
    ['maleEndForm','tongue'], ['maleTabCount','two'], ['maleTabAngle',0],
  ];
  const jointKeys = new Set(descriptor.parameters.filter(field =>
    ['joint', 'joint_connection', 'joint_male', 'joint_female'].includes(field.group)).map(field => field.key));
  const expectedDrafts = Object.fromEntries(Object.entries(defaults).filter(([key]) => key.startsWith('installHole') || jointKeys.has(key)));
  for (const [index, [key, value]] of operations.entries()) {
    expectedDrafts[key] = value;
    const processEditor = key === 'installHoleOrientation' || jointKeys.has(key);
    if (!processEditor) {
      const hit = await page.evaluate(parameter => {
        const trigger = document.querySelector(`button[data-tube-designer-parameter-key="${parameter}"]`);
        if (!trigger) throw new Error(`Missing annotation trigger ${parameter}`);
        const rect = trigger.getBoundingClientRect();
        return { x: (rect.left + rect.right) / 2, y: (rect.top + rect.bottom) / 2 };
      }, key);
      await page.mouse.dblclick(hit.x, hit.y);
    }
    const editor = page.locator(processEditor ? `[data-tube-designer-parameter="${key}"]`
      : `[data-tube-designer-scene-specification-input="${key}"]`);
    await editor.waitFor({ state: 'visible' });
    const editorType = await editor.evaluate(node => node.tagName.toLowerCase());
    if (key === 'installHoleCountPerSide') {
      assert.equal(editorType, 'select');
      assert.deepEqual(await editor.evaluate(node => [...node.options].map(option => ({
        value: Number(option.value), originalType: option.dataset.tubeDesignerValueType }))),
      [2, 3, 4, 5].map(value => ({ value, originalType: 'number' })));
    }
    if (editorType === 'select') await editor.selectOption(String(value));
    else { await editor.fill(String(value)); await editor.press(processEditor ? 'Tab' : 'Enter'); }
    await page.waitForFunction(() => window.productFixture.pending.length === 1);
    const result = await page.evaluate(async ({ key, value, side, expectedDrafts }) => {
      const f = window.productFixture;
      await f.completeHeldChange(Promise.all(f.operations), side);
      f.bindAnnotations(); f.refreshArtwork(); f.assertPhysicalCommits();
      const values = f.view.scene.tubeDesigner.product.parameters;
      if (values[key] !== value) throw new Error('Physical parameter update did not commit');
      if (f.requests.at(-1).parameters[key] !== value) throw new Error('Parameter payload lost the original value type');
      if (typeof values.installHoleCountPerSide !== 'number') throw new Error('Numeric count enum became a string');
      for (const [draftKey, expected] of Object.entries(expectedDrafts)) {
        if (values[draftKey] !== expected || f.requests.at(-1).parameters[draftKey] !== expected)
          throw new Error(`Process update lost preserved draft ${draftKey}`);
      }
      const inserted = values.jointMode !== 'butt_weld';
      const tongue=inserted && values.maleEndForm==='tongue';
      const jointVisibility = { jointMode: true, maleEndForm:inserted, maleTabCount:tongue, maleTabAngle:tongue, fullHeadTotalClearance:inserted&&!tongue, maleHeadLength: values.jointMode === 'male_female_half_hole',
        maleHeadWidth: tongue, maleCornerType: tongue,
        maleCornerSize: tongue && values.maleCornerType !== 'square',
        holeTotalClearanceDepth: tongue, holeTotalClearanceHeight: tongue, minimumFeatureWeb: tongue };
      for (const [jointKey, expected] of Object.entries(jointVisibility)) {
        const field = f.template.parameters.find(field => field.key === jointKey);
        if (f.parameterVisible(field, values) !== expected || Boolean(f.ordinary(jointKey)) !== expected)
          throw new Error(`Joint condition/editor mismatch for ${values.jointMode}/${jointKey}`);
        if (expected && String(f.ordinary(jointKey).value) !== String(expectedDrafts[jointKey]))
          throw new Error(`Joint editor failed to restore preserved draft ${jointKey}`);
      }
      const annotations = f.annotationState();
      const expectedCount = values.installHoleEnabled === 'yes' ? 4 : 1;
      if (annotations.length !== expectedCount || new Set(annotations.map(a => a.parameter)).size !== expectedCount)
        throw new Error('Duplicate or missing mounting annotations after conditional refresh');
      for (const a of annotations) if (f.ordinary(a.parameter)) throw new Error('Mounting annotation duplicated in right pane');
      for (const key of ['installHoleOrientation', 'installHoleLargeDiameter', 'installHoleSmallDiameter', 'installHoleMinimumWeb'])
        if (Boolean(f.ordinary(key)) !== (values.installHoleEnabled === 'yes')) throw new Error('Machining editor visibility mismatch');
      if (values.installHoleEnabled === 'yes') {
        const orientation = f.ordinary('installHoleOrientation');
        if (orientation.tagName !== 'SELECT' || orientation.value !== values.installHoleOrientation)
          throw new Error('Installation orientation must remain an editable process select');
        const count = annotations.find(annotation => annotation.parameter === 'installHoleCountPerSide');
        if (count.editor.type !== 'select' || typeof count.editor.value !== 'number'
            || JSON.stringify(count.editor.options.map(option => option.value)) !== '[2,3,4,5]')
          throw new Error('Resolved mounting count options lost their numeric enum contract');
      }
      const dots = [...document.querySelectorAll('#mounting-artwork [data-grille-install-hole]')];
      if (dots.length !== (values.installHoleEnabled === 'yes' ? 2 * values.installHoleCountPerSide : 0))
        throw new Error('Schematic does not show one mounting group per side/height');
      if (values.installHoleEnabled === 'yes') {
        const expected = Array.from({ length: values.installHoleCountPerSide }, (_, i) =>
          values.installHoleBottomOffset + i * (values.height - values.installHoleTopOffset - values.installHoleBottomOffset)
          / (values.installHoleCountPerSide - 1));
        for (const side of ['left', 'right']) {
          const actual = dots.filter(dot => dot.dataset.side === side).map(dot => Number(dot.dataset.centerZ)).sort((a, b) => a - b);
          if (actual.length !== expected.length || actual.some((z, i) => Math.abs(z - expected[i]) > 1e-7))
            throw new Error(`Schematic mounting spacing differs on ${side}`);
        }
      }
      return { key, value, valueType: typeof values[key], annotations: annotations.map(a => ({ parameter: a.parameter, kind: a.kind })),
        dotCount: dots.length, orientationDraft: values.installHoleOrientation,
        jointDrafts: { mode: values.jointMode, maleHeadLength: values.maleHeadLength,
          maleCornerType: values.maleCornerType, maleCornerSize: values.maleCornerSize },
        latestFocusSelectionScrollNodesAndListenersPreserved: true };
    }, { key, value, side: index % 2 ? 'right' : 'left', expectedDrafts });
    report.cases.push(result);
  }
  const persistence = await page.evaluate(() => {
    const f = window.productFixture, values = f.view.scene.tubeDesigner.product.parameters;
    return { count: values.installHoleCountPerSide, top: values.installHoleTopOffset, bottom: values.installHoleBottomOffset,
      orientation: values.installHoleOrientation, jointMode: values.jointMode, maleHeadLength: values.maleHeadLength,
      maleCornerType: values.maleCornerType, maleCornerSize: values.maleCornerSize,
      commits: f.requests.length, pageErrors: [] };
  });
  assert.deepEqual(persistence, { count: 4, top: 70, bottom: 80, orientation: 'side',
    jointMode: 'male_female_through_hole', maleHeadLength: 3.25, maleCornerType: 'chamfer', maleCornerSize: 0.4,
    commits: operations.length, pageErrors: [] });
  assert.deepEqual(errors, []);
  await page.screenshot({ path: resolve(output, 'mounting-layout.png') });
  Object.assign(report, { passed: true, actualBrowserEvents: true, webgl: true, preservedDrafts: persistence });
  console.log(`PASS minimal mounting browser: ${report.cases.length} real annotation/process edits, numeric enum types, schematic spacing and async interaction retention`);
} catch (error) {
  report.error = error.stack; throw error;
} finally {
  writeFileSync(resolve(output, 'report.json'), JSON.stringify(report, null, 2));
  await browser.close();
}
