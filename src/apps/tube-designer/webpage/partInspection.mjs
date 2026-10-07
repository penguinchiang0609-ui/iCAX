import { createThreeViewport } from "../../../iCAX-UI/SDK/Viewport/threeViewport.mjs";
import { attachViewCube, renderViewCube, stopViewCubeAnimation } from "../../_shared/workbench/viewport/viewCube.mjs";
import { attachPartInspectionWindow, disposePartInspectionWindow } from "./partInspectionWindow.mjs";
import { partDimensionCategories as dimensionCategories, annotatedPartElementIds as annotatedElementIds,
  renderPartDimensionVisibilityIcon as renderVisibilityIcon, visiblePartDimensionAnnotations,
  nearestPartDimensionElement, partDimensionVisibilityState, partDimensionElementVisible,
  setPartDimensionMasterVisibility, setPartDimensionCategoryVisibility,
  setPartDimensionElementVisibility } from "./partDimensionAnnotations.mjs";

export const PART_INSPECTION_PROGRESS_MINIMUM_VISIBLE_MS = 500;

const controllers = new WeakMap();

export function scheduleDesignerPartInspectionHydration(context, designer, view) {
  const inspectedPartId = view.tubeDesignerPartInspectionOpen
    ? String(view.tubeDesignerInspectedPartId ?? "")
    : "";
  const part = inspectedPartId ? findPart(designer, inspectedPartId, view) : null;
  if (part) attachPartInspectionWindow(context, view);
  else disposePartInspectionWindow(context);
  view.viewport?.setContinuousRendering?.(!part && !view.tubeDesignerAddDialogOpen);
  queueMicrotask(() => {
    void hydrateInspection(context, part);
  });
}

export function disposeDesignerPartInspection(context, { preserveWindow = false } = {}) {
  if (!preserveWindow) disposePartInspectionWindow(context);
  const controller = getController(context);
  if (!controller) return;
  controllers.delete(context.mount);
  disposeInspection(controller);
}

export function fitDesignerInspectedPart(context) {
  const controller = getController(context);
  if (!controller) return false;
  applyHorizontalPartPresentation(controller);
  return Boolean(controller.viewport.fitViewToViewport(1.16));
}

export function setDesignerInspectedPartView(context) {
  const controller = getController(context);
  if (!controller) return false;
  const oriented = applyHorizontalPartPresentation(controller);
  if (oriented) controller.viewport.fitViewToViewport(1.16);
  return oriented;
}

export function buildAutomaticDimensionReport(part) {
  const candidate = part?.geometryMeasurement ?? {};
  const measurement = candidate?.available === true
    && String(candidate?.source ?? "") === "final-brep"
    ? candidate
    : {};
  if (measurement.partKind === "accessory") {
    const bounds = Object.fromEntries(["width", "depth", "height"].map((key) => [key, finiteNumber(measurement.bounds?.[key]) ?? 0]));
    return Object.freeze({ partKind: "accessory", length: 0, bounds,
      profile: `配件 ${Object.values(bounds).map(formatMillimeters).join(" × ")} mm`,
      holes: [], pitches: [], annotations: [], hasLinearReference: false,
      source: "final-brep", axis: null, offsetDirection: null });
  }
  if (["plate", "glass"].includes(measurement.partKind) && measurement.plate) {
    const plate = measurement.plate;
    const center = finitePoint(plate.center);
    const axes = (plate.axes ?? []).map(finitePoint);
    const dimensions = [plate.longSide, plate.shortSide, plate.thickness].map(finiteNumber);
    const valid = center && axes.length === 3 && axes.every(Boolean)
      && dimensions.every((size) => size > 0);
    const annotations = [];
    if (valid) {
      const origin = axes.reduce((point, direction, index) => add(point, scale(direction, -dimensions[index] / 2)), center);
      for (let index = 0; index < 3; index += 1) {
        annotations.push({ id: `overall:${index}`, category: "overall", elementIds: [], start: origin, end: add(origin, scale(axes[index], dimensions[index])),
          offset: scale(axes[index === 0 ? 1 : 0], -Math.max(12, dimensions[1] * 0.13)),
          label: `${["长边", "短边", measurement.partKind === "glass" ? "玻璃厚度" : "板厚"][index]} ${formatMillimeters(dimensions[index])} mm`, color: "#ffc43d" });
      }
    }
    const referenceStart = finitePoint(measurement.linearReference?.start);
    const referenceEnd = finitePoint(measurement.linearReference?.end);
    const referenceVector = referenceStart && referenceEnd ? subtract(referenceEnd, referenceStart) : null;
    const referenceLength = referenceVector ? magnitude(referenceVector) : 0;
    const referenceAxis = referenceLength > 1e-7 ? scale(referenceVector, 1 / referenceLength) : null;
    const holes = (measurement.features ?? []).map((feature, index) =>
      normalizeHoleFeature(feature, index, referenceStart, referenceAxis, referenceLength)).filter(Boolean);
    return Object.freeze({ partKind: measurement.partKind, length: dimensions[0] ?? 0,
      profile: `${measurement.partKind === "glass" ? "玻璃" : "板件"} ${dimensions.map((size) => formatMillimeters(size ?? 0)).join(" × ")} mm`,
      plate, holes, pitches: [], annotations, hasLinearReference: Boolean(valid),
      source: "final-brep", axis: axes[0] ?? null, offsetDirection: axes[1] ?? null });
  }
  const reference = measurement.linearReference ?? {};
  const start = finitePoint(reference.start);
  const end = finitePoint(reference.end);
  const referenceVector = start && end ? subtract(end, start) : null;
  const referenceLength = referenceVector ? magnitude(referenceVector) : 0;
  const axis = referenceVector && referenceLength > 1.0e-7
    ? scale(referenceVector, 1 / referenceLength)
    : null;
  const length = finiteNumber(measurement.length) ?? referenceLength;
  const section = measurement.section ?? {};
  const profileSpan = Math.max(
    8,
    finiteNumber(section.width) ?? 0,
    finiteNumber(section.height) ?? 0,
  );
  const offsetDirection = axis
    ? perpendicularDirection(
      finitePoint(reference.dimensionOffsetDirection)
        ?? finitePoint(reference.sectionAxes?.[0]),
      axis,
    )
    : null;
  const rawFeatures = Array.isArray(measurement.features)
    ? measurement.features
    : [];
  const holes = rawFeatures
    .filter((feature) => ["through-opening", "throughHole", "side-opening"].includes(String(feature?.kind ?? "")))
    .map((feature, index) => normalizeHoleFeature(feature, index, start, axis, referenceLength))
    .filter(Boolean)
    .sort((left, right) => left.station - right.station)
    .map((hole, index) => ({ ...hole, index: index + 1, elementId: `hole:${index + 1}` }));
  const pitches = holes.slice(1).map((hole, index) => {
    const previous = holes[index];
    return {
      from: previous.index,
      to: hole.index,
      centerDistance: Math.max(0, hole.station - previous.station),
      edgeClearance: Math.max(
        0,
        hole.station - previous.station - previous.halfSpanAlong - hole.halfSpanAlong,
      ),
    };
  });
  const annotations = start && end && axis && offsetDirection
    ? buildDimensionAnnotations({ start, end, length: referenceLength, axis, offsetDirection, profileSpan, holes })
    : [];
  return Object.freeze({
    length,
    profile: formatMeasuredProfile(section),
    holes,
    pitches,
    annotations,
    sideProjection: normalizeSideProjection(measurement.sideProjection, length, section),
    unfolding: measurement.unfolding ?? null,
    hasLinearReference: Boolean(measurement.available && start && end && axis),
    source: String(measurement.source ?? ""),
    axis,
    offsetDirection,
  });
}

