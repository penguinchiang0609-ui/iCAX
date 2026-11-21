#include <ExtrusionRecognition/MachiningFaceCutCoverageWork.h>
#include <ExtrusionRecognition/MachiningFaceCutCoverageIntervals.h>
#include <Geom2d_BSplineCurve.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <BRepCheck_Result.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Ellipse.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <gp_Elips.hxx>
#include <gp_Circ2d.hxx>
#include <gp_Elips2d.hxx>

namespace machining_face_cut_coverage_audit
{
using Kind=R::EFaceCutCoverageKind;

namespace final_state_contract_audit
{
using namespace R;
#include <ExtrusionRecognition/MachiningFaceCutCoverageFinalState.inc>
}

namespace side_evidence_contract_audit
{
using namespace R;
namespace I=R::machining_face_cut_coverage;
#include <ExtrusionRecognition/MachiningFaceCutCoverageEvidence.inc>
}

namespace projection_contract_audit
{
struct CurveUse {Handle(Geom2d_Curve) Curve;double First=0.,Last=0.;std::uint64_t EdgeId=0;};
struct Branch {std::uint64_t EdgeId=0;Handle(Geom2d_Curve) Curve;double First=0.,Last=0.;};
#include <ExtrusionRecognition/MachiningFaceCutCoverageProjection.inc>
}

#include "MachiningFaceCutCoverageSplineBoundsAudit.inc"

struct CircularFixture final {TopoDS_Face Face;R::SSectionWiresResult Section;};

gp_Pnt RadialPoint(double radius,double angle){return {0.,radius*std::cos(angle),radius*std::sin(angle)};}

void FixtureArc(BRepBuilderAPI_MakeWire& native,R::SSectionWire& section,double radius,double first,double last,bool reverse=false)
{
    const gp_Circ circle(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(1,0,0),gp_Dir(0,1,0)),radius);
    auto shape=BRepBuilderAPI_MakeEdge(circle,first,last).Edge();if(reverse)shape.Reverse();native.Add(shape);
    R::SSectionWireEdge edge;G::Circle2 curve;curve.Radius=radius;edge.Curve=curve;
    edge.Range={first,last};edge.bReversed=reverse;
    const auto a=RadialPoint(radius,reverse?last:first),b=RadialPoint(radius,reverse?first:last);
    edge.Start={a.Y(),a.Z()};edge.End={b.Y(),b.Z()};section.Edges.push_back(std::move(edge));
}

void FixtureLine(BRepBuilderAPI_MakeWire& native,R::SSectionWire& section,const gp_Pnt& a,const gp_Pnt& b)
{
    native.Add(BRepBuilderAPI_MakeEdge(a,b).Edge());R::SSectionWireEdge edge;
    edge.Start={a.Y(),a.Z()};edge.End={b.Y(),b.Z()};edge.Curve=G::Segment2{edge.Start,edge.End};
    edge.Range={0.,a.Distance(b)};section.Edges.push_back(std::move(edge));
}

CircularFixture AnnularSector(double innerFirst=0.,double innerLast=machining_path_audit::Pi/2)
{
    CircularFixture result;BRepBuilderAPI_MakeWire native;R::SSectionWire section;section.bClosed=true;
    FixtureArc(native,section,4.,0.,machining_path_audit::Pi/2);
    FixtureLine(native,section,RadialPoint(4.,machining_path_audit::Pi/2),RadialPoint(2.,innerLast));
    FixtureArc(native,section,2.,innerFirst,innerLast,true);
    FixtureLine(native,section,RadialPoint(2.,innerFirst),RadialPoint(4.,0.));
    result.Face=BRepBuilderAPI_MakeFace(native.Wire()).Face();
    if(!BRepCheck_Analyzer(result.Face).IsValid())throw std::runtime_error("Annular sector fixture is invalid");
    GProp_GProps mass;BRepGProp::SurfaceProperties(result.Face,mass);section.dSignedArea=std::abs(mass.Mass());
    result.Section.bSuccess=true;result.Section.Wires.push_back(std::move(section));return result;
}

CircularFixture QuarterDisk(double radius)
{
    CircularFixture result;BRepBuilderAPI_MakeWire native;R::SSectionWire section;section.bClosed=true;
    FixtureArc(native,section,radius,0.,machining_path_audit::Pi/2);
    FixtureLine(native,section,RadialPoint(radius,machining_path_audit::Pi/2),gp_Pnt(0,0,0));
    FixtureLine(native,section,gp_Pnt(0,0,0),RadialPoint(radius,0.));
    result.Face=BRepBuilderAPI_MakeFace(native.Wire()).Face();
    if(!BRepCheck_Analyzer(result.Face).IsValid())throw std::runtime_error("Quarter disk fixture is invalid");
    section.dSignedArea=machining_path_audit::Pi*radius*radius/4.;
    result.Section.bSuccess=true;result.Section.Wires.push_back(std::move(section));return result;
}

CircularFixture MajorCircleSegment()
{
    CircularFixture result;BRepBuilderAPI_MakeWire native;R::SSectionWire section;section.bClosed=true;
    const double a=-3*machining_path_audit::Pi/4,b=3*machining_path_audit::Pi/4;
    FixtureArc(native,section,2.,a,b);FixtureLine(native,section,RadialPoint(2.,b),RadialPoint(2.,a));
    result.Face=BRepBuilderAPI_MakeFace(native.Wire()).Face();
    if(!BRepCheck_Analyzer(result.Face).IsValid())throw std::runtime_error("Major circular segment fixture is invalid");
    GProp_GProps mass;BRepGProp::SurfaceProperties(result.Face,mass);section.dSignedArea=std::abs(mass.Mass());
    result.Section.bSuccess=true;result.Section.Wires.push_back(std::move(section));return result;
}

machining_path_audit::Analysis SteppedEnd(const bool convexRidge)
{
    using namespace machining_path_audit;
    // A closed source solid gives the edge work an independent material-side
    // orientation. Only its end machining faces enter the downstream works.
    const auto profile=convexRidge
        ?PolygonFace({{-5,0,0},{0,0,0},{2,0,5},{0,0,10},{-5,0,10}})
        :PolygonFace({{-5,0,0},{0,0,0},{0,0,5},{-2,0,5},{-2,0,10},{-5,0,10}});
    Analysis result;result.Classification=AllMachining(BRepPrimAPI_MakePrism(profile,gp_Vec(0,10,0)).Shape());
    auto& classification=result.Classification;
    const auto built=O::ReconstructOpenCascadeShape(classification.SplitGeometry);
    classification.MachiningFaceIds.clear();
    for(const auto& [id,face]:built.Faces) {
        BRepAdaptor_Surface support(face);if(support.GetType()!=GeomAbs_Plane)continue;
        auto normal=support.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
        if(normal.X()>.1||(!convexRidge&&normal.Z()>.1&&std::abs(support.Plane().Location().Z()-5.)<1.e-7))
            classification.MachiningFaceIds.push_back(id);
        else classification.NonMachiningFaceIds.push_back(id);
    }
    classification.MachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.MachiningFaceIds);
    classification.NonMachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.NonMachiningFaceIds);
    const auto split=G::Persistence::Serialize(classification.SplitGeometry);
    const auto machining=G::Persistence::Serialize(classification.MachiningGeometry);
    result.Edges=R::MachiningEdgeAnalysisWork{}.Run(classification,RectangleSection());
    if(!result.Edges.bSuccess||!result.Edges.bComplete)throw std::runtime_error("Stepped end edge fixture failed");
    result.Paths=R::MachiningPathAnalysisWork{}.Run(classification,RectangleSection(),result.Edges);
    result.SourceUnchanged=split==G::Persistence::Serialize(classification.SplitGeometry)
        &&machining==G::Persistence::Serialize(classification.MachiningGeometry);
    return result;
}

machining_path_audit::Analysis SourceAttributedBoxEnd()
{
    using namespace machining_path_audit;
    Analysis result;result.Classification=AllMachining(BRepPrimAPI_MakeBox(10.,10.,10.).Shape());
    auto& classification=result.Classification;const auto built=O::ReconstructOpenCascadeShape(classification.SplitGeometry);
    classification.MachiningFaceIds.clear();
    for(const auto& [id,face]:built.Faces) {
        const auto surface=BRepAdaptor_Surface(face);
        if(surface.GetType()==GeomAbs_Plane&&std::abs(surface.Plane().Axis().Direction().X())>.999
            &&std::abs(surface.Plane().Location().X())<1.e-8)classification.MachiningFaceIds.push_back(id);
        else classification.NonMachiningFaceIds.push_back(id);
    }
    classification.MachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.MachiningFaceIds);
    classification.NonMachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.NonMachiningFaceIds);
    const auto split=G::Persistence::Serialize(classification.SplitGeometry),machining=G::Persistence::Serialize(classification.MachiningGeometry);
    result.Edges=R::MachiningEdgeAnalysisWork{}.Run(classification,RectangleSection());
    if(!result.Edges.bSuccess||!result.Edges.bComplete)throw std::runtime_error("Attributed box-end fixture edge analysis failed");
    result.Paths=R::MachiningPathAnalysisWork{}.Run(classification,RectangleSection(),result.Edges);
    result.SourceUnchanged=split==G::Persistence::Serialize(classification.SplitGeometry)&&machining==G::Persistence::Serialize(classification.MachiningGeometry);
    return result;
}

machining_path_audit::Analysis SourceAttributedDetachedLineEnd(const bool shortBottom=false)
{
    using namespace machining_path_audit;BRep_Builder builder;TopoDS_Compound model;builder.MakeCompound(model);
    builder.Add(model,PolygonFace({{0,0,0},{0,10,0},{0,10,10},{0,0,10}}));
    const double last=shortBottom?9.:10.;
    builder.Add(model,PolygonFace({{0,0,0},{10,0,0},{10,last,0},{0,last,0}}));
    builder.Add(model,PolygonFace({{0,0,10},{0,10,10},{10,10,10},{10,0,10}}));
    builder.Add(model,PolygonFace({{0,0,0},{0,0,10},{10,0,10},{10,0,0}}));
    builder.Add(model,PolygonFace({{0,10,0},{10,10,0},{10,10,10},{0,10,10}}));
    Analysis result;result.Classification=AllMachining(model);auto& classification=result.Classification;
    classification.MachiningFaceIds={classification.SplitGeometry.Faces.front().Id};
    for(std::size_t i=1;i<classification.SplitGeometry.Faces.size();++i)classification.NonMachiningFaceIds.push_back(classification.SplitGeometry.Faces[i].Id);
    classification.MachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.MachiningFaceIds);
    classification.NonMachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.NonMachiningFaceIds);
    const auto original=G::Persistence::Serialize(classification.SplitGeometry),machining=G::Persistence::Serialize(classification.MachiningGeometry);
    result.Edges=R::MachiningEdgeAnalysisWork{}.Run(classification,RectangleSection());
    if(!result.Edges.bSuccess||!result.Edges.bComplete)throw std::runtime_error("Detached native line source fixture edge analysis failed");
    result.Paths=R::MachiningPathAnalysisWork{}.Run(classification,RectangleSection(),result.Edges);
    for(auto& path:result.Paths.Edges)if(path.bSelectedPath&&path.Samples.size()==2&&path.Samples.front().Position.Z==0.&&path.Samples.back().Position.Z==0.){
        path.Status=R::EMachiningPathStatus::InvalidGeometry;for(auto& endpoint:path.Samples){endpoint.bToolAxisValid=false;endpoint.Status=R::EMachiningPathStatus::InvalidGeometry;}result.Paths.bComplete=false;}
    result.SourceUnchanged=original==G::Persistence::Serialize(classification.SplitGeometry)&&machining==G::Persistence::Serialize(classification.MachiningGeometry);return result;
}

machining_path_audit::Analysis NativeConicCornerEnd(const bool elliptical)
{
    using namespace machining_path_audit;
    const gp_Ax2 axes(gp_Pnt(0,99,9),gp_Dir(1,0,0),gp_Dir(0,1,0));
    const double first=-1.e-9,last=Pi/2,top=elliptical?9.5:10.;Handle(Geom_Curve) conic;
    if(elliptical)conic=new Geom_Ellipse(gp_Elips(axes,1.,.5));else conic=new Geom_Circle(gp_Circ(axes,1.));
    const auto a=conic->Value(first),b=conic->Value(last);BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,0),gp_Pnt(0,100,0)).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,100,0),a).Edge());wire.Add(BRepBuilderAPI_MakeEdge(conic,first,last).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(b,gp_Pnt(0,0,top)).Edge());wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,top),gp_Pnt(0,0,0)).Edge());
    const auto profile=BRepBuilderAPI_MakeFace(wire.Wire()).Face();const auto body=BRepPrimAPI_MakePrism(profile,gp_Vec(5,0,0)).Shape();
    if(!BRepCheck_Analyzer(body,true).IsValid())throw std::runtime_error("Native conic-corner complete solid fixture is invalid");
    Analysis result;result.Classification=AllMachining(body);auto& classification=result.Classification;
    const auto built=O::ReconstructOpenCascadeShape(classification.SplitGeometry);classification.MachiningFaceIds.clear();
    for(const auto& [id,face]:built.Faces){BRepAdaptor_Surface surface(face);
        const bool end=surface.GetType()==GeomAbs_Plane&&std::abs(surface.Plane().Axis().Direction().X())>.999&&std::abs(surface.Plane().Location().X())<1.e-8;
        if(end||surface.GetType()!=GeomAbs_Plane)classification.MachiningFaceIds.push_back(id);else classification.NonMachiningFaceIds.push_back(id);}
    if(classification.MachiningFaceIds.size()!=2)throw std::runtime_error("Conic corner fixture lost its real end and convex rounded machining face");
    classification.MachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.MachiningFaceIds);
    classification.NonMachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.NonMachiningFaceIds);
    const auto section=PolygonSection({{0,0},{100,0},{100,top},{0,top}});
    const auto original=G::Persistence::Serialize(classification.SplitGeometry),machining=G::Persistence::Serialize(classification.MachiningGeometry);
    result.Edges=R::MachiningEdgeAnalysisWork{}.Run(classification,section);
    if(!result.Edges.bSuccess||!result.Edges.bComplete)throw std::runtime_error("Conic corner source edge analysis failed");
    result.Paths=R::MachiningPathAnalysisWork{}.Run(classification,section,result.Edges);
    result.SourceUnchanged=original==G::Persistence::Serialize(classification.SplitGeometry)&&machining==G::Persistence::Serialize(classification.MachiningGeometry);return result;
}

machining_path_audit::Analysis SourceAttributedClosedSplineEnd()
{
    using namespace machining_path_audit;
    std::vector<Handle(Geom_BSplineCurve)> profiles;std::vector<TopoDS_Face> skins;
    const std::vector<gp_Pnt> original{{0,0,0},{0,0,0},{0,20,0},{0,0,20},{0,0,0}};
    for(int inner=0;inner<2;++inner) {
        TColgp_Array1OfPnt poles(1,5);TColgp_Array2OfPnt net(1,5,1,2);
        for(int i=1;i<=5;++i) {
            auto p=original[std::size_t(i-1)];if(inner)p={0,2.25+.25*p.Y(),2.25+.25*p.Z()};
            poles(i)=p;net(i,1)=p;net(i,2)=p.Translated(gp_Vec(10,0,0));
        }
        const Handle(Geom_BezierCurve) bezier=new Geom_BezierCurve(poles);
        profiles.push_back(GeomConvert::CurveToBSplineCurve(bezier));
        const Handle(Geom_BezierSurface) surface=new Geom_BezierSurface(net);
        const auto side=BRepBuilderAPI_MakeFace(surface,0.,1.,0.,1.,1.e-7).Face();
        skins.push_back(TopoDS::Face(BRepBuilderAPI_NurbsConvert(side,true).Shape()));
    }
    const auto outer=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(profiles[0]).Edge()).Wire();
    auto inner=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(profiles[1]).Edge()).Wire();inner.Reverse();
    BRepBuilderAPI_MakeFace capMaker(outer);capMaker.Add(inner);const auto cap=capMaker.Face();
    if(!BRepCheck_Analyzer(cap).IsValid())throw std::runtime_error("Closed spline double-loop cap fixture is invalid");
    BRep_Builder builder;TopoDS_Compound shape;builder.MakeCompound(shape);builder.Add(shape,cap);
    for(const auto& skin:skins)builder.Add(shape,skin);
    Analysis result;result.Classification=AllMachining(shape);auto& classification=result.Classification;
    classification.MachiningFaceIds={classification.SplitGeometry.Faces.front().Id};
    for(std::size_t i=1;i<classification.SplitGeometry.Faces.size();++i)
        classification.NonMachiningFaceIds.push_back(classification.SplitGeometry.Faces[i].Id);
    classification.MachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.MachiningFaceIds);
    classification.NonMachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.NonMachiningFaceIds);
    const auto built=O::ReconstructOpenCascadeShape(classification.SplitGeometry);
    R::SSectionWiresResult section;section.bSuccess=true;
    for(int index=0;index<2;++index) {
        const auto faceId=classification.NonMachiningFaceIds[std::size_t(index)];std::uint64_t sourceEdge=0;
        for(const auto& record:classification.SplitGeometry.Faces)if(record.Id==faceId)
            for(const auto wireId:record.WireIds)for(const auto& wire:classification.SplitGeometry.Wires)if(wire.Id==wireId)
                for(const auto& coedge:wire.Coedges) {
                    const auto found=built.Edges.find(coedge.EdgeId);if(found==built.Edges.end()||BRep_Tool::Degenerated(found->second))continue;
                    BRepAdaptor_Curve curve(found->second);
                    if((curve.GetType()==GeomAbs_BSplineCurve||curve.GetType()==GeomAbs_BezierCurve)
                        &&std::abs(curve.Value(curve.FirstParameter()).X())<1.e-9
                        &&std::abs(curve.Value(curve.LastParameter()).X())<1.e-9)sourceEdge=coedge.EdgeId;
                }
        if(!sourceEdge)throw std::runtime_error("Closed spline fixture has no exact native source skin rim");
        const auto profile=profiles[std::size_t(index)];G::BSpline2 curve;curve.Degree=profile->Degree();
        for(int i=1;i<=profile->NbPoles();++i)curve.Poles.push_back({profile->Pole(i).Y(),profile->Pole(i).Z()});
        for(int i=1;i<=profile->NbKnots();++i){curve.Knots.push_back(profile->Knot(i));curve.Multiplicities.push_back(profile->Multiplicity(i));}
        R::SSectionWire wire;wire.bClosed=true;wire.bInner=index==1;R::SSectionWireEdge edge;
        edge.Curve=curve;edge.Range={0.,1.};edge.SourceFaceId=faceId;edge.SourceEdgeId=sourceEdge;edge.bReversed=index==1;
        const auto point=profile->Value(0.);edge.Start=edge.End={point.Y(),point.Z()};wire.Edges.push_back(std::move(edge));section.Wires.push_back(std::move(wire));
    }
    const auto split=G::Persistence::Serialize(classification.SplitGeometry),machining=G::Persistence::Serialize(classification.MachiningGeometry);
    result.Edges=R::MachiningEdgeAnalysisWork{}.Run(classification,section);
    if(!result.Edges.bSuccess||!result.Edges.bComplete)throw std::runtime_error("Closed spline fixture edge analysis failed");
    result.Paths=R::MachiningPathAnalysisWork{}.Run(classification,section,result.Edges);
    result.SourceUnchanged=split==G::Persistence::Serialize(classification.SplitGeometry)&&machining==G::Persistence::Serialize(classification.MachiningGeometry);
    return result;
}

