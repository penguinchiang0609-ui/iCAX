import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {browserReportDirectory,serveBrowserAsset} from './browserPackageRuntime.mjs';

const {chromium}=createRequire(import.meta.url)(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const output=browserReportDirectory('viewport-depth-range');
const browser=await chromium.launch({channel:process.env.ICAX_BROWSER_CHANNEL||'msedge',headless:true,args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
const report={passed:false,cases:[]},errors=[];
try{
  const page=await browser.newPage({viewport:{width:800,height:600}});page.on('pageerror',e=>errors.push(e.message));
  await page.route('http://depth.test/**',serveBrowserAsset);await page.goto('http://depth.test/');
  await page.evaluate(async()=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const {createThreeViewport}=await import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs');
    document.body.style='margin:0;width:800px;height:600px';const host=document.createElement('div');host.style='width:800px;height:600px';document.body.append(host);
    const viewport=createThreeViewport({continuousRender:false,antialias:false,showGrid:false,showWorldAxes:false,showOrientationGizmo:false,projectionMode:'perspective'}).mount(host);
    const front=new THREE.Mesh(new THREE.BoxGeometry(800,300,1.2),new THREE.MeshBasicMaterial({color:0x00ff00}));front.position.z=.6;front.renderOrder=1;
    const rear=new THREE.Mesh(new THREE.BoxGeometry(600,200,3),new THREE.MeshBasicMaterial({color:0xff0000}));rear.position.z=-1.5;rear.renderOrder=2;viewport.content.add(front,rear);
    viewport.camera.position.set(0,-1500,2000);viewport.camera.up.set(0,0,1);viewport.camera.lookAt(0,0,0);viewport.camera.updateMatrixWorld(true);
    viewport.setTransformManipulator({mode:'translate',position:[350,0,0]});
    let plane;viewport.scene.traverse(o=>{if(o.isTransformControlsPlane)plane=o;});
    const conventional=new THREE.WebGLRenderer({antialias:false});conventional.setSize(800,600);
    const f=window.fixture={THREE,viewport,conventional,front,rear,plane,toggle:false};
    f.render=()=>{viewport.setBackgroundColor((f.toggle=!f.toggle)?0x101010:0x111111);return {near:viewport.camera.near,far:viewport.camera.far};};
    f.pixel=()=>{const gl=viewport.renderer.getContext(),p=new Uint8Array(4);gl.readPixels(Math.floor(gl.drawingBufferWidth/2),Math.floor(gl.drawingBufferHeight/2),1,1,gl.RGBA,gl.UNSIGNED_BYTE,p);return [...p];};
    f.patch=(renderer=viewport.renderer)=>{const gl=renderer.getContext(),p=new Uint8Array(80*40*4);gl.readPixels(Math.floor(gl.drawingBufferWidth/2)-40,Math.floor(gl.drawingBufferHeight/2)-20,80,40,gl.RGBA,gl.UNSIGNED_BYTE,p);let red=0,green=0;for(let i=0;i<p.length;i+=4){if(p[i]>245&&p[i+1]<10)red++;if(p[i+1]>245&&p[i]<10)green++;}return {red,green};};
  });
  const thin=await page.evaluate(()=>{const f=fixture,range=f.render(),pixel=f.pixel(),gl=f.viewport.renderer.getContext();const correct={range,pixel,patch:f.patch(),depthBits:gl.getParameter(gl.DEPTH_BITS),planeType:f.plane?.type,planeVisible:f.plane?.visible,materialVisible:f.plane?.material.visible};f.viewport.camera.near=.001;f.viewport.camera.far=50000;f.viewport.camera.updateProjectionMatrix();f.conventional.render(f.viewport.scene,f.viewport.camera);correct.badPatch=f.patch(f.conventional);correct.badRange={near:f.viewport.camera.near,far:f.viewport.camera.far};f.viewport.renderer.render(f.viewport.scene,f.viewport.camera);correct.wideRangePatch=f.patch();f.render();correct.restoredPixel=f.pixel();return correct;});
  report.thinSheet=thin;
  assert.equal(thin.planeType,'TransformControlsPlane');assert.equal(thin.planeVisible,true);assert.equal(thin.materialVisible,false);
  assert.ok(thin.range.near>500&&thin.range.far<5000,JSON.stringify(thin));assert.ok(thin.pixel[1]>245&&thin.pixel[0]<10,JSON.stringify(thin));
  assert.ok(thin.badPatch.red>0,'negative control must reproduce hidden support showing through the 1.2 mm sheet');assert.equal(thin.patch.red,0);assert.deepEqual(thin.restoredPixel,thin.pixel);
  assert.equal(thin.wideRangePatch.red,0,'close near plane must not destroy depth separation');
  report.cases.push('real invisible TransformControlsPlane leaves a tight depth range; a 1.2 mm opaque sheet occludes its support in actual WebGL pixels');report.thinSheet=thin;

  const groups=await page.evaluate(()=>{
    const f=fixture,{THREE,viewport}=f,base=f.render();
    const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute([-600,0,0,-500,0,0,-550,80,0, -600,0,-50000,-500,0,-50000,-550,80,50000],3));geometry.setIndex([0,1,2,3,4,5]);geometry.addGroup(0,3,0);geometry.addGroup(3,3,1);
    const materials=[new THREE.MeshBasicMaterial({color:0x777777}),new THREE.MeshBasicMaterial({color:0x333333,visible:false})];const mesh=new THREE.Mesh(geometry,materials);viewport.content.add(mesh);f.groupMesh=mesh;
    const position=geometry.attributes.position,readX=position.getX.bind(position);let reads=0;position.getX=i=>{++reads;return readX(i);};
    const hiddenGroup=f.render(),initialReads=reads;f.render();const repeatedReads=reads;position.needsUpdate=true;f.render();const updatedReads=reads;
    geometry.setDrawRange(0,0);const emptyRange=f.render();geometry.setDrawRange(0,Infinity);materials[0].visible=false;const allHidden=f.render();
    materials[1].visible=true;geometry.clearGroups();geometry.addGroup(0,3,0);const unusedVisibleMaterial=f.render();geometry.addGroup(3,3,1);const visibleFar=f.render();
    materials[1].transparent=true;materials[1].opacity=0;materials[1].depthWrite=true;const transparentDepth=f.render();mesh.layers.set(2);const hiddenLayer=f.render();
    viewport.content.remove(mesh);mesh.geometry.dispose();materials.forEach(m=>m.dispose());return {base,hiddenGroup,initialReads,repeatedReads,updatedReads,emptyRange,allHidden,unusedVisibleMaterial,visibleFar,transparentDepth,hiddenLayer};
  });
  for(const key of ['hiddenGroup','emptyRange','allHidden','unusedVisibleMaterial','hiddenLayer'])assert.ok(groups[key].near>500&&groups[key].far<5000,JSON.stringify({key,...groups[key]}));
  assert.ok(groups.initialReads>0);assert.equal(groups.repeatedReads,groups.initialReads,'unchanged grouped geometry must reuse cached bounds');assert.ok(groups.updatedReads>groups.repeatedReads,'attribute versions invalidate grouped bounds');
  assert.ok(groups.visibleFar.near<1&&groups.visibleFar.far>20000,JSON.stringify(groups.visibleFar));assert.deepEqual(groups.transparentDepth,groups.visibleFar,'transparent material may still write depth');
  report.cases.push('material arrays honor visible draw groups, zero draw ranges, camera layers, and opacity-zero depth writers; grouped bounds cache invalidates on geometry updates');report.materialGroups=groups;
  const closeViews=await page.evaluate(()=>{
    const f=fixture,{THREE,viewport}=f,geometry=new THREE.BufferGeometry();
    geometry.setAttribute('position',new THREE.Float32BufferAttribute([-10000,0,-50000,-9900,0,-50000,-9950,100,50000],3));
    const surround=new THREE.Mesh(geometry,new THREE.MeshBasicMaterial({color:0x777777}));viewport.content.add(surround);
    const results=[];
    for(const projectionMode of ['perspective','orthographic'])for(const radius of [1500,2500,4000])for(const theta of [-1.8,-1.57,-1.3]){
      viewport.setCameraState({target:{x:0,y:0,z:0},radius,theta,phi:.64,projectionMode});
      results.push({projectionMode,radius,theta,...f.render(),...f.patch()});
    }
    viewport.content.remove(surround);geometry.dispose();surround.material.dispose();return results;
  });
  assert.ok(closeViews.some(v=>v.near===.001&&v.far>20000),'surrounding geometry must force the difficult close-near range');
  assert.ok(closeViews.every(v=>v.red===0&&v.green>3000),JSON.stringify(closeViews));
  report.closeViews=closeViews;report.cases.push('18 rotated and zoomed close-inspection views retain thin-surface occlusion in perspective and orthographic projection');
  await page.evaluate(()=>fixture.render());await page.screenshot({path:resolve(output,'thin-sheet-occlusion.png')});assert.deepEqual(errors,[]);report.passed=true;
}finally{writeFileSync(resolve(output,'viewport-depth-range-report.json'),JSON.stringify({...report,errors},null,2));await browser.close();}
console.log(JSON.stringify(report));
