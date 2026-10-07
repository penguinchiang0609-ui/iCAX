// Current product structure/bend/join choices, using the actual published UI.
// Native transport is held deliberately so the user can edit before it returns.
import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, importBrowserAsset, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';
const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');
const raw = { ...JSON.parse(readBrowserAsset('apps/tube-designer/templates/product/single_face_security_window/template.json')),
  display: JSON.parse(readBrowserAsset('apps/tube-designer/templates/product/single_face_security_window/display.json')) };
const tools = ['v-notch-sharp', 'edge-arc-groove'].map(id => ({
  ...JSON.parse(readBrowserAsset(`apps/tube-designer/templates/mold/${id}/tool.json`)), libraryScope: 'system' }));
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ headless: true, channel: 'msedge' });
try {
  const page = await browser.newPage({ viewport: { width: 1350, height: 850 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://published-interaction.test/**', serveBrowserAsset);
  await page.goto('http://published-interaction.test/');
  await page.addStyleTag({ content: `${tubeDesignerCss}*{box-sizing:border-box}body{margin:0}.cam-workbench{display:flex;gap:10px}.cam-context-pane,.cam-info-pane{height:250px;overflow:auto;width:480px}.nested-left{height:150px;overflow:auto}.nested-left>div{height:1500px}.cam-context-pane:after,.cam-info-pane:after{content:"";display:block;height:1500px}.tube-designer-parameter-scroll{height:180px!important;overflow:auto!important}.tube-designer-parameter-scroll:after{content:"";display:block;height:1500px}.cam-viewport{width:250px}` });
  const result = await page.evaluate(async ({ raw, tools }) => {
    const views = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const actions = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const { catalogText } = await import('/src/apps/tube-designer/webpage/productCatalog.mjs');
    const { patchLibraryDom, rememberLibraryDom } = await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    const { capturePaneInteraction } = await import('/src/apps/_shared/workbench/utils/paneInteractionState.mjs');
    const template = { ...raw, available: true, descriptorLoaded: true, name: catalogText(raw.displayName),
      groups: raw.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
      parameters: raw.parameters.map(field => ({ ...field, type: { enum: 'select', string: 'text' }[field.valueType] ?? field.valueType,
        groupKey: field.group, displayName: catalogText(field.displayName),
        options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
    const values = { ...Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue])),
      tubeSpecificationPreset: 'custom', frameManufacturingMode: 'segment_weld', frameLayout: 'four_sides',
      accessDoorEnabled: true, infillPattern: 'grid' };
    const product = { entityId: 'published-window', templateId: template.id, parameters: values, name: '防盗窗', quantity: 1 };
    const view = { activeAreaId: 'view', tubeDesignerSystemPunchTools: tools, scene: { tubeDesigner: { templates: [template], product,
      activeProductId: product.entityId, members: [], joints: [], parts: [] } } };
    document.body.innerHTML = '<main><div class="cam-workbench"><aside class="cam-context-pane"></aside><div class="cam-viewport"><canvas></canvas><button class="cube">视角</button></div><aside class="cam-info-pane"></aside></div></main>';
    const mount = document.querySelector('main');
    const pending = [];
    const requests = [];
    const context = { mount, project: { projectId: 'published-interaction' }, actions: {},
      sceneProxy: { invoke(method, payload) {
        if (method !== 'TubeDesigner.UpdateProductParameters') throw new Error(method);
        requests.push(structuredClone(payload));
        return new Promise(resolve => pending.push(() => resolve({ tubeDesigner: { ...view.scene.tubeDesigner,
          product: { ...view.scene.tubeDesigner.product, parameters: structuredClone(payload.parameters), partsOutdated: true } } })));
      } } };
    const left = () => `<div class="nested-left"><div><input id="left-note" value="left draft">${views.renderDesignerLeftPane(context, view)}</div></div>`;
    const right = () => `<input id="right-note" value="right draft">${views.renderDesignerRightPane(context, view)}`;
    mount.querySelector('.cam-context-pane').innerHTML = left();
    mount.querySelector('.cam-info-pane').innerHTML = right();
    mount.querySelectorAll('details').forEach(node => { node.open = true; });
    rememberLibraryDom(view, mount, '');
    let patches = 0;
    const render = () => {
      const restore = capturePaneInteraction(mount);
      actions.captureDesignerScrollState(context, view);
      if (!patchLibraryDom(view, mount, { left: left(), right: right(),
        overlay: views.renderDesignerViewportOverlay(context, view), suffix: '' })) throw new Error('UI edit remounted the workbench');
      actions.restoreDesignerScrollState(context, view, { deferred: false }); restore(); patches++;
    };
    context.actions.refreshActiveSceneState = async () => render();
    const ops = { renderProject: render };
    const choice = key => mount.querySelector(`[data-product-control-editor="${key}"] select`);
    const ordinary = key => mount.querySelector(`[data-tube-designer-parameter="${key}"]`);
    const permanent = [mount.querySelector('#left-note'), mount.querySelector('#right-note'), mount.querySelector('canvas'),
      mount.querySelector('.cube'), ordinary('frameWidth')];
    if (permanent.some(node => !node)) throw new Error('Production fixture lacks a persistent field or canvas');
    let eventCount = 0;
    permanent.forEach(node => node.addEventListener('audit-event', () => eventCount++));
    const scrollNodes = () => [mount.querySelector('.cam-context-pane'), mount.querySelector('.cam-info-pane'),
      mount.querySelector('.nested-left'), mount.querySelector('[data-tube-designer-parameter-scroll]')];
    const setScrolls = positions => scrollNodes().forEach((node, index) => { node.scrollTop = positions[index]; });
    const assertState = (side, positions) => {
      const input = mount.querySelector(`#${side}-note`);
      if (document.activeElement !== input || input.value !== `${side} pending draft` || input.selectionStart !== 2
        || input.selectionEnd !== 8 || input.selectionDirection !== 'backward') throw new Error('Pending response lost current typing/selection');
      if (JSON.stringify(scrollNodes().map(node => node.scrollTop)) !== JSON.stringify(positions)) throw new Error('Pending response lost latest four scroll positions');
      const after = [mount.querySelector('#left-note'), mount.querySelector('#right-note'), mount.querySelector('canvas'),
        mount.querySelector('.cube'), ordinary('frameWidth')];
      if (permanent.some((node, index) => node !== after[index])) throw new Error('Pending response replaced a field/canvas node');
      eventCount = 0; after.forEach(node => node.dispatchEvent(new Event('audit-event')));
      if (eventCount !== after.length) throw new Error('Pending response removed node listeners');
    };
    const completeHeldChange = async (operation, side) => {
      for (let i = 0; !pending.length && i < 200; i++) await new Promise(resolve => setTimeout(resolve, 5));
      if (!pending.length) throw new Error('Product change did not reach its host commit');
      const input = mount.querySelector(`#${side}-note`);
      input.focus({ preventScroll: true }); input.value = `${side} pending draft`; input.setSelectionRange(1, 5);
      setScrolls([80, 110, 180, 150]);
      input.setSelectionRange(2, 8, 'backward'); setScrolls([140, 180, 280, 210]);
      pending.shift()(); await operation;
      assertState(side, [140, 180, 280, 210]);
      input.value = 'later user typing'; input.setSelectionRange(1, 5, 'forward'); setScrolls([150, 190, 290, 220]);
      await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
      if (input.value !== 'later user typing' || input.selectionStart !== 1 || input.selectionEnd !== 5
        || JSON.stringify(scrollNodes().map(node => node.scrollTop)) !== JSON.stringify([150, 190, 290, 220]))
        throw new Error('Delayed restoration overwrote later user interaction');
    };
    const change = async (target, value, side) => {
      if (!target) throw new Error('Required current product control is absent');
      target.value = value;
      await completeHeldChange(actions.handleDesignerAreaAction(context, view, target.dataset.camChangeAction, target, ops), side);
    };
    const openStructure = async () => {
      const opener = mount.querySelector('[data-cam-action="tube-designer-open-product-structure"][data-product-control-key="outerFrameStructure"]');
      if (!opener) throw new Error('Current structure opener is absent');
      await actions.handleDesignerAreaAction(context, view, opener.dataset.camAction, opener, ops);
      if (!mount.querySelector('[data-tube-designer-structure-dialog]')) throw new Error('Current structure editor did not open');
    };
    const changeStructure = async (mode, bend, side) => {
      await openStructure();
      const target = choice('outerFrameStructure'); target.value = mode;
      await actions.handleDesignerAreaAction(context, view, target.dataset.camChangeAction, target, ops);
      const confirm = mount.querySelector('[data-cam-action="tube-designer-confirm-product-structure"]');
      if (!confirm || confirm.disabled) throw new Error('Current structure choices cannot confirm');
      await completeHeldChange(actions.handleDesignerAreaAction(context, view, confirm.dataset.camAction, confirm, ops), side);
      if (bend) {
        const targetBend = choice('outerFrameBendType');
        if (!targetBend || JSON.stringify([...targetBend.options].map(option => option.value)) !== JSON.stringify(['v-groove', 'rounded-v-groove', 'edge-arc']))
          throw new Error(`Current folded bend choices disagree with the actual template: ${JSON.stringify(targetBend ? [...targetBend.options].map(option => option.value) : null)}`);
        await change(targetBend, bend, side);
      }
    };
    const joins = choice('outerFrameConnection');
    if (!joins || JSON.stringify([...joins.options].map(option => option.value)) !== JSON.stringify(['miter', 'butt', 'tabs']))
      throw new Error('Current joined frame choices disagree with the actual template');
    await change(ordinary('frameWidth'), '42', 'right');
    await change(choice('outerFrameConnection'), 'butt', 'left');
    const wrap = ordinary('frameButtWrapMode');
    if (!wrap) throw new Error('Butt choice did not expose its conditional wrap field');
    await changeStructure('plane-single', 'edge-arc', 'right');
    if (wrap.isConnected || choice('outerFrameConnection') || view.scene.tubeDesigner.product.parameters.frameManufacturingMode !== 'plane_v_notch')
      throw new Error('Folded structure retained joined controls or did not commit its declared mode');
    await changeStructure('joined', null, 'left');
    const departing = ordinary('frameButtWrapMode');
    if (!departing) throw new Error('Returning to joined structure lost its retained butt draft');
    departing.focus({ preventScroll: true }); setScrolls([160, 200, 300, 230]);
    const join = choice('outerFrameConnection'); join.value = 'miter';
    const operation = actions.handleDesignerAreaAction(context, view, join.dataset.camChangeAction, join, ops);
    for (let i = 0; !pending.length && i < 200; i++) await new Promise(resolve => setTimeout(resolve, 5));
    pending.shift()(); await operation;
    if (departing.isConnected || document.activeElement.matches('input,select,textarea')) throw new Error('Removed condition field gave focus to another editor');
    if (JSON.stringify(scrollNodes().map(node => node.scrollTop)) !== JSON.stringify([160, 200, 300, 230])) throw new Error('Removing the focused conditional field lost scroll');
    if (requests.some(request => template.extensions.productControls.some(control => Object.hasOwn(request.parameters, control.key))))
      throw new Error('Synthetic product control leaked into the persisted host parameters');
    await openStructure();
    const staleConfirm = mount.querySelector('[data-cam-action="tube-designer-confirm-product-structure"]');
    const callsBeforeSwitch = requests.length;
    view.scene.tubeDesigner.product = { ...view.scene.tubeDesigner.product, entityId: 'another-published-window' };
    const note = mount.querySelector('#right-note'); note.focus({ preventScroll: true });
    note.value = 'next instance typing'; note.setSelectionRange(2, 7, 'backward'); setScrolls([170, 210, 310, 240]);
    await actions.handleDesignerAreaAction(context, view, staleConfirm.dataset.camAction, staleConfirm, ops);
    if (requests.length !== callsBeforeSwitch || mount.querySelector('[data-tube-designer-structure-dialog]')
      || document.activeElement !== note || note.value !== 'next instance typing' || note.selectionStart !== 2 || note.selectionEnd !== 7)
      throw new Error('Stale structure dialog affected a different product instance');
    return { commits: requests.length, patches, actualCurrentJoinedChoices: ['miter', 'butt', 'tabs'],
      structureAndBendConditionsVerified: true, latestFourScrollsVerified: true, leftAndRightTypingSelectionVerified: true,
      laterInputNotOverwritten: true, permanentNodesAndListenersPreserved: true, removedFieldDoesNotStealFocus: true,
      syntheticControlsNotPersisted: true, staleDialogCannotCommitAnotherInstance: true };
  }, { raw, tools });
  assert.deepEqual(errors, []);
  writeFileSync(resolve(browserReportDirectory('published-product-interaction'), 'interaction-evidence.json'), JSON.stringify(result, null, 2));
  console.log('PASS current published structure/bend/join controls preserve asynchronous left/right editing and real DOM nodes', result);
} finally { await browser.close(); }
