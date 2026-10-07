const panelIds = ["parts", "inspector", "results"];
const zones = ["left", "right", "bottom"];
const labels = { parts: "零件清单", inspector: "当前选择", results: "排样结果" };
const defaults = { parts: "left", inspector: "right", results: "bottom" };
const storagePrefix = "tube-designer.nesting-dock.v1:";

function normalize(value) {
  const positions = Object.fromEntries(panelIds.map(id => [id,
    zones.includes(value?.positions?.[id]) ? value.positions[id] : defaults[id]]));
  const active = Object.fromEntries(zones.map(zone => {
    const members = panelIds.filter(id => positions[id] === zone);
    return [zone, members.includes(value?.active?.[zone]) ? value.active[zone] : members[0] ?? ""];
  }));
  return { positions, active };
}

function storageKey(context) {
  const projectId = String(context?.project?.projectId ?? "").trim();
  return projectId ? `${storagePrefix}${projectId}` : "";
}

export function getNestingDockLayout(view, context = null) {
  if (!view.tubeDesignerNestingDockLayout) {
    let saved = null;
    const key = storageKey(context);
    if (key) {
      try { saved = JSON.parse(globalThis.localStorage?.getItem(key) ?? "null"); } catch { /* Storage may be disabled. */ }
    }
    view.tubeDesignerNestingDockLayout = normalize(saved);
  }
  return view.tubeDesignerNestingDockLayout;
}

function persist(context, layout) {
  const key = storageKey(context);
  if (!key) return;
  try { globalThis.localStorage?.setItem(key, JSON.stringify(layout)); } catch { /* Keep the in-memory layout. */ }
}

export function nestingDockZoneCounts(view, context = null) {
  const { positions } = getNestingDockLayout(view, context);
  return Object.fromEntries(zones.map(zone => [zone,
    panelIds.filter(id => positions[id] === zone).length]));
}

function identity(node) {
  if (node.id) return `id:${node.id}`;
  const attrs = [...node.attributes].filter(attr => attr.name.startsWith("data-")
    || ["name", "type", "aria-label"].includes(attr.name));
  return `${node.tagName}:${JSON.stringify(attrs.map(attr => [attr.name, attr.value]).sort())}`;
}

function locate(panel, saved) {
  if (saved.node.isConnected && panel.contains(saved.node)) return saved.node;
  const candidates = [panel, ...panel.querySelectorAll("*")]
    .filter(node => identity(node) === saved.key);
  if (candidates.length === 1) return candidates[0];
  if (saved.className) {
    const byClass = [panel, ...panel.querySelectorAll("*")]
      .filter(node => node.className === saved.className);
    if (byClass.length === 1) return byClass[0];
  }
  return null;
}

// Capture immediately before workbench replacement, including whichever dock
// currently contains a panel. The normal left/right restoration cannot follow
// a panel that has moved to another zone.
export function captureNestingDockInteraction(mount, view) {
  if (view.activeAreaId !== "nesting") return;
  const active = mount?.ownerDocument?.activeElement;
  const snapshots = panelIds.map(id => {
    const panel = mount?.querySelector?.(`[data-tube-designer-dock-panel="${id}"]`);
    if (!panel) return null;
    const scrolls = [panel, ...panel.querySelectorAll("*")]
      .filter(node => node.scrollTop || node.scrollLeft)
      .map(node => ({ node, key: identity(node), className: node.className,
        top: node.scrollTop, left: node.scrollLeft }));
    const focused = panel.contains(active) ? { node: active, key: identity(active),
      className: active.className, value: active.value,
      start: active.selectionStart, end: active.selectionEnd,
      direction: active.selectionDirection } : null;
    return { id, scrolls, focused };
  }).filter(Boolean);
  view.tubeDesignerRestoreDockInteraction = () => {
    if (view.activeAreaId !== "nesting") return;
    for (const saved of snapshots) {
      const panel = mount.querySelector(`[data-tube-designer-dock-panel="${saved.id}"]`);
      if (!panel) continue;
      const focus = saved.focused && locate(panel, saved.focused);
      if (focus && !focus.disabled) {
        if (focus.tagName === "TEXTAREA" || focus.tagName === "INPUT"
          && !["checkbox", "radio", "button", "submit", "range", "file"].includes(focus.type))
          focus.value = saved.focused.value;
        focus.focus({ preventScroll: true });
        if (saved.focused.start != null && typeof focus.setSelectionRange === "function") {
          try { focus.setSelectionRange(saved.focused.start, saved.focused.end, saved.focused.direction); }
          catch { /* Inputs without text selection are still focused. */ }
        }
      }
      for (const scroll of saved.scrolls) {
        const node = locate(panel, scroll);
        if (node) { node.scrollTop = scroll.top; node.scrollLeft = scroll.left; }
      }
    }
  };
}

