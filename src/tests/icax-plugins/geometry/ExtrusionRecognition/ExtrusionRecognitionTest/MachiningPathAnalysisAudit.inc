#include <ExtrusionRecognition/MachiningPathAnalysisWork.h>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <gp_Circ.hxx>

namespace machining_path_audit
{
constexpr double Pi=3.14159265358979323846;
using Status=R::EMachiningPathStatus;
using Mode=R::EMachiningPathAxisMode;

gp_Vec Vector(const G::Vector3& value){return {value.X,value.Y,value.Z};}
gp_Pnt Point(const G::Point3& value){return {value.X,value.Y,value.Z};}

R::SSectionWiresResult PolygonSection(const std::vector<G::Point2>& points)
{
    R::SSectionWiresResult section;section.bSuccess=true;
    R::SSectionWire wire;wire.bClosed=true;
    for(std::size_t i=0;i<points.size();++i) {
        R::SSectionWireEdge edge;edge.Start=points[i];edge.End=points[(i+1)%points.size()];
        edge.Curve=G::Segment2{edge.Start,edge.End};
        edge.Range={0.,std::hypot(edge.End.X-edge.Start.X,edge.End.Y-edge.Start.Y)};
        wire.Edges.push_back(std::move(edge));
    }
    section.Wires.push_back(std::move(wire));return section;
}

R::SSectionWiresResult RectangleSection()
{
    return PolygonSection({{0,0},{10,0},{10,10},{0,10}});
}

R::SSectionWiresResult CircleSection(const double radius)
{
    R::SSectionWiresResult section;section.bSuccess=true;
    R::SSectionWire wire;wire.bClosed=true;
    R::SSectionWireEdge edge;G::Circle2 circle;circle.Placement.Location={5,5};circle.Radius=radius;
    edge.Curve=circle;edge.Range={0.,2*Pi};edge.Start=edge.End={5+radius,5};
    wire.Edges.push_back(std::move(edge));section.Wires.push_back(std::move(wire));return section;
}

R::SSectionWiresResult CurvedExtrusionSection()
{
    auto section=PolygonSection({{2,2},{2,8},{8,8},{8,2}});
    auto& curve=section.Wires.front().Edges.front();
    curve.Curve=G::Bezier2{{{2,2},{3,5},{2,8}}};curve.Range={0.,1.};return section;
}

TopoDS_Face PolygonFace(const std::vector<gp_Pnt>& points)
{
    BRepBuilderAPI_MakePolygon polygon;for(const auto& point:points)polygon.Add(point);
    polygon.Close();return BRepBuilderAPI_MakeFace(polygon.Wire()).Face();
}

R::SSectionFaceClassificationResult AllMachining(const TopoDS_Shape& shape)
{
    R::SSectionFaceClassificationResult classification;classification.bSuccess=true;
    classification.SplitGeometry=O::ConvertOpenCascadeShapeToBRep(shape,"","",0.001);
    classification.MachiningGeometry=classification.SplitGeometry;
    for(const auto& face:classification.SplitGeometry.Faces)classification.MachiningFaceIds.push_back(face.Id);
    return classification;
}

struct Analysis final
{
    R::SSectionFaceClassificationResult Classification;
    R::SMachiningEdgeAnalysisResult Edges;
    R::SMachiningPathAnalysisResult Paths;
    bool SourceUnchanged=false;
};

Analysis Analyze(const TopoDS_Shape& shape,const R::SSectionWiresResult& section)
{
    Analysis value;value.Classification=AllMachining(shape);
    const auto split=G::Persistence::Serialize(value.Classification.SplitGeometry);
    const auto machining=G::Persistence::Serialize(value.Classification.MachiningGeometry);
    value.Edges=R::MachiningEdgeAnalysisWork{}.Run(value.Classification,section);
    if(!value.Edges.bSuccess||!value.Edges.bComplete) {
        std::string reason="Upstream machining-edge fixture analysis failed";
        for(const auto& diagnostic:value.Edges.Diagnostics)reason+="; "+diagnostic;
        throw std::runtime_error(reason);
    }
    value.Paths=R::MachiningPathAnalysisWork{}.Run(value.Classification,section,value.Edges);
    value.SourceUnchanged=split==G::Persistence::Serialize(value.Classification.SplitGeometry)
        &&machining==G::Persistence::Serialize(value.Classification.MachiningGeometry);
    return value;
}

std::size_t Selected(const R::SMachiningPathAnalysisResult& paths)
{
    return std::count_if(paths.Edges.begin(),paths.Edges.end(),[](const auto& edge){return edge.bSelectedPath;});
}

bool SelectedSamples(const R::SMachiningPathAnalysisResult& result,const auto& predicate)
{
    std::size_t count=0;
    for(const auto& edge:result.Edges)if(edge.bSelectedPath)
        for(const auto& sample:edge.Samples){++count;if(!predicate(edge,sample))return false;}
    return count>0;
}

bool PlaneAxesEnterRectangle(const R::SMachiningPathAnalysisResult& paths)
{
    return SelectedSamples(paths,[](const auto& edge,const auto& sample) {
        const auto axis=Vector(sample.ToolAxis),tangent=Vector(sample.Tangent),normal=Vector(sample.FaceNormal);
        const gp_Vec fromCenter(gp_Pnt(0,5,5),Point(sample.Position));
        return edge.AxisMode==Mode::PlaneTangentCrossNormal&&sample.bToolAxisValid
            &&std::abs(axis.Magnitude()-1.)<1e-9&&std::abs(axis.Dot(tangent))<1e-9
            &&std::abs(axis.Dot(normal))<1e-9&&axis.Dot(fromCenter)<-4.99;
    });
}

TopoDS_Face PartiallyParallelCylinder(const double criticalParameter=.5)
{
    Handle(Geom_CylindricalSurface) support=new Geom_CylindricalSurface(
        gp_Ax3(gp_Pnt(0,5,5),gp_Dir(1,0,0),gp_Dir(0,1,0)),2.);
    // U(t)=1+4(t-criticalParameter)^2, V(t)=10(1-t). The
    // tangent is generator-parallel only at the independently known t*.
    const double firstU=1+4*criticalParameter*criticalParameter;
    const double middleU=firstU-4*criticalParameter;
    const double lastU=firstU+4*(1-2*criticalParameter);
    const double rightU=std::max(firstU,lastU)+.4;
    TColgp_Array1OfPnt2d poles(1,3);poles(1)={firstU,10};poles(2)={middleU,5};poles(3)={lastU,0};
    Handle(Geom2d_BezierCurve) curved=new Geom2d_BezierCurve(poles);
    BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(curved,support,0.,1.).Edge());
    const std::vector<gp_Pnt2d> points{{lastU,0},{rightU,0},{rightU,10},{firstU,10}};
    for(std::size_t i=0;i+1<points.size();++i) {
        Handle(Geom2d_TrimmedCurve) line=GCE2d_MakeSegment(points[i],points[i+1]);
        wire.Add(BRepBuilderAPI_MakeEdge(line,support,line->FirstParameter(),line->LastParameter()).Edge());
    }
    const auto face=BRepBuilderAPI_MakeFace(support,wire.Wire(),true).Face();
    if(!BRepLib::BuildCurves3d(face)||!BRepCheck_Analyzer(face).IsValid())
        throw std::runtime_error("Partially parallel cylinder fixture is invalid");
    return face;
}

