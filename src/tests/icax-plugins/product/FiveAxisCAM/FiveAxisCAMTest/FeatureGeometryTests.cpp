#include "pch.h"
#include <OpenCascadeResourceImport/GeometryFeatureAnalysisService.h>
#include <OpenCascadeResourceImport/CadReferenceService.h>
#include <OpenCascadeResourceImport/CadBRepImportService.h>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <Task/TaskStatus.h>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRep_Builder.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <STEPControl_Reader.hxx>
#include <TopoDS_Compound.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <gp_Elips.hxx>
#include <gp_Circ.hxx>
#include <numbers>
#include <filesystem>
#include <fstream>
#include <OpenCascadeResourceImport/SurfaceRegionService.h>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepTools.hxx>
#include <Geom2d_Curve.hxx>
#include <BRep_Tool.hxx>

namespace
{
    using namespace iCAX::Geometry;
    auto Service()
    {
        iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
        return provider.Resolve<IGeometryFeatureAnalysisService>();
    }
    auto Model(const TopoDS_Shape& shape){return iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"feature test","geometry-tests",0.1);}
    TopoDS_Wire Polygon(std::initializer_list<gp_Pnt> points)
    {
        BRepBuilderAPI_MakePolygon polygon;for(const auto& p:points)polygon.Add(p);polygon.Close();return polygon.Wire();
    }
    TopoDS_Wire Circle(double radius,gp_Pnt center={0,0,0})
    {return BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(center,gp::DZ()),radius)).Edge()).Wire();}
    auto Analyze(const TopoDS_Shape& face)
    {const auto model=Model(face);return Service()->AnalyzeFace(model,model.Faces.front().Id,{});}
    TopoDS_Face Stadium(double rightRadius=5,bool inward=false)
    {
        const gp_Pnt a(20,-rightRadius,0),b(20,rightRadius,0),c(0,5,0),d(0,-5,0);
        BRepBuilderAPI_MakeWire wire;
        wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(a,gp_Pnt(20+(inward?-rightRadius:rightRadius),0,0),b).Value()).Edge());
        wire.Add(BRepBuilderAPI_MakeEdge(b,c).Edge());
        wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(c,gp_Pnt(inward?5:-5,0,0),d).Value()).Edge());
        wire.Add(BRepBuilderAPI_MakeEdge(d,a).Edge());
        return BRepBuilderAPI_MakeFace(wire.Wire()).Face();
    }
    TopoDS_Face SplitStadium(size_t start=0,bool reverse=false,double kink=0,double rightRadius=5)
    {
        std::vector<TopoDS_Edge> edges;
        const auto line=[&](gp_Pnt a,gp_Pnt b){edges.push_back(BRepBuilderAPI_MakeEdge(a,b).Edge());};
        const auto arcs=[&](double cx,double r,double first){
            const auto p=[&](double t){return gp_Pnt(cx+r*std::cos(t),r*std::sin(t),0);};
            for(int i=0;i<2;++i){const auto a=first+i*std::numbers::pi/2;
                edges.push_back(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(p(a),p(a+std::numbers::pi/4),p(a+std::numbers::pi/2)).Value()).Edge());}
        };
        line({0,-5,0},{10,-(5+rightRadius)/2+kink,0});line({10,-(5+rightRadius)/2+kink,0},{20,-rightRadius,0});
        arcs(20,rightRadius,-std::numbers::pi/2);
        line({20,rightRadius,0},{10,(5+rightRadius)/2,0});line({10,(5+rightRadius)/2,0},{0,5,0});
        arcs(0,5,std::numbers::pi/2);
        BRepBuilderAPI_MakeWire builder;for(size_t i=0;i<edges.size();++i)builder.Add(edges[(start+i)%edges.size()]);
        auto wire=builder.Wire();if(reverse)wire.Reverse();return BRepBuilderAPI_MakeFace(wire).Face();
    }
}
TEST(FeatureGeometry, ExactCircularBoundariesHaveDifferentTrimmedRegionRolesRegardlessOfWireOrder)
{
    auto outer=Polygon({{0,0,0},{100,0,0},{100,80,0},{0,80,0}});
    auto hole=Circle(5,{20,30,0});hole.Reverse();
    BRepBuilderAPI_MakeFace face(outer);face.Add(hole);auto model=Model(face.Face());
    auto& f=model.Faces.front();std::reverse(f.WireIds.begin(),f.WireIds.end());std::reverse(f.WireOrientations.begin(),f.WireOrientations.end());
    const auto result=Service()->AnalyzeFace(model,f.Id,{});ASSERT_TRUE(result.Complete);ASSERT_TRUE(result.Planar);
    ASSERT_EQ(2u,result.Boundaries.size());
    int circular=0,rectangular=0;
    for(const auto& b:result.Boundaries)
    {
        ASSERT_TRUE(b.Valid);ASSERT_TRUE(b.Closed);
        if(b.GeometryKind=="Circle") {++circular;EXPECT_EQ("InnerLoop",b.RegionRelation);EXPECT_NEAR(5,b.RadiusMm,1e-8);EXPECT_EQ(1u,b.Spans.size());}
        if(b.GeometryKind=="Rectangle") {++rectangular;EXPECT_EQ("OuterLoop",b.RegionRelation);EXPECT_NEAR(360,b.PerimeterMm,1e-8);}
    }
    EXPECT_EQ(1,circular);EXPECT_EQ(1,rectangular);
    const auto disk=Analyze(BRepBuilderAPI_MakeFace(Circle(8)).Face());
    ASSERT_EQ(1u,disk.Boundaries.size());EXPECT_EQ("Circle",disk.Boundaries[0].GeometryKind);EXPECT_EQ("OuterLoop",disk.Boundaries[0].RegionRelation);
}
TEST(FeatureGeometry, RectangleMergesOnlyCollinearForwardSegmentsAndRejectsTrapezoid)
{
    const auto rectangle=Analyze(BRepBuilderAPI_MakeFace(Polygon({{0,0,0},{20,0,0},{40,0,0},{40,10,0},{40,20,0},{0,20,0}})).Face());
    ASSERT_EQ(1u,rectangle.Boundaries.size());const auto& b=rectangle.Boundaries[0];EXPECT_EQ("Rectangle",b.GeometryKind);
    EXPECT_EQ(6u,b.Spans.size());EXPECT_NEAR(120,b.PerimeterMm,1e-8); // Source spans are never merged away.
    const auto trapezoid=Analyze(BRepBuilderAPI_MakeFace(Polygon({{0,0,0},{40,0,0},{30,20,0},{0,20,0}})).Face());
    ASSERT_TRUE(trapezoid.Boundaries[0].Valid);EXPECT_EQ("GeneralContour",trapezoid.Boundaries[0].GeometryKind);
}
TEST(FeatureGeometry, StadiumRequiresEqualSemicirclesAndTangentParallelSides)
{
    const auto slot=Analyze(Stadium());ASSERT_EQ(1u,slot.Boundaries.size());const auto& b=slot.Boundaries[0];
    EXPECT_TRUE(b.Valid);EXPECT_EQ("Slot",b.GeometryKind);EXPECT_NEAR(10,b.WidthMm,1e-7);EXPECT_NEAR(30,b.LengthMm,1e-7);
    EXPECT_NEAR(40+10*std::numbers::pi,b.PerimeterMm,1e-6);
    const auto different=Analyze(Stadium(8));EXPECT_TRUE(different.Boundaries[0].Valid);EXPECT_EQ("GeneralContour",different.Boundaries[0].GeometryKind);
}
TEST(FeatureGeometry, SplitStadiumKeepsSourceSpansAcrossCyclicStartsTraversalAndModelFrames)
{
    gp_Trsf transform;transform.SetRotation(gp_Ax1({},gp_Dir(1,2,3)),.71);transform.SetTranslationPart({14,-26,38});
    for(size_t start=0;start<8;++start)for(const bool reverse:{false,true}){
        SCOPED_TRACE(::testing::Message()<<"start="<<start<<" reverse="<<reverse);
        const auto model=Model(BRepBuilderAPI_Transform(SplitStadium(start,reverse),transform,true).Shape());
        const auto face=Service()->AnalyzeFace(model,model.Faces.front().Id,{});
        const auto patch=Service()->AnalyzePatch(model,{model.Faces.front().Id},{});
        for(const auto& boundaries:{face.Boundaries,patch.Boundaries}){
            ASSERT_EQ(1u,boundaries.size());const auto& b=boundaries.front();
            EXPECT_TRUE(b.Valid);EXPECT_TRUE(b.TopologyClosed);EXPECT_EQ("Slot",b.GeometryKind);EXPECT_EQ(8u,b.Spans.size());
            EXPECT_NEAR(10,b.WidthMm,1e-8);EXPECT_NEAR(30,b.LengthMm,1e-8);EXPECT_NEAR(40+10*std::numbers::pi,b.PerimeterMm,1e-7);
            EXPECT_TRUE(b.StandardBoundProven);EXPECT_LE(b.StandardDeviationUpperMm,1e-9);
        }
    }
}
TEST(FeatureGeometry, SplitEdgesDoNotTurnBentSidesOrUnequalEndRadiiIntoStandardSlots)
{
    for(const auto& shape:{SplitStadium(0,false,.2),SplitStadium(0,false,0,6)}){
        const auto result=Analyze(shape);ASSERT_EQ(1u,result.Boundaries.size());
        EXPECT_TRUE(result.Boundaries.front().Valid);EXPECT_EQ("GeneralContour",result.Boundaries.front().GeometryKind);
        EXPECT_FALSE(result.Boundaries.front().StandardBoundProven);
    }
}
TEST(FeatureGeometry, EllipseIsGeneralContourAndRotatedCircleKeepsModelFrame)
{
    const auto ellipse=Analyze(BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Elips(gp_Ax2({},gp::DZ()),10,5)).Edge()).Wire()).Face());
    EXPECT_TRUE(ellipse.Boundaries[0].Valid);EXPECT_EQ("GeneralContour",ellipse.Boundaries[0].GeometryKind);
    gp_Trsf rotation;rotation.SetRotation(gp_Ax1({},gp::DX()),std::numbers::pi/2);rotation.SetTranslationPart(gp_Vec(10,20,30));
    const auto transformed=Analyze(BRepBuilderAPI_Transform(BRepBuilderAPI_MakeFace(Circle(8)).Face(),rotation,true).Shape());
    const auto& b=transformed.Boundaries[0];EXPECT_EQ("Circle",b.GeometryKind);EXPECT_NEAR(10,b.Center[0],1e-7);EXPECT_NEAR(20,b.Center[1],1e-7);EXPECT_NEAR(30,b.Center[2],1e-7);EXPECT_NEAR(1,std::abs(b.Normal[1]),1e-7);
}
TEST(FeatureGeometry, PeriodicSeamsAreReportedInsteadOfInventingCutLoops)
{
    const auto sphere=Analyze(BRepPrimAPI_MakeSphere(30).Shape());ASSERT_TRUE(sphere.Complete);
    for(const auto& b:sphere.Boundaries){EXPECT_FALSE(b.Valid);EXPECT_EQ("GeneralContour",b.GeometryKind);EXPECT_FALSE(b.Diagnostics.empty());}
}
TEST(FeatureGeometry, CancellationBudgetAndUnknownFaceNeverReturnPartialSuccess)
{
    const auto model=Model(BRepBuilderAPI_MakeFace(Polygon({{0,0,0},{40,0,0},{40,20,0},{0,20,0}})).Face());
    auto options=SGeometryAnalysisOptions{};options.MaximumEdgeUses=2;
    EXPECT_THROW(Service()->AnalyzeFace(model,model.Faces[0].Id,options),std::runtime_error);
    options.MaximumEdgeUses=100;options.IsCancelled=[]{return true;};
    EXPECT_THROW(Service()->AnalyzeFace(model,model.Faces[0].Id,options),iCAX::Tasks::TaskCanceledException);
    options.IsCancelled={};EXPECT_THROW(Service()->AnalyzeFace(model,99999,options),std::invalid_argument);
}
namespace
{
    std::vector<std::uint64_t> FaceType(const iCAX::GeometryData::BRepModel& model,GeomAbs_SurfaceType type)
    {
        const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(model);std::vector<std::uint64_t> ids;
        for(const auto& [id,face]:built.Faces)if(BRepAdaptor_Surface(face).GetType()==type)ids.push_back(id);
        return ids;
    }
}
TEST(FeatureGeometry, PatchGraphRemovesCylinderSeamAndPreservesTwoExactCircularEnds)
{
    const auto model=Model(BRepPrimAPI_MakeCylinder(10,30).Shape());
    const auto result=Service()->AnalyzePatch(model,FaceType(model,GeomAbs_Cylinder),{});
    ASSERT_TRUE(result.Complete);EXPECT_EQ(2u,result.CancelledInternalUses);ASSERT_EQ(2u,result.Boundaries.size());
    for(const auto& b:result.Boundaries){EXPECT_TRUE(b.Valid);EXPECT_TRUE(b.Closed);EXPECT_EQ("Circle",b.GeometryKind);EXPECT_NEAR(10,b.RadiusMm,1e-7);ASSERT_FALSE(b.Spans.empty());EXPECT_NE(0u,b.Spans[0].FaceID);EXPECT_NE(0u,b.Spans[0].WireID);
        EXPECT_EQ("Unknown",b.RegionRelation); // coincident projections cannot establish an exterior
    }
}
TEST(FeatureGeometry, SphericalHeadAndBentTubeEndBoundariesDoNotRequireExtrusionAssumptions)
{
    const auto head=Model(BRepPrimAPI_MakeSphere(30,0,std::numbers::pi/2).Shape());
    const auto headResult=Service()->AnalyzePatch(head,FaceType(head,GeomAbs_Sphere),{});
    ASSERT_TRUE(headResult.Complete);ASSERT_EQ(1u,headResult.Boundaries.size());
    EXPECT_TRUE(headResult.Boundaries[0].Valid);EXPECT_EQ("Circle",headResult.Boundaries[0].GeometryKind);EXPECT_NEAR(30,headResult.Boundaries[0].RadiusMm,1e-6);
    EXPECT_GE(headResult.DegenerateUses,1u);
    const auto bend=Model(BRepPrimAPI_MakeTorus(100,10,std::numbers::pi/2).Shape());
    const auto bendResult=Service()->AnalyzePatch(bend,FaceType(bend,GeomAbs_Torus),{});
    ASSERT_TRUE(bendResult.Complete);ASSERT_EQ(2u,bendResult.Boundaries.size());
    for(const auto& b:bendResult.Boundaries){EXPECT_TRUE(b.Valid);EXPECT_EQ("Circle",b.GeometryKind);EXPECT_NEAR(10,b.RadiusMm,1e-6);}
}
TEST(FeatureGeometry, ClosedSolidHasNoPatchBoundaryAndCoincidentIndependentFacesAreNotWelded)
{
    const auto box=Model(BRepPrimAPI_MakeBox(10,20,30).Shape());
    const auto solid=Service()->AnalyzePatch(box,FaceType(box,GeomAbs_Plane),{});
    EXPECT_TRUE(solid.Complete);EXPECT_TRUE(solid.Boundaries.empty());EXPECT_EQ(24u,solid.CancelledInternalUses);
    const auto source=BRepBuilderAPI_MakeFace(Polygon({{0,0,0},{10,0,0},{10,20,0},{0,20,0}})).Face();
    TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);builder.Add(compound,source);builder.Add(compound,BRepBuilderAPI_Copy(source,true,false).Shape());
    const auto model=Model(compound);const auto separate=Service()->AnalyzePatch(model,FaceType(model,GeomAbs_Plane),{});
    EXPECT_EQ(0u,separate.CancelledInternalUses);ASSERT_EQ(2u,separate.Boundaries.size());
    for(const auto& b:separate.Boundaries)EXPECT_TRUE(b.Valid);
    builder.MakeCompound(compound);builder.Add(compound,source);builder.Add(compound,BRepBuilderAPI_Copy(source,true,false).Shape().Reversed());
    const auto reverseModel=Model(compound);const auto reverse=Service()->AnalyzePatch(reverseModel,FaceType(reverseModel,GeomAbs_Plane),{});
    EXPECT_EQ(0u,reverse.CancelledInternalUses);EXPECT_EQ(2u,reverse.Boundaries.size());
}
TEST(FeatureGeometry, SelectedFacePatchHasExplicitScopeAndBudget)
{
    const auto box=Model(BRepPrimAPI_MakeBox(10,20,30).Shape());
    EXPECT_THROW(Service()->AnalyzePatch(box,{},{}),std::invalid_argument);
    EXPECT_THROW(Service()->AnalyzePatch(box,{box.Faces[0].Id,box.Faces[0].Id},{}),std::invalid_argument);
    SGeometryAnalysisOptions options;options.MaximumEdgeUses=1;
    EXPECT_THROW(Service()->AnalyzePatch(box,{box.Faces[0].Id},options),std::runtime_error);
    const auto one=Service()->AnalyzePatch(box,{box.Faces[0].Id},{});ASSERT_EQ(1u,one.Boundaries.size());
    EXPECT_EQ("OuterLoop",one.Boundaries[0].RegionRelation); // planar wire relation does not grant machining qualification
}
TEST(FeatureGeometry, IgesSelectedAdjacentFacesExcludeInternalSeams)
{
    wchar_t path[32768]{};ASSERT_GT(GetEnvironmentVariableW(L"ICAX_FA_PATCH_IGES",path,32768),0u);
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    iCAX::CAD::SCadBRepImportRequest request;request.SourcePath=path;
    const auto imported=provider.Resolve<iCAX::CAD::ICadBRepImportService>()->Load(request);
    const auto& model=imported.Geometry;ASSERT_EQ(176u,model.Faces.size());
    SGeometryAnalysisOptions options;options.DistanceToleranceMm=1e-5;
    // These two strips meet along seven exact curve pairs, separated by six
    // openings. Their independently stored IGES edge IDs must not become cuts.
    const std::set<std::uint64_t> seams{2,13,24,35,39,50,61,6400,6391,6382,6373,6369,6360,6351};
    std::set<std::uint64_t> expected;
    for(auto id:{1u,176u}){
        const auto single=Service()->AnalyzePatch(model,{id},options);ASSERT_EQ(1u,single.Boundaries.size());
        for(const auto& span:single.Boundaries.front().Spans)expected.insert(span.EdgeID);
    }
    for(auto edge:seams){ASSERT_TRUE(expected.contains(edge));expected.erase(edge);}
    const auto patch=Service()->AnalyzePatch(model,{1,176},options);
    EXPECT_EQ(14u,patch.CancelledInternalUses);ASSERT_EQ(7u,patch.Boundaries.size());
    std::set<std::uint64_t> actual;
    for(const auto& boundary:patch.Boundaries){EXPECT_TRUE(boundary.TopologyClosed);for(const auto& span:boundary.Spans){
        EXPECT_FALSE(seams.contains(span.EdgeID));EXPECT_TRUE(actual.insert(span.EdgeID).second);
        const auto& edge=*std::find_if(model.Edges.begin(),model.Edges.end(),[&](const auto& e){return e.Id==span.EdgeID;});
        EXPECT_EQ(edge.Curve3Id,span.CurveID);EXPECT_EQ(edge.Range.First,span.First);EXPECT_EQ(edge.Range.Last,span.Last);
    }}
    EXPECT_EQ(expected,actual);EXPECT_EQ(114u,actual.size());
    // A hole wall in this same file has the opposite surface orientation;
    // its two shared uses still belong to the interior of the selected patch.
    const auto opposite=Service()->AnalyzePatch(model,{1,15},options);
    EXPECT_EQ(4u,opposite.CancelledInternalUses);
    for(const auto& boundary:opposite.Boundaries)for(const auto& span:boundary.Spans)
        EXPECT_FALSE((std::set<std::uint64_t>{3,12,695,682}).contains(span.EdgeID));
}
TEST(FeatureGeometry, IndependentAdjacentFaceEdgesCancelOnlyWhenBothFacesAreSelected)
{
    const auto left=BRepBuilderAPI_MakeFace(Polygon({{0,0,0},{10,0,0},{10,20,0},{0,20,0}})).Face();
    BRepBuilderAPI_MakeFace right(Polygon({{10,0,0},{20,0,0},{20,20,0},{10,20,0}}));
    right.Add(TopoDS::Wire(Circle(2,{15,10,0}).Reversed()));
    TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);builder.Add(compound,left);builder.Add(compound,right.Face());
    const auto model=Model(compound);ASSERT_EQ(2u,model.Faces.size());
    const auto first=Service()->AnalyzePatch(model,{model.Faces[0].Id},{});
    ASSERT_EQ(1u,first.Boundaries.size());EXPECT_EQ(4u,first.Boundaries[0].Spans.size());
    const auto second=Service()->AnalyzePatch(model,{model.Faces[1].Id},{});ASSERT_EQ(2u,second.Boundaries.size());
    const auto both=Service()->AnalyzePatch(model,{model.Faces[0].Id,model.Faces[1].Id},{});
    EXPECT_EQ(2u,both.CancelledInternalUses);ASSERT_EQ(2u,both.Boundaries.size());
    unsigned outer=0,holes=0;
    for(const auto& b:both.Boundaries){EXPECT_TRUE(b.TopologyClosed);
        if(b.RegionRelation=="OuterLoop"){++outer;EXPECT_EQ(6u,b.Spans.size());EXPECT_NEAR(80,b.PerimeterMm,1e-6);}
        if(b.RegionRelation=="InnerLoop"){++holes;EXPECT_EQ("Circle",b.GeometryKind);EXPECT_NEAR(2,b.RadiusMm,1e-6);}
        for(const auto& span:b.Spans){
            const auto& edge=*std::find_if(model.Edges.begin(),model.Edges.end(),[&](const auto& e){return e.Id==span.EdgeID;});
            EXPECT_EQ(edge.Curve3Id,span.CurveID);EXPECT_EQ(edge.Range.First,span.First);EXPECT_EQ(edge.Range.Last,span.Last);
        }
    }
    EXPECT_EQ(1u,outer);EXPECT_EQ(1u,holes);
}
TEST(FeatureGeometry, CrossSeamOpeningAndExteriorUsePatchNestingAtAnyScaleOrOrientation)
{
    for(const double rise:{0.,2.})for(const double scale:{1.,5.}){
        const auto left=BRepBuilderAPI_MakeFace(Polygon({{0,0,0},{10,0,0},{10,4,0},{7,4,0},{7,6,0},{10,6,0},{10,10,0},{0,10,0}})).Face();
        const auto right=BRepBuilderAPI_MakeFace(Polygon({{10,0,0},{20,0,rise},{20,10,rise},{10,10,0},{10,6,0},{13,6,.3*rise},{13,4,.3*rise},{10,4,0}})).Face();
        TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);builder.Add(compound,left);builder.Add(compound,right);
        gp_Trsf rotate;rotate.SetRotation(gp_Ax1({12,-8,4},gp_Dir(1,2,3)),.73);gp_Trsf resize;resize.SetScale({3,7,-2},scale);
        const auto model=Model(BRepBuilderAPI_Transform(compound,rotate*resize,true).Shape());
        std::vector<std::uint64_t> ids;for(const auto& face:model.Faces)ids.push_back(face.Id);
        const auto patch=Service()->AnalyzePatch(model,ids,{});ASSERT_EQ(2u,patch.Boundaries.size());EXPECT_EQ(4u,patch.CancelledInternalUses);
        unsigned outer=0,inner=0;
        for(const auto& b:patch.Boundaries){ASSERT_TRUE(b.TopologyClosed);EXPECT_TRUE(b.Valid);
            outer+=b.RegionRelation=="OuterLoop";inner+=b.RegionRelation=="InnerLoop";
            std::set<std::uint64_t> supports;for(const auto& span:b.Spans){supports.insert(span.FaceID);
                const auto edge=std::find_if(model.Edges.begin(),model.Edges.end(),[&](const auto& e){return e.Id==span.EdgeID;});ASSERT_NE(model.Edges.end(),edge);
                EXPECT_EQ(edge->Curve3Id,span.CurveID);EXPECT_EQ(edge->Range.First,span.First);EXPECT_EQ(edge->Range.Last,span.Last);
            }EXPECT_EQ(2u,supports.size());
        }EXPECT_EQ(1u,outer);EXPECT_EQ(1u,inner);
    }
}
TEST(FeatureGeometry, IndependentBentFacesJoinButSeparatedOrPointTouchingFacesDoNot)
{
    const auto left=BRepBuilderAPI_MakeFace(Polygon({{0,0,0},{10,0,0},{10,20,0},{0,20,0}})).Face();
    const auto check=[&](TopoDS_Shape right){TopoDS_Compound compound;BRep_Builder b;b.MakeCompound(compound);b.Add(compound,left);b.Add(compound,right);
        const auto model=Model(compound);return Service()->AnalyzePatch(model,{model.Faces[0].Id,model.Faces[1].Id},{});};
    const auto bent=check(BRepBuilderAPI_MakeFace(Polygon({{10,20,0},{10,0,0},{10,0,10},{10,20,10}})).Face());
    EXPECT_EQ(2u,bent.CancelledInternalUses);ASSERT_EQ(1u,bent.Boundaries.size());EXPECT_EQ(6u,bent.Boundaries[0].Spans.size());EXPECT_TRUE(bent.Boundaries[0].TopologyClosed);
    const auto reversed=check(BRepBuilderAPI_MakeFace(Polygon({{10,20,0},{10,0,0},{10,0,10},{10,20,10}})).Face().Reversed());
    EXPECT_EQ(2u,reversed.CancelledInternalUses);ASSERT_EQ(1u,reversed.Boundaries.size());EXPECT_EQ(6u,reversed.Boundaries[0].Spans.size());EXPECT_TRUE(reversed.Boundaries[0].TopologyClosed);
    for(const auto& face:{BRepBuilderAPI_MakeFace(Polygon({{10.001,0,0},{20,0,0},{20,20,0},{10.001,20,0}})).Face(),
        BRepBuilderAPI_MakeFace(Polygon({{10,20,0},{20,20,0},{20,30,0},{10,30,0}})).Face()}){
        const auto separate=check(face);EXPECT_EQ(0u,separate.CancelledInternalUses);EXPECT_EQ(2u,separate.Boundaries.size());
    }
}
TEST(FeatureGeometry, InwardSemicirclesAreNotStadiumAndStandardBoundsCoverAnalyticSegments)
{
    const auto inward=Analyze(Stadium(5,true));ASSERT_EQ(1u,inward.Boundaries.size());
    EXPECT_TRUE(inward.Boundaries[0].Valid);EXPECT_EQ("GeneralContour",inward.Boundaries[0].GeometryKind);
    for(const auto& shape:{Stadium(),BRepBuilderAPI_MakeFace(Circle(5)).Face(),BRepBuilderAPI_MakeFace(Polygon({{0,0,0},{10,0,0},{10,5,0},{0,5,0}})).Face()})
    {
        const auto a=Analyze(shape);ASSERT_EQ(1u,a.Boundaries.size());
        EXPECT_TRUE(a.Boundaries[0].StandardBoundProven);EXPECT_LE(a.Boundaries[0].StandardDeviationUpperMm,1e-12);
    }
}

