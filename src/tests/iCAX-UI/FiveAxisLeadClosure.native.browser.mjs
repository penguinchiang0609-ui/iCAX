import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
import { verifyCutStarting } from './FiveAxisCutStarting.native.browser.mjs';

export async function verifyLeadClosure({page,rpc,calls,output}){
  console.log('Lead closure: spatial automotive contour');
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>invoke('CamScene.ReadState');
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const field=name=>page.locator(`[data-five-lead-field="${name}"]`);
  const ready=()=>page.waitForFunction(()=>fixture.view.fiveLeads?.loaded&&!fixture.view.fiveLeads.previewBusy);
  const preview=(kind,length)=>page.waitForFunction(({kind,length})=>{
    const s=fixture.view.fiveLeads;return !s?.previewBusy&&s?.preview?.items[0]?.closure.kind===kind&&s.preview.items[0].closure.lengthMm===length;
  },{kind,length});
  const confirm=async()=>{await page.locator('[data-five-command="five.leads.confirm"]').click();await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisLeadsOpen);};
  const before=await read();let definitions=[];
  for(let i=0;i<before.intentToolpaths.length;i+=256)definitions.push(...(await invoke('CamCut.Read',{expected:before.expected,intentIds:before.intentToolpaths.slice(i,i+256).map(c=>c.id)})).items);
  const cut=before.intentToolpaths.filter(c=>c.cutType==='Contour'&&c.geometryKind==='GeneralContour'&&definitions.find(d=>d.intentId===c.id)?.closed).sort((a,b)=>b.previewPoints.length-a.previewPoints.length)[0];
  assert.ok(cut,'closed spatial spline contour is required');
  await command('five.tool.leads');await page.locator(`[data-five-intent="${cut.id}"]`).click();await ready();
  await field('entry.kind').selectOption('None');await field('exit.kind').selectOption('Line');await field('exit.length').fill('0.1');
  await field('closure.kind').selectOption('Overcut');await field('closure.length').fill('2');await preview('Overcut',2);
  const over=await page.evaluate(()=>fixture.view.fiveLeads.preview.items[0]);
  assert.notDeepEqual(over.start,over.end);assert.ok(over.end.every((v,i)=>Math.abs(v-over.leads.find(l=>l.role==='LeadOut').anchor[i])<1e-7));
  assert.ok(over.closure.sourceSpans.length>0);assert.deepEqual(await read(),before);
  await page.evaluate(()=>{const v=fixture.view.viewport,item=fixture.view.fiveLeads.preview.items[0],p=v.camera.position.clone().set(...item.start);v.sceneObjects.get(item.placementId).localToWorld(p);v.setCameraState({...v.getCameraState(),target:{x:p.x,y:p.y,z:p.z},radius:22});});
  await page.screenshot({path:resolve(output,'automotive-overcut.png')});
  await confirm();let live=await read();const overPolicy=(await invoke('CamProcess.Read',{expected:live.expected,intentIds:[cut.id]})).items[0];
  assert.equal(overPolicy.assignment.overcutLengthMm,2);assert.equal(overPolicy.assignment.gapLengthMm,0);
  await command('five.tool.overcut');await ready();assert.equal(await field('closure.kind').inputValue(),'Overcut');
  assert.equal(await field('entry.kind').count(),0,'separate menu edits only closure');
  // The same lead editor handles gap; a delayed result cannot replace it or reset focus/scroll.
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.five-position-body::after{content:"";display:block;height:1100px;flex-shrink:0}.five-position-body{max-height:430px;overflow:auto}'});
  await page.evaluate(()=>{fixture.holdMethod='CamProcess.PreviewLeads';fixture.hold=true;});
  await field('closure.length').fill('3');await page.waitForFunction(()=>fixture.held);
  await field('closure.kind').selectOption('Gap');await field('closure.length').fill('2');await preview('Gap',2);
  const kept=await page.evaluate(()=>{const n=document.querySelector('[data-five-lead-field="closure.length"]');fixture.closureInput=n;fixture.closureCanvas=document.querySelector('canvas');n.focus({preventScroll:true});n.setSelectionRange(0,1);document.querySelector('.cam-context-pane').scrollTop=83;document.querySelector('.five-position-body').scrollTop=37;return [document.querySelector('.cam-context-pane').scrollTop,document.querySelector('.five-position-body').scrollTop];});
  await page.evaluate(()=>fixture.release());await page.waitForTimeout(150);
  assert.deepEqual(await page.evaluate(()=>[document.querySelector('.cam-context-pane').scrollTop,document.querySelector('.five-position-body').scrollTop]),kept);
  assert.equal(await page.evaluate(()=>document.activeElement===fixture.closureInput&&fixture.closureInput.selectionStart===0&&fixture.closureInput.selectionEnd===1&&fixture.closureCanvas===document.querySelector('canvas')),true);
  const gap=await page.evaluate(()=>fixture.view.fiveLeads.preview.items[0]);assert.equal(gap.closure.kind,'Gap');assert.notDeepEqual(gap.end,over.end);
  assert.ok(gap.end.every((v,i)=>Math.abs(v-gap.cutPoints.at(-1)[i])<1e-6));assert.ok(gap.end.every((v,i)=>Math.abs(v-gap.leads.find(l=>l.role==='LeadOut').anchor[i])<1e-7));
  assert.ok(await page.evaluate(id=>fixture.view.fiveLeadOverlay.group.children.some(g=>g.children.some(n=>n.userData.intentId===id&&n.userData.leadRole==='Gap'&&n.material.isLineDashedMaterial)),cut.id));
  assert.equal(await page.evaluate(id=>fixture.view.fiveCutPreview.group.children.find(n=>n.userData.cuttingLineId===id).geometry.getAttribute('position').count,cut.id),gap.cutPoints.length);
  await stress.evaluate(n=>n.remove());await page.evaluate(()=>{document.querySelector('.cam-context-pane').scrollTop=0;document.querySelector('.five-position-body').scrollTop=0;});
  await page.screenshot({path:resolve(output,'automotive-gap.png')});
  await confirm();live=await read();const gapPolicy=(await invoke('CamProcess.Read',{expected:live.expected,intentIds:[cut.id]})).items[0];
  assert.equal(gapPolicy.assignment.gapLengthMm,2);assert.equal(gapPolicy.assignment.overcutLengthMm,0);assert.deepEqual(gapPolicy.assignment.leadOut,overPolicy.assignment.leadOut);
  await rpc({action:'undo'});live=await read();assert.deepEqual((await invoke('CamProcess.Read',{expected:live.expected,intentIds:[cut.id]})).items[0],overPolicy);
  await rpc({action:'redo'});live=await read();assert.deepEqual((await invoke('CamProcess.Read',{expected:live.expected,intentIds:[cut.id]})).items[0],gapPolicy);
  const reopened=await rpc({action:'save-reopen'});assert.deepEqual((await invoke('CamProcess.PreviewLeads',{expected:reopened.expected,intentIds:[cut.id]})).items[0],gap);
  // The bridge recreates the repository on reopen; recreate the UI ViewClient as
  // the desktop project lifecycle does, instead of keeping old runtime view URLs.
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  await page.evaluate(async()=>{
    const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
    fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);
  });
  await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.viewport?.getDebugState()?.objectCount===1);
  await command('five.tool.leads');await page.locator(`[data-five-intent="${cut.id}"]`).click();await ready();assert.equal(await field('closure.kind').inputValue(),'Gap');
  // Zero lengths are canonical no-lead policies, while the same start anchor remains editable.
  await field('entry.kind').selectOption('Line');await field('entry.length').fill('0');await field('exit.length').fill('0');
  await field('closure.kind').selectOption('Normal');await preview('Normal',0);
  assert.ok(await page.evaluate(()=>fixture.view.fiveLeads.preview.items[0].leads.length===0));await confirm();
  live=await read();const zero=(await invoke('CamProcess.Read',{expected:live.expected,intentIds:[cut.id]})).items[0].assignment;
  assert.equal(zero.leadIn.kind,'None');assert.equal(zero.leadOut.kind,'None');
  await verifyCutStarting({page,rpc,calls,output,intentId:cut.id,automotive:false,focusContour:true});
  writeFileSync(resolve(output,'lead-closure-report.json'),JSON.stringify({passed:true,intentId:cut.id,cases:['separate menus share saved lead policy','exact spatial overcut and shortened gap with relocated exit','gap interval dashed and removed from cutting preview','read-only preview and stale response guard','focus selection canvas and both scrolls preserved','gap/overcut mutually exclusive','undo redo and embedded reopen','zero lead length and actual start click use same commit'],coverage:{native:true,standaloneCef:false}},null,2));
}
