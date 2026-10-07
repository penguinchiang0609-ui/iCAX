// Production parameter/sketch/machining renderers, delegated handlers and memory adapter.
// The CAD transport and opening contexts are controlled: this is a UI scope regression.
import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { browserReportDirectory, serveBrowserAsset } from './browserPackageRuntime.mjs';

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const directory = browserReportDirectory('tube-designer-window-memory-scopes');
const checks = [], browserErrors = [];
const check = (actual, expected, name) => { assert.deepEqual(actual, expected, name); checks.push(name); };
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
let page;
try {
  page = await browser.newPage({ viewport: { width: 1300, height: 1050 } });
  page.setDefaultTimeout(15000);
  page.on('pageerror', error => browserErrors.push(error.message));
  await page.route('http://window-memory-scopes.test/**', serveBrowserAsset);
  await page.goto('http://window-memory-scopes.test/');
  await page.evaluate(async () => {
    localStorage.clear();
    const [memory, designerViews, designerActions, sketch, machining, paths] = await Promise.all([
      'windowStateMemory', 'designerViews', 'designerActions', 'sketchArea', 'machiningArea', 'machiningEditor',
    ].map(name => import(`/src/apps/tube-designer/webpage/${name}.mjs`)));
    const { patchParameterContent } = await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    document.body.className = 'tube-designer-workspace';
    document.body.innerHTML = `<style>body{margin:0;background:#f5f8f9;font:14px sans-serif;color:#294953}
      #mount{padding:20px}.cam-workbench{display:flex;gap:20px}.cam-info-pane{width:780px;padding:16px;border:1px solid #9bb5bc}
      input,select{box-sizing:border-box;min-height:30px;max-width:100%;padding:4px}label{display:block;margin:5px}
      .tube-machining-field-grid,.tube-sketch-property-grid{display:grid;grid-template-columns:repeat(3,1fr)}
      .tube-machining-path-list{display:flex;gap:8px}.tube-machining-path-row{display:flex}button{padding:6px}
      details>summary{cursor:pointer}h4{margin:12px 0 4px}</style><div id="mount"><div class="cam-workbench"></div></div>`;
    const f = window.scopeFixture = { errors: [], calls: [], renders: 0, memory, sketch, machining, paths };
    f.context = { mount: document.querySelector('#mount'), project: { projectId: 'scope-project' }, actions: {
      async refreshActiveSceneState() {}, refreshProjectHistoryControls() {},
    }, productProxy: { async invoke(method, payload) {
      f.calls.push({ method, payload: structuredClone(payload) });
      if (method !== 'TubeDesigner.EvaluateProfilePackage') throw Error(`Unexpected product transport: ${method}`);
      const source = structuredClone(f.view.tubeDesignerSystemProfiles.find(row => row.id === payload.profileRef.id).previewProfile);
      source.parameters = structuredClone(payload.parameters);
      source.width = source.depth = payload.parameters.width;
      source.contours = [{ kind: 'circle', radius: source.width / 2 }, { kind: 'circle', radius: source.width / 2 - 2 }];
      return { profile: source };
    } }, sceneProxy: {
      async invoke(method, payload) {
        f.calls.push({ method, payload: structuredClone(payload) });
        if (method !== 'TubeDesigner.UpdateProductParameters') throw Error(`Unexpected scene transport: ${method}`);
        const designer = structuredClone(f.view.scene.tubeDesigner);
        designer.product.parameters = structuredClone(payload.parameters);
        return { tubeDesigner: designer };
      },
    } };
    f.ops = { renderProject() { f.draw(); }, appendProjectLog() {}, showNotice() {} };
    f.draw = () => {
      const view = f.view;
      const content = view.activeAreaId === 'view'
        ? designerViews.renderDesignerRightPane(f.context, view)
        : view.activeAreaId === 'sketch' ? sketch.renderSketchRightPane(f.context, view)
          : machining.renderTubeMachiningRightPane(f.context, view);
      const workbench = f.context.mount.querySelector('.cam-workbench');
      let pane = workbench.querySelector('.cam-info-pane');
      if (!pane) { pane = document.createElement('aside'); pane.className = 'cam-info-pane'; workbench.append(pane); }
      // Enrolment is declared by the actual production page, never by the fixture.
      patchParameterContent(f.context.mount, pane, content);
      for (const details of f.context.mount.querySelectorAll('details')) details.open = true;
      f.controller = memory.bindTubeDesignerWindowMemory(f.context, view, f.ops);
      f.renders += 1;
    };
    f.run = (action, target) => {
      const view = f.view;
      const handler = view.activeAreaId === 'view' ? designerActions.handleDesignerAreaAction
        : view.activeAreaId === 'sketch' ? sketch.handleSketchAreaAction : machining.handleTubeMachiningAction;
      const promise = handler(f.context, view, action, target, f.ops).then(result => {
        if (view.activeAreaId === 'view') f.draw();
        return result;
      });
      view.activeAreaAction = { action, promise };
      void promise.catch(error => f.errors.push(error.stack)).finally(() => {
        if (view.activeAreaAction?.promise === promise) view.activeAreaAction = null;
      });
      return promise;
    };
    f.context.mount.addEventListener('change', event => {
      if (event.target.matches('[data-cam-change-action]')) void f.run(event.target.dataset.camChangeAction, event.target);
    });
    f.context.mount.addEventListener('click', event => {
      const target = event.target.closest('[data-cam-action]');
      if (target && !target.disabled) void f.run(target.dataset.camAction, target);
    });
    f.open = view => {
      f.context.mount.querySelector('.cam-workbench').replaceChildren();
      f.controller?.refresh();
      f.view = view;
      f.draw();
    };
    const base = activeAreaId => ({ activeAreaId, pending: false, scene: { tubeDesigner: { templates: [], parts: [], members: [] } } });
    const profile = id => ({ id, name: `管型 ${id}`, available: true, profileType: 'profile-package', profileForm: 'parametric',
      previewProfile: { schema: 'icax.imported-tube-profile', schemaVersion: 1, kind: 'profile-package',
        sectionKind: 'round', profileForm: 'parametric', name: `管型 ${id}`, width: 40, depth: 40, hollow: true,
        parameters: { width: 40 }, parameterDefinitions: [{ key: 'width', displayName: '外径', valueType: 'number', defaultValue: 40 }],
        contours: [{ kind: 'circle', radius: 20 }, { kind: 'circle', radius: 18 }] } });
    const template = { id: 'scope-product', name: '记忆验收产品', available: true, descriptorLoaded: true,
      groups: [{ key: 'materials', displayName: '用料', defaultOpen: true }], parameters: [
        { key: 'frameProfileType', groupKey: 'materials', displayName: '外框', type: 'select', valueType: 'enum',
          defaultValue: 'round', options: [{ value: 'round', label: '圆管' }], presentation: {
            editor: 'profile-library', resourceRole: 'frame', profileConstraints: { sectionKinds: ['round'], hollow: true },
          } },
        { key: 'frameWidth', groupKey: 'materials', displayName: '外径', type: 'number', valueType: 'number', defaultValue: 40, unit: 'mm' },
      ], extensions: { parameterLayout: { sections: [{ key: 'materials', groups: ['materials'] }] },
        parameterDependencies: { creationOnly: [], manufacturingOnly: [] }, resourceRoles: { profiles: { frame: {
          parameter: 'frameProfileType', defaultResourcesBySectionKind: { round: 'system:A' },
          parameterBindingsBySectionKind: { round: { width: 'frameWidth' } },
          parameterLabelsBySectionKind: { round: { width: { 'zh-CN': '外径' } } }, managedParameters: ['frameWidth'],
        } } } } };
    f.openProduct = (entityId = 'product-one') => {
      const view = base('view');
      view.tubeDesignerSystemProfiles = ['A', 'B', 'C'].map(profile);
      view.tubeDesignerUserData = { profiles: [] };
      view.scene.tubeDesigner.templates = [structuredClone(template)];
      view.scene.tubeDesigner.product = { entityId, templateId: template.id, name: '产品', quantity: 1,
        parameters: { frameProfileType: 'round', frameWidth: 40,
          tubeDesignerProfileOverrides: { frame: { ...profile('A').previewProfile, profileScope: 'system', profileDefinitionId: 'A' } } } };
      f.open(view);
    };
    f.openSketch = options => {
      const view = base('sketch');
      view.tubeDesignerSketch = sketch.createInitialSketchState();
      view.tubeDesignerSketch.sectionName = '默认名称';
      view.tubeDesignerSketch.section.entities = [{ id: 'rectangle-one', kind: 'rectangle', x: 0, y: 0, width: 20, height: 10, radius: 0, closed: true }];
      view.tubeDesignerSketch.section.selectedIds = ['rectangle-one'];
      if (options.csg) {
        view.tubeDesignerComponentLibrary = { csgDraft: { mode: options.mode || 'edit', id: options.id || 'component-A', features: [] } };
        view.tubeDesignerComponentCSGProfileReturn = { featureId: options.feature || 'feature-A' };
      } else if (options.tool) {
        view.tubeDesignerToolSketchContext = { kind: options.kind || 'fixed', target: options.target || 'part', category: options.category || 'hole' };
      }
      f.open(view);
    };
    f.openMachining = (jobId, pathId, nodeIndex = 0) => {
      const view = base('machining');
      const jobs = ['job-A', 'job-B'].map(id => ({ id, sourceKind: 'cad', name: id, sourceFileName: `${id}.step`, solidCount: 1,
        analysis: { revision: `${id}-revision`, paths: ['path-one', 'path-two'].map((path, index) => ({ id: path, name: path,
          origin: 'analysis', closed: false, points: [[index * 20, 0, 0], [index * 20 + 10, 0, 0], [index * 20 + 10, 10, 0]] })) } }));
      view.scene.tubeDesigner.machiningTask = { revision: 'machining-revision', jobs, activeJobId: jobId };
      view.tubeDesignerMachining = { selectedId: jobId, section: 'overview', edits: {} };
      const job = jobs.find(row => row.id === jobId), editor = paths.pathEditor(view, job);
      // Opening contexts come from the existing production path/viewport-selection helpers.
      paths.selectMachiningPath(editor, pathId);
      paths.editAtPoint(editor, editor.paths.find(row => row.id === pathId).points[nodeIndex], pathId, nodeIndex);
      editor.nodeIndex = nodeIndex;
      [editor.fields.nx, editor.fields.ny, editor.fields.nz] = editor.paths.find(row => row.id === pathId).points[nodeIndex].map(String);
      f.open(view);
    };
    f.storage = () => JSON.parse(localStorage.getItem('icax.window-state:icax.tube-designer.window-state'));
  });
  const settle = async () => {
    await page.waitForFunction(() => !scopeFixture.view.activeAreaAction && !scopeFixture.view.pending);
    await page.evaluate(async () => {
      await Promise.race([scopeFixture.controller.restoreWindow(document.querySelector('.cam-info-pane')),
        new Promise((_, reject) => setTimeout(() => reject(Error('Scope restoration timed out')), 10000))]);
    });
    await page.waitForFunction(() => !scopeFixture.view.activeAreaAction && !scopeFixture.view.pending);
    check(await page.evaluate(() => scopeFixture.errors), [], 'delegated handlers complete without errors');
    check(await page.evaluate(() => scopeFixture.view.error || ''), '', 'production handler reports no validation error');
  };
  const change = async (selector, value) => {
    await page.locator(selector).fill(String(value));
    await page.locator(selector).dispatchEvent('change');
    await settle();
  };
  const open = async (method, ...args) => { await page.evaluate(({ method, args }) => scopeFixture[method](...args), { method, args }); await settle(); };
  const selectedProfile = '[data-cam-change-action="tube-designer-profile-selection-change"]';
  const selectProfile = async id => { await page.locator(selectedProfile).focus(); await page.locator(selectedProfile).selectOption(`system:${id}`); await settle(); };
  await open('openProduct');
  check(await page.locator(selectedProfile).inputValue(), 'system:A', 'actual product selector starts with declared resource A');
  for (const id of ['B', 'C']) {
    await selectProfile(id);
    check(await page.locator(selectedProfile).getAttribute('data-tube-designer-profile-current-selection'), `system:${id}`,
      `production rerender updates transient current-selection to ${id}`);
  }
  const productFields = await page.evaluate(() => scopeFixture.storage().windows.find(row => JSON.parse(row.key)[0] === 'area/view/right').fields);
  const profileKeys = Object.keys(productFields).filter(key => key.includes('profile-selection-change'));
  check(profileKeys.length, 1, 'A to B to C creates one stable profile-selection field identity');
  check(profileKeys.some(key => key.includes('profile-current-selection')), false, 'transient selected value is excluded from field identity');
  check(productFields[profileKeys[0]], 'system:C', 'latest C replaces B in persistent memory');
  await open('openProduct');
  check(await page.locator(selectedProfile).inputValue(), 'system:C', 'reopening actual product selector restores latest C');
  check(await page.evaluate(() => scopeFixture.view.scene.tubeDesigner.product.parameters.tubeDesignerProfileOverrides.frame.profileDefinitionId),
    'C', 'restoration reaches the actual product handler and scene-update payload');
  await open('openProduct', 'product-two');
  check(await page.locator(selectedProfile).inputValue(), 'system:A', 'another product retains its own default resource');

  const profileWidth = '[data-profile-parameter-key="width"]';
  check(await page.evaluate(() => {
    const input = document.querySelector('[data-profile-parameter-key="width"]');
    return { selectorInParameterScope: Boolean(input.closest('[data-profile-parameter-scope]').querySelector('select')),
      selectorInProfileField: Boolean(input.closest('.tube-designer-profile-field').querySelector('select')) };
  }), { selectorInParameterScope: false, selectorInProfileField: true }, 'real product renderer puts the selector beside the dimensions container');
  await change(profileWidth, 61);
  await selectProfile('B');
  check(await page.locator(profileWidth).inputValue(), '40', 'B starts from its own dimensions rather than A remembered width');
  await change(profileWidth, 72);
  await open('openProduct', 'product-two');
  check(await page.locator(selectedProfile).inputValue(), 'system:B', 'product reopen restores the latest resource B');
  check(await page.locator(profileWidth).inputValue(), '72', 'product reopen restores B width in the sibling-selector structure');
  check(await page.evaluate(() => scopeFixture.view.scene.tubeDesigner.product.parameters.frameWidth), 72,
    'B remembered dimensions update the real product model');
  await selectProfile('A');
  check(await page.locator(profileWidth).inputValue(), '61', 'switching back to A restores its remembered dimensions immediately');
  check(await page.evaluate(() => scopeFixture.view.scene.tubeDesigner.product.parameters.frameWidth), 61,
    'switching back to A synchronizes the restored DOM value into the product model');
  await open('openProduct', 'product-two');
  check(await page.locator(selectedProfile).inputValue(), 'system:A', 'returning to A remembers the resource switch');
  check(await page.locator(profileWidth).inputValue(), '61', 'returning and reopening restores A own width independently from B');
  check(await page.evaluate(() => scopeFixture.view.scene.tubeDesigner.product.parameters.frameWidth), 61,
    'A remembered dimensions update the real product model');

  const name = '[data-cam-change-action="tube-designer-sketch-section-name"]', width = '[data-sketch-field="width"]';
  for (const [options, title, size] of [
    [{ csg: true, mode: 'create', id: 'component-A', feature: 'feature-A' }, 'CSG A', 41],
    [{ csg: true, mode: 'create', id: 'component-A', feature: 'feature-B' }, 'CSG B', 42],
    [{ csg: true, mode: 'create', id: 'component-B', feature: 'feature-A' }, 'CSG Other', 43],
    [{ csg: true, mode: 'edit', id: 'component-A', feature: 'feature-A' }, 'CSG Edit', 44],
    [{ tool: true, kind: 'fixed', target: 'part', category: 'hole' }, 'Tool Hole', 51],
    [{ tool: true, kind: 'fixed', target: 'part', category: 'slot' }, 'Tool Slot', 52],
    [{ tool: true, kind: 'fixed', target: 'end', category: 'hole' }, 'Tool End', 53],
    [{ tool: true, kind: 'programmatic', target: 'part', category: 'hole' }, 'Tool Program', 54],
  ]) {
    await open('openSketch', options);
    check(await page.locator(name).inputValue(), '默认名称', `${title} has no unrelated sketch-name draft`);
    check(await page.locator(width).inputValue(), '20', `${title} has no unrelated entity-width draft`);
    await change(name, title); await change(width, size);
  }
  for (const [options, title, size] of [
    [{ csg: true, mode: 'create', id: 'component-A', feature: 'feature-A' }, 'CSG A', 41],
    [{ csg: true, mode: 'create', id: 'component-A', feature: 'feature-B' }, 'CSG B', 42],
    [{ csg: true, mode: 'create', id: 'component-B', feature: 'feature-A' }, 'CSG Other', 43],
    [{ csg: true, mode: 'edit', id: 'component-A', feature: 'feature-A' }, 'CSG Edit', 44],
    [{ tool: true, kind: 'fixed', target: 'part', category: 'hole' }, 'Tool Hole', 51],
    [{ tool: true, kind: 'fixed', target: 'part', category: 'slot' }, 'Tool Slot', 52],
    [{ tool: true, kind: 'fixed', target: 'end', category: 'hole' }, 'Tool End', 53],
    [{ tool: true, kind: 'programmatic', target: 'part', category: 'hole' }, 'Tool Program', 54],
  ]) {
    await open('openSketch', options);
    const expectedName = options.csg && options.mode === 'edit' ? '默认名称' : title;
    check(await page.locator(name).inputValue(), expectedName, `${title} respects the page-owned name memory policy`);
    check(await page.locator(width).inputValue(), '20', `${title} geometry fields remain outside page memory policy`);
    check(await page.evaluate(() => ({ name: scopeFixture.view.tubeDesignerSketch.sectionName,
      width: scopeFixture.view.tubeDesignerSketch.section.entities[0].width })), { name: expectedName, width: 20 },
    `${title} restores the permitted name without rewriting actual sketch geometry`);
  }

  const dx = '[data-path-field="dx"]', nx = '[data-path-field="nx"]', nodeIndex = '[data-cam-change-action="tube-path-node-index"]';
  for (const [job, path, translation, coordinate, defaultCoordinate] of [
    ['job-A', 'path-one', 11, 101, '0'], ['job-A', 'path-two', 22, 201, '20'], ['job-B', 'path-one', 33, 301, '0'],
  ]) {
    await open('openMachining', job, path);
    check(await page.locator(dx).inputValue(), '0', `${job}/${path} translation does not leak from other jobs or paths`);
    check(await page.locator(nodeIndex).inputValue(), '1', `${job}/${path} node selection does not leak from another path`);
    check(await page.locator(nx).inputValue(), defaultCoordinate, `${job}/${path}/0 coordinates do not leak from other nodes`);
    await change(nodeIndex, 1);
    await change(dx, translation); await change(nx, coordinate);
    if (job === 'job-A' && path === 'path-one') {
      await change(nodeIndex, 2);
      check(await page.locator(nx).inputValue(), '10', 'another node starts with its own actual coordinate');
      await change(nx, 102);
    }
  }
  for (const [job, path, node, translation, coordinate] of [
    ['job-A', 'path-one', 1, 11, 102], ['job-A', 'path-two', 0, 22, 201], ['job-B', 'path-one', 0, 33, 301],
  ]) {
    await open('openMachining', job, path);
    check(await page.locator(nodeIndex).inputValue(), String(node + 1), `${job}/${path} restores its own node selection`);
    check(await page.locator(dx).inputValue(), String(translation), `${job}/${path} restores its own translation`);
    check(await page.locator(nx).inputValue(), String(coordinate), `${job}/${path}/${node} restores its own node coordinate`);
    check(await page.evaluate(() => {
      const editor = scopeFixture.view.tubeDesignerMachining.edits[scopeFixture.view.tubeDesignerMachining.selectedId];
      return { translation: editor.fields.dx, coordinate: editor.fields.nx };
    }), { translation: String(translation), coordinate: String(coordinate) }, `${job}/${path}/${node} restored fields update actual machining editor`);
    if (job === 'job-A' && path === 'path-one') {
      await change(nodeIndex, 1);
      check(await page.locator(nx).inputValue(), '101', 'returning to node zero restores its own recent coordinate immediately');
      check(await page.evaluate(() => scopeFixture.view.tubeDesignerMachining.edits['job-A'].fields.nx), '101',
        'returning to node zero synchronizes the actual machining model');
    }
  }
  check(browserErrors, [], 'no browser module/runtime errors');
  await page.screenshot({ path: join(directory, 'machining-scope-memory.png'), fullPage: true });
  const report = { passed: true, checks, coverage: { actualRenderers: ['product right parameters', 'sketch right pane', 'machining right pane'],
    actualHandlers: ['handleDesignerAreaAction', 'handleSketchAreaAction', 'handleTubeMachiningAction'], actualMemoryAdapter: true,
    controlledCadTransport: true, controlledOpeningContexts: true, productResourceTransitions: 'A -> B -> C',
    sketchScopes: ['CSG mode/component/feature', 'tool kind/target/category'], machiningScopes: ['job', 'path', 'node'],
    nativeCadEndToEnd: false, standaloneCef: false, diskBrowserRestart: false } };
  writeFileSync(join(directory, 'report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report));
} catch (error) {
  if (page) await page.screenshot({ path: join(directory, 'failure.png'), fullPage: true }).catch(() => {});
  const diagnostics = page ? await page.evaluate(() => ({ storage: scopeFixture.storage(), calls: scopeFixture.calls,
    area: scopeFixture.view.activeAreaId, product: scopeFixture.view.scene.tubeDesigner.product,
    rightDraft: scopeFixture.view.tubeDesignerRightDraft,
    controls: [...document.querySelectorAll('input,select')].map(control => ({ value: control.value,
      attributes: [...control.attributes].map(attribute => [attribute.name, attribute.value]) })) })).catch(() => null) : null;
  writeFileSync(join(directory, 'report.json'), JSON.stringify({ passed: false, checks, browserErrors, error: error.stack, diagnostics }, null, 2));
  throw error;
} finally { await browser.close(); }
