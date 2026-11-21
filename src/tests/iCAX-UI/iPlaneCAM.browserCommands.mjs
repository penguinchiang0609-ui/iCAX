import assert from "node:assert/strict";

// Exercise the real AppShell menu, including compact Ribbon leaf commands.
// Menu discovery is UI-only; no command is invoked through a private handler.
export async function revealPlaneCommand(page, commandId) {
  const selector = `[data-command-id="${commandId}"][data-action="ribbon-command"]`;
  const visible = async () => {
    for (const command of await page.locator(selector).all()) if (await command.isVisible()) return command;
    return null;
  };
  let command = await visible();
  if (command) return command;
  for (const toggle of await page.locator('.product-ribbon [data-action="ribbon-command-menu-toggle"]').all()) {
    if (!await toggle.isVisible() || !await toggle.isEnabled()) continue;
    await toggle.click();
    command = await visible();
    if (command) return command;
    if (await toggle.getAttribute("aria-expanded") === "true") await toggle.click();
  }
  if (await page.locator(`[data-planar-tool="${commandId}"]`).count() || commandId.startsWith("plane.draw-")) return revealPlaneTool(page, commandId);
  assert.fail(`No available Ribbon or drawing-palette entry for ${commandId}`);
}

export async function clickPlaneCommand(page, commandId) {
  await (await revealPlaneCommand(page, commandId)).click();
}

// The product retains its five direct drawing buttons and offers additional
// tools through their adjacent SDK group arrows. Discover and click only real
// visible controls; do not dispatch a command or synthesize a private handler.
export async function revealPlaneTool(page, toolId, { viaMenu = false, groupId } = {}) {
  const selector = `[data-planar-tool="${toolId}"]`;
  const visible = async root => { for (const tool of await root.locator(selector).all()) if (await tool.isVisible()) return tool;return null; };
  if (!viaMenu) { const direct = await visible(page);if (direct) return direct; }
  const arrows = page.locator(groupId ? `[data-planar-tool-menu-toggle="${groupId}"]` : "[data-planar-tool-menu-toggle]");
  for (const arrow of await arrows.all()) {
    if (!await arrow.isVisible() || !await arrow.isEnabled()) continue;
    const id = await arrow.getAttribute("data-planar-tool-menu-toggle");
    if (await arrow.getAttribute("aria-expanded") !== "true") await arrow.click();
    const menu = page.locator(`[data-planar-tool-menu="${id}"]`), item = await visible(menu);
    if (item) return item;
    if (await arrow.getAttribute("aria-expanded") === "true") await arrow.click();
  }
  assert.fail(`No available drawing group entry for ${toolId}`);
}

export async function clickPlaneTool(page, toolId, options) {
  await (await revealPlaneTool(page, toolId, options)).click();
}

export async function ensurePlaneObjectsExpanded(page) {
  const toggle = page.locator('[data-planar-tool="plane.objects-toggle"]');
  assert.equal(await toggle.count(), 1, "The current product must expose its Objects palette control");
  if (await toggle.getAttribute("aria-pressed") !== "true") await toggle.click();
  await page.waitForFunction(() => document.querySelector(".cam-workbench")?.dataset.iplaneObjectsOpen === "true");
  assert.equal(await toggle.getAttribute("aria-pressed"), "true");
}
