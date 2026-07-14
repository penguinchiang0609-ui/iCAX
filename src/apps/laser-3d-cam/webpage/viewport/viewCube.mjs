const VIEW_CUBE_FACES = Object.freeze([
  makeFace("top", "TOP", [
    ["top-back-left", "top-back", "top-back-right"],
    ["top-left", "top", "top-right"],
    ["top-front-left", "top-front", "top-front-right"],
  ]),
  makeFace("bottom", "BOTTOM", [
    ["bottom-front-left", "bottom-front", "bottom-front-right"],
    ["bottom-left", "bottom", "bottom-right"],
    ["bottom-back-left", "bottom-back", "bottom-back-right"],
  ]),
  makeFace("front", "FRONT", [
    ["top-front-left", "top-front", "top-front-right"],
    ["front-left", "front", "front-right"],
    ["bottom-front-left", "bottom-front", "bottom-front-right"],
  ]),
  makeFace("back", "BACK", [
    ["top-back-right", "top-back", "top-back-left"],
    ["back-right", "back", "back-left"],
    ["bottom-back-right", "bottom-back", "bottom-back-left"],
  ]),
  makeFace("right", "RIGHT", [
    ["top-front-right", "top-right", "top-back-right"],
    ["front-right", "right", "back-right"],
    ["bottom-front-right", "bottom-right", "bottom-back-right"],
  ]),
  makeFace("left", "LEFT", [
    ["top-back-left", "top-left", "top-front-left"],
    ["back-left", "left", "front-left"],
    ["bottom-back-left", "bottom-left", "bottom-front-left"],
  ]),
]);

const VIEW_CUBE_ANIMATION_KEY = Symbol("laser3d.viewCubeAnimation");

export function renderViewCube() {
  return `
    <div class="cam-viewcube" data-cam-viewcube data-no-window-drag aria-label="视角导航">
      <div class="cam-viewcube-stage">
        <div class="cam-viewcube-cube" data-cam-viewcube-cube>
          ${VIEW_CUBE_FACES.map(renderViewCubeFace).join("")}
        </div>
      </div>
    </div>
  `;
}

export function attachViewCube(view, mount) {
  const cube = mount?.querySelector?.("[data-cam-viewcube-cube]");
  const host = mount?.querySelector?.("[data-cam-viewcube]");
  if (!cube || !host || !view?.viewport) {
    stopViewCubeAnimation(view);
    return;
  }

  const previous = view[VIEW_CUBE_ANIMATION_KEY];
  if (previous?.cube === cube) {
    return;
  }
  stopViewCubeAnimation(view);

  const animation = {
    cube,
    frameId: 0,
    lastSignature: "",
    tick: null,
  };
  animation.tick = () => {
    animation.frameId = 0;
    updateViewCubeOrientation(view, animation);
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
  view[VIEW_CUBE_ANIMATION_KEY] = null;
}

function makeFace(name, label, rows) {
  return Object.freeze({ name, label, rows });
}

function renderViewCubeFace(face) {
  return `
    <div class="cam-viewcube-face cam-viewcube-face-${face.name}" aria-label="${viewTitle(face.name)}">
      ${face.rows.map((row, rowIndex) => row.map((viewName, columnIndex) =>
        renderViewCubeCell(face, viewName, rowIndex, columnIndex)).join("")).join("")}
    </div>
  `;
}

function renderViewCubeCell(face, viewName, rowIndex, columnIndex) {
  const isCenter = rowIndex === 1 && columnIndex === 1;
  return `
    <button
      class="cam-viewcube-cell cam-viewcube-cell-${rowIndex}-${columnIndex}${isCenter ? " cam-viewcube-cell-center" : ""}"
      type="button"
      data-cam-action="view-standard"
      data-cam-view="${viewName}"
      title="${viewTitle(viewName)}"
      aria-label="${viewTitle(viewName)}"
    >${isCenter ? face.label : ""}</button>
  `;
}

function viewTitle(viewName) {
  return `${viewName.replaceAll("-", " ")} 视图`;
}

function updateViewCubeOrientation(view, animation) {
  const state = view.viewport?.getViewCubeState?.();
  if (!state?.direction) {
    return;
  }

  const rotation = makeViewCubeRotation(state.direction);
  const signature = `${rotation.pitch.toFixed(1)}:${rotation.yaw.toFixed(1)}`;
  if (signature === animation.lastSignature) {
    return;
  }
  animation.lastSignature = signature;
  animation.cube.style.setProperty("--viewcube-pitch", `${rotation.pitch.toFixed(1)}deg`);
  animation.cube.style.setProperty("--viewcube-yaw", `${rotation.yaw.toFixed(1)}deg`);
}

function makeViewCubeRotation(direction) {
  const eye = normalizeVector({
    x: -Number(direction.x ?? 0),
    y: -Number(direction.y ?? 0),
    z: -Number(direction.z ?? 0),
  });
  const horizontal = Math.max(0.000001, Math.hypot(eye.x, eye.y));
  return {
    pitch: clamp(-Math.atan2(eye.z, horizontal) * 180 / Math.PI, -82, 82),
    yaw: Math.atan2(eye.x, -eye.y) * 180 / Math.PI,
  };
}

function normalizeVector(value) {
  const length = Math.hypot(value.x, value.y, value.z);
  if (length <= Number.EPSILON) {
    return { x: 0.55, y: -0.68, z: 0.46 };
  }
  return {
    x: value.x / length,
    y: value.y / length,
    z: value.z / length,
  };
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}
