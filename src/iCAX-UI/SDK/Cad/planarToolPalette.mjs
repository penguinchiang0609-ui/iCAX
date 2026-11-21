import { createDomPatcher } from "../Forms/domPatch.mjs";
import { renderRibbonCommandIcon } from "../AppShell/app/ribbonIcons.mjs";
import { createPlanarContextMenu } from "./planarContextMenu.mjs";

const palettes = new WeakMap();
const { patchChildren } = createDomPatcher({ keyAttribute: "data-planar-tool-key" });
const STYLE_ID = "icax-planar-tool-palette-style";
let menuSequence = 0;

function ensureStyles(document) {
  if (document.getElementById(STYLE_ID)) return;
  const style = document.createElement("style");style.id = STYLE_ID;
  style.textContent = `.icax-planar-tool-palette{display:flex;flex-direction:column;gap:4px;width:42px;max-height:100%;box-sizing:border-box;padding:4px;border:1px solid #b9c6cc;border-radius:4px;background:#f5f8f9;box-shadow:0 2px 8px #0002;overflow:auto;pointer-events:auto}.icax-planar-tool-group{display:grid;gap:3px}.icax-planar-tool-group+.icax-planar-tool-group{border-top:1px solid #cbd5da;padding-top:4px}.icax-planar-tool-group-label{font:10px/12px sans-serif;text-align:center;color:#627985;user-select:none}.icax-planar-tool-slot{position:relative;width:32px;height:34px}.icax-planar-tool-palette button{display:grid;place-items:center;width:32px;height:34px;box-sizing:border-box;border:1px solid transparent;border-radius:3px;background:transparent;color:#35525f;cursor:pointer}.icax-planar-tool-palette button:hover{background:#e6edf0;border-color:#b7c6ce}.icax-planar-tool-palette button[aria-pressed=true],.icax-planar-tool-slot[data-active-child=true]>button[data-planar-tool-menu-toggle]{background:#dceff1;border-color:#68a5ad;color:#196976}.icax-planar-tool-palette button:disabled{opacity:.4;cursor:default}.icax-planar-tool-palette button:focus-visible{outline:2px solid #348ca4;outline-offset:-2px}.icax-planar-tool-palette .command-icon,.icax-planar-tool-palette svg,.icax-planar-tool-menu .command-icon,.icax-planar-tool-menu svg{width:23px;height:23px;display:block}.icax-planar-tool-palette svg,.icax-planar-tool-menu svg{fill:none;stroke:currentColor;stroke-width:1.6;stroke-linecap:round;stroke-linejoin:round}.icax-planar-tool-palette svg .fill,.icax-planar-tool-menu svg .fill{fill:currentColor;stroke:none}.icax-planar-tool-palette button[data-planar-tool-menu-toggle]{position:absolute;right:0;bottom:0;width:13px;height:13px;padding:1px;border-color:#c6d2d7;background:#f5f8f9;border-radius:3px 0 3px 0}.icax-planar-tool-palette button[data-planar-tool-menu-toggle] svg{width:8px;height:8px}.icax-planar-tool-menu{z-index:1200;width:220px;padding:4px;border:1px solid #b9c6cc;border-radius:4px;background:#f8fafb;color:#35525f;box-shadow:0 3px 12px #0003;pointer-events:auto}.icax-planar-tool-menu .icax-planar-context-menu-item{display:flex;align-items:center;gap:7px;width:100%;min-height:34px;padding:5px 8px;border:1px solid transparent;border-radius:3px;background:transparent;color:inherit;text-align:left;font:inherit;cursor:pointer}.icax-planar-tool-menu .icax-planar-context-menu-item:hover{background:#e6edf0}.icax-planar-tool-menu .icax-planar-context-menu-item:focus-visible{outline:2px solid #348ca4;outline-offset:-2px}.icax-planar-tool-menu .icax-planar-context-menu-item:disabled{opacity:.45;cursor:default}.icax-planar-tool-menu .icax-planar-context-menu-check{width:12px;flex:0 0 12px}.icax-planar-tool-menu .icax-planar-context-menu-label{flex:1;white-space:normal}`;
  document.head.appendChild(style);
}

