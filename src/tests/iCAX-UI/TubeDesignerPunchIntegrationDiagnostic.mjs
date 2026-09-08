// Integration regressions extracted from the original read-only diagnosis.
import assert from "node:assert/strict";
import { resolve } from "node:path";

export async function verifyPunchButtonIntents(page) {
  const idle = () => page.waitForFunction(() => !window.fixture.pending && !window.fixture.view.pending
    && !window.fixture.view.tubeDesignerPunchWizard?.uiPendingClick);
  await page.evaluate(() => { window.fixture.holdPreviews = false; });

  // Compact records edit numeric fields transactionally; only the enable toggle
  // stays inline. Confirming a dirty draft followed by Add must still save once.
  const beforeAdd = await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features.length);
  const beforeAddActions = await page.evaluate(() => window.fixture.actions.filter(action => action === "tube-designer-punch-add").length);
  await page.locator('[data-tube-designer-punch-row="draft"] [data-tube-designer-punch-editor-mode="shape"]').click();await idle();
  const popup=page.locator('[data-punch-parameter-dialog]');
  await popup.locator('[data-tube-designer-punch-parameter="diameter"]').fill("12");
  await popup.locator('[data-cam-action$="parameters-apply"]').click();await idle();
  await page.locator('[data-tube-designer-punch-row="draft"] [data-cam-action="tube-designer-punch-add"]').click();
  await idle();
  const afterFirstAdd = await page.evaluate(() => ({ count: window.fixture.view.tubeDesignerPunchWizard.features.length,
    diameter: window.fixture.view.tubeDesignerPunchWizard.features.at(-1).toolParameters.diameter, actions: window.fixture.actions.slice(-3) }));
  assert.equal(afterFirstAdd.diameter, 12);
  assert.equal(afterFirstAdd.count, beforeAdd + 1, "One Add click adds the confirmed draft exactly once");
  assert.equal(await page.evaluate(() => window.fixture.actions.filter(action => action === "tube-designer-punch-add").length), beforeAddActions + 1);
  assert.equal(await page.evaluate(() => window.fixture.previewMaximumActive), 1);

  const beforeCancel = await page.evaluate(() => {
    const f = window.fixture;
    f.savedWizard = f.view.tubeDesignerPunchWizard;
    return { previewCalls: f.previewCalls.length, value: structuredClone(f.savedWizard.features[0]) };
  });
  await page.locator('.tube-designer-punch-footer [data-cam-action="tube-designer-punch-cancel"]').click();
  await idle();
  assert.equal(await page.evaluate(() => !!window.fixture.view.tubeDesignerPunchWizard), false,
    "One Cancel click closes the wizard without submitting another edit");
  assert.equal(await page.evaluate(() => window.fixture.previewCalls.length), beforeCancel.previewCalls);
  assert.deepEqual(await page.evaluate(() => window.fixture.savedWizard.features[0]), beforeCancel.value);
  await page.evaluate(() => { const f = window.fixture; f.view.tubeDesignerPunchWizard = f.savedWizard; f.render(); });

  await page.evaluate(() => {
    const f = window.fixture, originalInvoke = f.context.sceneProxy.invoke;
    f.applies = [];
    f.context.sceneProxy.invoke = async function (method, payload, options) {
      if (method !== "TubeDesigner.ApplyPunchWizard") return originalInvoke.call(this, method, payload, options);
      f.applies.push(payload);
      assertNoOverlap();
      function assertNoOverlap() { if (f.previewActive !== 0) throw new Error("Apply overlapped an unfinished preview"); }
      await new Promise(resolve => setTimeout(resolve, 12));
      return { tubeDesigner: structuredClone(f.view.scene.tubeDesigner) };
    };
  });
  const beforeApply = await page.evaluate(() => window.fixture.actions.filter(action => action === "tube-designer-punch-apply").length);
  await page.locator('.tube-designer-punch-footer [data-cam-action="tube-designer-punch-apply"]').click();
  await idle();
  assert.equal(await page.evaluate(() => window.fixture.actions.filter(action => action === "tube-designer-punch-apply").length), beforeApply + 1,
    "One Apply click saves the confirmed records once after the tool preview response");
  assert.equal(await page.evaluate(() => window.fixture.applies.length), 1);
  assert.equal(await page.evaluate(() => window.fixture.applies[0].features[0].arrayGroups.length), 2);
  assert.equal(await page.evaluate(() => !!window.fixture.view.tubeDesignerPunchWizard), false);
  await page.evaluate(() => { const f = window.fixture; f.view.tubeDesignerPunchWizard = f.savedWizard; f.render(); });
  assert.deepEqual(await page.evaluate(() => window.fixture.failures), []);
}

