// Editable geometry is kept separate from tessellation used by validation/hit testing.
export const TAU = Math.PI * 2;
const EPS = 1e-9;
export const distance = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1]);
const mix = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
const mod = (a) => ((a % TAU) + TAU) % TAU;
export const isConic = (s) => ["circle", "ellipse", "circleArc", "ellipseArc"].includes(s?.kind);
export const isArc = (s) => ["circleArc", "ellipseArc"].includes(s?.kind);

export function arcThroughPoints(start, middle, end) {
  const ax = middle[0] - start[0], ay = middle[1] - start[1];
  const bx = end[0] - start[0], by = end[1] - start[1];
  const det = 2 * (ax * by - ay * bx);
  const scale = Math.max(ax * ax + ay * ay, bx * bx + by * by);
  if (Math.min(distance(start, middle), distance(middle, end), distance(start, end)) < EPS
    || Math.abs(det) <= scale * 1e-10) return null;
  const a2 = ax * ax + ay * ay, b2 = bx * bx + by * by;
  const cx = start[0] + (by * a2 - ay * b2) / det;
  const cy = start[1] + (ax * b2 - bx * a2) / det;
  const startAngle = Math.atan2(start[1] - cy, start[0] - cx);
  const endAngle = Math.atan2(end[1] - cy, end[0] - cx);
  const midAngle = Math.atan2(middle[1] - cy, middle[0] - cx);
  const ccw = mod(endAngle - startAngle);
  const sweep = mod(midAngle - startAngle) < ccw ? ccw : ccw - TAU;
  return { kind: "circleArc", cx, cy, radius: Math.hypot(start[0] - cx, start[1] - cy), startAngle, sweep, closed: false };
}

export function curvePoint(s, t) {
  if (s.kind === "line") return mix([s.x1, s.y1], [s.x2, s.y2], t);
  if (s.kind === "bezier") {
    let p = s.points.map((p) => [...p]);
    while (p.length > 1) p = p.slice(1).map((q, i) => mix(p[i], q, t));
    return p[0];
  }
  const a = (s.startAngle ?? 0) + (s.sweep ?? TAU) * t;
  const rx = s.radius ?? s.radiusX, ry = s.radius ?? s.radiusY;
  const r = s.rotation ?? 0, x = rx * Math.cos(a), y = ry * Math.sin(a);
  return [s.cx + x * Math.cos(r) - y * Math.sin(r), s.cy + x * Math.sin(r) + y * Math.cos(r)];
}

export function splitSegment(s, t) {
  if (s.kind === "line") {
    const p = curvePoint(s, t);
    return [{ ...s, x2: p[0], y2: p[1] }, { ...s, x1: p[0], y1: p[1] }];
  }
  if (s.kind === "bezier") {
    let level = s.points.map((p) => [...p]);
    const before = [level[0]], after = [level.at(-1)];
    while (level.length > 1) {
      level = level.slice(1).map((p, i) => mix(level[i], p, t));
      before.push(level[0]); after.unshift(level.at(-1));
    }
    return [{ kind: "bezier", points: before }, { kind: "bezier", points: after }];
  }
  return [{ ...s, sweep: s.sweep * t }, { ...s, startAngle: s.startAngle + s.sweep * t, sweep: s.sweep * (1 - t) }];
}

export function reverseSegment(s) {
  if (s.kind === "line") return { ...s, x1: s.x2, y1: s.y2, x2: s.x1, y2: s.y1 };
  if (s.kind === "bezier") return { ...s, points: [...s.points].reverse() };
  return { ...s, startAngle: s.startAngle + s.sweep, sweep: -s.sweep };
}

const line = (a, b) => ({ kind: "line", x1: a[0], y1: a[1], x2: b[0], y2: b[1] });
export function editableSegments(entity) {
  if (entity.kind === "path") return structuredClone(entity.segments);
  if (entity.kind === "line" || isArc(entity)) return [structuredClone(entity)];
  if (entity.kind === "circle" || entity.kind === "ellipse") {
    return [{ ...entity, kind: entity.kind === "circle" ? "circleArc" : "ellipseArc", startAngle: 0, sweep: TAU, closed: false }];
  }
  if (entity.kind === "rectangle") {
    const { x, y, width: w, height: h } = entity;
    const r = Math.max(0, Math.min(entity.radius ?? 0, w / 2, h / 2));
    if (!r) return [[x,y],[x+w,y],[x+w,y+h],[x,y+h]].map((p,i,a) => line(p,a[(i+1)%4]));
    const centers = [[x+w-r,y+r],[x+w-r,y+h-r],[x+r,y+h-r],[x+r,y+r]];
    const arcs = centers.map(([cx,cy],i) => ({ kind:"circleArc",cx,cy,radius:r,startAngle:(i-1)*Math.PI/2,sweep:Math.PI/2 }));
    return arcs.flatMap((arc,i) => {
      const edge = line(curvePoint(arcs[(i+3)%4],1),curvePoint(arc,0));
      return distance(curvePoint(edge,0),curvePoint(edge,1)) < EPS ? [arc] : [edge,arc];
    });
  }
  const p = entity.points ?? [];
  if (entity.kind === "arc") return p.length === 3 ? [{ kind:"bezier",points:structuredClone(p) }] : [];
  if (entity.kind === "spline" && p.length >= 3) {
    let start = p[0];
    const segments = [];
    for (let i=1;i<p.length-1;i++) {
      const end = mix(p[i],p[i+1],0.5);
      segments.push({kind:"bezier",points:[start,p[i],end]}); start=end;
    }
    const previousControl = p.at(-2);
    segments.push({kind:"bezier",points:[start,[2*start[0]-previousControl[0],2*start[1]-previousControl[1]],p.at(-1)]});
    if (entity.closed) segments.push(line(p.at(-1),p[0]));
    return structuredClone(segments);
  }
  return p.slice(0,entity.closed ? p.length : -1).map((a,i) => line(a,p[(i+1)%p.length]));
}

