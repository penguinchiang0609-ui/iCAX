import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {browserReportDirectory,serveBrowserAsset} from './browserPackageRuntime.mjs';

const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const output=browserReportDirectory('rapid-failed-draft-20261029');
const browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'msedge',args:['--enable-webgl','--use-angle=swiftshader']});
const page=await browser.newPage({viewport:{width:1440,height:900}}),errors=[];
page.on('pageerror',error=>errors.push(error.message));
const report={passed:false,browser:'Playwright Chromium',nativeTransport:'controlled metadata fixture with actual ICPS FlatBuffer resource hydration',standaloneCefEndToEnd:false,cases:[]};
const html=`<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>失败空移候选路径编辑回归</title>
<style>
*{box-sizing:border-box}body{margin:0;font:14px "Microsoft YaHei",sans-serif;background:#17222c;color:#e8eef4}header.test-title{height:58px;padding:16px 24px;background:#243442;font-size:19px}#mount{display:grid;grid-template-columns:240px 760px 400px;height:812px;margin:10px 20px;gap:0}.cam-context-pane,.cam-info-pane{background:#263846;overflow:auto;padding:14px}.cam-info-pane{border-left:1px solid #435767}.viewport{background:#17222c;position:relative;height:720px}canvas{display:block;width:760px;height:720px}.test-caption{padding:14px;color:#bed0dc}.five-tree-row{display:flex;gap:10px;width:100%;text-align:left;align-items:center;padding:10px;margin:6px 0;background:#1c2c38;border:1px solid #506474;color:inherit}.five-tree-row.selected{border:2px solid #e83eff}.five-tree-row small,.five-tree-row strong{display:block}.five-tree-row small{font-size:12px;color:#c9b39f;padding-top:5px}button,input,select{font:inherit}button{border:1px solid #607686;background:#314c60;color:inherit;padding:6px 10px;cursor:pointer}button:disabled{opacity:.45;cursor:default}input,select{min-width:0;background:#172c3c;color:#e8eef4;border:1px solid #5b7384;padding:6px;width:100%}.five-position-title{padding:8px 0 14px}.five-rapid-field{display:grid;grid-template-columns:1fr 1fr 35px;align-items:center;margin:8px 0;gap:4px}.five-rapid-body{max-height:650px;overflow:auto;padding-right:6px}.five-rapid-policy{padding:5px;margin:8px 0}.five-position-actions,.five-rapid-point-actions,.five-pose-action-grid{display:flex;gap:5px;flex-wrap:wrap;margin:10px 0}.five-rapid-point-coordinates{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px}.five-rapid-point-editor{display:block;padding:8px;border:1px solid #526c7c;margin-top:12px}.five-rapid-point-editor small,.five-rapid-body>small{display:block;margin:8px 0;line-height:1.6}.five-rapid-status{padding:10px 0;font-weight:bold}.five-position-actions button{flex:1}.five-position-error{color:#f49b93;padding:10px 0}.five-rapid-point-actions svg,.five-pose-action-grid svg{width:18px;height:18px;vertical-align:middle}label.test-left-field{display:block;margin:12px 0}.test-spacer{height:900px}.five-fixture-heading{display:flex;justify-content:space-between}fieldset small{display:block;line-height:1.6}
</style><header class="test-title">失败空移仍保留路径，可选中并编辑</header><div id="mount"><aside class="cam-context-pane"></aside><section><div class="viewport"><canvas width="760" height="720"></canvas></div><div class="test-caption">橙色：未验证候选　红色：碰撞候选　中间点可编辑，起终点锁定</div></section><aside class="cam-info-pane"></aside></div></html>`;

