import assert from 'node:assert/strict';
import test from 'node:test';
import {designDraft,designCommand} from '../../apps/five-axis-cam/webpage/fixtureDesign.mjs';

test('editing a saved alternative strategy marks only the draft stale and preserves saved geometry',()=>{
  const recipe={layout:'Manual',pattern:'Grid',spacing:175,thickness:4,supportFaces:['7'],stations:[{id:'station',coordinate:30}]};
  const saved=structuredClone(recipe),assembly={id:'existing-assembly',parts:[{partId:'existing-plate'}]};
  const view={fiveFixtureDesignOpen:true,scene:{domain:{activeSetupId:'s',setups:[{id:'s',fixtureDesign:{recipe,assembly}}]}}};
  const d=designDraft(view);
  assert.equal(d.layout,'Automatic');assert.equal(d.pattern,'Skeleton');assert.equal(d.dirty,true);
  assert.equal(d.spacing,175);assert.equal(d.thickness,4);assert.deepEqual(d.supportFaces,['7']);
  assert.deepEqual(recipe,saved);assert.equal(view.scene.domain.setups[0].fixtureDesign.assembly,assembly);
});

test('generation always submits skeleton strategy and lets native recompute stations while retaining edited dimensions',async()=>{
  const calls=[],view={fiveFixtureDesignOpen:true,scene:{domain:{activeSetupId:'s',expected:{},setups:[{id:'s',fixtureDesign:{}}]}}};
  const d=designDraft(view);Object.assign(d,{layout:'Manual',pattern:'Staggered',positions:'invalid legacy positions',spacing:'invalid obsolete spacing',supportCount:'6',connectionCount:'2',thickness:'4',supportFaces:['7']});
  const ctx={isCurrentProject:()=>true,sceneProxy:{async invoke(method,payload){calls.push({method,payload});return {status:'Completed',result:{design:{recipe:payload.recipe}}};}}};
  await designCommand(ctx,view,'generate',{renderProject(){}});
  const r=calls[0].payload.recipe;
  assert.equal(calls[0].method,'CamFixture.Generate');assert.equal(r.layout,'Automatic');assert.equal(r.pattern,'Skeleton');assert.deepEqual(r.stations,[]);
  assert.equal(r.spacing,undefined);assert.equal(r.supportCount,6);assert.equal(r.connectionCount,2);assert.equal(r.thickness,4);assert.equal(r.baseThickness,5);assert.equal(r.tenonRadius,1);assert.equal(r.windowMode,'Automatic');assert.equal(r.windowWeb,10);
});
test('invalid counts and mismatched fixed positions never start generation',async()=>{
  for(const [count,positions] of [['1',null],['2.5',null],['33',null],['6','1, 2']]){
    const view={fiveFixtureDesignOpen:true,scene:{domain:{activeSetupId:'s',expected:{},setups:[{id:'s',fixtureDesign:{}}]}}};
    Object.assign(designDraft(view),{supportCount:count,connectionCount:'2',supportFaces:['7'],...(positions?{positionMode:'Manual',supportPositionText:positions,connectionPositionText:'-50, 50'}:{})});
    let calls=0;await assert.rejects(designCommand({sceneProxy:{invoke(){calls++;}}},view,'generate',{renderProject(){}}),/数量|坐标/);assert.equal(calls,0);
  }
});
test('a late position calculation cannot overwrite counts typed while it was being calculated',async()=>{
  const view={fiveFixtureDesignOpen:true,scene:{domain:{activeSetupId:'s',expected:{},setups:[{id:'s',fixtureDesign:{}}]}}};
  Object.assign(designDraft(view),{supportFaces:['7']});let release;
  const ctx={isCurrentProject:()=>true,sceneProxy:{invoke(){return new Promise(r=>{release=r;});}}};
  const request=designCommand(ctx,view,'layout-positions',{renderProject(){}});
  const {designChanged}=await import('../../apps/five-axis-cam/webpage/fixtureDesign.mjs');designChanged(view,'supportCount','8');
  release({supportCount:6,connectionCount:2,axis:'X',supportPositions:[],connectionPositions:[]});await request;
  assert.equal(designDraft(view).supportCount,'8');
});
