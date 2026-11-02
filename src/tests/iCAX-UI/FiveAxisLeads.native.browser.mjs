import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyLeads({page,rpc,calls,output}){
  page.setDefaultTimeout(30000);
  console.log('Leads: open');
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState');
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const ready=()=>page.waitForFunction(()=>fixture.view.fiveLeads?.loaded&&!fixture.view.fiveLeads.previewBusy);
  const preview=length=>page.waitForFunction(length=>fixture.view.fiveLeads?.preview?.items[0]?.leads.find(l=>l.role==='LeadIn')?.lengthMm===length&&!fixture.view.fiveLeads.previewBusy,length);
  const field=name=>page.locator(`[data-five-lead-field="${name}"]`);
  await command('five.tool.leads');
  const before=await read(),cuts=before.intentToolpaths.filter(c=>c.previewPoints.length>10).sort((a,b)=>b.previewPoints.length-a.previewPoints.length).slice(0,2);assert.equal(cuts.length,2);
  await page.locator(`[data-five-intent="${cuts[0].id}"]`).click();await ready();
  assert.equal(await page.locator('.five-position-dialog[aria-label="引线"]').count(),1);
  assert.equal(await page.locator('[data-five-command="five.process.compile"]').count(),0);
  const beforePolicies=await invoke('CamProcess.Read',{expected:before.expected,intentIds:cuts.map(c=>c.id)});
  await field('entry.kind').selectOption('Line');await field('entry.length').fill('4');await preview(4);
  await field('exit.kind').selectOption('Arc');await field('exit.length').fill('2');await field('exit.radius').fill('3');
  await page.waitForFunction(()=>fixture.view.fiveLeads?.preview?.items[0]?.leads.length===2);
  assert.equal(await page.evaluate(()=>fixture.view.fiveLeads.error),'');
  assert.deepEqual(await read(),before,'draft preview does not change scene or resources');
  const exact=await page.evaluate(()=>fixture.view.fiveLeads.preview.items[0].leads);
  for(const lead of exact){assert.ok(lead.anchor.every((v,i)=>Math.abs(v-(lead.role==='LeadIn'?lead.points.at(-1):lead.points[0])[i])<1e-8));assert.ok(lead.points.every(p=>p.every(Number.isFinite)));}
  // Out-of-order preview, latest focus/selection, both pane scrolls, stable nodes/listeners.
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.five-position-body::after {content:"";display:block;height:1100px;flex-shrink:0}.five-position-body{max-height:430px;overflow:auto}'});
  await page.evaluate(()=>{fixture.holdMethod='CamProcess.PreviewLeads';fixture.hold=true;});
  await field('entry.length').fill('2.5');await page.waitForFunction(()=>fixture.held);
  await field('entry.length').fill('3.75');await preview(3.75);
  const scroll=await page.evaluate(()=>{
    const input=document.querySelector('[data-five-lead-field="entry.length"]');fixture.leadInput=input;fixture.leadCanvas=document.querySelector('canvas');fixture.leadListenerCount=0;
    input.addEventListener('input',()=>fixture.leadListenerCount++);input.focus({preventScroll:true});input.setSelectionRange(1,3);
    document.querySelector('.cam-context-pane').scrollTop=123;document.querySelector('.five-position-body').scrollTop=42;
    return [document.querySelector('.cam-context-pane').scrollTop,document.querySelector('.five-position-body').scrollTop];
  });
  await page.evaluate(()=>fixture.release());await page.waitForTimeout(150);
  const kept=await page.evaluate(()=>({input:fixture.leadInput===document.querySelector('[data-five-lead-field="entry.length"]'),canvas:fixture.leadCanvas===document.querySelector('canvas'),focus:document.activeElement===fixture.leadInput,selection:[fixture.leadInput.selectionStart,fixture.leadInput.selectionEnd],scroll:[document.querySelector('.cam-context-pane').scrollTop,document.querySelector('.five-position-body').scrollTop],length:fixture.view.fiveLeads.preview.items[0].leads[0].lengthMm}));
  assert.equal(kept.input,true);assert.equal(kept.canvas,true);assert.equal(kept.focus,true);assert.deepEqual(kept.selection,[1,3]);assert.deepEqual(kept.scroll,scroll);assert.equal(kept.length,3.75);
  await field('entry.length').fill('4');await preview(4);assert.ok(await page.evaluate(()=>fixture.leadListenerCount>0));await stress.evaluate(n=>n.remove());
  await page.evaluate(()=>{document.querySelector('.cam-context-pane').scrollTop=0;document.querySelector('.five-position-body').scrollTop=0;});
  await page.screenshot({path:resolve(output,'automotive-leads-preview.png')});
  // Closing while a read-only preview is delayed must not resurrect the dialog/overlay.
  await page.evaluate(()=>{fixture.hold=true;});await field('entry.length').fill('4.5');await page.waitForFunction(()=>fixture.held);
  await page.locator('[data-five-command="five.leads.close"]').last().click();await page.evaluate(()=>fixture.release());await page.waitForTimeout(150);
  assert.equal(await page.locator('[aria-label="引线"]').count(),0);assert.equal(await page.evaluate(()=>!!fixture.view.fiveLeads),false);assert.deepEqual(await read(),before);
  console.log('Leads: reopen after cancel');
  await command('five.tool.leads');await ready();
  assert.equal(await field('entry.kind').inputValue(),beforePolicies.items[0].assignment.leadIn.kind,'cancel restores saved parameters');
  console.log('Leads: batch');
  await page.locator(`[data-five-intent="${cuts[1].id}"]`).click({modifiers:['Control']});await ready();
  await field('entry.kind').selectOption('Line');await field('entry.length').fill('0.8');await field('entry.angle').fill('20');
  await field('exit.kind').selectOption('Arc');await field('exit.length').fill('0.3');await field('exit.radius').fill('0.5');
  await page.waitForFunction(()=>fixture.view.fiveLeads.preview?.items.length===2&&fixture.view.fiveLeads.preview.items.every(i=>i.leads.length===2)&&!fixture.view.fiveLeads.previewBusy);
  console.log('Leads: save');
  await page.evaluate(()=>{fixture.loseMethod='CamProcess.Update';});const count=calls.filter(c=>c.method==='CamProcess.Update').length;
  await page.locator('[data-five-command="five.leads.confirm"]').click();await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisLeadsOpen);
  assert.equal(calls.filter(c=>c.method==='CamProcess.Update').length,count+1,'receipt recovers lost response without duplicate save');
  const saved=await read(),ids=cuts.map(c=>c.id),policies=await invoke('CamProcess.Read',{expected:saved.expected,intentIds:ids});
  for(const item of policies.items){assert.equal(item.assignment.leadIn.lengthMm,0.8);assert.ok(Math.abs(item.assignment.leadIn.angleRad-20*Math.PI/180)<1e-12);assert.equal(item.assignment.leadOut.kind,'Arc');}
  await command('five.tool.reverse');
  await page.waitForFunction(ids=>ids.every(id=>fixture.view.fiveSavedLeads?.preview?.items.some(i=>i.intentId===id&&i.leads.length===2)),ids);
  const colors=await page.evaluate(ids=>{
    const lines=[];fixture.view.fiveLeadOverlay.group.traverse(n=>{if(n.isLine&&ids.includes(n.userData.intentId)&&['LeadIn','LeadOut'].includes(n.userData.leadRole))lines.push([n.userData.leadRole,n.material.color.getHex(),n.geometry.attributes.position.count,n.material.depthTest]);});return lines;
  },ids);
  assert.equal(colors.length,4,'saved leads remain visible after switching tools');
  for(const [role,color,count,depthTest] of colors){assert.equal(color,role==='LeadIn'?0x15a66b:0xff862b);assert.ok(count>=2);assert.equal(depthTest,false);}
  await page.screenshot({path:resolve(output,'automotive-leads-saved-visible.png')});
  await command('five.tool.leads');await ready();
  for(const cut of cuts){const after=saved.intentToolpaths.find(c=>c.id===cut.id);for(const name of ['curveRef','featureRef','poseRef','sourceRecipeRef'])assert.deepEqual(after[name],cut[name]);}
  await rpc({action:'undo'});let live=await read();assert.deepEqual((await invoke('CamProcess.Read',{expected:live.expected,intentIds:ids})).items,beforePolicies.items);
  await command('five.refresh');await page.waitForFunction(()=>fixture.view.fiveLeads?.preview&&!fixture.view.fiveLeads.previewBusy);
  assert.ok(await page.evaluate(()=>fixture.view.fiveLeads.preview.items.every(i=>i.leads.length===0)));
  await rpc({action:'redo'});live=await read();assert.deepEqual((await invoke('CamProcess.Read',{expected:live.expected,intentIds:ids})).items,policies.items);
  await command('five.refresh');await preview(0.8);
  const storedPreview=await invoke('CamProcess.PreviewLeads',{expected:live.expected,intentIds:ids});
  console.log('Leads: save-reopen');
  const reopened=await rpc({action:'save-reopen'});assert.deepEqual((await invoke('CamProcess.Read',{expected:reopened.expected,intentIds:ids})).items,policies.items);
  assert.deepEqual(await invoke('CamProcess.PreviewLeads',{expected:reopened.expected,intentIds:ids}),storedPreview,'native leads reproduce from embedded CAD and saved policies');
  await command('five.refresh');await command('five.tool.leads');await ready();await preview(0.8);
  assert.equal(await page.evaluate(()=>fixture.view.fiveLeads.error),'');await page.screenshot({path:resolve(output,'automotive-leads-reopened.png')});
  await page.evaluate(()=>{
    const v=fixture.view.viewport,item=fixture.view.fiveLeads.preview.items[0],p=v.camera.position.clone().set(...item.leads[0].anchor);
    fixture.view.viewport.sceneObjects.get(item.placementId).localToWorld(p);
    v.setCameraState({...v.getCameraState(),target:{x:p.x,y:p.y,z:p.z},radius:45});
  });
  await page.screenshot({path:resolve(output,'automotive-leads-detail.png')});

  writeFileSync(resolve(output,'leads-report.json'),JSON.stringify({passed:true,cases:['real spatial automotive boundary lead preview','straight entry and arc exit','read-only draft and cancel','out-of-order preview cannot replace newer geometry','stable input canvas listener focus selection and scroll','batch save and lost-response receipt','one-step undo and redo','embedded save reopen reconstructs identical geometry'],coverage:{native:'production geometry service and SDO',standaloneCef:false,boardOutsideLead:false},selected:ids,kept,preview:storedPreview},null,2));
}