export function segmentSamples(s) {
  const count = s.kind === "line" ? 1 : isArc(s) ? Math.max(8,Math.ceil(Math.abs(s.sweep)/TAU*128)) : 48;
  return Array.from({length:count+1},(_,i)=>curvePoint(s,i/count));
}
export function pathSamples(entity) {
  return editableSegments(entity).flatMap((s,i)=>segmentSamples(s).slice(i ? 1 : 0));
}
export function pathNodes(entity) {
  const segments = editableSegments(entity);
  if (!segments.length) return [];
  return [curvePoint(segments[0],0),...segments.map((s)=>curvePoint(s,1))].slice(0,entity.closed ? -1 : undefined);
}

// Find the closest parameter on the actual curve, not on its display chords.
export function nearestSegment(s,p) {
  if (s.kind === "line") {
    const dx=s.x2-s.x1,dy=s.y2-s.y1;
    const t=Math.max(0,Math.min(1,((p[0]-s.x1)*dx+(p[1]-s.y1)*dy)/(dx*dx+dy*dy || 1)));
    const point=curvePoint(s,t); return {t,point,distance:distance(p,point)};
  }
  const count=96;
  let best={t:0,point:curvePoint(s,0),distance:Infinity};
  const check=(t)=>{const point=curvePoint(s,t),d=distance(p,point);if(d<best.distance) best={t,point,distance:d};};
  const values=Array.from({length:count+1},(_,i)=>{const t=i/count;check(t);return distance(p,curvePoint(s,t));});
  for(let i=0;i<=count;i++) {
    if(values[i]>(values[i-1]??Infinity)||values[i]>(values[i+1]??Infinity)) continue;
    let lo=Math.max(0,(i-1)/count),hi=Math.min(1,(i+1)/count);
    for(let n=0;n<48;n++){const a=lo+(hi-lo)/3,b=hi-(hi-lo)/3;if(distance(p,curvePoint(s,a))<distance(p,curvePoint(s,b))) hi=b;else lo=a;}
    check((lo+hi)/2);
  }
  return best;
}
export function nearestPath(entity,p) {
  let best=null;
  editableSegments(entity).forEach((s,index)=>{const hit=nearestSegment(s,p);if(!best||hit.distance<best.distance) best={...hit,index};});
  return best;
}

export function editPathAtPoint(entity,p,opening) {
  const segments=editableSegments(entity), hit=nearestPath(entity,p);
  if(!hit) return null;
  if(!opening&&(entity.kind==="circle"||entity.kind==="ellipse")) {
    const arc={...segments[0],startAngle:hit.t*TAU};
    return {parts:[{kind:"path",segments:[arc],closed:true}],node:0,point:hit.point};
  }
  const closed=["circle","ellipse","rectangle"].includes(entity.kind)||entity.closed;
  // Use a geometric tolerance so a near-vertex click never creates tiny edges.
  const s=segments[hit.index];
  const atStart=distance(hit.point,curvePoint(s,0))<1e-7, atEnd=distance(hit.point,curvePoint(s,1))<1e-7;
  let node;
  if(atStart||atEnd) {
    node=hit.index+(atEnd?1:0);
    if(!opening) return null;
  } else {
    segments.splice(hit.index,1,...splitSegment(s,hit.t));node=hit.index+1;
  }
  if(!opening) return { parts:[{kind:"path",segments,closed:Boolean(closed),brokenStart:entity.brokenStart,brokenEnd:entity.brokenEnd}],node,point:hit.point };
  if(closed) {
    const ordered=[...segments.slice(node),...segments.slice(0,node)];
    return {parts:[{kind:"path",segments:ordered,closed:false,brokenStart:true,brokenEnd:true}],point:hit.point};
  }
  if(node===0||node===segments.length) return null;
  return {parts:[
    {kind:"path",segments:segments.slice(0,node),closed:false,brokenStart:entity.brokenStart,brokenEnd:true},
    {kind:"path",segments:segments.slice(node),closed:false,brokenStart:true,brokenEnd:entity.brokenEnd},
  ],point:hit.point};
}

