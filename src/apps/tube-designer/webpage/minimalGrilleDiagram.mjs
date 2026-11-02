import { catalogText } from './productCatalog.mjs';

const EPS = 1e-7;
const escape = value => String(value ?? '').replaceAll('&', '&amp;').replaceAll('<', '&lt;')
  .replaceAll('>', '&gt;').replaceAll('"', '&quot;');

// Front elevation for the editor only. Manufacturing remains owned by the
// product template; these stocks also let the preview show local clearances
// without splitting the continuous horizontal rows.
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
  const frame = get('frameType'), enabled = get('handleEnabled') === 'yes';
  const side = enabled ? get('handleSide') : null;
  const open = enabled && get('handleClosure') === 'open_slot';
  const bottomFrame = frame !== 'two_vertical', topFrame = frame === 'closed_frame';
  const low = bottomFrame ? fw : 0, high = topFrame ? height - fw : height;
  const first = topFrame ? null : height - number('firstBarTopOffset', bw / 2);
  const last = bottomFrame ? null : number('lastBarBottomOffset', bw / 2);
  const fixedCount = get('barLayoutMode') === 'fixed_count';
  const gapLimit = fixedCount ? null : number('maximumClearGap', 1);
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
  const installationHoles = { enabled: get('installHoleEnabled') === 'yes',
    countPerSide: 0, centers: [], groups: [] };
  if (installationHoles.enabled) {
    const count = number('installHoleCountPerSide', 2);
    const bottom = number('installHoleBottomOffset'), top = height - number('installHoleTopOffset');
    if (![2, 3, 4, 5].includes(count) || bottom >= top - EPS || top > height + EPS)
      throw new Error('请调整安装孔组数或上下端距');
    installationHoles.countPerSide = count;
    installationHoles.centers = Array.from({ length: count }, (_, i) =>
      bottom + i * (top - bottom) / (count - 1));
    for (const side of ['left', 'right']) {
      const members = stocks.filter(stock => stock.role === `frame.${side}`);
      for (const z of installationHoles.centers) {
        const stock = members.find(member => z >= member.start[1] - EPS && z <= member.end[1] + EPS);
        if (!stock) throw new Error('安装孔位于把手开槽处，请调整上下端距或组数');
        installationHoles.groups.push({ side, memberKey: stock.key,
          station: z - stock.start[1], point: [stock.start[0], 0, z] });
      }
    }
  }
  return { width, height, stocks, openings, installationHoles };
}

export function minimalGrilleStyleLabel(template, diagram, values) {
  const choice = name => {
    const key = diagram.bindings[name], value = values[key];
    const field = template.parameters.find(f => (f.key ?? f.name) === key);
    const item = (field?.choices ?? field?.options ?? []).find(c => c.value === value);
    return catalogText(item?.displayName ?? item?.label, String(value ?? ''));
  };
  const enabled = values[diagram.bindings.handleEnabled] === 'yes';
  return [choice('frameType'), ...(enabled ? [choice('handleSide'), choice('handleClosure')] : [choice('handleEnabled')])].join(' · ');
}

export function renderMinimalGrilleArtwork(template, diagram, values) {
  const caption = minimalGrilleStyleLabel(template, diagram, values);
  let artwork;
  try {
    const { width, height, stocks, openings, installationHoles } = minimalGrilleDiagramStocks(diagram, values);
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
  } catch (error) {
    artwork = `<text x="170" y="94" text-anchor="middle" style="fill:#efc27a;stroke:none;font-size:12px">${escape(error.message)}</text>`;
  }
  return `<svg class="tube-designer-product-structure-svg" data-minimal-grille-diagram viewBox="0 0 340 190" role="img" aria-label="极简防护网：${escape(caption)}">${artwork}</svg>`;
}