try{
 await page.route('http://rapid-failed.test/**',route=>new URL(route.request().url()).pathname==='/'?route.fulfill({contentType:'text/html',body:html}):serveBrowserAsset(route));
 await page.goto('http://rapid-failed.test/');
 await page.evaluate(async()=>{
  const T=await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
  const {selectTool}=await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs');
  const planning=await import('/src/apps/five-axis-cam/webpage/rapidPlanning.mjs');
  const editing=await import('/src/apps/five-axis-cam/webpage/rapidPathEditing.mjs');
  const generation=await import('/src/apps/five-axis-cam/webpage/toolpathGeneration.mjs');
  const {patchWorkspacePanels}=await import('/src/apps/five-axis-cam/webpage/workspaceDom.mjs');
  const {sceneColors}=await import('/src/apps/five-axis-cam/webpage/scenePalette.mjs');
  const {hasRapidGeometry}=await import('/src/apps/five-axis-cam/webpage/rapidGeometry.mjs');
  const {hydratePathSamples}=await import('/src/apps/five-axis-cam/webpage/pathSampleResource.mjs');
  const {createPathSamplesResource}=await import('/src/tests/iCAX-UI/fixtures/pathSamplesResource.mjs');
  const transform=new T.Matrix4().toArray(),placement={id:'p',transform},ids=['a','b','c','d'];
  const paths=[{from:'a',to:'b',y:25,geometryStatus:'Failed',validation:'Unchecked',reason:'TRAVEL_CLEARANCE_BUDGET'},
   {from:'b',to:'c',y:-25,geometryStatus:'Failed',validation:'Collision',reason:'RAPID_MANUAL_PATH_BLOCKED',failedSegment:'1'},
   {from:'c',to:'d',y:0,geometryStatus:'Generated',validation:'Clear'}];
  let transitions=paths.map(({y,...t})=>({...t,points:[[-45,y,0],[0,y+12,0],[45,y,0]],directions:[[0,0,1],[0,0,1],[0,0,1]],fromPlacementId:'p',frame:'Setup',referencePoint:'ToolTip',planningMethod:'Direct',lengthMm:94,editingOnly:t.geometryStatus==='Failed',policy:{strategy:'Quick',marginMm:.2,liftHeightMm:100}}));
  const operations=ids.map((intentId,i)=>({intentId,placementId:'p',order:String(i+1),start:[-50,-45+i*30,0],parts:[{role:'Cut',points:[[-52,-45+i*30,0],[-46,-45+i*30,0]],directions:[[0,0,1],[0,0,1]]}]}));
  const domain={expected:{epoch:'failed-draft',mutationSequence:'1'},activeSetupId:'s',placements:[placement],workpieces:[],fixtures:[],intentToolpaths:[],setups:[{id:'s',generatedGeometry:{status:'Current',revision:{id:'g'},operations,transitions}}]};
  const bytes=createPathSamplesResource([...operations.map(op=>({id:op.intentId,parts:op.parts})),...transitions.map(t=>({id:`rapid:${t.from}/${t.to}`,parts:[{role:'Rapid',points:t.points,directions:t.directions}]}))]),geometry=domain.setups[0].generatedGeometry;
  geometry.sampleGeometryRef={url:'resource://rapid-failed/icps',version:'7'};
  geometry.operations=operations.map(op=>({...op,parts:op.parts.map(({points,directions,...part})=>({...part,pointCount:String(points.length)}))}));
  geometry.transitions=transitions.map(({points,directions,...t})=>({...t,pointCount:String(points.length)}));
  if(geometry.transitions.some(t=>t.points!==undefined||t.directions!==undefined))throw Error('Fixture must begin with metadata-only transitions');
  let resourceRequests=0;
  await hydratePathSamples(domain,{resources:{async get(url,options){resourceRequests++;if(url!==geometry.sampleGeometryRef.url||options.headers['ICAX-Resource-Version']!=='7')throw Error('Incorrect ICPS resource request');return new Response(bytes,{headers:{'ICAX-Resource-Version':'7','Content-Type':'application/vnd.icax.flatbuffer'}});}}});
  transitions=geometry.transitions;
  const view={scene:{domain,intentToolpaths:ids.map(id=>({id,setupId:'s',placementId:'p'}))},fiveAxisArea:'toolpath',fiveCamPage:'rapid'};
  const mount=document.querySelector('#mount'),canvas=mount.querySelector('canvas'),renderer=new T.WebGLRenderer({canvas,antialias:true,preserveDrawingBuffer:true});
  renderer.setSize(760,720,false);renderer.setClearColor(0x17222c);
  const camera=new T.OrthographicCamera(-65,65,62,-62,.1,1000);camera.position.set(0,0,200);camera.lookAt(0,0,0);camera.updateMatrixWorld(true);
  const viewport={id:'failed-draft',renderer,camera,scene:new T.Scene(),specificationLabels:[],setSpecificationAnnotations(annotations){this.specificationLabels=annotations.map(()=>({element:document.createElement('span')}));},setCameraNavigationEnabled(){},setTransformManipulator(options){this.transformManipulator={options};},clearTransformManipulator(){this.transformManipulator=null;}};
  view.viewport=viewport;
  const fixture=window.fixture={T,view,planning,editing,generation,sceneColors,hasRapidGeometry,canvas,mount,resourceRequests,renderCount:0,canvasEvents:0,saveRequests:0,nativeCalls:[],checks:0};
  const context={mount,isCurrentProject:()=>true,sceneProxy:{async invoke(method,payload){fixture.nativeCalls.push({method,payload});throw Error('Unexpected native call: '+method);}}};
  const render=()=>{fixture.renderCount++;patchWorkspacePanels(mount,[['.cam-context-pane',`<label class="test-left-field" data-five-key="test-left-field">查找连接<input id="test-left-input" value="123456"></label>${planning.renderRapidTree(view)}<div class="test-spacer" data-five-key="test-left-spacer"></div>`],['.cam-info-pane',planning.renderRapid(view,true)+'<div class="test-spacer" data-five-key="test-right-spacer"></div>']]);generation.updateGeneratedPreview(view);renderer.render(viewport.scene,camera);};
  const ops={renderProject:render,appendProjectLog(){}};Object.assign(fixture,{context,ops,render,transitions,geometry:domain.setups[0].generatedGeometry});
  fixture.refresh=async()=>{fixture.refreshHeld=true;await new Promise(resolve=>fixture.releaseRefresh=resolve);fixture.refreshHeld=false;render();};
  fixture.bindSave=()=>{view.fiveRapid.savePath=()=>fixture.saveRequests++;view.fiveRapid.saveTopology=()=>fixture.saveRequests++;};
  selectTool(view,'rapid');planning.openRapid(view);fixture.bindSave();render();
  mount.addEventListener('click',async event=>{const row=event.target.closest('[data-five-rapid-id]'),button=event.target.closest('[data-five-command]');if(row){planning.rapidPick(view,row.dataset.fiveRapidId);fixture.bindSave();render();}else if(button?.dataset.fiveCommand.startsWith('five.rapid.path-')){fixture.busy=true;try{await editing.rapidPathCommand(context,view,button.dataset.fiveCommand.slice('five.rapid.'.length),ops,()=>fixture.checks++);}finally{fixture.busy=false;}}});
  mount.addEventListener('input',event=>{if(event.target.matches('input[data-five-rapid-field]')){editing.rapidPathFieldChanged(view,event.target.dataset.fiveRapidField,event.target.value);render();}});
  mount.addEventListener('change',event=>{if(event.target.matches('select[data-five-rapid-field]')){planning.rapidFieldChanged(view,event.target.dataset.fiveRapidField,event.target.value);render();}});
  canvas.addEventListener('click',event=>{fixture.canvasEvents++;planning.handleRapidPick(context,view,null,null,event,[],ops);});
  fixture.lines=()=>{const lines=[];view.fiveGeneratedPreview?.group.traverse(object=>{if(object.userData.rapidTransitionId)lines.push({id:object.userData.rapidTransitionId,color:object.material.color.getHex(),points:Array.from(object.geometry.attributes.position.array),stroke:object.children.filter(x=>x.userData.pathSelectionStroke).map(x=>({color:x.material.uniforms.color.value.getHex(),dashed:x.material.uniforms.dashSize.value>0}))});});return lines;};
  fixture.pixel=(id,segment=0)=>{const t=transitions.find(t=>`${t.from}/${t.to}`===id),p=new T.Vector3(...t.points[segment]).lerp(new T.Vector3(...t.points[segment+1]),.5).project(camera),r=canvas.getBoundingClientRect();return {x:r.x+(p.x+1)*r.width/2,y:r.y+(1-p.y)*r.height/2};};
 });

 const frame=()=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
 const state=()=>page.evaluate(()=>({lines:fixture.lines(),status:fixture.transitions.map(t=>({id:`${t.from}/${t.to}`,geometryStatus:t.geometryStatus,validation:t.validation})),selected:fixture.view.fiveRapid.id,nativeCalls:fixture.nativeCalls}));
 const initial=await state(),warning=await page.evaluate(()=>fixture.sceneColors.rapidUnverified),errorColor=await page.evaluate(()=>fixture.sceneColors.error);
 assert.equal(await page.evaluate(()=>fixture.resourceRequests),1,'the first rendered scene must hydrate Failed metadata through an actual ICPS resource');
 assert.equal(initial.lines.length,3);assert.equal(initial.lines.find(t=>t.id==='a/b').color,warning);assert.equal(initial.lines.find(t=>t.id==='b/c').color,errorColor);
 assert.equal(initial.lines.find(t=>t.id==='a/b').stroke.length,1);assert.equal(initial.lines.find(t=>t.id==='a/b').stroke[0].color,warning);assert.equal(initial.lines.find(t=>t.id==='a/b').stroke[0].dashed,true);
 assert.deepEqual(initial.status.slice(0,2).map(t=>[t.geometryStatus,t.validation]),[['Failed','Unchecked'],['Failed','Collision']]);
 await page.screenshot({path:resolve(output,'failed-candidates-visible.png')});
 report.cases.push('Metadata-only failed candidates hydrate actual ICPS resource bytes before the first scene render; Failed unchecked and collision candidates draw retained ToolTip geometry and selected warning coloring with a thick dashed stroke');

 for(const id of ['b/c','c/d','a/b']){
  const pixel=await page.evaluate(id=>fixture.pixel(id),id);
  const hit=await page.evaluate(p=>fixture.planning.rapidTransitionAtPointer(fixture.view,{clientX:p.x,clientY:p.y})?.id,pixel);assert.equal(hit,id);
  await page.mouse.click(pixel.x,pixel.y);await frame();assert.equal((await state()).selected,id);
  const line=(await state()).lines.find(t=>t.id===id);assert.equal(line.stroke.length,1);assert.equal(line.stroke[0].color,id==='c/d'?0xe83eff:id==='a/b'?warning:errorColor);
 }
 report.cases.push('Viewport clicks select failed unchecked and collision paths; checked successful paths retain purple selection');

 await page.locator('[data-five-command="five.rapid.path-edit"]').click();await page.waitForFunction(()=>!fixture.busy&&fixture.view.fiveRapid.pathEdit);
 let edited=await page.evaluate(()=>({edit:structuredClone(fixture.view.fiveRapid.pathEdit),draft:fixture.editing.manualRapidDraft(fixture.view),nativeCalls:fixture.nativeCalls}));
 assert.equal(edited.edit.pathKind,'Polyline');assert.equal(edited.edit.points.length,3);assert.equal(edited.nativeCalls.length,0);
 assert.equal(edited.draft.geometryStatus,'Failed');assert.equal(edited.draft.validation,'Unchecked');
 assert.equal(edited.draft.editingOnly,true);assert.equal(edited.draft.productionQualified,false);assert.equal(edited.draft.machineValidation,'Unchecked');
 assert.equal(await page.locator('[data-five-command="five.rapid.apply"]').isDisabled(),true);
 const endpoints=[edited.edit.points[0],edited.edit.points.at(-1)],directions=[edited.edit.directions[0],edited.edit.directions.at(-1)];
 await page.locator('[data-five-rapid-field="pathX"]').fill('11.25');await frame();
 edited=await page.evaluate(()=>({edit:structuredClone(fixture.view.fiveRapid.pathEdit),draft:fixture.editing.manualRapidDraft(fixture.view),rendered:fixture.planning.rapidPreview(fixture.view,fixture.geometry).transitions[0]}));
 assert.equal(edited.edit.points[1][0],11.25);assert.deepEqual([edited.edit.points[0],edited.edit.points.at(-1)],endpoints);assert.deepEqual([edited.edit.directions[0],edited.edit.directions.at(-1)],directions);
 assert.equal(edited.draft.geometryStatus,'Draft');assert.equal(edited.draft.validation,'Unchecked');assert.equal(edited.rendered.geometryStatus,'Draft');assert.equal(edited.rendered.validation,'Unchecked');
 assert.equal(edited.rendered.editingOnly,true);assert.equal(edited.rendered.productionQualified,false);assert.equal(edited.rendered.machineValidation,'Unchecked');
 assert.equal((await state()).lines.find(t=>t.id==='a/b').color,warning);
 await page.locator('[data-five-rapid-field="pathPoint"]').selectOption('0');await frame();assert.equal(await page.locator('[data-five-rapid-field="pathX"]').getAttribute('readonly'),'');
 await page.evaluate(()=>{fixture.editing.rapidPathFieldChanged(fixture.view,'pathX','999');fixture.editing.rapidPathFieldChanged(fixture.view,'angle-0','45');fixture.render();});
 assert.deepEqual(await page.evaluate(()=>[fixture.view.fiveRapid.pathEdit.points[0],fixture.view.fiveRapid.pathEdit.directions[0]]),[endpoints[0],directions[0]]);
 await page.locator('[data-five-rapid-field="pathPoint"]').selectOption('1');await frame();
 report.cases.push('Entering failed-path editing uses retained geometry without another planning call and preserves Failed/Unchecked; moving a middle point creates Draft/Unchecked while both endpoint positions and directions stay locked');

 // The production local gesture updater must also update failed/colliding lines
 // without forcing a sidebar/canvas rebuild on every pointer movement.
 await page.evaluate(()=>{fixture.editing.syncRapidPathEditing(fixture.context,fixture.view,fixture.ops,()=>fixture.checks++);fixture.gestureOptions=fixture.view.viewport.transformManipulator.options;fixture.gestureOptions.onStart();fixture.beforeGestureRender=fixture.renderCount;fixture.gestureOptions.onChange({translation:[0,5,0]});});
 const gesture=await page.evaluate(()=>({renders:fixture.renderCount,before:fixture.beforeGestureRender,points:fixture.view.fiveRapid.pathEdit.points,line:fixture.lines().find(t=>t.id==='a/b'),draft:fixture.editing.manualRapidDraft(fixture.view)}));
 assert.equal(gesture.renders,gesture.before);assert.equal(gesture.line.points[4],gesture.points[1][1]);assert.equal(gesture.draft.validation,'Unchecked');assert.notEqual(gesture.draft.geometryStatus,'Generated');
 await page.evaluate(()=>{fixture.gestureOptions.onEnd({moved:true});fixture.editing.disposeRapidPathEditing(fixture.view);});
 report.cases.push('Dragging an unverified candidate updates the existing THREE line directly and does not rebuild the sidebar or canvas per movement');

 // Capture late interaction state at patch time, after the async read began.
 await page.evaluate(()=>{const input=document.querySelector('[data-five-rapid-field="pathX"]');fixture.keptInput=input;fixture.keptCanvas=fixture.canvas;fixture.keptLeft=document.querySelector('#test-left-input');fixture.listenerCalls=0;input.addEventListener('test-kept-listener',()=>fixture.listenerCalls++);input.focus({preventScroll:true});input.setSelectionRange(1,3);});
 const refreshing=page.evaluate(()=>fixture.refresh());await page.waitForFunction(()=>fixture.refreshHeld);
 const positions=await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map((selector,index)=>{const pane=document.querySelector(selector);pane.scrollTop=45+index*25;return pane.scrollTop;}));
 await page.evaluate(()=>fixture.releaseRefresh());await refreshing;
 assert.deepEqual(await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-rapid-body'].map(selector=>document.querySelector(selector).scrollTop)),positions);
 assert.equal(await page.evaluate(()=>fixture.keptInput===document.activeElement&&fixture.keptInput.isConnected&&fixture.keptInput.selectionStart===1&&fixture.keptInput.selectionEnd===3&&fixture.keptCanvas===document.querySelector('canvas')&&fixture.keptLeft===document.querySelector('#test-left-input')),true);
 await page.evaluate(()=>fixture.keptInput.dispatchEvent(new Event('test-kept-listener')));assert.equal(await page.evaluate(()=>fixture.listenerCalls),1);
 await page.evaluate(()=>{for(const selector of ['.cam-context-pane','.cam-info-pane','.five-rapid-body'])document.querySelector(selector).scrollTop=0;});
 await page.screenshot({path:resolve(output,'failed-candidate-edited.png')});
 report.cases.push('A delayed production DOM patch preserves the latest left/right/nested scrolling, input identity, focus, caret selection, canvas identity and input listeners');

 await page.evaluate(()=>{
  const s=fixture.view.fiveRapid;s.pathEdit=null;s.dirty=false;s.preview=null;fixture.planning.rapidPick(fixture.view,'b/c');fixture.bindSave();fixture.render();
 });
 await page.locator('[data-five-command="five.rapid.path-edit"]').click();await page.waitForFunction(()=>!fixture.busy&&fixture.view.fiveRapid.pathEdit);
 assert.deepEqual(await page.evaluate(()=>{const d=fixture.editing.manualRapidDraft(fixture.view);return [d.geometryStatus,d.validation];}),['Failed','Collision']);
 await page.evaluate(()=>{fixture.editing.syncRapidPathEditing(fixture.context,fixture.view,fixture.ops,()=>fixture.checks++);fixture.collisionGesture=fixture.view.viewport.transformManipulator.options;fixture.collisionGesture.onStart();fixture.beforeCollisionDragRender=fixture.renderCount;fixture.collisionGesture.onChange({translation:[0,4,0]});});
 assert.equal(await page.evaluate(()=>fixture.renderCount===fixture.beforeCollisionDragRender),true,'collision coloring cannot force a full rerender per drag movement');
 assert.deepEqual(await page.evaluate(()=>{const p=fixture.view.fiveRapid.pathEdit.points[1],line=fixture.lines().find(t=>t.id==='b/c');return line.points.slice(3,6).map((v,i)=>Math.abs(v-p[i])<1e-5);}),[true,true,true]);
 await page.evaluate(()=>{fixture.collisionGesture.onCancel();fixture.editing.disposeRapidPathEditing(fixture.view);});
 assert.deepEqual(await page.evaluate(()=>{const d=fixture.editing.manualRapidDraft(fixture.view);return [d.geometryStatus,d.validation];}),['Failed','Collision']);
 await page.evaluate(()=>{const s=fixture.view.fiveRapid;s.preview={result:{...structuredClone(fixture.transitions[1]),validation:'Unchecked',reason:'TRAVEL_CLEARANCE_BUDGET'}};fixture.render();});
 assert.equal(await page.evaluate(()=>fixture.editing.manualRapidDraft(fixture.view).validation),'Unchecked');
 assert.equal(await page.locator('[data-five-command="five.rapid.apply"]').isDisabled(),true);
 await page.evaluate(()=>fixture.planning.rapidCommand(fixture.context,fixture.view,'apply',fixture.ops));assert.equal((await state()).nativeCalls.length,0);
 await page.locator('[data-five-rapid-field="pathY"]').fill('-6');
 assert.deepEqual(await page.evaluate(()=>{const d=fixture.editing.manualRapidDraft(fixture.view);return [d.geometryStatus,d.validation];}),['Draft','Unchecked']);
 report.cases.push('Failed collision candidates remain editable; a failed unchecked preview never fabricates Collision, never enables Apply, and editing clears obsolete collision results to Draft/Unchecked');

 const invalid=await page.evaluate(()=>{const source=fixture.transitions[0],has=fixture.hasRapidGeometry;return {
  valid:has(source),legacy:has({...source,referencePoint:undefined}),world:has({...source,frame:'World'}),tip:has({...source,referencePoint:'ToolBase'}),status:has({...source,geometryStatus:'Stale'}),mismatch:has({...source,directions:source.directions.slice(1)}),nan:has({...source,points:[[NaN,0,0],...source.points.slice(1)]})
 };});
 assert.deepEqual(invalid,{valid:true,legacy:true,world:false,tip:false,status:false,mismatch:false,nan:false});
 assert.deepEqual(errors,[]);report.cases.push('Geometry acceptance excludes stale, wrong-frame, wrong-reference, nonfinite and mismatched point/direction data while accepting legacy ToolTip-compatible geometry');report.passed=true;
}catch(error){report.error=error.stack;report.pageErrors=errors;process.exitCode=1;await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
