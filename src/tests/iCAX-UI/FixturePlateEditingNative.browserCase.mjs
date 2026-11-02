import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,readdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';

// Actual production DOM + production scene SDO/resources. No business replies
// are mocked; the host's file/history adapters are explicit test transports.
export async function recheckPlateEditing({page,rpc,info,output,errors}){
  const cases=[],state=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest&&!fixture.view.fiveAxisImportActive,null,{timeout:600000});
  const command=async id=>{await page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
  const designClick=async name=>{await page.locator(`[data-five-command="five.fixture.design.${name}"]`).click();await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
  const row=(id,action)=>page.locator(`[data-five-command="five.fixture.part-${action}:${id}"]`);
  const unitOf=s=>s.fixtureUnits.find(u=>u.kind==='Generated');
  const partOf=(s,id)=>unitOf(s).design.assembly.parts.find(p=>p.partId===id);
  const snapshots=s=>Object.fromEntries(unitOf(s).design.assembly.parts.map(p=>[p.partId,JSON.stringify({geometryRef:p.geometryRef,frame:p.frame,outline:p.outline,holes:p.holes,joints:p.joints})]));
  const meshVisible=(id,visible)=>page.waitForFunction(({id,visible})=>{const mesh=fixture.view.viewport?.sceneObjects.get(id);return visible?!!mesh&&mesh.visible&&mesh.geometry?.getAttribute('position')?.count>0:!mesh||!mesh.visible;},{id,visible},{timeout:120000});

  await selectWorkpieceFile(page,info.sourcePath);await idle();await command('five.fixture.focus.design');await designClick('pick');
  const selectedFaces=await page.evaluate(async()=>{const {designChanged}=await import('/src/apps/five-axis-cam/webpage/fixtureDesign.mjs');const faces=fixture.view.fixtureDesignFaces.map(f=>String(f.faceId));designChanged(fixture.view,'supportFaces',faces);fixture.view.fixtureDesignPicking=false;fixture.entry.mountProject(fixture.context);return faces;});
  assert.ok(selectedFaces.length);assert.equal(await page.locator('[data-five-fixture-field="design:windowMode"]').inputValue(),'Automatic');assert.equal(await page.locator('[data-five-fixture-field="design:windowWeb"]').inputValue(),'8');
  await designClick('generate');await designClick('confirm');
  let current=await state();const unit=unitOf(current);assert.ok(unit);assert.equal(unit.design.recipe.jointMode,'TabSlot');assert.equal(current.intentToolpaths.length,0);
  assert.ok(unit.design.assembly.parts.every(p=>Array.isArray(p.windowHoleIndices)),'Fresh generated metadata identifies editable windows');
  if(await page.locator('[data-five-command="five.fixture.design.close"]').count())await designClick('close');
  await command('five.fixture.select:'+unit.id);await row(unit.partIds[0],'edit').waitFor();
  await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,.8]);fixture.view.viewport.fitView();document.querySelector('.cam-info-pane').scrollTop=10000;});
  const before=snapshots(current),parts=unit.design.assembly.parts;
  for(const p of parts)await meshVisible(p.partId,true);
  const opacity=()=>page.evaluate(()=>{const result={};for(const [id,obj] of fixture.view.viewport.sceneObjects){const values=[];obj.traverse(node=>{if(node.userData?.meshFeatureEdges||node.userData?.independentInteractionVisual)return;for(const m of Array.isArray(node.material)?node.material:node.material?[node.material]:[])values.push(m.opacity);});if(values.length)result[id]=values;}return result;});
  const originalOpacity=await opacity(),focusId=parts[0].partId,otherId=parts[1].partId;
  await row(focusId,'select').click();await page.waitForFunction(id=>fixture.view.viewport.emphasizedObjectIds.size===1&&fixture.view.viewport.emphasizedObjectIds.has(id),focusId);assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getDebugState().emphasizedObjectIds),[focusId]);
  let focusedOpacity=await opacity();for(const [id,values] of Object.entries(originalOpacity))for(let i=0;i<values.length;++i)assert.ok(Math.abs(focusedOpacity[id][i]-values[i]*(id===focusId?1:.14))<1e-8,id+' material dim state');
  await row(otherId,'select').click();await page.waitForFunction(id=>fixture.view.viewport.emphasizedObjectIds.size===1&&fixture.view.viewport.emphasizedObjectIds.has(id),otherId);assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getDebugState().emphasizedObjectIds),[otherId]);
  await row(focusId,'select').click();await page.screenshot({path:resolve(output,'selected-part-other-geometry-dimmed.png')});
  await command('five.tool.select');assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getDebugState().emphasizedObjectIds),[]);focusedOpacity=await opacity();for(const [id,values] of Object.entries(originalOpacity))for(let i=0;i<values.length;++i)assert.ok(Math.abs(focusedOpacity[id][i]-values[i])<1e-8,id+' restored opacity');
  await command('five.fixture.select:'+unit.id);cases.push('Selecting one actual part emphasizes only its native mesh and dims every other workpiece/plate; switching selection and leaving the tool restore the material opacity');
  const disabled=parts.find(p=>p.role==='Support');assert.ok(disabled);await row(disabled.partId,'toggle').click();await idle();current=await state();
  assert.equal(current.fixtures.find(f=>f.id===disabled.partId).enabled,false);assert.equal(unitOf(current).partIds.length,parts.length);assert.deepEqual(snapshots(current),before);await meshVisible(disabled.partId,false);
  await page.screenshot({path:resolve(output,'part-disabled.png')});await row(disabled.partId,'toggle').click();await idle();current=await state();assert.notEqual(current.fixtures.find(f=>f.id===disabled.partId).enabled,false);await meshVisible(disabled.partId,true);
  cases.push('The actual generated row disables/enables one support: no true deletion, no sibling geometry/joint changes, and the native mesh is hidden/restored');

  const candidates=[];
  for(const p of parts)for(const index of p.windowHoleIndices){const polygon=p.holes[Number(index)];if(!polygon?.length)continue;const xs=polygon.map(q=>q[0]),ys=polygon.map(q=>q[1]),b=[Math.min(...xs),Math.min(...ys),Math.max(...xs),Math.max(...ys)],w=b[2]-b[0],h=b[3]-b[1];if(polygon.length===4&&w>5&&h>5&&polygon.every(q=>(Math.abs(q[0]-b[0])<1e-6||Math.abs(q[0]-b[2])<1e-6)&&(Math.abs(q[1]-b[1])<1e-6||Math.abs(q[1]-b[3])<1e-6)))candidates.push({p,index:Number(index),b,score:Math.min(w,h)+(p.role==='Base'?1000:0)});}
  candidates.sort((a,b)=>b.score-a.score);const target=candidates[0];assert.ok(target,'A fresh automatic rectangular window is required');const partId=target.p.partId;
  await row(partId,'edit').click();await page.locator('[data-fixture-plate-editor]').waitFor();await page.waitForFunction(()=>fixture.view.fiveFixturePlate?.profile&&!fixture.view.fiveFixturePlate.loading,null,{timeout:120000});
  assert.equal(await page.evaluate(()=>fixture.view.fiveFixturePlate.error),'');
  const profile=await page.evaluate(()=>structuredClone(fixture.view.fiveFixturePlate.profile));const protectedIds=await page.evaluate(()=>[...fixture.view.fiveFixturePlate.protectedIds]);assert.ok(protectedIds.includes('O0'));assert.ok(!protectedIds.includes('H'+target.index));
  const protectedHole=protectedIds.find(id=>id.startsWith('H'));if(protectedHole){await page.locator(`[data-plate-select="${protectedHole}"]`).click();assert.equal(await page.locator('[data-plate-action="delete"]').count(),0);}
  await page.locator(`[data-plate-select="H${target.index}"]`).click();await page.screenshot({path:resolve(output,'part-editor-original-window.png')});
  const width=page.locator('[data-plate-field="width"]'),originalWidth=Number(await width.inputValue());await width.fill(String(originalWidth+.5));await width.press('Enter');await page.locator('[data-plate-action="save"]').click();
  await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveFixturePlate?.saving,null,{timeout:120000});assert.equal(await page.evaluate(()=>fixture.view.fiveFixturePlate?.error||fixture.view.error||''),'');assert.equal(await page.locator('[data-fixture-plate-editor]').count(),0);
  const enlargedState=await state(),enlargedPart=partOf(enlargedState,partId);assert.notDeepEqual(enlargedPart.geometryRef,target.p.geometryRef);for(const p of parts)if(p.partId!==partId)assert.equal(snapshots(enlargedState)[p.partId],before[p.partId]);
  cases.push('An original automatic rectangular window grows by 0.5 mm through its numeric width field under the manual minimum-web and exact protected-region checks');
  await row(partId,'edit').click();await page.waitForFunction(()=>fixture.view.fiveFixturePlate?.profile&&!fixture.view.fiveFixturePlate.loading,null,{timeout:120000});await page.locator(`[data-plate-select="H${target.index}"]`).click();await page.locator('[data-plate-action="delete"]').click();
  const cx=(target.b[0]+target.b[2])/2,cy=(target.b[1]+target.b[3])/2,radius=Math.min(target.b[2]-target.b[0],target.b[3]-target.b[1])*.22;
  await page.locator('[data-plate-tool="circle"]').click();
  const screen=await page.evaluate(({cx,cy,radius})=>{const svg=document.querySelector('[data-plate-canvas]'),map=(x,y)=>{const p=svg.createSVGPoint();p.x=x;p.y=-y;const q=p.matrixTransform(svg.getScreenCTM());return {x:q.x,y:q.y};};return {center:map(cx,cy),edge:map(cx+radius,cy)};},{cx,cy,radius});
  await page.mouse.move(screen.center.x,screen.center.y);await page.mouse.down();await page.mouse.move(screen.edge.x,screen.edge.y,{steps:8});await page.mouse.up();
  const diameter=page.locator('[data-plate-field="diameter"]');await diameter.fill(String(radius*2));await diameter.press('Enter');
  assert.equal(await page.evaluate(()=>fixture.view.fiveFixturePlate.profile.Boundaries.length),profile.Boundaries.length);await page.screenshot({path:resolve(output,'part-editor-new-circle.png')});
  await page.locator('[data-plate-action="save"]').click();
  await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fiveFixturePlate?.saving,null,{timeout:120000});
  const editorError=await page.evaluate(()=>fixture.view.fiveFixturePlate?.error||fixture.view.error||'');assert.equal(editorError,'');assert.equal(await page.locator('[data-fixture-plate-editor]').count(),0);await idle();
  const after=await state(),edited=partOf(after,partId);assert.notDeepEqual(edited.geometryRef,target.p.geometryRef);assert.deepEqual(edited.frame,target.p.frame);assert.deepEqual(edited.joints,target.p.joints);assert.ok(edited.exactProfile);
  const siblingAfter=snapshots(after);for(const p of parts)if(p.partId!==partId)assert.equal(siblingAfter[p.partId],before[p.partId],p.number+' sibling unchanged');
  const circleLoop=edited.exactProfile.Boundaries.slice(1).find(loop=>loop.Curves.length===1&&loop.Curves[0].Segment.Curve.type==='Circle2'&&Math.abs(loop.Curves[0].Segment.Curve.value.Radius-radius)<1e-7);assert.ok(circleLoop,'The native saved geometry retains an exact Circle2');
  await meshVisible(partId,true);await page.screenshot({path:resolve(output,'part-edited-scene.png')});cases.push('The actual row editor preserves locked contours, deletes one automatic window and draws a new exact circle with pointer input; Done commits one BRep and preserves all siblings and joints');

  await page.evaluate(()=>{for(const direction of ['undo','redo'])fixture.context.actions[direction+'Project']=async()=>{await fiveNativeRpc({action:direction});await fixture.entry.onProjectHistoryChanged(fixture.context);};});
  for(const [direction,reference] of [['undo',enlargedPart.geometryRef],['redo',edited.geometryRef]]){await page.evaluate(direction=>fixture.entry.handleProjectHistoryCommand(fixture.context,direction),direction);await idle();const checked=await state();assert.deepEqual(partOf(checked,partId).geometryRef,reference);for(const p of parts)if(p.partId!==partId)assert.equal(snapshots(checked)[p.partId],before[p.partId]);}
  cases.push('Framework/database Undo and Redo restore only the edited plate geometry through the production history refresh');

  await row(disabled.partId,'toggle').click();await idle();await designClick('breakdown');await designClick('export');await designClick('breakdown-close');
  let exported=await state(),design=unitOf(exported).design,delivery=design.delivery;assert.ok(delivery?.directory);let manifest=JSON.parse(readFileSync(resolve(delivery.directory,'manifest.json'),'utf8'));assert.equal(manifest.parts.length,parts.length-1);assert.ok(!manifest.parts.some(p=>p.partId===disabled.partId||p.number===disabled.number));assert.equal(readdirSync(delivery.directory).filter(name=>name.toLowerCase().endsWith('.dxf')).length,parts.length-1);
  await row(disabled.partId,'toggle').click();await idle();await designClick('breakdown');await designClick('export');await designClick('breakdown-close');exported=await state();design=unitOf(exported).design;delivery=design.delivery;manifest=JSON.parse(readFileSync(resolve(delivery.directory,'manifest.json'),'utf8'));assert.equal(manifest.parts.length,parts.length);
  const dxf=readFileSync(resolve(delivery.directory,target.p.number+'.dxf'),'utf8'),lines=dxf.trim().split(/\r?\n/),entities=[];
  for(let i=0;i<lines.length;i+=2){const code=Number(lines[i]),value=lines[i+1];if(code===0)entities.push({type:value,groups:[]});else entities.at(-1)?.groups.push([code,value]);}
  const circles=entities.filter(e=>e.type==='CIRCLE'&&e.groups.some(([c,v])=>c===8&&v==='CUT_INNER'));assert.ok(circles.some(e=>e.groups.some(([c,v])=>c===40&&Math.abs(Number(v)-radius)<1e-7)),'DXF contains the exact new circle, not display chords');
  cases.push('A disabled plate is excluded from native breakdown and DXF output; enabling restores it, and manufacturing output retains the exact edited CIRCLE');
  const project=resolve(output,`actual-step-edited-plate-${Date.now()}.i5cam`);await rpc({action:'export-project',path:project});const saved=await state(),reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.fixtures,saved.fixtures);assert.deepEqual(reopened.fixtureUnits,saved.fixtureUnits);assert.equal(reopened.intentToolpaths.length,0);
  await rpc({action:'export-project',path:project});assert.deepEqual(errors,[]);
  const report={passed:true,cases,source:info.sourcePath,project,unitId:unit.id,partId,number:target.p.number,removedWindowId:'H'+target.index,automaticWindowWidthIncrease:.5,circle:{cx,cy,radius},geometryBefore:target.p.geometryRef,geometryEnlarged:enlargedPart.geometryRef,geometryAfter:edited.geometryRef,siblingCount:parts.length-1,delivery:delivery.directory,protectedIds,coverage:{browser:'Edge Chromium',productionUi:true,nativeSdo:true,nativeResources:true,actualStep:true,freshGeneration:true,computerUse:false,sourceFaceSelection:'native face IDs assigned to production draft',fileChooser:'production file chooser with directory path supplied',standaloneCef:false,autosave:'unsaved native bridge context; explicit native archive save and reopen verified'}};
  writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}
