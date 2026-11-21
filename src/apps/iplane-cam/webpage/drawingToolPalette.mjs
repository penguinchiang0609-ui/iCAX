import { synchronizePlanarToolPalette, closePlanarToolPaletteMenu, disposePlanarToolPalette } from "../../../iCAX-UI/SDK/Cad/planarToolPalette.mjs";
import { planarDrawingToolGroups } from "../../../iCAX-UI/SDK/Cad/planarRibbon.mjs";
import { getWorkspaceState, isGeometryEditing, readSnapshot } from "./workspaceState.mjs";

export function renderDrawingToolPalette() {
  return '<div class="iplane-tool-palette-host" data-iplane-tool-palette-host data-iplane-key="tool-palette"></div>';
}

export function closeDrawingToolPaletteMenu(view, restoreFocus = false) {
  return closePlanarToolPaletteMenu(view.iplaneToolPaletteHost, { restoreFocus });
}

export function disposeDrawingToolPalette(view) {
  disposePlanarToolPalette(view.iplaneToolPaletteHost);delete view.iplaneToolPaletteHost;
}

export function synchronizeDrawingToolPalette(context, view, { onCommand } = {}) {
  const host = context.mount?.querySelector("[data-iplane-tool-palette-host]");if (!host) return;
  if (view.iplaneToolPaletteHost && view.iplaneToolPaletteHost !== host) disposeDrawingToolPalette(view);
  view.iplaneToolPaletteHost = host;
  host.style.cssText = "position:absolute;left:8px;top:8px;bottom:8px;z-index:32;pointer-events:none;display:flex;align-items:flex-start";
  host.hidden = !isGeometryEditing(view);host.style.display = host.hidden ? "none" : "flex";if (host.hidden) { closeDrawingToolPaletteMenu(view);return; }
  const ui = getWorkspaceState(view), capabilities = readSnapshot(context, view).capabilities ?? {};
  const methods = capabilities.enabledMethods ?? [];
  const tools = [
    { id: "plane.select", label: "选择", icon: "select", group: "select" },
    ...planarDrawingToolGroups.flatMap(group => group.tools.map(tool => ({
      id: `plane.draw-${tool.key}`, label: tool.label, icon: tool.icon, group: group.id, groupLabel: group.label,
      menuLabel: group.label,
      menuItems: tool.alternatives.filter(item => (ui.geometryCapabilities?.primitives ?? []).includes(item.key))
        .map(item => ({ id: `plane.draw-${item.key}`, label: item.label, icon: item.icon })),
    }))),
    { id: "plane.text", label: "文字", icon: "text", group: "artwork", disabled: Boolean(ui.activeSketchId) || !methods.includes("PlaneText.Create"), reason: ui.activeSketchId ? "请在底图中添加文字" : !methods.includes("PlaneText.Create") ? "当前模块未提供文字对象" : undefined },
    { id: "plane.image", label: "图片", icon: "image", group: "artwork", disabled: Boolean(ui.activeSketchId) || !methods.includes("PlaneImage.Import"), reason: ui.activeSketchId ? "请在底图中添加参考图片" : !methods.includes("PlaneImage.Import") ? "当前模块未提供参考图片" : undefined },
    { id: "plane.erase", label: "橡皮擦", icon: "eraser", group: "edit", disabled: !(capabilities.drawingEdits ?? []).includes("delete") },
    { id: "plane.objects-toggle", label: "对象面板", icon: "bom", group: "panels", active: ui.objectsPanelOpen === true },
  ];
  for (const tool of tools.flatMap(tool => [tool, ...(tool.menuItems ?? [])])) {
    const availability = ui.commandAvailability?.[tool.id];
    tool.active ??= ui.activeTool === tool.id;
    tool.disabled ||= Boolean(ui.busy || ui.drawingCommitting || ui.geometryTaskId || availability?.disabled);
    tool.reason ??= availability?.unavailableReason;
  }
  synchronizePlanarToolPalette(host, { tools, onCommand, label: "绘图工具" });
}
