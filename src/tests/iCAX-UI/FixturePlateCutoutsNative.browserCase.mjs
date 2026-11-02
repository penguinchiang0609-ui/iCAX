import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

export async function recheckPlateCutouts({page,rpc,output,errors}){
  const cases=[],read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest&&!fixture.view.fiveFixturePlate?.saving);
  const command=async id=>{await page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));await idle();};
  const initial=await read(),unit=initial.fixtureUnits.find(u=>u.kind==='Generated');assert.ok(unit);
  const target=unit.design.assembly.parts.find(p=>p.number==='S5');assert.ok(target);
  const row=id=>page.locator(`[data-five-key="fixture-part-${id}"]`);
  await command('five.fixture.select:'+unit.id);
  assert.equal(await page.locator('#five-fixture-plate-style').count(),0,'Row styling must load before the editor');
  await row(target.partId).locator('td').nth(2).click();await idle();
  assert.equal(await row(target.partId).getAttribute('aria-selected'),'true');
  const selectedStyle=await row(target.partId).locator('td').first().evaluate(el=>getComputedStyle(el).backgroundColor);assert.equal(selectedStyle,'rgb(215, 241, 236)');
  assert.deepEqual(await page.evaluate(()=>[...fixture.view.viewport.emphasizedObjectIds]),[target.partId]);
  assert.equal(await row(target.partId).locator('.five-plate-row-actions button svg').count(),2);
  assert.equal(await row(target.partId).locator('.five-plate-row-actions button').first().innerText(),'');
  await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,.8]);fixture.view.viewport.fitView();document.querySelector('.cam-info-pane').scrollTop=0;});
  // Find an actual visible triangle via raycast, then deliver a real pointer
  // click through the renderer's native pick route rather than calling a handler.
  const picked=await page.evaluate(()=>{
    const v=fixture.view.viewport,rect=v.renderer.domElement.getBoundingClientRect(),objects=[...v.sceneObjects.values()].filter(o=>o.visible),ids=new Set(fixture.view.scene.domain.fixtures.map(f=>f.id));
    for(let y=.35;y<.9;y+=.015)for(let x=.18;x<.85;x+=.015){v.raycaster.setFromCamera({x:x*2-1,y:1-y*2},v.camera);const h=v.raycaster.intersectObjects(objects,true).find(h=>h.object.visible&&!h.object.userData?.meshFeatureEdges);const id=h?.object.userData?.objectId;if(ids.has(id))return {id,x:rect.left+x*rect.width,y:rect.top+y*rect.height};}return null;
  });assert.ok(picked,'A visible generated plate must be pickable');
  await page.mouse.click(picked.x,picked.y);await idle();assert.equal(await row(picked.id).getAttribute('aria-selected'),'true');
  assert.equal(await page.evaluate(()=>fixture.view.fiveFixtureSelectedPart),picked.id);
  assert.ok(await row(picked.id).evaluate(el=>{const p=el.closest('.cam-info-pane').getBoundingClientRect(),r=el.getBoundingClientRect();return r.top>=p.top&&r.bottom<=p.bottom;}));
  await page.screenshot({path:resolve(output,'bidirectional-selection-icons.png')});
  cases.push('Before opening the editor, a full-row click and a real raycast/pointer scene pick synchronize the single row highlight and selected mesh; scene picking reveals the row without replacing the viewport');
  const open=async()=>{await page.locator(`[data-five-command="five.fixture.part-edit:${target.partId}"]`).click();await page.waitForFunction(()=>fixture.view.fiveFixturePlate?.profile&&!fixture.view.fiveFixturePlate.loading);};
  const save=async()=>{await page.locator('[data-plate-action="save"]').click();await idle();assert.equal(await page.evaluate(()=>fixture.view.fiveFixturePlate?.error||fixture.view.error||''),'');assert.equal(await page.locator('[data-fixture-plate-editor]').count(),0);};
  await open();assert.equal(await page.locator('nav[aria-label="二维绘图工具"] button svg').count(),7);assert.equal(await page.locator('nav[aria-label="二维绘图工具"]').innerText(),'');
  const circle=await page.evaluate(()=>fixture.view.fiveFixturePlate.profile.Boundaries.slice(1).find(l=>l.Curves[0].Segment.Curve.type==='Circle2'));assert.ok(circle);const c=circle.Curves[0].Segment.Curve.value,r=c.Radius;
  const drawRect=async(a,b)=>{await page.locator('[data-plate-tool="rectangle"]').click();const points=await page.evaluate(([a,b])=>{const svg=document.querySelector('[data-plate-canvas]');return [a,b].map(q=>{const p=svg.createSVGPoint();p.x=q[0];p.y=-q[1];const out=p.matrixTransform(svg.getScreenCTM());return {x:out.x,y:out.y};});},[a,b]);await page.mouse.move(points[0].x,points[0].y);await page.mouse.down();await page.mouse.move(points[1].x,points[1].y,{steps:6});await page.mouse.up();};
  await drawRect([c.Placement.Location.X-r-2,c.Placement.Location.Y-r-2],[c.Placement.Location.X+r+2,c.Placement.Location.Y+r+2]);
  assert.equal(await page.locator('[data-plate-resize]').count(),8);
  const before=await page.evaluate(()=>{const s=fixture.view.fiveFixturePlate;return s.profile.Boundaries.find(l=>l.ID===s.selected);});
  const property=async key=>Number(await page.locator(`[data-plate-field="${key}"]`).inputValue());
  const width=await property('width'),height=await property('height');
  const handle=await page.locator('[data-plate-resize="e"]').boundingBox();await page.mouse.move(handle.x+handle.width/2,handle.y+handle.height/2);await page.mouse.down();await page.mouse.move(handle.x+handle.width/2+8,handle.y+handle.height/2);await page.mouse.up();assert.ok(await property('width')>width);assert.ok(Math.abs(await property('height')-height)<1e-7);
  await page.locator('[data-plate-action="undo"]').click();
  const shiftHandle=await page.locator('[data-plate-resize="ne"]').boundingBox();await page.keyboard.down('Shift');await page.mouse.move(shiftHandle.x+shiftHandle.width/2,shiftHandle.y+shiftHandle.height/2);await page.mouse.down();await page.mouse.move(shiftHandle.x+shiftHandle.width/2+8,shiftHandle.y+shiftHandle.height/2-8);await page.mouse.up();await page.keyboard.up('Shift');assert.ok(Math.abs((await property('width'))/(await property('height'))-width/height)<1e-6);
  await page.locator('[data-plate-action="undo"]').click();
  assert.deepEqual(await page.evaluate(()=>{const s=fixture.view.fiveFixturePlate;return s.profile.Boundaries.find(l=>l.ID===s.selected);}),before);
  // SVG mask is inspected and rasterized: all cutout paths independently paint
  // black, so a nested circle cannot become a white material island.
  assert.equal(await page.locator('mask path[fill="black"]').count(),2);
  const image=await page.evaluate(async()=>{const svg=document.querySelector('[data-plate-canvas]').cloneNode(true);svg.setAttribute('xmlns','http://www.w3.org/2000/svg');svg.querySelectorAll('.plate-boundary,.plate-resize-box,.plate-resize-handle').forEach(n=>n.remove());svg.querySelector('.plate-material').setAttribute('fill','#ffffff');const blob=new Blob([new XMLSerializer().serializeToString(svg)],{type:'image/svg+xml'}),url=URL.createObjectURL(blob),img=new Image();img.src=url;await img.decode();const canvas=document.createElement('canvas');canvas.width=img.width;canvas.height=img.height;const ctx=canvas.getContext('2d');ctx.drawImage(img,0,0);URL.revokeObjectURL(url);return {width:canvas.width,height:canvas.height,emptyCenter:ctx.getImageData(canvas.width/2,canvas.height/2,1,1).data[3]};});
  assert.equal(image.emptyCenter,0,'The overlapping cutouts remain transparent in the rasterized material preview');
  await page.screenshot({path:resolve(output,'nested-cutout-no-island-resize-icons.png')});
  await save();let after=await read(),part=after.fixtureUnits.find(u=>u.id===unit.id).design.assembly.parts.find(p=>p.partId===target.partId);assert.equal(part.exactProfile.Boundaries.length,2);assert.equal(part.exactProfile.Boundaries[1].Curves.length,4,'Nested circle becomes part of a single rectangular void');
  cases.push('Icon-only drawing toolbar, eight drag handles, one-axis edge resize, Shift proportional resize and local undo work through real pointer input; nested cutouts render as a union and native save removes the circle material island');
  await open();const bounds=await page.evaluate(()=>{const loop=fixture.view.fiveFixturePlate.profile.Boundaries[0];const points=loop.Curves.flatMap(c=>c.Segment.Curve.type==='Segment2'?[c.Segment.Curve.value.Start,c.Segment.Curve.value.End]:[]);return {left:Math.min(...points.map(p=>p.X)),bottom:Math.min(...points.map(p=>p.Y)),top:Math.max(...points.map(p=>p.Y))};});
  await drawRect([bounds.left-3,20],[bounds.left+5,28]);assert.match(await page.locator('.plate-boundary.is-protected').first().getAttribute('mask'),/^url\(#plate-material-/);await page.screenshot({path:resolve(output,'edge-cutout-draft.png')});await save();
  after=await read();part=after.fixtureUnits.find(u=>u.id===unit.id).design.assembly.parts.find(p=>p.partId===target.partId);assert.notDeepEqual(part.outline,target.outline);assert.equal(part.exactProfile.Boundaries.length,2);
  await open();const right=Math.max(...part.outline.map(p=>p[0]));await page.locator('[data-plate-tool="circle"]').click();const circlePoints=await page.evaluate(({right})=>{const svg=document.querySelector('[data-plate-canvas]');return [[right,24],[right+3,24]].map(q=>{const p=svg.createSVGPoint();p.x=q[0];p.y=-q[1];const out=p.matrixTransform(svg.getScreenCTM());return {x:out.x,y:out.y};});},{right});await page.mouse.move(circlePoints[0].x,circlePoints[0].y);await page.mouse.down();await page.mouse.move(circlePoints[1].x,circlePoints[1].y,{steps:5});await page.mouse.up();await page.locator('[data-plate-field="diameter"]').fill('6');await page.locator('[data-plate-field="diameter"]').press('Enter');await save();
  for(const old of unit.design.assembly.parts)if(old.partId!==target.partId)assert.deepEqual(after.fixtureUnits.find(u=>u.id===unit.id).design.assembly.parts.find(p=>p.partId===old.partId),old);
  for(const action of ['breakdown','export']){await page.locator(`[data-five-command="five.fixture.design.${action}"]`).click();await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');}
  after=await read();const design=after.fixtureUnits.find(u=>u.id===unit.id).design,dxf=readFileSync(resolve(design.delivery.directory,target.number+'.dxf'),'utf8');assert.ok(dxf.includes('CUT_OUTER'));assert.ok(dxf.includes('\n42\n'),'The round edge notch retains an exact DXF bulge');assert.ok(!/\nCIRCLE\n/.test(dxf),'The nested circle is no longer an independent manufacturing contour');
  assert.equal(await page.getByRole('dialog',{name:'夹具板件清单'}).count(),1);assert.equal(await page.locator('.five-fixture-breakdown tbody tr').count(),unit.partIds.length);assert.equal(await page.locator('.five-fixture-breakdown tbody svg').count(),unit.partIds.length);assert.equal(await page.locator('.five-fixture-editor [data-five-command="five.fixture.design.export"]').count(),0);await page.screenshot({path:resolve(output,'breakdown-plate-thumbnails.png')});await command('five.fixture.design.breakdown-close');
  await open();const polygonCount=await page.evaluate(()=>fixture.view.fiveFixturePlate.profile.Boundaries.length);await page.locator('[data-plate-tool="polyline"]').click();const polygonPoints=await page.evaluate(()=>{const svg=document.querySelector('[data-plate-canvas]');return [[-3,12],[3,12],[0,16]].map(q=>{const p=svg.createSVGPoint();p.x=q[0];p.y=-q[1];const out=p.matrixTransform(svg.getScreenCTM());return {x:out.x,y:out.y};});});for(const point of polygonPoints)await page.mouse.click(point.x,point.y);await page.locator('[data-plate-close-polygon]').waitFor();await page.screenshot({path:resolve(output,'polygon-start-and-close-guide.png')});await page.locator('[data-plate-close-polygon]').click();assert.equal(await page.evaluate(()=>fixture.view.fiveFixturePlate.profile.Boundaries.length),polygonCount+1);assert.equal(await page.evaluate(()=>fixture.view.fiveFixturePlate.tool),'select');await page.locator('[data-plate-action="undo"]').click();assert.equal(await page.evaluate(()=>fixture.view.fiveFixturePlate.profile.Boundaries.length),polygonCount);await page.locator('[data-plate-action="close"]').last().click();cases.push('Polygon closure by clicking its highlighted start works with a live closing guide; the standalone breakdown dialog shows one named, illustrated row per enabled plate and owns the export action');
  const project=resolve(output,`actual-step-union-edge-cutouts-${Date.now()}.i5cam`);await rpc({action:'export-project',path:project});const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.fixtureUnits,after.fixtureUnits);assert.deepEqual(errors,[]);
  cases.push('An edge-crossing rectangle cuts a real notch into one support; sibling geometry and joints remain unchanged; exact outer/inner contours export and the edited project reopens');
  const report={passed:true,cases,project,unitId:unit.id,partId:target.partId,number:target.number,delivery:design.delivery.directory,previewRaster:image};writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}
