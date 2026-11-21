import assert from "node:assert/strict";

// The five leaf choices belong to a child menu, not to the root's action list.
export async function assertDrawingMenu(menu, { polyline = false, splinePending = false } = {}) {
  assert.equal(await menu.getAttribute("role"), "menu");
  const actual = await menu.locator("button").evaluateAll(nodes => nodes.filter(node => node.closest('[role="menu"]') === node.ownerDocument.querySelector("[data-iplane-context-menu]")).map(node => ({
    action: node.dataset.planarSubmenuToggle ? `submenu:${node.dataset.planarSubmenuToggle}` : node.dataset.iplaneAction,
    text: (node.querySelector(".icax-planar-context-menu-label")?.textContent ?? node.textContent).trim(),
  })));
  assert.deepEqual(actual, [
    ...(polyline ? [{ action: "submenu:polyline-segments", text: "切换曲段" }] : []),
    ...(splinePending ? [{ action: "finish-polyline-segment", text: "完成本段" }] : []),
    { action: "close-drawing", text: "闭合" }, { action: "cancel-drawing", text: "取消" }, { action: "finish-drawing", text: "结束" },
  ]);
}
