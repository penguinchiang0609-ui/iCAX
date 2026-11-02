// Isolated Release CEF acceptance; all product calls use the real scene channel.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {join,resolve} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_GRILLE_VISUAL_NATIVE_EVIDENCE||'output/tests/minimal-protective-grille-20261026/visual-update/native');
mkdirSync(root,{recursive:true});
const descriptor=JSON.parse(readFileSync('src/apps/tube-designer/templates/product/minimal_protective_grille/template.json','utf8'));
const defaults=Object.fromEntries(descriptor.parameters.map(p=>[p.key,p.defaultValue]));
const browser=await chromium.connectOverCDP('http://127.0.0.1:19463');
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));
assert(page&&page.url().toLowerCase().includes('/runtime-visual-update/'),'Only the new isolated runtime may be operated');
page.setDefaultTimeout(120000);
const report={passed:false,nativeBusinessMocked:false,standaloneRelease:true,cdpPort:19463,
 originalUserProcessId:13536,originalUserWindowOperated:false,checks:[],diagramCases:[],navigation:[],pageErrors:[]};
page.on('pageerror',e=>report.pageErrors.push(e.message));
const write=()=>writeFileSync(join(root,'visual-native-report.json'),JSON.stringify(report,null,2));
const check=(name,details={})=>{report.checks.push({name,passed:true,...details});write();console.log(JSON.stringify({name,passed:true,...details}));};
const init=()=>page.evaluate(async()=>{
 const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');
 const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{timeoutMs:120000,initialCallId:1830000000}});
 await app.getState();const product=app.getProduct('icax.tube-designer');await product.getState();
 const project=product.getProject(window.__icaxWorkbench.getState().projectId);
 window.__visualTest={product,project,scene:project.getMainScene()};
});
const invoke=(method,args={})=>page.evaluate(({method,args})=>window.__visualTest.scene.invoke(method,args),{method,args});
const list=async()=>(await invoke('TubeDesigner.List')).tubeDesigner;
const viewport=()=>page.evaluate(()=>window.__icaxWorkbench.getViewportDebugState({samplePixels:true}));
const frames=()=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
const standard=async name=>{await page.evaluate(async name=>{await window.__icaxWorkbench.setStandardView(name);await window.__icaxWorkbench.fitView();},name);await frames();};
const dialog=()=>page.locator('[data-tube-designer-add-dialog]');
const setChoice=async(key,value)=>{
 const field=dialog().locator(`[data-tube-designer-parameter="${key}"]`);
 await field.selectOption(value);await frames();assert.equal(await field.inputValue(),value);
};
const selectCase=async c=>{
 await setChoice('frameType',c.frameType);await setChoice('handleEnabled',c.handleEnabled);
 if(c.handleEnabled==='yes'){await setChoice('handleSide',c.handleSide);await setChoice('handleClosure',c.handleClosure);}
};
const choiceLabel=(key,value)=>descriptor.parameters.find(p=>p.key===key).choices.find(c=>c.value===value).displayName['zh-CN'];
const defaultCase={frameType:'two_vertical',handleEnabled:'yes',handleSide:'left',handleClosure:'closed'};
const validateAnnotations=model=>{
 const p=model.product.parameters;
 const rows=Object.values(model.members[0].properties['tubeDesigner.minimalGrille'].rowCenters).flat();
 const bars=model.members.map(m=>({key:m.stableKey,...m.properties['assemblyFrame.member']}))
   .filter(m=>m.key.startsWith('inner.')&&Math.abs(m.start[2]-m.end[2])<1e-7);
 const result=[];
 for(const [key,boundary,center] of [['firstBarTopOffset',p.height,Math.max(...rows)],['lastBarBottomOffset',0,Math.min(...rows)]]){
  const a=model.specificationAnnotations.find(a=>a.parameter===key);assert(a);assert.equal(a.kind,'linear');
  const endpoint=bars.filter(m=>Math.abs(m.end[2]-center)<1e-7).sort((a,b)=>b.end[0]-a.end[0])[0].end;
  assert.deepEqual(a.end,endpoint);assert.deepEqual(a.start,[endpoint[0],endpoint[1],boundary]);
  assert.equal(a.generatedValue,p[key]);assert(Math.abs(Math.hypot(...a.start.map((v,i)=>v-a.end[i]))-p[key])<1e-7);
  assert(Math.abs(a.end[0]+a.offset[0]-p.width-75)<1e-7);assert.deepEqual(a.offset.slice(1),[0,0]);result.push(a);
 }
 return result;
};
const editAnnotation=async(key,value)=>{
 const label=page.locator(`[data-cam-action="tube-designer-edit-scene-specification"][data-tube-designer-parameter-key="${key}"]`);
 await label.waitFor({state:'visible'});const box=await label.boundingBox();assert(box);
 // The canvas intentionally owns hit-testing for projected dimension labels.
 await page.mouse.dblclick(box.x+box.width/2,box.y+box.height/2);
 const input=page.locator(`[data-tube-designer-scene-specification-input="${key}"]`);
 await input.waitFor({state:'visible'});await input.fill(String(value));await input.press('Enter');
 await page.waitForFunction(({key,value})=>{const p=window.__icaxWorkbench.getTubeDesignerState().product;return p?.parameters[key]===value&&p.modelOutdated;},{key,value});
 await page.locator('[data-cam-action="tube-designer-confirm-update"]').click();
 await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().product?.modelOutdated===false);
 await standard('front');
};
const directionDelta=(a,b)=>Math.hypot(...['x','y','z'].map(k=>a.cameraDirection[k]-b.cameraDirection[k]));
try{
 await page.setViewportSize({width:1600,height:1000});
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 const cdp=await page.context().newCDPSession(page);await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:true});
 report.projectPath=join(root,`grille-visual-update-${Date.now()}.ictd`);
 await page.evaluate(path=>window.__icaxAppShell.createProject({productId:'icax.tube-designer',projectName:'极简防护网交互改进验收',projectPath:path}),report.projectPath);
 await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId===window.__icaxAppShell.getState().activeProjectId);await init();
 await page.locator('[data-command-id="designer.add"]').click();
 await dialog().locator('[data-tube-designer-template-id="single-face-security-window"]').click();await frames();
 const referenceArt=await dialog().locator('.tube-designer-product-diagram-art').boundingBox();
 const referenceSvg=await dialog().locator('.tube-designer-product-diagram-art svg').boundingBox();
 assert.equal(referenceArt.height,96);assert.equal(referenceSvg.height,96);
 report.previewSizes={securityWindow:{artHeight:referenceArt.height,svgHeight:referenceSvg.height}};
 await dialog().screenshot({path:join(root,'reference-security-window-size.png')});
 await dialog().locator('[data-tube-designer-template-id="minimal-protective-grille"]').click();
 await page.waitForFunction(()=>!document.querySelector('[data-cam-action="tube-designer-confirm-add"]')?.disabled);
 const cases=[];
 for(const frameType of ['two_vertical','closed_frame','bottom_rail']){
  for(const handleSide of ['left','right','center_double'])for(const handleClosure of ['closed','open_slot'])cases.push({frameType,handleEnabled:'yes',handleSide,handleClosure});
  cases.push({frameType,handleEnabled:'no',handleSide:'left',handleClosure:'closed'});
 }
 for(const c of cases){
  await selectCase(c);
  const fields=await dialog().locator('[data-tube-designer-parameter]').evaluateAll(ns=>ns.map(n=>({key:n.dataset.tubeDesignerParameter,tag:n.tagName,type:n.type})));
  assert(fields.every(f=>['frameType','handleEnabled','handleSide','handleClosure'].includes(f.key)),'Adding must not expose numeric layout fields');
  assert.equal(fields.filter(f=>f.tag==='INPUT').length,0);
  const expectedFields=c.handleEnabled==='yes'?['frameType','handleEnabled','handleSide','handleClosure']:['frameType','handleEnabled'];
  assert.deepEqual(fields.map(f=>f.key).sort(),expectedFields.sort());
  const svg=dialog().locator('[data-minimal-grille-diagram]');await svg.waitFor({state:'visible'});
  const label=await svg.getAttribute('aria-label');assert(label.includes(choiceLabel('frameType',c.frameType)));
  for(const key of c.handleEnabled==='yes'?['handleSide','handleClosure']:['handleEnabled'])assert(label.includes(choiceLabel(key,c[key])));
  const drawn=await svg.locator('[data-grille-stock]').evaluateAll(ns=>ns.map(n=>n.dataset.grilleStock).sort());
  const preview=await invoke('TubeDesigner.GenerateProductTemplatePreview',{templateId:descriptor.id,parameters:{...defaults,...c}});
  assert.deepEqual(drawn,preview.items.map(i=>i.key).sort(),'Live SVG must match the real native product preview stocks');
  const box=await svg.boundingBox(),art=await dialog().locator('.tube-designer-product-diagram-art').boundingBox();
  assert.equal(box.height,referenceSvg.height,'All products share the compact preview height');assert.equal(art.height,referenceArt.height);
  report.previewSizes.minimalGrille={artHeight:art.height,svgHeight:box.height};
  const name=[c.frameType,c.handleEnabled==='no'?'without-handle':c.handleSide,c.handleClosure].join('-');
  report.diagramCases.push({name,...c,caption:label,memberCount:drawn.length,keys:drawn,previewHeight:box.height,fields:fields.map(f=>f.key),passed:true});write();
  if(c.handleSide==='center_double'||c.handleEnabled==='no')await dialog().screenshot({path:join(root,'dialog-'+name+'.png')});
 }
 check('Actual add dialog has only four choices and live SVG matches native previews',{cases:report.diagramCases.length});
 await selectCase(defaultCase);await dialog().screenshot({path:join(root,'add-linked-default.png')});
 await dialog().locator('[data-cam-action="tube-designer-confirm-add"]').click();await dialog().waitFor({state:'hidden'});
 await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().product?.modelOutdated===false);
 let model=await list();assert.equal(model.product.templateId,descriptor.id);assert.equal(model.members.length,15);
 report.productId=model.product.entityId;report.initialAnnotations=validateAnnotations(model);
 await standard('front');await page.screenshot({path:join(root,'initial-linear-dimensions.png')});
 check('Actual add creates the independent default product with true linear first-last dimensions',{members:model.members.length});
 for(const [key,value] of [['firstBarTopOffset',67],['lastBarBottomOffset',73]]){
  await editAnnotation(key,value);model=await list();assert.equal(model.product.entityId,report.productId);assert.equal(model.product.parameters[key],value);
  report.updatedAnnotations=validateAnnotations(model);check('Scene dimension edit regenerates real row anchors',{key,value});
 }
 assert((await page.evaluate(path=>window.__visualTest.project.save(path),report.projectPath)).saved);
 writeFileSync(join(root,'updated-native-model.json'),JSON.stringify(model,null,2));
 const invariant=JSON.stringify({parameters:model.product.parameters,members:model.members.map(m=>[m.entityId,m.properties['assemblyFrame.member']])});
 for(const mode of ['perspective','orthographic']){
  await page.locator(`button[data-projection-mode="${mode}"]`).click();await frames();assert.equal((await viewport()).projectionMode,mode);
  await standard('top-front-right');
  assert.equal(await page.locator('.cam-viewcube').getAttribute('data-appearance'),'beveled');
  assert.deepEqual((await page.locator('.cam-viewcube-label').allTextContents()).sort(),['前视','右视','顶视'].sort());
  assert.deepEqual(await page.locator('[data-cam-axis]').evaluateAll(ns=>ns.map(n=>n.dataset.camAxis)),['x','y','z']);
  assert.equal(await page.locator('[data-cam-compass]').count(),1);assert.equal(await page.locator('.icax-three-axis-gizmo').count(),0);
  const a=await page.locator('.cam-viewcube').boundingBox(),b=await page.locator('.icax-three-projection-toggle').boundingBox();
  assert(Math.abs(b.y-a.y-a.height)<=1);assert(Math.abs(b.x+b.width/2-a.x-a.width/2)<=1);
  const cubeFills=await page.locator('.cam-viewcube-surface').evaluateAll(ns=>[...new Set(ns.map(n=>getComputedStyle(n).fill))]);
  await page.locator('[data-cam-view="front"] polygon').first().click();await frames();
  const front=await viewport();assert(Math.abs(front.cameraDirection.y-1)<1e-5);assert.equal(front.projectionMode,mode);
  await standard('top-front-right');const beforeDrag=await viewport();
  const point=await page.locator('[data-cam-view="front"] polygon').first().evaluate(n=>{const b=n.getBBox(),p=new DOMPoint(b.x+b.width/2,b.y+b.height/2).matrixTransform(n.getScreenCTM());return {x:p.x,y:p.y};});
  await page.mouse.move(point.x,point.y);await page.mouse.down();await page.mouse.move(point.x-32,point.y+18,{steps:8});await page.mouse.up();await frames();
  const afterDrag=await viewport();assert(directionDelta(beforeDrag,afterDrag)>.01);assert.equal(afterDrag.projectionMode,mode);
  await standard('top-front-right');const beforeRing=await viewport();
  const ring=await page.locator('[data-cam-compass]').evaluate(n=>{const m=n.ownerSVGElement.getScreenCTM(),p=new DOMPoint(+n.dataset.cx,+n.dataset.cy).matrixTransform(m);return {x:p.x,y:p.y,rx:+n.dataset.rx*m.a,ry:+n.dataset.ry*m.d};});
  await page.mouse.move(ring.x,ring.y+ring.ry);await page.mouse.down();assert.equal(await page.locator('.cam-viewcube').getAttribute('data-compass-dragging'),'true');
  for(let i=1;i<=12;i++){const angle=Math.PI/2+i*Math.PI/24;await page.mouse.move(ring.x+ring.rx*Math.cos(angle),ring.y+ring.ry*Math.sin(angle));await frames();}
  await page.mouse.up();await frames();const afterRing=await viewport();assert(directionDelta(beforeRing,afterRing)>.1);assert.equal(afterRing.projectionMode,mode);assert(Math.abs(afterRing.cameraDirection.z-beforeRing.cameraDirection.z)<1e-5);
  report.navigation.push({mode,cubeBox:a,projectionBox:b,cubeFills,front:front.cameraDirection,beforeDrag:beforeDrag.cameraDirection,afterDrag:afterDrag.cameraDirection,beforeRing:beforeRing.cameraDirection,afterRing:afterRing.cameraDirection,passed:true});write();
  await page.screenshot({path:join(root,'navigation-'+mode+'.png')});
 }
 model=await list();assert.equal(JSON.stringify({parameters:model.product.parameters,members:model.members.map(m=>[m.entityId,m.properties['assemblyFrame.member']])}),invariant);
 check('Chinese gray cube XYZ compass and projection controls respond in the actual Release viewport',{projectionModes:report.navigation.length,modelUnchanged:true});
 await page.locator('button[data-projection-mode="perspective"]').click();await standard('front');
 await page.screenshot({path:join(root,'final-front-67-73.png')});
 await page.locator('[data-command-id="designer.add"]').click();await dialog().locator('[data-tube-designer-template-id="minimal-protective-grille"]').click();
 await selectCase({frameType:'bottom_rail',handleEnabled:'yes',handleSide:'center_double',handleClosure:'open_slot'});
 await dialog().screenshot({path:join(root,'final-linked-dialog.png')});
 await dialog().locator('[data-cam-action="tube-designer-cancel-add"]').first().click();await dialog().waitFor({state:'hidden'});
 await standard('front');report.finalModel={projectPath:report.projectPath,productId:model.product.entityId,parameters:model.product.parameters,members:model.members.length,view:'front'};
 assert.deepEqual(report.pageErrors,[]);report.passed=true;check('Leave isolated new runtime on corrected front view; original user window untouched');
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:join(root,'visual-failure.png')}).catch(()=>{});}
finally{write();console.log(JSON.stringify({passed:report.passed,cases:report.diagramCases.length,error:report.error}));await browser.close();}
