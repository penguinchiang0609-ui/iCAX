#include "pch.h"
#include "MachiningFaceCutCoverageWork.h"
#include "MachiningFaceCutCoverageIntervals.h"

#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <Adaptor3d_Curve.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Face.hxx>
#include <BRepCheck_Wire.hxx>
#include <BRepTopAdaptor_FClass2d.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <Geom2dAPI_ExtremaCurveCurve.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <Geom2dConvert_BSplineCurveToBezierCurve.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierSurface.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <GeomConvert_BSplineCurveToBezierCurve.hxx>
#include <Precision.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Cone.hxx>
#include <gp_Circ2d.hxx>
#include <gp_Elips2d.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf2d.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace iCAX::ExtrusionRecognition
{
namespace
{
namespace O = iCAX::OpenCascade;
namespace I = machining_face_cut_coverage;

double ParamTolerance(double first,double last)
{
    return 2.e-12*std::max({1.,std::abs(first),std::abs(last)});
}

bool HasFace(const SMachiningEdgeAnalysis& edge,std::uint64_t id)
{
    return std::find(edge.MachiningFaceIds.begin(),edge.MachiningFaceIds.end(),id)!=edge.MachiningFaceIds.end()
        ||std::find(edge.SourceMachiningFaceIds.begin(),edge.SourceMachiningFaceIds.end(),id)!=edge.SourceMachiningFaceIds.end();
}

gp_Vec Vec(const iCAX::GeometryData::Vector3& value) {return {value.X,value.Y,value.Z};}

// This chart is affine in a plane, or the support's unchanged UV frame on a
// certified straight-generator surface. It never flattens a general surface.
struct Chart final
{
    gp_Trsf2d Transform;
    int GeneratorDerivative=0;
    double Direction=1.;
    double STolerance=1.e-10;
    double HTolerance=1.e-10;
    bool bPlane=false;
    bool bPeriodic=false;
    double Period=0.;
    bool bHasGeneratorSingularity=false;
    double GeneratorSingularityH=0.;
    std::string Method;
};

struct CurveUse final
{
    std::uint64_t EdgeId=0;
    occ::handle<Geom2d_Curve> Curve;
    double First=0.,Last=0.;
    bool bReversed=false;
};

struct Branch final
{
    std::uint64_t EdgeId=0;
    occ::handle<Geom2d_Curve> Curve;
    double First=0.,Last=0.;
    double SFirst=0.,SLast=0.;
    bool bConstant=false;
};

struct ProjectionUncertainty final
{
    std::uint64_t EdgeId=0;
    double First=0.,Last=0.,Lower=0.,Upper=0.;
    std::string Reason;
};

bool ReadCurve(const TopoDS_Face& face,const TopoDS_Edge& edge,std::uint64_t edgeId,
    double requestedFirst,double requestedLast,const Chart& chart,CurveUse& result,std::string& reason)
{
    if(!BRep_Tool::SameParameter(edge)||!BRep_Tool::SameRange(edge)) {
        reason="boundary-does-not-preserve-native-edge-pcurve-parameter";return false;
    }
    double first=0.,last=0.;const auto curve=BRep_Tool::CurveOnSurface(edge,face,first,last);
    if(curve.IsNull()) {reason="physical-boundary-has-no-face-pcurve";return false;}
    const double tolerance=ParamTolerance(first,last);
    if(!std::isfinite(requestedFirst)||!std::isfinite(requestedLast)
        ||requestedFirst<first-tolerance||requestedLast>last+tolerance||requestedFirst>=requestedLast) {
        reason="native-source-or-target-range-is-outside-face-pcurve";return false;
    }
    result.EdgeId=edgeId;
    result.Curve=occ::down_cast<Geom2d_Curve>(curve->Transformed(chart.Transform));
    result.First=std::max(first,requestedFirst);result.Last=std::min(last,requestedLast);
    return true;
}

bool ConstantS(const occ::handle<Geom2d_Curve>& curve,double first,double last,double tolerance)
{
    Geom2dAdaptor_Curve adaptor(curve,first,last);const double s=curve->Value(first).X();
    if(adaptor.GetType()==GeomAbs_Line)
        return std::abs(adaptor.Line().Direction().X())<=1.e-13;
    if(adaptor.GetType()==GeomAbs_BSplineCurve) {
        const auto spline=adaptor.BSpline();
        for(int i=1;i<=spline->NbPoles();++i)if(std::abs(spline->Pole(i).X()-s)>tolerance)return false;
        return true;
    }
    if(adaptor.GetType()==GeomAbs_BezierCurve) {
        const auto bezier=adaptor.Bezier();
        for(int i=1;i<=bezier->NbPoles();++i)if(std::abs(bezier->Pole(i).X()-s)>tolerance)return false;
        return true;
    }
    return false;
}

// Positive rational Bernstein basis functions preserve the order of scalar
// control points. For i < j, B_i B'_j - B'_i B_j is positive inside (0,1),
// so the paired terms of N'W-NW' have the sign of S_j-S_i. Nonconstant ordered
// poles therefore prove strict interior monotonicity, including an endpoint
// with a zero derivative. This is a sufficient certificate, never a tolerance
// that discards a short branch returned by a numerical extrema algorithm.
bool StrictBezierS(const occ::handle<Geom2d_BezierCurve>& curve,int& direction)
{
    direction=0;
    for(int i=1;i<=curve->NbPoles();++i) {
        const double weight=curve->Weight(i),s=curve->Pole(i).X();
        if(!std::isfinite(weight)||weight<=0.||!std::isfinite(s))return false;
        if(i==1)continue;
        const double previous=curve->Pole(i-1).X();
        const int step=s>previous?1:(s<previous?-1:0);
        if(step&&direction&&step!=direction)return false;
        if(step)direction=step;
    }
    return direction!=0;
}

bool CertifiedMonotoneS(const CurveUse& use)
{
    try {
        Geom2dAdaptor_Curve adaptor(use.Curve,use.First,use.Last);
        if(adaptor.GetType()==GeomAbs_BezierCurve) {
            const auto trimmed=occ::down_cast<Geom2d_BezierCurve>(adaptor.Bezier()->Copy());
            trimmed->Segment(use.First,use.Last);int direction=0;
            return StrictBezierS(trimmed,direction);
        }
        if(adaptor.GetType()!=GeomAbs_BSplineCurve)return false;
        const auto spline=adaptor.BSpline();
        // A periodic control polygon has a cyclic closing segment, so the
        // stored linear order alone cannot certify its whole requested range.
        if(spline->IsPeriodic())return false;
        const auto trimmed=occ::down_cast<Geom2d_BSplineCurve>(spline->Copy());
        trimmed->Segment(use.First,use.Last,0.);
        if(trimmed->FirstParameter()!=use.First||trimmed->LastParameter()!=use.Last)return false;
        Geom2dConvert_BSplineCurveToBezierCurve extraction(trimmed);
        if(!extraction.NbArcs())return false;
        int direction=0;
        for(int i=1;i<=extraction.NbArcs();++i) {
            int spanDirection=0;
            // A constant knot span is a real platform. A reversal is a real
            // possible extremum. Neither may be hidden by this certificate.
            if(!StrictBezierS(extraction.Arc(i),spanDirection)
                ||(direction&&spanDirection!=direction))return false;
            direction=spanDirection;
        }
        return true;
    } catch(const Standard_Failure&) {return false;}
    catch(const std::exception&) {return false;}
}

#include "MachiningFaceCutCoverageProjection.inc"
#include "MachiningFaceCutCoverageSplineBounds.inc"
#include "MachiningFaceCutCoverageRootCount.inc"
#include "MachiningFaceCutCoverageSourceWindow.inc"
#include "MachiningFaceCutCoverageTargetWindow.inc"
#include "MachiningFaceCutCoverageInverseRefinement.inc"

// Events are native curve extrema, not points on a prescribed sampling grid.
// A branch between successive events has a single-valued inverse s -> u.
bool Branches(const CurveUse& use,const Chart& chart,std::vector<Branch>& result,std::string& reason,
    std::vector<ProjectionUncertainty>* uncertainties=nullptr)
{
    std::vector<double> events{use.First,use.Last};
    const bool constant=ConstantS(use.Curve,use.First,use.Last,chart.STolerance);
    Geom2dAdaptor_Curve adaptor(use.Curve,use.First,use.Last);
    bool analyticEvents=false;
    if(!constant&&adaptor.GetType()!=GeomAbs_Line&&!CertifiedMonotoneS(use)) {
        const auto analytic=AppendNativeConicProjectionEvents(use,events,reason);
        if(analytic==NativeProjectionEventStatus::Invalid)return false;
        analyticEvents=analytic==NativeProjectionEventStatus::Complete;
        if(!analyticEvents) {
        const occ::handle<Geom2d_Line> axis=new Geom2d_Line(gp_Pnt2d(0.,0.),gp_Dir2d(0.,1.));
        Geom2dAPI_ExtremaCurveCurve extrema(use.Curve,axis,use.First,use.Last,
            -Precision::Infinite(),Precision::Infinite());
        if(!extrema.Extrema().IsDone()) {reason="native-boundary-projection-extrema-not-solved";return false;}
        if(extrema.Extrema().IsParallel()) {reason="nonconstant-boundary-projection-has-continuous-extrema";return false;}
        for(int i=1;i<=extrema.NbExtrema();++i) {
            double u=0.,v=0.;extrema.Parameters(i,u,v);
            const double tolerance=ParamTolerance(use.First,use.Last);
            if(u>use.First+tolerance&&u<use.Last-tolerance)events.push_back(u);
        }
        }
    }
    std::sort(events.begin(),events.end());
    const double tolerance=ParamTolerance(use.First,use.Last);
    events.erase(std::unique(events.begin(),events.end(),[&](double a,double b){return analyticEvents?a==b:std::abs(a-b)<=tolerance;}),events.end());
    const auto retainUncertainty=[&](const Branch& branch,const std::string& diagnostic) {
        if(!uncertainties)return false;
        double lower=0.,upper=0.;std::string boundReason;
        if(!BoundNativeConicProjection(use,branch.First,branch.Last,lower,upper,boundReason)
            &&!BoundNativeSplineProjection(use,branch.First,branch.Last,lower,upper,boundReason))return false;
        // The whole unchanged native branch is enclosed, including endpoint
        // evaluation roundoff. No curve or source parameter is moved.
        lower=std::min({lower,branch.SFirst,branch.SLast});upper=std::max({upper,branch.SFirst,branch.SLast});
        if(!std::isfinite(lower)||!std::isfinite(upper)||!(upper>lower))return false;
        uncertainties->push_back({use.EdgeId,branch.First,branch.Last,lower,upper,
            diagnostic+";projection-bound-proof="+boundReason});
        return true;
    };
    for(std::size_t i=1;i<events.size();++i) {
        Branch branch;branch.EdgeId=use.EdgeId;branch.Curve=use.Curve;
        branch.First=events[i-1];branch.Last=events[i];
        branch.SFirst=use.Curve->Value(branch.First).X();branch.SLast=use.Curve->Value(branch.Last).X();
        branch.bConstant=constant;
        if(!constant&&std::abs(branch.SFirst-branch.SLast)<=ParamTolerance(branch.SFirst,branch.SLast)) {
            std::ostringstream detail;detail<<std::setprecision(17)
                <<"boundary-projection-branch-not-certifiably-monotone;edge="<<use.EdgeId<<";native=["<<branch.First<<","<<branch.Last
                <<"];original-s=["<<use.Curve->Value(branch.First).X()<<","<<use.Curve->Value(branch.Last).X()
                <<"];projection-s=["<<branch.SFirst<<","<<branch.SLast<<"];coordinates=original-native-pcurve";
            if(retainUncertainty(branch,detail.str()))continue;
            reason=detail.str();return false;
        }
        result.push_back(std::move(branch));
    }
    return true;
}

// Native curve/line intersection retains the original edge parameter. No
// endpoint interpolation is used for a curved physical destination.
bool ParameterAt(const Branch& branch,double s,double sTolerance,double& parameter,std::string* diagnostic=nullptr,
    NativeProjectionInverseFailureWindow* failureWindow=nullptr)
{
    if(diagnostic)diagnostic->clear();
    if(failureWindow)*failureWindow={};
    if(s==branch.SFirst){parameter=branch.First;return true;}
    if(s==branch.SLast){parameter=branch.Last;return true;}
    std::string nativeReason;
    const auto analytic=SolveNativeConicProjectionParameter(branch,s,parameter,nativeReason);
    if(analytic!=NativeProjectionEventStatus::Unsupported) {
        if(analytic!=NativeProjectionEventStatus::Complete&&failureWindow
            &&(nativeReason=="native-conic-projection-inverse-has-no-root-in-original-branch"
                ||nativeReason=="native-conic-projection-inverse-has-no-real-native-root"))
            BuildOriginalTargetEndpointInverseFailureWindow(branch,s,nativeReason,*failureWindow);
        if(diagnostic)*diagnostic=nativeReason;
        return analytic==NativeProjectionEventStatus::Complete;
    }
    const occ::handle<Geom2d_TrimmedCurve> trimmed=new Geom2d_TrimmedCurve(branch.Curve,branch.First,branch.Last,true,false);
    const occ::handle<Geom2d_Line> ray=new Geom2d_Line(gp_Pnt2d(s,0.),gp_Dir2d(0.,1.));
    Geom2dAPI_InterCurveCurve intersection(trimmed,ray,std::max(1.e-12,sTolerance*.1));
    std::vector<double> candidates;
    for(int i=1;i<=intersection.NbPoints();++i) {
        const double u=intersection.Intersector().Point(i).ParamOnFirst();
        if(!std::isfinite(u)||u<branch.First||u>branch.Last)continue;
        candidates.push_back(u);
    }
    std::sort(candidates.begin(),candidates.end());
    // Exact duplicates are the same reported value. Positive-separated
    // native parameters are never merged by a position/parameter tolerance.
    candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());
    if(candidates.empty()) {
        if(diagnostic)*diagnostic="kernel-native-projection-has-no-root";return false;
    }
    if(candidates.size()==1){parameter=candidates.front();return true;}

    // A kernel can report two approximations of ONE mathematical root near
    // a spline endpoint. An independent uniqueness proof on the ORIGINAL
    // positive-weight control data is required: either strict monotonicity,
    // or a request-specific root-count certificate. Genuine multiple roots,
    // platforms, nonpositive weights and out-of-domain events remain failures.
    CurveUse original;original.EdgeId=branch.EdgeId;original.Curve=branch.Curve;
    original.First=branch.First;original.Last=branch.Last;
    int direction=0;std::string proofReason;
    const bool strictlyMonotone=CertifyStrictOriginalPolynomialSplineProjection(original,direction,proofReason)
        ||CertifyStrictOriginalPositiveRationalSplineProjection(original,direction,proofReason);
    const bool uniqueRoot=(strictlyMonotone&&((direction>0)==(branch.SLast>branch.SFirst)))
        ||CertifyUniqueOriginalNativeSplineProjectionRoot(original,s,proofReason);
    if(!(s>std::min(branch.SFirst,branch.SLast)&&s<std::max(branch.SFirst,branch.SLast))
        ||!uniqueRoot) {
        if(failureWindow&&proofReason=="native-spline-root-count-original-endpoint-intervals-do-not-prove-strict-opposite-signs")
            BuildOriginalTargetEndpointInverseFailureWindow(branch,s,proofReason,*failureWindow);
        if(diagnostic)*diagnostic="kernel-native-projection-has-multiple-roots";return false;
    }
    bool certified=false;double best=0.,bestResidual=std::numeric_limits<double>::infinity();
    for(const auto candidate:candidates) {
        double lower=0.,upper=0.;std::string pointReason;
        if(!BoundNativeSplineProjectionPoint(original,candidate,lower,upper,pointReason)
            ||s<lower||s>upper)continue;
        const double actual=branch.Curve->Value(candidate).X(),residual=std::abs(actual-s);
        if(!std::isfinite(actual)||!std::isfinite(residual))continue;
        if(!certified||residual<bestResidual){best=candidate;bestResidual=residual;certified=true;}
    }
    if(!certified) {
        // The original-domain candidate list is nonempty and mathematical
        // uniqueness was proved above. A coarse kernel approximation may
        // still miss the rigorous point enclosure. Refine that SAME native
        // inverse inside a strict interval-sign bracket, never by snapping
        // an endpoint or importing a root from outside this original branch.
        std::string refinementReason;
        if(RefineUniqueOriginalNativeSplineProjectionRoot(original,s,candidates,best,refinementReason,failureWindow)) {
            parameter=best;
            if(diagnostic)*diagnostic=refinementReason;
            return true;
        }
        if(diagnostic)*diagnostic="kernel-unique-spline-root-candidates-do-not-enclose-requested-native-coordinate;refinement-status="+refinementReason;
        return false;
    }
    parameter=best;
    if(diagnostic)*diagnostic="kernel-spline-candidate-selected-by-original-unique-root-certificate-and-outward-point-bound";
    return true;
}

bool BuildChart(const TopoDS_Face& face,const O::SOpenCascadeBRepBuildResult& built,
    const SMachiningPathEdgeAnalysis& path,const SMachiningFaceCutCoverageOptions& options,
    Chart& chart,std::string& reason)
{
    if(path.Samples.size()!=2||!path.Samples[0].bToolAxisValid||!path.Samples[1].bToolAxisValid) {
        reason="source-has-no-two-recorded-ready-tool-axes";return false;
    }
    const auto& sample=path.Samples.front();
    const auto edgeIt=built.Edges.find(sample.SourceEdgeId);
    if(edgeIt==built.Edges.end()) {reason="source-endpoint-native-edge-missing";return false;}
    double first=0.,last=0.;const auto pcurve=BRep_Tool::CurveOnSurface(edgeIt->second,face,first,last);
    if(pcurve.IsNull()||!BRep_Tool::SameParameter(edgeIt->second)) {
        reason="source-endpoint-has-no-native-parameter-pcurve";return false;
    }
    const auto uv=pcurve->Value(sample.Parameter);
    BRepAdaptor_Surface surface(face,true);gp_Pnt point;gp_Vec du,dv;
    surface.D1(uv.X(),uv.Y(),point,du,dv);
    auto axis=Vec(sample.ToolAxis);auto endAxis=Vec(path.Samples.back().ToolAxis);
    if(axis.SquareMagnitude()<=1.e-24||endAxis.SquareMagnitude()<=1.e-24) {
        reason="source-recorded-tool-axis-is-zero";return false;
    }
    axis.Normalize();endAxis.Normalize();
    if(path.AxisMode==EMachiningPathAxisMode::PlaneTangentCrossNormal) {
        if(surface.GetType()!=GeomAbs_Plane) {reason="geometrically-planar-nonplane-parameterization-pending";return false;}
        if(path.SourceUses.empty()) {reason="source-has-no-native-uses";return false;}
        const auto plane=surface.Plane();
        auto tangent=Vec(sample.Tangent);
        if(!sample.bTangentValid||tangent.SquareMagnitude()<=1.e-24) {reason="source-has-no-directed-line-tangent";return false;}
        tangent.Normalize();
        const gp_Pnt lineOrigin(sample.Position.X,sample.Position.Y,sample.Position.Z);
        for(const auto& use:path.SourceUses) {
            const auto source=built.Edges.find(use.EdgeId);
            if(source==built.Edges.end()) {reason="source-native-edge-missing";return false;}
            BRepAdaptor_Curve curve(source->second);
            if(curve.GetType()!=GeomAbs_Line) {reason="plane-curved-driver-normal-family-pending";return false;}
            const auto line=curve.Line();
            if(gp_Vec(line.Direction()).Crossed(tangent).Magnitude()>1.e-10
                ||line.Distance(lineOrigin)>options.dPositionTolerance) {
                reason="logical-plane-source-not-one-certified-straight-line";return false;
            }
        }
        if(axis.Dot(endAxis)<=0.||axis.Crossed(endAxis).Magnitude()>1.e-10) {
            reason="certified-line-source-has-inconsistent-recorded-axis-senses";return false;
        }
        const auto x=gp_Vec(plane.Position().XDirection());const auto y=gp_Vec(plane.Position().YDirection());
        gp_Vec2d s(tangent.Dot(x),tangent.Dot(y)),h(axis.Dot(x),axis.Dot(y));
        if(s.SquareMagnitude()<=1.e-24||h.SquareMagnitude()<=1.e-24) {reason="plane-axis-chart-is-singular";return false;}
        s.Normalize();h.Normalize();
        if(std::abs(s.Dot(h))>1.e-10) {reason="recorded-plane-axis-not-perpendicular-to-driver";return false;}
        // Keep an exactly orthogonal chart while retaining the recorded axis sense.
        h=gp_Vec2d(-s.Y(),s.X());if(h.X()*axis.Dot(x)+h.Y()*axis.Dot(y)<0.)h.Reverse();
        chart.Transform.SetValues(s.X(),s.Y(),-s.X()*uv.X()-s.Y()*uv.Y(),
            h.X(),h.Y(),-h.X()*uv.X()-h.Y()*uv.Y());
        chart.bPlane=true;chart.STolerance=options.dPositionTolerance;chart.HTolerance=options.dPositionTolerance;
        chart.Method="plane-line-first-physical-boundary";return true;
    }
    if(path.AxisMode!=EMachiningPathAxisMode::StraightGenerator) {reason="source-surface-has-no-recorded-straight-generator";return false;}
    int derivative=0;
    switch(surface.GetType()) {
    case GeomAbs_Cylinder:case GeomAbs_Cone:case GeomAbs_SurfaceOfExtrusion:derivative=2;break;
    case GeomAbs_SurfaceOfRevolution:
        if(surface.BasisCurve()->GetType()==GeomAbs_Line)derivative=2;break;
    case GeomAbs_BSplineSurface: {
        const auto spline=surface.BSpline();
        const bool u=spline->UDegree()==1&&spline->NbUPoles()==2&&!spline->IsUPeriodic();
        const bool v=spline->VDegree()==1&&spline->NbVPoles()==2&&!spline->IsVPeriodic();
        if(u!=v)derivative=u?1:2;break;
    }
    case GeomAbs_BezierSurface: {
        const auto bezier=surface.Bezier();
        const bool u=bezier->UDegree()==1&&bezier->NbUPoles()==2;
        const bool v=bezier->VDegree()==1&&bezier->NbVPoles()==2;
        if(u!=v)derivative=u?1:2;break;
    }
    default:break;
    }
    if(!derivative) {reason="native-support-has-no-certified-recorded-generator-family";return false;}
    const auto generator=derivative==1?du:dv;
    if(generator.SquareMagnitude()<=1.e-24) {reason="source-start-at-generator-singularity";return false;}
    const double alignment=axis.Dot(generator)/generator.Magnitude();
    if(std::abs(alignment)<std::cos(options.dAngularToleranceRadians)) {reason="recorded-axis-not-native-generator";return false;}
    chart.GeneratorDerivative=derivative;chart.Direction=alignment>=0.?1.:-1.;
    if(derivative==1)chart.Transform.SetValues(0.,1.,0.,1.,0.,0.);
    chart.STolerance=std::max(1.e-12,derivative==1?surface.VResolution(options.dPositionTolerance):surface.UResolution(options.dPositionTolerance));
    chart.HTolerance=std::max(1.e-12,derivative==1?surface.UResolution(options.dPositionTolerance):surface.VResolution(options.dPositionTolerance));
    chart.bPeriodic=derivative==1?surface.IsVPeriodic():surface.IsUPeriodic();
    if(chart.bPeriodic)chart.Period=derivative==1?surface.VPeriod():surface.UPeriod();
    if(surface.GetType()==GeomAbs_Cone) {
        const auto cone=surface.Cone();const double sine=std::sin(cone.SemiAngle());
        if(std::abs(sine)>1.e-15) {
            chart.bHasGeneratorSingularity=true;chart.GeneratorSingularityH=-cone.RefRadius()/sine;
        }
    }
    chart.Method="straight-generator-uv-first-physical-boundary";
    // Endpoint directions are checked only after the native support has proved
    // the generator family. Source monotonicity is proved separately below.
    const auto& end=path.Samples.back();const auto endIt=built.Edges.find(end.SourceEdgeId);
    if(endIt==built.Edges.end()) {reason="source-end-native-edge-missing";return false;}
    double ef=0.,el=0.;const auto ec=BRep_Tool::CurveOnSurface(endIt->second,face,ef,el);
    if(ec.IsNull()||!BRep_Tool::SameParameter(endIt->second)) {reason="source-end-has-no-native-parameter-pcurve";return false;}
    const auto euv=ec->Value(end.Parameter);surface.D1(euv.X(),euv.Y(),point,du,dv);
    const auto eg=derivative==1?du:dv;
    if(eg.SquareMagnitude()<=1.e-24
        ||chart.Direction*endAxis.Dot(eg)/eg.Magnitude()<std::cos(options.dAngularToleranceRadians)) {
        reason="recorded-generator-endpoint-senses-disagree-or-are-singular";return false;
    }
    return true;
}

bool AppendArrival(const Branch& source,const Branch& target,double sFirst,double sLast,
    const Chart& chart,const SMachiningPathEdgeAnalysis& path,
    const std::vector<const SMachiningEdgeAnalysis*>& attributes,
    std::vector<SMachiningFaceCutArrival>& arrivals,std::string& reason,
    bool& ignoredZeroMeasureNativeContact,SourceEndpointInverseFailure* sourceFailure=nullptr,
    TargetEndpointInverseFailure* targetFailure=nullptr)
{
    if(sourceFailure)*sourceFailure={};
    if(targetFailure)*targetFailure={};
    double first=0.,last=0.,sourceAtFirst=0.,sourceAtLast=0.;
    std::string firstDiagnostic,lastDiagnostic;
    NativeProjectionInverseFailureWindow firstWindow,lastWindow;
    const bool firstSolved=ParameterAt(target,sFirst,chart.STolerance,first,&firstDiagnostic,targetFailure?&firstWindow:nullptr);
    const bool lastSolved=ParameterAt(target,sLast,chart.STolerance,last,&lastDiagnostic,targetFailure?&lastWindow:nullptr);
    if(!firstSolved||!lastSolved) {
        if(targetFailure) {
            targetFailure->First=!firstSolved;targetFailure->Last=!lastSolved;
            targetFailure->FirstWindow=std::move(firstWindow);targetFailure->LastWindow=std::move(lastWindow);
        }
        std::ostringstream detail;detail<<std::setprecision(17)
            <<"target-native-event-parameter-not-solved;source-edge="<<source.EdgeId<<";target-edge="<<target.EdgeId
            <<";source-s=["<<sFirst<<","<<sLast<<"];target-branch-native=["<<target.First<<","<<target.Last
            <<"];target-branch-s=["<<target.SFirst<<","<<target.SLast<<"];first-solved="<<firstSolved
            <<";last-solved="<<lastSolved<<";first-reason="<<firstDiagnostic<<";last-reason="<<lastDiagnostic;
        CurveUse native;native.EdgeId=target.EdgeId;native.Curve=target.Curve;native.First=target.First;native.Last=target.Last;
        native_conic_projection::Data data;std::string dataReason;
        if(native_conic_projection::Read(native,data,dataReason)==NativeProjectionEventStatus::Complete)
            detail<<";native-line="<<data.bLine<<";conic-CAB=["<<data.C<<","<<data.A<<","<<data.B<<"]";
        reason=detail.str();return false;
    }
    std::string sourceFirstDiagnostic,sourceLastDiagnostic;
    const bool sourceFirstSolved=ParameterAt(source,sFirst,chart.STolerance,sourceAtFirst,&sourceFirstDiagnostic);
    const bool sourceLastSolved=ParameterAt(source,sLast,chart.STolerance,sourceAtLast,&sourceLastDiagnostic);
    if(!sourceFirstSolved||!sourceLastSolved) {
        if(sourceFailure){sourceFailure->First=!sourceFirstSolved;sourceFailure->Last=!sourceLastSolved;}
        std::ostringstream detail;detail<<std::setprecision(17)
            <<"source-native-event-parameter-not-solved;source-edge="<<source.EdgeId<<";target-edge="<<target.EdgeId
            <<";source-native=["<<source.First<<","<<source.Last<<"];source-s=["<<sFirst<<","<<sLast
            <<"];first-solved="<<sourceFirstSolved<<";last-solved="<<sourceLastSolved
            <<";first-reason="<<sourceFirstDiagnostic<<";last-reason="<<sourceLastDiagnostic;
        reason=detail.str();return false;
    }
    double targetFirstS=sFirst,targetLastS=sLast;
    if(first>last){std::swap(first,last);std::swap(sourceAtFirst,sourceAtLast);std::swap(targetFirstS,targetLastS);}
    if(last-first<=ParamTolerance(first,last))return true;
    std::vector<std::pair<double,double>> covered;
    for(const auto* attr:attributes) {
        if(attr->EdgeId!=target.EdgeId||!HasFace(*attr,path.FaceId))continue;
        const double a=std::max(first,attr->FirstParameter),b=std::min(last,attr->LastParameter);
        if(b-a<=ParamTolerance(a,b))continue;
        const double attributeFirstS=a==first?targetFirstS:target.Curve->Value(a).X();
        const double attributeLastS=b==last?targetLastS:target.Curve->Value(b).X();
        // Test in the chart BEFORE inversion. Every interior attribute event
        // belongs to the same unchanged native curve and must remain inside
        // this projection cell. This also detects an unproven target fold.
        const double chartFirst=std::min(sFirst,sLast),chartLast=std::max(sFirst,sLast);
        if(!std::isfinite(attributeFirstS)||!std::isfinite(attributeLastS)
            ||attributeFirstS<chartFirst||attributeFirstS>chartLast
            ||attributeLastS<chartFirst||attributeLastS>chartLast
            ||(targetLastS-targetFirstS)*(attributeLastS-attributeFirstS)<0.) {
            std::ostringstream detail;detail<<std::setprecision(17)
                <<"native-target-attribute-event-crosses-or-reverses-the-source-event-cell;guard=chart"
                <<";chart-cell=["<<chartFirst<<","<<chartLast<<"];attribute-s=["<<attributeFirstS<<","<<attributeLastS
                <<"];target-native=["<<a<<","<<b<<"];canonical-vertex-events="<<0;
            detail<<";source-edge="<<source.EdgeId<<";target-edge="<<target.EdgeId
                <<";target-branch-native=["<<target.First<<","<<target.Last<<"];target-branch-s=["<<target.SFirst<<","<<target.SLast
                <<"];target-root-native=["<<first<<","<<last<<"];target-root-s=["<<target.Curve->Value(first).X()<<","<<target.Curve->Value(last).X()<<"]";
            reason=detail.str();return false;
        }
        SMachiningFaceCutArrival arrival;arrival.FaceId=path.FaceId;arrival.SourcePathKey=path.Key;
        arrival.SourceEdgeId=source.EdgeId;arrival.TargetEdgeId=target.EdgeId;
        arrival.TargetFirstParameter=a;arrival.TargetLastParameter=b;arrival.TargetSpanKey=attr->Key;
        arrival.TargetSkinRelation=attr->SkinRelation;arrival.TargetConvexity=attr->Convexity;
        arrival.bTargetSkinRelationValid=attr->bSkinRelationValid;arrival.bTargetConvexityValid=attr->bConvexityValid;
        // A chart event is one original source event even when adjacent
        // destinations are different edges. Do not round-trip that event via
        // each target curve: the two evaluations can differ by one ULP and
        // manufacture a false source-domain gap. Internal attribute events
        // still use the unchanged target's native parameter correspondence.
        if(a==first)arrival.SourceFirstParameter=sourceAtFirst;
        else if(!ParameterAt(source,attributeFirstS,chart.STolerance,arrival.SourceFirstParameter)) {
            reason="source-native-parameter-at-target-attribute-event-not-solved";return false;
        }
        if(b==last)arrival.SourceLastParameter=sourceAtLast;
        else if(!ParameterAt(source,attributeLastS,chart.STolerance,arrival.SourceLastParameter)) {
            reason="source-native-parameter-at-target-attribute-event-not-solved";return false;
        }
        // An interior attribute event must also retain its original native
        // source-cell order. It cannot cross a cell boundary or reverse the
        // correspondence and thereby manufacture positive coverage.
        const double cellFirst=std::min(sourceAtFirst,sourceAtLast),cellLast=std::max(sourceAtFirst,sourceAtLast);
        const double mappedFirst=arrival.SourceFirstParameter,mappedLast=arrival.SourceLastParameter;
        if(mappedFirst<cellFirst||mappedFirst>cellLast||mappedLast<cellFirst||mappedLast>cellLast
            ||(sourceAtLast-sourceAtFirst)*(mappedLast-mappedFirst)<0.) {
            std::ostringstream detail;detail<<std::setprecision(17)
                <<"native-target-attribute-event-crosses-or-reverses-the-source-event-cell;guard=native"
                <<";source-cell=["<<sourceAtFirst<<","<<sourceAtLast<<"];source-mapped=["<<mappedFirst<<","<<mappedLast
                <<"];chart-cell=["<<chartFirst<<","<<chartLast<<"];attribute-s=["<<attributeFirstS<<","<<attributeLastS
                <<"];canonical-vertex-events="<<0;
            detail<<";source-edge="<<source.EdgeId<<";target-edge="<<target.EdgeId
                <<";target-branch-native=["<<target.First<<","<<target.Last<<"];target-branch-s=["<<target.SFirst<<","<<target.SLast
                <<"];target-root-native=["<<first<<","<<last<<"];target-root-s=["<<target.Curve->Value(first).X()<<","<<target.Curve->Value(last).X()<<"]";
            reason=detail.str();return false;
        }
        if(arrival.SourceFirstParameter==arrival.SourceLastParameter) {
            // This inverse correspondence has no source measure and must not
            // become an arrival or cut-through witness. Keep the target-attr
            // bookkeeping only; exact native source-union checks below still
            // expose every positive-length proof gap without a tolerance merge.
            ignoredZeroMeasureNativeContact=true;covered.emplace_back(a,b);continue;
        }
        arrival.Method=chart.Method;arrival.Reason="first-positive-physical-boundary-on-current-finite-face";
        arrivals.push_back(std::move(arrival));covered.emplace_back(a,b);
    }
    std::sort(covered.begin(),covered.end());double end=first;
    for(const auto& interval:covered) {
        if(interval.first>end+ParamTolerance(first,last))break;
        end=std::max(end,interval.second);
    }
    if(end<last-ParamTolerance(first,last)) {reason="target-native-interval-has-no-complete-edge-analysis-attributes";return false;}
    return true;
}

bool NativeSourceCoverage(const SMachiningPathEdgeAnalysis& path,
    const std::vector<SMachiningFaceCutArrival>& arrivals,std::string& reason)
{
    bool complete=true;
    for(const auto& use:path.SourceUses) {
        const double first=std::min(use.FirstParameter,use.LastParameter),last=std::max(use.FirstParameter,use.LastParameter);
        std::vector<std::pair<double,double>> intervals;
        for(const auto& arrival:arrivals) {
            if(arrival.FaceId!=path.FaceId||arrival.SourcePathKey!=path.Key||arrival.SourceEdgeId!=use.EdgeId)continue;
            const double a=std::max(first,std::min(arrival.SourceFirstParameter,arrival.SourceLastParameter));
            const double b=std::min(last,std::max(arrival.SourceFirstParameter,arrival.SourceLastParameter));
            if(b>a)intervals.emplace_back(a,b);
        }
        std::sort(intervals.begin(),intervals.end());double end=first;
        // Completeness is an exact union of original native parameters.
        // A positive gap remains unfinished even when it is smaller than a
        // solver's event tolerance; that tolerance cannot invent evidence.
        for(const auto& interval:intervals) {if(interval.first>end)break;end=std::max(end,interval.second);}
        if(end<last) {
            const std::string missing="source-native-interval-not-fully-mapped-to-physical-boundaries";
            if(reason.empty())reason=missing;
            else if(reason.find(missing)==std::string::npos)reason+=";"+missing;
            complete=false;
        }
    }
    return complete;
}

#include "MachiningFaceCutCoverageCircle.inc"

bool MapAffineSource(const TopoDS_Face& face,const O::SOpenCascadeBRepBuildResult& built,
    const SMachiningFaceEdgeAnalysis& boundary,const SMachiningPathEdgeAnalysis& path,
    const std::vector<const SMachiningEdgeAnalysis*>& attributes,
    const SMachiningFaceCutCoverageOptions& options,
    std::vector<SMachiningFaceCutArrival>& arrivals,std::string& method,std::string& reason,
    bool& ignoredZeroMeasureNativeContact)
{
    if(!BRepCheck_Analyzer(face,true).IsValid()) {
        reason="affine-source-native-face-is-invalid";return false;
    }
    Chart chart;if(!BuildChart(face,built,path,options,chart,reason))return false;method=chart.Method;
    std::vector<Branch> targets;std::vector<ProjectionUncertainty> uncertainTargets;
    std::vector<CurveUse> targetCurves;std::set<std::uint64_t> seen;
    for(const auto& wire:boundary.Wires) {
        if(!wire.bOrdered) {reason="face-has-unordered-physical-boundary-wire";return false;}
        for(const auto& group:wire.Groups)for(const auto& use:group.SourceUses) {
            if(!seen.insert(use.EdgeId).second)continue;
            const auto target=built.Edges.find(use.EdgeId);
            if(target==built.Edges.end()) {reason="physical-target-native-edge-missing";return false;}
            if(BRep_Tool::Degenerated(target->second)||BRep_Tool::IsClosed(target->second,face))continue;
            double first=0.,last=0.;BRep_Tool::Range(target->second,first,last);
            CurveUse curve;if(!ReadCurve(face,target->second,use.EdgeId,first,last,chart,curve,reason))return false;
            targetCurves.push_back(std::move(curve));
        }
    }
    // Projection events always come from the unchanged native pcurve.
    for(const auto& curve:targetCurves)if(!Branches(curve,chart,targets,reason,&uncertainTargets))return false;
    if(targets.empty()&&uncertainTargets.empty()) {reason="face-has-no-real-physical-target-boundaries";return false;}
    std::vector<Branch> sources;
    double previousS=0.;bool havePrevious=false;int sourceSense=0;
    for(const auto& use:path.SourceUses) {
        const auto source=built.Edges.find(use.EdgeId);
        if(source==built.Edges.end()) {reason="source-native-edge-missing";return false;}
        CurveUse curve;if(!ReadCurve(face,source->second,use.EdgeId,use.FirstParameter,use.LastParameter,chart,curve,reason))return false;
        std::vector<Branch> parts;if(!Branches(curve,chart,parts,reason))return false;
        if(parts.size()!=1||parts.front().bConstant) {reason="source-interior-generator-parallel-or-changing-axis-sense-pending";return false;}
        auto branch=std::move(parts.front());
        double walkStart=use.bReversed?branch.SLast:branch.SFirst,walkEnd=use.bReversed?branch.SFirst:branch.SLast;
        if(chart.bPeriodic&&havePrevious) {
            const double shift=std::round((previousS-walkStart)/chart.Period)*chart.Period;
            if(shift!=0.) {
                branch.Curve=occ::down_cast<Geom2d_Curve>(branch.Curve->Translated(gp_Vec2d(shift,0.)));
                branch.SFirst=branch.Curve->Value(branch.First).X();branch.SLast=branch.Curve->Value(branch.Last).X();
                walkStart=use.bReversed?branch.SLast:branch.SFirst;walkEnd=use.bReversed?branch.SFirst:branch.SLast;
            }
        }
        if(havePrevious&&std::abs(previousS-walkStart)>chart.STolerance) {reason="logical-source-not-continuous-in-generator-chart";return false;}
        const int sense=walkEnd>walkStart?1:-1;
        if(sourceSense&&sense!=sourceSense) {reason="logical-source-reverses-non-generator-coordinate";return false;}
        sourceSense=sense;previousS=walkEnd;havePrevious=true;sources.push_back(std::move(branch));
    }
    if(sources.empty()) {reason="selected-source-has-no-native-range";return false;}
    bool complete=true;std::vector<std::string> sourceWindowNotes;
    for(const auto& source:sources) {
        const double lower=std::min(source.SFirst,source.SLast),upper=std::max(source.SFirst,source.SLast);
        std::vector<Branch> candidates;
        std::vector<ProjectionUncertainty> localUncertainties;
        std::vector<double> events{lower,upper};
        for(const auto& original:targets) {
            const double tlo=std::min(original.SFirst,original.SLast),thi=std::max(original.SFirst,original.SLast);
            long long kFirst=0,kLast=0;
            if(chart.bPeriodic) {
                kFirst=static_cast<long long>(std::ceil((lower-thi-chart.STolerance)/chart.Period));
                kLast=static_cast<long long>(std::floor((upper-tlo+chart.STolerance)/chart.Period));
                if(kLast-kFirst>16) {reason="periodic-face-has-excessive-unwrapped-boundary-multiplicity";return false;}
            }
            for(long long k=kFirst;k<=kLast;++k) {
                Branch branch=original;const double shift=chart.bPeriodic?static_cast<double>(k)*chart.Period:0.;
                if(shift!=0.) {
                    branch.Curve=occ::down_cast<Geom2d_Curve>(branch.Curve->Translated(gp_Vec2d(shift,0.)));
                    branch.SFirst=branch.Curve->Value(branch.First).X();branch.SLast=branch.Curve->Value(branch.Last).X();
                }
                const double a=std::min(branch.SFirst,branch.SLast),b=std::max(branch.SFirst,branch.SLast);
                if(b<lower||a>upper)continue;
                if(a>lower&&a<upper)events.push_back(a);
                if(b>lower&&b<upper)events.push_back(b);
                candidates.push_back(std::move(branch));
            }
        }
        for(const auto& original:uncertainTargets) {
            long long kFirst=0,kLast=0;
            if(chart.bPeriodic) {
                kFirst=static_cast<long long>(std::ceil((lower-original.Upper)/chart.Period));
                kLast=static_cast<long long>(std::floor((upper-original.Lower)/chart.Period));
                if(kLast-kFirst>16) {reason="periodic-face-has-excessive-unwrapped-boundary-multiplicity";return false;}
            }
            for(long long k=kFirst;k<=kLast;++k) {
                auto band=original;const double shift=chart.bPeriodic?static_cast<double>(k)*chart.Period:0.;
                band.Lower+=shift;band.Upper+=shift;
                if(band.Upper<=lower||band.Lower>=upper)continue;
                if(band.Lower>lower&&band.Lower<upper)events.push_back(band.Lower);
                if(band.Upper>lower&&band.Upper<upper)events.push_back(band.Upper);
                localUncertainties.push_back(std::move(band));
            }
        }
        std::sort(events.begin(),events.end());
        const double eventTolerance=ParamTolerance(lower,upper);
        // A local unresolved target band must never be absorbed into a
        // neighboring event by a parameter tolerance. Its positive native
        // preimage remains visible as Pending in the final exact union.
        events.erase(std::unique(events.begin(),events.end()),events.end());
        for(std::size_t i=1;i<events.size();++i) {
            const double a=events[i-1],b=events[i];if(!(b>a))continue;
            const auto uncertain=std::find_if(localUncertainties.begin(),localUncertainties.end(),[&](const auto& band) {
                return a<band.Upper&&b>band.Lower;
            });
            if(uncertain!=localUncertainties.end()) {
                std::ostringstream detail;detail<<std::setprecision(17)
                    <<"target-native-projection-uncertainty-is-local;target-edge="<<uncertain->EdgeId
                    <<";target-native=["<<uncertain->First<<","<<uncertain->Last
                    <<"];projection-band=["<<uncertain->Lower<<","<<uncertain->Upper
                    <<"];source-s=["<<a<<","<<b<<"];"<<uncertain->Reason;
                reason=detail.str();complete=false;continue;
            }
            if(b-a<=eventTolerance) {
                reason="positive-source-projection-cell-below-native-event-resolution";complete=false;continue;
            }
            const double middle=a+(b-a)*.5;double sourceParameter=0.;
            if(!ParameterAt(source,middle,chart.STolerance,sourceParameter)) {reason="source-native-event-parameter-not-solved";complete=false;continue;}
            const double sourceH=source.Curve->Value(sourceParameter).Y();
            const Branch* winner=nullptr;double nearest=std::numeric_limits<double>::infinity();bool ambiguous=false;
            for(const auto& candidate:candidates) {
                if(candidate.bConstant)continue;
                if(middle<std::min(candidate.SFirst,candidate.SLast)-eventTolerance
                    ||middle>std::max(candidate.SFirst,candidate.SLast)+eventTolerance)continue;
                double parameter=0.;if(!ParameterAt(candidate,middle,chart.STolerance,parameter)) {
                    reason="physical-target-ray-intersection-not-solved";complete=false;ambiguous=true;break;
                }
                const double distance=chart.Direction*(candidate.Curve->Value(parameter).Y()-sourceH);
                if(distance<=chart.HTolerance)continue;
                if(distance<nearest-chart.HTolerance) {nearest=distance;winner=&candidate;ambiguous=false;}
                else if(std::abs(distance-nearest)<=chart.HTolerance&&winner&&winner->EdgeId!=candidate.EdgeId)ambiguous=true;
            }
            if(!winner||ambiguous) {
                reason=ambiguous?"first-physical-boundary-is-geometrically-ambiguous":"no-positive-physical-boundary-before-face-generator-end";
                complete=false;continue;
            }
            if(chart.bHasGeneratorSingularity) {
                const double singularDistance=chart.Direction*(chart.GeneratorSingularityH-sourceH);
                if(singularDistance>=-chart.HTolerance&&singularDistance<=nearest+chart.HTolerance) {
                    reason="cone-apex-reached-before-a-physical-edge-interval";complete=false;continue;
                }
            }
            SourceEndpointInverseFailure sourceFailure;
            TargetEndpointInverseFailure targetFailure;
            if(!AppendArrival(source,*winner,a,b,chart,path,attributes,arrivals,reason,ignoredZeroMeasureNativeContact,&sourceFailure,&targetFailure)) {
                complete=false;
                Branch inner;double innerA=0.,innerB=0.;std::string windowReason;
                if(BuildTargetEndpointNativeWindowRetry(*winner,a,b,targetFailure,inner,innerA,innerB,windowReason)) {
                    // The original first-physical-boundary cell and its winner
                    // stay fixed. A retry is strictly inside that SAME cell;
                    // it cannot cross any existing physical-boundary event.
                    // Both excluded bands and gaps to the new known native
                    // target events receive no arrival and remain Pending.
                    const auto originalFailure=reason;
                    AppendArrival(source,inner,innerA,innerB,chart,path,attributes,arrivals,reason,ignoredZeroMeasureNativeContact);
                    sourceWindowNotes.push_back(windowReason);
                    if(reason.empty())reason=originalFailure;
                }
                else if(BuildSourceEndpointNativeWindowRetry(source,a,b,sourceFailure,inner,innerA,innerB,windowReason)) {
                    // Add a genuine original native-parameter event. The
                    // excluded one-ULP source window receives no arrival and
                    // remains explicit in the final exact source-domain union.
                    const auto originalFailure=reason;
                    AppendArrival(inner,*winner,innerA,innerB,chart,path,attributes,arrivals,reason,ignoredZeroMeasureNativeContact);
                    sourceWindowNotes.push_back(windowReason);
                    if(reason.empty())reason=originalFailure;
                }
                else if((targetFailure.First&&targetFailure.FirstWindow.bValid)
                    ||(targetFailure.Last&&targetFailure.LastWindow.bValid)) {
                    std::string orderReason;
                    if(!CertifyOriginalTargetProjectionOrder(*winner,orderReason))
                        sourceWindowNotes.push_back("target-native-local-window-retry-refused-without-original-projection-order-certificate;target-edge="
                            +std::to_string(winner->EdgeId)+";"+orderReason);
                }
            }
        }
    }
    if(complete)reason="all-source-intervals-reach-the-first-current-face-physical-boundary";
    for(const auto& note:sourceWindowNotes){if(!reason.empty())reason+=';';reason+=note;}
    return complete;
}

#include "MachiningFaceCutCoverageNormal.inc"
#include "MachiningFaceCutCoverageBoundarySet.inc"

bool MapSource(const TopoDS_Face& face,const O::SOpenCascadeBRepBuildResult& built,
    const SMachiningFaceEdgeAnalysis& boundary,const SMachiningPathEdgeAnalysis& path,
    const SMachiningPathAnalysisResult& allPaths,
    const std::vector<const SMachiningEdgeAnalysis*>& attributes,const SMachiningFaceCutCoverageOptions& options,
    std::vector<SMachiningFaceCutArrival>& arrivals,std::string& method,std::string& reason)
{
    bool radial=false,curvedPlane=false;
    if(path.AxisMode==EMachiningPathAxisMode::PlaneTangentCrossNormal)
        for(const auto& use:path.SourceUses) {
            const auto edge=built.Edges.find(use.EdgeId);
            if(edge!=built.Edges.end()) {
                const auto type=BRepAdaptor_Curve(edge->second).GetType();
                radial=radial||type==GeomAbs_Circle;
                curvedPlane=curvedPlane||type!=GeomAbs_Line;
            }
        }
    bool ignoredZeroMeasureNativeContact=false;
    bool complete=radial?MapRadialSource(face,built,boundary,path,attributes,options,arrivals,method,reason):
        curvedPlane?MapNormalStripSource(face,built,boundary,path,allPaths,attributes,options,arrivals,method,reason):
        MapAffineSource(face,built,boundary,path,attributes,options,arrivals,method,reason,ignoredZeroMeasureNativeContact);
    // Both geometric branches make exactly the same completeness claim:
    // every original SourceUse parameter interval must be covered by arrivals.
    if(complete||!method.empty()) {
        std::string coverageReason;
        if(!NativeSourceCoverage(path,arrivals,coverageReason)) {
            complete=false;
            if(reason.empty())reason=coverageReason;
            else if(reason!=coverageReason)reason+=";"+coverageReason;
        }
    }
    if(ignoredZeroMeasureNativeContact)reason+=";ignored-zero-measure-native-source-endpoint-contact";
    return complete;
}

#include "MachiningFaceCutCoverageEvidence.inc"
#include "MachiningFaceCutCoverageFinalState.inc"
}

