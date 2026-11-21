import { ensurePlaneObjectsExpanded } from "./iPlaneCAM.browserCommands.mjs";
import assert from "node:assert/strict";
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { resolve } from "node:path";
import { clickPlaneCommand, revealPlaneCommand } from "./iPlaneCAM.browserCommands.mjs";

// Commands are exercised through the public controls. Native queries only
// inspect outcomes; the held-response helper delays the real SDO transport.
export async function exerciseBottomTabs({ page, report, source, output, waitForAsync, passed }) {
  await ensurePlaneObjectsExpanded(page);
  const command = page.locator("[data-iplane-command-input]");
  const tab = id => page.locator(`[data-bottom-dock-tab="${id}"]`);
  const panel = id => page.locator(`[data-bottom-dock-panel="${id}"]`);
  const settled = () => waitForAsync(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    const view = getProjectView(__icaxAppShell.getState().activeProjectId), ui = view.iplaneCamUI;
    if (ui.operationError) throw new Error(ui.operationError);
    return !ui.busy && !ui.geometryTaskId && !ui.runningTaskId && !view.iplaneLayers?.queued;
  });
  const model = () => page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
    return (await getProjectView(__icaxAppShell.getState().activeProjectId).sceneProxy.invoke("PlaneScene.ReadState", {})).iplaneCam;
  });
  const selectTab = async id => {
    await tab(id).click();
    await page.waitForFunction(id => document.querySelector(`[data-bottom-dock-tab="${id}"]`)?.getAttribute("aria-selected") === "true", id);
    assert.equal(await panel(id).isVisible(), true);
  };
  const runCommand = async text => {
    await selectTab("command");await command.fill(text);await command.press("Enter");
    await waitForAsync(async () => {
      const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");
      const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;
      if (ui.operationError) throw new Error(ui.operationError);
      return !ui.busy && !ui.geometryTaskId && ui.commandInput === "";
    });
    await page.waitForFunction(() => document.querySelector("[data-iplane-command-input]")?.value === "");
  };
  const assertLayout = async area => {
    const result = await page.evaluate(() => {
      const box = selector => { const node = document.querySelector(selector), r = node?.getBoundingClientRect();return r && { x: r.x, y: r.y, width: r.width, height: r.height, right: r.right, bottom: r.bottom }; };
      return { status: box("[data-iplane-statusbar]"), canvas: box(".icax-three-viewport canvas"), viewer: box(".cam-viewer"), dock: box(".bottom-dock"), right: box(".cam-info-pane"), repeatedSettings: document.querySelectorAll('.cam-info-pane [data-iplane-action^="toggle-"]').length };
    });
    assert.ok(result.status && result.status.height > 0, `The ${area} scene status bar is missing`);
    assert.ok(result.status.y >= result.canvas.bottom - 2, `The ${area} status bar is not below its canvas`);
    assert.ok(result.status.bottom <= result.dock.y + 2, `The ${area} status bar must stay above the command/log dock`);
    assert.ok(result.status.x >= result.viewer.x - 1 && result.status.right <= result.viewer.right + 1, `The ${area} status bar must belong to the scene column`);
    assert.equal(result.repeatedSettings, 0, "The old right-side interaction settings must be removed");
    return result;
  };

  assert.equal(await tab("command").count(), 1);assert.equal(await tab("logs").count(), 1);
  assert.equal(await tab("command").getAttribute("role"), "tab");assert.equal(await tab("logs").getAttribute("role"), "tab");
  assert.equal(await panel("command").getAttribute("role"), "tabpanel");assert.equal(await panel("logs").getAttribute("role"), "tabpanel");
  assert.equal(await tab("command").getAttribute("aria-selected"), "true");
  assert.equal(await command.count(), 1);assert.equal(await page.locator(".log-list").count(), 1);
  const initialInput = await command.boundingBox(), initialDock = await page.locator(".bottom-dock").boundingBox();
  assert.ok(initialInput.y >= initialDock.y && initialInput.y + initialInput.height <= initialDock.y + initialDock.height + 1, "The command input must fit inside the initial tabbed dock");
  report.drawingLayout = await assertLayout("drawing");
  await page.screenshot({ path: resolve(output, "bottom-tabs-drawing.png") });
  passed("The actual AppShell exposes Command and Logs as two tabs, with one retained command field and one framework log list beneath the scene status bar");

  const raw = "  @12.50, -3.125  ";await command.fill(raw);
  await page.evaluate(() => {
    const input = document.querySelector("[data-iplane-command-input]");input.setSelectionRange(3, 10);
    globalThis.__bottomTabProbe = { input, canvas: document.querySelector(".icax-three-viewport canvas"), events: 0, logs: document.querySelector(".log-list") };
    input.addEventListener("bottom-input-probe", () => __bottomTabProbe.events++);
  });
  for (let index = 0; index < 3; index++) { await selectTab("logs");assert.equal(await command.isVisible(), false);await selectTab("command"); }
  const retainedCommand = await page.evaluate(() => {
    const probe = __bottomTabProbe, input = document.querySelector("[data-iplane-command-input]");input.dispatchEvent(new Event("bottom-input-probe"));
    return { sameInput: input === probe.input, connected: probe.input.isConnected, value: input.value, selection: [input.selectionStart, input.selectionEnd], sameCanvas: probe.canvas === document.querySelector(".icax-three-viewport canvas"), events: probe.events };
  });
  assert.deepEqual(retainedCommand, { sameInput: true, connected: true, value: raw, selection: [3, 10], sameCanvas: true, events: 1 });report.retainedCommand = retainedCommand;
  passed("Repeated real tab clicks preserve the raw command string, caret selection, input/listener identity and existing viewport canvas");

  await command.fill("");
  const handle = await page.locator("[data-bottom-dock-resize]").boundingBox(), beforeResize = await page.locator(".bottom-dock").boundingBox();
  await page.mouse.move(handle.x + handle.width / 2, handle.y + handle.height / 2);await page.mouse.down();
  await page.mouse.move(handle.x + handle.width / 2, handle.y - 110, { steps: 5 });await page.mouse.up();
  const afterResize = await page.locator(".bottom-dock").boundingBox();assert.ok(afterResize.height > beforeResize.height + 60);
  assert.equal(await page.evaluate(() => __bottomTabProbe.canvas === document.querySelector(".icax-three-viewport canvas")), true);
  report.resize = { before: beforeResize, after: afterResize };await assertLayout("drawing");
  passed("The existing AppShell bottom resize handle resizes the shared dock while retaining the canvas and the scene status bar layout");

  await command.fill("暂存的命令文本");await command.focus();await command.press("Home");await command.press("Shift+ArrowRight");
  const beforeLog = await page.evaluate(() => { const input = document.querySelector("[data-iplane-command-input]");return { value: input.value, selection: [input.selectionStart, input.selectionEnd] }; });
  await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const actions = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneLive.context.actions;for (let index = 0; index < 90; index++) actions.log("info", `底部标签验收 ${index}`); });
  assert.equal(await tab("command").getAttribute("aria-selected"), "true");
  const afterLog = await page.evaluate(() => { const input = document.querySelector("[data-iplane-command-input]");return { value: input.value, selection: [input.selectionStart, input.selectionEnd], focused: document.activeElement === input, same: input === __bottomTabProbe.input }; });
  assert.deepEqual(afterLog, { ...beforeLog, focused: true, same: true });
  await selectTab("logs");await page.locator(".log-list").getByText("底部标签验收 89", { exact: true }).waitFor();
  const logBefore = await page.evaluate(() => { const logs = document.querySelector(".log-list");logs.scrollTop = Math.floor((logs.scrollHeight - logs.clientHeight) * .3);return { scroll: logs.scrollTop, oldNode: logs === __bottomTabProbe.logs }; });
  assert.ok(logBefore.scroll > 0);assert.equal(logBefore.oldNode, true);
  await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const actions = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneLive.context.actions;for (let index = 90; index < 100; index++) actions.log("info", `底部标签验收 ${index}`); });
  assert.equal(await tab("logs").getAttribute("aria-selected"), "true");
  const logAfter = await page.evaluate(() => { const logs = document.querySelector(".log-list");return { scroll: logs.scrollTop, same: logs === __bottomTabProbe.logs, count: logs.querySelectorAll(".log-row").length }; });
  assert.equal(logAfter.scroll, logBefore.scroll);assert.equal(logAfter.same, true);
  await selectTab("command");assert.equal(await command.inputValue(), "暂存的命令文本");
  await selectTab("logs");assert.equal(await page.locator(".log-list").evaluate(node => node.scrollTop), logAfter.scroll);
  report.logRetention = { before: logBefore, after: logAfter };
  passed("Appending real AppShell logs neither steals the active tab or command focus nor resets the retained log node and user scroll position");

  await selectTab("command");await command.fill("");
  for (const [key, uiField] of [["snap", "snapEnabled"], ["ortho", "orthoEnabled"], ["grid", "gridVisible"]]) {
    const control = page.locator(`[data-iplane-statusbar] [data-iplane-action="toggle-${key}"]`);
    const initial = await control.getAttribute("aria-pressed");await control.click();
    assert.equal(await control.getAttribute("aria-pressed"), String(initial !== "true"));
    const ui = await page.evaluate(async field => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI[field]; }, uiField);
    assert.equal(ui, initial !== "true");
    const ribbon = await revealPlaneCommand(page, `plane.${key === "snap" ? "snap" : key}-toggle`);
    assert.equal(await ribbon.getAttribute("aria-pressed") ?? await ribbon.getAttribute("aria-checked"), String(ui));
    if (key === "grid") assert.equal(await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).viewport.scene.getObjectByName("iCAX XY Workbench Grid")?.visible; }), ui);
    if (await page.locator('.ribbon-command-menu-toggle[aria-expanded="true"]').count()) await page.keyboard.press("Escape");
    await control.click();assert.equal(await control.getAttribute("aria-pressed"), initial);
  }
  passed("Capture, orthogonal and grid controls live in the scene status bar and stay synchronized with the actual Ribbon and viewport state");

  await clickPlaneCommand(page, "plane.draw-polyline");
  const canvas = await page.locator(".icax-three-viewport canvas").boundingBox();
  const first = { x: canvas.x + canvas.width * .3, y: canvas.y + canvas.height * .45 }, next = { x: canvas.x + canvas.width * .5, y: canvas.y + canvas.height * .45 };
  await page.mouse.click(first.x, first.y);await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.commandPoints.length === 1; });
  await page.mouse.move(next.x, next.y);
  const prefix = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;return { tool: ui.activeTool, points: structuredClone(ui.commandPoints) }; });
  await selectTab("logs");await selectTab("command");
  const stillDrawing = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;return { tool: ui.activeTool, points: structuredClone(ui.commandPoints) }; });
  assert.deepEqual(stillDrawing, prefix);assert.equal((await model()).drawings.length, 0);
  await page.mouse.move(next.x, next.y);await page.locator('[data-iplane-dimension-row="length"]').waitFor({ state: "visible" });
  await command.focus();await command.press("Escape");await runCommand("圆 0,0 5");assert.equal((await model()).drawings.length, 1);
  passed("Switching the command/log tabs preserves an active polyline prefix and dimensions, and the moved command input still creates exact native geometry");

  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="nesting"]').click();await settled();
  report.nestingLayout = await assertLayout("nesting");
  const inputs = resolve(output, "inputs");mkdirSync(inputs, { recursive: true });const partPath = resolve(inputs, "bottom-tabs-part.dxf");
  writeFileSync(partPath, readFileSync(resolve(source, "tests/icax-plugins/product/iPlaneCAM/Fixtures/part_with_hole.dxf")));
  await page.evaluate(path => __planeNativeFileQueue.push(path), partPath);await clickPlaneCommand(page, "plane.import-parts");
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);return !view.iplaneCamUI.busy && view.scene.iplaneCam.parts.length === 1; });
  const part = (await model()).parts[0];
  await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${part.id}"]`).click();
  await page.locator(`[data-iplane-action="sketch-open"][data-iplane-object-id="${part.id}"]`).click();
  await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const ui = getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI;return !ui.busy && Boolean(ui.activeSketchId); });
  report.sketchLayout = await assertLayout("sketch");await selectTab("logs");await selectTab("command");
  assert.equal(await page.evaluate(() => __bottomTabProbe.canvas === document.querySelector(".icax-three-viewport canvas")), true);
  await clickPlaneCommand(page, "plane.sketch-cancel");await waitForAsync(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");return !getProjectView(__icaxAppShell.getState().activeProjectId).iplaneCamUI.activeSketchId; });await settled();
  passed("Drawing, nesting and an owned CAD sketch share the same tabs and central status bar without duplicating right-side settings or replacing the canvas");

  await page.locator('[data-action="select-ribbon-tab"][data-tab-id="drawing"]').click();await settled();
  for (let index = 0; index < 15; index++) await runCommand(`圆 ${20 + index * 8},20 2`);
  const drawing = (await model()).drawings[0];await page.locator(`[data-iplane-action="select-object"][data-iplane-object-id="${drawing.id}"]`).click();
  await page.setViewportSize({ width: 1280, height: 600 });
  const propertyName = page.locator('[data-iplane-field="name"]');await propertyName.fill("保留属性文本草稿");await propertyName.press("Home");await propertyName.press("ArrowRight");await propertyName.press("Shift+ArrowRight");await propertyName.press("Shift+ArrowRight");
  const beforeRefresh = await page.evaluate(async () => {
    const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), field = document.querySelector('[data-iplane-field="name"]'), left = document.querySelector("[data-iplane-list]"), right = document.querySelector(".cam-info-pane");
    left.scrollTop = (left.scrollHeight - left.clientHeight) * .2;right.scrollTop = (right.scrollHeight - right.clientHeight) * .2;
    globalThis.__bottomRefreshProbe = { field, command: document.querySelector("[data-iplane-command-input]"), logs: document.querySelector(".log-list"), canvas: document.querySelector(".icax-three-viewport canvas"), left, right, events: 0 };
    field.addEventListener("bottom-property-probe", () => __bottomRefreshProbe.events++);
    return { text: field.value, selection: [field.selectionStart, field.selectionEnd], scroll: [left.scrollTop, right.scrollTop], camera: view.viewport.getCameraState() };
  });
  assert.ok(beforeRefresh.scroll.every(value => value > 0), "Both actual side panes must scroll for the asynchronous refresh case");
  await page.evaluate(async () => { const { makeSDOMethodCodeFromName } = await import("/iCAX-UI/SDK/SDO/sdoMethod.mjs");__planeNativeBridge.holdNextResponse(String(makeSDOMethodCodeFromName("PlaneScene.ReadState"))); });
  const refresh = page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs"), { refreshPlaneState } = await import("/apps/iplane-cam/webpage/nativeOperations.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId);await refreshPlaneState(view.iplaneLive.context, view, view.iplaneLive.ops, { refreshView: true }); });
  await waitForAsync(() => __planeNativeBridge.isResponseHeld());
  const latest = await page.evaluate(() => { const p = __bottomRefreshProbe;p.left.scrollTop = (p.left.scrollHeight - p.left.clientHeight) * .8;p.right.scrollTop = (p.right.scrollHeight - p.right.clientHeight) * .8;return { scroll: [p.left.scrollTop, p.right.scrollTop], text: p.field.value, selection: [p.field.selectionStart, p.field.selectionEnd] }; });
  await page.evaluate(() => __planeNativeBridge.releaseResponse());await refresh;
  const afterRefresh = await page.evaluate(async () => { const { getProjectView } = await import("/apps/_shared/workbench/state/projectViewStore.mjs");const view = getProjectView(__icaxAppShell.getState().activeProjectId), p = __bottomRefreshProbe, field = document.querySelector('[data-iplane-field="name"]');field.dispatchEvent(new Event("bottom-property-probe"));return { sameField: field === p.field, sameCommand: p.command === document.querySelector("[data-iplane-command-input]"), sameLogs: p.logs === document.querySelector(".log-list"), sameCanvas: p.canvas === document.querySelector(".icax-three-viewport canvas"), focused: document.activeElement === field, text: field.value, selection: [field.selectionStart, field.selectionEnd], scroll: [p.left.scrollTop, p.right.scrollTop], events: p.events, camera: view.viewport.getCameraState() }; });
  assert.equal(afterRefresh.sameField, true);assert.equal(afterRefresh.sameCommand, true);assert.equal(afterRefresh.sameLogs, true);assert.equal(afterRefresh.sameCanvas, true);assert.equal(afterRefresh.focused, true);assert.equal(afterRefresh.text, latest.text);assert.deepEqual(afterRefresh.selection, latest.selection);assert.deepEqual(afterRefresh.scroll, latest.scroll);assert.equal(afterRefresh.events, 1);assert.deepEqual(afterRefresh.camera, beforeRefresh.camera);report.asyncRefresh = { beforeRefresh, latest, afterRefresh };
  await page.locator('[data-iplane-action="reset-draft"]').click();await page.setViewportSize({ width: 1600, height: 1000 });
  await selectTab("command");await page.screenshot({ path: resolve(output, "bottom-tabs-command.png") });await selectTab("logs");await page.screenshot({ path: resolve(output, "bottom-tabs-logs.png") });
  passed("A delayed real Scene response preserves property focus/caret/listeners and latest left/right scroll together with the retained command field, log list, canvas and camera");
}
