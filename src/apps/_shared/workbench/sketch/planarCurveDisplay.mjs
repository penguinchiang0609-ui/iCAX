import { curvePoint, segmentSvg, segmentSamples, splitSegment } from "./sketchGeometry.mjs";
// This is a display projection only. Native curves and their exact ranges stay
// in the shared document; samples below are never written back to geometry.
function conicDisplay(basis, radiusX, radiusY, first, last) {
  const { Location: center, XDirection: x, YDirection: y } = basis.Placement;
  const at = angle => {
    const u = radiusX * Math.cos(angle), v = radiusY * Math.sin(angle);
    return [center.X + x.X * u + y.X * v, center.Y + x.Y * u + y.Y * v];
  };
  const sweep = last - first;
  const count = Math.max(8, Math.ceil(Math.abs(sweep) / (Math.PI * 2) * 128));
  const points = Array.from({length: count + 1}, (_, index) => at(first + sweep * index / count));
  return {points, path: map => {
    const origin = map([center.X, center.Y]);
    const a = map([center.X + radiusX * x.X, center.Y + radiusX * x.Y]).map((value, axis) => value - origin[axis]);
    const b = map([center.X + radiusY * y.X, center.Y + radiusY * y.Y]).map((value, axis) => value - origin[axis]);
    // The two mapped basis vectors define the complete ellipse, including its
    // handedness. Principal axes also preserve rotated/reflected display maps.
    const xx = a[0] ** 2 + b[0] ** 2, yy = a[1] ** 2 + b[1] ** 2;
    const xy = a[0] * a[1] + b[0] * b[1], determinant = a[0] * b[1] - a[1] * b[0];
    const radius = Math.sqrt((xx + yy + Math.hypot(xx - yy, 2 * xy)) / 2);
    const minorRadius = Math.abs(determinant) / radius;
    const rotation = Math.atan2(2 * xy, xx - yy) * 90 / Math.PI;
    const flag = determinant * sweep > 0 ? 1 : 0;
    const pieces = Math.max(1, Math.ceil(Math.abs(sweep) / Math.PI));
    const arcs = sweep === 0 ? [] : Array.from({length: pieces}, (_, index) =>
      `A${radius} ${minorRadius} ${rotation} 0 ${flag} ${map(at(first + sweep * (index + 1) / pieces)).join(" ")}`);
    return `M${map(at(first)).join(" ")} ${arcs.join(" ")}`;
  }};
}
export function planarCadDisplayCurve(curve) {
  const {type,value}=curve.Segment.Curve,range=curve.Segment.Range;
  if(["Circle2","Ellipse2","Arc2","EllipseArc2"].includes(type)) {
    const arc=type==="Arc2"||type==="EllipseArc2",basis=arc?value.Basis:value;
    let first=arc?value.StartAngle:range.First,last=arc?value.EndAngle:range.Last;
    // Arc variants already own their angular interval; their Range is not a
    // normalized 0..1 interval. Match the native edge's two traversal reversals.
    if(arc&&!value.CounterClockwise)[first,last]=[last,first];
    if(range.Reversed)[first,last]=[last,first];
    return conicDisplay(basis,basis.Radius??basis.MajorRadius,basis.Radius??basis.MinorRadius,first,last);
  }
  let segment;
  if(type==="Segment2")segment={kind:"line",x1:value.Start.X,y1:value.Start.Y,x2:value.End.X,y2:value.End.Y};
  if(type==="Bezier2") {
    segment={kind:"bezier",points:value.Poles.map(p=>[p.X,p.Y])};
    if(range.Last!==1)segment=splitSegment(segment,range.Last)[0];
    if(range.First!==0)segment=splitSegment(segment,range.First/range.Last)[1];
  }
  if(segment) {
    if(range.Reversed) {
      if(segment.kind==="line")[segment.x1,segment.y1,segment.x2,segment.y2]=[segment.x2,segment.y2,segment.x1,segment.y1];
      else if(segment.kind==="bezier")segment.points.reverse();
      else {segment.startAngle+=segment.sweep;segment.sweep=-segment.sweep;}
    }
    const points=segmentSamples(segment);
    return {points,path:map=>`M${map(curvePoint(segment,0)).join(" ")} ${segmentSvg(segment,map)}`};
  }
  let points=[];
  if(type==="Polyline2") {
    points=value.Points.map(p=>[p.X,p.Y]);
    if(value.Closed&&points.length)points.push([...points[0]]);
  }
  if(type==="BSpline2"||type==="NURBS2") {
    const knots=value.Knots.flatMap((k,index)=>Array(value.Multiplicities[index]).fill(k));
    const degree=value.Degree,poles=value.Poles,weights=value.Weights??poles.map(()=>1);
    const basis=u=>{
      let terms=knots.slice(0,-1).map((k,i)=>u>=k&&u<knots[i+1]?1:0);
      for(let d=1;d<=degree;d++)terms=terms.slice(0,-1).map((_,i)=>
        (knots[i+d]!==knots[i]?(u-knots[i])/(knots[i+d]-knots[i])*terms[i]:0)
        +(knots[i+d+1]!==knots[i+1]?(knots[i+d+1]-u)/(knots[i+d+1]-knots[i+1])*terms[i+1]:0));
      return terms;
    };
    const first=Math.max(range.First,knots[degree]),last=Math.min(range.Last,knots[poles.length]);
    for(let index=0;index<=128;index++) {
      const u=first+(last-first)*Math.min(index/128,1-1e-12);
      const values=basis(u),terms=poles.map((_,i)=>values[i]*weights[i]),total=terms.reduce((a,b)=>a+b,0);
      if(total>1e-12)points.push(["X","Y"].map(axis=>poles.reduce((sum,p,i)=>sum+p[axis]*terms[i],0)/total));
    }
  }
  if(range.Reversed)points.reverse();
  return {points,path:map=>points.map((p,i)=>`${i?"L":"M"}${map(p).join(" ")}`).join(" ")};
}