async function hydrateInspection(context, part) {
  const mount = context.mount;
  if (!mount || (typeof mount !== "object" && typeof mount !== "function")) return;
  const host = mount.querySelector?.("[data-tube-designer-part-inspection-viewport]") ?? null;
  const previous = controllers.get(mount);
  if (!host || !part) {
    disposeInspection(previous);
    controllers.delete(mount);
    return;
  }

  const resourceId = String(part.thumbnailGeometryResourceId ?? "").trim();
  const resourceVersion = String(part.thumbnailGeometryResourceVersion ?? "0");
  const manufacturingResourceId = String(part.manufacturingGeometryResourceId ?? "").trim();
  const manufacturingResourceVersion = Number(part.manufacturingGeometryResourceVersion ?? 0);
  const partKey = `${String(part.entityId)}@${resourceId}@${resourceVersion}`
    + `@${manufacturingResourceId}@${manufacturingResourceVersion}`;
  if (previous?.host === host && previous.partKey === partKey) return;
  disposeInspection(previous);

  const controller = {
    host,
    partKey,
    automaticDimensionsVisible: true,
    treeCollapsed: false,
    hiddenCategories: new Set(),
    hiddenElementIds: new Set(),
    shownElementIds: new Set(),
    selectedElementId: "",
    dimensionReport: null,
    viewport: null,
  };
  controller.viewport = createThreeViewport({
    backgroundColor: 0x13252d,
    continuousRender: false,
    constrainOrbit: false,
    showProjectionToggle: true,
    pickingEnabled: false,
    blankDoubleClickFitEnabled: true,
    antialias: true,
    pixelRatioCap: 2,
  });
  controller.viewport.mount(host);
  attachInspectionControls(controller);
  host.dataset.tubeInspectionReady = "false";
  host.dataset.tubeInspectionEntityCount = "0";
  host.dataset.tubeInspectionDimensionCount = "0";
  host.dataset.tubeInspectionRenderMode = "on-demand";
  host.dataset.tubeInspectionPickingEnabled = "false";
  controllers.set(mount, controller);
  renderAutomaticDimensionState(controller);
  setInspectionStatus(controller, "正在载入最终零件三维资源…", false);
  setInspectionProgress(controller, "正在载入最终零件三维资源", true);
  const progressPaintedAt = waitForProgressPaint();

  if (!resourceId) {
    if (!await finishInspectionProgress(mount, controller, progressPaintedAt)) return;
    setInspectionStatus(controller, "该零件没有可读取的三维资源。", true);
    return;
  }
  try {
    const revision = `part-inspection:${String(part.entityId)}:${resourceVersion}`;
    const receipt = await controller.viewport.applyViewSnapshot({
      revision,
      rows: [{
        entityId: String(part.entityId),
        data: {
          geometry: { url: resourceId, version: Number(resourceVersion) },
          geometryKind: 1,
          renderClass: 1,
          visible: true,
          selectable: true,
        },
      }],
    }, context.sceneProxy?.resources);
    if (controllers.get(mount) !== controller || !host.isConnected) return;
    if (!receipt?.applied || !receipt.entityIds?.length) {
      throw new Error("零件三维资源没有进入复尺视口。");
    }
    controller.viewport.fitViewForRevision(revision, 1.3);
    controller.viewport.setStandardView("iso");
    host.dataset.tubeInspectionReady = "true";
    host.dataset.tubeInspectionEntityCount = String(receipt.entityIds.length);
    setInspectionStatus(controller, "正在从最终零件几何提取尺寸…", false);
    setInspectionProgress(controller, "正在分析最终零件几何并生成自动尺寸", true);
    const measurementStartedAt = performance.now();
    const geometryMeasurement = await context.sceneProxy?.invoke(
      "TubeDesigner.MeasurePartGeometry",
      {
        partEntityId: String(part.entityId),
        resourceVersion: manufacturingResourceVersion,
      },
      { timeoutMs: 120000 },
    );
    if (controllers.get(mount) !== controller || !host.isConnected) return;
    if (!geometryMeasurement?.available) {
      throw new Error(geometryMeasurement?.message ?? "无法从最终零件几何提取自动尺寸。");
    }
    if (String(geometryMeasurement.source ?? "") !== "final-brep"
      || String(geometryMeasurement.resourceId ?? "") !== manufacturingResourceId
      || Number(geometryMeasurement.resourceVersion ?? 0) !== manufacturingResourceVersion) {
      throw new Error("复尺结果与当前最终几何版本不一致，请重新打开零件。");
    }
    controller.dimensionReport = buildAutomaticDimensionReport({
      geometryMeasurement,
    });
    updateInspectionAnnotations(controller);
    applyHorizontalPartPresentation(controller);
    controller.viewport.fitViewToViewport(1.16);
    const viewportState = controller.viewport.getDebugState();
    const dimensionCount = Number(viewportState.dimensionAnnotationCount ?? 0);
    host.dataset.tubeInspectionDimensionCount = String(dimensionCount);
    host.dataset.tubeInspectionMeasurementMs = String(
      Math.max(0, Math.round(performance.now() - measurementStartedAt)),
    );
    if (controller.dimensionReport.hasLinearReference
        && visibleInspectionAnnotations(controller).length > 0 && dimensionCount < 1) {
      throw new Error("自动尺寸已经生成，但未能进入三维复尺场景。");
    }
    renderAutomaticDimensionState(controller);
    if (!await finishInspectionProgress(mount, controller, progressPaintedAt)) return;
    setInspectionStatus(controller, "", false);
  } catch (error) {
    if (!await finishInspectionProgress(mount, controller, progressPaintedAt)) return;
    setInspectionStatus(controller, error?.message ?? String(error), true);
  }
}

