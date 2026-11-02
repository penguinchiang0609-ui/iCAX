// Actual standalone creation, scene editing, readability and retained UI nodes.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,writeFileSync,readdirSync,statSync} from 'node:fs';
import {resolve,join} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');const root=resolve('output/tests/customer-remaining-20261023/interaction');mkdirSync(root,{recursive:true});
const browser=await chromium.connectOverCDP('http://127.0.0.1:19427');
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));page.setDefaultTimeout(120000);
const report={businessMocked:false,pickerPathsControlled:true,checks:[]};
const walk=d=>readdirSync(d).flatMap(n=>{const p=join(d,n);return statSync(p).isDirectory()?walk(p):[p]});
try{
 await page.setViewportSize({width:1600,height:1000});await page.reload();
 await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
 await page.locator('[data-command-id="designer.add"]').click();
 const dialog=page.locator('[data-tube-designer-add-dialog]');
 assert.equal(await dialog.locator('[data-tube-designer-template-id="minimal-protective-grille"]').count(),0);
 await dialog.locator('[data-tube-designer-template-id="single-face-security-window"]').click();
 const confirm=dialog.locator('[data-cam-action="tube-designer-confirm-add"]');await confirm.waitFor();
 await page.waitForFunction(()=>!document.querySelector('[data-cam-action="tube-designer-confirm-add"]')?.disabled);
 await dialog.locator('[data-tube-designer-parameter="infillPattern"]').selectOption('horizontal');
 await page.waitForFunction(()=>!document.querySelector('[data-cam-action="tube-designer-confirm-add"]')?.disabled);
 const side=dialog.locator('[data-tube-designer-parameter="doorPosition"]');
 await side.selectOption('right_middle');
 await page.waitForFunction(()=>!document.querySelector('[data-cam-action="tube-designer-confirm-add"]')?.disabled);
 assert.equal(await dialog.locator('[data-tube-designer-parameter="doorRightMargin"]').count(),1);
 assert.equal(await dialog.locator('[data-tube-designer-parameter="doorLeftMargin"]').count(),0);
 await dialog.locator('[data-tube-designer-parameter="doorRightMargin"]').fill('80');
 await dialog.locator('[data-tube-designer-parameter="doorRightMargin"]').press('Tab');
 await page.waitForFunction(()=>!document.querySelector('[data-cam-action="tube-designer-confirm-add"]')?.disabled);
 const bridge=dialog.locator('.product-diagram-door');
 const rightPoints=await bridge.getAttribute('points');await side.selectOption('left_middle');
 await page.waitForFunction(old=>document.querySelector('[data-tube-designer-add-dialog] .product-diagram-door')?.getAttribute('points')!==old,rightPoints);
 await page.waitForFunction(()=>!document.querySelector('[data-cam-action="tube-designer-confirm-add"]')?.disabled);
 assert.equal(await dialog.locator('[data-tube-designer-parameter="doorRightMargin"]').count(),0);
 const leftMargin=dialog.locator('[data-tube-designer-parameter="doorLeftMargin"]');assert.equal(await leftMargin.count(),1);
 await leftMargin.fill('50');await leftMargin.press('Tab');
 await page.waitForFunction(()=>!document.querySelector('[data-cam-action="tube-designer-confirm-add"]')?.disabled);
 assert.equal(await bridge.count(),1);
 report.checks.push({name:'visible catalogue and schematic track left/right escape leaf',passed:true});
 const fonts=await dialog.locator('[data-tube-designer-parameter="infillPattern"]').evaluate(n=>({input:parseFloat(getComputedStyle(n).fontSize),height:n.getBoundingClientRect().height,label:parseFloat(getComputedStyle(n.closest('label')).fontSize)}));
 assert(fonts.input>=14&&fonts.label>=13&&fonts.height>=30,JSON.stringify(fonts));report.checks.push({name:'creation form text and input size',...fonts,passed:true});
 await dialog.screenshot({path:join(root,'极简添加界面.png')});
 await confirm.click();await dialog.waitFor({state:'hidden'});
 await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().product?.parameters.infillPattern==='horizontal');
 await page.evaluate(async()=>{await window.__icaxWorkbench.waitForRenderableViewport();await window.__icaxWorkbench.setStandardView('front');await window.__icaxWorkbench.fitView();});
 assert.equal(await page.locator('[data-tube-designer-parameter-form] [data-tube-designer-parameter="doorPosition"]').count(),0);
 const annotation=page.locator('[data-tube-designer-parameter-key="horizontalMaximumCenterSpacing"][data-cam-action="tube-designer-edit-scene-specification"]').first();
 const box=await annotation.boundingBox();assert(box);await page.mouse.dblclick(box.x+box.width/2,box.y+box.height/2);
 const spacing=page.locator('[data-tube-designer-scene-specification-input="horizontalMaximumCenterSpacing"]');await spacing.fill('110');await spacing.press('Enter');
 await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().product?.parameters.horizontalMaximumCenterSpacing===110);
 report.checks.push({name:'spacing edited through real scene annotation',passed:true});
 const gapLabel=page.locator('[data-tube-designer-parameter-key="doorGap"][data-cam-action="tube-designer-edit-scene-specification"]');
 const gapBox=await gapLabel.boundingBox();assert(gapBox);await page.mouse.dblclick(gapBox.x+gapBox.width/2,gapBox.y+gapBox.height/2);
 const gapInput=page.locator('[data-tube-designer-scene-specification-input="doorGap"]');await gapInput.fill('9');await gapInput.press('Enter');
 await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().product?.parameters.doorGap===9);
 report.checks.push({name:'escape gap edited through real scene annotation',passed:true});
 const offsetLabel=page.locator('[data-tube-designer-parameter-key="doorVerticalOffset"][data-cam-action="tube-designer-edit-scene-specification"]');
 const offsetBox=await offsetLabel.boundingBox();assert(offsetBox);await page.mouse.dblclick(offsetBox.x+offsetBox.width/2,offsetBox.y+offsetBox.height/2);
 const offsetInput=page.locator('[data-tube-designer-scene-specification-input="doorVerticalOffset"]');await offsetInput.fill('20');await offsetInput.press('Enter');
 await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().product?.parameters.doorVerticalOffset===20);
 report.checks.push({name:'zero center offset remains editable in the scene',passed:true});
 const before=await page.evaluate(()=>{
  const pane=document.querySelector('[data-tube-designer-parameter-form]');pane.querySelectorAll('details').forEach(n=>n.open=true);
  const input=pane.querySelector('[data-tube-designer-parameter="frameWidth"]'),code=pane.querySelector('[data-tube-designer-parameter="productCode"]');
  const canvas=document.querySelector('canvas'),left=document.querySelector('.cam-context-pane'),right=document.querySelector('[data-tube-designer-parameter-scroll]');
  window.__customerNodes={input,code,canvas,left,right};window.__customerEvents=0;canvas.addEventListener('customer-retained',()=>window.__customerEvents++);
  input.value='26';input.dispatchEvent(new Event('change',{bubbles:true}));
  code.focus({preventScroll:true});code.setSelectionRange(0,1,'forward');left.scrollTop=17;right.scrollTop=80;
  return {left:left.scrollTop,right:right.scrollTop};
 });
 await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().product?.parameters.frameWidth===26);
 await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');return !getProjectView(window.__icaxWorkbench.getState().projectId).pending;});
 const retained=await page.evaluate(()=>{const n=window.__customerNodes;n.canvas.dispatchEvent(new Event('customer-retained'));return {
  nodes:n.canvas===document.querySelector('canvas')&&n.input===document.querySelector('[data-tube-designer-parameter-form] [data-tube-designer-parameter="frameWidth"]')&&n.code.isConnected,
  focus:document.activeElement===n.code,selection:[n.code.selectionStart,n.code.selectionEnd],left:n.left.scrollTop,right:n.right.scrollTop,listeners:window.__customerEvents,
  font:parseFloat(getComputedStyle(n.input).fontSize),labelFont:parseFloat(getComputedStyle(n.input.closest('label')).fontSize),
  redundantWrappers:[...document.querySelectorAll('[data-tube-designer-parameter-group]')].filter(n=>n.dataset.tubeDesignerGroupDepth!=='0'&&!n.querySelector(':scope > .tube-designer-parameter-group-content > .tube-designer-field-grid')&&n.querySelectorAll(':scope > .tube-designer-parameter-group-content > .tube-designer-parameter-subsection-list > details').length===1).length};});
 assert(retained.nodes&&retained.focus);assert.deepEqual(retained.selection,[0,1]);assert.equal(retained.left,before.left);assert.equal(retained.right,before.right);assert.equal(retained.listeners,1);assert(retained.font>=14&&retained.labelFont>=13);assert.equal(retained.redundantWrappers,0);
 report.checks.push({name:'native asynchronous material update retains inputs, focus, selection, scroll and canvas listeners',...retained,passed:true});
 await page.locator('[data-cam-action="tube-designer-confirm-update"]').click();
 await page.waitForFunction(()=>!window.__icaxWorkbench.getTubeDesignerState().product?.modelOutdated);
 await page.locator('[data-command-id="designer.disassemble"]').click();await page.locator('[data-cam-action="tube-designer-batch-disassembly-confirm"]').click();
 await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().parts.length>0&&!document.querySelector('[data-command-id="designer.export-active-product-parts"]')?.disabled);
 const native=await page.evaluate(async()=>{const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');const a=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{initialCallId:1600000000}});await a.getState();const p=a.getProduct('icax.tube-designer');await p.getState();const project=p.getProject(window.__icaxWorkbench.getState().projectId);window.__customerNative={project,scene:project.getMainScene()};return (await window.__customerNative.scene.invoke('TubeDesigner.List',{})).tubeDesigner;});
 const path=join(root,'极简-实际界面修改.ictd'),target=join(root,'极简-实际界面修改-export');mkdirSync(target,{recursive:true});
 await page.evaluate(path=>window.__customerNative.project.save(path),path);
 await page.evaluate(target=>{(window.icax??window.icaxNativeBridge).openDirectoryDialog=async()=>target;},target);
 await page.locator('[data-command-id="designer.export-active-product-parts"]').click();await page.waitForFunction(target=>document.body.innerText.includes(target),target);
 await page.evaluate(async()=>{await window.__icaxWorkbench.setStandardView('front');await window.__icaxWorkbench.fitView();});
 const labels=await page.locator('.icax-three-specification-label:not([hidden])').evaluateAll(ns=>ns.map(n=>{const r=n.getBoundingClientRect();return {label:n.textContent,x:r.x,y:r.y,w:r.width,h:r.height};}));
 for(let a=0;a<labels.length;a++)for(let b=a+1;b<labels.length;b++){
  const x=labels[a],y=labels[b];assert(!(x.x<y.x+y.w&&x.x+x.w>y.x&&x.y<y.y+y.h&&x.y+x.h>y.y),'visible dimension labels overlap');
 }
 report.checks.push({name:'actual model dimension captions do not overlap',labels,passed:true});
 for(const projection of ['perspective','orthographic'])for(const location of ['model','blank']){
  const anchor=await page.evaluate(async({projection,location})=>{
   const THREE=await import('../ThirdParty/three/three.module.js');
   const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');
   const v=getProjectView(window.__icaxWorkbench.getState().projectId).viewport;v.setProjectionMode(projection);v.fitViewToViewport();
   const canvas=v.renderer.domElement,r=canvas.getBoundingClientRect(),ray=new THREE.Raycaster();
   for(let iy=1;iy<10;iy++)for(let ix=1;ix<10;ix++){
    const x=Math.round(r.left+r.width*ix/10),y=Math.round(r.top+r.height*iy/10);
    if(document.elementFromPoint(x,y)!==canvas)continue;
    ray.setFromCamera(new THREE.Vector2((x-r.left)/r.width*2-1,1-(y-r.top)/r.height*2),v.camera);
    const hit=ray.intersectObjects([...v.sceneObjects.values()].filter(o=>o.visible),false)[0];
    if((location==='model')!==Boolean(hit))continue;
    const plane=new THREE.Plane().setFromNormalAndCoplanarPoint(v.camera.getWorldDirection(new THREE.Vector3()),hit?.point??v.cameraState.target);
    window.__customerZoom={v,anchor:ray.ray.intersectPlane(plane,new THREE.Vector3())};return {x,y};
   }
   throw new Error('Requested native model pointer location not found');
  },{projection,location});
  await page.mouse.move(anchor.x,anchor.y);
  for(const delta of [-120,-120,120,120]){
   await page.mouse.wheel(0,delta);await page.waitForTimeout(30);
   const drift=await page.evaluate(({x,y})=>{const {v,anchor}=window.__customerZoom,r=v.renderer.domElement.getBoundingClientRect(),p=anchor.clone().project(v.camera);return Math.hypot(r.left+(p.x*.5+.5)*r.width-x,r.top+(-p.y*.5+.5)*r.height-y);},anchor);
   assert(drift<.1,JSON.stringify({projection,location,delta,drift}));report.checks.push({name:'real model pointer anchored zoom',projection,location,delta,drift,passed:true});
  }
 }
 await page.evaluate(async()=>{await window.__icaxWorkbench.setStandardView('front');await window.__icaxWorkbench.fitView();});
 await page.screenshot({path:join(root,'极简-实际界面修改.png')});
 const result={name:'极简-实际界面修改',path,target,model:native,files:walk(target),types:native.parts.length,physicalQuantity:native.parts.reduce((n,p)=>n+p.quantity,0),passed:true};
 writeFileSync(join(root,result.name+'-report.json'),JSON.stringify(result,null,2));writeFileSync(join(root,'acceptance-report.json'),JSON.stringify({passed:true,cases:[result]},null,2));
 report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;await page.screenshot({path:join(root,'interaction-failure.png')}).catch(()=>{});}
finally{writeFileSync(join(root,'interaction-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,error:report.error,checks:report.checks}));await browser.close();}
