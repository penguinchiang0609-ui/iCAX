// Actual four guardrail descriptors/display files and production UI modules.
// The asynchronous host is a controlled protocol fixture; this is not native CAD.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { browserReportDirectory, importBrowserAsset, readBrowserAsset, serveBrowserAsset } from './browserPackageRuntime.mjs';

const variants = [
  ['modular_guardrail', 'modular-guardrail'],
  ['modular_guardrail_glass-straight', 'modular-guardrail-glass-straight'],
  ['modular_guardrail_cross-straight', 'modular-guardrail-cross-straight'],
  ['modular_guardrail_diamond-straight', 'modular-guardrail-diamond-straight'],
];
const catalogue=JSON.parse(readFileSync(resolve(process.env.ICAX_NATIVE_SCENE_EVIDENCE || 'output/tests/product-profile-boundary-20261005','system-profiles.json')));
const structureContracts = [
  { key: 'guardrailHandrailStructure', parameter: 'handrailMode', choices: ['per_bay', 'continuous'] },
  { key: 'guardrailPostStructure', parameter: 'largePostMode', choices: ['none', 'middle', 'ends'] },
  { key: 'guardrailRailStructure', parameter: 'railCount', choices: ['two', 'three'], values: [2, 3] },
];
const output = browserReportDirectory('guardrail-current-format/browser');
const { tubeDesignerCss } = await importBrowserAsset('apps/tube-designer/webpage/styles/tubeDesigner.css.mjs');

