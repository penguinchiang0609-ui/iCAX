import { catalogText } from './productCatalog.mjs';

const EPS = 1e-7;
const escape = value => String(value ?? '').replaceAll('&', '&amp;').replaceAll('<', '&lt;')
  .replaceAll('>', '&gt;').replaceAll('"', '&quot;');

// Compact structural artwork for the editor only. Manufacturing remains owned by the
// product template; these stocks also let the preview show local clearances
// with the selected continuous or physically segmented horizontal stocks.
export function minimalGrilleDiagramStocks(diagram, values) {
  const get = name => values[diagram.bindings[name]];
  const number = (name, minimum = 0) => {
    const value = get(name);
    if (value === '' || value == null || !Number.isFinite(Number(value)) || Number(value) < minimum)
      throw new Error('请完善当前尺寸');
    return Number(value);
  };
  const width = number('width', 1), height = number('height', 1);
  const sectionWidth = role => {
    const snapshot = values.tubeDesignerProfileOverrides?.[role]?.width;
    return Number.isFinite(snapshot) && snapshot > 0 ? snapshot : number(role + 'Width', 1);
  };
  const fw = sectionWidth('frame'), bw = sectionWidth('inner');
  const fiveFace = get('layoutStyle') === 'five_face';
  const balcony = get('layoutStyle') === 'balcony_closure', spatial = fiveFace || balcony;
  const frame = spatial ? 'closed_frame' : get('frameType'), enabled = get('handleEnabled') === 'yes';
  const side = enabled ? get('handleSide') : null;
  const open = enabled && get('handleClosure') === 'open_slot';
  const bottomFrame = frame !== 'two_vertical', topFrame = frame === 'closed_frame';
  const low = bottomFrame ? fw : 0, high = topFrame ? height - fw : height;
  const centerSpacing = get('barLayoutMode') === 'center_spacing';
  const first = topFrame || (centerSpacing && !enabled && frame === 'two_vertical') ? null : height - number('firstBarTopOffset', bw / 2);
  const last = bottomFrame ? null : number('lastBarBottomOffset', bw / 2);
  const fixedCount = get('barLayoutMode') === 'fixed_count';
  const pitch = centerSpacing ? number('barCenterSpacing', bw + EPS) : null;
  const gapLimit = fixedCount || centerSpacing ? null : number('maximumClearGap', 1);
  const stocks = [], openings = [];
  const add = (key, role, x1, z1, x2, z2) => {
    if (x2 - x1 + z2 - z1 <= EPS) throw new Error('当前尺寸不足以形成所选款式');
    stocks.push({ key, role, width: role.startsWith('frame.') ? fw : bw,
      start: [x1, z1], end: [x2, z2] });
  };
  let hb, ht, center = null;
  if (enabled) {
    hb = number('handleBottom'); ht = hb + number('handleHeight', 1);
    const hw = number('handleWidth', 1);
    if (hb - bw <= low + EPS || ht + bw >= high - EPS)
      throw new Error('请调整把手位置或成品高度');
    if (side === 'left') openings.push({ left: fw, right: fw + hw, bottom: hb, top: ht });
    else if (side === 'right') openings.push({ left: width - fw - hw, right: width - fw, bottom: hb, top: ht });
    else {
      center = width / 2 + number('handleCenterOffset', -10000);
      openings.push({ left: center - bw / 2 - hw, right: center - bw / 2, bottom: hb, top: ht },
        { left: center + bw / 2, right: center + bw / 2 + hw, bottom: hb, top: ht });
    }
    if (openings.some(o => o.left < fw - EPS || o.right > width - fw + EPS))
      throw new Error('请调整把手净宽或中间偏移');
  }
  for (const [label, x] of [['left', fw / 2], ['right', width - fw / 2]]) {
    const intervals = open && side === label ? [[0, hb], [ht, height]] : [[0, height]];
    intervals.forEach(([a, b], i) => add(`frame.${label}.${String(i + 1).padStart(4, '0')}`, `frame.${label}`, x, a, x, b));
  }
  if (bottomFrame) add('frame.bottom.0001', 'frame.bottom', fw, fw / 2, width - fw, fw / 2);
  if (topFrame) add('frame.top.0001', 'frame.top', fw, height - fw / 2, width - fw, height - fw / 2);
  if (center != null) (open ? [[low, hb], [ht, high]] : [[low, high]]).forEach(([a, b], i) =>
    add(`inner.center.${String(i + 1).padStart(4, '0')}`, 'inner.center', center, a, center, b));
  const rows = (a, b, countKey, bottom = null, top = null) => {
    if (centerSpacing) {
      const lo = a + bw / 2, hi = b - bw / 2;
      if (hi < lo - EPS) return [];
      let centers;
      if (bottom != null) {
        const count = Math.max(0, Math.floor((hi - bottom) / pitch + EPS)) + 1;
        centers = Array.from({ length: count }, (_, i) => bottom + i * pitch);
      } else if (top != null) {
        const count = Math.max(0, Math.floor((top - lo) / pitch + EPS)) + 1;
        centers = Array.from({ length: count }, (_, i) => top - (count - 1 - i) * pitch);
      } else {
        const count = Math.floor((hi - lo) / pitch + EPS) + 1;
        const start = (lo + hi - (count - 1) * pitch) / 2;
        centers = Array.from({ length: count }, (_, i) => start + i * pitch);
      }
      if (centers.length > 1500) throw new Error('横杆数量过多，请增大中心距');
      return centers;
    }
    const fixed = [];
    if (bottom != null) { fixed.push(bottom); a = bottom + bw / 2; }
    if (top != null) { fixed.push(top); b = top - bw / 2; }
    const available = b - a;
    const n = fixedCount ? number(countKey) - fixed.length : Math.max(0, Math.ceil((available - gapLimit) / (bw + gapLimit) - EPS));
    if (!Number.isInteger(n) || n < 0 || n > 1500 || available < -EPS || n * bw > available + EPS)
      throw new Error('请调整横杆根数或间距');
    const gap = (available - n * bw) / (n + 1);
    return [...fixed, ...Array.from({ length: n }, (_, i) => a + gap + bw / 2 + i * (gap + bw))].sort((x, y) => x - y);
  };
  const sets = enabled ? [
    ['lower', rows(low, hb - bw, 'lowerBarCount', last)], ['lower_boundary', [hb - bw / 2]],
    ['middle', rows(hb, ht, 'middleBarCount')], ['upper_boundary', [ht + bw / 2]],
    ['upper', rows(ht + bw, high, 'upperBarCount', null, first)],
  ] : [['full', rows(low, high, 'barCount', last, first)]];
  if (enabled) {
    const jambs = side === 'left' ? [openings[0].right + bw / 2]
      : side === 'right' ? [openings[0].left - bw / 2] : [openings[0].left - bw / 2, openings[1].right + bw / 2];
    jambs.forEach((x, i) => add(`inner.handle_jamb.${String(i + 1).padStart(4, '0')}`, 'inner.handle_jamb', x, hb, x, ht));
  }
  for (const [zone, centers] of sets) centers.forEach((z, i) => {
    let spans = center == null ? [[fw, width - fw]] : [[fw, center - bw / 2], [center + bw / 2, width - fw]];
    if (zone === 'middle') spans = side === 'left' ? [[openings[0].right + bw, width - fw]]
      : side === 'right' ? [[fw, openings[0].left - bw]] : [[fw, openings[0].left - bw], [openings[1].right + bw, width - fw]];
    spans.forEach(([a, b], j) => add(`inner.${zone}.${String(i + 1).padStart(4, '0')}.${String(j + 1).padStart(2, '0')}`,
      `inner.${zone}`, a, z, b, z));
  });
  if (spatial && center != null) {
    const central = stocks.find(stock => stock.role === 'inner.center');
    for (let i = stocks.length - 1; i >= 0; i--) if (stocks[i].role === 'inner.center') stocks.splice(i, 1);
    if (!open) stocks.push({ ...central, key: 'inner.center.0001', start: [center, hb], end: [center, ht] });
    const groups = new Map();
    for (const stock of stocks) if (stock.role.startsWith('inner.') && !['inner.handle_jamb', 'inner.center', 'inner.middle'].includes(stock.role)
        && Math.abs(stock.start[1] - stock.end[1]) < EPS) {
      const key = `${stock.role}/${stock.start[1]}`;
      if (!groups.has(key)) groups.set(key, []);
      groups.get(key).push(stock);
    }
    for (const row of groups.values()) if (row.length === 2) {
      row.sort((a, b) => a.start[0] - b.start[0]);
      row[0].end = [...row[1].end]; stocks.splice(stocks.indexOf(row[1]), 1);
    }
  }
  const installationHoles = { enabled: get('installHoleEnabled') === 'yes',
    countPerSide: 0, centers: [], groups: [] };
  if (installationHoles.enabled) {
    const count = number('installHoleCountPerSide', 2);
    const mountingHeight = height + (fiveFace ? number('topDrop') : 0);
    const bottom = number('installHoleBottomOffset'), top = mountingHeight - number('installHoleTopOffset');
    if (![2, 3, 4, 5].includes(count) || bottom >= top - EPS || top > mountingHeight + EPS)
      throw new Error('请调整安装孔组数或上下端距');
    installationHoles.countPerSide = count;
    installationHoles.centers = Array.from({ length: count }, (_, i) =>
      bottom + i * (top - bottom) / (count - 1));
    for (const side of ['left', 'right']) {
      const members = stocks.filter(stock => stock.role === `frame.${side}`);
      for (const z of installationHoles.centers) {
        const stock = fiveFace ? { key: `frame.rear_${side}.0001`, start: [side === 'left' ? fw / 2 : width - fw / 2, 0] }
          : members.find(member => z >= member.start[1] - EPS && z <= member.end[1] + EPS);
        if (!stock) throw new Error('安装孔位于把手开槽处，请调整上下端距或组数');
        installationHoles.groups.push({ side, memberKey: stock.key,
          station: z - stock.start[1], point: [stock.start[0], 0, z] });
      }
    }
  }
  const state = { layoutStyle: get('layoutStyle') ?? 'planar', width, height, stocks, openings, installationHoles };
  if (spatial) {
    const supportLinkCount = get('supportStyle') === 'double' && diagram.bindings.supportLinkCount ? number('supportLinkCount') : 0;
    if (!Number.isInteger(supportLinkCount) || supportLinkCount > 100) throw new Error('双竖杆间短横杆数量必须为 0～100 的整数');
    const depth = role => {
      const override = values.tubeDesignerProfileOverrides?.[role];
      if (Number.isFinite(override?.depth) && override.depth > 0) return override.depth;
      if (override?.sectionKind === 'round' || get(`${role}ProfileType`) === 'round') return sectionWidth(role);
      return number(`${role}Depth`, EPS);
    };
    state.spatial = fiveFaceDiagramGeometry(state, { balcony, frameWidth: fw, frameDepth: depth('frame'), innerWidth: bw,
      innerDepth: depth('inner'), supportWidth: sectionWidth('support'),
      projectionVerticalWidth: fiveFace ? sectionWidth('projectionVertical') : null,
      projectionVerticalDepth: fiveFace ? depth('projectionVertical') : null,
      projectionHorizontalWidth: sectionWidth('projectionHorizontal'), projectionHorizontalDepth: depth('projectionHorizontal'),
      supportProfileKind: values.tubeDesignerProfileOverrides?.support?.sectionKind ?? get('supportProfileType'),
      supportStyle: get('supportStyle') ?? 'single',
      supportPairClearGap: get('supportStyle') === 'double' ? number('supportPairClearGap', 1) : 0,
      supportLinkCount,
      supportLinkBottomOffset: supportLinkCount ? number('supportLinkBottomOffset') : 0,
      supportLinkTopOffset: supportLinkCount > 1 ? number('supportLinkTopOffset') : 0,
      projectionDepth: number(balcony ? 'balconyTopDepth' : 'projectionDepth', EPS),
      bottomProjectionDepth: number(balcony ? 'balconyBottomDepth' : 'projectionDepth', EPS),
      topDrop: number(balcony ? 'balconyTopDrop' : 'topDrop'),
      dryingRodEnabled: get('dryingRodEnabled') === 'yes',
      ...(get('dryingRodEnabled') === 'yes' ? {
        dryingWidth: sectionWidth('drying'), dryingDepth: depth('drying'),
        dryingRodRearOffset: number('dryingRodRearOffset'), dryingRodDrop: number('dryingRodDrop') } : {}),
      supportMaxSpacing: number('supportMaxSpacing', 1), returnMaximumClearGap: number('returnMaximumClearGap', 1) });
    if (fiveFace && installationHoles.enabled) for (const hole of installationHoles.groups) {
      hole.point[0] = hole.side === 'left' ? state.spatial.projectionVerticalWidth / 2 : width - state.spatial.projectionVerticalWidth / 2;
      hole.point[1] = state.spatial.rearVerticalY;
      hole.memberKey = `frame.rear_${hole.side}.0001`;
      hole.station = hole.point[2];
    }
  }
  return state;
}

