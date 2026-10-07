import { profileSvgGeometry } from './profileSvg.mjs';

/** Exact 2D contour display shared with the profile library; no 3D resources. */
export function createNestingDxfPreview() {
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(ns, 'svg');
  svg.dataset.nestingDxfPreview = '';
  svg.setAttribute('role', 'img');
  svg.setAttribute('aria-label', 'DXF 截面预览');
  const group = document.createElementNS(ns, 'g');
  group.setAttribute('transform', 'scale(1,-1)');
  svg.append(group);
  let bounds = [0, 0, 1, 1], box = [...bounds], drag = null, ready = false;
  const update = () => svg.setAttribute('viewBox', box.join(' '));
  const fitView = () => { box = [...bounds]; update(); };
  const point = event => {
    const matrix = svg.getScreenCTM();
    return matrix ? new DOMPoint(event.clientX, event.clientY).matrixTransform(matrix.inverse()) : new DOMPoint();
  };
  function setData(profile) {
    const geometry = profileSvgGeometry(profile);
    if (!geometry.bounds || !geometry.markup) throw Error('DXF 未返回有效的截面轮廓。');
    const { minX, minY, maxX, maxY } = geometry.bounds;
    const width = Math.max(maxX - minX, 1e-6), height = Math.max(maxY - minY, 1e-6);
    const padding = Math.max(width, height) * .12;
    bounds = [minX - padding, -maxY - padding, width + padding * 2, height + padding * 2];
    group.innerHTML = geometry.markup;
    ready = true; fitView(); svg.hidden = false;
  }
  function wheel(event) {
    if (!ready) return;
    event.preventDefault();
    const p = point(event), factor = Math.exp(Math.max(-200, Math.min(200, event.deltaY)) * .002);
    const ratio = box[2] * factor / bounds[2];
    if (ratio < .02 || ratio > 20) return;
    box = [p.x + (box[0] - p.x) * factor, p.y + (box[1] - p.y) * factor, box[2] * factor, box[3] * factor];
    update();
  }
  function pointerDown(event) {
    if (!ready || ![1, 2].includes(event.button)) return;
    event.preventDefault(); svg.setPointerCapture(event.pointerId);
    drag = { id: event.pointerId, point: point(event) };
  }
  function pointerMove(event) {
    if (drag?.id !== event.pointerId) return;
    const p = point(event);
    box[0] += drag.point.x - p.x; box[1] += drag.point.y - p.y; update();
  }
  function pointerEnd(event) { if (drag?.id === event.pointerId) drag = null; }
  function doubleClick(event) { if (event.button === 0 && event.target === svg) fitView(); }
  svg.addEventListener('wheel', wheel, { passive: false });
  svg.addEventListener('pointerdown', pointerDown);
  svg.addEventListener('pointermove', pointerMove);
  svg.addEventListener('pointerup', pointerEnd);
  svg.addEventListener('pointercancel', pointerEnd);
  svg.addEventListener('lostpointercapture', pointerEnd);
  svg.addEventListener('dblclick', doubleClick);
  svg.addEventListener('contextmenu', event => event.preventDefault());
  return {
    mount(container) { container.append(svg); }, setData, fitView,
    clear() { ready = false; group.replaceChildren(); svg.hidden = true; },
    getDebugState() { return { representation: 'svg-dxf-profile', viewBox: [...box], bounds: [...bounds], ready }; },
    dispose() { svg.remove(); ready = false; drag = null; },
  };
}
