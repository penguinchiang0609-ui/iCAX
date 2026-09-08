import { escapeAttr, formatNumber } from "../../_shared/workbench/utils/format.mjs";

// Shared by thumbnails and dimension diagrams, keeping exact curve handling identical.
export function profileSvgGeometry(profile) {
  const rendered = (Array.isArray(profile?.contours) ? profile.contours : []).map(renderSvgContour);
  const bounds = rendered.reduce((result, item) => includeBounds(result, item.bounds), emptyBounds());
  return { markup: rendered.map((item) => item.markup).join(""), bounds: validBounds(bounds) ? bounds : null };
}

export function renderProfileSvg(profile) {
  const contours = Array.isArray(profile?.contours) ? profile.contours : [];
  const rendered = contours.map((contour, index) => renderSvgContour(contour, index));
  const geometryBounds = rendered.reduce((bounds, item) => includeBounds(bounds, item.bounds), emptyBounds());
  const fallbackWidth = positiveNumber(profile?.width, 100);
  const fallbackDepth = positiveNumber(profile?.depth, 100);
  const bounds = validBounds(geometryBounds) ? geometryBounds : {
    minX: -fallbackWidth / 2,
    minY: -fallbackDepth / 2,
    maxX: fallbackWidth / 2,
    maxY: fallbackDepth / 2,
  };
  const spanX = Math.max(bounds.maxX - bounds.minX, 1.0e-6);
  const spanY = Math.max(bounds.maxY - bounds.minY, 1.0e-6);
  const reference = Math.max(spanX, spanY);
  const paddingX = Math.max(spanX * 0.12, reference * 0.025, 1.0e-3);
  const paddingY = Math.max(spanY * 0.12, reference * 0.025, 1.0e-3);
  const viewBox = [
    bounds.minX - paddingX,
    -bounds.maxY - paddingY,
    spanX + paddingX * 2,
    spanY + paddingY * 2,
  ].map(svgNumber).join(" ");
  const fallback = `<rect x="${svgNumber(-fallbackWidth / 2)}" y="${svgNumber(-fallbackDepth / 2)}" width="${svgNumber(fallbackWidth)}" height="${svgNumber(fallbackDepth)}" rx="2" />`;
  return `<svg class="tube-profile-library-svg" viewBox="${viewBox}" preserveAspectRatio="xMidYMid meet" role="img" aria-label="${escapeAttr(profile?.name ?? "管型截面")}">
    <g transform="scale(1,-1)">${rendered.map((item) => item.markup).join("") || fallback}</g>
  </svg>`;
}


function renderSvgContour(contour, index) {
  const hole = index > 0;
  const className = hole ? "hole" : "outer";
  const kind = String(contour?.kind ?? "");
  if (kind === "circle") {
    const center = point(contour?.center) ?? [0, 0];
    const radius = positiveNumber(contour?.radius, 0);
    return {
      markup: radius > 0 ? `<circle class="${className}" cx="${svgNumber(center[0])}" cy="${svgNumber(center[1])}" r="${svgNumber(radius)}" />` : "",
      bounds: radius > 0 ? { minX: center[0] - radius, minY: center[1] - radius, maxX: center[0] + radius, maxY: center[1] + radius } : emptyBounds(),
    };
  }
  if (kind === "ellipse") {
    const center = point(contour?.center) ?? [0, 0];
    const rx = positiveNumber(contour?.radiusX, positiveNumber(contour?.width, 0) / 2);
    const ry = positiveNumber(contour?.radiusY, positiveNumber(contour?.height, 0) / 2);
    const rotation = finiteNumber(contour?.rotation, 0);
    const bounds = ellipseBounds(center, rx, ry, rotation);
    const transform = Math.abs(rotation) > 1.0e-12
      ? ` transform="rotate(${svgNumber(rotation * 180 / Math.PI)} ${svgNumber(center[0])} ${svgNumber(center[1])})"`
      : "";
    return {
      markup: rx > 0 && ry > 0 ? `<ellipse class="${className}" cx="${svgNumber(center[0])}" cy="${svgNumber(center[1])}" rx="${svgNumber(rx)}" ry="${svgNumber(ry)}"${transform} />` : "",
      bounds,
    };
  }
  if (kind === "roundedRectangle" || kind === "capsule") {
    const center = point(contour?.center) ?? [0, 0];
    const width = positiveNumber(contour?.width, 0);
    const height = positiveNumber(contour?.height, 0);
    const radius = kind === "capsule" ? Math.min(width, height) / 2 : Math.max(0, finiteNumber(contour?.radius, 0));
    return {
      markup: width > 0 && height > 0 ? `<rect class="${className}" x="${svgNumber(center[0] - width / 2)}" y="${svgNumber(center[1] - height / 2)}" width="${svgNumber(width)}" height="${svgNumber(height)}" rx="${svgNumber(Math.min(radius, width / 2, height / 2))}" />` : "",
      bounds: width > 0 && height > 0 ? { minX: center[0] - width / 2, minY: center[1] - height / 2, maxX: center[0] + width / 2, maxY: center[1] + height / 2 } : emptyBounds(),
    };
  }
  if (kind === "polygon") {
    const points = (Array.isArray(contour.points) ? contour.points : []).map(point).filter(Boolean);
    return {
      markup: points.length > 2 ? `<polygon class="${className}" points="${points.map(svgPoint).join(" ")}" />` : "",
      bounds: boundsFromPoints(points),
    };
  }
  if (kind === "path") {
    return renderSvgPath(contour?.segments, className);
  }
  return { markup: "", bounds: emptyBounds() };
}


