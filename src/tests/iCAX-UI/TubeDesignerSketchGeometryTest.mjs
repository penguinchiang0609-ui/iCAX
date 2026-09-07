import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { arcThroughPoints, curvePoint, editableSegments, nearestSegment, pathNodes, pathSamples, pathSvg, segmentSamples, TAU } from "../../apps/tube-designer/webpage/sketchGeometry.mjs";
import { analyzeSectionDraft, breakEntityAtPoint, buildProfileFromSectionDraft, createInitialSketchState, entitiesFromProfile, finishCadCommand, handleCadPoint, handleSketchRibbonCommand, insertPointOnEntity, mergeSelectedEntities, updateEntityFromGrip } from "../../apps/tube-designer/webpage/sketchArea.mjs";

const near=(a,b,tol=1e-7)=>assert.ok(Math.hypot(a[0]-b[0],a[1]-b[1])<tol,`${a} != ${b}`);
const draftOf=(e)=>({...createInitialSketchState().section,entities:[e],selectedIds:[e.id],selectedId:e.id});
const circle={id:"circle",kind:"circle",cx:3.125678,cy:-4.987654,radius:17.987654,closed:true};
const rect={id:"rect",kind:"rectangle",x:0,y:0,width:60,height:40,radius:8,closed:true};
const ellipse={id:"ellipse",kind:"ellipse",cx:7,cy:-3,radiusX:28,radiusY:6,rotation:.713,closed:true};

// Three-point arcs must pass through every input point, including clockwise and major arcs.
for(const points of [
  [[10,0],[0,10],[-10,0]], [[10,0],[0,-10],[-10,0]],
  [[10,0],[-10,0],[0,10]], [[10,0],[-10,0],[0,-10]],
  [[1000000.123,2000000.456],[1000003.987,2000004.321],[1000010.333,2000001.234]],
]) {
  const state=createInitialSketchState();state.tool="arc";
  for(const p of points)handleCadPoint(state,state.section,p,{sampleStep:.001});
  const arc=state.section.entities[0];
  assert.equal(arc.kind,"circleArc");
  near(curvePoint(arc,0),points[0]);near(curvePoint(arc,1),points[2]);
  assert.ok(nearestSegment(arc,points[1]).distance<1e-7);
  for(const p of segmentSamples(arc))assert.ok(Math.abs(Math.hypot(p[0]-arc.cx,p[1]-arc.cy)-arc.radius)<1e-7);
  assert.match(pathSvg(arc,p=>[p[0],-p[1]]),/A/);
  assert.doesNotMatch(pathSvg(arc,p=>p),/[QL]/);
}
assert.equal(arcThroughPoints([0,0],[5,0],[10,0]),null);
{
  const state=createInitialSketchState();state.tool="arc";
  [[0,0],[5,0],[10,0]].forEach(p=>handleCadPoint(state,state.section,p,{sampleStep:.001}));
  assert.equal(state.section.entities.length,0);
  assert.equal(state.command.points.length,2,"invalid third point keeps the first two points for correction");
  handleCadPoint(state,state.section,[10,5],{sampleStep:.001});
  assert.equal(state.section.entities.length,1);
}

for(const original of [circle,ellipse,rect]) {
  const draft=draftOf(structuredClone(original));
  const s=editableSegments(original).find(s=>s.kind!=="line")??editableSegments(original)[0];
  const hit=curvePoint(s,.314);
  const inserted=insertPointOnEntity(draft,original.id,hit);
  assert.equal(inserted.changed,true);
  assert.equal(draft.entities.length,1);
  assert.equal(draft.entities[0].closed,true);
  assert.equal(analyzeSectionDraft(draft).ready,true);
  near(pathNodes(draft.entities[0])[inserted.index],hit);
  assert.ok(editableSegments(draft.entities[0]).some(s=>s.kind.includes("Arc")));
  assert.equal(insertPointOnEntity(draft,original.id,hit).changed,false,"duplicate node does not create tiny edges");
  const breakResult=breakEntityAtPoint(draft,original.id,hit);
  assert.equal(breakResult.changed,true,"break is valid at an existing connected node");
  assert.equal(draft.selectedPoints.length,2);
  assert.equal(analyzeSectionDraft(draft).ready,false);
  const repaired=mergeSelectedEntities(draft);
  assert.equal(repaired.changed,true);
  assert.equal(analyzeSectionDraft(draft).ready,true);
  const profile=buildProfileFromSectionDraft(draft);
  const reloaded=entitiesFromProfile(profile);
  assert.equal(analyzeSectionDraft({entities:reloaded}).ready,true);
  assert.ok(reloaded.some(e=>e.kind==="circle"||e.kind==="ellipse"||editableSegments(e).some(s=>s.kind.includes("Arc"))),"saving and reloading retain curved segments");
}

