import { parseDxfDocument } from "../../../iCAX-UI/SDK/Cad/dxfDocument.mjs";
import { curvePoint, reverseSegment, TAU } from "./sketchGeometry.mjs";

const unitScale = { 0: 1, 1: 25.4, 2: 304.8, 3: 1609344, 4: 1, 5: 10, 6: 1000, 7: 1000000, 8: 0.0000254, 9: 0.0254, 10: 914.4, 11: 1e-7, 12: 1e-6, 13: 0.001, 14: 100, 15: 10000, 16: 100000, 17: 1e12, 18: 1.495978707e14, 19: 9.4607304725808e18, 20: 3.085677581491367e19, 21: 1200000/3937, 22: 100000/3937, 23: 3600000/3937, 24: 6336000000/3937 };
const finite = (value, label = "CAD 数值") => {
  if (value == null || String(value).trim() === "" || !Number.isFinite(Number(value))) throw new Error(label + "无效。");
  return Number(value);
};
const positive = (value, label) => { const n = finite(value, label); if (n <= 0) throw new Error(label + "必须大于零。"); return n; };
const get = (record, code, fallback) => {
  const item = record.values.find(pair => pair[0] === code);
  if (!item && fallback == null) throw new Error(record.type + " 缺少组码 " + code + "。");
  return item ? finite(item[1], record.type + " " + code) : fallback;
};
const text = (record, code, fallback = "") => String(record.values.find(pair => pair[0] === code)?.[1] ?? fallback).trim();
const all = (record, code) => record.values.filter(pair => pair[0] === code).map(pair => finite(pair[1]));
const line = (a, b) => ({ kind: "line", x1: a[0], y1: a[1], x2: b[0], y2: b[1] });
const xy = (record, code = 10) => [get(record, code), get(record, code + 10)];
const normal = record => {
  const nx = get(record, 210, 0), ny = get(record, 220, 0), nz = get(record, 230, 1);
  if (Math.abs(nx) > 1e-9 || Math.abs(ny) > 1e-9 || Math.abs(Math.abs(nz) - 1) > 1e-9) throw new Error("二维草图不支持倾斜于 XY 平面的 CAD 图形。");
  return nz < 0 ? -1 : 1;
};
const multiply = (p, q) => [p[0]*q[0]+p[2]*q[1], p[1]*q[0]+p[3]*q[1], p[0]*q[2]+p[2]*q[3], p[1]*q[2]+p[3]*q[3], p[0]*q[4]+p[2]*q[5]+p[4], p[1]*q[4]+p[3]*q[5]+p[5]];
const point = (m, p) => {
  const result = [m[0]*p[0]+m[2]*p[1]+m[4], m[1]*p[0]+m[3]*p[1]+m[5]];
  if (!result.every(Number.isFinite)) throw new Error("CAD 图块变换后的坐标超出有效范围。");
  return result;
};

