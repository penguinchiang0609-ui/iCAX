// Standalone CEF, actual framework history, native geometry and project files.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const output=path.resolve('output/tests/cut-edit-audit-20261023');
const phase=process.argv[2]||'core',file=path.join(output,'cut-audit.i5cam').replaceAll('\\','/');
fs.mkdirSync(output,{recursive:true});
const report={phase,passed:false,cases:[],scope:'Actual standalone CEF controls, framework Database undo/redo, native parameter reads and actual project writes. No controller qualification.'};
if(process.argv.includes('--resume'))report.cases=JSON.parse(fs.readFileSync(path.join(output,phase+'-report.json'),'utf8')).cases;
let page;
const json=v=>JSON.parse(JSON.stringify(v));
(async()=>{
 const browser=await chromium.connectOverCDP('http://127.0.0.1:9483');page=browser.contexts()[0].pages()[0];page.setDefaultTimeout(30000);
 if(process.argv.includes('--fixed-viewport'))await page.setViewportSize({width:1600,height:950});
 report.viewport=await page.evaluate(()=>({width:innerWidth,height:innerHeight}));
 report.viewport.fixed=process.argv.includes('--fixed-viewport');
 assert.ok(await page.evaluate(()=>innerWidth>=1000&&innerHeight>=650),'Restore the actual native window before running mouse regressions');
 const poll=async(fn,arg,label='UI did not settle')=>{for(let i=0;i<300;i++){if(await page.evaluate(fn,arg))return;await new Promise(r=>setTimeout(r,50));}throw Error(label);};
 await poll(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(file=>__icaxAppShell.openProject(file,'icax.five-axis-cam'),file);
 await poll(async file=>{if(__icaxAppShell.getState().activeProjectPath!==file)return false;const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.cv=getProjectView(__icaxAppShell.getState().activeProjectId);return !!cv.scene?.intentToolpaths&&!cv.pending;},file);
 await page.evaluate(async()=>{
  const{ProjectProxy}=await import('../../../iCAX-UI/ProjectProxy/ProjectProxy.mjs');
  if(!window.cutAuditWrites){window.cutAuditWrites=[];const save=ProjectProxy.prototype.save;ProjectProxy.prototype.save=async function(file){const result=await save.call(this,file);cutAuditWrites.push({file,at:Date.now()});return result;};}
  window.cutAuditRead=async()=>{
   const s=await cv.viewportSceneProxy.invoke('CamScene.ReadState',{}),ids=s.intentToolpaths.map(c=>c.id);
   const processes=ids.length?(await cv.viewportSceneProxy.invoke('CamProcess.Read',{expected:s.expected,intentIds:ids})).items:[];
   return {order:s.setups[0].definitionOrder,ordering:s.setups[0].generatedGeometry?.ordering,groups:s.setups[0].cutGroups,
    cuts:s.intentToolpaths.map(c=>Object.fromEntries(['id','name','number','cutType','enabled','locked','ownEnabled','ownLocked','groupId','start','reversed','retainedSide','beforeCommands','afterCommands','previewPoints','startPoint','curveRef','featureRef','processRef','poseRef'].map(k=>[k,c[k]]))),
    policies:processes.map(p=>({id:p.intentId,assignment:p.assignment,pose:p.posePolicy}))};
  };
  window.cutAuditGeometry=()=>{
   const g=cv.scene.domain.setups[0].generatedGeometry;
   return{status:g.status,operations:g.operations,transitions:g.transitions};
  };
 });
 const idle=()=>poll(()=>!cv.pending&&!cv.fiveToolAxisRequest&&!cv.fiveHistoryBusy&&!__icaxAppShell.getState().pendingCount&&!cv.fiveCutSaveError,undefined,'Editor remained busy or autosave failed');
 await page.evaluate(()=>__icaxAppShell.selectRibbonTab('toolpath'));await page.getByRole('tab',{name:'切割',exact:true}).click();await idle();
 const ids=await page.evaluate(()=>cv.scene.intentToolpaths.map(c=>c.id));
 const select=async(id,modifiers=[])=>{await page.locator(`[data-five-intent="${id}"]`).click({modifiers});await idle();};
 const click=async id=>{await page.locator(`[data-five-command="${id}"],[data-command-id="${id}"]`).first().click();await idle();};
 const tool=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand('five.tool.'+id),id);await idle();if(id==='extract'&&await page.locator('[data-five-command="five.extraction.close"]').count())await click('five.extraction.close');};
 const pointReady=()=>poll(()=>cv.fiveAxisPointEdit?.axis&&!cv.fiveAxisPointEdit.loading&&!cv.fiveAxisPointEdit.influenceLoading);
 const pickCut=async(id,fraction=.35)=>{
  const xy=await page.evaluate(async({id,fraction})=>{
   const T=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js'),c=cv.scene.intentToolpaths.find(c=>c.id===id),v=cv.viewport;
   const m=new T.Matrix4().set(...cv.scene.domain.placements.find(p=>p.id===c.placementId).transform);
   const low=[0,1,2].map(i=>Math.min(...c.previewPoints.map(p=>p[i]))),high=[0,1,2].map(i=>Math.max(...c.previewPoints.map(p=>p[i])));
   const center=new T.Vector3(...low.map((n,i)=>(n+high[i])/2)).applyMatrix4(m);
   v.setCameraState({...v.getCameraState(),target:{x:center.x,y:center.y,z:center.z},radius:Math.max(5,Math.hypot(...low.map((n,i)=>high[i]-n))*1.5)});
   v.camera.updateMatrixWorld();const canvas=v.renderer.domElement,b=canvas.getBoundingClientRect();
   for(let n=Math.floor(c.previewPoints.length*fraction);n<c.previewPoints.length-2;n++){
    const p=new T.Vector3(...c.previewPoints[n]).applyMatrix4(m).project(v.camera),x=b.left+(p.x+1)*b.width/2,y=b.top+(1-p.y)*b.height/2;
    if(p.z>=-1&&p.z<=1&&document.elementFromPoint(x,y)===canvas)return{x,y};
   }throw Error('No unobscured curve point for actual mouse picking');
  },{id,fraction});await page.mouse.click(xy.x,xy.y);await idle();
 };
 const field=(name,kind='field')=>page.locator(`[data-five-${kind}="${name}"]`);
 const input=async(name,value,kind='field')=>{const el=field(name,kind);if(await el.evaluate(e=>e.tagName==='SELECT'))await el.selectOption(String(value));else{await el.fill(String(value));await el.press('Tab');}await idle();};
 const read=async()=>json(await page.evaluate(()=>cutAuditRead()));
 const writeCount=()=>page.evaluate(()=>cutAuditWrites.length);
 const history=()=>page.evaluate(()=>cv.viewportSceneProxy.getUndoRedoState());
 const visual=async()=>{
  // Read-only native previews are the geometry oracle for rendered offsets,
  // leads and generated operations. The fixture contains one placement.
  await poll(()=>!cv.fiveCompensation?.loading&&!cv.fiveSavedLeads?.loading&&!cv.fiveToolAxisDisplay?.loading&&!cv.fiveAxisPointEdit?.loading);
  return page.evaluate(async()=>{
   const {currentLeadPreview}=await import('../../../apps/five-axis-cam/webpage/cutLeads.mjs');
   const preview=currentLeadPreview(cv),errors=[];
   const check=(line,points,label)=>{const a=[...line.geometry.attributes.position.array],e=points.flat();if(a.length!==e.length||a.some((v,i)=>Math.abs(v-e[i])>1e-4))errors.push(label);};
   for(const l of cv.fiveCutPreview?.group.children||[]){if(!l.userData.cuttingLineId)continue;const id=l.userData.cuttingLineId,c=cv.scene.intentToolpaths.find(c=>c.id===id);if(!c||c.enabled===false){errors.push('obsolete cut');continue;}const p=l.userData.originalPath?null:preview?.items.find(p=>p.intentId===id&&p.cutPoints?.length);check(l,p?.cutPoints||c.previewPoints,'cut '+id);}
   let compensationLines=0,axisInstances=0;
   cv.fiveCompensationOverlay?.group.traverse(l=>{const id=l.userData.compensatedIntentId;if(!l.isLine||!id)return;const p=cv.fiveCompensation?.items.find(p=>p.intentId===id)?.parts.find(p=>p.role===l.userData.role);if(!p)errors.push('obsolete compensation '+id);else{check(l,p.points,'compensation '+id);compensationLines++;}});
   cv.fiveLeadOverlay?.group.traverse(l=>{if(!l.isLine||!l.userData.leadRole)return;const p=preview?.items.find(p=>p.intentId===l.userData.intentId)?.leads?.find(p=>p.role===l.userData.leadRole);if(p?.points)check(l,p.points,'lead '+l.userData.intentId);});
   const nativeIds=cv.scene.intentToolpaths.filter(c=>c.enabled!==false).map(c=>c.id);
   const nativeAxes=nativeIds.length?await cv.viewportSceneProxy.invoke('CamCut.ReadToolAxisSamples',{expected:cv.scene.domain.expected,intentIds:nativeIds}):{cuts:[]};
   cv.fiveToolAxisDisplay?.group?.traverse(g=>{
    if(!g.userData.toolAxisSamples)return;const samples=g.userData.toolAxisSamples,attributes=g.geometry.attributes;
    for(let i=0;i<attributes.instanceDirection.count;i++){
     const sample=samples[attributes.instanceIndex.getX(i)],row=nativeAxes.cuts.find(r=>r.intentId===sample.intentId),native=row?.samples.find(s=>JSON.stringify(s.anchor)===JSON.stringify(sample.anchor));
     const v=g.userData.kind==='normal'?native?.surfaceNormal:native?.direction;
     if(!v||v.some((n,j)=>Math.abs(n-attributes.instanceDirection.array[i*3+j])>1e-5))errors.push('axis buffer '+sample.intentId);axisInstances++;
    }
   });
   if(cv.fiveAxisPointEdit?.axis&&!cv.fiveAxisPointEdit.dirty){
    const d=cv.fiveAxisPointEdit,p=[...(d.points||[]),...(d.newPoint?[d.newPoint]:[])].find(p=>p.id===d.pointId);
    if(p){const native=await cv.viewportSceneProxy.invoke('CamCut.ReadToolAxis',{expected:cv.scene.domain.expected,intentId:d.id,anchor:p.anchor});
     const glyph=cv.fiveAxisPointPreview?.group.children.find(g=>g.userData.pointPreview);
     if(!glyph||native.direction.some((v,i)=>Math.abs(v-glyph.geometry.attributes.instanceDirection.array[i])>1e-5))errors.push('selected keypoint axis buffer');
    }
   }
   const selected=cv.scene.intentToolpaths.find(c=>c.id===cv.fiveAxisUI.selectedIntentId),name=document.querySelector('[data-five-cut-name]');
   if(name&&selected&&name.value!==selected.name)errors.push('stale name input');
   let generatedLines=0;const currentTool=cv.fiveAxisUI.tools.toolpath;
   if(!['manual-order','auto-order'].includes(currentTool)){
    const T=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js'),g=cv.scene.domain.setups[0].generatedGeometry,indices=new Map();
    for(const line of cv.fiveGeneratedPreview?.group.children||[]){const id=line.userData.generatedIntentId;if(!id)continue;
     const op=g.operations.find(op=>op.intentId===id),i=indices.get(id)||0;indices.set(id,i+1);
     const m=new T.Matrix4().set(...cv.scene.domain.placements.find(p=>p.id===op.placementId).transform);
     check(line,op.parts.filter(p=>p.points?.length)[i].points.map(p=>new T.Vector3(...p).applyMatrix4(m).toArray()),'generated '+id);generatedLines++;
    }
   }
   return {errors,cutLines:cv.fiveCutPreview?.group.children.filter(l=>l.userData.cuttingLineId).length||0,compensationLines,axisInstances,generatedLines,leadObjects:cv.fiveLeadOverlay?.group.children.length||0};
  });
 };
 const cycle=async(name,act)=>{
  if(process.argv.includes('--resume')&&report.cases.some(c=>c.name===name)){console.log('KEEP verified '+name);return;}
  await idle();const before=await read(),h=await history(),writes=await writeCount();
  await act();await idle();const after=await read();assert.notDeepEqual(after,before,name+' must change native facts');
  assert.ok((await writeCount())>writes,name+' must write the actual file automatically');
  const ah=await history();assert.ok(ah.canUndo&&ah.undoSteps.length>=h.undoSteps.length&&ah.undoSteps.length<=h.undoSteps.length+1,name+' native history (bounded stack)');
  await page.locator('[data-action="undo-project"]').click();await idle();assert.deepEqual(await read(),before,name+' undo');
  await page.locator('[data-action="redo-project"]').click();await idle();assert.deepEqual(await read(),after,name+' redo');
  const scene=await visual();assert.deepEqual(scene.errors,[],name+' rendered paths');
  report.cases.push({name,nativeUndo:true,nativeRedo:true,automaticFileSave:true,visual:scene});
  fs.writeFileSync(path.join(output,phase+'-report.json'),JSON.stringify(report,null,2));console.log('PASS '+name);
 };
 if(ids.length)await select(ids[0]);
 if(phase==='core'){
  for(const name of ['切割连续修改一','切割连续修改二'])await cycle('刀路名称 '+name,async()=>{await page.locator('[data-five-cut-name]').fill(name);await page.locator('[data-five-cut-name]').press('Enter');});
  await select(ids[1]);await cycle('切换刀路后修改名称',async()=>{await page.locator('[data-five-cut-name]').fill('第二刀路独立名称');await page.locator('[data-five-cut-name]').press('Tab');});
  await select(ids[0]);assert.equal(await page.locator('[data-five-cut-name]').inputValue(),'切割连续修改二');
  for(const [key,value] of [['beforeCommands','M7\nG4 P0.1'],['afterCommands','M9\nM5']])await cycle(key,async()=>{await field(key,'cut-command').fill(value);await field(key,'cut-command').press('Tab');});
  for(const action of ['ToggleEnabled','ToggleEnabled','ToggleLocked','ToggleLocked'])await cycle(action,()=>click('five.tree.'+action+':'+ids[1]));
  await select(ids[1]);await cycle('树上移',()=>click('five.tree.Up'));await cycle('树下移',()=>click('five.tree.Down'));
  await select(ids[0]);await select(ids[1],['Control']);await cycle('成组',()=>click('five.tree.Group'));
  const group=await page.evaluate(()=>cv.scene.domain.setups[0].cutGroups[0].id);
  for(const action of ['ToggleGroupEnabled','ToggleGroupEnabled','ToggleGroupLocked','ToggleGroupLocked'])await cycle(action,()=>click('five.tree.'+action+':'+group));
  await page.locator(`[data-five-group="${group}"]`).click();await cycle('解组',()=>click('five.tree.Ungroup'));
  await select(ids[0]);await cycle('反向',()=>tool('reverse'));
  await select(ids[1],['Control']);await cycle('批量反向',()=>tool('reverse'));
 }
 if(phase==='process'){
  await tool('leads');await poll(()=>cv.fiveLeads?.loaded&&!cv.fiveLeads.previewBusy);
  for(const [name,changes] of [
   ['引入直线',[['entry.kind','Line'],['entry.length','1.2'],['entry.angle','15']]],
   ['引入圆弧',[['entry.kind','Arc'],['entry.length','1.5'],['entry.radius','2']]],
   ['引出直线',[['exit.kind','Line'],['exit.length','1.1'],['exit.angle','20']]],
   ['引出圆弧',[['exit.kind','Arc'],['exit.length','1.6'],['exit.radius','2.2']]],
   ['过切',[['closure.kind','Overcut'],['closure.length','0.3']]],
   ['缺口',[['closure.kind','Gap'],['closure.length','0.4']]],
   ['正常收尾',[['closure.kind','Normal']]],
  ])await cycle(name,async()=>{await tool(name==='过切'||name==='缺口'||name==='正常收尾'?'overcut':'leads');await poll(()=>cv.fiveLeads?.loaded);for(const [k,v]of changes)await input(k,v,'lead-field');await click('five.leads.confirm');});
  await tool('compensation');await click('five.process.load');
  for(const [k,v]of [['cutKerf','0.25'],['cutKerfOwner','Executor'],['cutMeasurement','ThicknessSection'],['cutCorner','Miter'],['cutMiterLimit','3.5']])await cycle('补偿 '+k,async()=>{await tool('compensation');await click('five.process.load');await input(k,v);await click('five.process.apply');});
  await tool('side');await click('five.cut.load');await cycle('留料侧',async()=>{const before=await field('retainedSide').inputValue();await input('retainedSide',before==='Interior'?'Exterior':'Interior');await click('five.cut.apply');});
 }
 if(phase==='pose'){
  await tool('extract');
  for(const [name,k,v]of [['刀轴I','0','0.1'],['刀轴J','1','0.15'],['刀轴K','2','0.85']])await cycle(name,async()=>{await select(ids[0]);await poll(id=>cv.fiveToolAxes?.get(id)?.data,ids[0]);await input('notation','IJK','axis-field');await input(k,v,'axis-field');await click('five.axis.apply');});
  await cycle('固定刀轴反向',()=>click('five.axis.reverse'));
  await cycle('全局刀轴反向',()=>page.locator('[data-command-id="five.axes.reverse-all"]').click());
 }
 if(phase==='advanced'){
  await tool('adjust');await click('five.process.load');
  for(const [k,v]of [['cutPoseInitialization','TangentInSurface'],['cutLeadTilt','5'],['cutSideTilt','3'],['cutLeadTolerance','2'],['cutSideTolerance','2.5'],['cutAngularTolerance','4'],['cutNormalPreference','0.7'],['cutTransition','DirectionChange'],['cutPoseInitialization','FixedDirection'],['cutAxisI','0.1'],['cutAxisJ','0.2'],['cutAxisK','0.9']])await cycle('连续姿态 '+k,async()=>{await tool('adjust');await click('five.process.load');let next=v;if(await field(k).inputValue()===String(v)){next=await field(k).evaluate(e=>e.tagName==='SELECT'?[...e.options].find(o=>!o.disabled&&o.value!==e.value).value:String(Number(e.value)+.125));}await input(k,next);await click('five.process.apply');});
  await tool('extract');await cycle('欧拉角 X',async()=>{await poll(id=>cv.fiveToolAxes?.get(id)?.data,ids[0]);await input('notation','Euler','axis-field');await input('angle-0','10','axis-field');await click('five.axis.apply');});
  await cycle('欧拉角 Y',async()=>{await poll(id=>cv.fiveToolAxes?.get(id)?.data,ids[0]);await input('notation','Euler','axis-field');await input('angle-1','8','axis-field');await click('five.axis.apply');});
  await cycle('跟随法向',async()=>{await poll(id=>cv.fiveToolAxes?.get(id)?.data,ids[0]);await input('mode','SurfaceNormal','axis-field');await click('five.axis.apply');});
 }
 if(phase==='points'){
  await click('five.points.edit');await pointReady();await input('notation','IJK','point-field');
  for(const [k,v]of [['0','0.1'],['1','0.2'],['2','0.95'],['transition','DirectionChange']])await cycle('起点关键姿态 '+k,async()=>{await pointReady();await input('notation','IJK','point-field');await input(k,v,'point-field');await click('five.points.apply');});
  await cycle('终点关键姿态',async()=>{await pointReady();await input('point','end','point-field');await pointReady();await input('0','0.25','point-field');await click('five.points.apply');});
  await cycle('当前关键姿态反向',async()=>{await pointReady();await click('five.points.reverse');await click('five.points.apply');});
  await cycle('实际点选新增关键点',async()=>{await click('five.points.add');await pickCut(ids[0],.42);await pointReady();await input('0','0.15','point-field');await click('five.points.apply');});
  const manual=await page.evaluate(()=>cv.fiveAxisPointEdit.points.find(p=>!p.system&&!p.sampled).id);
  await cycle('删除局部关键点',async()=>{await pointReady();await input('point',manual,'point-field');await pointReady();await page.locator('[data-five-point-field="point"]').evaluate(n=>n.blur());await page.keyboard.press('Delete');await idle();await pointReady();});
  await cycle('恢复起点默认姿态',async()=>{await pointReady();await input('point','start','point-field');await pointReady();await click('five.points.reset');});
  await cycle('关键点跟随范围与权重',async()=>{await pointReady();await click('five.points.influence-open');await poll(()=>!!cv.fiveAxisPointEdit?.influence);await page.locator('[data-five-command^="five.points.influence-toggle:"]:enabled').nth(2).click();await input('influence-weight','35','point-field');await input('influence-interpolation','Linear','point-field');await click('five.points.apply');});
 }
 if(phase==='structure'){
  await tool('path-hole');await cycle('标准孔与轮廓切换',async()=>{await input('cutType','StandardHole','hole-field');await click('five.hole.confirm');});
  await tool('path-hole');await cycle('标准孔改回轮廓',async()=>{await input('cutType','Contour','hole-field');await click('five.hole.confirm');});
  await tool('path-start');await cycle('实际点击修改起点',()=>pickCut(ids[0],.3));
  await tool('manual-order');await cycle('手动排序确认',async()=>{await select(ids[2]);await select(ids[1]);await click('five.order.apply');});
  await tool('manual-order');await select(ids[1]);await cycle('排序面板前移使用原生历史',()=>click('five.order.redo'));
  await cycle('排序面板后移使用原生历史',()=>click('five.order.undo'));
  await select(ids[0]);await cycle('清除 Kerf',async()=>{await tool('clear');for(const id of ['Leads','Overcut','Gap','Kerf'])await page.locator(`[data-five-command="five.clear.category.${id}"]`).setChecked(id==='Kerf');await click('five.clear.apply');});
  await tool('extract');const autoBefore=await page.evaluate(()=>cv.scene.domain.setups[0].definitionOrder[0]);await select(autoBefore);await click('five.tree.Down');await cycle('自动排序',()=>tool('auto-order'));
  await tool('extract');const move=await page.evaluate(()=>cv.scene.domain.setups[0].definitionOrder.slice(0,2));await select(move[1]);await cycle('真实拖动调整刀路顺序',async()=>{await page.locator(`[data-five-intent="${move[1]}"]`).dragTo(page.locator(`[data-five-intent="${move[0]}"]`),{targetPosition:{x:40,y:3}});});
  await select(ids[0]);
  for(const category of ['Leads'])await cycle('清除 '+category,async()=>{await tool('clear');for(const id of ['Leads','Overcut','Gap','Kerf'])await page.locator(`[data-five-command="five.clear.category.${id}"]`).setChecked(id===category);await click('five.clear.apply');});
  await tool('split');await cycle('实际点选打断刀路',async()=>{await pickCut(ids[0],.55);await click('five.split.confirm');});
  await tool('extract');await select(ids[1]);await cycle('删除刀路',()=>click('five.tree.Delete'));
 }
 if(phase==='creation'){
  await tool('extract');await cycle('批量删除全部刀路',async()=>{await select(ids[0]);for(const id of ids.slice(1))await select(id,['Control']);await click('five.tree.Delete');});
  await cycle('从实际模型自动提取并创建刀路',async()=>{
   await page.locator('[data-five-command="five.tool.extract"]').first().click();
   await click('five.extraction.automatic');
   assert.ok(await page.locator('[data-five-candidate][aria-checked="true"]').count());
   await click('five.extraction.confirm');
  });
  const h=await history(),writes=await writeCount();await tool('generate');await idle();
  assert.equal(await page.evaluate(()=>cv.error),'');
  assert.equal((await history()).undoSteps.length,h.undoSteps.length,'Derived geometry must not add an independent undo record');
  assert.ok((await writeCount())>writes,'Generated cutting geometry must be saved');
  const geometry=await page.evaluate(async()=>{
   const T=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js');
   const g=cv.scene.domain.setups[0].generatedGeometry,lines=cv.fiveGeneratedPreview.group.children.filter(l=>l.userData.generatedIntentId),errors=[];
   for(const op of g.operations){const rendered=lines.filter(l=>l.userData.generatedIntentId===op.intentId);for(let i=0;i<rendered.length;i++){const a=[...rendered[i].geometry.attributes.position.array],m=new T.Matrix4().set(...cv.scene.domain.placements.find(p=>p.id===op.placementId).transform),e=op.parts.filter(p=>p.points?.length)[i].points.flatMap(p=>new T.Vector3(...p).applyMatrix4(m).toArray());if(a.length!==e.length||a.some((n,j)=>Math.abs(n-e[j])>1e-4))errors.push(op.intentId);}}
   return{status:g.status,operations:g.operations.length,renderedLines:lines.length,errors};
  });assert.equal(geometry.status,'Current');assert.ok(geometry.operations>0&&geometry.renderedLines>0);assert.deepEqual(geometry.errors,[]);
  report.cases.push({name:'实际生成切割与空移并自动保存',derivedWithoutExtraUndo:true,geometry});
 }
 if(phase==='interaction'){
  await tool('extract');await select(ids[0]);
  const style=await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{max-height:380px!important;overflow:auto!important}.cam-context-pane:after,.cam-info-pane:after{content:"";display:block;height:1200px}.five-cut-tree{height:160px;overflow:auto}.five-cut-tree:after{content:"";display:block;height:900px}.five-cut-parameters{height:270px;overflow:auto}.five-cut-parameters:after{content:"";display:block;height:900px}'});
  await page.evaluate(()=>{
   const proxy=cv.viewportSceneProxy,invoke=proxy.invoke.bind(proxy);
   window.cutHold=true;window.cutAckHeld=false;
   proxy.invoke=async(...args)=>{const result=await invoke(...args);if(args[0]==='CamCut.Rename'&&cutHold){cutHold=false;cutAckHeld=true;await new Promise(r=>window.cutReleaseAck=r);proxy.invoke=invoke;}return result;};
   window.cutKeep={input:document.querySelector('[data-five-cut-name]'),canvas:cv.viewport.renderer.domElement,nodes:['.cam-context-pane','.cam-info-pane','.five-cut-tree','.five-cut-parameters'].map(s=>document.querySelector(s)),events:0};
   cutKeep.input.addEventListener('cut-retained',()=>cutKeep.events++);cutKeep.canvas.addEventListener('cut-retained',()=>cutKeep.events++);
  });
  await page.locator('[data-five-cut-name]').fill('异步响应后保留交互');await page.locator('[data-five-cut-name]').press('Enter');await poll(()=>cutAckHeld);
  await page.evaluate(()=>{cutKeep.input.focus({preventScroll:true});cutKeep.input.setSelectionRange(1,5,'backward');cutKeep.nodes.forEach((n,i)=>n.scrollTop=50+i*20);});
  await page.evaluate(()=>{cutKeep.input.setSelectionRange(2,6,'forward');cutKeep.nodes.forEach((n,i)=>n.scrollTop=110+i*20);cutKeep.scrolls=cutKeep.nodes.map(n=>n.scrollTop);cutReleaseAck();});await idle();
  const interaction=await page.evaluate(()=>{
   cutKeep.input.dispatchEvent(new Event('cut-retained'));cutKeep.canvas.dispatchEvent(new Event('cut-retained'));
   return{input:cutKeep.input===document.querySelector('[data-five-cut-name]'),canvas:cutKeep.canvas===cv.viewport.renderer.domElement,focus:document.activeElement===cutKeep.input,selection:[cutKeep.input.selectionStart,cutKeep.input.selectionEnd,cutKeep.input.selectionDirection],scrolls:cutKeep.nodes.map(n=>n.scrollTop),expected:cutKeep.scrolls,events:cutKeep.events};
  });for(const k of ['input','canvas','focus'])assert.equal(interaction[k],true,k);assert.deepEqual(interaction.selection,[2,6,'forward']);assert.deepEqual(interaction.scrolls,interaction.expected);assert.ok(interaction.scrolls.every(n=>n>0));assert.equal(interaction.events,2);
  report.cases.push({name:'异步回执后的最新焦点光标滚动与节点监听器保持',interaction});await style.evaluate(n=>n.remove());
  await cycle('验证无效草稿前的合法改名',async()=>{await page.locator('[data-five-cut-name]').fill('无效草稿后应恢复此名称');await page.locator('[data-five-cut-name]').press('Enter');});
  const committed=await read();await page.locator('[data-action="undo-project"]').click();await idle();const undone=await read();
  await page.locator('[data-five-cut-name]').fill('');await page.locator('[data-five-cut-name]').press('Tab');await poll(()=>[...cv.fiveNameDrafts.values()].some(d=>d.error));
  await page.locator('[data-action="redo-project"]').click();await idle();assert.deepEqual(await read(),committed);assert.equal(await page.locator('[data-five-cut-name]').inputValue(),'无效草稿后应恢复此名称');
  await poll(id=>cv.fiveToolAxes?.get(id)?.data,ids[0]);await input('notation','IJK','axis-field');
  for(const k of ['0','1','2'])await input(k,'0','axis-field');await click('five.axis.apply');
  const historyBeforeInvalid=await history();await page.locator('[data-action="undo-project"]').click();await idle();assert.deepEqual(await read(),committed);assert.deepEqual(await history(),historyBeforeInvalid);
  report.cases.push({name:'无效名称后的重做与零向量后的撤销',invalidInputNeverCommitted:true,nativeRedoPreserved:true});
  await page.evaluate(async()=>{const{ProjectProxy}=await import('../../../iCAX-UI/ProjectProxy/ProjectProxy.mjs');const save=ProjectProxy.prototype.save;ProjectProxy.prototype.save=async function(...args){ProjectProxy.prototype.save=save;throw Error('Injected single file-write failure');};});
  await page.locator('[data-five-cut-name]').fill('自动保存失败后重试');await page.locator('[data-five-cut-name]').press('Enter');await poll(()=>!cv.pending&&!!cv.fiveCutSaveError);
  const beforeRetry=await read(),retryHistory=await history();assert.equal(beforeRetry.cuts.find(c=>c.id===ids[0]).name,'自动保存失败后重试');
  await page.locator('[data-command-id="app.save"]').click();await idle();assert.deepEqual(await read(),beforeRetry);assert.deepEqual(await history(),retryHistory);assert.equal(await page.evaluate(()=>cv.error),'');
  report.cases.push({name:'文件自动保存失败后手动重试',nativeEditAcknowledged:true,noDuplicateUndoRecord:true,errorCleared:true});
 }
 if(phase==='gizmo'){
  await click('five.points.edit');await pointReady();await pickCut(ids[0],.4);await pointReady();
  await page.evaluate(()=>{window.fixture={view:cv};});
  const {gizmoDriver}=await import('./FiveAxisPlacementGizmo.native.browser.mjs'),driver=gizmoDriver(page);
  await poll(()=>cv.viewport.transformManipulator?.control.mode==='rotate');
  await cycle('三维旋转环连续拖动',async()=>{await driver.drag('X',28,false);assert.equal(await page.locator('[data-action="undo-project"]').isDisabled(),true);await page.mouse.up();await idle();});
  await pointReady();await cycle('旋转环角度浮窗确认',async()=>{await driver.pick('Y');const angle=page.locator('[data-five-point-angle]');await angle.fill('9');await angle.press('Enter');await idle();});
  await pointReady();const before=await read(),h=await history();await driver.drag('X',20,false);await page.keyboard.press('Escape');await page.mouse.up();await idle();assert.deepEqual(await read(),before);assert.deepEqual(await history(),h);
  report.cases.push({name:'取消旋转手势不提交工程',nativeFactsAndHistoryUnchanged:true});
 }
 if(phase==='influence'){
  await page.mouse.up();await click('five.points.edit');await pointReady();if(await page.evaluate(()=>cv.fiveAxisPointEdit.dirty))await click('five.points.cancel');
  const openInfluence=async()=>{await pointReady();if(await page.locator('[data-five-command="five.points.influence-open"]').count())await click('five.points.influence-open');await poll(()=>!!cv.fiveAxisPointEdit?.influence);};
  await cycle('跟随曲线图实际拖动',async()=>{
   await openInfluence();const station=page.locator('[data-five-command^="five.points.influence-toggle:"]:enabled').nth(2);if(await station.getAttribute('aria-pressed')!=='true')await station.click();
   const graph=page.locator('.five-influence-graph'),dot=graph.locator('circle[role="slider"]').first();await dot.scrollIntoViewIfNeeded();const box=await dot.boundingBox(),area=await graph.boundingBox();assert.ok(box&&area);
   await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();await page.mouse.move(box.x+box.width/2,area.y+area.height*.45,{steps:6});
   assert.equal(await page.locator('[data-action="undo-project"]').isDisabled(),true);await page.mouse.up();assert.equal(await page.locator('[data-action="undo-project"]').isDisabled(),false);
   await click('five.points.apply');
  });
  for(const preset of ['uniform','smooth','linear'])await cycle('跟随曲线预设 '+preset,async()=>{await openInfluence();await click('five.points.influence-'+preset);await click('five.points.apply');});
 }
 if(phase==='fresh'){
  const expected=JSON.parse(fs.readFileSync(path.join(output,'saved-expected.json'),'utf8'));
  assert.deepEqual(await read(),expected.business);assert.deepEqual(json(await page.evaluate(()=>cutAuditGeometry())),expected.geometry);
  await click('five.points.edit');await pointReady();await page.evaluate(()=>cv.viewport.fitView());const scene=await visual();assert.deepEqual(scene.errors,[]);assert.ok(scene.generatedLines>0&&scene.axisInstances>0);report.cases.push({name:'全新程序进程重开',allNativeValuesEqual:true,allGeneratedPointsEqual:true,visual:scene});
 }
 if(phase==='reopen'){
  await tool('generate');await idle();assert.equal(await page.evaluate(()=>cv.error),'');
  const expected={business:await read(),geometry:json(await page.evaluate(()=>cutAuditGeometry()))};assert.equal(expected.geometry.status,'Current');fs.writeFileSync(path.join(output,'saved-expected.json'),JSON.stringify(expected,null,2));
  const old=await page.evaluate(()=>{window.cutAuditOld=cv;return __icaxAppShell.getState().activeProjectId});
  await page.locator(`[data-action="close-open-project"][data-project-id="${old}"]`).click();await page.locator('[data-project-close-choice="save"]').click();
  await poll(old=>!document.querySelector(`[data-action="close-open-project"][data-project-id="${old}"]`),old);
  await page.evaluate(file=>__icaxAppShell.openProject(file,'icax.five-axis-cam'),file);
  await poll(async file=>{if(__icaxAppShell.getState().activeProjectPath!==file)return false;const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.cv=getProjectView(__icaxAppShell.getState().activeProjectId);return cv.scene?.intentToolpaths?.length&&!cv.pending;},file);
  assert.equal(await page.evaluate(()=>cv===cutAuditOld),false);assert.deepEqual(await read(),expected.business);assert.deepEqual(json(await page.evaluate(()=>cutAuditGeometry())),expected.geometry);
  await page.evaluate(()=>__icaxAppShell.selectRibbonTab('toolpath'));await page.getByRole('tab',{name:'切割',exact:true}).click();await select(ids[0]);await tool('extract');
  const scene=await visual();assert.deepEqual(scene.errors,[]);report.cases.push({name:'实际保存关闭重开',freshProjectView:true,allNativeValuesEqual:true,visual:scene});
 }
 report.passed=true;report.actualWrites=await writeCount();report.final=await read();
 await page.screenshot({path:path.join(output,phase+'-final.png')});console.log('PASS cut edit audit '+phase+' '+report.cases.length+' cases');
})().catch(async error=>{report.error=error.stack||String(error);if(page){report.state=await page.evaluate(()=>({shell:__icaxAppShell.getState(),error:window.cv?.error,saveError:window.cv?.fiveCutSaveError})).catch(()=>null);await page.screenshot({path:path.join(output,phase+'-failure.png')}).catch(()=>{});}console.error(error);process.exitCode=1;}).finally(()=>{fs.writeFileSync(path.join(output,phase+'-report.json'),JSON.stringify(report,null,2));process.exit(report.passed?0:1);});