export function moveSegmentEnd(segment,end,p) {
  const s=structuredClone(segment);
  if(s.kind==="line") {s[end?"x2":"x1"]=p[0];s[end?"y2":"y1"]=p[1];return s;}
  if(s.kind==="bezier") {s.points[end?s.points.length-1:0]=[...p];return s;}
  if(s.kind==="circleArc") {
    const a=end?curvePoint(s,0):p,b=end?p:curvePoint(s,1);
    const result=arcThroughPoints(a,curvePoint(s,0.5),b);
    return result ?? s;
  }
  const hit=nearestSegment({...s,startAngle:0,sweep:TAU},p);
  let a=hit.t*TAU,original=end?s.startAngle+s.sweep:s.startAngle;
  a+=Math.round((original-a)/TAU)*TAU;
  const next=end?a-s.startAngle:s.startAngle+s.sweep-a;
  if(Math.sign(next)!==Math.sign(s.sweep)||Math.abs(next)<1e-8||Math.abs(next)>TAU) return s;
  if(!end)s.startAngle=a;s.sweep=next;return s;
}

export function movePathNode(entity,index,p) {
  const segments=editableSegments(entity), n=segments.length;
  const incoming=index>0?index-1:entity.closed?n-1:-1, outgoing=index<n?index:-1;
  const ellipse=segments[incoming]?.kind==="ellipseArc"?segments[incoming]:segments[outgoing]?.kind==="ellipseArc"?segments[outgoing]:null;
  if(ellipse)p=nearestSegment({...ellipse,startAngle:0,sweep:TAU},p).point;
  if(incoming===outgoing && incoming>=0 && isArc(segments[incoming])) {
    const s=segments[incoming];
    const full={...s,startAngle:0,sweep:TAU};
    s.startAngle=nearestSegment(full,p).t*TAU;
    if(s.kind==="circleArc")s.radius=distance([s.cx,s.cy],p);
  } else {
    if(incoming>=0)segments[incoming]=moveSegmentEnd(segments[incoming],true,p);
    if(outgoing>=0)segments[outgoing]=moveSegmentEnd(segments[outgoing],false,p);
  }
  return {...entity,segments};
}

export function translateSegment(s,dx,dy) {
  const result=structuredClone(s);
  if(s.kind==="line"){result.x1+=dx;result.x2+=dx;result.y1+=dy;result.y2+=dy;}
  else if(s.kind==="bezier")result.points=s.points.map(p=>[p[0]+dx,p[1]+dy]);
  else{result.cx+=dx;result.cy+=dy;}
  return result;
}

export function segmentSvg(s,map) {
  const start=map(curvePoint(s,0)),end=map(curvePoint(s,1));
  if(s.kind==="line")return `L${end.join(" ")}`;
  if(s.kind==="bezier") {
    const p=s.points.map(map);
    if(p.length===3)return `Q${p[1].join(" ")} ${p[2].join(" ")}`;
    if(p.length===4)return `C${p.slice(1).map(v=>v.join(" ")).join(" ")}`;
    return segmentSamples(s).slice(1).map(p=>`L${map(p).join(" ")}`).join(" ");
  }
  const origin=map([0,0]),unit=map([1,0]),unitY=map([0,1]);
  const scale=Math.hypot(unit[0]-origin[0],unit[1]-origin[1]);
  const reflected=(unit[0]-origin[0])*(unitY[1]-origin[1])-(unit[1]-origin[1])*(unitY[0]-origin[0])<0;
  const rotation=(s.rotation??0)*180/Math.PI*(reflected?-1:1);
  const rx=(s.radius??s.radiusX)*scale,ry=(s.radius??s.radiusY)*scale;
  const flag=(s.sweep>0)!==reflected?1:0;
  const arc=(p,span)=>`A${rx} ${ry} ${rotation} ${Math.abs(span)>Math.PI?1:0} ${flag} ${p.join(" ")}`;
  if(Math.abs(s.sweep)>TAU-1e-8)return `${arc(map(curvePoint(s,0.5)),s.sweep/2)} ${arc(end,s.sweep/2)}`;
  return arc(end,s.sweep);
}
export function pathSvg(entity,map) {
  const segments=editableSegments(entity);
  if(!segments.length)return "";
  return `M${map(curvePoint(segments[0],0)).join(" ")} ${segments.map(s=>segmentSvg(s,map)).join(" ")}${entity.closed?" Z":""}`;
}
