export const arcAngleKeys = ["startAngleDeg", "sweepAngleDeg"];
export const normalizeArcAngle = value => { const angle = value % 360;return angle < 0 ? angle + 360 : angle || 0; };
export const arcSweep = (start, end) => { const sweep = (end - start) % 360;return sweep <= 0 ? sweep + 360 : sweep; };

export function dimensionNumber(text, key) {
  if (!/^[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[-+]?\d+)?$/i.test(String(text).trim())) return null;
  const value = Number(text);if (!Number.isFinite(value)) return null;
  if (key === "startAngleDeg") return normalizeArcAngle(value);
  return value > 0 && (key !== "sweepAngleDeg" || value <= 360) ? value : null;
}

export const dimensionInputError = key => key === "startAngleDeg" ? "请输入有限的起始角（°）。"
  : key === "sweepAngleDeg" ? "扫过角须大于 0 且不超过 360°。" : "请输入大于 0 的有限数值。";

export function arcPoint(center, radius, angle) {
  const radians = normalizeArcAngle(angle) * Math.PI / 180;
  return [center[0] + radius * Math.cos(radians), center[1] + radius * Math.sin(radians)];
}

export function arcDimensions(parameters, originals = []) {
  const { center, radius, startAngleDeg, sweepAngleDeg } = parameters, alpha = normalizeArcAngle(startAngleDeg), c = [center[0], center[1], 0];
  const point = angle => [...arcPoint(center, radius, angle), 0];
  const seed = key => originals.find(item => (item.key ?? item.parameter) === key) ?? { id: key, key, parameter: key, editable: true };
  return [{ ...seed("radius"), key: "radius", label: "R", kind: "radius", unit: "mm", value: radius, start: c, end: point(startAngleDeg + sweepAngleDeg / 2) },
    { ...seed("startAngleDeg"), key: "startAngleDeg", label: "α", kind: "angle", unit: "deg", value: alpha, center: c, radius, startAngleDeg: 0, sweepAngleDeg: alpha, start: point(0), end: point(alpha) },
    { ...seed("sweepAngleDeg"), key: "sweepAngleDeg", label: "θ", kind: "angle", unit: "deg", value: sweepAngleDeg, center: c, radius, startAngleDeg, sweepAngleDeg, start: point(startAngleDeg), end: point(startAngleDeg + sweepAngleDeg) }];
}
