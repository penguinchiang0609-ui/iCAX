// Regenerate the bundled library previews from URDF/SDF visual geometry.
import {readFileSync,mkdirSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {resolve,extname,sep} from 'node:path';
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const root=fileURLToPath(new URL('../../',import.meta.url)),destination=resolve(root,'apps/five-axis-cam/resources/machines/thumbnails');
mkdirSync(destination,{recursive:true});
const browser=await chromium.launch({channel:'msedge',headless:true});
try{
 const page=await browser.newPage({viewport:{width:480,height:282},deviceScaleFactor:1});
 await page.route('https://machine-preview.test/**',route=>{
  const name=decodeURIComponent(new URL(route.request().url()).pathname);
  if(name==='/')return route.fulfill({contentType:'text/html',body:'<style>body{margin:0}canvas{display:block}</style>'});
  const path=resolve(root,'.'+name);
  if(!path.startsWith(root+sep))return route.abort();
  return route.fulfill({contentType:extname(path)==='.js'?'text/javascript':'application/octet-stream',body:readFileSync(path)});
 });
 for(const [folder,file] of [['industrial-ac','LC-3015.urdf'],['industrial-arm','IR-1600.sdf'],['rotary-four-plus-one','RC-1000.urdf'],['cradle-three-plus-two','TC-800.urdf']]){
  await page.goto('https://machine-preview.test/');
  await page.evaluate(async({folder,file})=>{
   const T=await import('/iCAX-UI/SDK/ThirdParty/three/three.module.js');
   const base='/apps/five-axis-cam/resources/machines/'+folder+'/',xml=new DOMParser().parseFromString(await (await fetch(base+file)).text(),'text/xml'),sdf=file.endsWith('.sdf');
   const model=xml.querySelector(sdf?'model':'robot'),group=new T.Group(),links=new Map(),joints=new Map();
   const numbers=(value,fallback='0 0 0')=>(value||fallback).trim().split(/\s+/).map(Number);
   const pose=(xyz,rpy)=>new T.Matrix4().makeRotationFromEuler(new T.Euler(...rpy,'ZYX')).setPosition(...xyz);
   for(const joint of model.querySelectorAll(':scope > joint')){const origin=joint.querySelector(':scope > origin');joints.set(joint.querySelector('child')?.getAttribute('link'),{parent:joint.querySelector('parent')?.getAttribute('link'),matrix:pose(numbers(origin?.getAttribute('xyz')),numbers(origin?.getAttribute('rpy')))});}
   const global=(name,seen=new Set())=>{if(seen.has(name))throw Error('joint cycle');seen.add(name);const j=joints.get(name);return j?global(j.parent,seen).multiply(j.matrix):new T.Matrix4();};
   for(const link of model.querySelectorAll(':scope > link')){
    const part=new T.Group(),v=numbers(link.querySelector(':scope > pose')?.textContent,'0 0 0 0 0 0');
    part.applyMatrix4(sdf?pose(v.slice(0,3),v.slice(3)):global(link.getAttribute('name')));links.set(link.getAttribute('name'),part);group.add(part);
    for(const visual of link.querySelectorAll(':scope > visual')){
     const ref=visual.querySelector('mesh')?.getAttribute('filename')||visual.querySelector('mesh > uri')?.textContent;if(!ref)continue;
     const bytes=await (await fetch(new URL(ref,new URL(base,location.href)))).arrayBuffer(),data=new DataView(bytes),count=data.getUint32(80,true),vertices=new Float32Array(count*9);
     if(84+count*50!==bytes.byteLength)throw Error('Preview generator expects binary STL');
     for(let i=0;i<count;i++)for(let c=0;c<9;c++)vertices[i*9+c]=data.getFloat32(84+i*50+12+c*4,true);
     const geometry=new T.BufferGeometry();geometry.setAttribute('position',new T.BufferAttribute(vertices,3));geometry.computeVertexNormals();
     const rgba=numbers(visual.querySelector('color')?.getAttribute('rgba')||visual.querySelector('diffuse')?.textContent,'0.7 0.75 0.8 1');
     const mesh=new T.Mesh(geometry,new T.MeshStandardMaterial({color:new T.Color(...rgba.slice(0,3)),roughness:.7,metalness:.15}));
     const origin=visual.querySelector(':scope > origin'),vp=numbers(visual.querySelector(':scope > pose')?.textContent,'0 0 0 0 0 0');
     mesh.applyMatrix4(sdf?pose(vp.slice(0,3),vp.slice(3)):pose(numbers(origin?.getAttribute('xyz')),numbers(origin?.getAttribute('rpy'))));part.add(mesh);
    }
   }
   const scene=new T.Scene();scene.background=new T.Color('#edf2f4');scene.add(group,new T.HemisphereLight(0xffffff,0x576875,2.5));const light=new T.DirectionalLight(0xffffff,3);light.position.set(4,-7,9);scene.add(light);
   const bounds=new T.Box3().setFromObject(group),center=bounds.getCenter(new T.Vector3()),size=bounds.getSize(new T.Vector3()),radius=size.length()/2;
   const camera=new T.PerspectiveCamera(35,480/282,.001,radius*30);camera.up.set(0,0,1);camera.position.copy(center).add(new T.Vector3(1,-1.7,1.05).normalize().multiplyScalar(radius/Math.sin(35*Math.PI/360)*1.08));camera.lookAt(center);
   const renderer=new T.WebGLRenderer({antialias:true,preserveDrawingBuffer:true});renderer.setSize(480,282);renderer.setPixelRatio(1);renderer.render(scene,camera);document.body.append(renderer.domElement);
  },{folder,file});
  const path=resolve(destination,file.replace(/\.(urdf|sdf)$/,'.png'));await page.screenshot({path});console.log(path);
 }
}finally{await browser.close();}
