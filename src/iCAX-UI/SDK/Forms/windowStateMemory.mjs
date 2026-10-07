// User defaults belong to a window and its semantic fields, never to DOM positions.
const MAX_WINDOWS = 128;
const MAX_FIELDS = 256;
const MAX_KEY_LENGTH = 1024;
const MAX_FIELD_BYTES = 65536;
const MAX_STORAGE_BYTES = 524288;
const CONTROL_SELECTOR = 'input,select,textarea';
const BINDING_ATTRIBUTES = ['data-parameter-key', 'data-parameter-name', 'data-field', 'data-binding', 'data-bind'];

function defaultStorage() { try { return globalThis.localStorage; } catch { return null; } }
function key(value) { return typeof value === 'string' && value.trim() && value.length <= MAX_KEY_LENGTH ? value : null; }
function plain(value) { return value && (Object.getPrototypeOf(value) === Object.prototype || Object.getPrototypeOf(value) === null); }
function validValue(value, depth = 0) {
  if (depth > 12) return false;
  if (value === null || typeof value === 'boolean' || typeof value === 'string') return true;
  if (typeof value === 'number') return Number.isFinite(value);
  if (Array.isArray(value)) return value.length <= 2048 && value.every(item => validValue(item, depth + 1));
  return plain(value) && Object.keys(value).length <= MAX_FIELDS && Object.entries(value).every(([name, item]) => key(name) && validValue(item, depth + 1));
}
function clone(value) { return JSON.parse(JSON.stringify(value)); }
function serializable(value) {
  try { if (!validValue(value)) return null; const json = JSON.stringify(value); return json.length <= MAX_FIELD_BYTES ? JSON.parse(json) : null; }
  catch { return null; }
}
function emptyState() { return { version: 1, windows: [] }; }
function decode(raw) {
  if (typeof raw !== 'string' || raw.length > MAX_STORAGE_BYTES) return emptyState();
  try {
    const state = JSON.parse(raw);
    if (state?.version !== 1 || !Array.isArray(state.windows)) return emptyState();
    const windows = [], seen = new Set();
    for (const row of state.windows.slice(-MAX_WINDOWS)) {
      if (!key(row?.key) || seen.has(row.key) || !plain(row.fields)) continue;
      const fields = Object.create(null);
      for (const [name, value] of Object.entries(row.fields).slice(-MAX_FIELDS)) {
        if (key(name) && validValue(value) && JSON.stringify(value).length <= MAX_FIELD_BYTES) fields[name] = value;
      }
      windows.push({ key: row.key, fields }); seen.add(row.key);
    }
    return { version: 1, windows };
  } catch { return emptyState(); }
}

/** Durable, bounded JSON defaults. Unavailable storage falls back to this instance's memory. */
export function createWindowStateMemory(options = {}) {
  const namespace = key(options.namespace);
  if (!namespace) throw new TypeError('Window state memory requires a stable namespace.');
  const storage = Object.hasOwn(options, 'storage') ? options.storage : defaultStorage();
  const storageKey = `icax.window-state:${encodeURIComponent(namespace)}`;
  let cached = emptyState(), storageFailed = !storage || typeof storage.getItem !== 'function' || typeof storage.setItem !== 'function';
  function readState() {
    if (!storageFailed && storage?.getItem) {
      try { cached = decode(storage.getItem(storageKey)); } catch { storageFailed = true; }
    }
    return cached;
  }
  function persist(state) {
    while (state.windows.length > MAX_WINDOWS) state.windows.shift();
    let json = JSON.stringify(state);
    while (json.length > MAX_STORAGE_BYTES && state.windows.length) { state.windows.shift(); json = JSON.stringify(state); }
    cached = state;
    if (!storageFailed && storage?.setItem) {
      try { storage.setItem(storageKey, json); } catch { storageFailed = true; }
    }
  }
  const memory = {
    namespace,
    read(windowKey, fieldKey, defaultValue) {
      if (!key(windowKey) || !key(fieldKey)) return defaultValue;
      const fields = readState().windows.find(row => row.key === windowKey)?.fields;
      return fields && Object.hasOwn(fields, fieldKey) ? clone(fields[fieldKey]) : defaultValue;
    },
    readFields(windowKey) {
      if (!key(windowKey)) return {};
      return clone(readState().windows.find(row => row.key === windowKey)?.fields ?? {});
    },
    write(windowKey, fieldKey, value) { return key(fieldKey) ? memory.writeFields(windowKey, { [fieldKey]: value }) : false; },
    writeFields(windowKey, fields) {
      if (!key(windowKey) || !plain(fields)) return false;
      const entries = [];
      for (const [name, value] of Object.entries(fields)) {
        if (!key(name) || !validValue(value)) continue;
        const copy = serializable(value);
        if (copy !== null || value === null) entries.push([name, copy]);
      }
      if (!entries.length) return false;
      const state = readState(), previous = state.windows.find(row => row.key === windowKey);
      const next = Object.assign(Object.create(null), previous?.fields);
      for (const [name, value] of entries) { delete next[name]; next[name] = value; }
      const names = Object.keys(next);
      for (const name of names.slice(0, Math.max(0, names.length - MAX_FIELDS))) delete next[name];
      state.windows = state.windows.filter(row => row.key !== windowKey);
      state.windows.push({ key: windowKey, fields: next }); persist(state); return true;
    },
    clear(windowKey) {
      if (!key(windowKey)) return false;
      const state = readState(); state.windows = state.windows.filter(row => row.key !== windowKey); persist(state); return true;
    },
  };
  return memory;
}