function supportPositions(width, fw, bw, maximum, openings, sw, minimumGroups = 0) {
  const left = fw / 2, right = width - fw / 2;
  const edgeSeparation = (fw + sw) / 2;
  const bays = Math.max(1 + minimumGroups, Math.ceil((right - left) / maximum - EPS));
  const forbidden = openings.map(o => [o.left - sw / 2 - bw - EPS, o.right + sw / 2 + bw + EPS]).sort((a, b) => a[0] - b[0]);
  const legal = x => left + edgeSeparation + EPS < x && x < right - edgeSeparation - EPS && forbidden.every(([a, b]) => !(a < x && x < b));
  const move = (x, a, b) => {
    const before = Math.abs(a - left) <= EPS ? edgeSeparation : sw;
    const after = Math.abs(b - right) <= EPS ? edgeSeparation : sw;
    const candidates = [x, ...forbidden.flat()].filter(v => a + before + EPS < v && v < b - after - EPS && legal(v));
    if (!candidates.length) throw new Error('请调整把手位置或支撑最大中心距');
    return candidates.sort((u, v) => Math.abs(u - x) - Math.abs(v - x) || u - v)[0];
  };
  let posts = [...new Set(Array.from({ length: bays - 1 }, (_, i) => move(left + (right - left) * (i + 1) / bays, left, right)))].sort((a, b) => a - b);
  for (let i = 0; i < 500; i++) {
    const anchors = [left, ...posts, right];
    const gaps = anchors.slice(1).map((b, j) => [b - anchors[j], anchors[j], b]).sort((a, b) => b[0] - a[0] || b[1] - a[1]);
    const [gap, a, b] = gaps[0];
    if (gap <= maximum + EPS) {
      if (anchors.slice(1).some((b, i) => b - anchors[i] <= ([0, anchors.length - 2].includes(i) ? edgeSeparation : sw) + EPS))
        throw new Error('请调整支撑间距或把手位置');
      return posts;
    }
    const value = move((a + b) / 2, a, b);
    if (posts.some(old => Math.abs(value - old) <= EPS)) throw new Error('当前把手位置无法满足支撑最大中心距');
    posts.push(value); posts.sort((a, b) => a - b);
  }
  throw new Error('竖杆数量过多，请增大间距');
}

