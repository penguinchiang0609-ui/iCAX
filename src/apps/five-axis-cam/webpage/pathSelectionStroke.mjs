import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { sceneColors } from './scenePalette.mjs';

// WebGL line widths are limited to one pixel on Windows. Draw the selected
// polyline as screen-space quads so it stays legible while zooming or orbiting.
export function syncPathSelectionStroke(line, selected, color=sceneColors.selected) {
  const previous=line.children.find(child=>child.userData.pathSelectionStroke);
  if(!selected){
    if(previous){previous.removeFromParent();previous.geometry.dispose();previous.material.dispose();}
    return;
  }
  if(previous){previous.material.uniforms.color.value.set(color);return;}
  const positions=line.geometry.getAttribute('position');
  if(!positions||positions.count<2)return;
  const starts=[],ends=[],distances=[],lineDistances=line.geometry.getAttribute('lineDistance');
  for(let i=1;i<positions.count;i++){
    starts.push(positions.getX(i-1),positions.getY(i-1),positions.getZ(i-1));
    ends.push(positions.getX(i),positions.getY(i),positions.getZ(i));
    distances.push(lineDistances?.getX(i-1)||0,lineDistances?.getX(i)||0);
  }
  const geometry=new THREE.InstancedBufferGeometry();
  geometry.setAttribute('position',new THREE.Float32BufferAttribute([0,-1,0,1,-1,0,0,1,0,0,1,0,1,-1,0,1,1,0],3));
  geometry.setAttribute('segmentStart',new THREE.InstancedBufferAttribute(new Float32Array(starts),3));
  geometry.setAttribute('segmentEnd',new THREE.InstancedBufferAttribute(new Float32Array(ends),3));
  geometry.setAttribute('segmentDistance',new THREE.InstancedBufferAttribute(new Float32Array(distances),2));
  geometry.instanceCount=positions.count-1;
  const dashed=line.material.isLineDashedMaterial;
  const material=new THREE.ShaderMaterial({
    uniforms:{color:{value:new THREE.Color(color)},resolution:{value:new THREE.Vector2(1,1)},
      dashSize:{value:dashed?line.material.dashSize:0},gapSize:{value:dashed?line.material.gapSize:0},distanceScale:{value:line.material.scale??1}},
    vertexShader:`
      attribute vec3 segmentStart;
      attribute vec3 segmentEnd;
      attribute vec2 segmentDistance;
      uniform vec2 resolution;
      uniform float distanceScale;
      varying float pathDistance;
      varying float shown;
      void main(){
        vec4 a=projectionMatrix*modelViewMatrix*vec4(segmentStart,1.0);
        vec4 b=projectionMatrix*modelViewMatrix*vec4(segmentEnd,1.0);
        vec2 delta=(b.xy/b.w-a.xy/a.w)*resolution;
        float size=length(delta);
        shown=(size>0.001&&a.w>0.0&&b.w>0.0)?1.0:0.0;
        vec2 along=delta/max(size,0.001);
        gl_Position=mix(a,b,position.x);
        gl_Position.xy+=vec2(-along.y,along.x)*position.y*4.0/resolution*gl_Position.w;
        pathDistance=mix(segmentDistance.x,segmentDistance.y,position.x)*distanceScale;
      }`,
    fragmentShader:`
      uniform vec3 color;
      uniform float dashSize;
      uniform float gapSize;
      varying float pathDistance;
      varying float shown;
      void main(){
        if(shown<0.5)discard;
        if(dashSize>0.0&&mod(pathDistance,dashSize+gapSize)>dashSize)discard;
        gl_FragColor=vec4(color,1.0);
        #include <colorspace_fragment>
      }`,
    depthTest:false,depthWrite:false,side:THREE.DoubleSide,
  });
  const stroke=new THREE.Mesh(geometry,material);
  stroke.name='five-selected-path';stroke.userData.pathSelectionStroke=true;
  stroke.renderOrder=80;stroke.frustumCulled=false;stroke.raycast=()=>{};
  stroke.onBeforeRender=renderer=>renderer.getSize(material.uniforms.resolution.value);
  line.add(stroke);
}
