import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {browserAssetPath,browserReportDirectory,serveBrowserAsset} from './browserPackageRuntime.mjs';

const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const output=browserReportDirectory('machine-dock-details'),browser=await chromium.launch({channel:'msedge',headless:true});
try {
 const page=await browser.newPage();
 await page.route('http://127.0.0.1:49217/**',serveBrowserAsset);
 await page.goto('http://127.0.0.1:49217/');
 for(const path of ['iCAX-UI/SDK/AppShell/theme/workbench.css','apps/five-axis-cam/webpage/workspace.css','apps/_shared/workbench/styles/workbench.css'])await page.addStyleTag({content:readFileSync(browserAssetPath(path),'utf8')});
 await page.evaluate(async()=>{
  const {renderMachineDock}=await import('/src/apps/five-axis-cam/webpage/machineSimulation.mjs');
  const {selectTool}=await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs');
  const {patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs');
  document.body.style='margin:0;height:100vh;overflow:hidden';
  document.body.innerHTML='<main class="cam-workbench five-workspace" data-five-area="simulation" style="height:calc(100vh - 80px);display:grid"><aside class="cam-context-pane" style="grid-area:left;overflow:auto"></aside><div class="cam-viewer" style="grid-area:viewer;min-height:0"><canvas></canvas></div><aside class="cam-info-pane" style="grid-area:right;overflow:auto"></aside><section class="five-dock" data-five-dock></section></main><footer style="height:80px;background:#eef2f5;position:relative">日志区</footer>';
  const setup={id:'setup',machineSetup:{machineId:'machine'}},v={fiveAxisArea:'simulation',scene:{domain:{activeSetupId:'setup',expected:{epoch:'e'},setups:[setup]},machines:[]}};
  selectTool(v,'machine-simulation');renderMachineDock(v);
  v.fiveMachine.report={playbackAllowed:false,failedTarget:{positionMm:[0,0,50],beamDirection:[0,1,0],role:'Cut'},frames:[],reason:'工作台与 C 回转叉架间隙不足 0.1 mm。'.repeat(55)};
  const pane=conditional=>'<input data-five-key="draft" value="draft"><div data-five-key="nested" class="nested" style="height:90px;overflow:auto"><div style="height:1500px">嵌套滚动区</div></div>'+(conditional?'<input data-five-key="conditional" value="changed">':'')+'<div style="height:1800px"></div>';
  window.fixture={v,render(conditional=false){patchWorkspacePanels(document.body,[['[data-five-dock]',renderMachineDock(v)],['.cam-context-pane',pane(false)],['.cam-info-pane',pane(conditional)]]);}};fixture.render();
 });
 const cases=[];
 for(const size of [{width:1024,height:600},{width:1366,height:768},{width:1600,height:1000}]){
  await page.setViewportSize(size);
  const summary=page.locator('[data-five-key="machine-recovery-detail"] summary');
  if(!await summary.evaluate(e=>e.parentElement.open))await summary.click();
  await page.locator('[data-five-key="machine-recovery-detail"] p').evaluate(e=>e.lastChild.parentElement.scrollIntoView({block:'end'}));
  const bounds=await page.evaluate(()=>{const dock=document.querySelector('[data-five-dock]'),p=document.querySelector('details p'),r=p.getBoundingClientRect(),d=dock.getBoundingClientRect(),hit=document.elementFromPoint(r.left+10,Math.min(r.bottom-3,d.bottom-3));return {overflow:getComputedStyle(dock).overflowY,scroll:dock.scrollTop,scrollHeight:dock.scrollHeight,height:dock.clientHeight,end:r.bottom,dockEnd:d.bottom,footer:document.querySelector('footer').getBoundingClientRect().top,hit:p.contains(hit)};});
  assert.equal(bounds.overflow,'auto');assert.ok(bounds.scroll>0);assert.ok(bounds.end<=bounds.dockEnd+1);assert.ok(bounds.dockEnd<=bounds.footer+1);assert.equal(bounds.hit,true);
  await page.locator('[data-five-command="five.machine.reset"]').scrollIntoViewIfNeeded();
  assert.ok(await page.locator('[data-five-command="five.machine.reset"]').evaluate(e=>{const r=e.getBoundingClientRect();return e.contains(document.elementFromPoint(r.left+r.width/2,r.top+r.height/2));}));
  cases.push({size,...bounds});
 }
 const interaction=await page.evaluate(async()=>{
  const left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane'),nested=right.querySelector('.nested'),input=right.querySelector('input'),canvas=document.querySelector('canvas'),dock=document.querySelector('[data-five-dock]');
  input.focus({preventScroll:true});input.value='12.345';input.setSelectionRange(1,4);left.scrollTop=120;right.scrollTop=140;nested.scrollTop=180;
  let events=0;input.addEventListener('probe',()=>events++);
  let release;const pending=new Promise(r=>release=r).then(()=>{fixture.v.fiveMachine.report.reason+='异步检查已完成';fixture.render(true);});
  left.scrollTop=230;right.scrollTop=270;nested.scrollTop=310;const dockScroll=dock.scrollTop;release();await pending;input.dispatchEvent(new Event('probe'));
  return {same:input===right.querySelector('input'),canvas:canvas===document.querySelector('canvas'),focus:document.activeElement===input,selection:[input.selectionStart,input.selectionEnd],scroll:[left.scrollTop,right.scrollTop,nested.scrollTop],dock:dock.scrollTop===dockScroll,open:document.querySelector('details').open,conditional:!!right.querySelector('[data-five-key="conditional"]'),events};
 });
 assert.deepEqual(interaction,{same:true,canvas:true,focus:true,selection:[1,4],scroll:[230,270,310],dock:true,open:true,conditional:true,events:1});
 await page.locator('[data-five-key="machine-recovery-detail"] p').evaluate(e=>e.scrollIntoView({block:'end'}));
 await page.screenshot({path:resolve(output,'details-readable.png')});
 const report={passed:true,cases,interaction,scope:'Production dock rendering, styles and DOM patching in real Chromium; controlled failure report'};
 writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
} finally {await browser.close();}
