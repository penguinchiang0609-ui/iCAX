import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';

export async function verifyToolAxisInfluence({page,rpc,calls,output}){
  page.setDefaultTimeout(30000);
  const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload}),read=()=>invoke('CamScene.ReadState'),near=(a,b)=>a.forEach((n,i)=>assert.ok(Math.abs(n-b[i])<1e-7,`${a} != ${b}`));
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveAxisPointEdit?.loading&&!fixture.view.fiveAxisPointEdit?.influenceLoading&&!fixture.view.fiveToolAxisDisplay?.loading);
  const command=async id=>{await page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);await idle();};
  const state=await read(),id=state.intentToolpaths[0].id,initial=await invoke('CamCut.ReadToolAxisSamples',{expected:state.expected,intentIds:[id]}),baseline=initial.cuts[0].samples;
  await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await page.getByRole('tab',{name:'切割',exact:true}).click();await page.locator(`[data-five-intent="${id}"]`).click();
  await command('five.points.edit');await page.getByRole('combobox',{name:'姿态表示方式',exact:true}).selectOption('IJK');await page.getByRole('combobox',{name:'关键点刀轴坐标系',exact:true}).selectOption('Workpiece');
  await page.getByRole('button',{name:'选择受影响姿态',exact:true}).click();await idle();
  const data=await page.evaluate(()=>({stations:fixture.view.fiveAxisPointEdit.influence.stations,main:fixture.view.fiveAxisPointEdit.axis,pointId:fixture.view.fiveAxisPointEdit.pointId}));assert.ok(data.stations.length>8);
  const indices=data.stations.map((s,i)=>({i,d:Math.abs(s.offset)})).sort((a,b)=>a.d-b.d).slice(0,4).map(s=>s.i).sort((a,b)=>a-b);
  for(const i of indices.slice(0,2))await page.getByRole('button',{name:`跟随姿态 ${i+1}`,exact:true}).click();
  assert.equal(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.pointId),data.pointId);
  await page.getByRole('button',{name:'连续选择',exact:true}).click();
  await page.getByRole('button',{name:`跟随姿态 ${indices[2]+1}`,exact:true}).click();await page.getByRole('button',{name:`跟随姿态 ${indices[3]+1}`,exact:true}).click();
  assert.ok(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.influence.stations.filter(s=>s.weight!==null).length)>=4);
  // Fit the selected contour, then toggle a station with a real viewport click.
  await page.evaluate(id=>{const v=fixture.view,vp=v.viewport,c=v.scene.intentToolpaths.find(c=>c.id===id),obj=vp.sceneObjects.get(c.placementId),lo=[0,1,2].map(i=>Math.min(...c.previewPoints.map(p=>p[i]))),hi=[0,1,2].map(i=>Math.max(...c.previewPoints.map(p=>p[i]))),p=obj.localToWorld(vp.camera.position.clone().set(...lo.map((n,i)=>(n+hi[i])/2)));vp.setCameraState({...vp.getCameraState(),target:{x:p.x,y:p.y,z:p.z},radius:Math.max(10,Math.hypot(...lo.map((n,i)=>hi[i]-n))*1.8)});},id);
  await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  const pick=await page.evaluate(()=>{const v=fixture.view,vp=v.viewport,box=vp.renderer.domElement.getBoundingClientRect(),e=v.fiveAxisPointEdit.influence,c=v.scene.intentToolpaths.find(c=>c.id===v.fiveAxisPointEdit.id),obj=vp.sceneObjects.get(c.placementId);for(let i=0;i<e.stations.length;i++){const s=e.stations[i];if(s.weight!==null)continue;const p=obj.localToWorld(vp.camera.position.clone().set(...s.point)).project(vp.camera),x=box.x+(p.x+1)*box.width/2,y=box.y+(1-p.y)*box.height/2;if(x>box.x+50&&x<box.right-50&&y>box.y+90&&y<box.bottom-80&&document.elementFromPoint(x,y)===vp.renderer.domElement)return {x,y,i};}throw Error('No visible influence station');});
  const camera=await page.evaluate(()=>fixture.view.viewport.getCameraState());await page.mouse.click(pick.x,pick.y);assert.notEqual(await page.evaluate(i=>fixture.view.fiveAxisPointEdit.influence.stations[i].weight,pick.i),null);
  assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getCameraState()),camera);await page.getByRole('button',{name:'结束选择',exact:true}).click();
  await page.getByRole('button',{name:'平滑衰减',exact:true}).click();assert.equal(await page.getByRole('combobox',{name:'影响曲线过渡',exact:true}).inputValue(),'Smooth');
  const dragIndex=pick.i,index=indices[1];await page.getByRole('combobox',{name:'当前影响曲线点',exact:true}).selectOption(String(dragIndex));const dot=page.locator(`[data-five-weight-index="${dragIndex}"]`);await dot.scrollIntoViewIfNeeded();const dotBox=await dot.boundingBox(),graph=await page.locator('.five-influence-graph').boundingBox();
  await page.mouse.move(dotBox.x+dotBox.width/2,dotBox.y+dotBox.height/2);await page.mouse.down();await page.mouse.move(dotBox.x+dotBox.width/2,graph.y+graph.height*(124-.4*98)/154,{steps:8});await page.mouse.up();
  const weight=await page.evaluate(i=>fixture.view.fiveAxisPointEdit.influence.stations[i].weight,dragIndex);assert.ok(Math.abs(weight-.4)<.025,`drag weight: ${weight}`);
  await page.getByRole('combobox',{name:'当前影响曲线点',exact:true}).selectOption(String(index));
  await page.getByRole('textbox',{name:'跟随比例百分比',exact:true}).fill('35');
  const field=a=>page.getByRole('textbox',{name:`关键点 ${a}`,exact:true});await field('I').fill('0');await field('J').fill('.6');await field('K').fill('.8');
  const preview=await page.evaluate(()=>{const v=fixture.view,d=v.fiveAxisPointEdit;return {directions:d.influence.stations.filter(s=>s.weight!==null).map(s=>({anchor:s.anchor,direction:v.fiveAxisPointPreview.group.children.find(m=>m.userData.influenceStation===s)?.userData.toolAxisSamples[0].direction})),main:d.axis.anchor};});assert.ok(preview.directions.every(p=>p.direction));
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:1500px}'});
  await page.evaluate(()=>{fixture.influenceNodes={input:document.querySelector('[aria-label="跟随比例百分比"]'),graph:document.querySelector('.five-influence-graph'),list:document.querySelector('.five-influence-stations'),canvas:fixture.view.viewport.renderer.domElement,events:0};fixture.influenceNodes.input.addEventListener('input',()=>fixture.influenceNodes.events++);fixture.holdMethod='CamCut.EditToolAxisPoint';fixture.hold=true;});
  await field('K').press('Enter');await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const n=fixture.influenceNodes.input;n.focus({preventScroll:true});n.setSelectionRange(0,2);return ['.cam-context-pane','.cam-info-pane','.five-influence-stations'].map((sel,i)=>{const p=document.querySelector(sel);p.scrollTop=80+25*i;return p.scrollTop;});});
  await page.evaluate(()=>fixture.release());await idle();assert.equal(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.error),'');
  assert.equal(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.influence.activeIndex),index);
  assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-influence-stations'].map(sel=>document.querySelector(sel).scrollTop)),scroll);
  assert.equal(await page.evaluate(()=>fixture.influenceNodes.input===document.querySelector('[aria-label="跟随比例百分比"]')&&document.activeElement===fixture.influenceNodes.input&&fixture.influenceNodes.graph===document.querySelector('.five-influence-graph')&&fixture.influenceNodes.list===document.querySelector('.five-influence-stations')&&fixture.influenceNodes.canvas===fixture.view.viewport.renderer.domElement),true);
  assert.deepEqual(await page.getByRole('textbox',{name:'跟随比例百分比',exact:true}).evaluate(n=>[n.selectionStart,n.selectionEnd]),[0,2]);await stress.evaluate(n=>n.remove());
  let current=await read();for(const p of preview.directions)near((await invoke('CamCut.ReadToolAxis',{expected:current.expected,intentId:id,anchor:p.anchor})).direction,p.direction);
  const selectedAnchors=preview.directions.map(p=>p.anchor),same=(a,b)=>a.sourceSpanId===b.sourceSpanId&&Math.abs(a.nativeParameter-b.nativeParameter)<1e-8;
  for(const s of baseline){if(same(s.anchor,preview.main)||selectedAnchors.some(a=>same(a,s.anchor))||Math.abs(s.pathDistanceMm-initial.cuts[0].lengthMm)<1e-7)continue;near((await invoke('CamCut.ReadToolAxis',{expected:current.expected,intentId:id,anchor:s.anchor})).direction,s.direction);}
  await page.getByRole('textbox',{name:'跟随比例百分比',exact:true}).fill('35');assert.ok(await page.evaluate(()=>fixture.influenceNodes.events)>0);await command('five.points.cancel');
  await rpc({action:'undo'});await command('five.refresh');await rpc({action:'redo'});await command('five.refresh');await idle();
  current=await read();for(const p of preview.directions)near((await invoke('CamCut.ReadToolAxis',{expected:current.expected,intentId:id,anchor:p.anchor})).direction,p.direction);
  const generate=async()=>{current=await read();let t=await invoke('CamPlan.Generate',{commandId:crypto.randomUUID(),expected:current.expected,setupId:current.activeSetupId});while(t.status==='Running'){await new Promise(r=>setTimeout(r,40));t=await invoke('CamPlan.QueryStatus',{epoch:t.epoch,taskId:t.taskId});}assert.equal(t.status,'Completed',t.error);await command('five.refresh');};await generate();await generate();
  const generated=await page.evaluate(id=>fixture.view.scene.domain.setups.find(s=>s.id===fixture.view.scene.domain.activeSetupId).generatedGeometry.operations.find(o=>o.intentId===id),id);let generatedHits=0;
  for(const expected of preview.directions){const station=data.stations.find(s=>same(s.anchor,expected.anchor));for(const part of generated.parts)for(let i=0;i<part.points.length;i++)if(Math.hypot(...part.points[i].map((n,j)=>n-station.point[j]))<1e-8){near(part.directions[i],expected.direction);generatedHits++;}}
  assert.ok(generatedHits>=2,'generated resource includes exact selected stations with the previewed axes');
  // A final curve edit is committed by project Save, without an explicit Apply.
  await command('five.points.edit');await idle();const e=await page.evaluate(()=>fixture.view.fiveAxisPointEdit.influence);assert.ok(e);assert.equal(e.stations.filter(s=>s.weight!==null).some(s=>s.owner),false,'closed start/end aliases must not lock their own followers');const savedIndex=e.stations.findIndex(s=>s.weight!==null);await page.locator(`[data-five-weight-index="${savedIndex}"]`).focus();await page.keyboard.press('ArrowUp');
  await page.evaluate(()=>fixture.entry.prepareProjectSave(fixture.context));await idle();assert.equal(await page.evaluate(()=>fixture.view.fiveAxisPointEdit.dirty),false);
  await page.screenshot({path:resolve(output,'tool-axis-influence.png')});current=await read();const before=await invoke('CamCut.ListToolAxisPoints',{expected:current.expected,intentId:id});
  await rpc({action:'export-project',path:resolve(output,'tool-axis-influence.i5cam')});await page.evaluate(()=>fixture.entry.releaseProject(fixture.context));const reopened=await rpc({action:'save-reopen'});
  const after=await invoke('CamCut.ListToolAxisPoints',{expected:reopened.expected,intentId:id});assert.deepEqual(after,before);
  writeFileSync(resolve(output,'influence-report.json'),JSON.stringify({passed:true,id,cases:['explicit follower selection','continuous range selection','real 3D mouse selection without camera movement','real curve drag and exact percentage input','live preview matches native evaluation','unselected control directions preserved','input graph canvas listeners focus caret and latest dual plus nested scroll preserved','native undo redo','repeated generation','save flush and embedded reopen retain weights references and curve'],coverage:{native:true,browser:'Edge',standaloneCef:false},source:'https://kb.sprutcam.com/docs/SprutCAM/17/en/10134.html',calls:calls.length},null,2));
  console.log('Tool-axis influence workflow passed');
}
