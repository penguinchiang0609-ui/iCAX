import { profileSvgGeometry } from './profileSvg.mjs';
import { parameterVisible } from './parameterConditions.mjs';
import { parameterDiagramLevelAttribute } from './parameterPresentation.mjs';
import { catalogText } from './productCatalog.mjs';

// Presentation geometry only. No result from this module is a manufacturing
// input, a saved parameter, or a measurement of the final BRep.
const EPS = 1e-7;
const TAU = Math.PI * 2;
const escape = value => String(value ?? '').replaceAll('&', '&amp;').replaceAll('<', '&lt;')
  .replaceAll('>', '&gt;').replaceAll('"', '&quot;').replaceAll("'", '&#39;');
const clone = value => JSON.parse(JSON.stringify(value));
const finite = value => typeof value === 'number' && Number.isFinite(value);
const num = value => Number(Number(value).toFixed(6));
const fmt = value => finite(value) ? new Intl.NumberFormat('zh-CN', { maximumFractionDigits: 3, useGrouping: false }).format(value) : '—';
const distance = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1]);
const dot = (a, b) => a[0] * b[0] + a[1] * b[1];
const positiveAngle = value => ((value % TAU) + TAU) % TAU;
const fieldMap = template => new Map((template?.parameters ?? []).map(field => [field.key, field]));
const bound = (declaration, name) => typeof declaration?.bindings?.[name] === 'string' ? declaration.bindings[name] : '';

function arcData(edge) {
  const [a, m, b] = [edge.start, edge.middle, edge.end];
  const [x, y, u, v] = [m[0] - a[0], m[1] - a[1], b[0] - a[0], b[1] - a[1]];
  const determinant = 2 * (x * v - y * u);
  if (Math.abs(determinant) < 1e-12) throw new Error('圆弧截面暂不可显示');
  const center = [a[0] + (v * (x * x + y * y) - y * (u * u + v * v)) / determinant,
    a[1] + (x * (u * u + v * v) - u * (x * x + y * y)) / determinant];
  const angle = p => Math.atan2(p[1] - center[1], p[0] - center[0]);
  const start = angle(a), span = positiveAngle(angle(b) - start);
  const sign = positiveAngle(angle(m) - start) <= span + EPS ? 1 : -1;
  return { center, radius: distance(a, center), start, sign, sweep: sign > 0 ? span : TAU - span };
}

function rectangleEdges(contour) {
  const [cx, cy] = contour.center ?? [0, 0];
  const x = contour.width / 2, y = contour.height / 2;
  const r = Math.min(contour.kind === 'capsule' ? Math.min(x, y) : (contour.radius ?? 0), x, y);
  if (!r) {
    const points = [[cx - x, cy - y], [cx + x, cy - y], [cx + x, cy + y], [cx - x, cy + y]];
    return points.map((start, index) => ({ kind: 'line', start, end: points[(index + 1) % 4] }));
  }
  const q = 1 - Math.SQRT1_2;
  const points = [[-x, y - r], [-x, -y + r], [-x + r, -y], [x - r, -y],
    [x, -y + r], [x, y - r], [x - r, y], [-x + r, y]];
  const middles = { 1: [-x + q * r, -y + q * r], 3: [x - q * r, -y + q * r],
    5: [x - q * r, y - q * r], 7: [-x + q * r, y - q * r] };
  const move = p => [p[0] + cx, p[1] + cy];
  return points.map((p, index) => ({ kind: middles[index] ? 'arc' : 'line', start: move(p),
    end: move(points[(index + 1) % points.length]), ...(middles[index] ? { middle: move(middles[index]) } : {}) }));
}

function contourEdges(contour) {
  if (contour.kind === 'path') return contour.segments.map(edge => edge.reversed
    ? { ...edge, start: edge.end, end: edge.start, reversed: false } : edge);
  if (contour.kind === 'polygon') return contour.points.map((start, index) => ({ kind: 'line', start, end: contour.points[(index + 1) % contour.points.length] }));
  if (['roundedRectangle', 'capsule'].includes(contour.kind)) return rectangleEdges(contour);
  throw new Error('此曲线仅显示原始截面');
}