export function restoreNestingDockInteraction(view) {
  const restore = view.tubeDesignerRestoreDockInteraction;
  view.tubeDesignerRestoreDockInteraction = null;
  restore?.();
}

function setActive(layout, zone, id, hosts) {
  layout.active[zone] = id;
  for (const button of hosts[zone].querySelectorAll("[data-tube-designer-dock-tab]")) {
    const selected = button.dataset.tubeDesignerDockTab === id;
    button.setAttribute("aria-selected", String(selected));
    button.tabIndex = selected ? 0 : -1;
  }
  for (const panel of hosts[zone].querySelectorAll("[data-tube-designer-dock-panel]"))
    panel.hidden = panel.dataset.tubeDesignerDockPanel !== id;
}

function dragOverlay(workbench) {
  const overlay = workbench.ownerDocument.createElement("div");
  overlay.className = "tube-designer-dock-overlay";
  overlay.setAttribute("aria-hidden", "true");
  overlay.innerHTML = zones.map(zone =>
    `<div class="tube-designer-dock-target" data-tube-designer-dock-target="${zone}">${
      { left: "停靠左侧", right: "停靠右侧", bottom: "停靠下方" }[zone]}</div>`).join("");
  workbench.append(overlay);
  return overlay;
}

function dropZone(overlay, x, y) {
  for (const node of overlay.querySelectorAll("[data-tube-designer-dock-target]")) {
    const rect = node.getBoundingClientRect();
    if (x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom)
      return node.dataset.tubeDesignerDockTarget;
  }
  return "";
}

