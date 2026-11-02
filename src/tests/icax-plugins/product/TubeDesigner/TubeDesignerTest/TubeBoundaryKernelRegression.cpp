#include <Windows.h>
#include "OpenCascadeResourceImport/TubeBoundaryGeometry.h"
#include "OpenCascadeResourceImport/OpenCascadeTubeCSGConverter.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepReader.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "OpenCascadeResourceImport/OpenCascadeFaceIntegration.h"
#include "GeometryData/BRepPersistence.h"
#include "GeometryData/TubeNeutralPersistence.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cmath>

namespace O=iCAX::OpenCascade;
namespace G=iCAX::GeometryData;
namespace T=G::Tube;

T::SRegionLoop2 Rectangle(const char* id,double y0,double z0,double y1,double z1,bool inner=false)
{
    T::SRegionLoop2 loop;loop.ID=id;
    std::vector<G::Point2> p{{y0,z0},{y1,z0},{y1,z1},{y0,z1}};if(inner)std::reverse(p.begin(),p.end());
    for(int i=0;i<4;++i){G::CurveSegment2 c;c.Curve=G::Segment2{p[i],p[(i+1)%4]};loop.Curves.push_back({std::string(id)+std::to_string(i),c});}
    return loop;
}
TopoDS_Shape Cut(const TopoDS_Shape& shape,const TopoDS_Shape& tool)
{
    BRepAlgoAPI_Cut operation(shape,tool);operation.SetNonDestructive(true);operation.Build();
    if(!operation.IsDone()||!BRepCheck_Analyzer(operation.Shape(),true).IsValid())throw std::runtime_error("fixture cut failed");return operation.Shape();
}
double Area(const TopoDS_Shape& shape)
{
    double area=0.;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())
        area+=O::integration_detail::RefinedArea(TopoDS::Face(it.Current()));
    return area;
}
int main(int argc,char** argv)
{
    int checks=0;
    const auto save=[&](const char* status){if(argc>1){std::filesystem::create_directories(std::filesystem::path(argv[1]).parent_path());std::ofstream out(argv[1]);out<<"{\"status\":\""<<status<<"\",\"checks\":"<<checks<<"}";}};
    save("running");
    const auto require=[&](bool passed,const std::string& name){if(!passed)throw std::runtime_error(name);++checks;};
    try {
        T::SExtrudedRegionNode nominal;
        nominal.Section.Boundaries={Rectangle("outer",-10,-10,10,10),Rectangle("inner",-8,-8,8,8,true)};
        nominal.Frame.XDirection={0,1,0};nominal.Frame.YDirection={0,0,1};nominal.Frame.ZDirection={1,0,0};nominal.First=0;nominal.Last=100;
        const auto stock=Cut(BRepPrimAPI_MakeBox(gp_Pnt(0,-10,-10),100,20,20).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(-1,-8,-8),102,16,16).Shape());
        std::vector<std::pair<std::string,TopoDS_Shape>> cases;
        cases.emplace_back("plain",stock);
        cases.emplace_back("through-hole",Cut(stock,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(40,0,-15),gp_Dir(0,0,1)),2,30).Shape()));
        cases.emplace_back("blind-slot",Cut(stock,BRepPrimAPI_MakeBox(gp_Pnt(20,-3,8.5),10,6,10).Shape()));
        cases.emplace_back("open-slot",Cut(stock,BRepPrimAPI_MakeBox(gp_Pnt(-1,-3,8.5),16,6,10).Shape()));
        cases.emplace_back("curved-blind-floor",Cut(stock,BRepPrimAPI_MakeSphere(gp_Pnt(40,0,12),3).Shape()));
        cases.emplace_back("enclosed-cavity",Cut(stock,BRepPrimAPI_MakeSphere(gp_Pnt(40,0,9),.5).Shape()));
        const auto plane=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(80,0,0),gp_Dir(1,0,.2))).Face();
        cases.emplace_back("oblique-end",Cut(stock,BRepPrimAPI_MakeHalfSpace(plane,gp_Pnt(200,0,0)).Solid()));
        cases.emplace_back("separated-pieces",Cut(stock,BRepPrimAPI_MakeBox(gp_Pnt(45,-20,-20),10,40,40).Shape()));
        for(const auto& [name,source]:cases) {
            const auto before=G::Persistence::Serialize(O::ConvertOpenCascadeShapeToBRepWithDisplay(source,"","",[](const TopoDS_Face&,std::uint64_t){return G::Triangulation3{};}));
            const auto compiled=O::CompileTubeBoundary(source,nominal);
            require(!compiled.Geometry.Section.Boundaries.empty()&&!compiled.Geometry.MachiningFaces.Faces.empty(),name+" stores only the section and explicit faces");
            const auto restored=O::RestoreTubeBoundary(compiled.Geometry);
            require(BRepCheck_Analyzer(restored,true).IsValid(),name+" restores valid solids without source or construction data");
            require(O::ValidateTubeCSGShapeReplay(source,restored,1e-6).bEquivalent,name+" independent bidirectional material comparison");
            require(std::abs(Area(source)-Area(restored))<Area(source)*1e-6,name+" complete surface area preserved");
            const auto after=G::Persistence::Serialize(O::ConvertOpenCascadeShapeToBRepWithDisplay(source,"","",[](const TopoDS_Face&,std::uint64_t){return G::Triangulation3{};}));
            require(before==after,name+" source geometry unchanged");
            if(name=="plain")require(compiled.Geometry.MachiningFaces.Faces.size()==2,"plain tube stores two end faces, no stock-skin geometry");
            if(name=="blind-slot")require(compiled.Geometry.MachiningFaces.Faces.size()==7,"blind slot retains all four walls, floor and both tube ends");
            if(name=="separated-pieces"){int n=0;for(TopExp_Explorer it(restored,TopAbs_SOLID);it.More();it.Next())++n;require(n==2,"both separated tube pieces retained");}
            std::cout<<name<<" passed faces="<<compiled.Geometry.MachiningFaces.Faces.size()<<"\n";
        }
        auto chamberNominal=nominal;chamberNominal.Section.Boundaries={Rectangle("outer",-10,-10,10,10),Rectangle("cavity1",-8,-8,-1,8,true),Rectangle("cavity2",1,-8,8,8,true)};
        auto chambers=Cut(BRepPrimAPI_MakeBox(gp_Pnt(0,-10,-10),100,20,20).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(-1,-8,-8),102,7,16).Shape());
        chambers=Cut(chambers,BRepPrimAPI_MakeBox(gp_Pnt(-1,1,-8),102,7,16).Shape());
        const auto multi=O::CompileTubeBoundary(chambers,chamberNominal);
        require(O::ValidateTubeCSGShapeReplay(chambers,O::RestoreTubeBoundary(multi.Geometry),1e-6).bEquivalent,"two-cavity material region restores");
        auto roundNominal=nominal;roundNominal.Section.Boundaries.clear();
        for(const auto& [id,radius,inner]:{std::tuple{"outer",10.,false},std::tuple{"inner",8.,true}}){T::SRegionLoop2 loop;loop.ID=id;G::CurveSegment2 c;G::Circle2 circle;circle.Radius=radius;c.Curve=circle;c.Range={0,2*std::acos(-1.),inner};loop.Curves.push_back({std::string(id)+"/circle",c});roundNominal.Section.Boundaries.push_back(loop);}
        auto round=Cut(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(1,0,0)),10,100).Shape(),BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1,0,0),gp_Dir(1,0,0)),8,102).Shape());
        round=Cut(round,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(40,0,-15),gp_Dir(0,0,1)),2,30).Shape());
        const auto circular=O::CompileTubeBoundary(round,roundNominal);
        require(O::ValidateTubeCSGShapeReplay(round,O::RestoreTubeBoundary(circular.Geometry),1e-6).bEquivalent,"periodic round-tube skins and radial through-hole restore");
        {
            T::SExtrudedRegionNode fixtureStock;
            fixtureStock.Section.Boundaries={Rectangle("outer",-40,-30,40,30),Rectangle("inner",-37,-27,37,27,true)};
            fixtureStock.Frame.XDirection={0,1,0};fixtureStock.Frame.YDirection={0,0,1};fixtureStock.Frame.ZDirection={1,0,0};
            fixtureStock.First=0;fixtureStock.Last=400;
            for(const double wall:{3.,.01}) {
                auto thinStock=fixtureStock;
                thinStock.Section.Boundaries={Rectangle("outer",-40,-30,40,30),Rectangle("inner",-40+wall,-30+wall,40-wall,30-wall,true)};
                const auto original=Cut(BRepPrimAPI_MakeBox(gp_Pnt(0,-40,-30),400,80,60).Shape(),
                    BRepPrimAPI_MakeBox(gp_Pnt(-1,-40+wall,-30+wall),402,80-2*wall,60-2*wall).Shape());
                const auto compiled=O::CompileTubeBoundary(original,thinStock);
                require(compiled.Geometry.MachiningFaces.Faces.size()==2,"80x60 thin-ring ends remain the only explicit faces");
                const auto restored=O::RestoreTubeBoundary(compiled.Geometry);
                require(O::ValidateTubeCSGShapeReplay(original,restored,1e-6).bEquivalent,"80x60 thin-ring end domain restores complete material without UV-grid assumptions");
                require(std::abs(Area(original)-Area(restored))<Area(original)*1e-6,"thin-ring restoration preserves complete source area");
            }
            T::CTubeNeutralGeometry fixture;fixture.BaseNodeID="stock";fixture.RootNodeID="part";
            fixture.SolidNodes.push_back({"stock","80x60x3",fixtureStock});
            T::SExtrudedRegionNode tool;tool.Frame.Location={140,0,0};tool.First=27;tool.Last=33;
            T::SRegionLoop2 loop;loop.ID="four-arc-opening";
            for(int q=0;q<4;++q) {
                const double angle=q*std::acos(-1.)/2;
                G::Circle2 circle;circle.Radius=5;circle.Placement.XDirection={std::cos(angle),std::sin(angle)};
                circle.Placement.YDirection={-std::sin(angle),std::cos(angle)};
                G::CurveSegment2 segment;segment.Curve=circle;segment.Range={0,std::acos(-1.)/2,false};
                loop.Curves.push_back({"quarter-"+std::to_string(q),segment});
            }
            tool.Section.Boundaries.push_back(loop);fixture.SolidNodes.push_back({"hole","four arcs",tool});
            fixture.SolidNodes.push_back({"part","",T::SBooleanNode{T::EBooleanOperation::Difference,{"stock","hole"}}});
            const auto evaluated=O::EvaluateTubeCSG(fixture);
            require(evaluated.bOK&&!evaluated.Shape.IsNull(),"real CEF four-arc hole fixture evaluates");
            const auto before=G::Persistence::Serialize(O::ConvertOpenCascadeShapeToBRepWithDisplay(evaluated.Shape,"","",[](const TopoDS_Face&,std::uint64_t){return G::Triangulation3{};}));
            const auto compiled=O::CompileTubeBoundary(evaluated.Shape,fixtureStock);
            const auto restored=O::RestoreTubeBoundary(compiled.Geometry);
            require(O::ValidateTubeCSGShapeReplay(evaluated.Shape,restored,1e-6).bEquivalent,"80x60x3 length400 diameter10 four-arc wall hole restores exact material");
            require(std::abs(Area(evaluated.Shape)-Area(restored))<Area(evaluated.Shape)*1e-6,"four-arc wall hole preserves complete source area");
            require(before==G::Persistence::Serialize(O::ConvertOpenCascadeShapeToBRepWithDisplay(evaluated.Shape,"","",[](const TopoDS_Face&,std::uint64_t){return G::Triangulation3{};})),"four-arc wall hole does not mutate source CAD");
        }
        gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),.7);transform.SetTranslationPart(gp_Vec(5,10,15));
        auto movedNominal=nominal;movedNominal.Frame.Location={5,10,15};
        const auto vector=[&](G::Direction3 d){auto v=gp_Vec(d.X,d.Y,d.Z).Transformed(transform);return G::Direction3{v.X(),v.Y(),v.Z()};};
        movedNominal.Frame.XDirection=vector(nominal.Frame.XDirection);movedNominal.Frame.YDirection=vector(nominal.Frame.YDirection);movedNominal.Frame.ZDirection=vector(nominal.Frame.ZDirection);
        const auto moved=BRepBuilderAPI_Transform(cases[2].second,transform,true).Shape();const auto normalized=O::CompileTubeBoundary(moved,movedNominal);
        require(O::ValidateTubeCSGShapeReplay(cases[2].second,O::RestoreTubeBoundary(normalized.Geometry),1e-6).bEquivalent,"rotated source restores in normalized X coordinates");
        require(std::abs(normalized.LocalToDocument.Location.X-5)<1e-12&&std::abs(normalized.LocalToDocument.XDirection.X-std::cos(.7))<1e-12,"normalization preserves instance placement");
        auto lineNominal=nominal;int lineOrdinal=0;
        for(auto& loop:lineNominal.Section.Boundaries)for(auto& curve:loop.Curves) {
            const auto segment=std::get<G::Segment2>(curve.Segment.Curve);
            const double dx=segment.End.X-segment.Start.X,dy=segment.End.Y-segment.Start.Y,length=std::hypot(dx,dy);
            const bool reversed=(lineOrdinal++%2)==0;const auto point=reversed?segment.End:segment.Start;
            const G::Direction2 direction{(reversed?-dx:dx)/length,(reversed?-dy:dy)/length};
            curve.Segment.Curve=G::Line2{{{point.X-7*direction.X,point.Y-7*direction.Y},direction}};
            curve.Segment.Range={7,7+length,reversed};
        }
        const auto finiteLines=O::CompileTubeBoundary(stock,lineNominal);
        const auto lineBytes=T::Persistence::Serialize(finiteLines.Geometry);
        require(O::ValidateTubeCSGShapeReplay(stock,O::RestoreTubeBoundary(finiteLines.Geometry),1e-6).bEquivalent,
            "finite Line2 ranges and reversed uses generate the same exact stock material");
        require(T::Persistence::Serialize(finiteLines.Geometry)==lineBytes,
            "derived Segment2 canonicalization preserves core Line2 supports and original parameter ranges");
        auto incomplete=O::CompileTubeBoundary(stock,nominal).Geometry;incomplete.MachiningFaces.Faces.pop_back();
        bool rejected=false;try{O::RestoreTubeBoundary(incomplete);}catch(const std::exception&){rejected=true;}
        require(rejected,"missing end boundary is rejected instead of returning a nominal stock cap");
        auto reversed=O::CompileTubeBoundary(stock,nominal).Geometry;
        auto& face=reversed.MachiningFaces.Faces.front();face.Orientation=face.Orientation==G::ETopologyOrientation::Reversed?G::ETopologyOrientation::Forward:G::ETopologyOrientation::Reversed;
        for(auto& root:reversed.MachiningFaces.RootShapes)if(root.Kind==G::EBRepShapeKind::Face&&root.Id==face.Id)root.Orientation=face.Orientation;
        for(auto& compound:reversed.MachiningFaces.Compounds)for(auto& child:compound.Children)
            if(child.Kind==G::EBRepShapeKind::Face&&child.Id==face.Id)child.Orientation=face.Orientation;
        rejected=false;try{O::RestoreTubeBoundary(reversed);}catch(const std::exception&){rejected=true;}
        require(rejected,"inconsistent material-side orientation is rejected");
        face.Orientation=G::ETopologyOrientation::Internal;
        for(auto& root:reversed.MachiningFaces.RootShapes)if(root.Kind==G::EBRepShapeKind::Face&&root.Id==face.Id)root.Orientation=face.Orientation;
        for(auto& compound:reversed.MachiningFaces.Compounds)for(auto& child:compound.Children)
            if(child.Kind==G::EBRepShapeKind::Face&&child.Id==face.Id)child.Orientation=face.Orientation;
        rejected=false;try{O::RestoreTubeBoundary(reversed);}catch(const std::exception&){rejected=true;}
        require(rejected,"an INTERNAL occurrence in supplied machining faces remains invalid input");
        for(int arg=2;arg+1<argc;arg+=2) {
            const auto read=[](const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);
                if(!input)throw std::runtime_error("Missing real kernel input: "+file.string());
                return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)),{});};
            const auto documentBytes=read(argv[arg]),sourceBytes=read(argv[arg+1]);
            const auto document=T::Persistence::DeserializeDocument(documentBytes);
            const auto source=O::ReconstructOpenCascadeShape(G::Persistence::Deserialize(sourceBytes));
            require(source.bOK&&!source.Shape.IsNull(),"real source face collection reconstructs");
            const auto restored=O::RestoreTubeBoundary(document.Geometry);
            require(BRepCheck_Analyzer(restored,true).IsValid(),"real classified core restores valid closed material");
            require(std::abs(Area(source.Shape)-Area(restored))<Area(source.Shape)*1e-6,
                "real classified core preserves all original trim-domain boundary area");
            int solids=0;for(TopExp_Explorer it(source.Shape,TopAbs_SOLID);it.More();it.Next())++solids;
            if(solids)require(O::ValidateTubeCSGShapeReplay(source.Shape,restored,1e-6).bEquivalent,
                "real classified solid retains bidirectionally equal material");
            require(documentBytes==T::Persistence::Serialize(document)&&sourceBytes==read(argv[arg+1]),
                "real classified core and source bytes remain unchanged");
            const auto reopened=T::Persistence::DeserializeDocument(documentBytes);
            const auto cold=O::RestoreTubeBoundary(reopened.Geometry);
            require(T::Persistence::Serialize(reopened)==documentBytes
                &&O::ValidateTubeCSGShapeReplay(restored,cold,1e-6).bEquivalent,
                "real classified core independently reopens with exact original records and material");
            auto reversedCore=document.Geometry;bool hasClosedSpline=false;
            for(auto& loop:reversedCore.Section.Boundaries) {
                if(loop.Curves.size()!=1)continue;
                auto& segment=loop.Curves.front().Segment;
                if(!std::holds_alternative<G::NURBS2>(segment.Curve)
                    &&!std::holds_alternative<G::BSpline2>(segment.Curve)
                    &&!std::holds_alternative<G::Bezier2>(segment.Curve))continue;
                const auto support=O::BuildOpenCascadeCurve2(segment.Curve);
                if(support.IsNull()||!support->IsClosed())continue;
                segment.Range.Reversed=!segment.Range.Reversed;hasClosedSpline=true;
            }
            if(hasClosedSpline) {
                const auto reversedBytes=T::Persistence::Serialize(reversedCore);
                const auto reversedRestore=O::RestoreTubeBoundary(reversedCore);
                require(O::ValidateTubeCSGShapeReplay(restored,reversedRestore,1e-6).bEquivalent
                    &&std::abs(Area(restored)-Area(reversedRestore))<Area(restored)*1e-6,
                    "closed spline derived spans preserve reversed traversal and all material");
                require(reversedBytes==T::Persistence::Serialize(reversedCore)
                    &&documentBytes==T::Persistence::Serialize(document),
                    "closed spline operand normalization leaves every core support and range unchanged");
            }
        }
        std::cout<<"PASSED "<<checks<<" checks\n";
        save("passed");
        return 0;
    } catch(const Standard_Failure& e){save("failed");std::cerr<<"OCC: "<<e.GetMessageString()<<"\n";return 1;}
    catch(const std::exception& e){save("failed");std::cerr<<"FAILED after "<<checks<<" checks: "<<e.what()<<"\n";return 1;}
}
