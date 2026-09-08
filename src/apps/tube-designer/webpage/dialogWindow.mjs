const DIALOG_TITLE_SELECTOR = [
  ".tube-designer-dialog-header",
  ".tube-nesting-settings-header",
  ".td-draw-title",
  ".tube-section-sketch-title",
  ".tube-sketch-save-choice > header",
  ".new-project-dialog > header",
  "[data-tube-designer-window-drag]",
].join(",");

const INTERACTIVE_SELECTOR = "button,input,select,textarea,a,[data-no-window-drag]";
const EXISTING_DRAG_SELECTOR = "[data-tube-designer-punch-window-drag],[data-punch-parameter-drag]";
const DIALOG_SELECTOR = "dialog,[role=\"dialog\"],.new-project-dialog";

const installedDocuments = new WeakSet();

function dialogKey(dialog) {
  return dialog.getAttribute("aria-labelledby")
    || dialog.getAttribute("aria-label")
    || [...dialog.classList].sort().join(".");
}

function getDialog(header) {
  return header.closest(DIALOG_SELECTOR);
}

function setFixedPosition(dialog, left, top, state) {
  const win = dialog.ownerDocument?.defaultView;
  if (!win) return;
  const rect = dialog.getBoundingClientRect();
  const margin = 12;
  const maxLeft = Math.max(margin, win.innerWidth - rect.width - margin);
  const maxTop = Math.max(margin, win.innerHeight - rect.height - margin);
  const nextLeft = Math.max(margin, Math.min(Number(left) || 0, maxLeft));
  const nextTop = Math.max(margin, Math.min(Number(top) || 0, maxTop));
  Object.assign(dialog.style, {
    position: "fixed",
    left: `${nextLeft}px`,
    top: `${nextTop}px`,
    margin: "0",
  });
  state.positions.set(dialogKey(dialog), { left: nextLeft, top: nextTop });
}

function restorePosition(dialog, state) {
  const position = state.positions.get(dialogKey(dialog));
  if (!position || !dialog.isConnected) return;
  setFixedPosition(dialog, position.left, position.top, state);
}

function restoreVisibleDialogs(document, state) {
  for (const header of document.querySelectorAll(DIALOG_TITLE_SELECTOR)) {
    const dialog = getDialog(header);
    if (dialog) restorePosition(dialog, state);
  }
}

/**
 * Install one delegated drag controller for every popup title bar in the
 * document. Delegation keeps the behavior alive when the workbench rerenders
 * a dialog during an edit, while the position map keeps the moved window in
 * place across those incremental renders.
 */
export function installTubeDesignerDialogDragging(documentRef = globalThis.document) {
  const document = documentRef;
  if (!document || installedDocuments.has(document)) return;
  installedDocuments.add(document);

  const state = { active: null, positions: new Map() };
  const win = document.defaultView;

  const stop = (event) => {
    const active = state.active;
    if (!active || event.pointerId !== active.pointerId) return;
    state.active = null;
    delete active.header.dataset.tdWindowDragging;
    if (active.header.hasPointerCapture?.(event.pointerId)) {
      active.header.releasePointerCapture(event.pointerId);
    }
  };

  document.addEventListener("pointerdown", (event) => {
    if (event.button !== 0 || event.isPrimary === false) return;
    const target = event.target && typeof event.target.closest === "function" ? event.target : null;
    const header = target?.closest(DIALOG_TITLE_SELECTOR);
    if (!header || target.closest(INTERACTIVE_SELECTOR) || header.matches(EXISTING_DRAG_SELECTOR)) return;
    const dialog = getDialog(header);
    if (!dialog) return;

    const rect = dialog.getBoundingClientRect();
    event.preventDefault();
    Object.assign(dialog.style, {
      position: "fixed",
      left: `${rect.left}px`,
      top: `${rect.top}px`,
      margin: "0",
    });
    state.active = {
      pointerId: event.pointerId,
      dialog,
      header,
      offsetX: event.clientX - rect.left,
      offsetY: event.clientY - rect.top,
    };
    header.dataset.tdWindowDragging = "true";
    header.setPointerCapture?.(event.pointerId);
  }, true);

  document.addEventListener("pointermove", (event) => {
    const active = state.active;
    if (!active || event.pointerId !== active.pointerId) return;
    event.preventDefault();
    setFixedPosition(active.dialog, event.clientX - active.offsetX, event.clientY - active.offsetY, state);
  }, true);

  for (const name of ["pointerup", "pointercancel", "lostpointercapture"]) {
    document.addEventListener(name, stop, true);
  }

  win?.addEventListener("resize", () => {
    const active = state.active;
    if (active) {
      const rect = active.dialog.getBoundingClientRect();
      setFixedPosition(active.dialog, rect.left, rect.top, state);
    }
    restoreVisibleDialogs(document, state);
  });

  // Dialogs are commonly replaced by innerHTML during live preview updates.
  // Restore a previously dragged position when its replacement is inserted.
  const observer = typeof win?.MutationObserver === "function"
    ? new win.MutationObserver(() => restoreVisibleDialogs(document, state))
    : null;
  observer?.observe(document.documentElement, { childList: true, subtree: true });
}
