// Runs against the real standalone CEF/native application. Only the directory
// picker is supplied a controlled local path; business and viewport are real.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { mkdirSync, writeFileSync, readdirSync, statSync } from 'node:fs';
import { resolve, join } from 'node:path';
const require = createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const { chromium } = require('playwright');
const output = resolve(process.env.ICAX_CUSTOMER_EXPORT_EVIDENCE || 'output/tests/customer-disassembly-export-20261023');
const before = process.argv.includes('--before');
const phase = before ? 'before' : 'after';
mkdirSync(output, { recursive: true });
const run = process.env.ICAX_CUSTOMER_EXPORT_RUN || '';
const target = join(output, (run ? run + '-' : '') + phase + '-export');
mkdirSync(target, { recursive: true });
const report = { phase, standaloneCef: true, nativeBusinessMocked: false, viewportMocked: false,
  directoryPicker: 'controlled path; native picker could not be operated by the desktop helper', target, checks: [] };
const browser = await chromium.connectOverCDP(`http://127.0.0.1:${process.env.ICAX_CUSTOMER_EXPORT_PORT || 19423}`);
const page = browser.contexts()[0].pages().find(p => p.url().includes('/AppShell/index.html'));
assert(page);
page.setDefaultTimeout(90000);
const state = () => page.evaluate(() => window.__icaxWorkbench.getTubeDesignerState());
try {
  await page.evaluate(() => { const bridge = window.icax ?? window.icaxNativeBridge;
    window.__qaOriginalDirectoryPicker = bridge.openDirectoryDialog;
    window.__qaExportDirectory = '';
    bridge.openDirectoryDialog = async () => window.__qaExportDirectory;
  });
  await page.locator('[data-command-id="designer.add"]').click();
  await page.locator('[data-tube-designer-catalog-entry-id="modular-guardrail"]').click();
  await page.locator('[data-tube-designer-parameter="layout"]').waitFor();
  await page.locator('[data-cam-action="tube-designer-confirm-add"]').click();
  await page.waitForFunction(() => window.__icaxWorkbench.getTubeDesignerState().product?.templateId === 'modular-guardrail', null, { timeout: 120000 });
  for (const key of ['handrailDepth', 'handrailWidth']) {
    const field = page.locator(`[data-tube-designer-parameter="${key}"]`);
    await field.fill('50'); await field.press('Tab');
    await page.waitForFunction(key => window.__icaxWorkbench.getTubeDesignerState().product?.parameters[key] === 50,
      key, { timeout: 60000 });
    assert.equal(await field.inputValue(), '50');
  }
  await page.locator('[data-cam-action="tube-designer-confirm-update"]').click();
  await page.waitForFunction(() => { const p = window.__icaxWorkbench.getTubeDesignerState().product;
    return p?.parameters.handrailDepth === 50 && p.parameters.handrailWidth === 50 && !p.modelOutdated; }, null, { timeout: 120000 });
  await page.locator('[data-cam-action="tube-designer-disassemble-active-product"]').click();
  await page.waitForFunction(() => window.__icaxWorkbench.getTubeDesignerState().manufacturingGroups.some(g => g.parts.length > 0), null, { timeout: 240000 });
  report.model = await state();
  assert.equal(report.model.members.length, 32);
  const parts = report.model.manufacturingGroups.flatMap(g => g.parts);
  assert(parts.length > 0);
  assert.equal(parts.reduce((n, p) => n + p.quantity, 0), 32);
  if (!before) for (const part of parts) {
    const check = part.properties['tubeDesigner.manufacturingValidation'];
    assert.equal(check?.status, 'verified');
    assert.equal(check.resourceId, part.manufacturingGeometryResourceId);
    assert.equal(check.resourceVersion, part.manufacturingGeometryResourceVersion);
    assert.equal(check.assemblyFitCertified, false);
  }
  for (const [prefix, size] of [['handrail', 50], ['post', 40], ['rail', 30], ['infill', 20]])
    for (const dimension of ['Depth', 'Width']) assert.equal(report.model.product.parameters[prefix + dimension], size);
  report.checks.push('real 3000 x 1200 guardrail; 32 members; 50/40/30/20 square hollow sections');
  await page.screenshot({ path: join(output, phase + '-model.png') });
  await page.evaluate(path => { window.__qaExportDirectory = path; }, target);
  await page.locator('[data-command-id="designer.export-active-product-parts"]').click();
  if (before) {
    await page.getByText('装配工艺尚未完成整件配合验证，不能作为生产下料导出', { exact: true }).first().waitFor();
    assert.equal(readdirSync(target).length, 0);
    report.checks.push('original production export rejection reproduced; controlled target directory remains empty');
  } else {
    await page.waitForFunction(() => document.body.innerText.includes('清单：') && document.body.innerText.includes('已导出'), null, { timeout: 180000 });
    const walk = dir => readdirSync(dir).flatMap(name => { const path = join(dir, name); return statSync(path).isDirectory() ? walk(path) : [path]; });
    report.files = walk(target);
    report.stepFiles = report.files.filter(p => /\.(step|stp)$/i.test(p));
    assert.equal(parts.length, 4);
    assert.equal(report.stepFiles.length, parts.length);
    assert(report.files.some(p => p.endsWith('零件清单.xlsx')));
    report.checks.push('UI export wrote four STEP types for 32 pieces and XLSX; actual output paths appear in the log');
    assert.equal((await state()).members.length, 32, 'export retains product and viewport');
  }
  await page.screenshot({ path: join(output, phase + '-export.png') });
  report.log = (await page.locator('body').innerText()).split('日志').at(-1);
  report.projectPath = join(output, '客户视频规格-方管护栏.ictd');
  report.saved = await page.evaluate(async path => {
    const { AppProxy } = await import('../../AppProxy/AppProxy.mjs');
    const app = await AppProxy.create(window.icax ?? window.icaxNativeBridge);
    await app.getState();
    const product = app.getProduct('icax.tube-designer');
    await product.getState();
    const project = product.getProject(window.__icaxWorkbench.getState().projectId);
    const result = await project.save(path);
    window.__qaApp = app; window.__qaProject = project;
    return result.saved;
  }, report.projectPath);
  assert.equal(report.saved, true);
  report.checks.push('actual project saved through Project.Save');
  report.passed = true;
} catch (error) {
  report.error = error.stack;
  await page.screenshot({ path: join(output, phase + '-failure.png') }).catch(() => {});
  process.exitCode = 1;
} finally {
  writeFileSync(join(output, phase + '-standalone-report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ phase, passed: report.passed, checks: report.checks, error: report.error }, null, 2));
  await browser.close();
}
