import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {machineAxisTarget,machineAxisValue} from '../../apps/five-axis-cam/webpage/machineAxisTarget.mjs';

const near=(actual,expected,tolerance=1e-10)=>{assert.equal(actual.length,expected.length);actual.forEach((value,i)=>assert.ok(Math.abs(value-expected[i])<tolerance,`${i}: ${value} != ${expected[i]}`));};
const base=()=>new THREE.Matrix4().makeRotationZ(Math.PI/2).setPosition(10,20,30);
const point=(matrix,values)=>new THREE.Vector3(...values).applyMatrix4(matrix).toArray();

test('translation uses normalized world direction without rotating it by the existing pose',()=>{
 const original=base(),target=machineAxisTarget(original,[3,0,0],'translate','4.5');
 near(point(target,[0,0,0]),[14.5,20,30]);
 near(point(target,[1,0,0]),[14.5,21,30]);
 near(machineAxisTarget(original,[0,2,0],'translate',-2).elements.slice(12,15),[10,18,30]);
 near(target.elements.slice(0,12),original.elements.slice(0,12));
});

test('rotation is in degrees about the selected part origin rather than the world origin',()=>{
 const target=machineAxisTarget(base(),[0,0,8],'rotate','90');
 near(point(target,[0,0,0]),[10,20,30]);
 near(point(target,[1,0,0]),[9,20,30]);
 near(point(target,[0,1,0]),[10,19,30]);
});

test('world rotation premultiplies the nonzero base rotation',()=>{
 const target=machineAxisTarget(base(),[0,2,0],'rotate',90);
 near(point(target,[0,0,0]),[10,20,30]);
 near(point(target,[1,0,0]),[10,21,30]);
 near(point(target,[0,1,0]),[10,20,31]);
 near(point(target,[0,0,1]),[11,20,30]);
});

test('base matrix and frozen direction stay unchanged and zero is a valid independent target',()=>{
 const original=base(),elements=[...original.elements],direction=Object.freeze([0,0,2]);Object.freeze(original.elements);
 const target=machineAxisTarget(original,direction,'rotate',45);
 assert.notEqual(target,original);assert.deepEqual(original.elements,elements);assert.deepEqual(direction,[0,0,2]);
 for(const mode of ['translate','rotate']){const unchanged=machineAxisTarget(original,direction,mode,0);assert.notEqual(unchanged,original);near(unchanged.elements,elements);}
});

test('normalizing finite nonzero directions does not overflow or underflow',()=>{
 near(machineAxisTarget(base(),[Number.MIN_VALUE,0,0],'translate',2).elements.slice(12,15),[12,20,30]);
 const target=machineAxisTarget(base(),[Number.MAX_VALUE,Number.MAX_VALUE,Number.MAX_VALUE],'translate',Math.sqrt(3));
 near(target.elements.slice(12,15),[11,21,31]);
});

test('empty and non-finite amounts cannot become native target matrices',()=>{
 for(const amount of ['', '  ',null,undefined,NaN,Infinity,-Infinity,'Infinity','not-a-number',true,{}])assert.throws(()=>machineAxisTarget(base(),[1,0,0],'translate',amount),/有效的位移或角度/);
});

test('invalid and zero directions are rejected without changing the source pose',()=>{
 const original=base(),before=[...original.elements];
 for(const direction of [[],[0,0],[0,0,0],[NaN,0,1],[Infinity,0,0],['1',0,0],null])assert.throws(()=>machineAxisTarget(original,direction,'rotate',1),/运动轴方向无效/);
 assert.deepEqual(original.elements,before);
});

test('invalid modes, invalid base poses and overflow fail before returning a target',()=>{
 assert.throws(()=>machineAxisTarget(base(),[1,0,0],'scale',1),/变换类型/);
 assert.throws(()=>machineAxisTarget(null,[1,0,0],'translate',1),/部件位姿无效/);
 const invalid=base();invalid.elements[0]=NaN;assert.throws(()=>machineAxisTarget(invalid,[1,0,0],'translate',1),/部件位姿无效/);
 const huge=base().setPosition(Number.MAX_VALUE,0,0);assert.throws(()=>machineAxisTarget(huge,[1,0,0],'translate',Number.MAX_VALUE),/数值超出范围/);
});

test('accepted motion reports actual displacement and signed rotation in the captured world axis',()=>{
 const original=base();
 assert.equal(machineAxisValue(original,[0,1,0],'translate',new THREE.Matrix4().copy(original).setPosition(10,24,33),1000),4);
 for(const [axis,degrees,reference]of [[[0,0,1],35,90],[[0,1,0],-20,-90],[[0,0,-1],170,200],[[0,0,1],450,450]]){
  const actual=machineAxisTarget(original,axis,'rotate',degrees);
  assert.ok(Math.abs(machineAxisValue(original,axis,'rotate',actual,reference)-degrees)<1e-8);
 }
});
