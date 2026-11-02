import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

export async function verifyViewportDisplay({ page, rpc, calls, output }) {
  const read = () => rpc({ action: 'invoke', scope: 'scene', method: 'CamScene.ReadState', payload: {} });
  const before = await read();
  await page.setViewportSize({ width: 1200, height: 640 });
  // Use an editable inspector for focus/scroll preservation. Generate now runs
  // as a compact command dialog and has no general-purpose note field.
  await page.evaluate(() => fixture.entry.handleRibbonCommand(fixture.context, 'five.tool.projection'));
  await page.waitForFunction(() => !fixture.view.pending);
  const modes = page.locator('#app').getByRole('combobox', { name: '显示模式' });
  assert.equal(await modes.inputValue(), 'shadedEdges');
  await page.evaluate(async () => {
    const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    const f = fixture;
    f.displayPeerHost = document.createElement('div');
    f.displayPeerHost.style.cssText = 'position:fixed;left:-1000px;top:0;width:300px;height:200px';
    document.body.append(f.displayPeerHost);
    f.displayPeer = createThreeViewport({ continuousRender: false, showDisplayModeControl: true, displayMode: 'shadedEdges' })
      .connectScene(f.context.sceneProxy).mount(f.displayPeerHost);
    await f.displayPeer.applyViewSnapshot(f.view.areas[f.view.activeAreaId].viewReader.snapshot);
  });

  // The same native data in two real WebGL viewports has independent display preferences.
  const count = calls.length;
  await modes.selectOption('wireframe');
  assert.equal(await page.evaluate(() => fixture.view.viewport.displayMode), 'wireframe');
  assert.equal(await page.evaluate(() => fixture.displayPeer.displayMode), 'shadedEdges');
  await modes.selectOption('shadedEdges');
  const seams = page.locator('#app').getByRole('checkbox', { name: '曲面拼接边' });
  assert.equal(await seams.count(),0);
  await page.evaluate(() => fixture.view.viewport.setShowSurfaceEdges(true));
  assert.equal(await page.evaluate(() => fixture.view.viewport.showSurfaceEdges), true);
  assert.equal(await page.evaluate(() => fixture.displayPeer.showSurfaceEdges), false);
  await page.evaluate(() => fixture.view.viewport.setShowSurfaceEdges(false));
  assert.deepEqual(calls.slice(count).filter(c => !(c.action === 'resource' && c.head)), [],
    'display switches must not invoke native methods or fetch geometry');

  await modes.selectOption('wireframe');
  for (const area of ['workpiece', 'toolpath']) {
    await page.evaluate(area => fixture.context.actions.selectRibbonTab(area), area);
    await page.waitForFunction(() => !fixture.view.pending);
    assert.equal(await modes.inputValue(), 'wireframe');
    const scene = await page.evaluate(() => ({
      debug: fixture.view.viewport.getDebugState(),
      objects: [...fixture.view.viewport.sceneObjects.values()].map(o => ({ visible: o.visible, edges: o.userData.meshEdges?.visible })),
    }));
    assert.ok(scene.objects.length && scene.objects.every(o => o.visible && o.edges), JSON.stringify(scene));
  }
  // A transient area can hide the shared viewport while the native snapshot stays unchanged.
  await page.evaluate(async () => {
    fixture.view.viewport.setVisibleEntityIds([]);
    await fixture.entry.handleRibbonCommand(fixture.context, 'five.refresh');
  });
  assert.equal(await page.evaluate(() => [...fixture.view.viewport.sceneObjects.values()].every(o => o.visible)), true);
  assert.equal(await modes.inputValue(), 'wireframe');

  // A delayed business refresh must not overwrite a newer viewport preference or editing position.
  const note = page.locator('[data-five-field="note"]');
  await note.fill('保留输入光标和滚动');
  await note.evaluate(input => input.setSelectionRange(2, 6));
  await page.evaluate(() => {
    const f = fixture, left = document.querySelector('.cam-context-pane'), right = document.querySelector('.cam-info-pane');
    f.displayNodes = { left, right, input: document.querySelector('[data-five-field="note"]'), canvas: f.view.viewport.renderer.domElement,
      geometry: [...f.view.viewport.sceneObjects.values()][0].geometry };
    left.scrollTop = 35; right.scrollTop = 40;
    f.holdMethod = 'CamScene.ReadState'; f.hold = true;
    f.displayRefresh = f.entry.handleRibbonCommand(f.context, 'five.refresh');
  });
  await page.waitForFunction(() => fixture.held);
  const scroll = await page.evaluate(() => {
    const { left, right } = fixture.displayNodes;
    left.scrollTop = 79; right.scrollTop = 91;
    fixture.view.viewport.setDisplayMode('shadedEdges').setShowSurfaceEdges(true);
    return { left: left.scrollTop, right: right.scrollTop };
  });
  assert.ok(scroll.left > 0 && scroll.right > 0, 'both production panes actually scroll');
  await page.evaluate(() => fixture.release());
  await page.evaluate(() => fixture.displayRefresh);
  await page.evaluate(async () => {
    const f = fixture;
    await f.view.viewport.applyViewSnapshot(f.view.areas[f.view.activeAreaId].viewReader.snapshot);
  });
  const preserved = await page.evaluate(() => {
    const { left, right, input, canvas, geometry } = fixture.displayNodes;
    return { left: left.scrollTop, right: right.scrollTop, focus: document.activeElement === input,
      start: input.selectionStart, end: input.selectionEnd, value: input.value,
      nodes: left === document.querySelector('.cam-context-pane') && right === document.querySelector('.cam-info-pane')
        && input === document.querySelector('[data-five-field="note"]') && canvas === fixture.view.viewport.renderer.domElement
        && geometry === [...fixture.view.viewport.sceneObjects.values()][0].geometry,
      mode: fixture.view.viewport.displayMode, seams: fixture.view.viewport.showSurfaceEdges,
      peer: fixture.displayPeer.displayMode, peerSeams: fixture.displayPeer.showSurfaceEdges,
      productState: Object.hasOwn(fixture.view, 'displayMode') || Object.hasOwn(fixture.view, 'showSurfaceEdges') };
  });
  assert.deepEqual({ left: preserved.left, right: preserved.right }, scroll);
  assert.equal(preserved.focus, true); assert.equal(preserved.start, 2); assert.equal(preserved.end, 6);
  assert.equal(preserved.value, '保留输入光标和滚动'); assert.equal(preserved.nodes, true); assert.equal(preserved.productState, false);
  assert.equal(preserved.mode, 'shadedEdges'); assert.equal(preserved.seams, true);
  assert.equal(preserved.peer, 'shadedEdges'); assert.equal(preserved.peerSeams, false);
  assert.equal(await seams.count(),0);
  assert.deepEqual(await read(), before);
  await page.evaluate(() => {
    fixture.view.viewport.setShowSurfaceEdges(false);
    fixture.displayPeer.dispose(); fixture.displayPeerHost.remove();
  });
  await page.setViewportSize({ width: 1440, height: 900 });
  writeFileSync(resolve(output, 'viewport-display-report.json'), JSON.stringify({ passed: true, preserved,
    cases: ['viewport local controls without native calls', 'two real viewports with same data remain independent',
      'area remount and data refresh retain mode', 'latest focus, selection, scroll and nodes retained', 'no CAM state mutation'],
    coverage: { native: true, browser: 'Edge', standaloneCef: false } }, null, 2));
}
