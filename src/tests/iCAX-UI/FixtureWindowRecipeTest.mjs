import assert from 'node:assert/strict';
import test from 'node:test';
import {designChanged, designCommand, designDraft, fixtureCutClearanceReady, renderFixtureCutClearance} from '../../apps/five-axis-cam/webpage/fixtureDesign.mjs';

function fixture(){
  const calls=[],setup={id:'setup',fixtureDesign:{}};
  const view={scene:{domain:{activeSetupId:'setup',expected:{},setups:[setup]}}};
  const ctx={isCurrentProject:()=>true,projectProxy:{state:{projectPath:'C:/fixture-window-test.i5cam'},async autosave(){calls.push('save');}},sceneProxy:{async invoke(method,payload){calls.push({method,payload});if(method==='CamFixture.SetRecipe'){setup.fixtureDesign.recipe=payload.recipe;return payload.recipe;}if(method==='CamFixture.Generate')return {status:'Completed'};throw Error(method);}}};
  const ops={renderProject(){},refreshSceneState(){},appendProjectLog(){}};
  designChanged(view,'supportFaces',['1']);
  return {view,ctx,ops,calls};
}

test('window recipe uses existing generation and acknowledged automatic saves',async()=>{
  const f=fixture();assert.equal(designDraft(f.view).windowMode,'Automatic');assert.equal(designDraft(f.view).windowWeb,8);
  designChanged(f.view,'windowMode','Automatic');designChanged(f.view,'windowWeb','16');
  await designCommand(f.ctx,f.view,'generate',f.ops);
  assert.equal(f.view.error,'');assert.equal(f.calls[0].method,'CamFixture.SetRecipe');
  assert.equal(f.calls[0].payload.recipe.windowMode,'Automatic');assert.equal(f.calls[0].payload.recipe.windowWeb,16);
  assert.equal(f.calls[1],'save');assert.equal(f.calls[2].method,'CamFixture.Generate');assert.equal(f.calls[3],'save');
});

test('hidden opening band keeps its draft and does not block a solid plate',async()=>{
  const f=fixture();designChanged(f.view,'windowMode','Automatic');designChanged(f.view,'windowWeb','');
  await assert.rejects(designCommand(f.ctx,f.view,'generate',f.ops),/有效的夹具尺寸/);assert.equal(f.calls.length,0);
  designChanged(f.view,'windowMode','None');await designCommand(f.ctx,f.view,'generate',f.ops);assert.equal(f.view.error,'');
  assert.equal(f.calls[0].payload.recipe.windowMode,'None');assert.equal(f.calls[0].payload.recipe.windowWeb,0);
  designChanged(f.view,'windowMode','Automatic');assert.equal(designDraft(f.view).windowWeb,'');
});

test('switching fixture units restores each saved window recipe',()=>{
  const f=fixture();f.view.scene.domain.fixtureUnits=[{id:'a',kind:'Generated',design:{recipe:{...designDraft(f.view),windowMode:'Automatic',windowWeb:12,stations:[]}}},{id:'b',kind:'Generated',design:{recipe:{...designDraft(f.view),windowMode:'None',windowWeb:22,stations:[]}}}];
  f.view.fixtureDesignDraft=null;f.view.fiveAxisFixtures={selected:'a'};assert.equal(designDraft(f.view).windowWeb,12);
  f.view.fiveAxisFixtures.selected='b';assert.equal(designDraft(f.view).windowMode,'None');assert.equal(designDraft(f.view).windowWeb,22);
  f.view.fiveAxisFixtures.selected='a';assert.equal(designDraft(f.view).windowMode,'Automatic');assert.equal(designDraft(f.view).windowWeb,12);
});

test('cutting clearance saves actual path inputs through the shared transaction and autosave route',async()=>{
  const f=fixture();designChanged(f.view,'cutClearanceMode','Toolpath');
  designChanged(f.view,'cutClearanceMm','2.5');designChanged(f.view,'beamRadiusMm','.6');
  await designCommand(f.ctx,f.view,'generate',f.ops);
  assert.deepEqual(f.calls.filter(c=>typeof c==='string'),['save','save']);
  const r=f.calls[0].payload.recipe;assert.equal(r.cutClearanceMode,'Toolpath');assert.equal(r.cutClearanceMm,2.5);assert.equal(r.beamRadiusMm,.6);
});

test('only active cutting clearance fields block generation and hidden drafts survive',async()=>{
  const f=fixture();designChanged(f.view,'cutClearanceMode','Toolpath');designChanged(f.view,'beamRadiusMm','');
  await assert.rejects(designCommand(f.ctx,f.view,'generate',f.ops),/有效的夹具尺寸/);assert.equal(f.calls.length,0);
  designChanged(f.view,'cutClearanceMode','None');await designCommand(f.ctx,f.view,'generate',f.ops);
  assert.equal(f.calls[0].payload.recipe.beamRadiusMm,0);designChanged(f.view,'cutClearanceMode','Toolpath');assert.equal(designDraft(f.view).beamRadiusMm,'');
});

test('stale or unchecked cutting clearance blocks adoption and delivery without claiming rapid clearance',()=>{
  const design={recipe:{cutClearanceMode:'Toolpath'},assembly:{cutClearance:{validity:'Stale',status:'Unchecked'}}};
  assert.equal(fixtureCutClearanceReady(design),false);assert.match(renderFixtureCutClearance(design.assembly),/已过期/);
  design.assembly.cutClearance={validity:'Current',status:'Clear',headScope:'BuiltinFrusta',operationCount:31,clearanceMm:2,beamRadiusMm:.5};
  assert.equal(fixtureCutClearanceReady(design),true);
  const html=renderFixtureCutClearance(design.assembly);assert.match(html,/空移需/);assert.match(html,/内置示意头/);
  design.assembly.cutClearance.status='Unchecked';assert.equal(fixtureCutClearanceReady(design),false);
});