machining_path_audit::Analysis ExactClosedRuledEnd()
{
    // Every curve here is an exact polynomial composition, rather than an
    // approximated Boolean/NURBS pcurve. The seam tangent is the real V ruling.
    using namespace machining_path_audit;
    const auto choose=[](int n,int k){double value=1.;for(int i=1;i<=k;++i)value*=double(n-k+i)/i;return value;};
    const auto product=[&](const std::vector<double>& a,const std::vector<double>& b){
        const int m=int(a.size())-1,n=int(b.size())-1;std::vector<double> out(std::size_t(m+n+1),0.);
        for(int i=0;i<=m;++i)for(int j=0;j<=n;++j)out[std::size_t(i+j)]+=a[std::size_t(i)]*b[std::size_t(j)]*choose(m,i)*choose(n,j)/choose(m+n,i+j);
        return out;};
    const auto elevate=[&](const std::vector<double>& a,int degree){return product(a,std::vector<double>(std::size_t(degree-int(a.size())+2),1.));};
    TColgp_Array2OfPnt supportPoles(1,4,1,2);const double xs[]{0.,.4,-.4,0.};
    for(int u=1;u<=4;++u)for(int v=1;v<=2;++v)supportPoles(u,v)={xs[u-1],-2.+4.*double(u-1)/3.,v==1?-2.:2.};
    const Handle(Geom_BSplineSurface) support=GeomConvert::SurfaceToBSplineSurface(new Geom_BezierSurface(supportPoles));
    std::vector<TopoDS_Wire> loops;std::vector<TopoDS_Face> skins;std::vector<Handle(Geom_BSplineCurve)> profiles;
    for(int inner=0;inner<2;++inner) {
        std::vector<double> us{.5,.5,.9,.5,.5},vs{.5,.9,.5,.1,.5};
        if(inner)for(std::size_t i=0;i<us.size();++i){us[i]=.575+.3*(us[i]-.575);vs[i]=.5+.3*(vs[i]-.5);}
        const auto u2=product(us,us),u3=product(u2,us),u12=elevate(us,12),u212=elevate(u2,12),v12=elevate(vs,12);
        TColgp_Array1OfPnt xyz(1,13);TColgp_Array1OfPnt2d uv(1,5);TColgp_Array1OfPnt profile(1,5);TColgp_Array2OfPnt skin(1,5,1,2);
        for(int i=1;i<=13;++i)xyz(i)={1.2*u12[std::size_t(i-1)]-3.6*u212[std::size_t(i-1)]+2.4*u3[std::size_t(i-1)],-2.+4.*u12[std::size_t(i-1)],-2.+4.*v12[std::size_t(i-1)]};
        for(int i=1;i<=5;++i){uv(i)={us[std::size_t(i-1)],vs[std::size_t(i-1)]};profile(i)={0.,-2.+4.*us[std::size_t(i-1)],-2.+4.*vs[std::size_t(i-1)]};skin(i,1)=profile(i).Translated(gp_Vec(-1,0,0));skin(i,2)=profile(i).Translated(gp_Vec(1,0,0));}
        const Handle(Geom_BezierCurve) native=new Geom_BezierCurve(xyz);const Handle(Geom2d_BezierCurve) pcurve=new Geom2d_BezierCurve(uv);
        auto edge=BRepBuilderAPI_MakeEdge(native).Edge();BRep_Builder builder;builder.UpdateEdge(edge,pcurve,support,TopLoc_Location(),1.e-7);builder.SameParameter(edge,true);builder.SameRange(edge,true);
        // The UV polynomial traverses clockwise. MakeFace normalizes the
        // outer loop; the inner loop must retain this original clockwise use.
        loops.push_back(BRepBuilderAPI_MakeWire(edge).Wire());
        profiles.push_back(GeomConvert::CurveToBSplineCurve(new Geom_BezierCurve(profile)));
        skins.push_back(TopoDS::Face(BRepBuilderAPI_NurbsConvert(BRepBuilderAPI_MakeFace(new Geom_BezierSurface(skin),0.,1.,0.,1.,1.e-7).Face(),true).Shape()));
    }
    BRepBuilderAPI_MakeFace capMaker(support,loops.front(),true);capMaker.Add(loops.back());const auto cap=capMaker.Face();
    if(!BRepCheck_Analyzer(cap).IsValid()){
        BRepCheck_Analyzer audit(cap);std::ostringstream errors;errors<<"Exact polynomial closed ruled fixture is invalid";
        for(const auto type:{TopAbs_FACE,TopAbs_WIRE,TopAbs_EDGE,TopAbs_VERTEX})for(TopExp_Explorer ex(cap,type);ex.More();ex.Next()){
            for(const auto status:audit.Result(ex.Current())->Status())if(status!=BRepCheck_NoError)errors<<";type="<<int(type)<<",status="<<int(status);}
        throw std::runtime_error(errors.str());}
    BRep_Builder builder;TopoDS_Compound shape;builder.MakeCompound(shape);builder.Add(shape,cap);for(const auto& skin:skins)builder.Add(shape,skin);
    Analysis result;result.Classification=AllMachining(shape);auto& classification=result.Classification;
    classification.MachiningFaceIds={classification.SplitGeometry.Faces.front().Id};
    for(std::size_t i=1;i<classification.SplitGeometry.Faces.size();++i)classification.NonMachiningFaceIds.push_back(classification.SplitGeometry.Faces[i].Id);
    classification.MachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.MachiningFaceIds);
    classification.NonMachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.NonMachiningFaceIds);
    const auto built=O::ReconstructOpenCascadeShape(classification.SplitGeometry);
    R::SSectionWiresResult section;section.bSuccess=true;
    for(int index=0;index<2;++index){const auto profile=profiles[std::size_t(index)];G::BSpline2 curve;curve.Degree=profile->Degree();
        for(int i=1;i<=profile->NbPoles();++i)curve.Poles.push_back({profile->Pole(i).Y(),profile->Pole(i).Z()});
        for(int i=1;i<=profile->NbKnots();++i){curve.Knots.push_back(profile->Knot(i));curve.Multiplicities.push_back(profile->Multiplicity(i));}
        R::SSectionWire wire;wire.bClosed=true;wire.bInner=index==1;R::SSectionWireEdge edge;edge.Curve=curve;edge.Range={0.,1.};edge.SourceFaceId=classification.NonMachiningFaceIds[std::size_t(index)];edge.bReversed=index==1;
        for(const auto& face:classification.SplitGeometry.Faces)if(face.Id==edge.SourceFaceId)
            for(const auto wireId:face.WireIds)for(const auto& nativeWire:classification.SplitGeometry.Wires)if(nativeWire.Id==wireId)
                for(const auto& use:nativeWire.Coedges){BRepAdaptor_Curve rim(built.Edges.at(use.EdgeId));
                    if((rim.GetType()==GeomAbs_BSplineCurve||rim.GetType()==GeomAbs_BezierCurve)
                        &&rim.Value(rim.FirstParameter()).X()==-1.&&rim.Value(rim.LastParameter()).X()==-1.)edge.SourceEdgeId=use.EdgeId;}
        if(!edge.SourceEdgeId)throw std::runtime_error("Exact ruled fixture lost its original source-skin rim edge");
        const auto p=profile->Value(0.);edge.Start=edge.End={p.Y(),p.Z()};wire.Edges.push_back(std::move(edge));section.Wires.push_back(std::move(wire));}
    const auto split=G::Persistence::Serialize(classification.SplitGeometry),machining=G::Persistence::Serialize(classification.MachiningGeometry);
    result.Edges=R::MachiningEdgeAnalysisWork{}.Run(classification,section);if(!result.Edges.bSuccess||!result.Edges.bComplete)throw std::runtime_error("Exact closed ruled source edge analysis failed");
    result.Paths=R::MachiningPathAnalysisWork{}.Run(classification,section,result.Edges);
    result.SourceUnchanged=split==G::Persistence::Serialize(classification.SplitGeometry)&&machining==G::Persistence::Serialize(classification.MachiningGeometry);return result;
}

machining_path_audit::Analysis SourceAttributedRuledEnd(const bool rectangular=false)
{
    using namespace machining_path_audit;
    if(!rectangular)return ExactClosedRuledEnd();
    TopoDS_Shape stock;
    if(rectangular)stock=BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(gp_Pnt(-2,-1,-1),4,2,2).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(-2,-.6,-.6),4,1.2,1.2).Shape()).Shape();
    else {
        const gp_Ax2 axis(gp_Pnt(-2,0,0),gp_Dir(1,0,0),gp_Dir(0,1,0));
        stock=BRepAlgoAPI_Cut(BRepPrimAPI_MakeCylinder(axis,1.,4.).Shape(),BRepPrimAPI_MakeCylinder(axis,.6,4.).Shape()).Shape();
    }
    TColgp_Array1OfPnt poles(1,4);poles(1)={0,-2,-2};poles(2)={.4,-2./3.,-2};
    poles(3)={-.4,2./3.,-2};poles(4)={0,2,-2};const Handle(Geom_BezierCurve) curve=new Geom_BezierCurve(poles);
    BRepBuilderAPI_MakeWire outline;outline.Add(BRepBuilderAPI_MakeEdge(curve).Edge());
    outline.Add(BRepBuilderAPI_MakeEdge(curve->Value(1.),gp_Pnt(2,2,-2)).Edge());
    outline.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(2,2,-2),gp_Pnt(2,-2,-2)).Edge());
    outline.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(2,-2,-2),curve->Value(0.)).Edge());
    const auto cutter=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(outline.Wire()).Face(),gp_Vec(0,0,4)).Shape();
    const auto body=BRepBuilderAPI_NurbsConvert(BRepAlgoAPI_Cut(stock,cutter).Shape(),true).Shape();
    Analysis result;result.Classification=AllMachining(body);auto& classification=result.Classification;
    const auto built=O::ReconstructOpenCascadeShape(classification.SplitGeometry);classification.MachiningFaceIds.clear();
    for(const auto& [id,face]:built.Faces) {
        BRepAdaptor_Surface support(face);bool wavy=false;
        if(support.GetType()==GeomAbs_BSplineSurface) {
            const auto spline=support.BSpline();wavy=spline->UDegree()==3&&spline->VDegree()==1&&spline->NbVPoles()==2
                &&!GeomLib_IsPlanarSurface(BRep_Tool::Surface(face),1.e-7).IsPlanar();
        }
        if(wavy)classification.MachiningFaceIds.push_back(id);else classification.NonMachiningFaceIds.push_back(id);
    }
    if(classification.MachiningFaceIds.size()!=1)throw std::runtime_error("Ruled pipe end fixture must have one genuine nonplanar cut face");
    classification.MachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.MachiningFaceIds);
    classification.NonMachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.NonMachiningFaceIds);
    R::SSectionWiresResult section;
    if(rectangular) {
        section=PolygonSection({{-1,-1},{1,-1},{1,1},{-1,1}});
        auto inner=PolygonSection({{-.6,-.6},{.6,-.6},{.6,.6},{-.6,.6}});inner.Wires.front().bInner=true;section.Wires.push_back(inner.Wires.front());
    } else {
        section.bSuccess=true;
        for(const double radius:{1.,.6}) {
            R::SSectionWire wire;wire.bClosed=true;wire.bInner=radius<1.;R::SSectionWireEdge edge;G::Circle2 circle;circle.Radius=radius;
            edge.Curve=circle;edge.Range={0.,2*Pi};edge.Start=edge.End={radius,0};edge.bReversed=wire.bInner;
            wire.Edges.push_back(std::move(edge));section.Wires.push_back(std::move(wire));
        }
    }
    const auto split=G::Persistence::Serialize(classification.SplitGeometry),machining=G::Persistence::Serialize(classification.MachiningGeometry);
    result.Edges=R::MachiningEdgeAnalysisWork{}.Run(classification,section);
    if(!result.Edges.bSuccess||!result.Edges.bComplete)throw std::runtime_error("Ruled pipe end fixture edge analysis failed");
    result.Paths=R::MachiningPathAnalysisWork{}.Run(classification,section,result.Edges);
    result.SourceUnchanged=split==G::Persistence::Serialize(classification.SplitGeometry)&&machining==G::Persistence::Serialize(classification.MachiningGeometry);
    return result;
}

CircularFixture Annulus()
{
    CircularFixture result;const auto center=gp_Ax2(gp_Pnt(0,5,5),gp_Dir(1,0,0),gp_Dir(0,1,0));
    const auto outer=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(center,4.)).Edge()).Wire();
    auto inner=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(center,2.)).Edge()).Wire();inner.Reverse();
    BRepBuilderAPI_MakeFace face(outer);face.Add(inner);result.Face=face.Face();
    result.Section=machining_path_audit::CircleSection(4.);
    auto innerSection=machining_path_audit::CircleSection(2.);innerSection.Wires.front().Edges.front().bReversed=true;
    result.Section.Wires.push_back(innerSection.Wires.front());return result;
}

CircularFixture NativePartialSkinEvent()
{
    // One original TopoEdge has an OnSkin rim interval [0,1] followed by an
    // internal side [1,2]. The shared t=1 event has no original TopoVertex.
    const double angle=.2;Handle(Geom_ConicalSurface) cone=new Geom_ConicalSurface(
        gp_Ax3(gp_Pnt(0,5,5),gp_Dir(1,0,0),gp_Dir(0,1,0)),angle,2.);
    TColgp_Array1OfPnt2d poles(1,3);poles(1)={0,10};poles(2)={1,10};poles(3)={.5,5};
    TColStd_Array1OfReal knots(1,3);knots(1)=0.;knots(2)=1.;knots(3)=2.;
    TColStd_Array1OfInteger multiplicities(1,3);multiplicities(1)=2;multiplicities(2)=1;multiplicities(3)=2;
    Handle(Geom2d_BSplineCurve) mixed=new Geom2d_BSplineCurve(poles,knots,multiplicities,1);
    BRepBuilderAPI_MakeWire wire;wire.Add(BRepBuilderAPI_MakeEdge(mixed,cone,0.,2.).Edge());
    for(const auto& endpoints:{std::pair{gp_Pnt2d(.5,5),gp_Pnt2d(0,5)},std::pair{gp_Pnt2d(0,5),gp_Pnt2d(0,10)}}) {
        const Handle(Geom2d_TrimmedCurve) line=GCE2d_MakeSegment(endpoints.first,endpoints.second);
        wire.Add(BRepBuilderAPI_MakeEdge(line,cone,line->FirstParameter(),line->LastParameter()).Edge());
    }
    CircularFixture fixture;fixture.Face=BRepBuilderAPI_MakeFace(cone,wire.Wire(),true).Face();
    if(!BRepLib::BuildCurves3d(fixture.Face,1.e-9)||!BRepCheck_Analyzer(fixture.Face).IsValid())
        throw std::runtime_error("Native partial-skin cone event fixture is invalid");
    fixture.Section=machining_path_audit::CircleSection(2.+10*std::sin(angle));return fixture;
}

CircularFixture NativeCanonicalCorner(const double rightY=.9999,const double faceTolerance=.001,const double originalWidth=1.,
    const double topRightY=std::numeric_limits<double>::quiet_NaN())
{
    const gp_Pnt a(0,0,0),b(0,originalWidth,0),c(0,originalWidth,1),d(0,0,1);
    BRepBuilderAPI_MakeWire wire;wire.Add(BRepBuilderAPI_MakeEdge(a,b).Edge());wire.Add(BRepBuilderAPI_MakeEdge(b,c).Edge());
    auto top=BRepBuilderAPI_MakeEdge(d,c).Edge();top.Reverse();wire.Add(top);wire.Add(BRepBuilderAPI_MakeEdge(d,a).Edge());
    CircularFixture fixture;fixture.Face=BRepBuilderAPI_MakeFace(wire.Wire()).Face();BRep_Builder builder;
    builder.UpdateFace(fixture.Face,faceTolerance);
    for(TopExp_Explorer explorer(fixture.Face,TopAbs_EDGE);explorer.More();explorer.Next())builder.UpdateEdge(TopoDS::Edge(explorer.Current()),.001);
    TopTools_IndexedMapOfShape vertices;TopExp::MapShapes(fixture.Face,TopAbs_VERTEX,vertices);
    for(int i=1;i<=vertices.Extent();++i){auto vertex=TopoDS::Vertex(vertices(i));auto p=BRep_Tool::Pnt(vertex);if(p.Y()==originalWidth)p.SetY(std::isfinite(topRightY)&&p.Z()>.5?topRightY:rightY);builder.UpdateVertex(vertex,p,.001);}
    if(!BRepCheck_Analyzer(fixture.Face).IsValid())throw std::runtime_error("Original tolerant canonical-corner fixture is invalid");
    fixture.Section=machining_path_audit::PolygonSection({{0,0},{originalWidth,0},{originalWidth,1},{0,1}});return fixture;
}

CircularFixture BezierNormalStrip(const bool folded=false,const bool reverseFace=false,const bool rational=false)
{
    std::vector<gp_Pnt> points=folded?std::vector<gp_Pnt>{{0,0,0},{0,1,10},{0,9,10},{0,10,0}}:
        std::vector<gp_Pnt>{{0,0,0},{0,5,0},{0,10,1}};
    if(rational&&!folded)points[1].SetZ(.2); // Separate the weight certificate from a collinear end-normal event.
    TColgp_Array1OfPnt poles(1,int(points.size()));
    for(int i=1;i<=poles.Length();++i)poles(i)=points[std::size_t(i-1)];
    TColStd_Array1OfReal weights(1,poles.Length());for(int i=1;i<=weights.Length();++i)weights(i)=i==2?1.1:(i==1?.9:.95);
    const Handle(Geom_BezierCurve) lower=rational?new Geom_BezierCurve(poles,weights):new Geom_BezierCurve(poles);
    const auto upper=occ::down_cast<Geom_BezierCurve>(lower->Translated(gp_Vec(0,0,.4)));
    BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(lower).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(lower->Value(1.),upper->Value(1.)).Edge());
    wire.Add(TopoDS::Edge(BRepBuilderAPI_MakeEdge(upper).Edge().Reversed()));
    wire.Add(BRepBuilderAPI_MakeEdge(upper->Value(0.),lower->Value(0.)).Edge());
    CircularFixture fixture;fixture.Face=BRepBuilderAPI_MakeFace(wire.Wire()).Face();
    if(reverseFace)fixture.Face.Reverse();
    if(!BRepCheck_Analyzer(fixture.Face).IsValid())throw std::runtime_error("Bezier normal-strip fixture is invalid");
    // The stock profile extends beyond the cut face's two side connectors,
    // so the two curved boundaries are skin and those connectors are sides.
    const auto a=occ::down_cast<Geom_BezierCurve>(lower->Copy()),b=occ::down_cast<Geom_BezierCurve>(upper->Copy());
    a->Segment(-.01,1.01);b->Segment(-.01,1.01);
    R::SSectionWire section;section.bClosed=true;
    const auto appendCurve=[&](const Handle(Geom_BezierCurve)& curve,const bool reversed) {
        R::SSectionWireEdge edge;
        if(curve->IsRational()) {
            G::NURBS2 geometry;geometry.Degree=curve->Degree();geometry.Knots={0.,1.};
            geometry.Multiplicities={curve->Degree()+1,curve->Degree()+1};
            for(int i=1;i<=curve->NbPoles();++i){geometry.Poles.push_back({curve->Pole(i).Y(),curve->Pole(i).Z()});geometry.Weights.push_back(curve->Weight(i));}
            edge.Curve=geometry;
        } else {
            G::Bezier2 geometry;
            for(int i=1;i<=curve->NbPoles();++i)geometry.Poles.push_back({curve->Pole(i).Y(),curve->Pole(i).Z()});
            edge.Curve=geometry;
        }
        edge.Range={0.,1.};edge.bReversed=reversed;
        const auto first=curve->Value(reversed?1.:0.),last=curve->Value(reversed?0.:1.);
        edge.Start={first.Y(),first.Z()};edge.End={last.Y(),last.Z()};section.Edges.push_back(std::move(edge));
    };
    const auto appendLine=[&](const gp_Pnt& first,const gp_Pnt& last) {
        R::SSectionWireEdge edge;edge.Start={first.Y(),first.Z()};edge.End={last.Y(),last.Z()};
        edge.Curve=G::Segment2{edge.Start,edge.End};edge.Range={0.,first.Distance(last)};section.Edges.push_back(std::move(edge));
    };
    appendCurve(a,false);appendLine(a->Value(1.),b->Value(1.));appendCurve(b,true);appendLine(b->Value(0.),a->Value(0.));
    fixture.Section.bSuccess=true;fixture.Section.Wires.push_back(std::move(section));return fixture;
}