function renderSvgPath(segments, className) {
  const rendered = (Array.isArray(segments) ? segments : []).map(renderSvgPathSegment).filter(Boolean);
  if (!rendered.length) return { markup: "", bounds: emptyBounds() };
  const commands = [];
  let current = null;
  let bounds = emptyBounds();
  for (const segment of rendered) {
    if (!current || pointDistance(current, segment.start) > 1.0e-7) {
      commands.push(`M ${svgPoint(segment.start)}`);
    }
    commands.push(segment.command);
    current = segment.end;
    bounds = includeBounds(bounds, segment.bounds);
  }
  return {
    markup: `<path class="${className}" d="${escapeAttr(`${commands.join(" ")} Z`)}" />`,
    bounds,
  };
}


function renderSvgPathSegment(segment) {
  const kind = String(segment?.kind ?? "");
  if (kind === "line") {
    const start = point(segment?.start);
    const end = point(segment?.end);
    if (!start || !end) return null;
    return { start, end, command: `L ${svgPoint(end)}`, bounds: boundsFromPoints([start, end]) };
  }
  if (kind === "arc") return renderSvgCircularArc(segment);
  if (kind === "ellipseArc") return renderSvgEllipseArc(segment);
  if (kind === "bezier") return renderSvgBezier(segment);
  if (kind === "bspline" || kind === "nurbs") return renderSvgSpline(segment);
  return null;
}


function renderSvgCircularArc(segment) {
  const start = point(segment?.start);
  const middle = point(segment?.middle);
  const end = point(segment?.end);
  if (!start || !middle || !end) return null;
  const ax = middle[0] - start[0];
  const ay = middle[1] - start[1];
  const bx = end[0] - start[0];
  const by = end[1] - start[1];
  const determinant = 2 * (ax * by - ay * bx);
  if (Math.abs(determinant) <= 1.0e-12) {
    return { start, end, command: `L ${svgPoint(end)}`, bounds: boundsFromPoints([start, middle, end]) };
  }
  const aSquared = ax * ax + ay * ay;
  const bSquared = bx * bx + by * by;
  const center = [
    start[0] + (by * aSquared - ay * bSquared) / determinant,
    start[1] + (ax * bSquared - bx * aSquared) / determinant,
  ];
  const radius = pointDistance(start, center);
  const startAngle = Math.atan2(start[1] - center[1], start[0] - center[0]);
  const middleAngle = Math.atan2(middle[1] - center[1], middle[0] - center[0]);
  const endAngle = Math.atan2(end[1] - center[1], end[0] - center[0]);
  const counterClockwiseSpan = positiveAngle(endAngle - startAngle);
  const middleCounterClockwiseSpan = positiveAngle(middleAngle - startAngle);
  const direction = middleCounterClockwiseSpan <= counterClockwiseSpan + 1.0e-9 ? 1 : -1;
  const sweep = direction > 0 ? counterClockwiseSpan : Math.PI * 2 - counterClockwiseSpan;
  const bounds = boundsFromPoints([start, middle, end]);
  for (const angle of [0, Math.PI / 2, Math.PI, Math.PI * 1.5]) {
    if (angleOnSweep(angle, startAngle, sweep, direction)) {
      includePoint(bounds, [center[0] + radius * Math.cos(angle), center[1] + radius * Math.sin(angle)]);
    }
  }
  return {
    start,
    end,
    command: `A ${svgNumber(radius)} ${svgNumber(radius)} 0 ${sweep > Math.PI + 1.0e-9 ? 1 : 0} ${direction > 0 ? 1 : 0} ${svgPoint(end)}`,
    bounds,
  };
}


