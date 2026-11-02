import assert from "node:assert/strict";
import { planarCadDisplayCurve } from "../../apps/_shared/workbench/sketch/planarCurveDisplay.mjs";

const point=(X,Y)=>({X,Y});
const placement=(center,x=[1,0],y=[0,1])=>({Location:point(...center),XDirection:point(...x),YDirection:point(...y)});
const curve=(type,value,First=0,Last=1,Reversed=false)=>({ID:"retained-curve",Segment:{Curve:{type,value},Range:{First,Last,Reversed}}});
const near=(a,b)=>{
  if(Array.isArray(a)){assert.equal(a.length,b.length);a.forEach((value,index)=>near(value,b[index]));}
  else assert.ok(Math.abs(a-b)<1e-9,`${a} != ${b}`);
};
const conicPoint=(basis,rx,ry,angle)=>["X","Y"].map(axis=>basis.Location[axis]
  +rx*Math.cos(angle)*basis.XDirection[axis]+ry*Math.sin(angle)*basis.YDirection[axis]);
const verifyConic=(source,first,last,rx,ry)=>{
  const original=structuredClone(source),basis=(source.Segment.Curve.value.Basis??source.Segment.Curve.value).Placement;
  const display=planarCadDisplayCurve(source);
  for(let index=0;index<display.points.length;index++)near(display.points[index],conicPoint(basis,rx,ry,first+(last-first)*index/(display.points.length-1)));
  const path=display.path(p=>p),commands=[...path.matchAll(/([MA])([^MA]+)/g)];
  near(commands[0][2].trim().split(/\s+/).map(Number),display.points[0]);
  near(commands.at(-1)[2].trim().split(/\s+/).map(Number).slice(-2),display.points.at(-1));
  assert.ok(!path.includes("L"),"An analytic conic must not be replaced by connecting chords");
  const determinant=basis.XDirection.X*basis.YDirection.Y-basis.XDirection.Y*basis.YDirection.X;
  for(const command of commands.slice(1))assert.equal(Number(command[2].trim().split(/\s+/)[4]),determinant*(last-first)>0?1:0);
  assert.deepEqual(source,original,"Display must preserve exact parameters, placement and identity");
  return display;
};

// The square-tube screenshot used left-handed 2D placements. Every corner must
// meet its actual two straight edges without modifying either source curve.
const r=3;
const rounded=[
  curve("Circle2",{Placement:placement([-27,-17],[-1,0],[0,1]),Radius:r},-Math.PI/2,0),
  curve("Segment2",{Start:point(-30,-17),End:point(-30,17)}),
  curve("Circle2",{Placement:placement([-27,17],[-1,0],[0,1]),Radius:r},0,Math.PI/2),
  curve("Segment2",{Start:point(-27,20),End:point(27,20)}),
  curve("Circle2",{Placement:placement([27,17],[1,0],[0,-1]),Radius:r},-Math.PI/2,0),
  curve("Segment2",{Start:point(30,17),End:point(30,-17)}),
  curve("Circle2",{Placement:placement([27,-17],[1,0],[0,-1]),Radius:r},0,Math.PI/2),
  curve("Segment2",{Start:point(27,-20),End:point(-27,-20)}),
];
const originalRounded=structuredClone(rounded),projected=rounded.map(planarCadDisplayCurve);
for(let i=0;i<projected.length;i++)near(projected[i].points.at(-1),projected[(i+1)%projected.length].points[0]);
assert.deepEqual(rounded,originalRounded);
const points=projected.flatMap(display=>display.points);
near([Math.min(...points.map(p=>p[0])),Math.min(...points.map(p=>p[1])),Math.max(...points.map(p=>p[0])),Math.max(...points.map(p=>p[1]))],[-30,-20,30,20]);

