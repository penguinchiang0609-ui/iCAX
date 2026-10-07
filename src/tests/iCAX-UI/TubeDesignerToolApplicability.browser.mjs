// Actual tool library, local DOM patching and WebGL canvas. Native business
// replies are controlled protocol fixtures; this test does not claim CEF/native geometry acceptance.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory, importBrowserAsset } from './browserPackageRuntime.mjs';

const tools = ['end-profile', 'end-key-joint'].map(id => {
  const descriptor = JSON.parse(readFileSync(browserAssetPath(`apps/tube-designer/templates/mold/${id}/tool.json`), 'utf8'));
  return { ...descriptor, libraryScope: 'system', defaultParameters: Object.fromEntries((descriptor.parameters ?? []).map(row => [row.key, row.defaultValue])) };
});
const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = browserReportDirectory('tool-applicability-20261006');
const browser = await chromium.launch({ headless: true, channel: 'chrome' });
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 960 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://tool-applicability.test/**', route => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"></head><body><main id="app"></main></body></html>' });
    if (!pathname.startsWith('/src/')) return route.abort();
    const file = browserAssetPath(pathname.slice('/src/'.length));
    return route.fulfill({ contentType: pathname.endsWith('.css') ? 'text/css' : 'text/javascript', body: readFileSync(file, 'utf8') });
  });
  await page.goto('http://tool-applicability.test/');
  await page.addStyleTag({ content: tubeDesignerCss + `*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,Arial,sans-serif}.cam-workbench{display:grid;grid-template-columns:300px minmax(0,1fr) 400px;height:940px}.cam-context-pane,.cam-info-pane{display:block;overflow:auto;min-width:0;height:940px}.cam-viewport{position:relative;background:#13252d;min-width:0}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1100px}.tube-tool-library-list{height:180px;overflow:auto}.tube-tool-library-list::after{content:"";display:block;height:700px}.tube-tool-library-editor-body{height:560px;overflow:auto}.tube-tool-library-editor-body::after{content:"";display:block;height:700px}` });
  await page.evaluate(async tools => {
    const library = await import('/src/apps/tube-designer/webpage/toolLibrary.mjs');
    const patch = await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    const { capturePaneInteraction } = await import('/src/apps/_shared/workbench/utils/paneInteractionState.mjs');
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const profiles = ['round', 'polygon', 'rect'].map(id => ({ id, name: id, libraryScope: 'system', profileForm: 'parametric', profileType: 'parametric-package',
      descriptor: { version: '1.0.0', parameters: [{ key: 'width', displayName: '宽度', valueType: 'number', defaultValue: 40, min: 1 },
        { key: 'wallThickness', displayName: '壁厚', valueType: 'number', defaultValue: 2, min: 0.1 }] },
      defaultParameters: { width: 40, wallThickness: 2 }, previewProfile: { name: id, kind: 'round', width: 40, depth: 40, wallThickness: 2,
        contours: [{ kind: 'circle', radius: 20 }, { kind: 'circle', radius: 18 }] } }));
    const mount = document.querySelector('#app');
    const view = { pending: false, activeAreaId: 'tools', scene: { tubeDesigner: {} }, tubeDesignerSystemProfiles: profiles,
      tubeDesignerSystemPunchTools: tools, tubeDesignerUserData: { profiles: [], punchTools: [] }, tubeDesignerToolLibrary: {
        scope: 'system', type: 'all', category: 'all', search: '', selectedKey: 'system::end-profile', catalogueStatus: 'ready', mainTubeCollapsed: false,
        profileKeysByTool: { 'system::end-profile': 'system:round' }, branchProfileKeysByTool: { 'system::end-profile': 'system:polygon' },
        profileDrafts: { 'system:round': { width: 50, wallThickness: 2 } }, branchProfileDrafts: { 'system:polygon': { width: 40, wallThickness: 2 } },
        parameterDrafts: { 'system::end-profile': { cutMode: 'convex' } }, operationDrafts: { 'system::end-profile': { trim: 7, angle: 70 } } } };
    const f = globalThis.__toolApplicability = { view, calls: [], checks: [], recommendations: [], snapshots: [], patches: 0, actionCount: 0, errors: [] };
    const context = { mount, sceneProxy: { resources: { async get(url) {
      const box = new THREE.BoxGeometry(url.includes('blank') ? 500 : 40, 40, 40);
      return { url, version: 1, type: 'geometry', data: { kind: 'mesh', positions: [...box.attributes.position.array], indices: [...box.index.array] } };
    } }, async invoke(method, payload, options) {
      const call = { method, payload: structuredClone(payload), options, id: f.calls.length + 1 }; f.calls.push(call);
      if (method === 'TubeDesigner.EvaluateProfilePackage') {
        const width = Number(payload.parameters.width);
        return { profile: { name: payload.profileRef.id, kind: 'round', width, depth: width, wallThickness: payload.parameters.wallThickness,
          contours: [{ kind: 'circle', radius: width / 2 }, { kind: 'circle', radius: width / 2 - Number(payload.parameters.wallThickness) }] } };
      }
      if (method === 'TubeDesigner.CheckPunchToolApplicability' || method === 'TubeDesigner.GetPunchToolProfileRecommendation') return new Promise((resolve, reject) => {
        const item = { call, resolve, reject, pending: true };
        (method.endsWith('CheckPunchToolApplicability') ? f.checks : f.recommendations).push(item);
      });
      throw new Error('Unexpected tool browser request: ' + method);
    } } };
    const render = () => {
      const existing = mount.querySelector('.cam-workbench');
      const restore = existing ? capturePaneInteraction(mount) : () => {};
      const left = library.renderToolLibraryLeftPane(context, view), right = library.renderToolLibraryRightPane(context, view);
      const overlay = library.renderToolLibraryViewportOverlay(context, view);
      if (!existing) {
        mount.innerHTML = `<div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport">${overlay}</div><aside class="cam-info-pane">${right}</aside></div>`;
        patch.rememberLibraryDom(view, mount, '', context.sceneProxy);
        view.viewport = createThreeViewport({ picking: false }); view.viewport.mount(mount.querySelector('.cam-viewport'));
        const applySnapshot = view.viewport.applyViewSnapshot.bind(view.viewport);
        view.viewport.applyViewSnapshot = (snapshot, ...args) => { f.snapshots.push(structuredClone(snapshot)); return applySnapshot(snapshot, ...args); };
      } else {
        if (!patch.patchLibraryDom(view, mount, { left, right, overlay, suffix: '', sceneProxy: context.sceneProxy })) throw new Error('Ordinary tool repaint replaced its entire workbench');
        f.patches++;
      }
      restore();
    };
    const ops = { renderProject: render };
    view.tubeDesignerToolLibraryRenderProject = render;
    const dispatch = async (action, target) => {
      f.actionCount++;
      try { await library.handleToolLibraryAction(context, view, action, target, ops); }
      catch (error) { f.errors.push(error.message); }
      finally { f.actionCount--; }
    };
    mount.addEventListener('change', event => { const target = event.target.closest('[data-cam-change-action]'); if (target) void dispatch(target.dataset.camChangeAction, target); });
    mount.addEventListener('click', event => { const target = event.target.closest('[data-cam-action]'); if (target && !target.disabled) void dispatch(target.dataset.camAction, target); });
    f.render = render;
    f.currentToolId = payload => payload.features?.[0]?.toolRef?.id ?? payload.ends?.start?.toolRef?.id;
    f.finishCheck = (index, applicable, reason = '', includeCurrentBlank = false) => {
      const item = f.checks[index], toolId = f.currentToolId(item.call.payload); item.pending = false;
      const result = { schema: 'icax.punch-tool-applicability', schemaVersion: 1, toolId, applicable, reason };
      if (applicable || includeCurrentBlank) {
        result.preview = { toolsOnly: true, previewToolsComplete: applicable,
          baseGeometry: { url: `memory://blank-${item.call.id}`, version: applicable ? 1 : 2 }, toolPreviews: [],
          ...(applicable ? { toolGeometry: { url: `memory://tool-${item.call.id}`, version: 1 } } : { resultError: reason }) };
        for (const geometry of [result.preview.baseGeometry, result.preview.toolGeometry].filter(Boolean)) {
          const blank = geometry.url.includes('blank'), width = Number(item.call.payload.parameters.width);
          const box = new THREE.BoxGeometry(blank ? item.call.payload.length : 40, blank ? width : 40, blank ? width : 40);
          view.viewport.resourcePromises.set(`${geometry.url}@${geometry.version}`, Promise.resolve({ url: geometry.url, version: geometry.version, type: 'geometry',
            data: { kind: 'mesh', positions: [...box.attributes.position.array], indices: [...box.index.array] } }));
        }
      }
      item.call.result = result; item.resolve(result);
    };
    f.finishRecommendation = (index, profiles, reason = '') => {
      const item = f.recommendations[index], toolId = f.currentToolId(item.call.payload); item.pending = false;
      const result = { schema: 'icax.punch-tool-profile-recommendation', schemaVersion: 1, toolId, available: !!Object.keys(profiles).length, profiles, reason };
      item.call.result = result; item.resolve(result);
    };
    f.capture = () => {
      const selectors = { search: '[aria-label="搜索单件工艺"]', input: '[data-tube-tool-library-operation-parameter="trim"]',
        canvas: '.icax-three-viewport-canvas', left: '.cam-context-pane', right: '.cam-info-pane', list: '.tube-tool-library-list', editor: '.tube-tool-library-editor-body' };
      const nodes = Object.fromEntries(Object.entries(selectors).map(([key, selector]) => [key, document.querySelector(selector)]));
      let inputEvents = 0, canvasEvents = 0;
      nodes.input.addEventListener('test-listener', () => inputEvents++); nodes.canvas.addEventListener('test-listener', () => canvasEvents++);
      f.nodes = nodes; f.selectors = selectors; f.listenerCounts = () => ({ inputEvents, canvasEvents });
    };
    f.interact = (scrolls, selection = [1, 3]) => {
      const nodes = f.nodes; nodes.search.value = '正在输入'; nodes.search.focus({ preventScroll: true }); nodes.search.setSelectionRange(...selection);
      for (const [index, key] of ['left', 'right', 'list', 'editor'].entries()) nodes[key].scrollTop = scrolls[index];
      f.expectedScroll = ['left', 'right', 'list', 'editor'].map(key => nodes[key].scrollTop);
      f.expectedSelection = selection;
    };
    f.interaction = () => ({ stableNodes: Object.fromEntries(Object.entries(f.nodes).map(([key, node]) => [key, node === document.querySelector(f.selectors[key])])),
      focus: document.activeElement === f.nodes.search, text: f.nodes.search.value, selection: [f.nodes.search.selectionStart, f.nodes.search.selectionEnd],
      expectedSelection: f.expectedSelection, expectedScroll: f.expectedScroll,
      scroll: ['left', 'right', 'list', 'editor'].map(key => f.nodes[key].scrollTop) });
    render();
  }, tools);
  const waitCheck = index => page.waitForFunction(index => __toolApplicability.checks.length > index, index);
  const waitRecommendation = index => page.waitForFunction(index => __toolApplicability.recommendations.length > index, index);
  const settled = () => page.waitForFunction(() => !__toolApplicability.actionCount && !__toolApplicability.view.tubeDesignerToolLibrary.previewRequest);
  const assertInteraction = async label => {
    const result = await page.evaluate(() => __toolApplicability.interaction());
    writeFileSync(resolve(output, `${label.replaceAll(' ', '-')}-interaction.json`), JSON.stringify(result, null, 2));
    assert(Object.values(result.stableNodes).every(Boolean), `${label}: input/canvas/panes/nested nodes must be retained`);
    assert.equal(result.focus, true, `${label}: latest search focus`); assert.equal(result.text, '正在输入');
    assert.deepEqual(result.selection, result.expectedSelection, `${label}: latest caret selection`);
    assert.deepEqual(result.scroll, result.expectedScroll, `${label}: latest four scroll positions`);
    assert(result.scroll.every(value => value > 0), `${label}: all four containers really scroll`);
    return result;
  };
  await waitCheck(0);
  await page.evaluate(() => { __toolApplicability.capture(); __toolApplicability.interact([30, 40, 50, 60]); });
  await page.evaluate(() => { __toolApplicability.interact([95, 105, 135, 175], [0, 3]); __toolApplicability.finishCheck(0, false, '刀具截面无法生成完整作用体：请减小刀具管型。', true); });
  await waitRecommendation(0);
  assert.match(await page.locator('[data-tool-library-applicability]').innerText(), /请减小刀具管型/);
  const checkInteraction = await assertInteraction('async applicability check');
  await page.evaluate(() => {
    __toolApplicability.interact([125, 155, 185, 215], [1, 4]);
    __toolApplicability.finishRecommendation(0, { branch: { profileRef: { scope: 'system', id: 'round' }, parameters: { width: 20, wallThickness: 2 } } });
  });
  await settled();
  const recommendationInteraction = await assertInteraction('async recommendation');
  const recommendationButton = page.locator('[data-cam-action="tube-designer-tool-library-use-recommended-profiles"]');
  await recommendationButton.waitFor({ state: 'attached' });
  const beforeUse = await page.evaluate(() => structuredClone(__toolApplicability.view.tubeDesignerToolLibrary));
  await recommendationButton.click();
  await waitCheck(1);
  const afterUse = await page.evaluate(() => {
    const state = __toolApplicability.view.tubeDesignerToolLibrary;
    return { profileKey: state.profileKey, profileDrafts: state.profileDrafts, branchProfileKey: state.branchProfileKey, branchProfileDrafts: state.branchProfileDrafts,
      parameterDrafts: state.parameterDrafts, operationDrafts: state.operationDrafts };
  });
  assert.equal(afterUse.profileKey, beforeUse.profileKey);
  assert.deepEqual(afterUse.profileDrafts, beforeUse.profileDrafts);
  assert.equal(afterUse.profileKey, 'system:round'); assert.equal(afterUse.profileDrafts['system:round'].width, 50);
  assert.equal(afterUse.branchProfileKey, 'system:round'); assert.deepEqual(afterUse.branchProfileDrafts['system:round'], { width: 20, wallThickness: 2 });
  assert.deepEqual(afterUse.parameterDrafts, beforeUse.parameterDrafts); assert.deepEqual(afterUse.operationDrafts, beforeUse.operationDrafts);
  await page.evaluate(() => {
    __toolApplicability.interact([140, 160, 180, 220], [0, 2]);
    __toolApplicability.finishCheck(1, true);
  });
  await settled();
  const successfulPreviewInteraction = await assertInteraction('recommended preview');
  const previewEvidence = await page.evaluate(() => {
    const f = __toolApplicability, state = f.view.tubeDesignerToolLibrary;
    f.nodes.input.dispatchEvent(new Event('test-listener')); f.nodes.canvas.dispatchEvent(new Event('test-listener'));
    return { previewKey: state.preview?.key, previewError: state.previewError, visibleObjectIds: [...f.view.viewport.sceneObjects].filter(([, object]) => object.visible).map(([id]) => id),
      listeners: f.listenerCounts(), actualPayload: f.checks[1].call.payload };
  });
  writeFileSync(resolve(output, 'recommended-preview-diagnostic.json'), JSON.stringify(previewEvidence, null, 2));
  assert(previewEvidence.previewKey); assert(previewEvidence.visibleObjectIds.includes('punch-preview-blank')); assert(previewEvidence.visibleObjectIds.includes('punch-preview-tools'));
  assert.deepEqual(previewEvidence.listeners, { inputEvents: 1, canvasEvents: 1 });
  assert.equal(previewEvidence.actualPayload.parameters.width, 50);
  assert.equal(previewEvidence.actualPayload.ends.start.section.parameters.width, 20);
  assert.equal(previewEvidence.actualPayload.ends.start.toolParameters.cutMode, 'convex'); assert.equal(previewEvidence.actualPayload.ends.start.trim, 7);
  await page.screenshot({ path: resolve(output, 'recommended-branch-preserves-main-and-operation.png') });

  // A recommendation calculated for an older parameter edit must not become actionable.
  await page.evaluate(() => {
    __toolApplicability.cameraBeforeUnsupported = __toolApplicability.view.viewport.getCameraState();
    const input = document.querySelector('input[data-tube-tool-library-profile-role="main"][data-tube-tool-library-profile-parameter="width"]');
    input.value = '55'; input.dispatchEvent(new Event('change', { bubbles: true }));
  });
  await waitCheck(2); await page.evaluate(() => __toolApplicability.finishCheck(2, false, '旧参数检查失败', true));
  await waitRecommendation(1);
  const currentBlank = await page.evaluate(async () => {
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const f = __toolApplicability, viewport = f.view.viewport, blank = viewport.sceneObjects.get('punch-preview-blank');
    const bounds = new THREE.Box3().setFromObject(blank), size = bounds.getSize(new THREE.Vector3());
    return { snapshot: f.snapshots.at(-1), size: size.toArray(), currentMainWidth: f.checks[2].call.payload.parameters.width,
      visibleObjectIds: [...viewport.sceneObjects].filter(([, object]) => object.visible).map(([id]) => id),
      cameraBefore: f.cameraBeforeUnsupported, cameraAfter: viewport.getCameraState() };
  });
  assert.equal(currentBlank.currentMainWidth, 55); assert.equal(currentBlank.size[1], 55);
  assert.equal(currentBlank.snapshot.rows.length, 1); assert.equal(currentBlank.snapshot.rows[0].data.geometry.version, 2);
  assert.deepEqual(currentBlank.visibleObjectIds, ['punch-preview-blank']); assert.deepEqual(currentBlank.cameraAfter, currentBlank.cameraBefore);
  await assertInteraction('unsupported current blank');
  await page.evaluate(() => {
    const input = document.querySelector('[data-tube-tool-library-parameter="cutMode"]');
    input.value = 'concave'; input.dispatchEvent(new Event('change', { bubbles: true }));
  });
  await waitCheck(3); await page.evaluate(() => __toolApplicability.finishCheck(3, true)); await settled();
  const newestKey = await page.evaluate(() => __toolApplicability.view.tubeDesignerToolLibrary.preview.key);
  await page.evaluate(() => __toolApplicability.finishRecommendation(1, { branch: { profileRef: { scope: 'system', id: 'polygon' }, parameters: { width: 99, wallThickness: 2 } } }));
  await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  const staleParameter = await page.evaluate(() => {
    const state = __toolApplicability.view.tubeDesignerToolLibrary;
    return { key: state.preview?.key, recommendation: state.profileRecommendation, previewError: state.previewError,
      branchKey: state.branchProfileKey, branchWidth: state.branchProfileDrafts['system:round'].width,
      cutMode: state.parameterDrafts['system::end-profile'].cutMode };
  });
  assert.equal(staleParameter.key, newestKey); assert.equal(staleParameter.recommendation, null); assert.equal(staleParameter.previewError, '');
  assert.equal(staleParameter.branchKey, 'system:round'); assert.equal(staleParameter.branchWidth, 20); assert.equal(staleParameter.cutMode, 'concave');
  assert.equal(await recommendationButton.count(), 0);

  // An older check must not overwrite the selected tool or issue its recommendation after a switch.
  await page.evaluate(() => {
    const input = document.querySelector('[data-tube-tool-library-parameter="cutMode"]');
    input.value = 'convex'; input.dispatchEvent(new Event('change', { bubbles: true }));
  });
  await waitCheck(4);
  await page.locator('[data-cam-action="tube-designer-tool-library-select"][data-tube-tool-library-key="system::end-key-joint"]').click();
  await waitCheck(5); await page.evaluate(() => __toolApplicability.finishCheck(5, true)); await settled();
  await page.evaluate(() => __toolApplicability.finishCheck(4, false, '旧工艺检查失败'));
  await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  const staleSelection = await page.evaluate(() => {
    const state = __toolApplicability.view.tubeDesignerToolLibrary;
    return { selected: state.selectedKey, previewError: state.previewError, recommendationCount: __toolApplicability.recommendations.length,
      applicabilityTool: state.applicability?.result.toolId, previewKey: state.preview?.key };
  });
  assert.equal(staleSelection.selected, 'system::end-key-joint'); assert.equal(staleSelection.previewError, '');
  assert.equal(staleSelection.recommendationCount, 2); assert.equal(staleSelection.applicabilityTool, 'end-key-joint'); assert(staleSelection.previewKey?.includes('end-key-joint'));

  // Conditional controls change without replacing persistent fields or the renderer.
  await page.evaluate(() => {
    __toolApplicability.conditionInput = document.querySelector('[data-tube-tool-library-parameter="width"]');
    __toolApplicability.nodes.search.focus({ preventScroll: true }); __toolApplicability.nodes.search.setSelectionRange(1, 4);
    const input = document.querySelector('[data-tube-tool-library-parameter="gender"]');
    input.value = 'female'; input.dispatchEvent(new Event('change', { bubbles: true }));
  });
  await waitCheck(6); await page.evaluate(() => __toolApplicability.finishCheck(6, true)); await settled();
  assert.equal(await page.locator('[data-tube-tool-library-parameter="sideClearance"]').count(), 1);
  assert.equal(await page.evaluate(() => __toolApplicability.conditionInput === document.querySelector('[data-tube-tool-library-parameter="width"]')
    && __toolApplicability.nodes.canvas === document.querySelector('.icax-three-viewport-canvas') && document.activeElement === __toolApplicability.nodes.search), true);
  await page.evaluate(() => {
    const input = document.querySelector('[data-tube-tool-library-parameter="gender"]');
    input.value = 'male'; input.dispatchEvent(new Event('change', { bubbles: true }));
  });
  await waitCheck(7); await page.evaluate(() => __toolApplicability.finishCheck(7, true)); await settled();
  assert.equal(await page.locator('[data-tube-tool-library-parameter="sideClearance"]').count(), 0);
  assert.equal(await page.evaluate(() => __toolApplicability.conditionInput === document.querySelector('[data-tube-tool-library-parameter="width"]')
    && __toolApplicability.nodes.canvas === document.querySelector('.icax-three-viewport-canvas') && document.activeElement === __toolApplicability.nodes.search), true);
  const final = await page.evaluate(() => ({ calls: __toolApplicability.calls, actionErrors: __toolApplicability.errors, patches: __toolApplicability.patches }));
  assert.deepEqual(errors, []); assert.deepEqual(final.actionErrors, []);
  assert(final.calls.filter(row => /CheckPunchToolApplicability|GetPunchToolProfileRecommendation/.test(row.method)).every(row => row.options?.timeoutMs === 120000));
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: true,
    unsupportedReasonShown: true, actualRecommendationButtonClick: true, onlyRecommendedRoleChanged: true, mainAndToolOperationParametersPreserved: true,
    asyncInteraction: { check: checkInteraction, recommendation: recommendationInteraction, recommendedPreview: successfulPreviewInteraction },
    previewEvidence, unsupportedPreviewUpdatesOnlyCurrentBlank: currentBlank, staleParameterRecommendationIgnored: staleParameter, staleToolCheckIgnored: staleSelection,
    conditionalFieldsPreserveControls: true, ...final, pageErrors: errors,
    nativeTransport: 'controlled protocol fixture', geometry: 'controlled box mesh fixture rendered by production WebGL viewport', standaloneCefEndToEnd: false }, null, 2));
  console.log('PASS tool applicability browser: unsupported reason, branch-only recommendation, preserved parameters and latest interaction, ignored stale replies, conditional DOM and renderer stability.');
} finally { await browser.close(); }
