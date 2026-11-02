// Run after restarting the final standalone binary with fresh user data.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { mkdirSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
const require = createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const { chromium } = require('playwright');
const output = resolve(process.env.ICAX_CUSTOMER_EXPORT_EVIDENCE || 'output/tests/customer-disassembly-export-20261023/verified');
const browser = await chromium.connectOverCDP('http://127.0.0.1:19424');
const page = browser.contexts()[0].pages().find(p => p.url().includes('/AppShell/index.html'));
const report = { standaloneNative: true, coldRestart: true, businessMocked: false, pickerPathsControlled: true };
const target = join(output, 'final-round-reopened-export'); mkdirSync(target, { recursive: true });
try {
  await page.evaluate(({ path, target }) => {
    const bridge = window.icax ?? window.icaxNativeBridge;
    bridge.openFileDialog = async () => path;
    bridge.openDirectoryDialog = async () => target;
  }, { path: join(output, '客户视频规格-圆管护栏.ictd'), target });
  await page.locator('[data-action="open-start-center"]').click();
  await page.locator('[data-action="choose-project"]').click();
  await page.waitForFunction(() => window.__icaxWorkbench.getTubeDesignerState().members.length === 32, null, { timeout: 120000 });
  report.model = await page.evaluate(() => window.__icaxWorkbench.getTubeDesignerState());
  assert.equal(report.model.instances.length, 1);
  for (const [prefix, size] of [['handrail', 50], ['post', 40], ['rail', 30], ['infill', 20]]) {
    assert.equal(report.model.product.parameters[prefix + 'ProfileType'], 'round');
    assert.equal(report.model.product.parameters[prefix + 'Width'], size);
  }
  const parts = report.model.manufacturingGroups.flatMap(g => g.parts);
  assert.equal(parts.length, 4);
  assert.equal(parts.reduce((n,p)=>n+p.quantity,0), 32);
  for (const part of parts) {
    const validation = part.properties['tubeDesigner.manufacturingValidation'];
    assert.equal(validation.status, 'verified');
    assert.equal(validation.resourceId, part.manufacturingGeometryResourceId);
    assert.equal(validation.resourceVersion, part.manufacturingGeometryResourceVersion);
  }
  await page.locator('[data-command-id="designer.export-active-product-parts"]').click();
  await page.waitForFunction(() => document.body.innerText.includes('final-round-reopened-export'), null, { timeout: 120000 });
  const walk = dir => readdirSync(dir).flatMap(n => { const p = join(dir, n); return statSync(p).isDirectory() ? walk(p) : [p]; });
  report.files = walk(target);
  assert.equal(report.files.filter(p => p.endsWith('.step')).length, 4);
  assert(report.files.some(p => p.endsWith('零件清单.xlsx')));
  await page.screenshot({ path: join(output, 'round-reopened.png') });
  report.passed = true;
} catch (error) { report.error = error.stack; process.exitCode = 1; }
finally { writeFileSync(join(output, 'round-cold-reopen-report.json'), JSON.stringify(report, null, 2)); console.log(JSON.stringify({ passed: report.passed, error: report.error })); await browser.close(); }
