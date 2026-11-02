import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {randomUUID} from 'node:crypto';
import {isDeepStrictEqual} from 'node:util';
import {selectWorkpieceFile} from './fixtures/fiveAxisFilePicker.mjs';

export async function verifyFixtureFlowOpenings({page,rpc,info,output,modelPath}){
  const idle=()=>page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest,undefined,{timeout:180000});
  const command=id=>page.evaluate(id=>fixture.entry.handleRibbonCommand(fixture.context,id),id);
  const read=()=>rpc({action:'invoke',scope:'scene',method:'CamScene.ReadState',payload:{}});
  await selectWorkpieceFile(page,modelPath||info.sourcePath);await idle();
  await page.waitForFunction(()=>!fixture.view.fiveAxisImportActive&&fixture.view.viewport?.sceneObjects.size===1);
  await command('five.setup.create');await idle();await command('five.fixture.generate');await idle();
  assert.deepEqual(await page.evaluate(()=>({mode:fixture.view.fixtureDesignDraft.windowMode,web:fixture.view.fixtureDesignDraft.windowWeb})),{mode:'Automatic',web:10});
  assert.equal(await page.locator('details[data-five-key="design-options"]').evaluate(n=>n.open),false);
  await command('five.fixture.design.auto');await idle();const before=await read();
  await page.locator('details[data-five-key="design-options"] > summary').click();
  await page.locator('[data-five-fixture-field="design:windowMode"]').selectOption('None');
  await command('five.fixture.design.generate');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const originalBase=await page.evaluate(()=>fixture.view.fiveFixtureGenerated.design.candidate.parts.find(p=>p.role==='Base'));
  await page.locator('[data-five-fixture-field="design:windowMode"]').selectOption('Automatic');
  const started=Date.now();await command('five.fixture.design.generate');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const generationMs=Date.now()-started,candidate=await page.evaluate(()=>fixture.view.fiveFixtureGenerated.design.candidate),recipe=await page.evaluate(()=>fixture.view.fiveFixtureGenerated.design.recipe);
  const supports=candidate.parts.filter(p=>p.role==='Support'),connections=candidate.parts.filter(p=>p.role==='Connection'),base=candidate.parts.find(p=>p.role==='Base');
  assert.equal(supports.length,6);assert.equal(connections.length,2);assert.equal(candidate.supportCheck.status,'Supported');
  assert.equal(base.windowHoleIndices.length,0);for(const key of ['outline','holes','frame','thickness'])assert.deepEqual(base[key],originalBase[key]);
  assert.deepEqual(base.joints.map(j=>({type:j.type,boundary:j.boundary})),originalBase.joints.map(j=>({type:j.type,boundary:j.boundary})));
  const area=loop=>Math.abs(loop.reduce((n,p,i)=>{const q=loop[(i+1)%loop.length];return n+p[0]*q[1]-p[1]*q[0];},0))/2;
  for(const part of [...supports,...connections]){assert.equal(part.windowHoleIndices.length,1,`${part.number} must have one opening`);const hole=part.holes[part.windowHoleIndices[0]],xs=hole.map(p=>p[0]),ys=hole.map(p=>p[1]);const width=Math.max(...xs)-Math.min(...xs),height=Math.max(...ys)-Math.min(...ys);if(part.role==='Connection')assert.ok(width>50&&width<=120.001&&height<=12.001&&width/height>5);else assert.ok(width>40&&width<=80.001&&height>10&&height<=25.001);const originalArea=area(part.outline)-part.holes.filter((_,i)=>!part.windowHoleIndices.includes(i)).reduce((n,h)=>n+area(h),0);assert.ok(area(hole)/originalArea<.25,`${part.number} must retain most of its material`);}
  for(const part of supports){assert.ok(part.contactPoints.length>0);assert.ok(part.span[0]<-100&&part.span[1]>100);}
  assert.deepEqual(candidate.layoutSummary.supportPositions,recipe.stations.filter(s=>!s.connection).map(s=>s.coordinate));
  assert.deepEqual(candidate.layoutSummary.connectionPositions,recipe.stations.filter(s=>s.connection).map(s=>s.coordinate));
  const draft=await read();for(const key of ['workpieces','placements','fixtures','fixtureUnits'])assert.deepEqual(draft[key],before[key]);
  await page.screenshot({path:resolve(output,'flow-generated.png')});
  await command('five.fixture.design.confirm');await idle();assert.equal(await page.evaluate(()=>fixture.view.error),'');
  const committed=await read();assert.equal(committed.fixtureUnits.length,1);assert.equal(committed.fixtures.length,9);assert.deepEqual(committed.workpieces[0].geometryRef,before.workpieces[0].geometryRef);assert.deepEqual(committed.placements,before.placements);
  const manufacture=async(method,extra={})=>{const state=await read();return rpc({action:'invoke',scope:'scene',method,payload:{commandId:randomUUID(),expected:state.expected,setupId:state.activeSetupId,unitId:state.fixtureUnits[0].id,...extra}});};
  await manufacture('CamFixture.Breakdown');mkdirSync(resolve(output,'manufacturing'),{recursive:true});const exported=await manufacture('CamFixture.Export',{destination:resolve(output,'manufacturing')});
  const manifest=JSON.parse(readFileSync(resolve(exported.directory,'manifest.json'),'utf8'));assert.equal(manifest.parts.length,9);
  for(const part of candidate.parts){const dxf=readFileSync(resolve(exported.directory,part.number+'.dxf'),'utf8');assert.match(dxf,/CUT_OUTER/);assert.match(dxf,/CUT_INNER/);}
  // Breakdown updates the saved manufacturing state. Compare reopening with
  // that current state, and keep large scene diffs out of assertion formatting.
  const saved=await read(),reopened=await rpc({action:'save-reopen'});
  for(const key of ['fixtureUnits','fixtures']){
    if(!isDeepStrictEqual(reopened[key],saved[key]))writeFileSync(resolve(output,`reopen-${key}.json`),JSON.stringify({saved:saved[key],reopened:reopened[key]}));
    assert.ok(isDeepStrictEqual(reopened[key],saved[key]),`Saved ${key} must survive reopening unchanged`);
  }
  await rpc({action:'export-project',path:resolve(output,'HatRail-flow-openings.i5cam')});
  await page.screenshot({path:resolve(output,'flow-adopted.png')});
  return {cases:['Default count layout opens flow holes in every transverse and longitudinal plate while retaining all 6 + 2 + 1 parts and fixed positions','The solid base outline, original mortises, frame, thickness and joints remain exactly unchanged when ventilation is enabled','The actual HatRail keeps both sides supported, source CAD and placements unchanged; native save/reopen and manufacturing DXFs retain the through-openings'],generationMs,supportCount:6,connectionCount:2,ventCount:[...supports,...connections].reduce((n,p)=>n+p.windowHoleIndices.length,0),baseUnchanged:true,baseVentCount:base.windowHoleIndices.length,manufacturingDirectory:exported.directory};
}