export async function verifyPunchHydration(page, artifactDir) {
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  const idle = () => page.waitForFunction(() => !window.fixture.pending && !window.fixture.view.pending);

  // Activate the real WebGL viewport and real ICRG/ICRM decoding. Only resource
  // bytes and native shape computation are doubles, never the hydration code.
  await page.setViewportSize({ width: 1280, height: 800 });
  await page.evaluate(async () => {
    const f = window.fixture;
    const { encodeNestingGeometry, encodePreviewMaterial } = await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const { ThreeRenderViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const originalMount = ThreeRenderViewport.prototype.mount;
    ThreeRenderViewport.prototype.mount = function (...args) { f.lastViewport = this; return originalMount.apply(this, args); };
    const { renderDesignerOperationOverlay } = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const geometry = new THREE.BoxGeometry(1000, 80, 80).toNonIndexed();
    geometry.translate(500, 0, 0);
    const bytes = encodeNestingGeometry({ positions: [...geometry.attributes.position.array],
      indices: Array.from({ length: geometry.attributes.position.count }, (_, index) => index) });
    const material = encodePreviewMaterial(0x61a8bb77),toolMaterial=encodePreviewMaterial(0xee9b3888);
    const toolPositions=[];
    for(let i=0;i<6;i++){const cutter=new THREE.BoxGeometry(14,14,120).toNonIndexed();cutter.translate(100+i*150,0,30);toolPositions.push(...cutter.attributes.position.array);cutter.dispose();}
    const toolBytes=encodeNestingGeometry({positions:toolPositions,indices:Array.from({length:toolPositions.length/3},(_,i)=>i)});
    geometry.dispose();
    f.resourceReads = [];
    f.resourceHold = true;
    f.resourcesFinish = [];
    f.context.sceneProxy.resources = { async get(url) {
      f.resourceReads.push(url);
      if (f.resourceHold) await new Promise(resolve => f.resourcesFinish.push(resolve));
      return new Response(url.includes("material") ? (url.includes("tool")?toolMaterial:material) : url.includes("tools")?toolBytes:bytes);
    } };
    f.workingResources = f.context.sceneProxy.resources;
    f.ops.renderProject = () => {
      f.renderHtml(f.parts.renderPunchWizardDialog(f.context, f.view)
        + renderDesignerOperationOverlay(f.context, f.view));
      f.editor.attachPunchEditor(f.context, f.view, f.context.mount, f.ops);
    };
    f.view.tubeDesignerPunchWizard.preview = {
      baseGeometry: { url: "diagnostic-base", version: 1 }, baseMaterial: { url: "diagnostic-material", version: 1 },
      toolGeometry: { url: "diagnostic-tools", version: 1 }, toolMaterial: { url: "diagnostic-tool-material", version: 1 },
      baseBounds: { min: [0, -40, -40], max: [1000, 40, 40] }, length: 1000,
      revision: f.view.tubeDesignerPunchWizard.revision, includesDraft: false,toolsOnly:true,
    };
    f.view.tubeDesignerPunchWizard.previewMode = "tools";
    f.ops.renderProject();
  });
  await page.waitForFunction(() => window.fixture.resourceReads.length >= 4);
  const loadingState = await page.evaluate(() => ({ pending: window.fixture.view.pending,
    renderPending: window.fixture.view.tubeDesignerPunchWizard.previewRenderPending,
    hasProgress: !!document.querySelector('[data-tube-designer-operation-wait]'),
    ready: document.querySelector('[data-tube-designer-punch-viewport]').dataset.punchPreviewReady,
    reads: window.fixture.resourceReads.length,
    status: document.querySelector('[data-punch-preview-status]')?.textContent }));
  assert.equal(loadingState.pending, true, "Resource loading keeps the wizard locked after native calculation");
  assert.equal(loadingState.renderPending, true);
  assert.equal(loadingState.hasProgress, true);
  assert.equal(loadingState.ready, undefined);
  const previewButton = page.locator('.tube-designer-punch-sheet-scene [data-cam-action="tube-designer-punch-preview"]');
  assert.equal(await previewButton.isDisabled(), true);
  await page.evaluate(async () => {
    const f = window.fixture;
    f.firstHydrationCanvas = f.lastViewport.renderer.domElement;
    await f.parts.handlePartsAreaAction(f.context, f.view, "tube-designer-punch-preview", {}, f.ops);
    f.ops.renderProject();
  });
  assert.equal(await page.evaluate(() => window.fixture.resourceReads.length), 4, "A busy preview action and rerender cannot start another resource flight");
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.previewMode), "tools");
  await page.screenshot({ path: resolve(artifactDir, "punch-hydration-wait.png") });
  await page.evaluate(() => { const f = window.fixture; f.resourceHold = false; f.resourcesFinish.splice(0).forEach(resolve => resolve()); });
  await page.waitForFunction(() => document.querySelector('[data-tube-designer-punch-viewport]')?.dataset.punchPreviewReady === "true");
  await idle();
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.previewRenderPending), false);
  const initialCamera = await page.evaluate(() => window.fixture.lastViewport.getCameraState());
  assert.equal(initialCamera.projectionMode,"orthographic");
  assert.ok(Math.abs(initialCamera.theta+Math.PI/2)<1e-8&&Math.abs(initialCamera.phi-Math.PI/2)<1e-8,"Initial camera is the exact side view, not a perspective or oblique preset");
  await page.evaluate(()=>{window.fixture.ops.renderProject();window.fixture.ops.renderProject();});await idle();
  assert.equal(await page.evaluate(() => window.fixture.resourceReads.length), 4,"Re-rendering tool bodies reuses every previously loaded resource");
  assert.equal(await page.evaluate(() => window.fixture.firstHydrationCanvas === window.fixture.lastViewport.renderer.domElement), true, "Tool body updates retain the same WebGL canvas");
  assert.deepEqual(await page.evaluate(() => window.fixture.lastViewport.getCameraState()), initialCamera);
  const canvasBoxes = await page.evaluate(() => {
    const box = element => { const r = element.getBoundingClientRect(); return { x: r.x, y: r.y, width: r.width, height: r.height, bottom: r.bottom, right: r.right }; };
    return { host: box(document.querySelector('[data-tube-designer-punch-viewport]')), root: box(window.fixture.lastViewport.root), canvas: box(window.fixture.lastViewport.renderer.domElement) };
  });
  assert.ok(canvasBoxes.root.height <= canvasBoxes.host.height + 1 && canvasBoxes.canvas.bottom <= canvasBoxes.host.bottom + 1,
    "The real viewport root/canvas fits inside the responsive scene host: " + JSON.stringify(canvasBoxes));
  await page.setViewportSize({ width: 1024, height: 768 });
  await page.waitForFunction(() => {
    const host = document.querySelector('[data-tube-designer-punch-viewport]')?.getBoundingClientRect();
    const canvas = window.fixture.lastViewport.renderer.domElement.getBoundingClientRect();
    return host && canvas.bottom <= host.bottom + 1 && canvas.right <= host.right + 1;
  });
  for(const size of [{width:1600,height:1000},{width:1280,height:800},{width:1024,height:768}]) {
    await page.setViewportSize(size);
    // Check a fresh dialog's default framing at each size, not a user's already
    // zoomed camera after window resizing. Ordinary edits above keep the camera.
    await page.evaluate(()=>{const f=window.fixture;f.view.tubeDesignerPunchWizard=structuredClone(f.view.tubeDesignerPunchWizard);f.ops.renderProject();});
    await page.waitForFunction(()=>document.querySelector('[data-tube-designer-punch-viewport]')?.dataset.punchPreviewReady==="true"&&!window.fixture.view.pending);
    await page.waitForFunction(()=>{const host=document.querySelector('[data-tube-designer-punch-viewport]')?.getBoundingClientRect(),canvas=window.fixture.lastViewport.renderer.domElement.getBoundingClientRect();return host&&canvas.bottom<=host.bottom+1&&canvas.right<=host.right+1;});
    const framing=await page.evaluate(()=>{const viewport=window.fixture.lastViewport,c=viewport.camera;return{...viewport.getCameraState(),widthRatio:1000/((c.right-c.left)/c.zoom)};});
    assert.equal(framing.projectionMode,"orthographic");
    assert.ok(Math.abs(framing.theta+Math.PI/2)<1e-8&&Math.abs(framing.phi-Math.PI/2)<1e-8);
    assert.ok(Math.abs(framing.widthRatio-.9)<.015,size.width+": a fresh side view should fill 90% of the canvas width: "+JSON.stringify(framing));
    await page.screenshot({path:resolve(artifactDir,"punch-hydrated-layout-"+size.width+".png")});
  }
  await page.setViewportSize({ width: 1280, height: 800 });
  await page.screenshot({ path: resolve(artifactDir, "punch-hydration-ready.png") });
  assert.equal(await page.locator('[data-cam-viewcube]').count(), 1);
  await page.evaluate(() => {
    const f = window.fixture;
    f.context.sceneProxy.resources = { async get() { return new Response("diagnostic resource failure", { status: 503 }); } };
    f.view.tubeDesignerPunchWizard.preview = { ...f.view.tubeDesignerPunchWizard.preview,
      baseGeometry: { url: "diagnostic-unavailable-base", version: 2 } };
    f.ops.renderProject();
  });
  await page.waitForFunction(() => window.fixture.view.tubeDesignerPunchWizard.previewRenderError?.includes("503"));
  await idle();
  const failedState = await page.evaluate(() => ({
    pending: window.fixture.view.pending,
    stateError: window.fixture.view.tubeDesignerPunchWizard.error,
    renderError: window.fixture.view.tubeDesignerPunchWizard.previewRenderError,
    ready: document.querySelector('[data-tube-designer-punch-viewport]')?.dataset.punchPreviewReady === "true",
    applyDisabled: document.querySelector('[data-cam-action="tube-designer-punch-apply"]').disabled,
  }));
  assert.equal(failedState.ready, false);
  assert.equal(failedState.applyDisabled, true);
  assert.match(failedState.renderError, /三维预览显示失败.*重试/);
  assert.equal(failedState.stateError, failedState.renderError);
  assert.equal(await page.getByRole("alert").filter({ hasText: "三维预览显示失败" }).isVisible(), true);
  await page.screenshot({ path: resolve(artifactDir, "punch-hydration-resource-failure.png") });
  const nativeCountBeforeRetry = await page.evaluate(() => {
    const f = window.fixture;
    f.context.sceneProxy.resources = f.workingResources;
    return f.previewCalls.length;
  });
  await page.locator('.tube-designer-punch-sheet-scene [data-cam-action="tube-designer-punch-preview"]').click(); await idle();
  await page.waitForFunction(() => document.querySelector('[data-tube-designer-punch-viewport]')?.dataset.punchPreviewReady === "true");
  assert.equal(await page.evaluate(() => window.fixture.previewCalls.length), nativeCountBeforeRetry, "A display-resource retry must not repeat the native boolean computation");
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.previewRenderError), "");
  assert.equal(await page.locator('[data-cam-action="tube-designer-punch-apply"]').isEnabled(), true);
  await verifyPunchFailureAndReopen(page);
  assert.deepEqual(errors, []);
  console.log("Punch hydration: resource wait, single flight, same canvas, cached tool bodies, initial fit, visible errors and resource-only retry passed.");
}

