// Real browser controls and production automation; native business is covered separately.
import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { readFileSync, writeFileSync } from 'node:fs';
import { browserReportDirectory, importBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const { nestingSettingsCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/nestingSettings.css.mjs');
const { batchExcelAutomationCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/batchExcelAutomation.css.mjs');
const output = browserReportDirectory('batch-excel-automation/browser');
const features = JSON.parse(readFileSync(new URL('../../licensing/features.json', import.meta.url))).features;
const browser = await chromium.launch({ headless: true, channel: 'chrome' });
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 950 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://auto-excel.test/**', serveBrowserAsset);
  await page.goto('http://auto-excel.test/');
  await page.addStyleTag({ content: `${nestingSettingsCss}${batchExcelAutomationCss}
    .cam-workbench{display:flex;gap:20px}.cam-context-pane,.cam-info-pane{width:350px;height:200px;overflow:auto}
    .nested{height:160px;overflow:auto}.spacer{height:800px}` });
  await page.evaluate(async features => {
    const automation = await import('/src/apps/tube-designer/webpage/batchExcelAutomation.mjs');
    const { ensureLicenseStatus } = await import('/src/apps/tube-designer/webpage/licensing.mjs');
    const { capturePaneInteraction } = await import('/src/apps/_shared/workbench/utils/paneInteractionState.mjs');
    const { patchDomNode } = await import('/src/apps/tube-designer/webpage/punchDomPatch.mjs');
    document.body.innerHTML = '<main><div class="cam-workbench"><aside class="cam-context-pane"><div class="nested"><input data-field="left-note" value="left draft"><div class="spacer"></div></div><div class="spacer"></div></aside><div class="cam-viewport"><canvas></canvas></div><aside class="cam-info-pane"><div class="nested"><input data-field="right-note" value="right draft"><div class="conditional"></div><div class="spacer"></div></div><div class="spacer"></div></aside></div><div id="dialog-host"></div></main>';
    const mount = document.querySelector('main');
    const f = { automation, mount, calls: [], logs: [], hold: null, failRow: false, race: false,
      settings: { enabled: false, inputDirectory: 'C:/incoming', tempDirectory: '', outputDirectory: 'C:/finished' }, files: [],
      directorySelections: [], claimedFiles: [],
      viewRefreshes: [], fits: [], trace: [], activeRefreshes: [], renderCount:0, camera: { radius: 456, theta: 0.25 } };
    f.designer = id => ({ product: { entityId:id,templateId:'window',parameters:{ assemblyClearance:0.1 },activeGenerationRunId:`run:${id}` },
      generationRun: { entityId:`run:${id}` }, members:[{ entityId:`${id}:member` }],
      templates:[{ id:'window',available:true,descriptorLoaded:true,extensions:{},groups:[],
        parameters:[{ key:'assemblyClearance',valueType:'number',defaultValue:0.1 }] }] });
    f.nativeDesigner = f.designer('previous');
    f.view = { tubeDesignerLoaded: true, activeAreaId: 'view', scene: { tubeDesigner: { product: { entityId: 'previous' } } } };
    f.context = { mount, project: { projectId: 'automation' }, appProxy: { bridge: {
      openDirectoryDialog: async options => {
        f.directorySelections.push(structuredClone(options));
        if (f.holdPicker) await new Promise(resolve => { f.releasePicker = resolve; });
        if (options.title === '选择自动处理临时目录（temp）') return 'C:/working';
        throw new Error('Unexpected directory picker title: ' + options.title);
      },
    } }, productProxy: { async invoke(method, payload) {
      f.calls.push({ scope: 'product', method, payload });
      if (method === 'TubeDesignerLicensing.Status') return { featureSchemaVersion: 1, activated: true,
        featureCatalog: features.map(({ id, label, parent }) => ({ id, label, parent })),
        capabilities: Object.fromEntries(features.map(feature => [feature.id, true])) };
      if (['GeneratePreview', 'DisassembleSelected', 'ExportSelected'].some(name => method.endsWith(name)))
        throw new Error('Product channel must reject scene business');
      if (method.endsWith('GetBatchExcelAutomationSettings')) {
        if (f.holdGet) await new Promise(resolve => { f.releaseGet = resolve; });
        return { settings: structuredClone(f.settings) };
      }
      if (method.endsWith('SaveBatchExcelAutomationSettings')) {
        const settings = payload.settings;
        if (settings.enabled && !settings.tempDirectory) throw new Error('启用自动处理前请设置临时目录。');
        for (const [left, right] of [['inputDirectory', 'tempDirectory'], ['inputDirectory', 'outputDirectory'], ['tempDirectory', 'outputDirectory']]) {
          const labels = { inputDirectory: '侦听目录', tempDirectory: '临时目录', outputDirectory: '输出目录' };
          const a = settings[left].replaceAll('\\', '/').replace(/\/+$/, '').toLowerCase();
          const b = settings[right].replaceAll('\\', '/').replace(/\/+$/, '').toLowerCase();
          if (a && b && (a === b || a.startsWith(b + '/') || b.startsWith(a + '/')))
            throw new Error(`${labels[left]}和${labels[right]}不能相同或互相包含。`);
        }
        f.settings = structuredClone(payload.settings); return { settings: f.settings };
      }
      if (method.endsWith('ScanBatchExcelAutomation')) {
        if (f.hold) await new Promise(resolve => { f.releaseScan = resolve; });
        return { settings: structuredClone(f.settings), files: structuredClone(f.files), errors: [] };
      }
      if (method.endsWith('ClaimBatchExcelAutomation')) {
        if (f.race) return { claimed: false, reason: 'already claimed' };
        const file = f.files.find(file => file.sourcePath === payload.sourcePath && file.fileToken === payload.fileToken);
        if (!file) throw new Error('Expected current input candidate to be claimed');
        const claimed = { claimed: true, sourcePath: `${f.settings.tempDirectory}/${file.fileToken}/${file.sourceName}`,
          originalSourcePath: file.sourcePath, fileToken: file.fileToken, sourceName: file.sourceName,
          targetDirectory: `${f.settings.outputDirectory}/${file.fileToken}` };
        f.files = f.files.filter(candidate => candidate !== file);
        f.claimedFiles.push(claimed);
        return claimed;
      }
      if (method.endsWith('ReadBatchExcelImport')) return { templateId: 'window', rows: [
        { sourceRow: 3, instanceName: 'first', instanceQuantity: 2, parameters: { assemblyClearance: 0.05 } },
        { sourceRow: 4, instanceName: 'second', instanceQuantity: 3, parameters: { assemblyClearance: 0.2 } },
      ] };
      if (method.endsWith('CompleteBatchExcelAutomation')) { f.files = []; return {}; }
      throw new Error(method);
    } }, sceneProxy: { async invoke(method, payload) {
      f.calls.push({ scope: 'scene', method, payload });
      if (method.endsWith('GeneratePreview')) {
        if (f.failRow && payload.instanceName === 'second') throw new Error('invalid row');
        f.nativeDesigner = f.designer(payload.instanceName);
        return { tubeDesigner: structuredClone(f.nativeDesigner) };
      }
      if (method.endsWith('DisassembleSelected')) return { tubeDesigner: { manufacturingGroups: payload.productEntityIds
        .map(id => ({ productEntityId: id, parts: [{ entityId: `${id}:part` }] })) } };
      if (method.endsWith('ExportSelected')) {
        if (f.holdExport) await new Promise(resolve => { f.releaseExport = resolve; });
        return { exportedCount: 2, exportedFiles: ['C:/finished/new-unique/first.step', 'C:/finished/new-unique/second.step'],
          partListFile: 'C:/finished/new-unique/零件清单.xlsx' };
      }
      if (method.endsWith('ActivateProduct')) { f.nativeDesigner=f.designer(payload.productEntityId);return {}; }
      if (method.endsWith('List')) {
        if (f.holdList) await new Promise(resolve => { f.releaseList=resolve; });
        return { tubeDesigner:structuredClone(f.nativeDesigner) };
      }
      throw new Error(method);
    } }, actions: {} };
    await ensureLicenseStatus(f.context, f.view);
    f.calls.length = 0;
    f.render = () => {
      f.renderCount++;
      const restore = capturePaneInteraction(mount);
      const old = mount.querySelector('#dialog-host');
      const next = old.cloneNode(false); next.innerHTML = automation.renderBatchExcelAutomationDialog(f.view);
      patchDomNode(old, next);
      // Conditional fields change during an asynchronous response.
      const conditional = mount.querySelector('.conditional');
      conditional.innerHTML = f.conditional ? '<input data-field="conditional-field" value="conditional">' : '';
      restore();
    };
    f.ops = { renderProject: f.render, appendProjectLog: (context, level, message) => f.logs.push({ level, message, projectId:context.project?.projectId }),
      refreshDesignerState: async () => { f.trace.push('designer'); f.view.scene.tubeDesigner=structuredClone(f.nativeDesigner);f.conditional = true; f.render(); },
      refreshActiveAreaView: async (context, view, expectation) => {
        f.trace.push('view');f.viewRefreshes.push(structuredClone(expectation));f.publishedProduct=view.scene.tubeDesigner.product.entityId;
        const revision=`revision:${f.viewRefreshes.length}`;
        return { revision,viewportReceipt:{ applied:true,revision,entityIds:[...expectation.expectedEntityIds] } };
      },
      fitDesignerDefaultView(view,revision,product) { f.trace.push('fit');f.fits.push({ revision,id:product.entityId });f.camera={ radius:1000,theta:0.1 }; },
    };
    f.context.actions.refreshActiveSceneState = async expected => {
      f.trace.push('acknowledge'); f.activeRefreshes.push({ correctScene:expected.sceneProxy===f.context.sceneProxy,
        projectId:expected.projectId, ownMutation:f.view.tubeDesignerOwnMutation===true });f.render();
    };
    mount.addEventListener('click', event => {
      const target = event.target.closest('[data-cam-action]');
      if (target) f.lastAction = automation.handleBatchExcelAutomationAction(f.context, f.view, target.dataset.camAction, target, f.ops);
    });
    mount.addEventListener('change', event => {
      const target = event.target.closest('[data-cam-action]');
      if (target) f.lastAction = automation.handleBatchExcelAutomationAction(f.context, f.view, target.dataset.camAction, target, f.ops);
    });
    f.nodes = [...mount.querySelectorAll('canvas,[data-field$="note"]')];
    let listenerCount = 0;
    f.nodes[1].addEventListener('input', () => { listenerCount++; });
    f.listenerCount = () => listenerCount;
    globalThis.autoFixture = f;
  }, features);
  await page.evaluate(async () => {
    const f = autoFixture;
    await f.automation.handleBatchExcelAutomationRibbonCommand(f.context, f.view, 'designer.excel.automation-settings', f.ops);
  });
  await page.getByLabel('启用自动处理', { exact: true }).check();
  await page.getByLabel('侦听目录', { exact: true }).fill('C:/incoming');
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await page.evaluate(() => autoFixture.lastAction);
  assert.match(await page.getByRole('alert').textContent(), /临时目录/);
  await page.evaluate(() => { autoFixture.holdPicker = true; });
  await page.locator('[data-directory-field="tempDirectory"]').click();
  await page.waitForFunction(() => Boolean(autoFixture.releasePicker));
  await page.evaluate(async () => {
    const f = autoFixture;
    const input = f.mount.querySelector('[data-field="right-note"]');
    input.focus({ preventScroll: true }); input.setSelectionRange(1, 4, 'backward');
    f.mount.querySelector('.cam-context-pane').scrollTop = 205;
    f.mount.querySelector('.cam-context-pane .nested').scrollTop = 145;
    f.mount.querySelector('.cam-info-pane').scrollTop = 155;
    f.mount.querySelector('.cam-info-pane .nested').scrollTop = 115;
    f.releasePicker(); await f.lastAction; f.holdPicker = false;
    if (document.activeElement !== input || input.selectionStart !== 1 || input.selectionEnd !== 4 || input.selectionDirection !== 'backward')
      throw new Error('Directory picker must retain latest input focus and selection');
    if (f.mount.querySelector('.cam-context-pane').scrollTop !== 205 || f.mount.querySelector('.cam-context-pane .nested').scrollTop !== 145
      || f.mount.querySelector('.cam-info-pane').scrollTop !== 155 || f.mount.querySelector('.cam-info-pane .nested').scrollTop !== 115)
      throw new Error('Directory picker must retain all latest pane and nested scroll positions');
  });
  assert.equal(await page.getByLabel('临时目录', { exact: true }).inputValue(), 'C:/working');
  await page.getByLabel('临时目录', { exact: true }).fill('C:/incoming');
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await page.evaluate(() => autoFixture.lastAction);
  assert.match(await page.getByRole('alert').textContent(), /侦听目录和临时目录不能相同/);
  await page.getByLabel('临时目录', { exact: true }).fill('C:/finished');
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await page.evaluate(() => autoFixture.lastAction);
  assert.match(await page.getByRole('alert').textContent(), /临时目录和输出目录不能相同/);
  await page.getByLabel('临时目录', { exact: true }).fill('C:/incoming/temp');
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await page.evaluate(() => autoFixture.lastAction);
  assert.match(await page.getByRole('alert').textContent(), /互相包含/);
  await page.getByLabel('临时目录', { exact: true }).fill('C:/working');
  await page.getByLabel('输出目录', { exact: true }).fill('C:/incoming');
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await page.evaluate(() => autoFixture.lastAction);
  assert.match(await page.getByRole('alert').textContent(), /不能相同/);
  await page.getByLabel('输出目录', { exact: true }).fill('C:/finished');
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await page.waitForFunction(() => !autoFixture.view.tubeDesignerExcelAutomationDialog);
  const results = await page.evaluate(async () => {
    const f = autoFixture, a = f.automation;
    const check = (value, message) => { if (!value) throw new Error(message); };
    check(f.settings.tempDirectory === 'C:/working', 'Save all three configured directories');
    check(f.directorySelections.length === 1 && f.directorySelections[0].title === '选择自动处理临时目录（temp）'
      && f.directorySelections[0].initialDirectory === '', 'Choose the independent temp directory');
    const input = f.mount.querySelector('[data-field="right-note"]');
    f.files = [{ sourcePath: 'C:/incoming/new.xlsx', fileToken: 'token', sourceName: 'new.xlsx' }];
    f.view.tubeDesignerRightDraftDirty = true;
    const before = f.calls.length;
    await a.runBatchExcelAutomationOnce(f.context, f.view, f.ops);
    check(f.calls.length === before, 'Unsaved draft must defer automation');
    f.view.tubeDesignerRightDraftDirty = false;
    f.hold = true;
    const scanning = a.runBatchExcelAutomationOnce(f.context, f.view, f.ops);
    await new Promise(resolve => setTimeout(resolve, 0));
    check(await a.runBatchExcelAutomationOnce(f.context, f.view, f.ops) === false, 'Directory scan must be single flight');
    input.focus({ preventScroll: true }); input.setSelectionRange(1, 4);
    f.mount.querySelector('.cam-context-pane').scrollTop = 130;
    f.mount.querySelector('.cam-info-pane').scrollTop = 90;
    f.mount.querySelector('.cam-info-pane .nested').scrollTop = 100;
    f.releaseScan(); await scanning; f.hold = false;
    check(!f.calls.some(c => c.method.endsWith('ClaimBatchExcelAutomation')), 'Editing begun while scanning must defer claim');
    const pendingBefore = f.calls.length;
    f.holdGet = true;
    const opening = a.handleBatchExcelAutomationRibbonCommand(f.context, f.view, 'designer.excel.automation-settings', f.ops);
    await new Promise(resolve => setTimeout(resolve, 0));
    input.focus({ preventScroll: true }); input.value = '12345'; input.setSelectionRange(1, 4, 'backward');
    f.mount.querySelector('.cam-context-pane').scrollTop = 210;
    f.mount.querySelector('.cam-info-pane').scrollTop = 125;
    f.mount.querySelector('.cam-info-pane .nested').scrollTop = 155;
    f.conditional = true; f.releaseGet(); await opening; f.holdGet = false;
    check(document.activeElement === input && input.selectionStart === 1 && input.selectionEnd === 4, 'Async settings response must preserve input selection');
    check(f.mount.querySelector('.cam-context-pane').scrollTop === 210 && f.mount.querySelector('.cam-info-pane').scrollTop === 125
      && f.mount.querySelector('.cam-info-pane .nested').scrollTop === 155, 'Latest left/right/nested scroll must survive');
    await a.handleBatchExcelAutomationAction(f.context, f.view, 'tube-designer-excel-automation-cancel', null, f.ops);
    input.blur();
    f.race = true;
    check(await a.runBatchExcelAutomationOnce(f.context, f.view, f.ops) === false, 'Other window can win claim');
    check(!f.view.pending && !f.view.tubeDesignerOperation, 'Lost claim must release UI without creating products');
    f.race = false;
    check(await a.runBatchExcelAutomationOnce(f.context, f.view, f.ops) === true, 'Complete automatic workflow');
    const generated = f.calls.filter(c => c.method.endsWith('GeneratePreview'));
    check(generated.length === 2 && generated.every(c => c.scope === 'scene'), 'Every row created in scene');
    check(generated[0].payload.instanceQuantity === 2 && generated[0].payload.assemblyClearance === 0.05, 'Quantity and assembly fields reach creation');
    const exportCall = f.calls.find(c => c.method.endsWith('ExportSelected'));
    check(JSON.stringify(exportCall.payload.partEntityIds) === JSON.stringify(['first:part', 'second:part']), 'Export only parts from imported workbook');
    check(f.calls.some(c => c.method.endsWith('ActivateProduct') && c.payload.productEntityId === 'previous'), 'Retain previous active instance');
    check(f.calls.find(c => c.method.endsWith('CompleteBatchExcelAutomation')).payload.status === 'succeeded', 'Record success only after export');
    check(f.calls.find(c => c.method.endsWith('ReadBatchExcelImport')).payload.sourcePath === 'C:/working/token/new.xlsx', 'Read the claimed workbook from configured temp');
    check(f.calls.find(c => c.method.endsWith('CompleteBatchExcelAutomation')).payload.sourcePath === 'C:/incoming/new.xlsx', 'Complete by original input identity');
    check(f.files.length === 0 && f.claimedFiles[0].sourcePath === 'C:/working/token/new.xlsx', 'Claim empties input and retains temp source');
    check(f.publishedProduct === 'previous' && f.viewRefreshes.length === 1, 'Restored active product is explicitly published after snapshot refresh');
    check(JSON.stringify(f.viewRefreshes[0]) === JSON.stringify({ correlationId:'run:previous',expectedEntityIds:['previous:member'] }), 'View waits for actual active generation and members');
    check(f.fits.length === 0 && f.camera.radius === 456 && f.camera.theta === 0.25, 'Existing product retains user camera');
    check(JSON.stringify(f.trace) === JSON.stringify(['designer','view','acknowledge']), 'Publish after designer snapshot, before history acknowledgement');
    check(f.activeRefreshes.every(r => r.correctScene && r.projectId==='automation' && r.ownMutation), 'History refresh is guarded by scene and project with own mutation');
    f.calls = []; f.failRow = true; f.files = [{ sourcePath: 'C:/incoming/bad.xlsx', fileToken: 'bad', sourceName: 'bad.xlsx' }];
    check(await a.runBatchExcelAutomationOnce(f.context, f.view, f.ops) === false, 'Invalid row fails workflow');
    check(!f.calls.some(c => c.method.endsWith('DisassembleSelected') || c.method.endsWith('ExportSelected')), 'Do not export incomplete workbook');
    const failure = f.calls.find(c => c.method.endsWith('CompleteBatchExcelAutomation'));
    check(failure.payload.status === 'failed' && /第 4 行/.test(failure.payload.error), 'Persist failure and original worksheet row');
    check(f.files.length === 0 && f.claimedFiles.at(-1).sourcePath === 'C:/working/bad/bad.xlsx', 'Failed processing retains claimed temp source');
    check(failure.payload.sourcePath === 'C:/incoming/bad.xlsx' && failure.payload.fileToken === 'bad', 'Failure records the original candidate identity');
    check(!f.view.pending && !f.view.tubeDesignerOperation, 'Failure must release operation state');
    check(f.nodes.every(node => node.isConnected), 'Canvas and both input nodes remain mounted');
    f.nodes[1].dispatchEvent(new Event('input', { bubbles: true }));
    check(f.listenerCount() === 1, 'Original input event listener remains attached');
    f.calls=[];f.trace=[];f.fits=[];f.viewRefreshes=[];f.failRow=false;
    f.view.scene.tubeDesigner={};f.nativeDesigner={};f.files=[{ sourcePath:'C:/incoming/fresh.xlsx',fileToken:'fresh',sourceName:'fresh.xlsx' }];
    check(await a.runBatchExcelAutomationOnce(f.context,f.view,f.ops) === true,'First automatically created product completes');
    check(f.publishedProduct === 'second' && f.fits.length === 1 && f.fits[0].id === 'second', 'First automation publishes and fits the actual active product');
    check(f.fits[0].revision === 'revision:1' && JSON.stringify(f.viewRefreshes[0]) === JSON.stringify({ correlationId:'run:second',expectedEntityIds:['second:member'] }), 'Fit uses the just-published actual View revision');
    check(JSON.stringify(f.trace) === JSON.stringify(['designer','view','fit','acknowledge']), 'Fresh product fits after publication and before acknowledgement');
    check(!f.view.tubeDesignerOwnMutation,'Own mutation marker clears after acknowledgement');
    // A project switch during export must not refresh or focus the new project.
    f.calls=[];f.trace=[];f.viewRefreshes=[];f.fits=[];f.files=[{ sourcePath:'C:/incoming/navigation.xlsx',fileToken:'navigation',sourceName:'navigation.xlsx' }];
    f.holdExport=true;
    const finishing = a.runBatchExcelAutomationOnce(f.context,f.view,f.ops);
    while (!f.releaseExport) await new Promise(resolve => setTimeout(resolve,0));
    const originalContext=f.context, originalScene=f.view.scene;
    const otherContext={ ...f.context,project:{ projectId:'other' },sceneProxy:{ invoke() { throw new Error('Old job used the new scene'); } } };
    f.view.scene={ tubeDesigner:{ product:{ entityId:'other-active' } } };
    f.view.tubeDesignerOperation={ kind:'new-project-operation' };f.view.pending=true;
    a.connectBatchExcelAutomation(otherContext,f.view,f.ops);a.stopBatchExcelAutomation();
    input.focus({ preventScroll:true });input.setSelectionRange(2,5);
    f.mount.querySelector('.cam-context-pane').scrollTop=260;f.mount.querySelector('.cam-info-pane').scrollTop=180;
    f.releaseExport();check(await finishing === true,'Old project export can finish in its original scene');f.holdExport=false;f.releaseExport=null;
    check(f.trace.length === 0 && f.fits.length === 0 && f.viewRefreshes.length === 0,'Late completion does not publish or refresh the new project');
    check(f.view.scene.tubeDesigner.product.entityId === 'other-active' && f.view.pending && f.view.tubeDesignerOperation.kind==='new-project-operation','Late completion does not overwrite new project state');
    check(document.activeElement === input && input.selectionStart===2 && input.selectionEnd===5
      && f.mount.querySelector('.cam-context-pane').scrollTop===260 && f.mount.querySelector('.cam-info-pane').scrollTop===180,'Late completion does not take focus or scroll');
    check(f.logs.at(-1).projectId==='automation','Completion log belongs to original project');
    f.context=originalContext;f.view.scene=originalScene;f.view.pending=false;f.view.tubeDesignerOperation=null;
    a.connectBatchExcelAutomation(f.context,f.view,f.ops);
    a.stopBatchExcelAutomation();
    // Manual Excel import also publishes and fits the last newly active model.
    const actions=await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    f.calls=[];f.trace=[];f.viewRefreshes=[];f.fits=[];
    f.context.appProxy.bridge.openFileDialog=async () => 'C:/incoming/manual.xlsx';
    f.ops.showNotice=() => {};
    f.view.viewport={
      setViewDirection() { f.trace.push('direction');return true; },
      fitViewForRevision(revision,padding) { f.trace.push('manual-fit');f.fits.push({ revision,padding });return { fitted:true,revision,renderSequence:42 }; },
      getAppliedViewState() { return { revision:`revision:${f.viewRefreshes.length}`,renderSequence:42 }; },
    };
    await actions.handleDesignerAreaAction(f.context,f.view,'tube-designer-batch-add',null,f.ops);
    check(f.view.scene.tubeDesigner.product.entityId==='second' && f.publishedProduct==='second','Manual import publishes last new active product');
    check(f.fits.length===1 && f.fits[0].padding===1.35 && f.fits[0].revision==='revision:1','Manual import fits just-published View with margin');
    check(JSON.stringify(f.trace)===JSON.stringify(['view','direction','manual-fit','acknowledge']),'Manual import publishes before default direction and fit');
    f.calls=[];f.trace=[];f.viewRefreshes=[];f.fits=[];f.holdList=true;
    f.view.sceneProxy=f.context.sceneProxy;
    const lateManual=actions.handleDesignerAreaAction(f.context,f.view,'tube-designer-batch-add',null,f.ops);
    while (!f.releaseList) await new Promise(resolve => setTimeout(resolve,0));
    // A same-project scene switch can update the view proxy before the old
    // context or scene snapshot changes. The old import must not fit it.
    f.view.sceneProxy={ invoke() { throw new Error('Old import used newly selected scene'); } };
    const lateScopeSnapshot={ product:{ entityId:'new-scope-active' },marker:'new scene snapshot' };
    f.view.scene.tubeDesigner=lateScopeSnapshot;
    input.focus({ preventScroll:true });input.setSelectionRange(1,3);
    f.mount.querySelector('.cam-context-pane').scrollTop=225;f.mount.querySelector('.cam-info-pane').scrollTop=145;
    f.releaseList();await lateManual;f.holdList=false;f.releaseList=null;
    check(f.trace.length===0 && f.viewRefreshes.length===0 && f.fits.length===0,'Late manual scene switch suppresses View publication, fit and acknowledgement');
    check(f.view.scene.tubeDesigner===lateScopeSnapshot && f.view.scene.tubeDesigner.marker==='new scene snapshot','Late manual List response must not overwrite new scope snapshot');
    check(document.activeElement===input && input.selectionStart===1 && input.selectionEnd===3
      && f.mount.querySelector('.cam-context-pane').scrollTop===225 && f.mount.querySelector('.cam-info-pane').scrollTop===145,'Late manual scene switch preserves focus and scroll');
    f.view.sceneProxy=f.context.sceneProxy;
    // Check the refresh itself independently of the existing operation wrapper,
    // which performs its own final render when an action completes.
    f.holdList=true;
    const lateRefresh=actions.refreshDesignerState(f.context,f.view,f.ops);
    while (!f.releaseList) await new Promise(resolve => setTimeout(resolve,0));
    f.view.sceneProxy={ invoke() { throw new Error('Old refresh used newly selected scene'); } };
    const refreshScopeSnapshot={ product:{ entityId:'next-scope-active' },marker:'next scene snapshot' };
    f.view.scene.tubeDesigner=refreshScopeSnapshot;
    const rendersBefore=f.renderCount;
    f.releaseList();check(await lateRefresh===false,'Refresh reports a superseded scope');f.holdList=false;f.releaseList=null;
    check(f.view.scene.tubeDesigner===refreshScopeSnapshot && f.view.scene.tubeDesigner.marker==='next scene snapshot','Old response must not write any new scope designer state');
    check(f.renderCount===rendersBefore,'Superseded refresh does not render');
    f.view.sceneProxy=f.context.sceneProxy;
    return { settings: f.settings, successRows: generated.length, inputNodeIdentity: true,
      canvasNodeIdentity: true, latestAsyncScrollAndSelection: true, conditionChanges: true,
      configuredTempDirectoryChosenAndSaved: true, directoryPickerPreservesLatestInteraction: true,
      threeDirectoryValidationErrors: true, readsClaimedTempSource: true, completionUsesOriginalInputIdentity: true,
      inputClearedOnClaimAndTempRetainedOnSuccessAndFailure: true,
      scopeSeparation: true, singleFlight: true, failureRow: failure.payload.error, pendingBefore,
      firstCreationPublishesThenFits:true,previousCameraPreserved:true,guardedHistoryAcknowledgement:true,
      navigationDuringExportDoesNotUpdateCurrentProject:true,
      manualImportPublishesAndFitsLastActiveModel:true,
      manualLateSameProjectSceneSwitchDoesNotPublishOrFit:true,
      lateRefreshDoesNotOverwriteNewScopeSnapshotOrRender:true,
      nativeBusinessResponsesMocked: true, standaloneCefEndToEnd: false };
  });
  assert.deepEqual(errors, []);
  await page.evaluate(async () => {
    const f = autoFixture;
    await f.automation.handleBatchExcelAutomationRibbonCommand(f.context, f.view, 'designer.excel.automation-settings', f.ops);
  });
  await page.screenshot({ path: resolve(output, 'settings.png'), fullPage: true });
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ status: 'passed', ...results }, null, 2));
  console.log('PASS Excel automation settings, scope-separated flow, single flight, row failure and browser interaction preservation');
} finally { await browser.close(); }
