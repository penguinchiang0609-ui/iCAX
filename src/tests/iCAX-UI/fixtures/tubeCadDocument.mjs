import { addTubeCadHole } from "../../../apps/tube-designer/webpage/tubeCadModel.mjs";

// Current native snapshot/neutral document contract for routing unit tests.
// Real evaluation and persistence are covered by the unified native suites.
export function createTubeCadFixture(length=300,withHole=true) {
  const p=(X,Y,Z)=>({X,Y,Z});
  const circle=(id,radius)=>({ID:id,Curves:[{ID:`${id}:curve`,Segment:{Curve:{type:"Circle2",value:{
    Placement:{Location:{X:0,Y:0},XDirection:{X:1,Y:0},YDirection:{X:0,Y:1}},Radius:radius}},
    Range:{First:0,Last:Math.PI*2,Reversed:false}}}]});
  const geometry={Version:1,BaseNodeID:"stock",RootNodeID:"stock",SolidNodes:[{ID:"stock",Label:"原材",Data:{type:"SExtrudedRegionNode",value:{
    First:0,Last:length,Frame:{Location:p(-length/2,0,0),XDirection:p(0,1,0),YDirection:p(0,0,1),ZDirection:p(1,0,0)},
    Section:{FillRule:0,Boundaries:[circle("outer",20),circle("inner",18)]},SectionPrimitives:[],SideAtlases:[]}},
    Relations:null,RemovalSemantics:null,Metadata:{source:"retained"}}],UVFeatures:[],Metadata:{source:"routing-fixture"}};
  if(withHole)addTubeCadHole(geometry,{diameter:8,station:length/2});
  return geometry;
}

export function tubeCadFixtureResult(part,geometry,revision="1",resourceVersion=1) {
  return {partEntityId:part.entityId,resourceVersion,revision,documentResourceId:`document-${part.entityId}`,documentResourceVersion:Number(revision),
    geometry:structuredClone(geometry),nodes:geometry.SolidNodes.map(node=>({nodeId:node.ID,kind:node.Data.type,base:node.ID===geometry.BaseNodeID,
      sketchEditable:node.Data.type==="SExtrudedRegionNode",geometryEditable:node.Data.type==="SExtrudedRegionNode"}))};
}
