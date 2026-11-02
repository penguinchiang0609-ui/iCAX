import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {frameAlignmentDriver} from './FiveAxisFrameAlignment.native.browser.mjs';

export async function verifyDenseSnap({page,command,read,output,base='/src/',injectFailure=false}){
  const before=await read(),cases=[];
  if(injectFailure)await page.evaluate(()=>{
    const proxy=fixture.context.sceneProxy,invoke=proxy.invoke.bind(proxy);let first=true;
    proxy.invoke=(method,args,...rest)=>{if(first&&method==='CamScene.ReadSnapFeatures'){first=false;return Promise.reject(Error('Injected resource transport failure'));}return invoke(method,args,...rest);};
  });
  await command('five.tool.align');
  if(injectFailure){
    await page.waitForFunction(()=>fixture.view.sceneSnapping?.errors?.size===1);
    await page.mouse.move(1050,550);
    assert.ok((await page.locator('[data-five-key="snap-status"]').textContent()).includes('读取失败'));
    await page.getByRole('button',{name:'重试读取吸附特征',exact:true}).click();
    cases.push('Catalog transport failure remains visible during mouse movement; explicit retry loads the actual geometry');
  }
  await page.waitForFunction(()=>fixture.view.sceneSnapping?.catalogs.has(fixture.view.fiveAlignment?.placementId)||fixture.view.sceneSnapping?.errors?.size,null,{timeout:120000});
  assert.equal(await page.evaluate(()=>[...fixture.view.sceneSnapping.errors.values()].map(e=>e.message).join('; ')),'');
  const stats=await page.evaluate(()=>{
    const cat=fixture.view.sceneSnapping.catalogs.get(fixture.view.fiveAlignment.placementId);
    return {features:cat.features.length,faces:cat.faces.length,bytes:new TextEncoder().encode(JSON.stringify(cat)).length};
  });
  assert.ok(stats.features>1000);assert.ok(stats.bytes>1024*1024);
  const descriptors=await page.evaluate(async()=>{
    const v=fixture.view,p=fixture.context?.sceneProxy||v.viewportSceneProxy;
    const request={expected:v.scene.domain.expected,setupId:v.scene.domain.activeSetupId,objectId:v.fiveAlignment.placementId,transport:'resource'};
    return [await p.invoke('CamScene.ReadSnapFeatures',request),await p.invoke('CamScene.ReadSnapFeatures',request)];
  });
  assert.deepEqual(descriptors[0],descriptors[1]);assert.equal(Number(descriptors[0].featureCount),stats.features);
  assert.ok(JSON.stringify(descriptors[0]).length<2048);
  assert.equal(await page.locator('[data-five-key="snap-status"]').count(),0);
  // Candidate screen points come from actual rendered triangles, independently
  // of the production inference code. Hover and click use real pointer events.
  const targets=await page.evaluate(async base=>{
    const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const v=fixture.view,vp=v.viewport,o=vp.sceneObjects.get(v.fiveAlignment.placementId),cat=v.sceneSnapping.catalogs.get(v.fiveAlignment.placementId);
    vp.setViewDirection([1,-1,1]);vp.fitView(1.2);vp.scene.updateMatrixWorld(true);vp.camera.updateMatrixWorld(true);
    const r=vp.renderer.domElement.getBoundingClientRect(),pos=o.geometry.attributes.position,idx=o.geometry.index;
    const planar=new Set(cat.features.filter(f=>f.mode==='plane'&&f.featureId.startsWith('plane:')).flatMap(f=>f.faceIds)),points=[];
    for(const face of cat.faces.filter(f=>planar.has(f.faceId)))for(let i=+face.first;i<+face.first+ +face.count;i++){
      const p=[0,1,2].map(j=>new T.Vector3().fromBufferAttribute(pos,idx?idx.getX(i*3+j):i*3+j));
      const area=new T.Triangle(...p).getArea(),q=p[0].clone().add(p[1]).add(p[2]).multiplyScalar(1/3).applyMatrix4(o.matrixWorld).project(vp.camera);
      const x=Math.round(r.left+(q.x+1)*r.width/2),y=Math.round(r.top+(1-q.y)*r.height/2);
      if(q.z>=-1&&q.z<=1&&document.elementFromPoint(x,y)===vp.renderer.domElement)points.push({x,y,area});
    }
    return points.sort((a,b)=>b.area-a.area).slice(0,30);
  },base);
  assert.ok(targets.length);
  let selected;const timings=[];
  for(const target of targets.slice(0,10)){
    const start=Date.now();await page.mouse.move(target.x,target.y);
    await page.waitForFunction(()=>fixture.view.fiveAlignment.hover,null,{timeout:10000});
    timings.push(Date.now()-start);
    const hover=await page.evaluate(()=>fixture.view.fiveAlignment.hover);
    assert.ok(hover.query.featureId);
    if(!hover.boxKind){selected={...target,hover};break;}
  }
  assert.ok(selected,'No actual model feature was inferred');
  assert.equal(await page.locator('[data-align-inference-hint]').isVisible(),true);
  assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.feedback.children.filter(c=>c.userData.alignmentRole==='hover').length),1);
  await page.screenshot({path:resolve(output,'dense-hover.png')});
  await page.mouse.click(selected.x,selected.y);
  await page.waitForFunction(()=>fixture.view.fiveAlignment?.stage==='normal'&&fixture.view.fiveAlignment.valid);
  assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.feedback.children.filter(c=>c.userData.alignmentActive).some(c=>c.children.some(o=>o.isSprite))),false);
  assert.deepEqual(await page.evaluate(()=>fixture.view.fiveAlignment.plane.source),selected.hover.query);
  cases.push(`Actual perforated STEP: ${stats.features} native features, ${stats.faces} face ranges and ${stats.bytes} catalog bytes loaded through versioned resource; real hover/click and native plane solve passed (hover ms: ${timings})`);
  const action=async name=>{await page.locator(`[data-five-command="five.align.${name}"]`).click();await page.waitForFunction(()=>!fixture.view.fiveAlignment?.solving&&!fixture.view.fiveAlignment?.picking);};
  const driver=await frameAlignmentDriver(page,base);
  await action('next');await driver.action('basis-u');await action('next');await driver.action('plane-center');
  await page.waitForFunction(()=>fixture.view.fiveAlignment?.valid&&!!fixture.view.fiveAlignment?.origin);
  await page.screenshot({path:resolve(output,'dense-frame-preview.png')});
  assert.deepEqual((await read()).workpieces,before.workpieces);
  await action('apply');await page.waitForFunction(()=>!fixture.view.fiveAlignment);
  const after=await read();assert.notDeepEqual(after.workpieces,before.workpieces);
  assert.deepEqual(after.placements[0].transform,[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]);
  cases.push('Native frame reset commits the selected real feature and preserves identity placement after rebasing');
  return cases;
}
