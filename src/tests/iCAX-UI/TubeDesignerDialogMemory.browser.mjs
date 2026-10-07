// Actual production dialogs and delegated actions; only native settings/profile transport is controlled.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = browserReportDirectory('tube-designer-dialog-memory-20261006');
const checks = [], errors = [];
const check = (actual, expected, name) => { assert.deepEqual(actual, expected, name); checks.push(name); };
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
let page;
try {
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  page.setDefaultTimeout(15000);
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://dialog-memory.test/**', route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><meta charset="utf-8"><body></body>' });
    if (!pathname.startsWith('/src/')) return route.abort();
    return route.fulfill({ contentType: pathname.endsWith('.css') ? 'text/css' : 'text/javascript',
      body: readFileSync(browserAssetPath(pathname.slice('/src/'.length)), 'utf8') });
  });
  await page.goto('http://dialog-memory.test/');
  await page.evaluate(async () => {
    localStorage.clear();
    const standard = await import('/src/apps/tube-designer/webpage/nestingStandardPart.mjs');
    const settings = await import('/src/apps/tube-designer/webpage/nestingSettings.mjs');
    const automation = await import('/src/apps/tube-designer/webpage/batchExcelAutomation.mjs');
    const { buildProfileGroups, listNestingParts } = await import('/src/apps/tube-designer/webpage/partsArea.mjs');
    const { bindTubeDesignerWindowMemory } = await import('/src/apps/tube-designer/webpage/windowStateMemory.mjs');
    const { patchParameterContent } = await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    const { ensureTubeDesignerStyles } = await import('/src/apps/tube-designer/webpage/styles/ensureStyles.mjs');
    document.body.classList.add('tube-designer-workspace');
    ensureTubeDesignerStyles();
    const style = document.createElement('style');
    style.textContent = `body{margin:0;font:14px Arial}#mount{height:100vh}.cam-workbench{display:grid;grid-template-columns:250px 1fr 320px;height:100vh}
      .cam-context-pane,.cam-info-pane{height:100vh;overflow:auto}.pane-spacer{height:1800px}.nested-scroll{height:130px;overflow:auto}
      #dialogs{grid-column:2}.tube-nesting-standard-part-fields{max-height:420px;overflow:auto}
      .tube-nesting-settings-body{max-height:450px;overflow:auto}`;
    document.head.append(style);
    document.body.insertAdjacentHTML('beforeend', `<div id="mount"><div class="cam-workbench">
      <aside class="cam-context-pane"><input name="left-search" value="保留焦点"><div class="nested-scroll"><div class="pane-spacer"></div></div><div class="pane-spacer"></div></aside>
      <main><div id="dialogs"></div></main><aside class="cam-info-pane"><input name="right-draft" value="右栏"><div class="nested-scroll"><div class="pane-spacer"></div></div><div class="pane-spacer"></div></aside>
    </div></div>`);
    const mount = document.querySelector('#mount'), dialogs = document.querySelector('#dialogs');
    const contours = [{ closed: true, edges: [
      { kind: 'line', start: [0, 0], end: [40, 0] }, { kind: 'line', start: [40, 0], end: [40, 40] },
      { kind: 'line', start: [40, 40], end: [0, 40] }, { kind: 'line', start: [0, 40], end: [0, 0] },
    ] }];
    const profile = (id, width, order) => ({ id, name: id === 'alpha' ? '方管 A' : '方管 B', profileForm: 'parametric',
      descriptor: { id, profileForm: 'parametric', catalog: { order }, parameters: [
        { key: 'width', valueType: 'number', defaultValue: width, min: 10, displayName: '宽度' },
        ...Array.from({ length: 14 }, (_, index) => ({ key: `extra${index}`, valueType: 'number', defaultValue: index + 1, displayName: `参数 ${index + 1}` })),
      ] }, previewProfile: { kind: 'rect', width, depth: 40, wallThickness: 2, parameters: { width }, contours } });
    const profiles = [profile('alpha', 40, 1), profile('beta', 24, 2)];
    const part = { entityId: 'part-1', name: '标准方管', length: 1000, quantity: 1,
      profile: { kind: 'rect', width: 40, depth: 40, wallThickness: 2, sectionIdentity: JSON.stringify({ schema: 'icax.nesting-section.v1', contours: ['memory-test-section'] }) },
      properties: { 'manufacturing.partKind': 'tube', 'manufacturing.sourcing': 'made' } };
    const baseScene = { tubeDesigner: { nestingGroups: [{ name: '零件', parts: [part] }] } };
    const group = buildProfileGroups(listNestingParts(baseScene.tubeDesigner))[0];
    if (!group) throw Error('Production nesting profile fixture was not eligible.');
    baseScene.tubeDesigner.nestingSettings = { version: 2, parameters: { partGap: 0 }, stocks: [{ profileKey: group.key,
      rows: [{ id: 'stock-row-1', length: 6000, quantity: -1 }, { id: 'stock-row-2', length: 4500, quantity: 5 }] }] };
    const f = window.fixture = { mount, dialogs, standard, settings, automation, profiles, baseScene, groupKey: group.key,
      calls: [], errors: [], holdSettings: false, holdEvaluation: false, renders: 0 };
    f.reset = projectId => {
      dialogs.replaceChildren();
      const oldController = f.controller;
      oldController?.refresh();
      f.view = { pending: false, activeAreaId: 'nesting', scene: structuredClone(baseScene),
        tubeDesignerSystemProfiles: structuredClone(profiles), tubeDesignerSelectedProfileId: 'system:alpha' };
      f.context = { mount, project: { projectId }, actions: { async refreshActiveSceneState() {} },
        productProxy: { async invoke(method, payload) {
          f.calls.push({ method, payload: structuredClone(payload ?? {}) });
          if (method === 'TubeDesigner.EvaluateProfilePackage') {
            if (f.holdEvaluation) await new Promise(resolve => { f.releaseEvaluation = resolve; });
            const source = profiles.find(profile => profile.id === payload.profileRef.id);
            return { profile: { ...structuredClone(source.previewProfile), parameters: payload.parameters } };
          }
          if (method === 'TubeDesigner.GetBatchExcelAutomationSettings') {
            if (f.holdSettings) await new Promise(resolve => { f.releaseSettings = resolve; });
            return { settings: { enabled: false, inputDirectory: 'D:/native/in', tempDirectory: 'D:/native/temp', outputDirectory: 'D:/native/out' } };
          }
          throw Error('Unexpected native product call ' + method);
        } }, sceneProxy: { async invoke(method) { throw Error('No mutation expected: ' + method); } } };
      f.ops = { renderProject() { f.render(); }, appendProjectLog() {}, showNotice() {} };
      f.render = () => {
        f.renders++;
        const html = standard.renderNestingStandardPartDialog(f.view)
          + settings.renderNestingSettingsDialogs(f.context, f.view) + automation.renderBatchExcelAutomationDialog(f.view);
        patchParameterContent(mount, dialogs, html);
        f.controller = bindTubeDesignerWindowMemory(f.context, f.view, f.ops);
      };
      f.render();
    };
    f.run = (action, target) => {
      const view = f.view, context = f.context, ops = f.ops;
      const operation = (async () => {
        for (const handler of [standard.handleNestingStandardPartAction, settings.handleNestingSettingsAction, automation.handleBatchExcelAutomationAction]) {
          const result = await handler(context, view, action, target, ops);
          if (result.handled) return result;
        }
        throw Error('Unknown production action ' + action);
      })();
      view.activeAreaAction = { action, promise: operation };
      void operation.catch(error => f.errors.push(error.message)).finally(() => {
        if (view.activeAreaAction?.promise === operation) view.activeAreaAction = null;
      });
      return operation;
    };
    mount.onclick = event => {
      const target = event.target.closest('[data-cam-action]');
      if (target && !target.disabled) void f.run(target.dataset.camAction, target);
    };
    mount.onchange = event => {
      const target = event.target.closest('[data-cam-change-action]');
      if (target && !target.disabled) void f.run(target.dataset.camChangeAction, target);
    };
    f.openStandard = () => standard.handleNestingStandardPartRibbonCommand(f.context, f.view, 'nesting.add-standard-part', f.ops);
    f.openSettings = kind => settings.handleNestingSettingsRibbonCommand(f.context, f.view, kind === 'stock' ? 'nesting.stock-settings' : 'nesting.parameters', f.ops);
    f.openAutomation = () => automation.handleBatchExcelAutomationRibbonCommand(f.context, f.view, 'designer.excel.automation-settings', f.ops);
    f.reset('project-A');
  });
  const settle = async () => {
    await page.waitForFunction(() => !fixture.view.pending && !fixture.view.activeAreaAction
      && !fixture.view.tubeDesignerExcelAutomationDialog?.loading);
    await page.evaluate(async () => {
      for (const dialog of fixture.dialogs.querySelectorAll('[role="dialog"]')) await fixture.controller.restoreWindow(dialog);
      await Promise.resolve();
    });
    await page.waitForFunction(() => !fixture.view.pending && !fixture.view.activeAreaAction);
  };
  const change = async (selector, value) => {
    await page.locator(selector).fill(String(value));
    await page.locator(selector).dispatchEvent('change');
    await settle();
  };
  const snapshot = () => page.evaluate(() => ({ profile: fixture.view.tubeDesignerNestingStandardPartDraft?.profileKey,
    width: fixture.view.tubeDesignerNestingStandardPartDraft?.parameters.width,
    length: fixture.view.tubeDesignerNestingStandardPartDraft?.length }));

  await page.evaluate(() => fixture.openStandard()); await settle();
  await change('[data-standard-part-parameter="width"]', 64);
  await change('[data-cam-change-action="tube-designer-nesting-standard-length"]', 1234);
  await page.locator('[data-cam-action="tube-designer-nesting-standard-cancel"]').last().click();
  await settle(); await page.evaluate(() => fixture.openStandard()); await settle();
  check(await snapshot(), { profile: 'system:alpha', width: 64, length: '1234' }, 'cancel/reopen restores dynamic profile parameter and length into production model');

  await page.locator('[data-cam-change-action="tube-designer-nesting-standard-profile-select"]').selectOption('system:beta'); await settle();
  check((await snapshot()).width, 24, 'a different profile starts with its own default');
  await change('[data-standard-part-parameter="width"]', 31);
  await change('[data-cam-change-action="tube-designer-nesting-standard-length"]', 1777);
  await page.locator('[data-cam-action="tube-designer-nesting-standard-cancel"]').last().click(); await settle();
  await page.evaluate(() => fixture.openStandard()); await settle();
  check(await snapshot(), { profile: 'system:beta', width: 31, length: '1777' }, 'last chosen profile is restored before its dependent parameters');
  await page.locator('[data-cam-change-action="tube-designer-nesting-standard-profile-select"]').selectOption('system:alpha'); await settle();
  check((await snapshot()).width, 64, 'profile A keeps its own parameter after profile B edits');

  await page.evaluate(() => { fixture.holdEvaluation = true; });
  await page.locator('[data-standard-part-parameter="width"]').fill('66');
  await page.locator('[data-standard-part-parameter="width"]').dispatchEvent('change');
  await page.waitForFunction(() => !!fixture.releaseEvaluation);
  const interaction = await page.evaluate(() => {
    const left = document.querySelector('.cam-context-pane'), right = document.querySelector('.cam-info-pane');
    const input = left.querySelector('input'); input.focus(); input.setSelectionRange(1, 3);
    left.scrollTop = 211; right.scrollTop = 317;
    left.querySelector('.nested-scroll').scrollTop = 57; right.querySelector('.nested-scroll').scrollTop = 89;
    fixture.interactionNodes = { input, left, right };
    return { focus: input.name, selection: [input.selectionStart, input.selectionEnd], scroll: [left.scrollTop, right.scrollTop, left.querySelector('.nested-scroll').scrollTop, right.querySelector('.nested-scroll').scrollTop] };
  });
  await page.evaluate(() => { fixture.holdEvaluation = false; fixture.releaseEvaluation(); }); await settle();
  check(await page.evaluate(() => {
    const { input, left, right } = fixture.interactionNodes;
    return { focus: document.activeElement.name, selection: [input.selectionStart, input.selectionEnd], scroll: [left.scrollTop, right.scrollTop, left.querySelector('.nested-scroll').scrollTop, right.querySelector('.nested-scroll').scrollTop] };
  }), interaction, 'delayed profile response and window hydration preserve latest focus, selection and both sidebar scrolls');
  check(await page.evaluate(() => fixture.interactionNodes.input === document.querySelector('.cam-context-pane input')), true, 'sidebar input node is retained');
  await page.locator('[data-cam-action="tube-designer-nesting-standard-cancel"]').last().click(); await settle();
  await page.evaluate(() => { fixture.reset('project-A'); return fixture.openStandard(); }); await settle();
  check(await snapshot(), { profile: 'system:alpha', width: 66, length: '1777' }, 'new view/context reads last form values from persistent memory');
  await page.locator('[data-cam-action="tube-designer-nesting-standard-cancel"]').last().click(); await settle();

  await page.evaluate(() => fixture.openSettings('parameters')); await settle();
  await change('[data-tube-nesting-parameter="partGap"]', 2.5);
  await page.locator('[data-cam-action="tube-designer-nesting-settings-cancel"]').last().click(); await settle();
  await page.evaluate(() => fixture.openSettings('stock')); await settle();
  await change('[data-row-id="stock-row-1"][data-field="length"]', 6500);
  await change('[data-row-id="stock-row-2"][data-field="length"]', 3200);
  await change('[data-row-id="stock-row-2"][data-field="quantity"]', 7);
  await page.locator('[data-cam-action="tube-designer-nesting-settings-cancel"]').last().click(); await settle();
  await page.evaluate(() => fixture.openSettings('parameters')); await settle();
  check(await page.locator('[data-tube-nesting-parameter="partGap"]').inputValue(), '2.5', 'settings kind scopes share a title but retain separate parameter values');
  await page.locator('[data-cam-action="tube-designer-nesting-settings-cancel"]').last().click(); await settle();
  await page.evaluate(() => fixture.openSettings('stock')); await settle();
  check(await page.evaluate(() => fixture.view.tubeDesignerNestingStockDraft[0].rows.map(row => [String(row.length), String(row.quantity)])),
    [['6500', '-1'], ['3200', '7']], 'two stock rows retain independent values');
  await page.locator('[data-cam-action="tube-designer-nesting-settings-cancel"]').last().click(); await settle();
  await page.evaluate(() => { fixture.reset('project-B'); fixture.openSettings('parameters'); }); await settle();
  check(await page.locator('[data-tube-nesting-parameter="partGap"]').inputValue(), '0', 'another project does not inherit unsaved nesting settings');
  await page.locator('[data-cam-action="tube-designer-nesting-settings-cancel"]').last().click(); await settle();
  await page.evaluate(() => { fixture.reset('project-A'); fixture.openSettings('stock'); }); await settle();
  check(await page.evaluate(() => fixture.view.tubeDesignerNestingStockDraft[0].rows.map(row => [String(row.length), String(row.quantity)])),
    [['6500', '-1'], ['3200', '7']], 'new context restores matching project stock drafts');
  check(await page.evaluate(() => fixture.view.scene.tubeDesigner.nestingSettings.stocks[0].rows.map(row => row.length)), [6000, 4500], 'restoring draft fields does not save or rewrite scene settings');
  await page.locator('[data-cam-action="tube-designer-nesting-settings-cancel"]').last().click(); await settle();

  await page.evaluate(() => fixture.openAutomation()); await settle();
  await page.locator('[data-excel-automation-field="enabled"]').check();
  await page.locator('[data-excel-automation-field="inputDirectory"]').fill('D:/remembered/in');
  await page.locator('[data-excel-automation-field="tempDirectory"]').fill('D:/remembered/temp');
  await page.locator('[data-excel-automation-field="outputDirectory"]').fill('D:/remembered/out');
  await page.locator('[data-cam-action="tube-designer-excel-automation-cancel"]').last().click(); await settle();
  await page.evaluate(() => { fixture.holdSettings = true; void fixture.openAutomation(); });
  await page.waitForFunction(() => !!fixture.releaseSettings);
  check(await page.locator('[data-excel-automation-field="inputDirectory"]').isDisabled(), true, 'asynchronous native settings load suspends restoration');
  await page.evaluate(() => { fixture.holdSettings = false; fixture.releaseSettings(); }); await settle();
  check(await page.evaluate(() => fixture.view.tubeDesignerExcelAutomationDialog.draft),
    { enabled: true, inputDirectory: 'D:/remembered/in', tempDirectory: 'D:/remembered/temp', outputDirectory: 'D:/remembered/out' },
    'remembered automation controls update draft after native GET completes');
  check(await page.evaluate(() => fixture.view.tubeDesignerExcelAutomationSettings.enabled), false, 'restoring enabled checkbox does not enable native service');
  check(await page.evaluate(() => fixture.calls.filter(call => /Save|AddNesting/.test(call.method)).length), 0, 'memory restoration performs no native save or part creation');
  check(await page.evaluate(() => fixture.errors), [], 'delegated production handlers complete without errors');
  check(errors, [], 'browser has no uncaught errors');
  await page.screenshot({ path: resolve(output, 'automation-restored.png'), fullPage: true });
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ status: 'passed', checks,
    browserEngine: 'Playwright Chromium', productionDialogs: ['standard-part', 'nesting-stock', 'nesting-parameters', 'excel-automation'],
    nativeTransport: 'controlled protocol fixtures', standaloneCefEndToEnd: false, nativeSettingsPersisted: false }, null, 2));
  console.log(`TubeDesigner dialog memory: passed (${checks.length} browser checks).`);
} catch (error) {
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ status: 'failed', checks, errors, error: { message: error.message, stack: error.stack } }, null, 2));
  if (page) await page.screenshot({ path: resolve(output, 'failure.png'), fullPage: true }).catch(() => {});
  throw error;
} finally { await browser.close(); }
