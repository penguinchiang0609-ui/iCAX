import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { selectWorkpieceFile } from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyRoundedCoverRapid({page,rpc,root,output,calls,errors}) {
  const source=resolve(root,'TestData/FA/刀路/刀路/整理测试刀路/圆弧多义线盖-无孔-9696.stp.STEP');
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  await selectWorkpieceFile(page,source);await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  await page.waitForFunction(()=>fixture.view.viewport.sceneObjects.size===1);
  const faces=await page.evaluate(async()=>{
    const d=fixture.view.scene.domain,p=d.placements[0],v=fixture.view.viewport,m=[...v.sceneObjects.values()][0],pos=m.geometry.getAttribute('position'),index=m.geometry.index;
    const map=await fixture.context.sceneProxy.invoke('CamPlacement.ReadReferenceFaces',{expected:d.expected,setupId:p.setupId,placementId:p.id});
    fixture.coverFaceMap=map;
    return map.faces.map(f=>{const min=[Infinity,Infinity,Infinity],max=[-Infinity,-Infinity,-Infinity];
      for(let i=Number(f.first)*3;i<(Number(f.first)+Number(f.count))*3;++i){const n=index?index.getX(i):i;for(let j=0;j<3;++j){const x=pos.array[n*3+j];min[j]=Math.min(min[j],x);max[j]=Math.max(max[j],x);}}
      return {...f,min,max};});
  });writeFileSync(resolve(output,'faces.json'),JSON.stringify(faces,null,2));
  await page.evaluate(async()=>{
    window.coverExtraction=await import('/src/apps/five-axis-cam/webpage/extractionActions.mjs');
    await fixture.context.actions.selectRibbonTab('toolpath');
    await fixture.entry.handleRibbonCommand(fixture.context,'five.tool.extract');
    const d=fixture.view.scene.domain,p=d.placements[0];
    const sources=await fixture.context.sceneProxy.invoke('CamCut.QuerySources',{expected:d.expected,setupId:p.setupId,placementId:p.id});
    const extraction=coverExtraction.extractionDraft(fixture.view).extraction;
    extraction.mode='Faces';extraction.faces=sources.faceUses.filter(f=>Number(f.localId)>=53);
    coverExtraction.extractionChanged(fixture.view);
    await fixture.entry.handleRibbonCommand(fixture.context,'five.extraction.run');
  });await idle();
  const extracted=await page.evaluate(()=>({error:fixture.view.error,result:coverExtraction.extractionResult(fixture.view)}));
  writeFileSync(resolve(output,'extraction.json'),JSON.stringify(extracted,null,2));
  await page.screenshot({path:resolve(output,'cover-extracted.png')});
  assert.equal(extracted.error,'');
  assert.equal(extracted.result.candidates.length,1);
  assert.ok(Math.abs(extracted.result.candidates[0].boundary.perimeterMm-138.751058358721)<1e-7);
  await page.locator('[data-five-command="five.extraction.confirm"]').click();await idle();
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload}),read=()=>invoke('CamScene.ReadState');
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const before=await read();assert.equal(before.intentToolpaths.length,1);
  const cut=before.intentToolpaths[0],id=cut.id;
  await page.locator(`[data-five-intent="${id}"]`).click();await command('five.points.edit');
  const pointsIdle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisPointLookup&&!fixture.view.fiveAxisPointEdit?.loading);
  await pointsIdle();
  await page.getByRole('combobox',{name:'姿态表示方式',exact:true}).selectOption('IJK');
  await page.getByRole('combobox',{name:'关键点刀轴坐标系',exact:true}).selectOption('Workpiece');
  const edits=[];
  await command('five.points.add');
  for(const fraction of [.3,.65]){
    const pick=await page.evaluate(({id,fraction})=>{
      const v=fixture.view.viewport,c=fixture.view.scene.intentToolpaths.find(c=>c.id===id),o=v.sceneObjects.get(c.placementId),box=v.renderer.domElement.getBoundingClientRect();
      for(let i=Math.floor(c.previewPoints.length*fraction);i<c.previewPoints.length-1;++i){
        const p=c.previewPoints[i].map((x,j)=>(x+c.previewPoints[i+1][j])/2),q=o.localToWorld(v.camera.position.clone().set(...p)).project(v.camera);
        const x=box.x+(q.x+1)*box.width/2,y=box.y+(1-q.y)*box.height/2;
        if(document.elementFromPoint(x,y)===v.renderer.domElement)return {x,y};
      }throw Error('No visible cutting point');
    },{id,fraction});
    await page.mouse.click(pick.x,pick.y);await pointsIdle();
    assert.equal(await page.evaluate(()=>!!fixture.view.fiveAxisPointInsert&&!fixture.view.fiveAxisPointEdit.newPoint),true);
    const chosen=await page.evaluate(()=>{const d=fixture.view.fiveAxisPointEdit;return d.newPoint||d.points.find(p=>p.id===d.pointId);});
    const direction=await page.evaluate(()=>fixture.view.fiveAxisPointEdit.axis.direction);
    direction[0]+=.1;direction[1]+=.05;const norm=Math.hypot(...direction);direction.forEach((x,i)=>direction[i]=x/norm);
    for(const [i,name] of ['I','J','K'].entries())await page.getByRole('textbox',{name:`关键点 ${name}`,exact:true}).fill(String(direction[i]));
    await page.locator('[data-five-command="five.points.apply"]').click();await pointsIdle();
    assert.equal(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.error),'');
    edits.push({anchor:chosen.anchor,direction});
  }
  await page.screenshot({path:resolve(output,'cover-local-points.png')});
  await page.getByRole('tab',{name:'空移',exact:true}).click();
  const generationCalls=calls.filter(c=>c.method==='CamPlan.Generate').length;
  await page.evaluate(()=>{fixture.holdMethod='CamPlan.Generate';fixture.hold=true;});
  await page.locator('[data-five-command="five.tool.generate"]').click();
  await page.waitForFunction(()=>fixture.held);
  assert.match(await page.getByRole('dialog',{name:'生成空移',exact:true}).innerText(),/1 条切割路径 · 无需工序间空移/);
  await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const state=await read(),g=state.setups.find(s=>s.id===state.activeSetupId).generatedGeometry;
  assert.equal(g.status,'Current');assert.equal(g.operations.length,1);assert.equal(g.transitions.length,0);
  assert.ok(calls.filter(c=>c.method==='CamPlan.Generate').length>generationCalls,'single-path request still calculates missing cutting geometry');
  assert.equal(calls.some(c=>c.method?.startsWith('CamRapid.Generate')),false,'no inter-operation travel is fabricated');
  for(const edit of edits){let hits=0;
    for(const part of g.operations[0].parts)for(const [i,s] of (part.sourceSamples||[]).entries()){
      if(s.sourceSpanId===edit.anchor.sourceSpanId&&Math.abs(s.nativeParameter-edit.anchor.nativeParameter)<1e-10){
        assert.ok(Math.hypot(...part.directions[i].map((x,j)=>x-edit.direction[j]))<1e-8);hits++;
      }
    }assert.ok(hits>0,'exact edited anchor and direction survive generation');
  }
  assert.match(await page.locator('.five-empty-row').first().innerText(),/无需工序间空移/);
  assert.doesNotMatch(await page.locator('body').innerText(),/包络检查 0\/0|CURVE_SAMPLE_FAILED/);
  assert.deepEqual(state.workpieces,before.workpieces);
  assert.deepEqual(state.intentToolpaths[0].previewPoints,cut.previewPoints);
  await page.screenshot({path:resolve(output,'cover-generated.png')});
  const generatedCount=calls.filter(c=>c.method==='CamPlan.Generate').length;
  await page.locator('[data-five-command="five.tool.generate"]').click();await idle();
  assert.equal(calls.filter(c=>c.method==='CamPlan.Generate').length,generatedCount,'current single-path result is reused');
  await rpc({action:'export-project',path:resolve(output,'rounded-cover-generated.i5cam')});
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  const reopened=await rpc({action:'save-reopen'});
  assert.deepEqual(reopened.setups.find(s=>s.id===reopened.activeSetupId).generatedGeometry,g);
  assert.deepEqual(errors,[]);
  writeFileSync(resolve(output,'report.json'),JSON.stringify({passed:true,source,edits,generated:g,
    cases:['one actual lower-rim cut','two local vectors edited through UI','generate-rapid button calculates cutting geometry','zero transitions without false sampling error','repeated generation reuses current result','save/reopen preserves exact geometry'],
    coverage:'Production file picker, native Scene SDO and View resources, native exterior face-use selection, real Edge controls; standalone CEF not automated'},null,2));
}
