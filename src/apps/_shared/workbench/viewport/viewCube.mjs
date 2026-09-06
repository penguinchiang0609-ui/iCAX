const VIEW_CUBE_ANIMATION_KEY = Symbol("laser3d.viewCubeAnimation");
const VIEW_CUBE_SIZE = 116;
const VIEW_CUBE_PIECES = Object.freeze(createViewCubePieces());
const AXES = Object.freeze([
  Object.freeze({ axis: "x", sign: 1, name: "right", normal: vector(1, 0, 0) }),
  Object.freeze({ axis: "x", sign: -1, name: "left", normal: vector(-1, 0, 0) }),
  Object.freeze({ axis: "y", sign: -1, name: "front", normal: vector(0, -1, 0) }),
  Object.freeze({ axis: "y", sign: 1, name: "back", normal: vector(0, 1, 0) }),
  Object.freeze({ axis: "z", sign: 1, name: "top", normal: vector(0, 0, 1) }),
  Object.freeze({ axis: "z", sign: -1, name: "bottom", normal: vector(0, 0, -1) }),
]);
const FACE_LABELS = Object.freeze({
  top: "TOP",
  bottom: "BOTTOM",
  front: "FRONT",
  back: "BACK",
  right: "RIGHT",
  left: "LEFT",
});
const BASE_COLORS = Object.freeze({
  center: "#efae1e",
  edge: "#d59514",
  corner: "#c47f0c",
});

export function renderViewCube() {
  return `
    <div
      class="cam-viewcube"
      data-cam-viewcube
      data-cam-viewcube-cube
      data-no-window-drag
      data-cam-viewcube-piece-count="${VIEW_CUBE_PIECES.length}"
      aria-label="视角导航">
    </div>
  `;
}

export function attachViewCube(view, mount) {
  const host = mount?.querySelector?.("[data-cam-viewcube]");
  if (!host || !view?.viewport) {
    stopViewCubeAnimation(view);
    return;
  }

  const previous = view[VIEW_CUBE_ANIMATION_KEY];
  if (previous?.host === host) {
    return;
  }
  stopViewCubeAnimation(view);

  const animation = {
    host,
    frameId: 0,
    lastSignature: "",
    tick: null,
  };
  animation.tick = () => {
    animation.frameId = 0;
    updateViewCube(view, animation);
    animation.frameId = window.requestAnimationFrame(animation.tick);
  };
  view[VIEW_CUBE_ANIMATION_KEY] = animation;
  animation.tick();
}

export function stopViewCubeAnimation(view) {
  const animation = view?.[VIEW_CUBE_ANIMATION_KEY];
  if (!animation) {
    return;
  }
  if (animation.frameId) {
    window.cancelAnimationFrame(animation.frameId);
  }
  animation.host.replaceChildren();
  view[VIEW_CUBE_ANIMATION_KEY] = null;
}

function updateViewCube(view, animation) {
  const state = view.viewport?.getViewCubeState?.();
  if (!state?.direction) {
    return;
  }

  const basis = makeProjectionBasis(state);
  const rotation = makeViewCubeRotation(state.direction);
  const signature = [
    rotation.pitch.toFixed(1),
    rotation.yaw.toFixed(1),
    basis.right.x.toFixed(3),
    basis.right.y.toFixed(3),
    basis.right.z.toFixed(3),
    basis.up.x.toFixed(3),
    basis.up.y.toFixed(3),
    basis.up.z.toFixed(3),
  ].join(":");
  if (signature === animation.lastSignature) {
    return;
  }

  animation.lastSignature = signature;
  animation.host.style.setProperty("--viewcube-pitch", `${rotation.pitch.toFixed(1)}deg`);
  animation.host.style.setProperty("--viewcube-yaw", `${rotation.yaw.toFixed(1)}deg`);
  animation.host.dataset.camViewCubePieceCount = String(VIEW_CUBE_PIECES.length);
  animation.host.innerHTML = renderViewCubeSvg(basis);
}

function renderViewCubeSvg(basis) {
  const visibleFaces = collectVisibleFaces(basis);
  const bounds = computeProjectionBounds(visibleFaces);
  const scale = Math.min(
    86 / Math.max(0.0001, bounds.maxX - bounds.minX),
    82 / Math.max(0.0001, bounds.maxY - bounds.minY),
  );
  const offsetX = VIEW_CUBE_SIZE * 0.5 - (bounds.minX + bounds.maxX) * 0.5 * scale;
  const offsetY = VIEW_CUBE_SIZE * 0.5 - (bounds.minY + bounds.maxY) * 0.5 * scale;

  const pieces = groupVisibleFacesByPiece(visibleFaces)
    .sort((left, right) => left.depth - right.depth)
    .map((piece) => renderPiece(piece, scale, offsetX, offsetY))
    .join("");

  return `
    <svg
      class="cam-viewcube-svg"
      viewBox="0 0 ${VIEW_CUBE_SIZE} ${VIEW_CUBE_SIZE}"
      width="${VIEW_CUBE_SIZE}"
      height="${VIEW_CUBE_SIZE}"
      role="img"
      aria-label="视角导航">
      <defs>
        <filter id="cam-viewcube-shadow" x="-20%" y="-20%" width="140%" height="140%">
          <feDropShadow dx="0" dy="2" stdDeviation="1.5" flood-color="#000000" flood-opacity="0.28"/>
        </filter>
      </defs>
      <g filter="url(#cam-viewcube-shadow)">
        ${pieces}
      </g>
    </svg>
  `;
}