function getController(context) {
  const mount = context.mount;
  return mount && (typeof mount === "object" || typeof mount === "function")
    ? controllers.get(mount) ?? null
    : null;
}

function applyHorizontalPartPresentation(controller) {
  const axis = controller.dimensionReport?.axis;
  const oriented = Array.isArray(axis)
    && controller.viewport.setPresentationAxis(axis, [1, 0, 0]);
  controller.viewport.setStandardView("top-front");
  return Boolean(oriented);
}

function findPart(designer, partId, view = {}) {
  const groups = view?.activeAreaId === "nesting"
    && view?.tubeDesignerBreakdownMode === "nesting-export"
    && Array.isArray(designer?.nestingGroups)
    ? designer.nestingGroups
    : (designer?.manufacturingGroups ?? []);
  for (const group of groups) {
    const part = (group.parts ?? []).find((item) => String(item.entityId) === partId);
    if (part) return part;
  }
  return null;
}

function renderAutomaticDimensionState(controller) {
  const dialog = controller.host.closest(".tube-designer-part-inspection-dialog");
  if (!dialog) return;
  const tree = dialog.querySelector("[data-tube-inspection-dimension-tree]");
  const collapse = tree?.querySelector("[data-tube-inspection-tree-collapse]");
  if (tree && collapse) {
    tree.classList.toggle("is-collapsed", controller.treeCollapsed);
    collapse.classList.toggle("is-collapsed", controller.treeCollapsed);
    collapse.setAttribute("aria-expanded", String(!controller.treeCollapsed));
    const label = controller.treeCollapsed ? "展开标尺" : "收起标尺到左侧";
    collapse.setAttribute("aria-label", label);
    collapse.title = label;
    collapse.querySelector("[data-tube-inspection-tree-collapse-icon]").textContent = controller.treeCollapsed ? "›" : "‹";
    collapse.querySelector("[data-tube-inspection-tree-collapse-label]").hidden = !controller.treeCollapsed;
  }
  const master = dialog.querySelector("[data-tube-inspection-all-dimensions]");
  if (master) {
    const visibility = partDimensionVisibilityState(controller);
    master.checked = visibility.total ? visibility.checked : controller.automaticDimensionsVisible;
    master.indeterminate = visibility.mixed;
  }
  const count = dialog.querySelector("[data-tube-inspection-visible-count]");
  if (count) count.textContent = controller.dimensionReport
    ? `已显示 ${controller.host.dataset.tubeInspectionDimensionCount ?? "0"} 项` : "正在载入";
  const result = dialog.querySelector("[data-tube-designer-automatic-dimensions]");
  if (!result) return;
  const report = controller.dimensionReport;
  if (!report) return;
  if (controller.renderedDimensionReport !== report) {
    controller.renderedDimensionReport = report;
    const categories = dialog.querySelector("[data-tube-inspection-dimension-categories]");
    if (categories) categories.innerHTML = dimensionCategories.map(([key, label]) => {
      const total = report.annotations.filter((annotation) => annotation.category === key).length;
      return total ? `<label><input type="checkbox" data-tube-inspection-dimension-category="${key}" checked /><span>${label}</span><small>${total}</small></label>` : "";
    }).join("");
    if (report.partKind === "accessory") {
      result.innerHTML = `<div class="tube-designer-inspection-summary"><div>${[["width", "宽 X"], ["depth", "深 Y"], ["height", "高 Z"]].map(([key, label]) => `<span>${label} <strong>${formatMillimeters(report.bounds[key])} mm</strong></span>`).join("")}</div></div>`;
    } else {
      result.innerHTML = `
        <div class="tube-designer-inspection-summary">
          <div><span>${report.plate ? "长边" : "总长"} <strong>${formatMillimeters(report.length)} mm</strong></span><span>孔 / 开口 <strong>${report.holes.length} 个</strong></span></div>
          <p>${escapeText(report.profile)}${report.plate ? "" : " mm"}</p>
        </div>
        ${report.holes.length ? `<div class="tube-designer-inspection-element-list"><table aria-label="孔和开口">
          <thead><tr><th scope="col" aria-label="显示标注"><button type="button" class="tube-designer-inspection-eye" data-tube-inspection-elements-visibility aria-pressed="true" aria-label="隐藏全部孔和开口标注" title="隐藏全部孔和开口标注">${renderVisibilityIcon(true, true)}</button></th><th scope="col">孔 / 开口</th><th scope="col">尺寸</th></tr></thead><tbody>
          ${report.holes.map((hole) => {
            const label = `${hole.kind === "side-opening" ? "单侧开口" : "孔"} ${hole.index}`;
            const hasAnnotations = report.annotations.some((annotation) => annotation.elementIds.includes(hole.elementId));
            return `<tr data-tube-inspection-element-row="${hole.elementId}">
              <td>${hasAnnotations ? `<button type="button" class="tube-designer-inspection-eye" data-tube-inspection-element-visibility="${hole.elementId}" aria-pressed="true" aria-label="隐藏${label}标注">${renderVisibilityIcon(true)}</button>` : ""}</td>
              <td colspan="2"><button type="button" data-tube-inspection-select-element="${hole.elementId}" aria-pressed="false"><strong>${label}</strong><span>${escapeText(hole.sizeLabel)}</span></button></td>
            </tr>`;
          }).join("")}</tbody></table></div>
          <section class="tube-designer-inspection-element-detail" data-tube-inspection-element-detail hidden>
            <strong data-tube-inspection-detail-title></strong>
            <dl>${["center", "edge", "face-center", "face-edge"].map((key) => `<div><dt>${{center:"中心距端",edge:"开口边距端","face-center":"中心距侧边","face-edge":"开口距侧边"}[key]}</dt><dd data-tube-inspection-detail="${key}"></dd></div>`).join("")}</dl>
          </section>` : ""}
      `;
    }
  }
  for (const input of dialog.querySelectorAll("[data-tube-inspection-dimension-category]")) {
    const visibility = partDimensionVisibilityState(controller, input.dataset.tubeInspectionDimensionCategory);
    input.checked = visibility.checked; input.indeterminate = visibility.mixed;
  }
  const bulk = dialog.querySelector("[data-tube-inspection-elements-visibility]");
  if (bulk) {
    const ids = annotatedElementIds(report);
    const visibleCount = ids.filter((id) => partDimensionElementVisible(controller, id)).length;
    const allVisible = ids.length > 0 && visibleCount === ids.length;
    const mixed = visibleCount > 0 && !allVisible;
    bulk.disabled = !ids.length;
    bulk.setAttribute("aria-pressed", mixed ? "mixed" : String(allVisible));
    const label = `${allVisible ? "隐藏" : "显示"}全部孔和开口标注`;
    bulk.setAttribute("aria-label", label);
    bulk.title = mixed ? `${label}（部分显示）` : label;
    bulk.querySelector("[data-tube-inspection-eye-slash]").toggleAttribute("hidden", visibleCount > 0);
    bulk.querySelector("[data-tube-inspection-eye-mixed]").toggleAttribute("hidden", !mixed);
  }
  for (const row of dialog.querySelectorAll("[data-tube-inspection-element-row]")) {
    const id = row.dataset.tubeInspectionElementRow;
    const selected = id === controller.selectedElementId;
    const visible = partDimensionElementVisible(controller, id);
    row.classList.toggle("is-selected", selected);
    row.classList.toggle("is-hidden", !visible);
    row.querySelector("[data-tube-inspection-select-element]")?.setAttribute("aria-pressed", String(selected));
    const eye = row.querySelector("[data-tube-inspection-element-visibility]");
    if (eye) {
      eye.setAttribute("aria-pressed", String(visible));
      eye.setAttribute("aria-label", `${visible ? "隐藏" : "显示"}${row.querySelector("strong").textContent}标注`);
      eye.querySelector("[data-tube-inspection-eye-slash]").toggleAttribute("hidden", visible);
    }
  }
  const selected = report.holes.find((hole) => hole.elementId === controller.selectedElementId);
  const detail = dialog.querySelector("[data-tube-inspection-element-detail]");
  if (!detail) return;
  detail.hidden = !selected;
  if (!selected) return;
  detail.querySelector("[data-tube-inspection-detail-title]").textContent = `${selected.kind === "side-opening" ? "单侧开口" : "孔"} ${selected.index} · ${selected.sizeLabel}`;
  const pairs = {
    center: [selected.station, Math.max(0, report.length - selected.station)],
    edge: [selected.startEdgeDistance, selected.endEdgeDistance],
    "face-center": [selected.centerToFaceEdgeNegative, selected.centerToFaceEdgePositive],
    "face-edge": [selected.faceEdgeClearanceNegative, selected.faceEdgeClearancePositive],
  };
  for (const [key, values] of Object.entries(pairs)) {
    detail.querySelector(`[data-tube-inspection-detail="${key}"]`).textContent = `${values.map(formatMillimeters).join(" / ")} mm`;
  }
}

