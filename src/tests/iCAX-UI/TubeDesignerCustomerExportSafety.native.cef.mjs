import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { mkdirSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
const require = createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const { chromium } = require('playwright');
const output = resolve(process.env.ICAX_CUSTOMER_EXPORT_EVIDENCE || 'output/tests/customer-disassembly-export-20261023');
const baseline = JSON.parse(readFileSync(join(output, 'after-standalone-report.json'), 'utf8'));
const browser = await chromium.connectOverCDP(`http://127.0.0.1:${process.env.ICAX_CUSTOMER_EXPORT_PORT || 19424}`);
const page = browser.contexts()[0].pages().find(p => p.url().includes('/AppShell/index.html'));
const report = { standaloneNative: true, mockedBusiness: false, checks: [] };
const run = process.env.ICAX_CUSTOMER_EXPORT_RUN || '';
const directory = name => { const p = join(output, (run ? run + '-' : '') + name); mkdirSync(p, { recursive: true }); return p; };
try {
  await page.evaluate(async () => {
    const { AppProxy } = await import('../../AppProxy/AppProxy.mjs');
    const app = await AppProxy.create(window.icax ?? window.icaxNativeBridge);
    await app.getState(); const product = app.getProduct('icax.tube-designer'); await product.getState();
    const project = product.getProject(window.__icaxWorkbench.getState().projectId);
    window.__qaNative = { app, product, project, scene: project.getMainScene() };
  });
  const invoke = (method, payload) => page.evaluate(({ method, payload }) => window.__qaNative.scene.invoke('TubeDesigner.' + method, payload), { method, payload });
  const productId = baseline.model.product.entityId;
  const ids = baseline.model.manufacturingGroups.flatMap(g => g.parts.map(p => p.entityId));
  report.repeat = await invoke('ExportSelected', { partEntityIds: ids, targetDirectory: directory('repeat-export') });
  assert.equal(report.repeat.exportedCount, ids.length); assert.equal(report.repeat.draftOnly, false);
  report.checks.push('repeat native export: one STEP per type, no draft bypass');
  const parameters = baseline.model.product.parameters;
  await invoke('UpdateProductParameters', { productEntityId: productId, parameters: { ...parameters, sideLength1: 3100 } });
  const staleTarget = directory('stale-rejected');
  try { await invoke('ExportSelected', { partEntityIds: ids, targetDirectory: staleTarget }); assert.fail('stale geometry was exported'); }
  catch (error) { assert.match(error.message, /重新生成.*拆单/); report.staleError = error.message; }
  assert.equal(readdirSync(staleTarget).length, 0);
  await invoke('UpdateProductParameters', { productEntityId: productId, parameters });
  report.checks.push('changed dimensions reject stale manufacturing geometry before writing files');
  const raw = await invoke('GeneratePreview', { templateId: 'assembly-frame-lt' });
  report.rawProduct = raw.tubeDesigner.product;
  const rawId = report.rawProduct.entityId;
  const disassembled = await invoke('DisassembleSelected', { productEntityIds: [productId, rawId] });
  const rawGroup = disassembled.tubeDesigner.manufacturingGroups.find(g => g.productEntityId === rawId);
  assert(rawGroup?.parts.length > 0);
  assert(rawGroup.parts.every(p => !p.properties['tubeDesigner.manufacturingValidation']));
  const rawTarget = directory('uncut-rejected');
  try { await invoke('ExportSelected', { partEntityIds: rawGroup.parts.map(p => p.entityId), targetDirectory: rawTarget }); assert.fail('uncut stock was exported'); }
  catch (error) { assert.match(error.message, /装配工艺尚未完成整件配合验证/); report.rawError = error.message; }
  assert.equal(readdirSync(rawTarget).length, 0);
  report.checks.push('real uncut L+T assembly remains blocked without a geometry validation record');
  const goodGroup = disassembled.tubeDesigner.manufacturingGroups.find(g => g.productEntityId === productId);
  report.selected = await invoke('ExportSelected', { partEntityIds: goodGroup.parts.slice(0, 2).map(p => p.entityId), targetDirectory: directory('selected-export') });
  assert.equal(report.selected.exportedCount, 2); assert.equal(report.selected.exportedGroups.length, 1);
  assert.equal(report.selected.exportedGroups[0].productEntityId, productId);
  assert.equal(report.selected.draftOnly, false);
  report.checks.push('two selected valid parts export without unrelated raw product folders or validation blocks');
  await invoke('ActivateProduct', { productEntityId: productId });
  report.passed = true;
} catch (error) { report.error = error.stack; process.exitCode = 1; }
finally { writeFileSync(join(output, 'safety-report.json'), JSON.stringify(report, null, 2)); console.log(JSON.stringify({ passed: report.passed, checks: report.checks, error: report.error }, null, 2)); await browser.close(); }