// Same analytic, convex line/arc offset relation as tube_profile_catalog's
// public section offset. Unsupported curves keep their original SVG; they are
// never substituted with a made-up rectangle or a fabricated hole size.
function offsetContour(source, amount) {
  const contour = clone(source);
  if (!amount) return contour;
  if (contour.kind === 'circle') {
    contour.radius += amount;
    if (!(contour.radius > 0)) throw new Error('间隙后的截面暂不可显示');
    return contour;
  }
  if (['roundedRectangle', 'capsule'].includes(contour.kind)) {
    contour.width += 2 * amount; contour.height += 2 * amount;
    if (contour.kind === 'roundedRectangle' && contour.radius > 0) contour.radius += amount;
    if (Math.min(contour.width, contour.height) <= 0 || contour.radius < 0) throw new Error('间隙后的截面暂不可显示');
    return contour;
  }
  const edges = contourEdges(contour);
  if (edges.some(edge => !['line', 'arc'].includes(edge.kind))) throw new Error('间隙后的曲线请以生成结果为准');
  const points = edges.flatMap(edge => edge.kind === 'arc' ? [edge.start, edge.middle] : [edge.start]);
  const area = points.reduce((sum, a, index) => { const b = points[(index + 1) % points.length]; return sum + a[0] * b[1] - a[1] * b[0]; }, 0);
  if (Math.abs(area) < 1e-12) throw new Error('截面暂不可显示');
  const sign = Math.sign(area);
  if (points.some((b, index) => {
    const a = points[(index + points.length - 1) % points.length], c = points[(index + 1) % points.length];
    return sign * ((b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])) < -1e-8;
  })) throw new Error('间隙后的曲线请以生成结果为准');
  const shifted = edges.map(edge => {
    const next = clone(edge);
    if (edge.kind === 'line') {
      const dx = edge.end[0] - edge.start[0], dy = edge.end[1] - edge.start[1], length = Math.hypot(dx, dy);
      if (length < 1e-12) throw new Error('截面暂不可显示');
      for (const name of ['start', 'end']) next[name] = [edge[name][0] + sign * amount * dy / length, edge[name][1] - sign * amount * dx / length];
    } else {
      const { center, radius } = arcData(edge), changed = radius + amount;
      if (!(changed > 0)) throw new Error('间隙后的圆角请以生成结果为准');
      for (const name of ['start', 'middle', 'end']) next[name] = edge[name].map((v, i) => center[i] + (v - center[i]) * changed / radius);
    }
    return next;
  });
  shifted.forEach((right, index) => {
    const left = shifted[(index + shifted.length - 1) % shifted.length];
    if (left.kind === 'line' && right.kind === 'line') {
      const a = left.start, b = right.start, u = left.end.map((v, i) => v - a[i]), v = right.end.map((p, i) => p - b[i]);
      const determinant = u[0] * v[1] - u[1] * v[0];
      if (Math.abs(determinant) > 1e-12) {
        const t = ((b[0] - a[0]) * v[1] - (b[1] - a[1]) * v[0]) / determinant;
        left.end = right.start = a.map((p, i) => p + t * u[i]);
      }
    }
    if (distance(left.end, right.start) > EPS) throw new Error('间隙后的曲线请以生成结果为准');
  });
  return { kind: 'path', closed: true, segments: shifted };
}

function profileFor(declaration, values, role) {
  const entry = values?.tubeDesignerProfileOverrides?.[role];
  const get = name => values?.[bound(declaration, role + name)];
  let contours, source, approximate = false;
  if (entry) {
    if (!Array.isArray(entry.contours) || entry.contours.length < 2) throw new Error('等待所选管型的截面轮廓');
    contours = clone(entry.contours); source = 'snapshot';
  } else {
    const width = get('Width'), round = ['round', 'circle'].includes(String(get('ProfileType')));
    const depth = round ? width : get('Depth'), wall = get('WallThickness'), radius = get('CornerRadius') ?? 0;
    if (![width, depth, wall, radius].every(finite) || Math.min(width, depth, wall) <= 0 || 2 * wall >= Math.min(width, depth)) throw new Error('请完善管型尺寸');
    contours = round ? [{ kind: 'circle', center: [0, 0], radius: width / 2 }, { kind: 'circle', center: [0, 0], radius: width / 2 - wall }]
      : [{ kind: 'roundedRectangle', width, height: depth, radius }, { kind: 'roundedRectangle', width: width - 2 * wall, height: depth - 2 * wall, radius: Math.max(0, radius - wall) }];
    source = 'parameters'; approximate = !round;
  }
  const bounds = profileSvgGeometry({ contours: [contours[0]] }).bounds;
  const inside = profileSvgGeometry({ contours: [contours[1]] }).bounds;
  if (!bounds || !inside) throw new Error('等待所选管型的截面轮廓');
  const wall = Math.min(inside.minX - bounds.minX, inside.minY - bounds.minY, bounds.maxX - inside.maxX, bounds.maxY - inside.maxY);
  return { source, approximate, contours, bounds, width: bounds.maxX - bounds.minX, depth: bounds.maxY - bounds.minY,
    wall, center: [(bounds.minX + bounds.maxX) / 2, (bounds.minY + bounds.maxY) / 2],
    kind: contours[0].kind === 'circle' ? 'round' : String(entry?.sectionKind ?? entry?.kind ?? get('ProfileType') ?? 'rect') };
}

function directions(count, angle, width) {
  return (count === 4 ? [angle, angle + 90] : [angle]).map(phase => ({ angle: phase, width,
    tangent: [Math.cos(phase * Math.PI / 180), Math.sin(phase * Math.PI / 180)] }));
}

