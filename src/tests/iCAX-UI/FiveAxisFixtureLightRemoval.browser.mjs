import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {serveBrowserAsset,browserReportDirectory} from './browserPackageRuntime.mjs';
import {chromium} from 'playwright';
const output=browserReportDirectory('remove-fixture-light-validation-20261102'),browser=await chromium.launch({channel:'msedge',headless:true});
try{
 const page=await browser.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));await page.route('https://removed-light.test/**',serveBrowserAsset);await page.goto('https://removed-light.test/');
 const result=await page.evaluate(async()=>{
  const {renderFixtureEditor}=await import('/src/apps/five-axis-cam/webpage/fixtureViews.mjs'),{fixtureCommand}=await import('/src/apps/five-axis-cam/webpage/fixtureActions.mjs'),{renderOverlay}=await import('/src/apps/five-axis-cam/webpage/workspaceViews.mjs'),{renderWorkspaceAlerts,workspaceAlertItems}=await import('/src/apps/five-axis-cam/webpage/alerts.mjs'),{patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs');
  const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],assembly={id:'a',validity:'Current',geometryValidity:'Current',cutClearance:{validity:'Stale',status:'Blocked'},parts:[{partId:'part',number:'S1',role:'Support',thickness:3}]};
  const domain={expected:{epoch:'e',mutationSequence:'1'},activeSetupId:'s',setups:[{id:'s',name:'姿态1',simulationCheck:{validity:'Current',collisionCount:'1',uncheckedCount:'0'},fixtureBeamCheck:{validity:'Stale',status:'Blocked'},generatedGeometry:{status:'Current',operations:[],transitions:[]}}],fixtureUnits:[{id:'u',setupId:'s',name:'支撑',kind:'Generated',partIds:['part'],transform:identity,design:{recipe:{cutClearanceMode:'Toolpath'},assembly}}],fixtures:[{id:'part',setupId:'s',unitId:'u',enabled:true}],intentToolpaths:[],workpieces:[],placements:[]};
  const view={fiveAxisArea:'toolpath',fiveCamPage:'cutting',fiveAlertsOpen:true,fiveFixtureLight:{open:true,setupId:'s',unitId:'u'},scene:{domain,intentToolpaths:[],machines:[],model:{isLoaded:true}}};
  document.body.innerHTML='<canvas></canvas><aside class="cam-context-pane" style="height:150px;overflow:auto"><input id="draft" value="123.456"><div class="nested" style="height:50px;overflow:auto"><div style="height:700px"></div></div><div style="height:2000px"></div></aside><aside class="cam-info-pane" style="height:200px;overflow:auto"></aside><section id="overlay"></section><section id="alerts"></section>';
  const calls=[],context={mount:document.body,activeRibbonTabId:'toolpath',isCurrentProject:()=>true,sceneProxy:{invoke:async m=>{calls.push(m);throw Error('Unexpected '+m);}}};
  let conditional=false;const ops={renderProject(){patchWorkspacePanels(document.body,[['.cam-info-pane',renderFixtureEditor(view)+(conditional?'<input data-five-key="condition" aria-label="条件字段">':'')+'<div style="height:2000px"></div>'],['#overlay',renderOverlay(context,view)],['#alerts',renderWorkspaceAlerts(view)]]);}};
  ops.renderProject();const breakdown=document.querySelector('[data-five-command="five.fixture.design.breakdown"]');if(!breakdown||breakdown.disabled)throw Error('A legacy light certificate still blocks fixture breakdown');
  if(document.querySelector('[data-five-fixture-light-dialog]')||document.body.textContent.includes('验证光路'))throw Error('Removed light workflow remains visible');
  const alerts=workspaceAlertItems(view);if(alerts.some(a=>a.id.startsWith('fixture-light:'))||!alerts.some(a=>a.id==='collision'))throw Error('Wrong alert scope');
  const input=document.querySelector('#draft'),canvas=document.querySelector('canvas'),left=document.querySelector('.cam-context-pane'),right=document.querySelector('.cam-info-pane'),nested=document.querySelector('.nested');input.focus({preventScroll:true});input.setSelectionRange(1,5);let events=0;input.addEventListener('probe',()=>events++);left.scrollTop=120;right.scrollTop=150;nested.scrollTop=180;
  await Promise.resolve();left.scrollTop=230;right.scrollTop=270;nested.scrollTop=310;conditional=true;domain.setups[0].fixtureBeamCheck={validity:'Current',status:'Unchecked'};ops.renderProject();input.dispatchEvent(new Event('probe'));
  const kept={input:input===document.querySelector('#draft'),canvas:canvas===document.querySelector('canvas'),focus:document.activeElement===input,selection:[input.selectionStart,input.selectionEnd],scroll:[left.scrollTop,right.scrollTop,nested.scrollTop],events,conditional:!!document.querySelector('[aria-label="条件字段"]')};
  await fixtureCommand(context,view,'light-open:u',ops);await fixtureCommand(context,view,'light-start',ops);
  return {kept,calls,alerts:alerts.map(a=>a.id),breakdownEnabled:!document.querySelector('[data-five-command="five.fixture.design.breakdown"]').disabled,lightDialog:!!document.querySelector('[data-five-fixture-light-dialog]')};
 });
 assert.deepEqual(result.kept,{input:true,canvas:true,focus:true,selection:[1,5],scroll:[230,270,310],events:1,conditional:true});assert.deepEqual(result.calls,[]);assert.equal(result.lightDialog,false);assert.equal(result.breakdownEnabled,true);assert.deepEqual(errors,[]);
 writeFileSync(resolve(output,'browser-removal.json'),JSON.stringify({passed:true,...result},null,2));console.log(JSON.stringify({passed:true,...result}));
}finally{await browser.close();}
