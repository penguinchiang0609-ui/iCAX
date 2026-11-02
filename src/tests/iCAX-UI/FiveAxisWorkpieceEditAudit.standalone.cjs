// Actual standalone CEF controls, native Database history and persisted projects.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const output=path.resolve('output/tests/workpiece-edit-audit-20261023'),file=path.join(output,'workpiece-audit.i5cam').replaceAll('\\','/'),phase=process.argv[2]||'core';
fs.mkdirSync(output,{recursive:true});if(!fs.existsSync(file))fs.copyFileSync('output/tests/cut-edit-audit-20261023/cut-audit.i5cam',file);
const report={phase,passed:false,cases:[],scope:'Standalone CEF UI and real native SDO, fixed 1600x950 CEF viewport, native facts and rendered geometry verified after history; project file writes verified.'};let page;
if(process.argv.includes('--resume'))report.cases=JSON.parse(fs.readFileSync(path.join(output,phase+'-report.json'),'utf8')).cases;
const clean=value=>Array.isArray(value)?value.map(clean):value&&typeof value==='object'?Object.fromEntries(Object.entries(value).filter(([k])=>!['expected','inputRevision','epoch','revision','renderVersion','derivedEpoch','sampleGeometryRef'].includes(k)).map(([k,v])=>[k,clean(v)])):value;
(async()=>{
 const browser=await chromium.connectOverCDP('http://127.0.0.1:9485');page=browser.contexts()[0].pages()[0];page.setDefaultTimeout(30000);await page.setViewportSize({width:1600,height:950});
 const poll=async(fn,arg,label='UI remained busy')=>{for(let i=0;i<1800;i++){if(await page.evaluate(fn,arg))return;await new Promise(r=>setTimeout(r,50));}throw Error(label);};
 await poll(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(file=>__icaxAppShell.openProject(file,'icax.five-axis-cam'),file);
 const bind=()=>poll(async file=>{if(__icaxAppShell.getState().activeProjectPath!==file)return false;const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.wv=getProjectView(__icaxAppShell.getState().activeProjectId);window.fixture={view:wv};return !!wv.scene?.domain&&!wv.pending;},file);
 await bind();await page.evaluate(async()=>{const{ProjectProxy}=await import('../../../iCAX-UI/ProjectProxy/ProjectProxy.mjs');if(!window.workpieceWrites){window.workpieceWrites=[];const save=ProjectProxy.prototype.save;ProjectProxy.prototype.save=async function(file){const result=await save.call(this,file);workpieceWrites.push({file,at:Date.now()});return result;};}});
 const idle=()=>poll(()=>!wv.pending&&!wv.fixtureRequest&&!wv.fiveHistoryBusy&&!wv.fiveSetupRequest&&!wv.fivePreparationRequest&&!wv.fiveWorkpieceSwitching&&!wv.fiveAxisImportActive&&!__icaxAppShell.getState().pendingCount);
 const check=async()=>{await idle();assert.equal(await page.evaluate(()=>wv.error||wv.fiveWorkpieceSaveError||''),'');};
 const command=async id=>{await page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);await check();};
 const click=async id=>{await page.locator(`[data-five-command="${id}"],[data-command-id="${id}"]`).first().click();await check();};
 const input=async(selector,value,key='Enter')=>{const e=page.locator(selector);await e.fill(String(value));if(key)await e.press(key);await check();};
 const read=()=>page.evaluate(()=>wv.viewportSceneProxy.invoke('CamScene.ReadState',{}));
 const business=async()=>{const s=await read();return clean(Object.fromEntries(['workpieces','placements','setups','fixtureUnits','fixtures','intentToolpaths','job'].filter(k=>s[k]!==undefined).map(k=>[k,s[k]])));};
 const history=()=>page.evaluate(()=>wv.viewportSceneProxy.getUndoRedoState());
 const writes=()=>page.evaluate(()=>workpieceWrites.length);
 const select=async p=>click('five.setup.activate:'+p);
 const visual=async()=>{
  await poll(()=>!wv.pending&&wv.viewport?.sceneObjects.size>0);
  return page.evaluate(()=>{
   const errors=[],objects=[],s=wv.scene.domain,v=wv.viewport;
   for(const p of s.placements){const o=v.sceneObjects.get(p.id);if(!o){if(p.id===wv.scene.activePlacementId)errors.push('missing active placement '+p.id);continue;}
    const matrix=o.matrix.clone().transpose().toArray();if(matrix.some((n,i)=>Math.abs(n-p.transform[i])>1e-5))errors.push('placement matrix '+p.id);
    const a=o.geometry?.attributes.position?.array;let hash=2166136261;if(a)for(const n of a)hash=Math.imul(hash^(Math.round(n*1e5)|0),16777619)>>>0;
    objects.push({id:p.id,positionCount:a?.length,hash,matrix,visible:o.visible});
   }
   for(const f of s.fixtures){const o=v.sceneObjects.get(f.id);if(!o)continue;const matrix=o.matrix.clone().transpose().toArray();if(matrix.some((n,i)=>Math.abs(n-f.transform[i])>1e-5))errors.push('fixture matrix '+f.id);const a=o.geometry?.attributes.position?.array;let hash=2166136261;if(a)for(const n of a)hash=Math.imul(hash^(Math.round(n*1e5)|0),16777619)>>>0;objects.push({id:f.id,positionCount:a?.length,hash,matrix,visible:o.visible});}
   const u=s.fixtureUnits.find(u=>u.id===wv.fiveAxisFixtures?.selected);if(u){for(const [key,i]of [['x',3],['y',7],['z',11]]){const e=document.querySelector(`[data-five-fixture-field="${key}"]`);if(e&&Math.abs(Number(e.value)-u.transform[i])>1e-5)errors.push('fixture parameter '+key);}const name=document.querySelector('[data-five-fixture-field="name"]');if(name&&name.value!==u.name)errors.push('fixture name');}
   const p=s.placements.find(p=>p.id===wv.scene.activePlacementId);if(p){for(const [key,i]of [['position-X',3],['position-Y',7],['position-Z',11]]){const e=document.querySelector(`[data-five-transform-field="${key}"]`);if(e&&Math.abs(Number(e.value)-p.transform[i])>1e-5)errors.push('parameter '+key);}}
   return{errors,objects};
  });
 };
 const record=(name,details={})=>{report.cases.push({name,...details});fs.writeFileSync(path.join(output,phase+'-report.json'),JSON.stringify(report,null,2));console.log('PASS '+name);};
 const cycle=async(name,act,{steps=1,save=true}={})=>{
  if(process.argv.includes('--resume')&&report.cases.some(c=>c.name===name))return;
  await check();const before=await business(),beforeGeometry=await visual(),count=await writes();await act();await check();const after=await business();
  assert.notDeepEqual(after,before,name+' changes native facts');if(save)assert.ok(await writes()>count,name+' automatically saves actual file');
  const afterGeometry=await visual();assert.deepEqual(afterGeometry.errors,[],name+' after visual');
  for(let i=0;i<steps;i++){await page.locator('[data-action="undo-project"]').click();await check();}
  assert.deepEqual(await business(),before,name+' native undo');const undone=await visual();assert.deepEqual(undone.errors,[],name+' undo visual');
  // Compare immutable vertex buffers for surviving workpiece entities.
  for(const o of undone.objects){const b=beforeGeometry.objects.find(b=>b.id===o.id);if(b)assert.equal(o.hash,b.hash,name+' restored vertex buffer');}
  for(let i=0;i<steps;i++){await page.locator('[data-action="redo-project"]').click();await check();}
  assert.deepEqual(await business(),after,name+' native redo');assert.deepEqual((await visual()).errors,[],name+' redo visual');
  record(name,{nativeUndo:true,nativeRedo:true,automaticFileSave:save,renderedMatricesAndFields:true});
 };
 await page.locator('[data-tab-id="workpiece"]').click();await check();let s=await read(),p=s.placements[0];if(phase!=='fresh')await select(p.id);
 if(phase==='core'){
  for(const [field,value]of [['position-X',12],['position-X',18],['position-Y',7],['position-Z',9],['rotation-X',12],['rotation-Y',8],['rotation-Z',16]])await cycle('装夹连续修改 '+field+'='+value,()=>input(`[data-five-transform-field="${field}"]`,value));
  for(const name of ['首装夹修改一','首装夹修改二'])await cycle('装夹名称 '+name,async()=>{await page.locator(`[data-five-command="five.setup.activate:${p.id}"]`).dblclick();await input('[data-five-setup-name]',name);});
  for(const action of ['lock','lock','toggle','toggle'])await cycle('装夹 '+action,()=>click('five.setup.'+action+':'+p.id));
  await cycle('新增装夹',()=>command('five.setup.create'));s=await read();const added=s.placements.find(x=>!report.originalPlacements?.includes(x.id)&&x.id===s.activePlacementId)||s.placements.at(-1);
  await cycle('复制装夹',()=>command('five.preparation.copy:setup'));
  await cycle('删除装夹',()=>command('five.setup.remove'));
  await click('five.preparation.workpiece:'+p.workpieceId);await cycle('复制工件',()=>command('five.preparation.copy:workpiece'));s=await read();const copy=s.workpieces.find(w=>w.id!==p.workpieceId),cp=s.placements.find(x=>x.workpieceId===copy.id);await select(cp.id);
  await cycle('切换工件后修改',()=>input('[data-five-transform-field="position-X"]',39));await select(p.id);assert.equal(await page.locator('[data-five-transform-field="position-X"]').inputValue(),'18');
  // History belongs to the native scene, independent of the current tree selection.
  await page.locator('[data-action="undo-project"]').click();await check();assert.equal((await read()).placements.find(x=>x.id===p.id).transform[3],18);await page.locator('[data-action="redo-project"]').click();await check();assert.equal((await read()).placements.find(x=>x.id===cp.id).transform[3],39);record('切换工件后撤销重做仍恢复原修改对象');
  for(const action of ['lock','lock','enable','enable'])await cycle('工件 '+action,()=>click('five.preparation.workpiece-action:'+action+':'+copy.id));
  await click('five.preparation.workpiece:'+copy.id);await cycle('删除工件及装夹',()=>command('workpiece.remove'));
 }
 if(phase==='scale'){
  await click('five.preparation.workpiece:'+p.workpieceId);
  for(const [axis,value]of [['X',1.1],['Y',.95],['Z',1.02]])await cycle('共享几何连续缩放 '+axis,()=>input(`[data-five-transform-field="scale-${axis}"]`,value));
 }
 if(phase==='fixtures'){
  const unit=s.fixtureUnits[0];await click('five.preparation.unit:'+unit.id);
  for(const [field,value]of [['name','工装连续修改一'],['name','工装连续修改二'],['x',15],['y',8],['z',6],['rx',5],['ry',7],['rz',9]])await cycle('工装 '+field+'='+value,async()=>{await input(`[data-five-fixture-field="${field}"]`,value,null);await click('five.fixture.apply');});
  for(const [i,action] of ['lock','lock','enable','enable'].entries())await cycle('工装 '+action+' '+i,()=>click('five.preparation.unit-action:'+action+':'+unit.id));
  await cycle('复制工装',()=>command('five.preparation.copy:fixture'));const copied=(await read()).fixtureUnits.at(-1);await click('five.preparation.unit:'+copied.id);if(copied.locked)await cycle('复制工装解锁',()=>click('five.preparation.unit-action:lock:'+copied.id));await cycle('删除工装',()=>command('five.fixture.remove'));
 }
 if(phase==='import'){
  const {selectModelFile}=await import('./fixtures/fiveAxisFilePicker.mjs');
  const pick=async(action,source)=>{const opening=page.locator(`[data-command-id="${action}"],[data-five-command="${action}"]`).first().click();await selectModelFile(page,path.resolve(source),{preview:true});await opening;await check();};
  await cycle('真实文件选择与工件导入',()=>pick('workpiece.import','TestData/real-sheet-metal/pololu-stamped-bracket/Pololu-2257-stamped-bracket.step'));
  await select(p.id);
  await cycle('真实文件选择与工装导入',async()=>{await command('five.fixture.add');await click('five.fixture.library.choose');await pick('five.fixture.library.import','samples/structural-steel/vendors-angle-channel/tekspan-60300-cranked-t-plate.step');});
 }
 if(phase==='library'){
  await command('five.fixture.library.open');
  const library=async()=>clean(await page.evaluate(async()=>{const s=await wv.viewportSceneProxy.invoke('CamFixture.LibraryList',{});return{defaultId:s.defaultId,entries:s.entries.map(({id,name,source,partCount,isDefault})=>({id,name,source,partCount,isDefault})).sort((a,b)=>a.id.localeCompare(b.id))};}));
  const libCycle=async(name,act)=>{const before=await library(),engineering=await business();await act();await check();const after=await library();assert.notDeepEqual(after,before);await page.locator('[data-action="undo-project"]').click();await check();assert.deepEqual(await library(),before);await page.locator('[data-action="redo-project"]').click();await check();assert.deepEqual(await library(),after);assert.deepEqual(await business(),engineering);record(name,{frameworkProductHistory:true,persistedLibrary:true,engineeringUnchanged:true});};
  const {selectModelFile}=await import('./fixtures/fiveAxisFilePicker.mjs');
  await libCycle('工装库导入',async()=>{const action=page.locator('[data-five-command="five.fixture.library.import"]').click();await selectModelFile(page,path.resolve('samples/structural-steel/vendors-angle-channel/tekspan-60235-saddle-bracket.step'),{preview:true});await action;});
  let id=await page.evaluate(()=>wv.fiveFixtureLibrary.selected);
  for(const name of ['库名称连续修改一','库名称连续修改二'])await libCycle('工装库名称 '+name,async()=>{await page.locator(`[data-five-library-select="${id}"]`).dblclick();await input(`[data-five-library-field="${id}"]`,name);});
  await libCycle('设为默认工装',()=>click('five.fixture.library.default:'+id));await libCycle('取消默认工装',()=>click('five.fixture.library.default:'+id));
  await libCycle('删除库工装与恢复',()=>click('five.fixture.library.delete:'+id));
  await page.locator('[data-action="undo-project"]').click();await check();
  await cycle('库工装添加到当前装夹',()=>click('five.fixture.library.use:'+id));
  assert.equal(await page.locator('[data-five-key="fixture-library"]').count(),0);
  await command('five.fixture.library.save-one');await input('[data-five-library-field="save-name"]','工程工装保存到库',null);await libCycle('当前工装保存到库',()=>click('five.fixture.library.save'));
  await click('five.fixture.library.close');
 }
 if(phase==='gizmo'){
  const {gizmoDriver}=await import('./FiveAxisPlacementGizmo.native.browser.mjs'),g=gizmoDriver(page);
  for(const tool of ['translate','rotate'])await cycle('实际鼠标拖动 '+tool,async()=>{await command('five.tool.'+tool);await page.evaluate(()=>wv.viewport.fitView());await g.drag(tool==='rotate'?'Z':'X',30,false);assert.equal(await page.locator('[data-action="undo-project"]').isDisabled(),true);await page.mouse.up();await check();assert.equal(await page.locator('[data-action="undo-project"]').isDisabled(),false);});
  const u=(await read()).fixtureUnits.find(u=>!u.locked);assert.ok(u);await click('five.preparation.unit:'+u.id);
  for(const mode of ['translate','rotate'])await cycle('工装实际鼠标拖动 '+mode,async()=>{await click('five.fixture.move-mode:'+mode);await page.evaluate(()=>wv.viewport.fitView());await g.drag(mode==='rotate'?'Z':'X',25,false);assert.equal(await page.locator('[data-action="undo-project"]').isDisabled(),true);await page.mouse.up();await check();assert.equal(await page.locator('[data-action="undo-project"]').isDisabled(),false);});
 }
 if(phase==='saveinputs'){
  const u=(await read()).fixtureUnits.find(u=>!u.locked);assert.ok(u);await click('five.preparation.unit:'+u.id);
  await cycle('保存按钮提交未应用的工装坐标',async()=>{await input('[data-five-fixture-field="x"]',Number(await page.locator('[data-five-fixture-field="x"]').inputValue())+3,null);await command('app.save');});
  await select(p.id);await cycle('保存按钮提交未应用的装夹名称',async()=>{await page.locator(`[data-five-command="five.setup.activate:${p.id}"]`).dblclick();await input('[data-five-setup-name]','保存时提交装夹名称',null);await command('app.save');});
  const before=await business();await page.evaluate(async()=>{const{ProjectProxy}=await import('../../../iCAX-UI/ProjectProxy/ProjectProxy.mjs');window.wpOriginalSave=ProjectProxy.prototype.save;window.wpFailSave=true;ProjectProxy.prototype.save=async function(...args){if(wpFailSave)throw Error('audit write failure');return wpOriginalSave.apply(this,args);};});
  const field=page.locator('[data-five-transform-field="position-X"]'),value=Number(await field.inputValue())+2;await field.fill(String(value));await field.press('Enter');await idle();assert.match(await page.evaluate(()=>wv.fiveWorkpieceSaveError),/audit write failure/);const after=await business();assert.notDeepEqual(after,before);assert.equal(await page.evaluate(()=>!!wv.fivePlacement?.request),false);
  await page.evaluate(()=>{window.wpFailSave=false;});await command('app.save');assert.deepEqual(await business(),after);await page.locator('[data-action="undo-project"]').click();await check();assert.deepEqual(await business(),before);await page.locator('[data-action="redo-project"]').click();await check();assert.deepEqual(await business(),after);record('自动保存失败保留已提交修改，重试后撤销重做不重复提交');
 }
 if(phase==='saveextras'){
  const{gizmoDriver}=await import('./FiveAxisPlacementGizmo.native.browser.mjs'),g=gizmoDriver(page);
  await cycle('保存按钮提交轴旁数值预览',async()=>{await command('five.tool.translate');await page.evaluate(()=>wv.viewport.fitView());await g.pick('X');await g.numeric().fill('4.5');await command('app.save');});
  await command('five.fixture.library.open');const readLibrary=()=>page.evaluate(async()=>{const s=await wv.viewportSceneProxy.invoke('CamFixture.LibraryList',{});return s.entries.map(({id,name})=>({id,name})).sort((a,b)=>a.id.localeCompare(b.id));}),before=await readLibrary(),id=before[0].id,engineering=await business();
  await page.locator(`[data-five-library-select="${id}"]`).dblclick();await input(`[data-five-library-field="${id}"]`,'保存时提交工装库名称',null);await command('app.save');const after=await readLibrary();assert.notDeepEqual(after,before);assert.equal(await page.locator(`[data-five-library-field="${id}"]`).count(),0);
  await page.locator('[data-action="undo-project"]').click();await check();assert.deepEqual(await readLibrary(),before);await page.locator('[data-action="redo-project"]').click();await check();assert.deepEqual(await readLibrary(),after);assert.deepEqual(await business(),engineering);await click('five.fixture.library.close');record('保存时提交工装库名称，库历史与工程历史隔离');
 }
 if(phase==='librarybulk'){
  const units=(await read()).fixtureUnits.filter(u=>u.setupId===p.setupId);assert.ok(units.length>1);await click('five.preparation.unit:'+units[0].id);await click('five.fixture.library.save-all');await input('[data-five-library-field="save-name"]','组合工装历史验收',null);
  const engineering=await business(),readLibrary=()=>page.evaluate(async()=>{const s=await wv.viewportSceneProxy.invoke('CamFixture.LibraryList',{});return s.entries.map(({id,name,partCount})=>({id,name,partCount})).sort((a,b)=>a.id.localeCompare(b.id));}),before=await readLibrary();await click('five.fixture.library.save');const after=await readLibrary();assert.equal(after.find(e=>e.name==='组合工装历史验收').partCount,units.reduce((n,u)=>n+u.partIds.length,0));
  await page.locator('[data-action="undo-project"]').click();await check();assert.deepEqual(await readLibrary(),before);await page.locator('[data-action="redo-project"]').click();await check();assert.deepEqual(await readLibrary(),after);assert.deepEqual(await business(),engineering);await click('five.fixture.library.close');record('多个导入和生成工装保存组合到库，完整撤销重做且不修改工程');
 }
 if(phase==='alignment'){
  const {frameAlignmentDriver}=await import('./FiveAxisFrameAlignment.native.browser.mjs'),d=await frameAlignmentDriver(page,'../../../');
  await cycle('实际取面、方向与原点后重置自身坐标系',async()=>{await click('five.preparation.workpiece:'+p.workpieceId);await command('five.tool.align');await poll(()=>wv.sceneSnapping?.catalogs.has(wv.fiveAlignment?.placementId)&&!wv.sceneSnapping?.pending.size);await d.pick('plane');await d.action('next');await d.action('basis-u');await d.action('next');await d.action('plane-center');await d.action('apply');});
 }
 if(phase==='locating'){
  await command('five.tool.registration');
  await cycle('实际选择三个 CAD 特征并保存定位',async()=>{
   await click('five.locating.pick:0');await poll(()=>wv.sceneSnapping?.catalogs.has(wv.scene.activePlacementId));
   for(let i=0;i<3;i++){
    const target=await page.evaluate(async()=>{
     const T=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js'),{sceneSnapCandidate}=await import('../../../apps/five-axis-cam/webpage/sceneSnapping.mjs'),v=wv.viewport,id=wv.scene.activePlacementId,o=v.sceneObjects.get(id),cat=wv.sceneSnapping.catalogs.get(id),points=wv.fiveLocating.points.filter(Boolean).map(p=>new T.Vector3(...p.point));
     for(const eye of [[0,0,1],[0,0,-1],[1,-1,1],[-1,1,1],[1,0,0],[0,1,0]]){v.setViewDirection(eye);v.fitView(1.3);v.scene.updateMatrixWorld(true);const r=v.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster();
      for(const f of cat.features.filter(f=>['endpoint','center','datum-point','midpoint'].includes(f.mode))){
       const pos=new T.Vector3(...f.point);if(points.some(p=>p.distanceTo(pos)<2))continue;if(points.length===2&&points[1].clone().sub(points[0]).cross(pos.clone().sub(points[0])).length()<2)continue;
       const p=pos.clone().applyMatrix4(o.matrixWorld).project(v.camera),x=r.left+(p.x+1)*r.width/2,y=r.top+(1-p.y)*r.height/2;if(document.elementFromPoint(x,y)!==v.renderer.domElement)continue;
       ray.setFromCamera({x:(x-r.left)/r.width*2-1,y:1-(y-r.top)/r.height*2},v.camera);const hit=ray.intersectObject(o,false)[0],c=sceneSnapCandidate({},wv,{objectId:id,allowedModes:['endpoint','midpoint','center','sphere-center','datum-point','point']},hit,{clientX:x,clientY:y});if(c?.result?.featureId===f.featureId)return{x,y,id:f.featureId};
      }
     }throw Error('No distinct visible CAD feature');
    });await page.mouse.move(target.x,target.y);await poll(id=>wv.fiveLocating.hover?.query.featureId===id,target.id);await page.mouse.click(target.x,target.y);await check();
   }
   await click('five.locating.save');
  });
  for(const offset of [10,20])await cycle('连续修改实测三点 '+offset,async()=>{
   const points=await page.evaluate(()=>{const m=wv.scene.domain.placements.find(p=>p.id===wv.scene.activePlacementId).transform;return wv.fiveLocating.points.map(p=>[0,1,2].map(i=>m[i*4+3]+p.point.reduce((n,v,j)=>n+v*m[i*4+j],0)));});
   for(let i=0;i<3;i++)for(let j=0;j<3;j++)await input(`[data-five-locating-field="${i}:${j}"]`,points[i][j]+(j===0?offset:0),null);await click('five.locating.save');
  });
  await cycle('定位误差参数',async()=>{await input('[data-five-locating-field="tolerance"]',.25,null);await click('five.locating.save');});
 }
 if(phase==='sequence'){
  const a=s.setups.filter(x=>s.placements.some(p=>p.setupId===x.id&&p.workpieceId===s.placements[0].workpieceId));assert.ok(a.length>1);
  await cycle('左侧树拖动装夹顺序',async()=>{const from=page.locator(`[data-five-setup-drag="${a[1].id}"]`),to=page.locator(`[data-five-setup-drag="${a[0].id}"]`);await from.dragTo(to,{targetPosition:{x:30,y:3}});});
 }
 if(phase==='generation'){
  if(!process.argv.includes('--resume')){
  await command('five.fixture.generate');await poll(()=>wv.fixtureDesignPicking&&wv.fixtureDesignFaces?.length);
  const xy=await page.evaluate(async()=>{
   const T=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=wv.viewport,o=v.sceneObjects.get(wv.scene.activePlacementId),g=o.geometry;v.setViewDirection([0,0,-1]);v.fitView(1.35);v.scene.updateMatrixWorld(true);const r=v.renderer.domElement.getBoundingClientRect(),ray=new T.Raycaster();
   for(const face of wv.fixtureDesignFaces){for(let i=Number(face.first);i<Number(face.first)+Number(face.count);i++){
    const p=new T.Vector3();for(let j=0;j<3;j++)p.add(new T.Vector3().fromBufferAttribute(g.attributes.position,g.index?g.index.getX(i*3+j):i*3+j));p.multiplyScalar(1/3).applyMatrix4(o.matrixWorld).project(v.camera);const x=r.left+(p.x+1)*r.width/2,y=r.top+(1-p.y)*r.height/2;if(document.elementFromPoint(x,y)!==v.renderer.domElement)continue;ray.setFromCamera({x:p.x,y:p.y},v.camera);const hit=ray.intersectObject(o,false)[0];if(hit?.faceIndex===i)return{x,y};
   }}throw Error('No selectable support face');
  });await page.mouse.click(xy.x,xy.y);await check();assert.ok(await page.evaluate(()=>wv.fixtureDesignDraft.supportFaces.length));await click('five.fixture.design.pick');
  const field=k=>page.locator(`[data-five-fixture-field="design:${k}"]`);await field('layout').selectOption('Manual');await field('positions').fill('-15, 0, 15');await page.locator('[data-five-key="design-options"]>summary').click();await field('jointMode').selectOption('Weld');
  }
  const field=k=>page.locator(`[data-five-fixture-field="design:${k}"]`);
  await cycle('夹具参数保存与实际生成',()=>click('five.fixture.design.generate'),{steps:2});
  await cycle('连续修改夹具参数后重新生成',async()=>{await click('five.fixture.design.page:parameters');await field('baseThickness').fill('6');await field('margin').fill('18');await click('five.fixture.design.generate');},{steps:2});
  await cycle('采用生成的夹具单元',()=>click('five.fixture.design.confirm'));
  const generated=(await read()).fixtureUnits.find(u=>u.kind==='Generated');assert.ok(generated);await click('five.preparation.unit:'+generated.id);
  await cycle('生成夹具拆单',()=>click('five.fixture.design.breakdown'));
  const delivery=path.join(output,'fixture-delivery').replaceAll('\\','/');fs.mkdirSync(delivery,{recursive:true});await page.evaluate(p=>{(globalThis.icax??globalThis.icaxNativeBridge).openDirectoryDialog=async()=>p;},delivery);
  await cycle('导出下料包的工程交付记录',()=>click('five.fixture.design.export'));assert.ok(fs.readdirSync(delivery).length);record('下料文件已产生；工程撤销恢复交付记录，保留已导出的外部文件');
 }
 if(phase==='check'){
  await command('five.fixture.focus.check');await cycle('干涉检查结果和间隙自动保存',()=>click('five.fixture.check'));await cycle('修改间隙后重新检查',async()=>{await input('[data-five-fixture-field="clearance"]',3,null);await click('five.fixture.check');});
 }
 if(phase==='interaction'){
  await page.addStyleTag({content:'.wp-scroll-test .cam-context-pane::after,.wp-scroll-test .cam-info-pane::after{content:"";display:block;height:1100px}'});
  await page.evaluate(()=>{document.body.classList.add('wp-scroll-test');const proxy=wv.viewportSceneProxy,invoke=proxy.invoke.bind(proxy);window.wpOriginalInvoke=invoke;let hold=true;proxy.invoke=async(method,...args)=>{const result=await invoke(method,...args);if(method==='CamScene.ReadState'&&hold){hold=false;await new Promise(r=>window.wpRelease=r);}return result;};window.wpRefreshing=__icaxAppShell.executeRibbonCommand('five.refresh');});
  await poll(()=>!!window.wpRelease);
  const scrolls=await page.evaluate(()=>{const input=document.querySelector('[data-five-transform-field="position-X"]');window.wpKeptInput=input;window.wpKeptCanvas=wv.viewport.renderer.domElement;window.wpListenerCalls=0;input.addEventListener('retention-probe',()=>wpListenerCalls++);input.focus({preventScroll:true});input.setSelectionRange(0,1);const scrolls=['.cam-context-pane','.cam-info-pane'].map((sel,i)=>{const n=document.querySelector(sel);n.scrollTop=80+i*50;return n.scrollTop;});wpRelease();return scrolls;});
  await page.evaluate(()=>wpRefreshing);await check();assert.equal(await page.evaluate(()=>{wpKeptInput.dispatchEvent(new Event('retention-probe'));return wpKeptInput.isConnected&&document.activeElement===wpKeptInput&&wpKeptInput.selectionStart===0&&wpKeptInput.selectionEnd===1&&wpListenerCalls===1&&wpKeptCanvas===wv.viewport.renderer.domElement;}),true);
  assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane'].map(sel=>document.querySelector(sel).scrollTop)),scrolls);await page.evaluate(()=>{wv.viewportSceneProxy.invoke=wpOriginalInvoke;document.body.classList.remove('wp-scroll-test');});record('异步刷新保留输入节点、画布、监听器、光标和晚到的两栏滚动');
  await command('five.fixture.focus.design');const field=k=>page.locator(`[data-five-fixture-field="design:${k}"]`);await field('sizing').selectOption('Manual');await field('baseShape').selectOption('Rectangle');await page.locator('[data-five-key="design-options"]>summary').click();
  await page.evaluate(()=>{document.body.classList.add('wp-scroll-test');const proxy=wv.viewportSceneProxy,invoke=proxy.invoke.bind(proxy);window.wpOriginalInvoke=invoke;let hold=true;proxy.invoke=async(method,...args)=>{const result=await invoke(method,...args);if(method==='CamScene.ReadState'&&hold){hold=false;await new Promise(r=>window.wpRelease=r);}return result;};window.wpRelease=null;window.wpRefreshing=__icaxAppShell.executeRibbonCommand('five.refresh');});await poll(()=>!!wpRelease);
  await field('baseShape').selectOption('Circle');assert.equal(await field('areaDepth').count(),0);await field('margin').fill('19');
  const nested=await page.evaluate(()=>{const e=document.querySelector('[data-five-fixture-field="design:margin"]');wpKeptInput=e;wpKeptCanvas=wv.viewport.renderer.domElement;wpListenerCalls=0;e.addEventListener('retention-probe',()=>wpListenerCalls++);e.focus({preventScroll:true});e.setSelectionRange(0,1);const a=['.cam-context-pane','.cam-info-pane','.five-design-body'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=40+i*40;return n.scrollTop;});wpRelease();return a;});await page.evaluate(()=>wpRefreshing);await check();
  assert.equal(await page.evaluate(()=>{wpKeptInput.dispatchEvent(new Event('retention-probe'));return wpKeptInput.isConnected&&document.activeElement===wpKeptInput&&wpKeptInput.selectionStart===0&&wpKeptInput.selectionEnd===1&&wpListenerCalls===1&&wpKeptCanvas===wv.viewport.renderer.domElement;}),true);assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-design-body'].map(s=>document.querySelector(s).scrollTop)),nested);await page.evaluate(()=>{wv.viewportSceneProxy.invoke=wpOriginalInvoke;document.body.classList.remove('wp-scroll-test');});
  await page.locator('[data-action="undo-project"]').click();await check();await click('five.fixture.design.close');record('条件字段变化与异步刷新保留嵌套滚动、最新草稿及节点身份');
 }
 if(phase==='related'){
  for(const area of ['toolpath','simulation']){
   const before=await business();await page.locator(`[data-tab-id="${area}"]`).click();await check();const id=await page.evaluate(()=>__icaxAppShell.getState().activeProjectId);
   await page.locator(`[data-action="close-open-project"][data-project-id="${id}"]`).click();await page.locator('[data-project-close-choice="save"]').click();await poll(id=>!document.querySelector(`[data-action="close-open-project"][data-project-id="${id}"]`),id);await page.evaluate(file=>__icaxAppShell.openProject(file,'icax.five-axis-cam'),file);await bind();await check();assert.deepEqual(await business(),before);record(area+' 页面保存关闭与重开不受工件编辑影响');
  }
 }
 if(phase==='invalid'){
  const before=await business(),h=await history();const e=page.locator('[data-five-transform-field="position-X"]');await e.fill('bad coordinate');await e.press('Enter');await idle();assert.ok(await page.evaluate(()=>wv.error));assert.deepEqual(await business(),before);
  await page.locator('[data-action="undo-project"]').click();await check();assert.deepEqual(await business(),before);assert.deepEqual(await history(),h);assert.deepEqual((await visual()).errors,[]);
  await page.locator('[data-action="undo-project"]').click();await check();await page.locator('[data-action="redo-project"]').click();await check();assert.deepEqual(await business(),before);record('无效坐标撤销清除草稿，再撤销重做原生记录');
  const u=(await read()).fixtureUnits.find(u=>!u.locked);await click('five.preparation.unit:'+u.id);const fh=await history();await page.locator('[data-five-fixture-field="x"]').fill('invalid');await page.locator('[data-five-command="five.fixture.apply"]').click();await idle();assert.ok(await page.evaluate(()=>wv.error));await page.locator('[data-action="undo-project"]').click();await check();assert.deepEqual(await history(),fh);record('无效工装坐标不会污染原生历史');
 }
 if(phase==='reopen'||phase==='fresh'){
  const expectedPath=path.join(output,'saved-expected.json');
  if(phase==='reopen'){
   const renderedByPlacement={};for(const p of (await read()).placements){await select(p.id);renderedByPlacement[p.id]=await visual();assert.deepEqual(renderedByPlacement[p.id].errors,[]);}
   const expected={business:await business(),activeSetupId:(await read()).activeSetupId,renderedByPlacement,library:await page.evaluate(async()=>{const s=await wv.viewportSceneProxy.invoke('CamFixture.LibraryList',{});return{defaultId:s.defaultId,entries:s.entries.map(({id,name,partCount,isDefault})=>({id,name,partCount,isDefault})).sort((a,b)=>a.id.localeCompare(b.id))};}),visual:await visual()};fs.writeFileSync(expectedPath,JSON.stringify(expected,null,2));
   const old=await page.evaluate(()=>{window.oldWorkpieceView=wv;return __icaxAppShell.getState().activeProjectId;});await page.locator(`[data-action="close-open-project"][data-project-id="${old}"]`).click();await page.locator('[data-project-close-choice="save"]').click();await poll(id=>!document.querySelector(`[data-action="close-open-project"][data-project-id="${id}"]`),old);await page.evaluate(file=>__icaxAppShell.openProject(file,'icax.five-axis-cam'),file);await bind();await check();assert.equal(await page.evaluate(()=>wv===oldWorkpieceView),false);
  }
  const expected=JSON.parse(fs.readFileSync(expectedPath,'utf8'));assert.deepEqual(await business(),expected.business);assert.equal((await read()).activeSetupId,expected.activeSetupId);assert.deepEqual((await visual()).errors,[]);const library=await page.evaluate(async()=>{const s=await wv.viewportSceneProxy.invoke('CamFixture.LibraryList',{});return{defaultId:s.defaultId,entries:s.entries.map(({id,name,partCount,isDefault})=>({id,name,partCount,isDefault})).sort((a,b)=>a.id.localeCompare(b.id))};});assert.deepEqual(library,expected.library);record(phase==='fresh'?'全新独立进程重开参数、当前工件、工装库与三维一致':'实际保存关闭重开参数、当前工件、工装库与三维一致');
  for(const [id,geometry]of Object.entries(expected.renderedByPlacement)){await select(id);assert.deepEqual(JSON.parse(JSON.stringify(await visual())),geometry,'reopened rendered vertex buffers and transforms '+id);}record('重开后逐个切换全部工件和装夹，三维顶点及矩阵与保存前一致');
 }
 report.final=await business();report.visual=await visual();report.actualWrites=await writes();report.passed=true;await page.screenshot({path:path.join(output,phase+'-final.png')});
})().catch(async e=>{report.error=e.stack;console.error(e);if(page){report.state=await page.evaluate(()=>({error:window.wv?.error,saveError:window.wv?.fiveWorkpieceSaveError,shell:__icaxAppShell.getState()})).catch(()=>null);await page.screenshot({path:path.join(output,phase+'-failure.png')}).catch(()=>{});}}).finally(()=>{fs.writeFileSync(path.join(output,phase+'-report.json'),JSON.stringify(report,null,2));process.exit(report.passed?0:1);});
