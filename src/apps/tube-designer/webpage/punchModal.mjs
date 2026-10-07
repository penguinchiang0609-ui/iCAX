const boundaries = new WeakMap();

function available(node) {
  return !node.disabled && !node.closest('[inert]') && node.tabIndex >= 0 && node.getClientRects().length > 0;
}

function restoreInert(entries) {
  for (const [node, value] of entries) {
    if (value === null) node.removeAttribute('inert'); else node.setAttribute('inert', value);
  }
}

function focusOpener(owner, mount) {
  const original = owner?.openerElement;
  const dataset = owner?.openerData;
  const opener = original?.isConnected ? original : dataset && [...mount.querySelectorAll('[data-cam-action],button')]
    .find(node => Object.entries(dataset).every(([key, value]) => node.dataset[key] === value));
  if (!opener || !available(opener)) return false;
  opener.focus({preventScroll: true});
  return true;
}

function syncAdvancedBoundary(boundary, mount) {
  const dialog = boundary.dialog.querySelector('[data-punch-profile-advanced-dialog]');
  const owner = boundary.owner.profileAdvancedEditor;
  const previous = boundary.advanced;
  if (previous && (!dialog || previous.owner !== owner || previous.dialog !== dialog)) {
    restoreInert(previous.inert);
    boundary.advanced = null;
    const active = mount.ownerDocument.activeElement;
    if (!dialog && (active === mount.ownerDocument.body || previous.dialog.contains(active))) {
      if (!focusOpener(previous.owner, mount)) boundary.pendingAdvancedOpener = previous.owner;
    }
  }
  if (!dialog || !owner) return;
  if (!boundary.advanced) boundary.advanced = {dialog, owner, inert: new Map(), needsInitialFocus: true};
  // Only the advanced editor is interactive while it is open. Keep the batch
  // tables and their canvas mounted and restore their prior inert state on close.
  let branch = dialog.closest('.punch-batch-profile-advanced-backdrop');
  while (branch?.parentElement && branch !== boundary.dialog) {
    for (const sibling of branch.parentElement.children) {
      if (sibling === branch) continue;
      if (!boundary.advanced.inert.has(sibling)) boundary.advanced.inert.set(sibling, sibling.getAttribute('inert'));
      sibling.setAttribute('inert', '');
    }
    branch = branch.parentElement;
  }
}

// The batch wizard remains a modal over the existing nesting workbench. Only
// its siblings become inert; no scene, editor or scroll container is replaced.
export function attachPunchModalBoundary(view, mount) {
  const dialog = mount?.querySelector?.('.tube-designer-punch-batch-dialog');
  const owner = view.tubeDesignerPunchBatch;
  let boundary = boundaries.get(mount);
  if (boundary && (!dialog || boundary.owner !== owner || boundary.dialog !== dialog)) {
    boundary.abort.abort();
    if (boundary.advanced) restoreInert(boundary.advanced.inert);
    restoreInert(boundary.inert);
    boundaries.delete(mount);
    const doc = mount.ownerDocument;
    if (!dialog && (doc.activeElement === doc.body || boundary.dialog.contains(doc.activeElement))) {
      focusOpener(boundary.owner, mount);
    }
    boundary = null;
  }
  if (!dialog || !owner) return;
  if (!boundary) {
    const abort = new AbortController();
    boundary = {owner, dialog, inert: new Map(), abort, needsInitialFocus: true};
    boundaries.set(mount, boundary);
    const doc = mount.ownerDocument;
    doc.addEventListener('keydown', event => {
      if (boundaries.get(mount) !== boundary || !dialog.isConnected || event.defaultPrevented) return;
      const activeDialog = boundary.advanced?.dialog ?? dialog;
      if (event.key === 'Escape') {
        if (view.pending) return;
        event.preventDefault();
        activeDialog.querySelector('[data-cam-action$="-cancel"]')?.click();
      } else if (event.key === 'Tab') {
        const controls = [...activeDialog.querySelectorAll('button,input,select,textarea,a[href],[tabindex]')].filter(available);
        if (!controls.length) {event.preventDefault(); return;}
        const index = controls.indexOf(doc.activeElement);
        if (index < 0 || !event.shiftKey && index === controls.length - 1 || event.shiftKey && index === 0) {
          event.preventDefault();
          controls[event.shiftKey ? controls.length - 1 : 0].focus({preventScroll: true});
        }
      }
    }, {signal: abort.signal});
  }
  syncAdvancedBoundary(boundary, mount);
  if (boundary.pendingAdvancedOpener && !view.pending) {
    // Applying may disable the entry button while the native preview runs.
    // Restore it on that render only if the user has not focused elsewhere.
    if (mount.ownerDocument.activeElement === mount.ownerDocument.body) focusOpener(boundary.pendingAdvancedOpener, mount);
    delete boundary.pendingAdvancedOpener;
  }
  const allowed = node => node.matches?.('.tube-designer-punch-backdrop,.tube-designer-punch-parameter-backdrop,[data-tube-designer-operation-wait]');
  let branch = dialog.closest('.tube-designer-punch-backdrop');
  while (branch?.parentElement && branch !== mount) {
    for (const sibling of branch.parentElement.children) {
      if (sibling === branch || allowed(sibling)) continue;
      if (!boundary.inert.has(sibling)) boundary.inert.set(sibling, sibling.getAttribute('inert'));
      sibling.setAttribute('inert', '');
    }
    branch = branch.parentElement;
  }
  const activeBoundary = boundary.advanced ?? boundary;
  if (activeBoundary.needsInitialFocus && !view.pending) {
    if (!activeBoundary.dialog.contains(mount.ownerDocument.activeElement)) {
      const first = [...activeBoundary.dialog.querySelectorAll('input,select,button')].find(available);
      first?.focus({preventScroll: true});
    }
    activeBoundary.needsInitialFocus = false;
  }
}