function visibleInspectionAnnotations(controller) {
  return visiblePartDimensionAnnotations(controller);
}

function updateInspectionAnnotations(controller) {
  const annotations = visibleInspectionAnnotations(controller);
  controller.viewport.setDimensionAnnotations(annotations.map((annotation) =>
    annotation.elementIds.includes(controller.selectedElementId)
      ? { ...annotation, color: 0xffed89 } : annotation));
  const selected = controller.dimensionReport?.holes.find((hole) => hole.elementId === controller.selectedElementId);
  const showSelection = selected && annotations.some(annotation => annotation.elementIds.includes(selected.elementId));
  const points = showSelection && controller.dimensionReport.axis && selected.halfSpanAlong > 0
    ? [-1, 1].map((sign) => add(selected.center, scale(controller.dimensionReport.axis, sign * selected.halfSpanAlong)))
    : showSelection ? [selected.center] : [];
  controller.viewport.setMeasurementPoints(points.map(([x, y, z]) => ({ x, y, z })));
  controller.host.dataset.tubeInspectionDimensionCount = String(controller.viewport.getDebugState().dimensionAnnotationCount ?? 0);
  controller.host.dataset.tubeInspectionVisibleAnnotationIds = JSON.stringify(annotations.map((annotation) => annotation.id));
  controller.host.dataset.tubeInspectionSelectedElement = controller.selectedElementId;
}