function renderSvgEllipseArc(segment) {
  const center = point(segment?.center);
  const majorRadius = positiveNumber(segment?.majorRadius, 0);
  const minorRadius = positiveNumber(segment?.minorRadius, 0);
  const rotation = finiteNumber(segment?.rotation, 0);
  const startAngle = finiteNumber(segment?.startAngle, 0);
  const endAngle = finiteNumber(segment?.endAngle, startAngle);
  if (!center || majorRadius <= 0 || minorRadius <= 0 || Math.abs(endAngle - startAngle) <= 1.0e-12) return null;
  const delta = endAngle - startAngle;
  const direction = delta >= 0 ? 1 : -1;
  const sweep = Math.min(Math.abs(delta), Math.PI * 2);
  const start = ellipsePoint(center, majorRadius, minorRadius, rotation, startAngle);
  const end = ellipsePoint(center, majorRadius, minorRadius, rotation, startAngle + direction * sweep);
  const rotationDegrees = rotation * 180 / Math.PI;
  const arcCommand = (target, span) => `A ${svgNumber(majorRadius)} ${svgNumber(minorRadius)} ${svgNumber(rotationDegrees)} ${span > Math.PI + 1.0e-9 ? 1 : 0} ${direction > 0 ? 1 : 0} ${svgPoint(target)}`;
  let command = arcCommand(end, sweep);
  if (sweep >= Math.PI * 2 - 1.0e-9) {
    const halfway = ellipsePoint(center, majorRadius, minorRadius, rotation, startAngle + direction * Math.PI);
    command = `${arcCommand(halfway, Math.PI)} ${arcCommand(end, Math.PI)}`;
  }
  const bounds = boundsFromPoints([start, end]);
  const xExtreme = Math.atan2(-minorRadius * Math.sin(rotation), majorRadius * Math.cos(rotation));
  const yExtreme = Math.atan2(minorRadius * Math.cos(rotation), majorRadius * Math.sin(rotation));
  for (const angle of [xExtreme, xExtreme + Math.PI, yExtreme, yExtreme + Math.PI]) {
    if (angleOnSweep(angle, startAngle, sweep, direction)) {
      includePoint(bounds, ellipsePoint(center, majorRadius, minorRadius, rotation, angle));
    }
  }
  return { start, end, command, bounds };
}


function renderSvgBezier(segment) {
  const controls = (Array.isArray(segment?.controlPoints) ? segment.controlPoints : []).map(point).filter(Boolean);
  if (controls.length < 2) return null;
  const start = controls[0];
  const end = controls.at(-1);
  let command;
  if (controls.length === 2) command = `L ${svgPoint(end)}`;
  else if (controls.length === 3) command = `Q ${svgPoint(controls[1])} ${svgPoint(end)}`;
  else if (controls.length === 4) command = `C ${svgPoint(controls[1])} ${svgPoint(controls[2])} ${svgPoint(end)}`;
  else {
    const samples = sampleBezier(controls, Math.min(256, Math.max(48, controls.length * 10)));
    command = smoothSampledCurve(samples).command;
  }
  return { start, end, command, bounds: bezierBounds(controls) };
}


function renderSvgSpline(segment) {
  const samples = sampleSpline(segment);
  if (samples.length < 2) return null;
  const rendered = smoothSampledCurve(samples);
  return {
    start: samples[0],
    end: samples.at(-1),
    command: rendered.command,
    bounds: rendered.bounds,
  };
}


