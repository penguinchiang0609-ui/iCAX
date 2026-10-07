import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserAssetPath, browserReportDirectory } from './browserPackageRuntime.mjs';

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const output = browserReportDirectory('window-state-memory-20261006');
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
const report = { passed: false, checks: [], transport: 'real Chromium DOM, mouse and keyboard, durable localStorage' };
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 1000 } });
  const errors = []; page.on('pageerror', error => errors.push(error.message));
  await page.route('http://window-memory.test/**', route => {
    const path = decodeURIComponent(new URL(route.request().url()).pathname);
    if (path === '/') return route.fulfill({ contentType: 'text/html', body: '<!doctype html><meta charset="utf-8"><body></body>' });
    if (!path.startsWith('/src/')) return route.abort();
    return route.fulfill({ contentType: 'text/javascript', body: readFileSync(browserAssetPath(path.slice(5)), 'utf8') });
  });
  await page.goto('http://window-memory.test/');
  const setup = async seed => page.evaluate(async seed => {
    const api = await import('/src/iCAX-UI/SDK/Forms/windowStateMemory.mjs');
    window.fixture?.controller?.dispose();
    document.head.innerHTML = '<style>body{margin:0;font:14px Arial}dialog{margin:10px;width:610px;height:640px;left:0;position:relative}input,select{display:block;margin:8px;width:240px}input[type=checkbox],input[type=radio]{width:20px}.pane{height:120px;overflow:auto}.nested{height:80px;overflow:auto}.spacer{height:1200px}#other{height:120px}</style>';
    document.body.innerHTML = `<dialog data-window-state-key="picker" data-window-state-scope="A" id="picker">
      <input name="path" value="default path"><input name="quantity" type="number" value="1">
      <input name="enabled" type="checkbox"><div id="conditional" hidden><input name="conditional" value="default condition"></div>
      <select name="shape"><option value="round">round</option><option value="rect">rect</option><option value="disabled" disabled>disabled</option></select>
      <select name="multiple" multiple><option value="a">a</option><option value="b">b</option><option value="c">c</option></select>
      <input name="mode" type="radio" value="one" checked><input name="mode" type="radio" value="two">
      <input name="duplicate" value="first"><input name="duplicate" value="second"><input value="unnamed">
      <input name="password" type="password" value="private"><input name="file" type="file"><input name="hidden" type="hidden" value="internal"><input name="readonly" readonly value="server">
      <select name="unknown"><option value="available">available</option></select>
      <input name="ignored" data-window-state-ignore value="adapter owned">
      <fieldset disabled><input name="fieldset-disabled" value="locked inherited"></fieldset>
      <section inert><input name="inert" value="locked inert"></section>
      <div class="pane left"><div class="nested"><div class="spacer"></div></div><div class="spacer"></div></div>
      <div class="pane right"><div class="spacer"></div></div>
    </dialog><dialog data-window-state-key="picker" data-window-state-scope="B" id="other"><input name="path" value="other default"></dialog>`;
    const memory = api.createWindowStateMemory({ namespace: 'sdk-browser', storage: localStorage });
    const mainKey = JSON.stringify(['picker', 'A']), otherKey = JSON.stringify(['picker', 'B']);
    if (seed) {
      memory.writeFields(mainKey, { path: 'D:/加工件', quantity: 5, enabled: true, conditional: 'remember condition',
        shape: 'rect', multiple: ['a', 'b'], mode: 'two', duplicate: 'must not leak', password: 'forbidden', hidden: 'forbidden', readonly: 'forbidden', unknown: 'deleted-option', ignored: 'forbidden', 'fieldset-disabled': 'must stay locked', inert: 'must stay inert' });
      memory.write(otherKey, 'path', 'D:/another-window');
    }
    const picker = document.querySelector('#picker'), other = document.querySelector('#other');
    picker.addEventListener('change', event => { if (event.target.name === 'enabled') document.querySelector('#conditional').hidden = !event.target.checked; });
    picker.show(); other.show();
    const captures = [], restores = [];
    const controller = api.installWindowStateMemory(document, { memory, controlSelector: 'input,select,textarea',
      onCapture(control, value) { captures.push([control.name, value]); }, onRestore(control, value) { restores.push([control.name, value]); } });
    await controller.restoreWindow(picker); await controller.restoreWindow(other);
    window.fixture = { api, memory, controller, mainKey, otherKey, captures, restores };
    return { path: picker.querySelector('[name=path]').value, quantity: picker.querySelector('[name=quantity]').value,
      checkbox: picker.querySelector('[name=enabled]').checked, conditional: picker.querySelector('[name=conditional]').value,
      shape: picker.querySelector('[name=shape]').value, multiple: [...picker.querySelector('[name=multiple]').selectedOptions].map(option => option.value),
      radio: picker.querySelector('[name=mode]:checked').value, duplicates: [...picker.querySelectorAll('[name=duplicate]')].map(input => input.value),
      other: other.querySelector('[name=path]').value, ignored: picker.querySelector('[name=ignored]').value,
      password: picker.querySelector('[name=password]').value, readonly: picker.querySelector('[name=readonly]').value,
      hidden: picker.querySelector('[name=hidden]').value, unknown: picker.querySelector('[name=unknown]').value,
      inheritedDisabled: picker.querySelector('[name=fieldset-disabled]').value, inert: picker.querySelector('[name=inert]').value,
      captures: captures.length };
  }, seed);
  let values = await setup(true);
  assert.deepEqual(values, { path: 'D:/加工件', quantity: '5', checkbox: true, conditional: 'remember condition', shape: 'rect',
    multiple: ['a', 'b'], radio: 'two', duplicates: ['first', 'second'], other: 'D:/another-window', ignored: 'adapter owned',
    password: 'private', readonly: 'server', hidden: 'internal', unknown: 'available', inheritedDisabled: 'locked inherited',
    inert: 'locked inert', captures: 0 });
  report.checks.push('semantic identities, independent scoped windows, types, dependent field, unknown select, excluded and ambiguous controls');
  const inheritedLocks = await page.evaluate(async () => {
    const locked = document.querySelector('[name=fieldset-disabled]'), inert = document.querySelector('[name=inert]');
    for (const input of [locked, inert]) { input.value = 'blocked edit'; input.dispatchEvent(new Event('input', { bubbles: true })); }
    const before = { disabledProperty: locked.disabled, matchesDisabled: locked.matches(':disabled'),
      disabledStored: fixture.memory.read(fixture.mainKey, 'fieldset-disabled', ''), inertStored: fixture.memory.read(fixture.mainKey, 'inert', '') };
    locked.closest('fieldset').disabled = false; inert.closest('[inert]').inert = false;
    await fixture.controller.restoreWindow(document.querySelector('#picker'));
    return { ...before, disabledAfterUnlock: locked.value, inertAfterUnlock: inert.value };
  });
  assert.deepEqual(inheritedLocks, { disabledProperty: false, matchesDisabled: true, disabledStored: 'must stay locked',
    inertStored: 'must stay inert', disabledAfterUnlock: 'must stay locked', inertAfterUnlock: 'must stay inert' });
  report.checks.push('inherited fieldset disabled and inert controls exclude capture and defer hydration until unlocked');
  await page.locator('#picker [name=path]').fill('D:/最近修改');
  await page.locator('#picker [name=quantity]').fill('8');
  await page.locator('#picker [name=shape]').selectOption('round');
  await page.locator('#picker [name=multiple]').selectOption(['b', 'c']);
  await page.locator('#picker [name=mode][value=one]').check();
  await page.locator('#picker [name=enabled]').uncheck();
  const persisted = await page.evaluate(() => fixture.memory.readFields(fixture.mainKey));
  assert.equal(persisted.path, 'D:/最近修改'); assert.equal(persisted.quantity, 8); assert.equal(persisted.enabled, false);
  assert.deepEqual(persisted.multiple, ['b', 'c']); assert.equal(persisted.mode, 'one');
  await page.evaluate(() => { document.querySelector('#picker').close(); document.querySelector('#other').close(); });
  await page.waitForTimeout(30);
  await page.evaluate(async () => {
    const picker = document.querySelector('#picker'); picker.querySelector('[name=path]').value = 'reset';
    picker.querySelector('[name=quantity]').value = '1'; picker.show(); await fixture.controller.restoreWindow(picker);
  });
  assert.equal(await page.locator('#picker [name=path]').inputValue(), 'D:/最近修改');
  report.checks.push('cancel/close and reopen restores the last edit rather than the last confirmed submission');
  await page.reload(); values = await setup(false);
  assert.equal(values.path, 'D:/最近修改'); assert.equal(values.quantity, '8'); assert.equal(values.checkbox, false);
  assert.equal(values.conditional, 'default condition');
  await page.locator('#picker [name=enabled]').check();
  await page.waitForFunction(() => document.querySelector('#picker [name=conditional]').value === 'remember condition');
  report.checks.push('new document reads durable storage; hidden fields restore only after becoming applicable');

  // An asynchronous model adapter must not overwrite input made while its response is pending.
  await page.evaluate(async () => {
    fixture.controller.dispose();
    document.body.innerHTML = `<dialog id="race" data-window-state-key="race"><input name="slow" value="default"><input name="later" value="later-default">
      <div class="pane left"><div class="nested"><div class="spacer"></div></div><div class="spacer"></div></div><div class="pane right"><div class="spacer"></div></div></dialog>`;
    const memory = fixture.api.createWindowStateMemory({ namespace: 'sdk-race', storage: localStorage });
    memory.writeFields(JSON.stringify(['race', '']), { slow: 'saved-old', later: 'saved-later' });
    const dialog = document.querySelector('#race'); dialog.show();
    let release; const gate = new Promise(resolve => { release = resolve; });
    const f = window.race = { memory, ready: true, restores: [], staleWrites: 0, release, gate, owner: {} };
    f.controller = fixture.api.installWindowStateMemory(document, { memory, controlSelector: 'input,select,textarea',
      describeWindow(element) { return { key: element.dataset.windowStateKey, owner: f.owner, ready: f.ready }; },
      async restoreControl(control, value, context) {
        f.restores.push(control.name); context.dispatch();
        if (control.name === 'slow') { f.context = context; await gate; if (context.isCurrent()) { control.value = value; f.staleWrites++; } }
      } });
    f.nodes = { slow: dialog.querySelector('[name=slow]'), later: dialog.querySelector('[name=later]'), left: dialog.querySelector('.left'),
      right: dialog.querySelector('.right'), nested: dialog.querySelector('.nested') };
    f.listeners = 0; f.nodes.slow.addEventListener('fixture-input', () => f.listeners++);
  });
  await page.locator('#race [name=slow]').fill('new-user-value');
  await page.locator('#race [name=later]').fill('later-user-value');
  await page.locator('#race [name=slow]').focus();
  const before = await page.evaluate(() => {
    race.nodes.slow.setSelectionRange(3, 8); race.nodes.left.scrollTop = 177; race.nodes.right.scrollTop = 201; race.nodes.nested.scrollTop = 72;
    race.ready = false; race.controller.refresh();
    // Continue interacting after the async request; these are the positions that must survive.
    race.nodes.left.scrollTop = 233; race.nodes.right.scrollTop = 277; race.nodes.nested.scrollTop = 99;
    return { left: race.nodes.left.scrollTop, right: race.nodes.right.scrollTop, nested: race.nodes.nested.scrollTop };
  });
  await page.evaluate(async () => { race.release(); await race.controller.restoreWindow(document.querySelector('#race')); race.ready = true; race.controller.refresh(); });
  const interaction = await page.evaluate(() => {
    race.nodes.slow.dispatchEvent(new Event('fixture-input'));
    return { value: race.nodes.slow.value, later: race.nodes.later.value, focus: document.activeElement === race.nodes.slow,
      selection: [race.nodes.slow.selectionStart, race.nodes.slow.selectionEnd], left: race.nodes.left.scrollTop,
      right: race.nodes.right.scrollTop, nested: race.nodes.nested.scrollTop, staleWrites: race.staleWrites,
      restores: race.restores, listeners: race.listeners, current: race.context.isCurrent(),
      sameInput: document.querySelector('[name=slow]') === race.nodes.slow,
      persisted: race.memory.read(JSON.stringify(['race', '']), 'slow', '') };
  });
  assert.deepEqual(interaction, { value: 'new-user-value', later: 'later-user-value', focus: true, selection: [3, 8], ...before,
    staleWrites: 0, restores: ['slow'], listeners: 1, current: false, sameInput: true, persisted: 'new-user-value' });
  report.interaction = interaction;
  report.checks.push('actual user input wins over pending async restoration; latest focus, caret, both sidebars, nested scroll and listener identities survive');

  const replaced = await page.evaluate(async () => {
    const old = document.querySelector('#race'), replacement = old.cloneNode(true);
    replacement.querySelector('[name=slow]').value = 'rerender reset'; replacement.querySelector('[name=later]').value = 'rerender reset';
    race.ready = false; old.replaceWith(replacement); race.controller.refresh();
    race.ready = true; race.controller.refresh(); await race.controller.restoreWindow(replacement);
    return { slow: replacement.querySelector('[name=slow]').value, later: replacement.querySelector('[name=later]').value, restores: race.restores };
  });
  assert.deepEqual(replaced, { slow: 'new-user-value', later: 'later-user-value', restores: ['slow'] });
  report.checks.push('pending synchronous window replacement keeps its session, silently transfers latest drafts and never repeats model hydration');

  const canceled = await page.evaluate(async () => {
    race.controller.dispose();
    const dialog = document.querySelector('#race'); dialog.querySelector('[name=slow]').value = 'new-owner-default';
    let release; const gate = new Promise(resolve => { release = resolve; });
    const f = window.cancelRace = { owner: {}, ready: true, callbacks: 0, release };
    f.controller = fixture.api.installWindowStateMemory(document, { memory: race.memory, controlSelector: 'input,select,textarea',
      describeWindow(element) { return { key: 'race', owner: f.owner, ready: f.ready }; },
      async restoreControl(control, value, context) { if (control.name === 'slow') { f.context = context; await gate; if (context.isCurrent()) f.callbacks++; } } });
    f.ready = false; f.owner = {}; f.controller.refresh(); release(); await Promise.resolve(); await Promise.resolve();
    return { aborted: f.context.signal.aborted, current: f.context.isCurrent(), callbacks: f.callbacks };
  });
  assert.deepEqual(canceled, { aborted: true, current: false, callbacks: 0 });
  report.checks.push('owner changes abort pending responses and cannot mutate another project');
  // Returning to a profile is a semantic reappearance, not a same-field DOM patch.
  await page.evaluate(async () => {
    cancelRace.controller.dispose();
    document.body.innerHTML = `<dialog id="semantic" data-window-state-key="semantic"><select name="profile"><option value="A">A</option><option value="B">B</option></select>
      <div id="width-container"><input name="width" type="number" value="20"></div><input name="note" value="unchanged note">
      <div class="pane left"><div class="nested"><div class="spacer"></div></div><div class="spacer"></div></div><div class="pane right"><div class="spacer"></div></div></dialog>`;
    const memory = fixture.api.createWindowStateMemory({ namespace: 'sdk-semantic-return', storage: localStorage });
    const fieldKey = profile => JSON.stringify(['width', profile]);
    memory.write(JSON.stringify(['semantic', '']), fieldKey('B'), 44);
    const dialog = document.querySelector('#semantic'); dialog.show();
    const f = window.semantic = { memory, profile: 'A', model: 20, updates: [], fieldKey, listeners: 0 };
    dialog.addEventListener('input', event => { if (event.target.name === 'width') f.model = Number(event.target.value); });
    dialog.addEventListener('change', event => {
      if (event.target.name !== 'profile') return;
      f.profile = event.target.value; f.model = f.profile === 'A' ? 20 : 40;
      document.querySelector('[name=width]').replaceWith(Object.assign(document.createElement('input'), { name: 'width', type: 'number', value: String(f.model) }));
    });
    f.controller = fixture.api.installWindowStateMemory(document, { memory, controlSelector: 'input,select,textarea',
      describeControl(control) { return { key: control.name === 'width' ? fieldKey(f.profile) : control.name }; },
      restoreControl(control, value, context) {
        if (control.name === 'width') { f.model = Number(value); f.updates.push([f.profile, context.previousValue, value]); }
        context.dispatch();
      } });
    f.nodes = { note: dialog.querySelector('[name=note]'), left: dialog.querySelector('.left'), right: dialog.querySelector('.right'), nested: dialog.querySelector('.nested') };
    f.nodes.note.addEventListener('fixture-note', () => f.listeners++);
  });
  await page.locator('#semantic [name=width]').fill('22');
  await page.locator('#semantic [name=note]').fill('latest note draft');
  await page.locator('#semantic [name=note]').focus();
  const semanticInteraction = await page.evaluate(() => {
    semantic.nodes.note.setSelectionRange(2, 9); semantic.nodes.left.scrollTop = 190;
    semantic.nodes.right.scrollTop = 230; semantic.nodes.nested.scrollTop = 77;
    return { left: semantic.nodes.left.scrollTop, right: semantic.nodes.right.scrollTop, nested: semantic.nodes.nested.scrollTop };
  });
  await page.locator('#semantic [name=profile]').selectOption('B');
  await page.waitForFunction(() => semantic.model === 44);
  await page.locator('#semantic [name=profile]').selectOption('A');
  await page.waitForFunction(() => semantic.model === 22);
  const semanticReturn = await page.evaluate(async () => {
    const width = document.querySelector('#semantic [name=width]');
    const before = semantic.updates.length;
    for (const restriction of ['hidden', 'disabled', 'inert']) {
      if (restriction === 'inert') width.closest('#width-container').inert = true;
      else width[restriction] = true;
      semantic.controller.refresh();
      if (restriction === 'inert') width.closest('#width-container').inert = false;
      else width[restriction] = false;
      await semantic.controller.restoreWindow(document.querySelector('#semantic'));
    }
    semantic.nodes.note.dispatchEvent(new Event('fixture-note'));
    return { width: width.value, model: semantic.model, updates: semantic.updates, additionalRestores: semantic.updates.length - before,
      note: semantic.nodes.note.value, sameNote: semantic.nodes.note === document.querySelector('[name=note]'),
      focus: document.activeElement === semantic.nodes.note, selection: [semantic.nodes.note.selectionStart, semantic.nodes.note.selectionEnd],
      left: semantic.nodes.left.scrollTop, right: semantic.nodes.right.scrollTop, nested: semantic.nodes.nested.scrollTop, listeners: semantic.listeners };
  });
  assert.deepEqual(semanticReturn, { width: '22', model: 22, updates: [['B', 40, 44], ['A', 20, 22]], additionalRestores: 0,
    note: 'latest note draft', sameNote: true, focus: true, selection: [2, 9], ...semanticInteraction, listeners: 1 });
  report.semanticReturn = semanticReturn;
  report.checks.push('profile A/B/A semantic reappearance synchronizes DOM and model; hidden/disabled/inert retention and latest interactions survive');
  const pagePolicy = await page.evaluate(async () => {
    semantic.controller.dispose();
    document.body.innerHTML = `<dialog id="policy" data-window-state-key="policy"><input name="width" value="default width">
      <input name="note" data-window-state-field="note" value="default note">
      <form data-window-state-controls="[data-remember-path]"><input name="path" data-remember-path value="default path"><input name="quantity" value="default quantity"></form>
      <section role="dialog" data-window-state-key="nested" aria-label="Nested page"><input name="nested-width" value="default nested width"><input name="nested-note" data-window-state-field="nested-note" value="default nested note"></section>
    </dialog><dialog id="off" data-window-state-key="off"><section data-window-state-controls="input"><input name="disabled-policy" data-window-state-field="disabled-policy" value="default disabled"></section></dialog>`;
    const memory = fixture.api.createWindowStateMemory({ namespace: 'sdk-page-policy', storage: localStorage });
    for (const [windowKey, fields] of [['policy', { width: 'saved width', note: 'saved note', path: 'saved path', quantity: 'saved quantity' }],
      ['nested', { 'nested-width': 'saved nested width', 'nested-note': 'saved nested note' }], ['off', { 'disabled-policy': 'saved disabled' }]]) {
      memory.writeFields(JSON.stringify([windowKey, '']), fields);
    }
    document.querySelector('#policy').show(); document.querySelector('#off').show();
    const described = [];
    const controller = fixture.api.installWindowStateMemory(document, { memory,
      describeWindow(element) { return { key: element.dataset.windowStateKey, ...(element.id === 'off' ? { controlSelector: false } : {}) }; },
      describeControl(control, descriptor) { described.push([descriptor.key, control.name]); return { key: control.name }; } });
    await controller.restoreWindow(document.querySelector('#policy'));
    window.policyFixture = { memory, controller, described };
    const get = name => document.querySelector(`[name="${name}"]`).value;
    return { width: get('width'), note: get('note'), path: get('path'), quantity: get('quantity'),
      nestedWidth: get('nested-width'), nestedNote: get('nested-note'), disabled: get('disabled-policy'),
      describedNames: [...new Set(described.map(row => row[1]))].sort() };
  });
  assert.deepEqual(pagePolicy, { width: 'default width', note: 'saved note', path: 'saved path', quantity: 'default quantity',
    nestedWidth: 'default nested width', nestedNote: 'saved nested note', disabled: 'default disabled',
    describedNames: ['nested-note', 'note', 'path'] });
  await page.locator('#policy > [name=width]').fill('unselected width edit');
  await page.locator('#policy [name=quantity]').fill('unselected quantity edit');
  await page.locator('#off [name=disabled-policy]').fill('disabled policy edit');
  const selectedSubset = await page.evaluate(async () => {
    const windowKey = JSON.stringify(['policy', '']);
    const excluded = { width: policyFixture.memory.read(windowKey, 'width', ''), quantity: policyFixture.memory.read(windowKey, 'quantity', ''),
      disabled: policyFixture.memory.read(JSON.stringify(['off', '']), 'disabled-policy', '') };
    document.querySelector('#policy').dataset.windowStateControls = '[name="width"]';
    await policyFixture.controller.restoreWindow(document.querySelector('#policy'));
    return { ...excluded, restoredWidth: document.querySelector('#policy > [name=width]').value,
      regionPath: document.querySelector('[name=path]').value, nestedWidth: document.querySelector('[name=nested-width]').value };
  });
  assert.deepEqual(selectedSubset, { width: 'saved width', quantity: 'saved quantity', disabled: 'saved disabled',
    restoredWidth: 'saved width', regionPath: 'saved path', nestedWidth: 'default nested width' });
  report.pagePolicy = { initial: pagePolicy, selectedSubset };
  report.checks.push('real page policies reject implicit controls, honor closest form subsets and whole-window disable, and keep nested window policy independent');
  assert.deepEqual(errors, []);
  report.passed = true;
  writeFileSync(resolve(output, 'report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report, null, 2));
} finally { await browser.close(); }
