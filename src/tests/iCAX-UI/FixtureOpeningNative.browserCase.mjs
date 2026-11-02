import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';

// Isolated production SDO and actual production UI. Reference-face identifiers
// are supplied from the native selection query; pointer hit testing is outside
// this geometry-generation acceptance case.
export async function recheckOpeningStrategy({page,rpc,info,output,errors}){
  const cases=[],idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest&&!fixture.view.fiveAxisImportActive,null,{timeout:600000});
  const state=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  const field=key=>page.locator(`[data-five-fixture-field="design:${key}"]`);
  const command=async id=>{await page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
  const click=async name=>{await page.locator(`[data-five-command="five.fixture.design.${name}"]`).click();await page.evaluate(()=>fixture.entry.waitForActiveAreaAction(fixture.context));await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');};
  const design=()=>page.evaluate(()=>fixture.view.scene.domain.setups.find(s=>s.id===fixture.view.scene.domain.activeSetupId).fixtureDesign);
  const cutCount=domain=>(domain.intentToolpaths??[]).length;
  await selectWorkpieceFile(page,info.sourcePath);await idle();
  await command('five.fixture.focus.design');
  assert.equal(await field('windowMode').inputValue(),'Automatic');assert.equal(await field('windowWeb').inputValue(),'8');assert.equal(await field('cutClearanceMode').inputValue(),'None');
  assert.equal(cutCount(await state()),0);
  await click('pick');
  const selectedFaces=await page.evaluate(async requested=>{
    const {designChanged}=await import('/src/apps/five-axis-cam/webpage/fixtureDesign.mjs');
    const available=fixture.view.fixtureDesignFaces.map(f=>String(f.faceId));
    const selected=requested.length?requested:available;
    if(selected.some(id=>!available.includes(id)))throw Error('Requested support face is absent from the native reference query');
    designChanged(fixture.view,'supportFaces',selected);fixture.view.fixtureDesignPicking=false;fixture.entry.mountProject(fixture.context);return selected;
  },(process.env.ICAX_FIXTURE_OPENING_FACE_IDS||'').split(',').filter(Boolean));
  assert.ok(selectedFaces.length);
  if(process.env.ICAX_FIXTURE_OPENING_FULL_DEFAULTS){
    for(const [key,value] of Object.entries({layout:'Automatic',pattern:'Skeleton',sizing:'Automatic',baseDrop:'40',spacing:'220',windowMode:'None'}))assert.equal(await field(key).inputValue(),value,`New default ${key}`);
    assert.equal(await page.evaluate(()=>fixture.view.fixtureDesignDraft.jointMode),'TabSlot');
  }else{
    await field('baseDrop').fill(process.env.ICAX_FIXTURE_OPENING_BASE_DROP||'100');
    await field('spacing').fill(process.env.ICAX_FIXTURE_OPENING_SPACING||'40');
    if(process.env.ICAX_FIXTURE_OPENING_JOINT_MODE){await page.locator('[data-five-key="design-options"]>summary').click();await field('jointMode').selectOption(process.env.ICAX_FIXTURE_OPENING_JOINT_MODE);}
  }
  // Automatic windows are an explicit choice, not a new-support default.
  await field('windowMode').selectOption('Automatic');
  if(process.env.ICAX_FIXTURE_OPENING_WINDOW_WEB)await field('windowWeb').fill(process.env.ICAX_FIXTURE_OPENING_WINDOW_WEB);
  await page.screenshot({path:resolve(output,'opening-native-parameters.png')});
  await click('generate');const generated=await design(),automatic=generated.candidate;
  assert.equal(generated.recipe.windowMode,'Automatic');assert.equal(generated.recipe.cutClearanceMode,'None');assert.equal(cutCount(await state()),0);
  assert.equal(automatic.supportCheck.status,'Supported');assert.ok(automatic.parts.some(p=>p.role==='Base'));assert.ok(automatic.parts.some(p=>p.role==='Support'));
  const opening=automatic.openingPreclearance;
  assert.ok(opening);assert.ok(['Prepared','Partial'].includes(opening.status));assert.equal(opening.validity,'Current');assert.equal(opening.toolpathRequired,false);assert.equal(opening.toolpathStatus,'NotChecked');assert.equal(opening.productionQualified,false);assert.ok(Number(opening.boundaryCount)>0);
  await page.getByText(opening.status==='Partial'?'部分孔洞待处理':'孔洞预避让已生成',{exact:true}).waitFor();await page.getByText('实际光路待验证',{exact:true}).waitFor();
  assert.equal(await page.locator('[data-five-command="five.fixture.design.confirm"]').isEnabled(),true);
  cases.push('The actual STEP imports into a new isolated project; explicitly enabled windows generate alongside geometric preclearance with zero toolpaths');
  await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,.8]);fixture.view.viewport.fitView();});
  await page.screenshot({path:resolve(output,'opening-native-automatic-preview.png')});
  await click('confirm');const adoptedState=await state();
  const unit=adoptedState.fixtureUnits.find(u=>u.kind==='Generated');assert.ok(unit);assert.equal(unit.design.recipe.windowMode,'Automatic');assert.equal(unit.partIds.length,automatic.parts.length);assert.equal(cutCount(adoptedState),0);
  assert.equal(unit.design.assembly.openingPreclearance.toolpathRequired,false);assert.equal(unit.design.assembly.openingPreclearance.toolpathStatus,'NotChecked');
  cases.push('The supported candidate is adoptable without toolpaths, and native published fixture parts retain their opening preclearance facts');
  const counts=assembly=>({base:assembly.parts.filter(p=>p.role==='Base').reduce((n,p)=>n+p.holes.length,0),support:assembly.parts.filter(p=>p.role==='Support').reduce((n,p)=>n+p.holes.length,0),perPart:assembly.parts.map(p=>({number:p.number,role:p.role,holes:p.holes.length}))});
  const autoCounts=counts(automatic);
  writeFileSync(resolve(output,'generation-and-adoption.json'),JSON.stringify({generated:true,adopted:true,source:info.sourcePath,selectedFaces,recipe:generated.recipe,openingPreclearance:opening,supportCheck:automatic.supportCheck,counts:autoCounts,adoptedUnitId:unit.id,toolpathCount:cutCount(adoptedState),multiWindowComparison:'Pending'},null,2));

  // Create a separate unadopted candidate with solid plates so slots and the
  // workpiece's existing hole channels cannot be mistaken for auto windows.
  await command('five.fixture.focus.design');await field('windowMode').selectOption('None');await click('generate');
  const solid=(await design()).candidate,solidCounts=counts(solid);
  assert.equal(solid.parts.length,automatic.parts.length);assert.equal(solid.openingPreclearance.boundaryCount,opening.boundaryCount);
  assert.equal(autoCounts.base,solidCounts.base,'The solid base retains only assembly mortises in both modes');
  assert.ok(autoCounts.support>solidCounts.support,JSON.stringify({automatic:autoCounts,solid:solidCounts}));
  assert.ok(automatic.parts.filter(p=>p.role==='Support').some(p=>p.holes.length>=2),'At least one support has multiple actual window holes');
  const finalState=await state();assert.equal(finalState.fixtureUnits.length,adoptedState.fixtureUnits.length);assert.equal(finalState.fixtureUnits.find(u=>u.id===unit.id).design.recipe.windowMode,'Automatic');assert.equal(cutCount(finalState),0);
  await click('close');await rpc({action:'export-project',path:resolve(output,'actual-step-automatic-opening.i5cam')});
  cases.push('A separate None-window native candidate has fewer base and support holes, proving automatic windows beyond mortises and source-hole channels; the adopted automatic unit stays intact');
  assert.deepEqual(errors,[]);
  const report={passed:true,cases,source:info.sourcePath,selectedFaces,recipe:generated.recipe,openingPreclearance:opening,automatic:autoCounts,solid:solidCounts,adoptedUnitId:unit.id,parts:automatic.parts,coverage:{browser:'Edge Chromium',native:'production SDO, geometry services and published fixture resources',isolatedProject:true,sourceFileChooser:'production chooser with directory supplied',supportSelection:'native reference face IDs supplied through production draft input',pointerFaceSelection:false,standaloneCef:false,exactBRepKeepoutIntersection:'native test suite owns exact volume verification',toolpathOpticalValidation:false,mechanicalLoadQualification:false}};
  writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:true,cases,source:info.sourcePath,openingPreclearance:opening,automatic:autoCounts,solid:solidCounts},null,2));
}
