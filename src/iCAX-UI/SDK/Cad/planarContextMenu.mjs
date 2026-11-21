function applyAttributes(element, attributes) {
  for (const [name, value] of Object.entries(attributes ?? {})) {
    if (value === undefined || value === null) continue;
    if (name === "class") element.classList.add(...String(value).split(/\s+/).filter(Boolean));
    else element.setAttribute(name, String(value));
  }
}

export function createPlanarContextMenu({ mount, boundsElement, clientX, clientY, label, items,
  rootAttributes = {}, itemAttributes = () => ({}) }) {
  const document = mount.ownerDocument, window = document.defaultView;
  const element = document.createElement("div");
  element.className = "ribbon-command-menu icax-planar-context-menu";
  element.setAttribute("role", "menu");element.setAttribute("aria-label", label ?? "绘图操作");
  element.dataset.planarContextMenu = "";applyAttributes(element, rootAttributes);
  Object.assign(element.style, { position: "fixed", boxSizing: "border-box", overflow: "visible" });
  const rootItems = makeItemsHost(element);
  const branches = new WeakMap();
  let openSubmenu = null, closed = false;

  function makeItemsHost(menu) {
    const host = document.createElement("div");host.className = "icax-planar-context-items";
    Object.assign(host.style, { minWidth: "0", overflowY: "auto", overflowX: "hidden" });
    menu.appendChild(host);return host;
  }
  function bounds() {
    const rect = boundsElement?.getBoundingClientRect();
    const left = Math.max(0, rect?.left ?? 0), top = Math.max(0, rect?.top ?? 0);
    return { left, top, right: Math.max(left, Math.min(window.innerWidth, rect?.right ?? window.innerWidth)),
      bottom: Math.max(top, Math.min(window.innerHeight, rect?.bottom ?? window.innerHeight)) };
  }
  function limitSize(menu, host, limit) {
    const width = Math.max(0, limit.right - limit.left), height = Math.max(0, limit.bottom - limit.top);
    Object.assign(menu.style, { minWidth: "0", maxWidth: `${width}px`, maxHeight: `${height}px` });
    const style = window.getComputedStyle(menu), chrome = ["paddingTop", "paddingBottom", "borderTopWidth", "borderBottomWidth"]
      .reduce((sum, key) => sum + (Number.parseFloat(style[key]) || 0), 0);
    host.style.maxHeight = `${Math.max(0, height - chrome)}px`;
  }
  function clamp(value, low, high) { return Math.max(low, Math.min(value, Math.max(low, high))); }
  function positionSubmenu(limit) {
    if (!openSubmenu) return;
    const { menu, host, button } = openSubmenu;
    limitSize(menu, host, limit);
    const rect = button.getBoundingClientRect(), width = menu.offsetWidth, height = menu.offsetHeight;
    const left = rect.right + width <= limit.right ? rect.right : rect.left - width;
    const top = rect.top + height <= limit.bottom ? rect.top : rect.bottom - height;
    menu.style.left = `${clamp(left, limit.left, limit.right - width)}px`;
    menu.style.top = `${clamp(top, limit.top, limit.bottom - height)}px`;
  }
  function reposition() {
    if (closed || !element.isConnected) return;
    const limit = bounds();limitSize(element, rootItems, limit);
    const width = element.offsetWidth, height = element.offsetHeight;
    const left = clientX + width <= limit.right ? clientX : clientX - width;
    const top = clientY + height <= limit.bottom ? clientY : clientY - height;
    element.style.left = `${clamp(left, limit.left, limit.right - width)}px`;
    element.style.top = `${clamp(top, limit.top, limit.bottom - height)}px`;
    positionSubmenu(limit);
  }
  function closeSubmenu() {
    if (!openSubmenu) return false;
    const { menu, button } = openSubmenu, restoreFocus = menu.contains(document.activeElement);
    openSubmenu = null;button.setAttribute("aria-expanded", "false");menu.remove();
    if (restoreFocus && !closed && button.isConnected) button.focus({ preventScroll: true });
    return true;
  }
  function showSubmenu(item, button) {
    if (closed || item.enabled === false || !item.children?.length) return;
    if (openSubmenu?.button === button) return;
    closeSubmenu();
    const menu = document.createElement("div");
    menu.className = "ribbon-command-menu icax-planar-context-submenu";
    menu.setAttribute("role", "menu");menu.setAttribute("aria-label", item.label);
    menu.dataset.planarContextSubmenu = item.id;
    Object.assign(menu.style, { position: "fixed", boxSizing: "border-box", overflow: "visible" });
    const host = makeItemsHost(menu);
    if (item.submenuHint) {
      const hint = document.createElement("div");hint.className = "icax-planar-context-submenu-hint";
      hint.textContent = item.submenuHint;host.appendChild(hint);
    }
    for (const child of item.children) host.appendChild(makeItem(child, false));
    // Keep the submenu inside the root for the owner's outside-click boundary,
    // but outside its scrolling items so it cannot be clipped by that scroller.
    element.appendChild(menu);openSubmenu = { menu, host, button };
    button.setAttribute("aria-expanded", "true");positionSubmenu(bounds());
  }
  function makeItem(item, topLevel) {
    const button = document.createElement("button"), branch = topLevel && Boolean(item.children?.length);
    button.type = "button";button.className = "ribbon-command-menu-item icax-planar-context-menu-item";
    button.disabled = item.enabled === false;
    button.setAttribute("role", item.checked === undefined || branch ? "menuitem" : "menuitemradio");
    if (item.checked !== undefined && !branch) button.setAttribute("aria-checked", String(Boolean(item.checked)));
    if (button.disabled) button.setAttribute("aria-disabled", "true");
    const check = document.createElement("span");check.className = "icax-planar-context-menu-check";
    check.setAttribute("aria-hidden", "true");check.textContent = item.checked ? "✓" : "";button.appendChild(check);
    const text = document.createElement("span");text.className = "icax-planar-context-menu-label";
    text.textContent = item.label;button.appendChild(text);
    if (item.hint) {
      const hint = document.createElement("small");hint.className = "icax-planar-context-menu-hint";
      hint.textContent = item.hint;button.appendChild(hint);button.title = item.hint;
    }
    if (branch) {
      branches.set(button, item);
      button.classList.add("has-submenu");button.dataset.planarSubmenuToggle = item.id;
      button.setAttribute("aria-haspopup", "menu");button.setAttribute("aria-expanded", "false");
      const arrow = document.createElement("span");arrow.className = "icax-planar-context-menu-arrow";
      arrow.setAttribute("aria-hidden", "true");arrow.textContent = "›";button.appendChild(arrow);
      button.addEventListener("pointerenter", () => showSubmenu(item, button));
      button.addEventListener("click", event => {
        event.preventDefault();event.stopPropagation();
        showSubmenu(item, button);
      });
    } else {
      button.dataset.planarMenuAction = item.id;applyAttributes(button, itemAttributes(item));
    }
    return button;
  }
  for (const item of items) rootItems.appendChild(makeItem(item, true));
  rootItems.addEventListener("scroll", () => positionSubmenu(bounds()));
  element.addEventListener("keydown", event => {
    const button = event.target.closest?.(".icax-planar-context-menu-item");if (!button) return;
    if (event.key === "Enter" || event.key === " ") { event.stopPropagation();return; }
    if (event.key === "ArrowRight" && button.dataset.planarSubmenuToggle) {
      event.preventDefault();event.stopPropagation();showSubmenu(branches.get(button), button);
      openSubmenu?.host.querySelector("button:not(:disabled)")?.focus({ preventScroll: true });return;
    }
    if (event.key === "ArrowLeft" && openSubmenu?.menu.contains(button)) {
      event.preventDefault();event.stopPropagation();closeSubmenu();return;
    }
    if (!["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key)) return;
    event.preventDefault();event.stopPropagation();
    const host = button.parentElement, buttons = [...host.querySelectorAll("button:not(:disabled)")];
    const index = buttons.indexOf(button), next = event.key === "Home" ? 0 : event.key === "End" ? buttons.length - 1
      : (index + (event.key === "ArrowDown" ? 1 : -1) + buttons.length) % buttons.length;
    buttons[next]?.focus({ preventScroll: true });
  });
  mount.appendChild(element);reposition();
  return { element, closeSubmenu, hasOpenSubmenu: () => Boolean(openSubmenu), reposition,
    close() { if (closed) return;closed = true;closeSubmenu();element.remove(); } };
}
