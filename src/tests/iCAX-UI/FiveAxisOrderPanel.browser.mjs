import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {browserAssetPath,browserReportDirectory} from './browserPackageRuntime.mjs';
const output=browserReportDirectory('order-popover-20261101'),report={passed:false,standaloneCef:false,cases:[],errors:[]};
const browser=await chromium.launch({channel:'msedge',headless:true}),page=await browser.newPage({viewport:{width:1600,height:900}});
try{
 page.setDefaultTimeout(15000);page.on('pageerror',e=>report.errors.push(e.message));
 await page.route('http://localhost:17761/**',route=>{
  const path=new URL(route.request().url()).pathname;
  if(path==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/src/iCAX-UI/SDK/AppShell/theme/workbench.css"><body><div id="app"></div><script type="module" src="/src/tests/iCAX-UI/fixtures/fiveAxisPreview.mjs"></script>'});
  if(path==='/src/iCAX-UI/SDK/runtime.mjs')return route.fulfill({contentType:'text/javascript',body:'export async function connectApplication(){return globalThis.__fiveFixture.appProxy;}'});
  if(!path.startsWith('/src/'))return route.abort();return route.fulfill({contentType:path.endsWith('.css')?'text/css':'text/javascript',body:readFileSync(browserAssetPath(path.slice(5)))});
 });
 await page.goto('http://localhost:17761/?product=icax.five-axis-cam');await page.waitForFunction(()=>globalThis.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(async()=>{
  await __fiveFixture.populate(44);const f=window.orderTest={calls:[]},v=f.view=__fiveFixture.view;
  const cuts=v.scene.intentToolpaths.map((c,i)=>({...c,enabled:true,locked:false,machiningKind:i<3?'Mark':'Cut',boundaryRole:i===43?'OuterTrim':'Opening',startPoint:[i%6*20,Math.floor(i/6)*20,45],previewPoints:[[i%6*20,Math.floor(i/6)*20,45],[i%6*20+10,Math.floor(i/6)*20+10,45]]}));
  const domain=f.native=structuredClone(v.scene.domain);domain.intentToolpaths=cuts;domain.setups[0].definitionOrder=cuts.map(c=>c.id);domain.setups[0].cutGroups=[];domain.setups[0].generatedGeometry={status:'Missing',operations:[],transitions:[],ordering:{strategy:'MarkAndHoleTrim',options:{pattern:'Snake',objective:'Time',scope:'Selected',plane:'Custom',normal:[0,0,0],optimizeStarts:true}}};Object.assign(v.scene,{intentToolpaths:cuts,domain:structuredClone(domain)});
  f.originalNumbers=Object.fromEntries(cuts.map(c=>[c.id,c.number]));
  const proxy=v.viewportSceneProxy,original=proxy.invoke.bind(proxy);
  const commit=(payload,ids)=>{const s=domain.setups[0];s.definitionOrder=ids;s.generatedGeometry.ordering={mode:payload.mode,strategy:payload.strategy||'MarkHoleTrim',options:payload.options,intentIds:ids};domain.expected.mutationSequence=String(Number(domain.expected.mutationSequence)+1);return {changed:true,expected:structuredClone(domain.expected),definitionOrder:ids,ordering:s.generatedGeometry.ordering,rapidPolicies:{},rapidDefaultPolicy:{strategy:'Quick'}};};
  proxy.invoke=async(method,payload)=>{
   f.calls.push({method,payload:structuredClone(payload)});
   if(method==='CamScene.ReadState')return structuredClone(domain);
   if(method==='CamPlan.BeginOrder'){
    if(f.hold){f.held=true;await new Promise(r=>f.release=r);f.held=false;f.hold=false;}
    const ids=[...cuts.slice(0,3),...cuts.slice(3,-1).reverse(),cuts.at(-1)].map(c=>c.id);
    f.task={status:'Completed',epoch:domain.expected.epoch,taskId:crypto.randomUUID(),result:commit(payload,ids)};return structuredClone(f.task);
   }
   if(method==='CamPlan.QueryStatus')return structuredClone(f.task);
   if(method==='CamPlan.SetOrder')return commit(payload,payload.intentIds);
   if(method==='CamCut.ManageTree'){
    if(payload.action!=='Move')throw Error('Unexpected tree action');
    const ids=domain.setups[0].definitionOrder.filter(id=>!payload.intentIds.includes(id)),index=ids.indexOf(payload.target.id)+(payload.target.position==='After'?1:0);ids.splice(index,0,...payload.intentIds);
    f.orderProjection=commit({...payload,mode:'Manual'},ids);return {changed:true,orderOnly:true,setupId:payload.setupId,expected:structuredClone(domain.expected)};
   }
   if(method==='CamPlan.QueryOrder')return structuredClone(f.orderProjection);
   if(method==='CamPlan.Generate')throw Error('Sorting must not generate toolpaths');
   if(method==='CamProcess.BeginPreview')return {status:'Completed',result:{items:[]}};
   if(method==='CamCut.ReadToolAxis')return {mode:'SurfaceNormal',baseDirection:[0,0,1],setupBaseDirection:[0,0,1]};
   return original(method,payload);
  };
  await __icaxAppShell.selectRibbonTab('toolpath');await __icaxAppShell.executeRibbonCommand('five.cam-page.order');
 });
 const command=id=>page.locator('.product-ribbon .ribbon-command[data-command-id="'+id+'"]');
 const automatic=command('five.tool.auto-order'),weight=page.locator('[data-ribbon-input="five.order.height-penalty"]');
 const manualChoice=mode=>command('five.tool.manual-order-'+mode);
 const chooseManual=async mode=>{await manualChoice(mode).click();await idle();};
 const idle=()=>page.waitForFunction(()=>!orderTest.view.pending&&!orderTest.view.fiveOrder?.request&&!orderTest.view.fiveOrder?.running);
 const controls=()=>page.locator('.five-cut-row-controls button');
 const verifyLabels=async()=>{
  const state=await page.evaluate(()=>{
   const v=orderTest.view,cuts=v.scene.intentToolpaths,ids=v.scene.domain.setups[0].definitionOrder,byId=new Map(cuts.map(c=>[c.id,c])),enabled=ids.filter(id=>byId.get(id).enabled!==false);
   return {rows:ids.map(id=>{const c=byId.get(id);return {id,name:c.name,number:c.number,sequence:enabled.includes(id)?String(enabled.indexOf(id)+1):'',label:document.querySelector(`[data-five-intent="${id}"] .five-cut-name`).textContent};}),scene:[...document.querySelectorAll('.five-path-number')].map(el=>({id:el.dataset.fiveIntentId,text:el.textContent})),selected:v.fiveAxisUI.selectedIntentId,header:document.querySelector('.five-cut-parameters>header')?.textContent};
  });
  for(const row of state.rows)assert.equal(row.label,row.name);
  for(const row of state.scene)assert.equal(row.text,state.rows.find(c=>c.id===row.id).sequence);
  const selected=state.rows.find(c=>c.id===state.selected);if(selected){assert.equal(state.header,selected.label);assert.ok((await page.locator('.five-cut-parameters').textContent()).includes('固定编号 #'+selected.number));}
  assert.deepEqual(await page.evaluate(()=>Object.fromEntries(orderTest.native.intentToolpaths.map(c=>[c.id,c.number]))),await page.evaluate(()=>orderTest.originalNumbers));
 };
 const setPage=id=>page.evaluate(id=>__icaxAppShell.executeRibbonCommand('five.cam-page.'+id),id);
 const verifyPages=async()=>{
  for(const id of ['cutting','rapid','process','order']){
   await setPage(id);
   assert.equal(await page.locator('.five-cut-move').count(),id==='order'?44*4:0);
   assert.equal(await page.locator('.five-cut-row-controls button:not(.five-cut-move)').count(),id==='cutting'?44*2:0);
   assert.equal(await page.locator('[data-command-id="five.tree.Group"],[data-command-id="five.tree.Ungroup"]').count(),0);
   assert.equal(await page.getByText('组织',{exact:true}).count(),0);
  }
 };
 await verifyPages();report.cases.push('Each CAM page exposes only its own row actions: four order controls, two cutting controls, none on rapid/process');
 await page.locator('[data-five-intent]').first().click();
 const width=(await page.locator('.cam-viewport').boundingBox()).width;
 assert.equal(await page.locator('[data-five-key="auto-order-panel"],[data-five-key="order-inspector-tabs"]').count(),0);
 assert.equal(await weight.inputValue(),'3');assert.equal(await weight.getAttribute('min'),null);assert.equal(await weight.getAttribute('max'),null);
 const buttonBox=await automatic.boundingBox(),inputBox=await weight.boundingBox();assert.ok(inputBox.x+inputBox.width<=buttonBox.x);
 assert.equal(await page.locator('.ribbon-command-menu-toggle[data-command-id="five.tool.auto-order"]').count(),0);
 assert.equal(await page.locator('.ribbon-command-menu-toggle[data-command-id^="five.tool.manual-order"]').count(),0);
 assert.equal(await page.evaluate(()=>orderTest.calls.some(c=>c.method==='CamPlan.BeginOrder')),false);
 assert.equal(await controls().evaluateAll(es=>es.some(e=>!e.disabled)),true);
 assert.equal((await page.locator('.cam-viewport').boundingBox()).width,width);
 await weight.fill('12.345');await weight.evaluate(el=>{el.setSelectionRange(1,4);window.savedWeight=el;window.weightEvents=0;el.addEventListener('input',()=>weightEvents++);});
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.select-all'));
 assert.equal(await weight.evaluate(el=>el===savedWeight),true);assert.equal(await weight.evaluate(el=>document.activeElement===el&&el.selectionStart===1&&el.selectionEnd===4),true);
 await weight.fill('12.5');assert.equal(await page.evaluate(()=>weightEvents),1);await page.keyboard.press('Enter');assert.equal(await weight.count(),1);assert.equal(await weight.inputValue(),'12.5');
 report.cases.push('The labelled height penalty stays immediately before automatic sorting; no automatic dropdown remains; editing never sorts or changes viewport width; input node, focus, selection and listener survive refresh');
 // Constrain the actual right inspector to exercise both sidebar scroll containers.
 await page.locator('[data-five-intent]').first().click();await page.addStyleTag({content:'.cam-info-pane{max-height:110px!important;overflow:auto!important}'});
 await page.evaluate(()=>{orderTest.canvas=orderTest.view.viewport.renderer.domElement;orderTest.hold=true;});await page.locator('[data-five-cut-name]').evaluate(el=>{el.focus();el.setSelectionRange(1,3);});await page.evaluate(()=>{void __icaxAppShell.executeRibbonCommand('five.tool.auto-order');});await page.waitForFunction(()=>orderTest.held);
 assert.equal(await controls().evaluateAll(es=>es.every(e=>e.disabled)),true);
 await page.evaluate(()=>{
  const pane=document.querySelector('.cam-context-pane');const left=[pane,...pane.querySelectorAll('*')].find(n=>n.scrollHeight>n.clientHeight+50&&/auto|scroll/.test(getComputedStyle(n).overflowY)),right=document.querySelector('.cam-info-pane');
  if(!left)throw Error('Missing left scroll container');window.orderScrollNodes=[left,right];for(const n of orderScrollNodes)n.scrollTop=50;
  window.savedName=document.querySelector('[data-five-cut-name]');savedName.focus({preventScroll:true});savedName.setSelectionRange(1,3);window.nameEvents=0;savedName.addEventListener('input',()=>nameEvents++);
  // These are the latest positions, changed after the request started.
  for(const n of orderScrollNodes)n.scrollTop=90;orderTest.scrolls=orderScrollNodes.map(n=>n.scrollTop);
 });
 assert.ok((await page.evaluate(()=>orderTest.scrolls)).every(n=>n>0));await page.evaluate(()=>orderTest.release());await idle();
 const state=await page.evaluate(()=>({sameName:savedName===document.querySelector('[data-five-cut-name]'),focus:document.activeElement===savedName,start:savedName.selectionStart,end:savedName.selectionEnd,scrolls:orderScrollNodes.map(n=>n.scrollTop),canvas:orderTest.canvas===orderTest.view.viewport.renderer.domElement,error:orderTest.view.error||''}));
 assert.deepEqual(state,{sameName:true,focus:true,start:1,end:3,scrolls:await page.evaluate(()=>orderTest.scrolls),canvas:true,error:''});
 assert.equal(await controls().evaluateAll(es=>es.some(e=>!e.disabled)),true);
 await page.locator('[data-five-cut-name]').evaluate(el=>el.dispatchEvent(new Event('input',{bubbles:true})));assert.equal(await page.evaluate(()=>nameEvents),1);
 const request=await page.evaluate(()=>orderTest.calls.find(c=>c.method==='CamPlan.BeginOrder').payload);assert.equal(request.strategy,'MarkHoleTrim');assert.deepEqual(request.options,{heightPenaltyWeight:12.5});assert.equal(Object.hasOwn(request,'preview'),false);assert.equal(Object.hasOwn(request,'intentIds'),false);
 assert.equal(await page.evaluate(()=>orderTest.calls.some(c=>['CamPlan.SetOrder','CamPlan.Generate'].includes(c.method))),false);
 report.cases.push('One click commits automatic sorting; old saved options do not leak; during async completion latest sidebar scrolls, focus, selection, input node/listener and canvas survive; row controls recover');
 for(const value of ['-2.5','0','99']){await weight.fill(value);await page.keyboard.press('Enter');await automatic.click();await idle();assert.equal(await page.evaluate(()=>orderTest.calls.filter(c=>c.method==='CamPlan.BeginOrder').at(-1).payload.options.heightPenaltyWeight),Number(value));}
 for(const value of ['','abc','Infinity']){const count=await page.evaluate(()=>orderTest.calls.filter(c=>c.method==='CamPlan.BeginOrder').length);await weight.fill(value);await page.keyboard.press('Enter');await automatic.click();await idle();assert.match(await page.evaluate(()=>orderTest.view.error),/有效数值/);assert.equal(await page.evaluate(()=>orderTest.calls.filter(c=>c.method==='CamPlan.BeginOrder').length),count);assert.equal(await controls().evaluateAll(es=>es.some(e=>!e.disabled)),true);}
 report.cases.push('Negative, zero and values above 3 are accepted; empty, nonnumeric and nonfinite input is rejected without a native request or disabled row controls');
 await weight.fill('3');await page.keyboard.press('Enter');
 await page.locator('[data-five-intent]').nth(0).click();await page.locator('[data-five-intent]').nth(1).click({modifiers:['Control']});
 assert.equal(await manualChoice('current').isDisabled(),true);assert.equal(await manualChoice('start').isEnabled(),true);
 const manualBefore=await page.evaluate(()=>[...orderTest.native.setups[0].definitionOrder]),anchor=manualBefore[7];
 await page.locator(`[data-five-intent="${anchor}"]`).click();await chooseManual('current');
 assert.deepEqual(await page.evaluate(()=>orderTest.view.fiveOrder.prefix),manualBefore.slice(0,8));
 assert.match(await page.locator('.five-order-status').textContent(),/从当前开始/);
 const manualCalls=await page.evaluate(()=>orderTest.calls.filter(c=>c.method==='CamPlan.SetOrder').length);
 await page.locator(`[data-five-intent="${manualBefore[1]}"]`).click();assert.deepEqual(await page.evaluate(()=>orderTest.view.fiveOrder.prefix),manualBefore.slice(0,8));
 await chooseManual('start');assert.deepEqual(await page.evaluate(()=>orderTest.view.fiveOrder.prefix),[]);assert.match(await page.locator('.five-order-status').textContent(),/从头开始/);
 assert.equal(await page.evaluate(()=>orderTest.calls.filter(c=>c.method==='CamPlan.SetOrder').length),manualCalls,'changing the start mode is a draft, without an implicit save');
 await chooseManual('current');await page.locator(`[data-five-intent="${manualBefore[11]}"]`).click();await page.keyboard.press('Enter');await idle();
 const manualExpected=[...manualBefore.slice(0,8),manualBefore[11],...manualBefore.slice(8).filter(id=>id!==manualBefore[11])];
 assert.deepEqual(await page.evaluate(()=>orderTest.native.setups[0].definitionOrder),manualExpected);assert.equal(await page.evaluate(()=>orderTest.calls.filter(c=>c.method==='CamPlan.SetOrder').at(-1).payload.mode),'Manual');
 await chooseManual('start');assert.deepEqual(await page.evaluate(()=>orderTest.view.fiveOrder.prefix),[]);await page.locator(`[data-five-intent="${manualExpected[12]}"]`).click();assert.deepEqual(await page.evaluate(()=>orderTest.view.fiveOrder.prefix),[manualExpected[12]]);await page.keyboard.press('Escape');
 assert.equal(await controls().evaluateAll(es=>es.some(e=>!e.disabled)),true);report.cases.push('Two direct manual commands, without a dropdown: current-start requires a single selection, preserves the selected prefix immediately and ignores earlier paths; switching modes never saves; only chosen suffix paths move, Enter commits and Escape cancels');
 const rowButton=(action,id)=>page.locator('[data-five-command="five.tree.Row'+action+':'+id+'"]');
 const idsBefore=await page.evaluate(()=>[...orderTest.native.setups[0].definitionOrder]),moved=idsBefore[5];let expected=[...idsBefore];
 await page.locator('[data-five-intent="'+moved+'"]').click();await verifyLabels();
 for(const action of ['Up','Down','Top','Bottom']){
  const count=await page.evaluate(()=>orderTest.calls.filter(c=>c.method==='CamCut.ManageTree').length),reads=await page.evaluate(()=>orderTest.calls.filter(c=>c.method==='CamScene.ReadState').length),index=expected.indexOf(moved),next=action==='Up'?index-1:action==='Down'?index+1:action==='Top'?0:expected.length-1;
  await rowButton(action,moved).click();await idle();expected.splice(index,1);expected.splice(next,0,moved);
  assert.equal(await page.evaluate(()=>orderTest.calls.filter(c=>c.method==='CamCut.ManageTree').length),count+1,'One click must commit exactly once');
  assert.deepEqual(await page.evaluate(()=>orderTest.native.setups[0].definitionOrder),expected);
  assert.deepEqual(await page.locator('[data-five-intent]').evaluateAll(es=>es.map(e=>e.dataset.fiveIntent)),expected);
  assert.equal(await page.evaluate(()=>orderTest.calls.filter(c=>c.method==='CamScene.ReadState').length),reads,'A row move must not read full scene state');
  await verifyLabels();
 }
 assert.equal(await rowButton('Down',moved).isDisabled(),true);assert.equal(await rowButton('Bottom',moved).isDisabled(),true);
 report.cases.push('All four row controls move only their own row exactly once; UI updates through the dedicated order query without reading the full scene');
 await page.evaluate(async()=>{const f=orderTest,id=f.native.setups[0].definitionOrder[0];for(const d of [f.native,f.view.scene.domain])d.intentToolpaths.find(c=>c.id===id).enabled=false;await __icaxAppShell.executeRibbonCommand('five.cam-page.cutting');});
 await verifyLabels();
 await page.evaluate(async()=>{const f=orderTest,id=f.native.setups[0].definitionOrder[0];for(const d of [f.native,f.view.scene.domain])d.intentToolpaths.find(c=>c.id===id).enabled=true;await __icaxAppShell.executeRibbonCommand('five.cam-page.order');});
 report.cases.push('Names have no number suffix after sorting, row moves and disabling; sequence stays in its column and scene, fixed numbers stay in details');
 await page.evaluate(async()=>{
  const f=orderTest,d=f.native,ids=d.setups[0].definitionOrder;
  d.setups[0].cutGroups=[{id:'test-row-group',name:'测试组',parentId:'',enabled:true,locked:false}];
  for(const c of d.intentToolpaths)c.groupId=ids.slice(1,4).includes(c.id)?'test-row-group':'';
  f.view.scene.domain=structuredClone(d);f.view.scene.intentToolpaths=f.view.scene.domain.intentToolpaths;
  await __icaxAppShell.executeRibbonCommand('five.display.numbers');
 });
 const groupMember=expected[3],groupFirst=expected[1];await rowButton('Top',groupMember).click();await idle();expected.splice(3,1);expected.splice(1,0,groupMember);
 assert.deepEqual(await page.evaluate(()=>orderTest.native.setups[0].definitionOrder),expected);
 assert.equal(await page.evaluate(()=>orderTest.calls.filter(c=>c.method==='CamCut.ManageTree').at(-1).payload.target.id),groupFirst);
 assert.equal(await rowButton('Top',groupMember).isDisabled(),true);
 await page.evaluate(async()=>{const f=orderTest,id=f.native.setups[0].definitionOrder[6];for(const d of [f.native,f.view.scene.domain])Object.assign(d.intentToolpaths.find(c=>c.id===id),{locked:true,ownLocked:true});await __icaxAppShell.executeRibbonCommand('five.display.numbers');});
 for(const action of ['Up','Down','Top','Bottom'])assert.equal(await rowButton(action,expected[6]).isDisabled(),true);
 assert.equal(await rowButton('Up',expected[7]).isDisabled(),true);assert.equal(await rowButton('Top',expected[7]).isDisabled(),true);
 report.cases.push('Row moves stay inside their group and stop at locked neighbours');
 await page.locator(`[data-five-intent="${expected[1]}"]`).click();await chooseManual('current');assert.deepEqual(await page.evaluate(()=>orderTest.view.fiveOrder.prefix),expected.slice(0,4));await page.keyboard.press('Escape');report.cases.push('Starting at a grouped path keeps the entire group together');
 assert.deepEqual(report.errors,[]);report.passed=true;await page.screenshot({path:resolve(output,'order-controls.png')});
}catch(error){report.failure=error.stack;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();}
