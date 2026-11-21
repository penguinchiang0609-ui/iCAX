import assert from 'node:assert/strict';
import { buildPlayback, playbackPose, advancePlayback } from '../../apps/five-axis-cam/webpage/trajectoryPlayback.mjs';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { paintSimulationTrace, clearSimulationTrace, disposeSimulationTrace } from '../../apps/five-axis-cam/webpage/simulationTrace.mjs';
const transform=[0,0,1,10,0,1,0,20,-1,0,0,30,0,0,0,1];
const part=points=>({role:'Cut',points,directions:points.map(()=>[0,0,1])});
const source={rapidSettings:{followHeightMm:2},generatedGeometry:{status:'Current',operations:[
  {intentId:'a',placementId:'p',order:'1',parts:[part([[0,0,0],[1,0,0]])]},
  {intentId:'b',placementId:'p',order:'2',parts:[part([[2,0,0],[3,0,0]])]},
],transitions:[{from:'a',to:'b',frame:'Setup',referencePoint:'ToolTip',points:[[12,20,29],[22,20,29],[22,20,28],[12,20,28]],directions:Array(4).fill([1,0,0]),geometryStatus:'Generated',validation:'Unchecked'}]}};
const {frames}=buildPlayback(source,[{id:'p',transform}]);
assert.deepEqual(frames[0].position.toArray(),[12,20,30]);
assert.deepEqual(frames[0].target.toArray(),[10,20,30]);
assert.equal(frames[2].target,null);
assert.deepEqual(frames[2].position.toArray(),[12,20,29]);
assert.deepEqual(frames[5].position.toArray(),frames[6].position.toArray());
assert.equal(frames.some(f=>f.breakBefore),false);
const changedEndpoint=structuredClone(source);changedEndpoint.generatedGeometry.transitions[0].directions[0]=[0,0,1];
const separate=buildPlayback(changedEndpoint,[{id:'p',transform}]).frames;
assert.equal(separate[2].breakBefore,true,'an independently edited rapid endpoint must not create an unmodelled rotary interpolation from the cut');
assert.equal(advancePlayback(separate,0,1000).cursor,1);assert.match(advancePlayback(separate,0,1000).reason,/端点姿态不连续/);
assert.deepEqual(playbackPose(separate,1.5).position.toArray(),separate[1].position.toArray());
assert.equal(frames[2].laser,false);assert.equal(frames[6].laser,true);
assert.deepEqual(playbackPose(frames,2.5).position.toArray(),[17,20,29]);
assert.ok(Math.abs(playbackPose(frames,2.5).rotation.length()-1)<1e-9);
const almost=advancePlayback(frames,2,frames[3].duration/2);assert.ok(Math.abs(almost.cursor-2.5)<1e-9);
assert.equal(advancePlayback(frames,0,1000).cursor,frames.length-1);
frames[2].collision={status:'Collision',fraction:.4};
assert.equal(advancePlayback(frames,0,1000).cursor,frames.length-1,'stored continuous reports never stop discrete playback between displayed poses');
assert.equal(advancePlayback(frames,0,1000).stopped,true);
assert.equal(advancePlayback(frames,2.1,frames[3].duration*.1).stopped,false);
assert.ok(advancePlayback(frames,2.4,1).cursor>2.4);
frames[2].collision={status:'Unchecked'};assert.equal(advancePlayback(frames,0,1000).cursor,frames.length-1);
delete frames[2].collision;
assert.equal(advancePlayback(frames,0,1000).waiting,undefined,'there is no native detection frontier to wait for');
const missing=structuredClone(source);missing.generatedGeometry.transitions=[];
const gap=buildPlayback(missing,[{id:'p',transform}]);assert.equal(gap.frames[2].breakBefore,true);
assert.equal(advancePlayback(gap.frames,0,1000).cursor,1);assert.ok(advancePlayback(gap.frames,0,1000).reason);
assert.deepEqual(playbackPose(gap.frames,1.5).position.toArray(),gap.frames[1].position.toArray());
const stale=structuredClone(source);stale.generatedGeometry.status='Stale';assert.equal(buildPlayback(stale,[]).frames.length,0);
const manual=structuredClone(source);manual.generatedGeometry.transitions[0].geometryStatus='Draft';
const manualPlayback=buildPlayback(manual,[{id:'p',transform}]);
assert.deepEqual(manualPlayback.frames.map(f=>f.position.toArray()),frames.map(f=>f.position.toArray()));
assert.equal(manualPlayback.frames.filter(f=>f.role==='Rapid').length,4,'authored draft geometry remains available for real detection and free-pose repair');
assert.equal(manualPlayback.frames.find(f=>f.role==='Rapid').validation,'Unchecked','displaying authored geometry does not certify its safety');
for(const status of ['Missing','Stale','Failed']){
  const blocked=structuredClone(source);blocked.generatedGeometry.transitions[0].geometryStatus=status;
  const playback=buildPlayback(blocked,[{id:'p',transform}]);
  assert.equal(playback.frames.some(f=>f.role==='Rapid'),false);
  assert.equal(advancePlayback(playback.frames,0,1000).cursor,1);
  const reason=advancePlayback(playback.frames,0,1000).reason;
  if(status==='Failed'){assert.match(reason,/未通过避障检查.*空移.*编辑.*候选路径/);assert.doesNotMatch(reason,/尚未生成/);assert.match(playback.reason,/编辑候选路径/);}
  else assert.match(reason,/生成/);
}
for(const status of ['Clear','Collision','Unchecked']){
  const checked=structuredClone(source);checked.generatedGeometry.transitions[0].validation=status;
  assert.equal(buildPlayback(checked,[{id:'p',transform}]).frames.some(f=>f.role==='Rapid'),true,'collision results do not replace or remove existing geometry');
}
const invalid=structuredClone(source);invalid.generatedGeometry.operations[0].parts[0].directions[0]=[0,0,0];assert.throws(()=>buildPlayback(invalid,[{id:'p',transform}]));
const view={viewport:{scene:new THREE.Scene()}},s={frames:buildPlayback(source,[{id:'p',transform}]).frames,cursor:.5};
paintSimulationTrace(view,s);
const line=s.trace.line,buffer=line.geometry.getAttribute('position');
const vertex=n=>new THREE.Vector3().fromBufferAttribute(buffer,n).toArray();
assert.equal(line.material.color.getHex(),0xff3030);assert.equal(line.geometry.drawRange.count,2);
assert.deepEqual(vertex(0),[10,20,30]);assert.deepEqual(vertex(1),[10,20,29.5]);
s.cursor=4.5;paintSimulationTrace(view,s);assert.equal(s.trace.line,line);assert.equal(line.geometry.drawRange.count,2,'rapid motion does not extend the cutting trail');
assert.equal(s.trace.rapidLine.material.color.getHex(),0xff3030);assert.equal(s.trace.rapidLine.material.isLineDashedMaterial,true);assert.ok(s.trace.rapidLine.geometry.drawRange.count>0);
assert.deepEqual(vertex(1),[10,20,29]);
s.cursor=6.4;paintSimulationTrace(view,s);assert.equal(line.geometry.drawRange.count,4);
clearSimulationTrace(view,s);assert.equal(line.geometry.drawRange.count,0);assert.equal(s.cursor,6.4);
s.cursor=6.8;paintSimulationTrace(view,s);assert.equal(line.geometry.drawRange.count,2);assert.equal(line.geometry.drawRange.start,2);
assert.ok(Math.abs(vertex(2)[2]-27.6)<1e-5);assert.ok(Math.abs(vertex(3)[2]-27.2)<1e-5);
s.cursor=3;paintSimulationTrace(view,s);assert.equal(line.geometry.drawRange.count,0,'rewinding before a cleared position cannot restore cleared traces');
s.traceStart=0;s.cursor=.25;paintSimulationTrace(view,s);assert.equal(line.geometry.drawRange.count,2);assert.deepEqual(vertex(1),[10,20,29.75]);
const disconnected={frames:gap.frames,cursor:gap.frames.length-1};paintSimulationTrace(view,disconnected);assert.equal(disconnected.trace.segments.length,2,'never join disconnected contours');
disposeSimulationTrace(disconnected);disposeSimulationTrace(s);assert.equal(view.viewport.scene.children.length,0);
console.log('Playback coordinates, TCP offsets, continuous interpolation, laser state, missing rapid and stale geometry passed.');
