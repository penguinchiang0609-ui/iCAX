import { attachViewCube, renderViewCube, stopViewCubeAnimation } from "../../_shared/workbench/viewport/viewCube.mjs";

// A drawing owns its viewport and resource lifetime. It never subscribes to the
// main scene, invokes a modelling command, or refreshes the project workbench.
const controllers = new WeakMap();
const emptyResources = { get: async () => { throw new Error("No drawing resources are available"); } };
const frame = () => typeof requestAnimationFrame === "function"
  ? new Promise(resolve => requestAnimationFrame(resolve)) : Promise.resolve();
const referenceKey = reference => reference?.url
  ? [String(reference.url), String(reference.version ?? 0)] : null;
const rowKey = row => JSON.stringify([row.entityId, referenceKey(row.data.geometry),
  referenceKey(row.data.material), row.data.localToWorldMatrix]);
const rowsKey = rows => JSON.stringify(rows.map(rowKey));
const references = rows => rows.flatMap(row => [row.data.geometry, row.data.material]).filter(Boolean);
const drawingCoverage = 0.9;

function fitDrawingBase(viewport, preview) {
  const bounds = preview?.baseBounds;
  const half = [0, 1, 2].map(axis => (Number(bounds?.max?.[axis]) - Number(bounds?.min?.[axis])) / 2);
  const camera = viewport.getCameraState?.();
  if (!half.every(value => Number.isFinite(value) && value >= 0) || !half.some(value => value > 0)
    || !camera || ![camera.theta, camera.phi].every(Number.isFinite) || !viewport.setCameraState)
    return viewport.fitViewToViewport(1 / drawingCoverage);
  const theta = camera.theta, phi = camera.phi;
  // Projection of the centred stock bounds onto the current camera basis. Its
  // tool operands never inflate the stock framing, even when they extend far out.
  const right = [-Math.sin(theta), Math.cos(theta), 0];
  const up = [-Math.cos(theta) * Math.cos(phi), -Math.sin(theta) * Math.cos(phi), Math.sin(phi)];
  const toward = [Math.cos(theta) * Math.sin(phi), Math.sin(theta) * Math.sin(phi), Math.cos(phi)];
  const extent = direction => half.reduce((sum, value, axis) => sum + value * Math.abs(direction[axis]), 0);
  const aspect = Math.max(0.01, Number(viewport.viewportAspect) || 1);
  const tanHalfFov = Math.tan((Number(viewport.perspectiveFov) || 45) * Math.PI / 360);
  const planarRadius = Math.max(extent(right) / aspect, extent(up)) / (tanHalfFov * drawingCoverage);
  // Looking down the tube after a user rotation must not put the camera inside
  // the stock. In the default broadside view the width calculation dominates.
  const radius = camera.projectionMode === "orthographic"
    ? Math.max(planarRadius, extent(toward) * 1.001) : planarRadius + extent(toward);
  // Keep the current direction/projection for a resized stock or a manual fit.
  // The centred row transforms place the stock centre at the origin.
  if (viewport.camera) {
    viewport.camera.near = Math.max(0.001, radius / 10000);
    viewport.camera.far = Math.max(1000, radius * 20, radius + extent(toward) * 2);
  }
  viewport.setCameraState({ ...camera, target: { x: 0, y: 0, z: 0 }, radius });
  return true;
}

function fitAcceptedPresentation(controller, presentation) {
  if (controller.ready && controller.boundsKey === presentation.boundsKey) return;
  if (!controller.ready) {
    controller.viewport.setProjectionMode?.("orthographic");
    controller.viewport.setStandardView("front");
  }
  if (fitDrawingBase(controller.viewport, presentation.preview) === false) throw new Error("当前模型无法适合窗口。");
  controller.boundsKey = presentation.boundsKey; controller.ready = true;
}

function centeredMatrix(preview) {
  const bounds = preview.baseBounds ?? {};
  const center = [0, 1, 2].map(index => {
    const min = Number(bounds.min?.[index]), max = Number(bounds.max?.[index]);
    if (Number.isFinite(min) && Number.isFinite(max)) return (min + max) / 2;
    return index === 0 ? Math.max(0, Number(bounds.width ?? preview.length) || 0) / 2 : 0;
  });
  return [1, 0, 0, -center[0], 0, 1, 0, -center[1], 0, 0, 1, -center[2], 0, 0, 0, 1];
}

