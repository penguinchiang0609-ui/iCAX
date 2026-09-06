import { loadRenderResource } from "../../../iCAX-UI/SDK/Viewport/renderResource.mjs";

const hydrationEpochs = new WeakMap();

export function scheduleDesignerPartThumbnailHydration(context) {
  const scheduled = beginHydrationEpoch(context);
  if (!scheduled) return;
  queueMicrotask(() => {
    if (!isHydrationCurrent(scheduled)) return;
    const indeterminateCheckboxes = context.mount?.querySelectorAll?.("[data-tube-designer-indeterminate=\"true\"]") ?? [];
    for (const checkbox of indeterminateCheckboxes) checkbox.indeterminate = true;
    void hydrateDesignerPartThumbnails(context, scheduled);
  });
}

export function cancelDesignerPartThumbnailHydration(context) {
  beginHydrationEpoch(context);
}

async function hydrateDesignerPartThumbnails(context, scheduled) {
  const resourceClient = context.sceneProxy?.resources;
  const canvases = context.mount?.querySelectorAll?.("[data-tube-designer-part-thumbnail]") ?? [];
  for (const canvas of canvases) {
    if (!isHydrationCurrent(scheduled) || !canvas.isConnected) return;
    if (!(canvas instanceof HTMLCanvasElement) || canvas.dataset.tubeThumbnailReady === "true") continue;
    canvas.dataset.tubeThumbnailReady = "true";
    drawFallback(canvas);
    canvas.dataset.tubeThumbnailSource = "fallback";
    const url = String(canvas.dataset.tubePreviewUrl ?? "");
    if (!resourceClient || !url) continue;
    try {
      const resource = await loadRenderResource(resourceClient, {
        url,
        version: Number(canvas.dataset.tubePreviewVersion ?? 0),
      });
      if (!isHydrationCurrent(scheduled)) return;
      if (canvas.isConnected && resource?.type === "geometry" && resource.data?.kind === "mesh") {
        drawMesh(canvas, resource.data);
        canvas.dataset.tubeThumbnailSource = "resource";
      }
    } catch {
      // 清单仍保留中性示意图；资源服务恢复后重新打开清单即可重新加载。
    }
  }
}

function beginHydrationEpoch(context) {
  const mount = context?.mount;
  if (!mount || (typeof mount !== "object" && typeof mount !== "function")) return null;
  const epoch = Number(hydrationEpochs.get(mount) ?? 0) + 1;
  hydrationEpochs.set(mount, epoch);
  return { mount, epoch };
}

function isHydrationCurrent(scheduled) {
  return Boolean(scheduled)
    && hydrationEpochs.get(scheduled.mount) === scheduled.epoch;
}

function drawMesh(canvas, mesh) {
  const context = canvas.getContext("2d");
  const positions = mesh.positions ?? [];
  if (!context || positions.length < 9) return;
  const projected = projectThumbnailVertices(positions);
  let minX = Infinity;
  let minY = Infinity;
  let maxX = -Infinity;
  let maxY = -Infinity;
  for (const point of projected) {
    minX = Math.min(minX, point.x);
    minY = Math.min(minY, point.y);
    maxX = Math.max(maxX, point.x);
    maxY = Math.max(maxY, point.y);
  }
  const padding = 8;
  const spanX = Math.max(1e-6, maxX - minX);
  const spanY = Math.max(1e-6, maxY - minY);
  const scale = Math.min((canvas.width - padding * 2) / spanX, (canvas.height - padding * 2) / spanY);
  const offsetX = (canvas.width - spanX * scale) * 0.5 - minX * scale;
  const offsetY = (canvas.height - spanY * scale) * 0.5 - minY * scale;
  const indices = mesh.indices?.length
    ? mesh.indices
    : Uint32Array.from({ length: projected.length }, (_, index) => index);
  const triangles = [];
  const triangleStep = Math.max(3, Math.ceil(indices.length / 6000 / 3) * 3);
  for (let offset = 0; offset + 2 < indices.length; offset += triangleStep) {
    const a = projected[indices[offset]];
    const b = projected[indices[offset + 1]];
    const c = projected[indices[offset + 2]];
    if (a && b && c) triangles.push({ a, b, c, depth: (a.depth + b.depth + c.depth) / 3 });
  }
  triangles.sort((left, right) => left.depth - right.depth);
  paintBackground(context, canvas);
  const minDepth = triangles[0]?.depth ?? 0;
  const maxDepth = triangles[triangles.length - 1]?.depth ?? minDepth + 1;
  for (const triangle of triangles) {
    const tone = (triangle.depth - minDepth) / Math.max(1e-6, maxDepth - minDepth);
    const light = Math.round(128 + tone * 40);
    context.beginPath();
    context.moveTo(triangle.a.x * scale + offsetX, triangle.a.y * scale + offsetY);
    context.lineTo(triangle.b.x * scale + offsetX, triangle.b.y * scale + offsetY);
    context.lineTo(triangle.c.x * scale + offsetX, triangle.c.y * scale + offsetY);
    context.closePath();
    context.fillStyle = `rgb(${light - 24}, ${light + 4}, ${light + 10})`;
    context.fill();
    context.strokeStyle = "rgba(52, 76, 84, 0.16)";
    context.lineWidth = 0.7;
    context.stroke();
  }
}

