import assert from 'node:assert/strict';
import {mkdirSync,copyFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/tests/lead-compensation-20261026/standalone');mkdirSync(output,{recursive:true});
const project=resolve(output,'连续引线与补偿.i5cam');copyFileSync(resolve(process.env.ICAX_LEAD_PROJECT||'output/tests/cut-delete-empty-20261026/trim-repro-expanded.i5cam'),project);
const browser=await chromium.connectOverCDP(process.env.ICAX_CEF_ENDPOINT||'http://127.0.0.1:9568');
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().endsWith('/SDK/AppShell/index.html'));page.setDefaultTimeout(60000);
const report={passed:false,project,cases:[],coverage:{standaloneCef:true,native:true},errors:[]};page.on('pageerror',e=>report.errors.push(e.message));
const idle=()=>page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending&&!fixture.view.fiveLeads?.saving,null,{timeout:120000});
const invoke=(method,payload={})=>page.evaluate(({method,payload})=>fixture.view.viewportSceneProxy.invoke(method,payload,{timeoutMs:120000}),{method,payload});
const read=()=>invoke('CamScene.ReadState');
const main=async()=>{await page.locator('.ribbon-command[data-command-id="five.tool.leads"]').click();await idle();};
const settings=async()=>{await page.locator('.ribbon-command-menu-toggle[data-command-id="five.tool.leads"]').click();await page.locator('.ribbon-command-menu-item[data-command-id="five.leads.settings"]').click();await idle();};
try{
  await page.setViewportSize({width:1600,height:1000});
  await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
  const closing=await page.evaluate(path=>{const s=__icaxAppShell.getState();if(s.activeProjectPath?.replaceAll('\\','/').toLowerCase()!==path.replaceAll('\\','/').toLowerCase())return false;void __icaxAppShell.closeProject(s.activeProjectId);return true;},project);
  if(closing){await page.waitForFunction(()=>!__icaxAppShell.getState().activeProjectId||!!document.querySelector('dialog[open]'));if(await page.getByRole('button',{name:'直接关闭',exact:true}).count())await page.getByRole('button',{name:'直接关闭',exact:true}).click();await page.waitForFunction(()=>!__icaxAppShell.getState().activeProjectId);}
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
  await page.waitForFunction(()=>__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId);
  await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};});
  await idle();await page.locator('[data-tab-id="toolpath"]').click();await idle();
  await page.waitForFunction(()=>fixture.view.viewport?.sceneObjects.size>0);console.log('Standalone workpiece is visible');
  const initial=await read(),holes=initial.intentToolpaths.filter(c=>c.machiningKind==='Cut'&&c.boundaryRole==='Opening');assert.equal(holes.length,3);
  await main();assert.equal(await page.locator('[role="dialog"][aria-label="引线"]').count(),0);assert.equal(await page.locator('.five-lead-tool-hint').count(),1);
  await settings();const field=name=>page.locator(`[data-five-lead-field="${name}"]`);
  await field('entry.kind').selectOption('Line');await field('entry.length').fill('.8');await field('entry.angle').fill('0');
  await field('exit.kind').selectOption('Arc');await field('exit.length').fill('.4');await field('exit.radius').fill('.6');
  const length=await field('entry.length').elementHandle();await field('entry.kind').selectOption('Arc');assert.equal(await field('entry.angle').count(),0);assert.equal(await field('entry.radius').count(),1);assert.equal(await length.evaluate(n=>n===document.querySelector('[data-five-lead-field="entry.length"]')),true);await field('entry.kind').selectOption('Line');
  await page.locator('[data-five-command="five.leads.confirm"]').click();await idle();assert.equal(await page.locator('[role="dialog"][aria-label="引线设置"]').count(),0);
  for(const hole of holes.slice(0,2)){await page.locator(`[data-five-intent="${hole.id}"]`).click();await page.waitForFunction(id=>fixture.view.fiveSavedLeads?.preview?.items.some(i=>i.intentId===id&&i.leads.length===2),hole.id);await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');}
  const id=holes[2].id;
  await page.evaluate(()=>{fixture.view.viewport.setViewDirection([0,0,1]);fixture.view.viewport.fitView();});
  const spot=await page.evaluate(async id=>{const {nearestCutLine}=await import('../../../apps/five-axis-cam/webpage/cutSelection.mjs'),THREE=await import('../../../iCAX-UI/SDK/ThirdParty/three/three.module.js'),v=fixture.view,vp=v.viewport,b=vp.renderer.domElement.getBoundingClientRect(),cut=v.scene.intentToolpaths.find(c=>c.id===id),obj=vp.sceneObjects.get(cut.placementId);obj.updateWorldMatrix(true,false);for(const p of cut.previewPoints){const q=new THREE.Vector3(...p).applyMatrix4(obj.matrixWorld).project(vp.camera),e={clientX:b.left+(q.x+1)*b.width/2,clientY:b.top+(1-q.y)*b.height/2};if(nearestCutLine(v,e)?.intentId===id)return {x:e.clientX,y:e.clientY};}return null;},id);
  assert.ok(spot);await page.mouse.click(spot.x,spot.y);await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  let state=await read();const ids=holes.map(h=>h.id),policy=await invoke('CamProcess.Read',{expected:state.expected,intentIds:ids});
  for(const item of policy.items){assert.equal(item.assignment.leadIn.kind,'Line');assert.equal(item.assignment.leadIn.lengthMm,.8);assert.equal(item.assignment.leadOut.kind,'Arc');}
  await page.waitForFunction(ids=>ids.every(id=>fixture.view.fiveSavedLeads?.preview?.items.some(i=>i.intentId===id&&i.leads.length===2)),ids);
  report.cases.push('Actual ribbon split button opens settings; main command enables continuous application; two tree clicks and one viewport click save all six leads');
  await page.screenshot({path:resolve(output,'三孔连续引线.png')});await main();assert.equal(await page.locator('.five-lead-tool-hint').count(),0);
  // Apply compensation through the real editor, then inspect the rendered and
  // exact nominal/result coordinates on this original workpiece.
  await page.locator(`[data-five-intent="${holes[0].id}"]`).click();await idle();await page.locator('.ribbon-command[data-command-id="five.tool.compensation"]').click();await idle();
  const stress=await page.addStyleTag({content:'.cam-context-pane::after,.cam-info-pane::after,.five-cut-tree::after{content:"";display:block;height:1800px;flex-shrink:0}.five-cut-tree{max-height:260px;overflow:auto}'});
  await page.evaluate(()=>{const c=fixture.view.viewportSceneProxy.sdoClient,base=c.invoke;fixture.restore=()=>c.invoke=base;fixture.canvas=fixture.view.viewport.renderer.domElement;fixture.hold=true;c.invoke=async function(...args){const r=await base.apply(this,args);if(args[1]==='CamProcess.PreviewCompensation'&&fixture.hold){fixture.hold=false;fixture.held=true;await new Promise(resolve=>fixture.release=resolve);fixture.held=false;}return r;};});
  const width=page.locator('[data-five-field="cutKerf"]');await width.fill('.8');await width.blur();await page.locator('[data-five-command="five.process.apply"]').click();await idle();await page.waitForFunction(()=>fixture.held);
  const scroll=await page.evaluate(()=>{const i=fixture.input=document.querySelector('[data-five-field="cutKerf"]');i.focus({preventScroll:true});i.setSelectionRange(0,1);fixture.events=0;i.addEventListener('lead-regression',()=>fixture.events++);const nodes=['.cam-context-pane','.cam-info-pane','.five-cut-tree'].map(s=>document.querySelector(s));nodes.forEach((n,i)=>n.scrollTop=70+i*25);return nodes.map(n=>n.scrollTop);});
  await page.evaluate(()=>{['.cam-context-pane','.cam-info-pane','.five-cut-tree'].forEach(s=>document.querySelector(s).scrollTop+=20);fixture.release();});
  await page.waitForFunction(id=>!fixture.view.fiveCompensation?.loading&&fixture.view.fiveCompensation.items.some(i=>i.intentId===id),holes[0].id);
  report.interaction=await page.evaluate(()=>{fixture.input.dispatchEvent(new Event('lead-regression'));return {input:fixture.input===document.querySelector('[data-five-field="cutKerf"]'),canvas:fixture.canvas===fixture.view.viewport.renderer.domElement,focus:document.activeElement===fixture.input,selection:[fixture.input.selectionStart,fixture.input.selectionEnd],scroll:['.cam-context-pane','.cam-info-pane','.five-cut-tree'].map(s=>document.querySelector(s).scrollTop),events:fixture.events};});
  assert.equal(report.interaction.input,true);assert.equal(report.interaction.canvas,true);assert.equal(report.interaction.focus,true);assert.deepEqual(report.interaction.selection,[0,1]);assert.deepEqual(report.interaction.scroll,scroll.map(n=>n+20));assert.equal(report.interaction.events,1);await page.evaluate(()=>fixture.restore());await stress.evaluate(n=>n.remove());
  await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-cut-tree'].forEach(s=>document.querySelector(s).scrollTop=0));
  report.cases.push('Delayed real native preview preserves the canvas, input, listener, focus, selection and latest scrolling in both sidebars and nested tree');
  const preview=(await invoke('CamProcess.PreviewCompensation',{expected:(await read()).expected,intentIds:[holes[0].id]})).items[0];assert.ok(!preview.error,preview.error);
  const bounds=ps=>[0,1,2].map(i=>[Math.min(...ps.map(p=>p[i])),Math.max(...ps.map(p=>p[i]))]);
  assert.notDeepEqual(bounds(preview.nominalPoints),bounds(preview.parts.find(p=>p.role==='Cut').points));
  report.visible=await page.evaluate(id=>{const v=fixture.view,original=v.fiveCutPreview.group.children.find(n=>n.userData.cuttingLineId===id),parts=[];v.fiveCompensationOverlay.group.traverse(n=>{if(n.isLine&&n.userData.compensatedIntentId===id)parts.push({role:n.userData.role,color:n.material.color.getHex(),count:n.geometry.attributes.position.count,opacity:n.material.opacity});});return {originalColor:original.material.color.getHex(),originalOpacity:original.material.opacity,parts};},holes[0].id);
  assert.ok(report.visible.originalOpacity<1);assert.ok(report.visible.parts.some(p=>p.role==='Cut'&&p.count>2&&p.opacity===1));assert.ok(report.visible.parts.some(p=>p.role==='LeadIn'&&p.count>1));assert.ok(report.visible.parts.some(p=>p.role==='LeadOut'&&p.count>1));
  report.cases.push('Actual compensation editor displays a faded nominal contour, geometrically distinct offset contour and both lead roles before generation; colors follow the path layer');
  await page.screenshot({path:resolve(output,'原轮廓与补偿.png')});
  report.nominalBounds=bounds(preview.nominalPoints);report.compensatedBounds=bounds(preview.parts.find(p=>p.role==='Cut').points);
  report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;report.diagnostic=await page.evaluate(()=>({error:window.fixture?.view.error,compensation:window.fixture?.view.fiveCompensation,leads:window.fixture?.view.fiveLeads})).catch(()=>null);await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,cases:report.cases,error:report.error}));await browser.close();}