// Extrema of an analytic ring clipped by a union of straight bands occur at
// curve extrema/endpoints or at a curve/band intersection. No polygon sampling
// is used to invent a dimensional readout for slanted or round tongues.
function sectionEnvelope(contours, bands, center) {
  const outer = profileSvgGeometry({ contours: [contours[0]] }).bounds;
  if (!bands.length) return { ...outer, width: outer.maxX - outer.minX, depth: outer.maxY - outer.minY, proven: true };
  const candidates = [];
  const accepted = p => bands.some(band => Math.abs(dot(p.map((v, i) => v - center[i]), band.tangent)) <= band.width / 2 + EPS);
  const add = p => { if (accepted(p)) candidates.push(p); };
  const circle = (data, onArc = () => true) => {
    const addAngle = angle => { if (onArc(angle)) add([data.center[0] + data.radius * Math.cos(angle), data.center[1] + data.radius * Math.sin(angle)]); };
    for (const a of [0, Math.PI / 2, Math.PI, Math.PI * 1.5]) addAngle(a);
    for (const band of bands) for (const side of [-1, 1]) {
      const d = side * band.width / 2 - dot(data.center.map((v, i) => v - center[i]), band.tangent);
      if (Math.abs(d) > data.radius + EPS) continue;
      const angle = Math.atan2(band.tangent[1], band.tangent[0]), delta = Math.acos(Math.max(-1, Math.min(1, d / data.radius)));
      addAngle(angle + delta); addAngle(angle - delta);
    }
  };
  try {
    for (const contour of contours) {
      if (contour.kind === 'circle') { circle({ center: contour.center ?? [0, 0], radius: contour.radius }); continue; }
      for (const edge of contourEdges(contour)) {
        add(edge.start); add(edge.end);
        if (edge.kind === 'arc') {
          const data = arcData(edge);
          circle(data, angle => positiveAngle(data.sign * (angle - data.start)) <= data.sweep + EPS);
        } else if (edge.kind === 'line') {
          const vector = edge.end.map((v, i) => v - edge.start[i]);
          for (const band of bands) for (const side of [-1, 1]) {
            const divisor = dot(vector, band.tangent);
            if (Math.abs(divisor) < 1e-12) continue;
            const t = (side * band.width / 2 - dot(edge.start.map((v, i) => v - center[i]), band.tangent)) / divisor;
            if (t >= -EPS && t <= 1 + EPS) add(edge.start.map((v, i) => v + t * vector[i]));
          }
        } else throw new Error('非解析曲线');
      }
    }
    if (!candidates.length) return { ...outer, width: null, depth: null, proven: false };
    const xs = candidates.map(p => p[0]), ys = candidates.map(p => p[1]);
    const bounds = { minX: Math.min(...xs), maxX: Math.max(...xs), minY: Math.min(...ys), maxY: Math.max(...ys) };
    return { ...bounds, width: bounds.maxX - bounds.minX, depth: bounds.maxY - bounds.minY, proven: true };
  } catch { return { ...outer, width: outer.maxX - outer.minX, depth: outer.maxY - outer.minY, proven: false }; }
}

function singleHoleDimensions(contours, bands, center) {
  if (contours.length !== 2 || !bands.length) return { available: false, holes: [] };
  try {
    const boxes = contours.map(contour => profileSvgGeometry({ contours: [contour] }).bounds);
    const edges = contours.map(contourEdges), holes = [];
    for (const band of bands) {
      const tangent = band.tangent;
      const along = Math.abs(tangent[0]) > 1 - EPS ? 0 : Math.abs(tangent[1]) > 1 - EPS ? 1 : -1;
      if (along < 0) return { available: false, holes: [], reason: 'oblique' };
      const normal = 1 - along, low = center[along] - band.width / 2, high = center[along] + band.width / 2;
      for (const side of ['min', 'max']) {
        const coordinate = (box, axis) => box[side + (axis === 0 ? 'X' : 'Y')];
        const supports = edges.map((loop, index) => loop.filter(edge => edge.kind === 'line'
          && Math.abs(edge.start[normal] - coordinate(boxes[index], normal)) < EPS
          && Math.abs(edge.end[normal] - coordinate(boxes[index], normal)) < EPS));
        if (supports.some(items => items.length !== 1)) return { available: false, holes: [], reason: 'curved' };
        if (supports.some(([edge]) => low < Math.min(edge.start[along], edge.end[along]) - EPS
          || high > Math.max(edge.start[along], edge.end[along]) + EPS)) return { available: false, holes: [], reason: 'crosses-corner' };
        holes.push({ angle: band.angle, side, width: band.width,
          thickness: Math.abs(coordinate(boxes[0], normal) - coordinate(boxes[1], normal)) });
      }
    }
    const thicknesses = holes.map(hole => hole.thickness);
    return { available: true, holes, width: bands[0].width, thickness: thicknesses[0],
      uniformThickness: thicknesses.every(value => Math.abs(value - thicknesses[0]) < EPS),
      minThickness: Math.min(...thicknesses), maxThickness: Math.max(...thicknesses) };
  } catch { return { available: false, holes: [], reason: 'curved' }; }
}

