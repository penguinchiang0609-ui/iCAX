import { buildPartDrawingPreviewRows } from "./partDrawingPreview.mjs";

// A side sketch preview owns neutral resources only. It never creates a scene
// entity and never shares the part-drawing editor's state or viewport lifetime.
const controllers = new WeakMap();
const keyFor = preview => JSON.stringify([preview?.baseGeometry, preview?.baseMaterial, preview?.baseBounds]);

function owned(controller) {
  return controllers.get(controller.mount) === controller
    && controller.view.tubeDesignerSketch === controller.state
    && controller.view.tubeDesignerSketchDialogOpen
    && controller.context.sceneProxy === controller.sceneProxy
    && controller.host.isConnected;
}

function notice(controller, message = "", error = false) {
  let node = controller.host.querySelector("[data-side-sketch-render-notice]");
  if (!message) { node?.remove(); return; }
  if (!node) {
    node = controller.host.ownerDocument.createElement("div");
    node.dataset.sideSketchRenderNotice = "";
    node.style.cssText = "position:absolute;left:8px;right:8px;bottom:8px;padding:6px;background:#13252de8;color:#dceeed;font-size:11px;pointer-events:none";
    controller.host.append(node);
  }
  node.setAttribute("role", error ? "alert" : "status");
  node.textContent = message;
}

export function attachSideSketchPreview(context, view, mount, ops = {}) {
  if (!mount?.querySelector) return Promise.resolve(false);
  const state = view.tubeDesignerSketch;
  const host = mount.querySelector("[data-side-sketch-viewport]");
  if (!view.tubeDesignerSketchDialogOpen || state?.mode !== "side" || !state.sidePreviewPayload || !host) {
    disposeSideSketchPreview(mount);
    return Promise.resolve(false);
  }
  let controller = controllers.get(mount);
  if (controller && (controller.state !== state || controller.partId !== state.targetPartId
    || controller.previewResourceKey !== state.sidePreviewResourceKey
    || controller.sceneProxy !== context.sceneProxy)) disposeSideSketchPreview(mount), controller = null;
  if (!controller) {
    controller = { mount, context, view, state, host, partId: state.targetPartId, sceneProxy: context.sceneProxy,
      previewResourceKey: state.sidePreviewResourceKey, viewport: null, key: null, desiredKey: null, sequence: 0, flight: null };
    controllers.set(mount, controller);
  }
  controller.context = context;
  if (controller.host !== host) {
    controller.host = host;
    controller.viewport?.mount(host);
  }
  const preview = state.sidePreview, key = keyFor(preview);
  if (!preview?.baseGeometry?.url) {
    notice(controller, "点击预览三维查看实际切割结果");
    return Promise.resolve(false);
  }
  if (controller.flight && controller.desiredKey === key) return controller.flight;
  // The viewport observes its host size. Reusing accepted resources must not
  // force another layout read and GPU render after an unrelated sketch edit.
  if (controller.key === key) return Promise.resolve(true);
  controller.desiredKey = key;
  const sequence = ++controller.sequence;
  const current = () => owned(controller) && controller.sequence === sequence;
  notice(controller, "正在显示三维预览…");
  controller.flight = Promise.resolve().then(async () => {
    if (!controller.viewport) {
      const create = ops.createSideSketchViewport
        ?? (await import("../../../iCAX-UI/SDK/Viewport/threeViewport.mjs")).createThreeViewport;
      if (!current()) return false;
      const viewport = await create({ backgroundColor: 0x13252d, continuousRender: false,
        constrainOrbit: false, projectionMode: "orthographic", showProjectionToggle: true,
        pickingEnabled: false, blankDoubleClickFitEnabled: true, antialias: true, pixelRatioCap: 2 });
      if (!current()) { viewport?.dispose(); return false; }
      controller.viewport = viewport;
      viewport.mount(controller.host);
      viewport.setStandardView("iso");
    }
    const rows = buildPartDrawingPreviewRows(preview);
    const references = rows.flatMap(row => [row.data.geometry, row.data.material]).filter(Boolean);
    const receipt = await controller.viewport.applyViewSnapshot({ revision: `side-sketch:${sequence}`, rows }, controller.sceneProxy.resources);
    if (!current()) return false;
    if (!receipt?.applied || receipt.missingGeometryEntityIds?.length
      || rows.some(row => !receipt.entityIds?.includes(row.entityId))) throw new Error("三维预览资源未完整显示。");
    controller.viewport.retainViewResources?.(references);
    // Preserve live camera changes while a new preview is loading. Only the
    // first accepted preview needs a fit.
    if (!controller.key) controller.viewport.fitViewToViewport(1.16);
    controller.viewport.setVisibleEntityIds?.(rows.map(row => row.entityId));
    controller.key = key;
    controller.host.dataset.sideSketchPreviewReady = "true";
    notice(controller);
    return true;
  }).catch(error => {
    if (current()) {
      delete controller.host.dataset.sideSketchPreviewReady;
      notice(controller, error?.message ?? String(error), true);
    }
    return false;
  }).finally(() => {
    if (current()) controller.flight = null;
  });
  return controller.flight;
}

export async function waitForSideSketchPreview(mount) {
  let controller;
  while ((controller = controllers.get(mount))?.flight) await controller.flight;
  return !!controller?.key && controller.key === controller.desiredKey;
}

export function disposeSideSketchPreview(mount) {
  const controller = mount && controllers.get(mount);
  if (!controller) return;
  controllers.delete(mount);
  controller.sequence++;
  controller.viewport?.renderer?.forceContextLoss?.();
  controller.viewport?.axisRenderer?.forceContextLoss?.();
  controller.viewport?.dispose();
  if (controller.state.sidePreviewResourceKey === controller.previewResourceKey)
    releaseSideSketchPreview({ sceneProxy: controller.sceneProxy }, controller.state);
  else releasePreviewKey(controller.sceneProxy, controller.previewResourceKey);
}

export function releaseSideSketchPreview(context, state) {
  const previewResourceKey = state?.sidePreviewResourceKey;
  if (!previewResourceKey) return;
  state.sidePreviewResourceKey = null;
  releasePreviewKey(context.sceneProxy, previewResourceKey);
}

function releasePreviewKey(sceneProxy, previewResourceKey) {
  if (!previewResourceKey) return;
  // Releasing transient presentation resources must not block cancel or save.
  try {
    const request = sceneProxy?.invoke?.("TubeDesigner.ReleaseNestingSideSketchPreview", { previewResourceKey });
    Promise.resolve(request).catch(() => {});
  } catch {}
}