export function projectThumbnailVertices(positions) {
  const vertexCount = Math.floor((positions?.length ?? 0) / 3);
  if (!vertexCount) return [];

  const center = [0, 0, 0];
  const minimum = [Infinity, Infinity, Infinity];
  const maximum = [-Infinity, -Infinity, -Infinity];
  for (let offset = 0; offset + 2 < positions.length; offset += 3) {
    for (let axis = 0; axis < 3; axis += 1) {
      const value = Number(positions[offset + axis]);
      center[axis] += value;
      minimum[axis] = Math.min(minimum[axis], value);
      maximum[axis] = Math.max(maximum[axis], value);
    }
  }
  for (let axis = 0; axis < 3; axis += 1) center[axis] /= vertexCount;

  const lengthAxis = principalAxis(positions, center, minimum, maximum);
  const reference = Math.abs(dot3(lengthAxis, [0, 0, 1])) < 0.9
    ? [0, 0, 1]
    : [0, 1, 0];
  const sideAxis = normalize3(cross3(reference, lengthAxis), [0, 1, 0]);
  const upAxis = normalize3(cross3(lengthAxis, sideAxis), [0, 0, 1]);

  const projected = [];
  for (let offset = 0; offset + 2 < positions.length; offset += 3) {
    const point = [
      Number(positions[offset]) - center[0],
      Number(positions[offset + 1]) - center[1],
      Number(positions[offset + 2]) - center[2],
    ];
    const along = dot3(point, lengthAxis);
    const side = dot3(point, sideAxis);
    const up = dot3(point, upAxis);
    projected.push({
      x: along + side * 0.34,
      y: side * 0.32 - up * 0.78,
      depth: side * 0.68 + up * 0.32,
    });
  }
  return projected;
}

function principalAxis(positions, center, minimum, maximum) {
  const covariance = [
    [0, 0, 0],
    [0, 0, 0],
    [0, 0, 0],
  ];
  for (let offset = 0; offset + 2 < positions.length; offset += 3) {
    const value = [
      Number(positions[offset]) - center[0],
      Number(positions[offset + 1]) - center[1],
      Number(positions[offset + 2]) - center[2],
    ];
    for (let row = 0; row < 3; row += 1) {
      for (let column = 0; column < 3; column += 1) {
        covariance[row][column] += value[row] * value[column];
      }
    }
  }

  const spans = maximum.map((value, axis) => value - minimum[axis]);
  const seedAxis = spans.indexOf(Math.max(...spans));
  let axis = [0, 0, 0];
  axis[seedAxis] = 1;
  for (let iteration = 0; iteration < 12; iteration += 1) {
    const next = covariance.map((row) => dot3(row, axis));
    axis = normalize3(next, axis);
  }
  return axis;
}

function dot3(left, right) {
  return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

function cross3(left, right) {
  return [
    left[1] * right[2] - left[2] * right[1],
    left[2] * right[0] - left[0] * right[2],
    left[0] * right[1] - left[1] * right[0],
  ];
}

function normalize3(value, fallback) {
  const length = Math.hypot(value[0], value[1], value[2]);
  return length > 1e-9 ? value.map((entry) => entry / length) : [...fallback];
}

function drawFallback(canvas) {
  const context = canvas.getContext("2d");
  if (!context) return;
  paintBackground(context, canvas);
  context.save();
  context.translate(canvas.width * 0.5, canvas.height * 0.5);
  context.rotate(-0.18);
  const width = canvas.width * 0.72;
  const height = canvas.height * 0.32;
  context.fillStyle = "#78959d";
  context.strokeStyle = "#3f5d65";
  context.lineWidth = 2;
  context.fillRect(-width / 2, -height / 2, width, height);
  context.strokeRect(-width / 2, -height / 2, width, height);
  context.fillStyle = "#edf2f3";
  context.fillRect(-width / 2 + 6, -height / 2 + 6, width - 12, height - 12);
  context.restore();
}

function paintBackground(context, canvas) {
  const gradient = context.createLinearGradient(0, 0, canvas.width, canvas.height);
  gradient.addColorStop(0, "#f7faf9");
  gradient.addColorStop(1, "#e6edef");
  context.fillStyle = gradient;
  context.fillRect(0, 0, canvas.width, canvas.height);
}
