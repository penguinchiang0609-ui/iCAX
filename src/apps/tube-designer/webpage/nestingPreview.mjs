import { parseRenderGeometryResource } from "../../../iCAX-UI/SDK/Viewport/renderResource.mjs";

const controllers = new WeakMap();
const sourceCaches = new WeakMap();
const resourceNamespaces = new WeakMap();
// Selection yellow is reserved exclusively for the current part. Normal plan
// colors deliberately omit yellow so the selection always reads immediately.
const HIGHLIGHT_COLOR = "#ffd400";
const COLORS = ["#1677ff", "#f04b23", "#08b9d6", "#ad35d6", "#18a66f", "#ef7d17"];
const emptyResources = { get: async () => new Response(null, { status: 404 }) };
let sequence = 0;

export function getNestingPlacementColor(index, highlighted = false) {
  if (highlighted) return HIGHLIGHT_COLOR;
  const value = Number.isFinite(Number(index)) ? Math.max(0, Math.trunc(Number(index))) : 0;
  return COLORS[value % COLORS.length];
}

/** Only owns cutting-plan content; product and single-part rendering remain unchanged. */
export function scheduleNestingPlanHydration(context, view, plan, parts = []) {
  if (!view || !plan) return;
  const partMap = new Map(parts.map((part) => [String(part.entityId), part]));
  const key = JSON.stringify([plan, view.tubeDesignerActiveNestingPlacementId ?? "",
    view.tubeDesignerActiveNestingPartId ?? "", (plan.placements ?? []).map((placement) => {
    const part = partMap.get(String(placement.partId));
    return [part?.thumbnailGeometryResourceId, part?.thumbnailGeometryResourceVersion];
  })]);
  const previous = controllers.get(view);
  if (previous?.key === key && previous.viewport === view.viewport) {
    // The shared workbench clears the visible-id filter on every render for a
    // custom view. Repeated clicks/renders must therefore restore the already
    // hydrated plan even when no geometry reload is needed.
    if (previous.entityIds?.length) view.viewport?.setVisibleEntityIds?.(previous.entityIds);
    queueMicrotask(() => { if (isCurrent(view, previous)) publish(context, view, previous.state); });
    return;
  }
  cancelNestingPlanHydration(view);
  const controller = { key, viewport: view.viewport, planId: String(plan.id), applying: false };
  controllers.set(view, controller);
  controller.state = { status: "loading", planId: controller.planId, message: "正在载入排样三维结果…" };
  queueMicrotask(() => {
    if (!isCurrent(view, controller)) return;
    publish(context, view, controller.state);
    void hydrate(context, view, controller, plan, partMap);
  });
}

export function cancelNestingPlanHydration(view) {
  const previous = controllers.get(view);
  controllers.delete(view);
  if (previous?.applying && previous.viewport?.applyViewSnapshot) {
    // Starting another snapshot invalidates an in-flight resource load immediately.
    void previous.viewport.applyViewSnapshot({ revision: `nesting-cancel:${++sequence}`, rows: [] }, emptyResources).catch(() => {});
  }
}

function isCurrent(view, controller) {
  return controllers.get(view) === controller
    && view.viewport === controller.viewport
    && view.activeAreaId === "nesting"
    && view.tubeDesignerNestingSelectionKind === "plan"
    && (!view.tubeDesignerActiveNestingPlanId || String(view.tubeDesignerActiveNestingPlanId) === controller.planId);
}

