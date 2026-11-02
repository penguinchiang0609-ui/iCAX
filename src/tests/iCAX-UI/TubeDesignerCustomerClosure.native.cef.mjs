// Customer G01 and remaining F10/F12 acceptance against the real CEF/native app.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,readFileSync,writeFileSync,readdirSync,statSync} from 'node:fs';
import {join,resolve} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_CUSTOMER_CLOSURE_EVIDENCE||'output/tests/customer-feedback-closure-20261023/verified');
mkdirSync(root,{recursive:true});
const reopen=process.argv.includes('--reopen');
const continueRun=process.argv.includes('--continue');
const dpiOnly=process.argv.includes('--dpi-only');
const refreshExports=process.argv.includes('--refresh-exports');
let browser;for(let i=0;!browser;i++){try{browser=await chromium.connectOverCDP('http://127.0.0.1:19427');}catch(e){if(i>=80)throw e;await new Promise(r=>setTimeout(r,250));}}
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));
// Native acceptance activates production DOM controls. Device-metric changes
// must not send desktop coordinates into a differently sized native window.
const click=async locator=>{await locator.waitFor({state:'visible'});for(let i=0;await locator.isDisabled()&&i<400;i++)await page.waitForTimeout(25);return locator.evaluate(n=>{if(n.disabled)throw new Error('Disabled production control');n.focus?.({preventScroll:true});n.click();});};
page.setDefaultTimeout(60000);
const report=continueRun||dpiOnly?JSON.parse(readFileSync(join(root,'acceptance-report.json'),'utf8')):{businessMocked:false,pickerPathsControlled:true,coldProcess:reopen,checks:[],cases:[]};
if(dpiOnly)delete report.error;
if(continueRun){delete report.error;report.checks=report.checks.filter(c=>!c.name.startsWith('multi-product')&&!c.name.startsWith('DPI')&&!c.name.startsWith('three real'));}
report.projectPath??=join(root,'G01用料复用与多实例.ictd');
report.dpiVerification={rendererEmulation:true,windowsGlobalScaleChanged:false,physicalSizes:[[1920,1080],[1366,768]],scales:[1,1.25,1.5]};
report.buttonActivation='production DOM controls; no business mocks';
report.passed=false;
const write=()=>writeFileSync(join(root,reopen?'cold-report.json':'acceptance-report.json'),JSON.stringify(report,null,2));
const check=(name,details={})=>{report.checks.push({name,...details,passed:true});write();console.log(JSON.stringify({name,passed:true}));};
const idle=()=>page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');const v=getProjectView(window.__icaxWorkbench.getState().projectId);return !v.pending&&!v.activeAreaAction&&!v.tubeDesignerExportOperation&&!v.tubeDesignerProductParameterCommitPromise;});
const settle=async()=>{await idle();await page.evaluate(()=>Promise.race([new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))),new Promise(r=>setTimeout(r,60))]));};
const product=()=>page.evaluate(()=>window.__icaxWorkbench.getTubeDesignerState().product);
const init=()=>page.evaluate(async()=>{const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{timeoutMs:120000,initialCallId:1850000000}});await app.getState();const product=app.getProduct('icax.tube-designer');await product.getState();const project=product.getProject(window.__icaxWorkbench.getState().projectId);window.__closure={product,project,scene:project.getMainScene()};});
const userData=()=>page.evaluate(()=>window.__closure.product.invoke('TubeDesigner.ListUserData',{}));
const field=k=>page.locator(`[data-tube-designer-parameter-form] [data-tube-designer-parameter="${k}"]`);
const materialSelect=()=>page.locator('[data-tube-designer-preset-selection="right"][data-tube-designer-preset-scope="materials"]');
const edit=async(k,value)=>{await field(k).focus();await field(k).fill(String(value));assert.equal(await field(k).inputValue(),String(value));await field(k).press('Tab');await page.waitForFunction(({k,value})=>window.__icaxWorkbench.getTubeDesignerState().product?.parameters[k]===value,{k,value});await settle();};
const add=async pattern=>{await click(page.locator('[data-command-id="designer.add"]'));const dialog=page.locator('[data-tube-designer-add-dialog]');await click(dialog.locator('[data-tube-designer-template-id="single-face-security-window"]'));await page.waitForFunction(()=>!document.querySelector('[data-cam-action="tube-designer-confirm-add"]')?.disabled);{await dialog.locator('[data-tube-designer-parameter="infillPattern"]').selectOption(pattern);await page.waitForFunction(()=>!document.querySelector('[data-cam-action="tube-designer-confirm-add"]')?.disabled);}const previous=(await product())?.entityId;await click(dialog.locator('[data-cam-action="tube-designer-confirm-add"]'));await page.waitForFunction(()=>!document.querySelector('[data-tube-designer-add-dialog]'));await page.waitForFunction(previous=>{const p=window.__icaxWorkbench.getTubeDesignerState().product;return p&&p.entityId!==previous;},previous);await field('frameWidth').waitFor();await settle();return product();};
const regenerate=async()=>{const before=(await product()).activeGenerationRunId;await click(page.locator('[data-cam-action="tube-designer-confirm-update"]'));await page.waitForFunction(before=>{const p=window.__icaxWorkbench.getTubeDesignerState().product;return p?.activeGenerationRunId!==before&&!p?.modelOutdated;},before);await settle();};
const walk=d=>readdirSync(d).flatMap(n=>{const p=join(d,n);return statSync(p).isDirectory()?walk(p):[p]});
const exportCurrent=async(name,disassemble=true)=>{
 if(disassemble){await click(page.locator('[data-command-id="designer.disassemble"]'));await click(page.locator('[data-cam-action="tube-designer-batch-disassembly-confirm"]'));await settle();await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().parts.length>0&&!document.querySelector('[data-command-id="designer.export-active-product-parts"]')?.disabled);}
 const model=await page.evaluate(async disassemble=>{let m=(await window.__closure.scene.invoke('TubeDesigner.List',{})).tubeDesigner;if(disassemble)await window.__closure.scene.invoke('TubeDesigner.DisassembleSelected',{productEntityIds:[m.product.entityId]});return (await window.__closure.scene.invoke('TubeDesigner.List',{})).tubeDesigner;},disassemble);
 const target=join(root,(reopen?'cold-':'')+name+'-export');mkdirSync(target,{recursive:true});
 await page.evaluate(target=>{(window.icax??window.icaxNativeBridge).openDirectoryDialog=async()=>target;},target);await click(page.locator('[data-command-id="designer.export-active-product-parts"]'));await page.waitForFunction(target=>document.body.innerText.includes(target),target);await settle();
 const files=walk(target);assert.equal(files.filter(f=>f.endsWith('.step')).length,model.parts.length);
 const entry={name,target,model,files,types:model.parts.length,physicalQuantity:model.parts.reduce((n,p)=>n+p.quantity,0),exportedBeforeDisassembly:!disassemble,passed:true};report.cases.push(entry);assert((await page.evaluate(path=>window.__closure.project.save(path),report.projectPath)).saved);writeFileSync(join(root,name+(reopen?'-cold':'')+'-report.json'),JSON.stringify(entry,null,2));write();return entry;
};
try{
 if(!dpiOnly)await page.setViewportSize({width:1600,height:1000});
 await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
 for(const action of ['tube-designer-cancel-add','tube-designer-close-preset-dialog']){const close=page.locator(`[data-cam-action="${action}"]`);if(await close.count())await click(close.last());}
 if(dpiOnly){
  await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openFileDialog=async()=>path;},report.projectPath);
  await click(page.locator('[data-action="open-start-center"]'));await click(page.locator('[data-action="choose-project"]'));
  await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().instances.some(p=>p.entityId===id),report.instances[0].entityId);await settle();
  await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');const v=getProjectView(window.__icaxWorkbench.getState().projectId);return v.scene.tubeDesigner.members.every(m=>v.viewport.sceneObjects.has(m.entityId));});
 }else if(reopen){
  const baseline=JSON.parse(readFileSync(join(root,'acceptance-report.json'),'utf8'));assert(baseline.passed);
  await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openFileDialog=async()=>path;},baseline.projectPath);
  await click(page.locator('[data-action="open-start-center"]'));await click(page.locator('[data-action="choose-project"]'));
  await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().instances.some(p=>p.entityId===id),baseline.instances[0].entityId);await settle();await init();
  const data=await userData();const saved=data.parameterPresets.find(p=>p.id===baseline.preset.id);assert.deepEqual(saved,baseline.preset);assert(data.customers.some(c=>c.id===saved.customerId&&c.name===baseline.customerName));
  check('customer link, preset values and resource snapshots survive cold process restart',{presetId:saved.id,customerId:saved.customerId});
  report.preset=saved;report.projectPath=baseline.projectPath;report.instances=baseline.instances;
  for(const instance of report.instances){const card=page.locator(`[data-cam-action="tube-designer-select-instance"][data-tube-designer-instance-id="${instance.entityId}"]`);if(!await card.isDisabled())await card.evaluate(n=>{n.focus({preventScroll:true});n.click();});await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().product?.entityId===id,instance.entityId);await settle();assert.deepEqual((await product()).parameters,instance.parameters);assert.equal((await product()).modelOutdated,false);assert.equal((await product()).partsOutdated,false);await exportCurrent('持久化-'+instance.pattern,false);check('cold product exports persisted parts before any regeneration',{pattern:instance.pattern});}
 }else if(continueRun){
  if(!await page.locator(`[data-tube-designer-instance-id="${report.instances[0].entityId}"]`).count()){await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openFileDialog=async()=>path;},report.projectPath);await click(page.locator('[data-action="open-start-center"]'));await click(page.locator('[data-action="choose-project"]'));await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().instances.some(p=>p.entityId===id),report.instances[0].entityId);await settle();}
  assert.equal(report.instances.length,3);await init();
  if(refreshExports)report.cases=[];
  for(const instance of report.instances){
   const card=page.locator(`[data-cam-action="tube-designer-select-instance"][data-tube-designer-instance-id="${instance.entityId}"]`);
   if(!await card.isDisabled())await click(card);
   await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().product?.entityId===id,instance.entityId);await settle();
   const current=await product();
   if(current.parameters.frameWidth!==42||current.parameters.frameDepth!==42){await materialSelect().focus();await materialSelect().selectOption('user:'+report.preset.id);await page.waitForFunction(()=>{const p=window.__icaxWorkbench.getTubeDesignerState().product?.parameters;return p?.frameWidth===42&&p?.frameDepth===42;});await settle();}
   if((await product()).modelOutdated)await regenerate();assert.deepEqual((await product()).parameters,instance.parameters);
   if(refreshExports)await exportCurrent('修复后-'+instance.pattern);
  }
 }else{
  await page.reload();await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);await init();
  const first=await add('grid');
  await edit('frameWidth',42);await edit('frameDepth',42);await edit('horizontalWidth',25);await edit('horizontalDepth',25);
  await click(page.locator('[data-cam-action="tube-designer-open-save-preset"][data-tube-designer-preset-scope="materials"]'));
  report.presetName='G01-42方管用料-'+Date.now();report.customerName='验收客户-G01';
  await page.locator('[data-tube-designer-preset-name]').fill(report.presetName);await page.locator('[data-tube-designer-preset-customer-name]').fill(report.customerName);
  await click(page.locator('[data-cam-action="tube-designer-save-parameter-preset"]'));await page.locator('[data-floating-editor-window="product-preset"]').waitFor({state:'hidden'});await settle();
  let data=await userData();let saved=data.parameterPresets.find(p=>p.name===report.presetName);assert(saved);assert.equal(saved.scopeKey,'materials');assert.equal(saved.values.frameWidth,42);assert.equal(saved.values.horizontalWidth,25);assert.equal(saved.values.tubeDesignerProfileOverrides.frame.width,42);assert(data.customers.some(c=>c.id===saved.customerId&&c.name===report.customerName));
  for(const k of ['width','height','infillPattern','doorPosition','frameJoinType','horizontalEndConnection','productCode'])assert(!Object.hasOwn(saved.values,k),k+' leaked into material preset');
  check('save material preset through UI with customer and complete section snapshots',{keys:Object.keys(saved.values),presetId:saved.id});
  await click(page.locator('[data-cam-action="tube-designer-open-manage-preset"][data-tube-designer-preset-scope="materials"]'));
  assert.equal(await page.locator('[data-tube-designer-preset-customer-id]').inputValue(),saved.customerId);report.presetName+='-复用';await page.locator('[data-tube-designer-preset-name]').fill(report.presetName);await click(page.locator('[data-cam-action="tube-designer-save-parameter-preset"]'));await page.locator('[data-floating-editor-window="product-preset"]').waitFor({state:'hidden'});await settle();
  data=await userData();const updated=data.parameterPresets.find(p=>p.id===saved.id);assert.equal(updated.name,report.presetName);assert(updated.revision>saved.revision);assert.equal(updated.customerId,saved.customerId);saved=updated;report.preset=saved;
  check('manage existing scheme retains customer and updates the same record',{revision:saved.revision});
  await regenerate();await exportCurrent('用料原始-格栅');
  report.instances=[{entityId:first.entityId,pattern:'grid',parameters:(await product()).parameters}];
  for(const pattern of ['horizontal','vertical']){
   const added=await add(pattern);await edit('frameWidth',40);await edit('frameDepth',40);const before=(await product()).parameters;
   await materialSelect().focus();await materialSelect().selectOption('user:'+saved.id);await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().product?.parameters.frameWidth===42);await settle();
   const applied=await product();assert.equal(applied.modelOutdated,true);assert.equal(applied.partsOutdated,true);for(const k of ['width','height','infillPattern','doorPosition','frameJoinType','horizontalEndConnection','productCode'])assert.deepEqual(applied.parameters[k],before[k],k);
   assert.equal(applied.parameters.tubeDesignerProfileOverrides.frame.width,42);assert.equal(applied.parameters.tubeDesignerProfileOverrides.horizontal.width,25);
   assert.equal(await page.locator('[data-tube-designer-preset-selection="right"][data-tube-designer-preset-scope="process"] option[value="user:'+saved.id+'"]').count(),0);
   await regenerate();await exportCurrent('用料复用-'+pattern);report.instances.push({entityId:added.entityId,pattern,parameters:(await product()).parameters});
   check('apply saved material scheme to new '+pattern+' product and export real manufactured parts',{entityId:added.entityId});
  }
 }
 if(!dpiOnly){
 // Multiple products coexist; the product workspace intentionally renders only the selected instance.
 for(let cycle=0;cycle<2;cycle++)for(const instance of report.instances){
  const card=page.locator(`[data-cam-action="tube-designer-select-instance"][data-tube-designer-instance-id="${instance.entityId}"]`);if(!await card.isDisabled())await card.evaluate(n=>{n.focus({preventScroll:true});n.click();});await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().product?.entityId===id,instance.entityId);await settle();
  const selected=await product();assert.deepEqual(selected.parameters,instance.parameters);
  await page.evaluate(async()=>{await window.__icaxWorkbench.waitForRenderableViewport();await window.__icaxWorkbench.setStandardView('front');await window.__icaxWorkbench.fitView();await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));});
  const annotations=await page.evaluate(()=>{const labels=[...document.querySelectorAll('.icax-three-specification-label')].filter(n=>!n.hidden&&n.getBoundingClientRect().width>0);const rects=labels.map(n=>{const b=n.getBoundingClientRect();return {key:n.querySelector('[data-tube-designer-parameter-key]')?.dataset.tubeDesignerParameterKey,text:n.innerText,x:b.x,y:b.y,w:b.width,h:b.height};});const overlaps=[];for(let i=0;i<rects.length;i++)for(let j=i+1;j<rects.length;j++){const a=rects[i],b=rects[j];if(Math.min(a.x+a.w,b.x+b.w)-Math.max(a.x,b.x)>.5&&Math.min(a.y+a.h,b.y+b.h)-Math.max(a.y,b.y)>.5)overlaps.push([a.key,b.key]);}const state=window.__icaxWorkbench.getTubeDesignerState();return {rects,overlaps,instances:state.instances.length,members:state.members.length};});
  assert.equal(annotations.instances,3);assert(annotations.rects.length>=5);assert.deepEqual(annotations.overlaps,[],JSON.stringify(annotations));
  if(instance.pattern==='horizontal')assert(!annotations.rects.some(r=>r.key==='verticalMaximumCenterSpacing'));
  if(instance.pattern==='vertical')assert(!annotations.rects.some(r=>r.key==='horizontalMaximumCenterSpacing'));
  check('multi-product selection restores its own annotations, parameters and parts',{cycle,entityId:instance.entityId,pattern:instance.pattern,labels:annotations.rects});
 }
 if(reopen){
  // Explicitly reapply a cold-loaded preset to an existing instance and return to the verified material values.
  await edit('frameWidth',44);await materialSelect().focus();await materialSelect().selectOption('user:'+report.preset.id);await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().product?.parameters.frameWidth===42);await settle();if((await product()).modelOutdated)await regenerate();
  await exportCurrent('冷启动复用-vertical');check('cold-loaded scheme reapplies through UI and produces current geometry');
 }else{
  report.projectPath=join(root,'G01用料复用与多实例.ictd');const saved=await page.evaluate(path=>window.__closure.project.save(path),report.projectPath);assert(saved.saved);check('three real product instances saved together',{projectPath:report.projectPath});
 }
 }
 // Renderer DPI and the matching logical viewport reproduce common 1920x1080 / 1366x768 display scales without changing the user's Windows setting.
 const cdp=await page.context().newCDPSession(page);
 for(const physical of [{width:1920,height:1080},{width:1366,height:768}])for(const scale of [1,1.25,1.5]){
  if(dpiOnly&&!(physical.width===1366&&scale>1))continue;
  const width=Math.round(physical.width/scale),height=Math.round(physical.height/scale);
  const stage=name=>console.log(JSON.stringify({dpi:physical.width,scale,stage:name}));
  // Emulate the logical viewport without resizing the physical native window;
  // its Windows minimum size is expressed in physical pixels, not CSS pixels.
  stage('device-metrics');await cdp.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:scale,mobile:false,screenWidth:width,screenHeight:height,dontSetVisibleSize:true});stage('idle');await settle();
  stage('fit');
  await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');getProjectView(window.__icaxWorkbench.getState().projectId).viewport.fitViewToViewport();});
  stage('labels');const scaledAnnotations=await page.evaluate(()=>{const c=document.querySelector('.cam-viewport canvas').getBoundingClientRect();const labels=[...document.querySelectorAll('.icax-three-specification-label')].filter(n=>!n.hidden&&n.getBoundingClientRect().width).map(n=>{const r=n.getBoundingClientRect();return {text:n.innerText,x:r.x,y:r.y,w:r.width,h:r.height};});const overlaps=[];for(let i=0;i<labels.length;i++)for(let j=i+1;j<labels.length;j++){const a=labels[i],b=labels[j];if(Math.min(a.x+a.w,b.x+b.w)-Math.max(a.x,b.x)>.5&&Math.min(a.y+a.h,b.y+b.h)-Math.max(a.y,b.y)>.5)overlaps.push([a.text,b.text]);}return {canvas:{x:c.x,y:c.y,w:c.width,h:c.height},labels,overlaps};});
  assert.deepEqual(scaledAnnotations.overlaps,[],JSON.stringify({physical,scale,scaledAnnotations}));
  assert(scaledAnnotations.labels.every(r=>r.x>=scaledAnnotations.canvas.x-.5&&r.y>=scaledAnnotations.canvas.y-.5&&r.x+r.w<=scaledAnnotations.canvas.x+scaledAnnotations.canvas.w+.5&&r.y+r.h<=scaledAnnotations.canvas.y+scaledAnnotations.canvas.h+.5),JSON.stringify({physical,scale,scaledAnnotations}));
  const measured=await page.evaluate(()=>{const pane=document.querySelector('[data-tube-designer-parameter-scroll]');const form=document.querySelector('[data-tube-designer-parameter-form]');const controls=[...form.querySelectorAll('input:not([type=checkbox]),select')].filter(n=>n.getClientRects().length);const bad=controls.flatMap(n=>{const b=n.getBoundingClientRect(),l=n.closest('label')?.getBoundingClientRect();return b.width<35||b.height<28||l&&b.right>l.right+1?[{key:n.dataset.tubeDesignerParameter??n.dataset.productParameterKey,width:b.width,height:b.height}]:[];});return {dpr:devicePixelRatio,width:innerWidth,height:innerHeight,pageOverflow:document.documentElement.scrollWidth>innerWidth+1,paneOverflow:pane.scrollWidth>pane.clientWidth+1,bad,inputFont:parseFloat(getComputedStyle(form.querySelector('[data-tube-designer-parameter="frameWidth"]')).fontSize),labelFont:parseFloat(getComputedStyle((form.querySelector('[data-tube-designer-parameter="frameWidth"]').closest('label').querySelector('span')??form.querySelector('[data-tube-designer-parameter="frameWidth"]').closest('label'))).fontSize)};});
  assert.equal(measured.dpr,scale);assert(!measured.pageOverflow,JSON.stringify(measured));assert(!measured.paneOverflow,JSON.stringify(measured));assert.deepEqual(measured.bad,[]);assert(measured.inputFont>=14&&measured.labelFont>=13);
  stage('right-screenshot');writeFileSync(join(root,`${reopen?'cold-':''}right-${physical.width}-${scale}.png`),Buffer.from((await Promise.race([cdp.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false}),new Promise((_,reject)=>setTimeout(()=>reject(new Error('CEF screenshot timed out')),5000))])).data,'base64'));
  stage('add-dialog');
  await click(page.locator('[data-command-id="designer.add"]'));const dialog=page.locator('[data-tube-designer-add-dialog]');await click(dialog.locator('[data-tube-designer-template-id="single-face-security-window"]'));await page.waitForFunction(()=>!document.querySelector('[data-cam-action="tube-designer-confirm-add"]')?.disabled);
  const creation=await dialog.evaluate(d=>{const r=d.getBoundingClientRect(),confirm=d.querySelector('[data-cam-action="tube-designer-confirm-add"]'),c=confirm.getBoundingClientRect();const fields=[...d.querySelectorAll('[data-tube-designer-parameter]')].filter(n=>n.getClientRects().length);return {x:r.x,y:r.y,width:r.width,height:r.height,viewport:[innerWidth,innerHeight],overflow:d.scrollWidth>d.clientWidth+1,confirm:{x:c.x,y:c.y,width:c.width,height:c.height},fonts:fields.map(n=>parseFloat(getComputedStyle(n).fontSize))};});
  assert(creation.x>=-1&&creation.y>=-1&&creation.x+creation.width<=width+1&&creation.y+creation.height<=height+1,JSON.stringify(creation));assert(!creation.overflow,JSON.stringify(creation));assert(creation.confirm.y>=0&&creation.confirm.y+creation.confirm.height<=height+1,JSON.stringify(creation));assert(creation.fonts.every(f=>f>=14));
  writeFileSync(join(root,`${reopen?'cold-':''}add-${physical.width}-${scale}.png`),Buffer.from((await Promise.race([cdp.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false}),new Promise((_,reject)=>setTimeout(()=>reject(new Error('CEF screenshot timed out')),5000))])).data,'base64'));await click(dialog.locator('[data-cam-action="tube-designer-cancel-add"]').last());await page.waitForFunction(()=>!document.querySelector('[data-tube-designer-add-dialog]'));await settle();
  await click(page.locator('[data-cam-action="tube-designer-open-save-preset"][data-tube-designer-preset-scope="materials"]'));const presetDialog=page.locator('[data-floating-editor-window="product-preset"]');
  const presetMetrics=await presetDialog.evaluate(d=>{const r=d.getBoundingClientRect(),footer=d.querySelector('footer').getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height,footerBottom:footer.bottom,controls:[...d.querySelectorAll('input,select')].map(n=>({font:parseFloat(getComputedStyle(n).fontSize),label:parseFloat(getComputedStyle(n.closest('label')).fontSize),height:n.getBoundingClientRect().height}))};});
  writeFileSync(join(root,`${reopen?'cold-':''}preset-${physical.width}-${scale}.png`),Buffer.from((await Promise.race([cdp.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false}),new Promise((_,reject)=>setTimeout(()=>reject(new Error('CEF screenshot timed out')),5000))])).data,'base64'));
  assert(presetMetrics.controls.every(c=>c.font>=14&&c.label>=13&&c.height>=30),JSON.stringify(presetMetrics));assert(presetMetrics.x>=0&&presetMetrics.y>=0&&presetMetrics.footerBottom<=height+1,JSON.stringify(presetMetrics));
  await click(presetDialog.locator('[data-cam-action="tube-designer-close-preset-dialog"]').last());await presetDialog.waitFor({state:'hidden'});await settle();
  check('DPI readability and accessible creation actions',{physical,scale,measured,creation,presetMetrics,scaledAnnotations});
 }
 await cdp.send('Emulation.clearDeviceMetricsOverride');await page.setViewportSize({width:1600,height:1000});
 report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;await page.screenshot({path:join(root,reopen?'cold-failure.png':'failure.png'),timeout:3000}).catch(()=>{});}
// Exiting only this test process disconnects CDP. Browser.close() can close the
// standalone native host, so leave application shutdown to the launch harness.
finally{write();console.log(JSON.stringify({passed:report.passed,error:report.error,checks:report.checks.length}));process.exit(process.exitCode??0);}
