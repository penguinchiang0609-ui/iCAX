import test from 'node:test';
import assert from 'node:assert/strict';
import {buildPlayback} from '../../apps/five-axis-cam/webpage/trajectoryPlayback.mjs';
import {collisionPathResults,collisionPathWarning,collisionPathStatus,collisionPathKey,selectedCollisionPath,collisionPathBadge} from '../../apps/five-axis-cam/webpage/collisionPathResults.mjs';
import {workspaceState} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';
import {renderCollisionCheck} from '../../apps/five-axis-cam/webpage/trajectorySimulation.mjs';
globalThis.cancelAnimationFrame??=()=>{};

function fixture(){
 const part=(role,points)=>({role,points,directions:points.map(()=>[0,0,1])});
 const setup={id:'s',rapidSettings:{followHeightMm:0},generatedGeometry:{status:'Current',operations:[
  {intentId:'a',order:1,placementId:'p',parts:[part('LeadIn',[[0,0,0],[1,0,0]]),part('Cut',[[1,0,0],[2,0,0]])]},
  {intentId:'b',order:2,placementId:'p',parts:[part('Cut',[[3,0,0],[4,0,0]])]}
 ],transitions:[{from:'a',to:'b',geometryStatus:'Generated',frame:'Setup',referencePoint:'ToolTip',points:[[2,0,0],[3,0,0]],directions:[[0,0,1],[0,0,1]]}]}},placements=[{id:'p',transform:[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]}];
 const frames=buildPlayback(setup,placements).frames;
 setup.simulationCheck={validity:'Current',frames:frames.map(f=>({position:f.position.toArray(),rotation:f.rotation.toArray()})),segments:[
  {index:'0',status:'Collision',fraction:.3},{index:'2',status:'Collision',fraction:.5},{index:'4',status:'Collision',fraction:.7},{index:'6',status:'Unchecked'}
 ]};
 const view={fiveAxisArea:'toolpath',fiveCamPage:'cutting',fiveCamInspector:'collision',scene:{intentToolpaths:[{id:'a',name:'A'},{id:'b',name:'B'}],domain:{activeSetupId:'s',setups:[setup],placements}}};
 workspaceState(view).selectedIntentId='a';return {view,setup};
}
test('cut including lead-in and rapid results belong to separate paths, even at identical endpoints',()=>{
 const {view}=fixture(),r=collisionPathResults(view);
 assert.deepEqual([...r.paths].map(([key,value])=>[key,value.map(s=>Number(s.index))]),[['cut:a',[0,2]],['rapid:a/b',[4]],['cut:b',[6]]]);
 assert.equal(collisionPathKey(r.frames[4]),'rapid:a/b');assert.match(collisionPathWarning(r,'cut:a'),/有碰撞：2 段/);
 assert.match(collisionPathWarning(r,'cut:b'),/未验证/);assert.equal(collisionPathWarning(r,'cut:missing'),'');
});
test('rapid status follows its own current collision and limit report and requires complete evidence for clear',()=>{
 const {view,setup}=fixture();assert.equal(collisionPathStatus(collisionPathResults(view),'rapid:a/b'),'检测到碰撞');
 setup.simulationCheck.segments[2]={index:'4',status:'Unchecked',reason:'CAM_HEAD_LIMIT_NO_SOLUTION'};assert.equal(collisionPathStatus(collisionPathResults(view),'rapid:a/b'),'超出切割头限位');
 setup.simulationCheck.segments[2]={index:'4',status:'Clear'};assert.equal(collisionPathStatus(collisionPathResults(view),'rapid:a/b'),'碰撞与限位检测通过');
 // Index 5 connects this rapid endpoint to the next cut; it is not rapid geometry.
 setup.simulationCheck.segments.push({index:'5',status:'Clear'});assert.equal(collisionPathStatus(collisionPathResults(view),'rapid:a/b'),'碰撞与限位检测通过');
 setup.simulationCheck.validity='Stale';assert.equal(collisionPathStatus(collisionPathResults(view),'rapid:a/b'),'');
});
test('stale reports and reports from different trajectories never mark either list',()=>{
 const {view,setup}=fixture();setup.simulationCheck.validity='Stale';assert.equal(collisionPathResults(view).paths.size,0);
 setup.simulationCheck.validity='Current';setup.simulationCheck.frames[4].position[0]+=1;assert.equal(collisionPathResults(view).paths.size,0);
});
test('green clearance requires a complete current path report; failures stay yellow and stale paths are untested',()=>{
 const {view,setup}=fixture();
 assert.match(collisionPathBadge(collisionPathResults(view),'rapid:a/b'),/data-five-collision-warning/);
 setup.simulationCheck.segments[2]={index:'4',status:'Clear'};
 assert.match(collisionPathBadge(collisionPathResults(view),'rapid:a/b'),/data-five-collision-pass/);
 assert.doesNotMatch(collisionPathBadge(collisionPathResults(view),'cut:a'),/data-five-collision-pass/);
 setup.simulationCheck.validity='Stale';
 assert.match(collisionPathBadge(collisionPathResults(view),'rapid:a/b'),/未检测/);
 assert.doesNotMatch(collisionPathBadge(collisionPathResults(view),'rapid:a/b'),/data-five-collision-pass/);
});
test('the inspector filters cut and rapid records and moves to the newly selected path without a dialog',()=>{
 const {view}=fixture();let html=renderCollisionCheck(view);
 assert.match(html,/collision-result:0/);assert.match(html,/collision-result:2/);assert.doesNotMatch(html,/collision-result:4|collision-result:6|role="dialog"|data-five-floating/);
 assert.equal(view.fiveSimulation.cursor,.3);
 view.fiveCamPage='rapid';view.fiveRapid={id:'a/b'};html=renderCollisionCheck(view);
 assert.equal(selectedCollisionPath(view),'rapid:a/b');assert.match(html,/collision-result:4/);assert.doesNotMatch(html,/collision-result:0|collision-result:2|collision-result:6/);assert.equal(view.fiveSimulation.cursor,4.7);
 view.fiveRapid.id='missing/pair';html=renderCollisionCheck(view);assert.doesNotMatch(html,/collision-result:/);assert.equal(view.fiveSimulation.collisionPathHasFrame,false);
});

test('global progress is labelled as all paths and remains cancellable without a selected path',()=>{
 const {view}=fixture();workspaceState(view).selectedIntentId='';renderCollisionCheck(view);
 Object.assign(view.fiveSimulation,{checking:true,checkScope:'AllPaths'});view.pending=true;
 const html=renderCollisionCheck(view);assert.match(html,/当前装夹全部刀路/);
 const cancel=html.match(/<button[^>]*data-five-command="five.simulation.collision-cancel"[^>]*>/)?.[0];
 assert(cancel);assert.doesNotMatch(cancel,/disabled/);
});