function collectVisibleFaces(basis) {
  const faces = [];
  for (const piece of VIEW_CUBE_PIECES) {
    const min = vector(piece.x - 0.5, piece.y - 0.5, piece.z - 0.5);
    const max = vector(piece.x + 0.5, piece.y + 0.5, piece.z + 0.5);
    for (const axis of AXES) {
      if (!isOuterFace(piece, axis) || dot(axis.normal, basis.eye) <= 0.0001) {
        continue;
      }
      const corners = faceCorners(min, max, axis).map((point) => project(point, basis));
      const center = vector(
        (min.x + max.x) * 0.5,
        (min.y + max.y) * 0.5,
        (min.z + max.z) * 0.5,
      );
      faces.push(Object.freeze({
        piece,
        axis,
        corners,
        center: project(add(center, scaleVector(axis.normal, 0.01)), basis),
        depth: dot(center, basis.eye),
        shade: faceShade(axis.normal, basis.eye),
      }));
    }
  }
  return faces;
}

function groupVisibleFacesByPiece(faces) {
  const groups = new Map();
  for (const face of faces) {
    let group = groups.get(face.piece.name);
    if (!group) {
      group = {
        piece: face.piece,
        faces: [],
        depth: -Infinity,
      };
      groups.set(face.piece.name, group);
    }
    group.faces.push(face);
    group.depth = Math.max(group.depth, face.depth);
  }
  return [...groups.values()];
}

function renderPiece(group, scale, offsetX, offsetY) {
  const faceMarkup = group.faces
    .sort((left, right) => left.depth - right.depth)
    .map((face) => renderFaceSurface(group.piece, face, scale, offsetX, offsetY))
    .join("");
  const labelMarkup = renderPieceLabel(group, scale, offsetX, offsetY);
  return `
    <g
      class="cam-viewcube-piece cam-viewcube-piece-${group.piece.kind}"
      data-cam-action="view-standard"
      data-cam-view="${escapeAttr(group.piece.name)}"
      aria-label="${escapeAttr(viewTitle(group.piece.name))}">
      ${faceMarkup}
      ${labelMarkup}
    </g>
  `;
}

function renderFaceSurface(piece, face, scale, offsetX, offsetY) {
  const points = face.corners
    .map((point) => toSvgPoint(point, scale, offsetX, offsetY))
    .map((point) => `${formatNumber(point.x)},${formatNumber(point.y)}`)
    .join(" ");
  return `
    <polygon
      class="cam-viewcube-surface"
      points="${points}"
      fill="${shadeColor(BASE_COLORS[piece.kind], face.shade)}" />
  `;
}

function renderPieceLabel(group, scale, offsetX, offsetY) {
  if (group.piece.kind !== "center") {
    return "";
  }
  const face = group.faces.find((item) => item.axis.name === group.piece.name);
  const label = FACE_LABELS[group.piece.name];
  if (!face || !label) {
    return "";
  }
  const point = toSvgPoint(face.center, scale, offsetX, offsetY);
  return `
    <text
      class="cam-viewcube-label"
      x="${formatNumber(point.x)}"
      y="${formatNumber(point.y)}">${escapeText(label)}</text>
  `;
}

function computeProjectionBounds(faces) {
  const bounds = { minX: Infinity, maxX: -Infinity, minY: Infinity, maxY: -Infinity };
  for (const face of faces) {
    for (const point of face.corners) {
      bounds.minX = Math.min(bounds.minX, point.x);
      bounds.maxX = Math.max(bounds.maxX, point.x);
      bounds.minY = Math.min(bounds.minY, point.y);
      bounds.maxY = Math.max(bounds.maxY, point.y);
    }
  }
  if (!Number.isFinite(bounds.minX)) {
    return { minX: -1, maxX: 1, minY: -1, maxY: 1 };
  }
  return bounds;
}

function makeProjectionBasis(state) {
  const eye = normalize(vector(
    -Number(state.direction?.x ?? 0),
    -Number(state.direction?.y ?? 0),
    -Number(state.direction?.z ?? 0),
  ), vector(0.55, -0.68, 0.46));
  const requestedUp = normalize(vector(
    Number(state.up?.x ?? 0),
    Number(state.up?.y ?? 0),
    Number(state.up?.z ?? 0),
  ), vector(0, 0, 1));
  let right = normalize(cross(requestedUp, eye), vector(1, 0, 0));
  let up = normalize(cross(eye, right), vector(0, 0, 1));
  if (length(right) <= 0.0001 || length(up) <= 0.0001) {
    right = vector(1, 0, 0);
    up = vector(0, 0, 1);
  }
  return { eye, right, up };
}