export function insertJointParameterDetailModel(declaration, values = {}, { template } = {}) {
  const part = declaration?.part;
  if (declaration?.kind !== 'directional-insert-joint' || !['male', 'female'].includes(part)) return { active: false, available: false, part };
  const get = name => values[bound(declaration, name)];
  const through = get('jointMode') === 'male_female_through_hole';
  const active = through || get('jointMode') === 'male_female_half_hole';
  const base = { active, available: false, part, through, bindings: { ...declaration.bindings }, form: get('maleEndForm') ?? 'tongue' };
  if (!active) return base;
  const fields = fieldMap(template);
  const label = (name, fallback) => catalogText(fields.get(bound(declaration, name))?.displayName, fallback);
  try {
    const frame = profileFor(declaration, values, 'frame'), inner = profileFor(declaration, values, 'inner');
    const full = base.form === 'full_section';
    const count = get('maleTabCount') === 'four' || get('maleTabCount') === 4 ? 4 : 2;
    const angle = full ? 0 : get('maleTabAngle') ?? 0;
    const width = full ? inner.width : get('maleHeadWidth');
    const clearanceDepth = full ? get('fullHeadTotalClearance') : get('holeTotalClearanceDepth');
    const clearanceHeight = full ? clearanceDepth : get('holeTotalClearanceHeight');
    if (![angle, width, clearanceDepth, clearanceHeight].every(finite) || width <= 0 || clearanceDepth < 0 || clearanceHeight < 0) throw new Error('请完善当前公母尺寸');
    const maleBands = full ? [] : directions(count, angle, width);
    const femaleBands = full ? [] : directions(count, angle, width + clearanceHeight);
    let femaleContours, socketAvailable = true, socketNote = '';
    try { femaleContours = full ? [offsetContour(inner.contours[0], clearanceDepth / 2)]
      : [offsetContour(inner.contours[0], clearanceDepth / 2), offsetContour(inner.contours[1], -clearanceDepth / 2)]; }
    catch (error) { femaleContours = inner.contours; socketAvailable = false; socketNote = error.message; }
    const male = { contours: inner.contours, bands: maleBands, envelope: sectionEnvelope(inner.contours, maleBands, inner.center) };
    const female = { contours: femaleContours, bands: femaleBands, available: socketAvailable,
      envelope: socketAvailable ? sectionEnvelope(femaleContours, femaleBands, inner.center) : { width: null, depth: null, proven: false } };
    const singleHole = !full && socketAvailable ? singleHoleDimensions(femaleContours, femaleBands, inner.center) : { available: false, holes: [] };
    const section = part === 'male' ? male : female;
    const curved = frame.kind === 'round';
    const length = { editable: !through, kind: through ? (curved ? 'curved-flush' : 'outer-face-flush') : 'single-wall',
      value: through ? (curved ? null : frame.width) : get('maleHeadLength'), receiverEnvelope: frame.width,
      variesAroundSection: curved, label: label('maleHeadLength', '伸出长度') };
    const readouts = [
      { key: 'width', label: section.envelope.proven ? '截面整体宽' : '截面宽度上限', value: section.envelope.width, unit: 'mm', derived: true },
      { key: 'depth', label: section.envelope.proven ? '截面整体高' : '截面高度上限', value: section.envelope.depth, unit: 'mm', derived: true },
      { key: 'wall', label: part === 'male' ? '公头厚度' : '母管壁厚', value: part === 'male' ? inner.wall : frame.wall, unit: 'mm', derived: true,
        parameter: bound(declaration, part === 'male' ? 'innerWallThickness' : 'frameWallThickness') },
    ];
    if (!full) readouts.push({ key: 'bandWidth', label: part === 'male' ? label('maleHeadWidth', '舌宽') : '母口方向带宽', value: part === 'male' ? width : width + clearanceHeight, unit: 'mm', parameter: part === 'male' ? bound(declaration, 'maleHeadWidth') : bound(declaration, 'holeTotalClearanceHeight') },
      { key: 'count', label: label('maleTabCount', '周向数量'), value: count, unit: '处', parameter: bound(declaration, 'maleTabCount') },
      { key: 'angle', label: label('maleTabAngle', '周向角'), value: angle, unit: '°', parameter: bound(declaration, 'maleTabAngle') });
    if (part === 'male') readouts.push({ key: 'length', label: length.editable ? length.label : '对穿端部',
      value: through && curved ? '随母管曲面齐平' : through ? `${fmt(frame.width)} mm · 对壁外表面齐平` : length.value,
      unit: through ? '' : 'mm', parameter: through ? '' : bound(declaration, 'maleHeadLength'), derived: through });
    else if (full) readouts.push({ key: 'fullClearance', label: label('fullHeadTotalClearance', '总间隙'), value: clearanceDepth, unit: 'mm', parameter: bound(declaration, 'fullHeadTotalClearance') });
    else readouts.push({ key: 'clearanceDepth', label: label('holeTotalClearanceDepth', '深向总间隙'), value: clearanceDepth, unit: 'mm', parameter: bound(declaration, 'holeTotalClearanceDepth') },
      { key: 'clearanceHeight', label: label('holeTotalClearanceHeight', '宽向总间隙'), value: clearanceHeight, unit: 'mm', parameter: bound(declaration, 'holeTotalClearanceHeight') });
    if (part === 'female' && singleHole.available) readouts.unshift(
      { key: 'holeWidth', label: '单个母孔 · 宽向', value: singleHole.width, unit: 'mm', derived: true, parameter: bound(declaration, 'holeTotalClearanceHeight') },
      { key: 'holeThickness', label: singleHole.uniformThickness ? '单个母孔 · 厚向' : '各方向母孔 · 厚向',
        value: singleHole.uniformThickness ? singleHole.thickness : `${fmt(singleHole.minThickness)}～${fmt(singleHole.maxThickness)}`,
        unit: 'mm', derived: true, parameter: bound(declaration, 'holeTotalClearanceDepth') });
    const primaryReadouts = part === 'male' ? readouts.filter(row => row.key === 'wall' || (through && row.key === 'length'))
      : full ? (inner.kind === 'round'
        ? [{ key: 'diameter', label: '母口直径', value: female.envelope.width, unit: 'mm', derived: true, parameter: bound(declaration, 'fullHeadTotalClearance') }]
        : [{ key: 'openingWidth', label: '母口宽', value: female.envelope.width, unit: 'mm', derived: true, parameter: bound(declaration, 'fullHeadTotalClearance') },
          { key: 'openingHeight', label: '母口高', value: female.envelope.depth, unit: 'mm', derived: true, parameter: bound(declaration, 'fullHeadTotalClearance') }])
        : singleHole.available ? readouts.filter(row => ['holeWidth', 'holeThickness'].includes(row.key))
          : [{ key: 'openingShape', label: '孔口形状', value: '随截面弯曲', unit: '', derived: true },
            { ...readouts.find(row => row.key === 'bandWidth'), label: '沿当前方向的孔宽' }];
    const corner = { type: full ? 'square' : get('maleCornerType'), size: full ? 0 : get('maleCornerSize') };
    const tipLength = through ? frame.width : length.value;
    const tip = { available: !full && finite(tipLength) && tipLength > 0,
      length: tipLength, width, corner: corner.type, size: corner.type === 'square' ? 0 : corner.size,
      curvedReceiver: curved, lengthEditable: !through, view: 'axial-band-unfold', envelopeOnly: curved };
    if (tip.available && corner.type !== 'square' && (!finite(corner.size) || corner.size <= 0 || corner.size >= Math.min(tipLength, width) / 2)) tip.available = false;
    const notes = [part === 'male' ? '蓝色为保留的公头管壁。' : '白色红边为切除的母口。',
      curved && through ? '端部随母管曲面齐平，各处伸出长度不同。'
        : part === 'female' && !full && !singleHole.available ? '曲线孔口按当前方向显示，整体范围不等于单个孔尺寸。'
          : inner.approximate || frame.approximate ? '按当前截面尺寸示意。' : '按所选截面示意。'];
    return { ...base, available: true, profiles: { frame, inner }, sections: { male, female },
      direction: { count, angle, phases: maleBands.map(b => b.angle) }, length,
      corner, tip, singleHole, readouts, primaryReadouts, socketNote, notes };
  } catch (error) { return { ...base, message: error.message, readouts: [], primaryReadouts: [], notes: ['截面示意暂未就绪。'] }; }
}

