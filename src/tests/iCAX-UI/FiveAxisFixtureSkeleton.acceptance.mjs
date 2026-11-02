import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {readFileSync,mkdirSync} from 'node:fs';
import {randomUUID} from 'node:crypto';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyFixtureSkeleton({page,rpc,info,output,modelPath,interactionOnly=false}){
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest,undefined,{timeout:180000});
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  await selectWorkpieceFile(page,modelPath||info.sourcePath);await idle();
  await page.waitForFunction(()=>!fixture.view.fiveAxisImportActive&&fixture.view.viewport?.sceneObjects.size===1);
  const resolution=await page.evaluate(()=>({actual:fixture.view.viewport.renderer.getPixelRatio(),expected:Math.min(Math.max(window.devicePixelRatio||1,1),2)}));assert.equal(resolution.actual,resolution.expected);
  await command('five.setup.create');await idle();await command('five.fixture.generate');await idle();
  assert.deepEqual(await page.evaluate(()=>({pattern:fixture.view.fixtureDesignDraft.pattern,windows:fixture.view.fixtureDesignDraft.windowMode})),{pattern:'Skeleton',windows:'Automatic'});
  const advanced=page.locator('details[data-five-key="design-options"]');
  assert.equal(await advanced.evaluate(n=>n.open),false);
  for(const key of ['layout','pattern','positions','family','postWidth','spacing'])assert.equal(await page.locator(`[data-five-fixture-field="design:${key}"]`).count(),0);
  assert.deepEqual(await page.evaluate(()=>{const d=fixture.view.fixtureDesignDraft;return {layout:d.layout,thickness:d.thickness,baseThickness:d.baseThickness,baseDrop:d.baseDrop,margin:d.margin,gap:d.gap,minimumWeb:d.minimumWeb,jointMode:d.jointMode,jointClearance:d.jointClearance,tenonWidth:d.tenonWidth,tenonPitch:d.tenonPitch,jointWeb:d.jointWeb,tenonRadius:d.tenonRadius,baseShape:d.baseShape,sizing:d.sizing};}),{layout:'Automatic',thickness:3,baseThickness:5,baseDrop:40,margin:15,gap:.2,minimumWeb:5,jointMode:'TabSlot',jointClearance:.2,tenonWidth:8,tenonPitch:60,jointWeb:1.5,tenonRadius:1,baseShape:'Rectangle',sizing:'Automatic'});
  await page.locator('[data-five-command="five.fixture.design.auto"]').click();await idle();
  assert.equal(await page.evaluate(()=>fixture.view.fixtureDesignDraft.supportFaces.length),46);
  assert.deepEqual(await page.evaluate(()=>({supportCount:fixture.view.fixtureDesignDraft.supportCount,connectionCount:fixture.view.fixtureDesignDraft.connectionCount,axis:fixture.view.fixtureDesignDraft.layoutInfo.axis})),{supportCount:6,connectionCount:2,axis:'X'});
  assert.equal(await page.locator('[data-five-fixture-field="design:supportCount"]').isVisible(),true);
  assert.equal(await page.evaluate(()=>fixture.view.fixtureLayoutGuide?.group.name),'fixture-layout-directions');
  await page.screenshot({path:resolve(output,'simplified-panel.png')});
  await advanced.locator('summary').click();

  // Conditional fields retain their shared input/canvas nodes; a field removed
  // by a window change must not focus an unrelated surviving input.
  await page.evaluate(()=>{fixture.spanInput=document.querySelector('[data-five-fixture-field="design:supportCount"]');fixture.savedCanvas=fixture.view.viewport.renderer.domElement;});
  await page.locator('[data-five-fixture-field="design:positionMode"]').selectOption('Manual');
  assert.equal(await page.locator('[data-five-fixture-field="design:supportPositionText"]').isVisible(),true);
  await page.locator('[data-five-fixture-field="design:positionMode"]').selectOption('Automatic');
  assert.equal(await page.locator('[data-five-fixture-field="design:supportPositionText"]').count(),0);
  await page.locator('[data-five-fixture-field="design:windowMode"]').selectOption('Automatic');
  assert.equal(await advanced.evaluate(n=>n.open),true);
  await page.locator('[data-five-fixture-field="design:windowWeb"]').focus();
  await page.locator('[data-five-fixture-field="design:windowMode"]').selectOption('None');
  assert.equal(await page.locator('[data-five-fixture-field="design:windowWeb"]').count(),0);
  assert.equal(await page.evaluate(()=>fixture.spanInput===document.querySelector('[data-five-fixture-field="design:supportCount"]')&&fixture.savedCanvas===fixture.view.viewport.renderer.domElement),true);
  assert.notEqual(await page.evaluate(()=>document.activeElement?.dataset.fiveFixtureField),'design:supportCount');

  if(interactionOnly){
    const style=await page.addStyleTag({content:'.cam-context-pane,.cam-info-pane{overflow:auto!important;max-height:400px!important}.cam-context-pane::after,.cam-info-pane::after{content:"";display:block;min-height:1400px}.five-design-body{max-height:300px!important}'});
    await page.evaluate(()=>{fixture.holdMethod='CamPlacement.ReadReferenceFaces';fixture.hold=true;fixture.focusInput=document.querySelector('[data-five-fixture-field="design:supportCount"]');fixture.focusCanvas=fixture.view.viewport.renderer.domElement;fixture.probeEvents=0;fixture.focusInput.addEventListener('skeleton-probe',()=>fixture.probeEvents++);void fixture.entry.handleRibbonCommand(fixture.context,'five.fixture.design.pick');});
    await page.waitForFunction(()=>fixture.held);
    await page.evaluate(()=>{fixture.focusInput.focus({preventScroll:true});fixture.focusInput.setSelectionRange(0,1);for(const s of ['.cam-context-pane','.cam-info-pane','.five-design-body'])document.querySelector(s).scrollTop=40;});
    // Continue scrolling after the request was sent. Restoration must use the
    // newest positions rather than those captured when the async read began.
    const scroll=await page.evaluate(()=>['.cam-context-pane','.cam-info-pane','.five-design-body'].map((s,i)=>{const n=document.querySelector(s);n.scrollTop=90+i*25;return n.scrollTop;}));
    await page.evaluate(()=>fixture.release());await idle();
    const interaction=await page.evaluate(()=>{fixture.focusInput.dispatchEvent(new Event('skeleton-probe'));return {inputSame:fixture.focusInput===document.querySelector('[data-five-fixture-field="design:supportCount"]'),canvasSame:fixture.focusCanvas===fixture.view.viewport.renderer.domElement,focused:document.activeElement===fixture.focusInput,selection:[fixture.focusInput.selectionStart,fixture.focusInput.selectionEnd],events:fixture.probeEvents,scroll:['.cam-context-pane','.cam-info-pane','.five-design-body'].map(s=>document.querySelector(s).scrollTop)};});
    assert.deepEqual(interaction,{inputSame:true,canvasSame:true,focused:true,selection:[0,1],events:1,scroll});
    await style.evaluate(n=>n.remove());
    return {cases:['The fixed automatic skeleton uses the agreed defaults with advanced parameters initially collapsed','Delayed native picking preserves the input/canvas nodes, event listener, focus and text selection while both sidebars and a nested container keep the scrolls made after the request began','Conditional window fields appear/disappear without closing advanced parameters, stealing focus or rebuilding the surviving span input and canvas'],interaction};
  }

  const before=await read(),started=Date.now();
  await page.locator('[data-five-command="five.fixture.design.generate"]').click();
  await page.waitForFunction(()=>fixture.view.fiveAxisFixtureTask?.status==='Running'||!!fixture.view.error,undefined,{timeout:180000});
  assert.equal(await page.evaluate(()=>fixture.view.error),'');
  assert.equal(await page.locator('dialog.five-blocking-progress[open] [role="progressbar"]').count(),1);
  assert.equal(await page.getByRole('progressbar').count(),1);
  await page.screenshot({path:resolve(output,'skeleton-generating.png')});
  await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const generationMs=Date.now()-started;
  const candidate=await page.evaluate(()=>fixture.view.fiveFixtureGenerated.design.candidate);
  const generatedRecipe=await page.evaluate(()=>fixture.view.fiveFixtureGenerated.design.recipe);
  assert.equal(generatedRecipe.layout,'Automatic');assert.equal(generatedRecipe.pattern,'Skeleton');assert.equal(generatedRecipe.supportCount,6);assert.equal(generatedRecipe.connectionCount,2);assert.equal(generatedRecipe.spacing,undefined);
  assert.equal(generatedRecipe.openingReliefDepth,5);
  assert.deepEqual(candidate.layoutSummary.supportPositions,generatedRecipe.stations.filter(s=>!s.connection).map(s=>s.coordinate));
  assert.deepEqual(candidate.layoutSummary.connectionPositions,generatedRecipe.stations.filter(s=>s.connection).map(s=>s.coordinate));
  assert.equal(candidate.openingPreclearance.reliefDepthMm,5);
  const supports=candidate.parts.filter(p=>p.role==='Support'),connections=candidate.parts.filter(p=>p.role==='Connection');
  assert.equal(supports.length,6);assert.equal(connections.length,2);
  assert.equal(candidate.parts.filter(p=>p.role==='Base').length,1);assert.ok(candidate.parts.length<=12);
  const base=candidate.parts.find(p=>p.role==='Base');assert.equal(base.windowHoleIndices.length,0);assert.equal(base.holes.length,base.joints.filter(j=>j.type==='BaseMortise').length);
  assert.equal(candidate.supportCheck.status,'Supported');assert.ok(candidate.supportCheck.maximumSpanMm<=220+1e-6);assert.ok(candidate.supportCheck.maximumEndOverhangMm<=110+1e-6);
  for(const p of supports){assert.ok(p.contactPoints.length>0);assert.ok(p.span[0]<-100&&p.span[1]>100,`${p.number} must support both sides of the actual HatRail: span=${JSON.stringify(p.span)}, frame=${JSON.stringify(p.frame)}`);}
  for(const p of candidate.parts)assert.equal(p.windowHoleIndices.length,0);
  const railCoordinates=connections.map(p=>p.frame[7]);assert.ok(Math.min(...railCoordinates)<0&&Math.max(...railCoordinates)>0,'Connection plates must straddle the workpiece center');
  for(const p of connections){assert.equal(p.contactPoints.length,0);assert.ok(p.minimumDistance>=.2-1e-6);}
  const generated=await read();for(const key of ['workpieces','placements','setups','fixtures','fixtureUnits'])assert.deepEqual(generated[key],before[key]);
  assert.match(await page.locator('.five-design-generated-status').innerText(),/承托板.*连接板.*底板/);
  await page.screenshot({path:resolve(output,'skeleton-generated.png')});
  await command('five.fixture.design.confirm');await idle();const committed=await read();
  assert.equal(committed.fixtureUnits.length,1);assert.equal(committed.fixtures.length,candidate.parts.length);
  assert.deepEqual(committed.workpieces[0].geometryRef,before.workpieces[0].geometryRef);assert.deepEqual(committed.placements,before.placements);
  await page.screenshot({path:resolve(output,'skeleton-adopted.png')});
  await rpc({action:'undo'});assert.deepEqual((await read()).fixtures,before.fixtures);
  await rpc({action:'redo'});assert.deepEqual((await read()).fixtures,committed.fixtures);
  const manufacture=async(method,extra={})=>{const state=await read();return rpc({action:'invoke',scope:'scene',method,payload:{commandId:randomUUID(),expected:state.expected,setupId:state.activeSetupId,unitId:state.fixtureUnits[0].id,...extra}});};
  const breakdown=await manufacture('CamFixture.Breakdown');assert.equal(breakdown.parts.length,candidate.parts.length);
  mkdirSync(resolve(output,'manufacturing'),{recursive:true});
  const exported=await manufacture('CamFixture.Export',{destination:resolve(output,'manufacturing')});
  const manifest=JSON.parse(readFileSync(resolve(exported.directory,'manifest.json'),'utf8'));
  assert.equal(manifest.parts.length,candidate.parts.length);
  const baseDxf=readFileSync(resolve(exported.directory,'B01.dxf'),'utf8');
  for(const part of connections){const dxf=readFileSync(resolve(exported.directory,part.number+'.dxf'),'utf8');assert.match(dxf,/CUT_OUTER/);assert.ok(baseDxf.includes(part.number));}
  // Reopen the adopted unit through the production editor and change both
  // quantities. Generation is a draft, and adoption replaces this same unit.
  await command('five.refresh');await idle();
  await page.locator('[data-five-command="five.fixture.edit:'+committed.fixtureUnits[0].id+'"]').click();await idle();
  await page.locator('[data-five-fixture-field="design:supportCount"]').fill('4');
  await page.locator('[data-five-fixture-field="design:connectionCount"]').fill('3');
  await command('five.fixture.design.generate');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const edited=await page.evaluate(()=>fixture.view.fiveFixtureGenerated.design.candidate);
  assert.equal(edited.parts.filter(p=>p.role==='Support').length,4);assert.equal(edited.parts.filter(p=>p.role==='Connection').length,3);
  const editedRecipe=await page.evaluate(()=>fixture.view.fiveFixtureGenerated.design.recipe);
  assert.deepEqual(edited.layoutSummary.supportPositions,editedRecipe.stations.filter(s=>!s.connection).map(s=>s.coordinate));
  assert.deepEqual(edited.layoutSummary.connectionPositions,editedRecipe.stations.filter(s=>s.connection).map(s=>s.coordinate));
  assert.deepEqual((await read()).fixtures,committed.fixtures);
  await command('five.fixture.design.confirm');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const revised=await read();assert.equal(revised.fixtureUnits.length,1);assert.equal(revised.fixtureUnits[0].id,committed.fixtureUnits[0].id);assert.equal(revised.fixtures.length,8);
  assert.deepEqual(revised.placements,before.placements);assert.deepEqual(revised.workpieces[0].geometryRef,before.workpieces[0].geometryRef);
  await page.screenshot({path:resolve(output,'count-edited.png')});
  const reopened=await rpc({action:'save-reopen'});assert.deepEqual(reopened.fixtures,revised.fixtures);assert.deepEqual(reopened.fixtureUnits,revised.fixtureUnits);
  return {cases:['The default skeleton derives sparse bearing stations and lower connecting plates from the actual video HatRail; unsupported spans and end overhangs are bounded',
    'Connection plates have no fabricated contact evidence and retain real CAD clearance; one progress bar completes without changing the draft project',
    'Fixed automatic skeleton and agreed defaults are submitted to native generation; advanced parameters start collapsed and retain nodes and expansion across conditional window changes',
    'Confirmation, undo, redo, native save/reopen and manufacturing export preserve all plate roles, source geometry and connection-plate DXFs/base references',
    'Editing the adopted unit to four bearing plates and three connecting plates creates exactly the requested quantities and replaces the same unit only after confirmation'],generationMs,supportCount:supports.length,connectionCount:connections.length,partCount:candidate.parts.length,editedCounts:{supports:4,connections:3,parts:8},maximumSpanMm:candidate.supportCheck.maximumSpanMm,maximumEndOverhangMm:candidate.supportCheck.maximumEndOverhangMm,manufacturingDirectory:exported.directory};
}
