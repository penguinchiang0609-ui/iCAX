const bindings = new WeakMap();
const documentOwners = new WeakMap();
const dialogSelector = ".tube-designer-part-inspection-dialog";
const handleSelector = "[data-tube-inspection-resize]";
const dragSelector = "[data-tube-inspection-window-drag]";
const directions = new Set(["n", "ne", "e", "se", "s", "sw", "w", "nw"]);
const interactiveSelector = "button,input,select,textarea,a,[contenteditable]:not([contenteditable=false])";
const focusableSelector = "button,input,select,textarea,a[href],summary,[tabindex],[contenteditable]:not([contenteditable=false])";
const captureEvents = [
  "click", "dblclick", "auxclick", "contextmenu", "mousedown", "mouseup",
  "pointerdown", "pointermove", "pointerup", "pointercancel", "pointerover", "pointerout",
  "wheel", "keydown", "keyup", "keypress", "focus", "focusin",
  "beforeinput", "input", "change", "submit", "dragstart", "drop",
];

function visible(element) {
  if (!element?.isConnected || element.hidden || !element.getClientRects?.().length) return false;
  const style = element.ownerDocument?.defaultView?.getComputedStyle?.(element);
  return style?.display !== "none" && style?.visibility !== "hidden";
}

function scopeMatches(state) {
  return state.context.mount === state.mount
    && String(state.context.project?.projectId ?? "") === state.projectId
    && state.view.activeAreaId === state.areaId;
}

function tableRoots(state) {
  const selector = state.areaId === "view" ? ".tube-designer-product-parts-table"
    : state.areaId === "nesting" ? ".tube-designer-breakdown-body > .tube-designer-sheet-wrap" : "";
  return selector ? [...state.mount.querySelectorAll(selector)].filter(visible) : [];
}

function allowed(state, element) {
  return Boolean(element && (state.dialog?.contains(element)
    || tableRoots(state).some(root => root.contains(element))));
}

function eventElement(event) {
  const target = event.composedPath?.()[0] ?? event.target;
  return target?.nodeType === 1 ? target : target?.parentElement;
}

function block(event) {
  if (event.cancelable) event.preventDefault();
  event.stopImmediatePropagation();
}

function viewportLimits(state) {
  const width = Math.max(1, Number(state.win.innerWidth) || state.document.documentElement.clientWidth || 1);
  const height = Math.max(1, Number(state.win.innerHeight) || state.document.documentElement.clientHeight || 1);
  const marginX = Math.min(12, Math.max(0, (width - 1) / 2));
  const marginY = Math.min(12, Math.max(0, (height - 1) / 2));
  return { left: marginX, top: marginY, right: width - marginX, bottom: height - marginY,
    minWidth: Math.min(640, width - 2 * marginX), minHeight: Math.min(360, height - 2 * marginY) };
}

function clamp(value, low, high) { return Math.max(low, Math.min(value, high)); }

function place(state, bounds) {
  const limits = viewportLimits(state);
  const rect = state.dialog.getBoundingClientRect();
  const number = (key, fallback) => Number.isFinite(Number(bounds?.[key])) ? Number(bounds[key]) : fallback;
  const width = clamp(number("width", rect.width), limits.minWidth, limits.right - limits.left);
  const height = clamp(number("height", rect.height), limits.minHeight, limits.bottom - limits.top);
  const left = clamp(number("left", rect.left), limits.left, limits.right - width);
  const top = clamp(number("top", rect.top), limits.top, limits.bottom - height);
  const next = { left, top, width, height };
  Object.assign(state.dialog.style, { position: "absolute", left: `${left}px`, top: `${top}px`,
    width: `${width}px`, height: `${height}px`, right: "auto", bottom: "auto", margin: "0", transform: "none" });
  state.view.tubeDesignerPartInspectionWindowBounds = next;
  return next;
}

function releaseGesture(state) {
  const gesture = state.gesture;
  state.gesture = null;
  if (!gesture) return;
  delete gesture.dialog.dataset.tubeInspectionResizing;
  delete gesture.dialog.dataset.tubeInspectionDragging;
  try {
    if (gesture.handle.hasPointerCapture?.(gesture.pointerId)) gesture.handle.releasePointerCapture(gesture.pointerId);
  } catch { /* The host may already have removed the captured handle. */ }
}