async function hydrate(context, view, controller, plan, partMap) {
  try {
    if (!controller.viewport?.applyViewSnapshot) throw new Error("三维视口尚未准备好。");
    const sourceClient = context.sceneProxy?.resources;
    const synthetic = new Map();
    let namespace = resourceNamespaces.get(plan);
    if (!namespace || namespace.key !== controller.key) {
      namespace = { key: controller.key, base: `icax-nesting-preview://${++sequence}` };
      resourceNamespaces.set(plan, namespace);
    }
    const base = namespace.base;
    const rows = [];
    const hullPoints = [];
    const envelopePartIds = new Set();
    let approximateCount = 0;
    const stockLength = Number(plan.stockLength);
    if (!(stockLength > 0) || !Number.isFinite(stockLength)) throw new Error("母材长度无效。");
    const placements = Array.isArray(plan.placements) ? plan.placements : [];
    if (!placements.length) throw new Error("当前母材没有零件排入。");
    const sourceByPart = new Map();
    // Each actual part mesh is fetched once, even when its quantity places it repeatedly.
    for (const placement of placements) {
      const id = String(placement.partId);
      if (sourceByPart.has(id)) continue;
      if (!isCurrent(view, controller)) return;
      const part = partMap.get(id);
      try {
        sourceByPart.set(id, await readPartSource(view, sourceClient, part));
      } catch {
        sourceByPart.set(id, null);
      }
    }
    if (!isCurrent(view, controller)) return;
    for (const [index, placement] of placements.entries()) {
      const part = partMap.get(String(placement.partId));
      const source = sourceByPart.get(String(placement.partId));
      let geometry;
      let matrix;
      if (source) {
        const bounds = meshBounds(source.mesh);
        matrix = nestingPlacementMatrix(bounds, placement);
        geometry = { url: source.url, version: source.version };
        // A cross-section envelope is only used for stock/remnant indication, not parts.
        if (!envelopePartIds.has(String(placement.partId))) {
          envelopePartIds.add(String(placement.partId));
          for (let offset = 0; offset < source.mesh.positions.length; offset += 3) {
            hullPoints.push([source.mesh.positions[offset + 1] - bounds.center[1], source.mesh.positions[offset + 2] - bounds.center[2]]);
          }
        }
      } else {
        const length = Number(placement.end) - Number(placement.start);
        const contour = fallbackContour(part?.profile ?? plan.profileData);
        hullPoints.push(...contour);
        geometry = addGeometry(synthetic, `${base}/fallback/${index}`, prism(contour, length));
        matrix = translationMatrix(Number(placement.start));
        approximateCount++;
      }
      const placementId = String(placement.instanceId ?? `${placement.partId}#${index + 1}`);
      const activePlacementId = String(view.tubeDesignerActiveNestingPlacementId ?? "");
      const activePartId = String(view.tubeDesignerActiveNestingPartId ?? "");
      const highlighted = activePlacementId
        ? placementId === activePlacementId
        : Boolean(activePartId && String(placement.partId) === activePartId);
      const color = getNestingPlacementColor(index, highlighted);
      const materialUrl = `${base}/color/${color.slice(1)}`;
      synthetic.set(materialUrl, encodeMaterial(linearColorRGBA(color)));
      rows.push({
        entityId: `nesting:${plan.id}:${placementId}`,
        data: { geometry, material: { url: materialUrl }, localToWorldMatrix: matrix,
          geometryKind: 1, renderClass: 1, visible: true, selectable: false },
      });
    }
    const contour = convexHull(hullPoints);
    if (contour.length < 3) throw new Error("不能确定母材截面包络。");
    const envelopeMaterial = `${base}/envelope-material`;
    synthetic.set(envelopeMaterial, encodeMaterial(0x9badb780));
    rows.push({ entityId: `nesting:${plan.id}:stock-envelope`, data: {
      geometry: addGeometry(synthetic, `${base}/stock-envelope`, outline(contour, stockLength)),
      material: { url: envelopeMaterial }, geometryKind: 2, visible: true, selectable: false,
    } });
    const usedLength = Math.max(...placements.map((placement) => Number(placement.end)));
    if (stockLength - usedLength > 0.001) {
      rows.push({ entityId: `nesting:${plan.id}:remnant`, data: {
        geometry: addGeometry(synthetic, `${base}/remnant`, prism(contour, stockLength - usedLength)),
        material: { url: envelopeMaterial }, geometryKind: 1,
        localToWorldMatrix: translationMatrix(usedLength), visible: true, selectable: false,
      } });
    }
    const cachedBuffers = new Map([...sourceByPart.values()].filter(Boolean).map((source) => [source.url, source.buffer]));
    const resources = {
      async get(url, options) {
        const bytes = synthetic.get(String(url)) ?? cachedBuffers.get(String(url));
        if (bytes) return new Response(bytes, { status: 200 });
        return sourceClient?.get(url, options) ?? new Response(null, { status: 404 });
      },
    };
    if (!isCurrent(view, controller)) return;
    controller.viewport.setDimensionAnnotations?.([]);
    controller.viewport.setPresentationAxis?.([1, 0, 0], [1, 0, 0]);
    controller.applying = true;
    const receipt = await controller.viewport.applyViewSnapshot({ revision: base, rows }, resources);
    controller.applying = false;
    if (!isCurrent(view, controller)) return;
    if (!receipt?.applied || receipt.entityIds?.length !== rows.length) throw new Error("排样几何没有完整进入三维视图。");
    controller.entityIds = [...receipt.entityIds];
    view.tubeDesignerPartViewportKey = null;
    controller.viewport.setStandardView?.("top-front");
    controller.viewport.fitViewToViewport?.(1.16);
    controller.state = {
      status: "ready", planId: controller.planId, entityCount: rows.length,
      partCount: placements.length, approximate: approximateCount > 0, approximateCount,
      message: approximateCount ? `${approximateCount} 件零件资源不可用，暂以截面包络示意；灰色为余料包络。` : "彩色：实际零件 · 灰色：余料包络",
    };
    publish(context, view, controller.state);
  } catch (error) {
    controller.applying = false;
    if (!isCurrent(view, controller)) return;
    // Do not leave the previous part/plan displayed as if it were this result.
    await controller.viewport?.applyViewSnapshot?.({ revision: `nesting-error:${++sequence}`, rows: [] }, emptyResources).catch(() => {});
    if (!isCurrent(view, controller)) return;
    controller.state = { status: "error", planId: controller.planId, message: error?.message ?? String(error) };
    publish(context, view, controller.state);
  }
}