async function verifyPunchFailureAndReopen(page) {
  const idle = () => page.waitForFunction(() => !window.fixture.pending && !window.fixture.view.pending
    && !window.fixture.view.tubeDesignerPunchWizard?.uiPendingClick);
  const contextWarnings = [];
  page.on("console", message => { if (message.text().includes("Too many active WebGL contexts")) contextWarnings.push(message.text()); });
  const popup = page.locator('[data-punch-parameter-dialog]');
  await page.locator('[data-tube-designer-punch-row="0"] [data-tube-designer-punch-editor-mode="shape"]').click(); await idle();
  const beforeParameter = await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolParameters.diameter);
  await page.evaluate(() => { window.fixture.context.sceneProxy.resources = { async get() { return new Response("intent failure", { status: 503 }); } }; });
  const diameter = popup.locator('[data-tube-designer-punch-parameter="diameter"]');
  await diameter.fill("22");
  await popup.locator('[data-cam-action$="parameters-apply"]').click(); await idle();
  assert.equal(await popup.count(), 1, "A failed display keeps the parameter transaction open instead of silently confirming it");
  assert.match(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.previewRenderError), /503/);
  const beforeRollback = await page.evaluate(() => { window.fixture.context.sceneProxy.resources = window.fixture.workingResources; return window.fixture.previewCalls.length; });
  await diameter.fill("29");
  await popup.getByRole("button", { name: "取消", exact: true }).click(); await idle();
  assert.equal(await popup.count(), 0, "Cancel remains usable after a parameter preview failure");
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.features[0].toolParameters.diameter), beforeParameter);
  assert.equal(await page.evaluate(() => window.fixture.previewCalls.length), beforeRollback + 1, "Cancel previews only the rollback, never the second unsubmitted dirty value");
  assert.equal(await page.evaluate(() => window.fixture.previewCalls.at(-1).payload.features[0].toolParameters.diameter), beforeParameter);

  await page.evaluate(() => { window.fixture.context.sceneProxy.resources = { async get() { return new Response("apply intent failure", { status: 503 }); } }; });
  const beforeApply = await page.evaluate(() => window.fixture.applies.length);
  await page.evaluate(async()=>{const f=window.fixture;f.wizard.updatePunchWizardField(f.view,{value:"102",dataset:{tubeDesignerPunchField:"station",tubeDesignerPunchIndex:"0"}});await f.editor.previewPunch(f.context,f.view,f.part,f.ops,null,{quiet:true});});await idle();
  assert.equal(await page.locator('.tube-designer-punch-footer [data-cam-action="tube-designer-punch-apply"]').isDisabled(),true);
  assert.equal(await page.evaluate(() => window.fixture.applies.length), beforeApply, "The final save remains unavailable after a tool-body resource failed to display");
  const beforeCancel = await page.evaluate(() => window.fixture.previewCalls.length);
  await page.locator('.tube-designer-punch-footer [data-cam-action="tube-designer-punch-cancel"]').click(); await idle();
  assert.equal(await page.evaluate(() => !!window.fixture.view.tubeDesignerPunchWizard), false);
  assert.equal(await page.evaluate(() => window.fixture.previewCalls.length), beforeCancel, "Cancelling the failed wizard does not submit another dirty value");

  await page.evaluate(() => {
    const f = window.fixture, invoke = f.context.sceneProxy.invoke;
    f.context.sceneProxy.resources = f.workingResources;
    f.context.sceneProxy.invoke = (method, payload, options) => method === "TubeDesigner.GetPunchTools"
      ? Promise.resolve({ tools: f.savedWizard.tools }) : invoke(method, payload, options);
    const part = f.view.scene.tubeDesigner.nestingGroups[0].parts.find(item => item.entityId === f.part.entityId);
    part.thumbnailGeometryResourceId = "reopen-stored-geometry"; part.thumbnailGeometryResourceVersion = 1;
  });
  for (let index = 0; index < 4; index++) {
    await page.evaluate(async () => { const f = window.fixture; await f.parts.handlePartsAreaAction(f.context, f.view, "tube-designer-punch-open", { dataset: { tubeDesignerPartId: f.part.entityId } }, f.ops); });
    await idle();
    await page.waitForFunction(() => document.querySelector('[data-tube-designer-punch-viewport]')?.dataset.punchPreviewReady === "true");
    assert.equal(await page.locator('[data-cam-viewcube]').count(), 1);
    assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerPunchWizard.previewRenderError ?? ""), "");
    await page.locator('.tube-designer-punch-footer [data-cam-action="tube-designer-punch-cancel"]').click(); await idle();
    assert.equal(await page.locator('.icax-three-viewport-canvas').count(), 0);
  }
  assert.deepEqual(contextWarnings, []);
  assert.equal(await page.evaluate(() => window.fixture.previewMaximumActive), 1);
  assert.deepEqual(await page.evaluate(() => window.fixture.failures), []);
}

