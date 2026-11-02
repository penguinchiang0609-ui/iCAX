import assert from 'node:assert/strict';
import { verifyFiveAxisWindowLayout } from './FiveAxisWindowLayout.browser.mjs';
import http from 'node:http';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';
const output = browserReportDirectory('five-axis-workbench');
const serve = process.argv.includes('--serve');
const server = http.createServer((request, response) => {
  const path = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
  try {
    let content, type = 'text/javascript';
    if (path === '/') { type = 'text/html'; content = '<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><title>五轴 CAM · UI 预览</title><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"></head><body><div id="app"></div><script type="module" src="/src/tests/iCAX-UI/fixtures/fiveAxisPreview.mjs"></script></body></html>'; }
    else if (path === '/src/iCAX-UI/SDK/runtime.mjs') content = 'export async function connectApplication(){return globalThis.__fiveFixture.appProxy;}';
    else if (path.startsWith('/src/')) { content = readFileSync(browserAssetPath(path.slice(5))); type = path.endsWith('.css') ? 'text/css' : path.endsWith('.json') ? 'application/json' : 'text/javascript'; }
    else { response.writeHead(404); response.end(); return; }
    response.writeHead(200, { 'Content-Type': `${type}; charset=utf-8`, 'Cache-Control': 'no-store' }); response.end(content);
  } catch { response.writeHead(404); response.end(); }
});
await new Promise(resolve => server.listen(serve ? 4186 : 0, '127.0.0.1', resolve));
const url = `http://127.0.0.1:${server.address().port}/?product=icax.five-axis-cam`;
if (serve) { console.log(`FiveAxis UI preview: ${url}`); }
else {
  const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
  let browser;
  const errors = [], cases = [];
  try {
    browser = await chromium.launch({ channel: process.env.ICAX_BROWSER_CHANNEL || 'chrome', headless: true });
    const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
    page.on('response', response => { if (response.status() >= 400) console.error(response.status(), response.url()); });
    page.on('pageerror', error => { errors.push(error.message); console.error(error.message); });
    await page.goto(url);
    try { await page.waitForFunction(() => globalThis.__icaxAppShell?.getState().startupPhase === 'ready', null, { timeout: 15000 }); }
    catch (error) { console.log(await page.evaluate(() => ({ state: globalThis.__icaxAppShell?.getState(), text: document.body.innerText }))); await page.screenshot({ path: resolve(output, 'failure.png') }); throw error; }
    await page.locator('[data-five-area="workpiece"]').waitFor();
    if(process.env.ICAX_DOCKED_EXTRACTION_ONLY){
      const { verifyDockedExtraction } = await import('./FiveAxisDockedExtraction.browser.mjs');
      cases.push(...await verifyDockedExtraction(page,output));
    } else if(process.env.ICAX_WINDOW_LAYOUT_ONLY){
      cases.push(...await verifyFiveAxisWindowLayout(page,output));
    } else {
    const select=page.locator('[data-command-id="five.tool.select"]');
    await select.waitFor();
    assert.equal(await select.locator('svg').count(),1,'Select uses the shared mouse-pointer icon');
    assert.equal(await select.evaluate(node=>node.classList.contains('active')),true);
    const commands=await page.locator('.product-ribbon [data-command-id]').evaluateAll(nodes=>nodes.map(n=>n.dataset.commandId));
    // The complete shell renders the Model group followed immediately by these controls.
    const importIndex=commands.indexOf('workpiece.import');
    assert.deepEqual(commands.slice(importIndex+1,importIndex+5),['five.tool.select','five.tool.translate','five.tool.rotate','five.tool.scale']);
    await page.screenshot({ path: resolve(output, '01-project.png') });
    assert.deepEqual(await page.locator('.ribbon-tab').allTextContents().then(values => values.map(value => value.trim())), ['工件', 'CAD', 'CAM', '孪生', '帮助']);
    assert.equal(await page.locator('[data-tab-id="file"]').count(), 0);
    assert.equal(await page.locator('[data-command-id="app.save"]').count(), 1, 'keep the shell save entry');
    for (const area of ['workpiece', 'cad', 'toolpath', 'simulation', 'help']) {
      await page.locator(`[data-tab-id="${area}"]`).click();
      await page.locator(`[data-five-area="${area}"]`).waitFor();
      assert.equal(await page.locator('canvas').count() > 0, true);
      const layout = await page.evaluate(() => {
        const bounds = selector => { const r = document.querySelector(selector).getBoundingClientRect(); return { x: r.x, y: r.y, width: r.width, height: r.height, right: r.right, bottom: r.bottom }; };
        return { left: bounds('.cam-context-pane'), right: bounds('.cam-info-pane'), viewport: bounds('.cam-viewport'), dock: bounds('[data-five-dock]'), summary: bounds('[data-five-summary]') };
      });
      assert.ok(layout.viewport.width >= 420 && layout.viewport.height > 200);
      if (area === 'workpiece') {
        assert.ok(layout.viewport.width >= 1200 && layout.left.right <= layout.viewport.x, 'workpiece list stays beside the positioning canvas');
        assert.equal(await page.locator('.cam-context-pane').isVisible(),true);
        assert.equal(await page.locator('[data-five-dock]').isVisible(),false);
      } else if(area==='toolpath'){
        assert.ok(layout.left.right<=layout.viewport.x&&layout.right.x>=layout.viewport.right&&layout.viewport.width>=420,JSON.stringify(layout));
        assert.equal(await page.locator('[data-five-dock]').isVisible(),false);
      } else { assert.ok(layout.left.right <= layout.viewport.x && layout.right.x >= layout.viewport.right); assert.ok(layout.dock.y >= layout.viewport.bottom); }
      assert.equal(await page.locator('[data-five-summary]').isVisible(),false);
    }
    cases.push('five task pages mount with SDK canvas; product File page is removed');
    await page.evaluate(() => __fiveFixture.populate());
    await page.locator('[data-tab-id="toolpath"]').click();
    await page.waitForFunction(() => __fiveFixture.view.viewport?.getDebugState()?.objectCount > 0);
    await page.screenshot({ path: resolve(output, '02-programming.png') });
    await page.locator('[data-five-intent="cut-1"]').click();
    await page.locator('[data-command-id="five.tool.adjust"]').click();
    await page.locator('[data-five-field="poseMode"]').selectOption('tilted');
    await page.locator('[data-five-field="forward"]').fill('12.5');
    await page.screenshot({ path: resolve(output, '03-pose.png') });
    await page.locator('[data-command-id="five.tool.adjust"]').click();
    await page.locator('[data-five-field="poseMode"]').selectOption('tilted');
    await page.locator('[data-five-field="forward"]').fill('9.25');
    await page.locator('[data-five-field="poseMode"]').selectOption('normal');
    assert.equal(await page.locator('[data-five-field="forward"]').count(), 0);
    await page.locator('[data-five-field="poseMode"]').selectOption('tilted');
    assert.equal(await page.locator('[data-five-field="forward"]').inputValue(), '9.25');
    cases.push('conditional parameter drafts survive hiding and inspector changes');
    await page.evaluate(() => __fiveFixture.populate(70));
    // Use a smaller real viewport to exercise both independently scrolling panes.
    await page.setViewportSize({ width: 1366, height: 520 });
    // Constrain the current cut-tree container to exercise nested scrolling.
    await page.addStyleTag({content:'.five-cut-tree { max-height:260px; overflow:auto; }'});
    await page.locator('[data-five-field="note"]').fill('保留文本光标与异步草稿');
    await page.evaluate(() => {
      const input = document.querySelector('[data-five-field="note"]');
      input.focus({ preventScroll: true }); input.setSelectionRange(2, 7);
      window.__preserved = { input, canvas: document.querySelector('.cam-render-viewport canvas'), listenerCalls: 0 };
      input.addEventListener('test-preserved', () => __preserved.listenerCalls++);
      __fiveFixture.hold(); void __icaxAppShell.executeRibbonCommand('five.refresh');
    });
    await page.waitForFunction(() => __fiveFixture.held);
    const scrolled = await page.evaluate(() => {
      const left = document.querySelector('.cam-context-pane'), right = document.querySelector('.cam-info-pane'), nested = document.querySelector('.five-cut-tree');
      left.scrollTop = 160; right.scrollTop = 140; nested.scrollTop = 230;
      return [left.scrollTop, right.scrollTop, nested.scrollTop];
    });
    assert.ok(scrolled.every(value => value > 0), `test must actually scroll all three containers: ${scrolled}`);
    await page.evaluate(() => __fiveFixture.release());
    await page.waitForFunction(() => !__fiveFixture.view.pending);
    const retained = await page.evaluate(() => {
      const input = document.querySelector('[data-five-field="note"]'); input.dispatchEvent(new Event('test-preserved'));
      return { sameInput: input === __preserved.input, sameCanvas: __preserved.canvas === document.querySelector('.cam-render-viewport canvas'),
        focused: document.activeElement === input, selection: [input.selectionStart, input.selectionEnd], value: input.value, calls: __preserved.listenerCalls,
        scroll: ['.cam-context-pane', '.cam-info-pane', '.five-cut-tree'].map(selector => document.querySelector(selector).scrollTop) };
    });
    assert.equal(retained.sameInput, true); assert.equal(retained.sameCanvas, true); assert.equal(retained.focused, true);
    assert.deepEqual(retained.selection, [2, 7]); assert.deepEqual(retained.scroll, scrolled); assert.equal(retained.calls, 1);
    assert.equal(retained.value, '保留文本光标与异步草稿');
    cases.push('async refresh preserves canvas/input/listeners, selection, both panes and nested scroll after request');
    await page.locator('[data-five-field="forward"]').focus();
    await page.evaluate(() => {
      __fiveFixture.hold(); void __icaxAppShell.executeRibbonCommand('five.refresh');
    });
    await page.waitForFunction(() => __fiveFixture.held);
    await page.evaluate(() => {
      __fiveFixture.view.fiveAxisUI.drafts['definition:cut-1'].poseMode = 'normal';
      __fiveFixture.release();
    });
    await page.waitForFunction(() => !__fiveFixture.view.pending);
    assert.equal(await page.locator('[data-five-field="forward"]').count(), 0);
    assert.equal(await page.evaluate(() => !!document.activeElement?.dataset.fiveField), false, 'removed field must not transfer focus to a different field');
    await page.locator('[data-five-field="poseMode"]').selectOption('tilted');
    assert.equal(await page.locator('[data-five-field="forward"]').inputValue(), '9.25');
    cases.push('conditional removal during async refresh does not steal focus; hidden draft survives');
    if(!process.env.ICAX_TRANSFORM_WORKBENCH_ONLY){
    await page.locator('[data-five-search]').fill('标准孔');
    assert.equal(await page.locator('[data-five-intent]').count(), 2);
    await page.locator('[data-five-search]').fill('');
    await page.locator('[data-five-intent="cut-2"]').click();
    assert.equal(await page.locator('[data-five-field="note"]').inputValue(), '');
    cases.push('search and per-definition draft isolation');
    // Cutting-line preparation belongs to CAM. A source definition remains
    // distinct from a generated path, with one hole and one start command.
    await page.setViewportSize({ width: 1600, height: 1000 });
    await page.locator('[data-tab-id="toolpath"]').click();
    const openAdvancedHole=async()=>{
      assert.equal(await page.locator('[data-command-id="five.tool.path-hole"]').count(),0);
      assert.equal(await page.locator('[data-command-id="five.tool.side"]').count(),0);
      await page.locator('[data-command-id="five.tool.settings"]').click();
      assert.equal(await page.locator('[data-five-command="five.tool.path-hole"]').isVisible(),false);
      await page.locator('[data-five-key="advanced-settings"] summary').click();
      await page.getByRole('button',{name:'孔加工方式',exact:true}).click();
    };
    const beforePreparation = await page.evaluate(() => { window.__prepCanvas = document.querySelector('.cam-render-viewport canvas'); return __fiveFixture.requests.length; });
    for (const tool of ['extract', 'manual-extract', 'projection', 'split', 'path-start']) {
      assert.equal(await page.locator(`[data-command-id="five.tool.${tool}"]`).count(), 1);
      await page.locator(`[data-command-id="five.tool.${tool}"]`).click();
      assert.equal(await page.locator(`[data-five-area="toolpath"] [data-five-tool="${tool}"]`).count(), 1);
      assert.equal(await page.evaluate(() => document.querySelector('.cam-render-viewport canvas') === __prepCanvas), true);
    }
    assert.equal(await page.evaluate(() => __fiveFixture.requests.length), beforePreparation);
    assert.equal(await page.locator('[data-command-id^="five.tool.geometry-"]').count(), 0);
    await openAdvancedHole();
    assert.equal(await page.getByRole('dialog',{name:'孔加工方式',exact:true}).count(),1);
    assert.equal(await page.locator('[data-five-field="rotaryStrategy"]').count(),0);
    assert.equal(await page.locator('#five-hole-type option[value="StandardHole"]').isDisabled(),true,'unqualified fixture cannot become a standard hole');
    await page.locator('[data-command-id="five.tool.path-start"]').click();
    assert.equal(await page.locator('.five-start-prompt').isVisible(),true);
    assert.equal(await page.locator('.cam-info-pane').isVisible(),false);
    await page.locator('[data-command-id="five.tool.generate"]').click();
    await openAdvancedHole();
    assert.equal(await page.locator('#five-hole-type').inputValue(), 'Contour');
    await page.locator('[data-command-id="five.tool.path-start"]').click();
    assert.equal(await page.locator('[data-five-field="startMode"]').count(),0);
    assert.equal(await page.locator('.five-start-prompt').isVisible(),true);
    await page.locator('[data-command-id="five.tool.manual-order"]').click();
    await page.locator('[data-five-command="five.order.after"]').click();
    await page.locator('[data-five-intent="cut-1"]').click();
    assert.equal(await page.locator('[data-five-command="five.order.after"]').getAttribute('aria-pressed'), 'true');
    assert.equal(await page.locator('[data-five-command="five.order.apply"]').isDisabled(),true);
    cases.push('cutting-line preparation is in Toolpath with unique commands and retained canvas/source drafts; sequence drafts remain global');
    // Rough orientation, measured registration and the field measurement plan
    // have distinct entry points, inputs and application responsibility.
    await page.locator('[data-tab-id="workpiece"]').click();
    const positioning = [['point', '选点定位', ['原点', 'X 轴点', 'XY 面内点']], ['line', '选直线定位', ['X 轴直线', 'XY 面内直线', '原点']], ['face', '选面定位', ['XY 平面', '原点', 'X 轴点']]];
    await page.evaluate(() => { window.__positionCanvas = document.querySelector('.cam-render-viewport canvas'); });
    assert.equal(await page.locator('[data-command-id="five.tool.rough"]').count(), 0);
    assert.equal(await page.getByRole('button', { name: '展开工件坐标系菜单', exact: true }).count(), 0);
    for (const width of [1024, 1366, 1600]) {
      await page.setViewportSize({ width, height: 768 });
      let previous;
      for (const [method, title, labels] of positioning) {
        const control = page.getByRole('button', { name: title, exact: true });
        const bounds = await control.boundingBox();
        assert.ok(bounds.height >= 44 && bounds.x >= 0 && bounds.x + bounds.width <= width);
        if (previous) assert.ok(Math.abs(bounds.y - previous.y) < 1 && bounds.x >= previous.x + previous.width);
        previous = bounds;
        await control.click();
        assert.deepEqual(await page.locator('.five-reference-row>span').allTextContents(), labels);
        assert.equal(await page.evaluate(() => __positionCanvas === document.querySelector('.cam-render-viewport canvas')), true);
        assert.equal(await page.locator('.ribbon-command.active[data-command-id^="five.rough."]').count(), 1);
      }
    }
    await page.locator('.ribbon-group').filter({ has: page.locator('[data-command-id="five.rough.point"]') }).screenshot({ path: resolve(output, 'positioning-buttons.png') });
    await page.screenshot({ path: resolve(output, '06-rough.png') });
    await page.locator('[data-command-id="five.tool.fine"]').click();
    assert.equal(await page.locator('[data-five-field^="measured"]').count(), 9);
    await page.locator('[data-five-field="measured1X"]').fill('120.25');
    cases.push('point/line/plane commands are adjacent at three widths, open the correct picker directly and retain the canvas');
    assert.equal(await page.locator('[data-five-field="note"]').count(),0);
    assert.equal(await page.getByRole('button',{name:'确定',exact:true}).isVisible(),true);
    const confirmBounds = await page.getByRole('button',{name:'确定',exact:true}).boundingBox();
    assert.ok(confirmBounds.y + confirmBounds.height <= 768, 'Confirm is on screen at laptop height');
    assert.equal(await page.locator('.bottom-dock').isVisible(),true);
    await page.screenshot({ path: resolve(output, '07-fine.png') });
    await page.locator('[data-tab-id="toolpath"]').click();
    await page.locator('[data-command-id="five.tool.registration"]').click();
    assert.equal(await page.locator('[data-five-area="toolpath"]').count(), 1);
    assert.equal(await page.locator('[data-five-field^="measured"]').count(), 0);
    assert.equal(await page.locator('[data-five-field="note"]').inputValue(), '');
    assert.match(await page.locator('[data-five-key="registration-state"]').innerText(), /执行端[\s\S]*待测量/);
    await page.screenshot({ path: resolve(output, '08-registration.png') });
    await page.locator('[data-five-command="five.tool.fine"]').click();
    assert.equal(await page.locator('[data-five-field="measured1X"]').inputValue(), '120.25');
    cases.push('rough point/line/face, measured registration and field three-point plan remain distinct');
    await page.locator('[data-tab-id="toolpath"]').click();
    const beforeTools = await page.evaluate(() => { window.__toolCanvas = document.querySelector('.cam-render-viewport canvas'); return __fiveFixture.requests.length; });
    for (const tool of ['keypoint', 'rapid', 'adjust', 'leads', 'overcut', 'compensation', 'clear', 'auto-order', 'generate', 'simulation']) {
      await page.locator(`[data-command-id="five.tool.${tool}"]`).click();
      assert.equal(await page.locator(`[data-five-tool="${tool}"]`).count(), 1);
      assert.equal(await page.evaluate(() => document.querySelector('.cam-render-viewport canvas') === __toolCanvas), true);
    }
    assert.ok((await page.evaluate(start => __fiveFixture.requests.slice(start),beforeTools)).every(r=>['CamProcess.Read','CamProcess.PreviewLeads','CamPlan.Generate'].includes(r.method)), 'generate invokes the scene command; other tools only read');
    await page.locator('[data-command-id="five.tool.rapid"]').click();
    assert.equal(await page.locator('[data-five-area="toolpath"] [data-five-tool="rapid"]').count(), 1);
    assert.equal(await page.locator('[data-five-key="simulation-bar"]').count(), 0);
    await page.locator('[data-command-id="five.tool.simulation"]').click();
    assert.equal(await page.locator('[data-five-key="simulation-bar"]').count(), 1);
    assert.equal(await page.getByRole('button', { name: '播放', exact: true }).isDisabled(), true);
    cases.push('simulation repair loop preserves canvas; generation is a one-click scene command');
    await page.setViewportSize({ width: 1366, height: 768 });
    for (const tool of ['extract', 'manual-extract', 'projection', 'path-start', 'generate', 'simulation', 'export', 'settings']) {
      const ribbonFits = await page.locator(`[data-command-id="five.tool.${tool}"]`).evaluate(node => {
        const r = node.getBoundingClientRect(); return r.left >= 0 && r.right <= innerWidth;
      });
      assert.equal(ribbonFits, true, `${tool} remains visible at desktop laptop width`);
    }
    await page.evaluate(() => { __fiveFixture.hold(); void __icaxAppShell.executeRibbonCommand('five.refresh'); });
    await page.waitForFunction(() => __fiveFixture.held);
    await page.locator('[data-tab-id="workpiece"]').click();
    await page.locator('[data-command-id="five.tool.fine"]').click();
    await page.locator('[data-five-field="measured1X"]').fill('120.5');
    await page.evaluate(() => __fiveFixture.release());
    await page.waitForFunction(() => !__fiveFixture.view.pending);
    assert.equal(await page.locator('[data-five-area="workpiece"]').count(), 1);
    assert.equal(await page.evaluate(() => document.activeElement === document.querySelector('[data-five-field="measured1X"]')), true);
    assert.equal(await page.locator('[data-five-field="measured1X"]').inputValue(), '120.5');
    assert.equal(await page.locator('.five-feedback').count(), 0);
    cases.push('response after area navigation updates current area without restoring old focus');
    await page.setViewportSize({ width: 1600, height: 1000 });
    await page.locator('[data-tab-id="toolpath"]').click();
    await page.locator('[data-command-id="five.tool.export"]').click();
    await page.screenshot({ path: resolve(output, '04-output.png') });
    assert.equal(await page.locator('[data-five-command="five.export"]').isDisabled(), true);
    await page.locator('[data-five-field="purpose"]').selectOption('acceptance');
    assert.equal(await page.locator('[data-five-command="five.export"]').isDisabled(), true);
    cases.push('production and equipment-acceptance output remain blocked without valid plan/report');
    await page.locator('[data-five-dock] [data-five-command="five.tool.settings"]').click();
    assert.equal(await page.locator('[data-five-area="toolpath"] [data-five-tool="settings"]').count(), 1);
    assert.equal(await page.locator('[data-five-command="machine.import"]').isVisible(), false, 'machine import is an advanced settings detail');
    await page.locator('[data-five-rapid-field="followHeightMm"]').fill('2.5');
    await page.screenshot({ path: resolve(output, '09-settings.png') });
    await page.locator('[data-five-key="rapid-machine"] summary').click();
    await page.evaluate(() => { window.__settingsDisclosure = document.querySelector('[data-five-key="rapid-machine"]'); });
    await page.evaluate(() => __icaxAppShell.executeRibbonCommand('five.refresh'));
    assert.equal(await page.evaluate(() => document.querySelector('[data-five-key="rapid-machine"]') === __settingsDisclosure && __settingsDisclosure.open), true);
    assert.equal(await page.locator('[data-five-command="machine.import"]').isVisible(), true);
    await page.locator('[data-tab-id="help"]').click();
    assert.equal(await page.locator('[data-command-id="five.tool.settings"]').count(), 0);
    for (const tool of ['manual', 'diagnostics', 'logs', 'about']) {
      await page.locator(`[data-command-id="five.tool.${tool}"]`).click();
      assert.equal(await page.locator(`[data-five-tool="${tool}"]`).count(), 1);
    }
    await page.locator('[data-tab-id="toolpath"]').click();
    await page.locator('[data-command-id="five.tool.settings"]').click();
    assert.equal(await page.locator('[data-five-rapid-field="followHeightMm"]').inputValue(), '2.5');
    await page.locator('[data-command-id="five.tool.export"]').click();
    assert.equal(await page.locator('[data-five-field="purpose"]').inputValue(), 'acceptance');
    cases.push('settings and output gate stay in Toolpath, Help contains support only, and drafts remain separate');
    await page.locator('[data-command-id="five.tool.simulation"]').click();
    assert.equal(await page.locator('.five-sequence-empty').count(), 0);
    assert.equal(await page.locator('[data-five-command="five.dock.issues"]').count(), 0);
    await page.screenshot({ path: resolve(output, '05-verification.png') });
    await page.locator('[data-command-id="five.tool.export"]').click();
    // Final user-approved division: Workpiece / optional CAD / CAM / real-machine Simulation / Help.
    await page.locator('[data-five-search]').fill('');
    await page.evaluate(() => __fiveFixture.populate());
    const selectedTask = await page.evaluate(() => __fiveFixture.view.fiveAxisUI.selectedIntentId);
    for (const area of ['workpiece', 'cad', 'simulation', 'toolpath']) {
      await page.locator(`[data-tab-id="${area}"]`).click();
      assert.equal(await page.locator('[data-five-intent]').count(), 6, 'the same task tree remains available on every page');
      assert.equal(await page.locator(`[data-five-intent="${selectedTask}"][aria-pressed="true"]`).count(), 1);
    }
    cases.push('one task tree retains source selection across Workpiece, CAD, CAM and real-machine Simulation');
    await page.locator('[data-tab-id="workpiece"]').click();
    assert.equal(await page.locator('[data-command-id="workpiece.import"]').count(), 1);
    assert.equal(await page.locator('[data-command-id="five.tool.models"], [data-five-command="five.tool.models"], [data-five-workpiece]').count(), 0);
    assert.equal(await page.locator('[data-command-id="workpiece.import"]').isDisabled(), false);
    assert.match(await page.locator('[data-five-key="workpieces"]').innerText(), /曲面薄板/);
    await page.screenshot({ path: resolve(output, '10-workpiece.png') });
    assert.equal(await page.locator('[data-command-id="five.tool.model-library"], [data-five-command="five.tool.model-library"]').count(), 0);
    await page.locator('[data-command-id="five.tool.fixture"]').click();
    assert.equal(await page.locator('[data-five-command="five.tool.model-library"]').count(), 0);
    assert.equal(await page.locator('[data-command-id="five.tool.fixture-generate"], [data-command-id="five.tool.fixture-parts"], [data-command-id="five.tool.fixture-export"]').count(), 0);
    assert.match(await page.locator('.five-fixture-heading').first().innerText(), /装夹对象/);
    assert.equal(await page.locator('[data-five-command="five.fixture.import"]').count(), 2);
    assert.equal(await page.locator('[data-five-command="five.fixture.check"]').isDisabled(), true);
    assert.equal(await page.locator('.five-draft-note').count(), 0);
    await page.screenshot({ path: resolve(output, '12-fixture-setup.png') });
    cases.push('real fixture setup entry replaces speculative manufacturing placeholders; empty setup cannot claim a check');
    await page.locator('[data-tab-id="cad"]').click();
    assert.equal(await page.locator('[data-command-id="workpiece.import"]').count(), 0);
    assert.equal(await page.locator('[data-command-id="five.tool.extract"]').count(), 0);
    await page.locator('[data-command-id="five.tool.inspection"]').click();
    await page.locator('[data-five-command="workpiece.check"]').click();
    await page.waitForFunction(() => !__fiveFixture.view.pending);
    assert.match(await page.locator('[data-five-key="inspection"]').innerText(), /结构检查通过/);
    const cadCall = await page.evaluate(() => __fiveFixture.requests.findLast(item => item.method === 'CamWorkpiece.Inspect'));
    assert.equal(cadCall.scope, 'scene');
    await page.evaluate(() => __icaxAppShell.executeRibbonCommand('five.refresh'));
    assert.match(await page.locator('[data-five-key="inspection"]').innerText(), /结构检查通过/, 'read-only inspection survives a scene refresh');
    await page.evaluate(async () => { __fiveFixture.scene.model.geometryRevision = '2'; await __icaxAppShell.executeRibbonCommand('five.refresh'); });
    assert.match(await page.locator('[data-five-key="inspection"]').innerText(), /尚未检查/, 'a geometry change invalidates the old inspection');
    await page.locator('[data-five-command="workpiece.check"]').click();
    await page.waitForFunction(() => !__fiveFixture.view.pending);
    assert.match(await page.locator('[data-five-key="inspection"]').innerText(), /结构检查通过/);
    await page.locator('[data-command-id="five.tool.cad-edit"]').click();
    assert.match(await page.locator('[data-five-key="cad-optional"]').innerText(), /不影响正常 CAM/);
    await page.locator('[data-five-field="holeDiameter"]').fill('18');
    await page.screenshot({ path: resolve(output, '14-cad.png') });
    await page.locator('[data-command-id="five.tool.measure"]').click();
    assert.equal(await page.locator('[data-five-tool="measure"]').count(), 1);
    cases.push('CAD is optional and owns geometry checks, repair, CSG editing and measurement; inspection uses actual scene contract');
    await page.locator('[data-tab-id="toolpath"]').click();
    assert.equal(await page.locator('[data-command-id="five.tool.measure"]').count(), 0);
    await page.locator('[data-five-intent="cut-2"]').click();
    await openAdvancedHole();
    assert.equal(await page.locator('#five-hole-type').inputValue(), 'Contour');
    assert.equal(await page.locator('#five-hole-type option[value="StandardHole"]').isDisabled(),true);
    assert.equal(await page.locator('[data-five-key="draft-note"]').count(),0);
    await page.locator('[data-command-id="five.tool.projection"]').click();
    await page.locator('[data-five-field="sketchDiameter"]').fill('14.5');
    await page.locator('[data-five-field="pitchX"]').fill('60');
    await page.locator('[data-five-field="sketchShape"]').selectOption('slot');
    assert.equal(await page.locator('[data-five-field="sketchDiameter"]').count(), 0);
    await page.locator('[data-five-field="sketchShape"]').selectOption('circle');
    assert.equal(await page.locator('[data-five-field="sketchDiameter"]').inputValue(), '14.5');
    await page.screenshot({ path: resolve(output, '15-projection.png') });
    await openAdvancedHole();
    assert.equal(await page.locator('#five-hole-type').inputValue(), 'Contour');
    await page.locator('[data-command-id="five.tool.projection"]').click();
    assert.equal(await page.locator('[data-five-field="pitchX"]').inputValue(), '60');
    cases.push('CAM standard-hole dialog has no invented rotary selector and does not disturb unrelated source drafts');
    await page.locator('[data-command-id="five.tool.simulation"]').click();
    assert.equal(await page.locator('[data-five-key="simulation-bar"]').isVisible(), true);
    assert.equal(await page.locator('[data-five-command="five.simulation.play"]').isDisabled(), true);
    await page.locator('[data-five-command="five.simulation.head-open"]').click();
    assert.equal(await page.getByRole('dialog', {name:'切割头',exact:true}).isVisible(), true);
    assert.equal(await page.locator('.cam-info-pane').isVisible(), false);
    await page.screenshot({ path: resolve(output, '16-path-simulation.png') });
    await page.locator('[data-tab-id="simulation"]').click();
    assert.equal(await page.locator('[data-five-area="simulation"]').count(), 1);
    assert.equal(await page.locator('[data-command-id="five.tool.machine-definitions"]').isVisible(), true);
    await page.locator('[data-command-id="five.tool.machine-definitions"]').click();
    assert.equal(await page.locator('[data-five-key="machine-definitions"]').isVisible(), true);
    await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();
    assert.equal(await page.locator('[data-five-key="instance-state"]').isVisible(), true);
    await page.locator('[data-command-id="five.tool.machine-simulation"]').click();
    assert.equal(await page.locator('[data-five-command="five.machine.build"]').isDisabled(), true);
    assert.equal(await page.locator('[data-five-command="five.machine.export"]').isDisabled(), true);
    await page.screenshot({ path: resolve(output, '17-machine-simulation.png') });
    await page.locator('[data-tab-id="toolpath"]').click();
    cases.push('Simulation exposes definitions, instances and jobs; an unbound job cannot build or export machine motion');
    // Final review image uses the small explicitly labelled UI sample again.
    await page.locator('[data-five-search]').fill('');
    await page.evaluate(() => __fiveFixture.populate());
    await page.locator('[data-command-id="five.tool.generate"]').click();
    assert.equal(await page.locator('[data-five-dock]').isVisible(),false);
    await page.evaluate(() => { document.querySelector('.cam-info-pane').scrollTop = 0; document.querySelector('.cam-context-pane').scrollTop = 0; });
    await page.screenshot({ path: resolve(output, 'preview.png') });
    const unsupported = await browser.newPage();
    await unsupported.goto(url.replace('icax.five-axis-cam', 'icax.unregistered'));
    await unsupported.waitForFunction(() => globalThis.__icaxAppShell?.getState().startupPhase === 'ready');
    assert.equal(await unsupported.evaluate(() => __icaxAppShell.getState().activeProductId), 'icax.five-axis-cam');
    await unsupported.close();
    cases.push('startup uses the sole configured product regardless of an unregistered URL product parameter');
    }
    }
    assert.deepEqual(errors, []);
    const requests = await page.evaluate(() => __fiveFixture.requests);
    assert.ok(requests.every(item => item.scope === 'scene'));
    writeFileSync(resolve(output, 'report.json'), JSON.stringify({ passed: true, cases, errors, coverage: { actualAppShell: true, actualProductEntry: true, realSdkViewport: true, controlledNativeTransport: true, nativeBackend: false, standaloneCef: false }, requests }, null, 2));
    console.log(JSON.stringify({ passed: true, cases: cases.length, output }));
  } finally { await browser?.close(); server.close(); }
}