function smoothSampledCurve(samples) {
  const closed = samples.length > 3 && pointDistance(samples[0], samples.at(-1)) <= 1.0e-7;
  let bounds = emptyBounds();
  const commands = [];
  for (let index = 0; index + 1 < samples.length; ++index) {
    const start = samples[index];
    const end = samples[index + 1];
    const previous = index > 0
      ? samples[index - 1]
      : (closed ? samples.at(-2) : [start[0] * 2 - end[0], start[1] * 2 - end[1]]);
    const following = index + 2 < samples.length
      ? samples[index + 2]
      : (closed ? samples[1] : [end[0] * 2 - start[0], end[1] * 2 - start[1]]);
    const control1 = [start[0] + (end[0] - previous[0]) / 6, start[1] + (end[1] - previous[1]) / 6];
    const control2 = [end[0] - (following[0] - start[0]) / 6, end[1] - (following[1] - start[1]) / 6];
    commands.push(`C ${svgPoint(control1)} ${svgPoint(control2)} ${svgPoint(end)}`);
    bounds = includeBounds(bounds, bezierBounds([start, control1, control2, end]));
  }
  return { command: commands.join(" "), bounds };
}


function sampleSpline(segment) {
  const degree = Math.trunc(finiteNumber(segment?.degree, 0));
  const controls = (Array.isArray(segment?.controlPoints) ? segment.controlPoints : []).map(point).filter(Boolean);
  if (degree < 1 || controls.length < degree + 1) return [];
  const rational = String(segment?.kind ?? "") === "nurbs";
  const weights = rational
    ? (Array.isArray(segment?.weights) ? segment.weights : []).map((value) => finiteNumber(value, Number.NaN))
    : controls.map(() => 1);
  if (weights.length !== controls.length || weights.some((value) => !Number.isFinite(value) || value <= 0)) return [];
  let knots = expandedKnots(segment?.knots, segment?.multiplicities);
  if (knots.length < 2) return [];
  let splineControls = controls;
  let splineWeights = weights;
  let domainStart;
  let domainEnd;
  const periodic = segment?.periodic === true;
  if (periodic) {
    const endpointMultiplicity = knots.findIndex((value) => value !== knots[0]);
    const tailMultiplicity = knots.length - 1 - knots.findLastIndex((value) => value !== knots.at(-1));
    if (endpointMultiplicity < 1 || endpointMultiplicity !== tailMultiplicity
        || knots.length - endpointMultiplicity !== controls.length) return sampleClosedControls(controls);
    const leftCount = degree + 1 - endpointMultiplicity;
    if (leftCount < 0) return sampleClosedControls(controls);
    const positiveSteps = knots.slice(1).map((value, index) => value - knots[index]).filter((value) => value > 1.0e-12);
    const fallbackStep = positiveSteps[0] ?? 1;
    const left = [];
    let cursor = knots[0];
    for (let index = 0; index < leftCount; ++index) {
      const step = positiveSteps.at(-1 - (index % positiveSteps.length)) ?? fallbackStep;
      cursor -= step;
      left.unshift(cursor);
    }
    const right = [];
    cursor = knots.at(-1);
    for (let index = 0; index < degree; ++index) {
      const step = positiveSteps[index % positiveSteps.length] ?? fallbackStep;
      cursor += step;
      right.push(cursor);
    }
    domainStart = knots[0];
    domainEnd = knots.at(-1);
    knots = [...left, ...knots, ...right];
    splineControls = [...controls, ...controls.slice(0, degree)];
    splineWeights = [...weights, ...weights.slice(0, degree)];
  } else {
    if (knots.length !== controls.length + degree + 1) return [];
    domainStart = knots[degree];
    domainEnd = knots[controls.length];
  }
  domainStart = finiteNumber(segment?.startParameter, domainStart);
  domainEnd = finiteNumber(segment?.endParameter, domainEnd);
  if (!(domainEnd > domainStart)) return [];
  const breaks = [domainStart, ...knots.filter((value) => value > domainStart + 1.0e-12 && value < domainEnd - 1.0e-12), domainEnd]
    .filter((value, index, values) => index === 0 || value > values[index - 1] + 1.0e-12);
  const spanCount = Math.max(1, breaks.length - 1);
  const samplesPerSpan = Math.max(4, Math.min(24, Math.floor(512 / spanCount)));
  const values = [];
  for (let span = 0; span + 1 < breaks.length; ++span) {
    for (let step = 0; step < samplesPerSpan; ++step) {
      const ratio = step / samplesPerSpan;
      const parameter = breaks[span] + (breaks[span + 1] - breaks[span]) * ratio;
      const value = evaluateSpline(splineControls, splineWeights, knots, degree, parameter);
      if (value) values.push(value);
    }
  }
  const finalValue = periodic
    ? values[0]
    : evaluateSpline(splineControls, splineWeights, knots, degree, domainEnd);
  if (finalValue) values.push([...finalValue]);
  return values;
}