TopoDS_Face SingularEndpointPlane()
{
    // An exact straight Bezier image whose original parameter derivative is
    // zero at t=0 and nonzero at t=1. No nearby point is needed to establish
    // that the first endpoint's requested tangent frame is undefined.
    TColgp_Array1OfPnt poles(1,3);
    poles(1)={0,0,0};poles(2)={0,0,0};poles(3)={0,10,0};
    Handle(Geom_BezierCurve) bottom=new Geom_BezierCurve(poles);
    BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(bottom,0.,1.).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,10,0),gp_Pnt(0,10,10)).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,10,10),gp_Pnt(0,0,10)).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,10),gp_Pnt(0,0,0)).Edge());
    const auto face=BRepBuilderAPI_MakeFace(wire.Wire()).Face();
    if(!BRepCheck_Analyzer(face).IsValid())throw std::runtime_error("Singular endpoint plane fixture is invalid");
    return face;
}

struct GroupRef final
{
    std::uint64_t FaceId=0;
    std::uint64_t WireId=0;
    const R::SMachiningBoundaryGroup* Group=nullptr;
};

std::vector<GroupRef> SourceGroups(const Analysis& analysis)
{
    std::vector<GroupRef> groups;
    for(const auto& face:analysis.Edges.Faces)for(const auto& wire:face.Wires)for(const auto& group:wire.Groups)
        groups.push_back({face.FaceId,wire.WireId,&group});
    return groups;
}

std::vector<const R::SMachiningPathEdgeAnalysis*> GroupRecords(const Analysis& analysis,const GroupRef& group)
{
    std::vector<const R::SMachiningPathEdgeAnalysis*> records;
    for(const auto& edge:analysis.Paths.Edges)
        if(edge.FaceId==group.FaceId&&edge.WireId==group.WireId&&edge.GroupId==group.Group->GroupId)
            records.push_back(&edge);
    return records;
}

bool OriginalTwoEnds(const R::SMachiningPathEdgeAnalysis& edge)
{
    if(edge.Samples.size()!=2||edge.SourceUses.empty())return false;
    const auto& first=edge.SourceUses.front();const auto& last=edge.SourceUses.back();
    return edge.Samples.front().SourceEdgeId==first.EdgeId&&edge.Samples.back().SourceEdgeId==last.EdgeId
        &&edge.Samples.front().SourceSpanKey==first.SpanKey&&edge.Samples.back().SourceSpanKey==last.SpanKey
        &&std::abs(edge.Samples.front().Parameter-(first.bReversed?first.LastParameter:first.FirstParameter))<1e-8
        &&std::abs(edge.Samples.back().Parameter-(last.bReversed?last.FirstParameter:last.LastParameter))<1e-8;
}

std::set<std::uint64_t> GroupIds(const GroupRef& group)
{
    std::set<std::uint64_t> ids;
    for(const auto& use:group.Group->SourceUses)ids.insert(use.EdgeId);
    return ids;
}

bool PreservesSourceGroup(const R::SMachiningPathEdgeAnalysis& edge,const GroupRef& group)
{
    const std::set<std::uint64_t> ids(edge.SourceEdgeIds.begin(),edge.SourceEdgeIds.end());
    return edge.PartIndex==1&&ids==GroupIds(group)&&OriginalTwoEnds(edge);
}

gp_Circ FixtureCircle()
{
    return gp_Circ(gp_Ax2(gp_Pnt(0,5,5),gp_Dir(1,0,0),gp_Dir(0,1,0)),2.);
}

void AddArcParts(BRepBuilderAPI_MakeWire& wire,const double last,const int parts)
{
    const auto circle=FixtureCircle();
    for(int i=0;i<parts;++i)
        wire.Add(BRepBuilderAPI_MakeEdge(circle,last*double(i)/parts,last*double(i+1)/parts).Edge());
}

TopoDS_Face FragmentedDisk(const int parts=12)
{
    BRepBuilderAPI_MakeWire wire;AddArcParts(wire,2*Pi,parts);
    const auto face=BRepBuilderAPI_MakeFace(wire.Wire()).Face();
    if(!BRepCheck_Analyzer(face).IsValid())throw std::runtime_error("Fragmented disk fixture is invalid");
    return face;
}

R::SSectionWiresResult RoundedSection()
{
    auto section=PolygonSection({{7,2},{7,5},{5,7},{2,7},{2,2}});
    G::Circle2 arc;arc.Placement.Location={5,5};arc.Radius=2;
    section.Wires.front().Edges[1].Curve=arc;
    section.Wires.front().Edges[1].Range={0.,Pi/2};return section;
}

TopoDS_Face FragmentedRoundedFace()
{
    BRepBuilderAPI_MakeWire wire;
    // The line and arc really are G1 at their junction. Their geometric
    // primitive kind, rather than tangency alone, must keep them separate.
    for(int i=0;i<3;++i)
        wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,7,2+i),gp_Pnt(0,7,3+i)).Edge());
    AddArcParts(wire,Pi/2,8);
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,5,7),gp_Pnt(0,2,7)).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,2,7),gp_Pnt(0,2,2)).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,2,2),gp_Pnt(0,7,2)).Edge());
    const auto face=BRepBuilderAPI_MakeFace(wire.Wire()).Face();
    if(!BRepCheck_Analyzer(face).IsValid())throw std::runtime_error("Fragmented rounded face fixture is invalid");
    return face;
}

R::SSectionWiresResult QuarterSection()
{
    auto section=PolygonSection({{7,5},{5,7},{5,5}});
    G::Circle2 arc;arc.Placement.Location={5,5};arc.Radius=2;
    section.Wires.front().Edges.front().Curve=arc;
    section.Wires.front().Edges.front().Range={0.,Pi/2};return section;
}

TopoDS_Shape SharedFragmentedArc()
{
    BRepBuilderAPI_MakeWire wire;AddArcParts(wire,Pi/2,12);
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,5,7),gp_Pnt(0,5,5)).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,5,5),gp_Pnt(0,7,5)).Edge());
    auto plane=BRepBuilderAPI_MakeFace(wire.Wire()).Face();plane.Reverse();
    Handle(Geom_CylindricalSurface) support=new Geom_CylindricalSurface(
        gp_Ax3(gp_Pnt(0,5,5),gp_Dir(1,0,0),gp_Dir(0,1,0)),2.);
    // Only half of the plane's arc has a second machining face. Thus the
    // plane's complete OnSkin arc has a real convex/free transition while it
    // remains a single logical path. Detached source topology is intentional.
    const auto cylinder=BRepBuilderAPI_MakeFace(support,0.,Pi/4,0.,10.,1e-7).Face();
    BRep_Builder builder;TopoDS_Compound pair;builder.MakeCompound(pair);
    builder.Add(pair,plane);builder.Add(pair,cylinder);return pair;
}

TopoDS_Face FragmentedSideFace()
{
    return PolygonFace({{0,0,2},{0,3,3.8},{0,7,6.2},{0,10,8},{0,5,9}});
}
}

