import assert from 'node:assert/strict';
import { copyFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyWorkpieceFilePreview({ page, rpc, info, root, output, calls }) {
  const initial = await rpc({ action: 'invoke', scope: 'scene', method: 'CamScene.ReadState', payload: {} });
  const originalCanvas = await page.evaluate(() => { window.previewOriginalCanvas = document.querySelector('canvas'); return !!previewOriginalCanvas; });
  assert.equal(originalCanvas, true);
  const bracket = resolve(root, 'TestData/real-sheet-metal/pololu-stamped-bracket/Pololu-2257-stamped-bracket.step');
  const invoke = (method, payload) => rpc({ action: 'invoke', scope: 'scene', method, payload });
  const unchanged = async () => assert.deepEqual(await invoke('CamScene.ReadState', {}), initial);
  const cancel = () => page.locator('footer [data-nesting-file-action="cancel"]').click();
  const debug = () => page.evaluate(() => fixture.view.fiveAxisFilePicker.viewport.getDebugState());
  const ready = () => page.waitForFunction(() => fixture.view.fiveAxisFilePicker?.previewStatus === 'ready', null, { timeout: 120000 });
  await selectWorkpieceFile(page, bracket, { preview: true, open: false });
  assert.ok((await debug()).edgeCount > 300, 'actual stamped solid supplies native CAD edge polylines');
  assert.equal(await page.locator('.tube-nesting-file-list').getAttribute('aria-multiselectable'), 'false');
  assert.equal(await page.locator('.tube-nesting-file-selection-hint').isVisible(), false);
  await unchanged();
  assert.equal((await rpc({ action: 'undo' })).changed, false, 'preview does not add an undo command');
  const screenshot = resolve(output, 'workpiece-import-preview.png');
  await page.screenshot({ path: screenshot });
  const svg = page.locator('[data-nesting-wireframe]');
  const rect = await svg.boundingBox();
  const center = { x: rect.x + rect.width * .5, y: rect.y + rect.height * .5 };
  const before = await debug();
  await page.mouse.move(center.x, center.y);
  await page.mouse.down({ button: 'right' });
  await page.mouse.move(center.x + 50, center.y + 25, { steps: 6 });
  await page.mouse.up({ button: 'right' });
  assert.notEqual((await debug()).yaw, before.yaw);
  await page.mouse.wheel(0, -140);
  await page.waitForFunction(width => fixture.view.fiveAxisFilePicker.viewport.getDebugState().viewBox[2] < width, before.viewBox[2]);
  const zoom = await debug();
  await page.mouse.down({ button: 'middle' });
  await page.mouse.move(center.x + 75, center.y + 50, { steps: 6 });
  await page.mouse.up({ button: 'middle' });
  assert.notDeepEqual((await debug()).viewBox.slice(0, 2), zoom.viewBox.slice(0, 2));
  assert.equal(await page.locator('[data-nesting-file-action="fit"]').count(), 0);
  const blank = await svg.evaluate(el => {
    const box = el.getBoundingClientRect();
    for (const [dx, dy] of [[8,8], [box.width-8,8], [8,box.height-8], [box.width-8,box.height-8]]) {
      const x = box.x + dx, y = box.y + dy;
      if (document.elementFromPoint(x, y) === el) return { x, y };
    }
    throw Error('No preview background point found');
  });
  await page.mouse.dblclick(blank.x, blank.y);
  const fitted = await debug();
  assert.deepEqual(fitted.viewBox, fitted.bounds);
  await page.setViewportSize({ width: 1024, height: 768 });
  const layout = await page.locator('.tube-nesting-file-picker').evaluate(el => {
    const r = el.getBoundingClientRect(); return { x: r.x, y: r.y, right: r.right, bottom: r.bottom, width: innerWidth, height: innerHeight };
  });
  assert.ok(layout.x >= 0 && layout.y >= 0 && layout.right <= layout.width && layout.bottom <= layout.height);
  await page.screenshot({ path: resolve(output, 'workpiece-import-preview-1024.png') });
  await cancel();
  await page.setViewportSize({ width: 1440, height: 900 });
  await unchanged();

  // Native reads finish after the user has edited the directory and scrolled.
  const directory = resolve(output, 'preview-files'); mkdirSync(directory, { recursive: true });
  for (let i = 0; i < 40; i++) copyFileSync(info.sourcePath, resolve(directory, `${String(i).padStart(2, '0')}-part.step`));
  writeFileSync(resolve(directory, 'invalid.step'), 'Not a STEP file');
  await page.evaluate(() => { fixture.holdMethod = 'CamWorkpiece.PreviewFile'; fixture.hold = true; });
  await selectWorkpieceFile(page, resolve(directory, '00-part.step'), { open: false });
  await page.waitForFunction(() => fixture.held);
  await page.evaluate(() => {
    const input = document.querySelector('[data-nesting-file-directory]');
    const list = document.querySelector('.tube-nesting-file-list');
    window.previewControls = { input, list, callbacks: 0, canvas: document.querySelector('canvas') };
    input.addEventListener('preview-preserved', () => previewControls.callbacks++);
    input.focus({ preventScroll: true }); input.value += '/正在输入'; input.setSelectionRange(2, 6);
    list.scrollTop = 300;
    fixture.release(); fixture.holdMethod = '';
  });
  await ready();
  assert.deepEqual(await page.evaluate(() => {
    const s = previewControls; s.input.dispatchEvent(new Event('preview-preserved'));
    return { input: s.input === document.querySelector('[data-nesting-file-directory]'), list: s.list === document.querySelector('.tube-nesting-file-list'),
      canvas: s.canvas === document.querySelector('canvas') && s.canvas === previewOriginalCanvas, focused: document.activeElement === s.input,
      selection: [s.input.selectionStart, s.input.selectionEnd], scroll: s.list.scrollTop, value: s.input.value.endsWith('/正在输入'), callbacks: s.callbacks };
  }), { input: true, list: true, canvas: true, focused: true, selection: [2,6], scroll: 300, value: true, callbacks: 1 });
  await page.locator('[data-nesting-file-is-directory="false"]').filter({ hasText: 'invalid.step' }).click();
  await page.waitForFunction(() => fixture.view.fiveAxisFilePicker?.previewStatus === 'error');
  assert.equal(await svg.count(), 0, 'failed file does not leave the previous model visible');
  await unchanged();
  await cancel();

  // Closing with a preview in flight must not reopen the dialog or import.
  await page.evaluate(() => { fixture.holdMethod = 'CamWorkpiece.PreviewFile'; fixture.hold = true; });
  await selectWorkpieceFile(page, resolve(directory, '01-part.step'), { open: false });
  await page.waitForFunction(() => fixture.held);
  await cancel();
  await page.evaluate(() => { fixture.release(); fixture.holdMethod = ''; });
  await unchanged();
  assert.equal(await page.locator('.tube-nesting-file-picker').count(), 0);
  assert.equal(calls.some(call => call.method === 'CamWorkpiece.Import'), false, 'browsing and cancelling never import');
  await assert.rejects(rpc({ action: 'invoke', scope: 'product', method: 'CamWorkpiece.PreviewFile', payload: { sourcePath: bracket } }), /SCENE_CHANNEL/);
  await assert.rejects(invoke('CamWorkpiece.ListFiles', { directory: 'Z:/not-an-existing-directory' }));
  await unchanged();
  writeFileSync(resolve(output, 'file-preview-report.json'), JSON.stringify({ passed: true, realStep: bracket, screenshot,
    checks: ['native solid preview', 'rotate/pan/zoom/blank double-click fit without fit button', '1024 and 1440 layout', 'single selection', 'no scene or undo mutation',
      'late preview retains canvas, controls, focus, selection and newer scroll', 'invalid file clears preview', 'cancel during pending preview', 'product channel rejected'],
    standaloneCef: false }, null, 2));
}
