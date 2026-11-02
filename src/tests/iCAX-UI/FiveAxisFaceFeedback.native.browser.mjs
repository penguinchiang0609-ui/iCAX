import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyFaceFeedback({page,rpc,root,output,errors}) {
  page.setDefaultTimeout(120000);
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const draft=()=>page.evaluate(async()=>(await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs')).currentDraft(fixture.view));
  const report=[];
  for(const [name,path,wanted] of [
    ['mercedes','TestData/automotive-stamping/mercedes-benz-t-node/Mercedes-Benz_T-Node.stp','rejected'],
    ['pololu','TestData/real-sheet-metal/pololu-stamped-bracket/Pololu-2257-stamped-bracket.step','accepted'],
  ]) {
    await selectWorkpieceFile(page,resolve(root,path));await idle();
    await page.waitForFunction(()=>fixture.view.viewport.sceneObjects.size===1);
    const before=await read();
    await page.evaluate(()=>{
      const v=fixture.view.viewport;v.fitView(1.1);
      fixture.feedbackBase={canvas:v.renderer.domElement,mesh:[...v.sceneObjects.values()][0],camera:v.getCameraState()};
    });
    const modes=page.getByRole('combobox',{name:'显示模式'}),display=[];
    for(const mode of ['shaded','wireframe','shadedEdges']) {
      await modes.selectOption(mode);
      const result=await page.evaluate(()=>{
        const v=fixture.view.viewport,base=fixture.feedbackBase,m=[...v.sceneObjects.values()][0],edges=m.userData.meshEdges;
        return {sameCanvas:base.canvas===v.renderer.domElement,sameMesh:base.mesh===m,camera:v.getCameraState(),previous:base.camera,
          surface:m.material.visible,edgeMode:edges?.userData.edgeMode,edgeColor:edges?.material.color.getHex(),edgeVertices:edges?.geometry.getAttribute('position').count||0,
          cadVertices:m.geometry.userData.topologyEdges.length/3,featureVertices:m.geometry.userData.topologyFeatureEdges.length/3};
      });
      assert.equal(result.sameCanvas,true);assert.equal(result.sameMesh,true);assert.deepEqual(result.camera,result.previous);
      assert.equal(result.surface,mode!=='wireframe');assert.equal(result.edgeVertices,mode==='shaded'?0:result.cadVertices);
      if(mode!=='shaded')assert.equal(result.edgeMode,'cad-all');
      if(mode==='shadedEdges')assert.equal(result.edgeColor,0x46505c);
      if(name==='mercedes')assert.ok(result.cadVertices>result.featureVertices);
      display.push({mode,edgeVertices:result.edgeVertices,cadVertices:result.cadVertices});
      await page.screenshot({path:resolve(output,`${name}-${mode}.png`)});
    }
    await command('five.rough.face');
    await page.waitForFunction(()=>fixture.view.fiveRegistrationInteraction?.faces?.length>0);
    const spots=await page.evaluate(async()=>{
      const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view,vp=v.viewport;
      const source=vp.sceneObjects.get(v.scene.activePlacementId),rect=vp.renderer.domElement.getBoundingClientRect(),ray=new THREE.Raycaster(),found=new Map();
      source.updateMatrixWorld(true);
      const geometry=source.geometry,position=geometry.getAttribute('position'),a=new THREE.Vector3(),b=a.clone(),c=a.clone();
      const project=(p,index)=>p.fromBufferAttribute(position,geometry.index?geometry.index.getX(index):index).applyMatrix4(source.matrixWorld).project(vp.camera);
      // Pick the interior of a large visible triangle, not a coarse grid sample
      // that can land exactly on a narrow CAD edge or on a subpixel face.
      for(const face of v.fiveRegistrationInteraction.faces){
        if(!found.has(face.faceId))found.set(face.faceId,{faceId:face.faceId,triangles:face.triangleRanges.reduce((s,r)=>s+Number(r.count),0),samples:[]});
        const candidate=found.get(face.faceId);
        for(let i=face.first*3;i<(face.first+face.count)*3;i+=3){
          project(a,i);project(b,i+1);project(c,i+2);
          if([a,b,c].some(p=>p.z < -1 || p.z > 1))continue;
          const x=(a.x+b.x+c.x)/3,y=(a.y+b.y+c.y)/3,area=Math.abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))*rect.width*rect.height/8;
          const screen={x:rect.x+(x+1)*rect.width/2,y:rect.y+(1-y)*rect.height/2,area};
          if(area<9 || document.elementFromPoint(screen.x,screen.y)!==vp.renderer.domElement)continue;
          candidate.samples.push(screen);
        }
        candidate.samples.sort((a,b)=>b.area-a.area);candidate.samples.length=Math.min(3,candidate.samples.length);
      }
      const visible=[];
      for(const candidate of [...found.values()].filter(f=>f.samples.length).sort((a,b)=>b.samples[0].area-a.samples[0].area)){
        for(const sample of candidate.samples){
          ray.setFromCamera(new THREE.Vector2((sample.x-rect.x)/rect.width*2-1,1-(sample.y-rect.y)/rect.height*2),vp.camera);
          const hit=ray.intersectObject(source,false)[0];if(!hit)continue;
          const face=v.fiveRegistrationInteraction.faces.find(f=>hit.faceIndex>=f.first&&hit.faceIndex<f.first+f.count);
          if(face?.faceId!==candidate.faceId)continue;
          visible.push({...sample,faceId:candidate.faceId,triangles:candidate.triangles});break;
        }
        if(visible.length===24)break;
      }
      return visible;
    });
    assert.ok(spots.length>1);
    let selected;
    for(const spot of spots){
      await page.mouse.move(spot.x,spot.y);
      await page.waitForFunction(id=>{
        const s=fixture.view.fiveRegistrationInteraction;return s?.previewFace?.faceId===id&&(!!s.hover||!!s.error);
      },spot.faceId);
      const result=await page.evaluate(()=>({valid:!!fixture.view.fiveRegistrationInteraction.hover,error:fixture.view.fiveRegistrationInteraction.error}));
      if(wanted==='accepted'?result.valid:result.error==='此面不是平面'){selected=spot;break;}
    }
    assert.ok(selected,`${name} has a visible ${wanted} positioning face`);
    await page.mouse.click(selected.x,selected.y);await idle();
    await page.mouse.move(40,230);
    await page.waitForFunction(()=>!fixture.view.fiveRegistrationInteraction?.previewFace);
    const feedback=await page.evaluate(()=>fixture.view.fiveRegistrationInteraction.group.children.filter(o=>o.userData.registrationFace&&!o.userData.registrationHover).map(o=>({id:o.userData.registrationFace,status:o.userData.registrationStatus,triangles:o.geometry.getAttribute('position').count/3,color:o.material.color.getHex()})));
    assert.deepEqual(feedback,[{id:selected.faceId,status:wanted,triangles:selected.triangles,color:wanted==='accepted'?0x3268ed:0xd85a4e}]);
    const d=await draft();
    if(wanted==='rejected'){
      assert.ok((await page.evaluate(()=>fixture.view.error)).includes('曲面不能用作平面'));
      assert.equal(d.referenceResults.plane,undefined);assert.equal(d.planeX,'');
    } else {
      assert.equal(d.referenceResults.plane.faceId,selected.faceId);
      assert.equal(await page.evaluate(()=>fixture.view.fiveAxisRegistrationPick.prefix),'origin');
    }
    await page.screenshot({path:resolve(output,`${name}-face-${wanted}.png`)});
    const pixels=await page.evaluate(async spot=>{
      const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
      const v=fixture.view.viewport,r=v.renderer.domElement.getBoundingClientRect(),gl=v.renderer.getContext(),rgba=new Uint8Array(4);
      v.renderer.render(v.scene,v.camera);
      gl.readPixels(Math.floor((spot.x-r.x)*gl.drawingBufferWidth/r.width),gl.drawingBufferHeight-1-Math.floor((spot.y-r.y)*gl.drawingBufferHeight/r.height),1,1,gl.RGBA,gl.UNSIGNED_BYTE,rgba);
      const mesh=fixture.view.fiveRegistrationInteraction.group.children.find(o=>o.userData.registrationFace&&!o.userData.registrationHover),ray=new THREE.Raycaster(),hits=[];
      ray.setFromCamera(new THREE.Vector2((spot.x-r.x)/r.width*2-1,1-(spot.y-r.y)/r.height*2),v.camera);
      THREE.Mesh.prototype.raycast.call(mesh,ray,hits);
      return {rgba:[...rgba],intersects:hits.length,spot,triangles:mesh.geometry.getAttribute('position').count/3};
    },selected);
    writeFileSync(resolve(output,`${name}-pixel-check.json`),JSON.stringify(pixels,null,2));
    assert.ok(pixels.intersects>0,'highlight covers the actual clicked pixel');
    assert.ok(wanted==='accepted'?pixels.rgba[2]>pixels.rgba[0]+15:pixels.rgba[0]>pixels.rgba[1]+15,'selected face color is visible in the rendered image');
    // A refresh must keep accepted/rejected feedback, the selection control,
    // and the existing viewport while waiting for a real native reply.
    await page.evaluate(()=>{
      fixture.feedbackInput=document.querySelector('.five-reference-value');
      fixture.feedbackInput.focus({preventScroll:true});
      fixture.holdMethod='CamScene.ReadState';fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.refresh');
    });
    await page.waitForFunction(()=>fixture.held);
    await page.evaluate(()=>fixture.release());await idle();
    assert.ok(await page.evaluate(()=>fixture.feedbackInput===document.querySelector('.five-reference-value')&&fixture.feedbackBase.canvas===fixture.view.viewport.renderer.domElement));
    assert.equal(await page.evaluate(()=>fixture.view.fiveRegistrationInteraction.group.children.filter(o=>o.userData.registrationFace&&!o.userData.registrationHover).length),1);
    await page.locator('[data-five-command="five.registration.cancel"]').last().click();
    assert.equal(await page.evaluate(()=>fixture.view.fiveRegistrationInteraction),null);
    assert.deepEqual(await read(),before);
    await command('five.rough.face');
    assert.deepEqual((await draft()).referenceFaces,{});
    await page.locator('[data-five-command="five.registration.cancel"]').last().click();
    report.push({name,selected,status:wanted,display,persistentAfterMouseLeave:true,refreshPreservesControl:true,cancelLeavesSceneUnchanged:true});
  }
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'face-feedback-report.json'),JSON.stringify({passed:true,cases:report,coverage:'Real native CAD import, exact face validation and Edge pointer interaction; not standalone CEF'},null,2));
  console.log('CAD edges and persistent accepted/rejected face feedback passed');
}
