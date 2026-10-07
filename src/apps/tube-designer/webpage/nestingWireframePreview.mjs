const SVG_NS = 'http://www.w3.org/2000/svg';

/** SVG projection of coarse CAD edges. No faces, GPU resources or WebGL context. */
export function createNestingWireframePreview() {
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('role', 'img');
  svg.setAttribute('aria-label', '零件线框预览');
  svg.dataset.nestingWireframe = '';
  const path = document.createElementNS(SVG_NS, 'path');
  svg.append(path);
  let lines = [], center = [0, 0, 0], majorAxis = 0, yaw = -Math.PI / 4, pitch = Math.PI / 7;
  let bounds = [0, 0, 1, 1], box = [...bounds], drag = null, frame = null;
  let edgeCount = 0, pointCount = 0;
  const updateView = () => svg.setAttribute('viewBox', box.join(' '));
  const fitView = () => { box = [...bounds]; updateView(); };
  function project() {
    const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
    let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    const commands = [];
    for (const line of lines) {
      for (let i = 0; i < line.length; i += 3) {
        const local = [line[i] - center[0], line[i + 1] - center[1], line[i + 2] - center[2]];
        // Present long parts along the screen's wider direction regardless of
        // the source file's construction axis; these are rigid rotations.
        const [x, y, z] = majorAxis === 2 ? [local[2], local[1], -local[0]]
          : majorAxis === 1 ? [local[1], -local[0], local[2]] : local;
        const px = cy * x - sy * y, py = sp * (sy * x + cy * y) - cp * z;
        minX = Math.min(minX, px); maxX = Math.max(maxX, px);
        minY = Math.min(minY, py); maxY = Math.max(maxY, py);
        commands.push(`${i ? 'L' : 'M'}${px.toFixed(3)} ${py.toFixed(3)}`);
      }
    }
    path.setAttribute('d', commands.join(''));
    if (!lines.length) return;
    const width = Math.max(1e-6, maxX - minX), height = Math.max(1e-6, maxY - minY);
    const padding = Math.max(width, height) * .07;
    bounds = [minX - padding, minY - padding, width + padding * 2, height + padding * 2];
  }
  function setWireframe(wireframe) {
    const data = wireframe?.polylines;
    if (!Array.isArray(data) || !data.length || data.length > 50000) throw Error('文件未返回有效的线框预览。');
    const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
    let count = 0;
    for (const line of data) {
      if (!Array.isArray(line) || line.length < 6 || line.length % 3 || line.length > 1000) throw Error('线框边线无效。');
      count += line.length / 3;
      if (count > 1250000) throw Error('线框预览过大。');
      for (let i = 0; i < line.length; ++i) {
        if (!Number.isFinite(line[i])) throw Error('线框坐标无效。');
        min[i % 3] = Math.min(min[i % 3], line[i]); max[i % 3] = Math.max(max[i % 3], line[i]);
      }
    }
    lines = data; edgeCount = data.length; pointCount = count;
    center = min.map((value, index) => (value + max[index]) / 2);
    const extents = min.map((value, index) => max[index] - value);
    majorAxis = extents.indexOf(Math.max(...extents));
    yaw = -Math.PI / 4; pitch = Math.PI / 7;
    project(); fitView(); svg.hidden = false;
  }
  const pointInView = event => {
    const matrix = svg.getScreenCTM();
    return matrix ? new DOMPoint(event.clientX, event.clientY).matrixTransform(matrix.inverse()) : new DOMPoint();
  };
  function wheel(event) {
    if (!lines.length) return;
    event.preventDefault();
    const p = pointInView(event), factor = Math.exp(Math.max(-200, Math.min(200, event.deltaY)) * .002);
    const ratio = box[2] * factor / bounds[2];
    if (ratio < .02 || ratio > 20) return;
    box = [p.x + (box[0] - p.x) * factor, p.y + (box[1] - p.y) * factor, box[2] * factor, box[3] * factor];
    updateView();
  }
  function pointerDown(event) {
    if (!lines.length || ![1, 2].includes(event.button)) return;
    event.preventDefault();
    svg.setPointerCapture(event.pointerId);
    drag = { id: event.pointerId, button: event.button, x: event.clientX, y: event.clientY, yaw, pitch,
      point: pointInView(event), box: [...box] };
  }
  function pointerMove(event) {
    if (!drag || event.pointerId !== drag.id) return;
    if (drag.button === 1) {
      const p = pointInView(event);
      box[0] += drag.point.x - p.x; box[1] += drag.point.y - p.y;
      updateView();
    } else {
      yaw = drag.yaw + (event.clientX - drag.x) * .008;
      pitch = drag.pitch + (event.clientY - drag.y) * .008;
      if (frame == null) frame = requestAnimationFrame(() => { frame = null; project(); });
    }
  }
  function pointerEnd(event) { if (drag?.id === event.pointerId) drag = null; }
  function doubleClick(event) { if (event.button === 0 && event.target === svg) fitView(); }
  const contextMenu = event => event.preventDefault();
  svg.addEventListener('wheel', wheel, { passive: false });
  svg.addEventListener('pointerdown', pointerDown);
  svg.addEventListener('pointermove', pointerMove);
  svg.addEventListener('pointerup', pointerEnd);
  svg.addEventListener('pointercancel', pointerEnd);
  svg.addEventListener('lostpointercapture', pointerEnd);
  svg.addEventListener('dblclick', doubleClick);
  svg.addEventListener('contextmenu', contextMenu);
  return {
    mount(container) { container.append(svg); }, setWireframe, fitView,
    clear() { lines = []; edgeCount = pointCount = 0; path.removeAttribute('d'); svg.hidden = true; },
    getDebugState() { return { representation: 'svg-wireframe', edgeCount, pointCount, viewBox: [...box], bounds: [...bounds], yaw, pitch }; },
    dispose() {
      if (frame != null) cancelAnimationFrame(frame);
      svg.remove(); lines = []; drag = null;
    },
  };
}