std::set<std::string> CircleSourceKeys(const machining_path_audit::Analysis& analysis,
    const O::SOpenCascadeBRepBuildResult& built,double radius)
{
    std::set<std::string> result;
    for(const auto& path:analysis.Paths.Edges)if(path.bSelectedPath&&!path.SourceUses.empty()) {
        BRepAdaptor_Curve curve(built.Edges.at(path.SourceUses.front().EdgeId));
        if(curve.GetType()==GeomAbs_Circle&&std::abs(curve.Circle().Radius()-radius)<1.e-7)result.insert(path.Key);
    }
    return result;
}

bool SourceSetComplete(const R::SMachiningFaceCutCoverageResult& result,const std::set<std::string>& keys)
{
    return !keys.empty()&&std::all_of(keys.begin(),keys.end(),[&](const auto& key){
        return std::any_of(result.Sources.begin(),result.Sources.end(),[&](const auto& source){return source.SourcePathKey==key&&source.bComplete;});});
}

const R::SMachiningPathEdgeAnalysis* BottomSource(const machining_path_audit::Analysis& analysis)
{
    for(const auto& path:analysis.Paths.Edges)if(path.bSelectedPath&&path.Samples.size()==2
        &&std::abs(path.Samples[0].Position.Z)<1.e-7&&std::abs(path.Samples[1].Position.Z)<1.e-7
        &&std::abs(path.Samples[0].Position.Y-path.Samples[1].Position.Y)>1.)return &path;
    return nullptr;
}

bool NativeIntervals(const R::SMachiningFaceCutCoverageResult& result,
    const O::SOpenCascadeBRepBuildResult& built)
{
    const auto inside=[&](std::uint64_t id,double first,double last) {
        const auto edge=built.Edges.find(id);if(edge==built.Edges.end()||!std::isfinite(first)||!std::isfinite(last))return false;
        BRepAdaptor_Curve curve(edge->second);const auto a=std::min(first,last),b=std::max(first,last);
        return b>a&&a>=curve.FirstParameter()-1.e-7&&b<=curve.LastParameter()+1.e-7;
    };
    for(const auto& arrival:result.Arrivals)
        if(!inside(arrival.TargetEdgeId,arrival.TargetFirstParameter,arrival.TargetLastParameter)
            ||!inside(arrival.SourceEdgeId,arrival.SourceFirstParameter,arrival.SourceLastParameter))return false;
    for(const auto& span:result.Coverage)if(!inside(span.SourceEdgeId,span.FirstParameter,span.LastParameter))return false;
    for(const auto& evidence:result.Evidence) {
        if(!inside(evidence.SourceEdgeId,evidence.SourceFirstParameter,evidence.SourceLastParameter))return false;
        if(evidence.bHasArrival&&!inside(evidence.WitnessEdgeId,evidence.WitnessFirstParameter,evidence.WitnessLastParameter))return false;
        for(const auto& witness:evidence.BoundaryWitnesses)
            if(!inside(witness.EdgeId,witness.FirstParameter,witness.LastParameter))return false;
    }
    return true;
}

bool OnlyPositiveTinyNativeLinePending(const R::SMachiningFaceCutCoverageResult& result,
    const O::SOpenCascadeBRepBuildResult& built,const std::set<std::string>& primarySources)
{
    std::size_t pending=0;
    for(const auto& atom:result.Coverage)if(atom.Kind==Kind::Pending){++pending;
        const auto found=built.Edges.find(atom.SourceEdgeId);if(found==built.Edges.end()||primarySources.contains(atom.SourcePathKey))return false;
        BRepAdaptor_Curve native(found->second);const double length=atom.LastParameter-atom.FirstParameter;
        if(native.GetType()!=GeomAbs_Line||!(length>0.)||length>=1.e-10
            ||atom.Reason.find("positive-source-projection-cell-below-native-event-resolution")==std::string::npos)return false;
        gp_Pnt point;gp_Vec derivative;native.D1(atom.FirstParameter,point,derivative);
        // These fixtures contain native unit-speed Lines in millimetres;
        // only after checking that fact is the native interval a length.
        if(std::abs(derivative.Magnitude()-1.)>1.e-12)return false;
    }
    return pending>0;
}

bool SourceEvidenceConsistent(const R::SMachiningFaceCutCoverageResult& result,
    const R::SMachiningPathAnalysisResult* paths=nullptr)
{
    for(const auto& span:result.Coverage) {
        if(span.SourcePathKey.empty()||span.SourcePathKeys!=std::vector<std::string>{span.SourcePathKey}
            ||span.BaselineSourcePathKeys!=span.SourcePathKeys||span.TargetEdgeId!=span.SourceEdgeId)return false;
        bool through=false;
        for(const auto index:span.EvidenceIndices) {
            if(index>=result.Evidence.size())return false;
            const auto& evidence=result.Evidence[index];
            if(evidence.FaceId!=span.FaceId||evidence.SourcePathKey!=span.SourcePathKey
                ||evidence.SourceEdgeId!=span.SourceEdgeId
                ||evidence.SourceFirstParameter>span.FirstParameter+1.e-7
                ||evidence.SourceLastParameter<span.LastParameter-1.e-7)return false;
            if(!evidence.bHasArrival) {
                if(evidence.Kind!=R::EFaceCutEvidenceKind::SkinExitBoundarySet||evidence.bExitCorrespondenceExpanded
                    ||evidence.BoundaryWitnesses.empty()||!evidence.bThrough||!evidence.bComplete)return false;
                if(paths) {
                    std::set<std::string> expected,actual;
                    for(const auto& path:paths->Edges)if(path.FaceId==span.FaceId)for(const auto& use:path.SourceUses) {
                        expected.insert(use.SpanKey);
                        if(!use.bSkinRelationValid||use.SkinRelation!=R::ESkinRelation::OnSkin||!use.bSourceSkinMembership)return false;
                    }
                    for(const auto& witness:evidence.BoundaryWitnesses) {
                        if(witness.SourceSkinFaceIds.empty()||witness.SkinReason.empty())return false;
                        actual.insert(witness.SpanKey);
                    }
                    if(actual!=expected)return false;
                }
            }
            through=through||(evidence.bThrough&&evidence.bComplete);
        }
        for(const auto index:span.ArrivalIndices)if(index>=result.Arrivals.size()
            ||result.Arrivals[index].FaceId!=span.FaceId
            ||result.Arrivals[index].SourcePathKey!=span.SourcePathKey)return false;
        if(span.Kind==Kind::Through&&!through)return false;
        if(span.Kind==Kind::Partial&&(!span.bMappingComplete||through))return false;
        if(span.Kind==Kind::Pending&&(span.bMappingComplete||through))return false;
        if(paths) {
            const auto source=std::find_if(paths->Edges.begin(),paths->Edges.end(),[&](const auto& path){return path.Key==span.SourcePathKey;});
            if(source==paths->Edges.end()||!source->bOnSkinPath||!source->bSelectedPath||source->FaceId!=span.FaceId)return false;
        }
    }
    for(const auto& side:result.SideCorrespondences) {
        if(side.ArrivalIndex>=result.Arrivals.size()||side.SidePathKey.empty()||side.SideSpanKey.empty())return false;
        if(!paths)continue;
        const auto& arrival=result.Arrivals[side.ArrivalIndex];
        const auto path=std::find_if(paths->Edges.begin(),paths->Edges.end(),[&](const auto& edge){return edge.Key==side.SidePathKey;});
        if(path==paths->Edges.end()||!path->bSide||path->FaceId!=arrival.FaceId)return false;
        if(std::none_of(path->SourceUses.begin(),path->SourceUses.end(),[&](const auto& use){
            return use.EdgeId==arrival.TargetEdgeId&&use.SpanKey==side.SideSpanKey
                &&arrival.TargetFirstParameter>=use.FirstParameter-1.e-9&&arrival.TargetLastParameter<=use.LastParameter+1.e-9;}))return false;
    }
    return true;
}

bool SideCompletionEndpointIdentity(const R::SMachiningFaceCutCoverageResult& result,
    const R::SMachiningPathAnalysisResult& paths,const O::SOpenCascadeBRepBuildResult& built)
{
    struct Endpoint final {TopoDS_Vertex Vertex;std::uint64_t EdgeId;double Parameter;};
    const auto endpoint=[&](const R::SMachiningPathSourceUse& use,const bool start) {
        const auto& edge=built.Edges.at(use.EdgeId);BRepAdaptor_Curve curve(edge);
        const double parameter=start?(use.bReversed?use.LastParameter:use.FirstParameter):
            (use.bReversed?use.FirstParameter:use.LastParameter);
        TopoDS_Vertex first,last;TopExp::Vertices(edge,first,last,false);
        return Endpoint{parameter==curve.FirstParameter()?first:parameter==curve.LastParameter()?last:TopoDS_Vertex{},use.EdgeId,parameter};
    };
    for(const auto& evidence:result.Evidence)if(evidence.Kind==R::EFaceCutEvidenceKind::SideCompletion) {
        const auto source=std::find_if(paths.Edges.begin(),paths.Edges.end(),[&](const auto& p){return p.Key==evidence.SourcePathKey;});
        const auto side=std::find_if(paths.Edges.begin(),paths.Edges.end(),[&](const auto& p){return p.Key==evidence.WitnessLogicEdgeId;});
        if(source==paths.Edges.end()||side==paths.Edges.end()||source->bClosed||!side->bSide
            ||source->FaceId!=side->FaceId||source->SourceUses.empty()||side->SourceUses.empty())return false;
        const std::vector<Endpoint> a{endpoint(source->SourceUses.front(),true),endpoint(source->SourceUses.back(),false)};
        const std::vector<Endpoint> b{endpoint(side->SourceUses.front(),true),endpoint(side->SourceUses.back(),false)};
        bool shared=false;
        for(const auto& first:a)for(const auto& second:b)
            shared=shared||(!first.Vertex.IsNull()&&!second.Vertex.IsNull()&&first.Vertex.IsSame(second.Vertex))
                ||(source->WireId==side->WireId&&first.EdgeId==second.EdgeId&&first.Parameter==second.Parameter);
        if(!shared)return false;
    }
    return true;
}

ObjectMap Summary(const R::SMachiningFaceCutCoverageResult& result)
{
    VariantArray sources,arrivals,evidence,spans;
    for(const auto& source:result.Sources)sources.emplace_back(ObjectMap{{"key",source.SourcePathKey},
        {"faceId",std::to_string(source.FaceId)},{"complete",source.bComplete},{"method",source.Method},{"reason",source.Reason}});
    for(const auto& arrival:result.Arrivals)arrivals.emplace_back(ObjectMap{{"source",arrival.SourcePathKey},
        {"faceId",std::to_string(arrival.FaceId)},{"sourceEdge",std::to_string(arrival.SourceEdgeId)},
        {"sourceFirst",arrival.SourceFirstParameter},{"sourceLast",arrival.SourceLastParameter},
        {"targetEdge",std::to_string(arrival.TargetEdgeId)},{"first",arrival.TargetFirstParameter},{"last",arrival.TargetLastParameter}});
    for(const auto& span:result.Coverage)spans.emplace_back(ObjectMap{{"faceId",std::to_string(span.FaceId)},
        {"source",span.SourcePathKey},{"edgeId",std::to_string(span.SourceEdgeId)},{"first",span.FirstParameter},{"last",span.LastParameter},
        {"mappingComplete",span.bMappingComplete},{"reason",span.Reason},{"kind",machining_face_cut_coverage_export::KindName(span.Kind)}});
    for(const auto& proof:result.Evidence) {
        ObjectMap row{{"faceId",std::to_string(proof.FaceId)},{"source",proof.SourcePathKey},
            {"sourceEdge",std::to_string(proof.SourceEdgeId)},{"first",proof.SourceFirstParameter},{"last",proof.SourceLastParameter},
            {"hasArrival",proof.bHasArrival},{"exitCorrespondenceExpanded",proof.bExitCorrespondenceExpanded},
            {"boundaryWitnessCount",static_cast<int>(proof.BoundaryWitnesses.size())},
            {"kind",machining_face_cut_coverage_export::EvidenceName(proof.Kind)},
            {"through",proof.bThrough},{"complete",proof.bComplete},{"reason",proof.Reason}};
        if(proof.bHasArrival)row["witnessEdge"]=std::to_string(proof.WitnessEdgeId);
        evidence.emplace_back(std::move(row));
    }
    return {{"success",result.bSuccess},{"complete",result.bComplete},{"sources",sources},{"arrivals",arrivals},
        {"coverage",spans},{"evidence",evidence},{"diagnostics",Strings(result.Diagnostics)}};
}

#include "MachiningFaceCutCoverageStraightAudit.inc"
}