function returnPositions(width, fw, bd, maximumGap, supports, sw, boundaryDepth, rearFrameWidth = 0) {
  const left = boundaryDepth == null ? fw / 2 : boundaryDepth - bd / 2;
  const right = boundaryDepth == null ? width - fw / 2 : width - boundaryDepth + bd / 2;
  const available = right - left - bd;
  if (available <= EPS) throw new Error('左右边界回杆之间须保留有效净空，请增大成品宽度或减小 B 管截面深度');
  const count = Math.max(0, Math.ceil((available - maximumGap) / (bd + maximumGap) - EPS));
  const gap = (available - count * bd) / (count + 1);
  const uniform = Array.from({ length: count }, (_, i) => left + bd + gap + i * (bd + gap));
  const reserve = (sw + bd) / 2 + 1e-5;
  const blocked = supports.map(x => [x - reserve, x + reserve]);
  const inset = Math.max(fw, rearFrameWidth, boundaryDepth ?? 0);
  const minimum = inset + bd / 2 + 1e-5, maximum = width - inset - bd / 2 - 1e-5;
  const legal = x => minimum - EPS <= x && x <= maximum + EPS && blocked.every(([a, b]) => !(a < x && x < b));
  if (uniform.every(legal)) return { positions: uniform, gaps: Array(count + 1).fill(gap) };
  const positions = [], step = bd + maximumGap;
  let current = left;
  for (let i = 0; i < 1500; i++) {
    if (right - current <= step + EPS) {
      const centers = [left, ...positions, right];
      return { positions, gaps: centers.slice(1).map((b, j) => b - centers[j] - bd) };
    }
    const target = current + step;
    const candidates = [target, ...blocked.map(([a]) => a), maximum];
    const choices = candidates.filter(x => current + bd + EPS < x && x <= target + EPS && legal(x));
    if (!choices.length) throw new Error('C 管支撑与顶底杆净距冲突，请增大净距或调整支撑位置/管型');
    current = Math.max(...choices); positions.push(current);
  }
  throw new Error('顶底回杆过多，请增大净距');
}