SMachiningFaceCutCoverageResult MachiningFaceCutCoverageWork::Run(
    const SSectionFaceClassificationResult& classification,const SMachiningEdgeAnalysisResult& edgeAnalysis,
    const SMachiningPathAnalysisResult& pathAnalysis,const SMachiningFaceCutCoverageOptions& options) const
{
    SMachiningFaceCutCoverageResult result;
    try {
        if(!classification.bSuccess||!edgeAnalysis.bSuccess||!pathAnalysis.bSuccess)
            throw std::invalid_argument("face-cut-coverage-requires-successful-classify-edge-and-path-inputs");
        if(!std::isfinite(options.dPositionTolerance)||options.dPositionTolerance<=0.
            ||!std::isfinite(options.dAngularToleranceRadians)||options.dAngularToleranceRadians<=0.)
            throw std::invalid_argument("face-cut-coverage-tolerances-must-be-finite-and-positive");
        const auto built=O::ReconstructOpenCascadeShape(classification.SplitGeometry);
        if(!built.bOK)throw std::runtime_error("cannot-reconstruct-classified-native-shape");
        const std::set<std::uint64_t> machining(classification.MachiningFaceIds.begin(),classification.MachiningFaceIds.end());
        std::map<std::uint64_t,const SMachiningFaceEdgeAnalysis*> boundaries;
        for(const auto& face:edgeAnalysis.Faces)boundaries[face.FaceId]=&face;
        std::vector<const SMachiningEdgeAnalysis*> attributes;
        for(const auto& edge:edgeAnalysis.Edges)attributes.push_back(&edge);
        std::set<std::uint64_t> incomplete;
        for(const auto faceId:machining)if(!boundaries.contains(faceId)) {
            incomplete.insert(faceId);
            result.Diagnostics.push_back("face-F"+std::to_string(faceId)+"-machining-face-edge-analysis-boundary-missing");
        }
        for(const auto& boundary:pathAnalysis.UnresolvedBoundaries)incomplete.insert(boundary.FaceId);
        // Missing skin membership can hide a real source. Convexity belongs
        // to the local first-boundary evidence and must not poison unrelated
        // skin intervals on the same face.
        for(const auto& edge:edgeAnalysis.Edges)if(!edge.bSkinRelationValid) {
            std::set<std::uint64_t> owners(edge.MachiningFaceIds.begin(),edge.MachiningFaceIds.end());
            owners.insert(edge.SourceMachiningFaceIds.begin(),edge.SourceMachiningFaceIds.end());
            for(const auto faceId:owners)if(machining.contains(faceId)) {
                incomplete.insert(faceId);
                result.Diagnostics.push_back("face-F"+std::to_string(faceId)+"-source-eligibility-not-proven-at-edge-"+edge.Key);
            }
        }
        for(const auto& path:pathAnalysis.Edges) {
            if(!path.bSelectedPath)continue;
            SMachiningFaceCutSource source;source.FaceId=path.FaceId;source.SourcePathKey=path.Key;source.SourceEdgeIds=path.SourceEdgeIds;
            if(!machining.contains(path.FaceId))throw std::runtime_error("path-source-owner-is-not-a-classified-machining-face");
            for(const auto& use:path.SourceUses) {
                if(!use.bSkinRelationValid||use.SkinRelation!=ESkinRelation::OnSkin)
                    throw std::runtime_error("selected-real-path-contains-a-non-skin-source-use");
                const double a=std::min(use.FirstParameter,use.LastParameter),b=std::max(use.FirstParameter,use.LastParameter);
                if(!std::isfinite(a)||!std::isfinite(b)||a>=b)throw std::runtime_error("baseline-native-source-range-is-invalid");
            }
            const auto face=built.Faces.find(path.FaceId);const auto boundary=boundaries.find(path.FaceId);
            if(face==built.Faces.end()||boundary==boundaries.end())source.Reason="current-face-or-edge-analysis-boundary-missing";
            else {
                try {
                    source.bComplete=MapSource(face->second,built,*boundary->second,path,pathAnalysis,attributes,options,
                        result.Arrivals,source.Method,source.Reason);
                    source.bExitCorrespondenceExpanded=source.bComplete;
                } catch(const Standard_Failure& error) {source.Reason=std::string("native-face-boundary-mapping-failed: ")+error.GetMessageString();}
                catch(const std::exception& error) {source.Reason=error.what();}
            }
            if(!source.bComplete&&face!=built.Faces.end()&&boundary!=boundaries.end()) {
                std::vector<SMachiningFaceCutBoundaryWitness> witnesses;
                std::string proofReason;
                if(CertifyAllSkinBoundaryExitFace(face->second,built,*boundary->second,path,
                    attributes,options,witnesses,proofReason)) {
                    for(const auto& use:path.SourceUses) {
                        SMachiningFaceCutEvidence proof;proof.FaceId=path.FaceId;proof.SourcePathKey=path.Key;
                        proof.SourceEdgeId=use.EdgeId;proof.SourceFirstParameter=use.FirstParameter;
                        proof.SourceLastParameter=use.LastParameter;proof.Kind=EFaceCutEvidenceKind::SkinExitBoundarySet;
                        proof.bHasArrival=false;proof.bExitCorrespondenceExpanded=false;
                        proof.bThrough=proof.bComplete=true;proof.BoundaryWitnesses=witnesses;
                        proof.Reason=proofReason+";exit-correspondence-not-expanded";
                        result.Evidence.push_back(std::move(proof));
                    }
                    source.bComplete=true;source.bExitCorrespondenceExpanded=false;
                    source.Method=path.AxisMode==EMachiningPathAxisMode::StraightGenerator?
                        "bounded-ruled-all-skin-boundary-exit":"bounded-planar-all-skin-boundary-exit";
                    source.Reason=proofReason+";exit-correspondence-not-expanded;direct-map-status="+source.Reason;
                }
                else source.Reason+=";boundary-set-status="+proofReason;
            }
            if(options.Progress)options.Progress("face-cut-coverage "+path.Key+": "+source.Reason);
            result.Sources.push_back(std::move(source));
        }
        // This is an index of already-proved arrivals into the existing
        // per-face side table. It adds no source or additional machining pass.
        std::set<std::tuple<std::size_t,std::string,std::string>> sideEvidence;
        for(std::size_t i=0;i<result.Arrivals.size();++i) {
            const auto& arrival=result.Arrivals[i];
            for(const auto& side:pathAnalysis.Edges)if(side.bSide&&side.FaceId==arrival.FaceId)
                for(const auto& use:side.SourceUses) {
                    if(use.EdgeId!=arrival.TargetEdgeId)continue;
                    const double a=std::min(use.FirstParameter,use.LastParameter),b=std::max(use.FirstParameter,use.LastParameter);
                    if(a>arrival.TargetFirstParameter||b<arrival.TargetLastParameter)continue;
                    if(sideEvidence.emplace(i,side.Key,use.SpanKey).second)
                        result.SideCorrespondences.push_back({i,side.Key,use.SpanKey});
                }
        }
        BuildSkinEvidence(built,pathAnalysis,result);
        BuildSourceSkinCoverage(pathAnalysis,result);
        FinalizeSourceCoverageState(pathAnalysis,result,incomplete);
        // A face-level status is useful for inspection, but local Kind is
        // already determined solely by the source interval's own evidence.
        for(auto& span:result.Coverage)span.bFaceMappingComplete=!incomplete.contains(span.FaceId);
        result.IncompleteFaceIds.assign(incomplete.begin(),incomplete.end());
        result.bSuccess=true;result.bComplete=incomplete.empty();
        if(!result.bComplete)result.Diagnostics.push_back("some-face-source-destinations-remain-explicitly-pending");
    } catch(const Standard_Failure& error) {result.Diagnostics.push_back(error.GetMessageString());}
    catch(const std::exception& error) {result.Diagnostics.push_back(error.what());}
    return result;
}
}
