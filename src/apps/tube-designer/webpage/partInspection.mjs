import { createThreeViewport } from "../../../iCAX-UI/SDK/Viewport/threeViewport.mjs";

const controllers = new WeakMap();

export function scheduleDesignerPartInspectionHydration(context, designer, view) {
  const inspectedPartId = view.tubeDesignerPartInspectionOpen
    ? String(view.tubeDesignerInspectedPartId ?? "")
    : "";
  const part = inspectedPartId ? findPart(designer, inspectedPartId) : null;
  view.viewport?.setContinuousRendering?.(!part);
  queueMicrotask(() => {
    void hydrateInspection(context, part);
  });
}

export function clearDesignerPartMeasurement(context) {
  const controller = getController(context);
  if (!controller) return false;
  controller.points = [];
  controller.viewport.clearMeasurementPoints();
  renderMeasurementState(controller);
  return true;
}

export function toggleDesignerPartMeasurement(context) {
  const controller = getController(context);
  if (!controller) return false;
  controller.measurementEnabled = !controller.measurementEnabled;
  controller.viewport.setPickingEnabled(controller.measurementEnabled);
  if (!controller.measurementEnabled) {
    controller.points = [];
    controller.viewport.clearMeasurementPoints();
  }
  renderMeasurementMode(controller);
  renderMeasurementState(controller);
  return true;
}

export function toggleDesignerAutomaticDimensions(context) {
  const controller = getController(context);
  if (!controller) return false;
  controller.automaticDimensionsVisible = !controller.automaticDimensionsVisible;
  controller.viewport.setDimensionAnnotations(
    controller.automaticDimensionsVisible ? controller.dimensionReport?.annotations ?? [] : [],
  );
  renderAutomaticDimensionState(controller);
  return true;
}

export function fitDesignerInspectedPart(context) {
  return Boolean(getController(context)?.viewport.fitView(1.22));
}

export function setDesignerInspectedPartView(context, viewName = "iso") {
  return Boolean(getController(context)?.viewport.setStandardView(viewName));
}

export function calculatePointMeasurement(first, second) {
  const dx = Number(second?.x) - Number(first?.x);
  const dy = Number(second?.y) - Number(first?.y);
  const dz = Number(second?.z) - Number(first?.z);
  return Object.freeze({
    dx: Math.abs(dx),
    dy: Math.abs(dy),
    dz: Math.abs(dz),
    distance: Math.hypot(dx, dy, dz),
  });
}

export function buildAutomaticDimensionReport(part) {
  const candidate = part?.geometryMeasurement ?? {};
  const measurement = candidate?.available === true
    && String(candidate?.source ?? "") === "final-brep"
    ? candidate
    : {};
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
    .filter((feature) => ["through-opening", "throughHole"].includes(String(feature?.kind ?? "")))
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
    points: [],
    measurementEnabled: false,
    automaticDimensionsVisible: true,
    dimensionReport: null,
    viewport: null,
  };
  controller.viewport = createThreeViewport({
    backgroundColor: 0x13252d,
    continuousRender: false,
    pickingEnabled: false,
    onPick: (_object, hit) => handleMeasurementPick(controller, hit),
  });
  controller.viewport.mount(host);
  host.dataset.tubeInspectionReady = "false";
  host.dataset.tubeInspectionEntityCount = "0";
  controllers.set(mount, controller);
  renderAutomaticDimensionState(controller);
  renderMeasurementMode(controller);
  renderMeasurementState(controller);

  if (!resourceId) {
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
    renderAutomaticDimensionState(controller);
    setInspectionStatus(
      controller,
      `已从最终几何测得 ${controller.dimensionReport.holes.length} 个贯穿孔；需要补测时再打开手工测量。`,
      false,
    );
  } catch (error) {
    setInspectionStatus(controller, error?.message ?? String(error), true);
  }
}

function getController(context) {
  const mount = context.mount;
  return mount && (typeof mount === "object" || typeof mount === "function")
    ? controllers.get(mount) ?? null
    : null;
}

function findPart(designer, partId) {
  for (const group of designer?.manufacturingGroups ?? []) {
    const part = (group.parts ?? []).find((item) => String(item.entityId) === partId);
    if (part) return part;
  }
  return null;
}

function handleMeasurementPick(controller, hit) {
  if (!controller.measurementEnabled) return;
  const point = hit?.point;
  if (!point || ![point.x, point.y, point.z].every(Number.isFinite)) return;
  if (controller.points.length >= 2) controller.points = [];
  controller.points.push({ x: point.x, y: point.y, z: point.z });
  controller.viewport.setMeasurementPoints(controller.points);
  renderMeasurementState(controller);
}

function renderMeasurementMode(controller) {
  const dialog = controller.host.closest(".tube-designer-part-inspection-dialog");
  if (!dialog) return;
  dialog.dataset.measurementEnabled = controller.measurementEnabled ? "true" : "false";
  const toggle = dialog.querySelector("[data-cam-action='tube-designer-toggle-part-measurement']");
  if (toggle) {
    toggle.setAttribute("aria-pressed", controller.measurementEnabled ? "true" : "false");
    toggle.textContent = controller.measurementEnabled ? "结束测量" : "开始测量";
  }
  const help = dialog.querySelector("[data-tube-designer-measurement-pick-help]");
  if (help) help.textContent = controller.measurementEnabled
    ? "左键：选取测量点"
    : "左键：测量关闭，不执行拾取";
}