function visibleItems(tool) { return (tool?.menuItems ?? []).filter(item => item.visible !== false); }
function tooltip(tool) { return tool.disabled && tool.reason ? `${tool.label}：${tool.reason}` : tool.label; }

function closeMenu(state, restoreFocus = false) {
  if (!state?.menu) return false;
  const { controller, toggle, cleanup } = state.menu;
  state.menu = null;cleanup();controller.close();
  toggle.setAttribute("aria-expanded", "false");toggle.removeAttribute("aria-controls");
  if (restoreFocus && toggle.isConnected && !toggle.disabled) toggle.focus({ preventScroll: true });
  return true;
}

// A dismissal click on the viewport only dismisses the menu. Consume its
// matching release/click too; the drawing owner never receives a new point.
function consumeCanvasDismissal(window, down) {
  let timer;
  const cleanup = () => {
    window.clearTimeout(timer);
    window.removeEventListener("pointerdown", nextPress, true);
    for (const name of ["pointerup", "pointercancel", "click", "contextmenu"]) window.removeEventListener(name, consume, true);
  };
  const nextPress = event => { if (event !== down) cleanup(); };
  const consume = event => {
    if (event.type.startsWith("pointer") && event.pointerId !== down.pointerId) return;
    if ((event.type === "click" || event.type === "contextmenu") && event.button !== down.button) { cleanup();return; }
    event.preventDefault();event.stopImmediatePropagation();
    if (event.type !== "pointerup") cleanup();
  };
  window.addEventListener("pointerdown", nextPress, true);
  for (const name of ["pointerup", "pointercancel", "click", "contextmenu"]) window.addEventListener(name, consume, true);
  timer = window.setTimeout(cleanup, 800);
  down.preventDefault();down.stopImmediatePropagation();
}

function positionMenu(state) {
  const menu = state.menu;if (!menu) return;
  if (!state.element.isConnected || !menu.toggle.isConnected || !state.element.getClientRects().length) { closeMenu(state);return; }
  menu.controller.reposition();
  const window = state.element.ownerDocument.defaultView, rect = menu.toggle.closest(".icax-planar-tool-slot").getBoundingClientRect();
  const element = menu.controller.element, width = element.offsetWidth, height = element.offsetHeight, gap = 5;
  const left = rect.right + gap + width <= window.innerWidth ? rect.right + gap : rect.left - gap - width;
  const top = rect.top + height <= window.innerHeight ? rect.top : rect.bottom - height;
  element.style.left = `${Math.max(0, Math.min(left, window.innerWidth - width))}px`;
  element.style.top = `${Math.max(0, Math.min(top, window.innerHeight - height))}px`;
}

function synchronizeOpenMenu(state) {
  if (!state.menu) return;
  const items = visibleItems(state.tools.get(state.menu.id)), ids = items.map(item => item.id);
  if (JSON.stringify(ids) !== JSON.stringify(state.menu.ids)) { closeMenu(state);return; }
  for (const button of state.menu.controller.element.querySelectorAll("[data-planar-tool]")) {
    const tool = items.find(item => item.id === button.dataset.planarTool);if (!tool) continue;
    button.disabled = Boolean(tool.disabled);button.setAttribute("aria-disabled", String(button.disabled));
    button.setAttribute("aria-checked", String(Boolean(tool.active)));button.title = tooltip(tool);
    button.querySelector(".icax-planar-context-menu-check").textContent = tool.active ? "✓" : "";
  }
  positionMenu(state);
}