// The compact artwork shows centre-line connections. Exact receiving-surface
// trims, apertures and manufacturing lengths remain in the native template.
function fiveFaceDiagramGeometry(state, settings) {
  const { width, height, stocks, openings } = state;
  const { balcony, frameWidth: fw, frameDepth: fd, innerWidth: bw, innerDepth: bd, supportWidth: sw, projectionDepth, bottomProjectionDepth, topDrop,
    supportMaxSpacing, returnMaximumClearGap, supportStyle, supportPairClearGap,
    projectionVerticalWidth: vw, projectionVerticalDepth: vd, projectionHorizontalWidth: hw, projectionHorizontalDepth: hd } = settings;
  const frontOuterY = -fd / 2, rearOuterY = frontOuterY + projectionDepth, bottomRearOuterY = frontOuterY + bottomProjectionDepth;
  const rearY = rearOuterY - hd / 2, bottomY = bottomRearOuterY - hd / 2;
  const rearVerticalY = balcony ? null : rearOuterY - vd / 2;
  if (width <= 2 * hd + EPS) throw new Error('左右边界回杆之间须保留有效净空，请增大成品宽度或减小飘面横向骨架深度');
  if (!Number.isFinite(fd) || !Number.isFinite(bd) || Math.min(rearY - hd / 2, bottomY - hd / 2,
      balcony ? rearY : rearVerticalY - vd / 2) <= fd / 2 + EPS) throw new Error('请增大飘出外包深度');
  const left = fw / 2, right = width - fw / 2, top = height - fw / 2, bottom = fw / 2;
  const members = stocks.map(stock => ({ ...stock, face: 'front', role: balcony && ['frame.left','frame.right'].includes(stock.role)
    ? stock.role : stock.role.replace(/^frame\./, 'frame.front_'),
    profileRole: stock.role.startsWith('frame.') ? 'frame' : 'inner',
    start: [stock.start[0], 0, stock.start[1]], end: [stock.end[0], 0, stock.end[1]] }));
  const add = (key, role, face, start, end, stockWidth, profileRole) => members.push({ key, role, face, start, end, width: stockWidth, profileRole });
  if (!balcony) for (const [side, x] of [['left', vw / 2], ['right', width - vw / 2]])
    add(`frame.rear_${side}.0001`, `frame.${side}`, 'mounting', [x, rearVerticalY, 0], [x, rearVerticalY, height + topDrop], vw, 'projectionVertical');
  for (const [name, z, y] of [['bottom', hw / 2, bottomY], ['top', height + topDrop - hw / 2, rearY]])
    add(`frame.rear_${name}.0001`, `frame.rear_${name}`, 'mounting', [balcony ? 0 : vw / 2, y, z], [width - (balcony ? 0 : vw / 2), y, z], hw, 'projectionHorizontal');
  if (!['single','double'].includes(supportStyle)) throw new Error('请选择单竖杆或双竖杆分段');
  const paired = supportStyle === 'double', segmented = paired || settings.supportProfileKind === 'rect-bar';
  const groupWidth = paired ? 2 * sw + supportPairClearGap : sw;
  const supportGroupCenters = supportPositions(width, fw, segmented ? 2 * bw : bw, supportMaxSpacing, openings, groupWidth, paired ? 1 : 0);
  const supportX = paired ? supportGroupCenters.flatMap(x => [x - (sw + supportPairClearGap) / 2, x + (sw + supportPairClearGap) / 2])
    : supportGroupCenters;
  supportX.forEach((x, i) => add(`frame.support.${String(i + 1).padStart(4, '0')}`, 'frame.support', 'front',
    [x, 0, fw], [x, 0, height - fw], sw, 'support'));
  let splitRowCount = 0;
  if (segmented) {
    const rows = members.filter(m => m.face === 'front' && m.role.startsWith('inner.') && Math.abs(m.start[2] - m.end[2]) < EPS);
    for (const row of rows) {
      let spans = [[row.start, row.end]];
      for (const center of supportGroupCenters) {
        const leftEnd = [center - groupWidth / 2, 0, row.start[2]], rightStart = [center + groupWidth / 2, 0, row.start[2]];
        const remaining = [];
        for (const [start, end] of spans) {
          if (end[0] <= leftEnd[0] + EPS || start[0] >= rightStart[0] - EPS) remaining.push([start,end]);
          else if (start[0] < leftEnd[0] - EPS && end[0] > rightStart[0] + EPS) remaining.push([start,leftEnd],[rightStart,end]);
          else throw new Error('支撑竖杆与把手端部冲突，请调整支撑间距或把手位置');
        }
        spans = remaining;
      }
      if (spans.length === 1) continue;
      splitRowCount++;
      members.splice(members.indexOf(row), 1);
      spans.forEach(([start,end], i) => members.push({ ...row, key:`${row.key}.span.${String(i+1).padStart(4,'0')}`, sourceRowKey:row.key, start, end }));
    }
  }
  const supportLinkCenters = [];
  if (settings.supportLinkCount) {
    const count = settings.supportLinkCount, first = settings.supportLinkBottomOffset;
    const last = count > 1 ? height - settings.supportLinkTopOffset : first;
    if (first - bw / 2 <= fw + EPS || last + bw / 2 >= height - fw - EPS)
      throw new Error('双竖杆间短横杆须位于前上下 A 料内侧并保留有效净空，请调整上下位置');
    if (count > 1 && (last - first) / (count - 1) <= bw + EPS)
      throw new Error('双竖杆间短横杆相互重叠或没有有效净空，请减少数量或调整上下位置');
    for (let i = 0; i < count; i++) supportLinkCenters.push(count === 1 ? first : first + (last - first) * i / (count - 1));
    supportGroupCenters.forEach((center, group) => supportLinkCenters.forEach((z, i) =>
      add(`inner.support_link.${String(group+1).padStart(4,'0')}.${String(i+1).padStart(4,'0')}`, 'inner.support_link', 'front',
        [center - supportPairClearGap / 2, 0, z], [center + supportPairClearGap / 2, 0, z], bw, 'inner')));
  }
  const levels = [...new Set(stocks.filter(s => s.role.startsWith('inner.') && s.start[1] === s.end[1]).map(s => s.start[1]))].sort((a, b) => a - b);
  const bottomCenterZ = hw / 2, roofFrontCenterZ = height - hw / 2, roofRearCenterZ = height + topDrop - hw / 2;
  const projectionBoundaryXs = [hd / 2, width - hd / 2];
  const sideInset = balcony ? null : Math.max(bd / 2, Math.min(fw, vw) / 2);
  for (const [side, x, boundaryX] of [['left', left, projectionBoundaryXs[0]], ['right', right, projectionBoundaryXs[1]]]) {
    const hosts = stocks.filter(s => s.role === `frame.${side}`);
    for (const [edge, z, rearZ, y] of [['bottom', bottomCenterZ, bottomCenterZ, bottomY], ['top', roofFrontCenterZ, roofRearCenterZ, rearY]]) {
      if (!hosts.some(s => s.start[1] - EPS <= z && z <= s.end[1] + EPS))
        throw new Error('侧面骨架的接收竖框缺失，请调整把手位置或骨架截面');
      add(`frame.return_${side}.${edge}`, `frame.return_${side}`, side,
        [boundaryX, 0, z], [boundaryX, y, rearZ], hw, 'projectionHorizontal');
    }
    if (!balcony) levels.forEach((z, i) => {
      if (!hosts.some(s => s.start[1] - EPS <= z && z <= s.end[1] + EPS)) return;
      const sideX = side === 'left' ? sideInset : width - sideInset;
      add(`inner.return_${side}.${String(i + 1).padStart(4, '0')}`, `inner.return_${side}`, side,
        [sideX, 0, z], [sideX, rearVerticalY, z], bw, 'inner');
    });
  }
  const { positions: returnX, gaps: returnClearGaps } = returnPositions(width, fw, bd, returnMaximumClearGap, supportX, sw, hd, balcony ? 0 : vw);
  const returnInset = Math.max(hw, bw) / 2;
  const returnBottomZ = returnInset, returnFrontTopZ = height - returnInset, returnRearTopZ = height + topDrop - returnInset;
  for (const [face, z, backZ] of [['top', returnFrontTopZ, returnRearTopZ], ['bottom', returnBottomZ, returnBottomZ]])
    returnX.forEach((x, i) => add(`inner.${face}_return.${String(i + 1).padStart(4, '0')}`, `inner.${face}_return`, face,
      [x, 0, z], [x, face === 'top' ? rearY : bottomY, backZ], bw, 'inner'));
  let dryingRod = { enabled: false };
  if (settings.dryingRodEnabled) {
    const dw = settings.dryingWidth, dd = settings.dryingDepth;
    const rodY = rearOuterY - settings.dryingRodRearOffset, rodZ = height - settings.dryingRodDrop;
    if (width <= 2 * dw + EPS || rodY - dd / 2 <= frontOuterY + EPS || rodY + dd / 2 >= rearOuterY - EPS
        || rodZ - dw / 2 <= EPS || rodZ + dw / 2 >= height + EPS)
      throw new Error('请调整晾衣杆后侧偏移、下移距离或管型尺寸');
    const topContacts = [];
    for (const [side, x] of [['left', dw / 2], ['right', width - dw / 2]]) {
      // Like the other artwork stocks, this is a source axis, not a fabricated
      // cut length. Follow the nearest real side stock above the arm; native
      // geometry resolves its exact receiving surface and final cope.
      const candidates = members.filter(m => m.key === `frame.return_${side}.top` || m.role === `inner.return_${side}`)
        .filter(m => Math.abs(x - m.start[0]) <= (m.profileRole === 'projectionHorizontal' ? hd : bd) / 2 + EPS
          && rodY >= Math.min(m.start[1], m.end[1]) - EPS && rodY <= Math.max(m.start[1], m.end[1]) + EPS)
        .map(m => ({ hostKey: m.key, z: m.start[2] + (m.end[2] - m.start[2]) * (rodY - m.start[1]) / (m.end[1] - m.start[1]) }))
        .filter(c => c.z > rodZ - dw / 2 + EPS).sort((a, b) => a.z - b.z || a.hostKey.localeCompare(b.hostKey));
      if (!candidates.length || candidates[0].z <= rodZ + dw / 2 + EPS)
        throw new Error('晾衣杆吊臂上方缺少有效侧杆接触，请调整距顶部或用料尺寸');
      const host = candidates[0], armKey = `frame.drying.${side}.0001`;
      add(armKey, `frame.drying.${side}`, 'drying', [x, rodY, rodZ - dw / 2], [x, rodY, host.z], dw, 'drying');
      topContacts.push({ armKey, hostKey: host.hostKey, point: [x, rodY, host.z] });
    }
    add('frame.drying.rod.0001', 'frame.drying.rod', 'drying',
      [dw, rodY, rodZ], [width - dw, rodY, rodZ], dw, 'drying');
    dryingRod = { enabled: true, rodY, rodZ, topContacts };
  }
  return { ...settings, rearY, bottomY, rearVerticalY, frontOuterY, topRearOuterY: rearOuterY, bottomRearOuterY,
    projectionBoundaryXs, bottomCenterZ, roofFrontCenterZ, roofRearCenterZ, returnBottomZ, returnFrontTopZ, returnRearTopZ,
    members, supportX, supportGroupCenters, supportLinkCenters, returnX, dryingRod,
    supportConnectionKind: segmented ? 'segmented-weld' : 'through', continuousHorizontalRows: splitRowCount === 0,
    returnActualClearGap: Math.max(...returnClearGaps), returnClearGaps };
}