function visibleKey(key, options, fields) {
  return !key || !options.template || (fields.has(key) && parameterVisible(fields.get(key), options.values));
}

function actionAttributes(key, options, fields) {
  const field = fields.get(key);
  if (!key || !visibleKey(key, options, fields)) return '';
  return ` data-cam-action="tube-designer-focus-product-parameter" data-tube-designer-editor-mode="${escape(options.mode)}" data-tube-designer-parameter-key="${escape(key)}" data-product-diagram-parameter="${escape(key)}" tabindex="0" role="button"${field ? parameterDiagramLevelAttribute(field) : ''}`;
}

function shape(contours, fill, holeFill, stroke = 'none', strokeWidth = 0) {
  return profileSvgGeometry({ contours }).markup
    .replaceAll('class="outer"', `class="tube-insert-detail-outline" fill="${fill}" stroke="${stroke}" stroke-width="${strokeWidth}"`)
    .replaceAll('class="hole"', `class="tube-insert-detail-inner" fill="${holeFill}" stroke="${stroke}" stroke-width="${strokeWidth}"`);
}

function dimension(from, to, offset, label, key, options, fields, axis = 'x') {
  if (!visibleKey(key, options, fields)) return '';
  const horizontal = axis === 'x', a = horizontal ? [from[0], offset] : [offset, from[1]], b = horizontal ? [to[0], offset] : [offset, to[1]];
  const point = p => p.map(num).join(' '), middle = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
  const arrow = (p, direction) => `<path d="M ${point(p)} l ${num(-6 * Math.cos(direction) + 2.5 * Math.sin(direction))} ${num(-6 * Math.sin(direction) - 2.5 * Math.cos(direction))} l ${num(-5 * Math.sin(direction))} ${num(5 * Math.cos(direction))} Z" fill="currentColor" stroke="none"/>`;
  const angle = Math.atan2(b[1] - a[1], b[0] - a[0]);
  return `<g class="tube-insert-detail-dimension product-diagram-measure${key === options.activeParameter ? ' is-active' : ''}" color="#64748b" fill="none" stroke="currentColor" stroke-width="1"${actionAttributes(key, options, fields)}>
    <path d="M ${point(from)} L ${point(a)} M ${point(to)} L ${point(b)}" opacity=".5"/><path d="M ${point(a)} L ${point(b)}"/>
    ${arrow(a, angle + Math.PI)}${arrow(b, angle)}<text x="${num(middle[0] + (horizontal ? 0 : 12))}" y="${num(middle[1] - (horizontal ? 7 : 0))}" text-anchor="middle" dominant-baseline="central" font-size="11" fill="currentColor" stroke="none"${horizontal ? '' : ` transform="rotate(-90 ${num(middle[0] + 12)} ${num(middle[1])})"`}>${escape(label)}</text></g>`;
}

