#define main ExistingCadBevelAcceptanceMain
#include "CadBevelAcceptanceBridge.cpp"
#undef main
#include "OpenCascadeResourceImport/TubeCadDocumentService.h"
#include "OpenCascadeResourceImport/OpenCascadeTubeCSGConverter.h"
#include "OpenCascadeResourceImport/TubeCadJson.h"
#include "Data/VariantSerializer.h"
#include "TemplateRuntime/StandardJsonCodec.h"
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Face.hxx>
#include <BRepLib.hxx>
#include <fstream>
#include <sstream>

int main(int argc,char** argv){try{
    Require(argc>1,"Exact source-stock document input required");std::ifstream input(argv[1]);
    const std::string text((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    const auto object=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(text).To<iCAX::Data::ObjectMap>();
    namespace G=iCAX::GeometryData;namespace T=G::Tube;
    auto graph=iCAX::CAD::TubeCadJson::Decode<T::CTubeNeutralGeometry>(object.at("geometry"));
    const auto& stock=std::get<T::SExtrudedRegionNode>(graph.SolidNodes.front().Data);
    G::Ellipse2 ellipse;ellipse.Placement.Location={194.84955592153878-3,350};
    ellipse.Placement.XDirection={0,1};ellipse.Placement.YDirection={-1,0};ellipse.MajorRadius=18;ellipse.MinorRadius=12;
    G::CurveSegment2 segment;segment.Curve=ellipse;segment.Range={0,2*std::acos(-1.),false};
    T::SWrappedVolumeNode wrapped;wrapped.Support={graph.BaseNodeID,stock.Section.Boundaries.front().ID,{}};
    wrapped.UVRegion.Boundaries={{"ellipse",{{"ellipse",segment}}}};wrapped.UpperNormalOffset=.2;
    wrapped.DepthRule=T::EWrappedDepthRule::FirstHostWall;
    graph.SolidNodes.push_back({"ellipse","ellipse",wrapped});
    graph.SolidNodes.push_back({"result","result",T::SBooleanNode{T::EBooleanOperation::Difference,{graph.RootNodeID,"ellipse"}}});graph.RootNodeID="result";
    const auto evaluated=iCAX::OpenCascade::EvaluateTubeCSG(graph);
    Require(evaluated.bOK&&!evaluated.Shape.IsNull(),"Wrapped graph evaluation failed");
    Require(BRepCheck_Analyzer(evaluated.Shape).IsValid(),"Raw OCCT graph is invalid");
    const auto neutral=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(evaluated.Shape,"","",.025);
    const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(neutral);
    std::cout<<"raw valid=1 rebuiltOK="<<rebuilt.bOK<<" rebuiltvalid="<<BRepCheck_Analyzer(rebuilt.Shape).IsValid()<<"\n";
    BRepBuilderAPI_NurbsConvert convert(evaluated.Shape,true);
    std::cout<<"native NURBS convertedvalid="<<BRepCheck_Analyzer(convert.Shape()).IsValid()<<"\n";
    const auto maxEdgeTolerance=[](const TopoDS_Shape& shape){double value=0;
        for(TopExp_Explorer edge(shape,TopAbs_EDGE);edge.More();edge.Next())value=std::max(value,BRep_Tool::Tolerance(TopoDS::Edge(edge.Current())));return value;};
    const auto originalTolerance=maxEdgeTolerance(evaluated.Shape),convertedTolerance=maxEdgeTolerance(convert.Shape());
    BRepLib::SameParameter(convert.Shape(),1e-7,true);
    const auto synchronizedTolerance=maxEdgeTolerance(convert.Shape());
    std::cout<<"max edge tolerance original="<<originalTolerance<<" converted="<<convertedTolerance<<" synchronized="<<synchronizedTolerance<<"\n";
    Require(synchronizedTolerance<=std::max(originalTolerance,convertedTolerance)+1e-7,"Parameter synchronization enlarged tolerance instead of repairing the representation");
    std::cout<<"synchronized NURBS convertedvalid="<<BRepCheck_Analyzer(convert.Shape()).IsValid()<<"\n";
    const auto synchronized=iCAX::OpenCascade::BuildOpenCascadeShape(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(convert.Shape(),"","",.025));
    std::cout<<"synchronized neutral rebuiltvalid="<<BRepCheck_Analyzer(synchronized.Shape).IsValid()<<"\n";
    for(int direction=0;direction<2;++direction){BRepAlgoAPI_Cut cut;NCollection_List<TopoDS_Shape> args,tools;
        args.Append(BRepBuilderAPI_Copy(direction?evaluated.Shape:synchronized.Shape,true,false).Shape());
        tools.Append(BRepBuilderAPI_Copy(direction?synchronized.Shape:evaluated.Shape,true,false).Shape());
        cut.SetArguments(args);cut.SetTools(tools);cut.SetNonDestructive(true);cut.SetToFillHistory(false);cut.Build();
        Require(cut.IsDone()&&BRepCheck_Analyzer(cut.Shape()).IsValid(),"Synchronized independent difference failed");
        const auto difference=Volume(cut.Shape());std::cout<<"synchronized directed difference="<<difference<<"\n";
        Require(difference<=1e-9,"Parameter synchronization changes actual material");}
    DiagnoseRoundTrip(convert.Shape(),rebuilt);
    for(const auto& [id,face]:rebuilt.Faces)if(!BRepCheck_Analyzer(face).IsValid()){
        BRepCheck_Analyzer analyzer(face);
        for(const auto kind:{TopAbs_WIRE,TopAbs_EDGE})for(TopExp_Explorer sub(face,kind);sub.More();sub.Next()){
            const auto result=analyzer.Result(sub.Current());if(result.IsNull())continue;
            std::cout<<"subshape type="<<static_cast<int>(kind)<<" status=";
            for(const auto status:result->Status())std::cout<<static_cast<int>(status)<<",";
            if(result->IsStatusOnShape(face)){std::cout<<" inface=";for(const auto status:result->StatusOnShape(face))std::cout<<static_cast<int>(status)<<",";}std::cout<<"\n";
        }
        BRepCheck_Face check(face);std::cout<<"face="<<id<<" intersect="<<check.IntersectWires()<<" classify="<<check.ClassifyWires()<<" orientation="<<check.OrientationOfWires()<<"\n";
        for(const auto& record:neutral.Faces)if(record.Id==id){std::cout<<"surface="<<record.Surface3Id<<" trimmed="<<record.SurfaceTrimmed<<" natural="<<record.NaturalRestriction<<" domain="<<record.Domain.UFirst<<","<<record.Domain.ULast<<","<<record.Domain.VFirst<<","<<record.Domain.VLast<<"\n";
            for(std::size_t i=0;i<record.WireIds.size();++i){std::cout<<"wire="<<record.WireIds[i]<<" orientation="<<static_cast<int>(record.WireOrientations[i])<<"\n";
                for(const auto& wire:neutral.Wires)if(wire.Id==record.WireIds[i])for(const auto& edge:wire.Coedges)std::cout<<"edge="<<edge.EdgeId<<" orientation="<<static_cast<int>(edge.Orientation)<<" range="<<edge.Range.First<<","<<edge.Range.Last<<"\n";}}}
    Require(rebuilt.bOK&&!rebuilt.Shape.IsNull()&&BRepCheck_Analyzer(rebuilt.Shape).IsValid(),"Exact rounded seam neutral transport failed");
    iCAX::Services::CServiceProvider services;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(services);
    const auto service=services.Resolve<iCAX::CAD::ITubeCadDocumentService>();Require(bool(service),"Service missing");
    const auto created=service->Create(graph);Require(BRepCheck_Analyzer(iCAX::OpenCascade::BuildOpenCascadeShape(created.Evaluation.Shape).Shape).IsValid(),"Registered service transport failed");
    std::cout<<"PASS exact R3 rounded seam graph and registered neutral transport\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
