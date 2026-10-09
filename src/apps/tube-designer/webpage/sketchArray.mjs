import { editableSegments } from "./sketchGeometry.mjs";

const EPS = 1e-9;
const finite = (value) => (typeof value === "number" || (typeof value === "string" && value.trim() !== ""))
  && Number.isFinite(Number(value));
const positiveInteger = (value) => finite(value) && Number.isSafeInteger(Number(value)) && Number(value) >= 1;
const validPoint = (value) => Array.isArray(value) && value.length >= 2 && finite(value[0]) && finite(value[1]);

/** Counts include the source instance; copyCount is the number of new entities. */
export function validateArraySpec(spec, sourceCount, totalEntityCount, maxEntities = 5000) {
  let copyCount = 0;
  const invalid = (message) => ({ ready: false, message, copyCount });
  if (!positiveInteger(sourceCount)) return invalid("请先选择需要阵列的图形");
  if (!finite(totalEntityCount) || !Number.isSafeInteger(Number(totalEntityCount)) || Number(totalEntityCount) < 0
      || !positiveInteger(maxEntities)) return invalid("图形数量无效");
  if (!spec || !["rectangular", "polar"].includes(spec.kind)) return invalid("请选择二维阵列或圆周阵列");
  if (spec.groupCenter !== undefined && !validPoint(spec.groupCenter)) return invalid("所选图形的中心坐标无效");

  let instanceCount;
  if (spec.kind === "rectangular") {
    if (!positiveInteger(spec.columns) || !positiveInteger(spec.rows)) return invalid("列数和行数必须是正整数");
    instanceCount = Number(spec.columns) * Number(spec.rows);
    if (!Number.isSafeInteger(instanceCount)) return invalid("阵列数量过大");
    copyCount = (instanceCount - 1) * Number(sourceCount);
    if (instanceCount < 2) return invalid("阵列至少需要两份图形，数量包含原件");
    if (!finite(spec.spacingX === undefined ? 0 : spec.spacingX) || !finite(spec.spacingY === undefined ? 0 : spec.spacingY)) return invalid("列间距和行间距必须是有限数值");
    if (Number(spec.columns) > 1 && Math.abs(Number(spec.spacingX ?? 0)) <= EPS) return invalid("多列阵列的列间距不能为零");
    if (Number(spec.rows) > 1 && Math.abs(Number(spec.spacingY ?? 0)) <= EPS) return invalid("多行阵列的行间距不能为零");
    if (!Number.isFinite((Number(spec.columns) - 1) * Number(spec.spacingX ?? 0))
        || !Number.isFinite((Number(spec.rows) - 1) * Number(spec.spacingY ?? 0))) return invalid("阵列范围过大");
  } else {
    if (!positiveInteger(spec.count) || Number(spec.count) < 2) return invalid("圆周阵列数量至少为 2，数量包含原件");
    instanceCount = Number(spec.count);
    copyCount = (instanceCount - 1) * Number(sourceCount);
    if (!finite(spec.centerX) || !finite(spec.centerY)) return invalid("请输入有效的阵列中心坐标");
    if (!finite(spec.angleStep) || Math.abs(Number(spec.angleStep)) <= EPS) return invalid("相邻角度不能为零");
    if (Math.abs(Number(spec.angleStep)) * (instanceCount - 1) >= 360 - EPS) return invalid("阵列角度不能绕过或重复一整圈，请减小数量或相邻角度");
    if (spec.rotateItems !== undefined && typeof spec.rotateItems !== "boolean") return invalid("图形旋转选项无效");
    if (spec.rotateItems === false && validPoint(spec.groupCenter)
        && Math.hypot(Number(spec.groupCenter[0]) - Number(spec.centerX), Number(spec.groupCenter[1]) - Number(spec.centerY)) <= EPS) {
      return invalid("保持图形方向时，阵列中心不能与所选图形中心重合");
    }
  }
  if (!Number.isSafeInteger(copyCount) || copyCount + Number(totalEntityCount) > Number(maxEntities)) {
    return invalid(`阵列后图形总数不能超过 ${Number(maxEntities)} 个`);
  }
  return { ready: true, message: `将新增 ${copyCount} 个图形`, copyCount };
}

/** Rigid model-space matrix [a,b,c,d,e,f], matching SVG's matrix convention. */
export function arrayTransformMatrix(transform = {}) {
  const dx = Number(transform.dx ?? 0), dy = Number(transform.dy ?? 0);
  const angle = Number(transform.angle ?? 0), center = transform.center ?? [0, 0];
  const groupCenter = transform.groupCenter ?? center;
  if (![dx, dy, angle].every(Number.isFinite) || !validPoint(center)
      || (transform.rotateItems === false && !validPoint(groupCenter))) throw new RangeError("阵列变换参数无效");
  const clean = (value) => Math.abs(value) < 1e-14 ? 0 : value;
  const cosine = clean(Math.cos(angle)), sine = clean(Math.sin(angle));
  const cx = Number(center[0]), cy = Number(center[1]);
  if (transform.rotateItems === false) {
    const gx = Number(groupCenter[0]), gy = Number(groupCenter[1]);
    return [1, 0, 0, 1,
      dx + cx + cosine * (gx - cx) - sine * (gy - cy) - gx,
      dy + cy + sine * (gx - cx) + cosine * (gy - cy) - gy];
  }
  return [cosine, sine, clean(-sine), cosine,
    dx + cx - cosine * cx + sine * cy,
    dy + cy - sine * cx - cosine * cy];
}