export function minimalGrilleStyleLabel(template, diagram, values) {
  const choice = name => {
    const key = diagram.bindings[name], value = values[key];
    const field = template.parameters.find(f => (f.key ?? f.name) === key);
    const item = (field?.choices ?? field?.options ?? []).find(c => c.value === value);
    return catalogText(item?.displayName ?? item?.label, String(value ?? ''));
  };
  const enabled = values[diagram.bindings.handleEnabled] === 'yes';
  const spatial = ['five_face','balcony_closure'].includes(values[diagram.bindings.layoutStyle]);
  return [...(diagram.bindings.layoutStyle ? [choice('layoutStyle')] : []),
    ...(!spatial ? [choice('frameType')] : []),
    ...(enabled ? [choice('handleSide'), choice('handleClosure')] : [choice('handleEnabled')]),
    ...(spatial && diagram.bindings.supportStyle ? [choice('supportStyle')] : []),
    ...(spatial && values[diagram.bindings.dryingRodEnabled] === 'yes' ? ['带晾衣杆'] : [])].join(' · ');
}

export function renderMinimalGrilleArtwork(template, diagram, values) {
  const caption = minimalGrilleStyleLabel(template, diagram, values);
  let artwork;
  try {
    const state = minimalGrilleDiagramStocks(diagram, values);
    const { width, height, stocks, openings, installationHoles } = state;
    if (state.spatial) artwork = renderFiveFaceArtwork(state);
    else {
    const scale = Math.min(270 / width, 166 / height);
    const x = v => 170 + (v - width / 2) * scale, y = v => 12 + (height - v) * scale;
    artwork = openings.map(o => `<rect class="minimal-grille-clearance" x="${x(o.left)}" y="${y(o.top)}" width="${(o.right - o.left) * scale}" height="${(o.top - o.bottom) * scale}" style="fill:#efb95522;stroke:#efb955;stroke-width:.7"/>`).join('');
    artwork += stocks.map(stock => {
      const horizontal = stock.start[1] === stock.end[1], half = stock.width / 2;
      const left = stock.start[0] - (horizontal ? 0 : half), top = stock.end[1] + (horizontal ? half : 0);
      const w = horizontal ? stock.end[0] - stock.start[0] : stock.width;
      const h = horizontal ? stock.width : stock.end[1] - stock.start[1];
      return `<rect data-grille-stock="${escape(stock.key)}" data-grille-role="${escape(stock.role)}" x="${x(left)}" y="${y(top)}" width="${w * scale}" height="${h * scale}" style="fill:#a8e5dc;stroke:#a8e5dc;stroke-width:.65"/>`;
    }).join('');
    artwork += installationHoles.groups.map((hole, i) =>
      `<circle data-grille-install-hole="${i + 1}" data-side="${hole.side}" data-center-z="${hole.point[2]}" data-member-key="${escape(hole.memberKey)}" cx="${x(hole.point[0])}" cy="${y(hole.point[2])}" r="2.2" style="fill:#ef5350;stroke:#7b2020;stroke-width:.55"><title>${hole.side === 'left' ? '左' : '右'}侧安装孔 · 离底 ${escape(hole.point[2])} mm</title></circle>`).join('');
    }
  } catch (error) {
    artwork = `<text x="170" y="94" text-anchor="middle" style="fill:#efc27a;stroke:none;font-size:12px">${escape(error.message)}</text>`;
  }
  const layout = ['five_face','balcony_closure'].includes(values[diagram.bindings.layoutStyle]) ? values[diagram.bindings.layoutStyle] : 'planar';
  return `<svg class="tube-designer-product-structure-svg" data-minimal-grille-diagram data-grille-layout="${layout}" viewBox="0 0 340 190" role="img" aria-label="极简防护网：${escape(caption)}">${artwork}</svg>`;
}