// Exact affine conics; nonuniform or nested block scales produce ellipses.
// Principal axes of M M^T preserve the curve, including reflected arc order.
function transform(entity, m) {
  if (entity.kind === "path") return { ...entity, segments: entity.segments.map(s => transform(s, m)) };
  if (entity.kind === "line") return { ...entity, ...line(point(m, [entity.x1, entity.y1]), point(m, [entity.x2, entity.y2])) };
  if (entity.kind === "bezier") return { ...entity, points: entity.points.map(p => point(m, p)) };
  const r = entity.rotation ?? 0, rx = entity.radius ?? entity.radiusX, ry = entity.radius ?? entity.radiusY;
  const [a,b,c,d] = m, det = a*d-b*c;
  const u = [rx*(a*Math.cos(r)+c*Math.sin(r)), rx*(b*Math.cos(r)+d*Math.sin(r))];
  const v = [ry*(-a*Math.sin(r)+c*Math.cos(r)), ry*(-b*Math.sin(r)+d*Math.cos(r))];
  const qxx=u[0]*u[0]+v[0]*v[0], qyy=u[1]*u[1]+v[1]*v[1], qxy=u[0]*u[1]+v[0]*v[1];
  const major=Math.sqrt((qxx+qyy+Math.hypot(qxx-qyy,2*qxy))/2), minor=Math.abs(u[0]*v[1]-u[1]*v[0])/major;
  if (!(major > 0 && minor > 0) || !Number.isFinite(major) || !Number.isFinite(minor)) throw new Error("图块变换使曲线退化。");
  const rotation=.5*Math.atan2(2*qxy,qxx-qyy), [cx,cy]=point(m,[entity.cx,entity.cy]);
  const full=["circle","ellipse"].includes(entity.kind), circular=Math.abs(major-minor)<=1e-10*Math.max(major,minor);
  let result = circular && entity.kind.startsWith("circle")
    ? { kind: full ? "circle" : "circleArc", cx,cy,radius:major }
    : { kind: full ? "ellipse" : "ellipseArc", cx,cy,radiusX:major,radiusY:minor,rotation };
  if (!full) {
    const start=point(m,curvePoint(entity,0)), dx=start[0]-cx,dy=start[1]-cy;
    result.startAngle = result.kind==="circleArc" ? Math.atan2(dy,dx) : Math.atan2((-Math.sin(rotation)*dx+Math.cos(rotation)*dy)/minor,(Math.cos(rotation)*dx+Math.sin(rotation)*dy)/major);
    result.sweep=entity.sweep*Math.sign(det);
  }
  if (entity.closed != null) result.closed=entity.closed;
  return result;
}
function bulgeSegment(a,b,bulge) {
  if (Math.abs(bulge)<1e-14) return line(a,b);
  const dx=b[0]-a[0],dy=b[1]-a[1];
  if (Math.hypot(dx,dy)<1e-12) throw new Error("CAD 多段线圆弧端点重合。");
  const k=(1-bulge*bulge)/(4*bulge),cx=(a[0]+b[0])/2-dy*k,cy=(a[1]+b[1])/2+dx*k;
  return {kind:"circleArc",cx,cy,radius:Math.hypot(a[0]-cx,a[1]-cy),startAngle:Math.atan2(a[1]-cy,a[0]-cx),sweep:4*Math.atan(bulge)};
}
const annotations = new Set(["TEXT","MTEXT","ATTRIB","ATTDEF","DIMENSION","LEADER","MLEADER","POINT","XLINE","RAY","VIEWPORT","HATCH","SOLID","TRACE","IMAGE","WIPEOUT","UNDERLAY","OLEFRAME","OLE2FRAME"]);

export function parseSketchDxf(content) { return mapSketchCadDocument(parseDxfDocument(content)); }