export function buildPartDrawingPreviewRows(preview = {}) {
  if (!preview.baseGeometry?.url) return [];
  const localToWorldMatrix = centeredMatrix(preview);
  const row = (entityId, geometry, material, renderClass) => ({ entityId, data: {
    geometry, material, geometryKind: 1, renderClass, visible: true, selectable: false, localToWorldMatrix,
  } });
  return [row("drawing:base", preview.baseGeometry, preview.baseMaterial, 1),
    ...(preview.toolPreviews ?? []).filter(tool => tool.geometry?.url).map(tool =>
      row(`drawing:${tool.target}:${tool.key}`, tool.geometry, preview.toolMaterial, 5))];
}

function isCurrentPreview(state) {
  return !!state.preview?.baseGeometry?.url && state.preview.revision === state.revision;
}

const baseRecipeKey = recipe => JSON.stringify([recipe?.partEntityId, recipe?.resourceId,
  recipe?.resourceVersion, recipe?.profileRef, recipe?.parameters, recipe?.length,
  recipe?.baseLength, recipe?.drawing]);

function unchangedRows(controller, state, drawing) {
  const previous = controller.recipe, next = state.pendingPreviewRecipe;
  if (previous && next) {
    if (baseRecipeKey(previous) !== baseRecipeKey(next)) return [];
    return controller.rows.filter(row => {
      if (row.entityId === "drawing:base") return true;
      const tool = controller.tools.get(row.entityId);
      if (!tool) return false;
      const find = recipe => tool.target === "end" ? recipe.ends?.[tool.key]
        : recipe.features?.find((feature, index) => String(feature.id ?? index) === String(tool.key));
      const old = find(previous), current = find(next);
      return old != null && current != null && JSON.stringify(old) === JSON.stringify(current);
    });
  }
  // Before a valid pending recipe exists (for example a half-entered number),
  // only hide the currently edited operand. The remaining model is still useful.
  if (drawing.mode === "main") return [];
  const dirtyId = state.editingId || drawing.selected;
  return controller.rows.filter(row => {
    if (row.entityId === "drawing:base") return true;
    const tool = controller.tools.get(row.entityId);
    return tool && !(tool.target === "end" && tool.key === drawing.mode)
      && String(tool.key) !== String(dirtyId);
  });
}

function desiredPresentation(controller) {
  const { state, drawing } = controller;
  const accepted = isCurrentPreview(state);
  const rows = accepted ? buildPartDrawingPreviewRows(state.preview) : unchangedRows(controller, state, drawing);
  return { rows, key: rowsKey(rows), accepted, preview: state.preview,
    boundsKey: JSON.stringify(state.preview?.baseBounds ?? { length: state.preview?.length }),
    recipe: accepted ? state.previewRecipe : controller.recipe };
}

function updateStatus(controller) {
  const { host, state, desired } = controller;
  if (!host) return;
  const loading = !!state.previewPending || !!state.previewRenderPending;
  const error = state.previewRenderError || (state.previewComputeError?.revision === state.revision
    ? state.previewComputeError.message : "");
  const message = error ? `当前特征预览失败：${error}` : state.previewRenderPending
    ? "正在加载并显示当前特征…" : state.previewPending ? String(state.previewPhase || "正在更新当前特征…")
    : desired?.accepted ? "" : state.preview ? "当前特征等待更新；其余模型保持显示。" : "选择主管截面后显示三维模型";
  let status = host.querySelector("[data-part-drawing-preview-notice]");
  if (!message && !loading) {
    status?.remove();
  } else {
    if (!status) {
      status = host.ownerDocument.createElement("div");
      status.dataset.partDrawingPreviewNotice = "";
      status.className = "td-draw-preview-status";
      host.append(status);
    }
    status.setAttribute("role", error ? "alert" : "status");
    status.replaceChildren();
    const label = host.ownerDocument.createElement("span");
    label.textContent = message; status.append(label);
    if (loading) {
      const progress = host.ownerDocument.createElement("div");
      progress.className = "td-draw-preview-progress is-indeterminate";
      progress.setAttribute("role", "progressbar");
      progress.setAttribute("aria-label", "三维特征预览进度");
      progress.setAttribute("aria-valuetext", message);
      progress.append(host.ownerDocument.createElement("i")); status.append(progress);
    }
    if (state.previewRenderError && !loading) {
      const retry = host.ownerDocument.createElement("button");
      retry.type = "button"; retry.textContent = "重试显示";
      retry.dataset.partDrawingPreviewRetry = ""; retry.style.pointerEvents = "auto";
      retry.addEventListener("click", event => {
        event.preventDefault(); event.stopPropagation();
        if (!stillOwned(controller)) return;
        controller.failedKey = null; state.previewRenderError = "";
        void attachPartDrawingPreview(controller.context, controller.view, controller.mount, controller.ops);
      });
      status.append(retry);
    }
  }
  host.setAttribute("aria-busy", String(loading));
  if (desired?.accepted && controller.displayKey === desired.key && !state.previewRenderPending && !error)
    host.dataset.partDrawingPreviewReady = "true";
  else delete host.dataset.partDrawingPreviewReady;
}