function attachInspectionControls(controller) {
  const dialog = controller.host.closest(".tube-designer-part-inspection-dialog");
  controller.dialog = dialog;
  controller.host.insertAdjacentHTML("beforeend", renderViewCube());
  controller.cubeView = { viewport: controller.viewport };
  attachViewCube(controller.cubeView, controller.host);
  controller.onClick = (event) => {
    const cube = event.target?.closest?.('[data-cam-viewcube] [data-cam-action="view-standard"]');
    if (cube && controller.host.contains(cube)) {
      event.preventDefault();
      event.stopImmediatePropagation();
      controller.viewport.setStandardView(String(cube.dataset.camView ?? "iso"));
      return;
    }
    const collapse = event.target?.closest?.("[data-tube-inspection-tree-collapse]");
    if (collapse) {
      event.preventDefault();
      event.stopPropagation();
      controller.treeCollapsed = !controller.treeCollapsed;
      renderAutomaticDimensionState(controller);
      return;
    }
    const bulk = event.target?.closest?.("[data-tube-inspection-elements-visibility]");
    if (bulk) {
      event.preventDefault();
      event.stopPropagation();
      const ids = annotatedElementIds(controller.dimensionReport);
      const hide = ids.length > 0 && ids.every((id) => partDimensionElementVisible(controller, id));
      for (const id of ids) {
        setPartDimensionElementVisibility(controller, id, !hide);
      }
      updateInspectionAnnotations(controller);
      renderAutomaticDimensionState(controller);
      return;
    }
    const eye = event.target?.closest?.("[data-tube-inspection-element-visibility]");
    const select = event.target?.closest?.("[data-tube-inspection-select-element]");
    if (!eye && !select) return;
    event.preventDefault();
    event.stopPropagation();
    if (eye) {
      const id = eye.dataset.tubeInspectionElementVisibility;
      setPartDimensionElementVisibility(controller, id, !partDimensionElementVisible(controller, id));
    } else {
      controller.selectedElementId = select.dataset.tubeInspectionSelectElement;
      setPartDimensionElementVisibility(controller, controller.selectedElementId, true);
    }
    updateInspectionAnnotations(controller);
    renderAutomaticDimensionState(controller);
  };
  controller.onChange = (event) => {
    const input = event.target;
    if (input.hasAttribute?.("data-tube-inspection-all-dimensions")) {
      setPartDimensionMasterVisibility(controller, input.checked);
    } else if (input.hasAttribute?.("data-tube-inspection-dimension-category")) {
      const key = input.dataset.tubeInspectionDimensionCategory;
      setPartDimensionCategoryVisibility(controller, key, input.checked);
    } else return;
    event.stopPropagation();
    updateInspectionAnnotations(controller);
    renderAutomaticDimensionState(controller);
  };
  controller.onPointerDown = (event) => {
    controller.scenePointerStart = event.button === 0 && event.target === controller.viewport.renderer.domElement
      ? { x: event.clientX, y: event.clientY } : null;
  };
  controller.onPointerUp = (event) => {
    const start = controller.scenePointerStart;
    controller.scenePointerStart = null;
    if (!start || event.target !== controller.viewport.renderer.domElement
        || Math.hypot(event.clientX - start.x, event.clientY - start.y) > 4) return;
    const nearest = nearestPartDimensionElement(controller.dimensionReport, controller.viewport, event);
    if (!nearest) return;
    controller.selectedElementId = nearest.elementId;
    setPartDimensionElementVisibility(controller, nearest.elementId, true);
    updateInspectionAnnotations(controller);
    renderAutomaticDimensionState(controller);
    const row = dialog.querySelector(`[data-tube-inspection-element-row="${nearest.elementId}"]`);
    const list = row?.closest(".tube-designer-inspection-element-list");
    if (row && list) {
      const headerHeight = list.querySelector("thead")?.offsetHeight ?? 0;
      const top = row.getBoundingClientRect().top - list.getBoundingClientRect().top + list.scrollTop;
      if (top < list.scrollTop + headerHeight) list.scrollTop = Math.max(0, top - headerHeight);
      else if (top + row.offsetHeight > list.scrollTop + list.clientHeight) list.scrollTop = top + row.offsetHeight - list.clientHeight;
    }
  };
  dialog.addEventListener("click", controller.onClick, true);
  dialog.addEventListener("change", controller.onChange);
  controller.host.addEventListener("pointerdown", controller.onPointerDown);
  controller.host.addEventListener("pointerup", controller.onPointerUp);
}

