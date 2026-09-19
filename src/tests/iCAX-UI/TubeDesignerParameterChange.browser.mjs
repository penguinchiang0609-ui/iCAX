// Browser regression: existing-product changes commit to the product EC while
// generated geometry stays frozen until explicit regeneration. Undo/redo is
// owned by the host project's native history.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { catalogText } from "../../apps/tube-designer/webpage/productCatalog.mjs";

const raw = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/product/single_face_security_window/template.json", import.meta.url,
)));
const display = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/product/single_face_security_window/display.json", import.meta.url,
)));
const groups = raw.groups.map((group) => ({ ...group, displayName: catalogText(group.displayName) }));
const template = {
  ...raw,
  display,
  available: true,
  name: catalogText(raw.displayName),
  groups,
  parameters: raw.parameters.map((parameter) => ({
    ...parameter,
    displayName: catalogText(parameter.displayName),
    type: { enum: "select", string: "text" }[parameter.valueType] || parameter.valueType,
    groupKey: parameter.group,
    group: groups.find((group) => group.key === parameter.group)?.displayName,
    options: parameter.choices?.map((choice) => ({
      ...choice,
      label: catalogText(choice.displayName),
    })),
  })),
};

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({
  headless: true,
  ...(process.env.ICAX_BROWSER_CHANNEL ? { channel: process.env.ICAX_BROWSER_CHANNEL } : {}),
});