int MachiningPathAnalysisAudit(const fs::path& directory)
{
    using namespace machining_path_audit;
    fs::create_directories(directory);VariantArray checks;std::size_t failures=0;
    const auto check=[&](const bool passed,const std::string& name) {
        checks.emplace_back(ObjectMap{{"name",name},{"passed",passed}});
        Save(directory/"checks.json",{{"status",std::string("running")},{"checks",checks}});
        if(!passed)++failures;
    };
    try {
        const auto section=RectangleSection();
        const auto plane=PolygonFace({{0,0,0},{0,10,0},{0,10,10},{0,0,10}});
        const auto flat=Analyze(plane,section);
        check(flat.Paths.bSuccess&&flat.Paths.bComplete&&Selected(flat.Paths)==4,
            "four finite OnSkin boundaries of a single plane become four selected paths");
        check(std::all_of(flat.Paths.Edges.begin(),flat.Paths.Edges.end(),[](const auto& edge)
            {return !edge.bSide&&edge.bOnSkinPath&&edge.Status==Status::Ready;}),
            "whole-edge OnSkin takes priority and is never recorded as an endpoint side");
        check(PlaneAxesEnterRectangle(flat.Paths),
            "plane axes enter the actual YZ rectangle toward its independently known interior center");
        check(SelectedSamples(flat.Paths,[](const auto& edge,const auto& sample) {
            if(edge.Samples.empty()||std::abs(edge.Samples.front().Position.Z)>1e-8
                ||std::abs(edge.Samples.back().Position.Z)>1e-8)return true;
            return sample.Tangent.Y>.999999&&sample.FaceNormal.X>.999999&&sample.ToolAxis.Z>.999999;
        }),"the Z=0 bottom boundary points toward the rectangle interior Z>0 while preserving its original +Y walk");

        const auto reverse=Analyze(plane.Reversed(),section);
        VariantArray reversedBottomEvidence;
        for(const auto& edge:reverse.Paths.Edges)if(edge.bSelectedPath&&!edge.Samples.empty()
            &&std::abs(edge.Samples.front().Position.Z)<1e-8&&std::abs(edge.Samples.back().Position.Z)<1e-8)
            for(const auto& sample:edge.Samples)reversedBottomEvidence.emplace_back(ObjectMap{
                {"pathKey",edge.Key},{"position",VariantArray{sample.Position.X,sample.Position.Y,sample.Position.Z}},
                {"tangent",VariantArray{sample.Tangent.X,sample.Tangent.Y,sample.Tangent.Z}},
                {"normal",VariantArray{sample.FaceNormal.X,sample.FaceNormal.Y,sample.FaceNormal.Z}},
                {"axis",VariantArray{sample.ToolAxis.X,sample.ToolAxis.Y,sample.ToolAxis.Z}}});
        Save(directory/"reverse-plane-evidence.json",{{"boundary",std::string("Z=0, geometric face interior is Z>0")},
            {"records",reversedBottomEvidence}});
        check(reverse.Paths.bSuccess&&reverse.Paths.bComplete&&Selected(reverse.Paths)==4
            &&SelectedSamples(reverse.Paths,[](const auto&,const auto& sample) {
                return sample.bToolAxisValid&&sample.FaceNormal.X<-.999999;
            }),"a reversed plane retains its reversed oriented normal for every selected boundary");
        check(SelectedSamples(reverse.Paths,[](const auto& edge,const auto& sample) {
            if(edge.Samples.empty()||std::abs(edge.Samples.front().Position.Z)>1e-8
                ||std::abs(edge.Samples.back().Position.Z)>1e-8)return true;
            return std::abs(sample.Tangent.Y)>.999999&&sample.FaceNormal.X<-.999999
                &&sample.ToolAxis.Z>.999999;
        }),"reversing the plane does not move its Z=0 boundary interior away from Z>0");
        check(SelectedSamples(flat.Paths,[](const auto& edge,const auto& sample) {
            if(edge.Samples.size()<2)return false;
            const gp_Vec travel(Point(edge.Samples.front().Position),Point(edge.Samples.back().Position));
            return travel.Dot(Vector(sample.Tangent))>0.&&OriginalTwoEnds(edge);
        }),"coedge reversal controls both original parameter traversal and the reported walking tangent");

        const auto nurbs=Analyze(BRepBuilderAPI_NurbsConvert(plane,true).Shape(),section);
        check(nurbs.Paths.bSuccess&&Selected(nurbs.Paths)==4&&PlaneAxesEnterRectangle(nurbs.Paths),
            "an exact planar NURBS support retains axes entering the same geometric rectangle");

        const auto box=Analyze(BRepPrimAPI_MakeBox(gp_Pnt(0,2,2),10,6,6).Shape(),section);
        check(box.Paths.bSuccess&&box.Paths.bComplete&&Selected(box.Paths)==0&&box.Paths.Edges.size()==24,
            "twelve convex OffSkin box edges retain twenty-four per-face evidence records and no cutting paths");
        check(std::all_of(box.Paths.Edges.begin(),box.Paths.Edges.end(),[](const auto& edge)
            {return edge.bConvexPath&&edge.bHasConvexEvidence&&!edge.bOnSkinPath&&!edge.bSide
                &&edge.AxisMode==Mode::None&&OriginalTwoEnds(edge)
                &&std::none_of(edge.Samples.begin(),edge.Samples.end(),[](const auto& sample){return sample.bToolAxisValid;});}),
            "non-skin convex evidence keeps two native endpoints without invented cutting axes");
        std::map<std::uint64_t,std::vector<const R::SMachiningPathEdgeAnalysis*>> bySource;
        bool singleBoxSources=true;
        for(const auto& edge:box.Paths.Edges) {
            singleBoxSources=singleBoxSources&&edge.SourceEdgeIds.size()==1;
            if(!edge.SourceEdgeIds.empty())bySource[edge.SourceEdgeIds.front()].push_back(&edge);
        }
        bool separateFacts=singleBoxSources&&bySource.size()==12;
        for(const auto& [id,uses]:bySource)separateFacts=separateFacts&&uses.size()==2
            &&uses[0]->FaceId!=uses[1]->FaceId&&uses[0]->Key!=uses[1]->Key
            &&uses[0]->bHasConvexEvidence&&uses[1]->bHasConvexEvidence;
        check(separateFacts,"each shared convex source edge preserves both face identities as geometry evidence");

        const auto convexSides=Analyze(BRepPrimAPI_MakeBox(gp_Pnt(0,0,2),10,6,6).Shape(),section);
        check(convexSides.Paths.bSuccess&&convexSides.Paths.bComplete
            &&std::count_if(convexSides.Paths.Edges.begin(),convexSides.Paths.Edges.end(),[](const auto& edge)
                {return edge.bSide&&!edge.bSelectedPath&&edge.bConvexPath&&edge.bHasConvexEvidence;})==8,
            "four convex endpoint sides remain per-face convex evidence without becoming cutting paths");

        // These fixtures deliberately require more than one original topology
        // use per upstream continuity group. Counting two endpoints on each
        // output alone would accept the previous per-fragment implementation.
        const auto disk=Analyze(FragmentedDisk(),CircleSection(2.));
        const auto diskGroups=SourceGroups(disk);GroupRef diskCircle;
        for(const auto& group:diskGroups)if(group.Group->CurveKind=="Circle"&&group.Group->SourceUses.size()==12)diskCircle=group;
        check(diskGroups.size()==1&&diskCircle.Group&&diskCircle.Group->bClosed,
            "fragmented full-circle input has twelve actual TopoEdges in one upstream closed Circle group");
        const auto diskRecords=diskCircle.Group?GroupRecords(disk,diskCircle):std::vector<const R::SMachiningPathEdgeAnalysis*>{};
        check(disk.Paths.bSuccess&&disk.Paths.bComplete&&disk.Paths.Edges.size()==1&&Selected(disk.Paths)==1
            &&diskRecords.size()==1&&PreservesSourceGroup(*diskRecords.front(),diskCircle),
            "one closed logical circle produces one path record and two axes rather than twelve records and twenty-four axes");
        check(diskRecords.size()==1&&diskRecords.front()->bClosed&&diskRecords.front()->SourceUses.size()==12
            &&diskRecords.front()->Samples.size()==2
            &&diskRecords.front()->Samples.front().SourceEdgeId!=diskRecords.front()->Samples.back().SourceEdgeId
            &&Point(diskRecords.front()->Samples.front().Position).Distance(Point(diskRecords.front()->Samples.back().Position))<1e-7,
            "closed logical-circle endpoints retain their different original edge identities even when their positions coincide");

        // The circle meets the profile only at (Y,Z)=(7,5), exactly at its
        // native parameter seam. Both one-edge and fragmented loops have no
        // physical open end, although their retained endpoint contact is true.
        const auto tangentSection=PolygonSection({{0,0},{7,0},{7,10},{0,10}});
        const auto closedSeam=Analyze(FragmentedDisk(1),tangentSection);
        const auto fragmentedSeam=Analyze(FragmentedDisk(),tangentSection);
        const auto closedContactIsReference=[](const Analysis& analysis,const std::size_t sourcePieces) {
            if(!analysis.Paths.bSuccess||!analysis.Paths.bComplete||analysis.Paths.Edges.size()!=1
                ||Selected(analysis.Paths)!=0)return false;
            const auto& loop=analysis.Paths.Edges.front();
            return loop.bClosed&&!loop.bOnSkinPath&&!loop.bSelectedPath&&!loop.bSide
                &&loop.SkinRelation==R::ESkinRelation::OffSkin&&loop.SourceUses.size()==sourcePieces
                &&loop.SourceUses.front().bStartOnSkin&&loop.SourceUses.back().bEndOnSkin
                &&loop.AxisMode==Mode::None&&OriginalTwoEnds(loop)
                &&Point(loop.Samples.front().Position).Distance(gp_Pnt(0,7,5))<1e-7
                &&Point(loop.Samples.front().Position).Distance(Point(loop.Samples.back().Position))<1e-7
                &&std::all_of(loop.Samples.begin(),loop.Samples.end(),[](const auto& sample) {
                    return sample.bPositionValid&&sample.bTangentValid&&!sample.bToolAxisValid;
                });
        };
        check(closedContactIsReference(closedSeam,1),
            "a real closed non-skin circle touching skin at its native seam has no side, cutting path or invented tool axes");
        check(closedContactIsReference(fragmentedSeam,12),
            "twelve continuous non-skin arcs touching skin only at the loop seam remain one closed reference rather than artificial endpoint sides");

        const auto diskBeforeGuard=G::Persistence::Serialize(disk.Classification.SplitGeometry);
        const auto diskMachiningBeforeGuard=G::Persistence::Serialize(disk.Classification.MachiningGeometry);
        auto unreliableCircle=disk.Edges;bool circleFlagChanged=false;
        for(auto& face:unreliableCircle.Faces)for(auto& wire:face.Wires)
            if(face.FaceId==diskCircle.FaceId&&wire.WireId==diskCircle.WireId) {
                wire.bOrdered=false;wire.Reason="fixture-explicitly-unreliable-circle-order";circleFlagChanged=true;
            }
        const auto blockedCircle=R::MachiningPathAnalysisWork{}.Run(disk.Classification,CircleSection(2.),unreliableCircle);
        std::set<std::uint64_t> diskSourceIds;
        for(const auto& edge:disk.Classification.MachiningGeometry.Edges)diskSourceIds.insert(edge.Id);
        bool circleUnresolved=false;
        if(blockedCircle.UnresolvedBoundaries.size()==1) {
            const auto& boundary=blockedCircle.UnresolvedBoundaries.front();
            circleUnresolved=boundary.FaceId==diskCircle.FaceId&&boundary.WireId==diskCircle.WireId
                &&boundary.Reason=="fixture-explicitly-unreliable-circle-order"
                &&std::set<std::uint64_t>(boundary.SourceEdgeIds.begin(),boundary.SourceEdgeIds.end())==diskSourceIds;
        }
        check(circleFlagChanged&&unreliableCircle.bSuccess&&unreliableCircle.bComplete
            &&diskCircle.Group&&diskCircle.Group->SourceUses.size()==12&&diskSourceIds.size()==12
            &&blockedCircle.bSuccess&&!blockedCircle.bComplete&&blockedCircle.Edges.empty()&&circleUnresolved
            &&diskBeforeGuard==G::Persistence::Serialize(disk.Classification.SplitGeometry)
            &&diskMachiningBeforeGuard==G::Persistence::Serialize(disk.Classification.MachiningGeometry),
            "an explicitly unordered twelve-fragment circle produces no invented path or axis and retains its complete unresolved boundary identity");

        BRep_Builder guardedPairBuilder;TopoDS_Compound guardedPairShape;guardedPairBuilder.MakeCompound(guardedPairShape);
        const auto firstDisk=FragmentedDisk();guardedPairBuilder.Add(guardedPairShape,firstDisk);
        gp_Trsf secondDiskPlacement;secondDiskPlacement.SetTranslation(gp_Vec(4,0,0));
        guardedPairBuilder.Add(guardedPairShape,BRepBuilderAPI_Transform(firstDisk,secondDiskPlacement,true).Shape());
        const auto guardedPair=Analyze(guardedPairShape,CircleSection(2.));
        if(guardedPair.Edges.Faces.size()!=2)throw std::runtime_error("Two-face unordered-boundary isolation fixture did not retain both faces");
        const auto pairBeforeGuard=G::Persistence::Serialize(guardedPair.Classification.SplitGeometry);
        const auto pairMachiningBeforeGuard=G::Persistence::Serialize(guardedPair.Classification.MachiningGeometry);
        auto unreliablePair=guardedPair.Edges;
        const auto blockedFaceId=unreliablePair.Faces.front().FaceId;
        const auto orderedFaceId=unreliablePair.Faces.back().FaceId;
        std::uint64_t blockedWireId=0;std::set<std::uint64_t> blockedSourceIds;
        for(auto& wire:unreliablePair.Faces.front().Wires) {
            blockedWireId=wire.WireId;wire.bOrdered=false;wire.Reason="fixture-one-unreliable-face";
            for(const auto& group:wire.Groups)for(const auto& use:group.SourceUses)blockedSourceIds.insert(use.EdgeId);
        }
        const auto isolatedPair=R::MachiningPathAnalysisWork{}.Run(guardedPair.Classification,CircleSection(2.),unreliablePair);
        bool pairUnresolved=false;
        if(isolatedPair.UnresolvedBoundaries.size()==1) {
            const auto& boundary=isolatedPair.UnresolvedBoundaries.front();
            pairUnresolved=boundary.FaceId==blockedFaceId&&boundary.WireId==blockedWireId
                &&boundary.Reason=="fixture-one-unreliable-face"
                &&std::set<std::uint64_t>(boundary.SourceEdgeIds.begin(),boundary.SourceEdgeIds.end())==blockedSourceIds;
        }
        check(blockedSourceIds.size()==12&&blockedFaceId!=orderedFaceId&&isolatedPair.bSuccess&&!isolatedPair.bComplete
            &&pairUnresolved&&isolatedPair.Edges.size()==1&&isolatedPair.Edges.front().FaceId==orderedFaceId
            &&isolatedPair.Edges.front().bSelectedPath&&isolatedPair.Edges.front().Status==Status::Ready
            &&OriginalTwoEnds(isolatedPair.Edges.front())
            &&std::all_of(isolatedPair.Edges.front().Samples.begin(),isolatedPair.Edges.front().Samples.end(),[](const auto& endpoint)
                {return endpoint.bPositionValid&&endpoint.bTangentValid&&endpoint.bToolAxisValid;})
            &&guardedPair.SourceUnchanged&&pairBeforeGuard==G::Persistence::Serialize(guardedPair.Classification.SplitGeometry)
            &&pairMachiningBeforeGuard==G::Persistence::Serialize(guardedPair.Classification.MachiningGeometry),
            "one unordered face is reported unresolved while the other ordered logical circle still produces its own two valid endpoint axes");

        const auto rounded=Analyze(FragmentedRoundedFace(),RoundedSection());
        const auto roundedGroups=SourceGroups(rounded);GroupRef roundedLine,roundedArc;
        for(const auto& group:roundedGroups) {
            if(group.Group->CurveKind=="Line"&&group.Group->SourceUses.size()==3)roundedLine=group;
            if(group.Group->CurveKind=="Circle"&&group.Group->SourceUses.size()==8)roundedArc=group;
        }
        check(rounded.Classification.MachiningGeometry.Edges.size()==14&&roundedGroups.size()==5
            &&roundedLine.Group&&roundedArc.Group&&roundedLine.Group->GroupId!=roundedArc.Group->GroupId,
            "a real G1 line-arc junction keeps its three-piece Line and eight-piece Circle as different geometric groups");
        bool roundedPreserved=rounded.Paths.Edges.size()==5&&Selected(rounded.Paths)==5;
        for(const auto& group:roundedGroups) {
            const auto records=GroupRecords(rounded,group);
            roundedPreserved=roundedPreserved&&records.size()==1&&PreservesSourceGroup(*records.front(),group);
        }
        check(rounded.Paths.bSuccess&&rounded.Paths.bComplete&&roundedPreserved,
            "four line groups and one circle group produce five logical paths despite fourteen source topology edges");

        const auto sharedArc=Analyze(SharedFragmentedArc(),QuarterSection());
        const auto sharedGroups=SourceGroups(sharedArc);GroupRef planeArc,cylinderArc;
        const auto sharedNative=O::ReconstructOpenCascadeShape(sharedArc.Classification.SplitGeometry);
        for(const auto& group:sharedGroups)if(group.Group->CurveKind=="Circle"&&!group.Group->SourceUses.empty()) {
            const auto type=BRepAdaptor_Surface(sharedNative.Faces.at(group.FaceId)).GetType();
            if(type==GeomAbs_Plane&&group.Group->SourceUses.size()==12)planeArc=group;
            if(type==GeomAbs_Cylinder) {
                BRepAdaptor_Curve curve(sharedNative.Edges.at(group.Group->SourceUses.front().EdgeId));
                if(std::abs(curve.Value(curve.FirstParameter()).X())<1e-7)cylinderArc=group;
            }
        }
        check(planeArc.Group&&cylinderArc.Group&&planeArc.FaceId!=cylinderArc.FaceId,
            "shared-arc input really has a twelve-piece plane group and a different cylindrical machining face");
        bool sharedOnSkin=true,hasConvex=false,hasNonConvex=false;
        if(planeArc.Group)for(const auto& use:planeArc.Group->SourceUses)
            for(const auto& edge:sharedArc.Edges.Edges)if(edge.EdgeId==use.EdgeId) {
                sharedOnSkin=sharedOnSkin&&edge.bSkinRelationValid&&edge.SkinRelation==R::ESkinRelation::OnSkin;
                if(edge.bConvexityValid&&edge.Convexity==R::EEdgeConvexity::Convex)hasConvex=true;
                else hasNonConvex=true;
            }
        check(planeArc.Group&&sharedOnSkin&&hasConvex&&hasNonConvex,
            "the complete OnSkin plane arc has a real convexity eligibility change where the cylindrical neighbor ends");
        const auto planeRecords=planeArc.Group?GroupRecords(sharedArc,planeArc):std::vector<const R::SMachiningPathEdgeAnalysis*>{};
        const auto cylinderRecords=cylinderArc.Group?GroupRecords(sharedArc,cylinderArc):std::vector<const R::SMachiningPathEdgeAnalysis*>{};
        check(sharedArc.Paths.bSuccess&&sharedArc.Paths.bComplete&&planeRecords.size()==1&&cylinderRecords.size()==1
            &&PreservesSourceGroup(*planeRecords.front(),planeArc)&&PreservesSourceGroup(*cylinderRecords.front(),cylinderArc)
            &&planeRecords.front()->bOnSkinPath&&cylinderRecords.front()->bOnSkinPath,
            "OnSkin ignores a raw convexity change and retains one logical arc record per incident face");
        check(planeRecords.size()==1&&cylinderRecords.size()==1&&planeRecords.front()->Key!=cylinderRecords.front()->Key
            &&planeRecords.front()->Samples.size()==2&&cylinderRecords.front()->Samples.size()==2
            &&planeRecords.front()->Samples.front().bToolAxisValid&&cylinderRecords.front()->Samples.front().bToolAxisValid
            &&std::abs(Vector(planeRecords.front()->Samples.front().ToolAxis).Dot(Vector(cylinderRecords.front()->Samples.front().ToolAxis)))<1e-8,
            "the shared logical arc keeps different plane and generator tool axes instead of merging face posture");

        const auto mixedSide=Analyze(FragmentedSideFace(),section);
        const auto sideGroups=SourceGroups(mixedSide);GroupRef mixedSideGroup;
        for(const auto& group:sideGroups)if(group.Group->CurveKind=="Line"&&group.Group->SourceUses.size()==3)mixedSideGroup=group;
        bool hasPartial=false,hasOff=false;
        if(mixedSideGroup.Group)for(const auto& use:mixedSideGroup.Group->SourceUses)
            for(const auto& edge:mixedSide.Edges.Edges)if(edge.EdgeId==use.EdgeId&&edge.bSkinRelationValid) {
                hasPartial=hasPartial||edge.SkinRelation==R::ESkinRelation::PartialOnSkin;
                hasOff=hasOff||edge.SkinRelation==R::ESkinRelation::OffSkin;
            }
        check(mixedSideGroup.Group&&hasPartial&&hasOff,
            "a three-piece straight side genuinely enters the work as Partial-Off-Partial source relationships");
        const auto mixedRecords=mixedSideGroup.Group?GroupRecords(mixedSide,mixedSideGroup):std::vector<const R::SMachiningPathEdgeAnalysis*>{};
        check(mixedSide.Paths.bSuccess&&mixedSide.Paths.bComplete&&mixedRecords.size()==1
            &&PreservesSourceGroup(*mixedRecords.front(),mixedSideGroup)&&mixedRecords.front()->bSide
            &&!mixedRecords.front()->bSelectedPath&&mixedRecords.front()->SourceUses.size()==3
            &&std::any_of(mixedRecords.front()->SourceUses.begin(),mixedRecords.front()->SourceUses.end(),[](const auto& use)
                {return use.SkinRelation==R::ESkinRelation::OffSkin;}),
            "one complete side retains its interior OffSkin piece and only its two outer endpoints");

        // Inject a valid per-use dihedral change into the already proven
        // three-fragment continuity group. This isolates the aggregation
        // contract from the separate native convexity work's geometry tests.
        auto variedSide=mixedSide.Edges;
        const auto variedIds=mixedSideGroup.Group?GroupIds(mixedSideGroup):std::set<std::uint64_t>{};
        std::size_t variedIndex=0;
        for(auto& edge:variedSide.Edges)if(variedIds.contains(edge.EdgeId)) {
            edge.bConvexityValid=true;
            edge.Convexity=(variedIndex++%2)?R::EEdgeConvexity::Concave:R::EEdgeConvexity::Convex;
        }
        const auto variedPaths=R::MachiningPathAnalysisWork{}.Run(mixedSide.Classification,section,variedSide);
        std::size_t variedRecords=0;bool variedFacts=true;
        for(const auto& edge:variedPaths.Edges)if(edge.FaceId==mixedSideGroup.FaceId
            &&edge.WireId==mixedSideGroup.WireId&&mixedSideGroup.Group&&edge.GroupId==mixedSideGroup.Group->GroupId) {
            ++variedRecords;variedFacts=variedFacts&&edge.SourceUses.size()==3&&OriginalTwoEnds(edge)
                &&edge.bSide&&!edge.bSelectedPath&&edge.bHasConvexEvidence&&edge.bMixedSourceConvexities
                &&!edge.bConvexityValid&&edge.AxisMode==Mode::None
                &&std::all_of(edge.SourceUses.begin(),edge.SourceUses.end(),[](const auto& use){return use.bConvexityValid;})
                &&std::none_of(edge.Samples.begin(),edge.Samples.end(),[](const auto& sample){return sample.bToolAxisValid;});
        }
        check(variedPaths.bSuccess&&variedPaths.bComplete&&variedIndex==3&&variedRecords==1&&variedFacts,
            "a non-skin continuity group retains mixed valid convex/concave evidence as one side with two endpoints and no axes");

        const auto sides=Analyze(PolygonFace({{0,0,0},{0,5,5},{0,10,0}}),section);
        check(sides.Paths.bSuccess&&Selected(sides.Paths)==1
            &&std::count_if(sides.Paths.Edges.begin(),sides.Paths.Edges.end(),[](const auto& edge){return edge.bSide;})==2,
            "endpoint-only skin contacts are two separate sides plus the one whole-skin path");
        check(std::all_of(sides.Paths.Edges.begin(),sides.Paths.Edges.end(),[](const auto& edge)
            {return !edge.bSide||(!edge.bSelectedPath&&edge.SkinRelation==R::ESkinRelation::PartialOnSkin);}),
            "side recording does not silently promote free PartialOnSkin edges to selected paths");
        const auto interior=Analyze(PolygonFace({{0,2,2},{0,5,2},{0,3,4}}),section);
        check(interior.Paths.bSuccess&&Selected(interior.Paths)==0
            &&std::none_of(interior.Paths.Edges.begin(),interior.Paths.Edges.end(),[](const auto& edge){return edge.bSide;}),
            "interior free edges with no skin contact remain unselected and are not sides");

        const gp_Ax2 alongX(gp_Pnt(0,5,5),gp_Dir(1,0,0),gp_Dir(0,1,0));
        const auto cylinder=Analyze(BRepPrimAPI_MakeCylinder(alongX,2,10).Shape(),CircleSection(2.));
        std::size_t cylinderSamples=0;bool cylinderAxes=true;
        for(const auto& edge:cylinder.Paths.Edges)if(edge.bSelectedPath&&edge.AxisMode==Mode::StraightGenerator)
            for(const auto& sample:edge.Samples){++cylinderSamples;cylinderAxes=cylinderAxes&&sample.bToolAxisValid
                &&std::abs(sample.ToolAxis.X)>.999999&&std::abs(sample.ToolAxis.Y)<1e-8&&std::abs(sample.ToolAxis.Z)<1e-8
                &&sample.ToolAxis.X*(5.-sample.Position.X)>4.99;}
        check(cylinder.Paths.bSuccess&&cylinder.Paths.bComplete&&cylinderSamples>0&&cylinderAxes,
            "cylinder rim generators point from X=0 or X=10 into the independently known finite cylinder interval");

        Handle(Geom_CylindricalSurface) cylinderWall=new Geom_CylindricalSurface(
            gp_Ax3(gp_Pnt(0,5,5),gp_Dir(1,0,0),gp_Dir(0,1,0)),2.);
        // An open quarter patch has reliable physical boundary ordering. A
        // detached full-period face also carries a repeated seam, which is
        // deliberately rejected by the separate unordered-boundary guard.
        const auto reversedCylinder=Analyze(BRepBuilderAPI_MakeFace(cylinderWall,0.,Pi/2,0.,10.,1e-7).Face().Reversed(),CircleSection(2.));
        VariantArray reversedCylinderEvidence;
        for(const auto& edge:reversedCylinder.Paths.Edges)for(const auto& sample:edge.Samples)
            reversedCylinderEvidence.emplace_back(ObjectMap{{"pathKey",edge.Key},{"selected",edge.bSelectedPath},
                {"valid",sample.bToolAxisValid},{"reason",sample.Reason},
                {"position",VariantArray{sample.Position.X,sample.Position.Y,sample.Position.Z}},
                {"tangent",VariantArray{sample.Tangent.X,sample.Tangent.Y,sample.Tangent.Z}},
                {"normal",VariantArray{sample.FaceNormal.X,sample.FaceNormal.Y,sample.FaceNormal.Z}},
                {"axis",VariantArray{sample.ToolAxis.X,sample.ToolAxis.Y,sample.ToolAxis.Z}}});
        Save(directory/"reverse-cylinder-evidence.json",{{"success",reversedCylinder.Paths.bSuccess},
            {"complete",reversedCylinder.Paths.bComplete},{"selected",static_cast<int>(Selected(reversedCylinder.Paths))},
            {"records",reversedCylinderEvidence}});
        check(reversedCylinder.Paths.bSuccess&&reversedCylinder.Paths.bComplete&&Selected(reversedCylinder.Paths)==4
            &&std::count_if(reversedCylinder.Paths.Edges.begin(),reversedCylinder.Paths.Edges.end(),[](const auto& edge)
                {return edge.Status==Status::Ready;})==2
            &&std::count_if(reversedCylinder.Paths.Edges.begin(),reversedCylinder.Paths.Edges.end(),[](const auto& edge)
                {return edge.Status==Status::ParallelToGenerator;})==2
            &&SelectedSamples(reversedCylinder.Paths,[](const auto& edge,const auto& sample) {
                if(sample.Status==Status::ParallelToGenerator)return !sample.bToolAxisValid;
                return edge.AxisMode==Mode::StraightGenerator&&sample.bToolAxisValid
                    &&std::abs(sample.ToolAxis.X)>.999999&&sample.ToolAxis.X*(5.-sample.Position.X)>4.99;
            }),"a reversed detached quarter-cylinder enters its same finite X=0 to X=10 trim while parallel sides stay pending");

        auto coneSection=CircleSection(2.);coneSection.Wires.push_back(CircleSection(4.).Wires.front());
        const auto cone=Analyze(BRepPrimAPI_MakeCone(alongX,2.,4.,10.,Pi/2).Shape(),coneSection);
        std::vector<gp_Vec> coneDirections;bool coneAxes=true;
        for(const auto& edge:cone.Paths.Edges)if(edge.bSelectedPath&&edge.AxisMode==Mode::StraightGenerator)
            for(const auto& sample:edge.Samples) {
                gp_Vec fromApex(gp_Pnt(-10,5,5),Point(sample.Position));fromApex.Normalize();
                if(sample.Status==Status::ParallelToGenerator)coneAxes=coneAxes&&!sample.bToolAxisValid;
                else {
                    coneAxes=coneAxes&&sample.bToolAxisValid&&std::abs(Vector(sample.ToolAxis).Dot(fromApex))>.999999
                        &&sample.ToolAxis.X*(5.-sample.Position.X)>0.;
                    coneDirections.push_back(Vector(sample.ToolAxis));
                }
            }
        check(cone.Paths.bSuccess&&cone.Paths.bComplete&&!coneDirections.empty()&&coneAxes,
            "cone rim axes follow its known apex generators toward the finite X=0 to X=10 cone interior");
        check(!coneDirections.empty()&&std::any_of(coneDirections.begin(),coneDirections.end(),[&](const auto& direction)
            {return std::abs(direction.Dot(coneDirections.front()))<.999;}),
            "the two ends of a quarter-cone rim have distinct generators rather than one constant revolution axis");

        TColgp_Array1OfPnt poles(1,3);poles(1)={0,2,2};poles(2)={0,3,5};poles(3)={0,2,8};
        Handle(Geom_BezierCurve) directrix=new Geom_BezierCurve(poles);
        Handle(Geom_SurfaceOfLinearExtrusion) support=new Geom_SurfaceOfLinearExtrusion(directrix,gp_Dir(1,0,0));
        const auto extrusion=Analyze(BRepBuilderAPI_MakeFace(support,0.,1.,0.,10.,1e-7).Face(),CurvedExtrusionSection());
        check(extrusion.Paths.bSuccess&&extrusion.Paths.bComplete&&Selected(extrusion.Paths)==4,
            "a curved directrix extrusion uses its exact source section to select all four skin boundaries");
        check(std::count_if(extrusion.Paths.Edges.begin(),extrusion.Paths.Edges.end(),[](const auto& edge)
            {return edge.Status==Status::ParallelToGenerator;})==2,
            "the two extrusion-generator boundaries are recorded separately as parallel and pending");
        check(std::all_of(extrusion.Paths.Edges.begin(),extrusion.Paths.Edges.end(),[](const auto& edge) {
            return edge.Status!=Status::ParallelToGenerator||(!edge.Samples.empty()
                &&std::all_of(edge.Samples.begin(),edge.Samples.end(),[](const auto& sample)
                    {return !sample.bToolAxisValid&&sample.Status==Status::ParallelToGenerator;}));
        }),"parallel-to-generator endpoints publish no fabricated tool axes");
        check(SelectedSamples(extrusion.Paths,[](const auto& edge,const auto& sample) {
            return edge.Status!=Status::Ready||(sample.bToolAxisValid&&std::abs(sample.ToolAxis.X)>.999999
                &&sample.ToolAxis.X*(5.-sample.Position.X)>4.99);
        }),"curved extrusion cross-boundaries enter their known X=0 to X=10 trim while parallel boundaries remain pending");

        TColgp_Array2OfPnt surfacePoles(1,3,1,2);
        for(int i=1;i<=3;++i){surfacePoles(i,1)=poles(i);surfacePoles(i,2)=poles(i).Translated(gp_Vec(10,0,0));}
        Handle(Geom_BezierSurface) ruled=new Geom_BezierSurface(surfacePoles);
        const auto ruledResult=Analyze(BRepBuilderAPI_MakeFace(ruled,0.,1.,0.,1.,1e-7).Face(),CurvedExtrusionSection());
        check(ruledResult.Paths.bSuccess&&ruledResult.Paths.bComplete
            &&std::count_if(ruledResult.Paths.Edges.begin(),ruledResult.Paths.Edges.end(),[](const auto& edge)
                {return edge.Status==Status::Ready&&edge.AxisMode==Mode::StraightGenerator;})==2
            &&std::count_if(ruledResult.Paths.Edges.begin(),ruledResult.Paths.Edges.end(),[](const auto& edge)
                {return edge.Status==Status::ParallelToGenerator;})==2,
            "an exact degree-one Bezier generator direction is certified without fitting or changing its surface");
        check(SelectedSamples(ruledResult.Paths,[](const auto& edge,const auto& sample) {
            return edge.Status!=Status::Ready||(sample.bToolAxisValid&&std::abs(sample.ToolAxis.X)>.999999
                &&sample.ToolAxis.X*(5.-sample.Position.X)>4.99);
        }),"degree-one Bezier rim generators enter the independently specified X=0 to X=10 ruled patch");

        const auto interiorParallel=Analyze(PartiallyParallelCylinder(),CircleSection(2.));
        bool interiorUsesOnlyEnds=false;
        for(const auto& edge:interiorParallel.Paths.Edges)if(edge.Status==Status::Ready&&edge.Samples.size()==2
            &&std::abs(edge.Samples.front().Position.X-edge.Samples.back().Position.X)>9.999)
            interiorUsesOnlyEnds=std::all_of(edge.Samples.begin(),edge.Samples.end(),[](const auto& endpoint)
                {return endpoint.bToolAxisValid&&(std::abs(endpoint.Parameter)<1e-8||std::abs(endpoint.Parameter-1)<1e-8);});
        check(interiorParallel.Paths.bSuccess&&interiorParallel.Paths.bComplete&&interiorUsesOnlyEnds
            &&std::none_of(interiorParallel.Paths.Edges.begin(),interiorParallel.Paths.Edges.end(),[](const auto& edge)
                {return edge.Status==Status::PartiallyParallelToGenerator;}),
            "an interior-only generator-parallel point does not expand the two endpoint analysis contract");

        const auto offGrid=Analyze(PartiallyParallelCylinder(.371),CircleSection(2.));
        check(offGrid.Paths.bSuccess&&offGrid.Paths.bComplete
            &&std::all_of(offGrid.Paths.Edges.begin(),offGrid.Paths.Edges.end(),[](const auto& edge)
                {return OriginalTwoEnds(edge);}),
            "an off-grid interior tangent does not add roots or intermediate logical-curve records");

        const auto endpointParallel=Analyze(PartiallyParallelCylinder(0.),CircleSection(2.));
        bool oneParallelEnd=false;
        for(const auto& edge:endpointParallel.Paths.Edges)if(edge.Status==Status::PartiallyParallelToGenerator) {
            const auto ready=std::count_if(edge.Samples.begin(),edge.Samples.end(),[](const auto& endpoint){return endpoint.bToolAxisValid;});
            const auto pending=std::count_if(edge.Samples.begin(),edge.Samples.end(),[](const auto& endpoint)
                {return !endpoint.bToolAxisValid&&endpoint.Status==Status::ParallelToGenerator;});
            oneParallelEnd=oneParallelEnd||(edge.Samples.size()==2&&ready==1&&pending==1);
        }
        check(endpointParallel.Paths.bSuccess&&endpointParallel.Paths.bComplete&&oneParallelEnd,
            "one generator-parallel endpoint and one regular endpoint remain two separately classified endpoint records");

        Handle(Geom_SphericalSurface) sphere=new Geom_SphericalSurface(
            gp_Ax3(gp_Pnt(0,5,5),gp_Dir(1,0,0),gp_Dir(0,1,0)),2.);
        const auto unsupported=Analyze(BRepBuilderAPI_MakeFace(sphere,0.,1.2,-.4,.4,1e-7).Face(),CircleSection(2*std::cos(.4)));
        check(unsupported.Paths.bSuccess&&unsupported.Paths.bComplete&&Selected(unsupported.Paths)==2
            &&std::all_of(unsupported.Paths.Edges.begin(),unsupported.Paths.Edges.end(),[](const auto& edge)
                {return !edge.bSelectedPath||(edge.Status==Status::UnsupportedSurface&&!edge.Reason.empty());}),
            "selected spherical boundaries are retained with an explicit unsupported-generator reason");
        check(SelectedSamples(unsupported.Paths,[](const auto&,const auto& sample){return !sample.bToolAxisValid;}),
            "unsupported surfaces never silently substitute a face normal or guessed generator");

        check(flat.SourceUnchanged&&reverse.SourceUnchanged&&nurbs.SourceUnchanged&&box.SourceUnchanged&&convexSides.SourceUnchanged
            &&disk.SourceUnchanged&&closedSeam.SourceUnchanged&&fragmentedSeam.SourceUnchanged
            &&rounded.SourceUnchanged&&sharedArc.SourceUnchanged&&mixedSide.SourceUnchanged
            &&sides.SourceUnchanged&&interior.SourceUnchanged&&cylinder.SourceUnchanged&&reversedCylinder.SourceUnchanged&&cone.SourceUnchanged
            &&extrusion.SourceUnchanged&&ruledResult.SourceUnchanged&&interiorParallel.SourceUnchanged
            &&offGrid.SourceUnchanged&&endpointParallel.SourceUnchanged&&unsupported.SourceUnchanged,
            "all real upstream edge analyses and path analyses leave split and machining CAD bytes unchanged");
        bool exactlyTwo=true;
        for(const auto* analysis:{&flat,&reverse,&nurbs,&box,&convexSides,&disk,&closedSeam,&fragmentedSeam,&rounded,&sharedArc,&mixedSide,&sides,&interior,&cylinder,&reversedCylinder,&cone,
            &extrusion,&ruledResult,&interiorParallel,&offGrid,&endpointParallel,&unsupported})
            for(const auto& edge:analysis->Paths.Edges)
                exactlyTwo=exactlyTwo&&OriginalTwoEnds(edge)
                    &&std::all_of(edge.Samples.begin(),edge.Samples.end(),[](const auto& endpoint)
                        {return endpoint.bPositionValid&&endpoint.bTangentValid;});
        check(exactlyTwo,"every regular selected, side, unsupported and unselected record contains exactly its two valid endpoints at the original parameters");
        bool closedKeepsEnds=false;
        for(const auto& edge:cylinder.Paths.Edges)if(edge.bSelectedPath&&edge.AxisMode==Mode::StraightGenerator&&edge.Samples.size()==2)
            closedKeepsEnds=closedKeepsEnds||(Point(edge.Samples.front().Position).Distance(Point(edge.Samples.back().Position))<1e-7
                &&std::abs(edge.Samples.front().Parameter-edge.Samples.back().Parameter)>1.);
        check(closedKeepsEnds,"a closed circular edge keeps two endpoint parameter records even when their positions coincide");

        const auto singular=Analyze(SingularEndpointPlane(),section);
        bool singularEndpointIsolated=false;
        for(const auto& edge:singular.Paths.Edges)if(edge.bSelectedPath&&edge.Status==Status::InvalidGeometry&&edge.Samples.size()==2) {
            const auto bad=std::count_if(edge.Samples.begin(),edge.Samples.end(),[](const auto& endpoint) {
                return endpoint.Status==Status::InvalidGeometry&&endpoint.bPositionValid
                    &&!endpoint.bTangentValid&&!endpoint.bToolAxisValid&&std::abs(endpoint.Parameter)<1e-8;
            });
            const auto good=std::count_if(edge.Samples.begin(),edge.Samples.end(),[](const auto& endpoint) {
                return endpoint.Status==Status::Ready&&endpoint.bPositionValid&&endpoint.bTangentValid
                    &&endpoint.bToolAxisValid&&std::abs(endpoint.Parameter-1)<1e-8;
            });
            singularEndpointIsolated=singularEndpointIsolated||(bad==1&&good==1);
        }
        check(singular.Paths.bSuccess&&!singular.Paths.bComplete&&singularEndpointIsolated&&singular.SourceUnchanged,
            "a zero-D1 endpoint keeps its exact point and failure while the other endpoint independently retains a ready axis without interior probing");
        R::SMachiningPathAnalysisOptions invalid;invalid.dAngularToleranceRadians=-1;
        check(!R::MachiningPathAnalysisWork{}.Run(flat.Classification,section,flat.Edges,invalid).bSuccess,
            "negative angular tolerance rejects the work request");
        Save(directory/"checks.json",{{"status",std::string(failures?"failed":"passed")},
            {"failedChecks",static_cast<double>(failures)},{"checks",checks}});return failures?1:0;
    } catch(const Standard_Failure& error) {
        Save(directory/"checks.json",{{"status",std::string("failed")},{"error",std::string(error.GetMessageString())},{"checks",checks}});return 1;
    } catch(const std::exception& error) {
        Save(directory/"checks.json",{{"status",std::string("failed")},{"error",std::string(error.what())},{"checks",checks}});return 1;
    }
}