function disposeInspection(controller) {
  if (!controller) return;
  controller.dialog?.removeEventListener("click", controller.onClick, true);
  controller.dialog?.removeEventListener("change", controller.onChange);
  controller.host?.removeEventListener("pointerdown", controller.onPointerDown);
  controller.host?.removeEventListener("pointerup", controller.onPointerUp);
  stopViewCubeAnimation(controller.cubeView);
  controller.viewport.dispose();
}

function buildDimensionAnnotations({ start, end, length, axis, offsetDirection, profileSpan, holes }) {
  const annotations = [{
    id: "overall:0", category: "overall", elementIds: [],
    start,
    end,
    offset: scale(offsetDirection, profileSpan * 5.1),
    label: `总长 ${formatMillimeters(length)} mm`,
    color: 0xffc857,
  }];
  if (!holes.length) return annotations;

  const centerOffset = scale(offsetDirection, profileSpan * 3.35);
  const sizeOffset = scale(offsetDirection, profileSpan * 1.55);
  const gapOffset = scale(offsetDirection, -profileSpan * 2.1);
  const edgeOffset = scale(offsetDirection, -profileSpan * 4.0);
  const first = holes[0];
  const last = holes[holes.length - 1];
  appendAnnotation(
    annotations, start, first.center, centerOffset,
    `首端距 ${formatMillimeters(first.station)}`, 0x73d4ca, "end-distance", [first.elementId],
  );
  for (let index = 1; index < holes.length; index += 1) {
    const previous = holes[index - 1];
    const hole = holes[index];
    appendAnnotation(
      annotations, previous.center, hole.center, centerOffset,
      `中心距 ${formatMillimeters(hole.station - previous.station)}`, 0x73d4ca,
      "center-distance", [previous.elementId, hole.elementId],
    );
    appendAnnotation(
      annotations,
      add(previous.center, scale(axis, previous.halfSpanAlong)),
      add(hole.center, scale(axis, -hole.halfSpanAlong)),
      gapOffset,
      `净距 ${formatMillimeters(hole.station - previous.station - previous.halfSpanAlong - hole.halfSpanAlong)} mm`,
      0xf09a6c,
      "clearance", [previous.elementId, hole.elementId],
    );
  }
  appendAnnotation(
    annotations, last.center, end, centerOffset,
    `末端距 ${formatMillimeters(length - last.station)}`, 0x73d4ca, "end-distance", [last.elementId],
  );

  for (const hole of holes) {
    appendAnnotation(
      annotations,
      add(hole.center, scale(axis, -hole.halfSpanAlong)),
      add(hole.center, scale(axis, hole.halfSpanAlong)),
      sizeOffset,
      `开${hole.index} ${hole.sizeLabel}`,
      0x8fe0a9,
      "opening-size", [hole.elementId],
    );
    if (hole.faceTangent && hole.centerToFaceEdge > 0) {
      appendAnnotation(
        annotations,
        add(hole.center, scale(hole.faceTangent, -hole.centerToFaceEdge)),
        add(hole.center, scale(hole.faceTangent, hole.centerToFaceEdge)),
        edgeOffset,
        `开${hole.index} 距边 ${formatMillimeters(hole.centerToFaceEdgeNegative)} / ${formatMillimeters(hole.centerToFaceEdgePositive)}`,
        0xb8a4ff,
        "face-distance", [hole.elementId],
      );
    }
  }
  return annotations;
}