export async function verifyPunchCreationButtonIntents(page) {
  const idle = () => page.waitForFunction(() => !window.fixture.creationPending && !window.fixture.view.pending
    && !window.fixture.view.tubeDesignerPunchWizard?.uiPendingClick);
  await page.evaluate(async () => {
    const f = window.fixture;
    const nesting = await import("/src/apps/tube-designer/webpage/nestingPunchPart.mjs");
    const { renderDesignerOperationOverlay } = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    f.savedCreationTest = { wizard: f.view.tubeDesignerPunchWizard, render: f.ops.renderProject, invoke: f.context.sceneProxy.invoke };
    f.creationCalls = [];
    f.context.sceneProxy.invoke = async function (method, payload, options) {
      if (method === "TubeDesigner.GetPunchTools") return { tools: f.savedCreationTest.wizard.tools };
      if (method !== "TubeDesigner.AddNestingPunchPart") return f.savedCreationTest.invoke.call(this, method, payload, options);
      if (f.previewActive) throw new Error("Creating the part overlapped its preview");
      f.creationCalls.push(payload);
      await new Promise(resolve => setTimeout(resolve, 12));
      return { tubeDesigner: structuredClone(f.view.scene.tubeDesigner), partEntityId: "fixture-created-part" };
    };
    const interactionMount = {
      querySelector(selector) { return selector === "[data-tube-designer-punch-viewport]" ? null : f.context.mount.querySelector(selector); },
      querySelectorAll(selector) { return f.context.mount.querySelectorAll(selector); },
    };
    f.ops.renderProject = () => {
      f.renderHtml(nesting.renderNestingPunchPartDialog(f.view) + renderDesignerOperationOverlay(f.context, f.view));
      f.editor.attachPunchEditor(f.context, f.view, interactionMount, f.ops);
    };
    const dispatch = async (action, target) => {
      if (!action?.startsWith("tube-designer-nesting-punch-create-")) return;
      f.creationPending = true;
      try { await nesting.handleNestingPunchPartAction(f.context, f.view, action, target, f.ops); }
      catch (error) { f.failures.push(error.message); }
      finally { f.creationPending = false; }
    };
    f.creationClick = event => { const target = event.target.closest("[data-cam-action]"); if (target) void dispatch(target.dataset.camAction, target); };
    f.creationChange = event => void dispatch(event.target.dataset.camChangeAction, event.target);
    document.addEventListener("click", f.creationClick);
    document.addEventListener("change", f.creationChange);
    f.openCreation = () => dispatch("tube-designer-nesting-punch-create-open", {});
    await f.openCreation();
  });
  await idle();
  const length = page.locator('[data-tube-designer-nesting-punch-field="length"]');
  const name = page.locator('[data-tube-designer-nesting-punch-field="name"]');
  await length.fill("1200"); await length.press("Tab"); await idle();
  assert.equal(await page.evaluate(() => document.activeElement?.dataset.tubeDesignerNestingPunchField), "quantity", "The main-tube fields retain distinct focus identities");
  const beforeCancel = await page.evaluate(() => window.fixture.previewCalls.length);
  await name.fill("未提交的名称");
  await page.locator('.tube-designer-punch-footer [data-cam-action$="create-cancel"]').click(); await idle();
  assert.equal(await page.evaluate(() => !!window.fixture.view.tubeDesignerNestingPunchPartDraft), false);
  assert.equal(await page.evaluate(() => window.fixture.previewCalls.length), beforeCancel);
  await page.evaluate(() => window.fixture.openCreation()); await idle();
  await length.fill("1300");
  await page.locator('.tube-designer-punch-footer [data-cam-action$="create-apply"]').click(); await idle();
  assert.equal(await page.evaluate(() => window.fixture.creationCalls.length), 1, "A dirty main length followed by Create saves exactly once");
  assert.equal(await page.evaluate(() => window.fixture.creationCalls[0].length), 1300);
  assert.equal(await page.evaluate(() => !!window.fixture.view.tubeDesignerNestingPunchPartDraft), false);
  assert.deepEqual(await page.evaluate(() => window.fixture.failures), []);
  await page.evaluate(() => {
    const f = window.fixture;
    document.removeEventListener("click", f.creationClick);
    document.removeEventListener("change", f.creationChange);
    f.view.tubeDesignerPunchWizard = f.savedCreationTest.wizard;
    f.ops.renderProject = f.savedCreationTest.render;
    f.context.sceneProxy.invoke = f.savedCreationTest.invoke;
    f.render();
  });
}