function project(point, basis) {
  return {
    x: dot(point, basis.right),
    y: -dot(point, basis.up),
  };
}

function toSvgPoint(point, scale, offsetX, offsetY) {
  return {
    x: offsetX + point.x * scale,
    y: offsetY + point.y * scale,
  };
}

function faceCorners(min, max, axis) {
  if (axis.axis === "x") {
    const x = axis.sign > 0 ? max.x : min.x;
    return axis.sign > 0
      ? [vector(x, min.y, min.z), vector(x, max.y, min.z), vector(x, max.y, max.z), vector(x, min.y, max.z)]
      : [vector(x, max.y, min.z), vector(x, min.y, min.z), vector(x, min.y, max.z), vector(x, max.y, max.z)];
  }
  if (axis.axis === "y") {
    const y = axis.sign > 0 ? max.y : min.y;
    return axis.sign > 0
      ? [vector(min.x, y, min.z), vector(min.x, y, max.z), vector(max.x, y, max.z), vector(max.x, y, min.z)]
      : [vector(min.x, y, min.z), vector(max.x, y, min.z), vector(max.x, y, max.z), vector(min.x, y, max.z)];
  }
  const z = axis.sign > 0 ? max.z : min.z;
  return axis.sign > 0
    ? [vector(min.x, min.y, z), vector(max.x, min.y, z), vector(max.x, max.y, z), vector(min.x, max.y, z)]
    : [vector(min.x, max.y, z), vector(max.x, max.y, z), vector(max.x, min.y, z), vector(min.x, min.y, z)];
}

function isOuterFace(piece, axis) {
  if (axis.axis === "x") {
    return piece.x === axis.sign;
  }
  if (axis.axis === "y") {
    return piece.y === axis.sign;
  }
  return piece.z === axis.sign;
}

function createViewCubePieces() {
  const pieces = [];
  for (const x of [-1, 0, 1]) {
    for (const y of [-1, 0, 1]) {
      for (const z of [-1, 0, 1]) {
        const nonZero = [x, y, z].filter((value) => value !== 0).length;
        if (nonZero === 0) {
          continue;
        }
        pieces.push(Object.freeze({
          kind: nonZero === 1 ? "center" : nonZero === 2 ? "edge" : "corner",
          name: makePieceName(x, y, z),
          x,
          y,
          z,
        }));
      }
    }
  }
  return pieces;
}

function makePieceName(x, y, z) {
  const parts = [];
  if (z > 0) {
    parts.push("top");
  } else if (z < 0) {
    parts.push("bottom");
  }
  if (y < 0) {
    parts.push("front");
  } else if (y > 0) {
    parts.push("back");
  }
  if (x > 0) {
    parts.push("right");
  } else if (x < 0) {
    parts.push("left");
  }
  return parts.join("-");
}

function faceShade(normal, eye) {
  return 0.78 + Math.max(0, dot(normal, eye)) * 0.22;
}

function shadeColor(hexColor, factor) {
  const value = Number.parseInt(hexColor.slice(1), 16);
  const r = clampByte(((value >> 16) & 255) * factor);
  const g = clampByte(((value >> 8) & 255) * factor);
  const b = clampByte((value & 255) * factor);
  return `rgb(${r}, ${g}, ${b})`;
}

function clampByte(value) {
  return Math.max(0, Math.min(255, Math.round(value)));
}

function vector(x, y, z) {
  return { x, y, z };
}

function add(left, right) {
  return vector(left.x + right.x, left.y + right.y, left.z + right.z);
}

function scaleVector(value, scalar) {
  return vector(value.x * scalar, value.y * scalar, value.z * scalar);
}

function dot(left, right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

function cross(left, right) {
  return vector(
    left.y * right.z - left.z * right.y,
    left.z * right.x - left.x * right.z,
    left.x * right.y - left.y * right.x,
  );
}

function length(value) {
  return Math.hypot(value.x, value.y, value.z);
}

function normalize(value, fallback) {
  const size = length(value);
  if (size <= 0.000001) {
    return fallback;
  }
  return vector(value.x / size, value.y / size, value.z / size);
}

function viewTitle(viewName) {
  return `${viewName.replaceAll("-", " ")} 视图`;
}

function makeViewCubeRotation(direction) {
  const eye = normalize(vector(
    -Number(direction.x ?? 0),
    -Number(direction.y ?? 0),
    -Number(direction.z ?? 0),
  ), vector(0.55, -0.68, 0.46));
  const horizontal = Math.max(0.000001, Math.hypot(eye.x, eye.y));
  return {
    pitch: clamp(-Math.atan2(eye.z, horizontal) * 180 / Math.PI, -82, 82),
    yaw: Math.atan2(eye.x, -eye.y) * 180 / Math.PI,
  };
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function formatNumber(value) {
  return Number(value).toFixed(2);
}

function escapeText(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;");
}

function escapeAttr(value) {
  return escapeText(value)
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}
