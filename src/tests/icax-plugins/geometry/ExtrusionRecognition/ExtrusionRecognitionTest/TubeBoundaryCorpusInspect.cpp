#include "OpenCascadeResourceImport/TubeCadJson.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "OpenCascadeResourceImport/OpenCascadeTubeCSGConverter.h"
#include "GeometryData/TubeNeutralPersistence.h"
#include "TemplateRuntime/StandardJsonCodec.h"
#include <Geom2d_Curve.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <queue>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace G=iCAX::GeometryData;namespace T=G::Tube;namespace O=iCAX::OpenCascade;
using iCAX::Data::ObjectMap;using iCAX::Data::VariantArray;
int SourceTopology(const std::filesystem::path& file) {
    std::ifstream stream(file,std::ios::binary);const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),{});
    const auto input=G::Persistence::Deserialize(bytes);const auto built=O::ReconstructOpenCascadeShape(input);
    if(!built.bOK)throw std::runtime_error("Source neutral BRep cannot be reconstructed");
    TopTools_IndexedMapOfShape faces,edges;TopExp::MapShapes(built.Shape,TopAbs_FACE,faces);
    struct Use{int Face;int Orientation;};std::vector<std::vector<Use>> owners(1);int degenerate=0,unknown=0;
    for(int f=1;f<=faces.Extent();++f) {
        auto face=faces(f);face.Orientation(TopAbs_FORWARD);
        for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
            if(BRep_Tool::Degenerated(TopoDS::Edge(it.Current()))){++degenerate;continue;}
            if(it.Current().Orientation()!=TopAbs_FORWARD&&it.Current().Orientation()!=TopAbs_REVERSED)++unknown;
            const int e=edges.Add(it.Current());if(owners.size()<=std::size_t(e))owners.resize(e+1);
            owners[e].push_back({f,int(it.Current().Orientation())});
        }
    }
    int free=0,multi=0,seams=0;std::vector<std::vector<std::pair<int,int>>> adjacency(faces.Extent()+1);
    for(std::size_t e=1;e<owners.size();++e) {
        const auto& uses=owners[e];if(uses.size()==1){++free;continue;}if(uses.size()!=2){++multi;continue;}
        const auto a=uses[0],b=uses[1];const int parity=a.Orientation==b.Orientation;
        if(a.Face==b.Face){++seams;if(parity)++unknown;continue;}
        adjacency[a.Face].push_back({b.Face,parity});adjacency[b.Face].push_back({a.Face,parity});
    }
    ObjectMap report{{"scope",std::string("Exact existing edge identities only; no geometric sewing or curve/trim editing")},{"source",file.string()},
        {"sourceFaces",faces.Extent()},{"uniquePhysicalEdges",edges.Extent()},{"freeEdges",free},{"nonManifoldEdges",multi},{"parameterSeams",seams},{"degenerateEdgeUses",degenerate},{"unknownBoundaryDirections",unknown}};
    const bool closed=free==0&&multi==0&&unknown==0;VariantArray components;std::vector<int> signs(faces.Extent()+1,-1);int conflicts=0;
    if(closed)for(int seed=1;seed<=faces.Extent();++seed)if(signs[seed]<0) {
        std::vector<int> members;std::queue<int> queue;queue.push(seed);signs[seed]=0;
        while(!queue.empty()) {const auto f=queue.front();queue.pop();members.push_back(f);
            for(const auto& [other,parity]:adjacency[f]){const int sign=signs[f]^parity;if(signs[other]<0){signs[other]=sign;queue.push(other);}else if(signs[other]!=sign)++conflicts;}}
        BRep_Builder builder;TopoDS_Shell shell;builder.MakeShell(shell);
        for(const auto f:members){auto face=faces(f);face.Orientation(signs[f]?TopAbs_REVERSED:TopAbs_FORWARD);builder.Add(shell,face);}shell.Closed(true);
        BRepBuilderAPI_MakeSolid maker(shell);auto solid=maker.Solid();const bool oriented=BRepLib::OrientClosedSolid(solid);
        const bool valid=oriented&&BRepCheck_Analyzer(solid,true).IsValid();GProp_GProps mass;BRepGProp::VolumeProperties(solid,mass);
        components.emplace_back(ObjectMap{{"faces",int(members.size())},{"orientationCertified",oriented},{"validSolid",valid},{"volumeMm3",mass.Mass()},
            {"sourceNativeFaceIdentityPreserved",true},{"curvesSupportsAndTrimsEdited",false}});
        if(valid)BRepTools::Write(solid,(file.parent_path()/("exact-source-component-"+std::to_string(components.size())+".brep")).string().c_str());
    }
    report["exactSharedTopologyClosed"]=closed;report["orientationConflicts"]=conflicts;report["components"]=components;
    report["inputNeutralUnchanged"]=G::Persistence::Serialize(input)==bytes;
    const auto output=file.parent_path()/"source-topology-diagnostic.json";std::ofstream out(output,std::ios::binary);out<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report);
    std::cout<<"faces="<<faces.Extent()<<" free="<<free<<" multi="<<multi<<" components="<<components.size()<<'\n';return 0;
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--source")return SourceTopology(argv[2]);
    if(argc!=2)return 2;const std::filesystem::path dir=argv[1];
    try {
        std::ifstream stream(dir/"candidate.ictcad",std::ios::binary);const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),{});
        const auto doc=T::Persistence::DeserializeDocument(bytes);T::CTubeNeutralGeometry graph;graph.BaseNodeID=graph.RootNodeID="nominal";
        T::SExtrudedRegionNode stock;stock.Section=doc.Geometry.Section;stock.First=-1;stock.Last=1;
        stock.Frame.XDirection={0,1,0};stock.Frame.YDirection={0,0,1};stock.Frame.ZDirection={1,0,0};graph.SolidNodes.push_back({"nominal","",stock});
        const auto result=O::EvaluateTubeCSG(graph,{});VariantArray diagnostics;for(const auto& message:result.Diagnostics)diagnostics.emplace_back(message);
        VariantArray loops;
        for(const auto& loop:doc.Geometry.Section.Boundaries) {
            double maxGap=0;ObjectMap kinds;VariantArray segments;std::vector<std::pair<gp_Pnt2d,gp_Pnt2d>> endpoints;
            for(const auto& item:loop.Curves) {
                const auto& segment=item.Segment;const auto type=std::to_string(segment.Curve.index());
                const auto support=O::BuildOpenCascadeCurve2(segment.Curve);gp_Pnt2d a,b;
                if(const auto* line=std::get_if<G::Segment2>(&segment.Curve)){a={line->Start.X,line->Start.Y};b={line->End.X,line->End.Y};if(segment.Range.Reversed)std::swap(a,b);}
                else {a=support->Value(segment.Range.Reversed?segment.Range.Last:segment.Range.First);b=support->Value(segment.Range.Reversed?segment.Range.First:segment.Range.Last);}
                endpoints.emplace_back(a,b);segments.emplace_back(ObjectMap{{"id",item.ID},{"kind",type},{"start",VariantArray{a.X(),a.Y()}},{"end",VariantArray{b.X(),b.Y()}}});
            }
            for(std::size_t i=0;i<endpoints.size();++i)maxGap=std::max(maxGap,endpoints[i].second.Distance(endpoints[(i+1)%endpoints.size()].first));
            loops.emplace_back(ObjectMap{{"loop",loop.ID},{"maximumEndpointGapMm",maxGap},{"segments",segments}});
        }
        ObjectMap report{{"nominalOK",result.bOK},{"diagnostics",diagnostics},{"loops",loops},{"section",iCAX::CAD::TubeCadJson::Encode(doc.Geometry.Section)}};
        const auto text=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report);std::ofstream out(dir/"section-diagnostic.json",std::ios::binary);out<<text;
        std::cout<<"nominal="<<result.bOK<<"\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