function startGesture(state, event, target) {
  if (event.button !== 0 || event.isPrimary === false) return false;
  const resize = target?.closest(handleSelector);
  const drag = target?.closest(dragSelector);
  const direction = resize?.dataset.tubeInspectionResize;
  const handle = resize && directions.has(direction) ? resize : drag;
  if (!handle || !state.dialog.contains(handle)
      || (!resize && target.closest(interactiveSelector))) return false;
  block(event);
  releaseGesture(state);
  const bounds = place(state, state.view.tubeDesignerPartInspectionWindowBounds);
  state.gesture = { pointerId: event.pointerId, handle, dialog: state.dialog,
    direction: resize ? direction : "", x: event.clientX, y: event.clientY, bounds };
  if (resize) state.dialog.dataset.tubeInspectionResizing = direction;
  else state.dialog.dataset.tubeInspectionDragging = "true";
  try { handle.setPointerCapture?.(event.pointerId); } catch { /* Document capture also handles uncaptured gestures. */ }
  return true;
}

function moveGesture(state, event) {
  const gesture = state.gesture;
  if (!gesture || gesture.pointerId !== event.pointerId) return false;
  block(event);
  if (event.type !== "pointermove") { releaseGesture(state); return true; }
  const limits = viewportLimits(state), initial = gesture.bounds;
  const dx = event.clientX - gesture.x, dy = event.clientY - gesture.y;
  if (!gesture.direction) {
    place(state, { ...initial, left: initial.left + dx, top: initial.top + dy });
    return true;
  }
  let left = initial.left, top = initial.top, right = left + initial.width, bottom = top + initial.height;
  if (gesture.direction.includes("w")) left = clamp(left + dx, limits.left, right - limits.minWidth);
  if (gesture.direction.includes("e")) right = clamp(right + dx, left + limits.minWidth, limits.right);
  if (gesture.direction.includes("n")) top = clamp(top + dy, limits.top, bottom - limits.minHeight);
  if (gesture.direction.includes("s")) bottom = clamp(bottom + dy, top + limits.minHeight, limits.bottom);
  place(state, { left, top, width: right - left, height: bottom - top });
  return true;
}

function focusable(element) {
  return visible(element) && !element.disabled && !element.closest("[inert]") && element.tabIndex >= 0;
}

function focusAllowed(state) {
  if (state.redirectingFocus) return;
  const previous = state.lastAllowedFocus;
  const candidate = allowed(state, previous) && focusable(previous) ? previous
    : [...state.dialog.querySelectorAll(focusableSelector)].find(focusable) ?? state.dialog;
  state.redirectingFocus = true;
  try { candidate.focus?.({ preventScroll: true }); }
  finally { state.redirectingFocus = false; }
}

function keyboard(state, event, target) {
  if (event.type === "keydown" && event.key === "Tab") {
    const candidates = [...state.document.querySelectorAll(focusableSelector)]
      .filter(element => allowed(state, element) && focusable(element));
    const index = candidates.indexOf(state.document.activeElement);
    const next = event.shiftKey ? (index <= 0 ? candidates.length - 1 : index - 1)
      : (index + 1) % Math.max(1, candidates.length);
    block(event);
    (candidates[next] ?? state.dialog).focus?.({ preventScroll: true });
    return;
  }
  if (!allowed(state, target)) { block(event); focusAllowed(state); return; }
  if (event.key === "Escape") {
    block(event);
    if (event.type === "keydown") state.dialog.querySelector('[data-cam-action="tube-designer-close-part-inspection"]')?.click();
    return;
  }
  const editable = target?.closest("input:not([type=checkbox]):not([type=radio]),textarea,[contenteditable]:not([contenteditable=false])");
  if (event.ctrlKey || event.metaKey || event.altKey) {
    const textCommand = !event.altKey && (editable
      ? ["a", "c", "v", "x", "z", "y", "arrowleft", "arrowright", "home", "end", "backspace", "delete"].includes(String(event.key).toLowerCase())
      : ["a", "c"].includes(String(event.key).toLowerCase()));
    // Preserve browser text editing while preventing workbench shortcuts from
    // changing the product behind this inspection window.
    if (textCommand) event.stopImmediatePropagation();
    else block(event);
  } else if (!editable && !["Enter", " ", "ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Home", "End", "PageUp", "PageDown", "Tab"].includes(event.key)) {
    block(event);
  }
}

function restoreInert(state) {
  for (const [element, attribute] of state.inert) {
    if (attribute === null) element.removeAttribute("inert");
    else element.setAttribute("inert", attribute);
  }
  state.inert.clear();
}

function reconcileInert(state) {
  restoreInert(state);
  const roots = [state.dialog, ...tableRoots(state)];
  const protect = element => {
    if (roots.includes(element)) return;
    if (roots.some(root => element.contains(root))) {
      for (const child of element.children) protect(child);
    } else {
      state.inert.set(element, element.getAttribute("inert"));
      element.setAttribute("inert", "");
    }
  };
  if (state.document.body) protect(state.document.body);
}

