import assert from 'node:assert/strict';
import test from 'node:test';
import { createWindowStateMemory, describeControlElement, installWindowStateMemory } from '../../iCAX-UI/SDK/Forms/windowStateMemory.mjs';

function storage() {
  const rows = new Map();
  return { rows, getItem: name => rows.get(name) ?? null, setItem: (name, value) => rows.set(name, value) };
}

test('typed defaults survive a new controller and isolate apps, windows and fields', () => {
  const local = storage(), first = createWindowStateMemory({ namespace: 'tube', storage: local });
  first.writeFields('import', { directory: 'D:\\加工件', recursive: true, quantity: 3, kinds: ['step', 'igs'], options: { fit: true }, empty: null });
  first.write('export', 'directory', 'D:\\成品');
  const reopened = createWindowStateMemory({ namespace: 'tube', storage: local });
  assert.deepEqual(reopened.readFields('import'), { directory: 'D:\\加工件', recursive: true, quantity: 3, kinds: ['step', 'igs'], options: { fit: true }, empty: null });
  assert.equal(reopened.read('export', 'directory', ''), 'D:\\成品');
  assert.equal(reopened.read('import', 'missing', 9), 9);
  assert.equal(createWindowStateMemory({ namespace: 'other', storage: local }).read('import', 'directory', ''), '');
  const returned = reopened.read('import', 'options', {}); returned.fit = false;
  assert.equal(reopened.read('import', 'options', {}).fit, true);
  reopened.clear('import'); assert.deepEqual(reopened.readFields('import'), {});
  assert.equal(reopened.read('export', 'directory', ''), 'D:\\成品');
});

test('unavailable and quota limited storage preserve the current session without throwing', () => {
  for (const local of [null, { getItem() { throw Error('security'); } }, { getItem() { return null; } },
    { getItem() { return null; }, setItem() { throw Error('quota'); } }]) {
    const memory = createWindowStateMemory({ namespace: 'tube', storage: local });
    assert.equal(memory.write('add', 'width', 1200), true);
    assert.equal(memory.read('add', 'width', 0), 1200);
    memory.clear('add'); assert.equal(memory.read('add', 'width', 0), 0);
  }
});

test('malformed and unsupported data do not enter controls', () => {
  const local = storage(), name = 'icax.window-state:tube';
  for (const raw of ['{', '{"version":2,"windows":[]}', '{"version":1,"windows":{}}', 'x'.repeat(524289)]) {
    local.setItem(name, raw);
    assert.deepEqual(createWindowStateMemory({ namespace: 'tube', storage: local }).readFields('add'), {});
  }
  const memory = createWindowStateMemory({ namespace: 'tube', storage: local });
  const circular = {}; circular.self = circular;
  for (const value of [undefined, NaN, Infinity, new Date(), circular, () => 2, 'x'.repeat(65537)]) assert.equal(memory.write('add', 'bad', value), false);
  assert.equal(memory.write('add', undefined, 2), false);
  assert.equal(memory.write('add', 'good', 0), true);
  assert.equal(memory.read('add', 'good', -1), 0);
});

test('the record stays bounded while retaining the most recently edited defaults', () => {
  const local = storage(), memory = createWindowStateMemory({ namespace: 'tube', storage: local });
  for (let index = 0; index < 140; index++) memory.write(`window-${index}`, 'field', index);
  const raw = JSON.parse([...local.rows.values()][0]);
  assert.equal(raw.windows.length, 128); assert.equal(memory.read('window-0', 'field', null), null);
  assert.equal(memory.read('window-139', 'field', null), 139);
  for (let index = 0; index < 270; index++) memory.write('window-139', `field-${index}`, index);
  assert.equal(Object.keys(memory.readFields('window-139')).length, 256);
  assert.equal(memory.read('window-139', 'field-269', null), 269);
  assert.ok([...local.rows.values()][0].length <= 524288);
});

test('control identities never depend on position and option checkboxes cannot collide', () => {
  const element = (attributes, type = 'text') => ({ type, getAttribute: name => attributes[name] ?? null, closest: () => null });
  assert.equal(describeControlElement(element({})), null);
  assert.deepEqual(describeControlElement(element({ 'data-window-state-field': 'directory', id: 'temporary-id', name: 'path' })), { key: 'directory' });
  const first = describeControlElement(element({ name: 'kind', value: 'step' }, 'checkbox'));
  const second = describeControlElement(element({ name: 'kind', value: 'iges' }, 'checkbox'));
  assert.notEqual(first.key, second.key);
});