function publish(context, view, state) {
  view.tubeDesignerNestingPreviewState = state;
  const status = context.mount?.querySelector?.("[data-tube-designer-nesting-preview-status]");
  if (status) {
    status.textContent = state.message || "";
    status.dataset.status = state.status;
    status.setAttribute("role", state.status === "error" ? "alert" : "status");
  }
  const header = context.mount?.querySelector?.(".tube-designer-cutting-scene-header");
  if (header) {
    header.dataset.nestingPreviewStatus = state.status;
    header.dataset.nestingPreviewPlan = state.planId;
    header.dataset.nestingPreviewEntityCount = String(state.entityCount ?? 0);
  }
}

async function readPartSource(view, resourceClient, part) {
  const url = String(part?.thumbnailGeometryResourceId ?? "");
  const version = Number(part?.thumbnailGeometryResourceVersion ?? 0);
  if (!url || !resourceClient?.get) throw new Error("零件没有三维资源。");
  let cache = sourceCaches.get(view);
  if (!cache) sourceCaches.set(view, cache = new Map());
  const key = `${url}@${version}`;
  if (!cache.has(key)) {
    cache.set(key, (async () => {
      const headers = new Headers({ Accept: "application/vnd.icax.flatbuffer" });
      if (version) headers.set("ICAX-Resource-Version", String(version));
      const response = await resourceClient.get(url, { headers });
      if (!response.ok) throw new Error(`零件三维资源读取失败 (${response.status})。`);
      const buffer = await response.arrayBuffer();
      const mesh = parseRenderGeometryResource(buffer);
      if (mesh.kind !== "mesh") throw new Error("零件资源不是实体网格。");
      meshBounds(mesh);
      return { url, version, buffer, mesh };
    })().catch((error) => { cache.delete(key); throw error; }));
  }
  return cache.get(key);
}

export function meshBounds(mesh) {
  const positions = mesh?.positions ?? [];
  if (positions.length < 9 || positions.length % 3) throw new Error("零件网格为空或不完整。");
  const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < positions.length; i++) {
    const value = Number(positions[i]);
    if (!Number.isFinite(value)) throw new Error("零件网格包含无效坐标。");
    min[i % 3] = Math.min(min[i % 3], value);
    max[i % 3] = Math.max(max[i % 3], value);
  }
  return { min, max, center: min.map((value, i) => (value + max[i]) / 2) };
}

/** Row-major rigid matrix, in mm. Disassembly already normalizes the long axis to +X. */
export function nestingPlacementMatrix(bounds, placement) {
  const start = Number(placement.start), end = Number(placement.end);
  if (!Number.isFinite(start) || !Number.isFinite(end) || start < 0 || end <= start) throw new Error("零件排入位置无效。");
  if (bounds.max[0] - bounds.min[0] > end - start + 0.1) throw new Error("实际零件长度超出排样占用长度，请重新排样。");
  const rotation = Number(placement.rotationRadians ?? 0);
  if (!Number.isFinite(rotation)) throw new Error("零件绕轴旋转角无效。");
  const xDirection = placement.reversed ? -1 : 1;
  const yDirection = placement.reversed ? -1 : 1;
  const cosine = Math.cos(rotation), sine = Math.sin(rotation);
  const m00 = xDirection;
  const m11 = cosine * yDirection, m12 = -sine;
  const m21 = sine * yDirection, m22 = cosine;
  return [m00, 0, 0, (start + end) / 2 - m00 * bounds.center[0],
    0, m11, m12, -(m11 * bounds.center[1] + m12 * bounds.center[2]),
    0, m21, m22, -(m21 * bounds.center[1] + m22 * bounds.center[2]), 0, 0, 0, 1];
}

function translationMatrix(x) { return [1, 0, 0, x, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]; }

function fallbackContour(profile = {}) {
  const width = Number(profile.width ?? profile.diameter), depth = Number(profile.depth ?? profile.height ?? width);
  if (!(width > 0 && depth > 0) || !Number.isFinite(width + depth)) throw new Error("零件三维资源不可用，且没有有效截面尺寸。");
  const kind = String(profile.kind ?? profile.id ?? "").toLowerCase();
  if (/round|circle|ellipse/.test(kind)) return Array.from({ length: 48 }, (_, i) => [Math.cos(i / 48 * Math.PI * 2) * width / 2, Math.sin(i / 48 * Math.PI * 2) * depth / 2]);
  return [[-width / 2, -depth / 2], [width / 2, -depth / 2], [width / 2, depth / 2], [-width / 2, depth / 2]];
}

