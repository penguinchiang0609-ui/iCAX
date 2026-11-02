import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { sceneColors } from './scenePalette.mjs';

// A thin 16 x 10 CSS-pixel arrow. Project the model tangent each frame so zoom,
// perspective and workpiece placement never inflate it into a solid 3D cone.
export function cutDirectionMarker(position, direction, id, color=sceneColors.selected) {
  const vertices = [];
  for (const [a, b] of [[[-8,0],[8,0]], [[2,5],[8,0]], [[2,-5],[8,0]]]) {
    const length = Math.hypot(b[0]-a[0], b[1]-a[1]);
    const x = -(b[1]-a[1]) / length * .75, y = (b[0]-a[0]) / length * .75;
    const p = [[a[0]+x,a[1]+y], [a[0]-x,a[1]-y], [b[0]+x,b[1]+y], [b[0]-x,b[1]-y]];
    for (const index of [0,1,2,2,1,3]) vertices.push(...p[index], 0);
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(vertices, 3));
  const material = new THREE.ShaderMaterial({
    uniforms: { tangent: { value: direction.clone() }, resolution: { value: new THREE.Vector2(1,1) }, color: { value: new THREE.Color(color) } },
    vertexShader: `
      uniform vec3 tangent;
      uniform vec2 resolution;
      varying float shown;
      void main() {
        vec4 anchor = projectionMatrix * modelViewMatrix * vec4(0.0,0.0,0.0,1.0);
        vec4 next = projectionMatrix * modelViewMatrix * vec4(tangent,1.0);
        vec2 delta = (next.xy / next.w - anchor.xy / anchor.w) * resolution;
        float size = length(delta);
        shown = (size > 0.001 && anchor.w > 0.0) ? 1.0 : 0.0;
        vec2 along = delta / max(size,0.001);
        vec2 offset = along * position.x + vec2(-along.y,along.x) * position.y;
        gl_Position = anchor;
        gl_Position.xy += offset * 2.0 / resolution * anchor.w;
      }`,
    fragmentShader: `
      uniform vec3 color;
      varying float shown;
      void main() {
        gl_FragColor = vec4(color, shown);
        #include <colorspace_fragment>
      }`,
    transparent: true, depthTest: false, depthWrite: false, side: THREE.DoubleSide,
  });
  const marker = new THREE.Mesh(geometry, material);
  marker.position.copy(position);
  marker.frustumCulled = false;
  marker.renderOrder = 44;
  marker.raycast = () => {};
  marker.userData.cutDirectionId = id;
  marker.userData.direction = direction.toArray();
  marker.onBeforeRender = renderer => renderer.getSize(material.uniforms.resolution.value);
  return marker;
}

export function addCutDirectionMarkers(group, coordinates, id, color, reversed=false) {
  if(!coordinates?.length)return;
  const points=coordinates.map(p=>new THREE.Vector3(...p)),lengths=points.slice(1).map((p,i)=>p.distanceTo(points[i]));
  const total=lengths.reduce((a,b)=>a+b,0);if(total<1e-6)return;
  for(const fraction of [.2,.55,.85]){
    let distance=total*fraction,index=0;
    while(index<lengths.length-1&&distance>lengths[index])distance-=lengths[index++];
    if(lengths[index]<1e-8)continue;
    const position=points[index].clone().lerp(points[index+1],distance/lengths[index]);
    const direction=points[index+1].clone().sub(points[index]).normalize().multiplyScalar(reversed?-1:1);
    group.add(cutDirectionMarker(position,direction,id,color));
  }
}
