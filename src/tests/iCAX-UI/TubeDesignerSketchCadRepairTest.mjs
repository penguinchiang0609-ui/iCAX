import assert from "node:assert/strict";
import {diagnoseSketchEntities,repairSketchEntities} from "../../apps/tube-designer/webpage/sketchCadRepair.mjs";
import {curvePoint,splitSegment,editableSegments,TAU} from "../../apps/tube-designer/webpage/sketchGeometry.mjs";
const line=(id,a,b)=>({id,kind:"line",x1:a[0],y1:a[1],x2:b[0],y2:b[1]});
const circle=(id,cx=0)=>({id,kind:"circle",cx,cy:10,radius:3,closed:true});
{
  const source=[circle("a"),circle("b"),line("forward",[0,0],[5,0]),line("reverse",[5,0],[0,0])];
  const frozen=JSON.stringify(source),issues=diagnoseSketchEntities(source),result=repairSketchEntities(source,{joinGaps:false});
  assert.equal(JSON.stringify(source),frozen);assert.equal(issues.filter(i=>i.type==="duplicate").length,2);
  assert.deepEqual(result.entities.map(e=>e.id),["a","forward"]);assert.deepEqual(result.removedIds,["b","reverse"]);
}
{
  const a={...circle("counter-a"),fillGroup:"text-a",fillRule:"evenodd"},b={...circle("counter-b"),fillGroup:"text-b",fillRule:"evenodd"};
  assert.equal(repairSketchEntities([a,b]).entities.length,2,"Different parity regions cannot lose a contour during repair");
}
{
  const source=[line("a",[0,0],[10,0]),line("b",[20,0],[10.005,0]),line("c",[20,0],[20,10])];
  const result=repairSketchEntities(source,{tolerance:.01});
  assert.equal(result.entities.length,1);assert.equal(result.entities[0].kind,"path");
  assert.equal(result.entities[0].segments.length,2,"Collinear bridge simplifies back to one exact straight edge");
  assert.deepEqual(curvePoint(result.entities[0].segments[0],0),[0,0]);assert.deepEqual(curvePoint(result.entities[0].segments[0],1),[20,0]);
  assert.deepEqual(result.removedIds,["b","c"]);
}
{
  const a={id:"arc",kind:"circleArc",cx:0,cy:0,radius:10,startAngle:0,sweep:Math.PI/2};
  const b=line("line",[0,10.005],[0,20]);const result=repairSketchEntities([a,b],{tolerance:.01,simplify:false});
  const path=result.entities[0];assert.equal(path.segments.length,3);assert.deepEqual(path.segments[0],a,"Curve is retained exactly; a tiny bridge closes the gap");
  const tiny=repairSketchEntities([a,line("tiny",[0,10+1e-7],[0,20])],{tolerance:.01,simplify:false});
  assert.equal(tiny.entities[0].segments.length,2);assert.deepEqual(curvePoint(tiny.entities[0].segments[0],1),curvePoint(tiny.entities[0].segments[1],0));
}
{
  const branch=[line("a",[-10,0],[0,0]),line("b",[0,0],[10,0]),line("c",[0,0],[0,10])];
  assert.ok(diagnoseSketchEntities(branch).some(i=>i.type==="branch"));
  assert.equal(repairSketchEntities(branch).entities.length,3,"Ambiguous branches must not be guessed");
  const tiny=[line("tiny",[0,0],[.001,0])];
  assert.equal(repairSketchEntities(tiny).entities[0].kind,"line","A short line must not become a zero-area closed loop");
  assert.equal(repairSketchEntities(tiny,{removeShort:true}).entities.length,0);
}
{
  const bow={id:"bow",kind:"path",closed:true,segments:[line("1",[0,0],[10,10]),line("2",[10,10],[0,10]),line("3",[0,10],[10,0]),line("4",[10,0],[0,0])]};
  const issues=diagnoseSketchEntities([bow]);assert.ok(issues.some(i=>i.type==="self-intersection"&&i.point[0]===5));
  const loop={id:"loop",kind:"path",segments:[{kind:"bezier",points:[[0,0],[10,10],[-10,10],[5,0]]}]};
  assert.ok(diagnoseSketchEntities([loop]).some(i=>i.type==="self-intersection"),"A single cubic crossing is diagnosed without tessellation");
  assert.throws(()=>repairSketchEntities([circle("bad",Infinity)]),/无效/);
  assert.throws(()=>diagnoseSketchEntities([],{tolerance:0}),/容差/);
}
{
  const rectangle={id:"rectangle",kind:"rectangle",x:0,y:0,width:10,height:5};
  const segmented={id:"segmented",kind:"polyline",closed:true,points:[[5,0],[10,0],[10,5],[0,5],[0,0]]};
  const fullCircle={id:"full-circle",kind:"path",closed:true,segments:[{kind:"circleArc",cx:0,cy:10,radius:3,startAngle:.75,sweep:-TAU}]};
  const result=repairSketchEntities([rectangle,segmented,circle("circle"),fullCircle],{joinGaps:false,simplify:false});
  assert.deepEqual(result.removedIds,["segmented","full-circle"],"Different path subdivision and closed-curve seams do not hide duplicates");
}
{
  const source=[line("first",[2,0],[4,0]),line("second",[6,0],[8,0]),line("remaining",[0,0],[10,0]),circle("remaining:repair:1",50)];
  const before=JSON.stringify(source),result=repairSketchEntities(source,{joinGaps:false});
  assert.equal(JSON.stringify(source),before);
  const fragments=result.entities.filter(entity=>entity.id.startsWith("remaining")&&entity.kind==="line");
  assert.deepEqual(fragments.map(entity=>[entity.x1,entity.x2]),[[0,2],[4,6],[8,10]],"Only covered spans are removed; every uncovered piece survives");
  assert.equal(new Set(result.entities.map(entity=>entity.id)).size,result.entities.length);
  assert.deepEqual(result.createdIds,["remaining:repair:2","remaining:repair:3"]);
  const disabled=repairSketchEntities(source,{removeDuplicates:false,joinGaps:false});
  assert.deepEqual(disabled.entities,source,"Disabling duplicate removal also disables overlap clipping");
  const different=repairSketchEntities([line("one",[0,0],[10,0]),{...line("two",[5,0],[15,0]),fillGroup:"other",fillRule:"evenodd"}],{joinGaps:false});
  assert.equal(different.entities[1].x1,5,"Independent fill groups retain their complete boundaries");
  const diverging=[line("horizontal",[0,0],[1e9,0]),line("diverging",[0,0],[1e9,.05])];
  assert.deepEqual(repairSketchEntities(diverging,{joinGaps:false,tolerance:.01}).entities,diverging,"Nearly parallel long lines may diverge beyond the geometric tolerance");
}
{
  const a={kind:"ellipseArc",cx:2,cy:-3,radiusX:10,radiusY:4,rotation:.3,startAngle:0,sweep:1};
  const b={...a,radiusX:4,radiusY:10,rotation:.3+Math.PI/2,startAngle:1-Math.PI/2};
  const source={id:"elliptic",kind:"path",closed:false,segments:[a,b]};
  const result=repairSketchEntities([source],{joinGaps:false});
  assert.equal(result.entities[0].kind,"ellipseArc");assert.equal(result.entities[0].sweep,2);
  for(let i=0;i<=20;i++) {
    const t=i/20,expected=t<=.5?curvePoint(a,2*t):curvePoint(b,2*t-1);
    assert.ok(Math.hypot(...curvePoint(result.entities[0],t).map((value,axis)=>value-expected[axis]))<1e-8);
  }
  assert.deepEqual(repairSketchEntities([source],{joinGaps:false,simplify:false}).entities,[source]);
}
{
  const original={kind:"bezier",points:[[0,0],[3,6],[7,6],[10,0]]},[first,rest]=splitSegment(original,.2),[middle,last]=splitSegment(rest,.75);
  const entity={id:"spline",kind:"path",closed:false,segments:[first,middle,last]},before=JSON.stringify(entity);
  const result=repairSketchEntities([entity],{joinGaps:false});
  assert.equal(JSON.stringify(entity),before);assert.equal(result.entities[0].segments.length,1);
  assert.equal(result.entities[0].segments[0].kind,"bezier","Spline simplification preserves its editable control curve");
  for(let i=0;i<=100;i++)assert.ok(Math.hypot(...curvePoint(original,i/100).map((value,axis)=>value-curvePoint(result.entities[0].segments[0],i/100)[axis]))<1e-8);
  assert.deepEqual(repairSketchEntities([{id:"original",kind:"path",closed:false,segments:[original]},entity],{joinGaps:false}).removedIds,["spline"]);
  const unrelated={id:"unrelated",kind:"path",segments:[{kind:"bezier",points:[[0,0],[2,3],[4,0]]},{kind:"bezier",points:[[4,0],[6,-3],[8,1]]}]};
  assert.deepEqual(repairSketchEntities([unrelated],{joinGaps:false}).entities,[unrelated],"A matching tangent alone cannot merge unrelated curves");
  const straight={id:"straight",kind:"path",segments:[{kind:"bezier",points:[[0,0],[2,0],[4,0],[6,0]]}]};
  assert.equal(repairSketchEntities([straight],{joinGaps:false}).entities[0].kind,"line","Exact degree elevation is removed without fitting");
}
{
  const bow={id:"bow",kind:"path",closed:true,fillGroup:"outline-group",fillRule:"evenodd",segments:[line("1",[0,0],[10,10]),line("2",[10,10],[0,10]),line("3",[0,10],[10,0]),line("4",[10,0],[0,0])]};
  const before=JSON.stringify(bow),disabled=repairSketchEntities([bow],{joinGaps:false});
  assert.deepEqual(disabled.entities,[bow],"Self-crossing trimming requires the explicit option");
  const result=repairSketchEntities([bow,circle("bow:repair:1",50)],{joinGaps:false,trimSelfIntersections:true});
  assert.equal(JSON.stringify(bow),before);assert.equal(result.trimmedSelfIntersections,1);
  const rings=result.entities.filter(entity=>entity.kind==="path");assert.equal(rings.length,2);
  assert.deepEqual(result.createdIds,["bow:repair:2"]);
  for(const ring of rings) {
    assert.equal(ring.closed,true);assert.equal(ring.fillGroup,bow.fillGroup);assert.equal(ring.fillRule,bow.fillRule);
    assert.equal(ring.segments.length,3);assert.ok(!diagnoseSketchEntities([ring]).some(issue=>issue.type==="self-intersection"||issue.type==="gap"));
    assert.ok(Math.hypot(...curvePoint(ring.segments[0],0).map((value,axis)=>value-curvePoint(ring.segments.at(-1),1)[axis]))<1e-8);
  }
  assert.deepEqual(repairSketchEntities(result.entities,{joinGaps:false,trimSelfIntersections:true}).entities,result.entities,"Repeated repair is stable");
}
{
  const cubic={kind:"bezier",points:[[0,0],[10,10],[-10,10],[5,0]]},open={id:"open-loop",kind:"path",closed:false,segments:[cubic]};
  const result=repairSketchEntities([open],{joinGaps:false,trimSelfIntersections:true});
  assert.equal(result.trimmedSelfIntersections,1);assert.equal(result.entities.length,1);assert.equal(result.entities[0].closed,false);
  assert.ok(result.entities[0].segments.every(segment=>segment.kind==="bezier"));
  assert.deepEqual(curvePoint(result.entities[0].segments[0],0),curvePoint(cubic,0));
  assert.deepEqual(curvePoint(result.entities[0].segments.at(-1),1),curvePoint(cubic,1));
  assert.ok(!result.issues.some(issue=>issue.type==="self-intersection"||issue.type==="gap"));
  const loop={id:"closed-loop",kind:"path",closed:true,segments:[cubic,line("closing",[5,0],[0,0])]};
  const closedResult=repairSketchEntities([loop],{joinGaps:false,trimSelfIntersections:true});
  assert.equal(closedResult.entities.length,2);assert.ok(closedResult.entities.every(entity=>entity.closed));
  assert.ok(closedResult.entities.flatMap(editableSegments).some(segment=>segment.kind==="bezier"));
  assert.ok(!closedResult.issues.some(issue=>issue.type==="self-intersection"||issue.type==="gap"));
}
{
  const ellipse={kind:"ellipseArc",cx:0,cy:0,radiusX:5,radiusY:3,startAngle:-Math.PI/2,sweep:Math.PI};
  const curve={id:"curved-crossing",kind:"path",closed:true,segments:[ellipse,line("one",[0,3],[-4,-2]),line("two",[-4,-2],[4,-2]),line("three",[4,-2],[0,-3])]};
  const result=repairSketchEntities([curve],{joinGaps:false,trimSelfIntersections:true});
  assert.ok(result.trimmedSelfIntersections>0);assert.ok(result.entities.length>1);
  assert.ok(result.entities.flatMap(editableSegments).some(segment=>segment.kind==="ellipseArc"));
  assert.ok(!result.issues.some(issue=>issue.type==="self-intersection"||issue.type==="gap"));
  const overlapping={id:"retrace",kind:"path",closed:false,segments:[line("a",[0,0],[10,0]),line("b",[10,0],[5,0])]};
  assert.ok(diagnoseSketchEntities([overlapping]).some(issue=>issue.type==="overlap"));
  assert.throws(()=>repairSketchEntities([overlapping],{joinGaps:false,trimSelfIntersections:true}),/重合/);
}
{
  const quartic={id:"quartic",kind:"path",segments:[{kind:"bezier",points:[[0,0],[7.5,7.5],[.5,10],[-6.25,7.5],[5,0]]}]};
  assert.ok(diagnoseSketchEntities([quartic]).some(issue=>issue.type==="self-intersection"),"A genuine degree-four spline is checked on its editable curve");
  const result=repairSketchEntities([quartic],{joinGaps:false,trimSelfIntersections:true});
  assert.equal(result.trimmedSelfIntersections,1);assert.ok(result.entities[0].segments.every(segment=>segment.kind==="bezier"&&segment.points.length===5));
  assert.ok(!result.issues.some(issue=>issue.type==="self-intersection"||issue.type==="gap"||issue.type==="complex"));
  const continuous={id:"continuous",kind:"path",segments:[{kind:"bezier",points:[[0,0],[1,3],[2,0]]},{kind:"bezier",points:[[2,0],[3,-3],[4,0]]}]};
  assert.equal(repairSketchEntities([continuous],{joinGaps:false,trimSelfIntersections:true}).trimmedSelfIntersections,0,"Adjacent endpoints are not self crossings");
}
console.log("Sketch CAD repair: geometric duplicates, overlapping lines, exact ellipse/spline simplification, gap repair, closed-ring splitting and open-loop trimming passed.");
