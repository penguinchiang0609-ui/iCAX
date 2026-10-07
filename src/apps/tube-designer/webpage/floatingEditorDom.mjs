import { patchDomNode } from "./punchDomPatch.mjs";
import { capturePaneInteraction } from "../../_shared/workbench/utils/paneInteractionState.mjs";

// Small parameter editors belong above the workbench. Their presentation must
// never participate in the WebGL host's size or lifetime.
const LAYERS = "[data-floating-editor-layer]";
const WINDOWS = "[data-floating-editor-window]";
const rendered = new WeakMap();
const bindings = new WeakMap();

function parse(mount, html) {
  const template = mount.ownerDocument.createElement("template");
  template.innerHTML = html;
  return template;
}
export function stripFloatingEditorWindows(mount, html) {
  const template = parse(mount, html);
  template.content.querySelectorAll(LAYERS).forEach(node => node.remove());
  return template.innerHTML.trim();
}
function sceneIdentity(view) {
  // Resource identity owns the displayed geometry. A refreshed scene may have
  // new repository/history metadata after saving settings without changing it.
  const designer = view.scene?.tubeDesigner ?? {};
  const groups = designer.nestingGroups ?? designer.manufacturingGroups ?? [];
  const result = view.tubeDesignerNestingResult ?? designer.nestingResult ?? designer.nesting ?? {};
  const plans = Array.isArray(result) ? result : result.plans ?? result.stockPlans ?? [];
  return JSON.stringify([
    (designer.members ?? []).map(member => [member.entityId, member.previewGeometryResourceId,
      member.previewGeometryResourceVersion, member.transform]),
    groups.flatMap(group => group.parts ?? []).map(part => [part.entityId,
      part.thumbnailGeometryResourceId, part.thumbnailGeometryResourceVersion, part.transform]),
    plans.map(plan => [plan.id ?? plan.stockId, plan.stockLength ?? plan.length,
      plan.profile, plan.profileSnapshot, plan.placements, plan.geometryResourceId, plan.geometryResourceVersion]),
    view.tubeDesignerNestingSelectionKind, view.tubeDesignerActivePartId,
    view.tubeDesignerActiveNestingPartId, view.tubeDesignerActiveNestingPlanId,
    view.tubeDesignerActiveNestingPlacementId, view.tubeDesignerNestingDockLayout,
  ]);
}
function workflowSuffix(mount, html) {
  const template = parse(mount, stripFloatingEditorWindows(mount, html));
  // These are HTML-owned presentation regions, including the stale-result
  // warning that changes when the settings are saved.
  template.content.querySelectorAll(".tube-designer-dock-bottom-host,.tube-designer-bottom-splitter,[data-tube-designer-operation-wait]").forEach(node => node.remove());
  return template.innerHTML.trim();
}
export function rememberFloatingEditorDom(view, mount, { suffix, sceneProxy = null }) {
  if (!mount?.querySelector) return;
  const previous = rendered.get(mount);
  rendered.set(mount, { view, area: view.activeAreaId, sceneProxy,
    scene: sceneIdentity(view), suffix: workflowSuffix(mount, suffix),
    hadEditors: !!mount.querySelector(LAYERS) || previous?.view === view && previous.area === view.activeAreaId && previous.hadEditors });
}
export function renderFloatingEditorResizeHandles() {
  return ["e", "s", "se"].map(edge => `<span class="tube-floating-editor-resize is-${edge}" data-floating-editor-resize="${edge}" aria-hidden="true"></span>`).join("");
}
function rememberGeometry(view, panel) {
  const key = panel?.dataset?.floatingEditorWindow;
  if (!key || !panel.style.width && !panel.style.height && !panel.style.left) return;
  const state = (view.tubeDesignerFloatingEditorWindows ??= {});
  state[key] = { width: panel.style.width, height: panel.style.height,
    position: panel.style.position, left: panel.style.left, top: panel.style.top,
    margin: panel.style.margin };
}
function restoreGeometry(view, panel) {
  const state = view?.tubeDesignerFloatingEditorWindows?.[panel.dataset.floatingEditorWindow];
  if (state) Object.assign(panel.style, state);
}
export function moveFloatingEditorWindowsToWorkspace(mount, view) {
  const workbench = mount?.querySelector?.(".cam-workbench");
  if (!workbench) return;
  for (const layer of mount.querySelectorAll(LAYERS)) {
    if (layer.parentElement !== workbench) workbench.append(layer);
    for (const panel of layer.querySelectorAll(WINDOWS)) restoreGeometry(view, panel);
  }
}
export function patchFloatingEditorWindows(mount, html, view = null) {
  const workbench = mount?.querySelector?.(".cam-workbench");
  if (!workbench) return false;
  const next = parse(mount, html).content;
  const oldLayers = new Map([...mount.querySelectorAll(LAYERS)].map(layer => [layer.dataset.floatingEditorLayer, layer]));
  const nextLayers = new Map([...next.querySelectorAll(LAYERS)].map(layer => [layer.dataset.floatingEditorLayer, layer]));
  const selectors = [".cam-context-pane", ".cam-info-pane", ...[...oldLayers.values()].flatMap(layer =>
    [...layer.querySelectorAll(WINDOWS)].map(panel => `[data-floating-editor-window="${panel.dataset.floatingEditorWindow}"]`))];
  const restore = capturePaneInteraction(mount, selectors);
  for (const [key, current] of oldLayers) {
    const replacement = nextLayers.get(key);
    for (const panel of current.querySelectorAll(WINDOWS)) if (view) rememberGeometry(view, panel);
    if (replacement) {
      // Generic reconciliation keeps exact controls and their event listeners.
      patchDomNode(current, replacement);
      nextLayers.delete(key);
    } else current.remove();
  }
  for (const layer of nextLayers.values()) workbench.append(layer);
  if (view) moveFloatingEditorWindowsToWorkspace(mount, view);
  restore();
  return true;
}
function patchNestingRegions(mount, { left, right, overlay, suffix }) {
  const restore = capturePaneInteraction(mount, [".cam-context-pane", ".cam-info-pane", ".tube-designer-dock-bottom-host",
    '[data-tube-designer-dock-panel="parts"]', '[data-tube-designer-dock-panel="inspector"]', '[data-tube-designer-dock-panel="results"]']);
  for (const [kind, html, rawSelector, fallback] of [
    ["parts", left, ".tube-designer-cutting-parts", ".cam-context-pane"],
    ["inspector", right, ".tube-designer-cutting-inspector", ".cam-info-pane"],
    ["results", suffix, ".tube-designer-nesting-bottom", null],
  ]) {
    if (html == null) continue;
    const fragment = parse(mount, html).content;
    const panel = mount.querySelector(`[data-tube-designer-dock-panel="${kind}"]`);
    const container = panel ?? (fallback ? mount.querySelector(fallback) : mount.querySelector(rawSelector));
    if (!container) continue;
    const next = container.cloneNode(false);
    const content = panel || !fallback ? fragment.querySelector(rawSelector) : fragment;
    if (!content) continue;
    next.append(...content.childNodes);
    patchDomNode(container, next);
  }
  const viewport = mount.querySelector(".cam-viewport");
  if (viewport && overlay != null) {
    const fragment = parse(mount, typeof overlay === "function" ? overlay() : overlay).content;
    for (const selector of [".tube-designer-nesting-empty", ".tube-designer-cutting-scene-header",
      ".tube-designer-nesting-preview-status", ".tube-designer-cutting-scene-summary",
      "[data-tube-designer-part-progress]", ".tube-designer-cutting-scene-help"]) {
      const current = viewport.querySelector(selector), next = fragment.querySelector(selector);
      if (current && next) patchDomNode(current, next);
      else if (current) current.remove();
      else if (next) viewport.append(next);
    }
  }
  const fragment = parse(mount, suffix).content;
  const workbench = mount.querySelector(".cam-workbench");
  for (const selector of ["[data-tube-designer-operation-wait]"]) {
    const current = mount.querySelector(selector), next = fragment.querySelector(selector);
    if (current && next) patchDomNode(current, next);
    else if (current) current.remove();
    else if (next) workbench.append(next);
  }
  restore();
}
export function patchFloatingEditorDom(view, mount, { suffix, sceneProxy = null, left, right, overlay }) {
  const previous = rendered.get(mount);
  if (previous?.view !== view || previous.area !== view.activeAreaId
    || previous.sceneProxy !== sceneProxy || previous.scene !== sceneIdentity(view)
    || previous.suffix !== workflowSuffix(mount, suffix)
    || !previous.hadEditors && !parse(mount, suffix).content.querySelector(LAYERS)) return false;
  if (!patchFloatingEditorWindows(mount, suffix, view)) return false;
  // Once the geometry guard passes, update every live HTML region as well.
  // Pass-through search/filter/selection controls must keep their normal effect.
  if (view.activeAreaId === "nesting") patchNestingRegions(mount, { left, right, overlay, suffix });
  rememberFloatingEditorDom(view, mount, { suffix, sceneProxy });
  return true;
}
export function bindFloatingEditorWindows(mount, view) {
  moveFloatingEditorWindowsToWorkspace(mount, view);
  const current = bindings.get(mount);
  if (current) { current.view = view; return; }
  const binding = { view }; bindings.set(mount, binding);
  mount.addEventListener("pointerup", () => {
    for (const panel of mount.querySelectorAll(WINDOWS)) rememberGeometry(binding.view, panel);
  });
  mount.addEventListener("pointerdown", event => {
    const handle = event.target.closest?.("[data-floating-editor-resize]");
    const panel = handle?.closest(WINDOWS);
    if (!panel || event.button !== 0) return;
    event.preventDefault(); event.stopPropagation();
    const rect = panel.getBoundingClientRect(), win = panel.ownerDocument.defaultView;
    const start = { x: event.clientX, y: event.clientY, width: rect.width, height: rect.height };
    const edge = handle.dataset.floatingEditorResize;
    Object.assign(panel.style, { position: "fixed", left: `${rect.left}px`, top: `${rect.top}px`, margin: "0" });
    handle.setPointerCapture(event.pointerId);
    const move = next => {
      if (next.pointerId !== event.pointerId || !panel.isConnected) return;
      if (edge.includes("e")) panel.style.width = `${Math.max(280, Math.min(win.innerWidth - rect.left - 12, start.width + next.clientX - start.x))}px`;
      if (edge.includes("s")) panel.style.height = `${Math.max(180, Math.min(win.innerHeight - rect.top - 12, start.height + next.clientY - start.y))}px`;
      rememberGeometry(binding.view, panel);
    };
    const end = next => {
      if (next.pointerId !== event.pointerId) return;
      for (const name of ["pointerup", "pointercancel", "lostpointercapture"]) handle.removeEventListener(name, end);
      handle.removeEventListener("pointermove", move);
      if (handle.hasPointerCapture(event.pointerId)) handle.releasePointerCapture(event.pointerId);
      rememberGeometry(binding.view, panel);
    };
    handle.addEventListener("pointermove", move);
    for (const name of ["pointerup", "pointercancel", "lostpointercapture"]) handle.addEventListener(name, end);
  });
}
