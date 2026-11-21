import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { browserReportDirectory, importBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const { nestingSettingsCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/nestingSettings.css.mjs');
const output = browserReportDirectory('tube-nesting-search-strategy/browser');
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'chrome' });
try {
  const page = await browser.newPage({ viewport: { width: 1420, height: 950 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://tube-search.test/**', serveBrowserAsset);
  await page.goto('http://tube-search.test/');
  await page.addStyleTag({ content: `${nestingSettingsCss}
    body{margin:0;font:13px Arial;background:#dde6e8} .cam-workbench{display:flex;gap:16px;padding:20px}
    .cam-context-pane,.cam-info-pane{height:250px;width:340px;overflow:auto;background:white}
    .nested{height:170px;overflow:auto}.spacer{height:650px}.cam-viewport{width:300px;height:250px;background:#20414b}
    .tube-designer-field{display:block}` });
  await page.evaluate(async () => {
    const settings = await import('/src/apps/tube-designer/webpage/nestingSettings.mjs');
    const workflow = await import('/src/apps/tube-designer/webpage/nestingWorkflow.mjs');
    const floating = await import('/src/apps/tube-designer/webpage/floatingEditorDom.mjs');
    const { nativeSectionIdentity } = await import('/src/tests/iCAX-UI/fixtures/nestingSectionIdentity.mjs');
    const profile = { sectionIdentity: nativeSectionIdentity('rect-20-20-t1'), kind: 'rect', width: 20, depth: 20,
      wallThickness: 1, displayName: '方管', specification: '20 × 20 × 1' };
    const f = { settings, workflow, floating, calls: [], logs: [], saving: false, conditional: false, renders: 0,
      view: { activeAreaId: 'nesting', scene: { tubeDesigner: { manufacturingGroups: [{ name: '测试', productEntityId: 'product',
        parts: [{ entityId: 'part', name: '横杆', quantity: 1, length: 1000, profile }] }],
        nestingSettings: { version: 2, stocks: [], parameters: { partGap: 2 } } } } } };
    document.body.innerHTML = '<main id="mount"><div class="cam-workbench"><aside class="cam-context-pane"></aside><div class="cam-viewport"><canvas></canvas></div><aside class="cam-info-pane"></aside></div></main>';
    f.mount = document.querySelector('#mount');
    f.left = () => '<div class="nested"><label class="tube-designer-field"><input data-field="left-note" value="left draft"></label><div class="spacer"></div></div><div class="spacer"></div>';
    f.right = () => `<div class="nested">${f.conditional ? '<label class="tube-designer-field"><input data-field="conditional" value="extra"></label>' : ''}<label class="tube-designer-field"><input data-field="right-note" value="right draft"></label><div class="spacer"></div></div><div class="spacer"></div>`;
    f.mount.querySelector('.cam-context-pane').innerHTML = f.left();
    f.mount.querySelector('.cam-info-pane').innerHTML = f.right();
    f.context = { mount: f.mount, project: { projectId: 'search-strategy' }, actions: {}, sceneProxy: { async invoke(method, payload, options) {
      f.calls.push({ method, payload: structuredClone(payload), options: structuredClone(options) });
      if (method !== 'TubeDesigner.SaveNestingSettings') throw new Error('Unexpected native fixture method: ' + method);
      if (f.holdSave) await new Promise(resolve => { f.releaseSave = resolve; });
      return { settings: structuredClone(payload.settings) };
    } } };
    f.context.actions.refreshActiveSceneState = async () => {
      if (f.holdRefresh) await new Promise(resolve => { f.releaseRefresh = resolve; });
      f.view.scene.tubeDesigner = { ...structuredClone(f.view.scene.tubeDesigner), repositoryRevision: 'saved' };
      f.conditional = true;
      f.render();
    };
    f.render = () => {
      const suffix = settings.renderNestingSettingsDialogs(f.context, f.view);
      if (!floating.patchFloatingEditorDom(f.view, f.mount, { suffix, sceneProxy: f.context.sceneProxy,
        left: f.left(), right: f.right() })) throw new Error('Production floating editor patch did not handle the refresh');
      f.renders++;
    };
    f.ops = { renderProject: f.render, appendProjectLog: (...args) => f.logs.push(args) };
    floating.rememberFloatingEditorDom(f.view, f.mount, { suffix: '', sceneProxy: f.context.sceneProxy });
    f.mount.addEventListener('change', event => {
      const target = event.target.closest('[data-cam-change-action]');
      if (target) f.action = settings.handleNestingSettingsAction(f.context, f.view, target.dataset.camChangeAction, target, f.ops);
    });
    f.mount.addEventListener('click', event => {
      const target = event.target.closest('[data-cam-action]');
      if (target) f.action = settings.handleNestingSettingsAction(f.context, f.view, target.dataset.camAction, target, f.ops);
    });
    f.nodes = [...f.mount.querySelectorAll('canvas,[data-field$="-note"]')];
    f.inputEvents = 0;
    f.nodes[1].addEventListener('input', () => f.inputEvents++);
    globalThis.searchFixture = f;
    settings.handleNestingSettingsRibbonCommand(f.context, f.view, 'nesting.parameters', f.ops);
    f.parameterNodes = [...f.mount.querySelectorAll('[data-tube-nesting-parameter]')];
  });
  const mode = page.getByLabel('优化偏好', { exact: true });
  const time = page.getByLabel('最长搜索时间（秒）', { exact: true });
  assert.equal(await mode.inputValue(), 'time');
  assert.equal(await time.inputValue(), '5');
  await mode.selectOption('utilization');
  assert.equal(await time.inputValue(), '60');
  await time.fill('120');
  await time.dispatchEvent('change');
  await time.evaluate(input => { input.focus(); input.setSelectionRange(1, 2, 'backward'); });
  const edit = await page.evaluate(() => {
    const f = searchFixture;
    return { stable: f.parameterNodes.every(node => node.isConnected), focused: document.activeElement === f.parameterNodes[2],
      selection: [f.parameterNodes[2].selectionStart, f.parameterNodes[2].selectionEnd, f.parameterNodes[2].selectionDirection], renders: f.renders };
  });
  assert.deepEqual(edit, { stable: true, focused: true, selection: [1, 2, 'backward'], renders: 1 });
  await mode.selectOption('time');
  assert.equal(await time.inputValue(), '120', 'Mode changes retain a custom duration');
  await mode.selectOption('utilization');
  await page.screenshot({ path: resolve(output, 'parameters.png'), fullPage: true });
  await page.evaluate(() => { searchFixture.holdSave = true; searchFixture.holdRefresh = true; });
  await page.getByRole('button', { name: '确定', exact: true }).click();
  await page.waitForFunction(() => Boolean(searchFixture.releaseSave));
  await page.evaluate(() => {
    const f = searchFixture;
    const input = f.mount.querySelector('[data-field="right-note"]');
    input.focus({ preventScroll: true }); input.value = 'latest right draft'; input.setSelectionRange(2, 7, 'backward');
    f.mount.querySelector('.cam-context-pane').scrollTop = 150;
    f.mount.querySelector('.cam-context-pane .nested').scrollTop = 115;
    f.mount.querySelector('.cam-info-pane').scrollTop = 120;
    f.mount.querySelector('.cam-info-pane .nested').scrollTop = 85;
    f.releaseSave();
  });
  await page.waitForFunction(() => Boolean(searchFixture.releaseRefresh));
  await page.evaluate(() => {
    const f = searchFixture;
    const input = f.mount.querySelector('[data-field="right-note"]');
    input.focus({ preventScroll: true }); input.setSelectionRange(3, 8, 'backward');
    f.mount.querySelector('.cam-context-pane').scrollTop = 185;
    f.mount.querySelector('.cam-context-pane .nested').scrollTop = 130;
    f.mount.querySelector('.cam-info-pane').scrollTop = 135;
    f.mount.querySelector('.cam-info-pane .nested').scrollTop = 100;
    f.releaseRefresh();
  });
  await page.evaluate(() => searchFixture.action);
  const result = await page.evaluate(() => {
    const f = searchFixture;
    const input = f.mount.querySelector('[data-field="right-note"]');
    f.nodes[1].dispatchEvent(new Event('input', { bubbles: true }));
    const request = f.workflow.buildNestingRequest(f.view, f.context);
    return { saved: f.view.scene.tubeDesigner.nestingSettings.parameters, request: request.parameters,
      stableNodes: f.nodes.every(node => node.isConnected), inputEvents: f.inputEvents,
      focused: document.activeElement === input, selection: [input.selectionStart, input.selectionEnd, input.selectionDirection],
      text: input.value, conditional: !!f.mount.querySelector('[data-field="conditional"]'),
      scrolls: ['.cam-context-pane', '.cam-context-pane .nested', '.cam-info-pane', '.cam-info-pane .nested']
        .map(selector => f.mount.querySelector(selector).scrollTop),
      saving: f.view.tubeDesignerNestingSettingsSaving, dialog: f.view.tubeDesignerNestingSettingsDialog };
  });
  assert.deepEqual(result.saved, { partGap: 2, optimizationMode: 'utilization', searchTimeSeconds: 120 });
  assert.deepEqual(result.request, result.saved);
  assert.equal(result.stableNodes, true);
  assert.equal(result.inputEvents, 1);
  assert.equal(result.focused, true);
  assert.deepEqual(result.selection, [3, 8, 'backward']);
  assert.equal(result.text, 'latest right draft');
  assert.equal(result.conditional, true);
  assert.deepEqual(result.scrolls, [185, 130, 135, 100]);
  assert.equal(result.saving, false);
  assert.equal(result.dialog, '');
  assert.deepEqual(errors, []);
  await page.evaluate(() => searchFixture.settings.handleNestingSettingsRibbonCommand(searchFixture.context,
    searchFixture.view, 'nesting.parameters', searchFixture.ops));
  assert.equal(await mode.inputValue(), 'utilization');
  assert.equal(await time.inputValue(), '120');
  await time.fill('0');
  await page.getByRole('button', { name: '确定', exact: true }).click();
  await page.evaluate(() => searchFixture.action);
  assert.match(await page.getByRole('alert').textContent(), /1 至 3600/);
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ status: 'passed',
    scope: 'Actual Chrome DOM with production settings/workflow and floating-editor reconciliation; controlled native protocol fixture, no native deployment or standalone application.',
    checks: ['legacy defaults', 'mode recommendations', 'custom duration retained', 'draft control identity/focus/selection',
      'save/request parameters', 'asynchronous latest left/right/nested scroll and selection', 'conditional insertion',
      'canvas/input identities and listener', 'reopen persistence', 'invalid duration refusal'], result, errors }, null, 2));
  console.log('Tube nesting search strategy browser regression passed');
} finally { await browser.close(); }