int MachiningFaceCutCoverageAudit(const fs::path& directory)
{
    using namespace machining_face_cut_coverage_audit;
    using namespace machining_path_audit;
    fs::create_directories(directory);VariantArray checks;std::size_t failures=0;
    const auto check=[&](bool passed,const std::string& name){checks.emplace_back(ObjectMap{{"passed",passed},{"name",name}});if(!passed)++failures;};
    try {
        spline_projection_contract_audit::Run(check,directory);
        const auto rectangle=Analyze(PolygonFace({{0,0,0},{0,10,0},{0,10,10},{0,0,10}}),RectangleSection());
        const auto rectangleSplit=G::Persistence::Serialize(rectangle.Classification.SplitGeometry);
        const auto rectangleMachining=G::Persistence::Serialize(rectangle.Classification.MachiningGeometry);
        const auto rectangleResult=R::MachiningFaceCutCoverageWork{}.Run(rectangle.Classification,rectangle.Edges,rectangle.Paths);
        const auto rectangleBuilt=O::ReconstructOpenCascadeShape(rectangle.Classification.SplitGeometry);
        Save(directory/"rectangle-result.json",Summary(rectangleResult));
        check(rectangleResult.bSuccess&&rectangleResult.bComplete&&rectangleResult.Sources.size()==4
            &&std::all_of(rectangleResult.Sources.begin(),rectangleResult.Sources.end(),[](const auto& s){return s.bComplete;}),
            "rectangle retains all four true upstream per-face sources and completes their boundary mappings");
        check(rectangleResult.Arrivals.size()==4&&rectangleResult.Coverage.size()==4
            &&std::all_of(rectangleResult.Coverage.begin(),rectangleResult.Coverage.end(),[](const auto& s){
                return s.Kind==Kind::Through&&s.SourcePathKeys.size()==1&&s.EvidenceIndices.size()==1&&s.bMappingComplete;}),
            "a skin exit proves each full rectangular skin-source interval Through without source-path counting");
        check(NativeIntervals(rectangleResult,rectangleBuilt)&&SourceEvidenceConsistent(rectangleResult,&rectangle.Paths),
            "rectangle evidence and result atoms preserve their source-native domains and per-face ownership");
        const auto rectangleArtifacts=machining_face_cut_coverage_export::Export(rectangle.Classification,rectangle.Edges,
            rectangle.Paths,rectangleResult,rectangleBuilt,directory/"rectangle","rectangle");
        check(rectangleArtifacts.at("passed").To<bool>(),"rectangle complete BREP and colored exact-interval STEP round trips pass");
        check(rectangle.SourceUnchanged&&rectangleSplit==G::Persistence::Serialize(rectangle.Classification.SplitGeometry)
            &&rectangleMachining==G::Persistence::Serialize(rectangle.Classification.MachiningGeometry),
            "rectangle upstream analysis, coverage, and export preserve complete CAD bytes");
        {
            auto pointResult=rectangleResult;
            auto& pending=pointResult.Coverage.front();
            pending.FirstParameter=0.;pending.LastParameter=std::nextafter(std::nextafter(0.,1.),1.);
            pending.Kind=Kind::Pending;pending.bMappingComplete=false;pending.bFaceMappingComplete=false;
            pending.Reason="export-only-positive-native-interval-regression";
            const auto beforePointExport=Json::Serialize(Summary(pointResult));
            const auto source=rectangleBuilt.Edges.at(pending.SourceEdgeId);
            TopLoc_Location location;double curveFirst,curveLast;
            const auto curve=BRep_Tool::Curve(source,location,curveFirst,curveLast);
            const auto located=Handle(Geom_Curve)::DownCast(curve->Transformed(location.Transformation()));
            BRepBuilderAPI_MakeEdge refused(located,pending.FirstParameter,pending.LastParameter);
            check(pending.LastParameter>pending.FirstParameter&&pending.LastParameter-pending.FirstParameter<=gp::Resolution()
                &&!refused.IsDone()&&refused.Error()==BRepBuilderAPI_LineThroughIdenticPoints,
                "the strictly positive two-subnormal native span reproduces OCC's edge-resolution refusal without collapsing its interval");
            bool underflowRejected=false,trailingRejected=false;
            try{ParseNativeIntervalParameter("1e-999");}catch(const std::exception&){underflowRejected=true;}
            try{ParseNativeIntervalParameter("0.5trailing");}catch(const std::exception&){trailingRejected=true;}
            check(ParseNativeIntervalParameter("9.8813129168249309e-324")==pending.LastParameter
                &&underflowRejected&&trailingRejected,
                "the independent native interval audit parses the exact positive subnormal endpoint and rejects true underflow or trailing text");
            const auto pointArtifacts=machining_face_cut_coverage_export::Export(rectangle.Classification,rectangle.Edges,
                rectangle.Paths,pointResult,rectangleBuilt,directory/"positive-native-point-overlay","positive-native-point-overlay");
            std::ifstream pointFile(directory/"positive-native-point-overlay.json",std::ios::binary);
            const std::string pointText((std::istreambuf_iterator<char>(pointFile)),{});
            const auto pointJson=Json::Parse(pointText).To<ObjectMap>();
            const auto rows=pointJson.at("coverage").To<VariantArray>();
            const auto markers=pointJson.at("display").To<ObjectMap>().at("stepPointMarkers").To<VariantArray>();
            const auto row=rows.front().To<ObjectMap>();const auto marker=markers.front().To<ObjectMap>();
            const auto pointAudit=pointArtifacts.at("stepRoundTrip").To<ObjectMap>();
            check(pointArtifacts.at("passed").To<bool>()&&rows.size()==pointResult.Coverage.size()&&markers.size()==1
                &&row.at("kind").To<std::string>()=="Pending"
                &&row.at("firstParameter").To<double>()==pending.FirstParameter&&row.at("lastParameter").To<double>()==pending.LastParameter
                &&marker.at("coverageKey")==row.at("key")&&marker.at("firstParameter")==row.at("firstParameter")
                &&marker.at("lastParameter")==row.at("lastParameter")&&marker.at("builderError").To<long long>()==6,
                "STEP point presentation retains the original strictly positive Pending span, ownership, and exact JSON native endpoints");
            check(pointAudit.at("pointMarkerCount").To<int>()==1&&pointAudit.at("restoredPointMarkers").To<int>()==1
                &&pointAudit.at("pointMarkerPositionsPreserved").To<bool>()&&pointAudit.at("pointMarkerMaxPositionErrorMm").To<double>()==0.
                &&pointAudit.at("curveStylesPreserved").To<bool>()
                &&beforePointExport==Json::Serialize(Summary(pointResult))
                &&rectangleSplit==G::Persistence::Serialize(rectangle.Classification.SplitGeometry)
                &&rectangleMachining==G::Persistence::Serialize(rectangle.Classification.MachiningGeometry),
                "the point marker round trip preserves its name, exact position, and color while leaving coverage and complete CAD unchanged");
            auto zeroResult=pointResult;zeroResult.Coverage.front().LastParameter=zeroResult.Coverage.front().FirstParameter;
            bool zeroRejected=false;
            try {machining_face_cut_coverage_export::Export(rectangle.Classification,rectangle.Edges,rectangle.Paths,
                zeroResult,rectangleBuilt,directory/"invalid-zero-native-overlay","invalid-zero-native-overlay");}
            catch(const Standard_Failure&){zeroRejected=true;}catch(const std::exception&){zeroRejected=true;}
            check(zeroRejected,"a zero native domain is rejected and cannot use the positive-span point-marker presentation");
            auto outOfRangeBuilt=rectangleBuilt;
            const auto finiteEdge=TopoDS::Edge(BRepBuilderAPI_NurbsConvert(source,true).Shape());
            TopLoc_Location finiteLocation;double finiteFirst,finiteLast;
            const auto finiteCurve=BRep_Tool::Curve(finiteEdge,finiteLocation,finiteFirst,finiteLast);
            auto outOfRangeResult=pointResult;auto& invalid=outOfRangeResult.Coverage.front();
            invalid.FirstParameter=finiteLast+1.;invalid.LastParameter=finiteLast+2.;
            BRepBuilderAPI_MakeEdge outside(finiteCurve,invalid.FirstParameter,invalid.LastParameter);
            outOfRangeBuilt.Edges[invalid.SourceEdgeId]=finiteEdge;bool outsideRejected=false;
            try {machining_face_cut_coverage_export::Export(rectangle.Classification,rectangle.Edges,rectangle.Paths,
                outOfRangeResult,outOfRangeBuilt,directory/"invalid-outside-native-overlay","invalid-outside-native-overlay");}
            catch(const Standard_Failure&){outsideRejected=true;}catch(const std::exception&){outsideRejected=true;}
            check(!outside.IsDone()&&outside.Error()==BRepBuilderAPI_ParameterOutOfRange&&outsideRejected,
                "a native parameter-out-of-range builder error remains an export failure instead of becoming a point marker");
        }
        auto invalidFaceClassification=rectangle.Classification;
        auto& invalidCoedge=invalidFaceClassification.SplitGeometry.Wires.front().Coedges.front();
        invalidCoedge.Orientation=invalidCoedge.Orientation==G::ETopologyOrientation::Forward?G::ETopologyOrientation::Reversed:G::ETopologyOrientation::Forward;
        invalidFaceClassification.MachiningGeometry=R::detail::SectionFaceSubset(invalidFaceClassification.SplitGeometry,invalidFaceClassification.MachiningFaceIds);
        const auto invalidOriginal=G::Persistence::Serialize(invalidFaceClassification.SplitGeometry);
        const auto invalidBuilt=O::ReconstructOpenCascadeShape(invalidFaceClassification.SplitGeometry);
        const auto invalidFaceCoverage=R::MachiningFaceCutCoverageWork{}.Run(invalidFaceClassification,rectangle.Edges,rectangle.Paths);
        Save(directory/"invalid-original-plane-face-result.json",Summary(invalidFaceCoverage));
        check(invalidBuilt.bOK&&!BRepCheck_Analyzer(invalidBuilt.Faces.begin()->second,true).IsValid()&&!invalidFaceCoverage.bComplete
            &&std::none_of(invalidFaceCoverage.Evidence.begin(),invalidFaceCoverage.Evidence.end(),[](const auto& proof){return proof.bThrough;})
            &&invalidOriginal==G::Persistence::Serialize(invalidFaceClassification.SplitGeometry),
            "an invalid original native planar face cannot reuse cached valid curve facts to fabricate Through evidence and is left byte-for-byte unchanged");

        using ProjectionStatus=projection_contract_audit::NativeProjectionEventStatus;
        bool rotatedNativeConics=true;
        for(const bool ellipse:{false,true})for(const bool forward:{false,true}) {
            const gp_Ax2d axis(gp_Pnt2d(7,-3),gp_Dir2d(std::cos(.37),std::sin(.37)));
            Handle(Geom2d_Curve) native=ellipse?Handle(Geom2d_Curve)(new Geom2d_Ellipse(axis,3.,.75,forward)):Handle(Geom2d_Curve)(new Geom2d_Circle(axis,2.,forward));
            projection_contract_audit::CurveUse use{native,0.,2*Pi};const auto unchanged=native->Value(.123);std::vector<double> events;std::string reason;
            const auto status=projection_contract_audit::AppendNativeConicProjectionEvents(use,events,reason);
            rotatedNativeConics=rotatedNativeConics&&status==ProjectionStatus::Complete&&events.size()==2&&native->Value(.123).Distance(unchanged)==0.;
            for(const auto parameter:events){gp_Pnt2d point;gp_Vec2d derivative;native->D1(parameter,point,derivative);
                rotatedNativeConics=rotatedNativeConics&&parameter>use.First&&parameter<use.Last&&std::abs(derivative.X())<1.e-13;}
        }
        check(rotatedNativeConics,"native rotated and reversed Circle/Ellipse projection extrema solve their unchanged analytic derivative and remain strictly inside the original parameter interval");
        projection_contract_audit::CurveUse lineUse{new Geom2d_Line(gp_Pnt2d(3,4),gp_Dir2d(1,2)),-3.,2.};std::string projectionReason;
        std::vector<double> lineEvents{-3.,2.};const auto lineStatus=projection_contract_audit::AppendNativeConicProjectionEvents(lineUse,lineEvents,projectionReason);
        TColgp_Array1OfPnt2d splinePoles(1,2);splinePoles(1)={0,0};splinePoles(2)={1,1};TColStd_Array1OfReal splineKnots(1,2);splineKnots(1)=0.;splineKnots(2)=1.;TColStd_Array1OfInteger splineMult(1,2);splineMult(1)=splineMult(2)=2;
        projection_contract_audit::CurveUse splineUse{new Geom2d_BSplineCurve(splinePoles,splineKnots,splineMult,1),0.,1.};std::vector<double> unsupportedEvents{.25};
        const auto unsupportedStatus=projection_contract_audit::AppendNativeConicProjectionEvents(splineUse,unsupportedEvents,projectionReason);
        check(lineStatus==ProjectionStatus::Complete&&lineEvents==std::vector<double>{-3.,2.}&&unsupportedStatus==ProjectionStatus::Unsupported&&unsupportedEvents==std::vector<double>{.25},
            "native Line adds no critical event and an unsupported BSpline leaves its caller's native event list unchanged");
        const Handle(Geom2d_Circle) tinyCircle=new Geom2d_Circle(gp_Ax2d(gp_Pnt2d(100,0),gp_Dir2d(1,0)),1.);
        projection_contract_audit::CurveUse tinyConic{tinyCircle,0.,1.e-9};double bandLower=-7.,bandUpper=9.;
        const bool tinyBound=projection_contract_audit::BoundNativeConicProjection(tinyConic,0.,1.e-9,bandLower,bandUpper,projectionReason);
        check(tinyCircle->Value(0.).X()==tinyCircle->Value(1.e-9).X()&&tinyBound&&std::isfinite(bandLower)&&std::isfinite(bandUpper)&&bandUpper>bandLower
            &&bandLower<=tinyCircle->Value(0.).X()&&bandUpper>=tinyCircle->Value(0.).X(),
            "a positive original conic tail whose endpoint projections round equal still has a finite positive outward continuous projection band");
        double rejectedLower=123.,rejectedUpper=456.;const bool rejectedBound=projection_contract_audit::BoundNativeConicProjection(tinyConic,-1.e-9,1.e-9,rejectedLower,rejectedUpper,projectionReason);
        auto invalidDomain=tinyConic;invalidDomain.Last=invalidDomain.First;std::vector<double> invalidEvents{42.};
        const auto invalidStatus=projection_contract_audit::AppendNativeConicProjectionEvents(invalidDomain,invalidEvents,projectionReason);
        auto excessive=tinyConic;excessive.Last=1.e6*Pi;std::vector<double> excessiveEvents{43.};
        const auto excessiveStatus=projection_contract_audit::AppendNativeConicProjectionEvents(excessive,excessiveEvents,projectionReason);
        check(!rejectedBound&&rejectedLower==123.&&rejectedUpper==456.&&invalidStatus==ProjectionStatus::Invalid&&invalidEvents==std::vector<double>{42.}
            &&excessiveStatus==ProjectionStatus::Invalid&&excessiveEvents==std::vector<double>{43.},
            "projection queries cannot expand the original trim and invalid or excessive native domains leave output bands and event lists untouched");
        projection_contract_audit::Branch exactLine{1,new Geom2d_Line(gp_Pnt2d(0,0),gp_Dir2d(1,0)),0.,10.};double nearFirst=-7.,nearLast=-8.,outside=-9.;
        const auto nearFirstStatus=projection_contract_audit::SolveNativeConicProjectionParameter(exactLine,1.e-13,nearFirst,projectionReason);
        const double lastInterior=std::nextafter(10.,0.);
        const auto nearLastStatus=projection_contract_audit::SolveNativeConicProjectionParameter(exactLine,lastInterior,nearLast,projectionReason);
        const auto outsideStatus=projection_contract_audit::SolveNativeConicProjectionParameter(exactLine,-1.e-13,outside,projectionReason);
        check(nearFirstStatus==ProjectionStatus::Complete&&nearFirst==1.e-13&&nearFirst!=exactLine.First&&nearLastStatus==ProjectionStatus::Complete&&nearLast==lastInterior&&nearLast!=exactLine.Last
            &&outsideStatus==ProjectionStatus::Invalid&&outside==-9.,
            "native Line inversion preserves actual positive interior parameters below the old event tolerance and never clamps an exterior query into an endpoint");
        bool uniqueConicInverse=true;
        for(const bool ellipse:{false,true})for(const bool forward:{false,true}){const gp_Ax2d axis(gp_Pnt2d(7,-3),gp_Dir2d(std::cos(.37),std::sin(.37)));
            Handle(Geom2d_Curve) native=ellipse?Handle(Geom2d_Curve)(new Geom2d_Ellipse(axis,3.,.75,forward)):Handle(Geom2d_Curve)(new Geom2d_Circle(axis,2.,forward));
            projection_contract_audit::Branch branch{2,native,.5,1.1};double root=-99.;const double s=native->Value(.8).X();
            const auto status=projection_contract_audit::SolveNativeConicProjectionParameter(branch,s,root,projectionReason);
            uniqueConicInverse=uniqueConicInverse&&status==ProjectionStatus::Complete&&std::abs(root-.8)<1.e-13&&std::abs(native->Value(root).X()-s)<1.e-13;}
        projection_contract_audit::Branch wholeConic{3,tinyCircle,0.,2*Pi},halfConic{3,tinyCircle,0.,Pi},unsupportedInverse{4,splineUse.Curve,0.,1.};
        double multipleRoot=-11.,plusRoot=-12.,minusRoot=-13.,noRealRoot=-14.,unsupportedRoot=-15.;
        const auto multipleStatus=projection_contract_audit::SolveNativeConicProjectionParameter(wholeConic,100.,multipleRoot,projectionReason);
        const auto plusStatus=projection_contract_audit::SolveNativeConicProjectionParameter(halfConic,101.,plusRoot,projectionReason);
        const auto minusStatus=projection_contract_audit::SolveNativeConicProjectionParameter(halfConic,99.,minusRoot,projectionReason);
        const auto noRealStatus=projection_contract_audit::SolveNativeConicProjectionParameter(halfConic,std::nextafter(101.,std::numeric_limits<double>::infinity()),noRealRoot,projectionReason);
        const auto unsupportedInverseStatus=projection_contract_audit::SolveNativeConicProjectionParameter(unsupportedInverse,.5,unsupportedRoot,projectionReason);
        check(uniqueConicInverse&&multipleStatus==ProjectionStatus::Invalid&&multipleRoot==-11.&&plusStatus==ProjectionStatus::Complete&&plusRoot==0.
            &&minusStatus==ProjectionStatus::Complete&&minusRoot==Pi&&noRealStatus==ProjectionStatus::Invalid&&noRealRoot==-14.
            &&unsupportedInverseStatus==ProjectionStatus::Unsupported&&unsupportedRoot==-15.,
            "analytic conic inversion returns one original native root, rejects two-root full circles and exterior ratios, and counts the coincident plus/minus-one root family exactly once");
        for(const bool ellipse:{false,true}) {
            const auto nativeCorner=NativeConicCornerEnd(ellipse);
            const auto nativeCornerCoverage=R::MachiningFaceCutCoverageWork{}.Run(nativeCorner.Classification,nativeCorner.Edges,nativeCorner.Paths);
            const auto nativeCornerBuilt=O::ReconstructOpenCascadeShape(nativeCorner.Classification.SplitGeometry);
            const auto* longSource=BottomSource(nativeCorner);double provenNativeLength=0.,unprovenNativeLength=0.;
            if(longSource)for(const auto& atom:nativeCornerCoverage.Coverage)if(atom.SourcePathKey==longSource->Key){
                if(atom.Kind==Kind::Through)provenNativeLength+=atom.LastParameter-atom.FirstParameter;
                if(atom.Kind==Kind::Pending)unprovenNativeLength+=atom.LastParameter-atom.FirstParameter;}
            Save(directory/(ellipse?"native-ellipse-tiny-tail-local-result.json":"native-circle-tiny-tail-local-result.json"),Summary(nativeCornerCoverage));
            check(longSource&&nativeCornerCoverage.bSuccess&&provenNativeLength>99.&&unprovenNativeLength<1.e-5
                &&std::any_of(nativeCorner.Edges.Edges.begin(),nativeCorner.Edges.Edges.end(),[&](const auto& edge){return edge.Convexity==R::EEdgeConvexity::Convex
                    &&BRepAdaptor_Curve(nativeCornerBuilt.Edges.at(edge.EdgeId)).GetType()==(ellipse?GeomAbs_Ellipse:GeomAbs_Circle);})
                &&NativeIntervals(nativeCornerCoverage,nativeCornerBuilt)&&SourceEvidenceConsistent(nativeCornerCoverage,&nativeCorner.Paths)&&nativeCorner.SourceUnchanged,
                ellipse?"a genuine convex native Ellipse projection tail around its stationary parameter cannot discard the independently proven remainder of a 100 mm source or alter the original complete solid":
                    "a genuine convex native Circle projection tail around its stationary parameter cannot discard the independently proven remainder of a 100 mm source or alter the original complete solid");
        }

        auto conservativeMapper=rectangleResult;std::set<std::uint64_t> finalUpstream;
        for(auto& source:conservativeMapper.Sources){source.bComplete=false;source.bExitCorrespondenceExpanded=false;source.Reason="original-conservative-mapper-diagnostic-retained";}
        final_state_contract_audit::FinalizeSourceCoverageState(rectangle.Paths,conservativeMapper,finalUpstream);
        check(finalUpstream.empty()&&std::all_of(conservativeMapper.Sources.begin(),conservativeMapper.Sources.end(),[](const auto& source){return source.bComplete&&source.bExitCorrespondenceExpanded
                &&source.Reason.find("original-conservative-mapper-diagnostic-retained")!=std::string::npos;})
            &&NativeIntervals(conservativeMapper,rectangleBuilt)&&SourceEvidenceConsistent(conservativeMapper,&rectangle.Paths),
            "exact final positive native proof and arrival unions restore conservative mapper flags while preserving its diagnostic and unchanged real evidence");
        auto oneUlpGap=rectangleResult;const auto* oneUlpPath=BottomSource(rectangle);bool preparedOneUlp=false;
        if(oneUlpPath){const double firstGap=5.,lastGap=std::nextafter(5.,std::numeric_limits<double>::infinity());
            for(std::size_t i=0;i<oneUlpGap.Coverage.size();++i)if(oneUlpGap.Coverage[i].SourcePathKey==oneUlpPath->Key){
                auto tail=oneUlpGap.Coverage[i];oneUlpGap.Coverage[i].LastParameter=firstGap;tail.FirstParameter=lastGap;oneUlpGap.Coverage.push_back(std::move(tail));break;}
            for(std::size_t i=0;i<oneUlpGap.Arrivals.size();++i)if(oneUlpGap.Arrivals[i].SourcePathKey==oneUlpPath->Key){
                const auto original=oneUlpGap.Arrivals[i];const auto crop=[&](double a,double b){auto result=original;
                    const auto targetAt=[&](double p){return original.TargetFirstParameter+(p-original.SourceFirstParameter)/(original.SourceLastParameter-original.SourceFirstParameter)*(original.TargetLastParameter-original.TargetFirstParameter);};
                    const double x=targetAt(a),y=targetAt(b);result.SourceFirstParameter=x<y?a:b;result.SourceLastParameter=x<y?b:a;result.TargetFirstParameter=std::min(x,y);result.TargetLastParameter=std::max(x,y);return result;};
                const double lo=std::min(original.SourceFirstParameter,original.SourceLastParameter),hi=std::max(original.SourceFirstParameter,original.SourceLastParameter);
                oneUlpGap.Arrivals[i]=crop(lo,firstGap);oneUlpGap.Arrivals.push_back(crop(lastGap,hi));preparedOneUlp=true;break;}}
        finalUpstream.clear();final_state_contract_audit::FinalizeSourceCoverageState(rectangle.Paths,oneUlpGap,finalUpstream);
        check(preparedOneUlp&&std::any_of(oneUlpGap.Sources.begin(),oneUlpGap.Sources.end(),[&](const auto& source){return source.SourcePathKey==oneUlpPath->Key&&!source.bComplete&&!source.bExitCorrespondenceExpanded;})
            &&finalUpstream.contains(oneUlpPath->FaceId)&&NativeIntervals(oneUlpGap,rectangleBuilt),
            "one representable ulp of missing original native proof and arrival domain prevents both completeness flags from being restored");
        auto upstreamProof=rectangleResult;finalUpstream={rectangle.Paths.Edges.front().FaceId};
        final_state_contract_audit::FinalizeSourceCoverageState(rectangle.Paths,upstreamProof,finalUpstream);
        bool preservedUpstream=finalUpstream.contains(rectangle.Paths.Edges.front().FaceId);auto unresolvedPaths=rectangle.Paths;
        unresolvedPaths.UnresolvedBoundaries.push_back({rectangle.Paths.Edges.front().FaceId,0,{},"real-upstream-boundary-contract-remains-unresolved"});
        finalUpstream.clear();final_state_contract_audit::FinalizeSourceCoverageState(unresolvedPaths,upstreamProof,finalUpstream);
        check(preservedUpstream&&finalUpstream.contains(rectangle.Paths.Edges.front().FaceId),
            "complete local source evidence never clears independent upstream missing geometry or an unresolved original boundary");

        const auto canonicalFixture=NativeCanonicalCorner();const auto canonical=Analyze(canonicalFixture.Face,canonicalFixture.Section);
        const auto canonicalResult=R::MachiningFaceCutCoverageWork{}.Run(canonical.Classification,canonical.Edges,canonical.Paths);
        Save(directory/"canonical-corner-positive-result.json",Summary(canonicalResult));
        check(canonicalResult.bSuccess&&canonicalResult.bComplete&&std::all_of(canonicalResult.Coverage.begin(),canonicalResult.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Through;})
            &&canonical.SourceUnchanged&&NativeIntervals(canonicalResult,O::ReconstructOpenCascadeShape(canonical.Classification.SplitGeometry)),
            "tolerant original TopoVertex positions leave the real unchanged curve endpoint events and complete native affine correspondence intact");
        auto canonicalAttributes=canonical.Edges;const auto canonicalBuilt=O::ReconstructOpenCascadeShape(canonical.Classification.SplitGeometry);
        std::uint64_t topEdge=0,bottomEdge=0;
        for(const auto& [id,edge]:canonicalBuilt.Edges){BRepAdaptor_Curve curve(edge);const auto a=curve.Value(curve.FirstParameter()),b=curve.Value(curve.LastParameter());
            if(a.Z()==1.&&b.Z()==1.)topEdge=id;if(a.Z()==0.&&b.Z()==0.)bottomEdge=id;}
        for(std::size_t i=0;i<canonicalAttributes.Edges.size();++i)if(canonicalAttributes.Edges[i].EdgeId==topEdge){
            auto tail=canonicalAttributes.Edges[i];canonicalAttributes.Edges[i].LastParameter=.99995;tail.FirstParameter=.99995;tail.Key+=":native-internal-event";canonicalAttributes.Edges.push_back(std::move(tail));break;}
        const auto canonicalSplitPaths=R::MachiningPathAnalysisWork{}.Run(canonical.Classification,canonicalFixture.Section,canonicalAttributes);
        const auto nativeSplitResult=R::MachiningFaceCutCoverageWork{}.Run(canonical.Classification,canonicalAttributes,canonicalSplitPaths);
        Save(directory/"canonical-corner-internal-target-event-result.json",Summary(nativeSplitResult));
        std::size_t originalAttributeArrivals=0;bool originalAttributePairing=true,headInterval=false,tailInterval=false;
        for(const auto& arrival:nativeSplitResult.Arrivals)if(arrival.SourceEdgeId==bottomEdge&&arrival.TargetEdgeId==topEdge){++originalAttributeArrivals;
            originalAttributePairing=originalAttributePairing&&arrival.SourceFirstParameter==arrival.TargetFirstParameter&&arrival.SourceLastParameter==arrival.TargetLastParameter;
            headInterval=headInterval||(arrival.SourceFirstParameter==0.&&arrival.SourceLastParameter==.99995);
            tailInterval=tailInterval||(arrival.SourceFirstParameter==.99995&&arrival.SourceLastParameter==1.);}
        check(topEdge&&bottomEdge&&nativeSplitResult.bSuccess&&nativeSplitResult.bComplete&&originalAttributeArrivals==2&&originalAttributePairing&&headInterval&&tailInterval
            &&std::all_of(nativeSplitResult.Coverage.begin(),nativeSplitResult.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Through;})
            &&canonical.SourceUnchanged&&NativeIntervals(nativeSplitResult,canonicalBuilt),
            "a native attribute split beyond a displaced TopoVertex still maps its exact two original source and target intervals instead of replacing curve geometry with a vertex coordinate");

        const auto optionalFixture=NativeCanonicalCorner(.9999995,1.e-7);const auto optionalNative=Analyze(optionalFixture.Face,optionalFixture.Section);
        const auto optionalResult=R::MachiningFaceCutCoverageWork{}.Run(optionalNative.Classification,optionalNative.Edges,optionalNative.Paths);
        Save(directory/"optional-shared-vertex-normalization-keeps-native-result.json",Summary(optionalResult));
        check(BRepCheck_Analyzer(optionalFixture.Face,true).IsValid()&&optionalResult.bSuccess&&optionalResult.bComplete
            &&std::all_of(optionalResult.Coverage.begin(),optionalResult.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Through;})
            &&optionalNative.SourceUnchanged&&SourceEvidenceConsistent(optionalResult,&optionalNative.Paths),
            "a valid original face with a smaller declared face tolerance follows its actual unchanged curves without moving their endpoints to the different tolerant vertex positions");
        const auto outwardFixture=NativeCanonicalCorner(46.+2.e-10,.001,46.,46.+4.e-10);
        const auto outwardNative=Analyze(outwardFixture.Face,outwardFixture.Section);
        const auto outwardCoverage=R::MachiningFaceCutCoverageWork{}.Run(outwardNative.Classification,outwardNative.Edges,outwardNative.Paths);
        const auto* outwardSource=BottomSource(outwardNative);double outwardProven=0.;
        if(outwardSource)for(const auto& atom:outwardCoverage.Coverage)if(atom.SourcePathKey==outwardSource->Key&&atom.Kind==Kind::Through)outwardProven+=atom.LastParameter-atom.FirstParameter;
        Save(directory/"outward-different-canonical-corners-native-range-result.json",Summary(outwardCoverage));
        check(outwardSource&&outwardCoverage.bSuccess&&std::any_of(outwardCoverage.Sources.begin(),outwardCoverage.Sources.end(),[&](const auto& source){return source.SourcePathKey==outwardSource->Key&&source.bComplete&&source.bExitCorrespondenceExpanded;})
            &&outwardProven==46.&&outwardNative.SourceUnchanged,
            "different legal outward shared-vertex events cannot discard a whole 46 mm original native Line when its finite target curve still has a complete unchanged mapping");

        // Two real native arrival intervals have a positive representable
        // separation smaller than the inverse solver's event tolerance. The
        // source completeness claim must agree with their exact union.
        auto tinyGapAttributes=rectangle.Edges;std::uint64_t tinyGapTarget=0;
        for(const auto& [id,edge]:rectangleBuilt.Edges){BRepAdaptor_Curve curve(edge);const auto a=curve.Value(curve.FirstParameter()),b=curve.Value(curve.LastParameter());
            if(a.Z()==10.&&b.Z()==10.)tinyGapTarget=id;}
        const double gapFirst=5.-1.e-12,gapLast=5.;
        for(std::size_t i=0;i<tinyGapAttributes.Edges.size();++i)if(tinyGapAttributes.Edges[i].EdgeId==tinyGapTarget){
            auto tail=tinyGapAttributes.Edges[i];tinyGapAttributes.Edges[i].LastParameter=gapFirst;tail.FirstParameter=gapLast;tail.Key+=":positive-native-gap-tail";tinyGapAttributes.Edges.push_back(std::move(tail));break;}
        const auto tinyGapPaths=R::MachiningPathAnalysisWork{}.Run(rectangle.Classification,RectangleSection(),tinyGapAttributes);
        const auto tinyGapResult=R::MachiningFaceCutCoverageWork{}.Run(rectangle.Classification,tinyGapAttributes,tinyGapPaths);
        Save(directory/"positive-sub-tolerance-native-arrival-gap-result.json",Summary(tinyGapResult));
        std::string tinyGapSourceKey;
        for(const auto& path:tinyGapPaths.Edges)if(path.bSelectedPath&&path.Samples.size()==2
            &&path.Samples.front().Position.Z==0.&&path.Samples.back().Position.Z==0.)tinyGapSourceKey=path.Key;
        const auto tinyGapSource=std::find_if(tinyGapResult.Sources.begin(),tinyGapResult.Sources.end(),[&](const auto& source){return source.SourcePathKey==tinyGapSourceKey;});
        check(tinyGapTarget&&!tinyGapSourceKey.empty()&&tinyGapResult.bSuccess&&!tinyGapResult.bComplete
            &&tinyGapSource!=tinyGapResult.Sources.end()&&!tinyGapSource->bComplete
            &&tinyGapSource->Reason.find("source-native-interval-not-fully-mapped-to-physical-boundaries")!=std::string::npos,
            "a positive native arrival-union gap below 2e-12 keeps its source incomplete instead of merging the solver tolerance into evidence");
        check(std::any_of(tinyGapResult.Coverage.begin(),tinyGapResult.Coverage.end(),[&](const auto& atom){return atom.SourcePathKey==tinyGapSourceKey
                &&atom.Kind==Kind::Pending&&atom.LastParameter>atom.FirstParameter&&atom.LastParameter-atom.FirstParameter<2.e-12;})
            &&rectangle.SourceUnchanged&&rectangleSplit==G::Persistence::Serialize(rectangle.Classification.SplitGeometry)
            &&NativeIntervals(tinyGapResult,rectangleBuilt),
            "every representable positive arrival-union gap remains an explicit Pending native interval and leaves the complete CAD unchanged");

        const auto tilted=Analyze(PolygonFace({{0,0,0},{0,10,0},{0,18,10},{0,8,10}}),
            PolygonSection({{0,0},{10,0},{18,10},{8,10}}));
        const auto tiltedSplit=G::Persistence::Serialize(tilted.Classification.SplitGeometry);
        const auto tiltedMachining=G::Persistence::Serialize(tilted.Classification.MachiningGeometry);
        const auto tiltedResult=R::MachiningFaceCutCoverageWork{}.Run(tilted.Classification,tilted.Edges,tilted.Paths);
        const auto tiltedBuilt=O::ReconstructOpenCascadeShape(tilted.Classification.SplitGeometry);
        Save(directory/"tilted-result.json",Summary(tiltedResult));
        std::set<std::string> tiltedHorizontalSources;
        for(const auto& path:tilted.Paths.Edges)if(path.bSelectedPath&&path.Samples.size()==2
            &&path.Samples.front().Position.Z==path.Samples.back().Position.Z)tiltedHorizontalSources.insert(path.Key);
        check(tiltedResult.bSuccess&&tiltedHorizontalSources.size()==2&&SourceSetComplete(tiltedResult,tiltedHorizontalSources),
            "both horizontal native drivers of the large tilted parallelogram completely map to their real first boundary");
        const auto* bottom=BottomSource(tilted);std::set<std::uint64_t> destinations;bool adjacent=false,exactSwitch=true;
        bool slantRange=false,topRange=false;
        if(bottom)for(const auto& arrival:tiltedResult.Arrivals)if(arrival.SourcePathKey==bottom->Key) {
            destinations.insert(arrival.TargetEdgeId);BRepAdaptor_Curve target(tiltedBuilt.Edges.at(arrival.TargetEdgeId));
            const auto a=target.Value(arrival.TargetFirstParameter),b=target.Value(arrival.TargetLastParameter);
            adjacent=adjacent||(std::abs(a.Z()-b.Z())>1.);
            BRepAdaptor_Curve source(tiltedBuilt.Edges.at(arrival.SourceEdgeId));
            const auto p=source.Value(arrival.SourceFirstParameter),q=source.Value(arrival.SourceLastParameter);
            exactSwitch=exactSwitch&&std::abs(p.X()-a.X())<1.e-7&&std::abs(q.X()-b.X())<1.e-7
                &&std::abs(p.Y()-a.Y())<1.e-7&&std::abs(q.Y()-b.Y())<1.e-7
                &&a.Z()>=p.Z()-1.e-7&&b.Z()>=q.Z()-1.e-7;
            const double first=std::min(p.Y(),q.Y()),last=std::max(p.Y(),q.Y());
            if(std::abs(a.Z()-b.Z())>1.)slantRange=slantRange||(std::abs(first)<1.e-7&&std::abs(last-8.)<1.e-7);
            else topRange=topRange||(std::abs(first-8.)<1.e-7&&std::abs(last-10.)<1.e-7);
        }
        check(bottom&&destinations.size()==2&&adjacent,
            "bottom source reaches both adjacent slanted boundary and top boundary rather than a fixed opposite edge");
        check(bottom&&exactSwitch&&slantRange&&topRange,
            "bottom native source intervals switch at y=8 and exact endpoint pairs follow the recorded positive-Z axis");
        check(std::all_of(tiltedResult.Coverage.begin(),tiltedResult.Coverage.end(),[&](const auto& atom){return !tiltedHorizontalSources.contains(atom.SourcePathKey)||atom.Kind==Kind::Through;})
            &&OnlyPositiveTinyNativeLinePending(tiltedResult,tiltedBuilt,tiltedHorizontalSources)&&tilted.SourceUnchanged,
            "the proven tilted drivers stay Through while each extra slanted-Line numerical cell remains positive, below 1e-10 mm, and explicitly Pending without CAD changes");
        check(NativeIntervals(tiltedResult,tiltedBuilt)&&SourceEvidenceConsistent(tiltedResult,&tilted.Paths),
            "tilted target/source intervals stay native and source keys remain unique on every atom");
        const auto tiltedArtifacts=machining_face_cut_coverage_export::Export(tilted.Classification,tilted.Edges,
            tilted.Paths,tiltedResult,tiltedBuilt,directory/"tilted","tilted");
        check(tiltedArtifacts.at("passed").To<bool>(),"tilted complete BREP and colored interval STEP round trips pass");
        check(tilted.SourceUnchanged&&tiltedSplit==G::Persistence::Serialize(tilted.Classification.SplitGeometry)
            &&tiltedMachining==G::Persistence::Serialize(tilted.Classification.MachiningGeometry),
            "tilted upstream, coverage, and export preserve complete CAD bytes");

        const auto extreme=Analyze(PolygonFace({{0,0,0},{0,10,0},{0,100,1},{0,90,1}}),
            PolygonSection({{0,0},{10,0},{100,1},{90,1}}));
        const auto extremeResult=R::MachiningFaceCutCoverageWork{}.Run(extreme.Classification,extreme.Edges,extreme.Paths);
        const auto extremeBuilt=O::ReconstructOpenCascadeShape(extreme.Classification.SplitGeometry);
        Save(directory/"extreme-tilted-result.json",Summary(extremeResult));
        const auto* extremeBottom=BottomSource(extreme);std::size_t extremeArrivals=0;bool extremeTarget=true;
        if(extremeBottom)for(const auto& arrival:extremeResult.Arrivals)if(arrival.SourcePathKey==extremeBottom->Key) {
            ++extremeArrivals;BRepAdaptor_Curve target(extremeBuilt.Edges.at(arrival.TargetEdgeId));
            BRepAdaptor_Curve source(extremeBuilt.Edges.at(arrival.SourceEdgeId));
            const auto a=target.Value(arrival.TargetFirstParameter),b=target.Value(arrival.TargetLastParameter);
            const auto p=source.Value(arrival.SourceFirstParameter),q=source.Value(arrival.SourceLastParameter);
            extremeTarget=extremeTarget&&std::abs(a.Y()-90.*a.Z())<1.e-7&&std::abs(b.Y()-90.*b.Z())<1.e-7
                &&std::abs(a.Y()-p.Y())<1.e-7&&std::abs(b.Y()-q.Y())<1.e-7
                &&std::abs(std::min(p.Y(),q.Y()))<1.e-7&&std::abs(std::max(p.Y(),q.Y())-10.)<1.e-7;
        }
        check(extremeResult.bSuccess&&extremeBottom&&SourceSetComplete(extremeResult,{extremeBottom->Key})&&extremeArrivals==1&&extremeTarget,
            "extreme (0,0),(10,0),(100,1),(90,1) bottom source entirely first-reaches the adjacent y=90z slant");
        check(extremeBottom&&OnlyPositiveTinyNativeLinePending(extremeResult,extremeBuilt,{extremeBottom->Key})&&extreme.SourceUnchanged,
            "the extreme parallelogram retains every extra positive slanted-Line numerical cell below 1e-10 mm with its native-resolution reason and unchanged CAD");
        check(NativeIntervals(extremeResult,extremeBuilt)&&SourceEvidenceConsistent(extremeResult,&extreme.Paths)&&extreme.SourceUnchanged,
            "extreme narrow face keeps exact native intervals, per-source counts, and original CAD");

        const auto box=Analyze(BRepPrimAPI_MakeBox(10,10,10).Shape(),RectangleSection());
        const auto boxResult=R::MachiningFaceCutCoverageWork{}.Run(box.Classification,box.Edges,box.Paths);
        Save(directory/"shared-faces-result.json",Summary(boxResult));
        std::map<std::uint64_t,std::set<std::uint64_t>> targetFaces;std::map<std::string,std::uint64_t> sourceFaces;bool ownership=true;
        for(const auto& source:boxResult.Sources)sourceFaces.emplace(source.SourcePathKey,source.FaceId);
        for(const auto& span:boxResult.Coverage) {
            targetFaces[span.TargetEdgeId].insert(span.FaceId);
            for(const auto& key:span.SourcePathKeys)ownership=ownership&&sourceFaces.contains(key)&&sourceFaces.at(key)==span.FaceId;
        }
        check(boxResult.bSuccess&&ownership&&std::any_of(targetFaces.begin(),targetFaces.end(),[](const auto& pair){return pair.second.size()>1;}),
            "shared physical edges retain separate faceId coverage and never pair sources across faces");
        check(SourceEvidenceConsistent(boxResult,&box.Paths),"shared-face evidence indices and distinct-source coverage are internally consistent");

        auto missing=rectangle.Paths;std::string missingKey;std::uint64_t opposite=0;
        if(const auto* source=BottomSource(rectangle)) {
            missingKey=source->Key;
            for(const auto& arrival:rectangleResult.Arrivals)if(arrival.SourcePathKey==missingKey)opposite=arrival.TargetEdgeId;
            for(auto& path:missing.Edges)if(path.Key==missingKey) {
                path.Status=R::EMachiningPathStatus::InvalidGeometry;
                for(auto& endpoint:path.Samples){endpoint.bToolAxisValid=false;endpoint.Status=R::EMachiningPathStatus::InvalidGeometry;}
            }
        }
        missing.bComplete=false;
        const auto missingResult=R::MachiningFaceCutCoverageWork{}.Run(rectangle.Classification,rectangle.Edges,missing);
        Save(directory/"missing-axis-result.json",Summary(missingResult));
        check(!missingKey.empty()&&missingResult.bSuccess&&!missingResult.bComplete
            &&std::any_of(missingResult.Sources.begin(),missingResult.Sources.end(),[&](const auto& s){return s.SourcePathKey==missingKey&&!s.bComplete;})
            &&std::none_of(missingResult.Arrivals.begin(),missingResult.Arrivals.end(),[&](const auto& a){return a.SourcePathKey==missingKey;}),
            "missing native endpoint axes keep an incomplete source and produce no invented arrivals");
        check(opposite&&std::any_of(missingResult.Coverage.begin(),missingResult.Coverage.end(),[&](const auto& s){
                return s.SourcePathKey==missingKey&&s.Kind==Kind::Pending&&!s.bMappingComplete&&!s.bFaceMappingComplete;})
            &&std::all_of(missingResult.Coverage.begin(),missingResult.Coverage.end(),[&](const auto& s){
                return s.SourcePathKey==missingKey||s.Kind==Kind::Through;})
            &&SourceEvidenceConsistent(missingResult,&missing),
            "one missing source stays Pending locally while independently completed sources retain Through evidence");
        check(std::none_of(missingResult.Evidence.begin(),missingResult.Evidence.end(),[](const auto& proof){
                return proof.Kind==R::EFaceCutEvidenceKind::SkinExitBoundarySet;}),
            "ideal-profile skin distance alone cannot activate the source-attributed complete-boundary theorem");

        const auto attributedEnd=SourceAttributedBoxEnd();auto noAxisEnd=attributedEnd.Paths;
        const std::string abstractSource=noAxisEnd.Edges.front().Key;
        for(auto& path:noAxisEnd.Edges)if(path.Key==abstractSource) {
            path.Status=R::EMachiningPathStatus::InvalidGeometry;
            for(auto& endpoint:path.Samples){endpoint.bToolAxisValid=false;endpoint.Status=R::EMachiningPathStatus::InvalidGeometry;}
        }
        noAxisEnd.bComplete=false;
        const auto attributedBuilt=O::ReconstructOpenCascadeShape(attributedEnd.Classification.SplitGeometry);
        const auto attributedCoverage=R::MachiningFaceCutCoverageWork{}.Run(attributedEnd.Classification,attributedEnd.Edges,noAxisEnd);
        Save(directory/"source-attributed-boundary-set-result.json",Summary(attributedCoverage));
        check(attributedCoverage.bSuccess&&attributedCoverage.bComplete&&attributedEnd.Classification.MachiningFaceIds.size()==1
            &&std::any_of(attributedCoverage.Evidence.begin(),attributedCoverage.Evidence.end(),[&](const auto& proof){
                return proof.SourcePathKey==abstractSource&&proof.Kind==R::EFaceCutEvidenceKind::SkinExitBoundarySet
                    &&proof.bThrough&&proof.bComplete&&!proof.bHasArrival&&!proof.bExitCorrespondenceExpanded
                    &&proof.BoundaryWitnesses.size()==4;})
            &&std::all_of(attributedCoverage.Coverage.begin(),attributedCoverage.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Through;})
            &&std::none_of(attributedCoverage.Arrivals.begin(),attributedCoverage.Arrivals.end(),[&](const auto& arrival){return arrival.SourcePathKey==abstractSource;})
            &&NativeIntervals(attributedCoverage,attributedBuilt)&&SourceEvidenceConsistent(attributedCoverage,&noAxisEnd),
            "a bounded valid plane with all physical boundaries on attributed source skin proves Through without inventing an opposite-edge arrival");
        const auto attributedArtifacts=machining_face_cut_coverage_export::Export(attributedEnd.Classification,attributedEnd.Edges,noAxisEnd,
            attributedCoverage,attributedBuilt,directory/"source-attributed-boundary-set","source-attributed-boundary-set");
        std::ifstream abstractFile(directory/"source-attributed-boundary-set.json",std::ios::binary);
        const std::string abstractText((std::istreambuf_iterator<char>(abstractFile)),{});
        const auto abstractJson=Json::Parse(abstractText).To<ObjectMap>();bool noInventedExitFields=false;
        for(const auto& value:abstractJson.at("evidence").To<VariantArray>()) {
            const auto row=value.To<ObjectMap>();if(row.at("hasArrival").To<bool>())continue;
            noInventedExitFields=!row.contains("arrivalIndex")&&!row.contains("witnessEdgeId")&&!row.contains("witnessSpanKey")
                &&row.at("boundaryWitnesses").To<VariantArray>().size()==4
                &&row.at("classificationProofComplete").To<bool>()&&!row.at("exitCorrespondenceExpanded").To<bool>();
        }
        check(attributedArtifacts.at("passed").To<bool>()&&noInventedExitFields&&attributedEnd.SourceUnchanged,
            "complete-model export distinguishes a classified boundary set from expanded exit correspondence and omits fictitious single-edge fields");

        const auto detachedLines=SourceAttributedDetachedLineEnd();
        const auto detachedLineResult=R::MachiningFaceCutCoverageWork{}.Run(detachedLines.Classification,detachedLines.Edges,detachedLines.Paths);
        Save(directory/"detached-native-lines-source-boundary-set-result.json",Summary(detachedLineResult));
        check(detachedLineResult.bSuccess&&detachedLineResult.bComplete
            &&std::all_of(detachedLines.Edges.Edges.begin(),detachedLines.Edges.Edges.end(),[](const auto& edge){return edge.bSourceSkinMembership&&!edge.SourceSkinFaceIds.empty()&&edge.SkinReason.find("native-line-original-parameter-degree-one")!=std::string::npos;})
            &&std::all_of(detachedLineResult.Coverage.begin(),detachedLineResult.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Through;})
            &&std::any_of(detachedLineResult.Evidence.begin(),detachedLineResult.Evidence.end(),[](const auto& proof){return proof.Kind==R::EFaceCutEvidenceKind::SkinExitBoundarySet&&!proof.bHasArrival&&!proof.bExitCorrespondenceExpanded;})
            &&detachedLines.SourceUnchanged,
            "four detached original finite native Line rims gain strict source-skin certificates and prove the closed all-skin plane without inventing a missing recorded endpoint axis");
        auto setOnlyState=detachedLineResult;finalUpstream.clear();
        final_state_contract_audit::FinalizeSourceCoverageState(detachedLines.Paths,setOnlyState,finalUpstream);
        const auto readyDetachedPaths=R::MachiningPathAnalysisWork{}.Run(detachedLines.Classification,RectangleSection(),detachedLines.Edges);
        const auto readyDetachedResult=R::MachiningFaceCutCoverageWork{}.Run(detachedLines.Classification,detachedLines.Edges,readyDetachedPaths);
        auto setWithArrivals=setOnlyState;setWithArrivals.Arrivals=readyDetachedResult.Arrivals;finalUpstream.clear();
        final_state_contract_audit::FinalizeSourceCoverageState(detachedLines.Paths,setWithArrivals,finalUpstream);
        check(std::any_of(setOnlyState.Sources.begin(),setOnlyState.Sources.end(),[](const auto& source){return source.bComplete&&!source.bExitCorrespondenceExpanded;})
            &&finalUpstream.empty()&&std::all_of(setWithArrivals.Sources.begin(),setWithArrivals.Sources.end(),[](const auto& source){return source.bComplete&&source.bExitCorrespondenceExpanded;}),
            "an unexpanded complete boundary-set proof stays classification-only until real full native arrival correspondence is independently available");
        const auto shortenedLines=SourceAttributedDetachedLineEnd(true);
        const auto shortenedLineResult=R::MachiningFaceCutCoverageWork{}.Run(shortenedLines.Classification,shortenedLines.Edges,shortenedLines.Paths);
        Save(directory/"detached-native-line-short-source-result.json",Summary(shortenedLineResult));
        check(shortenedLineResult.bSuccess&&!shortenedLineResult.bComplete
            &&std::any_of(shortenedLines.Edges.Edges.begin(),shortenedLines.Edges.Edges.end(),[](const auto& edge){return !edge.bSourceSkinMembership;})
            &&std::none_of(shortenedLineResult.Evidence.begin(),shortenedLineResult.Evidence.end(),[](const auto& proof){return proof.Kind==R::EFaceCutEvidenceKind::SkinExitBoundarySet;})
            &&std::any_of(shortenedLineResult.Coverage.begin(),shortenedLineResult.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Pending;})&&shortenedLines.SourceUnchanged,
            "a finite native Line source rim ending before the real machining boundary cannot extend its original trim or manufacture full stock-skin attribution");

        const auto closedSpline=SourceAttributedClosedSplineEnd();
        const auto closedSplineBuilt=O::ReconstructOpenCascadeShape(closedSpline.Classification.SplitGeometry);
        VariantArray closedInputEdges,closedInputPaths;
        for(const auto& edge:closedSpline.Edges.Edges)closedInputEdges.emplace_back(ObjectMap{
            {"edgeId",std::to_string(edge.EdgeId)},{"spanKey",edge.Key},{"skin",machining_edge_export::SkinName(edge.SkinRelation)},
            {"sourceSkinMembership",edge.bSourceSkinMembership},{"sourceSkinFaces",machining_edge_export::Ids(edge.SourceSkinFaceIds)},
            {"skinReason",edge.SkinReason}});
        for(const auto& path:closedSpline.Paths.Edges)closedInputPaths.emplace_back(ObjectMap{
            {"key",path.Key},{"axisMode",machining_path_export::ModeName(path.AxisMode)},
            {"status",machining_path_export::StatusName(path.Status)},{"reason",path.Reason}});
        Save(directory/"closed-nurbs-boundary-set-inputs.json",{{"edges",closedInputEdges},{"paths",closedInputPaths},
            {"nativeSurfaceType",static_cast<int>(BRepAdaptor_Surface(closedSplineBuilt.Faces.begin()->second).GetType())}});
        const auto closedSplineCoverage=R::MachiningFaceCutCoverageWork{}.Run(closedSpline.Classification,closedSpline.Edges,closedSpline.Paths);
        Save(directory/"closed-nurbs-boundary-set-result.json",Summary(closedSplineCoverage));
        check(closedSplineCoverage.bSuccess&&closedSplineCoverage.bComplete&&closedSplineCoverage.Sources.size()==2
            &&closedSplineCoverage.Arrivals.empty()
            &&std::all_of(closedSplineCoverage.Evidence.begin(),closedSplineCoverage.Evidence.end(),[](const auto& proof){
                return proof.Kind==R::EFaceCutEvidenceKind::SkinExitBoundarySet&&proof.bComplete&&proof.bThrough
                    &&!proof.bHasArrival&&!proof.bExitCorrespondenceExpanded&&proof.BoundaryWitnesses.size()==2;})
            &&std::all_of(closedSplineCoverage.Coverage.begin(),closedSplineCoverage.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Through;})
            &&std::any_of(closedSpline.Paths.Edges.begin(),closedSpline.Paths.Edges.end(),[](const auto& path){
                return path.bSelectedPath&&path.bClosed&&std::any_of(path.Samples.begin(),path.Samples.end(),[](const auto& end){
                    return !end.bTangentValid&&!end.bToolAxisValid&&end.Status==R::EMachiningPathStatus::InvalidGeometry;});})
            &&NativeIntervals(closedSplineCoverage,closedSplineBuilt)&&SourceEvidenceConsistent(closedSplineCoverage,&closedSpline.Paths),
            "two closed source-attributed NURBS skin loops prove Through as boundary sets while their real singular endpoint records remain invalid and no arrivals are fabricated");
        auto missingMembership=closedSpline.Edges;
        if(!missingMembership.Edges.empty()){missingMembership.Edges.front().bSourceSkinMembership=false;missingMembership.Edges.front().SourceSkinFaceIds.clear();}
        const auto missingMembershipCoverage=R::MachiningFaceCutCoverageWork{}.Run(closedSpline.Classification,missingMembership,closedSpline.Paths);
        Save(directory/"closed-nurbs-missing-source-membership-result.json",Summary(missingMembershipCoverage));
        check(missingMembershipCoverage.bSuccess&&!missingMembershipCoverage.bComplete
            &&std::none_of(missingMembershipCoverage.Evidence.begin(),missingMembershipCoverage.Evidence.end(),[](const auto& proof){return proof.Kind==R::EFaceCutEvidenceKind::SkinExitBoundarySet;})
            &&std::all_of(missingMembershipCoverage.Coverage.begin(),missingMembershipCoverage.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Pending;})
            &&closedSpline.SourceUnchanged,
            "removing one actual boundary's source-skin attribution conservatively disables the closed-loop theorem and retains Pending");

        const auto ruledEnd=SourceAttributedRuledEnd();
        const auto ruledCoverage=R::MachiningFaceCutCoverageWork{}.Run(ruledEnd.Classification,ruledEnd.Edges,ruledEnd.Paths);
        const auto ruledBuilt=O::ReconstructOpenCascadeShape(ruledEnd.Classification.SplitGeometry);
        Save(directory/"true-ruled-boundary-set-result.json",Summary(ruledCoverage));
        check(ruledCoverage.bSuccess&&!ruledCoverage.bComplete&&ruledCoverage.Evidence.empty()
            &&BRepCheck_Analyzer(ruledBuilt.Faces.at(ruledEnd.Classification.MachiningFaceIds.front())).IsValid()
            &&std::all_of(ruledCoverage.Coverage.begin(),ruledCoverage.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Pending;})
            &&std::any_of(ruledEnd.Paths.Edges.begin(),ruledEnd.Paths.Edges.end(),[](const auto& path){
                return path.bSelectedPath&&path.bClosed&&path.Status==R::EMachiningPathStatus::ParallelToGenerator
                    &&std::all_of(path.Samples.begin(),path.Samples.end(),[](const auto& endpoint){return !endpoint.bToolAxisValid;});})
            &&std::any_of(ruledEnd.Edges.Edges.begin(),ruledEnd.Edges.Edges.end(),[](const auto& edge){return !edge.bSourceSkinMembership;})
            &&std::all_of(ruledCoverage.Sources.begin(),ruledCoverage.Sources.end(),[](const auto& source){return source.Reason.find("source-skin-membership-not-proven")!=std::string::npos;})
            &&NativeIntervals(ruledCoverage,ruledBuilt)&&SourceEvidenceConsistent(ruledCoverage,&ruledEnd.Paths)&&ruledEnd.SourceUnchanged,
            "a valid exact U3/V1 ruled end without strict source-skin certificates remains Pending and retains its real parallel seam endpoints; actual strict ruled positives are independently checked on model 109");
        const auto ruledRectangle=SourceAttributedRuledEnd(true);
        const auto ruledRectangleCoverage=R::MachiningFaceCutCoverageWork{}.Run(ruledRectangle.Classification,ruledRectangle.Edges,ruledRectangle.Paths);
        Save(directory/"ruled-positive-length-parallel-result.json",Summary(ruledRectangleCoverage));
        bool parallelSourcesPending=true;std::size_t parallelSources=0;
        for(const auto& path:ruledRectangle.Paths.Edges)if(path.bSelectedPath&&path.Status==R::EMachiningPathStatus::ParallelToGenerator) {
            ++parallelSources;bool foundPending=false;
            for(const auto& atom:ruledRectangleCoverage.Coverage)if(atom.SourcePathKey==path.Key){foundPending=true;parallelSourcesPending=parallelSourcesPending&&atom.Kind==Kind::Pending;}
            parallelSourcesPending=parallelSourcesPending&&foundPending;
        }
        check(ruledRectangleCoverage.bSuccess&&!ruledRectangleCoverage.bComplete
            &&std::any_of(ruledRectangleCoverage.Sources.begin(),ruledRectangleCoverage.Sources.end(),[](const auto& source){
                return !source.bComplete&&source.Reason.find("positive-native-knot-interval-is-parallel-or-transversality-not-proven")!=std::string::npos;})
            &&parallelSources>0&&parallelSourcesPending
            &&std::any_of(ruledRectangleCoverage.Coverage.begin(),ruledRectangleCoverage.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Through;})
            &&ruledRectangle.SourceUnchanged,
            "a real positive-length stock-skin boundary parallel to the ruled generator stays Pending rather than being reclassified by the exit-set theorem");
        for(int failure=0;failure<2;++failure) {
            auto invalidSupport=ruledEnd.Classification;const auto surfaceId=invalidSupport.MachiningGeometry.Faces.front().Surface3Id;
            for(auto& surface:invalidSupport.SplitGeometry.Surfaces3)if(surface.Id==surfaceId) {
                auto* spline=std::get_if<G::BSplineSurface3>(&surface.Geometry);if(!spline)throw std::runtime_error("Ruled failure fixture lost its native spline support");
                for(std::uint32_t u=0;u<spline->UCount;++u) {
                    auto& a=spline->Poles[std::size_t(u)*spline->VCount];auto& b=spline->Poles[std::size_t(u)*spline->VCount+1];
                    if(!failure)b.Z=a.Z+4.*(2.*double(u)/double(spline->UCount-1)-1.);
                    else a.Y=b.Y=0.;
                }
            }
            invalidSupport.MachiningGeometry=R::detail::SectionFaceSubset(invalidSupport.SplitGeometry,invalidSupport.MachiningFaceIds);
            const auto before=G::Persistence::Serialize(invalidSupport.SplitGeometry);
            const auto rejected=R::MachiningFaceCutCoverageWork{}.Run(invalidSupport,ruledEnd.Edges,ruledEnd.Paths);
            Save(directory/(failure?"ruled-jacobian-failure-result.json":"ruled-zero-ruling-result.json"),Summary(rejected));
            check(rejected.bSuccess&&!rejected.bComplete
                &&std::none_of(rejected.Evidence.begin(),rejected.Evidence.end(),[](const auto& proof){return proof.bThrough;})
                &&before==G::Persistence::Serialize(invalidSupport.SplitGeometry),
                failure?"a ruled support with collapsed transverse Jacobian cannot reuse previous skin-boundary facts to fabricate Through":
                    "a ruled support whose generator vanishes inside its native domain is conservatively rejected without changing the source input");
        }

        BRep_Builder missingBoundaryBuilder;TopoDS_Compound missingBoundaryPair;
        missingBoundaryBuilder.MakeCompound(missingBoundaryPair);
        missingBoundaryBuilder.Add(missingBoundaryPair,PolygonFace({{0,0,0},{0,10,0},{0,10,10},{0,0,10}}));
        missingBoundaryBuilder.Add(missingBoundaryPair,PolygonFace({{4,2,2},{4,5,2},{4,3,4}}));
        const auto missingBoundary=Analyze(missingBoundaryPair,RectangleSection());
        std::uint64_t omittedFaceId=0;
        for(const auto faceId:missingBoundary.Classification.MachiningFaceIds)
            if(std::none_of(missingBoundary.Paths.Edges.begin(),missingBoundary.Paths.Edges.end(),[&](const auto& path){
                return path.FaceId==faceId&&path.bSelectedPath;}))omittedFaceId=faceId;
        auto omittedBoundary=missingBoundary.Edges;
        std::erase_if(omittedBoundary.Faces,[&](const auto& face){return face.FaceId==omittedFaceId;});
        const auto missingBoundaryResult=R::MachiningFaceCutCoverageWork{}.Run(
            missingBoundary.Classification,omittedBoundary,missingBoundary.Paths);
        Save(directory/"missing-face-boundary-result.json",Summary(missingBoundaryResult));
        check(omittedFaceId&&missingBoundaryResult.bSuccess&&!missingBoundaryResult.bComplete
            &&missingBoundaryResult.IncompleteFaceIds==std::vector<std::uint64_t>{omittedFaceId}
            &&missingBoundaryResult.Sources.size()==4
            &&std::all_of(missingBoundaryResult.Sources.begin(),missingBoundaryResult.Sources.end(),[](const auto& source){return source.bComplete;})
            &&std::all_of(missingBoundaryResult.Coverage.begin(),missingBoundaryResult.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Through;})
            &&SourceEvidenceConsistent(missingBoundaryResult,&missingBoundary.Paths),
            "a machining face with no skin sources and a missing boundary is explicitly incomplete while another face keeps its completed proofs");

        const auto sides=Analyze(PolygonFace({{0,0,0},{0,10,0},{0,18,10},{0,8,10}}),
            PolygonSection({{-100,0},{100,0},{100,10},{-100,10}}));
        const auto sidesResult=R::MachiningFaceCutCoverageWork{}.Run(sides.Classification,sides.Edges,sides.Paths);
        const auto sidesBuilt=O::ReconstructOpenCascadeShape(sides.Classification.SplitGeometry);
        Save(directory/"side-completion-result.json",Summary(sidesResult));
        check(sides.Paths.bComplete&&Selected(sides.Paths)==2
            &&std::count_if(sides.Paths.Edges.begin(),sides.Paths.Edges.end(),[](const auto& path){return path.bSide&&!path.bSelectedPath;})==2,
            "sheared end has precisely two skin-source paths and two separately retained logical sides");
        check(sidesResult.bSuccess&&sidesResult.bComplete
            &&std::any_of(sidesResult.Evidence.begin(),sidesResult.Evidence.end(),[](const auto& proof){
                return proof.Kind==R::EFaceCutEvidenceKind::SideCompletion&&proof.bThrough&&proof.bComplete;})
            &&std::all_of(sidesResult.Coverage.begin(),sidesResult.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Through;})
            &&NativeIntervals(sidesResult,sidesBuilt)&&SourceEvidenceConsistent(sidesResult,&sides.Paths),
            "shared-endpoint side correspondence completes sheared end gaps without an extra source or cut count");
        check(SideCompletionEndpointIdentity(sidesResult,sides.Paths,sidesBuilt),
            "every side completion joins true logical endpoints by TopoVertex or exact original-edge parameter identity rather than point proximity");
        auto nativeTargetCompletion=sidesResult;
        for(auto& arrival:nativeTargetCompletion.Arrivals) {
            const double first=arrival.SourceFirstParameter,last=arrival.SourceLastParameter;
            arrival.SourceFirstParameter=std::nextafter(first,last);
            arrival.SourceLastParameter=std::nextafter(last,first);
        }
        nativeTargetCompletion.Evidence.clear();nativeTargetCompletion.Coverage.clear();
        side_evidence_contract_audit::BuildSkinEvidence(sidesBuilt,sides.Paths,nativeTargetCompletion);
        side_evidence_contract_audit::BuildSourceSkinCoverage(sides.Paths,nativeTargetCompletion);
        const bool targetCompletesExistingArrivals=std::any_of(nativeTargetCompletion.Evidence.begin(),nativeTargetCompletion.Evidence.end(),[](const auto& proof){
            return proof.Kind==R::EFaceCutEvidenceKind::SideCompletion&&proof.bThrough&&proof.bComplete
                &&proof.Reason.find("target-native-component")!=std::string::npos;});
        check(targetCompletesExistingArrivals
            &&std::any_of(nativeTargetCompletion.Coverage.begin(),nativeTargetCompletion.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Pending;})
            &&SideCompletionEndpointIdentity(nativeTargetCompletion,sides.Paths,sidesBuilt)
            &&NativeIntervals(nativeTargetCompletion,sidesBuilt)&&SourceEvidenceConsistent(nativeTargetCompletion,&sides.Paths),
            "an exact target-side component at its real incident vertex proves only the existing arrivals while every one-ULP source endpoint gap stays Pending");
        auto separatedTarget=nativeTargetCompletion;
        for(auto& arrival:separatedTarget.Arrivals) {
            const double first=arrival.TargetFirstParameter,last=arrival.TargetLastParameter;
            arrival.TargetFirstParameter=std::nextafter(first,last);
            arrival.TargetLastParameter=std::nextafter(last,first);
        }
        const auto disconnected=side_evidence_contract_audit::EndSideCompletions(sidesBuilt,sides.Paths,separatedTarget);
        check(disconnected.empty(),
            "positive one-ULP gaps in both original source and target domains cannot be bridged by shared TopoVertex identity or proximity");
        bool noUnprovedReverseFamily=true;
        for(const std::string method:{"plane-circle-radial-first-physical-boundary",
            "plane-certified-normal-strip-first-boundary","straight-generator-uv-first-physical-boundary"}) {
            auto unprovedFamily=nativeTargetCompletion;
            for(auto& arrival:unprovedFamily.Arrivals)arrival.Method=method;
            noUnprovedReverseFamily=noUnprovedReverseFamily
                &&side_evidence_contract_audit::EndSideCompletions(sidesBuilt,sides.Paths,unprovedFamily).empty();
        }
        check(noUnprovedReverseFamily,
            "target-side completion cannot infer reverse continuity for radial, curved normal or periodic-generator image families from target intervals alone");
        check(machining_face_cut_coverage_export::Export(sides.Classification,sides.Edges,sides.Paths,
            sidesResult,sidesBuilt,directory/"side-completion","side-completion").at("passed").To<bool>(),
            "sheared end evidence exports in one complete original model");

        const auto eventFixture=NativePartialSkinEvent();auto nativeEvent=Analyze(eventFixture.Face,eventFixture.Section);
        const auto nativeEventBuilt=O::ReconstructOpenCascadeShape(nativeEvent.Classification.SplitGeometry);
        VariantArray nativeEventEndpointInputs;
        // This isolates Coverage's exact native-event contract. A C0 corner
        // has no unique whole-edge tangent at t=1, so Path's automatic trim
        // sense is deliberately not part of this fixture. Supply the exact
        // conical mother line at the same two original parameters, without
        // probing any nearby position or changing source geometry.
        for(auto& path:nativeEvent.Paths.Edges)if(path.bSelectedPath) {
            for(auto& endpoint:path.Samples) {
                nativeEventEndpointInputs.emplace_back(ObjectMap{{"sourcePathKey",path.Key},{"parameter",endpoint.Parameter},
                    {"originalStatus",machining_path_export::StatusName(endpoint.Status)},{"originalReason",endpoint.Reason}});
                double first,last;const auto pcurve=BRep_Tool::CurveOnSurface(
                    nativeEventBuilt.Edges.at(endpoint.SourceEdgeId),nativeEventBuilt.Faces.at(path.FaceId),first,last);
                const auto uv=pcurve->Value(endpoint.Parameter);gp_Pnt point;gp_Vec du,dv;
                BRepAdaptor_Surface(nativeEventBuilt.Faces.at(path.FaceId)).D1(uv.X(),uv.Y(),point,du,dv);
                dv.Normalize();dv.Reverse();endpoint.ToolAxis={dv.X(),dv.Y(),dv.Z()};
                endpoint.bToolAxisValid=true;endpoint.Status=R::EMachiningPathStatus::Ready;
                endpoint.Reason="fixture-exact-recorded-cone-generator-coverage-input";
            }
            path.Status=R::EMachiningPathStatus::Ready;
        }
        Save(directory/"native-partial-skin-event-endpoint-inputs.json",{{"coverageOnlyFixture",true},
            {"endpoints",nativeEventEndpointInputs},{"sourceCadUnchanged",nativeEvent.SourceUnchanged}});
        const auto nativeEventResult=R::MachiningFaceCutCoverageWork{}.Run(nativeEvent.Classification,nativeEvent.Edges,nativeEvent.Paths);
        Save(directory/"native-partial-skin-event-result.json",Summary(nativeEventResult));
        bool exactInternalTransition=false;
        for(const auto& source:nativeEvent.Paths.Edges)if(source.bSelectedPath&&!source.bClosed)
            for(const auto& side:nativeEvent.Paths.Edges)if(side.bSide&&source.FaceId==side.FaceId&&source.WireId==side.WireId)
                for(const auto& a:source.Samples)for(const auto& b:side.Samples)
                    if(a.SourceEdgeId==b.SourceEdgeId&&a.Parameter==b.Parameter) {
                        const auto& native=nativeEventBuilt.Edges.at(a.SourceEdgeId);BRepAdaptor_Curve curve(native);
                        exactInternalTransition=exactInternalTransition||(a.Parameter>curve.FirstParameter()
                            &&a.Parameter<curve.LastParameter()&&Count(native,TopAbs_VERTEX)==2);
                    }
        check(exactInternalTransition,
            "an internal skin-to-side transition has exact original-edge parameter identity at true logical endpoints without a TopoVertex");
        check(nativeEventResult.bSuccess&&!nativeEventResult.bComplete
            &&std::any_of(nativeEventResult.Coverage.begin(),nativeEventResult.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Pending;})
            &&!nativeEventResult.Arrivals.empty()
            &&std::none_of(nativeEventResult.Coverage.begin(),nativeEventResult.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Partial;})
            &&NativeIntervals(nativeEventResult,nativeEventBuilt)&&SourceEvidenceConsistent(nativeEventResult,&nativeEvent.Paths),
            "unsupported C0 far branches and free boundaries without material-retention proof stay Pending while their real arrivals are retained");

        straight_boundary_contract_audit::Run(directory,check);
        const auto curvedFixture=BezierNormalStrip();const auto curvedStrip=Analyze(curvedFixture.Face,curvedFixture.Section);
        const auto curvedBuilt=O::ReconstructOpenCascadeShape(curvedStrip.Classification.SplitGeometry);
        const auto curvedCoverage=R::MachiningFaceCutCoverageWork{}.Run(curvedStrip.Classification,curvedStrip.Edges,curvedStrip.Paths);
        Save(directory/"bezier-normal-strip-result.json",Summary(curvedCoverage));
        check(Selected(curvedStrip.Paths)==2&&std::count_if(curvedStrip.Paths.Edges.begin(),curvedStrip.Paths.Edges.end(),[](const auto& edge){return edge.bSide;})==2,
            "curved planar strip has two exact Bezier skin chains and two original straight sides");
        check(curvedCoverage.bSuccess&&curvedCoverage.bComplete&&curvedCoverage.Sources.size()==2
            &&std::all_of(curvedCoverage.Sources.begin(),curvedCoverage.Sources.end(),[](const auto& source){
                return source.bComplete&&source.Method=="plane-certified-normal-strip-first-boundary";})
            &&std::all_of(curvedCoverage.Coverage.begin(),curvedCoverage.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Through;})
            &&std::any_of(curvedCoverage.Evidence.begin(),curvedCoverage.Evidence.end(),[](const auto& proof){return proof.Kind==R::EFaceCutEvidenceKind::SideCompletion;})
            &&NativeIntervals(curvedCoverage,curvedBuilt)&&SourceEvidenceConsistent(curvedCoverage,&curvedStrip.Paths)
            &&SideCompletionEndpointIdentity(curvedCoverage,curvedStrip.Paths,curvedBuilt),
            "a continuous positive-weight Bezier normal-family certificate covers both skin chains, including their exact end-side switch");
        check(machining_face_cut_coverage_export::Export(curvedStrip.Classification,curvedStrip.Edges,curvedStrip.Paths,
            curvedCoverage,curvedBuilt,directory/"bezier-normal-strip","bezier-normal-strip").at("passed").To<bool>()&&curvedStrip.SourceUnchanged,
            "certified Bezier normal coverage exports a complete unchanged BREP and colored STEP");
        const auto reversedCurvedFixture=BezierNormalStrip(false,true);
        const auto reversedCurved=Analyze(reversedCurvedFixture.Face,reversedCurvedFixture.Section);
        const auto reversedCurvedCoverage=R::MachiningFaceCutCoverageWork{}.Run(reversedCurved.Classification,reversedCurved.Edges,reversedCurved.Paths);
        Save(directory/"reversed-bezier-normal-strip-result.json",Summary(reversedCurvedCoverage));
        check(reversedCurvedCoverage.bSuccess&&reversedCurvedCoverage.bComplete
            &&std::all_of(reversedCurvedCoverage.Coverage.begin(),reversedCurvedCoverage.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Through;})
            &&SourceEvidenceConsistent(reversedCurvedCoverage,&reversedCurved.Paths)&&reversedCurved.SourceUnchanged,
            "reversing a curved strip face preserves its native normal correspondence and physical interior sense");
        const auto rationalFixture=BezierNormalStrip(false,false,true);
        const auto rationalStrip=Analyze(rationalFixture.Face,rationalFixture.Section);
        const auto rationalCoverage=R::MachiningFaceCutCoverageWork{}.Run(rationalStrip.Classification,rationalStrip.Edges,rationalStrip.Paths);
        const auto rationalBuilt=O::ReconstructOpenCascadeShape(rationalStrip.Classification.SplitGeometry);
        Save(directory/"rational-bezier-normal-strip-result.json",Summary(rationalCoverage));
        check(rationalCoverage.bSuccess&&rationalCoverage.bComplete&&Selected(rationalStrip.Paths)==2
            &&std::all_of(rationalCoverage.Coverage.begin(),rationalCoverage.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Through;})
            &&NativeIntervals(rationalCoverage,rationalBuilt)&&SourceEvidenceConsistent(rationalCoverage,&rationalStrip.Paths)
            &&rationalStrip.SourceUnchanged,
            "positive nonuniform rational Bezier weights retain a certified continuous normal family and the original native parameter intervals");
        const auto foldedFixture=BezierNormalStrip(true);const auto folded=Analyze(foldedFixture.Face,foldedFixture.Section);
        const auto foldedCoverage=R::MachiningFaceCutCoverageWork{}.Run(folded.Classification,folded.Edges,folded.Paths);
        Save(directory/"uncertified-bezier-normal-strip-result.json",Summary(foldedCoverage));
        check(foldedCoverage.bSuccess&&!foldedCoverage.bComplete
            &&std::any_of(foldedCoverage.Sources.begin(),foldedCoverage.Sources.end(),[](const auto& source){
                return source.Reason.find("continuous-control-net-certificate-not-proven")!=std::string::npos;})
            &&std::all_of(foldedCoverage.Coverage.begin(),foldedCoverage.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Pending;})
            &&folded.SourceUnchanged,
            "a curved strip outside the strict tangent-cone and normal-family certificate remains Pending without fabricated through coverage");

        const auto ridge=SteppedEnd(true);
        const auto ridgeResult=R::MachiningFaceCutCoverageWork{}.Run(ridge.Classification,ridge.Edges,ridge.Paths);
        const auto ridgeBuilt=O::ReconstructOpenCascadeShape(ridge.Classification.SplitGeometry);
        Save(directory/"x-ridge-result.json",Summary(ridgeResult));
        std::set<std::uint64_t> ridgeSides;
        for(const auto& path:ridge.Paths.Edges)if(path.bSide&&path.bHasConvexEvidence&&!path.bSelectedPath)
            ridgeSides.insert(path.SourceEdgeIds.begin(),path.SourceEdgeIds.end());
        check(ridge.Classification.MachiningFaceIds.size()==2&&Selected(ridge.Paths)==6&&!ridgeSides.empty(),
            "X end central convex ridge with both endpoints on skin remains side evidence and never a cutting path");
        check(ridgeResult.bSuccess&&ridgeResult.bComplete
            &&std::all_of(ridgeResult.Coverage.begin(),ridgeResult.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Through;})
            &&std::any_of(ridgeResult.Evidence.begin(),ridgeResult.Evidence.end(),[&](const auto& proof){
                return proof.Kind==R::EFaceCutEvidenceKind::ConvexBoundary&&proof.bThrough&&ridgeSides.contains(proof.WitnessEdgeId);})
            &&NativeIntervals(ridgeResult,ridgeBuilt)&&SourceEvidenceConsistent(ridgeResult,&ridge.Paths),
            "the X ridge proves both skin paths Through even though its logical records also satisfy the side definition");
        check(machining_face_cut_coverage_export::Export(ridge.Classification,ridge.Edges,ridge.Paths,
            ridgeResult,ridgeBuilt,directory/"x-ridge","x-ridge").at("passed").To<bool>(),
            "X end classification and evidence retain complete solid BREP and colored STEP");

        const auto floor=SteppedEnd(false);
        const auto floorResult=R::MachiningFaceCutCoverageWork{}.Run(floor.Classification,floor.Edges,floor.Paths);
        const auto floorBuilt=O::ReconstructOpenCascadeShape(floor.Classification.SplitGeometry);
        Save(directory/"concave-floor-result.json",Summary(floorResult));
        std::set<std::string> floorSources;
        for(const auto& proof:floorResult.Evidence)if(proof.Kind==R::EFaceCutEvidenceKind::ConcaveTermination)
            floorSources.insert(proof.SourcePathKey);
        check(floorResult.bSuccess&&floorResult.bComplete&&!floorSources.empty()
            &&std::any_of(floorResult.Coverage.begin(),floorResult.Coverage.end(),[&](const auto& atom){
                return floorSources.contains(atom.SourcePathKey)&&atom.Kind==Kind::Partial&&atom.bMappingComplete;})
            &&std::none_of(floorResult.Evidence.begin(),floorResult.Evidence.end(),[&](const auto& proof){
                return floorSources.contains(proof.SourcePathKey)&&proof.Kind==R::EFaceCutEvidenceKind::SideCompletion;})
            &&NativeIntervals(floorResult,floorBuilt)&&SourceEvidenceConsistent(floorResult,&floor.Paths),
            "a true solid concave floor terminates a skin path as Partial and is never rescued by an unrelated side");
        check(machining_face_cut_coverage_export::Export(floor.Classification,floor.Edges,floor.Paths,
            floorResult,floorBuilt,directory/"concave-floor","concave-floor").at("passed").To<bool>(),
            "the real stepped concave floor retains every original model face in BREP and STEP");

        const auto annulusFixture=Annulus();const auto annulus=Analyze(annulusFixture.Face,annulusFixture.Section);
        const auto annulusBuilt=O::ReconstructOpenCascadeShape(annulus.Classification.SplitGeometry);
        const auto annulusResult=R::MachiningFaceCutCoverageWork{}.Run(annulus.Classification,annulus.Edges,annulus.Paths);
        Save(directory/"plain-pipe-end-result.json",Summary(annulusResult));
        check(annulusResult.bSuccess&&annulusResult.bComplete&&annulusResult.Sources.size()==2
            &&std::all_of(annulusResult.Coverage.begin(),annulusResult.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Through;})
            &&std::none_of(annulus.Paths.Edges.begin(),annulus.Paths.Edges.end(),[](const auto& path){return path.bSide||path.bHasConvexEvidence;})
            &&NativeIntervals(annulusResult,annulusBuilt)&&SourceEvidenceConsistent(annulusResult,&annulus.Paths),
            "a plain circular pipe end with two skin loops and no convex or side edges is Through by SkinExit");

        const auto ringFixture=AnnularSector();
        const auto ring=Analyze(ringFixture.Face,ringFixture.Section);
        const auto ringSplit=G::Persistence::Serialize(ring.Classification.SplitGeometry);
        const auto ringMachining=G::Persistence::Serialize(ring.Classification.MachiningGeometry);
        const auto ringBuilt=O::ReconstructOpenCascadeShape(ring.Classification.SplitGeometry);
        const auto ringResult=R::MachiningFaceCutCoverageWork{}.Run(ring.Classification,ring.Edges,ring.Paths);
        Save(directory/"concentric-ring-result.json",Summary(ringResult));
        auto ringCircles=CircleSourceKeys(ring,ringBuilt,2.);const auto ringOuter=CircleSourceKeys(ring,ringBuilt,4.);
        ringCircles.insert(ringOuter.begin(),ringOuter.end());
        check(ringResult.bSuccess&&ringCircles.size()==2&&SourceSetComplete(ringResult,ringCircles),
            "concentric annular sector maps both true native Circle source families completely");
        std::size_t localRingLinePending=0;bool ringLineUncertaintyLocal=true;
        for(const auto& span:ringResult.Coverage)if(span.Kind==Kind::Pending){++localRingLinePending;
            ringLineUncertaintyLocal=ringLineUncertaintyLocal&&BRepAdaptor_Curve(ringBuilt.Edges.at(span.SourceEdgeId)).GetType()==GeomAbs_Line
                &&span.LastParameter>span.FirstParameter&&span.LastParameter-span.FirstParameter<1.e-10
                &&span.Reason.find("target-native-projection-uncertainty-is-local")!=std::string::npos;}
        check(localRingLinePending>0&&!ringResult.bComplete&&ringLineUncertaintyLocal&&ring.SourceUnchanged
            &&ringSplit==G::Persistence::Serialize(ring.Classification.SplitGeometry)&&ringMachining==G::Persistence::Serialize(ring.Classification.MachiningGeometry),
            "the annular sector preserves every positive radial-Line endpoint uncertainty below 1e-10 mm in its local conic band instead of falsely completing the whole model");
        std::size_t ringArcAtoms=0;bool ringArcsThrough=true;
        for(const auto& span:ringResult.Coverage)if(BRepAdaptor_Curve(ringBuilt.Edges.at(span.TargetEdgeId)).GetType()==GeomAbs_Circle) {
            ++ringArcAtoms;ringArcsThrough=ringArcsThrough&&span.Kind==Kind::Through&&span.SourcePathKeys.size()==1;
        }
        check(ringArcAtoms>=2&&ringArcsThrough&&NativeIntervals(ringResult,ringBuilt)&&SourceEvidenceConsistent(ringResult,&ring.Paths),
            "inner and outer concentric skin arcs are Through on their own native source intervals");
        const auto ringArtifacts=machining_face_cut_coverage_export::Export(ring.Classification,ring.Edges,ring.Paths,
            ringResult,ringBuilt,directory/"concentric-ring","concentric-ring");
        check(ringArtifacts.at("passed").To<bool>(),"concentric arc coverage exports a complete colored model with valid BREP and STEP round trips");

        const auto unequalFixture=AnnularSector(Pi/6,Pi/3);
        const auto unequal=Analyze(unequalFixture.Face,unequalFixture.Section);
        const auto unequalSplit=G::Persistence::Serialize(unequal.Classification.SplitGeometry);
        const auto unequalMachining=G::Persistence::Serialize(unequal.Classification.MachiningGeometry);
        const auto unequalBuilt=O::ReconstructOpenCascadeShape(unequal.Classification.SplitGeometry);
        const auto unequalResult=R::MachiningFaceCutCoverageWork{}.Run(unequal.Classification,unequal.Edges,unequal.Paths);
        Save(directory/"unequal-arc-result.json",Summary(unequalResult));
        const auto unequalOuter=CircleSourceKeys(unequal,unequalBuilt,4.);std::set<std::uint64_t> unequalTargets;
        bool unequalExact=true,switchLow=false,switchMiddle=false,switchHigh=false;
        for(const auto& arrival:unequalResult.Arrivals)if(unequalOuter.contains(arrival.SourcePathKey)) {
            unequalTargets.insert(arrival.TargetEdgeId);BRepAdaptor_Curve source(unequalBuilt.Edges.at(arrival.SourceEdgeId));
            BRepAdaptor_Curve target(unequalBuilt.Edges.at(arrival.TargetEdgeId));
            const auto p=source.Value(arrival.SourceFirstParameter),q=source.Value(arrival.SourceLastParameter);
            const auto a=target.Value(arrival.TargetFirstParameter),b=target.Value(arrival.TargetLastParameter);
            unequalExact=unequalExact&&std::abs(p.Y()*a.Z()-p.Z()*a.Y())<1.e-6
                &&std::abs(q.Y()*b.Z()-q.Z()*b.Y())<1.e-6&&p.Y()*a.Y()+p.Z()*a.Z()>=-1.e-7;
            const double first=std::min(std::atan2(p.Z(),p.Y()),std::atan2(q.Z(),q.Y()));
            const double last=std::max(std::atan2(p.Z(),p.Y()),std::atan2(q.Z(),q.Y()));
            if(target.GetType()==GeomAbs_Circle)switchMiddle=switchMiddle||(std::abs(first-Pi/6)<1.e-7&&std::abs(last-Pi/3)<1.e-7);
            else {
                switchLow=switchLow||(std::abs(first)<1.e-7&&std::abs(last-Pi/6)<1.e-7);
                switchHigh=switchHigh||(std::abs(first-Pi/3)<1.e-7&&std::abs(last-Pi/2)<1.e-7);
            }
        }
        check(unequalResult.bSuccess&&unequalResult.bComplete&&unequalOuter.size()==1&&SourceSetComplete(unequalResult,unequalOuter),
            "unequal inner and outer arc spans retain a complete analytic radial source family");
        check(unequalTargets.size()==3&&unequalExact&&switchLow&&switchMiddle&&switchHigh,
            "outer arc switches exactly at pi/6 and pi/3 between two adjacent Lines and the shorter inner Circle");
        check(NativeIntervals(unequalResult,unequalBuilt)&&SourceEvidenceConsistent(unequalResult,&unequal.Paths),
            "arc-to-adjacent-line arrivals retain directed source and exact native target intervals");
        const auto unequalArtifacts=machining_face_cut_coverage_export::Export(unequal.Classification,unequal.Edges,unequal.Paths,
            unequalResult,unequalBuilt,directory/"unequal-arc","unequal-arc");
        check(unequalArtifacts.at("passed").To<bool>(),"arc target-switch coverage round trips in one complete colored model");

        const auto diskFace=BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(FixtureCircle()).Edge()).Wire()).Face();
        const auto crossing=Analyze(diskFace,CircleSection(2.));
        const auto crossingSplit=G::Persistence::Serialize(crossing.Classification.SplitGeometry);
        const auto crossingMachining=G::Persistence::Serialize(crossing.Classification.MachiningGeometry);
        const auto crossingBuilt=O::ReconstructOpenCascadeShape(crossing.Classification.SplitGeometry);
        const auto crossingResult=R::MachiningFaceCutCoverageWork{}.Run(crossing.Classification,crossing.Edges,crossing.Paths);
        Save(directory/"circle-cross-center-result.json",Summary(crossingResult));
        bool crossesCenter=!crossingResult.Arrivals.empty(),sameNativeEdge=false;
        for(const auto& arrival:crossingResult.Arrivals) {
            BRepAdaptor_Curve source(crossingBuilt.Edges.at(arrival.SourceEdgeId)),target(crossingBuilt.Edges.at(arrival.TargetEdgeId));
            const auto p=source.Value(arrival.SourceFirstParameter),q=source.Value(arrival.SourceLastParameter);
            const auto a=target.Value(arrival.TargetFirstParameter),b=target.Value(arrival.TargetLastParameter);
            crossesCenter=crossesCenter&&a.Distance(gp_Pnt(0,10.-p.Y(),10.-p.Z()))<1.e-6
                &&b.Distance(gp_Pnt(0,10.-q.Y(),10.-q.Z()))<1.e-6;
            sameNativeEdge=sameNativeEdge||arrival.SourceEdgeId==arrival.TargetEdgeId;
        }
        check(crossingResult.bSuccess&&crossingResult.bComplete&&crossingResult.Sources.size()==1&&crossesCenter&&sameNativeEdge,
            "an inward full Circle family crosses its center and first reaches the far interval of the same native edge");
        check(!crossingResult.Coverage.empty()&&std::all_of(crossingResult.Coverage.begin(),crossingResult.Coverage.end(),[](const auto& span){
                return span.Kind==Kind::Through&&span.SourcePathKeys.size()==1;})&&NativeIntervals(crossingResult,crossingBuilt)&&SourceEvidenceConsistent(crossingResult,&crossing.Paths),
            "the distant skin interval of the same closed Circle is valid Through evidence without requiring another path key");
        const auto crossingArtifacts=machining_face_cut_coverage_export::Export(crossing.Classification,crossing.Edges,crossing.Paths,
            crossingResult,crossingBuilt,directory/"circle-cross-center","circle-cross-center");
        check(crossingArtifacts.at("passed").To<bool>(),"full periodic Circle coverage round trips without losing the complete model");

        const auto majorFixture=MajorCircleSegment();const auto major=Analyze(majorFixture.Face,majorFixture.Section);
        const auto majorSplit=G::Persistence::Serialize(major.Classification.SplitGeometry);
        const auto majorMachining=G::Persistence::Serialize(major.Classification.MachiningGeometry);
        const auto majorBuilt=O::ReconstructOpenCascadeShape(major.Classification.SplitGeometry);
        const auto majorResult=R::MachiningFaceCutCoverageWork{}.Run(major.Classification,major.Edges,major.Paths);
        Save(directory/"major-arc-cross-center-result.json",Summary(majorResult));
        const auto majorSources=CircleSourceKeys(major,majorBuilt,2.);bool farChord=false,farSelf=false;
        for(const auto& arrival:majorResult.Arrivals)if(majorSources.contains(arrival.SourcePathKey)) {
            BRepAdaptor_Curve source(majorBuilt.Edges.at(arrival.SourceEdgeId)),target(majorBuilt.Edges.at(arrival.TargetEdgeId));
            const auto p=source.Value(arrival.SourceFirstParameter),q=source.Value(arrival.SourceLastParameter);
            const auto a=target.Value(arrival.TargetFirstParameter),b=target.Value(arrival.TargetLastParameter);
            if(target.GetType()==GeomAbs_Line) {
                const double first=std::min(std::atan2(p.Z(),p.Y()),std::atan2(q.Z(),q.Y()));
                const double last=std::max(std::atan2(p.Z(),p.Y()),std::atan2(q.Z(),q.Y()));
                farChord=farChord||(std::abs(first+Pi/4)<1.e-7&&std::abs(last-Pi/4)<1.e-7
                    &&std::abs(a.Y()+std::sqrt(2.))<1.e-7&&std::abs(b.Y()+std::sqrt(2.))<1.e-7
                    &&p.Y()*a.Y()+p.Z()*a.Z()<0.&&q.Y()*b.Y()+q.Z()*b.Z()<0.);
            } else farSelf=farSelf||(arrival.SourceEdgeId==arrival.TargetEdgeId
                &&a.Distance(gp_Pnt(0,-p.Y(),-p.Z()))<1.e-6&&b.Distance(gp_Pnt(0,-q.Y(),-q.Z()))<1.e-6);
        }
        check(majorResult.bSuccess&&SourceSetComplete(majorResult,majorSources)&&farChord&&farSelf,
            "270-degree Circle family crosses center to the far chord for [-pi/4,pi/4] and far self-arc for remaining angles");
        check(OnlyPositiveTinyNativeLinePending(majorResult,majorBuilt,majorSources)&&major.SourceUnchanged,
            "the proven 270-degree Circle keeps its far destinations while its extra chord-Line numerical cell stays positive and below 1e-10 mm with the original CAD retained");
        check(NativeIntervals(majorResult,majorBuilt)&&SourceEvidenceConsistent(majorResult,&major.Paths),
            "negative radial destinations preserve actual edge intervals and distinct-source coverage counts");
        const auto majorArtifacts=machining_face_cut_coverage_export::Export(major.Classification,major.Edges,major.Paths,
            majorResult,majorBuilt,directory/"major-arc-cross-center","major-arc-cross-center");
        check(majorArtifacts.at("passed").To<bool>(),"major Circle and far-chord coverage retain one complete BREP and colored STEP");

        BRepBuilderAPI_Sewing sharedSewing(1.e-7);sharedSewing.Add(ringFixture.Face);sharedSewing.Add(QuarterDisk(2.).Face);sharedSewing.Perform();
        const auto shared=Analyze(sharedSewing.SewedShape(),QuarterDisk(4.).Section);
        const auto sharedSplit=G::Persistence::Serialize(shared.Classification.SplitGeometry);
        const auto sharedMachining=G::Persistence::Serialize(shared.Classification.MachiningGeometry);
        const auto sharedBuilt=O::ReconstructOpenCascadeShape(shared.Classification.SplitGeometry);
        const auto sharedResult=R::MachiningFaceCutCoverageWork{}.Run(shared.Classification,shared.Edges,shared.Paths);
        Save(directory/"shared-circle-result.json",Summary(sharedResult));
        std::map<std::uint64_t,std::set<std::uint64_t>> sharedCircleFaces;
        for(const auto& span:sharedResult.Coverage)if(BRepAdaptor_Curve(sharedBuilt.Edges.at(span.TargetEdgeId)).GetType()==GeomAbs_Circle)
            sharedCircleFaces[span.TargetEdgeId].insert(span.FaceId);
        std::map<std::string,std::uint64_t> sharedSources;for(const auto& source:sharedResult.Sources)sharedSources.emplace(source.SourcePathKey,source.FaceId);
        bool sharedOwnership=true;for(const auto& arrival:sharedResult.Arrivals)
            sharedOwnership=sharedOwnership&&sharedSources.at(arrival.SourcePathKey)==arrival.FaceId;
        check(sharedResult.bSuccess&&!sharedResult.bComplete&&sharedOwnership&&!sharedCircleFaces.empty()
            &&std::any_of(sharedResult.Coverage.begin(),sharedResult.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Pending;})
            &&std::none_of(sharedResult.Coverage.begin(),sharedResult.Coverage.end(),[](const auto& atom){return atom.Kind==Kind::Partial;})
            &&SourceEvidenceConsistent(sharedResult,&shared.Paths),
            "a shared coplanar Circle boundary without retained-material proof stays Pending and preserves separate face source ownership");
        check(std::any_of(sharedResult.Sources.begin(),sharedResult.Sources.end(),[&](const auto& source){return !source.bComplete&&source.bExitCorrespondenceExpanded
                &&std::any_of(sharedResult.Evidence.begin(),sharedResult.Evidence.end(),[&](const auto& proof){return proof.SourcePathKey==source.SourcePathKey&&proof.Kind==R::EFaceCutEvidenceKind::OtherBoundary;});}),
            "full raw native correspondence to a real shared coplanar boundary remains classified Pending instead of confusing expanded mapping with through or retained-material evidence");
        const auto sharedArtifacts=machining_face_cut_coverage_export::Export(shared.Classification,shared.Edges,shared.Paths,
            sharedResult,sharedBuilt,directory/"shared-circle","shared-circle");
        check(sharedArtifacts.at("passed").To<bool>(),"shared-circle face coverage exports one complete model with native identities retained");
        bool allArrivalsHavePositiveNativeLength=true;
        for(const auto* result:{&rectangleResult,&tiltedResult,&extremeResult,&boxResult,&missingResult,&attributedCoverage,&closedSplineCoverage,&missingMembershipCoverage,&ruledCoverage,&ruledRectangleCoverage,&missingBoundaryResult,
            &sidesResult,&nativeEventResult,&curvedCoverage,&reversedCurvedCoverage,&rationalCoverage,&foldedCoverage,&ridgeResult,&floorResult,&annulusResult,&ringResult,&unequalResult,&crossingResult,&majorResult,&sharedResult}) {
            for(const auto& arrival:result->Arrivals)
                allArrivalsHavePositiveNativeLength=allArrivalsHavePositiveNativeLength
                    &&std::isfinite(arrival.SourceFirstParameter)&&std::isfinite(arrival.SourceLastParameter)
                    &&arrival.SourceFirstParameter!=arrival.SourceLastParameter
                    &&std::isfinite(arrival.TargetFirstParameter)&&std::isfinite(arrival.TargetLastParameter)
                    &&arrival.TargetLastParameter>arrival.TargetFirstParameter;
            for(const auto& evidence:result->Evidence)
                allArrivalsHavePositiveNativeLength=allArrivalsHavePositiveNativeLength
                    &&evidence.SourceLastParameter>evidence.SourceFirstParameter;
        }
        check(allArrivalsHavePositiveNativeLength,
            "every emitted arrival and evidence has strictly positive native source length and every arrival target is positive; zero-measure endpoint contacts are not coverage records");
        check(ring.SourceUnchanged&&unequal.SourceUnchanged&&crossing.SourceUnchanged&&major.SourceUnchanged&&shared.SourceUnchanged
            &&ringSplit==G::Persistence::Serialize(ring.Classification.SplitGeometry)&&ringMachining==G::Persistence::Serialize(ring.Classification.MachiningGeometry)
            &&unequalSplit==G::Persistence::Serialize(unequal.Classification.SplitGeometry)&&unequalMachining==G::Persistence::Serialize(unequal.Classification.MachiningGeometry)
            &&crossingSplit==G::Persistence::Serialize(crossing.Classification.SplitGeometry)&&crossingMachining==G::Persistence::Serialize(crossing.Classification.MachiningGeometry)
            &&majorSplit==G::Persistence::Serialize(major.Classification.SplitGeometry)&&majorMachining==G::Persistence::Serialize(major.Classification.MachiningGeometry)
            &&sharedSplit==G::Persistence::Serialize(shared.Classification.SplitGeometry)&&sharedMachining==G::Persistence::Serialize(shared.Classification.MachiningGeometry),
            "all Circle fixtures preserve complete split and machining CAD bytes through upstream work, mapping, and export");
        Save(directory/"checks.json",{{"status",std::string(failures?"failed":"passed")},
            {"failedChecks",static_cast<int>(failures)},{"checks",checks}});return failures?1:0;
    } catch(const Standard_Failure& error) {
        Save(directory/"checks.json",{{"status",std::string("failed")},{"error",std::string(error.GetMessageString())},{"checks",checks}});return 1;
    } catch(const std::exception& error) {
        Save(directory/"checks.json",{{"status",std::string("failed")},{"error",std::string(error.what())},{"checks",checks}});return 1;
    }
}
