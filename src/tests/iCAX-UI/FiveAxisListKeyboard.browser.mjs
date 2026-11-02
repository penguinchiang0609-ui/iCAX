import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {chromium} from 'playwright';
import {browserAssetPath,browserReportDirectory} from './browserPackageRuntime.mjs';

const output=browserReportDirectory('list-keyboard-20261031'),report={passed:false,standaloneCef:false,cases:[],errors:[]};
const browser=await chromium.launch({headless:true,channel:'msedge'}),page=await browser.newPage({viewport:{width:1450,height:900}});
try{
 page.setDefaultTimeout(10000);page.on('pageerror',e=>report.errors.push(e.message));
 await page.route('http://localhost:17759/**',route=>{
  const path=new URL(route.request().url()).pathname;
  if(path==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><meta charset="utf-8"><body></body>'});
  if(!path.startsWith('/src/'))return route.abort();
  let body=readFileSync(browserAssetPath(path.slice(5)),'utf8');
  if(path.endsWith('/webpage/entry.mjs'))body+='\nexport {attach as attachForTest};';
  return route.fulfill({contentType:path.endsWith('.css')?'text/css':'text/javascript',body});
 });
 await page.goto('http://localhost:17759/');await page.addStyleTag({url:'http://localhost:17759/src/apps/five-axis-cam/webpage/workspace.css'});
 await page.evaluate(async()=>{
  const base='/src/apps/five-axis-cam/webpage/',T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
  const [{attachForTest},{createThreeViewport},{getProjectView},{patchWorkspacePanels},{faceSelectionList},{renderCutTree},{renderRapidTree,openRapid},{extractionDraft},{designDraft,renderFixtureDesign}]=await Promise.all([
   import(base+'entry.mjs'),import('/src/iCAX-UI/SDK/Viewport/threeViewport.mjs'),import('/src/apps/_shared/workbench/state/projectViewStore.mjs'),import(base+'workspaceDom.mjs'),import(base+'faceSelectionPresentation.mjs'),import(base+'cutTree.mjs'),import(base+'rapidPlanning.mjs'),import(base+'extractionActions.mjs'),import(base+'fixtureDesign.mjs')]);
  document.body.innerHTML='<style>body{margin:0}main{display:grid;grid-template-columns:280px 850px 320px;height:900px}.cam-context-pane,.cam-info-pane{height:850px;overflow:auto;display:block!important}#viewer{height:800px}.tail{height:1400px}.five-path-list{max-height:170px;overflow:auto}.five-support-face-list{max-height:150px}.five-position-body{max-height:370px;overflow:auto}</style><main id="mount"><aside class="cam-context-pane"><div id="left"></div><input aria-label="左侧草稿" value="draft"><div class="tail"></div></aside><div id="viewer"></div><aside class="cam-info-pane"><div class="five-position-body"><div id="right"></div><input aria-label="右侧草稿" value="abcdef"><div class="tail"></div></div><div class="tail"></div></aside></main>';
  const v=getProjectView('keyboard-test'),mount=document.querySelector('main'),matrix=new T.Matrix4().identity().toArray();
  const cuts=Array.from({length:22},(_,i)=>({id:'cut-'+i,entityId:'cut-'+i,number:i+1,name:'轮廓',placementId:'p',setupId:'s',enabled:true,locked:i===1,startPoint:[i*25,0,0],previewPoints:[[i*25,0,0],[i*25+10,0,0],[i*25+10,10,5]]}));
  const transitions=cuts.slice(1).map((c,i)=>({from:cuts[i].id,to:c.id,frame:i%2?'Setup':'Workpiece',points:[[i*25+10,10,5],[i*25+10,10,25],[(i+1)*25,0,25],[(i+1)*25,0,0]],endpointStatus:'Current',geometryStatus:'Generated',validation:'Unchecked',policy:{strategy:'Quick',marginMm:2,liftHeightMm:10}}));
  const domain={expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'s',placements:[{id:'p',setupId:'s',workpieceId:'w',transform:matrix}],workpieces:[{id:'w',name:'stock'}],setups:[{id:'s',enabled:true,cutGroups:[],rapidSettings:{strategy:'Quick'},generatedGeometry:{status:'Current',operations:cuts.map(c=>({intentId:c.id,placementId:'p',start:c.startPoint,order:c.number,parts:[{role:'Cut',points:c.previewPoints}]})),transitions}}],fixtureUnits:[],fixtures:[],intentToolpaths:cuts};
  Object.assign(v,{fiveAxisArea:'workpiece',activeAreaId:'workpiece',fivePreparationKind:'unit',scene:{domain,activePlacementId:'p',model:{isLoaded:true,entityId:'w'},intentToolpaths:cuts,machines:[]}});
  const faces=Array.from({length:36},(_,i)=>({faceId:String(i),first:i*2,count:2})),geometry=new T.BufferGeometry(),points=[];
  for(let i=0;i<faces.length;i++){const x=i*15;points.push(x,0,0,x+10,0,0,x+10,10,0,x,0,0,x+10,10,0,x,10,0);}geometry.setAttribute('position',new T.Float32BufferAttribute(points,3));
  const vp=v.viewport=createThreeViewport({showGrid:false,continuousRender:true,constrainOrbit:false});vp.mount(document.querySelector('#viewer'));vp.setCameraState({target:{x:0,y:0,z:0},theta:.6,phi:1.2,radius:1500});
  const mesh=new T.Mesh(geometry,new T.MeshBasicMaterial({color:0x69abc1,side:T.DoubleSide}));vp.content.add(mesh);vp.sceneObjects.set('p',mesh);
  const s=window.fixture={view:v,faces,mode:'support',current:true,calls:[],held:false,canvas:vp.renderer.domElement};
  const state=s.extraction=extractionDraft(v).extraction;state.faces=faces.map(f=>({kind:'Face',localId:f.faceId}));state.faceRanges=Object.fromEntries(faces.map(f=>[f.faceId,[f]]));state.selectedFace='0';
  Object.assign(designDraft(v),{supportFaces:faces.map(f=>f.faceId),dirty:true,revision:7});v.fixtureDesignFaces=faces;v.fixtureDesignPlacement='p';v.fixtureDesignSelectedFace='0';
  const context=s.context={mount,project:{projectId:'keyboard-test'},isCurrentProject:()=>s.current,sceneProxy:{async invoke(method,payload){
   s.calls.push({method,payload});if(method==='CamPlacement.ReadReferenceFaces'){s.held=true;await new Promise(r=>s.release=r);s.held=false;return {faces};}
   if(method==='CamCut.Read')return {items:payload.intentIds.map(id=>({id,retainedSide:'Exterior'}))};
   if(method==='CamProcess.Read')return {items:payload.intentIds.map(intentId=>({intentId,library:{name:'工艺',parameters:{}},posePolicy:{},assignment:{kerf:{widthMm:0},overrides:[],overcutLengthMm:0,gapLengthMm:0,leadIn:{kind:'None',lengthMm:0,angleRad:0},leadOut:{kind:'None',lengthMm:0,angleRad:0}}}))};
   if(method==='CamProcess.BeginPreview')return {status:'Completed',result:{items:[]}};
   if(method==='CamCut.ReadToolAxis')return {mode:'SurfaceNormal',baseDirection:[0,0,1],setupBaseDirection:[0,0,1],surfaceNormal:[0,0,1],setupSurfaceNormal:[0,0,1]};
   throw Error('Unexpected operation '+method);
  }}};
  s.render=()=>{
   const face=s.mode==='support'||s.mode==='extraction',support=s.mode==='support',selected=support?v.fixtureDesignSelectedFace:state.selectedFace;
   const rows=face?faceSelectionList({ids:faces.map(f=>f.faceId),selectedId:selected,key:'faces',label:'面列表',selectCommand:id=>support?'five.fixture.design.select-face:'+id:'five.extraction.select-face.'+id,focusCommand:id=>support?'five.fixture.design.focus-face:'+id:'five.extraction.focus-face.'+id,removeCommand:id=>'unused-remove:'+id,empty:'空'}):s.mode==='cut'?renderCutTree(v,cuts,c=>'轮廓 #'+c.number):`<div data-five-key="path-list-rapid" class="five-path-list">${renderRapidTree(v,{heading:false})}</div>`;
   patchWorkspacePanels(mount,[['#left',face?'':rows],['#right',support?renderFixtureDesign(v):face?rows:'']]);
  };
  s.ops={renderProject:s.render,appendProjectLog(){}};
  s.show=mode=>{s.mode=mode;v.fiveAxisArea=mode==='support'?'workpiece':'toolpath';v.activeAreaId=v.fiveAxisArea;context.activeRibbonTabId=v.fiveAxisArea;v.fiveCamPage=mode==='rapid'?'rapid':'cutting';v.fiveFixtureDesignOpen=mode==='support';v.fiveAxisExtractionOpen=mode==='extraction';v.fiveAxisUI.tools.workpiece='fixture';v.fiveAxisUI.tools.toolpath=mode==='extraction'?'extract':mode==='rapid'?'rapid':'select';if(mode==='rapid')openRapid(v);s.render();attachForTest(context,v,mount,s.ops);};
  s.show('support');
 });
 const rows=()=>page.locator('[data-five-face-focus],.five-tree-row[data-five-node],.five-tree-row[data-five-rapid-id]');
 const camera=()=>page.evaluate(()=>fixture.view.viewport.getCameraState());
 const state=()=>page.evaluate(()=>{const s=fixture,v=s.view;return {selected:s.mode==='support'?v.fixtureDesignSelectedFace:s.mode==='extraction'?s.extraction.selectedFace:s.mode==='cut'?v.fiveAxisUI.selectedIntentId:v.fiveRapid.id,focus:document.activeElement?.dataset.fiveCommand||document.activeElement?.dataset.fiveIntent||document.activeElement?.dataset.fiveRapidId,camera:v.viewport.getCameraState(),revision:s.mode==='support'?v.fixtureDesignDraft.revision:s.extraction.revision};});
 for(const mode of ['support','extraction','cut','rapid']){
  await page.evaluate(mode=>{fixture.show(mode);const vp=fixture.view.viewport;vp.setCameraState({...vp.getCameraState(),target:{x:0,y:0,z:0},radius:1500});},mode);await rows().first().evaluate(e=>e.focus({preventScroll:true}));const before=await state();
  await page.keyboard.press('ArrowDown');await page.waitForFunction(()=>!fixture.view.pending);const next=await state();
  assert.notEqual(next.selected,before.selected);assert.deepEqual(next.camera,before.camera);assert.equal(next.revision,before.revision);
  const second=mode==='cut'?rows().nth(2):rows().nth(1);assert.equal(await second.evaluate(e=>document.activeElement===e),true);
  await page.keyboard.press('Enter');const focused=await camera();assert.notDeepEqual(focused,before.camera);for(const k of ['theta','phi','projectionMode'])assert.equal(focused[k],before.camera[k]);
  await page.keyboard.press('ArrowUp');await page.waitForFunction(()=>!fixture.view.pending);assert.equal(await rows().first().evaluate(e=>e===document.activeElement),true);
  await page.keyboard.press('ArrowUp');assert.equal(await rows().first().evaluate(e=>e===document.activeElement),true);
  await rows().last().evaluate(e=>e.focus({preventScroll:true}));await page.keyboard.press('ArrowDown');assert.equal(await rows().last().evaluate(e=>e===document.activeElement),true);
  await page.keyboard.press('ArrowUp');await page.waitForFunction(()=>!fixture.view.pending);
  assert.equal(await rows().nth(await rows().count()-2).evaluate(e=>{const r=e.getBoundingClientRect();let p=e.parentElement;while(p&&!(p.scrollHeight>p.clientHeight&&/(auto|scroll)/.test(getComputedStyle(p).overflowY)))p=p.parentElement;if(!p)return true;const b=p.getBoundingClientRect();return r.top>=b.top-1&&r.bottom<=b.bottom+1;}),true);
  const guards=await state();await page.evaluate(()=>{for(const extra of [{ctrlKey:true},{metaKey:true},{altKey:true},{shiftKey:true},{isComposing:true}])document.activeElement.dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowDown',bubbles:true,cancelable:true,...extra}));});assert.deepEqual(await state(),guards);
  await page.evaluate(()=>fixture.current=false);await page.keyboard.press('ArrowDown');assert.deepEqual(await state(),guards);await page.evaluate(()=>fixture.current=true);
  report.cases.push(mode+': real Up/Down/Enter selects, skips disabled rows, clamps endpoints, reveals the row and preserves camera direction, draft revision and project ownership');
 }
 // An unowned arrow is left to the browser; let its native scroll finish.
 await page.waitForTimeout(250);
 // Delayed support topology must not restore an earlier row over newer editing.
 await page.evaluate(()=>{fixture.show('support');fixture.view.fixtureDesignFaces=null;fixture.view.fixtureDesignPlacement=null;});await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));await rows().first().evaluate(e=>e.focus({preventScroll:true}));await page.keyboard.press('ArrowDown');await page.waitForFunction(()=>fixture.held);
 const retained=await page.evaluate(()=>{const s=fixture,input=s.input=document.querySelector('[aria-label="右侧草稿"]');s.events=0;input.addEventListener('probe',()=>s.events++);input.focus({preventScroll:true});input.setSelectionRange(1,3);return ['.cam-context-pane','.cam-info-pane','.five-position-body','.five-support-face-list'].map((selector,i)=>{const e=document.querySelector(selector);e.scrollTop=50+i*10;return e.scrollTop;});});
 await page.evaluate(()=>fixture.release());await page.waitForFunction(()=>!fixture.held);await page.evaluate(()=>new Promise(r=>requestAnimationFrame(r)));
 const after=await page.evaluate(()=>{const s=fixture;s.input.dispatchEvent(new Event('probe'));return {input:s.input===document.querySelector('[aria-label="右侧草稿"]'),focus:document.activeElement===s.input,selection:[s.input.selectionStart,s.input.selectionEnd],events:s.events,canvas:s.canvas===s.view.viewport.renderer.domElement,scroll:['.cam-context-pane','.cam-info-pane','.five-position-body','.five-support-face-list'].map(selector=>document.querySelector(selector).scrollTop)};});
 assert.deepEqual(after,{input:true,focus:true,selection:[1,3],events:1,canvas:true,scroll:retained});
 const typing=await state();await page.keyboard.press('ArrowDown');await page.keyboard.press('Enter');assert.deepEqual((await state()).camera,typing.camera);assert.equal((await state()).selected,typing.selected);
 report.cases.push('Delayed response retains the latest input node, caret, listener, canvas and left/right/nested scrolls; typing and Enter in an input do not navigate the list');
 report.methods=await page.evaluate(()=>[...new Set(fixture.calls.map(c=>c.method))]);assert.ok(report.methods.every(method=>['CamCut.Read','CamProcess.Read','CamProcess.BeginPreview','CamCut.ReadToolAxis','CamPlacement.ReadReferenceFaces'].includes(method)),JSON.stringify(report.methods));
 assert.equal(await page.evaluate(()=>fixture.view.error||''),'');
 assert.deepEqual(report.errors,[]);report.passed=true;await page.screenshot({path:resolve(output,'keyboard-faces.png')});
}catch(error){report.failure=error.stack;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));await browser.close();}