test('inherited disabled fields and inert controls neither restore nor capture until editable', async () => {
  const memory = createWindowStateMemory({ namespace: 'locks', storage: storage() });
  const windowKey = JSON.stringify(['editor', '']);
  memory.writeFields(windowKey, { locked: 90, inert: 80, editable: 70 });
  const listeners = new Map();
  const window = { isConnected: true, getAttribute: name => name === 'data-window-state-key' ? 'editor' : null,
    closest: () => null, getClientRects: () => [{}], querySelectorAll: () => controls };
  const control = (name, restriction) => ({ type: 'text', tagName: 'INPUT', name, value: `default-${name}`,
    isConnected: true, disabled: false, readOnly: false, restriction,
    getAttribute: attribute => attribute === 'name' ? name : null, getClientRects: () => [{}],
    matches(selector) { return selector === ':disabled' ? this.restriction === 'disabled' : true; },
    closest(selector) {
      if (selector === '[inert]') return this.restriction === 'inert' ? {} : null;
      return selector === 'dialog,[role="dialog"],.new-project-dialog' ? window : null;
    },
    dispatchEvent(event) { listeners.get(event.type)?.({ target: this }); },
  });
  const controls = [control('locked', 'disabled'), control('inert', 'inert'), control('editable', '')];
  const document = { querySelectorAll: () => [window],
    defaultView: { Event, getComputedStyle: () => ({ display: 'block', visibility: 'visible' }) },
    addEventListener: (name, handler) => listeners.set(name, handler), removeEventListener: name => listeners.delete(name) };
  const applied = [];
  const controller = installWindowStateMemory(document, { memory, controlSelector: 'input,select,textarea',
    restoreControl(control, value, context) {
      applied.push({ field: control.name, previous: context.previousValue, current: control.value, remembered: value });
    } });
  await controller.restoreWindow(window);
  assert.equal(controls[0].value, 'default-locked'); assert.equal(controls[1].value, 'default-inert');
  assert.equal(controls[2].value, '70');
  assert.deepEqual(applied, [{ field: 'editable', previous: 'default-editable', current: '70', remembered: 70 }]);
  for (const input of controls.slice(0, 2)) { input.value = 'blocked-change'; listeners.get('input')({ target: input }); }
  assert.equal(memory.read(windowKey, 'locked', null), 90); assert.equal(memory.read(windowKey, 'inert', null), 80);
  controls[0].restriction = ''; controls[1].restriction = '';
  await controller.restoreWindow(window);
  assert.equal(controls[0].value, '90'); assert.equal(controls[1].value, '80');
  assert.deepEqual(applied.slice(1), [
    { field: 'locked', previous: 'blocked-change', current: '90', remembered: 90 },
    { field: 'inert', previous: 'blocked-change', current: '80', remembered: 80 },
  ]);
  controller.dispose();
});

test('model-owned controls can disable live transfer while retaining next-opening memory', async () => {
  const memory = createWindowStateMemory({ namespace: 'model-owned', storage: storage() });
  const listeners = new Map();
  const window = { isConnected: true, getAttribute: name => name === 'data-window-state-key' ? 'selection-window' : null,
    closest: () => null, getClientRects: () => [{}], querySelectorAll: () => controls };
  const makeControl = (name, type) => ({ type, tagName: 'INPUT', name, value: 'model default', checked: false,
    isConnected: true, disabled: false, readOnly: false,
    getAttribute: attribute => attribute === 'name' ? name : null, getClientRects: () => [{}],
    matches: selector => selector !== ':disabled',
    closest: selector => selector === 'dialog,[role="dialog"],.new-project-dialog' ? window : null,
    dispatchEvent() {},
  });
  let controls = [makeControl('selected', 'checkbox'), makeControl('draft', 'text')];
  const document = { querySelectorAll: () => [window],
    defaultView: { Event, getComputedStyle: () => ({ display: 'block', visibility: 'visible' }) },
    addEventListener: (name, handler) => listeners.set(name, handler), removeEventListener: name => listeners.delete(name) };
  const options = { memory, controlSelector: 'input,select,textarea', describeControl: control => ({ key: control.name, transfer: control.type !== 'checkbox' }) };
  let controller = installWindowStateMemory(document, options);
  controls[0].checked = true; listeners.get('input')({ target: controls[0] });
  controls[1].value = 'latest text draft'; listeners.get('input')({ target: controls[1] });
  controls.forEach(control => { control.isConnected = false; });
  controls = [makeControl('selected', 'checkbox'), makeControl('draft', 'text')];
  // The model has cleared selection while rebuilding the form.
  await controller.restoreWindow(window);
  assert.equal(controls[0].checked, false, 'model selection must not be overwritten by the earlier leaf edit');
  assert.equal(controls[1].value, 'latest text draft', 'ordinary text drafts still transfer');
  const windowKey = JSON.stringify(['selection-window', '']);
  assert.equal(memory.read(windowKey, 'selected', null), true, 'transfer opt-out keeps persistence');
  controller.dispose();
  controller = installWindowStateMemory(document, options); await controller.restoreWindow(window);
  assert.equal(controls[0].checked, true, 'a new logical opening still restores its remembered value');
  controller.dispose();
});