function openMenu(state, id) {
  const parent = state.tools.get(id), items = visibleItems(parent);
  if (!items.length || !items.some(item => !item.disabled)) return;
  if (state.menu?.id === id) { closeMenu(state, true);return; }
  closeMenu(state);
  const document = state.element.ownerDocument, window = document.defaultView;
  const toggle = [...state.element.querySelectorAll("[data-planar-tool-menu-toggle]")].find(button => button.dataset.planarToolMenuToggle === id);
  if (!toggle || toggle.disabled) return;
  const rect = toggle.getBoundingClientRect(), menuId = `icax-planar-tool-menu-${++menuSequence}`;
  const controller = createPlanarContextMenu({ mount: document.body, clientX: rect.right + 5, clientY: rect.top,
    label: parent.menuLabel ?? `${parent.label}工具`,
    items: items.map(item => ({ id: item.id, label: item.label, enabled: !item.disabled, checked: Boolean(item.active) })),
    rootAttributes: { class: "icax-planar-tool-menu", id: menuId, "data-planar-tool-menu": id },
    itemAttributes: item => ({ "data-planar-tool": item.id, "data-planar-tool-key": item.id }) });
  for (const button of controller.element.querySelectorAll("[data-planar-tool]")) {
    const item = items.find(item => item.id === button.dataset.planarTool);
    button.insertAdjacentHTML("afterbegin", renderRibbonCommandIcon(item.icon, item.fallback ?? item.label.slice(0, 1)));button.title = tooltip(item);
  }
  const outside = event => {
    if (controller.element.contains(event.target) || state.element.contains(event.target)) return;
    closeMenu(state);
    if (event.target.closest?.("canvas")) consumeCanvasDismissal(window, event);
  };
  const keydown = event => {
    if (event.key === "Escape") { event.preventDefault();event.stopImmediatePropagation();closeMenu(state, true); }
    else if (event.key === "Tab") closeMenu(state, true);
  };
  const reposition = event => { if (!controller.element.contains(event.target)) positionMenu(state); };
  const blur = () => closeMenu(state);
  const observer = new window.MutationObserver(() => {
    if (!state.element.isConnected || !toggle.isConnected || !state.element.getClientRects().length) closeMenu(state);
  });
  observer.observe(document.documentElement, { childList: true, subtree: true });
  window.addEventListener("pointerdown", outside, true);window.addEventListener("keydown", keydown, true);
  window.addEventListener("scroll", reposition, true);window.addEventListener("resize", reposition);window.addEventListener("blur", blur);
  const cleanup = () => {
    observer.disconnect();window.removeEventListener("pointerdown", outside, true);window.removeEventListener("keydown", keydown, true);
    window.removeEventListener("scroll", reposition, true);window.removeEventListener("resize", reposition);window.removeEventListener("blur", blur);
  };
  state.menu = { id, ids: items.map(item => item.id), controller, toggle, cleanup };
  toggle.setAttribute("aria-expanded", "true");toggle.setAttribute("aria-controls", menuId);
  controller.element.addEventListener("pointerdown", event => event.stopPropagation());
  controller.element.addEventListener("pointerup", event => event.stopPropagation());
  controller.element.addEventListener("contextmenu", event => { event.preventDefault();event.stopPropagation(); });
  controller.element.addEventListener("click", event => {
    const target = event.target.closest("[data-planar-tool]");if (!target || target.disabled) return;
    event.preventDefault();event.stopPropagation();const tool = target.dataset.planarTool;
    closeMenu(state);state.onCommand?.(tool);
  });
  synchronizeOpenMenu(state);
  (controller.element.querySelector('[aria-checked="true"]:not(:disabled)') ?? controller.element.querySelector("button:not(:disabled)"))?.focus({ preventScroll: true });
}

export function closePlanarToolPaletteMenu(host, { restoreFocus = false } = {}) {
  return closeMenu(palettes.get(host), restoreFocus);
}

export function disposePlanarToolPalette(host) {
  const state = palettes.get(host);if (!state) return;
  closeMenu(state);state.element.remove();palettes.delete(host);
}

