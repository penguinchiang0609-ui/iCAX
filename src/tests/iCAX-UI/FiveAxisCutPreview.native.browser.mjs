import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { verifyRectangleSelection } from './FiveAxisRectangleSelection.native.browser.mjs';
import { verifyStandardHoleSplitting } from './FiveAxisStandardHoleSplitting.native.browser.mjs';

export async function verifyCutPreview({ page, rpc, output }) {
  page.setDefaultTimeout(30000);
  const command = id => page.evaluate(id => fixture.entry.handleRibbonCommand(fixture.context, id), id);
  const idle = () => page.waitForFunction(() => !fixture.view.pending);
  const verifyNumbers=async generated=>{
    const result=await page.evaluate(generated=>{
      const view=fixture.view,v=view.viewport,box=v.renderer.domElement.getBoundingClientRect(),state=view.scene.domain,setup=state.setups.find(s=>s.id===state.activeSetupId);
      const cuts=setup.definitionOrder.map(id=>view.scene.intentToolpaths.find(c=>c.id===id)).filter(c=>c.enabled!==false);
      const ops=setup.generatedGeometry?.operations;
      return {leaders:v.specificationLeaderSvg.children.length,labels:v.specificationLabels.map((label,i)=>{
        const cut=cuts[i],op=generated?ops[i]:null,placement=state.placements.find(p=>p.id===(op?.placementId||cut.placementId));
        const point=op?op.parts.find(p=>['Cut','Mark'].includes(p.role)).points[0]:cut.startPoint;
        const matrix=placement.transform,world=point.map((_,j)=>matrix[j*4]*point[0]+matrix[j*4+1]*point[1]+matrix[j*4+2]*point[2]+matrix[j*4+3]);
        const p=v.camera.position.clone().set(...world).project(v.camera),r=label.element.getBoundingClientRect();
        return {text:label.element.textContent,expected:String(i+1),anchorError:Math.hypot((r.left+r.right)/2-box.left-(p.x+1)*box.width/2,(r.top+r.bottom)/2-box.top-(1-p.y)*box.height/2),width:r.width,height:r.height,visible:!label.element.hidden};
      })};
    },generated);
    assert.equal(result.leaders,0,'path numbers have no leader lines');
    assert.ok(result.labels.length>0);
    for(const row of result.labels){assert.equal(row.text,row.expected,'only the machining sequence number is shown');if(row.visible){assert.ok(row.anchorError<1,JSON.stringify(row));assert.ok(row.width<32&&row.height<=18,'compact number label');}}
    return result;
  };
  const source = await rpc({ action:'invoke', scope:'scene', method:'CamScene.ReadState', payload:{} });
  await page.waitForFunction(() => !fixture.view.fiveToolAxisDisplay?.loading, null, {timeout:120000});
  const axisDisplay=await page.evaluate(()=>({cuts:fixture.view.fiveToolAxisDisplay?.data?.cuts.map(c=>({intentId:c.intentId,samples:c.samples.length,keypoints:c.samples.filter(s=>s.keypointIds.length).length})),errors:fixture.view.fiveToolAxisDisplay?.data?.errors}));
  writeFileSync(resolve(output,'axis-display-diagnostics.json'),JSON.stringify(axisDisplay,null,2));
  assert.deepEqual(axisDisplay.errors,[],'every enabled cut must have readable tool-axis keypoints');
  assert.deepEqual(axisDisplay.cuts.map(c=>c.intentId).sort(),source.intentToolpaths.filter(c=>c.enabled!==false&&c.machiningKind==='Cut').map(c=>c.id).sort());
  const cut = await page.evaluate(()=>fixture.view.scene.intentToolpaths.filter(c=>c.machiningKind==='Cut').sort((a,b)=>b.previewPoints.length-a.previewPoints.length)[0]);
  await page.locator(`[data-five-intent="${cut.id}"]`).click();
  const directionRows=await page.evaluate(()=>fixture.view.fiveCutPreview.group.children.filter(c=>c.userData.cutDirectionId).map(c=>({id:c.userData.cutDirectionId,color:c.material.uniforms.color.value.getHex()})));
  assert.deepEqual([...new Set(directionRows.map(c=>c.id))].sort(),source.intentToolpaths.filter(c=>c.enabled!==false).map(c=>c.id).sort(),'all enabled paths show travel direction without selection');
  for(const row of directionRows)assert.equal(row.color,row.id===cut.id?0xffc857:0x55b9e7,'only selected paths use the accent color');
  const ribbonCommands = await page.evaluate(() => fixture.entry.getRibbonDefinition(fixture.context).tabs.find(t=>t.id==='toolpath').groups.flatMap(g=>g.commands.map(c=>c.id)));
  assert.ok(!ribbonCommands.includes('five.tool.extract') && !ribbonCommands.includes('five.tool.manual-extract'));
  await page.evaluate(() => { fixture.previewCanvas = fixture.view.viewport.renderer.domElement; fixture.previewRow = document.querySelector('[data-five-intent]'); });
  await command('five.tool.extract');
  assert.equal(await page.getByRole('dialog', {name:'添加刀路'}).count(), 1);
  assert.equal(await page.getByRole('button', {name:'自动提取',exact:true}).count(), 1);
  assert.equal(await page.locator('[data-five-command="five.extraction.pick"]').count(), 1);
  await page.getByRole('dialog',{name:'添加刀路',exact:true}).getByRole('button', {name:'取消',exact:true}).click();
  assert.deepEqual((await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}})).intentToolpaths, source.intentToolpaths);
  assert.equal(await page.evaluate(() => fixture.previewCanvas===fixture.view.viewport.renderer.domElement && fixture.previewRow===document.querySelector('[data-five-intent]')), true);
  const toolRows=await page.getByRole('toolbar',{name:'刀路管理'}).getByRole('button').evaluateAll(buttons=>buttons.map(button=>button.getBoundingClientRect().top));
  assert.equal(new Set(toolRows).size,1,'all six tree actions fit on one row');
  await page.locator('.cam-context-pane').evaluate(node=>node.scrollTop=0);
  await page.evaluate(() => fixture.view.viewport.fitView());
  const nominalNumbers=await verifyNumbers(false);
  await page.screenshot({path:resolve(output,'compact-numbers-before-generation.png')});
  await command('five.display.numbers');
  assert.equal(await page.locator('.icax-three-specification-label-layer').evaluate(n=>getComputedStyle(n).visibility),'hidden');
  await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
  const renderedAxes=await page.evaluate(()=>fixture.view.fiveToolAxisDisplay.group.children.map(g=>({id:g.userData.intentId,count:g.userData.renderedCount})));
  assert.equal(renderedAxes.length,axisDisplay.cuts.length);
  assert.ok(renderedAxes.every(g=>g.count>=1),'each path retains a visible keypoint glyph in fit view');
  await page.screenshot({path:resolve(output,'cut-tree-new-and-small-directions.png')});

  // Read actual GPU pixels, isolating one marker only for measurement. This
  // catches shader failures and world-sized arrows even if metadata is correct.
  const measure = (scale, projection) => page.evaluate(({scale,projection}) => {
    const viewport=fixture.view.viewport, group=fixture.view.fiveCutPreview.group;
    const marker=group.children.find(c=>c.userData.cutDirectionId&&c.material.uniforms.color.value.getHex()===0xffc857), camera=viewport.getCameraState();
    const saved=[...viewport.scene.children,...group.children].map(object=>[object,object.visible]);
    const background=viewport.scene.background;
    try {
      viewport.scene.updateMatrixWorld(true);
      const target=marker.getWorldPosition(marker.position.clone());
      viewport.setCameraState({...camera, projectionMode:projection, target:target.toArray().reduce((r,v,i)=>(r[['x','y','z'][i]]=v,r),{}), radius:camera.radius*scale});
      for(const [object] of saved) object.visible=object===group||object===marker;
      viewport.scene.background=null;
      viewport.renderer.setClearColor(0x000000,0);
      viewport.renderer.render(viewport.scene,viewport.camera);
      const gl=viewport.renderer.getContext(),width=gl.drawingBufferWidth,height=gl.drawingBufferHeight;
      const bytes=new Uint8Array(width*height*4);gl.readPixels(0,0,width,height,gl.RGBA,gl.UNSIGNED_BYTE,bytes);
      let left=width,right=-1,bottom=height,top=-1,pixels=0;
      for(let y=0;y<height;y++)for(let x=0;x<width;x++){
        const i=(y*width+x)*4;
        if(bytes[i]>180&&bytes[i+1]>130&&bytes[i+1]<230&&bytes[i+2]<140&&bytes[i+3]>60){
          left=Math.min(left,x);right=Math.max(right,x);bottom=Math.min(bottom,y);top=Math.max(top,y);++pixels;
        }
      }
      const ratio=viewport.renderer.getPixelRatio();
      return {scale,projection,width:(right-left+1)/ratio,height:(top-bottom+1)/ratio,pixels:pixels/(ratio*ratio),direction:marker.userData.direction};
    } finally {
      for(const [object,visible] of saved)object.visible=visible;
      viewport.scene.background=background;
      viewport.setCameraState(camera);
    }
  },{scale,projection});
  const samples=[];
  for(const projection of ['orthographic','perspective'])for(const scale of [.2,1,5]) {
    const sample=await measure(scale,projection);samples.push(sample);
    assert.ok(sample.pixels>=12,JSON.stringify(sample));
    assert.ok(sample.width>=7&&sample.width<=20&&sample.height>=5&&sample.height<=20,JSON.stringify(sample));
  }
  for(const projection of ['orthographic','perspective']){
    const views=samples.filter(s=>s.projection===projection);
    assert.ok(Math.max(...views.map(s=>s.width))-Math.min(...views.map(s=>s.width))<=1);
    assert.ok(Math.max(...views.map(s=>s.height))-Math.min(...views.map(s=>s.height))<=1);
  }
  await command('five.tool.reverse');
  await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const reversed=await measure(1,'orthographic');
  reversed.direction.forEach((v,i)=>assert.ok(Math.abs(v+samples[1].direction[i])<1e-9));
  assert.ok(reversed.pixels>=12&&reversed.width<=20&&reversed.height<=20);
  await page.screenshot({path:resolve(output,'cut-direction-reversed.png')});
  await rpc({action:'undo'}); await command('five.refresh'); await idle();
  assert.deepEqual((await rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}})).intentToolpaths,source.intentToolpaths);
  if(process.env.ICAX_DIRECTION_DISPLAY_ONLY){
    const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
    const current=await invoke('CamScene.ReadState');let task=await invoke('CamPlan.Generate',{commandId:crypto.randomUUID(),expected:current.expected,setupId:current.activeSetupId});
    const deadline=Date.now()+120000;
    while(task.status==='Running'&&Date.now()<deadline){await new Promise(resolve=>setTimeout(resolve,50));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}
    assert.equal(task.status,'Completed',task.error);
    await command('five.refresh');await command('five.tool.adjust');await idle();
    const generatedIds=await page.evaluate(()=>fixture.view.fiveGeneratedPreview.group.children.filter(c=>c.userData.cutDirectionId).map(c=>c.userData.cutDirectionId));
    assert.deepEqual([...new Set(generatedIds)].sort(),source.intentToolpaths.filter(c=>c.enabled!==false&&c.machiningKind==='Cut').map(c=>c.id).sort(),'generated cutting paths retain all direction arrows');
    await page.evaluate(()=>{const v=fixture.view.viewport,c=v.getCameraState();v.setCameraState({...c,theta:-1.2,phi:2.5,radius:c.radius*.8});});
    await command('five.display.numbers');
    const generatedNumbers=await verifyNumbers(true);
    await page.screenshot({path:resolve(output,'all-cut-directions-and-keypoints.png')});
    await command('five.tool.manual-order');
    // Fixed-anchor labels can overlap at fit zoom. Click an exposed number as
    // a user would; never force a pointer through another visible caption.
    const pickedId=await page.locator('.five-path-number[data-five-order-id]').evaluateAll(nodes=>nodes.find(n=>{const r=n.getBoundingClientRect();return n.textContent!=='1'&&!n.hidden&&n.contains(document.elementFromPoint((r.left+r.right)/2,(r.top+r.bottom)/2));})?.dataset.fiveOrderId);
    assert.ok(pickedId);await page.locator(`.five-path-number[data-five-order-id="${pickedId}"]`).click();
    assert.equal(await page.locator(`.five-path-number[data-five-order-id="${pickedId}"]`).innerText(),'1','anchored number remains clickable for manual sorting');
    assert.equal(await page.evaluate(()=>fixture.view.viewport.specificationLeaderSvg.children.length),0);
    await command('five.order.close');
    writeFileSync(resolve(output,'path-numbers-report.json'),JSON.stringify({passed:true,nominal:nominalNumbers,generated:generatedNumbers,manualOrderClick:true,hideToggle:true},null,2));
    await rpc({action:'export-project',path:resolve(output,'perforated-cover.i5cam')});
    await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
    const reopened=await rpc({action:'save-reopen'}),axes=await invoke('CamCut.ReadToolAxisSamples',{expected:reopened.expected});
    assert.deepEqual(axes.errors,[]);assert.equal(axes.cuts.length,axisDisplay.cuts.length);
    writeFileSync(resolve(output,'generated-directions-report.json'),JSON.stringify({passed:true,operations:axisDisplay.cuts.length,directionCount:generatedIds.length,reopenedAxes:axes.cuts.length},null,2));
  }
  writeFileSync(resolve(output,'preview-report.json'),JSON.stringify({passed:true,cases:['all enabled cuts show direction arrows with selection color only on selected cuts','all enabled cuts load system/manual tool-axis keypoints without errors','new action in left tree with no duplicate ribbon entries','existing extraction dialog opens and cancels without changing paths','canvas and tree nodes retained','GPU arrow pixels stay small across 25x zoom and both projections','native reversal flips marker direction; undo restores paths'],cutCount:axisDisplay.cuts.length,directionCount:directionRows.length,samples,reversed,native:true,browser:'Edge',standaloneCef:false},null,2));
  if(!process.env.ICAX_DIRECTION_DISPLAY_ONLY)await verifyRectangleSelection({page,rpc,output});
  if(process.env.ICAX_BRACKET_FACE)await verifyStandardHoleSplitting({page,rpc,output});
}
