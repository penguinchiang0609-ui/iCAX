import { curvePoint, editableSegments, reverseSegment, splitSegment, distance, TAU } from "./sketchGeometry.mjs";
import { intersectionsOfEntities, boundsOfEntities } from "./sketchCadGeometry.mjs";

const clone = value => typeof structuredClone === "function" ? structuredClone(value) : JSON.parse(JSON.stringify(value));
const closed = entity => entity.closed === true || ["circle", "ellipse", "rectangle"].includes(entity.kind);
const segments = entity => entity.kind === "bezier" ? [clone(entity)] : editableSegments(entity);
const start = chain => curvePoint(chain[0], 0);
const end = chain => curvePoint(chain[chain.length - 1], 1);
const reverse = chain => [...chain].reverse().map(reverseSegment);
const line = (a,b) => ({kind:"line",x1:a[0],y1:a[1],x2:b[0],y2:b[1]});
const angleDistance = (a,b,period=TAU) => Math.abs((a-b)-Math.round((a-b)/period)*period);
const cross = (a,b) => a[0]*b[1]-a[1]*b[0];
const subtract = (a,b) => [a[0]-b[0],a[1]-b[1]];
const segmentEntity = segment => ({kind:"path",segments:[segment],closed:false});
function toleranceValue(options) {
  const value=options.tolerance??0.01;
  if (String(value).trim()==="" || !Number.isFinite(Number(value)) || Number(value)<=0 || Number(value)>100) throw new Error("修图容差须大于零且不超过 100 mm。");
  return Number(value);
}
function near(a,b,tolerance) { return distance(a,b)<=tolerance; }
function sameSegment(a,b,tolerance) {
  if (a.kind!==b.kind&&!(isConicArc(a)&&isConicArc(b)&&sameConic(a,b,tolerance))) return false;
  if (a.kind==="line") return near(curvePoint(a,0),curvePoint(b,0),tolerance)&&near(curvePoint(a,1),curvePoint(b,1),tolerance);
  if (a.kind==="bezier") return a.points.length===b.points.length&&a.points.every((p,i)=>near(p,b.points[i],tolerance));
  const radiusA=a.radius??a.radiusX,radiusB=b.radius??b.radiusX,minorA=a.radius??a.radiusY,minorB=b.radius??b.radiusY;
  if (Math.abs(Math.abs(a.sweep)-TAU)<1e-9&&Math.abs(Math.abs(b.sweep)-TAU)<1e-9) return sameConic(a,b,tolerance);
  return near([a.cx,a.cy],[b.cx,b.cy],tolerance)&&sameConic(a,b,tolerance)
    && Math.abs(a.sweep-b.sweep)*Math.max(radiusA,radiusB,minorA,minorB)<=tolerance
    && near(curvePoint(a,0),curvePoint(b,0),tolerance)&&near(curvePoint(a,0.5),curvePoint(b,0.5),tolerance)&&near(curvePoint(a,1),curvePoint(b,1),tolerance);
}
function sameConic(a,b,tolerance) {
  if(!near([a.cx,a.cy],[b.cx,b.cy],tolerance))return false;
  const ax=a.radius??a.radiusX,ay=a.radius??a.radiusY,bx=b.radius??b.radiusX,by=b.radius??b.radiusY;
  if(Math.abs(ax-ay)<=tolerance&&Math.abs(bx-by)<=tolerance)return Math.abs(ax-bx)<=tolerance&&Math.abs(ay-by)<=tolerance;
  if(a.kind!==b.kind)return false;
  const sameAxes=Math.abs(a.radiusX-b.radiusX)<=tolerance&&Math.abs(a.radiusY-b.radiusY)<=tolerance;
  return sameAxes&&angleDistance(a.rotation??0,b.rotation??0,Math.PI)*Math.max(a.radiusX,a.radiusY,b.radiusX,b.radiusY)<=tolerance
    ||Math.abs(a.radiusX-b.radiusY)<=tolerance&&Math.abs(a.radiusY-b.radiusX)<=tolerance&&angleDistance(a.rotation??0,(b.rotation??0)+Math.PI/2,Math.PI)*Math.max(a.radiusX,a.radiusY,b.radiusX,b.radiusY)<=tolerance;
}
const isConicArc=segment=>["circleArc","ellipseArc"].includes(segment.kind);
function sameGeometry(a,b,tolerance) {
  // Text counters and separate text instances have their own parity regions.
  if ((a.fillGroup||"")!==(b.fillGroup||"") || (a.fillRule||"")!==(b.fillRule||"")) return false;
  if (a.kind==="text" || b.kind==="text") return false;
  if (closed(a)!==closed(b)) return false;
  if (a.kind==="circle"&&b.kind==="circle") return near([a.cx,a.cy],[b.cx,b.cy],tolerance)&&Math.abs(a.radius-b.radius)<=tolerance;
  if (a.kind==="ellipse"&&b.kind==="ellipse") {
    return sameConic({...a,kind:"ellipseArc"},{...b,kind:"ellipseArc"},tolerance);
  }
  const aa=simplified(segments(a),closed(a),Math.min(tolerance,1e-7)),bb=simplified(segments(b),closed(b),Math.min(tolerance,1e-7));
  if (!aa.length || aa.length!==bb.length) return false;
  const matches = candidate => {
    const shifts=closed(a)?candidate.map((s,i)=>sameSegment(aa[0],s,tolerance)?i:-1).filter(i=>i>=0):[0];
    return shifts.some(shift=>aa.every((s,i)=>sameSegment(s,candidate[(i+shift)%candidate.length],tolerance)));
  };
  return matches(bb)||matches(reverse(bb));
}
function lengthUpperBound(entity) {
  return segments(entity).reduce((sum,s)=>sum+(s.kind==="line"?distance(curvePoint(s,0),curvePoint(s,1)):s.kind==="bezier"?s.points.slice(1).reduce((total,p,i)=>total+distance(p,s.points[i]),0):Math.abs(s.sweep)*Math.max(s.radius??s.radiusX,s.radius??s.radiusY)),0);
}
const choose=(n,k)=>{let value=1;for(let i=1;i<=k;i++)value=value*(n-i+1)/i;return value;};
function curveEpsilon(points,tolerance) {
  const scale=Math.max(1,...points.map(point=>distance(point,points[0])));
  return Math.min(tolerance,Math.max(1e-8,Number.EPSILON*32*scale));
}
function reducedBezier(segment,tolerance) {
  if(segment.kind!=="bezier")return segment;
  let points=segment.points;
  const epsilon=curveEpsilon(points,tolerance);
  while(points.length>2) {
    const degree=points.length-1,lower=[points[0]];
    for(let i=1;i<degree;i++)lower.push(points[i].map((value,axis)=>(degree*value-i*lower[i-1][axis])/(degree-i)));
    if(!near(lower.at(-1),points.at(-1),epsilon))break;
    points=lower;
  }
  return points.length===2?line(points[0],points[1]):{...segment,points};
}
function joinedBezier(a,b,tolerance) {
  if(a.kind!=="bezier"||b.kind!=="bezier"||a.points.length!==b.points.length)return null;
  const epsilon=curveEpsilon([...a.points,...b.points],tolerance),degree=a.points.length-1;
  let left=a.points,right=b.points,split=null;
  for(let order=1;order<=degree;order++) {
    left=left.slice(1).map((p,i)=>subtract(p,left[i]));right=right.slice(1).map((p,i)=>subtract(p,right[i]));
    const u=left.at(-1),v=right[0],lu=Math.hypot(...u),lv=Math.hypot(...v);
    if(lu<=epsilon&&lv<=epsilon)continue;
    if(lu<=epsilon||lv<=epsilon||u[0]*v[0]+u[1]*v[1]<=0||Math.abs(cross(u,v))>epsilon*(lu+lv))return null;
    const ratio=(lu/lv)**(1/order);split=ratio/(1+ratio);break;
  }
  if(split===null||split<1e-7||split>1-1e-7)return null;
  // Invert subdivision using the longer half to keep the control-point solve
  // well conditioned, then verify both original halves before accepting it.
  const first=split>=.5?a:reverseSegment(b),t=split>=.5?split:1-split,points=[];
  for(let j=0;j<=degree;j++)points.push(first.points[j].map((value,axis)=>{
    for(let k=0;k<j;k++)value-=choose(j,k)*(1-t)**(j-k)*t**k*points[k][axis];
    return value/t**j;
  }));
  let candidate={kind:"bezier",points};if(split<.5)candidate=reverseSegment(candidate);
  const [before,after]=splitSegment(candidate,split);
  return before.points.every((p,i)=>near(p,a.points[i],epsilon))&&after.points.every((p,i)=>near(p,b.points[i],epsilon))?candidate:null;
}
function joinPair(a,b,tolerance) {
  if (!near(curvePoint(a,1),curvePoint(b,0),Math.min(tolerance,1e-7))) return null;
  if(a.kind==="line"&&b.kind==="line") {
    const u=[a.x2-a.x1,a.y2-a.y1],v=[b.x2-b.x1,b.y2-b.y1];
    const merged=[u[0]+v[0],u[1]+v[1]],deviation=Math.abs(cross(u,merged))/Math.hypot(...merged);
    if(u[0]*v[0]+u[1]*v[1]<=0||deviation>curveEpsilon([curvePoint(a,0),curvePoint(a,1),curvePoint(b,1)],tolerance))return null;
    return {...a,x2:b.x2,y2:b.y2};
  }
  if(["circleArc","ellipseArc"].includes(a.kind)&&sameConic(a,b,Math.min(tolerance,1e-8))&&Math.sign(a.sweep)===Math.sign(b.sweep)&&Math.abs(a.sweep+b.sweep)<=TAU+1e-9)return {...a,sweep:a.sweep+b.sweep};
  return joinedBezier(a,b,tolerance);
}
function simplified(chain,isClosed,tolerance) {
  const result=[];
  for(const original of chain) {
    const s=reducedBezier(original,tolerance);
    const merged=result.length?joinPair(result[result.length-1],s,tolerance):null;
    if(merged)result[result.length-1]=merged;else result.push(s);
  }
  if(isClosed&&result.length>1) {
    const merged=joinPair(result[result.length-1],result[0],tolerance);
    if(merged) {result[0]=merged;result.pop();}
  }
  return result;
}
function asPath(source,chain,isClosed) {
  const entity={...source};
  for(const key of ["x","y","width","height","radius","radiusX","radiusY","cx","cy","rotation","startAngle","sweep","x1","y1","x2","y2","points","segments","brokenStart","brokenEnd"])delete entity[key];
  if(chain.length===1&&!isClosed&&chain[0].kind!=="bezier")return {...entity,...chain[0],id:source.id,closed:false};
  return {...entity,kind:"path",segments:chain,closed:isClosed};
}
function bridgeChains(aa,bb,same=false) {
  const gap=distance(end(aa),start(bb));
  if(gap<=1e-9)return true;
  if(gap>1e-6){aa.push(line(end(aa),start(bb)));return true;}
  const last=aa[aa.length-1],first=bb[0],p=start(bb),q=end(aa);
  if(last.kind==="line"){last.x2=p[0];last.y2=p[1];return true;}
  if(last.kind==="bezier"){last.points[last.points.length-1]=[...p];return true;}
  if(first.kind==="line"){first.x1=q[0];first.y1=q[1];return true;}
  if(first.kind==="bezier"){first.points[0]=[...q];return true;}
  return false;
}
function validRecord(entity) {
  try {
    const chain=segments(entity);if(!chain.length)return false;
    const box=boundsOfEntities([entity]);
    return [box.minX,box.minY,box.maxX,box.maxY].every(Number.isFinite)&&chain.every(s=>s.kind==="line"||s.kind==="bezier"&&s.points.length>=2||["circleArc","ellipseArc"].includes(s.kind)&&Math.abs(s.sweep)>1e-10&&(s.radius??s.radiusX)>0&&(s.radius??s.radiusY)>0);
  } catch {return false;}
}
function cubicCrossing(segment) {
  if(segment.kind!=="bezier"||segment.points.length!==4)return null;
  const p=segment.points,a=[0,1].map(i=>-p[0][i]+3*p[1][i]-3*p[2][i]+p[3][i]),b=[0,1].map(i=>3*p[0][i]-6*p[1][i]+3*p[2][i]),c=[0,1].map(i=>3*(p[1][i]-p[0][i]));
  const den=cross(a,b);
  if(Math.abs(den)<1e-12)return null;
  const u=-cross(a,c)/den,index=Math.abs(a[0])>Math.abs(a[1])?0:1,v=u*u+(b[index]*u+c[index])/a[index],discriminant=u*u-4*v;
  if(discriminant<=1e-14)return null;
  const t=(u-Math.sqrt(discriminant))/2,s=(u+Math.sqrt(discriminant))/2;
  if(t<0||s>1||s-t<1e-7||t<1e-7&&s>1-1e-7)return null;
  return distance(curvePoint(segment,t),curvePoint(segment,s))<1e-7?{point:curvePoint(segment,t),t,s}:null;
}
function bezierCrossing(segment) {
  if(segment.kind!=="bezier")return null;
  const normalized=reducedBezier(segment,1e-8);
  if(normalized.kind!=="bezier"||normalized.points.length<=3)return null;
  if(normalized.points.length===4)return cubicCrossing(normalized);
  let visits=0;
  const monotone=points=>{
    const differences=points.slice(1).map((point,index)=>subtract(point,points[index]));
    return [[1,0],[0,1],subtract(points.at(-1),points[0]),...differences].some(direction=>{
      const size=Math.hypot(...direction);if(size<1e-12)return false;
      const projections=differences.map(delta=>(delta[0]*direction[0]+delta[1]*direction[1])/size);
      return projections.every(value=>value>=0)&&projections.some(value=>value>0)||projections.every(value=>value<=0)&&projections.some(value=>value<0);
    });
  };
  const search=(curve,lo,hi,depth)=>{
    if(monotone(curve.points))return null;
    if(++visits>2048||depth>24)throw new Error("高阶样条过于复杂，请先分割后修图。");
    const [left,right]=splitSegment(curve,.5),middle=(lo+hi)/2;
    for(const hit of intersectionsOfEntities(segmentEntity(left),segmentEntity(right))) {
      const t=lo+(middle-lo)*hit.a.t,s=middle+(hi-middle)*hit.b.t;
      if(s-t>1e-7&&!(t<1e-7&&s>1-1e-7))return {point:hit.point,t,s};
    }
    return search(left,lo,middle,depth+1)||search(right,middle,hi,depth+1);
  };
  return search(normalized,0,1,0);
}
function overlapOfLines(a,b,tolerance) {
  const p=curvePoint(a,0),u=subtract(curvePoint(a,1),p),q=curvePoint(b,0),v=subtract(curvePoint(b,1),q),length=Math.hypot(...u);
  if(length<1e-9||Math.hypot(...v)<1e-9||Math.abs(cross(u,v))>1e-10*length*Math.hypot(...v))return null;
  if(Math.abs(cross(subtract(q,p),u))/length>tolerance||Math.abs(cross(subtract(curvePoint(b,1),p),u))/length>tolerance)return null;
  const project=point=>((point[0]-p[0])*u[0]+(point[1]-p[1])*u[1])/(length*length),ends=[project(q),project(curvePoint(b,1))];
  const lo=Math.max(0,Math.min(...ends)),hi=Math.min(1,Math.max(...ends));
  return (hi-lo)*length>1e-8?[lo,hi]:null;
}
function sliceSegment(segment,lo,hi) {
  let value=clone(segment);
  if(hi<1-1e-10)value=splitSegment(value,hi)[0];
  if(lo>1e-10)value=splitSegment(value,lo/hi)[1];
  return value;
}
function sliceChain(chain,lo,hi) {
  const result=[];
  for(let i=Math.floor(lo);i<Math.ceil(hi-1e-10);i++) {
    const from=Math.max(0,lo-i),to=Math.min(1,hi-i);
    if(to-from>1e-10)result.push(sliceSegment(chain[i],from,to));
  }
  return result;
}
function selfCrossing(chain,isClosed) {
  for(let a=0;a<chain.length;a++) {
    const internal=bezierCrossing(chain[a]);
    if(internal)return {point:internal.point,lo:a+internal.t,hi:a+internal.s};
    for(let b=a+1;b<chain.length;b++) {
      if(chain[a].kind==="line"&&chain[b].kind==="line"&&overlapOfLines(chain[a],chain[b],1e-8))throw new Error("路径内存在重合线段，请先分割或删除重合部分。");
      for(const hit of intersectionsOfEntities(segmentEntity(chain[a]),segmentEntity(chain[b]))) {
        const lo=a+Math.max(0,Math.min(1,hit.a.t)),hi=b+Math.max(0,Math.min(1,hit.b.t));
        if(hi-lo<1e-7||(isClosed&&chain.length-(hi-lo)<1e-7))continue;
        return {point:hit.point,lo,hi};
      }
    }
  }
  return null;
}
function trimCrossings(entity) {
  const pending=[{chain:segments(entity),closed:closed(entity)}],parts=[];let count=0;
  while(pending.length) {
    const item=pending.pop(),hit=selfCrossing(item.chain,item.closed);
    if(!hit){parts.push(item);continue;}
    if(++count>256)throw new Error("自相交数量过多，请分段修图。");
    const before=sliceChain(item.chain,0,hit.lo),loop=sliceChain(item.chain,hit.lo,hit.hi),after=sliceChain(item.chain,hit.hi,item.chain.length);
    if(item.closed) {
      // Each resulting ring consists of exact subdivisions of the input. No
      // tessellated contour, fitted replacement or guessed new boundary is used.
      pending.push({chain:[...after,...before],closed:true},{chain:loop,closed:true});
    } else {
      const remainder=[...before,...after];
      if(remainder.length)pending.push({chain:remainder,closed:false});
    }
  }
  return {parts:count?parts.map(part=>asPath(entity,part.chain,part.closed)):[entity],count};
}
function nextRepairId(sourceId,usedIds) {
  let index=1,id;
  do{id=`${sourceId}:repair:${index++}`;}while(usedIds.has(id));
  usedIds.add(id);return id;
}
function removeOverlappingLines(entities,tolerance,removedIds,changedIds,createdIds,usedIds) {
  const result=[],kept=[];
  for(const entity of entities) {
    if(entity.kind!=="line"||closed(entity)){result.push(entity);continue;}
    let spans=[[0,1]];
    for(const previous of kept) {
      if((previous.fillGroup||"")!==(entity.fillGroup||"")||(previous.fillRule||"")!==(entity.fillRule||""))continue;
      const overlap=overlapOfLines(entity,previous,tolerance);if(!overlap)continue;
      const [lo,hi]=overlap;
      spans=spans.flatMap(([from,to])=>hi<=from+1e-10||lo>=to-1e-10?[[from,to]]:[...(lo>from+1e-10?[[from,Math.min(to,lo)]]:[]),...(hi<to-1e-10?[[Math.max(from,hi),to]]:[])]);
    }
    if(!spans.length){removedIds.push(entity.id);continue;}
    if(spans.length!==1||spans[0][0]!==0||spans[0][1]!==1)changedIds.add(entity.id);
    const fragments=spans.map(([lo,hi],index)=>{
      const id=index?nextRepairId(entity.id,usedIds):entity.id;if(index)createdIds.push(id);
      return {...sliceSegment(entity,lo,hi),id};
    });
    kept.push(...fragments);result.push(...fragments);
  }
  return result;
}
function endpointsOf(entities) {
  const endpoints=[];
  for(const entity of entities) {
    if(closed(entity)||entity.kind==="text"||!validRecord(entity))continue;
    const chain=segments(entity);
    endpoints.push({entityId:entity.id,end:false,point:start(chain)},{entityId:entity.id,end:true,point:end(chain)});
  }
  return endpoints;
}
export function diagnoseSketchEntities(entities,options={}) {
  const tolerance=toleranceValue(options),issues=[],boxes=[];
  const issue=(type,message,entityIds,point,details)=>issues.push({type,message,entityIds,...(point?{point}:{}),...(details?{details}:{})});
  for(let i=0;i<entities.length;i++) {
    const entity=entities[i];
    if(!validRecord(entity)){issue("invalid","图形参数无效",[entity.id]);boxes.push(null);continue;}
    boxes.push(boundsOfEntities([entity]));
    if(lengthUpperBound(entity)<tolerance)issue("short","图形短于修图容差",[entity.id],curvePoint(segments(entity)[0],0));
    const chain=segments(entity);
    let crossing;
    try{crossing=chain.map(bezierCrossing).find(Boolean);}catch(error){issue("complex","样条须分割后继续检查",[entity.id],curvePoint(chain[0],0),{message:error.message});}
    if(crossing)issue("self-intersection","图形存在自相交",[entity.id],crossing.point);
    for(let j=1;j<chain.length;j++)if(distance(curvePoint(chain[j-1],1),curvePoint(chain[j],0))>1e-6)issue("gap","路径内部存在断口",[entity.id],curvePoint(chain[j],0));
    if(closed(entity)&&distance(start(chain),end(chain))>1e-6)issue("gap","闭合路径首尾存在断口",[entity.id],end(chain));
    for(let a=0;a<chain.length;a++)for(let b=a+1;b<chain.length;b++) {
      let hits;
      try {
        if(chain[a].kind==="line"&&chain[b].kind==="line"&&overlapOfLines(chain[a],chain[b],1e-8))throw new Error("路径内存在重合线段");
        hits=intersectionsOfEntities(segmentEntity(chain[a]),segmentEntity(chain[b]));
      }
      catch(error) {issue("overlap","曲线存在重合，需要分割或删除重复段",[entity.id],curvePoint(chain[a],0),{message:error.message});a=chain.length;break;}
      const hit=hits.find(value=> !((b===a+1&&near(value.point,curvePoint(chain[a],1),1e-7))||(closed(entity)&&a===0&&b===chain.length-1&&near(value.point,start(chain),1e-7))));
      if(hit) {issue("self-intersection","图形存在自相交",[entity.id],hit.point);a=chain.length;break;}
    }
  }
  for(let a=0;a<entities.length;a++)for(let b=a+1;b<entities.length;b++) {
    const aa=boxes[a],bb=boxes[b];if(!aa||!bb)continue;
    if(Math.abs(aa.minX-bb.minX)>tolerance||Math.abs(aa.maxX-bb.maxX)>tolerance||Math.abs(aa.minY-bb.minY)>tolerance||Math.abs(aa.maxY-bb.maxY)>tolerance)continue;
    if(sameGeometry(entities[a],entities[b],tolerance))issue("duplicate","存在重复图形",[entities[a].id,entities[b].id],[aa.centerX,aa.centerY]);
  }
  const endpoints=endpointsOf(entities);
  for(let i=0;i<endpoints.length;i++) {
    const p=endpoints[i],neighbors=endpoints.filter((q,j)=>j!==i&&near(p.point,q.point,tolerance));
    if(neighbors.length>1)issue("branch","多个端点聚集，需确认连接关系",[p.entityId,...neighbors.map(q=>q.entityId)],p.point);
    else if(neighbors.length&&distance(p.point,neighbors[0].point)>1e-6&&i<endpoints.indexOf(neighbors[0]))issue("gap","开放端点之间存在可连接的小断口",[p.entityId,neighbors[0].entityId],p.point);
  }
  return issues;
}
export function repairSketchEntities(entities,options={}) {
  const tolerance=toleranceValue(options),original=clone(entities),removedIds=[],createdIds=[],changedIds=new Set(),usedIds=new Set(entities.map(entity=>entity.id));let result=clone(entities),trimmedSelfIntersections=0;
  if(result.some(entity=>!validRecord(entity)))throw new Error("存在无效图形，请先检查并定位后修改。");
  if(options.removeDuplicates!==false) {
    const unique=[];
    for(const entity of result) {
      if(unique.some(candidate=>sameGeometry(candidate,entity,tolerance)))removedIds.push(entity.id);else unique.push(entity);
    }
    result=removeOverlappingLines(unique,tolerance,removedIds,changedIds,createdIds,usedIds);
  }
  if(options.removeShort===true)result=result.filter(entity=>{if(lengthUpperBound(entity)>=tolerance)return true;removedIds.push(entity.id);return false;});
  if(options.trimSelfIntersections===true)result=result.flatMap(entity=>{
    const trimmed=trimCrossings(entity);if(!trimmed.count)return [entity];
    changedIds.add(entity.id);trimmedSelfIntersections+=trimmed.count;
    if(!trimmed.parts.length){removedIds.push(entity.id);return [];}
    return trimmed.parts.map((part,index)=>{
      if(!index)return part;
      const id=nextRepairId(entity.id,usedIds);createdIds.push(id);return {...part,id};
    });
  });
  if(options.joinGaps!==false) {
    // Join only unambiguous endpoints. A small straight bridge retains both
    // original analytic curves instead of distorting a circle or spline.
    let didJoin=true;
    while(didJoin) {
      didJoin=false;const ends=endpointsOf(result);
      for(let i=0;i<ends.length&&!didJoin;i++) {
        const p=ends[i],neighbors=ends.filter((q,j)=>j!==i&&near(p.point,q.point,tolerance));
        if(neighbors.length!==1)continue;const q=neighbors[0];
        if(ends.filter((r,j)=>r!==q&&near(r.point,q.point,tolerance)).length!==1)continue;
        const a=result.find(entity=>entity.id===p.entityId),b=result.find(entity=>entity.id===q.entityId);
        if((a.fillGroup||"")!==(b.fillGroup||""))continue;
        let aa=segments(a),bb=segments(b);
        if(a===b) {
          if(p.end===q.end)continue;
          if(aa.length===1&&aa[0].kind==="line")continue;
          if(!bridgeChains(aa,aa,true))continue;
          result=result.map(entity=>entity===a?asPath(a,aa,true):entity);changedIds.add(a.id);didJoin=true;
        } else {
          if(!p.end)aa=reverse(aa);if(q.end)bb=reverse(bb);
          if(!bridgeChains(aa,bb))continue;
          const merged=asPath(a,[...aa,...bb],false);
          result=result.filter(entity=>entity!==b).map(entity=>entity===a?merged:entity);removedIds.push(b.id);changedIds.add(a.id);didJoin=true;
        }
      }
    }
  }
  if(options.simplify!==false) result=result.map(entity=>{
    if(entity.kind==="text"||["circle","ellipse","rectangle"].includes(entity.kind))return entity;
    const chain=segments(entity),next=simplified(chain,closed(entity),tolerance);
    if(JSON.stringify(next)===JSON.stringify(chain))return entity;
    changedIds.add(entity.id);return asPath(entity,next,closed(entity));
  });
  for(const entity of result) {
    const previous=original.find(item=>item.id===entity.id);if(JSON.stringify(previous)!==JSON.stringify(entity))changedIds.add(entity.id);
  }
  return {entities:result,removedIds,createdIds:createdIds.filter(id=>result.some(entity=>entity.id===id)),changedIds:[...changedIds],trimmedSelfIntersections,issues:diagnoseSketchEntities(result,{tolerance})};
}
