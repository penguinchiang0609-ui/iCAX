import { Vector3 } from "../ThirdParty/three/three.module.js";

export const dimensionValueText = value => Number.isFinite(Number(value)) ? String(Number(Number(value).toFixed(4))) : "—";

export function dimensionScale(viewport) {
  const canvas = viewport?.root?.querySelector("canvas"), box = canvas?.getBoundingClientRect();
  if (!box) return 1;
  const x = box.left + box.width / 2, y = box.top + box.height / 2;
  const a = viewport.pointOnWorkPlane(x, y, [0, 0, 1]), b = viewport.pointOnWorkPlane(x + 1, y, [0, 0, 1]);
  return a && b ? Math.hypot(b[0] - a[0], b[1] - a[1]) || 1 : 1;
}

export function dimensionAnnotation(viewport, descriptor) {
  if (descriptor.kind === "angle") {
    const scale = dimensionScale(viewport), radius = Number(descriptor.radius), key = descriptor.key ?? descriptor.parameter;
    return { id: descriptor.id ?? descriptor.key, kind: "angle", center: descriptor.center,
      radius: key === "includedAngleDeg" ? radius : key === "startAngleDeg" ? Math.max(scale * 22, radius * .32) : radius + scale * 30,
      startAngleDeg: descriptor.startAngleDeg, sweepAngleDeg: descriptor.sweepAngleDeg,
      label: "", color: 0x27c27a };
  }
  const distance = descriptor.kind === "radius" ? 0 : dimensionScale(viewport) * 30;
  const direction = descriptor.offsetDirection ?? [0, 1, 0];
  return { id: descriptor.id ?? descriptor.key, start: descriptor.start, end: descriptor.end,
    offset: descriptor.offset ?? direction.map(value => value * distance), label: "", color: 0x27c27a };
}

const presentations = new WeakMap();
export function showDimensionLines(viewport, owner, descriptors) {
  if (!viewport) return;
  const state = presentations.get(viewport) ?? {};presentations.set(viewport, state);
  const annotations = descriptors.map(descriptor => dimensionAnnotation(viewport, descriptor));
  const signature = `${owner}:${JSON.stringify(annotations)}`;
  if (signature !== state.signature) {
    viewport?.setDimensionAnnotations(annotations);state.signature = signature;
  }
  state.owner = owner;
}

export function clearDimensionLines(viewport, owner) {
  const state = presentations.get(viewport);if (!state) return;
  if (state.owner !== owner) return;
  viewport?.clearDimensionAnnotations();delete state.owner;delete state.signature;
}

export function projectDimensionAnchor(viewport, descriptor) {
  const canvas = viewport?.root?.querySelector("canvas"), box = canvas?.getBoundingClientRect();
  if (!viewport?.camera || !box) return null;
  const annotation = dimensionAnnotation(viewport, descriptor), point = new Vector3();
  if (descriptor.kind === "angle") {
    const angle = (annotation.startAngleDeg % 360 + annotation.sweepAngleDeg / 2) * Math.PI / 180;
    point.fromArray(annotation.center).add(new Vector3(annotation.radius * Math.cos(angle), annotation.radius * Math.sin(angle), 0));
  } else point.fromArray(descriptor.start).lerp(new Vector3().fromArray(descriptor.end), .5).add(new Vector3().fromArray(annotation.offset));
  viewport.content.updateMatrixWorld(true);viewport.camera.updateMatrixWorld(true);
  point.applyMatrix4(viewport.content.matrixWorld).project(viewport.camera);
  return { x: box.left + (point.x + 1) * box.width / 2, y: box.top + (1 - point.y) * box.height / 2, box };
}

export function placeDimensionLabel(viewport, root, element, descriptor, state, editing = false) {
  const anchor = projectDimensionAnchor(viewport, descriptor);if (!anchor) return;
  const key = descriptor.key ?? descriptor.parameter;
  state.positions ??= {};state.fixedPositions ??= {};
  let position = editing ? state.fixedPositions[key] ?? state.positions[key] : null;
  if (!position) {
    const halfWidth = element.offsetWidth / 2, halfHeight = element.offsetHeight / 2;
    position = { x: Math.max(anchor.box.left + halfWidth + 4, Math.min(anchor.x, anchor.box.right - halfWidth - 4)),
      y: Math.max(anchor.box.top + halfHeight + 4, Math.min(anchor.y, anchor.box.bottom - halfHeight - 4)) };
  }
  state.positions[key] = { ...position };if (editing) state.fixedPositions[key] ??= { ...position };
  element.style.left = `${position.x}px`;element.style.top = `${position.y}px`;
  element.dataset.planarDimensionAnchorX = String(anchor.x);element.dataset.planarDimensionAnchorY = String(anchor.y);
  const svg = root.querySelector("[data-planar-dimension-leaders]");
  if (!svg) return;
  svg.setAttribute("viewBox", `0 0 ${innerWidth} ${innerHeight}`);
  let leader = svg.querySelector(`[data-planar-dimension-leader="${key}"]`);
  if (!leader) { leader = root.ownerDocument.createElementNS("http://www.w3.org/2000/svg", "path");leader.dataset.planarDimensionLeader = key;svg.appendChild(leader); }
  leader.setAttribute("d", Math.hypot(position.x - anchor.x, position.y - anchor.y) > 2 ? `M ${anchor.x} ${anchor.y} L ${position.x} ${position.y}` : "");
}

export function renderDimensionLeaderLayer() { return `<svg class="icax-planar-dimension-leaders" data-planar-dimension-leaders aria-hidden="true"></svg>`; }
