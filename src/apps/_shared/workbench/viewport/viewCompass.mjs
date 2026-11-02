const fixed = value => Number(value.toFixed(3));

// The compass lies on the cube's XY base. Keep a small visible thickness at
// the horizon so a side view never turns the rotation handle into a hairline.
export function renderViewCompass({basis, origin, scale, id}) {
  const flatten = Math.max(.28, Math.abs(basis.eye.z));
  const sign = basis.eye.z < -1e-6 ? -1 : 1;
  const cx = origin.x, cy = origin.y + 1.5 * scale * basis.up.z, radius = 58;
  const point = (angle, r = radius) => ({
    x: cx + r * (Math.cos(angle) * basis.right.x + Math.sin(angle) * basis.right.y),
    y: cy + r * flatten * sign * (Math.cos(angle) * basis.right.y - Math.sin(angle) * basis.right.x),
  });
  const ticks = Array.from({length:24}, (_,i) => {
    if (i % 6 === 0) return '';
    const a = point(i*Math.PI/12, radius-2.5), b = point(i*Math.PI/12, radius+2.5);
    return `<path d="M${fixed(a.x)} ${fixed(a.y)}L${fixed(b.x)} ${fixed(b.y)}"/>`;
  }).join('');
  const labels = ['右','后','左','前'].map((label,i) => {
    const p = point(i*Math.PI/2);
    return `<text x="${fixed(p.x)}" y="${fixed(p.y)}">${label}</text>`;
  }).join('');
  return `<g class="cam-viewcube-compass" data-cam-compass data-cx="${cx}" data-cy="${fixed(cy)}" data-rx="${radius}" data-ry="${fixed(radius*flatten)}" data-sign="${sign}" role="slider" tabindex="0" aria-label="绕竖直轴旋转视野" aria-valuemin="0" aria-valuemax="360">
    <title>拖动圆环旋转视野 · 左右方向键微调 · Esc 取消拖动</title>
    <defs><linearGradient id="${id}-compass" x2="0" y2="1"><stop stop-color="#8996a2"/><stop offset="1" stop-color="#c1cad2"/></linearGradient></defs>
    <ellipse class="cam-viewcube-compass-band" cx="${cx}" cy="${fixed(cy)}" rx="${radius}" ry="${fixed(radius*flatten)}" fill="none" stroke="url(#${id}-compass)" stroke-width="8"/>
    <g class="cam-viewcube-compass-marks" pointer-events="none">${ticks}${labels}</g>
    <ellipse cx="${cx}" cy="${fixed(cy)}" rx="${radius}" ry="${fixed(radius*flatten)}" fill="none" stroke="transparent" stroke-width="16" pointer-events="stroke"/>
  </g>`;
}

