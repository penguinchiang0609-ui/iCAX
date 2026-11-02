import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {cutPolicyDraft,processDraftDefaults} from '../../apps/five-axis-cam/webpage/processPolicyDraft.mjs';

export async function prepareJobSetup(invoke,read,placement,index=0){
  const request=async()=>({commandId:crypto.randomUUID(),expected:(await read()).expected,setupId:placement.setupId});
  const wait=async task=>{while(task.status==='Running'){await new Promise(r=>setTimeout(r,30));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',task.error);return task;};

  const state=await read(),setup=state.setups.find(s=>s.id===placement.setupId),workpiece=state.workpieces.find(w=>w.id===placement.workpieceId),digest=crypto.randomUUID();
  const sources=await invoke('CamCut.QuerySources',{expected:state.expected,setupId:setup.id,placementId:placement.id});
  const task=await wait(await invoke('CamCut.ExtractCandidates',{...await request(),placementId:placement.id,sourceGeometry:workpiece.geometryRef,extractionRuleRef:setup.planningProfileRef,selection:{mode:'Faces',faceUses:[sources.faceUses[index % sources.faceUses.length]]},geometryOnly:true,representation:'SkinSurface',workpieceFamily:'SheetSurface',side:{outwardSign:1},requestedKinds:['GeneralContour','Circle','Rectangle','Slot'],draftId:crypto.randomUUID(),draftRevision:'1',draftDigest:digest}));
  const candidates=await invoke('CamCut.QueryCandidates',{epoch:task.epoch,candidateSet:task.result.candidateSet,pageSize:'100'});
  const ready=candidates.candidates.filter(c=>c.confirmationEligibility==='Ready');
  assert.ok(ready.length,JSON.stringify(candidates));
  const policy=await invoke('CamProcess.Prepare',{...await request(),placementId:placement.id,...cutPolicyDraft(processDraftDefaults)});
  const created=await invoke('CamCut.Create',{...await request(),placementId:placement.id,candidateSet:task.result.candidateSet,candidateIds:[ready[index%ready.length].candidateId],draftDigest:digest,decisions:[],process:policy.process,pose:policy.pose});
  await wait(await invoke('CamPlan.Generate',await request()));
  return created.createdIntentIds;
}
export async function verifyJobSequence({page,command,read,invoke,output,standalone=false}){
  const cases=[],initial=await read(),first=initial.placements[0];
  await prepareJobSetup(invoke,read,first);
  let state=await read();const firstGeometry=state.setups.find(s=>s.id===first.setupId).generatedGeometry;
  const created=await invoke('CamSetup.Manage',{commandId:crypto.randomUUID(),expected:state.expected,placementId:first.id,action:'create',name:'翻面装夹'});
  state=await read();const second=state.placements.find(p=>p.id===created.placementId);
  await prepareJobSetup(invoke,read,second,1);
  state=await read();assert.deepEqual(state.setups.find(s=>s.id===first.setupId).generatedGeometry,firstGeometry);
  assert.equal(state.intentToolpaths.filter(c=>c.setupId===first.setupId).length,1);assert.equal(state.intentToolpaths.filter(c=>c.setupId===second.setupId).length,1);
  // Resolve actual model features for each setup. Different P1/P2/P3 order
  // makes leakage through the shared workpiece visible in UI and persistence.
  const catalog=await invoke('CamScene.ReadSnapFeatures',{expected:state.expected,setupId:first.setupId,objectId:first.id});
  const features=catalog.features.filter(f=>['endpoint','center','datum-point'].includes(f.mode)&&Array.isArray(f.point));
  const picked=[];
  for(const feature of features){
    const p=feature.point;
    if(picked.length===1&&Math.hypot(...p.map((v,i)=>v-picked[0].point[i]))<1)continue;
    if(picked.length===2){
      const a=p.map((v,i)=>v-picked[0].point[i]),b=picked[1].point.map((v,i)=>v-picked[0].point[i]);
      if(Math.hypot(a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])<1)continue;
    }
    picked.push(feature);if(picked.length===3)break;
  }
  assert.equal(picked.length,3);
  const queries=picked.map(f=>({frame:'Workpiece',mode:f.mode,featureId:f.featureId}));
  const plans=[];
  for(const [i,placement] of [first,second].entries()){
    plans.push(await invoke('CamSetup.SetRegistrationPlan',{commandId:crypto.randomUUID(),expected:(await read()).expected,placementId:placement.id,queries:i?[...queries].reverse():queries}));
    const exported=await invoke('CamSetup.ExportRegistrationPlan',{expected:(await read()).expected,setupId:placement.setupId});
    const document=JSON.parse(exported.content);
    assert.equal(document.setupId,placement.setupId);assert.equal(document.coordinateFrame,'Setup');
    assert.deepEqual(document.points,plans[i].points);
  }
  state=await read();
  assert.deepEqual(state.setups.find(s=>s.id===first.setupId).registrationPlan,plans[0]);
  assert.deepEqual(state.setups.find(s=>s.id===second.setupId).registrationPlan,plans[1]);
  assert.notDeepEqual(plans[0].points,plans[1].points);
  assert.deepEqual(state.workpieces,initial.workpieces);
  cases.push('Two setups of one workpiece store and export separate three-point locating plans in their own Setup frame; shared workpiece data stays unchanged');
  await assert.rejects(async()=>invoke('CamSetup.ExportProgram',{expected:(await read()).expected}),/NOT_READY/);
  await command('five.refresh');
  for(const [i,placement] of [first,second].entries()){
    await command('five.job.open');await command(`five.job.edit:${placement.setupId}`);
    await command('five.area.fixture');await command('five.tool.registration');
    assert.equal(await page.locator('.cam-info-pane [data-five-locating-field]').count(),0);
    assert.equal(await page.locator('[data-five-floating="registration"]').count(),1);
    const measured=plans[i].points.map(p=>[100+i*400-p.point[1],200+p.point[0],300+p.point[2]]);
    for(let k=0;k<3;k++)for(let j=0;j<3;j++)await page.getByRole('textbox',{name:`P${k+1} 机床 ${'XYZ'[j]}`,exact:true}).fill(String(measured[k][j]));
    // Re-rendering on typing must retain the live editor and its selection.
    const editor=page.getByRole('textbox',{name:'P3 机床 Z',exact:true});
    await editor.evaluate(node=>{fixture.machineEditor=node;node.focus({preventScroll:true});node.setSelectionRange(1,2);node.dispatchEvent(new Event('input',{bubbles:true}));});
    assert.equal(await editor.evaluate(node=>node===fixture.machineEditor&&node===document.activeElement&&node.selectionStart===1&&node.selectionEnd===2),true);
    if(!standalone&&i===0){
      const style=await page.addStyleTag({content:'[data-five-key="locating-dialog"] .five-position-body{max-height:360px!important;overflow:auto}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1600px}'});
      await page.evaluate(()=>{fixture.hold=true;fixture.holdMethod='CamSetup.SetRegistrationPlan';});
      const pending=page.getByRole('button',{name:'保存并计算定位',exact:true}).click();
      await page.waitForFunction(()=>fixture.held);
      const positions=await editor.evaluate(node=>{
        node.focus({preventScroll:true});node.setSelectionRange(1,2);
        fixture.machineCanvas=fixture.view.viewport.renderer.domElement;
        return ['.cam-context-pane','.cam-info-pane','[data-five-key="locating-dialog"] .five-position-body'].map((selector,j)=>{const el=document.querySelector(selector);el.scrollTop=41+j*19;return el.scrollTop;});
      });
      await page.evaluate(()=>fixture.release());await pending;await page.waitForFunction(()=>!fixture.view.pending);
      assert.equal(await editor.evaluate(node=>node===fixture.machineEditor&&node===document.activeElement&&node.selectionStart===1&&node.selectionEnd===2&&fixture.machineCanvas===fixture.view.viewport.renderer.domElement),true);
      assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','[data-five-key="locating-dialog"] .five-position-body'].map(s=>document.querySelector(s).scrollTop)),positions);
      await style.evaluate(n=>n.remove());
      cases.push('Delayed machine registration save preserves input node, focus, selection, latest sidebars/dialog scroll and canvas');
    }else {await page.getByRole('button',{name:'保存并计算定位',exact:true}).click();await page.waitForFunction(()=>!fixture.view.pending);}
    assert.equal(await page.evaluate(()=>fixture.view.error),'');
    plans[i]=(await read()).setups.find(s=>s.id===placement.setupId).registrationPlan;
    assert.equal(plans[i].machineStatus,'Qualified');assert.deepEqual(plans[i].machineRegistration.measuredPoints,measured);
    assert.ok(plans[i].machineRegistration.maximumResidualMm<1e-7);
    const exported=JSON.parse((await invoke('CamSetup.ExportRegistrationPlan',{expected:(await read()).expected,setupId:placement.setupId})).content);
    assert.deepEqual(exported.machineRegistration,plans[i].machineRegistration);
    if(i===1)await page.screenshot({path:resolve(output,'machine-locating.png')});
    await command('five.locating.close');
  }
  assert.deepEqual((await read()).workpieces,initial.workpieces);
  assert.deepEqual((await read()).setups.find(s=>s.id===first.setupId).generatedGeometry,firstGeometry);
  cases.push('Real coordinate editors fit separate rotated/translated machine frames per setup, preserve focus/selection while typing, and retain CAD and generated local paths');
  await command('five.refresh');await command('five.area.toolpath');
  await command('five.job.open');
  assert.equal(await page.getByRole('dialog',{name:'装夹工序与输出'}).isVisible(),true);
  if(!standalone){
    const style=await page.addStyleTag({content:'.five-job-dialog .five-position-body{max-height:120px!important;overflow:auto}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1600px}'});
    await page.evaluate(()=>{fixture.hold=true;fixture.holdMethod='CamSetup.SetSequence';});
    const pending=page.locator(`[data-five-command="five.job.up:${second.setupId}"]`).click();
    await page.waitForFunction(()=>fixture.held);
    const positions=await page.evaluate(()=>{
      fixture.jobFocus=document.querySelector('[data-five-command="five.job.close"]');fixture.jobCanvas=fixture.view.viewport.renderer.domElement;
      fixture.jobFocus.focus({preventScroll:true});
      return ['.cam-context-pane','.cam-info-pane','.five-job-dialog .five-position-body'].map((s,i)=>{const node=document.querySelector(s);node.scrollTop=35+i*11;return node.scrollTop;});
    });
    await page.evaluate(()=>fixture.release());await pending;await page.waitForFunction(()=>!fixture.view.pending);
    assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-job-dialog .five-position-body'].map(s=>document.querySelector(s).scrollTop)),positions);
    assert.equal(await page.evaluate(()=>document.activeElement===fixture.jobFocus&&fixture.jobCanvas===fixture.view.viewport.renderer.domElement),true);
    await style.evaluate(n=>n.remove());
    cases.push('Asynchronous sequence commit preserves current focus, canvas node and both sidebars plus nested dialog scroll');
  }else await page.locator(`[data-five-command="five.job.up:${second.setupId}"]`).click();
  await page.waitForFunction(()=>!fixture.view.pending);state=await read();
  assert.deepEqual(state.setups.map(s=>s.id),[second.setupId,first.setupId]);
  assert.deepEqual(state.setups[1].generatedGeometry,firstGeometry);
  await page.screenshot({path:resolve(output,'job-order.png')});
  cases.push('Two setups share real CAD geometry but own independent cuts and generated poses; changing task order preserves local toolpaths');
  const target=resolve(output,'two-setup.cl');
  await page.evaluate(({path,standalone})=>{(standalone?(globalThis.icax??globalThis.icaxNativeBridge):fixture.context.appProxy.bridge).saveFileDialog=async()=>path;},{path:target,standalone});
  await page.getByRole('button',{name:'输出切割点 XYZ/IJK',exact:true}).click();await page.waitForFunction(()=>!fixture.view.pending);
  assert.equal(await page.evaluate(()=>fixture.view.fiveJobError),'');
  const program=readFileSync(target,'utf8');
  assert.equal(program.split('\nM00\n').length-1,1);
  assert.ok(program.indexOf(second.setupId)<program.indexOf(first.setupId));
  assert.equal(program.split('\nFROM/').length-1,2);
  assert.ok(program.includes('POSTPROCESSING REQUIRED'));assert.ok(program.includes('GOTO/'));
  assert.ok(program.split('\nM00\n')[1].indexOf('FRAME/SETUP')<program.split('\nM00\n')[1].indexOf('FROM/'));
  assert.ok(program.includes('REFERENCE/CUTPOINT'));assert.ok(!program.includes('\nRAPID\n'));
  const outputSetups=await page.evaluate(()=>fixture.view.scene.domain.setups);
  for(const [i,placement] of [first,second].entries()){
    const setup=outputSetups.find(s=>s.id===placement.setupId),op=setup.generatedGeometry.operations[0],part=op.parts[0],matrix=placement.transform;
    const apply=(p,vector)=>[0,1,2].map(j=>matrix[j*4]*p[0]+matrix[j*4+1]*p[1]+matrix[j*4+2]*p[2]+(vector?0:matrix[j*4+3]));
    let axis=apply(part.directions[0],true);const length=Math.hypot(...axis);axis=axis.map(v=>v/length);
    const wanted=[...apply(part.points[0],false),...axis];
    const actual=program.slice(program.indexOf(placement.setupId)).match(/FROM\/([^\n]+)/)[1].split(',').map(Number);
    actual.forEach((v,j)=>assert.ok(Math.abs(v-wanted[j])<1e-7,`${i}/${j}: ${v} != ${wanted[j]}`));
  }
  cases.push('Output uses cutting points and unit IJK in each Setup frame without follow-height offsets, in saved setup order with one M00; file readback and analytic coordinates verified');
  const localCut=state.intentToolpaths.find(c=>c.setupId===second.setupId);
  await invoke('CamCut.Reverse',{commandId:crypto.randomUUID(),expected:(await read()).expected,intentIds:[localCut.id]});
  assert.equal((await read()).setups.find(s=>s.id===first.setupId).generatedGeometry.status,'Current');
  await assert.rejects(()=>invoke('CamSetup.ExportProgram',{expected:state.expected,destination:target,coordinateFrame:'Setup',referencePoint:'CutPoint'}));
  await assert.rejects(async()=>invoke('CamSetup.ExportProgram',{expected:(await read()).expected,destination:target,coordinateFrame:'Setup',referencePoint:'CutPoint'}),/NOT_READY/);
  assert.equal(readFileSync(target,'utf8'),program);
  let task=await invoke('CamPlan.Generate',{commandId:crypto.randomUUID(),expected:(await read()).expected,setupId:second.setupId});
  while(task.status==='Running'){await new Promise(r=>setTimeout(r,30));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}
  assert.equal(task.status,'Completed',task.error);
  await command('five.refresh');
  await invoke('CamSetup.ExportProgram',{expected:(await read()).expected,destination:target,coordinateFrame:'Setup',referencePoint:'CutPoint'});
  cases.push('Editing the second setup leaves the first setup current; stale snapshot and stale toolpaths block whole-job output atomically without overwriting the existing file');
  await page.getByRole('button',{name:'按工序顺序仿真',exact:true}).click();
  await page.waitForFunction(()=>fixture.view.fiveJobPlayback?.index===0&&fixture.view.fiveSimulation?.frames?.length>0&&!fixture.view.pending);
  await command('five.simulation.pause');
  assert.equal((await read()).activeSetupId,second.setupId);
  assert.equal(await page.evaluate(()=>new Set(fixture.view.fiveSimulation.frames.map(f=>f.intentId)).size),1);
  // Use the real scrubber and single-step controls to reach the boundary;
  // no geometry, trajectory or native validation result is mocked.
  const last=await page.evaluate(()=>fixture.view.fiveSimulation.frames.length-1);
  const seek=page.locator('[data-five-simulation-field="seek"]');
  await seek.fill(String(last-1));await seek.dispatchEvent('input');await command('five.simulation.next');
  await page.waitForFunction(()=>fixture.view.fiveJobPlayback?.boundary);
  assert.equal((await read()).activeSetupId,second.setupId);
  await page.screenshot({path:resolve(output,'reclamp-boundary.png')});
  await page.getByRole('button',{name:'确认换装夹，继续预览',exact:true}).click();
  await page.waitForFunction(()=>fixture.view.fiveJobPlayback?.index===1&&!fixture.view.pending&&fixture.view.fiveSimulation?.frames?.length>0);
  await command('five.simulation.pause');assert.equal((await read()).activeSetupId,first.setupId);
  assert.equal(await page.evaluate(()=>new Set(fixture.view.fiveSimulation.frames.map(f=>f.intentId)).size),1);
  await command('five.job.stop');
  for(const [i,placement] of [first,second].entries()){
    await command('five.job.open');await command(`five.job.edit:${placement.setupId}`);
    await command('five.area.fixture');await command('five.tool.registration');
    await page.getByRole('dialog',{name:'三点定位',exact:true}).waitFor();
    assert.deepEqual(await page.evaluate(()=>fixture.view.fiveLocating.points.map(p=>p.point)),plans[i].points.map(p=>p.modelPoint));
    assert.ok(await page.getByRole('dialog',{name:'三点定位',exact:true}).getByText(state.setups.find(s=>s.id===placement.setupId).name,{exact:true}).count());
    await command('five.locating.close');
  }
  cases.push('Whole-job preview activates setups in saved order, stops at M00 until explicit continue, replaces the previous setup trajectory and never interpolates across setups');
  return cases;
}