// Business mapping consumes decoded service data; it never loads file bytes.
// Records are mapped atomically: unsupported geometry aborts the whole import.
export function mapSketchCadDocument(document) {
  if (!document?.header || !Array.isArray(document.sections?.ENTITIES)) throw new Error("CAD 中性文档无效。");
  const units=finite(document.header.$INSUNITS?.find(pair=>pair[0]===70)?.[1]??0,"CAD 单位"),scale=unitScale[units];
  if (scale==null) throw new Error("不支持 CAD 单位代码 "+units+"。");
  const entities=[], loose=[], ignoredEntityCounts={}, blocks=new Map(), records=document.sections.BLOCKS??[];
  const hiddenLayers=new Set((document.sections.TABLES??[]).filter(r=>r.type==="LAYER"&&(get(r,62,7)<0||(get(r,70,0)&1))).map(r=>text(r,2).toUpperCase()));
  let block;
  for (const r of records) {
    if (r.type==="BLOCK") {
      const name=text(r,2); if (!name || blocks.has(name.toUpperCase())) throw new Error("CAD 图块名称无效或重复。");
      block={name,origin:[get(r,10,0),get(r,20,0)],baseZ:get(r,30,0),flags:get(r,70,0),records:[]}; blocks.set(name.toUpperCase(),block);
    } else if (r.type==="ENDBLK") block=null;
    else if (block) block.records.push(r);
  }
  let expanded=0;
  const skip = kind => { ignoredEntityCounts[kind]=(ignoredEntityCounts[kind]??0)+1; };
  const admit = (e,r,m,layer,open=false) => {
    const result={...transform(e,m),dxfLayer:layer};
    if (open) loose.push(result); else entities.push(result);
    if (entities.length+loose.length>10000) throw new Error("CAD 图形数量过多，请分批导入。");
  };
  const skipSequence = (list, index) => {
    // POLYLINE vertices and INSERT attributes belong to their parent record;
    // an invisible parent must not leave its children to the geometry mapper.
    const child = list[index].type === "POLYLINE" ? "VERTEX" : list[index].type === "INSERT" ? "ATTRIB" : null;
    if (!child) return index;
    while (list[index + 1]?.type === child) index++;
    if (list[index + 1]?.type === "SEQEND") index++;
    return index;
  };
  function visit(list,m,inheritedLayer="0",stack=[]) {
    for (let i=0;i<list.length;i++) {
      const r=list[i];
      if (!r || typeof r.type!=="string" || !Array.isArray(r.values)) throw new Error("CAD 实体记录无效。");
      if (++expanded>100000) throw new Error("CAD 图块展开数量过多。");
      const layout = text(r,410).toUpperCase();
      if (get(r,67,0)!==0 || (layout && layout!=="MODEL")) { skip("PAPERSPACE"); i=skipSequence(list,i); continue; }
      const layer=text(r,8,"0")==="0"?inheritedLayer:text(r,8,"0");
      if(get(r,60,0)===1||get(r,62,256)<0||hiddenLayers.has(layer.toUpperCase())){skip("HIDDEN");i=skipSequence(list,i);continue;}
      const checkZ=(codes=[30])=>{for(const code of codes) if(all(r,code).some(z=>Math.abs(z*scale)>1e-7)) throw new Error("二维草图只支持 Z=0 的 CAD 图形。"); if(get(r,39,0)!==0) throw new Error("二维草图不支持带厚度的 CAD 图形。");};
      if (annotations.has(r.type)) { skip(r.type); continue; }
      if (["SEQEND","ENDBLK"].includes(r.type)) continue;
      if (r.type==="INSERT") {
        checkZ(); const direction=normal(r),name=text(r,2),target=blocks.get(name.toUpperCase());
        if (!target) throw new Error("CAD 图块不存在："+name+"。");
        if (Math.abs(target.baseZ*scale)>1e-7) throw new Error("二维草图只支持 Z=0 的 CAD 图块基点。");
        if (target.flags&12) throw new Error("CAD 外部参照须先绑定为图块后导入。");
        if (stack.length>=32 || stack.includes(name.toUpperCase())) throw new Error("CAD 图块包含循环或嵌套过深。");
        const sx=get(r,41,1),sy=get(r,42,1),angle=get(r,50,0)*Math.PI/180;
        if (sx===0 || sy===0 || get(r,43,1)===0) throw new Error("CAD 图块缩放不能为零。");
        const cols=get(r,70,1),rows=get(r,71,1);
        if (!Number.isInteger(cols)||!Number.isInteger(rows)||cols<1||rows<1||cols*rows>10000) throw new Error("CAD 图块阵列数量无效。");
        const ca=Math.cos(angle),sa=Math.sin(angle),base=[direction*sx*ca,sx*sa,-direction*sy*sa,sy*ca,0,0];
        for(let row=0;row<rows;row++)for(let col=0;col<cols;col++){
          const ox=col*get(r,44,0),oy=row*get(r,45,0);
          const local=[...base];local[4]=direction*(get(r,10,0)+ca*ox-sa*oy)-base[0]*target.origin[0]-base[2]*target.origin[1];
          local[5]=get(r,20,0)+sa*ox+ca*oy-base[1]*target.origin[0]-base[3]*target.origin[1];
          visit(target.records,multiply(m,local),layer,[...stack,name.toUpperCase()]);
        }
        if(get(r,66,0)) {
          while(list[i+1]?.type==="ATTRIB"){skip("ATTRIB");i++;}
          if(list[i+1]?.type==="SEQEND")i++;
        }
      } else if(r.type==="LINE") { checkZ([30,31]); admit(line(xy(r),xy(r,11)),r,m,layer,true); }
      else if(["ARC","CIRCLE"].includes(r.type)) {
        checkZ();const direction=normal(r),center=xy(r),common={cx:direction*center[0],cy:center[1],radius:positive(get(r,40),"CAD 半径")};
        if(r.type==="CIRCLE")admit({kind:"circle",...common,closed:true},r,m,layer);
        else {
          const first=get(r,50)*Math.PI/180,last=get(r,51)*Math.PI/180,sweep=((last-first)%TAU+TAU)%TAU;
          if(sweep<1e-12)throw new Error("CAD 圆弧角度为零。");
          admit({kind:"circleArc",...common,startAngle:direction>0?first:Math.PI-first,sweep:direction*sweep,closed:false},r,m,layer,true);
        }
      } else if(r.type==="ELLIPSE") {
        checkZ([30,31]);const direction=normal(r),[cx,cy]=xy(r),[mx,my]=xy(r,11);
        const radiusX=positive(Math.hypot(mx,my),"CAD 椭圆长半径"),ratio=positive(get(r,40),"CAD 椭圆轴比");
        if(ratio>1+1e-9)throw new Error("CAD 椭圆轴比不能超过1。");
        const first=get(r,41,0),last=get(r,42,TAU),rotation=Math.atan2(my,mx);
        if(Math.abs(last-first)<1e-12||Math.abs(last-first)>TAU+1e-9)throw new Error("CAD 椭圆弧角度须大于零且不超过一周。");
        let sweep=((last-first)%TAU+TAU)%TAU;if(sweep<1e-10)sweep=TAU;
        const full=Math.abs(sweep-TAU)<1e-9,e={kind:full?"ellipse":"ellipseArc",cx,cy,radiusX,radiusY:radiusX*ratio,rotation,closed:full};
        if(!full){e.startAngle=direction*first;e.sweep=direction*sweep;}admit(e,r,m,layer,!full);
      } else if(["LWPOLYLINE","POLYLINE"].includes(r.type)) {
        const direction=normal(r),flags=get(r,70,0),vertices=[];
        if(r.type==="LWPOLYLINE") {
          checkZ([38]);
          for(const [code,raw]of r.values){
            if(code===10)vertices.push({point:[finite(raw)*direction,NaN],bulge:0});
            else if(code===20&&vertices.length)vertices.at(-1).point[1]=finite(raw);
            else if(code===42&&vertices.length)vertices.at(-1).bulge=finite(raw)*direction;
          }
          if(get(r,90,vertices.length)!==vertices.length)throw new Error("CAD 多段线点数量不一致。");
        } else {
          if(flags&(2|4))throw new Error("拟合 POLYLINE 不能按直线段导入，请先转换为精确样条。");
          if(flags&(8|16|32|64))throw new Error("二维草图不支持3D或网格多段线。");
          checkZ();
          while(list[i+1]?.type==="VERTEX"){
            const vertex=list[++i];if(all(vertex,30).some(z=>Math.abs(z*scale)>1e-7))throw new Error("二维草图只支持 Z=0 的 CAD 图形。");
            const p=xy(vertex);vertices.push({point:[direction*p[0],p[1]],bulge:direction*get(vertex,42,0)});
          }
          if(list[++i]?.type!=="SEQEND")throw new Error("CAD POLYLINE 缺少 SEQEND。");
        }
        if(vertices.length<2||vertices.some(v=>!v.point.every(Number.isFinite)))throw new Error("CAD 多段线点数据不完整。");
        const closed=Boolean(flags&1),segments=vertices.slice(0,closed?vertices.length:-1).map((v,j)=>bulgeSegment(v.point,vertices[(j+1)%vertices.length].point,v.bulge));
        admit({kind:"path",segments,closed},r,m,layer);
      } else if(r.type==="SPLINE") {
        checkZ();normal(r);const flags=get(r,70,0),degree=get(r,71),xs=all(r,10),ys=all(r,20),weights=all(r,41),knots=all(r,40);
        if(xs.length!==ys.length||xs.length!==get(r,73)||knots.length!==get(r,72)||!Number.isInteger(degree)||degree<1||degree>25||knots.length!==xs.length+degree+1)throw new Error("CAD SPLINE 的控制点、次数或节点无效。");
        if(flags&2)throw new Error("周期 SPLINE 暂不支持导入，请转换为非周期曲线。");
        if(weights.length&&(weights.length!==xs.length||weights.some(w=>w<=0||Math.abs(w-weights[0])>1e-12)))throw new Error("有理 SPLINE 无法精确保留，不能按折线近似导入。");
        const segments=splineBezierPieces(xs.map((x,j)=>[x,ys[j]]),degree,knots);
        if(flags&1){
          const first=point(m,curvePoint(segments[0],0)),last=point(m,curvePoint(segments.at(-1),1));
          if(Math.hypot(first[0]-last[0],first[1]-last[1])>1e-7)throw new Error("闭合 SPLINE 的端点不重合。");
          admit({kind:"path",segments,closed:true},r,m,layer);
        } else for(const segment of segments)admit(segment,r,m,layer,true);
      } else throw new Error("暂不支持 CAD "+r.type+"，请转换为直线、圆弧、椭圆或非有理样条轮廓。");
    }
  }
  visit(document.sections.ENTITIES,[scale,0,0,scale,0,0]);
  // Layer boundaries are meaningful; coincident endpoints in different layers
  // cannot create an unintended cutting loop.
  const layers=new Map();
  for(const s of loose){const key=s.dxfLayer.toUpperCase();if(!layers.has(key))layers.set(key,[]);layers.get(key).push(s);}
  for(const segments of layers.values())entities.push(...joinDxfSegments(segments).map(e=>({...e,dxfLayer:segments[0].dxfLayer})));
  if(!entities.length)throw new Error("CAD 中没有可用的二维图形。"+Object.keys(ignoredEntityCounts).join("、"));
  if(entities.length>5000)throw new Error("CAD 超过5000个图形，请分批导入。");
  const warnings=Object.entries(ignoredEntityCounts).map(([kind,n])=>"已忽略 "+n+" 个 "+kind+" 辅助或标注对象");
  return {entities,sourceUnitCode:units,unitScaleToMillimeter:scale,sourceFormat:document.sourceFormat,warnings,ignoredEntityCounts};
}