TEST(FeatureGeometry, FreeformPatchClosureAndDisplaySamplingDoNotInventStandardShapeProof)
{
    const auto model=Model(BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Elips(gp_Ax2({},gp::DZ()),10,5)).Edge()).Wire()).Face());
    const auto result=Service()->AnalyzePatch(model,{model.Faces.front().Id},{});
    ASSERT_EQ(1u,result.Boundaries.size());const auto& b=result.Boundaries.front();
    EXPECT_TRUE(b.Closed);EXPECT_TRUE(b.TopologyClosed);EXPECT_FALSE(b.StandardBoundProven);
    EXPECT_EQ("GeneralContour",b.GeometryKind);EXPECT_EQ("OuterLoop",b.RegionRelation);
    ASSERT_GT(b.PreviewPoints.size(),12u);EXPECT_EQ(1u,b.Spans.size());
    EXPECT_NEAR(b.PreviewPoints.front()[0],b.PreviewPoints.back()[0],1e-8);
    EXPECT_NEAR(b.PreviewPoints.front()[1],b.PreviewPoints.back()[1],1e-8);
}

TEST(FeatureGeometry, PillarRetainedSkinClassifiesTwentyOpeningsAndOneExterior)
{
    wchar_t path[32768];ASSERT_GT(GetEnvironmentVariableW(L"ICAX_PILLAR_STEP",path,32768),0u);
    std::ifstream stream(std::filesystem::path(path),std::ios::binary);ASSERT_TRUE(stream.good());
    STEPControl_Reader reader;ASSERT_EQ(IFSelect_RetDone,reader.ReadStream("pillar.step",stream));reader.TransferRoots();
    const auto model=Model(reader.OneShape());
    const std::vector<std::uint64_t> faces{1};
    const auto patch=Service()->AnalyzePatch(model,faces,{});ASSERT_TRUE(patch.Complete);ASSERT_EQ(21u,patch.Boundaries.size());
    unsigned outer=0,inner=0;
    for(const auto& b:patch.Boundaries){EXPECT_TRUE(b.TopologyClosed);EXPECT_TRUE(b.Valid);outer+=b.RegionRelation=="OuterLoop";inner+=b.RegionRelation=="InnerLoop";}
    EXPECT_EQ(1u,outer);EXPECT_EQ(20u,inner);
}
TEST(FeatureGeometry, RealStampedBracketRecognizesBothLongSlotsFromExactEdges)
{
    char path[32768];ASSERT_GT(GetEnvironmentVariableA("ICAX_STAMPED_BRACKET_STEP",path,sizeof(path)),0u)<<"Set ICAX_STAMPED_BRACKET_STEP to the manufacturer model";
    STEPControl_Reader reader;ASSERT_EQ(IFSelect_RetDone,reader.ReadFile(path));reader.TransferRoots();
    const auto model=Model(reader.OneShape());const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(model);
    unsigned checked=0;
    for(const auto& [faceID,face]:built.Faces){
        if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane)continue;
        unsigned wires=0,splitWires=0;for(TopExp_Explorer e(face,TopAbs_WIRE);e.More();e.Next()){
            ++wires;unsigned edges=0;for(TopExp_Explorer w(e.Current(),TopAbs_EDGE);w.More();w.Next())++edges;if(edges==5)++splitWires;
        }
        if(wires!=6||splitWires!=2)continue;
        const auto result=Service()->AnalyzeFace(model,faceID,{});
        ASSERT_TRUE(result.Planar);ASSERT_EQ(6u,result.Boundaries.size());
        ++checked;unsigned slots=0,circles=0,splitSlots=0;
        for(const auto& b:result.Boundaries){
            if(b.RegionRelation!="InnerLoop")continue;
            EXPECT_TRUE(b.Valid);EXPECT_TRUE(b.Closed);
            if(b.GeometryKind=="Slot"){
                ++slots;EXPECT_TRUE(b.StandardBoundProven);EXPECT_LT(b.StandardDeviationUpperMm,1e-6);
                if(b.Spans.size()==5){++splitSlots;EXPECT_NEAR(3.2,b.WidthMm,1e-6);EXPECT_NEAR(9.7,b.LengthMm,1e-6);}
            }else if(b.GeometryKind=="Circle")++circles;
        }
        EXPECT_EQ(4u,slots);EXPECT_EQ(1u,circles);EXPECT_EQ(2u,splitSlots);
    }
    EXPECT_EQ(2u,checked);
}

