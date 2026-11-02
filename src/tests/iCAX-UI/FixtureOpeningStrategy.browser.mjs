// Actual production parameter rendering and DOM patching in Edge Chromium.
// Scene read facts are controlled; this does not certify native fixture geometry.
import assert from 'node:assert/strict';
import {readFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve,sep,extname} from 'node:path';
import {chromium} from 'playwright';

const root=resolve(import.meta.dirname,'../../..');
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||resolve(root,'output/tests/fixture-opening-strategy/browser'));
mkdirSync(output,{recursive:true});
const browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'msedge'});
const report={passed:false,cases:[],coverage:{browser:'Edge Chromium',productionModules:true,nativeTransport:'controlled scene facts',nativeGeometry:false,standaloneCef:false}};
let page;
try{
  page=await browser.newPage({viewport:{width:1440,height:900}});report.errors=[];page.on('pageerror',error=>report.errors.push(error.message));
  await page.route('http://fixture-opening.test/**',route=>{
    const pathname=decodeURIComponent(new URL(route.request().url()).pathname);
    if(pathname==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><meta charset="utf-8"><body></body>'});
    const file=resolve(root,pathname.slice(1));
    if(!file.startsWith(root+sep))return route.abort();
    return route.fulfill({contentType:extname(file)==='.css'?'text/css':'text/javascript',body:readFileSync(file)});
  });
  await page.goto('http://fixture-opening.test/');
  await page.evaluate(async()=>{
    const [design,{patchWorkspacePanels}]=await Promise.all([import('/src/apps/five-axis-cam/webpage/fixtureDesign.mjs'),import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs')]);
    const link=document.createElement('link');link.rel='stylesheet';link.href='/src/apps/five-axis-cam/webpage/workspace.css';document.head.append(link);
    await new Promise((resolve,reject)=>{link.onload=resolve;link.onerror=reject;});
    const style=document.createElement('style');style.textContent='*{box-sizing:border-box}body{margin:0;font:13px system-ui}#mount{height:100vh;display:grid;grid-template-columns:280px 1fr 440px}.cam-context-pane,.cam-info-pane{height:100vh;overflow:auto;background:#f3f6f8;padding:12px}.cam-render-viewport{background:#ccd9df}.cam-render-viewport canvas{width:100%;height:100%}.fixture-left-nested{height:140px;overflow:auto}.fixture-spacer{height:1000px}.five-fixture-generator{height:640px}.five-design-body{height:430px;overflow:auto;flex:none}';document.head.append(style);
    document.body.innerHTML='<main id="mount"><aside class="cam-context-pane"></aside><section class="cam-render-viewport"><canvas></canvas></section><aside class="cam-info-pane"></aside></main>';
    const view={scene:{model:{isLoaded:true},domain:{activeSetupId:'setup',setups:[{id:'setup',name:'孔洞工件',fixtureDesign:{}}],fixtureUnits:[]}}};
    const f=window.fixtureOpening={view,design,reads:0};
    f.render=()=>patchWorkspacePanels(document.querySelector('#mount'),[
      ['.cam-context-pane','<h3 data-five-key="tree-title">工件</h3><div class="fixture-left-nested" data-five-key="left-nested"><div class="fixture-spacer">孔洞工件</div></div><div class="fixture-spacer"></div>'],
      ['.cam-info-pane',design.renderFixtureDesign(view)+'<div class="fixture-spacer" data-five-key="right-spacer"></div>']
    ]);
    document.querySelector('#mount').addEventListener('input',event=>{const name=event.target.dataset.fiveFixtureField;if(!name?.startsWith('design:'))return;design.designChanged(view,name.slice(7),event.target.value);f.render();});
    f.sceneProxy={async invoke(method){if(method!=='CamScene.ReadState')throw Error(method);++f.reads;await new Promise(resolve=>f.release=resolve);return {inputRevision:'2'};}};
    f.refresh=async()=>{f.lastRead=await f.sceneProxy.invoke('CamScene.ReadState');f.render();};
    f.preview=status=>{
      const draft=design.designDraft(view);draft.dirty=false;
      view.scene.domain.setups[0].fixtureDesign.candidate={id:'candidate',validity:'Current',supportCheck:{status:'Supported',marginMm:10},parts:[],openingPreclearance:{status,validity:'Current',boundaryCount:3,unclassifiedFaceCount:status==='Partial'?1:0,marginMm:1,toolpathRequired:false}};
      view.fiveFixtureDesignPage='preview';f.render();
    };
    f.render();
  });
  const field=key=>page.locator(`[data-five-fixture-field="design:${key}"]`);
  assert.equal(await field('windowMode').inputValue(),'Automatic');assert.equal(await field('windowWeb').inputValue(),'8');
  assert.equal(await page.getByLabel('板件开窗',{exact:true}).isVisible(),true);
  assert.equal(await field('windowMode').locator('option:checked').innerText(),'支撑与底板自动开窗');
  assert.equal(await field('windowMode').evaluate(node=>!!node.closest('[data-five-key="design-group-plates"]')),true);
  assert.equal(await field('windowWeb').evaluate(node=>!!node.closest('details')),false);
  assert.equal(await page.getByLabel('开窗保留板带',{exact:true}).isVisible(),true);
  assert.equal(await field('cutClearanceMode').inputValue(),'None');assert.equal(await field('cutClearanceMode').locator('option:checked').innerText(),'待刀路生成');
  const parameters=await page.locator('[data-five-key="design-parameters"]').innerText();
  assert.match(parameters,/孔洞/);assert.match(parameters,/无需先生成刀路/);assert.match(parameters,/连接与间隙/);
  assert.equal(await field('cutClearanceMm').count(),0);assert.equal(await field('beamRadiusMm').count(),0);
  await page.screenshot({path:resolve(output,'automatic-windows-parameters.png')});
  report.cases.push('A fresh design exposes automatic support/base windows and 8 mm material bands without opening advanced settings; geometry preclearance does not require toolpaths');

  await field('windowWeb').fill('18');await field('windowMode').selectOption('None');assert.equal(await field('windowWeb').count(),0);
  await field('windowMode').selectOption('Automatic');assert.equal(await field('windowWeb').inputValue(),'18');
  await field('cutClearanceMode').selectOption('Toolpath');assert.equal(await field('cutClearanceMm').isVisible(),true);assert.equal(await field('beamRadiusMm').isVisible(),true);
  await field('beamRadiusMm').fill('0.75');await field('cutClearanceMode').selectOption('None');assert.equal(await field('beamRadiusMm').count(),0);
  await field('cutClearanceMode').selectOption('Toolpath');assert.equal(await field('beamRadiusMm').inputValue(),'0.75');
  await field('cutClearanceMode').selectOption('None');
  report.cases.push('Window and later toolpath settings show only applicable fields and restore independently edited hidden values');

  await page.evaluate(()=>{const f=fixtureOpening;f.pending=f.refresh();});await page.waitForFunction(()=>!!fixtureOpening.release);
  await field('windowWeb').fill('19.25');
  const expected=await page.evaluate(()=>{
    const f=fixtureOpening,input=document.querySelector('[data-five-fixture-field="design:windowWeb"]');
    f.kept={input,canvas:document.querySelector('canvas'),left:document.querySelector('.cam-context-pane'),right:document.querySelector('.cam-info-pane'),body:document.querySelector('.five-design-body'),nested:document.querySelector('[data-five-key="left-nested"]')};f.listenerCalls=0;
    input.addEventListener('retention-probe',()=>++f.listenerCalls);input.focus({preventScroll:true});input.setSelectionRange(1,4,'backward');
    return ['left','right','body','nested'].map((key,i)=>{f.kept[key].scrollTop=60+i*25;return f.kept[key].scrollTop;});
  });
  assert.ok(expected.every(x=>x>0));
  const retained=await page.evaluate(async()=>{
    const f=fixtureOpening;f.release();await f.pending;f.kept.input.dispatchEvent(new Event('retention-probe'));
    return {input:f.kept.input===document.querySelector('[data-five-fixture-field="design:windowWeb"]'),canvas:f.kept.canvas===document.querySelector('canvas'),body:f.kept.body===document.querySelector('.five-design-body'),nested:f.kept.nested===document.querySelector('[data-five-key="left-nested"]'),focus:document.activeElement===f.kept.input,value:f.kept.input.value,selection:[f.kept.input.selectionStart,f.kept.input.selectionEnd,f.kept.input.selectionDirection],scroll:['left','right','body','nested'].map(key=>f.kept[key].scrollTop),listenerCalls:f.listenerCalls,reads:f.reads};
  });
  assert.deepEqual(retained,{input:true,canvas:true,body:true,nested:true,focus:true,value:'19.25',selection:[1,4,'backward'],scroll:expected,listenerCalls:1,reads:1});
  await field('windowWeb').press('7');assert.equal(await field('windowWeb').inputValue(),'175');
  report.retention=retained;report.cases.push('A delayed scene reply retains later typing, focus, backward selection, both pane and nested scrolls, input/canvas/body nodes and existing listeners');

  await page.evaluate(()=>fixtureOpening.preview('Prepared'));
  await page.getByText('孔洞预避让已生成',{exact:true}).waitFor();
  const prepared=await page.locator('[data-five-key="fixture-design-result"]').innerText();assert.match(prepared,/实际光路/);assert.match(prepared,/待验|验证/);
  await page.evaluate(()=>fixtureOpening.preview('Partial'));
  await page.getByText('部分孔洞待处理',{exact:true}).waitFor();
  const partial=await page.locator('[data-five-key="fixture-design-result"]').innerText();assert.match(partial,/实际光路/);assert.match(partial,/1 个面的孔界未能自动处理/);assert.doesNotMatch(partial,/光路已通过/);
  await page.evaluate(()=>{document.querySelector('.cam-info-pane').scrollTop=0;document.querySelector('.five-design-body').scrollTop=0;});
  await page.screenshot({path:resolve(output,'opening-preclearance-partial.png')});
  await page.evaluate(()=>{const f=fixtureOpening,c=f.view.scene.domain.setups[0].fixtureDesign.candidate;c.validity='Stale';c.openingPreclearance.validity='Stale';f.render();});
  await page.getByText('孔洞预避让已过期',{exact:true}).waitFor();
  assert.equal(await page.getByText('部分孔洞待处理',{exact:true}).count(),0);
  const stale=await page.locator('[data-five-key="fixture-design-result"]').innerText();assert.match(stale,/工件或夹具位置、参数已变化/);assert.match(stale,/实际光路待验证/);assert.doesNotMatch(stale,/孔洞预避让已生成|光路已通过/);
  await page.evaluate(()=>{const f=fixtureOpening,c=f.view.scene.domain.setups[0].fixtureDesign.candidate;c.validity='Current';c.openingPreclearance.validity='Stale';c.openingPreclearance.status='Unchecked';f.render();});
  await page.getByText('孔洞预避让已过期',{exact:true}).waitFor();
  const openingOnlyStale=await page.locator('[data-five-key="opening-preclearance-result"]').innerText();assert.match(openingOnlyStale,/工件或夹具位置、参数已变化/);assert.doesNotMatch(openingOnlyStale,/孔洞预避让已生成|部分孔洞待处理/);
  assert.deepEqual(report.errors,[]);await page.screenshot({path:resolve(output,'opening-preclearance-stale.png')});
  await page.evaluate(()=>{const f=fixtureOpening,c=f.view.scene.domain.setups[0].fixtureDesign.candidate;c.validity='Stale';c.openingPreclearance.validity='Current';c.openingPreclearance.status='Prepared';c.cutClearance={validity:'Stale',status:'Unchecked'};f.render();});
  await page.getByText('孔洞预避让已生成',{exact:true}).waitFor();
  assert.equal(await page.getByText('孔洞预避让已过期',{exact:true}).count(),0);
  await page.getByText('切割避让已过期',{exact:true}).waitFor();
  report.cases.push('Prepared, Partial and Stale geometry results stay distinct; optical-only expiry preserves a current opening certificate and still requires actual optical validation');
  report.passed=true;writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:true,output,cases:report.cases},null,2));
}catch(error){report.error=error.stack||error.message;await page?.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));throw error;}
finally{await browser.close();}
