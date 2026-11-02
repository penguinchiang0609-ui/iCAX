import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {TransformManipulator} from '../../iCAX-UI/SDK/Viewport/transformManipulator.mjs';

test('a rejected gesture start consumes the handle press without pointer capture or proxy changes',()=>{
 let captures=0,downs=0,prevented=0,ends=0;
 const canvas={style:{},clientHeight:600,getBoundingClientRect:()=>({left:0,top:0,width:800,height:600}),focus(){},setPointerCapture(){captures++;},hasPointerCapture:()=>false,removeEventListener(){}};
 const viewport={scene:new THREE.Scene(),camera:new THREE.PerspectiveCamera(),renderer:{domElement:canvas}};
 const m=new TransformManipulator(viewport,()=>{});m.configure({mode:'translate',position:[2,3,4],onStart:()=>false,onEnd(){ends++;}});
 m.hover=()=>{m.control.axis='X';return true;};m.update=()=>{};m.control.pointerDown=()=>{downs++;};
 const event={button:0,clientX:10,clientY:20,pointerId:7,preventDefault(){prevented++;}};
 assert.equal(m.down(event),true);assert.equal(prevented,1);assert.equal(captures,0);assert.equal(downs,0);assert.equal(m.pointerId,undefined);assert.equal(m.start,undefined);assert.deepEqual(m.proxy.position.toArray(),[2,3,4]);assert.equal(m.up(event),false);assert.equal(ends,0);
 m.dispose();
});