function attribute(element, name) { return element?.getAttribute?.(name) ?? null; }
function ignored(element) { return !!element?.closest?.('[data-window-state-ignore]'); }
function visible(element, document) {
  if (!element?.isConnected || element.hidden || ignored(element)) return false;
  if (element.closest?.('[hidden],[aria-hidden="true"]')) return false;
  const dialog = element.closest?.('dialog');
  if (dialog && !dialog.open) return false;
  try {
    const style = document.defaultView?.getComputedStyle?.(element);
    if (style?.display === 'none' || style?.visibility === 'hidden') return false;
    if (element.getClientRects && !element.getClientRects().length) return false;
  } catch { return false; }
  return true;
}

/** Override these descriptors to supply application-owned model and context identities. */
export function describeWindowElement(element) {
  if (ignored(element)) return null;
  const windowKey = attribute(element, 'data-window-state-key') || attribute(element, 'id') || attribute(element, 'name')
    || attribute(element, 'aria-label') || attribute(element, 'aria-labelledby');
  return key(windowKey) ? { key: windowKey, scope: attribute(element, 'data-window-state-scope') || '',
    owner: attribute(element, 'data-window-state-owner') || '' } : null;
}
export function describeControlElement(element) {
  if (ignored(element)) return null;
  let fieldKey = attribute(element, 'data-window-state-field') || attribute(element, 'data-window-state-key')
    || attribute(element, 'id') || attribute(element, 'name');
  if (!fieldKey) for (const name of BINDING_ATTRIBUTES) {
    const value = attribute(element, name); if (value) { fieldKey = `${name}:${value}`; break; }
  }
  if (!fieldKey) for (const item of element.attributes ?? []) {
    if (/^data-.+-(?:field|parameter|key|bind|name)$/.test(item.name) && item.value) {
      fieldKey = `${item.name}:${item.value}`; break;
    }
  }
  fieldKey ||= attribute(element, 'aria-label');
  if (!key(fieldKey)) return null;
  // Named radio controls form one value; explicitly valued checkboxes are independent options.
  if (String(element.type).toLowerCase() === 'checkbox' && !attribute(element, 'data-window-state-field')
    && !attribute(element, 'id') && attribute(element, 'name') && attribute(element, 'value')) {
    fieldKey = JSON.stringify([fieldKey, attribute(element, 'value')]);
  }
  return { key: fieldKey };
}
export function controlValue(control) {
  const type = String(control.type).toLowerCase();
  if (type === 'checkbox') return !!control.checked;
  if (type === 'radio') return control.checked ? control.value : undefined;
  if (control.tagName?.toLowerCase() === 'select' && control.multiple) return [...control.selectedOptions].map(option => option.value);
  if ((type === 'number' || type === 'range') && control.value !== '' && Number.isFinite(control.valueAsNumber)) return control.valueAsNumber;
  return String(control.value ?? '');
}
function applyValue(control, value) {
  const type = String(control.type).toLowerCase(), tag = control.tagName?.toLowerCase();
  if (type === 'checkbox') { if (typeof value !== 'boolean') return false; control.checked = value; return true; }
  if (type === 'radio') {
    if (typeof value !== 'string' || control.value !== value) return false;
    control.checked = true; return true;
  }
  if (tag === 'select') {
    const options = [...control.options].filter(option => !option.disabled && !option.parentElement?.disabled);
    if (control.multiple) {
      if (!Array.isArray(value) || !value.every(item => typeof item === 'string' && options.some(option => option.value === item))) return false;
      for (const option of control.options) option.selected = value.includes(option.value);
    } else {
      if (typeof value !== 'string' || !options.some(option => option.value === value)) return false;
      control.value = value;
    }
    return true;
  }
  if (!['string', 'number'].includes(typeof value)) return false;
  control.value = String(value); return true;
}
function eligible(control, document) {
  // :disabled includes inherited fieldset/optgroup restrictions; the DOM
  // .disabled property describes only an attribute on the control itself.
  return visible(control, document) && !control.disabled && !control.matches?.(':disabled')
    && !control.closest?.('[inert]') && !control.readOnly
    && !['password', 'file', 'hidden', 'button', 'submit', 'reset', 'image'].includes(String(control.type).toLowerCase());
}

