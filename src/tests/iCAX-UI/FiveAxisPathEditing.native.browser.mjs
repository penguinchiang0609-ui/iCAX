import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {writeFileSync} from 'node:fs';
export async function verifyPathEditing({page,rpc,output,assetBase='/src/',standaloneCef=false}){
 const read=()=>rpc({action:'read-state'}),invoke=(method,payload)=>rpc({action:'invoke',scope:'scene',method,payload}),idle=()=>page.waitForFunction(()=>!fixture.view.pending),command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
 const refresh=async()=>{await command('five.refresh');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
 await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await idle();await command('five.cam-page.cutting');await idle();
 let state=await read();const cad=state.workpieces,placements=state.placements;const cases=[];
 const none={kind:'None',placement:'ScrapSide',lengthMm:0,angleRad:0};await invoke('CamProcess.Update',{commandId:crypto.randomUUID(),expected:state.expected,intentIds:state.intentToolpaths.map(c=>c.id),patch:{kerf:{widthMm:0},leadIn:none,leadOut:none,gapLengthMm:0,overcutLengthMm:0}});await refresh();state=await read();
 for(const kind of ['Circle','Ellipse']){const cut=state.intentToolpaths.find(c=>c.geometryKind===kind);assert.ok(cut,`real recognized ${kind}`);await page.locator(`[data-five-intent="${cut.id}"]`).click();await idle();const field=kind==='Circle'?'diameterMm':'majorDiameterMm',value=cut.standardHole[field]+2;await page.locator(`[data-five-hole-dimension="${field}"]`).fill(String(value));await page.locator(`[data-five-command="five.hole-dimensions.apply:${cut.id}"]`).click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');state=await read();assert.equal(state.intentToolpaths.find(c=>c.id===cut.id).standardHole[field],value);assert.deepEqual(state.workpieces,cad);cases.push(`${kind} uses its actual parameter editor and changes CAM geometry only`);}
 const cut=state.intentToolpaths.find(c=>c.geometryKind==='Circle');await page.locator(`[data-five-intent="${cut.id}"]`).click();await command('five.tool.split');await page.evaluate(()=>{fixture.view.viewport.setViewDirection([0,0,1]);fixture.view.viewport.fitView(1.15);});
 const point=await page.evaluate(async ({id,assetBase})=>{const T=await import(assetBase+'iCAX-UI/SDK/ThirdParty/three/three.module.js'),{nearestCutLine}=await import(assetBase+'apps/five-axis-cam/webpage/cutSelection.mjs'),v=fixture.view,vp=v.viewport,c=v.scene.intentToolpaths.find(c=>c.id===id),source=vp.sceneObjects.get(c.placementId),box=vp.renderer.domElement.getBoundingClientRect();for(const p of c.previewPoints.slice(2,-2)){const q=new T.Vector3(...p).applyMatrix4(source.matrixWorld).project(vp.camera),x=box.x+(q.x+1)*box.width/2,y=box.y+(1-q.y)*box.height/2;if(document.elementFromPoint(x,y)===vp.renderer.domElement&&nearestCutLine(v,{clientX:x,clientY:y})?.intentId===id)return{x,y};}throw Error('No visible circle split station');},{id:cut.id,assetBase});
 await page.mouse.click(point.x,point.y);await idle();assert.equal(await page.evaluate(()=>fixture.view.fiveSplit.points.length),1);const count=state.intentToolpaths.length;await page.locator('[data-five-command="five.split.confirm"]').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');state=await read();assert.equal(state.intentToolpaths.length,count+1);const pieces=state.intentToolpaths.filter(c=>c.sourceKey===cut.sourceKey);assert.equal(pieces.length,2);cases.push('A real mouse split creates two independent path rows');
 await page.locator(`[data-five-intent="${pieces[0].id}"]`).click();await page.locator(`[data-five-intent="${pieces[1].id}"]`).click({modifiers:['Control']});
 assert.deepEqual((await page.evaluate(()=>fixture.view.fiveAxisUI.selectedIntentIds)).sort(),pieces.map(c=>c.id).sort());const inlineTolerance=page.locator('[data-ribbon-input="five.path.merge-tolerance"]');if(await inlineTolerance.count())await inlineTolerance.fill('0.1');else await page.evaluate(()=>fixture.entry.handleRibbonInput(fixture.context,'five.path.merge-tolerance','0.1'));const dialogs=await page.getByRole('dialog').count();await command('five.path.merge');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');assert.equal(await page.getByRole('dialog').count(),dialogs);state=await read();assert.equal(state.intentToolpaths.length,count);const merged=state.intentToolpaths.find(c=>c.sourceKey===cut.sourceKey);assert.ok(merged);cases.push('At least two selected paths merge into one with the inline tolerance and no merge dialog');
 let row=merged;for(const [tool,field,value,expected] of [
  ['path-translate','x','0.35',p=>[p[0]+.35,p[1],p[2]]],['path-rotate','angle','1',p=>{const a=Math.PI/180;return[p[0]*Math.cos(a)-p[1]*Math.sin(a),p[0]*Math.sin(a)+p[1]*Math.cos(a),p[2]];}]]){
  const before=row.startPoint;await command(`five.tool.${tool}`);await page.locator(`[data-five-path-field="${field}"]`).fill(value);await page.locator('[data-five-command="five.path.apply"]').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');state=await read();row=state.intentToolpaths.find(c=>c.id===row.id);assert.ok(row);const target=expected(before);for(let i=0;i<3;i++)assert.ok(Math.abs(row.startPoint[i]-target[i])<1e-7);assert.deepEqual(state.workpieces,cad);assert.deepEqual(state.placements,placements);}
 cases.push('Actual translate and rotate controls persist exact CAM changes while CAD and placement stay unchanged');
 assert.equal(await page.locator('[data-five-hole-dimension]').count(),0,'ordinary contours have no dimension editor');
 assert.equal(await page.locator('[data-ribbon-command="five.tool.path-scale"],[data-five-command="five.tool.path-scale"],[data-transform-owner="path"] [data-transform-mode="scale"]').count(),0);
 await command('five.tool.path-scale');assert.equal(await page.locator('[data-five-path-field="factor"]').count(),0);assert.deepEqual(await read(),state);
 await page.locator('canvas').first().focus();await page.keyboard.press('r');assert.notEqual(await page.evaluate(()=>fixture.view.viewport.transformManipulator?.control.mode),'scale');
 cases.push('Ribbon, floating toolbar, legacy command and R shortcut expose no path scaling');
 await verifyManualHole({page,rpc,output,cut:row});state=await read();
 await command('five.tool.path-translate');await page.screenshot({path:resolve(output,'path-editing.png')});await rpc({action:'export-project',path:resolve(output,'path-edit-accepted.i5cam')});const saved=await read();await rpc({action:'save-reopen'});assert.deepEqual((await read()).intentToolpaths,saved.intentToolpaths);cases.push('Native project save and reopen preserves all edited paths');
 writeFileSync(resolve(output,'path-edit-report.json'),JSON.stringify({passed:true,cases,coverage:{native:true,realMouse:true,filePicker:false,standaloneCef},cadUnchanged:true},null,2));
}
export async function verifyManualHole({page,rpc,output,cut,withKerf=false}){
 const read=()=>rpc({action:'read-state'}),idle=()=>page.waitForFunction(()=>!fixture.view.pending),command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
 await page.evaluate(()=>fixture.context.actions.selectRibbonTab('toolpath'));await command('five.cam-page.cutting');
 await page.locator(`[data-five-intent="${cut.id}"]`).click();
 const properties=page.locator('[data-five-command="five.cam-inspector.properties"]');if(await properties.count())await properties.click();
 const before=await read(),row=before.intentToolpaths.find(c=>c.id===cut.id);assert.equal(await page.locator('[data-five-hole-dimension]').count(),0);
 await page.locator(`[data-five-command="five.hole.manual:${cut.id}"]`).click();await idle();
 await page.locator('[data-five-hole-field="kind"]').selectOption('Ellipse');
 const major=Number(await page.locator('[data-five-hole-field="majorDiameterMm"]').inputValue()),minor=Number(await page.locator('[data-five-hole-field="minorDiameterMm"]').inputValue());assert.ok(major>=minor&&minor>0);
 await page.locator('[data-five-command="five.hole.confirm"]').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
 let declared=await read();const hole=declared.intentToolpaths.find(c=>c.id===cut.id);assert.equal(hole.standardHole.kind,'Ellipse');assert.equal(hole.standardHole.manual,true);assert.deepEqual(hole.previewPoints,row.previewPoints);
 if(withKerf){
  await rpc({action:'invoke',scope:'scene',method:'CamProcess.Update',payload:{commandId:crypto.randomUUID(),expected:declared.expected,intentIds:[cut.id],patch:{kerf:{widthMm:.2,measurementSection:'BeamNormalSection'}}}});
  for(let i=0;i<400;i++){declared=await read();if(declared.intentToolpaths.find(c=>c.id===cut.id).sourceState==='Current')break;assert.equal(declared.intentGenerationErrors?.length||0,0,JSON.stringify(declared.intentGenerationErrors));await new Promise(resolve=>setTimeout(resolve,50));}
  await command('five.refresh');await idle();assert.equal(declared.intentToolpaths.find(c=>c.id===cut.id).sourceState,'Current');
 }
 const dimensionBaseline=declared.intentToolpaths.find(c=>c.id===cut.id);
 await page.locator('[data-five-hole-dimension="majorDiameterMm"]').fill(String(major+1));
 await page.evaluate(()=>{
  const v=fixture.view,proxy=fixture.context.sceneProxy||v.viewportSceneProxy,invoke=proxy.invoke;window.holeInteraction={proxy,invoke,canvas:v.viewport.renderer.domElement,name:document.querySelector('[data-five-cut-name]'),events:0};
  holeInteraction.name.addEventListener('hole-test',()=>holeInteraction.events++);
  holeInteraction.name.focus({preventScroll:true});holeInteraction.name.setSelectionRange(0,1);
  proxy.invoke=async function(method,...args){const result=await invoke.call(this,method,...args);if(method==='CamCut.SetStandardHoleDimensions'){holeInteraction.waiting=true;await new Promise(resolve=>holeInteraction.release=resolve);}return result;};
 });
 await page.evaluate(id=>{void fixture.entry.handleRibbonCommand(fixture.context,id);},`five.hole-dimensions.apply:${cut.id}`);await page.waitForFunction(()=>holeInteraction.waiting,undefined,{timeout:20000});
 const interaction=await page.evaluate(async()=>{
  const h=holeInteraction,style=document.createElement('style');style.textContent='.cam-context-pane,.cam-info-pane{max-height:160px!important;overflow:auto!important}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;height:500px}.five-cut-tree{max-height:55px!important;overflow:auto!important}';document.head.append(style);h.style=style;
  h.scrolls=[...document.querySelectorAll('.cam-context-pane,.cam-info-pane,.five-cut-tree')];h.scrolls.forEach((n,i)=>n.scrollTop=35+i*7);
  await new Promise(resolve=>requestAnimationFrame(resolve));h.positions=h.scrolls.map(n=>n.scrollTop);h.release();return h.positions;
 });
 await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
 assert.deepEqual(await page.evaluate(()=>{const h=holeInteraction;h.name.dispatchEvent(new Event('hole-test'));const result={canvas:h.canvas===fixture.view.viewport.renderer.domElement,name:h.name===document.querySelector('[data-five-cut-name]'),focus:document.activeElement===h.name,selection:[h.name.selectionStart,h.name.selectionEnd],scrolls:h.scrolls.map(n=>n.scrollTop),events:h.events};h.proxy.invoke=h.invoke;h.style.remove();return result;}),{canvas:true,name:true,focus:true,selection:[0,1],scrolls:interaction,events:1});
 const after=await read(),resized=after.intentToolpaths.find(c=>c.id===cut.id);assert.equal(resized.standardHole.majorDiameterMm,major+1);assert.deepEqual(after.workpieces,before.workpieces);assert.deepEqual(after.placements,before.placements);
 const depth=c=>{const values=c.previewPoints.map(p=>p.reduce((s,v,i)=>s+v*hole.standardHole.direction[i],0));return Math.max(...values)-Math.min(...values);};assert.ok(Math.abs(depth(resized)-depth(dimensionBaseline))<.02,'manual dimensions retain the curved contour depth');
 await rpc({action:'undo'});await command('five.refresh');await idle();assert.deepEqual((await read()).intentToolpaths,declared.intentToolpaths);await rpc({action:'redo'});await command('five.refresh');await idle();assert.deepEqual((await read()).intentToolpaths,after.intentToolpaths);
 await page.screenshot({path:resolve(output,'manual-standard-hole.png')});
 writeFileSync(resolve(output,'manual-standard-hole-report.json'),JSON.stringify({passed:true,kind:'Ellipse',withKerf,geometryPreservedOnDeclaration:true,depthBefore:depth(dimensionBaseline),depthAfter:depth(resized),dimensionsEditable:true,undoRedo:true,cadUnchanged:true,interaction:{canvasAndInputNodes:true,listeners:true,focusAndSelection:true,latestScrollPositions:interaction}},null,2));
}
