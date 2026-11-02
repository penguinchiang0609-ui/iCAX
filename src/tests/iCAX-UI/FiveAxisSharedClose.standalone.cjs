// Real standalone CEF and native file persistence, using the isolated audit
// project. Run after the full rapid editor audit has saved its expected result.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { chromium } = require('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');

(async () => {
  const output = 'output/tests/full-edit-audit-20261023';
  const file = path.resolve(output, 'rapid.i5cam').replaceAll('\\', '/');
  const expected = JSON.parse(fs.readFileSync(path.join(output, 'rapid-entry-report.json'))).final;
  const browser = await chromium.connectOverCDP('http://127.0.0.1:9483');
  const page = browser.contexts()[0].pages()[0];
  page.setDefaultTimeout(60000);
  const ready = async () => {
    await page.waitForFunction(async () => {
      const { getProjectView } = await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');
      window.sharedCloseView = getProjectView(__icaxAppShell.getState().activeProjectId);
      return sharedCloseView?.scene?.domain?.setups?.[0]?.generatedGeometry.transitions?.length > 1
        && !sharedCloseView.pending;
    });
  };
  await page.evaluate(file => __icaxAppShell.openProject(file, 'icax.five-axis-cam'), file);
  await ready();
  await page.evaluate(async () => {
    await __icaxAppShell.selectRibbonTab('toolpath');
  });
  await page.getByRole('tab', { name: '空移', exact: true }).click();
  await page.waitForFunction(async () => {
    const { getWindowCloseGuard } = await import('../../../apps/five-axis-cam/webpage/entry.mjs');
    return !getWindowCloseGuard({ project: { projectId: __icaxAppShell.getState().activeProjectId },
      projectProxy: sharedCloseView.viewportSceneProxy?.project });
  });
  const closedId = await page.evaluate(() => {
    window.sharedClosedView = sharedCloseView;
    return __icaxAppShell.getState().activeProjectId;
  });
  await page.locator(`[data-action="close-open-project"][data-project-id="${closedId}"]`).click();
  await page.locator('[data-project-close-choice="save"]').click();
  await page.waitForFunction(id => !document.querySelector(`[data-action="close-open-project"][data-project-id="${id}"]`), closedId);
  await page.evaluate(file => __icaxAppShell.openProject(file, 'icax.five-axis-cam'), file);
  await ready();
  assert.equal(await page.evaluate(() => sharedCloseView === sharedClosedView), false);
  await page.evaluate(async () => {
    await __icaxAppShell.selectRibbonTab('toolpath');
  });
  await page.getByRole('tab', { name: '空移', exact: true }).click();
  const actual = await page.evaluate(() => {
    const setup = sharedCloseView.scene.domain.setups[0];
    return { settings: setup.rapidSettings, transitions: setup.generatedGeometry.transitions.map(t => ({
      from: t.from, to: t.to, policy: t.policy, manualPath: t.manualPath, points: t.points,
      directions: t.directions, geometryStatus: t.geometryStatus, validation: t.validation,
    })) };
  });
  assert.deepEqual(JSON.parse(JSON.stringify(actual)), expected);
  await page.waitForFunction(() => {
    const view = sharedCloseView;
    const transitions = view.scene.domain.setups[0].generatedGeometry.transitions
      .filter(t => ['Generated', 'Draft'].includes(t.geometryStatus) && t.points?.length);
    const lines = view.fiveGeneratedPreview?.group.children.filter(o => o.userData.generatedRapid);
    return lines?.length === transitions.length && lines.every((line, i) => {
      const actual = [...line.geometry.attributes.position.array], expected = transitions[i].points.flat();
      return actual.length === expected.length && actual.every((v, j) => Math.abs(v - expected[j]) < 1e-4);
    });
  });
  await page.screenshot({ path: path.join(output, 'shared-close-rapid-reopened.png') });
  fs.writeFileSync(path.join(output, 'shared-close-rapid-report.json'), JSON.stringify({ passed: true,
    standaloneCef: true, sameProcessSaveCloseReopen: true, freshProjectView: true,
    nativeParameterEquality: true, renderedPathsEquality: true }, null, 2));
  console.log('PASS standalone rapid page save/close/reopen, native values and rendered paths');
  process.exit(0);
})().catch(error => { console.error(error); process.exit(1); });