/**
 * Delegated form memory with one hydration per semantic field per logical open.
 * describeWindow -> {key,scope,owner}; owner cancels sessions and is not persisted.
 * describeControl -> {key,read?,apply?,restore?,dispatch?}; null excludes a control.
 * restoreControl runs after applying the value, sequentially, and can await model updates.
 * With an adapter, call context.dispatch() if DOM events are needed; isCurrent() and signal
 * protect pending work when a user edits, changes owners, replaces a node, or closes a window.
 */
export function installWindowStateMemory(document, options = {}) {
  const memory = options.memory ?? createWindowStateMemory(options);
  const dialogSelector = options.dialogSelector ?? 'dialog,[role="dialog"],.new-project-dialog';
  const describeWindow = options.describeWindow ?? describeWindowElement;
  const describeControl = options.describeControl ?? describeControlElement;
  const sessions = new Map(), restoredEvents = new WeakSet(), restoringControls = new WeakSet();
  let disposed = false, scheduled = false;
  const notify = (name, ...args) => { try { options[name]?.(...args); } catch { /* Diagnostics cannot interrupt editing. */ } };
  function windowDescriptor(element) {
    if (!visible(element, document)) return null;
    let descriptor;
    try { descriptor = describeWindow(element); } catch (error) { notify('onError', error); return null; }
    if (typeof descriptor === 'string') descriptor = { key: descriptor };
    if (!descriptor || !key(descriptor.key)) return null;
    const scope = String(descriptor.scope ?? ''), windowKey = JSON.stringify([descriptor.key, scope]);
    if (!key(windowKey)) return null;
    return { ...descriptor, scope, windowKey, owner: descriptor.owner ?? '' };
  }
  function controlDescriptions(element, descriptor, includeIneligible = false) {
    const rows = [];
    if (descriptor.controlSelector === false) return rows;
    for (const control of element.querySelectorAll?.(CONTROL_SELECTOR) ?? []) {
      if (control.closest?.(dialogSelector) !== element || ignored(control)
        || (!includeIneligible && !eligible(control, document))) continue;
      const region = control.closest?.('[data-window-state-controls]');
      const ownRegion = region && region !== element && region.closest?.(dialogSelector) === element;
      const selector = ownRegion ? attribute(region, 'data-window-state-controls')
        : descriptor.controlSelector !== undefined ? descriptor.controlSelector
          : attribute(element, 'data-window-state-controls') !== null ? attribute(element, 'data-window-state-controls')
            : options.controlSelector !== undefined ? options.controlSelector
              : '[data-window-state-field],[data-window-state-key]';
      if (selector === false || typeof selector !== 'string' || !selector.trim()) continue;
      // Each page explicitly declares its remembered controls. A custom key
      // adapter cannot broaden that policy into incidental domain fields.
      try { if (!control.matches?.(selector)) continue; }
      catch (error) { notify('onError', error); continue; }
      let description;
      try { description = describeControl(control, descriptor); } catch (error) { notify('onError', error); continue; }
      if (typeof description === 'string') description = { key: description };
      if (!description || description.ignore || !key(description.key)) continue;
      rows.push({ control, description });
    }
    return rows;
  }
  function controls(element, descriptor) {
    const rows = controlDescriptions(element, descriptor), groups = new Map();
    for (const row of rows) {
      const { description } = row;
      const group = groups.get(description.key) ?? []; group.push(row); groups.set(description.key, group);
    }
    // An ambiguous field never receives another control's value. Radio groups are intentional.
    return rows.filter(row => {
      const group = groups.get(row.description.key);
      return group.length === 1 || (row.control.name && group.every(item =>
        String(item.control.type).toLowerCase() === 'radio' && item.control.name === row.control.name));
    });
  }
  function retireAbsentFields(session) {
    // Hidden, inherited-disabled and inert controls still own their semantic
    // identities during loading. A profile/type switch can remove an identity
    // altogether; its next appearance must synchronize the model through the
    // normal adapter rather than receive a silent transfer from the old node.
    const present = new Set(controlDescriptions(session.element, session.descriptor, true).map(row => row.description.key));
    const tracked = new Set([...session.restored, ...session.edited, ...session.nodes.keys()]);
    for (const fieldKey of tracked) if (!present.has(fieldKey)) {
      session.restored.delete(fieldKey); session.edited.delete(fieldKey); session.nodes.delete(fieldKey);
      session.revisions.set(fieldKey, (session.revisions.get(fieldKey) ?? 0) + 1);
    }
  }
  function baseIdentity(element) {
    const descriptor = describeWindowElement(element);
    return descriptor ? JSON.stringify([descriptor.key, descriptor.scope ?? '']) : null;
  }
  function transferUserDrafts(session) {
    for (const { control, description } of controls(session.element, session.descriptor)) {
      const fieldKey = description.key;
      // A rerender is not another opening. Transfer only edits from this live session,
      // without firing model events, touching focus, or resetting an active text selection.
      if (description.transfer !== false && session.edited.has(fieldKey) && session.nodes.get(fieldKey) !== control
        && document.activeElement !== control && Object.hasOwn(session.fields, fieldKey)) {
        try { applyValue(control, clone(session.fields[fieldKey])); } catch (error) { notify('onError', error); }
      }
      session.nodes.set(fieldKey, control);
    }
  }
  function current(session) {
    if (disposed || session.controller.signal.aborted || sessions.get(session.windowKey) !== session
      || !visible(session.element, document)) return false;
    const descriptor = windowDescriptor(session.element);
    return !descriptor || (descriptor.windowKey === session.windowKey && descriptor.owner === session.owner);
  }
  function dispatch(control) {
    const Event = document.defaultView?.Event ?? globalThis.Event;
    for (const type of ['input', 'change']) {
      const event = new Event(type, { bubbles: true }); restoredEvents.add(event); control.dispatchEvent(event);
    }
  }
  async function hydrate(session) {
    if (session.running || !current(session)) return;
    session.running = true;
    try {
      let row;
      while (current(session) && !session.suspended && (row = controls(session.element, session.descriptor).find(item =>
        item.description.restore !== false && !session.restored.has(item.description.key) && !session.edited.has(item.description.key)
        && (String(item.control.type).toLowerCase() !== 'radio' || session.fields[item.description.key] === item.control.value)))) {
        const { control, description } = row, fieldKey = description.key;
        session.restored.add(fieldKey);
        if (!Object.hasOwn(session.fields, fieldKey)) continue;
        const revision = session.revisions.get(fieldKey) ?? 0;
        const isCurrent = () => current(session) && control.isConnected && control.closest?.(dialogSelector) === session.element
          && revision === (session.revisions.get(fieldKey) ?? 0) && !session.edited.has(fieldKey);
        if (!isCurrent()) continue;
        const value = clone(session.fields[fieldKey]);
        const previousValue = controlValue(control);
        let applied;
        try {
          restoringControls.add(control);
          applied = description.apply ? description.apply(control, value) !== false : applyValue(control, value);
        }
        catch (error) { notify('onError', error); continue; }
        finally { restoringControls.delete(control); }
        if (!applied) continue;
        const context = { window: session.element, windowKey: session.windowKey, descriptor: session.descriptor, previousValue,
          fieldKey, signal: session.controller.signal, isCurrent,
          dispatch: () => { if (isCurrent() && description.dispatch !== false) dispatch(control); } };
        try {
          if (options.restoreControl) {
            let pending;
            restoringControls.add(control);
            try { pending = options.restoreControl(control, value, context); }
            finally { restoringControls.delete(control); }
            await pending;
          }
          else context.dispatch();
          if (isCurrent()) notify('onRestore', control, value, context);
        } catch (error) { if (current(session)) notify('onError', error); }
      }
    } finally { session.running = false; }
  }
  function scan() {
    scheduled = false; if (disposed) return;
    const active = new Set();
    for (const element of document.querySelectorAll(dialogSelector)) {
      const descriptor = windowDescriptor(element);
      if (!descriptor) {
        // A model adapter can postpone hydration while this visible dialog loads its descriptor.
        // Suspension is not a close/reopen and must retain edits and hydrated-field identities.
        if (visible(element, document)) for (const session of sessions.values()) if (session.element === element
          || (!session.element.isConnected && session.baseIdentity && session.baseIdentity === baseIdentity(element))) {
          session.element = element; session.suspended = true; active.add(session.windowKey);
        }
        continue;
      }
      // Simultaneously open windows with the same identity are ambiguous; do not transfer owners.
      if (active.has(descriptor.windowKey)) continue;
      active.add(descriptor.windowKey);
      let session = sessions.get(descriptor.windowKey);
      if (session && session.owner !== descriptor.owner) { session.controller.abort(); sessions.delete(descriptor.windowKey); session = null; }
      if (!session) {
        session = { windowKey: descriptor.windowKey, owner: descriptor.owner, element, descriptor, baseIdentity: baseIdentity(element),
          controller: new AbortController(), fields: memory.readFields(descriptor.windowKey),
          restored: new Set(), edited: new Set(), revisions: new Map(), nodes: new Map(), running: false };
        sessions.set(descriptor.windowKey, session);
      } else { session.element = element; session.descriptor = descriptor; }
      session.suspended = descriptor.ready === false || descriptor.suspended === true;
      retireAbsentFields(session);
      transferUserDrafts(session);
      if (!session.running && !session.suspended) session.promise = hydrate(session);
    }
    for (const [windowKey, session] of sessions) if (!active.has(windowKey)) { session.controller.abort(); sessions.delete(windowKey); }
  }
  function schedule() { if (!scheduled && !disposed) { scheduled = true; queueMicrotask(scan); } }
  function closed(event) {
    for (const [windowKey, session] of sessions) if (session.element === event.target) {
      session.controller.abort(); sessions.delete(windowKey);
    }
    schedule();
  }
  function record(event) {
    if (restoredEvents.has(event) || restoringControls.has(event.target) || disposed) return;
    const control = event.target;
    if (!control?.matches?.(CONTROL_SELECTOR) || !eligible(control, document)) return;
    const element = control.closest(dialogSelector), descriptor = element && windowDescriptor(element); if (!descriptor) return;
    let session = sessions.get(descriptor.windowKey);
    if (!session || session.owner !== descriptor.owner) { scan(); session = sessions.get(descriptor.windowKey); }
    const row = controls(element, descriptor).find(item => item.control === control); if (!row) return;
    const fieldKey = row.description.key;
    let value;
    try { value = row.description.read ? row.description.read(control) : controlValue(control); } catch (error) { notify('onError', error); return; }
    if (value === undefined || !memory.write(descriptor.windowKey, fieldKey, value)) return;
    session?.edited.add(fieldKey); session?.revisions.set(fieldKey, (session.revisions.get(fieldKey) ?? 0) + 1);
    if (session) { session.fields[fieldKey] = clone(value); session.nodes.set(fieldKey, control); }
    notify('onCapture', control, value, { window: element, windowKey: descriptor.windowKey, fieldKey, descriptor });
  }
  const MutationObserver = document.defaultView?.MutationObserver ?? globalThis.MutationObserver;
  const observer = MutationObserver ? new MutationObserver(schedule) : null;
  observer?.observe(document.documentElement ?? document, { subtree: true, childList: true, attributes: true });
  document.addEventListener('input', record, true); document.addEventListener('change', record, true);
  document.addEventListener('close', closed, true); document.addEventListener('cancel', schedule, true);
  const controller = {
    memory,
    refresh: scan,
    async restoreWindow(element) {
      scan(); const descriptor = windowDescriptor(element), session = descriptor && sessions.get(descriptor.windowKey);
      if (session) await session.promise;
    },
    captureWindow(element) {
      const descriptor = windowDescriptor(element); if (!descriptor) return false;
      for (const { control } of controls(element, descriptor)) record({ target: control });
      return true;
    },
    dispose() {
      disposed = true; observer?.disconnect();
      document.removeEventListener('input', record, true); document.removeEventListener('change', record, true);
      document.removeEventListener('close', closed, true); document.removeEventListener('cancel', schedule, true);
      for (const session of sessions.values()) session.controller.abort(); sessions.clear();
    },
  };
  scan(); return controller;
}
