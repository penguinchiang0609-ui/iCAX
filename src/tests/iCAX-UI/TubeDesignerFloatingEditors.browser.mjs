// Real Chromium/WebGL component regression. Project saves use a controlled
// transport fixture; this test makes no native or standalone CEF claims.
import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
const reportDirectory = browserReportDirectory('tube-designer-floating-editors');
const results = [];
try {
  const page = await browser.newPage({ viewport: { width: 1600, height: 950 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://floating-editors.test/**', serveBrowserAsset);
  await page.goto('http://floating-editors.test/');
  await page.addStyleTag({ content: readBrowserAsset('apps/_shared/workbench/styles/laser3dcam.css') + `
    *{box-sizing:border-box}body{margin:0;font-family:Segoe UI,sans-serif}
    #app{width:1600px;height:950px}.cam-workbench{position:relative;display:grid!important;grid-template-areas:none!important;grid-template-columns:230px 950px 420px!important;grid-template-rows:950px!important;height:950px!important;min-height:0}
    .cam-context-pane,.cam-info-pane{grid-area:auto!important;grid-row:1!important;display:block!important;height:950px;min-height:0;overflow:auto!important}
    .cam-context-pane{grid-column:1!important}.cam-info-pane{grid-column:3!important}
    .cam-viewport{grid-area:auto!important;grid-column:2!important;grid-row:1!important;position:relative;height:950px;min-width:0;min-height:0}.fixture-space{height:1600px}
    .fixture-nested{height:105px;overflow:auto}.fixture-nested>div{height:850px}
    .fixture-editor{height:44px;margin:8px;width:90%}
    .fixture-with-results .tube-designer-dock-bottom-host{position:absolute!important;left:230px;right:420px;bottom:0;height:120px}
    .fixture-with-results .tube-designer-bottom-splitter{display:none}
  ` });
  await page.evaluate(async () => {
    const floating = await import('/src/apps/tube-designer/webpage/floatingEditorDom.mjs');
    const library = await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    const nesting = await import('/src/apps/tube-designer/webpage/nestingSettings.mjs');
    const views = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const parts = await import('/src/apps/tube-designer/webpage/partsArea.mjs');
    const workflow = await import('/src/apps/tube-designer/webpage/nestingWorkflow.mjs');
    const { capturePaneInteraction } = await import('/src/apps/_shared/workbench/utils/paneInteractionState.mjs');
    const { ensureTubeDesignerStyles } = await import('/src/apps/tube-designer/webpage/styles/ensureStyles.mjs');
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    ensureTubeDesignerStyles();
    const f = window.fixture = { floating, library, nesting, views, parts, workflow, patches: 0, saves: [], events: { input: 0, canvas: 0 }, stages: [] };
    f.check = (condition, message) => { if (!condition) throw Error(message); };
    f.rect = node => { const b = node.getBoundingClientRect(); return [b.x, b.y, b.width, b.height]; };
    f.cameraState = () => JSON.stringify(f.viewport.getCameraState());
    f.frameHash = () => {
      // Read the actual WebGL framebuffer after a render; object identity alone
      // would miss a mesh that survived reconciliation but became invisible.
      const renderer = f.viewport.renderer; renderer.render(f.viewport.scene, f.viewport.camera);
      const gl = renderer.getContext(), width = gl.drawingBufferWidth, height = gl.drawingBufferHeight;
      const pixels = new Uint8Array(width * height * 4); gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      let hash = 2166136261;
      for (let i = 0; i < pixels.length; i += 44) hash = Math.imul(hash ^ pixels[i] ^ (pixels[i + 1] << 8) ^ (pixels[i + 2] << 16), 16777619);
      return hash >>> 0;
    };
    f.leftHtml = () => '<input class="fixture-editor" data-fixture-input="left" value="left"><div class="fixture-nested" data-fixture-scroll="left"><div>Nested left</div></div><div class="fixture-space">Left tree</div>' + (f.actualNesting ? parts.renderNestingLeftPane(f.context, f.view) : '');
    f.rightHtml = () => '<input class="fixture-editor" data-fixture-input="right" value="right">' +
      (f.condition ? '<input class="fixture-editor" data-fixture-input="conditional" value="conditional">' : '') +
      '<div class="fixture-nested" data-fixture-scroll="right"><div>Nested right</div></div><div class="fixture-space">Right parameters</div>' + (f.actualNesting ? parts.renderNestingRightPane(f.context, f.view) : '');
    f.suffix = () => f.view.activeAreaId === 'nesting'
      ? (f.actualNesting ? parts.renderNestingResultDock(f.context, f.view) : '') + nesting.renderNestingSettingsDialogs(f.context, f.view)
      : views.renderDesignerDialogs(f.view.scene.tubeDesigner, f.view);
    f.scrollNodes = () => ['.cam-context-pane', '.cam-info-pane', '[data-fixture-scroll="left"]', '[data-fixture-scroll="right"]'].map(s => f.mount.querySelector(s));
    f.setScroll = seed => f.scrollNodes().forEach((node, i) => { node.scrollTop = seed + i * 17; });
    f.render = () => {
      const suffix = f.suffix();
      const restore = capturePaneInteraction(f.mount);
      const patched = f.view.activeAreaId === 'nesting'
        ? floating.patchFloatingEditorDom(f.view, f.mount, { suffix, sceneProxy: f.context.sceneProxy, left: f.leftHtml(), right: f.rightHtml(), overlay: f.actualNesting ? () => parts.renderNestingViewportOverlay(f.context, f.view) : '' })
        : library.patchLibraryDom(f.view, f.mount, { left: f.leftHtml(), right: f.rightHtml() + (f.popupInRight ? suffix : ''), overlay: '', suffix: f.popupInRight ? '' : suffix, sceneProxy: f.context.sceneProxy });
      f.check(patched, 'Popup update fell through to full workbench rendering: ' + f.view.activeAreaId + ' / ' + f.stages.at(-1));
      restore();
      f.patches++;
      floating.bindFloatingEditorWindows(f.mount, f.view);
      f.assertScene('popup update');
    };
    f.assertScene = stage => {
      f.check(f.mount.querySelector('canvas') === f.canvas && f.viewport.renderer.domElement === f.canvas, stage + ': canvas replaced');
      f.check(f.viewport.camera === f.camera, stage + ': camera object replaced');
      f.check(f.viewport.sceneObjects.get('fixture-box') === f.mesh && f.viewport.geometryObjects.get('fixture-box') === f.geometry, stage + ': scene object/geometry replaced');
      f.check(f.mesh.visible && f.mesh.parent === f.viewport.content && f.mesh.geometry === f.geometry && f.mesh.material === f.material, stage + ': displayed geometry/material hidden or detached');
      f.check(JSON.stringify(f.viewport.domListeners) === f.listeners && f.viewport.domListeners.every((tuple, i) => tuple === f.listenerRefs[i]), stage + ': viewport listeners replaced');
      f.check(JSON.stringify(f.rect(f.canvas)) === f.canvasRect, stage + ': canvas dimensions/position changed');
      f.check(f.cameraState() === f.expectedCamera, stage + ': camera state reset');
      f.check(f.frameHash() === f.expectedPixels, stage + ': rendered geometry frame changed');
      f.check(f.leftInput === f.mount.querySelector('[data-fixture-input="left"]') && f.rightInput === f.mount.querySelector('[data-fixture-input="right"]'), stage + ': sidebar input replaced');
    };
    f.setup = async area => {
      f.viewport?.dispose();
      f.actualNesting = false; f.asyncError = '';
      f.popupInRight = area === 'profiles';
      f.condition = true;
      const profiles = Array.from({ length: 22 }, (_, i) => ({ id: 'profile-' + i, name: '我的管型 ' + String(i).padStart(2, '0'), sourceFileName: 'test.dxf', contourCount: 2, specification: '60 × 40 × 2' }));
      const parts = Array.from({ length: 14 }, (_, i) => ({ entityId: 'part-' + i, name: '零件 ' + i, length: 500, quantity: 1,
        profile: { id: 'rect', kind: 'rect', displayName: '方管', width: 40 + i, depth: 20, wallThickness: 2, hollow: true,
          sectionIdentity: JSON.stringify({ schema: 'icax.nesting-section.v1', contours: [JSON.stringify({ edges: ['browser-section-' + i] })] }) } }));
      f.view = { activeAreaId: area, pending: false, scene: { tubeDesigner: {
        templates: [{ id: 'fixture-template', name: '浏览器产品', version: '1.0.0', parameters: [], groups: [] }],
        product: { entityId: 'fixture-product', templateId: 'fixture-template', parameters: {} },
        members: [{ entityId: 'fixture-member', previewGeometryResourceId: 'fixture-box', previewGeometryResourceVersion: 1, transform: [1, 0, 0] }],
        manufacturingGroups: [{ productEntityId: 'fixture-product', parts }], nestingSettings: {},
      } }, tubeDesignerUserData: { customers: [], parameterPresets: [], profiles } };
      f.context = { sceneProxy: { async invoke(method, payload) {
        f.check(method === 'TubeDesigner.SaveNestingSettings', 'Unexpected transport: ' + method);
        f.saves.push(structuredClone(payload.settings));
        if (f.holdSave) { f.saveWaiting = true; try { await new Promise((resolve, reject) => { f.releaseSave = resolve; f.failSave = reject; }); } finally { f.saveWaiting = false; f.holdSave = false; } }
        return { settings: structuredClone(payload.settings) };
      } }, actions: { async refreshActiveSceneState() {} } };
      document.body.innerHTML = '<main id="app" class="tube-designer-workspace"><div class="cam-workbench"><aside class="cam-context-pane">' + f.leftHtml() + '</aside><section class="cam-viewport"></section><aside class="cam-info-pane">' + f.rightHtml() + '</aside></div></main>';
      f.mount = document.querySelector('#app'); f.context.mount = f.mount;
      f.viewport = createThreeViewport({ continuousRender: false, showGrid: false, pickingEnabled: false, projectionMode: 'perspective' });
      f.view.viewport = f.viewport;
      f.viewport.mount(f.mount.querySelector('.cam-viewport'));
      const box = new THREE.BoxGeometry(500, 60, 40);
      f.viewport.resourcePromises.set('fixture-box@1', Promise.resolve({ url: 'fixture-box', version: 1, type: 'geometry', data: {
        kind: 'mesh', positions: Array.from(box.attributes.position.array), normals: Array.from(box.attributes.normal.array), indices: Array.from(box.index.array),
      } })); box.dispose();
      await f.viewport.applyViewSnapshot({ revision: 'fixture', rows: [{ entityId: 'fixture-box', data: { geometry: { url: 'fixture-box', version: 1 }, flags: 3 } }] }, { get() { throw Error('Unexpected resource request'); } });
      f.viewport.fitView(1.6);
      await new Promise(resolve => requestAnimationFrame(resolve));
      f.canvas = f.viewport.renderer.domElement; f.camera = f.viewport.camera;
      f.mesh = f.viewport.sceneObjects.get('fixture-box'); f.geometry = f.viewport.geometryObjects.get('fixture-box');
      f.material = f.mesh.material;
      f.check(f.mesh && f.geometry && f.viewport.renderer.getContext() instanceof WebGL2RenderingContext, 'Actual WebGL scene was not created');
      f.listenerRefs = [...f.viewport.domListeners]; f.listeners = JSON.stringify(f.viewport.domListeners);
      f.canvasRect = JSON.stringify(f.rect(f.canvas)); f.expectedCamera = f.cameraState();
      f.expectedPixels = f.frameHash();
      f.leftInput = f.mount.querySelector('[data-fixture-input="left"]'); f.rightInput = f.mount.querySelector('[data-fixture-input="right"]');
      f.events = { input: 0, canvas: 0 };
      f.rightInput.addEventListener('input', () => f.events.input++); f.canvas.addEventListener('pointerdown', () => f.events.canvas++);
      floating.rememberFloatingEditorDom(f.view, f.mount, { suffix: f.suffix(), sceneProxy: f.context.sceneProxy });
      library.rememberLibraryDom(f.view, f.mount, f.suffix(), f.context.sceneProxy);
      floating.bindFloatingEditorWindows(f.mount, f.view);
      f.ops = { renderProject() { f.render(); } };
      f.mount.addEventListener('click', event => {
        const target = event.target.closest('[data-cam-action]'); if (!target) return;
        const action = target.dataset.camAction; f.lastAction = action;
        if (action.startsWith('tube-designer-nesting-')) nesting.handleNestingSettingsAction(f.context, f.view, action, target, f.ops).catch(error => { f.asyncError = error.message; });
        else if (action.startsWith('tube-designer-parts-')) parts.handlePartsAreaAction(f.context, f.view, action, target, f.ops).catch(error => { f.asyncError = error.message; });
        else if (action === 'tube-designer-close-preset-dialog') { f.view.tubeDesignerPresetDialog = null; f.render(); }
        else if (action === 'tube-designer-close-profile-dialog') { f.view.tubeDesignerProfileDialog = null; f.render(); }
        else if (action === 'tube-designer-close-profile-library') { f.view.tubeDesignerProfileLibraryDialog = false; f.render(); }
      });
      f.mount.addEventListener('change', event => {
        if (event.target.dataset.camChangeAction?.startsWith('tube-designer-nesting-')) nesting.handleNestingSettingsAction(f.context, f.view, event.target.dataset.camChangeAction, event.target, f.ops);
        else if (event.target.dataset.camChangeAction === 'tube-designer-parts-search') parts.handlePartsAreaAction(f.context, f.view, event.target.dataset.camChangeAction, event.target, f.ops);
      });
    };
    f.openNesting = async kind => { f.stages.push('open ' + kind); await nesting.handleNestingSettingsRibbonCommand(f.context, f.view, kind === 'stock' ? 'nesting.stock-settings' : 'nesting.parameters', f.ops); };
    f.snapshotInteraction = () => ({ scroll: f.scrollNodes().map(node => node.scrollTop), value: f.rightInput.value,
      selection: [f.rightInput.selectionStart, f.rightInput.selectionEnd, f.rightInput.selectionDirection] });
    f.assertInteraction = expected => { f.check(document.activeElement === f.rightInput, 'Sidebar focus lost');
      f.check(f.rightInput.value === expected.value, 'Current text draft overwritten');
      f.check(JSON.stringify([f.rightInput.selectionStart, f.rightInput.selectionEnd, f.rightInput.selectionDirection]) === JSON.stringify(expected.selection), 'Current text selection changed');
      f.check(JSON.stringify(f.scrollNodes().map(node => node.scrollTop)) === JSON.stringify(expected.scroll), 'Latest independent/nested sidebar scroll changed'); };
    await f.setup('nesting');
  });
  const scene = async stage => page.evaluate(stage => window.fixture.assertScene(stage), stage);
  const screenshot = async name => page.screenshot({ path: resolve(reportDirectory, name + '.png') });
  const panel = key => page.locator('[data-floating-editor-window="' + key + '"]');
  const geometry = async node => node.evaluate(element => { const b = element.getBoundingClientRect(); return [b.x, b.y, b.width, b.height]; });
  const assertWindowRect = (actual, expected, message) => assert.ok(actual.every((value, i) => Math.abs(value - expected[i]) < 0.1), message + ' ' + JSON.stringify({ actual, expected }));
  const dragResize = async key => {
    const window = panel(key), initial = await geometry(window);
    const header = await window.locator(':scope > header').boundingBox();
    await page.mouse.move(header.x + 70, header.y + 20); await page.mouse.down();
    await page.mouse.move(header.x + 110, header.y + 55, { steps: 6 }); await page.mouse.up();
    const moved = await geometry(window);
    // Tall lists can clamp the final vertical position at the viewport edge.
    assert.ok(Math.abs(moved[0] - initial[0] - 40) < 2 && moved[1] > initial[1] + 5, key + ': title drag did not move the window ' + JSON.stringify({ initial, moved }));
    const handle = await window.locator('[data-floating-editor-resize="se"]').boundingBox();
    await page.mouse.move(handle.x + handle.width / 2, handle.y + handle.height / 2); await page.mouse.down();
    await page.mouse.move(handle.x + handle.width / 2 - 55, handle.y + handle.height / 2 - 90, { steps: 6 }); await page.mouse.up();
    const resized = await geometry(window);
    assert.ok(resized[2] < moved[2] - 45 && resized[3] < moved[3] - 75, key + ': resize handle did not resize the window ' + JSON.stringify({ moved, resized }));
    await scene(key + ' drag/resize');
    await page.evaluate(() => window.fixture.render());
    assertWindowRect(await geometry(window), resized, key + ': window rectangle changed on content patch');
    return resized;
  };
  const outsideOrbit = async () => {
    const before = await page.evaluate(() => window.fixture.cameraState());
    const point = await page.evaluate(() => {
      const f = window.fixture, b = f.canvas.getBoundingClientRect();
      for (let y = b.top + 80; y < b.bottom - 80; y += 40) for (let x = b.left + 80; x < b.right - 80; x += 40)
        if (document.elementFromPoint(x, y) === f.canvas && document.elementFromPoint(x + 35, y + 20) === f.canvas) return { x, y };
      throw Error('No canvas pointer target outside floating window');
    });
    await page.mouse.move(point.x, point.y); await page.mouse.down({ button: 'right' });
    await page.mouse.move(point.x + 35, point.y + 20, { steps: 5 }); await page.mouse.up({ button: 'right' });
    const after = await page.evaluate(() => { const f = window.fixture; f.expectedCamera = f.cameraState(); f.expectedPixels = f.frameHash(); return { state: f.expectedCamera, pointers: f.events.canvas }; });
    assert.notEqual(after.state, before, 'Orbit gesture outside window did not reach actual viewport');
    assert.ok(after.pointers > 0, 'Original canvas pointer listener was detached');
    await scene('outside pointer orbit');
  };
  const transparent = async () => assert.equal(await page.evaluate(() => Array.from(document.querySelectorAll('[data-floating-editor-layer]')).every(layer => {
    const css = getComputedStyle(layer); return css.backgroundColor === 'rgba(0, 0, 0, 0)' && css.pointerEvents === 'none' && css.backdropFilter === 'none';
  })), true, 'Window layer must be transparent and allow outside interaction');

  await page.evaluate(() => window.fixture.openNesting('stock'));
  assert.equal(await panel('nesting-settings-stock').getAttribute('aria-modal'), 'false');
  await transparent(); await screenshot('stock-transparent-backdrop');
  const stockRect = await dragResize('nesting-settings-stock');
  await outsideOrbit();
  // This is a real delayed project-save response while sidebar input and four
  // independent scroll containers continue to change after the request starts.
  await page.evaluate(() => { const f = window.fixture; f.holdSave = true; f.stages.push('delayed save failure'); });
  await panel('nesting-settings-stock').locator('[data-cam-action="tube-designer-nesting-stock-save"]').click();
  await page.waitForFunction(() => window.fixture.saveWaiting || window.fixture.asyncError);
  await page.evaluate(() => window.fixture.check(!window.fixture.asyncError, window.fixture.asyncError));
  await page.evaluate(() => { const f = window.fixture; f.rightInput.focus({ preventScroll: true }); f.rightInput.value = 'typing after request'; f.rightInput.setSelectionRange(3, 11, 'backward'); f.setScroll(420); f.latest = f.snapshotInteraction(); f.failSave(Error('controlled save failure')); });
  await page.waitForFunction(() => !window.fixture.saveWaiting && !window.fixture.view.tubeDesignerNestingSettingsSaving);
  await page.evaluate(() => { const f = window.fixture; f.check(!f.asyncError, f.asyncError); f.assertInteraction(f.latest); f.assertScene('delayed save failure'); });
  assertWindowRect(await geometry(panel('nesting-settings-stock')), stockRect, 'Stock async patch rectangle changed');
  assert.equal(await panel('nesting-settings-stock').locator('[role="alert"]').textContent(), 'controlled save failure');
  // Adding and removing actual stock rows must retain a different row's input
  // node and listener, not only preserve the canvas.
  await page.evaluate(() => { const f = window.fixture; f.stockInput = f.mount.querySelector('[data-tube-nesting-stock-field]'); f.stockEvents = 0; f.stockInput.addEventListener('input', () => f.stockEvents++); });
  const add = panel('nesting-settings-stock').locator('[data-cam-action="tube-designer-nesting-stock-add"]').first();
  await add.click();
  await page.evaluate(() => { const f = window.fixture; f.check(f.stockInput === f.mount.querySelector('[data-tube-nesting-stock-field]'), 'Stock input replaced on row insertion'); f.stockInput.dispatchEvent(new Event('input', { bubbles: true })); f.check(f.stockEvents === 1, 'Stock input listener detached'); });
  await panel('nesting-settings-stock').locator('[data-cam-action="tube-designer-nesting-stock-remove"]').nth(1).click();
  await scene('stock row removal');
  await panel('nesting-settings-stock').getByRole('button', { name: '关闭母材设置', exact: true }).click();
  assert.equal(await panel('nesting-settings-stock').count(), 0); await scene('stock close');
  await page.evaluate(() => window.fixture.openNesting('parameters'));
  await transparent(); await dragResize('nesting-settings-parameters'); await outsideOrbit();
  await panel('nesting-settings-parameters').locator('[data-tube-nesting-parameter]').fill('2.5');
  await panel('nesting-settings-parameters').getByRole('button', { name: '确定', exact: true }).click();
  await page.waitForFunction(() => !window.fixture.view.tubeDesignerNestingSettingsDialog && !window.fixture.view.tubeDesignerNestingSettingsSaving);
  await page.evaluate(() => { const f = window.fixture; f.check(!f.asyncError, f.asyncError); f.assertScene('successful settings save'); f.check(f.view.scene.tubeDesigner.nestingSettings.parameters.partGap === 2.5, 'Settings were not committed'); });
  results.push({ area: 'nesting', stockRows: true, parameters: true, projectSaveFixture: true, canvasCameraGeometryIdentity: true, renderedFrameAndMaterialPreserved: true, outsideOrbit: true, transparentLayer: true, latestAsyncInteraction: true });

  // Exercise the ordinary workbench around an open editor using the actual
  // nesting list/inspector/result/HUD renderers. Saving settings changes the
  // stale-result presentation and scene history metadata, not its geometry.
  await page.evaluate(async () => {
    const f = window.fixture; await f.setup('nesting'); f.actualNesting = true;
    f.mount.classList.add('fixture-with-results');
    f.view.tubeDesignerNestingSelectionKind = 'plan'; f.view.tubeDesignerActiveNestingPlanId = 'stock-plan';
    f.view.tubeDesignerNestingResult = { revision: 'existing-plan', inputSignature: f.workflow.getNestingInputSignature(f.view, f.context),
      plans: [{ id: 'stock-plan', profile: '方管 40 × 20 × 2', stockLength: 6000, usedLength: 500, partCount: 1, utilization: 1 / 12,
        placements: [{ partId: 'part-0', start: 0, length: 500 }], detailsLoaded: false }] };
    f.mount.querySelector('.cam-context-pane').insertAdjacentHTML('beforeend', f.parts.renderNestingLeftPane(f.context, f.view));
    f.mount.querySelector('.cam-info-pane').insertAdjacentHTML('beforeend', f.parts.renderNestingRightPane(f.context, f.view));
    f.mount.querySelector('.cam-viewport').insertAdjacentHTML('beforeend', f.parts.renderNestingViewportOverlay(f.context, f.view));
    f.mount.querySelector('.cam-workbench').insertAdjacentHTML('beforeend', f.parts.renderNestingResultDock(f.context, f.view));
    f.context.actions.refreshActiveSceneState = async () => {
      f.view.scene = structuredClone(f.view.scene);
      f.view.scene.repositoryRevision = 2; f.view.scene.history = { undo: [{ operation: 'save nesting settings' }] };
      f.view.scene.tubeDesigner.manufacturingGroups[0].name = 'Refreshed metadata';
    };
    f.floating.rememberFloatingEditorDom(f.view, f.mount, { suffix: f.suffix(), sceneProxy: f.context.sceneProxy });
    await f.openNesting('parameters');
    f.check(!f.mount.querySelector('.tube-designer-nesting-warning'), 'Existing result unexpectedly started stale');
  });
  await panel('nesting-settings-parameters').locator('[data-tube-nesting-parameter]').fill('3');
  await panel('nesting-settings-parameters').getByRole('button', { name: '确定', exact: true }).click();
  await page.waitForFunction(() => !window.fixture.view.tubeDesignerNestingSettingsSaving);
  await page.evaluate(() => {
    const f = window.fixture; f.check(!f.asyncError, f.asyncError); f.assertScene('saved settings with refreshed history clone');
    f.check(f.mount.querySelector('.tube-designer-nesting-warning')?.textContent.includes('重新计算'), 'Old result warning was not patched after settings save');
    f.check(f.mount.querySelector('.tube-designer-cutting-scene-title')?.textContent.includes('旧排样结果'), 'Scene HUD did not show stale result');
    f.check(f.mount.querySelector('.tube-designer-cutting-status')?.textContent === '需重新排样', 'Existing plan inspector did not become stale');
    f.check(f.view.scene.repositoryRevision === 2, 'Scene refresh metadata fixture was not applied');
  });
  // Search/filter and checkbox selection stay functional while the popup is
  // open. These HTML changes must not disappear behind the popup-only path.
  await page.evaluate(() => window.fixture.openNesting('stock'));
  await page.evaluate(async () => {
    const f = window.fixture;
    await f.parts.handlePartsAreaAction(f.context, f.view, 'tube-designer-parts-search', { value: '零件 1' }, f.ops);
    f.check(f.mount.querySelector('[data-cam-change-action="tube-designer-parts-search"]').value === '零件 1', 'Search update was swallowed');
    const visible = [...f.mount.querySelectorAll('.tube-designer-cutting-part')];
    f.check(visible.length > 0 && visible.length < 14, 'Search did not update rendered part list');
    f.view.tubeDesignerSelectedPartIds = ['part-1'];
    await f.parts.handlePartsAreaAction(f.context, f.view, 'tube-designer-parts-filter', { dataset: { tubeDesignerPartFilter: 'selected' } }, f.ops);
    f.check(f.mount.querySelector('[data-tube-designer-part-filter="selected"]').getAttribute('aria-pressed') === 'true', 'Filter button update was swallowed');
    f.check(f.mount.querySelector('.tube-designer-cutting-pane-header').textContent.includes('1 / 14'), 'Selection total did not update');
    f.assertScene('search filter and checkbox selection');
    let overlays = 0;
    const options = () => ({ suffix: f.suffix(), left: f.leftHtml(), right: f.rightHtml(), sceneProxy: f.context.sceneProxy, overlay() { overlays++; return ''; } });
    const expectRejected = (mutate, restore, label) => {
      mutate(); f.check(f.floating.patchFloatingEditorDom(f.view, f.mount, options()) === false, label + ': changed workbench identity was swallowed');
      f.check(overlays === 0, label + ': geometry hydration ran before guard'); restore();
    };
    const oldSelection = f.view.tubeDesignerNestingSelectionKind;
    expectRejected(() => { f.view.tubeDesignerNestingSelectionKind = 'part'; f.view.tubeDesignerActivePartId = 'part-1'; },
      () => { f.view.tubeDesignerNestingSelectionKind = oldSelection; delete f.view.tubeDesignerActivePartId; }, 'active scene selection');
    expectRejected(() => { f.view.scene.tubeDesigner.members[0].previewGeometryResourceVersion = 2; },
      () => { f.view.scene.tubeDesigner.members[0].previewGeometryResourceVersion = 1; }, 'new model geometry');
    const priorArea = f.view.activeAreaId;
    expectRejected(() => { f.view.activeAreaId = 'profiles'; }, () => { f.view.activeAreaId = priorArea; }, 'page navigation');
    f.assertScene('rejected legitimate viewport lifecycle changes');
  });
  results.push({ area: 'nesting', actualNestingWorkbenchRenderers: true, staleResultAfterSave: true, clonedSceneHistoryMetadata: true, renderedFrameAndMaterialPreserved: true,
    searchFilterCheckboxUpdates: true, resourceAndActiveSelectionGate: true, hydrationGuardOrder: true });

  for (const area of ['view', 'profiles']) {
    await page.evaluate(area => window.fixture.setup(area), area);
    for (const kind of ['product-preset', 'imported-profile', 'imported-profile-library']) {
      await page.evaluate(kind => { const f = window.fixture; f.stages.push('open ' + kind);
        if (kind === 'product-preset') f.view.tubeDesignerPresetDialog = { mode: 'right', scopeKey: 'materials', name: 'Fixture name' };
        if (kind === 'imported-profile') f.view.tubeDesignerProfileDialog = { name: 'Fixture profile', profile: { name: 'DXF', specification: '60 × 40 × 2' } };
        if (kind === 'imported-profile-library') f.view.tubeDesignerProfileLibraryDialog = true;
        f.render();
      }, kind);
      assert.equal(await panel(kind).getAttribute('aria-modal'), 'false');
      await transparent();
      if (area === 'view') await screenshot(kind + '-transparent-backdrop');
      const resized = await dragResize(kind); await outsideOrbit();
      await page.evaluate(async kind => {
        const f = window.fixture, popup = f.mount.querySelector('[data-floating-editor-window="' + kind + '"]');
        const input = popup.querySelector('input[type="text"]'); f.popupInput = input;
        let changes = 0; input.addEventListener('input', () => changes++);
        input.focus({ preventScroll: true }); input.value = 'draft before request'; input.setSelectionRange(2, 8, 'backward');
        const body = popup.querySelector('.tube-designer-preset-dialog-body,.tube-designer-profile-library-list');
        body.scrollTop = 150;
        let release; const response = new Promise(resolve => { release = resolve; }).then(() => {
          if (kind === 'product-preset') f.view.tubeDesignerUserData.customers.push({ id: 'late-customer', name: 'Late customer' });
          if (kind === 'imported-profile') f.view.tubeDesignerProfileDialog.profile.specification = '50 × 30 × 2';
          if (kind === 'imported-profile-library') f.view.tubeDesignerUserData.profiles.push({ id: 'added-profile', name: 'A newly saved profile', specification: '30 × 20 × 2' });
          f.render();
        });
        // The late refresh must capture interaction at mutation time.
        input.value = 'draft after request'; input.setSelectionRange(3, 12, 'backward'); f.setScroll(390); body.scrollTop = 230;
        const latestBodyScroll = body.scrollTop, latestScroll = f.scrollNodes().map(node => node.scrollTop);
        release(); await response;
        f.check(input.isConnected && document.activeElement === input && popup.contains(input), 'Floating text input identity/focus changed');
        f.check(input.value === 'draft after request' && input.selectionStart === 3 && input.selectionEnd === 12 && input.selectionDirection === 'backward', 'Floating text draft/selection changed');
        f.check(body.scrollTop === latestBodyScroll && JSON.stringify(f.scrollNodes().map(node => node.scrollTop)) === JSON.stringify(latestScroll), 'Floating or nested sidebar latest scroll changed');
        input.dispatchEvent(new Event('input', { bubbles: true })); f.check(changes === 1, 'Floating input listener detached');
        f.assertScene('floating asynchronous content update');
      }, kind);
      assertWindowRect(await geometry(panel(kind)), resized, kind + ': async content update changed drag/resize rectangle');
      if (kind === 'imported-profile-library') await page.evaluate(() => {
        const f = window.fixture, removed = f.popupInput, id = removed.dataset.tubeDesignerProfileId;
        f.check(id, 'Profile input needs a stable profile identity');
        f.view.tubeDesignerUserData.profiles = f.view.tubeDesignerUserData.profiles.filter(profile => profile.id !== id);
        f.render();
        f.check(!removed.isConnected && !document.activeElement?.matches('[data-tube-designer-library-profile-name]'), 'Removed focused profile gave its focus to another row');
      });
      await panel(kind).getByRole('button', { name: '关闭', exact: true }).click();
      assert.equal(await panel(kind).count(), 0); await scene(kind + ' close');
    }
    // Conditions remove a focused field. The replacement must not acquire its
    // focus, and a subsequent response must not steal focus from another pane.
    await page.evaluate(async () => {
      const f = window.fixture, conditional = f.mount.querySelector('[data-fixture-input="conditional"]');
      conditional.focus({ preventScroll: true }); conditional.setSelectionRange(2, 6); f.setScroll(450);
      const scroll = f.scrollNodes().map(node => node.scrollTop); f.condition = false; f.render();
      f.check(!conditional.isConnected && document.activeElement !== f.rightInput && document.activeElement !== f.leftInput, 'Removed field gave focus to another input');
      f.check(JSON.stringify(f.scrollNodes().map(node => node.scrollTop)) === JSON.stringify(scroll), 'Conditional removal changed nested sidebar scrolling');
      f.leftInput.focus({ preventScroll: true }); f.leftInput.value = 'left latest'; f.leftInput.setSelectionRange(1, 5, 'backward');
      await new Promise(resolve => requestAnimationFrame(resolve)); f.render();
      f.check(document.activeElement === f.leftInput && f.leftInput.value === 'left latest' && f.leftInput.selectionStart === 1 && f.leftInput.selectionEnd === 5, 'Refresh stole later left input focus');
      f.rightInput.dispatchEvent(new Event('input', { bubbles: true })); f.check(f.events.input === 1, 'Original right input listener detached');
      f.assertScene('conditional field removal');
    });
    results.push({ area, popupSource: area === 'profiles' ? 'right-pane HTML' : 'workbench suffix', actualPopupRenderers: ['product-preset', 'imported-profile', 'imported-profile-library'], canvasCameraGeometryInputListenerIdentity: true, renderedFrameAndMaterialPreserved: true, dragResizeRectangles: true, outsideOrbit: true, focusSelectionNestedScrolling: true, conditionalRemoval: true });
  }
  assert.deepEqual(errors, [], 'Browser raised unexpected errors');
  writeFileSync(resolve(reportDirectory, 'report.json'), JSON.stringify({ browser: 'Playwright Chromium/Edge', actualWebGL: true, nativeTransport: 'controlled project save fixture', standaloneCefEndToEnd: false, results }, null, 2));
  console.log('Floating parameter editors preserve actual WebGL canvas/camera/geometry/listeners, sidebar/popup controls, current text selection and nested scrolling through open/edit/async/drag/resize/close; outside orbit gestures remain available.');
  console.log('Report: ' + reportDirectory);
} finally { await browser.close(); }
