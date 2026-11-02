// Actual licensed Release standalone; native templates, machining, save and export.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { mkdirSync, writeFileSync, readFileSync, readdirSync, statSync } from 'node:fs';
import { join, resolve } from 'node:path';
const require = createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const { chromium } = require('playwright');
const root = resolve(process.env.ICAX_MOUNTING_EVIDENCE || 'output/tests/minimal-grille-installation-holes-20261027/native');
const throughAcceptance = process.env.ICAX_THROUGH_ACCEPTANCE === '1';
const receiverConnectivity = process.env.ICAX_RECEIVER_CONNECTIVITY === '1';
mkdirSync(root, { recursive: true });
const browser = await (async () => {
  for (let attempt=0; ; attempt++) {
    try { return await chromium.connectOverCDP(process.env.ICAX_MOUNTING_CDP || 'http://127.0.0.1:19478'); }
    catch (error) { if(attempt>=24)throw error; await new Promise(resolve=>setTimeout(resolve,500)); }
  }
})();
const page = browser.contexts()[0].pages().find(p => p.url().includes('/AppShell/index.html'));
assert(page);
page.setDefaultTimeout(120000);
const report = { passed: false, standaloneRelease: true, nativeBusinessMocked: false,
  runtime: page.url(), cdpEndpoint: process.env.ICAX_MOUNTING_CDP,
  processId: Number(process.env.ICAX_MOUNTING_PROCESS_ID), cases: [], checks: [], pageErrors: [] };
page.on('pageerror', error => report.pageErrors.push(error.message));
const write = () => writeFileSync(join(root, 'acceptance-report.json'), JSON.stringify(report, null, 2));
const walk = path => readdirSync(path).flatMap(n => { const p = join(path, n); return statSync(p).isDirectory() ? walk(p) : [p]; });
const init = () => page.evaluate(async () => {
  const { AppProxy } = await import('../../AppProxy/AppProxy.mjs');
  const app = await AppProxy.create(window.icax ?? window.icaxNativeBridge,
    { sdo: { timeoutMs: 120000, initialCallId: 1980000000 } });
  await app.getState(); const product = app.getProduct('icax.tube-designer'); await product.getState();
  const project = product.getProject(window.__icaxWorkbench.getState().projectId);
  window.__mountingNative = { project, scene: project.getMainScene() };
});
const invoke = (name, args) => page.evaluate(({ name, args }) => window.__mountingNative.scene.invoke(name, args), { name, args });
const list = async () => {
  const raw = (await invoke('TubeDesigner.List', {})).tubeDesigner;
  const group = raw.manufacturingGroups.find(g => g.productEntityId === raw.product?.entityId);
  return { product: raw.product, members: raw.members, parts: group?.parts ?? [],
    manufacturingGroups: group ? [group] : [], specificationAnnotations: raw.specificationAnnotations };
};
const base = { frameType: 'two_vertical', handleEnabled: 'no', barLayoutMode: 'fixed_count', barCount: 2,
  firstBarTopOffset: 100, lastBarBottomOffset: 100,
  frameWidth: 40, frameDepth: 30, frameWallThickness: 1.5, frameCornerRadius: 2,
  innerWidth: 20, innerDepth: 10, innerWallThickness: 1, innerCornerRadius: 1,
  installHoleEnabled: 'yes', installHoleCountPerSide: 3, installHoleTopOffset: 50,
  installHoleBottomOffset: 50, installHoleLargeDiameter: 6, installHoleSmallDiameter: 4,
  installHoleMinimumWeb: 0.2, installHoleOrientation: 'side', jointMode: 'butt_weld' };