function splineBezierPieces(controls, degree, sourceKnots) {
  let points = controls, knots = [...sourceKnots];
  if (knots.some((k, i) => i && k < knots[i - 1])) throw new Error("DXF SPLINE 节点必须递增。");
  const first = knots[degree], last = knots[points.length];
  if (!(last > first) || knots.slice(0, degree + 1).some(k => k !== first) || knots.slice(-degree - 1).some(k => k !== last)) throw new Error("非夹持 SPLINE 请在原 CAD 中转为夹持曲线后导入。");
  const unique = [...new Set(knots.filter(k => k > first && k < last))];
  for (const u of unique) {
    let multiplicity = knots.filter(k => k === u).length;
    if (multiplicity > degree) throw new Error("DXF SPLINE 包含断开的内部节点。");
    while (multiplicity < degree) {
      const k = knots.findIndex((value, i) => value <= u && knots[i + 1] > u), n = points.length - 1;
      const next = Array(points.length + 1);
      for (let i = 0; i <= k - degree; i++) next[i] = points[i];
      for (let i = k - multiplicity; i <= n; i++) next[i + 1] = points[i];
      for (let i = k - degree + 1; i <= k - multiplicity; i++) {
        const alpha = (u - knots[i]) / (knots[i + degree] - knots[i]);
        next[i] = points[i - 1].map((value, axis) => value * (1 - alpha) + points[i][axis] * alpha);
      }
      points = next; knots.splice(k + 1, 0, u); multiplicity++;
    }
  }
  const result = [];
  for (let i = degree; i < points.length; i++) if (knots[i + 1] > knots[i]) result.push({ kind: "bezier", points: points.slice(i - degree, i + 1).map(p => [...p]) });
  return result;
}

