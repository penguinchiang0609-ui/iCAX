import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {TransformManipulator} from '../../iCAX-UI/SDK/Viewport/transformManipulator.mjs';
import {syncPlacementInteraction} from '../../apps/five-axis-cam/webpage/workpiecePlacement.mjs';

function fixture(t,owner){
 const captures=new Set(),previews=[],changes=[],ends=[];let clears=0;
 const canvas={style:{},clientWidth:800,clientHeight:600,closest:()=>null,getBoundingClientRect:()=>({left:0,top:0,width:800,height:600}),focus(){},setPointerCapture:id=>captures.add(id),hasPointerCapture:id=>captures.has(id),releasePointerCapture:id=>captures.delete(id),removeEventListener(){}};
 const camera=new THREE.PerspectiveCamera(45,800/600,.1,2000);camera.position.set(200,-250,200);camera.lookAt(0,0,0);camera.updateMatrixWorld(true);
 const viewport={scene:new THREE.Scene(),camera,renderer:{domElement:canvas},setCameraNavigationEnabled(){},clearTransformPreview:id=>previews.push(id),clearTransformManipulator(){clears++;const old=this.transformManipulator;this.transformManipulator=null;old?.dispose();}};
 let manipulator;manipulator=new TransformManipulator(viewport,()=>manipulator?.update());viewport.transformManipulator=manipulator;
 manipulator.configure({owner,mode:'translate',space:'world',position:[1,2,3],onChange:value=>changes.push(value),onEnd:value=>ends.push(value)});
 // The ownership regression starts after a real handle has been identified;
 // retain the actual TransformControls plane solver and capture lifecycle.
 manipulator.hover=()=>{manipulator.control.axis='X';return true;};
 const view={fiveAxisArea:'simulation',scene:{intentToolpaths:[],domain:{activeSetupId:'setup',setups:[{id:'setup',locked:true}],workpieces:[],placements:[]}},viewport};
 const context={isCurrentProject:()=>true},ops={renderProject(){}};
 const event=(x=420)=>({button:0,buttons:1,clientX:x,clientY:300,pointerId:7,preventDefault(){}});
 assert.equal(manipulator.down(event()),true);assert.equal(manipulator.pointerId,7);assert.equal(manipulator.control.dragging,true);assert.equal(captures.has(7),true);
 t.after(()=>{if(viewport.transformManipulator===manipulator)viewport.clearTransformManipulator();});
 return {view,context,ops,viewport,manipulator,captures,previews,changes,ends,event,get clears(){return clears;}};
}

test('locked workpiece refreshes preserve an active machine gizmo and its real pointer capture',t=>{
 for(const owner of [undefined,'machine-ik']){
  const f=fixture(t,owner),geometry=structuredClone(f.view.scene.domain);
  for(let i=0;i<3;i++)syncPlacementInteraction(f.context,f.view,f.ops);
  assert.equal(f.viewport.transformManipulator,f.manipulator);assert.equal(f.clears,0);assert.equal(f.manipulator.pointerId,7);assert.equal(f.manipulator.control.dragging,true);assert.equal(f.captures.has(7),true);
  f.manipulator.move(f.event(450));assert.ok(f.changes.length>0,'the original plane solver must still deliver movement after locked refresh');assert.ok(Math.abs(f.changes.at(-1).translation[0])>1e-6);
  f.manipulator.up(f.event(450));assert.equal(f.ends.length,1);assert.equal(f.ends[0].moved,true);assert.equal(f.captures.size,0);assert.deepEqual(f.view.scene.domain,geometry);
 }
});

test('a stale workpiece preview releases only its own preview when the shared gizmo belongs to a machine',t=>{
 const f=fixture(t,'machine-ik');f.view.fivePlacement={placementId:'workpiece-placement',previewViewport:f.viewport,gizmoViewport:f.viewport,previewKey:'stale'};
 syncPlacementInteraction(f.context,f.view,f.ops);
 assert.deepEqual(f.previews,['workpiece-placement']);assert.equal(f.viewport.transformManipulator,f.manipulator);assert.equal(f.clears,0);assert.equal(f.manipulator.pointerId,7);assert.equal(f.captures.has(7),true);assert.equal(f.view.fivePlacement.previewViewport,null);assert.equal(f.view.fivePlacement.gizmoViewport,null);
});

test('locking a workpiece still disposes its own gizmo and capture without committing a cancelled preview',t=>{
 const f=fixture(t,'workpiece-placement');f.view.fivePlacement={placementId:'workpiece-placement',previewViewport:f.viewport,gizmoViewport:f.viewport,previewKey:'shown'};
 syncPlacementInteraction(f.context,f.view,f.ops);
 assert.equal(f.viewport.transformManipulator,null);assert.equal(f.clears,1);assert.deepEqual(f.previews,['workpiece-placement']);assert.equal(f.captures.size,0);assert.equal(f.manipulator.pointerId,null);assert.equal(f.manipulator.control.dragging,false);assert.equal(f.ends.length,0);assert.equal(f.view.fivePlacement.previewKey,'');
});
