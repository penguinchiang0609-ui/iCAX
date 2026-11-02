// Positions belong to this project's active interaction, never to scene data.
const activeBindings = new WeakMap();
const current = (mount, binding) => mount.isConnected && !binding.view?.disposed
  && binding.context.isCurrentProject?.() !== false
  && (binding.area === undefined || binding.area === binding.view?.activeAreaId);

export function floatingDialogStyle(view, id) {
  const p = view.fiveDialogPositions?.[id];
  return p ? `style="left:${p.left}px;top:${p.top}px;transform:none"` : '';
}

function constrain(dialog, left, top) {
  const window = dialog.ownerDocument.defaultView, size = dialog.getBoundingClientRect();
  const inset = 12;
  return { left: Math.max(inset, Math.min(left, window.innerWidth - size.width - inset)),
    top: Math.max(inset, Math.min(top, window.innerHeight - size.height - inset)) };
}
function place(view, dialog, left, top) {
  const p = constrain(dialog, left, top);
  (view.fiveDialogPositions ??= {})[dialog.dataset.fiveFloating] = p;
  Object.assign(dialog.style, { left: `${p.left}px`, top: `${p.top}px`, transform: 'none' });
}
export function attachFloatingDialogs(mount, binding) {
  disposeFloatingDialogs(binding);
  const window = mount.ownerDocument.defaultView;
  let drag;
  const finish = () => {
    if (!drag) return;
    const previous = drag; drag = null;
    previous.title.classList.remove('is-dragging');
    if (previous.title.hasPointerCapture(previous.pointerId)) previous.title.releasePointerCapture(previous.pointerId);
  };
  const down = event => {
    const title = event.target.closest?.('[data-five-dialog-drag]'), dialog = title?.closest('[data-five-floating]');
    if (!dialog || event.button !== 0 || event.target.closest('button,input,select,textarea,a')
      || !current(mount, binding)) return;
    event.preventDefault(); event.stopPropagation();
    const rect = dialog.getBoundingClientRect();
    drag = { title, dialog, view: binding.view, pointerId: event.pointerId,
      startX: event.clientX, startY: event.clientY, left: rect.left, top: rect.top };
    title.setPointerCapture(event.pointerId); title.classList.add('is-dragging');
  };
  const move = event => {
    if (!drag || event.pointerId !== drag.pointerId) return;
    if (binding.view !== drag.view || !drag.dialog.isConnected || !current(mount, binding)) { finish(); return; }
    event.preventDefault(); event.stopPropagation();
    place(drag.view, drag.dialog, drag.left + event.clientX - drag.startX, drag.top + event.clientY - drag.startY);
  };
  const up = event => {
    if (drag && event.pointerId === drag.pointerId) { event.stopPropagation(); finish(); }
  };
  const sync = () => syncFloatingDialogs(mount, binding), resize = new window.ResizeObserver(sync);
  mount.addEventListener('pointerdown', down);
  mount.addEventListener('pointermove', move);
  for (const name of ['pointerup', 'pointercancel', 'lostpointercapture']) mount.addEventListener(name, up);
  window.addEventListener('resize', sync);
  binding.floatingDialogs = { document: mount.ownerDocument, resize, finish, observed: [], dispose() {
    finish(); resize.disconnect(); window.removeEventListener('resize', sync);
    mount.removeEventListener('pointerdown', down); mount.removeEventListener('pointermove', move);
    for (const name of ['pointerup', 'pointercancel', 'lostpointercapture']) mount.removeEventListener(name, up);
  } };
}
export function syncFloatingDialogs(mount, binding) {
  if (!current(mount, binding)) { binding.floatingDialogs?.finish(); return; }
  const document = mount.ownerDocument, previous = activeBindings.get(document);
  if (previous !== binding) disposeFloatingDialogs(previous);
  // Cached area mounts re-enter through sync, while only the active one owns
  // window listeners. Closing a project cannot leave a hidden area's handler.
  if (!binding.floatingDialogs) attachFloatingDialogs(mount, binding);
  activeBindings.set(document, binding);
  const state = binding.floatingDialogs;
  const dialogs = [...mount.querySelectorAll('[data-five-floating]')], ids = new Set(dialogs.map(d => d.dataset.fiveFloating));
  for (const id of Object.keys(binding.view.fiveDialogPositions ?? {})) if (!ids.has(id)) delete binding.view.fiveDialogPositions[id];
  const observed = [...new Set([mount.ownerDocument.documentElement, ...dialogs.flatMap(d => [d.parentElement, d])])];
  if (observed.length !== state.observed.length || observed.some((d, i) => d !== state.observed[i])) {
    state.resize.disconnect(); observed.forEach(d => state.resize.observe(d)); state.observed = observed;
  }
  for (const dialog of dialogs) {
    const p = binding.view.fiveDialogPositions?.[dialog.dataset.fiveFloating];
    // Start beside the scene controls, then keep the position in client-window
    // coordinates. A fixed dialog can cross the viewport's clipped side panes
    // without being reparented or losing its inputs during a panel patch.
    const origin = dialog.closest('[data-five-overlay]')?.getBoundingClientRect();
    place(binding.view, dialog, p?.left ?? (origin?.left ?? 0) + 16, p?.top ?? (origin?.top ?? 0) + 64);
  }
}
export function disposeFloatingDialogs(binding) {
  const document = binding?.floatingDialogs?.document;
  binding?.floatingDialogs?.dispose();
  if (document && activeBindings.get(document) === binding) activeBindings.delete(document);
  if (binding) binding.floatingDialogs = null;
}