try {
  const page = await browser.newPage();
  const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://tube-designer.test/**", async (route) => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") {
      return route.fulfill({ contentType: "text/html", body: "<!doctype html><body></body>" });
    }
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/")
        || !path.startsWith(sourceRoot.replace(/[\\\/]$/, "") + sep)
        || !/\.m?js$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://tube-designer.test/");

  const result = await page.evaluate(async (templateDescriptor) => {
    const { renderDesignerProductPartsDock, renderDesignerRightPane, renderDesignerViewportOverlay } = await import(
      "/src/apps/tube-designer/webpage/designerViews.mjs"
    );
    const { getDesignerProjectHistoryState, handleDesignerAreaAction, handleDesignerRibbonCommand } = await import(
      "/src/apps/tube-designer/webpage/designerActions.mjs"
    );
    const { bindProductParameterDiagrams, bindProductSpecificationAnnotations } = await import(
      "/src/apps/tube-designer/webpage/productParameterDiagram.mjs"
    );

    const defaults = Object.fromEntries(templateDescriptor.parameters.map((parameter) => [
      parameter.key, parameter.defaultValue,
    ]));
    const base = {
      ...defaults,
      faceType: "five",
      horizontalMaximumCenterSpacing: 400,
      sideHorizontalMaximumCenterSpacing: 400,
      sideVerticalMaximumCenterSpacing: 400,
      topBottomCrossbarMaximumCenterSpacing: 400,
      topBottomRodMaximumCenterSpacing: 400,
    };
    const products = {
      "security-window-1": {
        entityId: "security-window-1", templateId: templateDescriptor.id, name: "防盗窗",
        current: { ...base }, generated: { ...base }, manufactured: { ...base }, memberId: "member-before-1",
      },
      "security-window-2": {
        entityId: "security-window-2", templateId: templateDescriptor.id, name: "防盗窗 2",
        current: { ...base, horizontalMaximumCenterSpacing: 300 },
        generated: { ...base, horizontalMaximumCenterSpacing: 300 },
        manufactured: { ...base, horizontalMaximumCenterSpacing: 300 },
        memberId: "member-before-2",
      },
    };
    const annotationDefinitions = [
      ["horizontalMaximumCenterSpacing", [1200, 0, 38], [1200, 0, 1762]],
      ["sideHorizontalMaximumCenterSpacing", [0, -600, 38], [0, -600, 1762]],
      ["sideVerticalMaximumCenterSpacing", [0, 0, 1762], [0, -600, 1762]],
      ["topBottomCrossbarMaximumCenterSpacing", [0, 0, 1800], [1200, 0, 1800]],
      ["topBottomRodMaximumCenterSpacing", [0, 0, 1800], [0, -600, 1800]],
    ];
    const canonical = (value) => {
      if (Array.isArray(value)) return value.map(canonical);
      if (!value || typeof value !== "object") return value;
      return Object.fromEntries(Object.entries(value)
        .sort(([left], [right]) => left.localeCompare(right))
        .map(([key, item]) => [key, canonical(item)]));
    };
    const equal = (left, right) => JSON.stringify(canonical(left)) === JSON.stringify(canonical(right));
    const manufacturingGroups = new Set(templateDescriptor.extensions.parameterLayout.sections
      .filter((section) => ["materials", "process"].includes(section.key))
      .flatMap((section) => section.groups));
    const manufacturingKeys = new Set(templateDescriptor.parameters
      .filter((parameter) => manufacturingGroups.has(parameter.groupKey))
      .map((parameter) => parameter.key));
    const geometryValues = (parameters) => Object.fromEntries(Object.entries(parameters)
      .filter(([key]) => !manufacturingKeys.has(key)
        && !["tubeDesignerProfileOverrides", "tubeDesignerToolBindings"].includes(key)));
    let activeId = "security-window-1";
    let generationSequence = 0;
    const calls = [];
    const updateInvocationFeedback = [];
    let paintedFrame = 0;
    let specificationCommitStartFrame = -1;
    const countPaintedFrame = () => {
      paintedFrame += 1;
      requestAnimationFrame(countPaintedFrame);
    };
    requestAnimationFrame(countPaintedFrame);
    let deferParameterUpdates = false;
    const pendingParameterUpdates = [];
    const activationCalls = [];
    const annotationCalls = [];
    const annotationFocusCalls = [];
    const parts = {
      "security-window-1": { entityId: "part-before-1", partNumber: "P-001", name: "旧零件 1", transient: true },
      "security-window-2": { entityId: "part-before-2", partNumber: "P-002", name: "旧零件 2", transient: true },
    };

    function snapshot() {
      const selected = products[activeId];
      const modelOutdated = !equal(geometryValues(selected.current), geometryValues(selected.generated));
      const partsOutdated = !equal(selected.current, selected.manufactured);
      return { tubeDesigner: {
        templates: [templateDescriptor],
        activeProductId: activeId,
        product: {
          entityId: selected.entityId,
          templateId: selected.templateId,
          name: selected.name,
          parameters: { ...selected.current },
          modelOutdated,
          partsOutdated,
        },
        specificationAnnotations: annotationDefinitions.map(([parameter, start, end]) => ({
          id: `specification-${parameter}`,
          parameter,
          kind: "spacing",
          start,
          end,
          offset: [32, 0, 24],
          generatedValue: selected.generated[parameter],
        })),
        generationRun: { entityId: `run-${activeId}-${generationSequence}` },
        members: [{ entityId: selected.memberId }],
        instances: Object.values(products).map((item) => ({
          entityId: item.entityId,
          hasDisassembly: true,
          partCount: 1,
          modelOutdated: !equal(geometryValues(item.current), geometryValues(item.generated)),
          partsOutdated: !equal(item.current, item.manufactured),
        })),
        manufacturingGroups: Object.values(products).map((item) => ({
          productEntityId: item.entityId,
          transient: true,
          parts: [parts[item.entityId]],
        })),
        parts: [parts[activeId]],
      } };
    }

    const view = {
      activeAreaId: "view",
      pending: false,
      scene: { tubeDesigner: snapshot().tubeDesigner },
      viewport: {
        fitViewForRevision: (revision) => ({ fitted: true, revision, renderSequence: 2 }),
        setViewDirection: () => true,
        getAppliedViewState: () => ({ revision: "view-next", renderSequence: 3 }),
        setSpecificationAnnotations: (items) => annotationCalls.push(items.map((item) => ({
          parameter: item.parameter,
          pending: item.pending,
          color: item.color,
          label: item.label,
          editing: item.editing,
        }))),
        clearSpecificationAnnotations: () => annotationCalls.push([]),
        focusSpecificationAnnotationEditor: (parameter) => {
          annotationFocusCalls.push(parameter);
          return true;
        },
        setCustomVisibleEntityIds() {},
      },
    };
    document.body.innerHTML = '<main id="mount"></main>';
    const mount = document.querySelector("#mount");
    const context = {
      mount,
      actions: { refreshActiveSceneState: async () => {}, refreshProjectHistoryControls() {} },
      sceneProxy: {
        async invoke(method, payload) {
          if (method === "TubeDesigner.UpdateProductParameters") {
            updateInvocationFeedback.push({
              value: payload.parameters.horizontalMaximumCenterSpacing,
              paintedFrame,
              startFrame: specificationCommitStartFrame,
              status: mount.querySelector("[data-tube-designer-runtime-status]")?.textContent.trim(),
              annotation: annotationCalls.at(-1)?.find(
                (item) => item.parameter === "horizontalMaximumCenterSpacing",
              ),
            });
            calls.push({ method, productEntityId: payload.productEntityId });
            if (deferParameterUpdates) {
              return new Promise((resolve) => pendingParameterUpdates.push({
                complete() {
                  products[payload.productEntityId].current = { ...payload.parameters };
                  resolve(snapshot());
                },
              }));
            }
            products[payload.productEntityId].current = { ...payload.parameters };
            return snapshot();
          }
          if (method === "TubeDesigner.ActivateProduct") {
            activationCalls.push(payload.productEntityId);
            activeId = payload.productEntityId;
            return snapshot();
          }
          if (method === "TubeDesigner.GeneratePreview") {
            calls.push({ method, productEntityId: payload.productEntityId });
            const selected = products[payload.productEntityId];
            const nextParameters = { ...payload };
            for (const key of ["templateId", "templateVersion", "productEntityId", "instanceName", "createdAt"])
              delete nextParameters[key];
            selected.current = { ...nextParameters };
            selected.generated = { ...nextParameters };
            selected.manufactured = { ...nextParameters };
            generationSequence += 1;
            selected.memberId = `member-generated-${generationSequence}`;
            return snapshot();
          }
          throw new Error(`Unexpected method: ${method}`);
        },
      },
    };
    const ops = {
      renderProject() {},
      getActiveAreaViewRevision: () => "view-before",
      refreshActiveAreaView: async (_context, currentView) => ({
        revision: "view-next",
        viewportReceipt: {
          applied: true,
          revision: "view-next",
          entityIds: currentView.scene.tubeDesigner.members.map((member) => member.entityId),
          renderSequence: 1,
        },
      }),
      showNotice(_context, currentView, message) { currentView.notice = message; },
    };

    mount.innerHTML = `<section class="cam-viewport">${renderDesignerViewportOverlay(context, view)}</section>
      <aside class="cam-info-pane">${renderDesignerRightPane(view.scene.tubeDesigner, view, context)}</aside>`;
    for (const details of mount.querySelectorAll("details")) details.open = true;
    bindProductParameterDiagrams(mount);
    bindProductSpecificationAnnotations(mount, view);

    const initialMember = view.scene.tubeDesigner.members[0].entityId;
    const initialAnnotation = annotationCalls.at(-1).find((item) => item.parameter === "horizontalMaximumCenterSpacing");
    const metadataInput = mount.querySelector('[data-tube-designer-parameter="productCode"]');
    const originalMetadata = metadataInput.value;
    const changedMetadata = `${originalMetadata}-A`;
    metadataInput.value = changedMetadata;
    await handleDesignerAreaAction(context, view, "tube-designer-parameter-change", metadataInput, ops);
    const metadataChanged = {
      call: calls.at(-1),
      current: products[activeId].current.productCode,
      generated: products[activeId].generated.productCode,
      member: view.scene.tubeDesigner.members[0].entityId,
      modelDirty: view.tubeDesignerRightDraftDirty,
      partsDirty: view.tubeDesignerPartsDraftDirty,
      status: mount.querySelector("[data-tube-designer-runtime-status]")?.textContent.trim(),
      partsDock: renderDesignerProductPartsDock(context, view),
    };
    await handleDesignerAreaAction(context, view, "tube-designer-edit-scene-specification", {
      dataset: { tubeDesignerParameterKey: "horizontalMaximumCenterSpacing" },
    }, ops);
    const editorOpened = annotationCalls.at(-1).find((item) => item.parameter === "horizontalMaximumCenterSpacing");
    const callsAfterEditorOpen = calls.length;

    specificationCommitStartFrame = paintedFrame;
    await handleDesignerAreaAction(context, view, "tube-designer-scene-specification-change", {
      type: "number", value: "350", dataset: { tubeDesignerParameter: "horizontalMaximumCenterSpacing" },
    }, ops);
    const changed = {
      calls: [...calls],
      current: products[activeId].current.horizontalMaximumCenterSpacing,
      generated: products[activeId].generated.horizontalMaximumCenterSpacing,
      sceneCurrent: view.scene.tubeDesigner.product.parameters.horizontalMaximumCenterSpacing,
      member: view.scene.tubeDesigner.members[0].entityId,
      dirty: view.tubeDesignerRightDraftDirty,
      annotation: annotationCalls.at(-1).find((item) => item.parameter === "horizontalMaximumCenterSpacing"),
      status: mount.querySelector("[data-tube-designer-runtime-status]")?.textContent.trim(),
    };

    await handleDesignerAreaAction(context, view, "tube-designer-scene-specification-change", {
      type: "number", value: "400", dataset: { tubeDesignerParameter: "horizontalMaximumCenterSpacing" },
    }, ops);
    const reverted = {
      current: products[activeId].current.horizontalMaximumCenterSpacing,
      generated: products[activeId].generated.horizontalMaximumCenterSpacing,
      dirty: view.tubeDesignerRightDraftDirty,
      annotation: annotationCalls.at(-1).find((item) => item.parameter === "horizontalMaximumCenterSpacing"),
      status: mount.querySelector("[data-tube-designer-runtime-status]")?.textContent.trim(),
    };

    const values = {
      horizontalMaximumCenterSpacing: 320,
      sideHorizontalMaximumCenterSpacing: 330,
      sideVerticalMaximumCenterSpacing: 340,
      topBottomCrossbarMaximumCenterSpacing: 350,
      topBottomRodMaximumCenterSpacing: 360,
    };
    for (const [parameter, value] of Object.entries(values)) {
      await handleDesignerAreaAction(context, view, "tube-designer-scene-specification-change", {
        type: "number", value: String(value), dataset: { tubeDesignerParameter: parameter },
      }, ops);
    }
    const beforeSwitch = { ...products[activeId].current };
    await handleDesignerAreaAction(context, view, "tube-designer-select-instance", {
      dataset: { tubeDesignerInstanceId: "security-window-2" },
    }, ops);
    const second = {
      productId: view.scene.tubeDesigner.product.entityId,
      current: view.scene.tubeDesigner.product.parameters.horizontalMaximumCenterSpacing,
      dirty: view.tubeDesignerRightDraftDirty,
    };
    await handleDesignerAreaAction(context, view, "tube-designer-select-instance", {
      dataset: { tubeDesignerInstanceId: "security-window-1" },
    }, ops);
    const firstAgain = {
      productId: view.scene.tubeDesigner.product.entityId,
      current: view.scene.tubeDesigner.product.parameters.horizontalMaximumCenterSpacing,
      generated: products[activeId].generated.horizontalMaximumCenterSpacing,
      dirty: view.tubeDesignerRightDraftDirty,
    };

    deferParameterUpdates = true;
    const firstQueuedChange = handleDesignerAreaAction(
      context, view, "tube-designer-scene-specification-change",
      {
        type: "number", value: "310",
        dataset: { tubeDesignerParameter: "horizontalMaximumCenterSpacing" },
      },
      ops,
    );
    await Promise.resolve();
    const secondQueuedChange = handleDesignerAreaAction(
      context, view, "tube-designer-scene-specification-change",
      {
        type: "number", value: "315",
        dataset: { tubeDesignerParameter: "horizontalMaximumCenterSpacing" },
      },
      ops,
    );
    for (let index = 0;
      index < 20 && view.tubeDesignerRightDraft.horizontalMaximumCenterSpacing !== 315;
      index += 1) await new Promise((resolve) => setTimeout(resolve, 0));
    const queued = {
      draft: view.tubeDesignerRightDraft.horizontalMaximumCenterSpacing,
      editing: annotationCalls.at(-1)
        .find((item) => item.parameter === "horizontalMaximumCenterSpacing")?.editing,
      label: annotationCalls.at(-1)
        .find((item) => item.parameter === "horizontalMaximumCenterSpacing")?.label,
    };
    for (let index = 0; index < 10 && pendingParameterUpdates.length < 1; index += 1)
      await new Promise((resolve) => setTimeout(resolve, 0));
    pendingParameterUpdates.shift().complete();
    await firstQueuedChange;
    const afterStaleResponse = {
      draft: view.tubeDesignerRightDraft.horizontalMaximumCenterSpacing,
      sceneCurrent: view.scene.tubeDesigner.product.parameters.horizontalMaximumCenterSpacing,
      label: annotationCalls.at(-1)
        .find((item) => item.parameter === "horizontalMaximumCenterSpacing")?.label,
    };
    for (let index = 0; index < 10 && pendingParameterUpdates.length < 1; index += 1)
      await new Promise((resolve) => setTimeout(resolve, 0));
    pendingParameterUpdates.shift().complete();
    await secondQueuedChange;
    deferParameterUpdates = false;
    const afterLatestResponse = {
      draft: view.tubeDesignerRightDraft.horizontalMaximumCenterSpacing,
      sceneCurrent: view.scene.tubeDesigner.product.parameters.horizontalMaximumCenterSpacing,
    };

    const history = getDesignerProjectHistoryState(view);
    const undoHandled = await handleDesignerRibbonCommand(context, view, "designer.history.undo", ops);
    const callsBeforeGenerate = calls.length;
    await handleDesignerAreaAction(context, view, "tube-designer-confirm-update", { dataset: {} }, ops);
    const final = {
      calls: [...calls],
      current: products[activeId].current,
      generated: products[activeId].generated,
      member: view.scene.tubeDesigner.members[0].entityId,
      dirty: view.tubeDesignerRightDraftDirty,
      activeParts: view.scene.tubeDesigner.parts.length,
      firstInstance: view.scene.tubeDesigner.instances.find((item) => item.entityId === "security-window-1"),
      secondInstance: view.scene.tubeDesigner.instances.find((item) => item.entityId === "security-window-2"),
      annotation: annotationCalls.at(-1).find((item) => item.parameter === "horizontalMaximumCenterSpacing"),
    };
    return {
      rightSectionTitles: [...mount.querySelectorAll(
        ".tube-designer-parameter-sections > .tube-designer-parameter-section > summary > span",
      )].map((node) => node.textContent.trim()),
      initialMember,
      initialAnnotation,
      originalMetadata,
      changedMetadata,
      metadataChanged,
      editorOpened,
      annotationFocusCalls,
      callsAfterEditorOpen,
      changed,
      updateInvocationFeedback,
      reverted,
      values,
      beforeSwitch,
      activationCalls,
      second,
      firstAgain,
      queued,
      afterStaleResponse,
      afterLatestResponse,
      history,
      undoHandled,
      callsBeforeGenerate,
      final,
    };
  }, template);

  assert.deepEqual(result.rightSectionTitles, ["材料", "工艺"]);
  assert.equal(result.editorOpened.editing, true);
  assert.deepEqual(result.annotationFocusCalls, ["horizontalMaximumCenterSpacing"]);
  assert.equal(result.callsAfterEditorOpen, 1,
    "opening an annotation editor must not add another EC update");
  assert.equal(result.initialAnnotation.pending, false);
  assert.equal(result.initialAnnotation.color, 0x27c27a);
  assert.deepEqual(result.metadataChanged.call, {
    method: "TubeDesigner.UpdateProductParameters", productEntityId: "security-window-1",
  });
  assert.equal(result.metadataChanged.current, result.changedMetadata);
  assert.equal(result.metadataChanged.generated, result.originalMetadata);
  assert.equal(result.metadataChanged.member, result.initialMember);
  assert.equal(result.metadataChanged.modelDirty, false,
    "product metadata EC edits must not expire the three-dimensional model");
  assert.equal(result.metadataChanged.partsDirty, true);
  assert.equal(result.metadataChanged.status, "实例模型已是最新");
  assert.match(result.metadataChanged.partsDock, /零件清单已过期，是否重新生成/);
  assert.match(result.metadataChanged.partsDock, /tube-designer-disassemble-active-product/);

  assert.deepEqual(result.changed.calls.slice(-1), [{
    method: "TubeDesigner.UpdateProductParameters", productEntityId: "security-window-1",
  }], "one change event must create one EC parameter update");
  assert.equal(result.changed.current, 350);
  assert.equal(result.changed.sceneCurrent, 350);
  assert.equal(result.changed.generated, 400, "EC update must not regenerate geometry");
  assert.equal(result.changed.member, result.initialMember, "EC update must preserve scene members");
  assert.equal(result.changed.dirty, true);
  assert.equal(result.changed.annotation.pending, true);
  assert.equal(result.changed.annotation.color, 0xef5b55);
  assert.match(result.changed.annotation.label, /400.*350/);
  assert.match(result.changed.status, /产品实例已过期/);
  const immediateFeedback = result.updateInvocationFeedback.find((item) => item.value === 350);
  assert.match(immediateFeedback.status, /产品实例已过期/,
    "expiration feedback must be visible before the native EC update starts");
  assert.equal(immediateFeedback.annotation.pending, true);
  assert.equal(immediateFeedback.annotation.color, 0xef5b55);
  assert.match(immediateFeedback.annotation.label, /400.*350/,
    "the annotation old/new value must be painted before the native EC update starts");
  assert.ok(immediateFeedback.paintedFrame > immediateFeedback.startFrame,
    "the browser must receive a paint opportunity before the native EC update starts");

  assert.equal(result.reverted.current, 400);
  assert.equal(result.reverted.generated, 400);
  assert.equal(result.reverted.dirty, false);
  assert.equal(result.reverted.annotation.pending, false);
  assert.equal(result.reverted.annotation.color, 0x27c27a);
  assert.equal(result.reverted.status, "实例模型已是最新");

  for (const [key, value] of Object.entries(result.values)) {
    assert.equal(result.beforeSwitch[key], value, `${key} must persist before instance switching`);
  }
  assert.deepEqual(result.activationCalls, ["security-window-2", "security-window-1"]);
  assert.deepEqual(result.second, { productId: "security-window-2", current: 300, dirty: false });
  assert.deepEqual(result.firstAgain, {
    productId: "security-window-1", current: 320, generated: 400, dirty: true,
  });
  assert.equal(result.queued.draft, 315);
  assert.equal(result.queued.editing, false,
    "Enter/change must close the scene annotation editor before the EC request finishes");
  assert.match(result.queued.label, /400.*315/);
  assert.equal(result.afterStaleResponse.draft, 315,
    "the first queued response must not overwrite the second submitted value");
  assert.equal(result.afterStaleResponse.sceneCurrent, 310);
  assert.match(result.afterStaleResponse.label, /400.*315/);
  assert.deepEqual(result.afterLatestResponse, { draft: 315, sceneCurrent: 315 });

  assert.deepEqual(result.history, { canUndo: false, canRedo: false });
  assert.equal(result.undoHandled, false, "the app shell must execute native project undo");
  assert.equal(result.callsBeforeGenerate, 10,
    "each change event must be an independent EC update");
  assert.equal(result.final.calls.at(-1).method, "TubeDesigner.GeneratePreview");
  assert.equal(result.final.calls.at(-1).productEntityId, "security-window-1");
  assert.deepEqual(result.final.current, result.final.generated);
  assert.equal(result.final.member, "member-generated-1");
  assert.equal(result.final.dirty, false);
  assert.equal(result.final.activeParts, 0);
  assert.equal(result.final.firstInstance.hasDisassembly, false);
  assert.equal(result.final.firstInstance.partCount, 0);
  assert.equal(result.final.secondInstance.hasDisassembly, true);
  assert.equal(result.final.secondInstance.partCount, 1);
  assert.equal(result.final.annotation.pending, false);
  assert.equal(result.final.annotation.color, 0x27c27a);
  console.log("PASS product parameter changes persist to EC one step at a time and native history owns undo/redo");
} finally {
  await browser.close();
}
