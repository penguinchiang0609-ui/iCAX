import assert from 'node:assert/strict';
import { placeSpecificationCallout, layoutSpecificationCallouts } from '../../iCAX-UI/SDK/Viewport/specificationCalloutLayout.mjs';
const callout={x:610,y:300,w:210,h:32};
const occupied=[{x:505,y:284,w:210,h:32},{x:800,y:0,w:120,h:130}];
const p=placeSpecificationCallout(callout,occupied,920,600);
assert.ok(Math.hypot(p.x+105-callout.x,p.y+16-callout.y)<50,'Collision must be resolved close to the caption');
assert.ok(!occupied.some(b=>p.x<b.x+b.w+4&&p.x+210+4>b.x&&p.y<b.y+b.h+4&&p.y+32+4>b.y));
const free=placeSpecificationCallout({x:450,y:400,w:100,h:30},occupied,920,600);
assert.deepEqual(free,{x:400,y:385});
const clipped=placeSpecificationCallout({x:400,y:410,w:100,h:30},[],920,600,{x:120,y:60,w:500,h:200});
assert.ok(clipped.x>=128&&clipped.y>=68&&clipped.x+100<=612&&clipped.y+30<=252);
console.log('PASS callouts retain free anchors, resolve overlaps locally and avoid reserved controls');

// An exhaustive oracle protects the nearest-point and stable tie behavior.
function exhaustive(entry,occupied,width,height){
  const {x,y,w,h}=entry,clamp=(v,max)=>Math.max(8,Math.min(v,Math.max(8,max-8)));
  const xs=[clamp(x-w/2,width-w)],ys=[clamp(y-h/2,height-h)];
  for(const b of occupied){xs.push(clamp(b.x-w-8,width-w),clamp(b.x+b.w+8,width-w));ys.push(clamp(b.y-h-8,height-h),clamp(b.y+b.h+8,height-h));}
  const candidates=[...new Set(xs)].flatMap(x=>[...new Set(ys)].map(y=>({x,y})));
  candidates.sort((a,b)=>(a.x+w/2-x)**2+(a.y+h/2-y)**2-((b.x+w/2-x)**2+(b.y+h/2-y)**2));
  return candidates.find(p=>!occupied.some(b=>p.x<b.x+b.w+4&&p.x+w+4>b.x&&p.y<b.y+b.h+4&&p.y+h+4>b.y))??candidates[0];
}
let seed=123456789;
const random=()=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed/2**32;};
for(let i=0;i<400;++i){
  const width=100+random()*1000,height=80+random()*700;
  const entry={x:random()*width,y:random()*height,w:12+random()*220,h:12+random()*35};
  const boxes=Array.from({length:i%40},()=>({x:random()*width,y:random()*height,w:20+random()*150,h:15+random()*60}));
  assert.deepEqual(placeSpecificationCallout(entry,boxes,width,height),exhaustive(entry,boxes,width,height));
}
const packed=[],started=performance.now();
for(let i=0;i<480;++i){
  const label={x:200+(i%20)*15,y:200+Math.floor(i/20)*8,w:42,h:24};
  const position=placeSpecificationCallout(label,packed,760,820);
  assert.ok(Number.isFinite(position.x)&&Number.isFinite(position.y));packed.push({...position,w:42,h:24});
}
const elapsed=performance.now()-started;
assert.ok(elapsed<2000,`480 callouts must not block the UI: ${elapsed} ms`);
console.log(`PASS 400 exhaustive comparisons; 480 dense callouts in ${elapsed.toFixed(0)} ms`);

// Real G01 grid at the 1366x768 / 150% logical viewport: all ten labels
// must fit beside the specification tree, cube, axes and projection control.
const entries=[[222.7,145,190,42],[232,103.5,129.75,25],[196.2,111,190,42],
  [232,70.5,104.12,25],[218.05,37.5,104.12,25],[354.73,186.5,129.75,25],
  [174.84,20.5,189.58,25],[308.57,134.2,154.92,25],[194.44,20.5,154.92,25],
  [202.61,166.94,152.12,25]].map(([x,y,w,h])=>({x,y,w,h}));
const reserved=[[181.86,194,100,35],[14,171,58,58],[371.73,12,80,78],[8,8,150,96]]
  .map(([x,y,w,h])=>({x,y,w,h}));
const positions=layoutSpecificationCallouts(entries,reserved,463.73,243);
assert.deepEqual(positions,layoutSpecificationCallouts(entries,reserved,463.73,243));
const boxes=[...reserved];
positions.forEach((p,i)=>{
  const a={...p,w:entries[i].w,h:entries[i].h};
  assert(a.x>=0&&a.y>=0&&a.x+a.w<=463.73&&a.y+a.h<=243);
  assert(!boxes.some(b=>a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y));
  boxes.push(a);
});
console.log('PASS real small-window captions fit without control or label overlap');