function longitudinal(model, options, fields) {
  const { frame, inner } = model.profiles, curved = frame.kind === 'round';
  const depth = Math.min(inner.depth, frame.depth * .94), half = depth / 2;
  const span = frame.width, maximum = model.through ? span : (finite(model.length.value) ? model.length.value : span / 3);
  const scale = Math.min(3.4, 138 / Math.max(span, maximum, 1)), x0 = 108, cy = 254;
  const point = (x, y) => `${num(x0 + x * scale)},${num(cy - y * scale)}`;
  const near = y => curved ? span / 2 - Math.sqrt(Math.max(0, (span / 2) ** 2 - y ** 2)) : 0;
  const far = y => curved ? span / 2 + Math.sqrt(Math.max(0, (span / 2) ** 2 - y ** 2)) : span;
  const ys = Array.from({ length: 25 }, (_, i) => -half + 2 * half * i / 24);
  const nearPath = ys.map(y => point(near(y), y)).join(' '), farPath = ys.map(y => point(far(y), y)).join(' ');
  const end = y => model.through ? far(y) : Math.max(near(y), maximum);
  const tongue = [...ys.map(y => point(near(y), y)), ...[...ys].reverse().map(y => point(end(y), y))].join(' ');
  const attribute = actionAttributes(model.through ? '' : model.bindings.maleHeadLength, options, fields);
  return `<g class="tube-insert-detail-axial" data-insert-joint-axial="${curved ? 'curved' : 'planar'}">
    <text x="180" y="208" text-anchor="middle" font-size="11" fill="#64748b">${model.through ? '对壁外表面齐平' : '单边伸出'} · 轴向示意</text>
    <path d="M ${point(-20 / scale, -half)} L ${point(-20 / scale, half)} L ${point(near(half), half)} L ${point(near(-half), -half)} Z" fill="#dbe3ec" stroke="#94a3b8"/>
    <g${attribute}><polygon points="${tongue}" fill="#60a5fa" fill-opacity=".65" stroke="#2563eb" stroke-width="1"/></g>
    <polyline points="${nearPath}" fill="none" stroke="#64748b" stroke-width="1.3"/><polyline points="${farPath}" fill="none" stroke="#64748b" stroke-width="1.3" stroke-dasharray="4 3"/>
    ${!model.through && finite(model.length.value) ? dimension([x0, cy + half * scale], [x0 + maximum * scale, cy + half * scale], 300,
      `${model.length.label} ${fmt(model.length.value)}`, model.bindings.maleHeadLength, options, fields) : ''}
  </g>`;
}

function singleHoleArtwork(model, options, fields) {
  const hole = model.singleHole;
  if (!hole.available || !hole.uniformThickness) return '';
  const scale = Math.min(112 / hole.width, 28 / hole.thickness);
  const width = hole.width * scale, height = hole.thickness * scale;
  const x = 170 - width / 2, y = 256 - height / 2;
  return `<g class="tube-insert-detail-single-hole" data-insert-joint-single-hole="true">
    <text x="170" y="232" text-anchor="middle" font-size="11" fill="#64748b">单个母孔 · 局部放大</text>
    <rect x="${num(x - 14)}" y="${num(y - 9)}" width="${num(width + 28)}" height="${num(height + 18)}" fill="#cbd5e1"/>
    <rect x="${num(x)}" y="${num(y)}" width="${num(width)}" height="${num(height)}" fill="#fff" stroke="#dc2626"/>
    ${dimension([x, y + height], [x + width, y + height], 300, `孔宽 ${fmt(hole.width)}`, model.bindings.holeTotalClearanceHeight, options, fields)}
    ${dimension([x + width, y], [x + width, y + height], 278, `孔厚 ${fmt(hole.thickness)}`, model.bindings.holeTotalClearanceDepth, options, fields, 'y')}
  </g>`;
}

