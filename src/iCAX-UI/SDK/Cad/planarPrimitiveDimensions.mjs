import { arcDimensions } from "./planarDimensionValues.mjs";
import { pointDistance } from "./planarCurveSegments.mjs";

const rad = degrees => degrees * Math.PI / 180;
const starInnerAngle = state => (state.current.rotationDeg ?? 0) + 180 / (Number(state.primitiveOptions?.tips) || 5);
const at = (state, radius, angle = state.current.rotationDeg ?? 0) => [state.anchor[0] + radius * Math.cos(rad(angle)), state.anchor[1] + radius * Math.sin(rad(angle))];
const ellipseAt = (state, a, b, angle) => {
  const rotation = rad(state.current.rotationDeg ?? 0), t = rad(angle), x = a * Math.cos(t), y = b * Math.sin(t);
  return [state.anchor[0] + x * Math.cos(rotation) - y * Math.sin(rotation), state.anchor[1] + x * Math.sin(rotation) + y * Math.cos(rotation)];
};

export const primitiveDimensionFields = primitive => ({ ellipse: ["radiusX", "radiusY"], ellipseArc: ["radiusX", "radiusY", "sweepAngleDeg"], polygon: ["radius"], star: ["outerRadius", "innerRadius"] })[primitive] ?? null;
export const primitiveDimensionLabels = [["radiusX", "A"], ["radiusY", "B"], ["outerRadius", "R"], ["innerRadius", "r"]];

export function synchronizePrimitiveDimensionConstruction(state, points) {
  const previous = state.phase, primitive = state.primitive;
  state.constructionPoints = points.map(point => [...point]);
  const ellipse = primitive === "ellipse" || primitive === "ellipseArc";
  state.phase = ellipse ? points.length >= 3 ? "ellipse-sweep" : points.length === 2 ? "ellipse-minor" : "ellipse-axis"
    : primitive === "star" ? points.length >= 2 ? "star-inner" : "star-outer" : "radius";
  const lock = (key, value) => {
    state.current[key] = value;
    Object.assign(state.drafts[key], { text: String(value), dirty: true, locked: true, lastValid: value });
  };
  if (points.length >= 2) {
    const d = points[1].map((value, i) => value - state.anchor[i]);
    state.current.rotationDeg = Math.atan2(d[1], d[0]) * 180 / Math.PI;
    if (ellipse) lock("radiusX", Math.hypot(...d));
    if (primitive === "star") {
      lock("outerRadius", Math.hypot(...d));
      if (previous !== state.phase) state.current.innerRadius = state.current.outerRadius * .5;
    }
  }
  if (ellipse && points.length >= 3) {
    const angle = rad(state.current.rotationDeg), d = points[2].map((value, i) => value - state.anchor[i]);
    const minor = -d[0] * Math.sin(angle) + d[1] * Math.cos(angle);
    state.minorDirection = Math.sign(minor) || 1;lock("radiusY", Math.abs(minor));
  }
  if (previous !== state.phase) {
    state.activeField = state.phase === "ellipse-axis" ? "radiusX" : state.phase === "ellipse-minor" ? "radiusY"
      : state.phase === "ellipse-sweep" ? "sweepAngleDeg" : state.phase === "star-outer" ? "outerRadius" : state.phase === "star-inner" ? "innerRadius" : "radius";
  }
}

export function recordPrimitiveDimensionPoint(state, point) {
  const [dx, dy] = point.map((value, axis) => value - state.anchor[axis]), length = Math.hypot(dx, dy);
  if (state.phase === "ellipse-axis") {
    state.current.radiusX = length;state.current.radiusY = length * .6;
    if (length > 1e-9) state.current.rotationDeg = Math.atan2(dy, dx) * 180 / Math.PI;
  } else if (state.phase === "ellipse-minor") {
    const angle = rad(state.current.rotationDeg), signed = -dx * Math.sin(angle) + dy * Math.cos(angle);
    state.current.radiusY = Math.abs(signed);if (Math.abs(signed) > 1e-9) state.minorDirection = Math.sign(signed);
  } else if (state.phase === "ellipse-sweep") {
    const angle = rad(state.current.rotationDeg), x = (dx * Math.cos(angle) + dy * Math.sin(angle)) / state.current.radiusX,
      y = (-dx * Math.sin(angle) + dy * Math.cos(angle)) / state.current.radiusY;
    let sweep = Math.atan2(y, x) * 180 / Math.PI;
    if ((state.minorDirection ?? 1) > 0 && sweep <= 0) sweep += 360;
    if ((state.minorDirection ?? 1) < 0 && sweep >= 0) sweep -= 360;
    state.current.sweepAngleDeg = sweep;
  } else if (state.phase === "star-inner") state.current.innerRadius = length;
  else {
    state.current[state.primitive === "star" ? "outerRadius" : "radius"] = length;
    if (state.primitive === "star") state.current.innerRadius = length * .5;
    if (length > 1e-9) state.current.rotationDeg = Math.atan2(dy, dx) * 180 / Math.PI;
  }
}