/** Produce each additional instance; [0,0] / angle zero is the original. */
export function buildArrayTransforms(spec) {
  // The editor's entity limit also bounds preview instance allocation.
  if (!validateArraySpec(spec, 1, 1).ready) return [];
  const group = spec.groupCenter === undefined ? {} : { groupCenter: spec.groupCenter.map(Number) };
  if (spec.kind === "rectangular") {
    const result = [];
    for (let row = 0; row < Number(spec.rows); row += 1) {
      for (let column = 0; column < Number(spec.columns); column += 1) {
        if (row || column) result.push({ dx: column ? column * Number(spec.spacingX ?? 0) : 0, dy: row ? row * Number(spec.spacingY ?? 0) : 0, ...group });
      }
    }
    return result;
  }
  return Array.from({ length: Number(spec.count) - 1 }, (_, index) => ({
    angle: (index + 1) * Number(spec.angleStep) * Math.PI / 180,
    center: [Number(spec.centerX), Number(spec.centerY)],
    rotateItems: spec.rotateItems !== false,
    ...group,
  }));
}

function entityCenter(entity) {
  const points = [];
  const collect = (value) => {
    if (value.kind === "path") value.segments.forEach(collect);
    else if (value.kind === "line") points.push([value.x1, value.y1], [value.x2, value.y2]);
    else if (value.kind === "rectangle") points.push([value.x, value.y], [value.x + value.width, value.y + value.height]);
    else if (["circle", "circleArc", "ellipse", "ellipseArc"].includes(value.kind)) {
      // The parent conic is a conservative bound for an arc. No tessellation
      // or curve approximation is needed to find a stable standalone pivot.
      const radius = Math.max(value.radius ?? 0, value.radiusX ?? 0, value.radiusY ?? 0);
      points.push([value.cx - radius, value.cy - radius], [value.cx + radius, value.cy + radius]);
    } else if (value.kind === "text") points.push([value.x, value.y]);
    else if (Array.isArray(value.points)) points.push(...value.points);
  };
  collect(entity);
  if (!points.length) return [0, 0];
  let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
  for (const point of points) {
    minX = Math.min(minX, point[0]); maxX = Math.max(maxX, point[0]);
    minY = Math.min(minY, point[1]); maxY = Math.max(maxY, point[1]);
  }
  return [minX / 2 + maxX / 2, minY / 2 + maxY / 2];
}

/** Copy exact editable geometry. It deliberately does not assign a new ID. */
export function transformArrayEntity(entity, transform = {}) {
  const effective = transform.rotateItems === false && transform.groupCenter === undefined
    ? { ...transform, groupCenter: entityCenter(entity) } : transform;
  const matrix = arrayTransformMatrix(effective);
  const [a, b, c, d, e, f] = matrix;
  const point = ([x, y]) => [a * x + c * y + e, b * x + d * y + f];
  const angle = Math.atan2(b, a);
  const transformed = structuredClone(entity);
  const apply = (value) => {
    if (value.kind === "rectangle" && Math.abs(angle) > 1e-14) {
      const segments = editableSegments(value);
      value.kind = "path"; value.closed = true; value.segments = segments;
      delete value.x; delete value.y; delete value.width; delete value.height; delete value.radius;
      segments.forEach(apply);
    } else if (value.kind === "path") value.segments.forEach(apply);
    else if (value.kind === "line") {
      [value.x1, value.y1] = point([value.x1, value.y1]);
      [value.x2, value.y2] = point([value.x2, value.y2]);
    } else if (value.kind === "rectangle" || value.kind === "text") {
      [value.x, value.y] = point([value.x, value.y]);
      if (value.kind === "text" && angle) value.rotation = (value.rotation ?? 0) + angle;
    } else if (["circle", "circleArc", "ellipse", "ellipseArc"].includes(value.kind)) {
      [value.cx, value.cy] = point([value.cx, value.cy]);
      if (value.kind === "circleArc" && angle) value.startAngle += angle;
      else if (["ellipse", "ellipseArc"].includes(value.kind) && angle) value.rotation = (value.rotation ?? 0) + angle;
    } else if (Array.isArray(value.points)) value.points = value.points.map(point);
  };
  apply(transformed);
  return transformed;
}