function joinDxfSegments(segments) {
  const epsilon = 1e-7, buckets = new Map(), ends = segments.map(s => [curvePoint(s, 0), curvePoint(s, 1)]);
  const key = (x, y) => `${x},${y}`;
  ends.forEach((pair, index) => pair.forEach((point, end) => {
    const k = key(Math.floor(point[0] / epsilon), Math.floor(point[1] / epsilon));
    if (!buckets.has(k)) buckets.set(k, []);
    buckets.get(k).push({ index, end, point });
  }));
  const matches = point => {
    const [x, y] = point.map(value => Math.floor(value / epsilon)), result = [];
    for (let dx = -1; dx <= 1; dx++) for (let dy = -1; dy <= 1; dy++)
      for (const value of buckets.get(key(x + dx, y + dy)) ?? []) if (Math.hypot(point[0] - value.point[0], point[1] - value.point[1]) < epsilon) result.push(value);
    return result;
  };
  const used = new Set(), entities = [];
  const order = segments.map((_, index) => index).sort((a, b) => Math.min(...ends[a].map(p => matches(p).length)) - Math.min(...ends[b].map(p => matches(p).length)));
  for (const index of order) {
    if (used.has(index)) continue;
    const reversed = matches(ends[index][1]).length === 1 && matches(ends[index][0]).length !== 1;
    const chain = [reversed ? reverseSegment(segments[index]) : segments[index]];
    used.add(index);
    const start = curvePoint(chain[0], 0);
    let closed = false;
    while (true) {
      const end = curvePoint(chain.at(-1), 1);
      if (Math.hypot(end[0] - start[0], end[1] - start[1]) < epsilon) { closed = true; break; }
      const allMatches = matches(end);
      if (allMatches.length !== 2) break; // A junction is not an arbitrary chain.
      const next = allMatches.filter(hit => !used.has(hit.index));
      if (next.length !== 1) break;
      used.add(next[0].index); chain.push(next[0].end ? reverseSegment(segments[next[0].index]) : segments[next[0].index]);
    }
    if (chain.length === 1 && !closed && chain[0].kind !== "bezier") entities.push({ ...chain[0], closed: false });
    else entities.push({ kind: "path", segments: chain, closed });
  }
  return entities;
}
