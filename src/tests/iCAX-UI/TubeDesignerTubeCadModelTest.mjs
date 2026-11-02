import assert from "node:assert/strict";
import { addTubeCadHole, setTubeCadLocation, setTubeCadRotation, tubeCadPlacement, tubeCadRegion,
  editTubeCadCurve, mergeTubeCadDraft, setTubeCadSectionDimension, tubeCadLoopDimensions,
  isTubeBoundaryGeometry, isTubeCadDocumentSnapshot, tubeCadCoreSectionNode, tubeCadEditableNodes } from "../../apps/tube-designer/webpage/tubeCadModel.mjs";
import { planarCadDisplayCurve } from "../../apps/_shared/workbench/sketch/planarCurveDisplay.mjs";
const p=(X,Y,Z)=>({X,Y,Z});
const geometry={BaseNodeID:"base",RootNodeID:"base",SolidNodes:[{ID:"base",Label:"原材",Data:{type:"SExtrudedRegionNode",value:{
  First:0,Last:300,Frame:{Location:p(-150,0,0),XDirection:p(0,1,0),YDirection:p(0,0,1),ZDirection:p(1,0,0)},
  Section:{FillRule:0,Boundaries:[]},SectionPrimitives:[],SideAtlases:[]}},Relations:null,RemovalSemantics:null,Metadata:{source:"retained"}}],
  UVFeatures:[{ID:"retained-text",Data:{type:"fixture",value:{text:"保留文字"}}}],Metadata:{source:"input"}};
const preserved=structuredClone(geometry.SolidNodes[0]);
const id=addTubeCadHole(geometry,{diameter:8,station:250,offset:15});
const hole=geometry.SolidNodes.find(node=>node.ID===id);
assert.deepEqual(hole.Data.value.Frame.Location,p(100,15,0));
assert.equal(hole.Relations.Extent.Rule,1);
assert.equal(hole.Relations.Placement.Rule,1);
assert.deepEqual(geometry.SolidNodes[0],preserved);
const loop=hole.Data.value.Section.Boundaries[0],full=structuredClone(loop.Curves[0]);
loop.Curves=Array.from({length:4},(_,index)=>({...structuredClone(full),ID:`quarter-${index}`,
  Segment:{...structuredClone(full.Segment),Range:{First:index*Math.PI/2,Last:(index+1)*Math.PI/2,Reversed:false}}}));
const ranges=loop.Curves.map(curve=>structuredClone(curve.Segment.Range));
assert.equal(tubeCadLoopDimensions(hole,loop).kind,"circle");
setTubeCadSectionDimension(geometry,id,loop.ID,"radius",5);
assert.deepEqual(loop.Curves.map(curve=>curve.Segment.Curve.value.Radius),[5,5,5,5]);
assert.deepEqual(loop.Curves.map(curve=>curve.Segment.Range),ranges);
const partial=structuredClone(loop);partial.Curves.pop();
assert.equal(tubeCadLoopDimensions(hole,partial).kind,"curves");
setTubeCadLocation(geometry,id,"X",112);
assert.equal(tubeCadPlacement(hole).Location.X,112);
assert.equal(hole.Relations.Placement.LocalFrame.Location.Z,262);
setTubeCadLocation(geometry,id,"Y",18);
assert.equal(hole.Relations.Placement.LocalFrame.Location.X,18);
setTubeCadRotation(geometry,id,"Z",30);
for(const axis of ["XDirection","YDirection","ZDirection"])
  assert.ok(Math.abs(Math.hypot(...Object.values(hole.Data.value.Frame[axis]))-1)<1e-12);
const before=structuredClone(geometry),draft=structuredClone(geometry),accepted=structuredClone(geometry);
draft.SolidNodes.find(node=>node.ID===id).Label="继续输入的新名称";
accepted.SolidNodes.find(node=>node.ID===id).Data.value.First=-41;
accepted.Metadata.normalized="native";
const merged=mergeTubeCadDraft(before,draft,accepted);
assert.equal(merged.SolidNodes.find(node=>node.ID===id).Label,"继续输入的新名称");
assert.equal(merged.SolidNodes.find(node=>node.ID===id).Data.value.First,-41);
assert.equal(merged.Metadata.normalized,"native");
assert.deepEqual(merged.UVFeatures,geometry.UVFeatures);
const conflicting=structuredClone(accepted);conflicting.SolidNodes.find(node=>node.ID===id).Label="原生同时修改";
assert.throws(()=>mergeTubeCadDraft(before,draft,conflicting),/冲突/);
const profile={ID:"opening",Data:{type:"SOpeningProfileSweepNode",value:{ProfileField:{Keys:[{ContourParameter:{Value:0},
  Profile:{Segments:[{Curve:{type:"Segment2",value:{Start:{X:0,Y:0},End:{X:2,Y:1}}},Range:{First:0,Last:1,Reversed:false}}],Closed:false},Metadata:{evidence:"keep"}}]}}},Metadata:{source:"face"}};
