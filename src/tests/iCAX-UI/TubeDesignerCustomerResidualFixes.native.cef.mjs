import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,readFileSync,writeFileSync,readdirSync,statSync} from 'node:fs';
import {join,resolve} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_CUSTOMER_FIX_EVIDENCE||'output/tests/customer-feedback-fixes-20261024');
const cold=process.argv.includes('--cold'),dpiOnly=process.argv.includes('--dpi-only');
const saved=join(root,'G01翻头归并验证.ictd');
const baseline=JSON.parse(readFileSync(resolve('output/tests/customer-feedback-closure-20261023/qualified/acceptance-report.json'),'utf8'));
const grid=baseline.cases.find(c=>c.name==='修复后-grid');
const browser=await chromium.connectOverCDP(process.env.ICAX_CUSTOMER_FIX_CDP||'http://127.0.0.1:19429',{timeout:15000});
const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));
const cdp=await page.context().newCDPSession(page);
// Playwright's screenshot cleanup clears manually applied device metrics in CEF.
// Capture directly without changing the viewport under test.
const screenshot=async name=>{
 const before=await page.evaluate(()=>[innerWidth,innerHeight,devicePixelRatio]);
 const {data}=await cdp.send('Page.captureScreenshot',{format:'png',fromSurface:true,captureBeyondViewport:false});
 writeFileSync(join(root,name),Buffer.from(data,'base64'));
 assert.deepEqual(await page.evaluate(()=>[innerWidth,innerHeight,devicePixelRatio]),before);
};
page.setDefaultTimeout(30000);
const report={businessMocked:false,pickerPathsControlled:true,coldProcess:cold,checks:[],cases:[],dpi:[],projectPath:saved};
const reportFile=join(root,dpiOnly?`dpi-${process.env.ICAX_CUSTOMER_FIX_DPI||'all'}-report.json`:cold?'cold-report.json':'acceptance-report.json');
const write=()=>writeFileSync(reportFile,JSON.stringify(report,null,2));
const check=(name,details={})=>{report.checks.push({name,passed:true,...details});write();console.log(JSON.stringify({name,passed:true}));};
const click=async locator=>{await locator.waitFor({state:'visible'});for(let i=0;i<300&&!(await locator.isEnabled());++i)await page.waitForTimeout(100);await locator.evaluate(n=>{if(n.disabled)throw Error('Disabled production control');n.focus({preventScroll:true});n.click();});};
const settle=async()=>{
 await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');const v=getProjectView(window.__icaxWorkbench.getState().projectId);return v&&!v.pending&&!v.activeAreaAction&&!v.tubeDesignerExportOperation&&!v.tubeDesignerProductParameterCommitPromise;},{},{timeout:120000});
 await page.waitForTimeout(100);
};
const snapshot=()=>page.evaluate(async()=>(await window.__fixProject.getMainScene().invoke('TubeDesigner.List',{})).tubeDesigner);
const walk=d=>readdirSync(d).flatMap(n=>{const p=join(d,n);return statSync(p).isDirectory()?walk(p):[p]});
const verify=m=>{
 assert.deepEqual(m.product.parameters,grid.model.product.parameters);
 assert.equal(m.parts.length,11);assert.equal(m.parts.reduce((n,p)=>n+p.quantity,0),grid.physicalQuantity);
 const pair=m.parts.filter(p=>p.name.includes('主横杆')&&Math.abs(p.length-215.7)<.001);
 assert.equal(pair.length,1);assert.equal(pair[0].quantity,6);
 const sources=pair[0].properties['manufacturing.sourceMembers'];assert.equal(sources.length,6);
 assert.equal(new Set(sources.map(s=>s.itemKey)).size,6);
 assert(sources.some(s=>s.spans[0].placement.xAxis[0]===-1));
 assert(sources.every(s=>Math.abs(s.spans[0].stockStart)<1e-6));
 return pair[0];
};
const disassemble=async()=>{await click(page.locator('[data-command-id="designer.disassemble"]'));await click(page.locator('[data-cam-action="tube-designer-batch-disassembly-confirm"]'));await settle();await page.waitForFunction(()=>window.__icaxWorkbench.getTubeDesignerState().parts.length===11,{},{timeout:120000});};
try {
 await page.reload();
 await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
 await page.locator('[data-startup-screen]').waitFor({state:'hidden'});
 await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openFileDialog=async()=>path;},saved);
 await click(page.locator('[data-action="open-start-center"]'));await click(page.locator('[data-action="choose-project"]'));
 await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().instances.some(p=>p.entityId===id),grid.model.product.entityId);await settle();
 const card=page.locator(`[data-cam-action="tube-designer-select-instance"][data-tube-designer-instance-id="${grid.model.product.entityId}"]`);
 if(!await card.isDisabled())await click(card);
 await page.waitForFunction(id=>window.__icaxWorkbench.getTubeDesignerState().product?.entityId===id,grid.model.product.entityId);await settle();
 await page.evaluate(async()=>{const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');const app=await AppProxy.create(window.icax??window.icaxNativeBridge,{sdo:{timeoutMs:120000,initialCallId:1854000000}});await app.getState();const product=app.getProduct('icax.tube-designer');await product.getState();window.__fixProject=product.getProject(window.__icaxWorkbench.getState().projectId);});
 if(!dpiOnly){
  if(cold){verify(await snapshot());check('cold reopen preserves eleven types and six flipped horizontal members before disassembly');}
  await disassemble();const first=await snapshot();const pair=verify(first);
  check('real grid merges two three-piece horizontal rows into one six-piece row',{beforeTypes:12,afterTypes:11,pair});
  await disassemble();const repeated=await snapshot();verify(repeated);
  assert.deepEqual(repeated.parts.map(p=>[p.entityId,p.quantity,p.properties['manufacturing.sourceMembers']]),first.parts.map(p=>[p.entityId,p.quantity,p.properties['manufacturing.sourceMembers']]));
  check('repeated disassembly preserves identities quantities and source placements');
  const target=join(root,cold?'cold-grid-export':'fixed-grid-export');mkdirSync(target,{recursive:true});
  await page.evaluate(path=>{(window.icax??window.icaxNativeBridge).openDirectoryDialog=async()=>path;},target);
  await click(page.locator('[data-command-id="designer.export-active-product-parts"]'));await page.waitForFunction(path=>document.body.innerText.includes(path),target,{timeout:120000});await settle();
  const files=walk(target);assert.equal(files.filter(p=>p.endsWith('.step')).length,11);assert.equal(files.filter(p=>p.endsWith('.xlsx')).length,1);
  const entry={name:cold?'cold-grid':'fixed-grid',target,model:await snapshot(),files,types:11,physicalQuantity:grid.physicalQuantity,passed:true};report.cases.push(entry);
  writeFileSync(join(root,entry.name+'-report.json'),JSON.stringify(entry,null,2));
  assert((await page.evaluate(path=>window.__fixProject.save(path),saved)).saved);check('real STEP and workbook exported and project saved',{target});
 }
 if(!cold){
  for(const [physicalWidth,physicalHeight] of [[1920,1080],[1366,768]])for(const scale of [1,1.25,1.5]){
   if(process.env.ICAX_CUSTOMER_FIX_DPI&&process.env.ICAX_CUSTOMER_FIX_DPI!==`${physicalWidth}-${scale}`)continue;
   const width=Math.round(physicalWidth/scale),height=Math.round(physicalHeight/scale);
   console.log(JSON.stringify({stage:'DPI',physicalWidth,scale}));
   if(!process.env.ICAX_CUSTOMER_FIX_NATIVE_DPI)await cdp.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:scale,mobile:false,screenWidth:width,screenHeight:height,dontSetVisibleSize:true});
   console.log('device metrics applied');
   await settle();
   console.log('UI settled');
   await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');const v=getProjectView(window.__icaxWorkbench.getState().projectId).viewport;v.resize();v.fitViewToViewport();});await page.waitForTimeout(200);
   console.log('model fitted');
   const metrics=await page.evaluate(()=>{
    const canvas=document.querySelector('.cam-viewport canvas'),c=canvas.getBoundingClientRect(),container=canvas.closest('.cam-viewport').getBoundingClientRect();
    const rect=n=>{const r=n.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height,text:n.innerText,textOverflow:n.scrollWidth-n.clientWidth};};
    const labels=[...document.querySelectorAll('.icax-three-specification-label')].filter(n=>!n.hidden&&n.getBoundingClientRect().width).map(rect);
    const controls=[...document.querySelectorAll('.cam-viewcube,.icax-three-axis-gizmo,.icax-three-projection-toggle,.icax-three-transform-space,.icax-three-display-mode,.tube-designer-scene-specification-tree')].filter(n=>n.getBoundingClientRect().width).map(rect);
    const overlap=(a,b)=>Math.min(a.x+a.w,b.x+b.w)-Math.max(a.x,b.x)>.5&&Math.min(a.y+a.h,b.y+b.h)-Math.max(a.y,b.y)>.5;
    const hidden=labels.filter(r=>r.x<container.left-.5||r.y<container.top-.5||r.x+r.w>container.right+.5||r.y+r.h>container.bottom+.5);
    const blocked=labels.filter(r=>controls.some(c=>overlap(r,c))||document.elementFromPoint(r.x+r.w/2,r.y+r.h/2)!==canvas);
    const collisions=labels.flatMap((r,i)=>labels.slice(i+1).filter(b=>overlap(r,b)).map(b=>[r.text,b.text]));
    return {width:innerWidth,height:innerHeight,dpr:devicePixelRatio,canvas:rect(canvas),container:{x:container.x,y:container.y,w:container.width,h:container.height},labels,controls,hidden,blocked,collisions,canvasFits:c.bottom<=container.bottom+.5,pageOverflow:document.documentElement.scrollWidth>innerWidth+1};
   });
   report.dpi.push({physicalWidth,physicalHeight,scale,metrics,nativeWindowSized:!!process.env.ICAX_CUSTOMER_FIX_NATIVE_DPI,rendererScaleAtLaunch:!!process.env.ICAX_CUSTOMER_FIX_NATIVE_DPI});write();
   assert(Math.abs(metrics.width-width)<=1&&Math.abs(metrics.height-height)<=1,JSON.stringify(metrics));
   assert.equal(metrics.dpr,scale);assert(metrics.labels.length>0);assert(metrics.canvasFits);assert(!metrics.pageOverflow);
   assert.deepEqual(metrics.hidden,[]);assert.deepEqual(metrics.blocked,[]);assert.deepEqual(metrics.collisions,[]);
   assert(metrics.labels.every(l=>l.textOverflow<=1),JSON.stringify(metrics.labels));
   await screenshot(`fixed-${physicalWidth}-${scale}.png`);
   const fonts=await page.evaluate(()=>{
    const form=document.querySelector('[data-tube-designer-parameter-form]'),pane=document.querySelector('[data-tube-designer-parameter-scroll]');
    const controls=[...form.querySelectorAll('input:not([type=checkbox]),select')].filter(n=>n.getClientRects().length);
    return {paneOverflow:pane.scrollWidth>pane.clientWidth+1,controls:controls.map(n=>{const r=n.getBoundingClientRect(),l=n.closest('label')?.getBoundingClientRect();return {key:n.dataset.tubeDesignerParameter,font:parseFloat(getComputedStyle(n).fontSize),w:r.width,h:r.height,contained:!l||r.right<=l.right+1};})};
   });
   report.dpi.at(-1).fonts=fonts;write();
   assert(!fonts.paneOverflow);assert(fonts.controls.every(c=>c.font>=14&&c.w>=35&&c.h>=28&&c.contained),JSON.stringify(fonts));
   await page.evaluate(()=>{const n=[...document.querySelectorAll('.icax-three-specification-label')].find(n=>n.textContent.includes('正面宽度'));const r=n.getBoundingClientRect();document.querySelector('.cam-viewport canvas').dispatchEvent(new MouseEvent('dblclick',{button:0,clientX:r.x+r.width/2,clientY:r.y+r.height/2,bubbles:true,cancelable:true}));});
   const editor=page.locator('.icax-three-specification-input');await editor.waitFor();await editor.press('Escape');await editor.waitFor({state:'hidden'});await settle();
   await click(page.locator('[data-command-id="designer.add"]'));const dialog=page.locator('[data-tube-designer-add-dialog]').last();
   await page.waitForFunction(()=>{const n=[...document.querySelectorAll('[data-cam-action="tube-designer-confirm-add"]')].at(-1);return n&&!n.disabled;});
   const creation=await dialog.evaluate(n=>{const r=n.getBoundingClientRect(),b=n.querySelector('[data-cam-action="tube-designer-confirm-add"]').getBoundingClientRect();return {fits:r.left>=-1&&r.top>=-1&&r.right<=innerWidth+1&&r.bottom<=innerHeight+1,buttonVisible:b.top>=0&&b.bottom<=innerHeight+1,overflow:n.scrollWidth>n.clientWidth+1};});
   assert(creation.fits&&creation.buttonVisible&&!creation.overflow,JSON.stringify(creation));
   await screenshot(`fixed-add-${physicalWidth}-${scale}.png`);
   await click(dialog.locator('[data-cam-action="tube-designer-cancel-add"]').last());await dialog.waitFor({state:'hidden'});await settle();
   await click(page.locator('[data-cam-action="tube-designer-open-save-preset"][data-tube-designer-preset-scope="materials"]'));
   const preset=page.locator('[data-floating-editor-window="product-preset"]');
   const presetMetrics=await preset.evaluate(n=>{const r=n.getBoundingClientRect(),f=n.querySelector('footer').getBoundingClientRect();return {fits:r.left>=0&&r.top>=0&&r.right<=innerWidth+1&&f.bottom<=innerHeight+1,controls:[...n.querySelectorAll('input,select')].map(c=>({font:parseFloat(getComputedStyle(c).fontSize),height:c.getBoundingClientRect().height}))};});
   assert(presetMetrics.fits&&presetMetrics.controls.every(c=>c.font>=14&&c.height>=30));
   await screenshot(`fixed-preset-${physicalWidth}-${scale}.png`);
   await click(preset.locator('[data-cam-action="tube-designer-close-preset-dialog"]').last());await preset.waitFor({state:'hidden'});await settle();
   report.dpi.at(-1).fonts=fonts;report.dpi.at(-1).creation=creation;report.dpi.at(-1).dimensionDoubleClickEditable=true;
   report.dpi.at(-1).preset=presetMetrics;
   check('real model labels visible and unobstructed',{physicalWidth,physicalHeight,scale});
  }
  await cdp.send('Emulation.clearDeviceMetricsOverride');
 }
 report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;await screenshot('fix-failure.png').catch(()=>{});}
finally{write();console.log(JSON.stringify({passed:report.passed,error:report.error}));process.exit(process.exitCode??0);}
