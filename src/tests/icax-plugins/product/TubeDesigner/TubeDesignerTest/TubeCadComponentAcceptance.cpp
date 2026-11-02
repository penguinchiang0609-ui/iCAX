#include "OpenCascadeResourceImport/TubeCadDocumentService.h"
#include "OpenCascadeResourceImport/TubeCadJson.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepReader.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "GeometryData/TubeNeutralPersistence.h"
#include "Services/ServiceProvider.h"
#include "Services/ServiceRegistrationCatalog.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>
#include <iostream>
namespace G=iCAX::GeometryData;namespace T=G::Tube;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void Reject(F&& action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}Require(rejected,"Expected explicit component rejection");}
TopoDS_Shape Native(const G::BRepModel& body){const auto result=iCAX::OpenCascade::BuildOpenCascadeShape(body);Require(result.bOK&&!result.Shape.IsNull()&&BRepCheck_Analyzer(result.Shape).IsValid(),"Invalid body");return result.Shape;}
double Volume(const TopoDS_Shape& shape){GProp_GProps p;BRepGProp::VolumeProperties(shape,p,1e-10);return std::abs(p.Mass());}
void Equivalent(const TopoDS_Shape& a,const TopoDS_Shape& b){
    for(int direction=0;direction<2;++direction){BRepAlgoAPI_Cut cut;NCollection_List<TopoDS_Shape> args,tools;
        args.Append(BRepBuilderAPI_Copy(direction?a:b,true,false).Shape());tools.Append(BRepBuilderAPI_Copy(direction?b:a,true,false).Shape());
        cut.SetArguments(args);cut.SetTools(tools);cut.SetNonDestructive(true);cut.SetToFillHistory(false);cut.Build();
        Require(cut.IsDone()&&BRepCheck_Analyzer(cut.Shape()).IsValid(),"Invalid independent directed comparison");
        Require(Volume(cut.Shape())<=1e-8*std::max(1.,Volume(a)),"Connected selection changes material");}
}
T::SExtrudedRegionNode Stock(double start){
    T::SExtrudedRegionNode stock;stock.Last=40;stock.Frame.Location={start,0,0};
    stock.Frame.XDirection={0,1,0};stock.Frame.YDirection={0,0,1};stock.Frame.ZDirection={1,0,0};
    T::SRegionLoop2 loop;loop.ID="outer";const std::vector<G::Point2> p{{-10,-10},{10,-10},{10,10},{-10,10}};
    for(std::size_t i=0;i<4;++i){G::CurveSegment2 segment;segment.Curve=G::Segment2{p[i],p[(i+1)%4]};loop.Curves.push_back({"edge/"+std::to_string(i),segment});}
    stock.Section.Boundaries={loop};return stock;
}
int main(){try{
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    const auto service=provider.Resolve<iCAX::CAD::ITubeCadDocumentService>();Require(bool(service),"Missing registered CAD service");
    const auto edge=BRepBuilderAPI_MakeEdge(gp_Pnt(25,420,0),gp_Pnt(25,460,0)).Edge();
    const auto transported=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(edge,"","",.025);
    const auto curves=service->ReadPlanarCurves(transported,G::Placement3{});
    Require(curves.size()==1&&std::holds_alternative<G::Segment2>(curves[0].Curve),"Selected standalone exact line boundary lost its neutral root");
    T::CTubeNeutralGeometry graph;graph.BaseNodeID="left";graph.RootNodeID="source";
    graph.SolidNodes={{"left","left",Stock(0)},{"right","right",Stock(80)},
        {"source","source",T::SBooleanNode{T::EBooleanOperation::Union,{"left","right"}}}};
    const auto source=service->Create(graph);
    const auto left=source.Evaluation.ConstructionBodies.at("left"),right=source.Evaluation.ConstructionBodies.at("right");
    const auto anchor=service->FindInteriorAnchor(left);
    BRepClass3d_SolidClassifier classifier(TopoDS::Solid(Native(left)),gp_Pnt(anchor.X,anchor.Y,anchor.Z),1e-7);
    Require(classifier.State()==TopAbs_IN,"Anchor is not a certified interior material point");
    graph.SolidNodes.push_back({"piece","piece",T::SConnectedComponentNode{"source",anchor}});graph.RootNodeID="piece";
    const auto selected=service->Create(graph);Equivalent(Native(selected.Evaluation.Shape),Native(left));
    auto moved=selected.Document.Construction;std::get<T::SExtrudedRegionNode>(moved.SolidNodes[0].Data).Last=50;
    const auto changed=service->Edit(selected.Document,1,moved);
    Equivalent(Native(changed.Evaluation.Shape),Native(changed.Evaluation.ConstructionBodies.at("left")));
    Require(std::abs(Volume(Native(changed.Evaluation.Shape))-20000)<1e-6,"Selection froze finished geometry");
    auto opposite=graph;std::get<T::SConnectedComponentNode>(opposite.SolidNodes.back().Data).Anchor=service->FindInteriorAnchor(right);
    Equivalent(Native(service->Create(opposite).Evaluation.Shape),Native(right));
    const auto bytes=T::Persistence::Serialize(changed.Document);Require(bytes[7]=='5',"Document format was not advanced");
    const auto restored=T::Persistence::DeserializeDocument(bytes);Equivalent(Native(service->Evaluate(restored).Shape),Native(changed.Evaluation.Shape));
    const auto geometryBytes=T::Persistence::Serialize(restored.Geometry);Require(geometryBytes[7]=='1',"Geometry format was not advanced");
    const auto json=iCAX::CAD::TubeCadJson::Encode(restored.Geometry);
    const auto decoded=iCAX::CAD::TubeCadJson::Decode<T::CTubeBoundaryGeometry>(json);
    Require(T::Persistence::Serialize(decoded)==geometryBytes,"Typed JSON dropped component references or anchor");
    auto obsolete=bytes;obsolete[7]='3';Reject([&]{T::Persistence::DeserializeDocument(obsolete);});
    auto missing=graph;std::get<T::SConnectedComponentNode>(missing.SolidNodes.back().Data).Child="absent";Reject([&]{service->Create(missing);});
    auto cyclic=graph;std::get<T::SConnectedComponentNode>(cyclic.SolidNodes.back().Data).Child="piece";Reject([&]{service->Create(cyclic);});
    auto outside=graph;std::get<T::SConnectedComponentNode>(outside.SolidNodes.back().Data).Anchor={-999,0,0};Reject([&]{service->Create(outside);});
    auto touching=graph;std::get<T::SConnectedComponentNode>(touching.SolidNodes.back().Data).Anchor={0,0,0};Reject([&]{service->Create(touching);});
    BRep_Builder builder;TopoDS_Compound overlap;builder.MakeCompound(overlap);builder.Add(overlap,Native(left));builder.Add(overlap,BRepBuilderAPI_Copy(Native(left),true,false).Shape());
    const auto overlapping=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(overlap,"","",.025);
    T::CTubeNeutralGeometry ambiguous;ambiguous.BaseNodeID="overlap";ambiguous.RootNodeID="piece";
    T::SCompositeVolumeNode exact;exact.BRepResourceID="overlap-body";
    ambiguous.SolidNodes={{"overlap","overlap",exact},{"piece","piece",T::SConnectedComponentNode{"overlap",anchor}}};
    Reject([&]{service->Create(ambiguous,{{"overlap-body",overlapping}});});
    Reject([&]{service->FindInteriorAnchor(overlapping);});
    std::cout<<"PASS: strict component selection, reevaluation, independent bidirectional geometry, typed JSON and binary versions, invalid/ambiguous/cyclic rejection\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
