import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {frameAlignmentDriver} from './FiveAxisFrameAlignment.native.browser.mjs';

export async function verifyAlignmentInference({page,command,read,output,holdRefresh=false,base='/src/'}){
  const before=await read(),cases=[],{idle,action,pick}=await frameAlignmentDriver(page,base);
  await command('five.tool.align');
  await page.waitForFunction(()=>fixture.view.sceneSnapping?.catalogs.has(fixture.view.fiveAlignment?.placementId));
  const panel=page.getByRole('dialog',{name:'自身坐标系重置',exact:true});
  assert.equal(await page.locator('[data-align-choice],.five-align-sources').count(),0);
  assert.equal(await page.evaluate(()=>fixture.view.sceneSnapping.presentation.layer.records.length),0);
  await page.screenshot({path:resolve(output,'01-clean-inference.png')});
  // Search native feature samples and real triangle interiors for observable
  // screen locations. Assertions below then use actual mouse hover/click.
  const locate=async kind=>page.evaluate(async({base,kind})=>{
    const T=await import(base+'iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const {inferAlignmentCandidate}=await import(base+'apps/five-axis-cam/webpage/alignmentInference.mjs');
    const v=fixture.view,d=v.fiveAlignment,vp=v.viewport,o=vp.sceneObjects.get(d.placementId),cat=v.sceneSnapping.catalogs.get(d.placementId);
    const points=cat.features.flatMap(f=>[f.point,...(f.edgeSamples||[]).filter((_,i)=>i%8===0)]);
    const pos=o.geometry.attributes.position,idx=o.geometry.index;
    for(const f of cat.faces)for(let i=+f.first;i<+f.first+ +f.count;i+=Math.max(1,Math.floor(f.count/20))){
      const p=new T.Vector3();for(let j=0;j<3;j++)p.add(new T.Vector3().fromBufferAttribute(pos,idx?idx.getX(i*3+j):i*3+j));points.push(p.multiplyScalar(1/3).toArray());
    }
    for(const eye of [[1,-1,1],[0,0,1],[0,0,-1],[1,0,0],[0,1,0],[-1,1,1]]){
      vp.setViewDirection(eye);vp.fitView(1.2);vp.scene.updateMatrixWorld(true);vp.camera.updateMatrixWorld(true);
      const rect=vp.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster();
      for(const p of points){
        const q=new T.Vector3(...p).applyMatrix4(o.matrixWorld).project(vp.camera),x=Math.round(rect.left+(q.x+1)*rect.width/2),y=Math.round(rect.top+(1-q.y)*rect.height/2);
        if(document.elementFromPoint(x,y)!==vp.renderer.domElement)continue;
        ray.setFromCamera({x:(x-rect.left)/rect.width*2-1,y:1-(y-rect.top)/rect.height*2},vp.camera);
        const hit=ray.intersectObject(o,false)[0],front=cat.faces.find(f=>hit&&hit.faceIndex>=+f.first&&hit.faceIndex<+f.first+ +f.count);
        d.inference=null;
        const c=inferAlignmentCandidate(v,d,hit,{clientX:x,clientY:y})?.result;
        if(!c)continue;
        if(kind==='model'&&!(c.featureId.startsWith('plane:')&&c.faceIds.includes(front?.faceId)))continue;
        if(kind==='box'&&c.boxKind!=='OBB')continue;
        if(kind==='rim'&&(c.boxKind||!c.inferenceReason.includes('附近圆边')))continue;
        if(kind==='model'){
          const stable=[[-1,0],[1,0],[0,-1],[0,1]].every(([dx,dy])=>{
            const k=inferAlignmentCandidate(v,d,null,{clientX:x+dx,clientY:y+dy})?.result;return k?.featureId===c.featureId;
          });if(!stable)continue;
        }
        d.inference=null;return {x,y,result:c};
      }
    }return null;
  },{base,kind});
  let plane;
  for(const kind of ['box','rim','model']){
    const found=await locate(kind);assert.ok(found,`Missing ${kind} inference on real model`);
    await page.mouse.move(found.x,found.y);
    await page.waitForFunction(id=>fixture.view.fiveAlignment.hover?.featureId===id,found.result.featureId);
    const hover=await page.evaluate(()=>fixture.view.fiveAlignment.hover);
    assert.deepEqual(hover.query,found.result.query);
    assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.feedback.children.filter(c=>c.userData.alignmentRole==='hover').length),1);
    assert.equal(await page.locator('[data-align-inference-hint]').isVisible(),true);
    await page.screenshot({path:resolve(output,`02-${kind}-inference.png`)});
    if(kind==='model'){
      for(const [dx,dy] of [[-1,0],[1,0],[0,-1],[0,1],[0,0]]){
        await page.mouse.move(found.x+dx,found.y+dy);
        assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.hover?.featureId),hover.featureId);
      }
      plane=found;
    }
  }
  cases.push('Real goblet/cup: view-facing box fallback, visible native planar face and nearby circular rim inferred without lists; exactly one hover target and cursor label; one-pixel jitter is stable');
  const empty=await page.evaluate(()=>{const r=fixture.view.viewport.renderer.domElement.getBoundingClientRect();return {x:r.right-30,y:r.bottom-30};});
  await page.mouse.move(empty.x,empty.y);
  await page.waitForFunction(()=>!fixture.view.fiveAlignment.hover);
  assert.equal(await page.locator('[data-align-inference-hint]').isVisible(),false);
  await page.mouse.move(plane.x,plane.y);
  await page.waitForFunction(id=>fixture.view.fiveAlignment.hover?.featureId===id,plane.result.featureId);
  await page.mouse.click(plane.x,plane.y);await idle();
  assert.deepEqual(await page.evaluate(()=>fixture.view.fiveAlignment.plane.source),plane.result.query);
  assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.stage),'normal');
  await action('next');
  assert.equal(await page.evaluate(()=>fixture.view.sceneSnapping.presentation.layer.records.length),2);
  await action('basis-u');assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.x.source.construction.kind),'PlaneU');
  await action('next');await action('plane-center');
  assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.origin.source.construction.kind),'PlaneCenter');
  assert.equal(await page.evaluate(()=>fixture.view.fiveAlignment.valid),true);
  assert.deepEqual((await read()).workpieces,before.workpieces);
  cases.push('Actual mouse commits the hovered plane, then inferred U direction and plane centre; native exact solver validates a complete frame; preview is read-only');
  if(holdRefresh){
    await page.addStyleTag({content:'.alignment-scroll-test .five-align-panel{max-height:300px!important}.alignment-scroll-test .cam-context-pane::after,.alignment-scroll-test .cam-info-pane::after{content:"";display:block;height:1600px}'});
    await page.evaluate(()=>{document.body.classList.add('alignment-scroll-test');fixture.hold=true;fixture.holdMethod='CamScene.ReadState';});
    const pending=command('five.refresh');await page.waitForFunction(()=>fixture.held);
    const positions=await page.evaluate(()=>{
      const panel=document.querySelector('.five-align-panel');fixture.savedPanel=panel;fixture.savedCanvas=fixture.view.viewport.renderer.domElement;panel.focus({preventScroll:true});
      return ['.cam-context-pane','.cam-info-pane','.five-align-panel'].map((s,i)=>{const p=document.querySelector(s);p.scrollTop=40+i*10;return p.scrollTop;});
    });
    await page.evaluate(()=>fixture.release());await pending;await idle();
    assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-align-panel'].map(s=>document.querySelector(s).scrollTop)),positions);
    assert.equal(await page.evaluate(()=>document.activeElement===fixture.savedPanel&&fixture.savedPanel===document.querySelector('.five-align-panel')&&fixture.savedCanvas===fixture.view.viewport.renderer.domElement),true);
    await page.evaluate(()=>document.body.classList.remove('alignment-scroll-test'));
    cases.push('Delayed native refresh preserves panel and canvas nodes, current focus, both sidebar scroll positions and dialog scroll');
  }
  await page.screenshot({path:resolve(output,'03-frame-preview.png')});
  await action('close');assert.deepEqual((await read()).workpieces,before.workpieces);
  assert.equal(await page.locator('[data-align-inference-hint]').count(),0);
  await command('five.tool.align');await idle();
  await pick('plane');await action('next');await action('basis-u');await action('next');await action('plane-center');await action('apply');
  const after=await read();
  assert.notDeepEqual(after.workpieces,before.workpieces);
  assert.deepEqual(after.placements.find(p=>p.setupId===after.activeSetupId).transform,[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]);
  assert.equal(await panel.count(),0);
  cases.push('Cancel clears inference without mutation; repeat alignment applies atomically and resets placement to identity');
  return cases;
}

