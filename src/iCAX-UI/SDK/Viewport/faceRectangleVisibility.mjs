import * as THREE from '../ThirdParty/three/three.module.js';

// Keep the completed gesture's physical meshes and transforms while topology
// resolves. Presentation overlays (hover, paths, grid) are deliberately absent.
export function captureFaceOccluders(viewport,target){
  const meshes=[];
  for(const o of viewport.sceneObjects.values()){
    if(!o.isMesh||o===target||!o.geometry)continue;
    let shown=true;for(let parent=o;parent;parent=parent.parent)if(!parent.visible){shown=false;break;}
    // sceneObjects contains the physical resource meshes. Do not traverse their
    // children: products attach selected/hovered faces and path glyphs there.
    if(shown)meshes.push({geometry:o.geometry,matrix:o.matrixWorld.clone()});
  }
  return meshes;
}

// Render face IDs through the same GPU depth test as the viewport. Sampling
// centroids/normals cannot reliably distinguish hidden faces, holes and narrow
// visible slivers; this pass selects only faces actually present in the box.
export function visibleRectangleFaces(renderer,geometry,faces,world,camera,viewport,rectangle,occluders=[]){
  if(!faces.length||viewport.width<=0||viewport.height<=0)return [];
  if(faces.length>=0xffffff)throw Error('Too many selectable faces');
  const width=Math.ceil(viewport.width),height=Math.ceil(viewport.height);
  const x=Math.max(0,Math.floor(rectangle.left-viewport.left)),right=Math.min(width,Math.ceil(rectangle.right-viewport.left));
  const top=Math.max(0,Math.floor(rectangle.top-viewport.top)),bottom=Math.min(height,Math.ceil(rectangle.bottom-viewport.top));
  if(right<=x||bottom<=top)return [];
  const position=geometry.attributes.position,index=geometry.index,count=index?.count||position.count,positions=new Float32Array(count*3),colors=new Uint8Array(count*3);
  for(let n=0;n<count;n++){const i=index?index.getX(n):n;positions[n*3]=position.getX(i);positions[n*3+1]=position.getY(i);positions[n*3+2]=position.getZ(i);}
  faces.forEach((face,i)=>{
    const id=i+1,r=id&255,g=(id>>8)&255,b=(id>>16)&255;
    for(const range of face.triangleRanges)for(let n=+range.first*3;n<(+range.first+ +range.count)*3;n++){colors[n*3]=r;colors[n*3+1]=g;colors[n*3+2]=b;}
  });
  const pickGeometry=new THREE.BufferGeometry();pickGeometry.setAttribute('position',new THREE.BufferAttribute(positions,3));pickGeometry.setAttribute('faceColor',new THREE.BufferAttribute(colors,3,true));
  const vertex='attribute vec3 faceColor; varying vec3 pickColor; void main(){pickColor=faceColor;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}';
  const material=new THREE.ShaderMaterial({vertexShader:vertex,fragmentShader:'varying vec3 pickColor; void main(){gl_FragColor=vec4(pickColor,1.0);}',side:THREE.DoubleSide,toneMapped:false});
  const black=new THREE.ShaderMaterial({vertexShader:'void main(){gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}',fragmentShader:'void main(){gl_FragColor=vec4(0.0,0.0,0.0,1.0);}',side:THREE.DoubleSide,toneMapped:false});
  const scene=new THREE.Scene(),mesh=new THREE.Mesh(pickGeometry,material);mesh.matrixAutoUpdate=false;mesh.matrix.copy(world);scene.add(mesh);
  for(const source of occluders){const o=new THREE.Mesh(source.geometry,black);o.matrixAutoUpdate=false;o.matrix.copy(source.matrix);scene.add(o);}
  const target=new THREE.WebGLRenderTarget(width,height,{format:THREE.RGBAFormat,type:THREE.UnsignedByteType,depthBuffer:true,stencilBuffer:false});
  const previous={target:renderer.getRenderTarget(),viewport:renderer.getViewport(new THREE.Vector4()),scissor:renderer.getScissor(new THREE.Vector4()),scissorTest:renderer.getScissorTest(),color:renderer.getClearColor(new THREE.Color()),alpha:renderer.getClearAlpha(),autoClear:renderer.autoClear};
  try{
    // setRenderTarget installs its physical viewport. setViewport would apply
    // the desktop device pixel ratio again and distort a high-DPI pick pass.
    renderer.setRenderTarget(target);renderer.setScissorTest(false);renderer.setClearColor(0,1);renderer.autoClear=true;renderer.render(scene,camera);
    const pixels=new Uint8Array((right-x)*(bottom-top)*4);renderer.readRenderTargetPixels(target,x,height-bottom,right-x,bottom-top,pixels);
    const visible=new Set();for(let i=0;i<pixels.length;i+=4){const id=pixels[i]|pixels[i+1]<<8|pixels[i+2]<<16;if(id)visible.add(id-1);}
    return faces.filter((_,i)=>visible.has(i));
  }finally{
    renderer.setViewport(previous.viewport);renderer.setScissor(previous.scissor);renderer.setScissorTest(previous.scissorTest);renderer.setRenderTarget(previous.target);renderer.setClearColor(previous.color,previous.alpha);renderer.autoClear=previous.autoClear;
    target.dispose();pickGeometry.dispose();material.dispose();black.dispose();
  }
}
