const VIEW_CUBE_BUTTONS = Object.freeze([
  { kind: "face", name: "top", label: "TOP", title: "顶视图" },
  { kind: "face", name: "bottom", label: "BOTTOM", title: "底视图" },
  { kind: "face", name: "front", label: "FRONT", title: "前视图" },
  { kind: "face", name: "back", label: "BACK", title: "后视图" },
  { kind: "face", name: "right", label: "RIGHT", title: "右视图" },
  { kind: "face", name: "left", label: "LEFT", title: "左视图" },
  { kind: "edge", name: "top-front", title: "顶前斜视图" },
  { kind: "edge", name: "top-back", title: "顶后斜视图" },
  { kind: "edge", name: "top-right", title: "顶右斜视图" },
  { kind: "edge", name: "top-left", title: "顶左斜视图" },
  { kind: "edge", name: "bottom-front", title: "底前斜视图" },
  { kind: "edge", name: "bottom-back", title: "底后斜视图" },
  { kind: "edge", name: "bottom-right", title: "底右斜视图" },
  { kind: "edge", name: "bottom-left", title: "底左斜视图" },
  { kind: "edge", name: "front-right", title: "前右斜视图" },
  { kind: "edge", name: "front-left", title: "前左斜视图" },
  { kind: "edge", name: "back-right", title: "后右斜视图" },
  { kind: "edge", name: "back-left", title: "后左斜视图" },
  { kind: "corner", name: "top-front-right", title: "顶前右等轴测" },
  { kind: "corner", name: "top-front-left", title: "顶前左等轴测" },
  { kind: "corner", name: "top-back-right", title: "顶后右等轴测" },
  { kind: "corner", name: "top-back-left", title: "顶后左等轴测" },
  { kind: "corner", name: "bottom-front-right", title: "底前右等轴测" },
  { kind: "corner", name: "bottom-front-left", title: "底前左等轴测" },
  { kind: "corner", name: "bottom-back-right", title: "底后右等轴测" },
  { kind: "corner", name: "bottom-back-left", title: "底后左等轴测" },
]);

const VIEW_CUBE_ANIMATION_KEY = Symbol("laser3d.viewCubeAnimation");

export function renderViewCube() {
  return `
    <div class="cam-viewcube" data-cam-viewcube data-no-window-drag aria-label="视角导航">
      <button class="cam-viewcube-home" type="button" data-cam-action="view-standard" data-cam-view="iso" title="等轴测视图">HOME</button>
      <div class="cam-viewcube-stage">
        <div class="cam-viewcube-cube" data-cam-viewcube-cube>
          ${VIEW_CUBE_BUTTONS.map(renderViewCubeButton).join("")}
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

function renderViewCubeButton(button) {
  const label = button.label ?? "";
  return `
    <button
      class="cam-viewcube-${button.kind} cam-viewcube-${button.kind}-${button.name}"
      type="button"
      data-cam-action="view-standard"
      data-cam-view="${button.name}"
      title="${button.title}"
      aria-label="${button.title}"
    >${label}</button>
  `;
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