function evaluateSpline(controls, weights, knots, degree, parameter) {
  const lastControl = controls.length - 1;
  if (knots.length !== controls.length + degree + 1) return null;
  let span = degree;
  if (parameter >= knots[lastControl + 1] - 1.0e-12) span = lastControl;
  else {
    let low = degree;
    let high = lastControl + 1;
    while (high - low > 1) {
      const middle = Math.floor((low + high) / 2);
      if (parameter < knots[middle]) high = middle;
      else low = middle;
    }
    span = low;
  }
  const values = [];
  for (let index = 0; index <= degree; ++index) {
    const controlIndex = span - degree + index;
    const weight = weights[controlIndex];
    const control = controls[controlIndex];
    if (!control || !Number.isFinite(weight)) return null;
    values.push([control[0] * weight, control[1] * weight, weight]);
  }
  for (let level = 1; level <= degree; ++level) {
    for (let index = degree; index >= level; --index) {
      const controlIndex = span - degree + index;
      const denominator = knots[controlIndex + degree - level + 1] - knots[controlIndex];
      const alpha = Math.abs(denominator) <= 1.0e-15 ? 0 : (parameter - knots[controlIndex]) / denominator;
      values[index] = values[index - 1].map((value, component) => value * (1 - alpha) + values[index][component] * alpha);
    }
  }
  const value = values[degree];
  return Math.abs(value[2]) <= 1.0e-15 ? null : [value[0] / value[2], value[1] / value[2]];
}


function expandedKnots(rawKnots, rawMultiplicities) {
  const knots = (Array.isArray(rawKnots) ? rawKnots : []).map((value) => finiteNumber(value, Number.NaN));
  if (knots.some((value) => !Number.isFinite(value))) return [];
  if (!Array.isArray(rawMultiplicities)) return knots;
  if (rawMultiplicities.length !== knots.length) return [];
  const expanded = [];
  for (let index = 0; index < knots.length; ++index) {
    const count = Math.trunc(finiteNumber(rawMultiplicities[index], 0));
    if (count < 1) return [];
    for (let repeat = 0; repeat < count; ++repeat) expanded.push(knots[index]);
  }
  return expanded;
}


function sampleClosedControls(controls) {
  if (controls.length < 3) return controls;
  const values = [];
  for (let index = 0; index < controls.length; ++index) {
    const left = controls[(index + controls.length - 1) % controls.length];
    const center = controls[index];
    const right = controls[(index + 1) % controls.length];
    for (let step = 0; step < 16; ++step) {
      const ratio = step / 16;
      const inverse = 1 - ratio;
      values.push([
        inverse * inverse * center[0] + 2 * inverse * ratio * ((center[0] + right[0]) / 2) + ratio * ratio * right[0],
        inverse * inverse * center[1] + 2 * inverse * ratio * ((center[1] + right[1]) / 2) + ratio * ratio * right[1],
      ]);
    }
  }
  values.push([...values[0]]);
  return values;
}


function sampleBezier(controls, count) {
  const values = [];
  for (let index = 0; index <= count; ++index) {
    const points = controls.map((value) => [...value]);
    const ratio = index / count;
    for (let level = points.length - 1; level > 0; --level) {
      for (let cursor = 0; cursor < level; ++cursor) {
        points[cursor][0] = points[cursor][0] * (1 - ratio) + points[cursor + 1][0] * ratio;
        points[cursor][1] = points[cursor][1] * (1 - ratio) + points[cursor + 1][1] * ratio;
      }
    }
    values.push(points[0]);
  }
  return values;
}


function bezierBounds(controls) {
  const bounds = boundsFromPoints([controls[0], controls.at(-1)]);
  if (controls.length === 3) {
    for (let component = 0; component < 2; ++component) {
      const denominator = controls[0][component] - 2 * controls[1][component] + controls[2][component];
      if (Math.abs(denominator) > 1.0e-15) {
        const ratio = (controls[0][component] - controls[1][component]) / denominator;
        if (ratio > 0 && ratio < 1) includePoint(bounds, bezierPoint(controls, ratio));
      }
    }
  } else if (controls.length === 4) {
    for (let component = 0; component < 2; ++component) {
      const p0 = controls[0][component];
      const p1 = controls[1][component];
      const p2 = controls[2][component];
      const p3 = controls[3][component];
      const a = -p0 + 3 * p1 - 3 * p2 + p3;
      const b = 2 * (p0 - 2 * p1 + p2);
      const c = p1 - p0;
      for (const ratio of quadraticRoots(a, b, c)) {
        if (ratio > 0 && ratio < 1) includePoint(bounds, bezierPoint(controls, ratio));
      }
    }
  } else if (controls.length > 4) {
    return boundsFromPoints(sampleBezier(controls, Math.min(512, Math.max(96, controls.length * 16))));
  }
  return bounds;
}


