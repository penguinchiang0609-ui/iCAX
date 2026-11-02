import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {paintSimulationTrace,clearSimulationTrace,disposeSimulationTrace} from '../../apps/five-axis-cam/webpage/simulationTrace.mjs';
import {paintCollision,disposeCollision} from '../../apps/five-axis-cam/webpage/simulationCollision.mjs';
import {sceneColors} from '../../apps/five-axis-cam/webpage/scenePalette.mjs';

function fixture(){
  const cut=(x,laser=true)=>({role:'Cut',intentId:'a',laser,target:new THREE.Vector3(x,0,0),position:new THREE.Vector3(x,0,5)});
  const rapid=(x,y,z)=>({role:'Rapid',intentId:'b',laser:false,target:null,position:new THREE.Vector3(x,y,z)});
  const frames=[cut(0),cut(10),cut(20,false),rapid(20,0,5),rapid(20,10,15),rapid(30,10,15),
    {...cut(30),intentId:'b',target:new THREE.Vector3(30,10,10),position:new THREE.Vector3(30,10,15)},
    {...cut(40,false),intentId:'b',target:new THREE.Vector3(40,10,10),position:new THREE.Vector3(40,10,15)}];
  return {view:{viewport:{scene:new THREE.Scene()}},s:{frames,cursor:0,traceStart:0}};
}
const vertices=track=>{const g=track.line.geometry,p=g.getAttribute('position'),r=g.drawRange;return Array.from({length:r.count},(_,i)=>new THREE.Vector3().fromBufferAttribute(p,r.start+i));};
const near=(a,b)=>assert.ok(a.distanceTo(b)<1e-5,`${a.toArray()} versus ${b.toArray()}`);

test('walked cuts are red surface paths and walked rapids are red dashed TCP paths',()=>{
  const {view,s}=fixture();s.cursor=4.25;paintSimulationTrace(view,s);const t=s.trace;
  assert.equal(t.line.material.color.getHex(),0xff3030);assert.equal(t.rapidLine.material.color.getHex(),0xff3030);
  assert.equal(t.rapidLine.material.isLineDashedMaterial,true);assert.equal(t.rapidLine.material.transparent,false);
  assert.deepEqual(t.segments,[0,1,6]);assert.deepEqual(t.rapidSegments,[3,4]);
  assert.equal(vertices(t).length,4);assert.ok(vertices(t).every(p=>p.z===0));
  near(vertices(t.rapid).at(-1),new THREE.Vector3(22.5,10,15));
  const cut=t.line,rapid=t.rapidLine;s.cursor=3.5;paintSimulationTrace(view,s);
  assert.equal(s.trace.line,cut);assert.equal(s.trace.rapidLine,rapid);assert.equal(vertices(t.rapid).length,2);
  near(vertices(t.rapid).at(-1),new THREE.Vector3(20,5,10));disposeSimulationTrace(s);assert.equal(view.viewport.scene.children.length,0);
});

test('clearing and resuming inside a rapid clips both endpoints and dashed distances',()=>{
  const {view,s}=fixture();s.cursor=3.25;paintSimulationTrace(view,s);clearSimulationTrace(view,s);
  assert.equal(s.trace.rapidLine.geometry.drawRange.count,0);s.cursor=3.75;paintSimulationTrace(view,s);
  const t=s.trace,points=vertices(t.rapid),distance=t.rapidLine.geometry.getAttribute('lineDistance'),offset=t.rapidLine.geometry.drawRange.start;
  assert.equal(t.line.geometry.drawRange.count,0);assert.equal(points.length,2);
  near(points[0],new THREE.Vector3(20,2.5,7.5));near(points[1],new THREE.Vector3(20,7.5,12.5));
  assert.ok(Math.abs(distance.getX(offset+1)-distance.getX(offset)-points[0].distanceTo(points[1]))<1e-5);
});

test('discrete collision is immediately red, preserves rapid dashes and records the incoming segment at an integer endpoint',()=>{
  const {view,s}=fixture();s.cursor=4;s.frameContact={status:'Collision',cursor:4};paintSimulationTrace(view,s);
  const t=s.trace;assert.deepEqual([...t.contacts],[3]);assert.deepEqual(t.collisionRapid.segments,[3]);
  assert.equal(t.collisionRapid.line.material.color.getHex(),sceneColors.collision);assert.equal(t.collisionRapid.line.material.isLineDashedMaterial,true);
  assert.ok(t.collisionRapid.line.renderOrder>t.rapidLine.renderOrder);assert.equal(s.collisionReport,undefined);
  clearSimulationTrace(view,s);assert.deepEqual([...t.contacts],[]);assert.equal(t.collisionRapid.line.geometry.drawRange.count,0);
  s.cursor=0;s.frameContact={status:'Collision',cursor:0};paintSimulationTrace(view,s);assert.equal(t.collisionCut.line.geometry.drawRange.count,0);assert.equal(t.collisionRapid.line.geometry.drawRange.count,0);
});

