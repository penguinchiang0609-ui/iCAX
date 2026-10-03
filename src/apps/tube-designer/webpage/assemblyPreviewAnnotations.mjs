import { buildBendPreviewDimensionAnnotations } from "./bendPreviewAnnotations.mjs";

const FINISHED_COLOR = 0x66d7ca;
const BLANK_COLOR = 0xffc857;
const MAX_DECLARED = 64;

const finite = (value) => typeof value === "number" && Number.isFinite(value);
const vector = (value) => Array.isArray(value) && value.length === 3 && value.every(finite);
const matrix = (value) => Array.isArray(value) && value.length === 16 && value.every(finite);
const format = (value) => Number(value.toFixed(2)).toString();
const normalizedView = (mode) => mode === "exploded" || mode === "blank" || mode === true ? "blank" : "finished";

function transformPoint(pose, [x, y, z]) {
  return [
    pose[0] * x + pose[1] * y + pose[2] * z + pose[3],
    pose[4] * x + pose[5] * y + pose[6] * z + pose[7],
    pose[8] * x + pose[9] * y + pose[10] * z + pose[11],
  ];
}

function transformVector(pose, [x, y, z]) {
  return [
    pose[0] * x + pose[1] * y + pose[2] * z,
    pose[4] * x + pose[5] * y + pose[6] * z,
    pose[8] * x + pose[9] * y + pose[10] * z,
  ];
}

function sectionClearance(request) {
  const params = request?.parameters ?? {};
  const width = Number(params.width), depth = Number(params.depth);
  const extent = Math.max(Number.isFinite(width) ? width : 0,
    Number.isFinite(depth) ? depth : 0);
  return Math.max(30, extent / 2 + 24);
}

function blankLengthDimension(part, index) {
  const length = part?.request?.length;
  const pose = part?.explodedMatrix ?? part?.matrix;
  if (!finite(length) || length <= 0 || !matrix(pose)) return null;
  const name = String(part.label ?? part.blankId ?? part.id ?? "下料件").trim().slice(0, 42);
  const spacing = sectionClearance(part.request) + index % 2 * 16;
  return {
    id: `manufacturing-length:${part.id ?? part.blankId ?? index}`,
    start: transformPoint(pose, [0, 0, 0]),
    end: transformPoint(pose, [length, 0, 0]),
    offset: transformVector(pose, [0, -spacing, 0]),
    label: `下料基准长 · ${name} ${format(length)} mm`,
    color: BLANK_COLOR,
  };
}

function finishedExteriorDimensions(bounds, preview) {
  const min = bounds?.min, max = bounds?.max;
  if (!vector(min) || !vector(max)) return [];
  const spans = min.map((value, index) => max[index] - value);
  if (spans.some((value) => !finite(value) || value < 0)) return [];
  const longest = Math.max(...spans);
  if (longest <= 1e-6) return [];
  // A tube's section size remains in the product controls. The viewport only
  // marks the significant outer spans, keeping straight assemblies to one
  // overall dimension rather than one line per overlapping member.
  const axes = [0, 1, 2].filter((axis) => spans[axis] > 1e-6 && spans[axis] >= longest * 0.25);
  const approximate = Boolean(preview?.finishedShape?.mesh?.metadata?.approximate
    || preview?.formedPreview?.mesh?.metadata?.approximate);
  const caption = preview?.stockProcess ? "成形校核外廓" : approximate ? "成品示意外廓" : "成品外廓";
  const corners = [
    [[min[0], min[1], min[2]], [max[0], min[1], min[2]], [0, -1, 0]],
    [[max[0], min[1], min[2]], [max[0], max[1], min[2]], [1, 0, 0]],
    [[max[0], max[1], min[2]], [max[0], max[1], max[2]], [1, 0, 0]],
  ];
  return axes.map((axis) => ({
    id: `finished-envelope:${"xyz"[axis]}`,
    start: corners[axis][0],
    end: corners[axis][1],
    offset: corners[axis][2].map((value) => value * Math.max(24, longest * 0.08)),
    label: axes.length === 1
      ? `${approximate ? "成品示意总长" : "成品总长"} ${format(spans[axis])} mm`
      : `${caption} ${"XYZ"[axis]} 向 ${format(spans[axis])} mm`,
    color: FINISHED_COLOR,
  }));
}

