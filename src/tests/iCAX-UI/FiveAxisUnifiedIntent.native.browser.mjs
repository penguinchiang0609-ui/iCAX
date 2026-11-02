import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyUnifiedIntent({page,rpc,calls,output}) {
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
  const read=()=>rpc({action:'read-state'});
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending);
  const refresh=async()=>{await command('five.refresh');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
  const edit=async(method,fields)=>invoke(method,{commandId:crypto.randomUUID(),expected:(await read()).expected,...fields});
  let state=await read();
  assert.ok(state.intentToolpaths.length>1);
  const original=state.intentToolpaths.find(c=>c.cutType==='StandardHole'&&c.geometryKind==='Slot') ||
    state.intentToolpaths.find(c=>c.machiningKind==='Cut');
  assert.ok(original);
  const none={kind:'None',placement:'ScrapSide',lengthMm:0,angleRad:0};
  await edit('CamProcess.Update',{intentIds:[original.id],patch:{kerf:{widthMm:0},leadIn:none,leadOut:none,gapLengthMm:0,overcutLengthMm:0}});
  await refresh();state=await read();
  await command('five.tool.split');
  await page.evaluate(()=>fixture.view.viewport.fitView());
  const pick=await page.evaluate(async id=>{
    const THREE=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const {nearestCutLine}=await import('/src/apps/five-axis-cam/webpage/cutSelection.mjs');
    const v=fixture.view.viewport,cut=fixture.view.scene.intentToolpaths.find(c=>c.id===id);
    const placement=fixture.view.scene.domain.placements.find(p=>p.id===cut.placementId);
    const matrix=new THREE.Matrix4().set(...placement.transform),box=v.renderer.domElement.getBoundingClientRect();
    const dialog=document.querySelector('[data-five-key="split-dialog"]').getBoundingClientRect();
    const screen=p=>{const q=new THREE.Vector3(...p).applyMatrix4(matrix).project(v.camera);return{x:box.x+(q.x+1)*box.width/2,y:box.y+(1-q.y)*box.height/2};};
    const start=screen(cut.startPoint);
    for(const p of cut.previewPoints.slice(2,-2)){
      const q=screen(p);
      if(q.x>Math.max(box.left+30,dialog.right+20)&&q.x<box.right-30&&q.y>box.top+70&&q.y<box.bottom-70&&Math.hypot(q.x-start.x,q.y-start.y)>25&&nearestCutLine(fixture.view,{clientX:q.x,clientY:q.y})?.intentId===id)return q;
    }
    throw Error('No visible interior station for the real source boundary');
  },original.id);
  await page.evaluate(()=>{fixture.unifiedNodes={canvas:fixture.view.viewport.renderer.domElement,tree:document.querySelector('.five-cut-tree')};});
  await page.mouse.click(pick.x,pick.y);await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  assert.equal(await page.evaluate(()=>fixture.view.fiveSplit.points.length),1);
  assert.equal(await page.evaluate(()=>fixture.view.fiveSplit.points[0].intentId),original.id);
  const before=await read();
  await page.getByRole('button',{name:'确定',exact:true}).click();await idle();
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  state=await read();
  assert.equal(state.intentToolpaths.length,before.intentToolpaths.length+1);
  const children=state.intentToolpaths.filter(c=>c.sourceKey===original.sourceKey&&c.split);
  assert.equal(children.length,2);
  assert.ok(children.some(c=>c.id===original.id&&c.number===original.number));
  assert.ok(children.every(c=>c.cutType==='Contour'&&!c.canBeStandardHole));
  assert.ok(await page.evaluate(()=>fixture.unifiedNodes.canvas===fixture.view.viewport.renderer.domElement&&fixture.unifiedNodes.tree===document.querySelector('.five-cut-tree')));
  const splitState=state.intentToolpaths;
  await rpc({action:'undo'});await refresh();assert.deepEqual((await read()).intentToolpaths,before.intentToolpaths);
  await rpc({action:'redo'});await refresh();assert.deepEqual((await read()).intentToolpaths,splitState);

  const child=children.find(c=>c.id!==original.id);
  await edit('CamCut.SetCommands',{intentId:child.id,beforeCommands:'M10',afterCommands:'M11'});
  await refresh();state=await read();
  assert.equal(state.intentToolpaths.find(c=>c.id===child.id).beforeCommands,'M10');
  assert.equal(state.intentToolpaths.find(c=>c.id===original.id).beforeCommands,'');
  await rpc({action:'export-project',path:resolve(output,'unified-intent-before-interaction.i5cam')});
  writeFileSync(resolve(output,'unified-intent-checkpoint.json'),JSON.stringify({passed:true,originalId:original.id,childId:child.id,initial:before.intentToolpaths.length,final:state.intentToolpaths.length,
    cases:['actual source import and extraction','mouse split with stable identity','native undo and redo','local segment commands']},null,2));
  await page.getByRole('tab',{name:'切割',exact:true}).click();
  await page.locator(`[data-five-intent="${child.id}"]`).click();
  const name=page.getByRole('textbox',{name:'刀路名称',exact:true});
  assert.equal(await name.count(),1);
  const stress=await page.addStyleTag({content:'.five-path-list::after,.cam-info-pane::after{content:"";display:block;height:1800px}.five-cut-tree{max-height:180px;overflow:auto}.five-cut-tree::after{content:"";display:block;height:800px}'});
  await page.evaluate(()=>{fixture.holdMethod='CamCut.ManageTree';fixture.hold=true;fixture.unifiedInput=document.querySelector('[data-five-cut-name]');fixture.unifiedKeys=0;fixture.unifiedInput.addEventListener('keydown',()=>++fixture.unifiedKeys);});
  await page.evaluate(id=>{void fixture.entry.handleRibbonCommand(fixture.context,id);},`five.tree.ToggleEnabled:${child.id}`);await page.waitForFunction(()=>fixture.held);
  await name.fill('正在输入的分段名称');
  const interaction=await page.evaluate(()=>{
    const input=fixture.unifiedInput;input.focus({preventScroll:true});input.setSelectionRange(2,5);
    fixture.unifiedScrollNodes=['.five-path-list','.cam-info-pane','.five-cut-tree'].map(s=>document.querySelector(s));
    fixture.unifiedScrollNodes.forEach((n,i)=>n.scrollTop=90+i*25);
    return {value:input.value,selection:[input.selectionStart,input.selectionEnd],scroll:fixture.unifiedScrollNodes.map(n=>n.scrollTop)};
  });
  assert.ok(interaction.scroll.every(v=>v>0),`The actual left list, right pane and nested tree must all scroll: ${JSON.stringify(interaction)}`);
  const scrollBounds=await page.evaluate(()=>fixture.unifiedScrollNodes.map(n=>({height:n.getBoundingClientRect().height,clientHeight:n.clientHeight,scrollHeight:n.scrollHeight})));
  assert.ok(scrollBounds.every(n=>n.height>0&&n.clientHeight>0&&n.scrollHeight>n.clientHeight),`Every stressed scroll container must retain a visible scrollport: ${JSON.stringify(scrollBounds)}`);
  await page.evaluate(()=>fixture.release());await idle();
  assert.deepEqual(await page.evaluate(()=>({node:fixture.unifiedInput===document.querySelector('[data-five-cut-name]'),focus:document.activeElement===fixture.unifiedInput,
    value:fixture.unifiedInput.value,selection:[fixture.unifiedInput.selectionStart,fixture.unifiedInput.selectionEnd],scroll:fixture.unifiedScrollNodes.map(n=>n.scrollTop)})),{node:true,focus:true,...interaction});
  await name.press('Shift');assert.ok(await page.evaluate(()=>fixture.unifiedKeys>0),'the original input listener survives the response');
  await name.press('Enter');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  state=await read();assert.equal(state.intentToolpaths.find(c=>c.id===child.id).name,interaction.value);
  assert.equal(state.intentToolpaths.find(c=>c.id===child.id).enabled,false);
  await stress.evaluate(n=>n.remove());await refresh();
  await edit('CamCut.ManageTree',{setupId:state.activeSetupId,action:'Enable',intentIds:[child.id]});await refresh();
  await edit('CamCut.ManageTree',{setupId:state.activeSetupId,action:'Lock',intentIds:[child.id]});
  await assert.rejects(edit('CamCut.Rename',{intentId:child.id,name:'锁定时不能改'}),/LOCK/);
  await edit('CamCut.ManageTree',{setupId:state.activeSetupId,action:'Unlock',intentIds:[child.id]});await refresh();
  await page.locator(`[data-five-intent="${child.id}"]`).click();
  await name.fill('已提交的名称');
  await page.evaluate(()=>{fixture.holdMethod='CamCut.Rename';fixture.hold=true;});
  await name.press('Enter');await page.waitForFunction(()=>fixture.held);
  await name.fill('响应期间继续编辑');await name.evaluate(n=>n.setSelectionRange(2,6));
  await page.evaluate(()=>fixture.release());await idle();
  assert.equal(await name.inputValue(),'响应期间继续编辑');
  assert.deepEqual(await name.evaluate(n=>[n.selectionStart,n.selectionEnd]),[2,6]);
  assert.equal((await read()).intentToolpaths.find(c=>c.id===child.id).name,'已提交的名称');
  await name.press('Enter');await idle();assert.equal((await read()).intentToolpaths.find(c=>c.id===child.id).name,'响应期间继续编辑');
  await command('five.tool.adjust');await idle();
  const basis=page.locator('[data-five-field="cutPoseInitialization"]');assert.equal(await basis.count(),1);
  await basis.evaluate(n=>{fixture.unifiedBasis=n;});
  await basis.selectOption('FixedDirection');await idle();
  const axisInput=page.locator('[data-five-field="cutAxisI"]');assert.equal(await axisInput.count(),1);
  assert.equal(await page.locator('[data-five-field="cutLeadTilt"]').count(),0);
  assert.ok(await basis.evaluate(n=>n===fixture.unifiedBasis));
  await axisInput.focus();
  await page.evaluate(()=>{const select=fixture.unifiedBasis;select.value='SurfaceNormal';select.dispatchEvent(new Event('change',{bubbles:true}));});
  assert.equal(await axisInput.count(),0);
  assert.ok(await page.evaluate(()=>!document.activeElement?.hasAttribute('data-five-field')),'a removed conditional field cannot transfer focus to a different parameter');
  assert.ok(await page.evaluate(()=>fixture.unifiedNodes.canvas===fixture.view.viewport.renderer.domElement));
  await command('five.process.load');await idle();
  const saved=await read();
  const projectPath=resolve(output,'unified-intent-real-model.i5cam');await rpc({action:'export-project',path:projectPath});
  await rpc({action:'save-reopen'});assert.deepEqual((await read()).intentToolpaths,saved.intentToolpaths);
  await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));
  await page.evaluate(async()=>{
    const {getProjectView}=await import('/src/apps/_shared/workbench/state/projectViewStore.mjs');
    const {ViewClient}=await import('/src/iCAX-UI/SDK/View/viewClient.mjs');
    fixture.view=getProjectView(fixture.context.project.projectId);fixture.context.sceneProxy.views=new ViewClient(fixture.context.sceneProxy);fixture.entry.mountProject(fixture.context);
  });await idle();await page.waitForFunction(()=>fixture.view.viewport?.getDebugState()?.objectCount>0);
  await page.screenshot({path:resolve(output,'unified-intent-reopened.png')});
  writeFileSync(resolve(output,'unified-intent-report.json'),JSON.stringify({passed:true,projectPath,initial:before.intentToolpaths.length,final:saved.intentToolpaths.length,
    cases:['actual source import and extraction','mouse split with stable identity','native undo and redo','local segment commands','disabled segment remains editable','locks reject edits','latest dual-pane and nested scrolling','live input value focus selection node and listener','typing continues during a rename receipt','conditional fields retain nodes and cannot steal removed focus','save reopen'],
    pdoReads:calls.filter(c=>c.action==='pdo-read').length,coverage:{native:true,browser:true,standaloneCef:false}},null,2));
}