function renderFiveFaceArtwork(state) {
  const { width, height, openings, installationHoles, spatial } = state;
  const { members, rearY, bottomY, topDrop, balcony } = spatial;
  // Oblique projection keeps the front elevation readable while the returns
  // and raised rear top show the actual depth and outward roof slope.
  const project = ([x, y, z]) => [x + .7 * y, -z - .45 * y];
  const extents = members.flatMap(m => [project(m.start), project(m.end)]);
  const minX = Math.min(...extents.map(p => p[0])), maxX = Math.max(...extents.map(p => p[0]));
  const minY = Math.min(...extents.map(p => p[1])), maxY = Math.max(...extents.map(p => p[1]));
  const scale = Math.min(290 / (maxX - minX), 160 / (maxY - minY));
  const point = p => { const [x, y] = project(p); return [170 + (x - (minX + maxX) / 2) * scale, 95 + (y - (minY + maxY) / 2) * scale]; };
  const points = values => values.map(p => point(p).join(',')).join(' ');
  const outline = {
    front: [[0, 0, 0], [width, 0, 0], [width, 0, height], [0, 0, height]],
    left: [[0, 0, 0], [0, rearY, 0], [0, rearY, height + topDrop], [0, 0, height]],
    right: [[width, 0, 0], [width, rearY, 0], [width, rearY, height + topDrop], [width, 0, height]],
    top: [[0, 0, height], [width, 0, height], [width, rearY, height + topDrop], [0, rearY, height + topDrop]],
    bottom: [[0, 0, 0], [width, 0, 0], [width, bottomY, 0], [0, bottomY, 0]],
  };
  const stroke = { mounting: '#689f9c', drying: '#efc27a', left: '#72bfb5', right: '#72bfb5', top: '#92d8cf', bottom: '#72bfb5', front: '#a8e5dc' };
  const memberLine = m => {
    const a = point(m.start), b = point(m.end);
    return `<line data-grille-stock="${escape(m.key)}" data-grille-role="${escape(m.role)}" data-grille-profile-role="${escape(m.profileRole)}" x1="${a[0]}" y1="${a[1]}" x2="${b[0]}" y2="${b[1]}" style="fill:none;stroke:${stroke[m.face]};stroke-width:${Math.max(.8, m.width * scale)};stroke-linecap:butt"/>`;
  };
  let result = `<g data-grille-mounting-frame>${members.filter(m => m.face === 'mounting').map(memberLine).join('')}</g>`;
  if (spatial.dryingRod.enabled)
    result += `<g data-grille-drying-rod><title>晾衣杆：一根通长横杆及左右吊臂</title>${members.filter(m => m.face === 'drying').map(memberLine).join('')}</g>`;
  const visibleFace = m => balcony && ['left','right'].includes(m.face) ? (m.start[2] > height / 2 ? 'top' : 'bottom') : m.face;
  for (const face of (balcony ? ['bottom','top','front'] : ['bottom', 'left', 'right', 'top', 'front'])) {
    result += `<g data-grille-face="${face}"><polygon points="${points(outline[face])}" style="fill:${face === 'front' ? '#a8e5dc05' : '#a8e5dc0c'};stroke:none"/>`;
    if (face === 'front') result += openings.map(o => `<polygon class="minimal-grille-clearance" points="${points([[o.left,0,o.bottom],[o.right,0,o.bottom],[o.right,0,o.top],[o.left,0,o.top]])}" style="fill:#efb95522;stroke:#efb955;stroke-width:.7"/>`).join('');
    result += members.filter(m => visibleFace(m) === face).map(memberLine).join('') + '</g>';
  }
  result += installationHoles.groups.map((hole, i) => {
    const [x, y] = point(hole.point);
    return `<circle data-grille-install-hole="${i + 1}" data-side="${hole.side}" data-center-z="${hole.point[2]}" data-member-key="${escape(hole.memberKey)}" cx="${x}" cy="${y}" r="2.2" style="fill:#ef5350;stroke:#7b2020;stroke-width:.55"><title>${balcony ? '前侧立柱' : '后侧安装框'} · ${hole.side === 'left' ? '左' : '右'} · 离底 ${escape(hole.point[2])} mm</title></circle>`;
  }).join('');
  return result;
}