function finishedLAngle(preview) {
  if (preview?.layoutShape !== "l" || !preview?.finishedShape) return null;
  const productPlan = preview.finishedPlan ?? preview.plan;
  const [first, second] = productPlan?.designParts ?? [];
  const lengthA = Number(first?.request?.length), lengthB = Number(second?.request?.length);
  const scene = productPlan?.sceneParameters ?? {};
  const metadata = preview.finishedShape.mesh?.metadata;
  const angle = Number(metadata?.angle ?? scene.angle ?? scene.jointAngle);
  const rotation = Number(metadata?.planeRotation ?? scene.planeRotation ?? 0) * Math.PI / 180;
  if (!finite(lengthA) || !finite(lengthB) || lengthA <= 0 || lengthB <= 0
      || !finite(angle) || angle <= 0 || angle >= 180 || !finite(rotation)) return null;
  const radians = angle * Math.PI / 180;
  const direction = metadata?.endDirection ?? [Math.cos(radians),
    -Math.sin(radians) * Math.sin(rotation), Math.sin(radians) * Math.cos(rotation)];
  const normal = metadata?.planeNormal ?? [0, Math.cos(rotation), Math.sin(rotation)];
  if (!vector(direction) || !vector(normal)) return null;
  const leg = Math.min(lengthA, lengthB, 65);
  const distance = sectionClearance(first.request) + 80;
  return {
    id: "finished-angle",
    start: [-leg, 0, 0],
    end: direction.map((coordinate) => coordinate * leg),
    offset: normal.map((coordinate) => distance * coordinate),
    label: `成品转角 θ=${format(angle)}°`,
    color: FINISHED_COLOR,
  };
}

function previewDeclarations(plan, view) {
  return (Array.isArray(plan?.previewAnnotations) ? plan.previewAnnotations : [])
    .slice(0, MAX_DECLARED).filter((item) => item?.view === view);
}

function applyBlankLengthDeclarations(preview, dimensions) {
  const plan = preview.plan;
  const result = [...dimensions];
  const seen = new Set();
  for (const item of previewDeclarations(plan, "blank")) {
    if (item.kind !== "length-blank") continue;
    const parts = plan.manufacturingParts ?? [];
    const index = parts.findIndex((part) => part.blankId === item.blankId);
    if (index < 0 || seen.has(index)) continue;
    seen.add(index);
    const part = parts[index], length = part.request?.length;
    if (!finite(length) || length <= 0) continue;
    const automaticId = `manufacturing-length:${part.id ?? part.blankId ?? index}`;
    let slot = result.findIndex((dimension) => dimension.id === automaticId);
    if (slot < 0 && preview.formedPreview?.annotations?.kind === "cold-bend-development")
      slot = result.findIndex((dimension) => dimension.id === "total-blank");
    const base = slot >= 0 ? result[slot] : blankLengthDimension(part, index);
    if (!base) continue;
    const title = String(item.label ?? part.label ?? "下料长度").trim().slice(0, 64) || "下料长度";
    const label = `${title}${title.includes("下料基准长") ? "" : "（下料基准长）"} ${format(length)} mm`;
    const declared = { ...base, id: `declared:${String(item.id ?? automaticId).slice(0, 80)}`, label };
    if (slot >= 0) result[slot] = declared;
    else result.push(declared);
  }
  return result;
}

/** Resolved template notes have values but no trustworthy measurement line. */
export function buildAssemblyPreviewValueNotes(preview, mode = "finished") {
  const plan = normalizedView(mode) === "finished" ? preview?.finishedPlan ?? preview?.plan : preview?.plan;
  if (!plan) return [];
  return previewDeclarations(plan, normalizedView(mode)).flatMap((item) => {
    if (item.kind !== "value-note") return [];
    const label = String(item.label ?? "").trim();
    const value = item.value;
    if (!label || label.length > 120 || !(finite(value)
        || typeof value === "boolean" || typeof value === "string" && value.length <= 120)) return [];
    return [{ id: String(item.id ?? "").slice(0, 80), label, value,
      unit: String(item.unit ?? "").slice(0, 20) }];
  });
}

/**
 * Finished dimensions come from the currently displayed geometry's world
 * bounds, after all part cuts and placements. Design-part request.length is a
 * component setting, and blank request.length is an input stock length.
 */
export function buildAssemblyPreviewDimensionAnnotations(preview, mode = "finished", finishedBounds = null) {
  const plan = preview?.plan;
  if (!plan) return [];
  const view = normalizedView(mode);
  if (view === "finished") {
    const dimensions = finishedExteriorDimensions(finishedBounds, preview);
    const angle = finishedLAngle(preview)
      ?? (preview?.formedPreview?.annotations?.kind === "cold-bend-development"
        ? buildBendPreviewDimensionAnnotations(preview, "finished")[0] : null);
    if (angle) dimensions.push(angle);
    return dimensions.map((dimension) => ({ ...dimension, placement: "outside" }));
  }
  const dimensions = preview?.formedPreview?.annotations?.kind === "cold-bend-development"
    ? buildBendPreviewDimensionAnnotations(preview, "exploded")
    : (plan.manufacturingParts ?? []).map(blankLengthDimension).filter(Boolean);
  return applyBlankLengthDeclarations(preview, dimensions).map((dimension) => ({
    ...dimension, placement: "outside",
  }));
}