function renderMeasurementState(controller) {
  const dialog = controller.host.closest(".tube-designer-part-inspection-dialog");
  const result = dialog?.querySelector("[data-tube-designer-measurement-result]");
  const clear = dialog?.querySelector("[data-cam-action='tube-designer-clear-part-measurement']");
  if (clear) clear.disabled = !controller.measurementEnabled || !controller.points.length;
  if (!result) return;
  result.dataset.measurementPointCount = String(controller.points.length);
  if (!controller.measurementEnabled) {
    result.innerHTML = `<strong>测量已关闭</strong><span>查看模型不会触发拾取检测</span>`;
    return;
  }
  if (!controller.points.length) {
    result.innerHTML = `<strong>等待起点</strong><span>左键点击模型选择起点</span>`;
    return;
  }
  const first = controller.points[0];
  if (controller.points.length === 1) {
    result.innerHTML = `<strong>已选择起点</strong><span>P1 ${formatPoint(first)}</span><small>继续点击模型选择终点</small>`;
    return;
  }
  const second = controller.points[1];
  const measurement = calculatePointMeasurement(first, second);
  result.innerHTML = `
    <strong>${formatMillimeters(measurement.distance)} mm</strong>
    <span>空间直线距离</span>
    <dl>
      <div><dt>ΔX</dt><dd>${formatMillimeters(measurement.dx)} mm</dd></div>
      <div><dt>ΔY</dt><dd>${formatMillimeters(measurement.dy)} mm</dd></div>
      <div><dt>ΔZ</dt><dd>${formatMillimeters(measurement.dz)} mm</dd></div>
    </dl>
    <small>P1 ${formatPoint(first)}<br />P2 ${formatPoint(second)}</small>`;
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
  result.innerHTML = `
    <div class="tube-designer-dimension-overview">
      <span><small>总长</small><strong>${formatMillimeters(report.length)} mm</strong></span>
      <span><small>规格</small><strong>${escapeText(report.profile)}</strong></span>
      <span><small>贯穿孔</small><strong>${report.holes.length} 个</strong></span>
      <span><small>数据来源</small><strong>最终几何</strong></span>
    </div>
    ${report.holes.length ? `
      <div class="tube-designer-dimension-list">
        ${report.holes.map((hole) => `
          <article>
            <strong>孔 ${hole.index} · ${escapeText(hole.sizeLabel)}</strong>
            <span>中心距首端 ${formatMillimeters(hole.station)} · 距末端 ${formatMillimeters(Math.max(0, report.length - hole.station))}</span>
            <span>孔边距首端 ${formatMillimeters(hole.startEdgeDistance)} · 距末端 ${formatMillimeters(hole.endEdgeDistance)}</span>
            <span>面宽方向中心距边 ${formatMillimeters(hole.centerToFaceEdgeNegative)} / ${formatMillimeters(hole.centerToFaceEdgePositive)}</span>
            <span>面宽方向孔边净距 ${formatMillimeters(hole.faceEdgeClearanceNegative)} / ${formatMillimeters(hole.faceEdgeClearancePositive)}</span>
          </article>`).join("")}
        ${report.pitches.map((pitch) => `
          <article class="tube-designer-dimension-pitch">
            <strong>孔 ${pitch.from} — 孔 ${pitch.to}</strong>
            <span>中心距 ${formatMillimeters(pitch.centerDistance)} · 最近孔边净距 ${formatMillimeters(pitch.edgeClearance)}</span>
          </article>`).join("")}
      </div>`
      : `<div class="tube-designer-dimension-empty">当前最终几何中未识别到贯穿孔；总长与截面仍取自当前实体。</div>`}
  `;
}

function buildDimensionAnnotations({ start, end, length, axis, offsetDirection, profileSpan, holes }) {
  const annotations = [{
    start,
    end,
    offset: scale(offsetDirection, profileSpan * 3.25),
    label: `总长 ${formatMillimeters(length)} mm`,
    color: 0xffc857,
  }];
  if (!holes.length) return annotations;

  const centerOffset = scale(offsetDirection, profileSpan * 2.05);
  const gapOffset = scale(offsetDirection, -profileSpan * 1.75);
  const first = holes[0];
  const last = holes[holes.length - 1];
  appendAnnotation(
    annotations, start, first.center, centerOffset,
    `首端—孔1中心 ${formatMillimeters(first.station)} mm`, 0x73d4ca,
  );
  for (let index = 1; index < holes.length; index += 1) {
    const previous = holes[index - 1];
    const hole = holes[index];
    appendAnnotation(
      annotations, previous.center, hole.center, centerOffset,
      `孔${previous.index}—孔${hole.index}中心 ${formatMillimeters(hole.station - previous.station)} mm`, 0x73d4ca,
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
    `孔${last.index}中心—末端 ${formatMillimeters(length - last.station)} mm`, 0x73d4ca,
  );

  for (const hole of holes) {
    appendAnnotation(
      annotations,
      add(hole.center, scale(axis, -hole.halfSpanAlong)),
      add(hole.center, scale(axis, hole.halfSpanAlong)),
      [0, 0, 0],
      `孔${hole.index} ${hole.sizeLabel}`,
      0x8fe0a9,
    );
    if (hole.faceTangent && hole.centerToFaceEdge > 0) {
      appendAnnotation(
        annotations,
        add(hole.center, scale(hole.faceTangent, -hole.centerToFaceEdge)),
        add(hole.center, scale(hole.faceTangent, hole.centerToFaceEdge)),
        scale(axis, hole.halfSpanAlong * 1.4),
        `孔${hole.index} 中心距边 ${formatMillimeters(hole.centerToFaceEdgeNegative)} / ${formatMillimeters(hole.centerToFaceEdgePositive)} mm`,
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

function formatPoint(point) {
  return `(${formatMillimeters(point.x)}, ${formatMillimeters(point.y)}, ${formatMillimeters(point.z)})`;
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
