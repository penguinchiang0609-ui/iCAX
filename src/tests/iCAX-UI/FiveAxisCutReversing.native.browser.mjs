import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyCutReversing({page,rpc,calls,output}){
  console.log('Reverse: automotive closed contour and split open curves');
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState');
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  let scene=await read();
  const definitions=(await invoke('CamCut.Read',{expected:scene.expected,intentIds:scene.intentToolpaths.map(c=>c.id)})).items;
  const closed=scene.intentToolpaths.filter(c=>c.cutType==='Contour'&&c.geometryKind==='GeneralContour'&&definitions.find(d=>d.intentId===c.id)?.closed).sort((a,b)=>b.previewPoints.length-a.previewPoints.length)[0];
  const open=scene.intentToolpaths.filter(c=>c.split&&!definitions.find(d=>d.intentId===c.id)?.closed);
  assert.ok(closed&&open.length>=2,'real closed spatial contour and split open curves');
  const ids=[closed.id,...open.map(c=>c.id)];
  const none={kind:'None',placement:'ScrapSide',lengthMm:0,angleRad:0};
  await invoke('CamProcess.Update',{commandId:crypto.randomUUID(),expected:scene.expected,intentIds:ids,patch:{leadIn:none,leadOut:none,overcutLengthMm:0,gapLengthMm:0}});
  scene=await read();
  await invoke('CamProcess.Update',{commandId:crypto.randomUUID(),expected:scene.expected,intentIds:[closed.id],patch:{gapLengthMm:1,leadOut:{...none,kind:'Line',lengthMm:.02}}});
  await command('five.refresh');await command('five.tool.extract');await command('five.extraction.close');
  await page.locator(`[data-five-intent="${closed.id}"]`).click();
  for(const cut of open)await page.locator(`[data-five-intent="${cut.id}"]`).click({modifiers:['Control']});
  assert.equal(await page.locator('[data-five-intent][aria-pressed="true"]').count(),ids.length);
  const before=await read(),preview=(await invoke('CamProcess.PreviewLeads',{expected:before.expected,intentIds:ids})).items;
  const arrows=()=>page.evaluate(()=>fixture.view.fiveCutPreview.group.children.filter(n=>n.userData.cutDirectionId).map(n=>n.userData));
  const forwardArrows=await arrows();assert.equal(forwardArrows.length,ids.length*3);
  await page.evaluate(() => fixture.view.viewport.fitView());
  await page.screenshot({path:resolve(output,'automotive-direction-before.png')});
  // Drop the mutation response and the first receipt response. Retry must reuse
  // the same command and cannot toggle the cuts for a second time.
  const prior=calls.filter(c=>c.method==='CamCut.Reverse').length;
  await page.evaluate(()=>{
    fixture.loseMethod='CamCut.Reverse';fixture.reverseCanvas=document.querySelector('canvas');
    const proxy=fixture.context.sceneProxy,original=proxy.invoke.bind(proxy);
    proxy.invoke=async(method,payload)=>{if(method==='CamScene.QueryCommand'){proxy.invoke=original;throw Error('Injected lost receipt');}return original(method,payload);};
  });
  await command('five.tool.reverse');await idle();
  assert.match(await page.evaluate(()=>fixture.view.error),/尚未确认/);
  assert.equal(await page.getByRole('dialog',{name:'切割方向'}).count(),0);
  assert.equal(await page.evaluate(()=>fixture.reverseCanvas===document.querySelector('canvas')),true);
  await command('five.tool.reverse');await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const requests=calls.filter(c=>c.method==='CamCut.Reverse').slice(prior);
  assert.equal(requests.length,2);assert.equal(requests[0].payload.commandId,requests[1].payload.commandId);
  const after=await read(),reversed=(await invoke('CamProcess.PreviewLeads',{expected:after.expected,intentIds:ids})).items;
  for(const id of ids){
    const a=before.intentToolpaths.find(c=>c.id===id),b=after.intentToolpaths.find(c=>c.id===id);
    const p=preview.find(c=>c.intentId===id),q=reversed.find(c=>c.intentId===id);
    assert.equal(b.reversed,!a.reversed);
    for(const key of ['curveRef','processRef','poseRef','retainedSide','sourceKey'])assert.deepEqual(b[key],a[key],key);
    if(id===closed.id){assert.deepEqual(b.startPoint,a.startPoint);assert.notDeepEqual(q.end,p.end);assert.ok(q.end.every((x,k)=>Math.abs(x-q.leads[0].anchor[k])<1e-7));}
    else {assert.deepEqual(q.start,p.end);assert.deepEqual(q.end,p.start);assert.deepEqual(b.startPoint,q.start);}
  }
  const reverseArrows=await arrows();assert.equal(reverseArrows.length,forwardArrows.length);
  reverseArrows.forEach((a,i)=>a.direction.forEach((x,k)=>assert.ok(Math.abs(x+forwardArrows[i].direction[k])<1e-10)));
  await page.screenshot({path:resolve(output,'automotive-direction-after.png')});
  await rpc({action:'undo'});await command('five.refresh');assert.deepEqual((await read()).intentToolpaths,before.intentToolpaths);
  await rpc({action:'redo'});await command('five.refresh');assert.deepEqual((await read()).intentToolpaths,after.intentToolpaths);
  // A pending reverse may finish after the user changes page; it must not
  // switch back or steal the current page's search focus.
  await page.evaluate(()=>{fixture.holdMethod='CamCut.Reverse';fixture.hold=true;void fixture.entry.handleRibbonCommand(fixture.context,'five.tool.reverse');});
  await page.waitForFunction(()=>fixture.held);
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('help'));
  await page.locator('[data-five-search]').fill('保留当前输入');
  await page.evaluate(()=>{document.querySelector('[data-five-search]').setSelectionRange(1,4);});
  await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await page.evaluate(()=>fixture.view.activeAreaId),'help');
  assert.equal(await page.locator('[data-five-search]').inputValue(),'保留当前输入');
  assert.equal(await page.evaluate(()=>document.activeElement?.hasAttribute('data-five-search')),true);
  assert.deepEqual(await page.evaluate(()=>[document.activeElement.selectionStart,document.activeElement.selectionEnd]),[1,4]);
  await page.locator('[data-five-search]').fill('');await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await command('five.refresh');
  const twice=(await invoke('CamProcess.PreviewLeads',{expected:(await read()).expected,intentIds:ids})).items;
  assert.deepEqual(twice,preview);
  const saved=await rpc({action:'save-reopen'});
  assert.deepEqual((await invoke('CamProcess.PreviewLeads',{expected:saved.expected,intentIds:ids})).items,preview);
  writeFileSync(resolve(output,'reverse-report.json'),JSON.stringify({passed:true,ids,cases:['direct ribbon command without dialog or persistent tool state','batch closed spatial and open split reversal','open endpoints swap; closed anchor and retained side persist','existing gap and lead-out rebuilt','visible arrows flip','uncertain receipt retries exactly once','undo redo and embedded reopen','late response preserves current page and focus'],coverage:{native:true,browser:'Edge',standaloneCef:false}},null,2));
}