// Projection is onto a rotated, eccentric ellipse, rather than a chord or scaled radial ray.
{
  const arc=editableSegments(ellipse)[0], p=[13,18],hit=nearestSegment(arc,p);
  const a=hit.t*TAU, r=ellipse.rotation;
  const tangent=[-ellipse.radiusX*Math.sin(a)*Math.cos(r)-ellipse.radiusY*Math.cos(a)*Math.sin(r),-ellipse.radiusX*Math.sin(a)*Math.sin(r)+ellipse.radiusY*Math.cos(a)*Math.cos(r)];
  assert.ok(Math.abs((p[0]-hit.point[0])*tangent[0]+(p[1]-hit.point[1])*tangent[1])<1e-4);
}

// Restoring one of two cuts must not close the other cut, for polygonal or analytic curves.
for(const original of [circle,{id:"square",kind:"polyline",closed:true,points:[[0,0],[20,0],[20,20],[0,20]]}]) {
  const draft=draftOf(structuredClone(original));
  const positions=original.kind==="circle"?[[circle.cx+circle.radius,circle.cy],[circle.cx-circle.radius,circle.cy]]:[[10,0],[10,20]];
  breakEntityAtPoint(draft,original.id,positions[0]);
  breakEntityAtPoint(draft,draft.entities[0].id,positions[1]);
  assert.equal(draft.entities.length,2);
  assert.equal(mergeSelectedEntities(draft).changed,true);
  assert.equal(analyzeSectionDraft(draft).ready,false);
  const e=draft.entities[0],n=pathNodes(e).length-1;
  draft.selectedPoints=[{entityId:e.id,index:0},{entityId:e.id,index:n}];
  assert.equal(mergeSelectedEntities(draft).changed,true);
  assert.equal(analyzeSectionDraft(draft).ready,true);
}

// Moving an arc endpoint freely preserves the other endpoint and passes through the new position.
{
  const arc={...arcThroughPoints([10,0],[0,10],[-10,0]),id:"arc"};
  updateEntityFromGrip(arc,{role:"vertex",index:1,original:structuredClone(arc),start:[-10,0]},[-13,4]);
  near(curvePoint(arc,0),[10,0]);near(curvePoint(arc,1),[-13,4]);
  assert.equal(arc.kind,"circleArc");
}
{
  const draft=draftOf(structuredClone(circle));
  breakEntityAtPoint(draft,circle.id,[circle.cx+circle.radius,circle.cy]);
  const e=draft.entities[0],original=structuredClone(e),p=curvePoint(e,1);
  updateEntityFromGrip(e,{role:"vertex",index:1,original,start:p},[p[0]+3,p[1]+2]);
  near(pathNodes(e)[0],p);near(pathNodes(e).at(-1),[p[0]+3,p[1]+2]);
  assert.ok(editableSegments(e).every(s=>s.kind==="circleArc"));
}

// Insert a point into a rounded edge and ensure only adjoining curve segments change on drag.
{
  const arc={...arcThroughPoints([20,0],[0,20],[-20,0]),id:"arc"};
  const draft=draftOf(arc),p=curvePoint(arc,.4);
  const result=insertPointOnEntity(draft,arc.id,p),e=draft.entities[0];
  const target=[p[0],p[1]+5];
  updateEntityFromGrip(e,{role:"vertex",index:result.index,original:structuredClone(e),start:p},target);
  near(pathNodes(e)[result.index],target);
  near(curvePoint(e.segments[0],1),curvePoint(e.segments[1],0));
  near(pathNodes(e)[0],[20,0]);near(pathNodes(e).at(-1),[-20,0]);
}

