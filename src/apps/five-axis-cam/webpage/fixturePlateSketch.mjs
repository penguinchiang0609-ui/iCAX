import {planarCadDisplayCurve} from '../../_shared/workbench/sketch/planarCurveDisplay.mjs';
import {editableSegments,TAU} from '../../_shared/workbench/sketch/sketchGeometry.mjs';

const point=p=>({X:p[0],Y:p[1]});
const range=(last=1)=>({First:0,Last:last,Reversed:false});
const placement=(x,y)=>({Location:{X:x,Y:y},XDirection:{X:1,Y:0},YDirection:{X:0,Y:1}});
export function sketchBoundary(entity,id=crypto.randomUUID()){
  const curves=editableSegments(entity).map((segment,index)=>{
    let Curve,Range=range();
    if(segment.kind==='line')Curve={type:'Segment2',value:{Start:point([segment.x1,segment.y1]),End:point([segment.x2,segment.y2])}};
    else if(segment.kind==='circleArc'){
      Curve={type:'Circle2',value:{Placement:placement(segment.cx,segment.cy),Radius:segment.radius}};
      Range={First:Math.min(segment.startAngle,segment.startAngle+segment.sweep),Last:Math.max(segment.startAngle,segment.startAngle+segment.sweep),Reversed:segment.sweep<0};
    }else throw Error('当前开孔支持直线、矩形和圆弧。');
    return {ID:`${id}/C${index}`,Segment:{Curve,Range}};
  });
  if(!curves.length)throw Error('请绘制完整的闭合孔。');
  return {ID:id,Curves:curves};
}
export function boundaryDisplay(boundary,map=p=>p){
  return boundary.Curves.map((curve,index)=>{const path=planarCadDisplayCurve(curve).path(map);return index?path.replace(/^M[^A-Z]+/,''):path;}).join(' ')+' Z';
}
export function boundaryBounds(boundary){
  const points=boundary.Curves.flatMap(curve=>planarCadDisplayCurve(curve).points);
  if(!points.length)throw Error('无法显示板件轮廓。');
  return [Math.min(...points.map(p=>p[0])),Math.min(...points.map(p=>p[1])),Math.max(...points.map(p=>p[0])),Math.max(...points.map(p=>p[1]))];
}
export function circleBoundary(boundary){
  const curves=boundary.Curves;
  if(!curves.length||curves.some(c=>!['Circle2','Arc2'].includes(c.Segment.Curve.type)))return null;
  const basis=c=>c.Segment.Curve.type==='Arc2'?c.Segment.Curve.value.Basis:c.Segment.Curve.value;
  const first=basis(curves[0]);let sweep=0;
  for(const c of curves){const b=basis(c);if(Math.abs(b.Radius-first.Radius)>1e-8||Math.hypot(b.Placement.Location.X-first.Placement.Location.X,b.Placement.Location.Y-first.Placement.Location.Y)>1e-8)return null;
    const s=c.Segment;sweep+=s.Curve.type==='Arc2'?Math.abs(s.Curve.value.EndAngle-s.Curve.value.StartAngle):Math.abs(s.Range.Last-s.Range.First);
  }
  return Math.abs(sweep-TAU)<1e-7?{x:first.Placement.Location.X,y:first.Placement.Location.Y,radius:first.Radius}:null;
}
export function boundaryProperties(boundary){
  const circle=circleBoundary(boundary),bounds=boundaryBounds(boundary);
  if(circle)return {kind:'circle',x:circle.x,y:circle.y,diameter:circle.radius*2};
  const linear=boundary.Curves.every(c=>['Segment2','Polyline2'].includes(c.Segment.Curve.type));
  return {kind:linear?'polygon':'curve',x:bounds[0],y:bounds[1],width:bounds[2]-bounds[0],height:bounds[3]-bounds[1],factor:1};
}
export function transformBoundary(boundary,{origin=[0,0],scaleX=1,scaleY=scaleX,dx=0,dy=0}={}){
  if(![...origin,scaleX,scaleY,dx,dy].every(Number.isFinite)||scaleX<=0||scaleY<=0)throw Error('请填写有效的开孔尺寸。');
  const out=structuredClone(boundary),move=p=>{p.X=origin[0]+(p.X-origin[0])*scaleX+dx;p.Y=origin[1]+(p.Y-origin[1])*scaleY+dy;};
  for(const edge of out.Curves){const {type,value}=edge.Segment.Curve;
    if(type==='Segment2'){move(value.Start);move(value.End);continue;}
    if(type==='Polyline2'){value.Points.forEach(move);continue;}
    if(Math.abs(scaleX-scaleY)>1e-12)throw Error('曲线孔请使用等比放大。');
    if(['Circle2','Arc2','Ellipse2','EllipseArc2'].includes(type)){
      const basis=type==='Arc2'||type==='EllipseArc2'?value.Basis:value;move(basis.Placement.Location);
      if('Radius'in basis)basis.Radius*=scaleX;else{basis.MajorRadius*=scaleX;basis.MinorRadius*=scaleX;}
    }else if(['Bezier2','BSpline2','NURBS2'].includes(type))value.Poles.forEach(move);
    else throw Error('此曲线暂不能修改，原始几何已保留。');
  }
  return out;
}
export function applyBoundaryProperties(boundary,fields){
  const before=boundaryProperties(boundary),n=k=>{if(String(fields[k]).trim()===''||!Number.isFinite(Number(fields[k])))throw Error('请填写有效的开孔尺寸。');return Number(fields[k]);};
  if(before.kind==='circle'){
    const diameter=n('diameter');if(diameter<=0)throw Error('孔径须大于零。');
    return transformBoundary(boundary,{origin:[before.x,before.y],scaleX:diameter/before.diameter,dx:n('x')-before.x,dy:n('y')-before.y});
  }
  if(before.kind==='polygon')return transformBoundary(boundary,{origin:[before.x,before.y],scaleX:n('width')/before.width,scaleY:n('height')/before.height,dx:n('x')-before.x,dy:n('y')-before.y});
  return transformBoundary(boundary,{origin:[before.x,before.y],scaleX:n('factor'),dx:n('x')-before.x,dy:n('y')-before.y});
}