function tipArtwork(model, options, fields) {
  const tip = model.tip;
  if (!tip?.available) return '';
  const length = tip.length, width = tip.width, radius = tip.size;
  const scale = Math.min(188 / length, 70 / width), x = 170 - length * scale / 2, y = 66;
  let path;
  if (tip.corner === 'round') path = `M ${radius} 0 H ${length - radius} A ${radius} ${radius} 0 0 1 ${length} ${radius} V ${width - radius} A ${radius} ${radius} 0 0 1 ${length - radius} ${width} H ${radius} A ${radius} ${radius} 0 0 1 0 ${width - radius} V ${radius} A ${radius} ${radius} 0 0 1 ${radius} 0 Z`;
  else if (tip.corner === 'chamfer') path = `M 0 0 H ${length - radius} L ${length} ${radius} V ${width - radius} L ${length - radius} ${width} H 0 Z`;
  else path = `M 0 0 H ${length} V ${width} H 0 Z`;
  const label = (key, fallback) => catalogText(fields.get(key)?.displayName, fallback);
  const lengthKey = model.through ? '' : model.bindings.maleHeadLength;
  const widthKey = model.bindings.maleHeadWidth, sizeKey = model.bindings.maleCornerSize;
  const cornerField = fields.get(model.bindings.maleCornerType);
  const choice = cornerField?.choices?.find(item => item.value === tip.corner);
  const cornerLabel = catalogText(choice?.displayName, tip.corner === 'round' ? '圆角' : '倒角');
  const corner = tip.corner !== 'square' && visibleKey(sizeKey, options, fields) && visibleKey(model.bindings.maleCornerType, options, fields)
    ? `<g class="tube-insert-detail-tip-corner"${actionAttributes(sizeKey, options, fields)}><path d="M ${num(x + (length - radius * .3) * scale)} ${num(y + radius * .3 * scale)} L 287 42" fill="none" stroke="#64748b"/>
       <text x="287" y="34" font-size="11" fill="#64748b" text-anchor="middle">${escape(cornerLabel)} ${fmt(radius)}</text></g>` : '';
  const showLength = !(model.through && tip.curvedReceiver);
  return `<svg class="tube-insert-detail-artwork tube-insert-detail-tip-artwork" data-insert-joint-artwork="male-tip" data-insert-joint-tip="${escape(tip.corner)}" data-insert-joint-tip-envelope="${tip.envelopeOnly}" viewBox="0 0 360 195" role="group" aria-label="单个公头展开示意">
    <text x="170" y="18" text-anchor="middle" font-size="12" fill="#475569">单个公头 · 沿伸出方向展开</text>
    <path data-insert-joint-tip-outline="${escape(tip.corner)}" d="${path}" transform="translate(${num(x)} ${y}) scale(${scale})" fill="#60a5fa" stroke="#2563eb" stroke-width="${1.2 / scale}"${actionAttributes(model.bindings.maleCornerType, options, fields)}/>
    ${showLength ? dimension([x, y + width * scale], [x + length * scale, y + width * scale], 158,
      `${model.through ? '齐平长度' : label(lengthKey, '伸出长度')} ${fmt(length)}`, lengthKey, options, fields) : ''}
    ${dimension([x + length * scale, y], [x + length * scale, y + width * scale], 291, `${label(widthKey, '舌宽')} ${fmt(width)}`, widthKey, options, fields, 'y')}${corner}
    ${tip.curvedReceiver ? '<text x="170" y="187" text-anchor="middle" font-size="10" fill="#64748b">端形示意；实际端部按母管曲面裁切</text>' : ''}
  </svg>`;
}