// Frontend history restores both geometry and the two logical break endpoints.
{
  const view={tubeDesignerSketch:createInitialSketchState(),scene:{tubeDesigner:{}}};
  view.tubeDesignerSketch.section=draftOf(structuredClone(circle));
  const draft=view.tubeDesignerSketch.section,ops={renderProject(){}};
  breakEntityAtPoint(draft,circle.id,[circle.cx+circle.radius,circle.cy]);
  await handleSketchRibbonCommand({},view,"sketch.undo",ops);
  assert.equal(draft.entities[0].kind,"circle");
  await handleSketchRibbonCommand({},view,"sketch.redo",ops);
  assert.equal(draft.entities[0].kind,"circleArc");
  assert.equal(draft.selectedPoints.length,2);
}
// The C++ neutral-model reader and DXF writer require positive ellipse sweeps
// and a major radius no smaller than the minor radius.
const exportProfiles=[];
for(const source of [
  {entities:[{id:"outer",kind:"rectangle",x:-60,y:-60,width:120,height:120,closed:true},{...circle,cx:14,cy:8,radius:7}]},
  {entities:[{...ellipse,radiusX:6,radiusY:28}]},
  {entities:[{...editableSegments(ellipse)[0],id:"clockwise",sweep:-Math.PI},{id:"chord",kind:"line",x1:curvePoint(ellipse,.5)[0],y1:curvePoint(ellipse,.5)[1],x2:curvePoint(ellipse,0)[0],y2:curvePoint(ellipse,0)[1]}]},
]) {
  const profile=buildProfileFromSectionDraft(source);
  exportProfiles.push(profile);
  for(const contour of profile.contours)for(const s of contour.segments??[])if(s.kind==="ellipseArc") {
    assert.ok(s.majorRadius>=s.minorRadius);
    assert.ok(s.endAngle>s.startAngle);
    assert.ok(s.endAngle-s.startAngle<=TAU+1e-9);
  }
  assert.equal(analyzeSectionDraft({entities:entitiesFromProfile(profile)}).ready,true);
}
assert.equal(exportProfiles[0].contours[1].kind,"path","an offset circular hole needs explicit arc centers");
if(process.env.ICAX_PYTHON) {
  const exporter=fileURLToPath(new URL("../../apps/tube-designer/templates/_shared/profile_export_runtime.py",import.meta.url));
  const output=execFileSync(process.env.ICAX_PYTHON,["-c",[
    "import importlib.util, json, sys",
    "spec=importlib.util.spec_from_file_location('exporter',sys.argv[1])",
    "module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)",
    "result=[]",
    "for profile in json.load(sys.stdin):",
    " writer=module._Writer()",
    " for contour in profile['contours']: module._contour(writer,contour)",
    " result.append(writer.count)",
    "print(json.dumps(result))",
  ].join("\n"),exporter],{input:JSON.stringify(exportProfiles),encoding:"utf8"});
  assert.ok(JSON.parse(output).every(n=>n>0));
}
// Automatic drawing joins are one undo step, exact, and limited to the new endpoints.
{
  const line=(id,a,b)=>({id,kind:"line",x1:a[0],y1:a[1],x2:b[0],y2:b[1]});
  const draw=(entities,points,tool="line",snapEnabled=true)=>{
    const state=createInitialSketchState();state.tool=tool;state.snapEnabled=snapEnabled;
    state.section.entities=structuredClone(entities);
    for(const p of points)handleCadPoint(state,state.section,p,{sampleStep:.001,snapTolerance:.1});
    finishCadCommand(state,state.section,{});
    return state.section;
  };
  const a=line("a",[-20,0],[0,0]), b=line("b",[20,10],[40,10]);
  let d=draw([a,b],[[0,0],[20,10]]);
  assert.equal(d.entities.length,1);assert.equal(d.history.length,1);
  assert.equal(d.entities[0].points.length,4);
  assert.equal(JSON.parse(d.history[0]).entities.length,2);
  d=draw([a],[[0,0],[20,0]]);
  assert.deepEqual(d.entities[0].points,[[-20,0],[0,0],[20,0]],"keep the shared collinear node editable");
  d=draw([a],[[0,0],[10,10],[20,0]],"arc");
  assert.equal(d.entities.length,1);
  assert.deepEqual(d.entities[0].segments.map(s=>s.kind),["line","circleArc"]);
  for(const tool of ["polyline","spline"]) {
    d=draw([a],[[0,0],[10,10],[20,0]],tool);
    assert.equal(d.entities.length,1,`${tool} connects on completion`);
  }
  assert.equal(draw([a],[[0,0],[20,0]],"line",false).entities.length,2,"disabled snapping disables automatic joins");
  assert.equal(draw([a],[[.001,0],[20,0]]).entities.length,2,"do not bridge an unsnapped gap");
  assert.equal(draw([a,line("branch",[0,0],[0,20])],[[0,0],[20,0]]).entities.length,3,"ambiguous endpoints require explicit selection");
  assert.equal(draw([a],[[-10,0],[0,20]]).entities.length,2,"a middle node does not merge whole curves");
  const left={...line("left",[0,0],[-20,0]),brokenStart:true};
  const right={...line("right",[20,0],[0,0]),brokenEnd:true};
  d=draw([left,right],[[-20,0],[0,20],[20,0]],"polyline");
  assert.equal(d.entities.length,1);assert.equal(d.entities[0].closed,false);
  assert.equal(d.entities[0].brokenStart,true);assert.equal(d.entities[0].brokenEnd,true);
}
console.log("Sketch geometry regression tests passed");
