import { loadRenderResource } from "../../../iCAX-UI/SDK/Viewport/renderResource.mjs";

export function scheduleTubeWorkpieceThumbnailHydration(context) {
  queueMicrotask(() => {
    void hydrateTubeWorkpieceThumbnails(context);
  });
}

async function hydrateTubeWorkpieceThumbnails(context) {
  const resourceClient = context.sceneProxy?.resources;
  const canvases = context.mount?.querySelectorAll?.("[data-tube-workpiece-thumbnail]") ?? [];
  for (const canvas of canvases) {
    if (!(canvas instanceof HTMLCanvasElement) || canvas.dataset.tubeThumbnailReady === "true") {
      continue;
    }
    canvas.dataset.tubeThumbnailReady = "true";
    const innerCavities = Number(canvas.dataset.tubeInnerCavities ?? 0);
    drawFallback(canvas, innerCavities);
    canvas.dataset.tubeThumbnailSource = "fallback";
    const url = String(canvas.dataset.tubePreviewUrl ?? "");
    if (!resourceClient || !url) {
      continue;
    }
    try {
      const resource = await loadRenderResource(resourceClient, {
        url,
        version: Number(canvas.dataset.tubePreviewVersion ?? 0),
      });
      if (canvas.isConnected && resource?.type === "geometry" && resource.data?.kind === "mesh") {
        drawMesh(canvas, resource.data);
        canvas.dataset.tubeThumbnailSource = "resource";
      }
    } catch {
      // 卡片已有稳定的中性管材示意图；缩略资源暂时不可用不影响工件操作。
    }
  }
}

function drawMesh(canvas, mesh) {
  const context = canvas.getContext("2d");
  const positions = mesh.positions ?? [];
  if (!context || positions.length < 9) {
    return;
  }
  const projected = [];
  let minX = Infinity;
  let minY = Infinity;
  let maxX = -Infinity;
  let maxY = -Infinity;
  for (let offset = 0; offset + 2 < positions.length; offset += 3) {
    const x = Number(positions[offset]);
    const y = Number(positions[offset + 1]);
    const z = Number(positions[offset + 2]);
    const screenX = x * 0.82 - y * 0.5;
    const screenY = x * 0.24 + y * 0.36 - z * 0.72;
    const depth = x * 0.35 + y * 0.58 + z * 0.42;
    projected.push({ x: screenX, y: screenY, depth });
    minX = Math.min(minX, screenX);
    minY = Math.min(minY, screenY);
    maxX = Math.max(maxX, screenX);
    maxY = Math.max(maxY, screenY);
  }
  const padding = 10;
  const spanX = Math.max(1e-6, maxX - minX);
  const spanY = Math.max(1e-6, maxY - minY);
  const scale = Math.min(
    (canvas.width - padding * 2) / spanX,
    (canvas.height - padding * 2) / spanY,
  );
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
    if (a && b && c) {
      triangles.push({ a, b, c, depth: (a.depth + b.depth + c.depth) / 3 });
    }
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

function drawFallback(canvas, innerCavities) {
  const context = canvas.getContext("2d");
  if (!context) return;
  paintBackground(context, canvas);
  context.save();
  context.translate(canvas.width * 0.5, canvas.height * 0.5);
  context.rotate(-0.18);
  const width = canvas.width * 0.7;
  const height = canvas.height * 0.36;
  const x = -width * 0.5;
  const y = -height * 0.5;
  context.fillStyle = "#78959d";
  context.strokeStyle = "#3f5d65";
  context.lineWidth = 2;
  roundedRect(context, x, y, width, height, 8);
  context.fill();
  context.stroke();
  const cavityCount = Math.min(3, Math.max(1, innerCavities || 1));
  const gap = width / cavityCount;
  context.fillStyle = "#e9eff0";
  for (let index = 0; index < cavityCount; index += 1) {
    roundedRect(context, x + index * gap + 7, y + 7, gap - 14, height - 14, 4);
    context.fill();
  }
  context.restore();
}

function paintBackground(context, canvas) {
  const gradient = context.createLinearGradient(0, 0, canvas.width, canvas.height);
  gradient.addColorStop(0, "#f6f9f8");
  gradient.addColorStop(1, "#e6edef");
  context.fillStyle = gradient;
  context.fillRect(0, 0, canvas.width, canvas.height);
}

function roundedRect(context, x, y, width, height, radius) {
  const r = Math.min(radius, width * 0.5, height * 0.5);
  context.beginPath();
  context.moveTo(x + r, y);
  context.arcTo(x + width, y, x + width, y + height, r);
  context.arcTo(x + width, y + height, x, y + height, r);
  context.arcTo(x, y + height, x, y, r);
  context.arcTo(x, y, x + width, y, r);
  context.closePath();
}