function bezierPoint(controls, ratio) {
  const points = controls.map((value) => [...value]);
  for (let level = points.length - 1; level > 0; --level) {
    for (let index = 0; index < level; ++index) {
      points[index][0] = points[index][0] * (1 - ratio) + points[index + 1][0] * ratio;
      points[index][1] = points[index][1] * (1 - ratio) + points[index + 1][1] * ratio;
    }
  }
  return points[0];
}


function quadraticRoots(a, b, c) {
  if (Math.abs(a) <= 1.0e-15) return Math.abs(b) <= 1.0e-15 ? [] : [-c / b];
  const discriminant = b * b - 4 * a * c;
  if (discriminant < 0) return [];
  const root = Math.sqrt(discriminant);
  return [(-b - root) / (2 * a), (-b + root) / (2 * a)];
}


function ellipsePoint(center, radiusX, radiusY, rotation, angle) {
  const cosRotation = Math.cos(rotation);
  const sinRotation = Math.sin(rotation);
  const x = radiusX * Math.cos(angle);
  const y = radiusY * Math.sin(angle);
  return [center[0] + x * cosRotation - y * sinRotation, center[1] + x * sinRotation + y * cosRotation];
}


function ellipseBounds(center, radiusX, radiusY, rotation) {
  if (!(radiusX > 0 && radiusY > 0)) return emptyBounds();
  const extentX = Math.hypot(radiusX * Math.cos(rotation), radiusY * Math.sin(rotation));
  const extentY = Math.hypot(radiusX * Math.sin(rotation), radiusY * Math.cos(rotation));
  return { minX: center[0] - extentX, minY: center[1] - extentY, maxX: center[0] + extentX, maxY: center[1] + extentY };
}


function angleOnSweep(angle, start, sweep, direction) {
  const travel = direction > 0 ? positiveAngle(angle - start) : positiveAngle(start - angle);
  return travel <= sweep + 1.0e-9;
}


function positiveAngle(value) {
  const full = Math.PI * 2;
  return ((value % full) + full) % full;
}


function emptyBounds() {
  return { minX: Number.POSITIVE_INFINITY, minY: Number.POSITIVE_INFINITY, maxX: Number.NEGATIVE_INFINITY, maxY: Number.NEGATIVE_INFINITY };
}


function boundsFromPoints(points) {
  const bounds = emptyBounds();
  for (const value of points) includePoint(bounds, value);
  return bounds;
}


function includePoint(bounds, value) {
  if (!value || !value.every(Number.isFinite)) return bounds;
  bounds.minX = Math.min(bounds.minX, value[0]);
  bounds.minY = Math.min(bounds.minY, value[1]);
  bounds.maxX = Math.max(bounds.maxX, value[0]);
  bounds.maxY = Math.max(bounds.maxY, value[1]);
  return bounds;
}


function includeBounds(target, source) {
  if (!validBounds(source)) return target;
  includePoint(target, [source.minX, source.minY]);
  includePoint(target, [source.maxX, source.maxY]);
  return target;
}


function validBounds(bounds) {
  return bounds && [bounds.minX, bounds.minY, bounds.maxX, bounds.maxY].every(Number.isFinite)
    && bounds.maxX >= bounds.minX && bounds.maxY >= bounds.minY;
}


function point(value) {
  if (!Array.isArray(value) || value.length < 2) return null;
  const x = Number(value[0]);
  const y = Number(value[1]);
  return Number.isFinite(x) && Number.isFinite(y) ? [x, y] : null;
}


function pointDistance(left, right) {
  return Math.hypot(left[0] - right[0], left[1] - right[1]);
}


function finiteNumber(value, fallback) {
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}


function positiveNumber(value, fallback) {
  const number = finiteNumber(value, fallback);
  return number > 0 ? number : fallback;
}


function svgNumber(value) {
  return formatNumber(value, 6);
}


function svgPoint(value) {
  return `${svgNumber(value[0])},${svgNumber(value[1])}`;
}