// Full circles and four-arc authored circles retain angular ranges, exact
// quadrant points, closure and traversal under either placement handedness.
for(const handedness of [1,-1]) {
  const angle=.37,basis=placement([4,-6],[Math.cos(angle),Math.sin(angle)],[-handedness*Math.sin(angle),handedness*Math.cos(angle)]);
  for(const reversed of [false,true]) {
    const full=curve("Circle2",{Placement:basis,Radius:7},.2,.2+Math.PI*2,reversed);
    const display=verifyConic(full,reversed?full.Segment.Range.Last:full.Segment.Range.First,reversed?full.Segment.Range.First:full.Segment.Range.Last,7,7);
    near(display.points[0],display.points.at(-1));
    assert.equal((display.path(p=>p).match(/A/g)??[]).length,2);
    const quarters=Array.from({length:4},(_,i)=>curve("Circle2",{Placement:basis,Radius:7},i*Math.PI/2,(i+1)*Math.PI/2,reversed));
    const pieces=quarters.map(source=>verifyConic(source,reversed?source.Segment.Range.Last:source.Segment.Range.First,reversed?source.Segment.Range.First:source.Segment.Range.Last,7,7));
    for(let i=0;i<4;i++)near(pieces[i].points.at(-1),pieces[(i+(reversed?3:1))%4].points[0]);
    const ellipse=curve("Ellipse2",{Placement:basis,MajorRadius:9,MinorRadius:2},.4,2.1,reversed);
    verifyConic(ellipse,reversed?2.1:.4,reversed?.4:2.1,9,2);
  }
}

// Arc variants already carry angular intervals. CounterClockwise reverses that
// same edge; it does not replace a quarter arc with the complementary 3/4 arc.
for(const type of ["Arc2","EllipseArc2"])for(const ccw of [false,true])for(const reversed of [false,true]) {
  const basis={Placement:placement([2,5],[0,1],[1,0]),...(type==="Arc2"?{Radius:5}:{MajorRadius:5,MinorRadius:2})};
  const source=curve(type,{Basis:basis,StartAngle:.3,EndAngle:1.7,CounterClockwise:ccw},.3,1.7,reversed);
  const forward=ccw!==reversed;
  verifyConic(source,forward?.3:1.7,forward?1.7:.3,5,type==="Arc2"?5:2);
}

const line=curve("Segment2",{Start:point(-8,2),End:point(3,7)},0,1,true);
const lineBefore=structuredClone(line),lineDisplay=planarCadDisplayCurve(line);
assert.deepEqual(lineDisplay.points,[[3,7],[-8,2]]);assert.deepEqual(line,lineBefore);
const bezier=curve("Bezier2",{Poles:[point(0,0),point(4,8),point(8,0)]},.25,.75,true);
const bezierBefore=structuredClone(bezier),bezierDisplay=planarCadDisplayCurve(bezier);
near(bezierDisplay.points[0],[6,3]);near(bezierDisplay.points.at(-1),[2,3]);
assert.ok(bezierDisplay.path(p=>p).includes("Q"));assert.deepEqual(bezier,bezierBefore);
const polyline=curve("Polyline2",{Points:[point(0,0),point(4,0),point(4,3)],Closed:true},0,1,true);
assert.deepEqual(planarCadDisplayCurve(polyline).points,[[0,0],[4,3],[4,0],[0,0]]);

// Display transforms must preserve both mapped ellipse axes, not derive its
// rotation from X alone or infer orientation only from a flipped screen Y.
const ellipse=curve("Ellipse2",{Placement:placement([3,4],[0,1],[1,0]),MajorRadius:9,MinorRadius:2},0,Math.PI/2);
const transformed=planarCadDisplayCurve(ellipse),map=([x,y])=>[2*x+y+10,-x+3*y-4];
const path=transformed.path(map),arc=[...path.matchAll(/A([^MA]+)/g)][0][1].trim().split(/\s+/).map(Number);
near(arc.slice(-2),map([5,4]));assert.equal(arc[4],0);
near(arc[0]*arc[1],9*2*7);

console.log("PASS tube CAD display: square-tube corner continuity, full/four-arc circles, full placement bases, ellipses, arc traversal, exact Bezier ranges, reversed curves, source preservation and affine display paths");
