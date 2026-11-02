import * as THREE from '../ThirdParty/three/three.module.js';

const axes = ['X', 'Y', 'Z'];
const along = (geometry, axis) => {
  if (axis === 'X') geometry.rotateZ(-Math.PI / 2);
  if (axis === 'Z') geometry.rotateX(Math.PI / 2);
  return geometry;
};
const replaceGeometry = (handle, geometry) => { handle.geometry.dispose(); handle.geometry = geometry; };

// Presentation stays outside the vendored gesture solver. Visible handles and
// their larger, invisible pick regions share the same positive-axis layout.
export function styleTransformGizmo(control) {
  const { gizmo, picker } = control._gizmo;
  for (const mode of ['translate', 'scale']) {
    for (const group of [gizmo[mode], picker[mode]]) for (const handle of [...group.children]) {
      const axis = handle.name;
      if (axes.includes(axis)) {
        handle.geometry.computeBoundingBox();
        const box = handle.geometry.boundingBox, key = axis.toLowerCase();
        if (box.getCenter(new THREE.Vector3())[key] < -1e-6) {
          handle.removeFromParent(); handle.geometry.dispose(); continue;
        }
        if (group === picker[mode]) {
          replaceGeometry(handle, along(new THREE.CylinderGeometry(.045, .045, .57, 8).translate(0, .325, 0), axis));
        } else if (box.min[key] < .1) {
          replaceGeometry(handle, along(new THREE.CylinderGeometry(.005, .005, .5, 8).translate(0, .25, 0), axis));
        } else if (mode === 'translate') {
          replaceGeometry(handle, along(new THREE.ConeGeometry(.03, .10, 20).translate(0, .55, 0), axis));
        } else {
          replaceGeometry(handle, along(new THREE.BoxGeometry(.06, .06, .06).translate(0, .53, 0), axis));
        }
      } else if (axis === 'XYZ') {
        replaceGeometry(handle, mode === 'scale'
          ? new THREE.BoxGeometry(...Array(3).fill(group === picker[mode] ? .13 : .065))
          : new THREE.OctahedronGeometry(group === picker[mode] ? .09 : .045));
      } else if (mode === 'scale') {
        handle.geometry.translate(...axes.map(a => axis.includes(a) ? .13 : 0));
      }
    }
    for (const handle of [...gizmo[mode].children]) {
      if (!['XY', 'XZ', 'YZ'].includes(handle.name)) continue;
      const border = new THREE.LineSegments(new THREE.EdgesGeometry(handle.geometry), new THREE.LineBasicMaterial({
        color: handle.material.color, opacity: .85, transparent: true, depthTest: false, depthWrite: false, toneMapped: false,
      }));
      border.name = handle.name; border.renderOrder = Infinity; gizmo[mode].add(border);
    }
  }
  for (const group of [gizmo.rotate, picker.rotate]) for (const handle of group.children) {
    if (![...axes,'E'].includes(handle.name)) continue;
    const outer = handle.name === 'E';
    const geometry = new THREE.TorusGeometry(outer ? .56 : .5, group === picker.rotate ? .025 : outer ? .0045 : .007, 8, 128);
    if (handle.name === 'X') geometry.rotateY(Math.PI / 2);
    if (handle.name === 'Y') geometry.rotateX(Math.PI / 2);
    replaceGeometry(handle, geometry);
    if (group === gizmo.rotate) {
      handle.material = handle.material.clone();
      if (outer) handle.material.color.setHex(0xc6d2d8);
      handle.material.opacity = 1;
      handle.userData.restColor = handle.material.color.clone();
      if (!outer) {
        const eye = { value:new THREE.Vector3() };
        handle.userData.ringEye = eye;
        handle.material.onBeforeCompile = shader => {
          shader.uniforms.ringEye = eye;
          shader.vertexShader = 'varying vec3 ringPosition;\n' + shader.vertexShader.replace('#include <begin_vertex>', '#include <begin_vertex>\nringPosition = position;');
          shader.fragmentShader = 'uniform vec3 ringEye;\nvarying vec3 ringPosition;\n' + shader.fragmentShader.replace('#include <color_fragment>', '#include <color_fragment>\ndiffuseColor.a *= length(ringEye) < 0.1 ? 1.0 : mix(0.12, 1.0, smoothstep(-0.01, 0.01, dot(ringPosition, ringEye)));');
        };
        handle.material.customProgramCacheKey = () => 'icax-rotation-front-back-v1';
      }
    }
  }
}

export function updateRotationGizmoAppearance(control, activeAxis) {
  const selected = control.axis || activeAxis;
  for (const handle of control._gizmo.gizmo.rotate.children) {
    if (!handle.userData.restColor) continue;
    handle.material.color.copy(selected === handle.name ? control._gizmo.materialLib.active.color : handle.userData.restColor);
    handle.material.opacity = selected && selected !== handle.name ? .6 : 1;
    // TransformControls updates again during the renderer's scene traversal.
    // Keep its restoration colors in sync with the product's retained selection.
    (handle.material._color ??= new THREE.Color()).copy(handle.material.color);
    handle.material._opacity = handle.material.opacity;
    if (handle.userData.ringEye) {
      const eye = handle.userData.ringEye.value.copy(control.eye).applyQuaternion(handle.quaternion.clone().invert());
      // Face-on rings lie on the silhouette; keep the whole circle visible.
      const normal = new THREE.Vector3(...({X:[1,0,0],Y:[0,1,0],Z:[0,0,1]}[handle.name]));
      if (Math.abs(eye.dot(normal)) > .999) eye.set(0,0,0);
    }
  }
}