export function constrainPrimitiveDimensionPoint(state, values) {
  if (state.phase === "ellipse-axis") return at(state, values.radiusX);
  if (state.phase === "ellipse-minor") return at(state, values.radiusY * (state.minorDirection ?? 1), (state.current.rotationDeg ?? 0) + 90);
  if (state.phase === "ellipse-sweep") return ellipseAt(state, values.radiusX, values.radiusY, values.sweepAngleDeg);
  if (state.phase === "star-inner") {
    const point = state.mousePoint, angle = point ? Math.atan2(point[1] - state.anchor[1], point[0] - state.anchor[0]) * 180 / Math.PI : starInnerAngle(state);
    return at(state, values.innerRadius, angle);
  }
  return at(state, values[state.primitive === "star" ? "outerRadius" : "radius"]);
}

export function primitiveDimensionDescriptors(state, value) {
  const center = [...state.anchor, 0], rotation = state.current.rotationDeg ?? 0;
  const radius = (key, label, size, angle) => ({ key, label, kind: "radius", value: size, start: center, end: [...at(state, size, angle), 0] });
  if (state.primitive === "polygon") return [radius("radius", "R", value("radius"), rotation)];
  if (state.primitive === "star") return [radius("outerRadius", "R", value("outerRadius"), rotation), radius("innerRadius", "r", value("innerRadius"), starInnerAngle(state))];
  const result = [radius("radiusX", "A", value("radiusX"), rotation), radius("radiusY", "B", value("radiusY"), rotation + 90 * (state.minorDirection ?? 1))];
  if (state.primitive === "ellipseArc" && state.phase === "ellipse-sweep") {
    const sweep = value("sweepAngleDeg"), descriptor = arcDimensions({ center: state.anchor, radius: Math.min(value("radiusX"), value("radiusY")) * .6,
      startAngleDeg: rotation + Math.min(0, sweep), sweepAngleDeg: Math.abs(sweep) }).find(item => item.key === "sweepAngleDeg");
    if (descriptor) result.push({ ...descriptor, value: sweep });
  }
  return result;
}

// A dimension Enter may consume earlier entered dimensions as a single
// construction prefix, e.g. A -> Tab -> B -> Enter with no extra canvas click.
export function primitiveDimensionAction(state, field, values) {
  const center = [...state.anchor], primitive = state.primitive;
  if (primitive === "polygon") return { preparePoints: [center], point: at(state, values.radius) };
  if (primitive === "star") {
    const outer = at(state, values.outerRadius);
    if (field === "outerRadius") return { preparePoints: [center], point: outer, nextField: "innerRadius" };
    if (!(values.innerRadius < values.outerRadius)) return { error: "星形内半径必须小于外半径。", field: "innerRadius" };
    return { preparePoints: [center, outer], point: at(state, values.innerRadius, starInnerAngle(state)) };
  }
  const axis = at(state, values.radiusX), minor = at(state, values.radiusY * (state.minorDirection ?? 1), (state.current.rotationDeg ?? 0) + 90);
  if (field === "radiusX") return { preparePoints: [center], point: axis, nextField: "radiusY" };
  if (field === "radiusY") return { preparePoints: [center, axis], point: minor, ...(primitive === "ellipseArc" ? { nextField: "sweepAngleDeg" } : {}) };
  return { preparePoints: [center, axis, minor], point: ellipseAt(state, values.radiusX, values.radiusY, values.sweepAngleDeg), sweepAngleDeg: values.sweepAngleDeg };
}