test('returning semantic fields hydrate the model again while temporary restrictions retain their live identity', async () => {
  const memory = createWindowStateMemory({ namespace: 'semantic-switch', storage: storage() });
  const windowKey = JSON.stringify(['profile-editor', '']), fieldKey = profile => JSON.stringify(['width', profile]);
  memory.write(windowKey, fieldKey('B'), 44);
  const state = { profile: 'A', model: 20 }, listeners = new Map(), restorations = [];
  const window = { isConnected: true, getAttribute: name => name === 'data-window-state-key' ? 'profile-editor' : null,
    closest: () => null, getClientRects: () => [{}], querySelectorAll: () => [width] };
  const makeWidth = () => ({ type: 'number', tagName: 'INPUT', name: 'width', value: String(state.model),
    get valueAsNumber() { return Number(this.value); }, isConnected: true, disabled: false, readOnly: false, restriction: '',
    getAttribute: attribute => attribute === 'name' ? 'width' : null,
    getClientRects() { return this.restriction === 'hidden' ? [] : [{}]; },
    matches(selector) { return selector === ':disabled' ? this.restriction === 'disabled' : true; },
    closest(selector) {
      if (selector === '[inert]') return this.restriction === 'inert' ? {} : null;
      return selector === 'dialog,[role="dialog"],.new-project-dialog' ? window : null;
    }, dispatchEvent() {},
  });
  let width = makeWidth();
  const document = { querySelectorAll: () => [window],
    defaultView: { Event, getComputedStyle: () => ({ display: 'block', visibility: 'visible' }) },
    addEventListener: (name, handler) => listeners.set(name, handler), removeEventListener: name => listeners.delete(name) };
  const controller = installWindowStateMemory(document, { memory, controlSelector: 'input,select,textarea',
    describeControl: () => ({ key: fieldKey(state.profile) }),
    restoreControl(control, value, context) { state.model = value; restorations.push({ profile: state.profile, previous: context.previousValue, value }); } });
  width.value = '22'; state.model = 22; listeners.get('input')({ target: width });
  state.profile = 'B'; state.model = 40; width.isConnected = false; width = makeWidth(); await controller.restoreWindow(window);
  assert.equal(state.model, 44); assert.equal(width.value, '44');
  state.profile = 'A'; state.model = 20; width.isConnected = false; width = makeWidth(); await controller.restoreWindow(window);
  assert.equal(width.value, '22'); assert.equal(state.model, 22, 'returning A must synchronize its model as well as its DOM');
  assert.deepEqual(restorations, [{ profile: 'B', previous: 40, value: 44 }, { profile: 'A', previous: 20, value: 22 }]);
  for (const restriction of ['hidden', 'disabled', 'inert']) {
    width.restriction = restriction; await controller.restoreWindow(window);
    width.restriction = ''; await controller.restoreWindow(window);
  }
  assert.equal(restorations.length, 2, 'temporary restrictions do not rehydrate existing semantic fields');
  assert.equal(memory.read(windowKey, fieldKey('A'), null), 22);
  controller.dispose();
});

test('retiring a semantic identity invalidates an old adapter even when the physical input remains connected', async () => {
  const memory = createWindowStateMemory({ namespace: 'semantic-race', storage: storage() });
  memory.write(JSON.stringify(['editor', '']), 'width/A', 22);
  const state = { profile: 'A' }, contexts = [];
  let release; const gate = new Promise(resolve => { release = resolve; });
  const window = { isConnected: true, getAttribute: name => name === 'data-window-state-key' ? 'editor' : null,
    closest: () => null, getClientRects: () => [{}], querySelectorAll: () => [width] };
  const width = { type: 'text', tagName: 'INPUT', name: 'width', value: '20', isConnected: true, disabled: false, readOnly: false,
    getClientRects: () => [{}], matches: selector => selector !== ':disabled',
    closest: selector => selector === 'dialog,[role="dialog"],.new-project-dialog' ? window : null, dispatchEvent() {} };
  const document = { querySelectorAll: () => [window], defaultView: { Event, getComputedStyle: () => ({ display: 'block', visibility: 'visible' }) },
    addEventListener() {}, removeEventListener() {} };
  const controller = installWindowStateMemory(document, { memory, controlSelector: 'input,select,textarea', describeControl: () => ({ key: `width/${state.profile}` }),
    async restoreControl(_control, _value, context) { contexts.push(context); await gate; } });
  assert.equal(contexts[0].isCurrent(), true);
  state.profile = 'B'; width.value = '40'; controller.refresh();
  assert.equal(contexts[0].isCurrent(), false, 'semantic retirement must cancel the earlier response despite the same DOM node');
  release(); await controller.restoreWindow(window);
  assert.equal(width.value, '40');
  controller.dispose();
});

