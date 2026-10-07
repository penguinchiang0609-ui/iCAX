// Compare production dialog renderers in a real browser; no native project writes.
import assert from 'node:assert/strict';
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { resolve, sep } from 'node:path';
import { browserAssetPath, browserAssetRoot, importBrowserAsset } from './browserPackageRuntime.mjs';
const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = resolve(process.env.ICAX_ARTIFACT_DIR || 'output/tests/dialog-appearance');
mkdirSync(output, { recursive: true });
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
const results = [], errors = [];
let passed = false;
try {
  const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://dialog.test/**', route => {
    const path = new URL(route.request().url()).pathname;
    if (path === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><html lang="zh-CN"><meta charset="utf-8"><body><div id="app" class="tube-designer-workspace"></div></body></html>' });
    const file = resolve(browserAssetRoot, path.replace(/^\/src\//, ''));
    if (!path.startsWith('/src/') || !file.startsWith(browserAssetRoot.replace(/[\\/]$/, '') + sep) || !/\.(mjs|js|css)$/.test(file)) return route.abort();
    return route.fulfill({ contentType: path.endsWith('.css') ? 'text/css' : 'text/javascript', body: readFileSync(file, 'utf8') });
  });
  await page.goto('http://dialog.test/');
  await page.addStyleTag({ content: '*{box-sizing:border-box}body{margin:0}' + readFileSync(browserAssetPath('apps/_shared/workbench/styles/laser3dcam.css'), 'utf8') + tubeDesignerCss });
  const names = await page.evaluate(async () => {
    const designer = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const settings = await import('/src/apps/tube-designer/webpage/nestingSettings.mjs');
    const excel = await import('/src/apps/tube-designer/webpage/batchExcelAutomation.mjs');
    const drawing = await import('/src/apps/tube-designer/webpage/partDrawingView.mjs');
    const model = await import('/src/apps/tube-designer/webpage/partDrawingModel.mjs');
    const parts = await import('/src/apps/tube-designer/webpage/partsArea.mjs');
    const punch = await import('/src/apps/tube-designer/webpage/punchWizard.mjs');
    const sketch = await import('/src/apps/tube-designer/webpage/sketchArea.mjs');
    const part = { entityId: 'appearance-only', name: '样式测试零件', length: 500, profile: { kind: 'round', diameter: 40, width: 40, depth: 40, wallThickness: 2 } };
    const base = () => ({ pending: false, activeAreaId: 'nesting', scene: { tubeDesigner: { templates: [], products: [], nestingGroups: [] } }, tubeDesignerUserData: { profiles: [], productTemplates: [] } });
    const fixtures = [];
    for (const [name, key, value] of [
      ['新增产品', 'tubeDesignerAddDialogOpen', true],
      ['Excel工作簿', 'tubeDesignerExcelTemplateDialog', {}],
      ['保存参数方案', 'tubeDesignerPresetDialog', {}],
      ['保存我的管型', 'tubeDesignerProfileDialog', {}],
      ['选择管型', 'tubeDesignerProfileLibraryDialog', {}],
      ['模板管理', 'tubeDesignerTemplateManager', {}],
      ['零件清单', 'tubeDesignerBreakdownOpen', true],
      ['产品导入下料', 'tubeDesignerDisassemblySelectorOpen', true],
    ]) {
      const view = base(); view[key] = value;
      fixtures.push({ name, html: designer.renderDesignerDialogs(view.scene.tubeDesigner, view) });
    }
    const view = base(); view.tubeDesignerNestingSettingsDialog = 'parameters';
    fixtures.push({ name: '排样参数', html: settings.renderNestingSettingsDialogs({}, view) });
    const automation = base(); automation.tubeDesignerExcelAutomationDialog = { draft: { enabled: false, inputDirectory: '', tempDirectory: '', outputDirectory: '' } };
    fixtures.push({ name: '自动处理设置', html: excel.renderBatchExcelAutomationDialog(automation) });
    const state = model.createDrawingState(part); state.drawing = { length: 500, name: part.name, quantity: 1, material: '', section: { name: '圆管' } };
    const dv = base(); dv.tubeDesignerPartDrawing = { state, mode: 'main', selected: 'main', mainApplied: true };
    fixtures.push({ name: '三维绘制', html: drawing.renderPartDrawingWorkbench(dv, () => '') });
    const pv = base(); pv.scene.tubeDesigner.nestingGroups = [{ parts: [part] }]; pv.tubeDesignerPunchWizard = punch.createPunchWizardState(part);
    fixtures.push({ name: '冲孔向导', html: parts.renderPunchWizardDialog({}, pv) });
    const sv = base(); sv.tubeDesignerSketchDialogOpen = true;
    fixtures.push({ name: '截面草图', html: sketch.renderSectionSketchDialog({}, sv) });
    window.appearanceFixtures = fixtures;
    return fixtures.map(fixture => fixture.name);
  });
  for (const name of names) {
    await page.evaluate(name => {
      document.querySelector('#app').innerHTML = window.appearanceFixtures.find(fixture => fixture.name === name).html;
      const dialog = document.querySelector('dialog'); if (dialog) dialog.showModal();
    }, name);
    const dialog = page.locator('#app :is([role="dialog"], dialog)').first();
    assert.equal(await dialog.count(), 1, name);
    const style = await dialog.evaluate(dialog => {
      const header = dialog.querySelector(':scope > header'), title = header.querySelector('strong') || header;
      const close = header.querySelector(':scope > button'), input = dialog.querySelector('input:not([type="checkbox"]):not([type="hidden"]),select');
      const css = node => node && getComputedStyle(node);
      return { compact: dialog.classList.contains('tube-designer-punch-sheet-dialog'), font: css(dialog).font, border: css(dialog).border, background: css(dialog).backgroundColor,
        header: { background: css(header).backgroundColor, padding: css(header).padding, border: css(header).borderBottom, fontSize: css(title).fontSize, minHeight: css(header).minHeight },
        close: close && { width: close.getBoundingClientRect().width, height: close.getBoundingClientRect().height, fontSize: css(close).fontSize, border: css(close).border },
        input: input && { minHeight: css(input).minHeight, border: css(input).border, fontSize: css(input).fontSize } };
    });
    const baseline = results[0]?.style;
    if (baseline) {
      assert.equal(style.font, style.compact ? baseline.font.replace('12px / 16.8px', '11px / 15.4px') : baseline.font, name);
      assert.equal(style.border, baseline.border, name);
      assert.equal(style.background, baseline.background, name);
      assert.deepEqual(style.header, style.compact ? { ...baseline.header, padding: '4px 10px', fontSize: '13px', minHeight: '36px' } : baseline.header, name);
      if (style.close) assert.deepEqual(style.close, style.compact ? { ...baseline.close, width: 28, height: 28 } : baseline.close, name);
      if (style.input && baseline.input) assert.deepEqual(style.input, style.compact ? { ...baseline.input, minHeight: '26px', fontSize: '11px' } : baseline.input, name);
    }
    assert.equal(style.header.minHeight, style.compact ? '36px' : '48px', name);
    assert.equal(style.header.fontSize, style.compact ? '13px' : '14px', name);
    if (style.close) { assert.equal(style.close.width, style.compact ? 28 : 32, name); assert.equal(style.close.fontSize, '22px', name); }
    results.push({ name, style });
    if (['三维绘制', '冲孔向导', '排样参数'].includes(name)) await page.screenshot({ path: resolve(output, name + '.png') });
  }
  // The native HTML confirmation has no caption by design, but shares controls.
  await page.evaluate(async () => {
    document.querySelector('#app').innerHTML = '';
    const { confirmWithoutTitle } = await import('/src/apps/tube-designer/webpage/confirmDialog.mjs');
    window.confirmJob = confirmWithoutTitle('测试确认框', '确定');
  });
  const confirm = page.locator('.tube-designer-confirm-dialog');
  assert.equal(await confirm.locator('button.confirm').evaluate(node => getComputedStyle(node).backgroundColor), 'rgb(22, 142, 132)');
  assert.equal(await confirm.evaluate(node => getComputedStyle(node, '::backdrop').backgroundColor), 'rgba(12, 25, 31, 0.48)');
  await confirm.getByRole('button', { name: '取消', exact: true }).click();
  assert.equal(await page.evaluate(() => window.confirmJob), false);
  assert.deepEqual(errors, []);
  passed = true;
  console.log(`Dialog appearance: ${results.length} production renderers and confirmation passed.`);
} finally {
  writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed, results, errors }, null, 2));
  await browser.close();
}