test('native report changes repaint collisions at the same cursor without rebuilding the walked buffers',()=>{
  const {view,s}=fixture();s.cursor=4.5;paintSimulationTrace(view,s);const cut=s.trace.line,rapid=s.trace.rapidLine;
  s.collisionReport={validity:'Current',segments:[{index:'0',status:'Collision'},{index:'4',status:'Collision'}]};const report=JSON.stringify(s.collisionReport);paintSimulationTrace(view,s);
  assert.equal(s.trace.line,cut);assert.equal(s.trace.rapidLine,rapid);assert.equal(s.trace.collisionCut.line.geometry.drawRange.count,2);assert.equal(s.trace.collisionRapid.line.geometry.drawRange.count,2);
  assert.equal(JSON.stringify(s.collisionReport),report);s.collisionReport={validity:'Stale',segments:s.collisionReport.segments};paintSimulationTrace(view,s);
  assert.equal(s.trace.collisionCut.line.geometry.drawRange.count,0);assert.equal(s.trace.collisionRapid.line.geometry.drawRange.count,0);
});

test('missing connections are never colored or bridged, and a new trajectory releases every old line',()=>{
  const {view,s}=fixture();s.frames[4].breakBefore=true;s.cursor=4.5;paintSimulationTrace(view,s);assert.deepEqual(s.trace.rapidSegments,[4]);
  const old=[s.trace.line,s.trace.rapidLine,s.trace.collisionCut.line,s.trace.collisionRapid.line];let disposed=0;
  old.forEach(line=>line.geometry.addEventListener('dispose',()=>disposed++));s.frames=fixture().s.frames;paintSimulationTrace(view,s);
  assert.equal(disposed,4);assert.ok(old.every(line=>line.parent===null));assert.deepEqual([...s.trace.contacts],[]);
});

test('native collision overlays use cut targets and dashed rapid TCPs above both walked layers',()=>{
  const {view,s}=fixture();s.collisionReport={validity:'Current',segments:[{index:'0',status:'Collision'},{index:'3',status:'Collision'}]};paintCollision(view,s);
  const cut=s.collisionOverlay.getObjectByName('collision-Collision'),rapid=s.collisionOverlay.getObjectByName('collision-Collision-rapid');
  assert.equal(cut.material.color.getHex(),sceneColors.collision);assert.equal(rapid.material.color.getHex(),sceneColors.collision);assert.equal(rapid.material.isLineDashedMaterial,true);
  assert.deepEqual(Array.from(cut.geometry.getAttribute('position').array),[0,0,0,10,0,0]);assert.deepEqual(Array.from(rapid.geometry.getAttribute('position').array),[20,0,5,20,10,15]);
  assert.ok(cut.renderOrder>50&&rapid.renderOrder>50);disposeCollision(s);
});

test('collision rapid subsets reuse the walked and source path dash phase, including fractional endpoints',()=>{
  const {view,s}=fixture();s.cursor=4.5;s.collisionReport={validity:'Current',segments:[{index:'4',status:'Collision'}]};paintSimulationTrace(view,s);paintCollision(view,s);
  const t=s.trace,walked=t.rapidLine.geometry.getAttribute('lineDistance'),red=t.collisionRapid.line.geometry.getAttribute('lineDistance'),native=s.collisionOverlay.getObjectByName('collision-Collision-rapid').geometry.getAttribute('lineDistance');
  assert.ok(walked.getX(2)>0);assert.equal(red.getX(0),walked.getX(2));assert.equal(red.getX(1),walked.getX(3));assert.equal(native.getX(0),walked.getX(2));
  assert.ok(Math.abs(native.getX(1)-native.getX(0)-10)<1e-5);
});

test('a collision at the cleared cursor can be detected again after rewinding and stale context hits are removed',()=>{
  const {view,s}=fixture();s.cursor=.5;s.frameContact={status:'Collision',cursor:.5};paintSimulationTrace(view,s);assert.deepEqual([...s.trace.contacts],[0]);
  clearSimulationTrace(view,s);assert.deepEqual([...s.trace.contacts],[]);
  s.cursor=.1;s.frameContact={status:'Clear',cursor:.1};paintSimulationTrace(view,s);s.cursor=.5;s.frameContact={status:'Collision',cursor:.5};paintSimulationTrace(view,s);assert.deepEqual([...s.trace.contacts],[0]);
  const line=s.trace.line;s.traceContactKey='new head or obstacle';s.frameContact={status:'Clear',cursor:.5};paintSimulationTrace(view,s);assert.deepEqual([...s.trace.contacts],[]);assert.equal(s.trace.line,line);
});