test('window and page-region policies opt in selected fields before custom descriptors, and whole-window false wins', async () => {
  const memory = createWindowStateMemory({ namespace: 'page-policies', storage: storage() });
  const windowKey = JSON.stringify(['policy-editor', '']);
  memory.writeFields(windowKey, { explicit: 'saved explicit', path: 'saved path', quantity: 'saved quantity', note: 'saved note' });
  const listeners = new Map(), described = [];
  let disabled = false;
  const window = { isConnected: true, policy: undefined,
    getAttribute(name) { return name === 'data-window-state-key' ? 'policy-editor' : name === 'data-window-state-controls' ? this.policy ?? null : null; },
    closest: () => null, getClientRects: () => [{}], querySelectorAll: () => controls };
  const region = { getAttribute: name => name === 'data-window-state-controls' ? '[name="path"]' : null,
    closest: selector => selector === 'dialog,[role="dialog"],.new-project-dialog' ? window : null };
  const makeControl = (name, explicit = false, ownRegion = null) => ({ type: 'text', tagName: 'INPUT', name, value: `default ${name}`,
    isConnected: true, disabled: false, readOnly: false,
    getAttribute: attribute => attribute === 'name' ? name : attribute === 'data-window-state-field' && explicit ? name : null,
    getClientRects: () => [{}],
    matches(selector) {
      if (selector === ':disabled') return false;
      if (selector === '[data-window-state-field],[data-window-state-key]') return explicit;
      if (selector === '[name="path"]') return name === 'path';
      if (selector === '[name="note"]') return name === 'note';
      return selector === 'input,select,textarea';
    },
    closest(selector) {
      if (selector === '[data-window-state-controls]') return ownRegion ?? (window.policy !== undefined ? window : null);
      return selector === 'dialog,[role="dialog"],.new-project-dialog' ? window : null;
    }, dispatchEvent() {},
  });
  const controls = [makeControl('explicit', true), makeControl('path', false, region), makeControl('quantity', false, region), makeControl('note')];
  const document = { querySelectorAll: () => [window], defaultView: { Event, getComputedStyle: () => ({ display: 'block', visibility: 'visible' }) },
    addEventListener: (name, handler) => listeners.set(name, handler), removeEventListener: name => listeners.delete(name) };
  const controller = installWindowStateMemory(document, { memory,
    describeWindow: () => ({ key: 'policy-editor', ...(disabled ? { controlSelector: false } : {}) }),
    describeControl(control) { described.push(control.name); return { key: control.name }; } });
  await controller.restoreWindow(window);
  assert.deepEqual(controls.map(control => control.value), ['saved explicit', 'saved path', 'default quantity', 'default note']);
  assert.ok(described.every(name => ['explicit', 'path'].includes(name)), 'custom descriptor must never see unselected controls');
  controls[2].value = 'quantity edit'; listeners.get('input')({ target: controls[2] });
  controls[3].value = 'note edit'; listeners.get('input')({ target: controls[3] });
  assert.equal(memory.read(windowKey, 'quantity', ''), 'saved quantity'); assert.equal(memory.read(windowKey, 'note', ''), 'saved note');
  window.policy = '[name="note"]'; await controller.restoreWindow(window);
  assert.equal(controls[3].value, 'saved note', 'window declaration can select an otherwise excluded field');
  assert.equal(controls[1].value, 'saved path', 'page region retains its own narrower policy');
  disabled = true; controller.refresh();
  controls[0].value = 'blocked explicit'; controls[1].value = 'blocked path';
  listeners.get('input')({ target: controls[0] }); listeners.get('input')({ target: controls[1] });
  assert.equal(memory.read(windowKey, 'explicit', ''), 'saved explicit'); assert.equal(memory.read(windowKey, 'path', ''), 'saved path');
  disabled = false; await controller.restoreWindow(window);
  assert.equal(controls[1].value, 'saved path', 'reenabled region uses ordinary model synchronization');
  controller.dispose();
});
