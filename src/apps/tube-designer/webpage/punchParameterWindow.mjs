// Parameter edits are a single transaction, but their window must not own the
// scene. Moving this window is UI-only and never queues geometry work.
const bindings = new WeakMap();
const margin = 16;

export function attachPunchParameterWindow(mount, state) {
  if (!mount?.querySelector) return;
  const dialog = mount.querySelector("[data-punch-parameter-dialog]");
  const editor = state?.parameterEditor;
  const previous = bindings.get(mount);
  if (previous && previous.dialog === dialog && previous.editor === editor) {
    previous.keepReachable();
    return;
  }
  previous?.abort.abort();
  bindings.delete(mount);
  if (!dialog || !editor) return;

  const win = dialog.ownerDocument.defaultView;
  const abort = new AbortController();
  const options = { signal: abort.signal };
  const handle = dialog.querySelector("[data-punch-parameter-drag]");
  let drag = null;
  const place = (left, top) => {
    const box = dialog.getBoundingClientRect();
    left = Math.max(margin, Math.min(left, win.innerWidth - box.width - margin));
    top = Math.max(margin, Math.min(top, win.innerHeight - box.height - margin));
    editor.windowPosition = { left, top };
    Object.assign(dialog.style, { position: "fixed", left: left + "px", top: top + "px", margin: "0px" });
  };
  const keepReachable = () => {
    if (dialog.isConnected && editor.windowPosition)
      place(editor.windowPosition.left, editor.windowPosition.top);
  };
  const stop = event => {
    if (!drag || event.pointerId !== drag.id) return;
    drag = null;
    delete handle.dataset.dragging;
    if (handle.hasPointerCapture?.(event.pointerId)) handle.releasePointerCapture(event.pointerId);
  };
  handle?.addEventListener("pointerdown", event => {
    if (event.button !== 0 || event.isPrimary === false ||
        event.target.closest("button,input,select,textarea,a")) return;
    // Keep a partially typed value intact when the user is only moving the
    // window; normal input blur/preview still runs when they enter the scene.
    event.preventDefault();
    const box = dialog.getBoundingClientRect();
    drag = { id: event.pointerId, x: event.clientX - box.left, y: event.clientY - box.top };
    handle.dataset.dragging = "true";
    handle.setPointerCapture(event.pointerId);
  }, options);
  handle?.addEventListener("pointermove", event => {
    if (drag?.id === event.pointerId) place(event.clientX - drag.x, event.clientY - drag.y);
  }, options);
  for (const name of ["pointerup", "pointercancel", "lostpointercapture"])
    handle?.addEventListener(name, stop, options);

  // The native viewport consumes pointer gestures. Explicitly leave the input
  // before that happens, so a later preview response cannot steal focus back.
  mount.addEventListener("pointerdown", event => {
    if (!event.target.closest?.("[data-tube-designer-punch-viewport]")) return;
    delete state.uiFocus;
    const active = dialog.ownerDocument.activeElement;
    if (dialog.contains(active)) active.blur();
  }, { ...options, capture: true });
  mount.addEventListener("focusin", event => {
    if (!dialog.contains(event.target)) delete state.uiFocus;
  }, { ...options, capture: true });
  win.addEventListener("resize", keepReachable, options);
  const observer = new win.ResizeObserver(keepReachable);
  observer.observe(dialog);
  abort.signal.addEventListener("abort", () => {
    observer.disconnect();
    if (drag && handle.hasPointerCapture?.(drag.id)) handle.releasePointerCapture(drag.id);
    delete handle?.dataset.dragging;
  }, { once: true });
  bindings.set(mount, { dialog, editor, abort, keepReachable });
  keepReachable();
}