function convexHull(points) {
  const unique = [...new Map(points.map((p) => [p.map((x) => x.toFixed(5)).join(","), p])).values()]
    .sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  if (unique.length <= 2) return unique;
  const cross = (o, a, b) => (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
  const half = (ordered) => {
    const hull = [];
    for (const point of ordered) {
      while (hull.length >= 2 && cross(hull.at(-2), hull.at(-1), point) <= 1e-8) hull.pop();
      hull.push(point);
    }
    return hull.slice(0, -1);
  };
  return [...half(unique), ...half([...unique].reverse())];
}

function prism(contour, length) {
  const positions = [0, length].flatMap((x) => contour.flatMap(([y, z]) => [x, y, z]));
  const indices = [], n = contour.length;
  for (let i = 0; i < n; i++) {
    const j = (i + 1) % n;
    indices.push(i, j, n + j, i, n + j, n + i);
  }
  for (let i = 1; i < n - 1; i++) indices.push(0, i + 1, i, n, n + i, n + i + 1);
  return { kind: 1, positions, indices };
}

function outline(contour, length) {
  const positions = [], ranges = [];
  const segment = (a, b) => { ranges.push(positions.length / 3, 2, 0, 0, 0); positions.push(...a, ...b); };
  for (let i = 0; i < contour.length; i++) {
    const [y, z] = contour[i], [ny, nz] = contour[(i + 1) % contour.length];
    segment([0, y, z], [0, ny, nz]); segment([length, y, z], [length, ny, nz]);
    // End rings establish stock extent without painting gray longitudinal lines
    // over colored parts. A thin round tube can project to only a few pixels high.
  }
  return { kind: 2, positions, ranges };
}

function addGeometry(resources, url, geometry) {
  resources.set(url, encodeNestingGeometry(geometry));
  return { url, version: 1 };
}

/** Small schema-specific FlatBuffer writer for local preview resources, never project data. */
export function encodeNestingGeometry({ kind = 1, positions, indices = [], ranges = [] }) {
  return encodeResource("ICRG", 15, [[0, 1], [1, kind]], [[5, positions, true], [9, indices, false], [10, ranges, false]]);
}

function encodeMaterial(color) { return encodeResource("ICRM", 10, [[0, 1], [2, color]], []); }

function linearColorRGBA(cssColor) {
  // The viewport's MeshPhongMaterial consumes numeric channels as linear RGB.
  // Convert our display/swatch colors first; otherwise THREE washes them to pastels.
  const packed = Number.parseInt(cssColor.slice(1), 16);
  const channel = (shift) => {
    const srgb = ((packed >>> shift) & 255) / 255;
    return Math.round((srgb <= 0.04045 ? srgb / 12.92 : ((srgb + 0.055) / 1.055) ** 2.4) * 255);
  };
  return ((channel(16) << 24) | (channel(8) << 16) | (channel(0) << 8) | 255) >>> 0;
}

function encodeResource(identifier, fieldCount, scalars, vectors) {
  const vtable = 8, vtableLength = 4 + fieldCount * 2;
  const table = Math.ceil((vtable + vtableLength) / 8) * 8;
  const tableLength = 4 + fieldCount * 4;
  const bytes = table + tableLength + vectors.reduce((sum, [, data]) => sum + 4 + data.length * 4, 0);
  const buffer = new ArrayBuffer(bytes), data = new DataView(buffer);
  data.setUint32(0, table, true);
  for (let i = 0; i < 4; i++) data.setUint8(4 + i, identifier.charCodeAt(i));
  data.setUint16(vtable, vtableLength, true); data.setUint16(vtable + 2, tableLength, true);
  data.setInt32(table, table - vtable, true);
  const fieldOffset = (field) => {
    const offset = 4 + field * 4;
    data.setUint16(vtable + 4 + field * 2, offset, true);
    return table + offset;
  };
  for (const [field, value] of scalars) data.setUint32(fieldOffset(field), value >>> 0, true);
  let cursor = table + tableLength;
  for (const [field, values, floating] of vectors) {
    if (!values.length) continue;
    const offset = fieldOffset(field);
    data.setUint32(offset, cursor - offset, true); data.setUint32(cursor, values.length, true);
    for (let i = 0; i < values.length; i++) {
      if (floating) data.setFloat32(cursor + 4 + i * 4, values[i], true);
      else data.setUint32(cursor + 4 + i * 4, values[i], true);
    }
    cursor += 4 + values.length * 4;
  }
  return buffer;
}
