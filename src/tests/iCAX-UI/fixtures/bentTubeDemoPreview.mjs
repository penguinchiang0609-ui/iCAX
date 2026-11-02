import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

// Render the actual imported native mesh, without picking or toolpath overlays.
export async function captureBentTubeEnds(page,base,demo,groups){
  const png=await page.evaluate(async({base,groups})=>{
    const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const source=fixture.view.viewport.sceneObjects.get(fixture.view.scene.activePlacementId);if(!source?.geometry)throw Error('Native display mesh is not ready');
    const geometry=source.geometry.clone();geometry.deleteAttribute('color');
    const result=document.createElement('canvas');result.width=1200;result.height=800;const ctx=result.getContext('2d');ctx.fillStyle='#10252e';ctx.fillRect(0,0,1200,800);
    const renderer=new T.WebGLRenderer({antialias:true,preserveDrawingBuffer:true});renderer.setSize(560,660);renderer.setPixelRatio(1);renderer.setClearColor('#142e38');
    for(let i=0;i<2;i++){
      const scene=new T.Scene(),mesh=new T.Mesh(geometry,new T.MeshStandardMaterial({color:0xc1d5da,metalness:.24,roughness:.4,side:T.DoubleSide}));
      mesh.matrixAutoUpdate=false;mesh.matrix.set(...groups[i].transform);scene.add(mesh);
      const local=new T.Vector3(groups[i].axisX,0,i? -640:1500),target=local.applyMatrix4(mesh.matrix);
      const camera=new T.OrthographicCamera(-140,140,165,-165,1,5000);camera.up.set(0,0,1);camera.position.copy(target).add(new T.Vector3(.3,-1,.22).normalize().multiplyScalar(1000));camera.lookAt(target);
      scene.add(new T.HemisphereLight(0xe8faff,0x293a45,1.3));const key=new T.DirectionalLight(0xffffff,2.1);key.position.copy(target).add(new T.Vector3(150,-400,300));key.target.position.copy(target);scene.add(key,key.target);
      const rim=new T.DirectionalLight(0x7fb9dc,1.3);rim.position.copy(target).add(new T.Vector3(-150,200,100));rim.target.position.copy(target);scene.add(rim,rim.target);
      renderer.render(scene,camera);const x=20+i*600;ctx.drawImage(renderer.domElement,x,115);ctx.fillStyle='#e1f2f3';ctx.font='bold 24px "Microsoft YaHei",sans-serif';ctx.fillText(i?'B端：菱形窗、圆孔、斜切口':'A端：渐变叶形镂空、波浪口',x+8,46);ctx.fillStyle='#86b9c5';ctx.font='18px "Microsoft YaHei",sans-serif';ctx.fillText(i?'工序 2 · 翻转 180° · 6 条刀路':'工序 1 · 7 条刀路',x+8,82);mesh.material.dispose();
    }
    renderer.dispose();geometry.dispose();return result.toDataURL('image/png');
  },{base,groups});
  writeFileSync(resolve(demo,'ends-comparison.png'),Buffer.from(png.split(',')[1],'base64'));
}
