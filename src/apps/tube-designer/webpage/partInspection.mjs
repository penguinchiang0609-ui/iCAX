import { createThreeViewport } from "../../../iCAX-UI/SDK/Viewport/threeViewport.mjs";

export const PART_INSPECTION_PROGRESS_MINIMUM_VISIBLE_MS = 500;

const controllers = new WeakMap();

export function scheduleDesignerPartInspectionHydration(context, designer, view) {
  const inspectedPartId = view.tubeDesignerPartInspectionOpen
    ? String(view.tubeDesignerInspectedPartId ?? "")
    : "";
  const part = inspectedPartId ? findPart(designer, inspectedPartId) : null;
  view.viewport?.setContinuousRendering?.(!part && !view.tubeDesignerAddDialogOpen);
  queueMicrotask(() => {
    void hydrateInspection(context, part);
  });
}

export function toggleDesignerAutomaticDimensions(context) {
  const controller = getController(context);
  if (!controller) return false;
  controller.automaticDimensionsVisible = !controller.automaticDimensionsVisible;
  controller.viewport.setDimensionAnnotations(
    controller.automaticDimensionsVisible ? controller.dimensionReport?.annotations ?? [] : [],
  );
  const viewportState = controller.viewport.getDebugState();
  controller.host.dataset.tubeInspectionDimensionCount = String(
    viewportState.dimensionAnnotationCount ?? 0,
  );
  renderAutomaticDimensionState(controller);
  return true;
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
        annotations.push({ start: origin, end: add(origin, scale(axes[index], dimensions[index])),
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
    .map((hole, index) => ({ ...hole, index: index + 1 }));
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
    previous?.viewport.dispose();
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
  previous?.viewport.dispose();

  const controller = {
    host,
    partKey,
    automaticDimensionsVisible: true,
    dimensionReport: null,
    viewport: null,
  };
  controller.viewport = createThreeViewport({
    backgroundColor: 0x13252d,
    continuousRender: false,
    constrainOrbit: false,
    showProjectionToggle: true,
    pickingEnabled: false,
    antialias: true,
    pixelRatioCap: 2,
  });
  controller.viewport.mount(host);
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
    controller.viewport.setDimensionAnnotations(
      controller.automaticDimensionsVisible ? controller.dimensionReport.annotations : [],
    );
    applyHorizontalPartPresentation(controller);
    controller.viewport.fitViewToViewport(1.16);
    const viewportState = controller.viewport.getDebugState();
    const dimensionCount = Number(viewportState.dimensionAnnotationCount ?? 0);
    host.dataset.tubeInspectionDimensionCount = String(dimensionCount);
    host.dataset.tubeInspectionMeasurementMs = String(
      Math.max(0, Math.round(performance.now() - measurementStartedAt)),
    );
    if (controller.dimensionReport.hasLinearReference && dimensionCount < 1) {
      throw new Error("自动尺寸已经生成，但未能进入三维复尺场景。");
    }
    renderAutomaticDimensionState(controller);
    if (!await finishInspectionProgress(mount, controller, progressPaintedAt)) return;
    setInspectionStatus(
      controller,
      controller.dimensionReport.partKind === "accessory" ? "已读取配件真实外包尺寸；配件不生成管材标尺。"
        : `已显示 ${dimensionCount} 条自动标尺，识别到 ${controller.dimensionReport.holes.length} 个孔 / 开口。`,
      false,
    );
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

function findPart(designer, partId) {
  for (const group of designer?.manufacturingGroups ?? []) {
    const part = (group.parts ?? []).find((item) => String(item.entityId) === partId);
    if (part) return part;
  }
  return null;
}

function renderAutomaticDimensionState(controller) {
  const dialog = controller.host.closest(".tube-designer-part-inspection-dialog");
  if (!dialog) return;
  const toggle = dialog.querySelector("[data-cam-action='tube-designer-toggle-automatic-dimensions']");
  if (toggle) {
    toggle.setAttribute("aria-pressed", controller.automaticDimensionsVisible ? "true" : "false");
    toggle.textContent = controller.automaticDimensionsVisible ? "隐藏标尺" : "显示标尺";
  }
  const result = dialog.querySelector("[data-tube-designer-automatic-dimensions]");
  if (!result) return;
  const report = controller.dimensionReport;
  if (!report) {
    result.innerHTML = `<div class="tube-designer-dimension-empty">正在从最终零件几何提取尺寸…</div>`;
    return;
  }
  if (report.partKind === "accessory") {
    result.innerHTML = `<div class="tube-designer-dimension-overview">${[["width", "宽 X"], ["depth", "深 Y"], ["height", "高 Z"]].map(([key, label]) => `<span><small>${label}</small><strong>${formatMillimeters(report.bounds[key])} mm</strong></span>`).join("")}<span><small>数据来源</small><strong>最终几何</strong></span></div><div class="tube-designer-dimension-empty">配件按完整模型复尺，不生成管材总长或孔距标尺。</div>`;
    return;
  }
  result.innerHTML = `
    <div class="tube-designer-dimension-overview">
      <span><small>${report.plate ? "长边" : "总长"}</small><strong>${formatMillimeters(report.length)} mm</strong></span>
      <span><small>规格</small><strong>${escapeText(report.profile)}</strong></span>
      <span><small>孔 / 开口</small><strong>${report.holes.length} 个</strong></span>
      <span><small>数据来源</small><strong>最终几何</strong></span>
    </div>
    ${report.holes.length ? `
      <div class="tube-designer-dimension-list">
        ${report.holes.map((hole) => `
          <article>
            <strong>${hole.kind === "side-opening" ? "单侧开口" : "孔"} ${hole.index} · ${escapeText(hole.sizeLabel)}</strong>
            <span>中心距首端 ${formatMillimeters(hole.station)} · 距末端 ${formatMillimeters(Math.max(0, report.length - hole.station))}</span>
            <span>开口边距首端 ${formatMillimeters(hole.startEdgeDistance)} · 距末端 ${formatMillimeters(hole.endEdgeDistance)}</span>
            <span>面宽方向中心距边 ${formatMillimeters(hole.centerToFaceEdgeNegative)} / ${formatMillimeters(hole.centerToFaceEdgePositive)}</span>
            <span>面宽方向开口边净距 ${formatMillimeters(hole.faceEdgeClearanceNegative)} / ${formatMillimeters(hole.faceEdgeClearancePositive)}</span>
          </article>`).join("")}
        ${report.pitches.map((pitch) => `
          <article class="tube-designer-dimension-pitch">
            <strong>开口 ${pitch.from} — 开口 ${pitch.to}</strong>
            <span>中心距 ${formatMillimeters(pitch.centerDistance)} · 最近开口边净距 ${formatMillimeters(pitch.edgeClearance)}</span>
          </article>`).join("")}
      </div>`
      : `<div class="tube-designer-dimension-empty">${report.plate ? "长边、短边与厚度取自当前实体，不作为管材处理。" : "当前最终几何中未识别到孔或开口；总长与截面仍取自当前实体。"}</div>`}
  `;
}

function buildDimensionAnnotations({ start, end, length, axis, offsetDirection, profileSpan, holes }) {
  const annotations = [{
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
    `首端距 ${formatMillimeters(first.station)}`, 0x73d4ca,
  );
  for (let index = 1; index < holes.length; index += 1) {
    const previous = holes[index - 1];
    const hole = holes[index];
    appendAnnotation(
      annotations, previous.center, hole.center, centerOffset,
      `中心距 ${formatMillimeters(hole.station - previous.station)}`, 0x73d4ca,
    );
    appendAnnotation(
      annotations,
      add(previous.center, scale(axis, previous.halfSpanAlong)),
      add(hole.center, scale(axis, -hole.halfSpanAlong)),
      gapOffset,
      `净距 ${formatMillimeters(hole.station - previous.station - previous.halfSpanAlong - hole.halfSpanAlong)} mm`,
      0xf09a6c,
    );
  }
  appendAnnotation(
    annotations, last.center, end, centerOffset,
    `末端距 ${formatMillimeters(length - last.station)}`, 0x73d4ca,
  );

  for (const hole of holes) {
    appendAnnotation(
      annotations,
      add(hole.center, scale(axis, -hole.halfSpanAlong)),
      add(hole.center, scale(axis, hole.halfSpanAlong)),
      sizeOffset,
      `开${hole.index} ${hole.sizeLabel}`,
      0x8fe0a9,
    );
    if (hole.faceTangent && hole.centerToFaceEdge > 0) {
      appendAnnotation(
        annotations,
        add(hole.center, scale(hole.faceTangent, -hole.centerToFaceEdge)),
        add(hole.center, scale(hole.faceTangent, hole.centerToFaceEdge)),
        edgeOffset,
        `开${hole.index} 距边 ${formatMillimeters(hole.centerToFaceEdgeNegative)} / ${formatMillimeters(hole.centerToFaceEdgePositive)}`,
        0xb8a4ff,
      );
    }
  }
  return annotations;
}

function appendAnnotation(collection, start, end, offset, label, color) {
  if (!start || !end || distance(start, end) <= 0.01) return;
  collection.push({ start, end, offset, label, color });
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
    sizeLabel,
  };
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
