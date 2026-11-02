import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';

// WebGL LineBasicMaterial is usually limited to one physical pixel. Render the
// selected box edges as screen-space strips so zoom and DPI cannot hide them.
export function addColliderHighlight(wire,options={}){
 const positions=[],ends=[],corners=[],source=wire.geometry.attributes.position;
 for(let i=0;i<source.count;i+=2){
  const a=[source.getX(i),source.getY(i),source.getZ(i)],b=[source.getX(i+1),source.getY(i+1),source.getZ(i+1)];
  for(const [along,side] of [[0,-1],[1,-1],[1,1],[0,-1],[1,1],[0,1]]){
   positions.push(...a);ends.push(...b);corners.push(along,side);
  }
 }
 const strip=new THREE.BufferGeometry();
 strip.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));
 strip.setAttribute('end',new THREE.Float32BufferAttribute(ends,3));
 strip.setAttribute('corner',new THREE.Float32BufferAttribute(corners,2));
 const order=options.renderOrder??900,lineColor=options.color??0x55ffe0;
 const root=new THREE.Group();root.name=options.name??'Selected collider';root.visible=false;wire.add(root);
 const fill=new THREE.Mesh(new THREE.BoxGeometry(1,1,1),new THREE.MeshBasicMaterial({color:options.fillColor??lineColor,transparent:true,opacity:options.fillOpacity??.16,depthTest:false,depthWrite:false,toneMapped:false,side:THREE.DoubleSide}));
 fill.renderOrder=order+1;root.add(fill);
 for(const [width,color,renderOrder] of [[options.borderWidth??5,options.borderColor??0x102a32,order+2],[options.lineWidth??3,lineColor,order+3]]){
  const material=new THREE.ShaderMaterial({
   uniforms:{resolution:{value:new THREE.Vector2(1,1)},width:{value:width},nearPlane:{value:.001},color:{value:new THREE.Color(color)}},
   vertexShader:`
    attribute vec3 end;
    attribute vec2 corner;
    uniform vec2 resolution;
    uniform float width;
    uniform float nearPlane;
    void main(){
     vec4 a=modelViewMatrix*vec4(position,1.0),b=modelViewMatrix*vec4(end,1.0);
     float plane=-nearPlane;
     if(a.z>plane&&b.z>plane){gl_Position=vec4(2.0,2.0,2.0,1.0);return;}
     if(a.z>plane)a=mix(a,b,(plane-a.z)/(b.z-a.z));
     if(b.z>plane)b=mix(b,a,(plane-b.z)/(a.z-b.z));
     a=projectionMatrix*a;b=projectionMatrix*b;
     vec2 delta=(b.xy/b.w-a.xy/a.w)*resolution;
     float lengthPx=length(delta);
     vec2 normal=lengthPx>0.0001?vec2(-delta.y,delta.x)/lengthPx:vec2(1.0,0.0);
     vec4 p=mix(a,b,corner.x);
     p.xy+=normal*corner.y*width/resolution*p.w;
     gl_Position=p;
    }`,
   fragmentShader:`
    uniform vec3 color;
    void main(){
     gl_FragColor=vec4(color,1.0);
     #include <colorspace_fragment>
    }`,
   transparent:true,depthTest:false,depthWrite:false,toneMapped:false,side:THREE.DoubleSide,
  });
  // Separate geometry ownership makes normal overlay disposal straightforward.
  const mesh=new THREE.Mesh(strip.clone(),material);mesh.frustumCulled=false;mesh.renderOrder=renderOrder;
  mesh.onBeforeRender=(renderer,scene,camera)=>{renderer.getSize(material.uniforms.resolution.value);material.uniforms.nearPlane.value=camera.near;};
  root.add(mesh);
 }
 strip.dispose();
 const handles=[];for(let i=0;i<6;i++)handles.push(...new THREE.Vector3().setComponent(Math.floor(i/2),i%2?.5:-.5).toArray());
 const points=new THREE.Points(new THREE.BufferGeometry().setAttribute('position',new THREE.Float32BufferAttribute(handles,3)),new THREE.PointsMaterial({color:options.handleColor??0xb9fff1,size:11,sizeAttenuation:false,depthTest:false,depthWrite:false,toneMapped:false,transparent:true}));
 points.name='Collider face handles';points.renderOrder=order+4;points.visible=false;root.add(points);
 root.traverse(o=>{o.userData.independentInteractionVisual=true;o.raycast=()=>{};});
 return root;
}
