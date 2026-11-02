import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {browserAssetPath,browserReportDirectory,serveBrowserAsset} from './browserPackageRuntime.mjs';

const out=browserReportDirectory('head-preview-cache-20261101'),report={passed:false,standaloneCef:false,realWebGL:true,domain:'Controlled head snapshots',errors:[]};
const browser=await chromium.launch({channel:'msedge',headless:true}),page=await browser.newPage({viewport:{width:1100,height:850}});
try{
 page.on('pageerror',error=>report.errors.push(error.message));
 await page.route('http://localhost:17773/**',route=>{
  const path=new URL(route.request().url()).pathname;
  if(path==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><meta charset="utf-8"><main><aside class="cam-context-pane"><input id="name" value="AB-instance"><div class="spacer"></div></aside><aside class="cam-info-pane"><div class="nested"><div class="spacer"></div></div><div id="preview"></div><div class="spacer"></div></aside></main>'});
  if(path.endsWith('.css'))return route.fulfill({contentType:'text/css',body:readFileSync(browserAssetPath(path.slice(5)))});
  return serveBrowserAsset(route);
 });
 await page.goto('http://localhost:17773/');await page.addStyleTag({url:'http://localhost:17773/src/apps/five-axis-cam/webpage/workspace.css'});
 await page.addStyleTag({content:'body{margin:0}main{display:grid;grid-template-columns:250px 500px;height:800px}.cam-context-pane,.cam-info-pane{overflow:auto;height:800px}.spacer{height:1400px}.nested{height:50px;overflow:auto}'});
 await page.evaluate(async()=>{
  const P=await import('/src/apps/five-axis-cam/webpage/cuttingHeadPreview.mjs'),D=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs'),{encodeNestingGeometry}=await import('/src/apps/tube-designer/webpage/nestingPreview.mjs');
  const id=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],a=[...id];a[11]=-20;
  const head={kind:'Builtin',calibrated:true,builtinDefinitionId:'AB',instance:{id:'instance',name:'AB-instance',configurationId:'profile',revision:'1'},assembly:{kind:'AB',tool:2,tcp:[0,0,-40],beam:[0,0,-1],nodes:[
   {name:'mount',type:'fixed',parent:-1,origin:id,axis:[0,0,1],position:0,previewRef:{url:'resource://mount',version:'1'}},
   {name:'B',joint:'B',type:'revolute',parent:0,origin:id,axis:[0,1,0],position:0,lower:-1.5,upper:1.5,previewRef:{url:'resource://B',version:'1'}},
   {name:'A',joint:'A',type:'revolute',parent:1,origin:a,axis:[1,0,0],position:0,lower:-1.5,upper:1.5,previewRef:{url:'resource://A',version:'1'}}
  ]}};
  const s=window.cacheTest={P,head,calls:0},v=s.view={fiveAxisArea:'toolpath',fiveSimulation:{headOpen:true},scene:{domain:{activeSetupId:'setup',setups:[{id:'setup',headModel:head}]}}};
  s.context={mount:document.querySelector('main'),isCurrentProject:()=>true,sceneProxy:{resources:{get:async()=>{s.calls++;return new Response(encodeNestingGeometry({positions:[-8,-5,-20,8,-5,-20,0,8,20],indices:[0,1,2]}));}}}};
  s.render=()=>{D.patchWorkspacePanels(s.context.mount,[['#preview',P.renderCuttingHeadPreview(v)]]);P.syncCuttingHeadPreview(s.context,v);};s.render();
 });
 const ready=()=>page.waitForFunction(()=>cacheTest.view.fiveHeadPreview?.phase==='ready');await ready();
 await page.locator('[data-five-head-joint="2"]').evaluate(input=>{input.value='30';input.dispatchEvent(new Event('input',{bubbles:true}));});
 const box=await page.locator('[data-five-head-preview]').boundingBox();await page.mouse.move(box.x+150,box.y+100);await page.mouse.down({button:'right'});await page.mouse.move(box.x+220,box.y+140,{steps:4});await page.mouse.up({button:'right'});
 await page.evaluate(()=>{const s=cacheTest,p=s.view.fiveHeadPreview;s.saved={model:p.model,renderer:p.renderer,canvas:p.canvas,camera:p.camera.position.toArray(),rotation:p.camera.quaternion.toArray(),positions:[...p.previewPositions],calls:s.calls,input:document.querySelector('#name')};s.saved.input.focus({preventScroll:true});s.saved.input.setSelectionRange(1,5);document.querySelector('.cam-context-pane').scrollTop=45;document.querySelector('.cam-info-pane').scrollTop=30;document.querySelector('.nested').scrollTop=25;s.head.instance={...s.head.instance,name:'Renamed head',configurationId:'new-profile',revision:'25',source:{name:'Another source',revision:'27'}};s.render();});
 const retained=await page.evaluate(()=>{const s=cacheTest,p=s.view.fiveHeadPreview;return {model:s.saved.model===p.model,renderer:s.saved.renderer===p.renderer,canvas:s.saved.canvas===p.canvas,camera:p.camera.position.toArray(),rotation:p.camera.quaternion.toArray(),positions:p.previewPositions,calls:s.calls,input:s.saved.input===document.querySelector('#name'),focus:document.activeElement===s.saved.input,selection:[s.saved.input.selectionStart,s.saved.input.selectionEnd],scroll:[document.querySelector('.cam-context-pane').scrollTop,document.querySelector('.cam-info-pane').scrollTop,document.querySelector('.nested').scrollTop],expected:{camera:s.saved.camera,rotation:s.saved.rotation,positions:s.saved.positions,calls:s.saved.calls}};});
 assert.ok(retained.model&&retained.renderer&&retained.canvas&&retained.input&&retained.focus);for(const key of ['camera','rotation','positions','calls'])assert.deepEqual(retained[key],retained.expected[key]);assert.deepEqual(retained.selection,[1,5]);assert.deepEqual(retained.scroll,[45,30,25]);report.metadataRetained=retained;
 report.physicalChanges=[];
 for(const field of ['geometry','axis','origin','tcp']){
  await page.evaluate(field=>{const s=cacheTest;s.previous=s.view.fiveHeadPreview.model;s.previousCalls=s.calls;if(field==='geometry')s.head.assembly.nodes[1].previewRef.version='2';if(field==='axis')s.head.assembly.nodes[1].axis=[0,0,1];if(field==='origin')s.head.assembly.nodes[1].origin=[1,0,0,5,0,1,0,0,0,0,1,0,0,0,0,1];if(field==='tcp')s.head.assembly.tcp=[0,0,-45];s.render();},field);await ready();
  assert.equal(await page.evaluate(()=>cacheTest.previous!==cacheTest.view.fiveHeadPreview.model&&cacheTest.calls===cacheTest.previousCalls+3),true,field+' must refresh the physical display');report.physicalChanges.push(field);
 }
 await page.screenshot({path:resolve(out,'preview-cache-preserved.png')});assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.failure=error.stack;await page.screenshot({path:resolve(out,'failure.png')}).catch(()=>{});throw error;}
finally{writeFileSync(resolve(out,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();}
