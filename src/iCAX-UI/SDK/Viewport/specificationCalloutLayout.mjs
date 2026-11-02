// Keep a numeric callout near its projected anchor while avoiding captions
// and viewport controls. Choose the nearest clear rectangle, not a corner.
export function layoutSpecificationCallouts(entries, reserved, width, height, bounds = {x:0,y:0,w:width,h:height}) {
  const overlaps=(a,b)=>a.x<b.x+b.w+2&&a.x+a.w+2>b.x&&a.y<b.y+b.h+2&&a.y+a.h+2>b.y;
  const order=entries.map((_,i)=>i);
  const pack=(indices,corner=null,spacing=8)=>{
    const occupied=[...reserved], positions=[];let conflicts=0;
    for(const i of indices){
      const entry=entries[i];
      const anchor=corner?{...entry,x:corner[0],y:corner[1]}:entry;
      const p=placeSpecificationCallout(anchor,occupied,width,height,bounds,spacing),box={...p,w:entry.w,h:entry.h};
      conflicts+=occupied.filter(b=>overlaps(box,b)).length;
      positions[i]=p;occupied.push(box);
    }
    return {positions,conflicts};
  };
  let best=pack(order);
  if(!best.conflicts||entries.length>40)return best.positions;
  // When many captions share a small viewport, moving only the last caption
  // can leave it nowhere to go. Repack all captions, keeping every control.
  const largest=[...order].sort((a,b)=>entries[b].w*entries[b].h-entries[a].w*entries[a].h);
  const corners=[[bounds.x,bounds.y],[bounds.x+bounds.w,bounds.y],
    [bounds.x,bounds.y+bounds.h],[bounds.x+bounds.w,bounds.y+bounds.h]];
  let seed=123456789;
  for(let attempt=0;attempt<80;++attempt){
    const indices=[...largest];
    if(attempt>=5)for(let i=indices.length-1;i>0;--i){seed=(Math.imul(seed,1664525)+1013904223)>>>0;const j=seed%(i+1);[indices[i],indices[j]]=[indices[j],indices[i]];}
    const result=pack(indices,attempt%5?corners[(attempt-1)%4]:null,attempt>=20?4:8);
    if(result.conflicts<best.conflicts)best=result;
    if(!best.conflicts)break;
  }
  return best.positions;
}

export function placeSpecificationCallout(entry, occupied, width, height, visibleBounds = null, spacing = 8) {
  if (visibleBounds) {
    const {x, y, w, h} = visibleBounds;
    const result = placeSpecificationCallout({...entry, x:entry.x-x, y:entry.y-y},
      occupied.map(box=>({...box,x:box.x-x,y:box.y-y})), w, h, null, spacing);
    return {x:result.x+x,y:result.y+y};
  }
  const { x, y, w, h } = entry, gap = spacing, clearance = gap / 2;
  const clamp = (value, maximum) => Math.max(gap, Math.min(value, Math.max(gap, maximum - gap)));
  const xs = [clamp(x - w / 2, width - w)], ys = [clamp(y - h / 2, height - h)];
  for (const box of occupied) {
    xs.push(clamp(box.x - w - gap, width - w), clamp(box.x + box.w + gap, width - w));
    ys.push(clamp(box.y - h - gap, height - h), clamp(box.y + box.h + gap, height - h));
  }
  const anchor = { x: xs[0], y: ys[0] };
  if (!occupied.some(box => anchor.x < box.x + box.w + clearance && anchor.x + w + clearance > box.x
      && anchor.y < box.y + box.h + clearance && anchor.y + h + clearance > box.y)) return anchor;
  // The nearest clear point is on this same discrete set of caption edges.
  // Sweep x and maintain the nearest uncovered y in a range-add tree instead
  // of materializing and sorting the Cartesian product for every label/frame.
  const columns = [...new Set(xs)].map((value, rank) => ({ value, rank })).sort((a,b)=>a.value-b.value);
  const rows = [...new Set(ys)].map((value, rank) => ({ value, rank, cost: (value+h/2-y)**2 })).sort((a,b)=>a.value-b.value);
  const n=rows.length, minimum=new Int32Array(n*4), lazy=new Int32Array(n*4), best=new Int32Array(n*4);
  const nearer=(a,b)=>rows[a].cost<rows[b].cost || (rows[a].cost===rows[b].cost && rows[a].rank<rows[b].rank)?a:b;
  const pull=node=>{
    const a=node*2,b=a+1;minimum[node]=Math.min(minimum[a],minimum[b]);
    best[node]=minimum[a]===minimum[b]?nearer(best[a],best[b]):minimum[a]<minimum[b]?best[a]:best[b];
  };
  const build=(node,lo,hi)=>{
    if(lo===hi){best[node]=lo;return;}
    const mid=(lo+hi)>>1;build(node*2,lo,mid);build(node*2+1,mid+1,hi);pull(node);
  };
  const update=(node,lo,hi,first,last,delta)=>{
    if(first<=lo&&hi<=last){minimum[node]+=delta;lazy[node]+=delta;return;}
    const a=node*2,b=a+1,mid=(lo+hi)>>1;
    if(lazy[node]){for(const child of [a,b]){minimum[child]+=lazy[node];lazy[child]+=lazy[node];}lazy[node]=0;}
    if(first<=mid)update(a,lo,mid,first,last,delta);
    if(last>mid)update(b,mid+1,hi,first,last,delta);
    pull(node);
  };
  const bound=(value,inclusive)=>{
    let lo=0,hi=n;
    while(lo<hi){const mid=(lo+hi)>>1;if(rows[mid].value<value||(inclusive&&rows[mid].value===value))lo=mid+1;else hi=mid;}
    return lo;
  };
  const events=[];
  for(const box of occupied){
    const first=bound(box.y-h-clearance,true),last=bound(box.y+box.h+clearance,false)-1;
    if(first>last)continue;
    events.push({x:box.x-w-clearance,first,last,delta:1},{x:box.x+box.w+clearance,first,last,delta:-1});
  }
  events.sort((a,b)=>a.x-b.x||a.delta-b.delta);build(1,0,n-1);
  let next=0,choice=null,score=Infinity,columnRank=Infinity,rowRank=Infinity;
  for(const column of columns){
    // Overlap inequalities are strict: remove right edges at x, add left
    // edges only after x. This also preserves exact-touching placement.
    while(next<events.length&&(events[next].x<column.value||(events[next].x===column.value&&events[next].delta<0))){
      const event=events[next++];update(1,0,n-1,event.first,event.last,event.delta);
    }
    if(minimum[1]!==0)continue;
    const row=rows[best[1]],distance=(column.value+w/2-x)**2+row.cost;
    if(distance<score||(distance===score&&(column.rank<columnRank||(column.rank===columnRank&&row.rank<rowRank)))){
      choice={x:column.value,y:row.value};score=distance;columnRank=column.rank;rowRank=row.rank;
    }
  }
  return choice??anchor;
}