TEST(FeatureGeometry, PlaneReferencesPreserveOrientedFacesOnBothSidesOfRealBracket)
{
    char path[32768];ASSERT_GT(GetEnvironmentVariableA("ICAX_STAMPED_BRACKET_STEP",path,sizeof(path)),0u)<<"Set ICAX_STAMPED_BRACKET_STEP to the manufacturer model";
    STEPControl_Reader reader;ASSERT_EQ(IFSelect_RetDone,reader.ReadFile(path));reader.TransferRoots();
    const auto model=Model(reader.OneShape());const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(model);
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    const auto references=provider.Resolve<ICadReferenceService>();unsigned reversed=0,forward=0;
    for(TopExp_Explorer e(built.Shape,TopAbs_FACE);e.More();e.Next()){
        const auto face=TopoDS::Face(e.Current());BRepAdaptor_Surface surface(face);if(surface.GetType()!=GeomAbs_Plane)continue;
        unsigned wires=0;for(TopExp_Explorer w(face,TopAbs_WIRE);w.More();w.Next())++wires;
        if(wires<6)continue;
        std::uint64_t id=0;for(const auto& [key,f]:built.Faces)if(f.IsSame(face)){id=key;break;}ASSERT_NE(0u,id);
        const auto analysis=Service()->AnalyzeFace(model,id,{});ASSERT_FALSE(analysis.Boundaries.empty());
        SCadReferenceQuery q;q.Mode=SCadReferenceQuery::Kind::Plane;q.FaceID=id;q.NearPoint=analysis.Boundaries.front().PreviewPoints.front();
        const auto result=references->Resolve(model,q);auto expected=surface.Plane().Axis().Direction();
        if(face.Orientation()==TopAbs_REVERSED){expected.Reverse();++reversed;}else ++forward;
        EXPECT_NEAR(expected.X(),result.Direction[0],1e-9);EXPECT_NEAR(expected.Y(),result.Direction[1],1e-9);EXPECT_NEAR(expected.Z(),result.Direction[2],1e-9);
    }
    EXPECT_GT(reversed,0u);EXPECT_GT(forward,0u);
}