export function renderInsertJointParameterDetail(declaration, values = {}, { mode = 'right', activeParameter = '', idPrefix = '', template } = {}) {
  const model = insertJointParameterDetailModel(declaration, values, { template });
  if (!model.active) return '';
  const options = { mode, activeParameter, template, values }, fields = fieldMap(template);
  const title = catalogText(declaration.title, model.part === 'male' ? '公头截面示意' : '母口截面示意');
  const readouts = model.primaryReadouts.filter(row => visibleKey(row.parameter, options, fields)).map(row => `<div data-insert-joint-readout="${escape(row.key)}" data-insert-joint-derived="${row.derived ? 'true' : 'false'}"${actionAttributes(row.parameter, options, fields)}><dt>${escape(row.label)}</dt><dd>${escape(typeof row.value === 'number' ? fmt(row.value) : row.value ?? '—')}${row.unit ? ` ${escape(row.unit)}` : ''}</dd></div>`).join('');
  let artwork = '';
  if (model.available) {
    const section = model.sections[model.part], { inner } = model.profiles;
    const sectionBounds = profileSvgGeometry({ contours: section.contours }).bounds;
    const reference = Math.max(inner.width, inner.depth, sectionBounds.maxX - sectionBounds.minX, sectionBounds.maxY - sectionBounds.minY);
    const scale = 126 / Math.max(reference, EPS), cx = 170, cy = 110;
    const map = p => [cx + (p[0] - inner.center[0]) * scale, cy - (p[1] - inner.center[1]) * scale];
    const id = `insert-joint-${[idPrefix, mode, declaration.group, model.part].filter(Boolean).join('-').replace(/[^a-zA-Z0-9_-]/g, '_')}`;
    const reach = reference * 3, transform = `translate(${cx} ${cy}) scale(${scale} ${-scale}) translate(${-inner.center[0]} ${-inner.center[1]})`;
    const bandRects = section.bands.map((band, index) => `<rect data-insert-joint-band="${index}" data-insert-joint-angle="${escape(band.angle)}" x="${num(inner.center[0] - band.width / 2)}" y="${num(inner.center[1] - reach)}" width="${num(band.width)}" height="${num(reach * 2)}" transform="rotate(${num(band.angle)} ${num(inner.center[0])} ${num(inner.center[1])})"/>`).join('');
    const maskContent = shape(section.contours, 'white', 'black');
    const colored = `<g clip-path="url(#${id}-bands)"><rect x="${num(inner.center[0] - reach)}" y="${num(inner.center[1] - reach)}" width="${num(reach * 2)}" height="${num(reach * 2)}" fill="${model.part === 'male' ? '#3b82f6' : '#fff'}" mask="url(#${id}-ring)"/></g>`;
    const stock = shape(inner.contours, '#cbd5e1', '#fff', '#94a3b8', 1 / scale);
    const socket = shape(section.contours, 'none', 'none', '#dc2626', 1.3 / scale);
    let geometry = model.part === 'male'
      ? `<g data-insert-joint-shape="stock">${stock}</g><g data-insert-joint-shape="retained">${colored}</g>`
      : `<rect x="${num(inner.center[0] - reference * .64)}" y="${num(inner.center[1] - reference * .62)}" width="${num(reference * 1.28)}" height="${num(reference * 1.24)}" rx="${num(model.profiles.frame.kind === 'round' ? reference * .2 : reference * .03)}" fill="#cbd5e1" stroke="#94a3b8" stroke-width="${1 / scale}"/>
        <g data-insert-joint-shape="socket" data-insert-joint-offset-available="${section.available}">${colored}<g clip-path="url(#${id}-bands)">${socket}</g></g>`;
    if (model.part === 'female' && !section.available) geometry = `<g data-insert-joint-shape="stock">${stock}</g>`;
    const envelope = section.envelope, proven = envelope.proven;
    const dimKeyX = model.part === 'male' ? model.bindings.maleHeadWidth : model.form === 'full_section' ? model.bindings.fullHeadTotalClearance : model.bindings.holeTotalClearanceHeight;
    const dimKeyY = model.part === 'male' ? model.bindings.maleTabAngle : model.form === 'full_section' ? model.bindings.fullHeadTotalClearance : model.bindings.holeTotalClearanceDepth;
    const dimensions = proven ? dimension(map([envelope.minX, envelope.maxY]), map([envelope.maxX, envelope.maxY]), 25, `截面整体宽 ${fmt(envelope.width)}`, dimKeyX, options, fields)
      + dimension(map([envelope.maxX, envelope.maxY]), map([envelope.maxX, envelope.minY]), 284, `截面整体高 ${fmt(envelope.depth)}`, dimKeyY, options, fields, 'y') : '';
    const directionCaption = [visibleKey(model.bindings.maleTabCount, options, fields) ? `${fmt(model.direction.count)} 处` : '',
      visibleKey(model.bindings.maleTabAngle, options, fields) ? `${fmt(model.direction.angle)}°` : ''].filter(Boolean).join(' · ');
    const direction = model.form === 'full_section' ? '' : `<g class="tube-insert-detail-direction"${actionAttributes(model.bindings.maleTabAngle, options, fields)}>
      <path d="M 170 110 l ${num(42 * Math.cos(model.direction.angle * Math.PI / 180))} ${num(-42 * Math.sin(model.direction.angle * Math.PI / 180))}" stroke="#0f766e" stroke-width="1.2" stroke-dasharray="3 3"/>
      ${directionCaption ? `<text x="170" y="194" text-anchor="middle" font-size="11" fill="#0f766e">${directionCaption}</text>` : ''}</g>`;
    artwork = `<svg class="tube-insert-detail-artwork" data-insert-joint-artwork="${model.part}" data-insert-joint-profile-source="${inner.source}" viewBox="0 0 360 ${model.part === 'male' || model.singleHole.available ? 318 : 220}" role="group" aria-label="${escape(title)}">
      <defs><mask id="${id}-ring" maskUnits="userSpaceOnUse" x="${num(inner.center[0] - reach)}" y="${num(inner.center[1] - reach)}" width="${num(reach * 2)}" height="${num(reach * 2)}">${maskContent}</mask>
      <clipPath id="${id}-bands" clipPathUnits="userSpaceOnUse">${bandRects || `<rect x="${num(inner.center[0] - reach)}" y="${num(inner.center[1] - reach)}" width="${num(reach * 2)}" height="${num(reach * 2)}"/>`}</clipPath></defs>
      <g transform="${transform}">${geometry}</g>${dimensions}${direction}${model.part === 'male' ? longitudinal(model, options, fields) : `<text x="170" y="214" text-anchor="middle" font-size="11" fill="#64748b">沿插入方向看母口 · 管壁局部投影</text>${singleHoleArtwork(model, options, fields)}`}</svg>${model.part === 'male' ? tipArtwork(model, options, fields) : ''}`;
  }
  return `<section class="tube-insert-detail" data-insert-joint-part="${model.part}" data-insert-joint-form="${escape(model.form)}" data-insert-joint-through="${model.through}" data-insert-joint-available="${model.available}">
    <header>${escape(title)}</header>${artwork}${model.message ? `<p class="tube-insert-detail-note">${escape(model.message)}</p>` : ''}
    <dl class="tube-insert-detail-readouts">${readouts}</dl><p class="tube-insert-detail-note">${model.notes.map(escape).join(' ')}</p></section>`;
}