// Tools and commands are supplied by the consumer; this component only owns
// the retained controls and their presentation, never a product or scene.
export function synchronizePlanarToolPalette(host, { tools = [], onCommand, label = "二维工具" } = {}) {
  if (!host) return;
  ensureStyles(host.ownerDocument);
  let state = palettes.get(host);
  if (!state) {
    const element = host.ownerDocument.createElement("div");
    element.className = "icax-planar-tool-palette";element.setAttribute("role", "toolbar");element.setAttribute("aria-orientation", "vertical");
    host.appendChild(element);state = { element, tools: new Map() };palettes.set(host, state);
    element.addEventListener("pointerdown", event => event.stopPropagation());
    element.addEventListener("click", event => {
      const toggle = event.target.closest("[data-planar-tool-menu-toggle]");
      if (toggle && !toggle.disabled) { event.preventDefault();event.stopPropagation();openMenu(state, toggle.dataset.planarToolMenuToggle);return; }
      const target = event.target.closest("[data-planar-tool]");
      if (!target || target.disabled) return;
      event.preventDefault();event.stopPropagation();closeMenu(state);state.onCommand?.(target.dataset.planarTool);
    });
    element.addEventListener("contextmenu", event => {
      const target = event.target.closest("[data-planar-tool-slot]");if (!target || !visibleItems(state.tools.get(target.dataset.planarToolSlot)).length) return;
      event.preventDefault();event.stopPropagation();openMenu(state, target.dataset.planarToolSlot);
    });
    element.addEventListener("keydown", event => {
      if (["Enter", " "].includes(event.key)) event.stopPropagation();
      const toggle = event.target.closest("[data-planar-tool-menu-toggle]");
      if (toggle && event.key === "ArrowDown") { event.preventDefault();event.stopPropagation();openMenu(state, toggle.dataset.planarToolMenuToggle); }
    });
  }
  state.onCommand = onCommand;state.element.setAttribute("aria-label", label);
  const rows = tools.filter(tool => tool.visible !== false);state.tools = new Map(rows.map(tool => [tool.id, tool]));
  const next = host.ownerDocument.createElement("div");
  let group, groupHost;
  for (const tool of rows) {
    if (!groupHost || tool.group !== group) {
      groupHost = next.ownerDocument.createElement("div");groupHost.className = "icax-planar-tool-group";
      groupHost.setAttribute("role", "group");groupHost.setAttribute("aria-label", tool.groupLabel ?? tool.group ?? label);
      groupHost.dataset.planarToolKey = `group:${tool.group ?? "default"}`;next.appendChild(groupHost);
      if (tool.groupLabel) {
        const caption = next.ownerDocument.createElement("span");caption.className = "icax-planar-tool-group-label";
        caption.dataset.planarToolKey = `group-label:${tool.group}`;caption.textContent = tool.groupLabel;groupHost.appendChild(caption);
      }
    }
    group = tool.group;
    const slot = next.ownerDocument.createElement("div");slot.className = "icax-planar-tool-slot";
    slot.dataset.planarToolKey = tool.id;slot.dataset.planarToolSlot = tool.id;
    const items = visibleItems(tool);slot.dataset.activeChild = String(items.some(item => item.active));
    const button = next.ownerDocument.createElement("button");button.type = "button";
    button.dataset.planarToolKey = `primary:${tool.id}`;button.dataset.planarTool = tool.id;
    button.title = tooltip(tool);
    button.setAttribute("aria-label", tool.label);button.setAttribute("aria-pressed", String(Boolean(tool.active)));
    button.disabled = Boolean(tool.disabled);button.innerHTML = renderRibbonCommandIcon(tool.icon, tool.fallback ?? tool.label.slice(0, 1));
    slot.appendChild(button);
    if (items.length) {
      const toggle = next.ownerDocument.createElement("button");toggle.type = "button";
      toggle.dataset.planarToolKey = `menu:${tool.id}`;toggle.dataset.planarToolMenuToggle = tool.id;
      toggle.setAttribute("aria-label", `展开${tool.menuLabel ?? `${tool.label}工具`}`);toggle.title = toggle.getAttribute("aria-label");
      toggle.setAttribute("aria-haspopup", "menu");toggle.setAttribute("aria-expanded", String(state.menu?.id === tool.id));
      if (state.menu?.id === tool.id) toggle.setAttribute("aria-controls", state.menu.controller.element.id);
      toggle.disabled = !items.some(item => !item.disabled);
      toggle.innerHTML = '<svg viewBox="0 0 10 10" aria-hidden="true"><path d="m3 1 4 4-4 4"/></svg>';slot.appendChild(toggle);
    }
    groupHost.appendChild(slot);
  }
  patchChildren(state.element, next);
  synchronizeOpenMenu(state);
}
