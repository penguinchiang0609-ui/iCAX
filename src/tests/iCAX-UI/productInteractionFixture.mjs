// Test-only shell with actual production modules; host responses are controlled.
export async function installProductInteractionFixture(page, raw, nativeScene = null, options = {}) {
  return page.evaluate(async ({ raw, nativeScene, options }) => {
    const views = await import('/src/apps/tube-designer/webpage/designerViews.mjs');
    const actions = await import('/src/apps/tube-designer/webpage/designerActions.mjs');
    const { catalogText } = await import('/src/apps/tube-designer/webpage/productCatalog.mjs');
    const { patchLibraryDom, rememberLibraryDom } = await import('/src/apps/tube-designer/webpage/libraryDomPatch.mjs');
    const { capturePaneInteraction } = await import('/src/apps/_shared/workbench/utils/paneInteractionState.mjs');
    const { bindProductParameterDiagrams } = await import('/src/apps/tube-designer/webpage/productParameterDiagram.mjs');
    const { productDisplayParameters } = await import('/src/apps/tube-designer/webpage/productParameterDependencies.mjs');
    const template = { ...raw, available: true, descriptorLoaded: true, name: catalogText(raw.displayName),
      groups: raw.groups.map(group => ({ ...group, displayName: catalogText(group.displayName) })),
      parameters: raw.parameters.map(field => ({ ...field, type: { enum: 'select', string: 'text' }[field.valueType] ?? field.valueType,
        groupKey: field.group, displayName: catalogText(field.displayName),
        options: field.choices?.map(choice => ({ ...choice, label: catalogText(choice.displayName) })) })) };
    const parameters = structuredClone(options.initialParameters
      ?? Object.fromEntries(template.parameters.map(field => [field.key, field.defaultValue])));
    // Native JSON key ordering differs from descriptor ordering. Compare the
    // same physical fields independent of that serialization detail.
    const displayFingerprint = values => JSON.stringify(Object.fromEntries(
      Object.entries(productDisplayParameters(template, values)).sort(([a], [b]) => a.localeCompare(b))));
    const product = { entityId: `${template.id}:first`, templateId: template.id, parameters, name: catalogText(raw.displayName), quantity: 1 };
    const view = { activeAreaId: 'view', scene: { tubeDesigner: { templates: [template], product,
      activeProductId: product.entityId, members: [], joints: [], parts: [],
      specificationAnnotations: nativeScene?.specificationAnnotations ?? [] } } };
    document.body.innerHTML = '<main><div class="cam-workbench"><aside class="cam-context-pane"></aside><div class="cam-viewport"><canvas></canvas><button class="cube">视角</button></div><aside class="cam-info-pane"></aside></div></main>';
    const mount = document.querySelector('main'), pending = [], requests = [], creations = [], notices = [];
    const context = { mount, project: { projectId: `${template.id}:interaction` }, actions: {}, sceneProxy: {
      invoke(method, payload) {
        if (method === 'TubeDesigner.GeneratePreview') {
          creations.push(structuredClone(payload));
          const physical = Object.fromEntries(template.parameters.map(field => [field.key, payload[field.key]]));
          return Promise.resolve({ tubeDesigner: { ...view.scene.tubeDesigner,
            product: { ...view.scene.tubeDesigner.product, entityId: `${template.id}:created`, parameters: physical },
            activeProductId: `${template.id}:created`, generationRun: { entityId: 'fixture-generation' },
            members: [{ entityId: 'fixture-member', role: 'fixture.member' }], receiptOnly: false } });
        }
        if (method !== 'TubeDesigner.UpdateProductParameters') throw new Error(`Unexpected host fixture method: ${method}`);
        requests.push(structuredClone(payload));
        return new Promise(resolve => pending.push(() => resolve({ tubeDesigner: { ...view.scene.tubeDesigner,
            product: { ...view.scene.tubeDesigner.product, parameters: structuredClone(payload.parameters),
              modelOutdated: displayFingerprint(parameters) !== displayFingerprint(payload.parameters),
              partsOutdated: true } } })));
      },
    } };
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
        overlay: views.renderDesignerViewportOverlay(context, view), suffix: '' })) throw new Error('Product refresh remounted the workbench');
      actions.restoreDesignerScrollState(context, view, { deferred: false });
      restore(); bindProductParameterDiagrams(mount); patches++;
    };
    context.actions.refreshActiveSceneState = async () => render();
    let viewportSequence = 1;
    view.viewport = { fitViewForRevision: revision => ({ fitted: true, revision, renderSequence: viewportSequence++ }),
      setViewDirection: () => { viewportSequence++; return true; },
      getAppliedViewState: () => ({ revision: 'fixture-1', renderSequence: viewportSequence }) };
    if (nativeScene) {
      const { createThreeViewport } = await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
      mount.querySelector('canvas').remove();
      const host = document.createElement('div'); host.style.cssText = 'width:100%;height:600px;position:relative';
      mount.querySelector('.cam-viewport').prepend(host);
      const viewport = createThreeViewport({ continuousRender: false }); viewport.mount(host);
      const points = nativeScene.specificationAnnotations.flatMap(anchor => [anchor.start, anchor.end]);
      const min = [0,1,2].map(i => Math.min(...points.map(point => point[i])));
      const max = [0,1,2].map(i => Math.max(...points.map(point => point[i])));
      const center = min.map((v,i) => (v+max[i])/2);
      const span = Math.max(...max.map((v,i) => v-min[i]), 1000);
      viewport.camera.position.set(center[0]+span*0.18,center[1]-span*1.7,center[2]+span*0.15);
      viewport.camera.lookAt(...center); viewport.camera.updateProjectionMatrix();
      view.viewport = viewport;
      const { bindProductSpecificationAnnotations } = await import('/src/apps/tube-designer/webpage/productParameterDiagram.mjs');
      bindProductSpecificationAnnotations(mount, view);
      viewport.renderer.render(viewport.scene, viewport.camera);
    }
    const ops = { renderProject: render, showNotice: (...args) => notices.push(args),
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
    const permanentKey = template.parameters.map(field => field.key).find(key => key === 'productCode' && ordinary(key)) ?? template.parameters.find(field => !field.visibleWhen && ordinary(field.key))?.key;
    const permanent = [mount.querySelector('#left-note'), mount.querySelector('#right-note'), mount.querySelector('canvas'),
      mount.querySelector('.cube'), ordinary(permanentKey)];
    if (!permanentKey || permanent.some(node => !node)) throw new Error('Actual product fixture lacks a physical persistent parameter or canvas');
    let eventCount = 0;
    permanent.forEach(node => node.addEventListener('product-audit', () => eventCount++));
    const scrollNodes = () => [mount.querySelector('.cam-context-pane'), mount.querySelector('.cam-info-pane'),
      mount.querySelector('.nested-left'), mount.querySelector('[data-tube-designer-parameter-scroll]')];
    if (scrollNodes().some(node => !node)) throw new Error('Actual product sidebars lack their nested parameter scroll');
    const setScrolls = positions => scrollNodes().forEach((node, index) => { node.scrollTop = positions[index]; });
    const scrolls = () => scrollNodes().map(node => node.scrollTop);
    const same = (actual, expected, message) => { if (JSON.stringify(actual) !== JSON.stringify(expected)) throw new Error(message); };
    const verifyInteraction = (side, positions) => {
      const input = mount.querySelector(`#${side}-note`);
      if (document.activeElement !== input || input.value !== `${side} pending draft` || input.selectionStart !== 2
        || input.selectionEnd !== 8 || input.selectionDirection !== 'backward') throw new Error('Response lost latest sidebar typing/selection');
      same(scrolls(), positions, 'Response lost latest left/right and nested scroll positions');
      const after = [mount.querySelector('#left-note'), mount.querySelector('#right-note'), mount.querySelector('canvas'),
        mount.querySelector('.cube'), ordinary(permanentKey)];
      if (permanent.some((node, index) => node !== after[index])) throw new Error('Response replaced persistent field/canvas nodes');
      eventCount = 0; after.forEach(node => node.dispatchEvent(new Event('product-audit')));
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
      if (!target) throw new Error('Required actual product field is absent');
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
      const synthetic = new Set((template.extensions?.productControls ?? []).map(control => control.key));
      for (const request of requests) {
        // Profile selections use the existing host override container. It is
        // a resource binding, never a synthetic structure parameter.
        const keys = Object.keys(request.parameters).filter(key => key !== 'tubeDesignerProfileOverrides');
        same(keys.sort(), [...physical].sort(), 'Commit did not preserve the complete original physical parameter contract');
        if (Object.hasOwn(request.parameters, 'tubeDesignerProfileOverrides')
          && (!request.parameters.tubeDesignerProfileOverrides || Array.isArray(request.parameters.tubeDesignerProfileOverrides)
            || typeof request.parameters.tubeDesignerProfileOverrides !== 'object')) throw new Error('Profile override container has an invalid shape');
        if (Object.keys(request.parameters).some(key => synthetic.has(key))) throw new Error('Synthetic product editor key leaked into persisted parameters');
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
    window.productFixture = { template, context, view, mount, requests, creations, pending, notices, render, actions, views, ops, defaultDisclosure,
      focusEvents, diagramActions, bindProductParameterDiagrams,
      ordinary, choice, change, openStructure, chooseStructure, completeHeldChange, waitForPending,
      setScrolls, scrolls, same, verifyInteraction, assertPhysicalCommits, permanentKey, stats: () => ({ commits: requests.length, patches }) };
    return { templateId: template.id, physicalParameterCount: template.parameters.length, persistentParameter: permanentKey };
  }, { raw, nativeScene, options });
}
