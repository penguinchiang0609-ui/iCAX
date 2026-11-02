// Actual standalone CEF/native geometry, disassembly, file export and persistence.
// Only the export-directory picker is controlled; no business response is mocked.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,readFileSync,writeFileSync,readdirSync,statSync,existsSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {createHash} from 'node:crypto';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_GRILLE_EVIDENCE||'output/tests/minimal-protective-grille-20261026/native');
const cold=process.env.ICAX_GRILLE_COLD==='1';
const gateOnly=process.env.ICAX_GRILLE_GATE_ONLY==='1';
mkdirSync(root,{recursive:true});
const browser=await chromium.connectOverCDP(process.env.ICAX_GRILLE_CDP||'http://127.0.0.1:19461');
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));
assert(page);page.setDefaultTimeout(120000);
const previousCases=process.env.ICAX_GRILLE_RESUME==='1'?readdirSync(root).filter(n=>n.endsWith('-report.json')&&!['acceptance-report.json','cold-reopen-report.json','existing-instance-edited-report.json'].includes(n)).map(n=>JSON.parse(readFileSync(join(root,n),'utf8'))).filter(c=>c.passed&&c.model&&c.name):[];
const previousEdited=process.env.ICAX_GRILLE_RESUME==='1'&&readdirSync(root).includes('existing-instance-edited-report.json')?JSON.parse(readFileSync(join(root,'existing-instance-edited-report.json'),'utf8')):undefined;
const previousChecks=process.env.ICAX_GRILLE_RESUME==='1'&&readdirSync(root).includes('acceptance-report.json')?JSON.parse(readFileSync(join(root,'acceptance-report.json'),'utf8')).checks:[];
const report={passed:false,nativeBusinessMocked:false,pickerPathsControlled:true,coldProcess:cold,runtimeLabel:process.env.ICAX_GRILLE_RUNTIME_LABEL||'formal-0.2.0-original-dll',checks:previousChecks,cases:previousCases,edited:previousEdited,pageErrors:[]};
page.on('pageerror',e=>report.pageErrors.push(e.message));
const concise=c=>c.model?{name:c.name,path:c.path,frameType:c.frameType,handleSide:c.handleSide,handleClosure:c.handleClosure,handleEnabled:c.handleEnabled,passed:c.passed,runtimeLabel:c.runtimeLabel||'formal-0.2.0-original-dll',memberCount:c.model.members.length,partTypeCount:c.model.parts.length,physicalQuantity:c.model.parts.reduce((n,p)=>n+p.quantity,0),elapsedMs:c.elapsedMs,disassemblyMs:c.disassemblyMs,layout:c.layout,exports:c.files,reportPath:join(root,c.name+'-report.json')}:c;
const write=()=>writeFileSync(join(root,gateOnly?'export-gate-report.json':cold?(process.env.ICAX_GRILLE_COLD_REPORT||'cold-reopen-report.json'):'acceptance-report.json'),JSON.stringify({...report,coverage:gateOnly?{kind:'unfinished-assembly-production-export-rejection',expected:3,completed:report.checks.length}:cold?{kind:'saved-projects',expected:report.expectedSavedProjects,completed:report.cases.length}:{kind:'variant-matrix',expectedMatrixVariants:cases.length,completedMatrixVariants:report.cases.length,requestedNames:(process.env.ICAX_GRILLE_CASES||'').split(',').filter(Boolean)},cases:report.cases.map(concise),edited:report.edited?concise(report.edited):undefined},null,2));
const check=(name,details={})=>{report.checks.push({name,passed:true,...details});write();console.log(JSON.stringify({name,passed:true,...details}));};
const walk=dir=>readdirSync(dir).flatMap(n=>{const path=join(dir,n);return statSync(path).isDirectory()?walk(path):[path];});
const fileHash=path=>createHash('sha256').update(readFileSync(path)).digest('hex');
const init=()=>page.evaluate(async()=>{
 const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');
 const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{timeoutMs:120000,initialCallId:1740000000}});
 await app.getState();const product=app.getProduct('icax.tube-designer');await product.getState();
 const project=product.getProject(window.__icaxWorkbench.getState().projectId);
 window.__grilleTest={project,scene:project.getMainScene()};
});
const list=()=>page.evaluate(async()=>(await window.__grilleTest.scene.invoke('TubeDesigner.List',{})).tubeDesigner);
const invoke=(name,args)=>page.evaluate(({name,args})=>window.__grilleTest.scene.invoke(name,args),{name,args});
const open=async path=>{
 await page.reload();await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 // Saved-as snapshots retain project identity. Close the test-owned native
 // projects before opening another snapshot so reopening reads its saved data.
 const ids=await page.evaluate(async root=>{
  const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');
  const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{timeoutMs:120000,initialCallId:1760000000}});
  await app.getState();const product=app.getProduct('icax.tube-designer');const state=await product.getState();
  const prefix=root.replaceAll('\\','/').toLowerCase()+'/';
  return [...new Set((state.catalogs||[]).flatMap(c=>[c.mainProject,...(c.projects||[])]).filter(p=>p&&p.projectPath?.replaceAll('\\','/').toLowerCase().startsWith(prefix)).map(p=>p.projectId))];
 },root);
 for(const id of ids){
  const closing=page.evaluate(id=>window.__icaxAppShell.closeProject(id),id);
  await page.locator(`[data-project-close-dialog="${id}"] [data-project-close-choice="discard"]`).click();
  await closing;
 }
 await page.evaluate(path=>window.__icaxAppShell.openProject(path,'icax.tube-designer'),path);
 await page.waitForFunction(path=>{const s=window.__icaxAppShell.getState();return s.activeProjectPath.replaceAll('\\','/').toLowerCase()===path.replaceAll('\\','/').toLowerCase()&&s.pendingCount===0&&window.__icaxWorkbench?.getState().projectId===s.activeProjectId;},path);
 await init();
};
const frameTypes=['two_vertical','closed_frame','bottom_rail'];
const sides=['left','right','center_double'];
const closures=['closed','open_slot'];
const cases=frameTypes.flatMap(frameType=>sides.flatMap(handleSide=>closures.map(handleClosure=>({name:[frameType,handleSide,handleClosure].join('-'),frameType,handleSide,handleClosure}))));
cases.push(...frameTypes.map(frameType=>({name:frameType+'-without-handle',frameType,handleSide:'left',handleClosure:'closed',handleEnabled:'no'})));
cases.push({name:'two_vertical-left-mixed-male-female',frameType:'two_vertical',handleSide:'left',handleClosure:'closed',parameters:{frameWidth:40,frameDepth:30,frameWallThickness:1.5,frameCornerRadius:2,innerWidth:20,innerDepth:10,innerWallThickness:1,innerCornerRadius:1,maleHeadLength:3,maleHeadWidth:6,minimumFeatureWeb:0.2,jointMode:'male_female_half_hole'}});
const approx=(actual,want,label)=>assert(Math.abs(actual-want)<1e-5,`${label}: ${actual} != ${want}`);
const validate=(model,test,parts=true)=>{
 const p=model.product.parameters;
 assert.equal(model.product.templateId,'minimal-protective-grille');
 for(const key of ['frameType','handleSide','handleClosure'])assert.equal(p[key],test[key]);
 assert.equal(model.product.modelOutdated,false);
 const layout=model.members.find(m=>m.properties?.['tubeDesigner.minimalGrille'])?.properties['tubeDesigner.minimalGrille'];
 assert(layout,'Native members must preserve grille layout metadata');
 assert(!model.members.some(m=>/access_door|leaf|hinge/.test(m.stableKey)),'Handle clearances must not create an opening sash');
 const enabled=test.handleEnabled!=='no';
 assert.equal(layout.handleSide,enabled?test.handleSide:null);assert.equal(layout.handleClosure,enabled?test.handleClosure:null);
 const members=model.members.map(m=>({key:m.stableKey,...m.properties['assemblyFrame.member']}));
 assert(members.every(m=>m.start&&m.end&&m.axisLength>0));
 const horizontal=members.filter(m=>Math.abs(m.end[2]-m.start[2])<1e-5);
 // Real handle voids contain no horizontal member axis. Boundaries are outside.
 for(const opening of layout.clearOpenings)for(const member of horizontal){
  const z=member.start[2];if(z>opening.bottom+1e-5&&z<opening.top-1e-5)
   assert(member.end[0]<=opening.left+1e-5||member.start[0]>=opening.right-1e-5,`${member.key} crosses handle clearance`);
 }
 if(test.handleSide!=='center_double'){
  // Every uninterrupted full row is one stock, never three semantic pieces.
  for(const z of Object.values(layout.rowCenters).flat().filter(z=>!layout.clearOpenings.some(o=>z>o.bottom&&z<o.top))){
   const row=horizontal.filter(m=>Math.abs(m.start[2]-z)<1e-5);
   assert.equal(row.length,1,`Uninterrupted row at ${z} must stay one stock`);
  }
 }else{
  assert(layout.centralMullion);assert(members.some(m=>m.key==='inner.center.0001'));
 }
 const outer=side=>members.filter(m=>m.key.startsWith('frame.'+side+'.'));
 for(const side of ['left','right']){
  assert.equal(outer(side).length,enabled&&test.handleClosure==='open_slot'&&test.handleSide===side?2:1);
  if(outer(side).length===1)approx(outer(side)[0].end[2]-outer(side)[0].start[2],p.height,'Full side frame');
 }
 assert.equal(members.some(m=>m.key.startsWith('frame.top.')),test.frameType==='closed_frame');
 assert.equal(members.some(m=>m.key.startsWith('frame.bottom.')),test.frameType!=='two_vertical');
 if(parts){assert.equal(model.product.partsOutdated,false);assert(model.parts.length>0);assert.equal(model.parts.reduce((n,p)=>n+p.quantity,0),model.members.length);}
 return layout;
};
const screenshot=async name=>{
 await page.evaluate(async()=>{await window.__icaxWorkbench.waitForRenderableViewport();await window.__icaxWorkbench.setStandardView('front');await window.__icaxWorkbench.fitView();});
 await page.screenshot({path:join(root,name+'.png')});
};
const ready=()=>page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().product?.modelOutdated===false);
const uiEdit=async(key,value)=>{
 const entry=page.locator(`[data-cam-action="tube-designer-edit-scene-specification"][data-tube-designer-parameter-key="${key}"]`);
 await entry.waitFor({state:'visible'});const box=await entry.boundingBox();assert(box);
 await page.mouse.dblclick(box.x+box.width/2,box.y+box.height/2);
 const editor=page.locator(`[data-tube-designer-scene-specification-input="${key}"]`);await editor.waitFor({state:'visible'});
 if(await editor.evaluate(n=>n.tagName==='SELECT'))await editor.selectOption(String(value));
 else{await editor.fill(String(value));await editor.press('Enter');}
 await page.waitForFunction(({key,value})=>{const p=window.__icaxWorkbench.getTubeDesignerState().product;return p?.parameters[key]===value&&p.modelOutdated&&p.partsOutdated;},{key,value});
 await page.locator('[data-cam-action="tube-designer-confirm-update"]').click();await ready();
};
try{
 await page.setViewportSize({width:1600,height:1000});
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 const cdp=await page.context().newCDPSession(page);await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:true});
 if(gateOnly){
  const original=JSON.parse(readFileSync(join(root,'acceptance-report.json'),'utf8'));
  const returnCase=process.env.ICAX_GRILLE_RETURN_CASE?original.cases.find(c=>c.name===process.env.ICAX_GRILLE_RETURN_CASE):original.cases[0];
  assert(returnCase);report.returnProject=returnCase.name;await open(returnCase.path);
  const path=join(root,'unexecuted-assembly.ictd');
  await page.evaluate(path=>window.__icaxAppShell.createProject({productId:'icax.tube-designer',projectName:'Unexecuted assembly export gate',projectPath:path}),path);
  await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId===window.__icaxAppShell.getState().activeProjectId);await init();
  await invoke('TubeDesigner.GeneratePreview',{templateId:'assembly-frame-lt'});
  let model=await list();const id=model.product.entityId;assert.equal(model.product.templateId,'assembly-frame-lt');
  await invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[id]});model=await list();
  const parts=model.manufacturingGroups.find(g=>g.productEntityId===id)?.parts;assert(parts?.length>0);
  assert(parts.every(p=>!p.properties['tubeDesigner.manufacturingValidation']));
  const target=join(root,'unexecuted-assembly-rejected-export');mkdirSync(target,{recursive:true});assert.equal(readdirSync(target).length,0);
  let rejected='';try{await invoke('TubeDesigner.ExportSelected',{partEntityIds:parts.map(p=>p.entityId),targetDirectory:target});}catch(error){rejected=error.message;}
  assert.match(rejected,/装配工艺尚未完成整件配合验证/);assert.equal(readdirSync(target).length,0);
  assert((await page.evaluate(path=>window.__grilleTest.project.save(path),path)).saved);
  report.projectPath=path;report.productId=id;report.partCount=parts.length;report.nativeRejection=rejected;report.filesWritten=0;
  writeFileSync(join(root,'unexecuted-assembly-native-model.json'),JSON.stringify(model,null,2));
  check('unassigned assembly-frame-lt operations still block production export before any file is written');
  report.faultFixtures=[];
  for(const templateId of ['icax-test-grille-missing-association','icax-test-grille-wrong-target']){
   const faultPath=join(root,templateId+'.ictd');
   await page.evaluate(({name,path})=>window.__icaxAppShell.createProject({productId:'icax.tube-designer',projectName:name,projectPath:path}),{name:templateId,path:faultPath});
   await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId===window.__icaxAppShell.getState().activeProjectId);await init();
   const parameters={frameType:'two_vertical',handleSide:'left',handleClosure:'closed',frameWidth:40,frameDepth:30,frameWallThickness:1.5,frameCornerRadius:2,innerWidth:20,innerDepth:10,innerWallThickness:1,innerCornerRadius:1,maleHeadLength:3,maleHeadWidth:6,minimumFeatureWeb:0.2,jointMode:'male_female_half_hole'};
   const preview=await invoke('TubeDesigner.GenerateProductTemplatePreview',{templateId,parameters});
   await invoke('TubeDesigner.GeneratePreview',{templateId,...preview.parameters});
   let faultModel=await list();assert.equal(faultModel.product.templateId,templateId);
   const faultId=faultModel.product.entityId;await invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[faultId]});faultModel=await list();
   assert(faultModel.parts.length>0,'Fault injection must reach real manufacturing parts');
   const faultTarget=join(root,templateId+'-rejected-export');mkdirSync(faultTarget,{recursive:true});assert.equal(readdirSync(faultTarget).length,0);
   let faultError='';try{await invoke('TubeDesigner.ExportSelected',{partEntityIds:faultModel.parts.map(p=>p.entityId),targetDirectory:faultTarget});}catch(error){faultError=error.message;}
   assert.match(faultError,/装配工艺尚未完成整件配合验证/);assert.equal(readdirSync(faultTarget).length,0);
   assert((await page.evaluate(path=>window.__grilleTest.project.save(path),faultPath)).saved);
   const detailPath=join(root,templateId+'-native-model.json');writeFileSync(detailPath,JSON.stringify(faultModel,null,2));
   report.faultFixtures.push({templateId,projectPath:faultPath,nativeModelPath:detailPath,partCount:faultModel.parts.length,nativeRejection:faultError,filesWritten:0,passed:true});
   check('processed participants cannot mask an unassociated or wrongly associated connection',{templateId});
  }
  await open(returnCase.path);await screenshot(returnCase===original.cases[0]?'default-final-front':'gate-return-front');
 }else if(cold){
  const original=JSON.parse(readFileSync(join(root,'acceptance-report.json'),'utf8'));
  const requested=(process.env.ICAX_GRILLE_COLD_CASES||'').split(',').filter(Boolean);
  const snapshots=[...original.cases,...(original.edited?[original.edited]:[])].filter(c=>!requested.length||requested.includes(c.name));
  assert(snapshots.length>0);report.requestedSavedProjects=requested;report.fullSavedMatrix=requested.length===0;
  report.expectedSavedProjects=snapshots.length;
  for(const summary of snapshots){
   const previous=summary.reportPath?JSON.parse(readFileSync(summary.reportPath,'utf8')):summary;
   await open(previous.path);const model=await list();validate(model,previous);
   assert.equal(model.product.entityId,previous.model.product.entityId);
   assert.deepEqual(model.product.parameters,previous.model.product.parameters,'Cold reopen must preserve every saved parameter');
   assert.deepEqual(model.members.map(m=>m.entityId),previous.model.members.map(m=>m.entityId));
   assert.deepEqual(model.parts.map(p=>[p.entityId,p.quantity]),previous.model.parts.map(p=>[p.entityId,p.quantity]));
   report.cases.push({name:previous.name,path:previous.path,passed:true,members:model.members.length,types:model.parts.length});write();
  }
  check('cold process preserves all selected saved projects',{count:report.cases.length,editedInstanceVerified:!!original.edited&&snapshots.some(c=>c.name===original.edited.name)});
  if(!requested.length){
   await open(original.cases[0].path);
   await page.locator('[data-cam-action="tube-designer-toggle-specification-annotations"]').click();
   await screenshot('default-clean-front');
   await page.locator('[data-cam-action="tube-designer-toggle-specification-annotations"]').click();
  }
 }else{
  const requested=(process.env.ICAX_GRILLE_CASES||'').split(',').filter(Boolean);
  for(const [index,test] of cases.entries()){
   if(requested.length&&!requested.includes(test.name))continue;
   if(report.cases.some(c=>c.name===test.name))continue;
   const started=Date.now(),path=join(root,test.name+(process.env.ICAX_GRILLE_PROJECT_SUFFIX||'')+'.ictd');
   console.log(JSON.stringify({case:test.name,stage:'preview-create'}));
   await page.evaluate(({name,path})=>window.__icaxAppShell.createProject({productId:'icax.tube-designer',projectName:name,projectPath:path}),{name:test.name,path});
   await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId===window.__icaxAppShell.getState().activeProjectId);await init();
   const parameters={frameType:test.frameType,handleSide:test.handleSide,handleClosure:test.handleClosure,handleEnabled:test.handleEnabled??'yes',...test.parameters};
   const preview=await invoke('TubeDesigner.GenerateProductTemplatePreview',{templateId:'minimal-protective-grille',parameters});
   for(const [key,value] of Object.entries(parameters))assert.equal(preview.parameters[key],value);
   if(index===0){
    await page.locator('[data-command-id="designer.add"]').click();
    const dialog=page.locator('[data-tube-designer-add-dialog]');
    assert(await dialog.locator('[data-tube-designer-template-id="single-face-security-window"]').count());
    await dialog.locator('[data-tube-designer-template-id="minimal-protective-grille"]').click();
    await page.waitForFunction(()=>!document.querySelector('[data-cam-action="tube-designer-confirm-add"]')?.disabled);
    await dialog.screenshot({path:join(root,'add-dialog.png')});
    await dialog.locator('[data-cam-action="tube-designer-confirm-add"]').click();await dialog.waitFor({state:'hidden'});
    await ready();check('independent template selectable alongside security window in actual add dialog');
   }else await invoke('TubeDesigner.GeneratePreview',{templateId:'minimal-protective-grille',...preview.parameters});
   let model=await list();const layout=validate(model,test,false),id=model.product.entityId;
   for(const [key,value] of Object.entries(preview.parameters))assert.deepEqual(model.product.parameters[key],value,`Native/UI parameter ${key}`);
   const completePreview=await invoke('TubeDesigner.GenerateProductTemplatePreview',{templateId:'minimal-protective-grille',parameters:model.product.parameters});
   assert.deepEqual(completePreview.parameters,model.product.parameters,'Complete normalized inputs including resource snapshots must remain unchanged');
   console.log(JSON.stringify({case:test.name,stage:'disassemble',members:model.members.length}));
   const disassemblyStarted=Date.now();
   await invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[id]});model=await list();validate(model,test);
   const firstParts=model.parts.map(p=>[p.entityId,p.quantity]);
   await invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[id]});model=await list();assert.deepEqual(model.parts.map(p=>[p.entityId,p.quantity]),firstParts);
   const disassemblyMs=Date.now()-disassemblyStarted;
   assert((await page.evaluate(path=>window.__grilleTest.project.save(path),path)).saved);
   await open(path);model=await list();validate(model,test);
   const target=join(root,test.name+'-export');mkdirSync(target,{recursive:true});
   await page.evaluate(target=>{(window.icax??window.icaxNativeBridge).openDirectoryDialog=async()=>target;},target);
   await page.locator('[data-command-id="designer.export-active-product-parts"]').click();
   await page.waitForFunction(target=>document.body.innerText.includes(target)||window.__icaxAppShell.getState().error,target);
   const exportError=await page.evaluate(()=>window.__icaxAppShell.getState().error);
   assert(!exportError,`Native production export refused: ${exportError}`);
   const files=walk(target);assert.equal(files.filter(p=>p.endsWith('.step')).length,model.parts.length);assert.equal(files.filter(p=>p.endsWith('.xlsx')).length,1);
   await screenshot(test.name);
   const entry={...test,path,target,model,layout,files,runtimeLabel:report.runtimeLabel,physicalQuantity:model.parts.reduce((n,p)=>n+p.quantity,0),types:model.parts.length,elapsedMs:Date.now()-started,disassemblyMs,passed:true};
   report.cases.push(entry);writeFileSync(join(root,test.name+'-report.json'),JSON.stringify(entry,null,2));
   check('real create disassemble repeat save-reopen STEP Excel',{case:test.name,members:model.members.length,types:model.parts.length});
  }
  if(!requested.length||requested.includes(cases[0].name)){
   const base=report.cases.find(c=>c.name===cases[0].name);await open(base.path);
   const sourceBodySha256=fileHash(base.path);
   let model=await list();const id=model.product.entityId;
   await page.locator('[data-command-id="designer.disassemble"]').click();
   await page.locator('[data-cam-action="tube-designer-batch-disassembly-confirm"]').click();
   await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().parts.length>0&&!window.__icaxWorkbench.getTubeDesignerState().product.partsOutdated);
   model=await list();validate(model,base);check('default real UI disassembly updates displayed part list',{members:model.members.length,parts:model.parts.length});
   const template=model.templates.find(t=>t.id==='minimal-protective-grille');
   for(const key of ['handleSide','handleClosure','handleBottom','handleWidth','handleHeight','handleCenterOffset'])assert(!template.extensions.parameterDependencies.creationOnly.includes(key));
   await screenshot('before-existing-instance-edit');
   for(const [key,value] of [['handleSide','right'],['handleBottom',420],['handleHeight',210],['handleWidth',190]]){
    await uiEdit(key,value);model=await list();assert.equal(model.product.entityId,id);
    await invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[id]});model=await list();
    validate(model,{...base,handleSide:'right'});check('existing instance scene annotation edit',{key,value,productId:id});
    if(key==='handleSide'){
     const blocker=join(root,'saveas-failure-blocker');writeFileSync(blocker,'Owned ordinary file blocks a Save As directory.',{flag:'wx'});
     const failedPath=join(blocker,'must-not-be-created.ictd'),journal=base.path+'.log';
     const journalBefore={sha256:fileHash(journal),bytes:statSync(journal).size};
     let failure='';try{await page.evaluate(path=>window.__grilleTest.project.save(path),failedPath);}catch(error){failure=error.message;}
     assert(failure,'Saving below an ordinary file must fail');assert(!existsSync(failedPath));
     const nativePath=await page.evaluate(async()=>{const p=window.__grilleTest.project;await p.product.getState();return p.product.getProject(p.projectId).state.projectPath;});
     assert.equal(nativePath.replaceAll('\\','/').toLowerCase(),base.path.replaceAll('\\','/').toLowerCase());
     assert.equal(fileHash(journal),journalBefore.sha256,'Failed save must preserve the source recovery journal');
     assert.equal(fileHash(base.path),sourceBodySha256,'Failed save must preserve the original project body');
     report.failedSaveAsProbe={failedPath,nativePath,failure,journalBefore,journalAfter:{sha256:fileHash(journal),bytes:statSync(journal).size},passed:true};
     check('failed Save As preserves current path original file and recovery journal');
    }
   }
   const path=join(root,'existing-instance-edited.ictd');
   const journalSize=file=>existsSync(file+'.log')?statSync(file+'.log').size:null;
   report.saveAsJournal={source:base.path,destination:path,sourceBytesBefore:journalSize(base.path)};
   assert((await page.evaluate(path=>window.__grilleTest.project.save(path),path)).saved);
   Object.assign(report.saveAsJournal,{sourceBytesAfter:journalSize(base.path),destinationBytes:journalSize(path)});write();
   assert.equal(fileHash(base.path),sourceBodySha256,'Successful Save As must not rewrite the original project body');
   Object.assign(report.saveAsJournal,{sourceBodySha256Before:sourceBodySha256,sourceBodySha256After:fileHash(base.path)});
   check('successful Save As preserves original file body after further editing');
   await open(path);model=await list();validate(model,{...base,handleSide:'right'});
   await screenshot('existing-instance-edited');
   report.edited={name:'existing-instance-edited',frameType:base.frameType,handleSide:'right',handleClosure:base.handleClosure,path,model,runtimeLabel:report.runtimeLabel,passed:true};
   writeFileSync(join(root,'existing-instance-edited-report.json'),JSON.stringify(report.edited,null,2));
   let rejected='';try{await invoke('TubeDesigner.UpdateProductParameters',{productEntityId:id,parameters:{...model.product.parameters,frameType:'closed_frame'}});}catch(e){rejected=e.message;}
   assert.match(rejected,/结构.*固定/);check('frame structure locked while handle layout remains editable');
  }
 }
 assert.deepEqual(report.pageErrors,[]);report.runPassed=true;
 if(!gateOnly&&!cold)report.pendingVariants=cases.filter(t=>!report.cases.some(c=>c.name===t.name)).map(t=>t.name);
 report.passed=gateOnly||cold||report.pendingVariants.length===0;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:join(root,cold?'cold-failure.png':'failure.png')}).catch(()=>{});}
finally{write();console.log(JSON.stringify({passed:report.passed,error:report.error,cases:report.cases.length}));await browser.close();}