geometry.SolidNodes.push(profile);
assert.equal(tubeCadRegion(profile).Boundaries.length,1);
editTubeCadCurve(geometry,"opening","profile:0","profile:0:curve:0",["End","Y"],1.5);
assert.equal(profile.Data.value.ProfileField.Keys[0].Profile.Segments[0].Curve.value.End.Y,1.5);
assert.equal(profile.Data.value.ProfileField.Keys[0].Metadata.evidence,"keep");
const spline={Segment:{Curve:{type:"BSpline2",value:{Degree:10,Poles:Array.from({length:24},(_,i)=>({X:i,Y:Math.sin(i)})),
  Knots:Array.from({length:15},(_,i)=>i),Multiplicities:[11,...Array(13).fill(1),11]}},Range:{First:0,Last:14,Reversed:false}}};
const start=performance.now(),display=planarCadDisplayCurve(spline);
assert.ok(display.points.length>100);assert.ok(display.points.flat().every(Number.isFinite));
assert.ok(performance.now()-start<1000,"High degree spline display must use an iterative basis");
const boundary={Section:structuredClone(hole.Data.value.Section),MachiningFaces:{Faces:[{Id:71}],Edges:[{Id:12}]}};
assert.equal(isTubeBoundaryGeometry(boundary),true);
assert.equal(isTubeBoundaryGeometry(geometry),false,"Construction graphs cannot stand in for the core geometry");
const sectionView=tubeCadCoreSectionNode(boundary);
assert.equal(sectionView.ID,"section");
assert.equal(sectionView.Data.value.Section,boundary.Section,"The editable 2D section references the core rather than a second recipe");
assert.equal(tubeCadEditableNodes(boundary,"sketch")[0].Data.value.Section,boundary.Section);
const facesBefore=structuredClone(boundary.MachiningFaces),sectionCurve=boundary.Section.Boundaries[0].Curves[0];
editTubeCadCurve(boundary,"section",boundary.Section.Boundaries[0].ID,sectionCurve.ID,["Radius"],6);
assert.equal(sectionView.Data.value.Section.Boundaries[0].Curves[0].Segment.Curve.value.Radius,6);
assert.deepEqual(boundary.MachiningFaces,facesBefore,"2D section input preserves the explicit faces for native reconstruction and validation");
assert.equal(isTubeCadDocumentSnapshot({geometry:boundary,construction:geometry,revision:"1"}),true);
assert.equal(isTubeCadDocumentSnapshot({geometry,revision:"1"}),false,"Old CSG-as-geometry snapshots are rejected");
assert.equal(isTubeCadDocumentSnapshot({geometry:boundary,revision:"1"}),false,"Construction and core geometry have separate explicit fields");
const topologyBefore={Faces:[{Id:"9007199254740993",Tolerance:0,Metadata:{}},{Id:"9007199254740994",Tolerance:0,Metadata:{}}]},
  topologyDraft=structuredClone(topologyBefore),topologyAccepted=structuredClone(topologyBefore);
topologyDraft.Faces[0].Tolerance=.01;topologyAccepted.Faces.reverse();topologyAccepted.Faces[0].Metadata.normalized=true;
const topologyMerged=mergeTubeCadDraft(topologyBefore,topologyDraft,topologyAccepted);
assert.equal(topologyMerged.Faces[0].Id,"9007199254740994");
assert.equal(topologyMerged.Faces[1].Id,"9007199254740993");
assert.equal(topologyMerged.Faces[1].Tolerance,.01,"BRep edits merge by exact topology Id rather than array position");
assert.equal(topologyMerged.Faces[0].Metadata.normalized,true);
console.log("PASS neutral CAD host coordinates, rotations, draft merge, opening profiles and iterative curve display");
