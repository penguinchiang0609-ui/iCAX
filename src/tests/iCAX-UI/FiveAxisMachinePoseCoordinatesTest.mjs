import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {machinePoseFrame,machinePoseValues,machinePoseTarget} from '../../apps/five-axis-cam/webpage/machinePoseCoordinates.mjs';

const pose=(position,angles)=>new THREE.Matrix4().compose(new THREE.Vector3(...position),new THREE.Quaternion().setFromEuler(new THREE.Euler(...angles,'ZYX')),new THREE.Vector3(1,1,1));
const near=(actual,expected,tolerance=1e-10)=>actual.forEach((value,index)=>assert.ok(Math.abs(value-expected[index])<tolerance,`${index}: ${value} vs ${expected[index]}`));
function fixture(){
 const m={entityId:'machine',elements:[{entityId:'machine',parentEntityId:''},{entityId:'parent',parentEntityId:'machine'},{entityId:'tip',parentEntityId:'parent'}]};
 const view={viewport:{sceneObjects:new Map(),transformPreviews:new Map(),transformPayloads:new Map()}};
 return {view,m};
}

test('world coordinates use identity while local coordinates follow the physical parent, not the render container',()=>{
 const {view,m}=fixture(),parent=new THREE.Object3D(),container=new THREE.Object3D();
 container.position.set(1000,-200,50);container.add(parent);parent.position.set(10,20,30);parent.rotation.set(.2,-.3,.4,'ZYX');parent.updateWorldMatrix(true,false);
 view.viewport.sceneObjects.set('parent',parent);
 const frame=machinePoseFrame(view,m,'tip','local');near(frame.elements,parent.matrixWorld.elements);
 near(machinePoseFrame(view,m,'tip','world').elements,new THREE.Matrix4().elements);
 frame.elements[12]+=100;assert.notEqual(frame.elements[12],parent.matrixWorld.elements[12],'callers receive an independent frame');
 near(machinePoseFrame(view,m,'machine','local').elements,new THREE.Matrix4().elements);
});

test('a translated and rotated parent frame round-trips local positions and ZYX angles without modifying any input',()=>{
 const frame=pose([130,-240,350],[.3,-.4,.5]),localValues=['12','-7','9','15','-20','35'];
 const originalFrame=[...frame.elements];Object.freeze(frame.elements);
 const world=machinePoseTarget(localValues,frame),originalWorld=[...world.elements];Object.freeze(world.elements);
 const values=machinePoseValues(world,frame);
 assert.ok(values.every(value=>typeof value==='string'));near(values.map(Number),localValues.map(Number));
 near(machinePoseTarget(values,frame).elements,world.elements);assert.deepEqual(frame.elements,originalFrame);assert.deepEqual(world.elements,originalWorld);assert.deepEqual(localValues,['12','-7','9','15','-20','35']);
 near(machinePoseTarget(machinePoseValues(world)).elements,world.elements);
});

test('geometry-free parents use accepted preview or transform payload world frames',()=>{
 const {view,m}=fixture(),persisted=pose([100,200,300],[0,.4,0]),accepted=pose([120,230,340],[.2,0,.3]);
 view.viewport.transformPayloads.set('parent',{localToWorld:[...persisted.elements]});
 near(machinePoseFrame(view,m,'tip','local').elements,persisted.elements);
 view.viewport.transformPreviews.set('parent',{localToWorld:[...accepted.elements]});
 near(machinePoseFrame(view,m,'tip','local').elements,accepted.elements);
 const object=new THREE.Object3D();object.position.set(-100,-200,-300);object.updateWorldMatrix(true,false);view.viewport.sceneObjects.set('parent',object);
 near(machinePoseFrame(view,m,'tip','local').elements,accepted.elements);
 view.viewport.transformPreviews.delete('parent');near(machinePoseFrame(view,m,'tip','local').elements,object.matrixWorld.elements);
});

test('missing physical parent matrices never silently reinterpret local coordinates as world coordinates',()=>{
 const {view,m}=fixture();assert.equal(machinePoseFrame(view,m,'tip','local'),null);
 assert.equal(machinePoseFrame(view,m,'parent','local'),null,'a machine root with unknown world position is also unavailable');
 assert.equal(machinePoseFrame(view,m,'missing','local'),null);
 view.viewport.transformPayloads.set('parent',{localToWorld:[1,2,3]});assert.equal(machinePoseFrame(view,m,'tip','local'),null);
 view.viewport.transformPreviews.set('parent',{localToWorld:new Array(16).fill(NaN)});assert.equal(machinePoseFrame(view,m,'tip','local'),null);
 assert.throws(()=>machinePoseFrame(view,m,'tip','camera'),/Unknown transform space/);
});

test('invalid numeric pose input cannot produce a non-finite machine target',()=>{
 for(const values of [[],['1','2','3','0','0',''],['1','2','3','0','0','no'],[1,2,3,0,0,Infinity]])assert.throws(()=>machinePoseTarget(values),/六个有效数值/);
});