function bindTab(button, id, zone, context, view, mount, ops, hosts, layout) {
  let drag = null;
  let suppressClick = false;
  button.addEventListener("click", event => {
    if (suppressClick) { suppressClick = false; event.preventDefault(); return; }
    setActive(layout, zone, id, hosts);
    persist(context, layout);
  });
  button.addEventListener("keydown", event => {
    if (!["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) return;
    const tabs = [...hosts[zone].querySelectorAll("[data-tube-designer-dock-tab]")];
    const index = tabs.indexOf(button);
    const next = event.key === "Home" ? 0 : event.key === "End" ? tabs.length - 1
      : (index + (event.key === "ArrowRight" ? 1 : -1) + tabs.length) % tabs.length;
    tabs[next]?.focus({ preventScroll: true });
    tabs[next]?.click();
    event.preventDefault();
  });
  button.addEventListener("pointerdown", event => {
    if (event.button !== 0) return;
    drag = { x: event.clientX, y: event.clientY, overlay: null, candidate: "" };
    button.setPointerCapture?.(event.pointerId);
  });
  button.addEventListener("pointermove", event => {
    if (!drag) return;
    if (!drag.overlay && Math.hypot(event.clientX - drag.x, event.clientY - drag.y) < 6) return;
    drag.overlay ??= dragOverlay(mount.querySelector(".cam-workbench"));
    drag.candidate = dropZone(drag.overlay, event.clientX, event.clientY);
    for (const target of drag.overlay.querySelectorAll("[data-tube-designer-dock-target]"))
      target.classList.toggle("active", target.dataset.tubeDesignerDockTarget === drag.candidate);
  });
  const finish = (event, canceled = false) => {
    if (!drag) return;
    const { overlay, candidate } = drag;
    drag = null;
    overlay?.remove();
    if (!overlay) return;
    suppressClick = !canceled;
    if (canceled || !candidate) return;
    layout.positions[id] = candidate;
    layout.active[candidate] = id;
    if (layout.active[zone] === id && zone !== candidate)
      layout.active[zone] = panelIds.find(other => other !== id && layout.positions[other] === zone) ?? "";
    persist(context, layout);
    attachNestingDocking(context, view, mount, ops);
    event.preventDefault();
  };
  button.addEventListener("pointerup", finish);
  button.addEventListener("pointercancel", event => finish(event, true));
}

export function attachNestingDocking(context, view, mount, ops) {
  if (view.activeAreaId !== "nesting") return;
  const workbench = mount.querySelector(".cam-workbench");
  const hosts = {
    left: workbench?.querySelector(".cam-context-pane"),
    right: workbench?.querySelector(".cam-info-pane"),
    bottom: workbench?.querySelector(".tube-designer-dock-bottom-host"),
  };
  if (Object.values(hosts).some(host => !host)) return;
  const panels = {
    parts: workbench.querySelector('[data-tube-designer-dock-panel="parts"]')
      ?? hosts.left.querySelector(":scope > .tube-designer-cutting-parts"),
    inspector: workbench.querySelector('[data-tube-designer-dock-panel="inspector"]')
      ?? hosts.right.querySelector(":scope > .tube-designer-cutting-inspector"),
    results: workbench.querySelector('[data-tube-designer-dock-panel="results"]')
      ?? hosts.bottom.querySelector(":scope > .tube-designer-nesting-bottom"),
  };
  if (Object.values(panels).some(panel => !panel)) return;
  const layout = getNestingDockLayout(view, context);
  const scrollers = Object.values(panels).flatMap(panel => [panel, ...panel.querySelectorAll("*")]
    .filter(node => node.scrollTop || node.scrollLeft)
    .map(node => ({ node, top: node.scrollTop, left: node.scrollLeft })));
  for (const panel of Object.values(panels)) panel.remove();
  const counts = nestingDockZoneCounts(view, context);
  for (const zone of zones)
    workbench.classList.toggle(`tube-designer-dock-${zone}-empty`, !counts[zone]);
  workbench.style.gridTemplateColumns = `${counts.left ? "var(--cam-left-width,300px) 5px" : "0px 0px"} minmax(0,1fr) ${counts.right ? "5px var(--cam-right-width,320px)" : "0px 0px"}`;
  workbench.style.gridTemplateRows = `auto minmax(0,1fr) ${counts.bottom ? "5px var(--cam-bottom-height,156px)" : "0px 0px"}`;
  const resetZone = zones.find(zone => panelIds.some(id => layout.positions[id] === zone));
  for (const zone of zones) {
    const host = hosts[zone];
    host.classList.add("tube-designer-dock-zone");
    host.dataset.tubeDesignerDockZone = zone;
    const tablist = mount.ownerDocument.createElement("div");
    tablist.className = "tube-designer-dock-tabs";
    tablist.setAttribute("role", "tablist");
    tablist.setAttribute("aria-label", `${{ left: "左侧", right: "右侧", bottom: "下方" }[zone]}停靠面板`);
    const body = mount.ownerDocument.createElement("div");
    body.className = "tube-designer-dock-body";
    const members = panelIds.filter(id => layout.positions[id] === zone);
    for (const id of members) {
      const button = mount.ownerDocument.createElement("button");
      button.type = "button";
      button.className = "tube-designer-dock-tab";
      button.dataset.tubeDesignerDockTab = id;
      button.setAttribute("role", "tab");
      button.setAttribute("data-no-window-drag", "");
      button.setAttribute("title", `拖动“${labels[id]}”并停靠到左、右或下方`);
      button.textContent = labels[id];
      tablist.append(button);
      const panel = panels[id];
      panel.classList.add("tube-designer-dock-panel");
      panel.dataset.tubeDesignerDockPanel = id;
      panel.setAttribute("role", "tabpanel");
      body.append(panel);
      bindTab(button, id, zone, context, view, mount, ops, hosts, layout);
    }
    if (members.length && zone === resetZone) {
      const reset = mount.ownerDocument.createElement("button");
      reset.type = "button";
      reset.className = "tube-designer-dock-reset";
      reset.textContent = "还原布局";
      reset.title = "恢复下料工作区的默认面板位置";
      reset.setAttribute("data-no-window-drag", "");
      reset.addEventListener("click", () => {
        view.tubeDesignerNestingDockLayout = normalize(null);
        persist(context, view.tubeDesignerNestingDockLayout);
        attachNestingDocking(context, view, mount, ops);
      });
      tablist.append(reset);
    }
    host.replaceChildren(tablist, body);
    if (members.length) setActive(layout, zone, layout.active[zone], hosts);
  }
  for (const { node, top, left } of scrollers) {
    node.scrollTop = top;
    node.scrollLeft = left;
  }
}