async function installFixture(page, raw) {
  return page.evaluate(async ({ raw,catalogue }) => {
    const views = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const actions = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const { catalogText } = await import('/src/apps/tube-designer/webpage/productCatalog.mjs');
    const { patchLibraryDom, rememberLibraryDom } = await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    const { capturePaneInteraction } = await import('/src/apps/_shared/workbench/utils/paneInteractionState.mjs');
    const { bindProductParameterDiagrams } = await import('/src/apps/tube-designer/webpage/productParameterDiagram.mjs');
    const template = { ...raw, available: true, descriptorLoaded: true, name: catalogText(raw.displayName),
      groups: raw.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
      parameters: raw.parameters.map(field => ({ ...field, type: { enum: 'select', string: 'text' }[field.valueType] ?? field.valueType,
        groupKey: field.group, displayName: catalogText(field.displayName),
        options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
    const parameters = Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue]));
    const product = { entityId: `${template.id}:first`, templateId: template.id, parameters, name: catalogText(raw.displayName), quantity: 1 };
    const view = { tubeDesignerSystemProfiles:structuredClone(catalogue),activeAreaId: 'view', scene: { tubeDesigner: { templates: [template], product,
      activeProductId: product.entityId, members: [], joints: [], parts: [] } } };
    document.body.innerHTML = '<main><div class="cam-workbench"><aside class="cam-context-pane"></aside><div class="cam-viewport"><canvas></canvas><button class="cube">视角</button></div><aside class="cam-info-pane"></aside></div></main>';
    const mount = document.querySelector('main'), pending = [], requests = [], creations = [];
    const context = { mount, project: { projectId: `${template.id}:interaction` }, actions: {}, sceneProxy: {
      invoke(method, payload) {
        if(method==='TubeDesigner.EvaluateProfilePackage'){
          const profile=structuredClone(catalogue.find(item=>item.id===payload.profileRef.id).previewProfile);
          profile.parameters=structuredClone(payload.parameters);return Promise.resolve({profile});
        }
        if (method === 'TubeDesigner.GeneratePreview') {
          creations.push(structuredClone(payload));
          const physical = Object.fromEntries(template.parameters.map(field => [field.key, payload[field.key]]));
          return Promise.resolve({ tubeDesigner: { ...view.scene.tubeDesigner,
            product: { ...view.scene.tubeDesigner.product, entityId: `${template.id}:created`, parameters: physical },
            activeProductId: `${template.id}:created`, generationRun: { entityId: 'fixture-generation' },
            members: [{ entityId: 'fixture-member', role: 'guardrail.handrail' }], receiptOnly: false } });
        }
        if (method !== 'TubeDesigner.UpdateProductParameters') throw new Error(`Unexpected host fixture method: ${method}`);
        requests.push(structuredClone(payload));
        return new Promise(resolve => pending.push(() => resolve({ tubeDesigner: { ...view.scene.tubeDesigner,
          product: { ...view.scene.tubeDesigner.product, parameters: structuredClone(payload.parameters), partsOutdated: true } } })));
      },
    } };
    context.productProxy={invoke:(method,payload)=>context.sceneProxy.invoke(method,payload)};
    const leftHTML = () => `<div class="nested-left"><div><input id="left-note" value="left draft">${views.renderDesignerLeftPane(context, view)}</div></div>`;
    const rightHTML = () => `<input id="right-note" value="right draft">${views.renderDesignerRightPane(context, view)}`;
    mount.querySelector('.cam-context-pane').innerHTML = leftHTML();
    mount.querySelector('.cam-info-pane').innerHTML = rightHTML();
    mount.querySelector('.cam-viewport').insertAdjacentHTML('beforeend', views.renderDesignerViewportOverlay(context, view));
    const defaultDisclosure = [...mount.querySelectorAll('[data-tube-designer-parameter-group]')]
      .map(node => [node.dataset.tubeDesignerParameterGroup, node.open]);
    mount.querySelectorAll('details').forEach(node => { node.open = true; });
    rememberLibraryDom(view, mount, '');
    let patches = 0;
    const render = () => {
      const restore = capturePaneInteraction(mount);
      actions.captureDesignerScrollState(context, view);
      if (!patchLibraryDom(view, mount, { left: leftHTML(), right: rightHTML(),
        overlay: views.renderDesignerViewportOverlay(context, view), suffix: '' })) throw new Error('Guardrail refresh remounted the workbench');
      actions.restoreDesignerScrollState(context, view, { deferred: false });
      restore(); bindProductParameterDiagrams(mount); patches++;
    };
    context.actions.refreshActiveSceneState = async () => render();
    let viewportSequence = 1;
    view.viewport = { fitViewForRevision: revision => ({ fitted: true, revision, renderSequence: viewportSequence++ }),
      setViewDirection: () => { viewportSequence++; return true; },
      getAppliedViewState: () => ({ revision: 'fixture-1', renderSequence: viewportSequence }) };
    const ops = { renderProject: render, showNotice: () => {},
      refreshActiveAreaView: async () => ({ revision: 'fixture-1', viewportReceipt: {
        applied: true, revision: 'fixture-1', entityIds: ['fixture-member'], renderSequence: viewportSequence++ } }) };
    const acceptFixtureViewport = ops.refreshActiveAreaView;
    ops.refreshActiveAreaView = async () => {
      // Creating a new model is an explicit viewport lifecycle transition,
      // separate from the ordinary refresh identity assertions above.
      rememberLibraryDom(view, mount, '');
      return acceptFixtureViewport();
    };
    const ordinary = key => mount.querySelector(`[data-tube-designer-parameter="${key}"]`);
    const choice = key => mount.querySelector(`[data-product-control-editor="${key}"] select`);
    const permanentKey = ['guardHeight', 'sideLength1'].find(key => ordinary(key));
    const permanent = [mount.querySelector('#left-note'), mount.querySelector('#right-note'), mount.querySelector('canvas'),
      mount.querySelector('.cube'), ordinary(permanentKey), ordinary('productCode')];
    if (!permanentKey || permanent.some(node => !node)) throw new Error('Actual guardrail fixture lacks a physical persistent parameter or canvas');
    let eventCount = 0;
    permanent.forEach(node => node.addEventListener('guardrail-audit', () => eventCount++));
    const scrollNodes = () => [mount.querySelector('.cam-context-pane'), mount.querySelector('.cam-info-pane'),
      mount.querySelector('.nested-left'), mount.querySelector('[data-tube-designer-parameter-scroll]')];
    if (scrollNodes().some(node => !node)) throw new Error('Actual guardrail sidebars lack their nested parameter scroll');
    const setScrolls = positions => scrollNodes().forEach((node, index) => { node.scrollTop = positions[index]; });
    const scrolls = () => scrollNodes().map(node => node.scrollTop);
    const same = (actual, expected, message) => { if (JSON.stringify(actual) !== JSON.stringify(expected)) throw new Error(message); };
    const verifyInteraction = (side, positions) => {
      const input = mount.querySelector(`#${side}-note`);
      if (document.activeElement !== input || input.value !== `${side} pending draft` || input.selectionStart !== 2
        || input.selectionEnd !== 8 || input.selectionDirection !== 'backward') throw new Error('Response lost latest sidebar typing/selection');
      same(scrolls(), positions, 'Response lost latest left/right and nested scroll positions');
      const after = [mount.querySelector('#left-note'), mount.querySelector('#right-note'), mount.querySelector('canvas'),
        mount.querySelector('.cube'), ordinary(permanentKey), ordinary('productCode')];
      if (permanent.some((node, index) => node !== after[index])) throw new Error('Response replaced persistent field/canvas nodes');
      eventCount = 0; after.forEach(node => node.dispatchEvent(new Event('guardrail-audit')));
      if (eventCount !== after.length) throw new Error('Response discarded listeners attached to the actual retained nodes');
    };
    const waitForPending = async () => {
      for (let index = 0; !pending.length && index < 200; index++) await new Promise(resolve => setTimeout(resolve, 5));
      if (pending.length !== 1) throw new Error(`Expected one held physical parameter commit, got ${pending.length}`);
    };
    const completeHeldChange = async (operation, side) => {
      await waitForPending();
      const input = mount.querySelector(`#${side}-note`);
      input.focus({ preventScroll: true }); input.value = `${side} pending draft`; input.setSelectionRange(1, 5);
      setScrolls([80, 110, 180, 150]);
      input.setSelectionRange(2, 8, 'backward'); setScrolls([140, 180, 280, 210]);
      pending.shift()(); await operation; verifyInteraction(side, [140, 180, 280, 210]);
      input.value = 'later user typing'; input.setSelectionRange(1, 5, 'forward'); setScrolls([150, 190, 290, 220]);
      await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
      if (input.value !== 'later user typing' || input.selectionStart !== 1 || input.selectionEnd !== 5)
        throw new Error('Deferred restoration overwrote later typing/selection');
      same(scrolls(), [150, 190, 290, 220], 'Deferred restoration overwrote later scroll positions');
    };
    const change = async (target, value, side = 'right') => {
      if (!target) throw new Error('Required actual guardrail field is absent');
      target.value = String(value);
      await completeHeldChange(actions.handleDesignerAreaAction(context, view, target.dataset.camChangeAction, target, ops), side);
    };
    const openStructure = async key => {
      const opener = mount.querySelector(`[data-cam-action="tube-designer-open-product-structure"][data-product-control-key="${key}"]`);
      if (!opener) throw new Error(`Actual structure opener missing: ${key}`);
      await actions.handleDesignerAreaAction(context, view, opener.dataset.camAction, opener, ops);
      if (!mount.querySelector('[data-tube-designer-structure-dialog]')) throw new Error(`Actual structure modal failed to open: ${key}`);
    };
    const chooseStructure = async (key, value, side) => {
      await openStructure(key);
      const target = choice(key);
      if (!target || ![...target.options].some(option => option.value === value)) throw new Error(`Undeclared structure choice: ${key}/${value}`);
      target.value = value;
      await actions.handleDesignerAreaAction(context, view, target.dataset.camChangeAction, target, ops);
      const confirm = mount.querySelector('[data-cam-action="tube-designer-confirm-product-structure"]');
      if (!confirm || confirm.disabled) throw new Error('Actual structure modal cannot confirm its declared choice');
      await completeHeldChange(actions.handleDesignerAreaAction(context, view, confirm.dataset.camAction, confirm, ops), side);
    };
    const assertPhysicalCommits = () => {
      const physical = new Set(template.parameters.map(field => field.key));
      const synthetic = new Set(template.extensions.productControls.map(control => control.key));
      for (const request of requests) {
        // Profile selections use the existing host override container. It is
        // a resource binding, never a synthetic structure parameter.
        const keys = Object.keys(request.parameters).filter(key => key !== 'tubeDesignerProfileOverrides');
        same(keys.sort(), [...physical].sort(), 'Commit did not preserve the complete original physical parameter contract');
        if (Object.hasOwn(request.parameters, 'tubeDesignerProfileOverrides')
          && (!request.parameters.tubeDesignerProfileOverrides || Array.isArray(request.parameters.tubeDesignerProfileOverrides)
            || typeof request.parameters.tubeDesignerProfileOverrides !== 'object')) throw new Error('Profile override container has an invalid shape');
        if (Object.keys(request.parameters).some(key => synthetic.has(key))) throw new Error('Synthetic guardrail editor key leaked into persisted parameters');
      }
    };
    const focusEvents = [], diagramActions = [];
    mount.addEventListener('tube-designer-product-parameter-focus', event => focusEvents.push(structuredClone(event.detail)));
    // Test-shell routing invokes the actual production action for genuine
    // browser clicks on the production add-dialog SVG, with no synthetic SVG.
    mount.addEventListener('click', event => {
      const target = event.target.closest?.('[data-product-diagram-parameter][data-cam-action="tube-designer-focus-product-parameter"]');
      if (target) diagramActions.push({ key: target.dataset.tubeDesignerParameterKey,
        operation: actions.handleDesignerAreaAction(context, view, target.dataset.camAction, target, ops) });
    });
    bindProductParameterDiagrams(mount);
    window.guardrailFixture = { template, context, view, mount, requests, creations, pending, render, actions, views, ops, defaultDisclosure,
      focusEvents, diagramActions, bindProductParameterDiagrams,
      ordinary, choice, change, openStructure, chooseStructure, completeHeldChange, waitForPending,
      setScrolls, scrolls, same, assertPhysicalCommits, permanentKey, stats: () => ({ commits: requests.length, patches }) };
    return { templateId: template.id, physicalParameterCount: template.parameters.length, persistentParameter: permanentKey };
  }, { raw,catalogue });
}

async function verifyCurrentGuardrailContract(page, contracts) {
  return page.evaluate(async contracts => {
    const f = window.guardrailFixture;
    const header = f.mount.querySelector('.cam-info-pane .tube-designer-heading > span')?.textContent ?? '';
    const physical = f.view.scene.tubeDesigner.product.parameters;
    const dimensions = header.replaceAll(',', '');
    if (!dimensions.includes('直式') || !dimensions.includes(`各段 ${physical.sideLength1}`)
      || !dimensions.includes(`高 ${physical.guardHeight} mm`) || dimensions.includes('- × -')) throw new Error(`Actual default guardrail heading does not use sideLength1/guardHeight: ${header}`);
    const expectedGroups = ['guardrail_handrail', 'guardrail_post', 'guardrail_rail', 'guardrail_infill', 'guardrail_installation'];
    f.same(Object.keys(f.template.display.views.right.sectionGroups), expectedGroups, 'Actual display declaration lost a component');
    const groupEvidence = expectedGroups.map(key => {
      const groups = [...f.mount.querySelectorAll(`[data-tube-designer-parameter-group$=":section-group:${key}"]`)];
      const title = f.template.display.views.right.sectionGroups[key].title['zh-CN'];
      if (!groups.length || groups.some(group => group.querySelector(':scope > summary > span')?.textContent !== title))
        throw new Error(`Actual right component group is missing or incorrectly titled: ${key}`);
      return { key, title, actualGroups: groups.map(group => group.dataset.tubeDesignerParameterGroup) };
    });
    for (const section of f.mount.querySelectorAll('[data-tube-designer-parameter-group^="section:"]')) {
      const actual = [...section.querySelectorAll('[data-tube-designer-parameter-group]')]
        .filter(group => /:section-group:guardrail_\w+$/.test(group.dataset.tubeDesignerParameterGroup))
        .map(group => group.dataset.tubeDesignerParameterGroup.split(':').at(-1));
      f.same(actual, expectedGroups.filter(key => actual.includes(key)), 'Actual component order differs from display.right within a section');
    }
    for (const contract of contracts) {
      const control = f.template.extensions.productControls?.find(control => control.key === contract.key);
      if (!control) throw new Error(`Actual descriptor lacks ${contract.key}`);
      if (!f.template.extensions.structureEditors?.some(editor => editor.controlKey === contract.key))
        throw new Error(`Actual descriptor lacks independent structure editor ${contract.key}`);
      f.same(control.choices.map(choice => choice.value), contract.choices, `Descriptor choices changed for ${contract.key}`);
      const editor = f.template.extensions.structureEditors.find(editor => editor.controlKey === contract.key);
      f.same(editor.controls, [contract.key], `Structure editor affects another component: ${contract.key}`);
      if (f.ordinary(contract.parameter)) throw new Error(`Replaced physical mode still renders an ordinary input: ${contract.parameter}`);
      const field = f.mount.querySelector(`[data-product-control-editor="${contract.key}"]`);
      const component = f.template.display.views.right.fields[contract.key].sectionGroup;
      if (!field?.closest(`[data-tube-designer-parameter-group$=":section-group:${component}"]`)) throw new Error(`Actual structure summary is missing or routed to the wrong component: ${contract.key}`);
    }
    for (const [fieldKey, component] of [['handrailProfileType', 'guardrail_handrail'], ['postProfileType', 'guardrail_post'],
      ['railProfileType', 'guardrail_rail'], ['installation', 'guardrail_installation']]) {
      const field = f.ordinary(fieldKey) ?? f.mount.querySelector(`select[data-tube-designer-profile-parameter="${fieldKey}"]`);
      if (!field?.closest(`[data-tube-designer-parameter-group$=":section-group:${component}"]`)) throw new Error(`Actual physical field is routed to the wrong component: ${fieldKey}`);
    }
    if (f.template.id !== 'modular-guardrail') {
      const infill = f.template.parameters.find(field => field.key === 'infillType');
      if (!infill?.readOnly || infill.choices?.length !== 1
        || f.template.extensions.productControls.some(control => control.key === 'guardrailInfillStructure'))
        throw new Error('A fixed infill variant acquired an invented editable infill structure');
    }
    return { contractsPresent: contracts.map(contract => contract.key), rightComponentGroups: groupEvidence,
      defaultHeading: { text: header, layout: physical.layout, sideLength1: physical.sideLength1, guardHeight: physical.guardHeight, verified: true } };
  }, contracts);
}

async function exerciseCurrentGuardrail(page, contracts) {
  return page.evaluate(async contracts => {
    const f = window.guardrailFixture;
    const values = () => f.view.scene.tubeDesigner.product.parameters;
    const opener = key => f.mount.querySelector(`[data-cam-action="tube-designer-open-product-structure"][data-product-control-key="${key}"]`);
    const selectedStructure = async contract => {
      await f.openStructure(contract.key);
      const input = f.choice(contract.key);
      f.same([...input.options].map(option => option.value), contract.choices, 'Actual modal choices differ from the template');
      const physicalValue = values()[contract.parameter];
      const expected = contract.choices[(contract.values ?? contract.choices).indexOf(physicalValue)];
      if (input.value !== expected) throw new Error(`Modal failed to recognize physical value: ${contract.key}`);
      const cancel = f.mount.querySelector('[data-cam-action="tube-designer-cancel-product-structure"]');
      await f.actions.handleDesignerAreaAction(f.context, f.view, cancel.dataset.camAction, cancel, f.ops);
    };
    const transitions = [];
    for (const contract of contracts) {
      await selectedStructure(contract);
      const original = values()[contract.parameter];
      const choices = contract.choices.filter((choice, index) => (contract.values ?? contract.choices)[index] !== original);
      choices.push(contract.choices[(contract.values ?? contract.choices).indexOf(original)]);
      for (const choice of choices) {
        const before = f.requests.length;
        const expected = (contract.values ?? contract.choices)[contract.choices.indexOf(choice)];
        await f.chooseStructure(contract.key, choice, transitions.length % 2 ? 'left' : 'right');
        if (f.requests.length !== before + 1 || !Object.is(values()[contract.parameter], expected)
          || !Object.is(f.requests.at(-1).parameters[contract.parameter], expected)) throw new Error(`Synthetic choice did not submit its declared physical value: ${contract.key}/${choice}`);
        if (typeof expected === 'number' && typeof f.requests.at(-1).parameters[contract.parameter] !== 'number')
          throw new Error('Numeric railCount became a string');
        transitions.push({ control: contract.key, choice, parameter: contract.parameter, physicalValue: expected });
      }
    }
    // Post subfields remain drafts when their physical parent is disabled.
    await f.chooseStructure('guardrailPostStructure', 'middle', 'left');
    if (!f.ordinary('largePostSize')) throw new Error('Middle posts did not expose their actual size field');
    await f.change(f.ordinary('largePostSize'), 90, 'right');
    await f.chooseStructure('guardrailPostStructure', 'none', 'left');
    if (f.ordinary('largePostSize') || values().largePostSize !== 90) throw new Error('Disabling large posts lost their draft or retained an inapplicable input');
    await f.chooseStructure('guardrailPostStructure', 'ends', 'right');
    if (f.ordinary('largePostSize')?.value !== '90') throw new Error('Restoring large posts lost the size draft');
    await f.chooseStructure('guardrailPostStructure', 'none', 'left');

    await f.chooseStructure('guardrailHandrailStructure', 'continuous', 'right');
    // Overall layout/path fields are summaries in the current right inspector;
    // their real editable controls and stepped condition are tested in Add.
    if (!opener('guardrailHandrailStructure')) throw new Error('Level mode lost its actual handrail editor');
    await selectedStructure(contracts[0]);

    const profileEvidence = [];
    for (const prefix of ['handrail', 'post', 'rail']) {
      const radius = `${prefix}CornerRadius`, parameter = `${prefix}ProfileType`;
      const select = () => f.mount.querySelector(`select[data-tube-designer-profile-parameter="${parameter}"][data-tube-designer-profile-mode="right"]`);
      if (!f.template.extensions.productDiagram.profileRoles[prefix].parameters.includes(radius)) throw new Error(`Profile role omits its actual radius: ${prefix}`);
      const defaultType = f.template.parameters.find(field => field.key === parameter).defaultValue;
      if (select()?.value !== `system:${defaultType}` || Boolean(f.ordinary(radius)) !== (defaultType === 'rect')) throw new Error(`Actual default profile/radius visibility disagrees with the template: ${prefix}`);
      if (defaultType !== 'rect') await f.change(select(), 'system:rect', 'right');
      if (!f.ordinary(radius)?.closest(`[data-tube-designer-parameter-group$=":section-group:guardrail_${prefix}"]`)) throw new Error(`Rectangle radius was routed to the wrong actual component: ${prefix}`);
      await f.change(f.ordinary(radius), 3, 'left');
      await f.change(select(), 'system:round', 'right');
      if (values()[parameter] !== 'round' || values()[radius] !== 3 || f.ordinary(radius) || f.ordinary(`${prefix}Depth`))
        throw new Error(`Round profile conditions or radius draft changed: ${prefix}`);
      await f.change(select(), 'system:rect', 'left');
      if (values()[parameter] !== 'rect' || f.ordinary(radius)?.value !== '3') throw new Error(`Returning to rectangle lost the radius draft: ${prefix}`);
      profileEvidence.push({ role: prefix, parameter, radius, actualDefaultType: defaultType, retainedRadius: 3, rectRoundRectVerified: true });
    }

    // The actual material parameter, not a fixture note, is edited while a
    // different physical request is pending. Its input/change is not submitted.
    const material = f.ordinary('productCode');
    if (!material || material.type !== 'text' || material.disabled || material.readOnly) throw new Error('Actual editable productCode text input is absent');
    const savedMaterial = values().productCode;
    const dimensions = f.ordinary('guardHeight'); dimensions.value = String(Number(values().guardHeight) + 1);
    const materialOperation = f.actions.handleDesignerAreaAction(f.context, f.view, dimensions.dataset.camChangeAction, dimensions, f.ops);
    await f.waitForPending();
    let materialListenerCount = 0;
    material.addEventListener('material-draft-audit', () => materialListenerCount++);
    material.focus({ preventScroll: true }); material.value = '304 material pending draft'; material.setSelectionRange(1, 6);
    f.setScrolls([80, 110, 180, 150]);
    material.setSelectionRange(2, 10, 'backward'); f.setScrolls([180, 220, 320, 250]);
    if (f.requests.at(-1).parameters.productCode !== savedMaterial) throw new Error('Unsubmitted material typing entered the pending host request');
    f.pending.shift()(); await materialOperation;
    if (f.ordinary('productCode') !== material || document.activeElement !== material || material.value !== '304 material pending draft'
      || material.selectionStart !== 2 || material.selectionEnd !== 10 || material.selectionDirection !== 'backward'
      || values().productCode !== savedMaterial) throw new Error('Response lost actual productCode typing, selection or physical host boundary');
    material.dispatchEvent(new Event('material-draft-audit'));
    if (materialListenerCount !== 1) throw new Error('Response replaced the actual productCode listener');
    f.same(f.scrolls(), [180, 220, 320, 250], 'Actual material input response lost latest four scroll positions');
    material.value = '316L later draft'; material.setSelectionRange(1, 8, 'forward'); f.setScrolls([190, 230, 330, 260]);
    await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    if (material.value !== '316L later draft' || material.selectionStart !== 1 || material.selectionEnd !== 8
      || material.selectionDirection !== 'forward') throw new Error('Deferred restoration overwrote the actual material parameter draft');
    f.same(f.scrolls(), [190, 230, 330, 260], 'Deferred material restoration overwrote later scroll');
    material.value = String(savedMaterial); // Discard only this test's unsubmitted text, without firing change.

    // The focused number input is genuinely removed by the profile condition.
    const departing = f.ordinary('handrailCornerRadius');
    departing.focus({ preventScroll: true }); f.setScrolls([80, 110, 180, 150]);
    const select = f.mount.querySelector('select[data-tube-designer-profile-parameter="handrailProfileType"][data-tube-designer-profile-mode="right"]');
    select.value = 'system:round';
    const operation = f.actions.handleDesignerAreaAction(f.context, f.view, select.dataset.camChangeAction, select, f.ops);
    await f.waitForPending(); f.setScrolls([160, 200, 300, 230]);
    f.pending.shift()(); await operation;
    if (departing.isConnected || document.activeElement.matches('input,select,textarea')) throw new Error('Removed conditional field stole focus into another editor');
    f.same(f.scrolls(), [160, 200, 300, 230], 'Removing the focused conditional field lost the latest four scroll positions');
    await f.change(f.mount.querySelector('select[data-tube-designer-profile-parameter="handrailProfileType"][data-tube-designer-profile-mode="right"]'), 'system:rect', 'right');

    // A stale modal cannot apply a local draft to a newly active instance.
    await f.openStructure('guardrailRailStructure');
    const staleChoice = f.choice('guardrailRailStructure'); staleChoice.value = 'three';
    await f.actions.handleDesignerAreaAction(f.context, f.view, staleChoice.dataset.camChangeAction, staleChoice, f.ops);
    const staleConfirm = f.mount.querySelector('[data-cam-action="tube-designer-confirm-product-structure"]');
    const callsBeforeSwitch = f.requests.length;
    const nextProduct = { ...f.view.scene.tubeDesigner.product, entityId: `${f.template.id}:second`, parameters: { ...values(), guardHeight: 1300 } };
    f.view.scene.tubeDesigner.product = nextProduct; f.view.scene.tubeDesigner.activeProductId = nextProduct.entityId;
    const note = f.mount.querySelector('#right-note'); note.focus({ preventScroll: true });
    note.value = 'next instance typing'; note.setSelectionRange(2, 7, 'backward'); f.setScrolls([170, 210, 310, 240]);
    await f.actions.handleDesignerAreaAction(f.context, f.view, staleConfirm.dataset.camAction, staleConfirm, f.ops);
    if (f.requests.length !== callsBeforeSwitch || f.mount.querySelector('[data-tube-designer-structure-dialog]')
      || f.view.scene.tubeDesigner.product !== nextProduct || nextProduct.parameters.guardHeight !== 1300
      || document.activeElement !== note || note.value !== 'next instance typing' || note.selectionStart !== 2
      || note.selectionEnd !== 7 || note.selectionDirection !== 'backward') throw new Error('Stale structure dialog affected a different instance');
    f.same(f.scrolls(), [170, 210, 310, 240], 'Stale modal changed latest sidebar scroll');
    f.assertPhysicalCommits();
    return { ...f.stats(), transitions, profiles: profileEvidence,
      latestFourScrollsVerified: true, leftAndRightTypingSelectionVerified: true,
      laterInputNotOverwritten: true, permanentNodesAndListenersPreserved: true,
      removedFieldDoesNotStealFocus: true, conditionalParentDraftsRetained: true,
      actualIdentityTextDraft: { parameter: 'productCode', latestSelectionDirection: 'backward',
        inputNodeAndListenerPreserved: true, hostValueUnchangedWithoutChangeEvent: true, laterDraftNotOverwritten: true },
      leftTextFocusScope: 'Test note because the left instance list has no equivalent free text parameter',
      syntheticControlsNotPersisted: true, numericPhysicalTypePreserved: true, staleDialogCannotCommitAnotherInstance: true };
  }, contracts);
}

async function exerciseAddDialog(page, contracts) {
  return page.evaluate(async contracts => {
    const f = window.guardrailFixture;
    if (!f.mount.querySelector('[data-tube-designer-add-dialog]')) throw new Error('Actual add dialog was not opened for its SVG regression');
    const form = () => f.mount.querySelector('[data-tube-designer-add-form]');
    const ordinary = key => form().querySelector(`[data-tube-designer-parameter="${key}"]`);
    const choice = key => form().querySelector(`[data-product-control-editor="${key}"] select`);
    const change = async (target, value) => {
      if (!target) throw new Error('Actual add dialog field missing');
      target.value = String(value);
      await f.actions.handleDesignerAreaAction(f.context, f.view, target.dataset.camChangeAction, target, f.ops);
    };
    const expectedTitles = ['整体结构', '扶手', '立柱', '横档', ...(f.template.id === 'modular-guardrail' ? ['填充'] : [])];
    const actualTitles = [...form().querySelectorAll('[data-tube-designer-parameter-group] > summary > span')].map(node => node.textContent);
    f.same(actualTitles, expectedTitles, 'Actual add dialog groups do not match the declared component order');
    for (const contract of contracts) {
      if (ordinary(contract.parameter)) throw new Error(`Add dialog renders the replaced hidden physical mode: ${contract.parameter}`);
      const input = choice(contract.key);
      if (!input) throw new Error(`Add dialog lacks actual synthetic structure: ${contract.key}`);
      f.same([...input.options].map(option => option.value), contract.choices, 'Add dialog structure choices changed');
      const value = contract.choices.at(-1);
      await change(input, value);
      const expected = (contract.values ?? contract.choices).at(-1);
      if (!Object.is(f.view.tubeDesignerAddDraft[contract.parameter], expected)) throw new Error('Add synthetic control failed to keep physical type/value');
    }
    if (ordinary('cornerPostMode')) throw new Error('Straight add layout shows an inapplicable corner mode');
    const layoutField = f.template.parameters.find(field => field.key === 'layout');
    let cornerDraftVerified = false;
    if (layoutField.choices.some(choice => choice.value === 'left_l')) {
      await change(ordinary('layout'), 'left_l');
      if (!ordinary('cornerPostMode')) throw new Error('L add layout lacks its original corner mode');
      await change(ordinary('cornerPostMode'), 'double');
      await change(ordinary('layout'), 'straight');
      if (ordinary('cornerPostMode') || f.view.tubeDesignerAddDraft.cornerPostMode !== 'double') throw new Error('Straight layout discarded the corner draft');
      await change(ordinary('layout'), 'u');
      if (ordinary('cornerPostMode')?.value !== 'double') throw new Error('U layout lost the retained double corner draft');
      cornerDraftVerified = true;
    } else if (layoutField.choices.length !== 1 || layoutField.choices[0].value !== 'straight') throw new Error('Fixed straight variant has unexpected layout choices');
    const expectedDraft = structuredClone(f.view.tubeDesignerAddDraft);
    if (f.template.extensions.productControls.some(control => Object.hasOwn(expectedDraft, control.key))) throw new Error('Add draft persisted a synthetic key');
    const confirm = f.mount.querySelector('[data-cam-action="tube-designer-confirm-add"]');
    await f.actions.handleDesignerAreaAction(f.context, f.view, confirm.dataset.camAction, confirm, f.ops);
    if (f.creations.length !== 1) throw new Error('Add confirmation failed to submit one controlled host request');
    const payload = f.creations[0];
    for (const field of f.template.parameters) {
      if (!Object.is(payload[field.key], expectedDraft[field.key])) throw new Error(`Add confirmation changed physical parameter ${field.key}`);
    }
    if (f.template.extensions.productControls.some(control => Object.hasOwn(payload, control.key))) throw new Error('Add creation payload contains synthetic controls');
    const allowedPayload = new Set([...f.template.parameters.map(field => field.key),
      'templateId', 'templateVersion', 'instanceQuantity', 'instanceName', 'createdAt', 'receiptOnly','tubeDesignerProfileOverrides']);
    if (Object.keys(payload).some(key => !allowedPayload.has(key))) throw new Error('Add creation payload enlarged the physical parameter and host envelope contract');
    return { actualGroupTitles: actualTitles, cornerDraftVerified, fixedStraightChoicesPreserved: !cornerDraftVerified,
      creationRequestCount: f.creations.length, syntheticControlsNotSubmitted: true, physicalInputsAndTypesPreserved: true,
      viewportTransport: 'controlled protocol receipt fixture; no native CAD or mesh' };
  }, contracts);
}

async function exerciseRightStructureEvents(page, contracts) {
  const events = [];
  for (const contract of contracts) {
    const button = page.locator(`[data-cam-action="tube-designer-open-product-structure"][data-product-control-key="${contract.key}"]`);
    await page.evaluate(() => window.guardrailFixture.mount.querySelector('#right-note').focus({ preventScroll: true }));
    // Different buttons may scroll to the same screen coordinate. Move out of
    // the inspector first so every hover generates a genuine pointer entry.
    await page.mouse.move(1, 1);
    await button.hover();
    const hovered = await page.evaluate(key => {
      const f = window.guardrailFixture, event = f.focusEvents.at(-1);
      if (event?.key !== key || event.mode !== 'right') throw new Error(`Actual right structure hover did not emit its synthetic parameter key: ${key}; actual=${JSON.stringify(event)}`);
      return event;
    }, contract.key);
    await button.focus();
    const focused = await page.evaluate(key => {
      const f = window.guardrailFixture, event = f.focusEvents.at(-1);
      const actual = f.mount.querySelector(`[data-cam-action="tube-designer-open-product-structure"][data-product-control-key="${key}"]`);
      if (event?.key !== key || document.activeElement !== actual) throw new Error('Actual right structure focus did not remain on its own production button');
      return event;
    }, contract.key);
    await page.evaluate(async key => {
      const f = window.guardrailFixture;
      f.mount.querySelector('#right-note').focus({ preventScroll: true });
      // Explicit action contract: the right inspector currently has no SVG.
      await f.actions.handleDesignerAreaAction(f.context, f.view, 'tube-designer-focus-product-parameter', {
        dataset: { tubeDesignerEditorMode: 'right', tubeDesignerParameterKey: key },
      }, f.ops);
      if (document.activeElement?.dataset.productControlKey !== key) throw new Error('Focus action failed to resolve the actual current right structure button');
    }, contract.key);
    events.push({ control: contract.key, hovered, focused, focusActionContractVerified: true });
  }
  return { actualHoverFocusEvents: events, rightInspectorSvgPresent: false,
    scope: 'Real right buttons emit production binder events; focus action checked separately, no right SVG fixture' };
}

async function exerciseActualAddDiagram(page, contracts, id) {
  await page.evaluate(() => {
    const f = window.guardrailFixture;
    f.view.tubeDesignerAddDialogOpen = true; f.view.tubeDesignerAddTemplateId = f.template.id;
    f.view.tubeDesignerAddDraft = Object.fromEntries(f.template.parameters.map(field => [field.key, field.defaultValue]));
    f.view.tubeDesignerAddInstanceName = 'Guardrail protocol fixture';
    f.mount.querySelector('.cam-workbench').insertAdjacentHTML('beforeend', f.views.renderDesignerAddDialog(f.view.scene.tubeDesigner, f.view));
    f.bindProductParameterDiagrams(f.mount);
  });
  const levelGeometry = await page.evaluate(() => {
    const f = window.guardrailFixture, svg = f.mount.querySelector('[data-tube-designer-add-form] svg');
    const lines = selector => [...svg.querySelectorAll(selector)].map(line =>
      Object.fromEntries(['x1','y1','x2','y2'].map(key => [key, Number(line.getAttribute(key))])));
    const handrail = lines('.product-diagram-handrail'), rails = lines('.product-diagram-rail'), posts = lines('.product-diagram-post');
    if (handrail.length !== 1 || handrail.concat(rails).some(line => Math.abs(line.y2-line.y1) > 0.01)) throw new Error('Horizontal guardrail is visually tilted');
    if (svg.querySelector('.product-diagram-slope-guide')) throw new Error('Compact preview retains a floating auxiliary handrail');
    const values = f.view.tubeDesignerAddDraft;
    const ratio = Math.abs((handrail[0].x2-handrail[0].x1)/(posts[0].y2-posts[0].y1));
    if (Math.abs(ratio-(values.sideLength1+values.postWidth+values.startExtension+values.finishExtension)/(values.guardHeight-values.handrailDepth/2)) > 0.01) throw new Error('Guardrail preview squashes its true proportions');
    if (rails.some(line => line.y1 >= posts[0].y2)) throw new Error('Lower rails lose foot clearance');
    const panels = [...svg.querySelectorAll('.product-diagram-glass')];
    if (f.template.id === 'modular-guardrail-glass-straight' && panels.length !== values.sideBayCount1) throw new Error('Board preview loses panels');
    for (const panel of panels) {
      const ys = [...panel.points].map(point => point.y);
      if (Math.min(...ys) <= handrail[0].y1 || Math.max(...ys) >= rails[0].y1) throw new Error('Boards extend outside their actual rail boundaries');
    }
    return { horizontal: true, widthHeightRatio: ratio, bottomClearanceRetained: true, auxiliaryHandrailAbsent: true, boardCount: panels.length };
  });
  const clickEvidence = [];
  for (const contract of contracts) {
    const selector = `[data-tube-designer-add-form] svg [data-product-diagram-parameter~="${contract.key}"][data-cam-action="tube-designer-focus-product-parameter"]`;
    const before = await page.evaluate(() => structuredClone(window.guardrailFixture.view.tubeDesignerAddDraft));
    await page.locator(selector).first().scrollIntoViewIfNeeded();
    const clickPoint = await page.evaluate(selector => {
      const group = document.querySelector(selector);
      // Find an exposed point on actual painted geometry. Rail/post crossings
      // are intentionally skipped; no forced click or dispatched event is used.
      for (const line of group.querySelectorAll('line')) {
        const matrix = line.getScreenCTM();
        if (!matrix) continue;
        for (const ratio of [0.23, 0.37, 0.61, 0.79, 0.11, 0.91, 0.5]) {
          const x = Number(line.getAttribute('x1')) * (1 - ratio) + Number(line.getAttribute('x2')) * ratio;
          const y = Number(line.getAttribute('y1')) * (1 - ratio) + Number(line.getAttribute('y2')) * ratio;
          const point = new DOMPoint(x, y).matrixTransform(matrix);
          const hit = document.elementFromPoint(point.x, point.y);
          if (hit?.closest('[data-product-diagram-parameter]') === group) return {
            x: point.x, y: point.y, ratio, paintedClass: line.getAttribute('class'), hitTag: hit.tagName,
          };
        }
      }
      throw new Error(`Actual component has no exposed painted click target: ${group?.dataset.productDiagramParameter}`);
    }, selector);
    await page.mouse.click(clickPoint.x, clickPoint.y);
    const clicked = await page.evaluate(async ({ key, before }) => {
      const f = window.guardrailFixture;
      await Promise.all(f.diagramActions.map(item => item.operation));
      const field = f.mount.querySelector(`[data-tube-designer-add-form] select[data-product-control-key="${key}"]`);
      if (!field || document.activeElement !== field || f.diagramActions.at(-1)?.key !== key) throw new Error(`Actual add SVG click failed to focus its declared select: ${key}`);
      const geometry = [...f.mount.querySelectorAll(`[data-tube-designer-add-form] svg [data-product-diagram-parameter~="${key}"]`)];
      if (!geometry.length || geometry.some(node => !node.classList.contains('is-active'))) throw new Error('Actual add SVG failed to highlight its focused structure');
      f.same(f.view.tubeDesignerAddDraft, before, 'Diagram focus mutated physical input');
      if (f.requests.length || f.creations.length) throw new Error('Diagram focus caused an unsolicited host mutation');
      return { control: key, focusedSelect: field.dataset.productControlKey, activeGeometryGroups: geometry.length, physicalInputsUnchanged: true };
    }, { key: contract.key, before });
    clickEvidence.push({ ...clicked, clickPoint });
  }
  const conditional = await page.evaluate(async () => {
    const f = window.guardrailFixture, form = () => f.mount.querySelector('[data-tube-designer-add-form]');
    const field = key => form().querySelector(`[data-tube-designer-parameter="${key}"]`);
    const change = async (target, value) => {
      target.value = value;
      await f.actions.handleDesignerAreaAction(f.context, f.view, target.dataset.camChangeAction, target, f.ops);
    };
    await change(field('pathMode'), 'stepped');
    const handrail = form().querySelector('.product-diagram-guardrail-handrail');
    if (!handrail?.querySelector('line') || handrail.hasAttribute('data-cam-action') || handrail.hasAttribute('data-product-diagram-parameter')
      || form().querySelector('select[data-product-control-key="guardrailHandrailStructure"]')) throw new Error('Stepped add SVG lost handrail geometry or retained an inapplicable click target');
    await change(field('pathMode'), 'level');
    if (!form().querySelector('svg [data-product-diagram-parameter~="guardrailHandrailStructure"]')) throw new Error('Returning to level lost the handrail diagram target');
    const orientation = [];
    if (f.template.id === 'modular-guardrail') {
      const select = () => form().querySelector('select[data-product-control-key="guardrailInfillStructure"]');
      for (const value of ['horizontal', 'vertical']) {
        await change(select(), value);
        const lines = [...form().querySelectorAll('.product-diagram-guardrail-infill line')];
        if (!lines.length) throw new Error('Base bars diagram has no actual infill lines');
        const vectors = lines.map(line => ({ dx: Number(line.getAttribute('x2')) - Number(line.getAttribute('x1')),
          dy: Number(line.getAttribute('y2')) - Number(line.getAttribute('y1')) }));
        if (value === 'horizontal' ? vectors.some(vector => Math.abs(vector.dx) < Math.abs(vector.dy))
          : vectors.some(vector => Math.abs(vector.dx) > 0.01 || Math.abs(vector.dy) < 1)) throw new Error(`Base bars diagram did not draw actual ${value} lines`);
        if (f.view.tubeDesignerAddDraft.barOrientation !== value || f.view.tubeDesignerAddDraft.infillType !== 'bars') throw new Error('Diagram orientation changed the host infill contract');
        orientation.push({ orientation: value, lineCount: lines.length, sampleVector: vectors[0] });
      }
    }
    return { steppedHandrailGeometryRetainedWithoutClick: true, levelHandrailClickRestored: true, baseInfillDirections: orientation };
  });
  await page.evaluate(async () => {
    const dialog = window.guardrailFixture.mount.querySelector('[data-tube-designer-add-dialog]');
    for (const node of dialog.querySelectorAll('.tube-designer-config-parameters, .tube-designer-config-body, .tube-designer-template-list')) node.scrollTop = 0;
    await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    if (dialog.querySelector('.tube-designer-config-parameters').scrollTop !== 0) throw new Error('Actual add dialog scroll was not reset to show the overall structure');
  });
  const screenshot = resolve(output, `${id}.add-dialog-after-interaction.png`);
  await page.locator('[data-tube-designer-add-dialog]').screenshot({ path: screenshot });
  return { clicks: clickEvidence, levelGeometry, ...conditional, screenshot, screenshotStage: 'After interaction, actual add scroll reset to top',
    actualProductionSvg: true, nativeGeometryVerified: false };
}

async function captureActualUI(page, id) {
  // Only production panes/modal enter the image. Scroll sentinels remain
  // outside the crop; no invented labels appear in product screenshots.
  const style = await page.addStyleTag({ content: '.cam-info-pane{height:780px!important}.tube-designer-parameter-scroll{height:700px!important}.cam-info-pane:after,.tube-designer-parameter-scroll:after{display:none!important}' });
  await page.evaluate(() => {
    const f = window.guardrailFixture; f.setScrolls([0, 0, 0, 0]);
    for (const [key, open] of f.defaultDisclosure) f.mount.querySelector(`[data-tube-designer-parameter-group="${key}"]`).open = open;
  });
  const panePath = resolve(output, `${id}.right-default.png`);
  await page.locator('.cam-info-pane > [data-tube-designer-product-parameter-scope]').screenshot({ path: panePath });
  await style.evaluate(node => node.remove());
  await page.evaluate(() => window.guardrailFixture.mount.querySelectorAll('details').forEach(node => { node.open = true; }));
  await page.evaluate(async () => window.guardrailFixture.openStructure('guardrailHandrailStructure'));
  const modalPath = resolve(output, `${id}.structure-dialog.png`);
  await page.locator('[data-tube-designer-structure-dialog]').screenshot({ path: modalPath });
  await page.evaluate(async () => {
    const f = window.guardrailFixture, cancel = f.mount.querySelector('[data-cam-action="tube-designer-cancel-product-structure"]');
    await f.actions.handleDesignerAreaAction(f.context, f.view, cancel.dataset.camAction, cancel, f.ops);
  });
  return { defaultRightPane: panePath, structureModal: modalPath };
}

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
let browser;
const results = [];
const startedAt = new Date().toISOString();
let activeTemplate = null, activeStage = 'startup';
const saveReport = (status, error = null) => {
  const json = JSON.stringify({
    scope: 'Actual production guardrail UI/templates with controlled protocol fixture; not native CAD',
    browserEngine: `${process.env.ICAX_BROWSER_CHANNEL || 'msedge'} via Playwright`, standaloneCefEndToEnd: false,
    status, passed: status === 'passed', startedAt, finishedAt: new Date().toISOString(),
    variants: results, ...(error ? { failure: { templateId: activeTemplate, stage: activeStage, message: error.message } } : {}),
  }, null, 2);
  writeFileSync(resolve(output, 'interaction-evidence.json'), json);
  if (error) writeFileSync(resolve(output, `failure-${startedAt.replace(/[^\dT]/g, '-')}.json`), json);
};
try {
  for (const [directory, id] of variants) {
    activeTemplate = id; activeStage = 'load actual assets';
    const raw = { ...JSON.parse(readBrowserAsset(`apps/tube-designer/templates/product/${directory}/template.json`)),
      display: JSON.parse(readBrowserAsset(`apps/tube-designer/templates/product/${directory}/display.json`)) };
    assert.equal(raw.id, id);
    // Each template owns an isolated browser lifecycle, including page teardown.
    browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || 'msedge' });
    const page = await browser.newPage({ viewport: { width: 1400, height: 850 } });
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.route('http://guardrail-interaction.test/**', serveBrowserAsset);
    await page.goto('http://guardrail-interaction.test/');
    await page.addStyleTag({ content: `${tubeDesignerCss}*{box-sizing:border-box}body{margin:0}.cam-workbench{display:flex;gap:10px}.cam-context-pane,.cam-info-pane{height:250px;overflow:auto;width:500px}.nested-left{height:150px;overflow:auto}.nested-left>div{height:1600px}.cam-context-pane:after,.cam-info-pane:after{content:"";display:block;height:1600px}.tube-designer-parameter-scroll{height:180px!important;overflow:auto!important}.tube-designer-parameter-scroll:after{content:"";display:block;height:1600px}.cam-viewport{width:250px}` });
    try {
      activeStage = 'install fixture';
      const installed = await installFixture(page, raw);
      const result = { ...installed, status: 'running', interactionsVerified: false };
      results.push(result);
      const contracts = id === 'modular-guardrail' ? [...structureContracts,
        { key: 'guardrailInfillStructure', parameter: 'barOrientation', choices: ['vertical', 'horizontal'] }] : structureContracts;
      activeStage = 'display and controls contract';
      const checked = await verifyCurrentGuardrailContract(page, contracts);
      Object.assign(result, checked);
      activeStage = 'production right pane and modal screenshots';
      const screenshots = await captureActualUI(page, id);
      Object.assign(result, { screenshots });
      activeStage = 'actual right hover/focus events';
      const rightStructureEvents = await exerciseRightStructureEvents(page, contracts);
      Object.assign(result, { rightStructureEvents });
      activeStage = 'asynchronous structure/profile interactions';
      const interactions = await exerciseCurrentGuardrail(page, contracts);
      Object.assign(result, interactions);
      await installFixture(page, raw);
      activeStage = 'actual add SVG interactions';
      const addDiagram = await exerciseActualAddDiagram(page, contracts, id);
      Object.assign(result, { addDiagram });
      activeStage = 'actual add groups/conditions/physical payload';
      const addDialog = await exerciseAddDialog(page, contracts);
      assert.deepEqual(errors, []);
      Object.assign(result, { addDialog, status: 'passed', interactionsVerified: true });
    } finally { await page.close(); await browser.close(); browser = null; }
  }
  saveReport('passed');
  console.log(`PASS ${results.length} actual guardrail templates/display files and production UI interaction cases; controlled protocol fixture`);
} catch (error) {
  if (results.at(-1)?.templateId === activeTemplate) results.at(-1).status = 'failed';
  saveReport('failed', error);
  throw error;
} finally { if (browser) await browser.close(); }