function mountController(controller, host, mount) {
  const viewport = controller.viewport;
  if (controller.host === host && (!viewport || viewport.root?.parentElement === host)) return;
  stopViewCubeAnimation(controller.cubeView);
  if (controller.host && controller.cubeClick) controller.host.removeEventListener("click", controller.cubeClick, true);
  controller.host = host;
  if (!viewport) return;
  viewport.mount(host);
  if (typeof window !== "undefined") {
    host.querySelector("[data-cam-viewcube]")?.remove();
    host.insertAdjacentHTML("beforeend", renderViewCube());
    controller.cubeView = { viewport }; attachViewCube(controller.cubeView, host);
  }
  controller.cubeClick = event => {
    const target = event.target?.closest?.('[data-cam-viewcube] [data-cam-action="view-standard"]');
    if (!target || !host.contains(target)) return;
    event.preventDefault(); event.stopPropagation(); event.stopImmediatePropagation?.();
    viewport.setStandardView(String(target.dataset.camView ?? "iso"));
  };
  host.addEventListener("click", controller.cubeClick, true);
  host.querySelector("[data-part-drawing-preview-controls]")?.remove();
  const nav = host.ownerDocument.createElement("nav");
  nav.dataset.partDrawingPreviewControls = ""; nav.className = "td-draw-view-controls";
  nav.setAttribute("aria-label", "三维观察方向");
  const fit = host.ownerDocument.createElement("button"); fit.type = "button"; fit.textContent = "适合窗口";
  fit.addEventListener("click", event => { event.stopPropagation(); setPartDrawingPreviewView(mount, "fit"); });
  nav.append(fit); host.append(nav);
}

function stillOwned(controller) {
  return controllers.get(controller.mount) === controller
    && controller.view.tubeDesignerPartDrawing === controller.drawing
    && controller.drawing.state === controller.state;
}

async function ensureViewport(controller) {
  if (controller.viewport) return controller.viewport;
  if (!controller.viewportPromise) controller.viewportPromise = Promise.resolve().then(async () => {
    const create = controller.ops?.createPartDrawingViewport
      ?? (await import("../../../iCAX-UI/SDK/Viewport/threeViewport.mjs")).createThreeViewport;
    if (!stillOwned(controller)) return null;
    const viewport = await create({ backgroundColor: 0x13252d, continuousRender: false, constrainOrbit: false, projectionMode: "orthographic",
      showProjectionToggle: true, pickingEnabled: false, antialias: true, pixelRatioCap: 2 });
    if (!stillOwned(controller)) { viewport?.dispose(); return null; }
    controller.viewport = viewport; mountController(controller, controller.host, controller.mount);
    updateStatus(controller);
    return viewport;
  }).finally(() => { controller.viewportPromise = null; });
  return controller.viewportPromise;
}

function keepUnchangedVisible(controller, desired) {
  if (!controller.viewport) return;
  const old = new Map(controller.rows.map(row => [row.entityId, rowKey(row)]));
  controller.viewport.setVisibleEntityIds(desired.rows.filter(row => old.get(row.entityId) === rowKey(row)).map(row => row.entityId));
}