function appendAnnotation(collection, start, end, offset, label, color, category, elementIds) {
  if (!start || !end || distance(start, end) <= 0.01) return;
  collection.push({ id: `${category}:${collection.length}`, category, elementIds, start, end, offset, label, color });
}

function normalizeHoleFeature(feature, index, start, axis, referenceLength) {
  const station = finiteNumber(feature?.station);
  if (station == null || station < -0.01 || station > referenceLength + 0.01) return null;
  const center = finitePoint(feature.center)
    ?? (start && axis ? add(start, scale(axis, station)) : null);
  if (!center) return null;
  const spanAlong = Math.max(0, finiteNumber(feature.openingSpanAlong) ?? 0);
  const spanAcross = Math.max(0, finiteNumber(feature.openingSpanAcross) ?? 0);
  const centerToFaceEdgeNegative = Math.max(
    0,
    finiteNumber(feature.centerToFaceEdgeNegative)
      ?? finiteNumber(feature.centerToFaceEdge)
      ?? 0,
  );
  const centerToFaceEdgePositive = Math.max(
    0,
    finiteNumber(feature.centerToFaceEdgePositive)
      ?? finiteNumber(feature.centerToFaceEdge)
      ?? 0,
  );
  const faceEdgeClearanceNegative = Math.max(
    0,
    finiteNumber(feature.faceEdgeClearanceNegative)
      ?? finiteNumber(feature.faceEdgeClearance)
      ?? 0,
  );
  const faceEdgeClearancePositive = Math.max(
    0,
    finiteNumber(feature.faceEdgeClearancePositive)
      ?? finiteNumber(feature.faceEdgeClearance)
      ?? 0,
  );
  const shape = String(feature.shape ?? "");
  const diameter = finiteNumber(feature.diameter);
  const sizeLabel = shape === "circle" && diameter != null
    ? `⌀${formatMillimeters(diameter)} mm`
    : `${formatMillimeters(spanAlong)} × ${formatMillimeters(spanAcross)} mm`;
  const halfSpanAlong = spanAlong / 2;
  return {
    index: index + 1,
    elementId: `hole:${index + 1}`,
    kind: String(feature.kind ?? "through-opening"),
    station: Math.max(0, station),
    center,
    shape,
    diameter,
    spanAlong,
    spanAcross,
    halfSpanAlong,
    startEdgeDistance: Math.max(0, station - halfSpanAlong),
    endEdgeDistance: Math.max(0, referenceLength - station - halfSpanAlong),
    centerToFaceEdge: Math.max(centerToFaceEdgeNegative, centerToFaceEdgePositive),
    centerToFaceEdgeNegative,
    centerToFaceEdgePositive,
    faceEdgeClearanceNegative,
    faceEdgeClearancePositive,
    faceTangent: finitePoint(feature.faceTangent),
    sideCenter: finitePoint2(feature.sideCenter),
    sideSpanAcross: Math.max(0, finiteNumber(feature.sideSpanAcross) ?? spanAcross),
    sideVisible: feature.sideVisible !== false,
    sizeLabel,
  };
}

function normalizeSideProjection(value, fallbackLength, section) {
  const source = value && typeof value === "object" ? value : {};
  const width = Math.max(0, finiteNumber(source.width) ?? finiteNumber(fallbackLength) ?? 0);
  const height = Math.max(
    0,
    finiteNumber(source.height)
      ?? finiteNumber(section?.height)
      ?? finiteNumber(section?.width)
      ?? 0,
  );
  const point = (candidate) => {
    const normalized = finitePoint2(candidate);
    if (!normalized) return null;
    return [
      Math.min(width, Math.max(0, normalized[0])),
      Math.min(height, Math.max(0, normalized[1])),
    ];
  };
  let outline = (Array.isArray(source.outline) ? source.outline : [])
    .map(point).filter(Boolean);
  if (outline.length < 3 && width > 0 && height > 0) {
    outline = [[0, 0], [width, 0], [width, height], [0, height]];
  }
  const segments = (Array.isArray(source.segments) ? source.segments : [])
    .map((segment) => ({ start: point(segment?.start), end: point(segment?.end) }))
    .filter((segment) => segment.start && segment.end
      && Math.hypot(
        segment.end[0] - segment.start[0],
        segment.end[1] - segment.start[1],
      ) > 1e-7);
  return Object.freeze({
    width,
    height,
    outline,
    segments,
    horizontalAxis: finitePoint(source.horizontalAxis),
    verticalAxis: finitePoint(source.verticalAxis),
    viewDirection: finitePoint(source.viewDirection),
  });
}

