import * as THREE from "../ThirdParty/three/three.module.js";
import { curveSegmentSamples, pointDistance } from "./planarCurveSegments.mjs";
import { PlanarPrimitiveSession, standalonePrimitiveDefinitions } from "./planarDrawingPrimitives.mjs";
const TAU = Math.PI * 2;
const segmentSamples = arc => { const count = Math.max(8, Math.ceil(Math.abs(arc.sweep) / TAU * 128));return Array.from({length: count + 1}, (_, index) => {const angle = arc.startAngle + arc.sweep * index / count;return [arc.cx + arc.radius * Math.cos(angle),arc.cy + arc.radius * Math.sin(angle)];}); };

const mod = angle => (angle % TAU + TAU) % TAU;
const vector = ([x, y]) => new THREE.Vector3(x, y, 0);

export function createPlanarDrawingPreview(viewport) {
  if (!viewport?.content) return null;
  const group = new THREE.Group();
  group.name = "Planar editing preview";group.userData.planarDrawingPreview = true;
  const material = new THREE.LineBasicMaterial({ color: 0x137d9f, depthTest: false, depthWrite: false });
  const guideMaterial = new THREE.LineDashedMaterial({ color: 0x6b9aab, dashSize: 4, gapSize: 3, depthTest: false, depthWrite: false });
  const anchorMaterial = new THREE.PointsMaterial({ color: 0x137d9f, size: 6, sizeAttenuation: false, depthTest: false, depthWrite: false });
  const outline = new THREE.Line(new THREE.BufferGeometry(), material);
  const guides = new THREE.LineSegments(new THREE.BufferGeometry(), guideMaterial);
  const anchors = new THREE.Points(new THREE.BufferGeometry(), anchorMaterial);
  for (const object of [outline, guides, anchors]) { object.raycast = () => {};object.renderOrder = 1000;group.add(object); }
  viewport.content.add(group);
  const preview = { viewport, group, outline, guides, anchors, material, guideMaterial, anchorMaterial };
  preview.update = (primitive, accepted, pointer, options) => updateDrawingPreview(preview, primitive, accepted, pointer, options);
  preview.clear = () => clearDrawingPreview(preview);preview.dispose = () => disposeDrawingPreview(preview);
  return preview;
}

function setPoints(object, points) {
  object.geometry.dispose();
  object.geometry = new THREE.BufferGeometry().setFromPoints(points.map(vector));
  object.visible = points.length > 0;
}

// Display-only sampling. Exact points/radii still go to the native command.
function updateDrawingPreview(preview, primitive, accepted, pointer, { closed = false, arc = null, segments = null, controls = [], primitiveOptions = {} } = {}) {
  if (!preview) return;
  const points = accepted.map(point => [...point]);
  if (pointer) points.push(pointer);
  let outline = [], guides = [];
  const standalone = standalonePrimitiveDefinitions[primitive] ? new PlanarPrimitiveSession(primitive, accepted, primitiveOptions).preview(pointer) : null;
  if (standalone) { outline = standalone.outline;guides = standalone.guides;segments = standalone.segments;controls = standalone.controls; }
  if (primitive === "polyline" || primitive === "line") outline = points;
  if (primitive === "polyline" && closed && points.length >= 3) outline = [...points, points[0]];
  if (primitive === "polyline" && segments) {
    outline = segments.flatMap((segment, index) => curveSegmentSamples(segment).slice(index ? 1 : 0));
    if (closed && outline.length > 1 && pointDistance(outline[0], outline.at(-1)) > 1e-9) outline.push([...outline[0]]);
    for (let index = 1; index < controls.length; index++) guides.push(controls[index - 1], controls[index]);
  }
  if (primitive === "rect" && points.length >= 2) {
    const [a, b] = points;outline = [a, [b[0], a[1]], b, [a[0], b[1]], a];
  }
  if (["circle", "arc"].includes(primitive) && points.length >= 2) {
    const [center, start, end] = points;
    const radius = arc?.radius ?? Math.hypot(start[0] - center[0], start[1] - center[1]);
    const startAngle = primitive === "circle" ? 0 : arc ? arc.startAngleDeg * Math.PI / 180 : Math.atan2(start[1] - center[1], start[0] - center[0]);
    // Native CCW arc semantics use one complete turn for coincident angles.
    const sweep = primitive === "circle" ? TAU : arc ? arc.sweepAngleDeg * Math.PI / 180 : end ? mod(Math.atan2(end[1] - center[1], end[0] - center[0]) - startAngle) || TAU : 0;
    if (sweep && radius) outline = segmentSamples({ kind: "circleArc", cx: center[0], cy: center[1], radius, startAngle, sweep });
    guides = [center, start];
    if (end && outline.length) guides.push(center, outline.at(-1));
  }
  setPoints(preview.outline, outline);setPoints(preview.guides, guides);
  const anchors = standalone ? controls.length ? controls : accepted : [...accepted, ...controls.slice(1)];
  setPoints(preview.anchors, anchors.length ? anchors : pointer ? [pointer] : []);
  preview.guides.computeLineDistances();
  preview.group.visible = Boolean(points.length);
  preview.group.userData.primitive = primitive;
  preview.group.userData.acceptedPointCount = accepted.length;
  preview.group.userData.outlinePointCount = outline.length;
  preview.group.userData.segmentTypes = segments?.map(segment => segment.type) ?? [];
  preview.group.userData.controlPointCount = controls.length;

}

function clearDrawingPreview(preview) {
  if (preview) { preview.group.visible = false;Object.assign(preview.group.userData, { acceptedPointCount: 0, outlinePointCount: 0, segmentTypes: [], controlPointCount: 0 });for (const object of [preview.outline, preview.guides, preview.anchors]) setPoints(object, []); }

}

function disposeDrawingPreview(preview) {
  if (!preview) return;
  clearDrawingPreview(preview);preview.group.removeFromParent();
  for (const object of [preview.outline, preview.guides, preview.anchors]) object.geometry.dispose();
  for (const material of [preview.material, preview.guideMaterial, preview.anchorMaterial]) material.dispose();

}