export function attachPartDrawingPreview(context, view, mount, ops = {}) {
  if (!mount || (typeof mount !== "object" && typeof mount !== "function")) return Promise.resolve(false);
  const drawing = view.tubeDesignerPartDrawing, state = drawing?.state;
  const host = mount.querySelector?.("[data-part-drawing-viewport]");
  let controller = controllers.get(mount);
  if (!drawing || !state || !host) { disposePartDrawingPreview(mount); return Promise.resolve(false); }
  if (controller?.state !== state || controller?.drawing !== drawing) {
    disposePartDrawingPreview(mount);
    controller = { mount, context, view, drawing, state, ops, host: null, viewport: null, viewportPromise: null,
      ready: false, sequence: 0, flight: null, requestKey: null, displayKey: null, rows: [], tools: new Map(), recipe: null };
    controllers.set(mount, controller);
  }
  controller.context = context; controller.ops = ops;
  mountController(controller, host, mount);
  const desired = desiredPresentation(controller); controller.desired = desired;
  keepUnchangedVisible(controller, desired);
  if (controller.flight && controller.requestKey === desired.key) { updateStatus(controller); return controller.flight; }
  if (!controller.flight && controller.displayKey === desired.key) {
    controller.viewport?.setVisibleEntityIds(desired.rows.map(row => row.entityId));
    if (desired.accepted) {
      controller.recipe = desired.recipe ? structuredClone(desired.recipe) : null;
      controller.tools = new Map((desired.preview?.toolPreviews ?? []).map(tool => [`drawing:${tool.target}:${tool.key}`, tool]));
      fitAcceptedPresentation(controller, desired);
    }
    updateStatus(controller); return Promise.resolve(desired.accepted);
  }
  if (controller.failedKey === desired.key && state.previewRenderError) {
    updateStatus(controller); return Promise.resolve(false);
  }
  if (!desired.rows.length && !controller.viewport && !controller.flight) {
    updateStatus(controller); return Promise.resolve(false);
  }
  const sequence = ++controller.sequence;
  const current = () => stillOwned(controller) && controller.sequence === sequence;
  controller.requestKey = desired.key;
  state.previewRenderPending = true; state.previewRenderError = "";
  controller.flight = Promise.resolve().then(async () => {
    const viewport = await ensureViewport(controller);
    if (!viewport || !current()) return false;
    // A new snapshot also invalidates an older in-flight resource load. When the
    // user edits again before it arrives, only the unchanged operands survive.
    viewport.retainViewResources?.(references([...controller.rows, ...desired.rows]));
    const receipt = await viewport.applyViewSnapshot({ revision: `drawing:${sequence}`, rows: desired.rows },
      context.sceneProxy?.resources ?? emptyResources);
    if (!current()) return false;
    const ids = new Set(receipt?.entityIds ?? []);
    if (!receipt?.applied || receipt.missingGeometryEntityIds?.length || desired.rows.some(row => !ids.has(row.entityId)))
      throw new Error("主管或特征资源未完整进入视口。");
    controller.rows = desired.rows; controller.displayKey = desired.key; controller.failedKey = null;
    // The latest attach may have accepted the same references for a newer recipe.
    const latest = controller.desired;
    if (latest.accepted) {
      controller.recipe = latest.recipe ? structuredClone(latest.recipe) : null;
      controller.tools = new Map((latest.preview?.toolPreviews ?? []).map(tool => [`drawing:${tool.target}:${tool.key}`, tool]));
      fitAcceptedPresentation(controller, latest);
    }
    // Do not restore a camera captured before loading: the user may have orbited
    // in the meantime. Ordinary operand updates never change the live camera.
    viewport.setVisibleEntityIds(desired.rows.map(row => row.entityId));
    viewport.retainViewResources?.(references(desired.rows));
    await frame();
    return current() && controller.desired.accepted;
  }).catch(error => {
    if (current()) {
      controller.failedKey = desired.key;
      state.previewRenderError = error?.message ?? String(error);
      keepUnchangedVisible(controller, controller.desired);
    }
    return false;
  }).finally(() => {
    if (!current()) return;
    controller.flight = null; controller.requestKey = null; state.previewRenderPending = false;
    updateStatus(controller);
  });
  updateStatus(controller);
  return controller.flight;
}

export async function waitForPartDrawingPreview(mount) {
  let controller;
  while ((controller = controllers.get(mount))?.flight) await controller.flight;
  return !!controller?.desired?.accepted && !controller.state.previewRenderError
    && controller.displayKey === controller.desired.key;
}

export function setPartDrawingPreviewView(mount, name) {
  const controller = controllers.get(mount), viewport = controller?.viewport;
  if (!viewport) return false;
  if (name === "fit") fitDrawingBase(viewport, controller.desired?.preview);
  else if (["iso", "top", "bottom", "front", "back", "left", "right"].includes(name)) viewport.setStandardView(name);
  else return false;
  return true;
}

export function disposePartDrawingPreview(mount) {
  const controller = controllers.get(mount);
  if (!controller) return;
  controllers.delete(mount); controller.sequence++; controller.state.previewRenderPending = false;
  stopViewCubeAnimation(controller.cubeView);
  if (controller.host && controller.cubeClick) controller.host.removeEventListener("click", controller.cubeClick, true);
  controller.host?.querySelector("[data-part-drawing-preview-notice]")?.remove();
  controller.host?.querySelector("[data-part-drawing-preview-controls]")?.remove();
  controller.viewport?.renderer?.forceContextLoss?.();
  controller.viewport?.axisRenderer?.forceContextLoss?.();
  controller.viewport?.dispose();
}