const create = async (name, changes = {}) => {
  console.log(JSON.stringify({ name, stage: 'create-preview' }));
  const path = join(root, name + '.ictd');
  await page.evaluate(({ name, path }) => window.__icaxAppShell.createProject({
    productId: 'icax.tube-designer', projectName: name, projectPath: path }), { name, path });
  await page.waitForFunction(() => window.__icaxWorkbench?.getState().projectId === window.__icaxAppShell.getState().activeProjectId);
  await init();
  const parameters = { ...base, ...changes };
  const preview = await invoke('TubeDesigner.GenerateProductTemplatePreview', { templateId: 'minimal-protective-grille', parameters });
  for (const [key, value] of Object.entries(parameters)) assert.deepEqual(preview.parameters[key], value);
  await invoke('TubeDesigner.GeneratePreview', { templateId: 'minimal-protective-grille', ...preview.parameters });
  const model = await list();
  assert.deepEqual(model.product.parameters, preview.parameters);
  assert.equal(model.product.modelOutdated, false);
  // Native creation does not synthesize a frontend click. Activate the real
  // instance through the same workbench operation used by the left-hand list.
  await page.evaluate(id => window.__icaxWorkbench.executeAreaAction('tube-designer-select-instance',
    { dataset: { tubeDesignerInstanceId: id } }), model.product.entityId);
  await page.waitForFunction(id => window.__icaxWorkbench.getTubeDesignerState().product?.entityId === id,
    model.product.entityId);
  const projectId = await page.evaluate(() => window.__icaxAppShell.getState().activeProjectId);
  assert(projectId);
  return { name, path, projectId };
};
const uiEdit = async (key, value) => {
  const entry = page.locator(`[data-cam-action="tube-designer-edit-scene-specification"][data-tube-designer-parameter-key="${key}"]`);
  await entry.waitFor({ state: 'visible', timeout: 15000 });
  const box = await entry.boundingBox(); assert(box);
  // Specification labels deliberately route pointer input through the canvas.
  // Use a real double-click at its screen position, as the standalone user does.
  await page.mouse.dblclick(box.x+box.width/2, box.y+box.height/2);
  const editor = page.locator(`[data-tube-designer-scene-specification-input="${key}"]`);
  await editor.waitFor({ state: 'visible', timeout: 15000 });
  if (await editor.evaluate(n => n.tagName === 'SELECT')) await editor.selectOption(String(value));
  else { await editor.fill(String(value)); await editor.press('Enter'); }
  await page.locator('[data-cam-action="tube-designer-confirm-update"]').click();
  await page.waitForFunction(({ key, value }) => { const p = window.__icaxWorkbench.getTubeDesignerState().product;
    return !p?.modelOutdated && p.parameters[key] === value; }, { key, value });
  await page.waitForFunction(() => !document.querySelector('[data-cam-action="tube-designer-confirm-update"]'));
  await page.waitForFunction(() => !window.__icaxWorkbench.getTubeDesignerProfileLibraryState().pending);
};
const finish = async state => {
  console.log(JSON.stringify({ name: state.name, stage: 'native-disassemble-export' }));
  let model = await list(); const id = model.product.entityId;
  await page.evaluate(() => window.__icaxWorkbench.executeAreaAction('tube-designer-disassemble-active-product'));
  model = await list();
  assert(!model.product.partsOutdated); assert(model.parts.length > 0);
  const quantity = model.parts.reduce((n, p) => n + p.quantity, 0);
  assert.equal(quantity, model.members.length);
  const parameters = model.product.parameters;
  const facts = model.parts[0].properties['tubeDesigner.minimalGrille'].installationHoles;
  assert.equal(facts.enabled, parameters.installHoleEnabled === 'yes');
  if (facts.enabled) {
    assert.equal(facts.groups.length, parameters.installHoleCountPerSide * 2);
    const expected = Array.from({ length: parameters.installHoleCountPerSide }, (_, i) =>
      parameters.installHoleBottomOffset + i * (parameters.height - parameters.installHoleTopOffset - parameters.installHoleBottomOffset)
        / (parameters.installHoleCountPerSide - 1));
    facts.centers.forEach((value, i) => assert(Math.abs(value - expected[i]) < 1e-7));
  } else { assert.deepEqual(facts.groups, []); }
  const measurements = [];
  for (const part of model.parts.filter(p => p.stableKey.startsWith('frame.left.') || p.stableKey.startsWith('frame.right.'))) {
    const measurement = await invoke('TubeDesigner.MeasurePartGeometry', {
      partEntityId: part.entityId, resourceVersion: part.manufacturingGeometryResourceVersion });
    assert(measurement.available && measurement.source === 'final-brep');
    measurements.push({ part: part.name, measurement });
  }
  const target = join(root, state.name + '-export'); mkdirSync(target, { recursive: true });
  await invoke('TubeDesigner.ExportSelected', { partEntityIds: model.parts.map(p => p.entityId), targetDirectory: target });
  const files = walk(target); assert.equal(files.filter(p => /\.step$/i.test(p)).length, model.parts.length);
  assert.equal(files.filter(p => /\.xlsx$/i.test(p)).length, 1);
  const saved = await page.evaluate(path => window.__mountingNative.project.save(path), state.path);
  assert(saved.saved);
  await page.evaluate(async () => { await window.__icaxWorkbench.waitForRenderableViewport();
    await window.__icaxWorkbench.setStandardView('front'); await window.__icaxWorkbench.fitView(); });
  await page.screenshot({ path: join(root, state.name + '.png') });
  const full = { ...state, model, target: files.find(p => /\.xlsx$/i.test(p)).replace(/[\\/][^\\/]+$/, ''), measurements, files, passed: true };
  const reportPath = join(root, state.name + '-report.json'); writeFileSync(reportPath, JSON.stringify(full, null, 2));
  report.cases.push({ ...state, passed: true, reportPath, members: model.members.length,
    physicalQuantity: quantity, partTypes: model.parts.length, groupsPerSide: facts.countPerSide });
  write(); console.log(JSON.stringify(report.cases.at(-1))); return full;
};
try {
  await page.waitForFunction(() => window.__icaxAppShell?.getState().startupPhase === 'ready');
  const cdp = await page.context().newCDPSession(page);
  await cdp.send('Emulation.setFocusEmulationEnabled', { enabled: true });
  await page.setViewportSize({ width: 1600, height: 1000 });
  const first = await create('three-groups-ui-enabled', { installHoleEnabled: 'no' });
  await uiEdit('installHoleEnabled', 'yes');
  const firstFull = await finish(first); const originalId = firstFull.model.product.entityId;
  await uiEdit('installHoleCountPerSide', 5); await uiEdit('installHoleTopOffset', 150);
  const edited = await finish({ name: 'five-groups-existing-instance-edited', path: join(root, 'five-groups-existing-instance-edited.ictd') });
  assert.equal(edited.model.product.entityId, originalId);
  const top = edited.model.specificationAnnotations.find(a => a.parameter === 'installHoleTopOffset');
  assert(top && Math.abs(top.start[2] - top.end[2]) === 150);
  report.checks.push({ name: 'real annotation edits preserve instance and recompute native linear anchors', passed: true }); write();
  await finish(await create('three-groups-male-female', { jointMode: 'male_female_half_hole', maleHeadLength: 3, maleHeadWidth: 6 }));
  await finish(await create('three-groups-closed-frame', { frameType: 'closed_frame' }));
  await finish(await create('three-groups-bottom-rail', { frameType: 'bottom_rail' }));
  await finish(await create('front-three-groups', { installHoleOrientation: 'front', frameWidth: 10,
    frameDepth: 5, frameWallThickness: 0.8, frameCornerRadius: 1,
    firstBarTopOffset: 55, lastBarBottomOffset: 55, installHoleTopOffset: 55, installHoleBottomOffset: 55 }));
  await finish(await create('open-slot-two-groups', { handleEnabled: 'yes', handleSide: 'left', handleClosure: 'open_slot', installHoleCountPerSide: 2 }));
  if (throughAcceptance) {
    const editable = await create('through-both-ends-ui', { jointMode: 'butt_weld', maleHeadLength: -99,
      maleHeadWidth: 6 });
    // Direct native generation has no resource selection UI. Bind the actual
    // selected A/B resources before isolating a manufacturing-only mode edit.
    const materialEditor = page.locator('[data-tube-designer-parameter="frameWidth"]');
    await materialEditor.scrollIntoViewIfNeeded();
    await materialEditor.fill('40');
    await materialEditor.dispatchEvent('change');
    await page.waitForFunction(() => Boolean(window.__icaxWorkbench.getTubeDesignerState()
      .product?.parameters.tubeDesignerProfileOverrides?.frame));
    const regenerate = page.locator('[data-cam-action="tube-designer-confirm-update"]');
    if (await page.evaluate(() => window.__icaxWorkbench.getTubeDesignerState().product?.modelOutdated))
      await regenerate.click();
    await page.waitForFunction(() => !window.__icaxWorkbench.getTubeDesignerState().product?.modelOutdated);
    await page.waitForFunction(() => !document.querySelector('[data-cam-action="tube-designer-confirm-update"]'));
    await page.waitForFunction(() => !window.__icaxWorkbench.getTubeDesignerProfileLibraryState().pending);
    const jointEditor = page.locator('[data-tube-designer-parameter="jointMode"]');
    await jointEditor.selectOption('male_female_through_hole');
    await page.waitForFunction(() => window.__icaxWorkbench.getTubeDesignerState().product?.parameters.jointMode
      === 'male_female_through_hole');
    const changed = await list();
    assert.equal(changed.product.modelOutdated, false);
    assert.equal(changed.product.parameters.maleHeadLength, -99);
    assert.equal(await page.locator('[data-tube-designer-parameter="maleHeadLength"]').count(), 0);
    await finish(editable);
    report.checks.push({ name: 'real right-panel through edit preserves inactive length and display model', passed: true }); write();
    await finish(await create('through-open-slot-round', { jointMode: 'male_female_through_hole',
      maleHeadLength: -99, maleHeadWidth: 6, maleCornerType: 'round', maleCornerSize: 0.5,
      handleEnabled: 'yes', handleSide: 'left', handleClosure: 'open_slot', installHoleCountPerSide: 2 }));
    await finish(await create('through-larger-receiver-chamfer', { jointMode: 'male_female_through_hole',
      maleHeadLength: 9999, maleHeadWidth: 6, maleCornerType: 'chamfer', maleCornerSize: 0.5,
      frameWidth: 50, frameDepth: 40, frameWallThickness: 2, frameCornerRadius: 3 }));
    await finish(await create('through-front-installation', { jointMode: 'male_female_through_hole',
      maleHeadLength: -99, maleHeadWidth: 6, installHoleOrientation: 'front' }));
  }
  if (receiverConnectivity) {
    const noMounting = { installHoleEnabled: 'no', jointMode: 'male_female_half_hole', maleHeadWidth: 6, maleHeadLength: 3 };
    const cornerProfiles = JSON.parse(readFileSync(join(root,'..','coherent-corner-profile.json'),'utf8'));
    await finish(await create('half-default-cross-corners', { ...noMounting,
      frameWidth: 10, frameDepth: 5, frameWallThickness: 0.8, frameCornerRadius: 1,
      innerWidth: 10, innerDepth: 5, innerWallThickness: 0.8, innerCornerRadius: 1,
      maleHeadLength: 2, maleHeadWidth: 2 }));
    await finish(await create('half-large-corners', { ...noMounting, frameCornerRadius: 12,
      tubeDesignerProfileOverrides: cornerProfiles }));
    await finish(await create('through-large-corners', { ...noMounting, frameCornerRadius: 12,
      tubeDesignerProfileOverrides: cornerProfiles, jointMode: 'male_female_through_hole' }));
    await finish(await create('half-open-to-end', { ...noMounting, firstBarTopOffset: 25,
      lastBarBottomOffset: 25, holeTotalClearanceHeight: 54 }));
    await finish(await create('through-overlapping-mouths', { ...noMounting, jointMode: 'male_female_through_hole',
      height: 250, barCount: 4, firstBarTopOffset: 50, lastBarBottomOffset: 50, holeTotalClearanceHeight: 45 }));
  }
  for (const [name, changes, message] of [
    ['too-narrow-side', { frameWidth: 10, frameDepth: 5, frameWallThickness: 0.8, frameCornerRadius: 1 }, /平直区域/],
    ['rail-contact-overlap', { firstBarTopOffset: 55, lastBarBottomOffset: 55 }, /接头|转角/],
    ...(throughAcceptance ? [['through-front-screw-blocked', { jointMode: 'male_female_through_hole',
      maleHeadLength: -99, maleHeadWidth: 6, installHoleOrientation: 'front',
      firstBarTopOffset: 50, lastBarBottomOffset: 50 }, /公头遮挡/]] : []),
    ...(receiverConnectivity ? [
      ['through-disconnected-default', { installHoleEnabled: 'no', jointMode: 'male_female_through_hole',
        frameWidth: 10, frameDepth: 5, frameWallThickness: 0.8, frameCornerRadius: 1,
        innerWidth: 10, innerDepth: 5, innerWallThickness: 0.8, innerCornerRadius: 1,
        maleHeadWidth: 2 }, /最终加工形状无效/],
      ['through-disconnected-wide-slot', { installHoleEnabled: 'no', jointMode: 'male_female_through_hole',
        maleHeadWidth: 6, holeTotalClearanceDepth: 25 }, /最终加工形状无效/],
    ] : []),
  ]) {
    await create(name, changes); let error = '';
    try { await invoke('TubeDesigner.DisassembleSelected', { productEntityIds: [(await list()).product.entityId] }); }
    catch (e) { error = e.message; }
    assert.match(error, message); assert.equal((await list()).parts.length, 0);
    report.checks.push({ name: 'native rejects ' + name, passed: true, error }); write();
  }
  const frozen = firstFull.model;
  // The project identifier differs from the product entity identifier. A clean
  // project closes directly; only dirty projects show the save/discard dialog.
  await page.evaluate(id => {
    window.__mountingCloseDone = false;
    window.__mountingClosePromise = window.__icaxAppShell.closeProject(id)
      .then(result => { window.__mountingCloseDone = true; return result; });
  }, first.projectId);
  await page.waitForFunction(id => window.__mountingCloseDone
    || document.querySelector(`[data-project-close-dialog="${id}"]`), first.projectId);
  const discard = page.locator(`[data-project-close-dialog="${first.projectId}"] [data-project-close-choice="discard"]`);
  if (await discard.isVisible()) await discard.click();
  const closed = await page.evaluate(() => window.__mountingClosePromise);
  assert.notEqual(closed.activeProjectId, first.projectId);
  await page.evaluate(path => window.__icaxAppShell.openProject(path, 'icax.tube-designer'), first.path);
  await page.waitForFunction(path => {
    const app = window.__icaxAppShell.getState();
    return app.pendingCount === 0 && app.activeProjectPath.replaceAll('\\', '/') === path.replaceAll('\\', '/')
      && window.__icaxWorkbench.getState().projectId === app.activeProjectId;
  }, first.path);
  await init(); const reopened = await list();
  assert.deepEqual(reopened.product.parameters, frozen.product.parameters);
  assert.deepEqual(reopened.parts.map(p => [p.entityId, p.quantity]), frozen.parts.map(p => [p.entityId, p.quantity]));
  report.checks.push({ name: 'saved installation project reopens with parameters, part IDs and quantities', passed: true });
  assert.deepEqual(report.pageErrors, []); report.passed = true; write();
} catch (error) { report.error = error.stack; write(); throw error; }
finally { await browser.close(); }
