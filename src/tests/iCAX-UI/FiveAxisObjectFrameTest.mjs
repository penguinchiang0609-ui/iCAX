import assert from 'node:assert/strict';
import {identityFrame,turnFrame,frameTransform,frameVector,editFrameAxis,frameInReference,frameReferenceVector} from '../../apps/five-axis-cam/webpage/workpieceFrame.mjs';
const dot=(a,b)=>a.reduce((s,n,i)=>s+n*b[i],0),near=(a,b)=>a.forEach((n,i)=>assert.ok(Math.abs(n-b[i])<1e-8));
let frame=identityFrame();frame.origin=[12,-8,31];
for(let i=0;i<1500;++i){
  const axis=['x','y','z'][i%3],direction=i%5===0?frame[axis].map(n=>-n):[Math.sin(i+.2),Math.cos(i*1.31),Math.sin(i*.73+.8)];
  const before=structuredClone(frame);frame=turnFrame(frame,axis,direction);
  near(frame[axis],direction.map(n=>n/Math.hypot(...direction)));near(frame.origin,before.origin);near(before.origin,[12,-8,31]);
  for(const a of ['x','y','z'])assert.ok(Math.abs(dot(frame[a],frame[a])-1)<1e-8);
  for(const [a,b] of [['x','y'],['x','z'],['y','z']])assert.ok(Math.abs(dot(frame[a],frame[b]))<1e-8);
  const [x,y,z]=[frame.x,frame.y,frame.z];near([x[1]*y[2]-x[2]*y[1],x[2]*y[0]-x[0]*y[2],x[0]*y[1]-x[1]*y[0]],z);
  const matrix=frameTransform(frame),map=(p,direction=false)=>[0,1,2].map(r=>dot(matrix.slice(r*4,r*4+3),p)+(direction?0:matrix[r*4+3]));
  near(map(frame.origin),[0,0,0]);near(map(frame.x,true),[1,0,0]);near(map(frame.y,true),[0,1,0]);near(map(frame.z,true),[0,0,1]);
}
assert.throws(()=>turnFrame(frame,'x',[0,0,0]));assert.throws(()=>turnFrame(frame,'y',[Infinity,1,0]));
for(const v of [['',0,1],['1e309',0,1],['abc',0,1]])assert.throws(()=>frameVector(v));
console.log('Object frame: 1500 linked edits and reversals preserve origin, orthonormality, handedness and inverse mapping');
for(const first of ['x','z']){
  const second=first==='x'?'z':'x';let f=editFrameAxis(identityFrame(),first,[1,2,3]);
  const fixed=[...f[first]];f=editFrameAxis(f,second,[3,2,1],first);near(f[first],fixed);
  assert.ok(Math.abs(dot(f.x,f.z))<1e-10);near([f.z[1]*f.x[2]-f.z[2]*f.x[1],f.z[2]*f.x[0]-f.z[0]*f.x[2],f.z[0]*f.x[1]-f.z[1]*f.x[0]],f.y);
  assert.throws(()=>editFrameAxis(f,second,f[first],first),/不能平行/);
  const flipped=editFrameAxis(f,second,f[second].map(n=>-n),first);near(flipped[first],fixed);near(flipped[second],f[second].map(n=>-n));
  f.origin=[4,-5,6];const next=editFrameAxis(f,first,[2,-1,3],first);next.origin=[7,8,-9];
  const relative=frameInReference(next,f),before=frameTransform(f),after=frameTransform(next),delta=frameTransform(relative);
  const map=(m,p)=>[0,1,2].map(r=>dot(m.slice(r*4,r*4+3),p)+m[r*4+3]);
  for(const p of [[0,0,0],[8,-1,42],[-1,20,-5]])near(map(delta,map(before,p)),map(after,p));
  near(frameReferenceVector(relative.origin,f,true),next.origin);
  for(const a of ['x','y','z'])near(frameReferenceVector(relative[a],f),next[a]);
  assert.throws(()=>editFrameAxis(f,'y',[1,0,0]),/自动计算/);
}
console.log('Two-axis edits preserve the first direction, derive right-handed Y, reject parallel axes and compose successive native frame saves');