function perpendicularDirection(candidate, axis) {
  let value = candidate ? subtract(candidate, scale(axis, dot(candidate, axis))) : null;
  if (!value || magnitude(value) <= 1.0e-7) {
    const fallback = Math.abs(axis[2]) < 0.85 ? [0, 0, 1] : [1, 0, 0];
    value = subtract(fallback, scale(axis, dot(fallback, axis)));
  }
  return normalize(value);
}

function formatMeasuredProfile(section) {
  const width = finiteNumber(section?.width);
  const height = finiteNumber(section?.height);
  const wall = finiteNumber(section?.wallThickness);
  if (String(section?.shape ?? "").toLowerCase() === "circle") {
    return `圆形截面 ⌀${formatMillimeters(section?.diameter ?? width)}`
      + (wall && wall > 0 ? ` × 壁厚 ${formatMillimeters(wall)}` : "");
  }
  return `截面 ${formatMillimeters(width)} × ${formatMillimeters(height)}`
    + (wall && wall > 0 ? ` × 壁厚 ${formatMillimeters(wall)}` : "");
}

function setInspectionStatus(controller, message, isError) {
  const status = controller.host.closest(".tube-designer-part-inspection-dialog")
    ?.querySelector("[data-tube-designer-inspection-status]");
  if (!status) return;
  status.textContent = message;
  status.hidden = !message;
  status.classList.toggle("error", Boolean(isError));
}

function setInspectionProgress(controller, message, isBusy) {
  const progress = controller.host.closest(".tube-designer-part-inspection-dialog")
    ?.querySelector("[data-tube-designer-inspection-progress]");
  if (!progress) return;
  progress.hidden = !isBusy;
  progress.setAttribute("aria-hidden", isBusy ? "false" : "true");
  progress.setAttribute("aria-valuetext", message || (isBusy ? "处理中" : "已完成"));
  controller.host.dataset.tubeInspectionProgressVisible = isBusy ? "true" : "false";
}

async function finishInspectionProgress(mount, controller, progressPaintedAt) {
  const paintedAt = await progressPaintedAt;
  await waitMinimum(paintedAt, PART_INSPECTION_PROGRESS_MINIMUM_VISIBLE_MS);
  if (controllers.get(mount) !== controller || !controller.host.isConnected) return false;
  controller.host.dataset.tubeInspectionProgressMs = String(
    Math.max(0, Math.round(performanceNow() - paintedAt)),
  );
  setInspectionProgress(controller, "", false);
  return true;
}

function performanceNow() {
  return typeof globalThis.performance?.now === "function" ? globalThis.performance.now() : Date.now();
}

async function waitForProgressPaint() {
  if (typeof globalThis.requestAnimationFrame !== "function") return performanceNow();
  await new Promise((resolve) => {
    let settled = false;
    const finish = () => {
      if (settled) return;
      settled = true;
      clearTimeout(fallback);
      resolve();
    };
    const fallback = setTimeout(finish, 100);
    globalThis.requestAnimationFrame(() => globalThis.requestAnimationFrame(finish));
  });
  return performanceNow();
}

async function waitMinimum(startedAt, minimum) {
  const remaining = minimum - (performanceNow() - startedAt);
  if (remaining > 0) await new Promise((resolve) => setTimeout(resolve, remaining));
}

function finitePoint(value) {
  const source = Array.isArray(value) ? value : [value?.x, value?.y, value?.z];
  if (source.length !== 3 || !source.every((entry) => Number.isFinite(Number(entry)))) return null;
  return source.map(Number);
}

function finitePoint2(value) {
  const source = Array.isArray(value) ? value : [value?.x, value?.y];
  if (source.length !== 2 || !source.every((entry) => Number.isFinite(Number(entry)))) return null;
  return source.map(Number);
}

function finiteNumber(value) {
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

function add(left, right) {
  return left.map((value, index) => value + right[index]);
}

function subtract(left, right) {
  return left.map((value, index) => value - right[index]);
}

function scale(vector, factor) {
  return vector.map((value) => value * factor);
}

function dot(left, right) {
  return left.reduce((sum, value, index) => sum + value * right[index], 0);
}

function normalize(vector) {
  const length = magnitude(vector);
  return length > 1.0e-12 ? scale(vector, 1 / length) : [0, 0, 0];
}

function magnitude(vector) {
  return Math.hypot(...vector);
}

function distance(left, right) {
  return magnitude(subtract(left, right));
}

function formatMillimeters(value) {
  const number = Number(value);
  if (!Number.isFinite(number)) return "—";
  return number.toLocaleString("zh-CN", { minimumFractionDigits: 0, maximumFractionDigits: 2 });
}

function escapeText(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}