function current(state) {
  if (state.disposed || documentOwners.get(state.document) !== state || !scopeMatches(state)
      || !state.view.tubeDesignerPartInspectionOpen || !state.mount.isConnected) {
    teardown(state, false); return false;
  }
  const dialog = state.mount.querySelector(dialogSelector);
  if (!visible(dialog)) { teardown(state, false); return false; }
  if (state.dialog !== dialog) {
    releaseGesture(state);
    state.dialog = dialog;
    if (!dialog.hasAttribute("tabindex")) dialog.setAttribute("tabindex", "-1");
    dialog.querySelector(dragSelector)?.setAttribute("data-no-window-drag", "");
    place(state, state.view.tubeDesignerPartInspectionWindowBounds);
  }
  return true;
}

function teardown(state, restoreFocus) {
  if (!state || state.disposed) return;
  const focusedDialog = state.dialog?.contains(state.document.activeElement);
  const sameScope = scopeMatches(state);
  state.disposed = true;
  releaseGesture(state);
  state.observer?.disconnect();
  for (const remove of state.listeners) remove();
  restoreInert(state);
  if (bindings.get(state.context) === state) bindings.delete(state.context);
  if (documentOwners.get(state.document) === state) documentOwners.delete(state.document);
  const previous = state.previousFocus;
  if (restoreFocus && focusedDialog && sameScope && visible(previous)
      && !previous.disabled && !previous.closest("[inert]") && !state.dialog?.contains(previous)) {
    previous.focus?.({ preventScroll: true });
  }
}

/** Keep inspection and its current parts list interactive; geometry stays mounted. */
export function attachPartInspectionWindow(context, view) {
  const mount = context?.mount;
  if (!mount?.querySelector || !mount.ownerDocument) return;
  let state = bindings.get(context);
  if (state && (state.mount !== mount || state.view !== view || !scopeMatches(state))) {
    teardown(state, false); state = null;
  }
  const dialog = mount.querySelector(dialogSelector);
  if (!view?.tubeDesignerPartInspectionOpen || !visible(dialog)) {
    teardown(state, true); return;
  }
  if (!state) {
    const document = mount.ownerDocument, win = document.defaultView;
    if (!win) return;
    teardown(documentOwners.get(document), false);
    state = { context, view, mount, document, win, dialog: null, disposed: false,
      projectId: String(context.project?.projectId ?? ""), areaId: view.activeAreaId,
      previousFocus: document.activeElement, lastAllowedFocus: null, inert: new Map(), listeners: [], gesture: null };
    bindings.set(context, state); documentOwners.set(document, state);
    const listen = (element, name, listener, options) => {
      element.addEventListener(name, listener, options);
      state.listeners.push(() => element.removeEventListener(name, listener, options));
    };
    const guard = event => {
      if (!current(state)) return;
      const target = eventElement(event);
      if (["pointermove", "pointerup", "pointercancel"].includes(event.type) && moveGesture(state, event)) return;
      if (event.type === "pointerdown" && startGesture(state, event, target)) return;
      if (["keydown", "keyup", "keypress"].includes(event.type)) { keyboard(state, event, target); return; }
      if (!allowed(state, target)) {
        block(event);
        if (event.type === "focus" || event.type === "focusin") focusAllowed(state);
      } else if (event.type === "focus" || event.type === "focusin") state.lastAllowedFocus = target;
    };
    for (const name of captureEvents) listen(document, name, guard, { capture: true, passive: false });
    listen(document, "lostpointercapture", event => {
      if (state.gesture?.pointerId === event.pointerId) releaseGesture(state);
    }, true);
    listen(win, "resize", () => {
      if (!current(state)) return;
      releaseGesture(state); place(state, view.tubeDesignerPartInspectionWindowBounds);
    });
    if (typeof win.MutationObserver === "function") {
      state.observer = new win.MutationObserver(() => {
        if (!current(state)) return;
        reconcileInert(state);
        if (!allowed(state, document.activeElement)) focusAllowed(state);
      });
      state.observer.observe(mount.ownerDocument.body, { childList: true, subtree: true });
    }
  }
  if (!current(state)) return;
  place(state, view.tubeDesignerPartInspectionWindowBounds);
  reconcileInert(state);
  if (!allowed(state, state.document.activeElement)) focusAllowed(state);
}

export function disposePartInspectionWindow(context) {
  if (context && (typeof context === "object" || typeof context === "function")) teardown(bindings.get(context), true);
}
