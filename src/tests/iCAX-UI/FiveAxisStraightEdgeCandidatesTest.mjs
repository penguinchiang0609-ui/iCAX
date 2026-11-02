import assert from 'node:assert/strict';
import * as T from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {SnapFeatureScene} from '../../apps/_shared/workbench/viewport/snapFeatureScene.mjs';
import {clipScreenSegment} from '../../apps/_shared/workbench/viewport/screenSegment.mjs';

const rect={left:0,top:0,width:100,height:100},p=(x,y,z,w=1)=>({x,y,z,w});
assert.deepEqual(clipScreenSegment(p(-4,0,0),p(4,0,0),rect),[{x:0,y:50,z:0},{x:100,y:50,z:0}]);
assert.deepEqual(clipScreenSegment(p(0,0,-4),p(0,0,0),rect),[{x:50,y:50,z:-1},{x:50,y:50,z:0}]);
const offscreenMidpoint=clipScreenSegment(p(-.5,0,0),p(.5,0,8),rect);
assert.ok(offscreenMidpoint);assert.deepEqual(offscreenMidpoint,[{x:25,y:50,z:0},{x:31.25,y:50,z:1}]);
assert.equal(clipScreenSegment(p(-4,0,0),p(-3,0,0),rect),null);
const camera=new T.PerspectiveCamera(60,1,1,100),behind=new T.Vector4(-2,0,1,1).applyMatrix4(camera.projectionMatrix),front=new T.Vector4(2,0,-10,1).applyMatrix4(camera.projectionMatrix);
const perspective=clipScreenSegment(behind,front,rect);assert.ok(perspective);assert.ok(perspective.flatMap(v=>Object.values(v)).every(Number.isFinite));assert.ok(perspective.every(v=>v.x>=0&&v.x<=100&&v.y>=0&&v.y<=100&&v.z>=-1&&v.z<=1));

const line=(id,half)=>({featureId:id,mode:'line',point:[0,0,0],direction:[1,0,0],edgeSamples:[[-half,0,0],[half,0,0]],faceIds:[],edgeId:id});
const short=line('short',1),long=line('long',1000),reverse={...long,featureId:'same-span-reversed',direction:[-1,0,0],edgeSamples:[...long.edgeSamples].reverse()};
const object=new T.Mesh(new T.BoxGeometry(1,1,1),new T.MeshBasicMaterial()),layer=new SnapFeatureScene(object,{features:[short,long,reverse],faces:[],geometryRef:{}},{inference:true,candidateDisplay:'directions',allowedModes:['line']});
assert.deepEqual(layer.records.map(r=>r.feature.featureId),['short','long'],'Collinear edges with the same midpoint but different extents both stay visible');
assert.deepEqual(layer.records[1].ends.map(p=>p.toArray()),[[-1000,0,0],[1000,0,0]]);
layer.dispose();object.geometry.dispose();object.material.dispose();
console.log('Straight-edge candidates preserve different spans and clip full segments across the viewport and near/far planes');
