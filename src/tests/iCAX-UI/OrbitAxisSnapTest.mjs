import assert from 'node:assert/strict';
import { OrbitAxisSnap } from '../../iCAX-UI/SDK/Viewport/orbitAxisSnap.mjs';

const run=(moves)=>{const snap=new OrbitAxisSnap(),out=moves.map(([x,y])=>snap.move(x,y));return {snap,out,x:out.reduce((s,p)=>s+p.dx,0),y:out.reduce((s,p)=>s+p.dy,0)};};
for(const sign of [-1,1])for(const steps of [1,12,120]){
  const h=run(Array.from({length:steps},()=>[120*sign/steps,8/steps]));
  assert.equal(h.snap.axis,'horizontal');assert.ok(Math.abs(h.x-120*sign)<1e-9);assert.equal(h.y,0);
  const v=run(Array.from({length:steps},()=>[8/steps,120*sign/steps]));
  assert.equal(v.snap.axis,'vertical');assert.equal(v.x,0);assert.ok(Math.abs(v.y-120*sign)<1e-9);
}
const diagonal=run(Array.from({length:40},()=>[1,1]));assert.equal(diagonal.snap.axis,null);assert.equal(diagonal.x,40);assert.equal(diagonal.y,40);
const jitter=run([[1,1],[1,-1],[8,.4],[-8,-.4]]);assert.equal(jitter.y,0);assert.equal(jitter.snap.axis,'horizontal');
const snap=new OrbitAxisSnap();snap.move(30,1);
const turning=snap.move(0,14);assert.equal(turning.dx,0);assert.equal(turning.dy,14,'release must not add filtered historical Y');
assert.equal(turning.axis,null);assert.equal(snap.move(0,14).axis,'vertical');
assert.equal(new OrbitAxisSnap().move(0,8).axis,'vertical','next gesture never inherits previous axis');
const small=run([[1,.1],[1,-.1]]);assert.equal(small.x,0);assert.equal(small.y,0);
assert.deepEqual(new OrbitAxisSnap().move(0,0),{dx:0,dy:0,axis:null});
console.log('Orbit axis snap: direction dominance, event sizes, jitter, reversal, diagonal, turn release and new gesture passed.');
