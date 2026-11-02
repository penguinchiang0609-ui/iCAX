import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';
import {worldGeometry} from './FiveAxisAlignmentResume.native.browser.mjs';

export async function verifyFrameCandidates({page,rpc,info,output,modelPath}){
  const cases=[],read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  await selectWorkpieceFile(page,modelPath||info.sourcePath);
  await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisImportActive&&fixture.view.viewport?.sceneObjects.size===1);
  const initial=await read(),original=await worldGeometry(page);
  const command=action=>page.locator(`[data-five-command="five.align.${action}"]`);
  const canvas=page.locator('.cam-render-viewport canvas').first();
  const cursor=async row=>{const expected=row==='origin'?'point':'axis';assert.equal(await canvas.getAttribute('data-five-pick'),expected);assert.match(await canvas.evaluate(n=>getComputedStyle(n).cursor),new RegExp(expected+'-pick-cursor.svg'));};
  const start=async row=>{await command('pick.'+row).click();await page.waitForFunction(()=>!!fixture.view.sceneSnapping?.presentation?.layer?.records.length);await cursor(row);};
  const saved=()=>page.waitForFunction(()=>!fixture.view.fiveAlignment||!fixture.view.fiveAlignment.saving&&!fixture.view.fiveAlignment.saveTimer&&!fixture.view.fiveAlignment.dirty&&!fixture.view.fiveAlignment.request);
  const snapshot=s=>({workpieces:s.workpieces,placements:s.placements,setups:s.setups});
  const close=async()=>{
    if(await page.evaluate(()=>!!fixture.view.fiveAlignment?.selecting)){await canvas.focus();await page.keyboard.press('Escape');}
    await saved();await canvas.focus();await page.keyboard.press('Escape');await page.waitForFunction(()=>!fixture.view.fiveAlignment);
    for(let i=0;i<20&&JSON.stringify(snapshot(await read()))!==JSON.stringify(snapshot(initial));++i)await rpc({action:'undo'});
    assert.deepEqual(snapshot(await read()),snapshot(initial));await page.evaluate(()=>fixture.entry.handleRibbonCommand(fixture.context,'five.refresh'));
    assert.equal(await canvas.getAttribute('data-five-pick'),null);
  };
  const feedback=async row=>{await cursor(row);const actual=await page.evaluate(()=>{const group=fixture.view.fiveAlignment.feedback,axis=group.getObjectByName('alignment-direction-hover');let dots=0;group.traverse(o=>{if(o.isPoints)++dots;});return {axis:!!axis,dots,point:!!group.getObjectByName('alignment-origin-hover')};});if(row==='origin'){assert.equal(actual.axis,false);assert.equal(actual.point,true);}else{assert.equal(actual.axis,true);assert.equal(actual.dots,0);assert.equal(actual.point,false);}};
  const setView=async()=>{await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,1]);fixture.view.viewport.fitView(1.3);});await page.evaluate(()=>new Promise(requestAnimationFrame));};
  await setView();

  // Select the rendered coordinate handles by their projected arrow bodies,
  // without using the production inference routine to find a passing target.
  const coordinate=async(row,frame,axis)=>{
    await start(row);await setView();
    const target=await page.evaluate(async({frame,axis})=>{
      const {alignmentCoordinateTargets}=await import('/src/apps/five-axis-cam/webpage/alignmentCoordinatePicking.mjs'),v=fixture.view,vp=v.viewport;
      const target=alignmentCoordinateTargets(v,v.fiveAlignment).find(t=>t.coordinateFrame===frame&&(axis?t.query.axis===axis:t.mode==='datum-point'));
      if(!target)throw Error('Displayed coordinate handle missing');
      const world=axis?target.worldPoint.clone().lerp(target.worldEnd,.8):target.worldPoint.clone(),p=world.project(vp.camera),r=vp.renderer.domElement.getBoundingClientRect();
      const {frameReferenceVector}=await import('/src/apps/five-axis-cam/webpage/workpieceFrame.mjs');
      return {x:r.x+(p.x+1)*r.width/2,y:r.y+(1-p.y)*r.height/2,featureId:target.featureId,direction:target.direction,point:target.point,sourceDirection:frameReferenceVector(target.direction,v.fiveAlignment.savedFrame),sourcePoint:frameReferenceVector(target.point,v.fiveAlignment.savedFrame,true)};
    },{frame,axis});
    await page.mouse.move(target.x,target.y);
    await page.waitForFunction(({row,expected})=>{const h=fixture.view.fiveAlignment?.hover;return !!h?.coordinateFrame&&(row==='origin'?h.point:h.direction).every((n,i)=>Math.abs(n-expected[i])<1e-7);},{row,expected:row==='origin'?target.point:target.direction});
    await feedback(row);
    await page.screenshot({path:resolve(output,`coordinate-${frame}-${axis||'origin'}.png`)});
    await page.mouse.click(target.x,target.y);await page.waitForFunction(()=>!fixture.view.fiveAlignment.selecting&&!fixture.view.fiveAlignment.picking);
    const value=await page.evaluate(row=>fixture.view.fiveAlignment.frame[row],row),expected=row==='origin'?target.sourcePoint:target.sourceDirection;
    value.forEach((n,i)=>assert.ok(Math.abs(n-expected[i])<1e-7));await saved();return target;
  };
  await coordinate('x','world','z');await close();
  await coordinate('origin','world',null);await close();
  cases.push('Actual pointer hover/click selects the existing world Z arrow and world origin, converting through the displayed workpiece transform');

  await start('x');
  const lines=await page.evaluate(()=>{const p=fixture.view.sceneSnapping.presentation;return {display:p.candidateDisplay,records:p.layer.records.filter(r=>r.type==='line').length,attached:!!p.layer.group.parent,strokes:p.layer.group.children.filter(o=>o.isLine).length};});
  assert.equal(lines.display,'directions');assert.ok(lines.records>0&&lines.strokes>0&&lines.attached);
  await page.screenshot({path:resolve(output,'axis-candidates-visible.png')});
  await command('pick.x').click();
  await start('x');
  await page.evaluate(()=>{fixture.view.viewport.setViewDirection([0,0,1]);fixture.view.viewport.fitView(1.3);});
  await page.evaluate(()=>new Promise(requestAnimationFrame));
  const straight=await page.evaluate(()=>{
    const v=fixture.view,vp=v.viewport,layer=v.sceneSnapping.presentation.layer,object=layer.object,catalog=v.sceneSnapping.catalogs.get(v.fiveAlignment.placementId),native=catalog.features.filter(f=>f.mode==='line'&&!f.boxKind);
    const close=(a,b)=>a.every((n,i)=>Math.abs(n-b[i])<1e-7),same=(a,b)=>close(a[0],b[0])&&close(a.at(-1),b.at(-1))||close(a[0],b.at(-1))&&close(a.at(-1),b[0]);
    const missing=native.filter(f=>!layer.records.some(r=>r.type==='line'&&same(f.edgeSamples,r.source.edgeSamples))).map(f=>f.featureId);
    const r=vp.renderer.domElement.getBoundingClientRect(),project=p=>{const q=object.position.clone().set(...p).applyMatrix4(object.matrixWorld).project(vp.camera);return {x:r.left+(q.x+1)*r.width/2,y:r.top+(1-q.y)*r.height/2,z:q.z};};
    const candidates=layer.records.filter(r=>r.type==='line').map(record=>{const a=record.ends[0],b=record.ends.at(-1);return {id:record.feature.featureId,direction:record.feature.direction,length:a.distanceTo(b),positions:[.1,.3,.5,.7,.9].map(t=>project(a.clone().lerp(b,t).toArray()))};}).sort((a,b)=>b.length-a.length);
    fixture.edgeMethodStart=fixture.methods.length;
    return {nativeCount:native.length,displayedCount:layer.records.length,missing,maxLength:candidates[0].length,candidates:candidates.filter(c=>c.length>=candidates[0].length*.9).slice(0,40)};
  });
  assert.deepEqual(straight.missing,[],'Every native straight-edge span must have a displayed candidate');
  let longEdge=null;
  for(const candidate of straight.candidates){
    if(candidate.positions.some(p=>p.z< -1||p.z>1))continue;
    let matched=0;
    for(const position of candidate.positions){
      await page.mouse.move(position.x,position.y);await page.evaluate(()=>new Promise(requestAnimationFrame));
      const hover=await page.evaluate(()=>{const h=fixture.view.fiveAlignment.hover;return h&&{mode:h.mode,direction:h.direction,span:h.edgeSamples?.length?Math.hypot(...h.edgeSamples.at(-1).map((n,i)=>n-h.edgeSamples[0][i])):0};});
      if(!hover||hover.mode!=='line'||hover.span<candidate.length*.9||Math.abs(hover.direction.reduce((s,n,i)=>s+n*candidate.direction[i],0))<.999999)break;
      ++matched;
    }
    if(matched===5){longEdge=candidate;break;}
  }
  assert.ok(longEdge,'A long straight edge must be selectable at 10%, 30%, 50%, 70% and 90% of its span');
  await page.mouse.move(longEdge.positions[2].x,longEdge.positions[2].y);await page.evaluate(()=>new Promise(requestAnimationFrame));await feedback('x');
  const direction=await page.evaluate(()=>fixture.view.fiveAlignment.hover.direction);
  await page.screenshot({path:resolve(output,'long-edge-middle-highlight.png')});
  assert.deepEqual(await page.evaluate(()=>fixture.methods.slice(fixture.edgeMethodStart).map(m=>m.method)),[]);
  await page.mouse.click(longEdge.positions[2].x,longEdge.positions[2].y);await page.waitForFunction(()=>!fixture.view.fiveAlignment.selecting&&!fixture.view.fiveAlignment.picking);
  (await page.evaluate(()=>fixture.view.fiveAlignment.frame.x)).forEach((n,i)=>assert.ok(Math.abs(n-direction[i])<1e-7));
  await close();
  cases.push(`All ${straight.nativeCount} native straight edges have visible candidates; a ${longEdge.length.toFixed(1)} mm edge picks along its full span, including its middle, without native requests during hover; picking saves automatically`);
  for(const [row,axis] of [['x','z'],['z','y']])await coordinate(row,'current',axis);
  await close();
  cases.push('X and Z can pick the visible current-coordinate arrows after rotation and automatic saves, preserving the first axis and deriving Y');

  const rendered=async(row,kind)=>{
    await start(row);await setView();
    const targets=await page.evaluate(kind=>{
      const v=fixture.view,vp=v.viewport,layer=v.sceneSnapping.presentation.layer,object=layer.object,r=vp.renderer.domElement.getBoundingClientRect();object.updateWorldMatrix(true,false);
      const project=p=>{const v=object.position.clone().set(...p).applyMatrix4(object.matrixWorld).project(vp.camera);return {x:r.x+(v.x+1)*r.width/2,y:r.y+(1-v.y)*r.height/2,z:v.z};};
      return layer.records.filter(record=>kind==='line'?record.type==='line':record.type==='point'&&(kind==='center'?['center','datum-point','sphere-center'].includes(record.feature.mode):record.feature.mode==='endpoint')).flatMap(record=>{
        const f=record.feature,points=kind==='line'?[.3,.6,.85].map(t=>record.ends[0].clone().lerp(record.ends.at(-1),t).toArray()):[f.point];
        return points.map(point=>({...project(point),point:f.point,direction:f.direction,featureId:f.featureId,mode:f.mode}));
      }).filter(t=>t.z>=-1&&t.z<=1&&document.elementFromPoint(t.x,t.y)===vp.renderer.domElement).slice(0,150);
    },kind);
    assert.ok(targets.length>0,`Rendered ${kind} candidates must exist`);
    for(const target of targets){
      await page.mouse.move(target.x,target.y);await page.evaluate(()=>new Promise(requestAnimationFrame));
      const hover=await page.evaluate(()=>fixture.view.fiveAlignment.hover&&{featureId:fixture.view.fiveAlignment.hover.featureId,point:fixture.view.fiveAlignment.hover.point,direction:fixture.view.fiveAlignment.hover.direction,mode:fixture.view.fiveAlignment.hover.mode});
      if(!hover||hover.featureId!==target.featureId)continue;
      await feedback(row);
      const highlighted=await page.evaluate(id=>{const layer=fixture.view.sceneSnapping.presentation.layer,r=layer.records.find(r=>r.feature.featureId===id);if(r.pointBatch){const b=r.pointBatch.colors,i=r.pointIndex;return [b.getX(i),b.getY(i),b.getZ(i)];}return r.objects[0].material.color.toArray();},target.featureId);
      assert.ok(highlighted[1]>.7&&highlighted[0]<.6);
      await page.screenshot({path:resolve(output,`${kind}-candidate-highlight.png`)});
      await page.evaluate(()=>fixture.candidateLayer=fixture.view.sceneSnapping.presentation.layer);
      await page.mouse.click(target.x,target.y);await page.waitForFunction(()=>!fixture.view.fiveAlignment.selecting&&!fixture.view.fiveAlignment.picking);
      const value=await page.evaluate(row=>fixture.view.fiveAlignment.frame[row],row),expected=row==='origin'?target.point:target.direction.map(n=>n/Math.hypot(...target.direction));
      value.forEach((n,i)=>assert.ok(Math.abs(n-expected[i])<1e-7));
      assert.equal(await page.evaluate(()=>fixture.candidateLayer.group.parent),null);return target;
    }
    throw Error(`Visible ${kind} candidates did not highlight and pick`);
  };
  await rendered('x','line');await close();
  await rendered('origin','endpoint');await close();
  await rendered('origin','center');await close();
  cases.push('Projected visible straight edges, endpoints and hole/contour centers highlight cyan and commit their exact native direction/point through real pointer input; temporary candidates disappear after each selection');

  await start('origin');const points=await page.evaluate(()=>{const layer=fixture.view.sceneSnapping.presentation.layer;return {count:layer.records.length,batches:layer.pointBatches.size,children:layer.group.children.length};});
  assert.ok(points.count>0&&points.batches>0);await page.screenshot({path:resolve(output,'origin-candidates-visible.png')});
  await close();assert.deepEqual(await worldGeometry(page),original);assert.deepEqual(snapshot(await read()),snapshot(initial));
  cases.push('Origin mode visibly batches exact point candidates and coordinate origins; leaving selection without picking does not change native state; native undo restores each saved selection');
  return {cases,lines,points,straight:{nativeCount:straight.nativeCount,displayedCount:straight.displayedCount,maxLength:straight.maxLength,pickedLongEdgeLength:longEdge.length},model:modelPath||info.sourcePath};
}
