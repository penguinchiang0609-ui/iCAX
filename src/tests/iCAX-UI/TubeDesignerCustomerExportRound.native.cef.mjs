// Continues the cold-open real standalone acceptance. Only picker paths are controlled.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { mkdirSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
const require = createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const { chromium } = require('playwright');
const output = resolve(process.env.ICAX_CUSTOMER_EXPORT_EVIDENCE || 'output/tests/customer-disassembly-export-20261023');
const browser = await chromium.connectOverCDP('http://127.0.0.1:19424');
const page = browser.contexts()[0].pages().find(p => p.url().includes('/AppShell/index.html'));
page.setDefaultTimeout(120000);
const report = { standaloneNative: true, businessMocked: false, pickerPathsControlled: true, checks: [] };
const state = () => page.evaluate(() => window.__icaxWorkbench.getTubeDesignerState());
const files = dir => readdirSync(dir).flatMap(n => { const p = join(dir, n); return statSync(p).isDirectory() ? files(p) : [p]; });
try {
  const squarePath = join(output, '客户视频规格-方管护栏.ictd');
  await page.evaluate(path => { (window.icax ?? window.icaxNativeBridge).openFileDialog = async () => path; }, squarePath);
  await page.locator('[data-action="open-start-center"]').click();
  await page.locator('[data-action="choose-project"]').click();
  await page.waitForFunction(() => window.__icaxWorkbench?.getTubeDesignerState().members.length === 32);
  const reopenedTarget = join(output, 'final-reopened-export'); mkdirSync(reopenedTarget, { recursive: true });
  await page.evaluate(path => { (window.icax ?? window.icaxNativeBridge).openDirectoryDialog = async () => path; }, reopenedTarget);
  await page.locator('[data-command-id="designer.export-active-product-parts"]').click();
  await page.waitForFunction(() => document.body.innerText.includes('final-reopened-export'));
  const square = await state();
  const squareGroup = square.manufacturingGroups.find(g => g.productEntityId === square.product.entityId);
  assert.equal(square.members.length, 32);
  assert.equal(squareGroup.parts.length, 4);
  assert(squareGroup.parts.every(p => p.properties['tubeDesigner.manufacturingValidation']?.status === 'verified'));
  assert.equal(files(join(output, 'final-reopened-export')).filter(p => p.endsWith('.step')).length, 4);
  report.checks.push('cold-open saved square project; actual UI export produced four STEP types for 32 pieces');
  // Remove only the negative-case fixture made by this acceptance run before delivering the model.
  await page.evaluate(async path => {
    const { AppProxy } = await import('../../AppProxy/AppProxy.mjs');
    const app = await AppProxy.create(window.icax ?? window.icaxNativeBridge); await app.getState();
    const product = app.getProduct('icax.tube-designer'); await product.getState();
    const project = product.getProject(window.__icaxWorkbench.getState().projectId);
    const scene = project.getMainScene();
    const state = window.__icaxWorkbench.getTubeDesignerState();
    for (const fixture of state.instances.filter(p => p.templateId === 'assembly-frame-lt'))
      await scene.invoke('TubeDesigner.DeleteProduct', { productEntityId: fixture.entityId });
    await project.save(path);
  }, squarePath);
  await page.reload();
  await page.evaluate(path => { (window.icax ?? window.icaxNativeBridge).openFileDialog = async () => path; }, squarePath);
  await page.locator('[data-action="open-start-center"]').click();
  await page.locator('[data-action="choose-project"]').click();
  await page.waitForFunction(() => window.__icaxWorkbench?.getTubeDesignerState().members.length === 32);
  assert.equal((await state()).instances.length, 1);
  for (const prefix of ['handrail', 'post', 'rail', 'infill']) {
    await page.locator(`[data-product-parameter-key="${prefix}ProfileType"]`).selectOption('system:round');
    await page.waitForFunction(prefix => window.__icaxWorkbench.getTubeDesignerState().product.parameters[prefix + 'ProfileType'] === 'round', prefix);
  }
  for (const [prefix, size] of [['handrail', 50], ['post', 40], ['rail', 30], ['infill', 20]]) {
    const field = page.locator(`[data-tube-designer-parameter="${prefix}Width"]`);
    await field.fill(String(size)); await field.press('Tab');
    await page.waitForFunction(({ prefix, size }) => window.__icaxWorkbench.getTubeDesignerState().product.parameters[prefix + 'Width'] === size, { prefix, size });
  }
  report.roundParameters = (await state()).product.parameters;
  await page.locator('[data-cam-action="tube-designer-confirm-update"]').click();
  await page.waitForFunction(() => !window.__icaxWorkbench.getTubeDesignerState().product.modelOutdated, null, { timeout: 240000 });
  await page.locator('[data-cam-action="tube-designer-disassemble-active-product"]').click();
  await page.waitForFunction(() => { const s = window.__icaxWorkbench.getTubeDesignerState(); return s.manufacturingGroups.some(g => g.productEntityId === s.product.entityId && g.parts.length === 4 && g.parts.every(p => p.properties['tubeDesigner.manufacturingValidation']?.status === 'verified')); }, null, { timeout: 240000 });
  report.roundModel = await state();
  assert.equal(report.roundModel.members.length, 32);
  for (const prefix of ['handrail', 'post', 'rail', 'infill']) assert.equal(report.roundModel.product.parameters[prefix + 'ProfileType'], 'round');
  const target = join(output, 'final-round-export'); mkdirSync(target, { recursive: true });
  await page.evaluate(path => { (window.icax ?? window.icaxNativeBridge).openDirectoryDialog = async () => path; }, target);
  await page.locator('[data-command-id="designer.export-active-product-parts"]').click();
  await page.waitForFunction(() => document.body.innerText.includes('final-round-export'));
  report.roundFiles = files(target);
  assert.equal(report.roundFiles.filter(p => p.endsWith('.step')).length, 4);
  assert(report.roundFiles.some(p => p.endsWith('零件清单.xlsx')));
  report.checks.push('round tube real regeneration, disassembly and UI export: four STEP types for 32 pieces and XLSX');
  await page.screenshot({ path: join(output, 'final-round-export.png') });
  report.roundSaved = await page.evaluate(async path => {
    const { AppProxy } = await import('../../AppProxy/AppProxy.mjs');
    const app = await AppProxy.create(window.icax ?? window.icaxNativeBridge); await app.getState();
    const product = app.getProduct('icax.tube-designer'); await product.getState();
    return (await product.getProject(window.__icaxWorkbench.getState().projectId).save(path)).saved;
  }, join(output, '客户视频规格-圆管护栏.ictd'));
  assert.equal(report.roundSaved, true);
  report.passed = true;
} catch (error) { report.error = error.stack; process.exitCode = 1; await page.screenshot({ path: join(output, 'round-failure.png') }).catch(() => {}); }
finally { writeFileSync(join(output, 'reopen-round-report.json'), JSON.stringify(report, null, 2)); console.log(JSON.stringify({ passed: report.passed, checks: report.checks, error: report.error }, null, 2)); await browser.close(); }
