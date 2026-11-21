// Included after Normal.inc inside the coverage Work's private namespace.
// This is a bounded-domain existence proof, not a second boundary mapper.
// It stores no ray samples, artificial arrival or invented target parameter.
namespace all_skin_boundary
{
using normal_strip::L;
using normal_strip::V;

bool SeparatedDerivativeHull(const normal_strip::Piece& piece)
{
    if(piece.R.empty())return false;
    V direction{};
    for(const auto value:piece.R)direction=direction+value;
    direction=direction*(1.L/L(piece.R.size()));
    if(!normal_strip::Finite(direction))return false;
    if(piece.RExactZero.size()!=piece.R.size()||!std::isfinite(piece.RError)||piece.RError<0.)return false;
    L lower=std::numeric_limits<L>::infinity();bool positive=false;
    for(std::size_t i=0;i<piece.R.size();++i) {
        const auto value=piece.R[i];
        if(!normal_strip::Finite(value))return false;
        // A zero derivative control is permitted at an isolated knot/end.
        // Every Bernstein basis is positive in the open native interval, so
        // at least one strictly positive projected control proves C' != 0
        // there. Never round a small negative coefficient up to zero.
        if(piece.RExactZero[i])continue;
        // The direction is an arbitrary fixed floating-point vector. Its
        // construction needs no geometric error assumption; all uncertainty
        // in the derivative numerator and dot arithmetic is outward-bounded.
        const auto dot=normal_strip::DotInterval(direction,0.,value,piece.RError);
        if(!std::isfinite(dot.Lo)||!std::isfinite(dot.Hi))return false;
        lower=std::min(lower,dot.Lo);
        positive=true;
    }
    return positive&&std::isfinite(lower)&&lower>0.;
}

bool RegularPieces(std::vector<normal_strip::Piece> pieces,const gp_Pnt2d& origin)
{
    // Every piece has its own separating direction. A closed trace may turn
    // through a full revolution and need not fit one global tangent cone.
    for(int level=0;level<=8;++level) {
        std::vector<normal_strip::Piece> unresolved;
        for(auto& piece:pieces)if(!SeparatedDerivativeHull(piece))unresolved.push_back(std::move(piece));
        if(unresolved.empty())return true;
        if(level==8||unresolved.size()>256)return false;
        normal_strip::Refine(unresolved,origin);pieces=std::move(unresolved);
    }
    return false;
}

bool SourcePieces(const CurveUse& use,const gp_Pnt2d& origin,
    std::vector<normal_strip::Piece>& pieces,std::string& reason)
{
    const auto base=normal_strip::Basis(use.Curve);
    if(base.IsNull()) {reason="skin-exit-boundary-set-source-pcurve-missing";return false;}
    Geom2dAdaptor_Curve native(base,use.First,use.Last);
    if(native.GetType()==GeomAbs_BSplineCurve&&native.BSpline()->IsPeriodic()) {
        // SetNotPeriodic and exact knot insertion are representation changes
        // on a private copy. They retain the native parameter interval and do
        // not turn the stored seam into a physical open endpoint.
        const auto periodic=native.BSpline();
        if(!periodic->IsCN(0)) {reason="skin-exit-boundary-set-periodic-source-is-not-C0";return false;}
        const auto trimmed=occ::down_cast<Geom2d_BSplineCurve>(periodic->Copy());
        trimmed->SetNotPeriodic();trimmed->Segment(use.First,use.Last,0.);
        if(trimmed->FirstParameter()!=use.First||trimmed->LastParameter()!=use.Last) {
            reason="skin-exit-boundary-set-periodic-extraction-changed-native-range";return false;
        }
        Geom2dConvert_BSplineCurveToBezierCurve extraction(trimmed);
        if(!extraction.NbArcs()) {reason="skin-exit-boundary-set-source-has-no-native-bezier-pieces";return false;}
        NCollection_Array1<double> knots(1,extraction.NbArcs()+1);extraction.Knots(knots);
        for(int i=1;i<=extraction.NbArcs();++i) {
            normal_strip::Piece piece;
            if(!normal_strip::MakePiece(extraction.Arc(i),knots(i),knots(i+1),false,origin,piece)) {
                reason="skin-exit-boundary-set-source-weights-or-controls-not-certified";return false;
            }
            pieces.push_back(std::move(piece));
        }
    } else if(!normal_strip::Extract(use,false,false,origin,pieces,reason)) {
        reason="skin-exit-boundary-set-regular-source-extraction-not-proven;"+reason;return false;
    }
    return !pieces.empty();
}

bool RegularSourceUse(const CurveUse& use,const gp_Pnt2d& origin,std::string& reason)
{
    const auto base=normal_strip::Basis(use.Curve);
    if(base.IsNull()) {reason="skin-exit-boundary-set-source-pcurve-missing";return false;}
    Geom2dAdaptor_Curve native(base,use.First,use.Last);
    switch(native.GetType()) {
    case GeomAbs_Line:return true;
    case GeomAbs_Circle:
        if(std::isfinite(native.Circle().Radius())&&native.Circle().Radius()>0.)return true;
        reason="skin-exit-boundary-set-source-circle-is-not-regular";return false;
    case GeomAbs_Ellipse:
        if(std::isfinite(native.Ellipse().MajorRadius())&&std::isfinite(native.Ellipse().MinorRadius())
            &&native.Ellipse().MinorRadius()>0.)return true;
        reason="skin-exit-boundary-set-source-ellipse-is-not-regular";return false;
    default:break;
    }
    std::vector<normal_strip::Piece> pieces;
    if(!SourcePieces(use,origin,pieces,reason))return false;
    if(!RegularPieces(std::move(pieces),origin)) {
        reason="skin-exit-boundary-set-source-derivative-hull-does-not-exclude-zero";return false;
    }
    return true;
}

bool TransverseRuledSourceUse(const CurveUse& use,int generatorDerivative,std::string& reason)
{
    const auto base=normal_strip::Basis(use.Curve);
    if(base.IsNull()) {reason="ruled-skin-exit-boundary-set-source-pcurve-missing";return false;}
    Geom2dAdaptor_Curve native(base,use.First,use.Last);
    if(native.GetType()==GeomAbs_Line) {
        const auto d=native.Line().Direction();const double value=generatorDerivative==2?d.X():d.Y();
        if(std::isfinite(value)&&value!=0.)return true;
        reason="ruled-skin-exit-boundary-set-positive-native-interval-is-generator-parallel";return false;
    }
    // A nondegenerate native circle or ellipse cannot have a constant U or V
    // coordinate on a positive interval. Its isolated critical points do not
    // create a positive-width coverage gap or a fabricated tool-axis record.
    if(native.GetType()==GeomAbs_Circle&&std::isfinite(native.Circle().Radius())&&native.Circle().Radius()>0.)return true;
    if(native.GetType()==GeomAbs_Ellipse&&std::isfinite(native.Ellipse().MajorRadius())
        &&std::isfinite(native.Ellipse().MinorRadius())&&native.Ellipse().MinorRadius()>0.)return true;
    std::vector<normal_strip::Piece> pieces;
    if(!SourcePieces(use,use.Curve->Value(use.First),pieces,reason))return false;
    for(const auto& piece:pieces) {
        bool nonconstant=false;
        for(const auto r:piece.R) {
            const L value=generatorDerivative==2?r.X:r.Y;
            const auto interval=normal_strip::Around(value,piece.RError);
            if(std::isfinite(interval.Lo)&&std::isfinite(interval.Hi)&&(interval.Lo>0.||interval.Hi<0.)) {
                nonconstant=true;break;
            }
        }
        // A nonzero scalar polynomial has only finitely many roots. Thus the
        // source is regular and transverse on each open component between its
        // isolated critical parameters, with inward ruling sign given by the
        // directed coedge's transverse derivative. No root grid is sampled.
        if(!nonconstant) {
            reason="ruled-skin-exit-boundary-set-positive-native-knot-interval-is-parallel-or-transversality-not-proven";return false;
        }
    }
    return true;
}

using I3=std::array<normal_strip::Interval,3>;
using Net3=std::vector<I3>;
I3 Add3(const I3& a,const I3& b) {return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
I3 Difference3(const I3& a,const I3& b) {return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
I3 Scale3(const I3& a,normal_strip::Interval b) {return {a[0]*b,a[1]*b,a[2]*b};}
I3 Cross3(const I3& a,const I3& b) {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
bool Separated3(const Net3& first,const Net3& second={})
{
    std::array<L,3> direction{};L scale=0.;std::size_t count=0;
    for(const auto* controls:{&first,&second})for(const auto& value:*controls)for(const auto component:value) {
        if(!std::isfinite(component.Lo)||!std::isfinite(component.Hi))return false;
        scale=std::max(scale,std::abs(normal_strip::Mid(component)));
    }
    if(!(scale>0.))return false;
    for(const auto* controls:{&first,&second})for(const auto& value:*controls) {
        for(std::size_t axis=0;axis<3;++axis)direction[axis]+=normal_strip::Mid(value[axis])/scale;
        ++count;
    }
    if(!count)return false;
    for(auto& component:direction){component/=L(count);if(!std::isfinite(component))return false;}
    for(const auto* controls:{&first,&second})for(const auto& value:*controls) {
        normal_strip::Interval dot;
        for(std::size_t axis=0;axis<3;++axis)dot=dot+normal_strip::Exact(direction[axis])*value[axis];
        if(!std::isfinite(dot.Lo)||!(dot.Lo>0.))return false;
    }
    return true;
}

struct CurveNet3 final {Net3 P,R;std::vector<normal_strip::Interval> W;int Degree=0;};
bool CurveControls3(const occ::handle<Geom_BezierCurve>& curve,CurveNet3& result)
{
    using namespace normal_strip;
    if(curve.IsNull()||curve->Degree()<1||curve->Degree()>12)return false;
    result.Degree=curve->Degree();
    for(int i=1;i<=curve->NbPoles();++i) {
        const auto p=curve->Pole(i);const L weight=curve->Weight(i);
        if(!std::isfinite(p.X())||!std::isfinite(p.Y())||!std::isfinite(p.Z())
            ||!std::isfinite(weight)||!(weight>0.))return false;
        result.P.push_back({Exact(L(p.X())),Exact(L(p.Y())),Exact(L(p.Z()))});result.W.push_back(Exact(weight));
    }
    const int degree=result.Degree;result.R.resize(2*degree);
    // The derivative is in this Bezier piece's normalized parameter. Its
    // positive native-range scale does not affect a nonzero/Jacobian proof.
    for(int i=0;i<=degree;++i)for(int j=i+1;j<=degree;++j)for(const int k:{i+j-1,i+j}) {
        const auto coefficient=Exact(L(j-i))*Exact(Choose(degree,i))*Exact(Choose(degree,j))
            /Exact(Choose(2*degree-1,k));
        result.R[k]=Add3(result.R[k],Scale3(Difference3(result.P[j],result.P[i]),result.W[i]*result.W[j]*coefficient));
    }
    return true;
}

Net3 RuledDifference3(const CurveNet3& a,const CurveNet3& b)
{
    using namespace normal_strip;
    const int p=a.Degree,q=b.Degree;Net3 result(p+q+1);
    // G=N1*W0-N0*W1, formed from original pole differences to avoid a
    // subtraction of large, separately translated homogeneous positions.
    for(int i=0;i<=p;++i)for(int j=0;j<=q;++j) {
        const auto coefficient=Exact(Choose(p,i))*Exact(Choose(q,j))/Exact(Choose(p+q,i+j));
        result[i+j]=Add3(result[i+j],Scale3(Difference3(b.P[j],a.P[i]),a.W[i]*b.W[j]*coefficient));
    }
    return result;
}

Net3 CrossProduct3(const Net3& a,const Net3& b)
{
    using namespace normal_strip;
    const int p=int(a.size())-1,q=int(b.size())-1;Net3 result(p+q+1);
    for(int k=0;k<=p+q;++k)for(int i=std::max(0,k-q);i<=std::min(p,k);++i) {
        const auto coefficient=Exact(Choose(p,i))*Exact(Choose(q,k-i))/Exact(Choose(p+q,k));
        result[k]=Add3(result[k],Scale3(Cross3(a[i],b[k-i]),coefficient));
    }
    return result;
}

bool RuledPiece(const occ::handle<Geom_BezierCurve>& first,
    const occ::handle<Geom_BezierCurve>& last,int refinement=0)
{
    CurveNet3 a,b;
    if(!CurveControls3(first,a)||!CurveControls3(last,b))return false;
    const auto g=RuledDifference3(a,b);
    if(Separated3(g)) {
        const auto ja=CrossProduct3(a.R,g),jb=CrossProduct3(b.R,g);
        // With positive weights S=(1-lambda)C0+lambda C1 and lambda_v>0.
        // S_u x S_v is a positive multiple of the convex combination of
        // C0' x (C1-C0) and C1' x (C1-C0). A common strict separating
        // direction therefore proves a regular chart for every v, not only
        // at the two generating endpoints or a grid of surface samples.
        if(Separated3(ja,jb))return true;
    }
    if(refinement>=7)return false;
    for(int half=0;half<2;++half) {
        const auto ca=occ::down_cast<Geom_BezierCurve>(first->Copy());
        const auto cb=occ::down_cast<Geom_BezierCurve>(last->Copy());
        ca->Segment(half*.5,(half+1)*.5);cb->Segment(half*.5,(half+1)*.5);
        if(!RuledPiece(ca,cb,refinement+1))return false;
    }
    return true;
}

bool RuledEndCurves(const occ::handle<Geom_Curve>& first,const occ::handle<Geom_Curve>& last)
{
    const auto a=occ::down_cast<Geom_BSplineCurve>(first),b=occ::down_cast<Geom_BSplineCurve>(last);
    if(!a.IsNull()&&!b.IsNull()) {
        if(a->IsPeriodic()||b->IsPeriodic()||a->Degree()!=b->Degree()||a->NbKnots()!=b->NbKnots())return false;
        for(int i=1;i<=a->NbKnots();++i)
            if(a->Knot(i)!=b->Knot(i)||a->Multiplicity(i)!=b->Multiplicity(i))return false;
        GeomConvert_BSplineCurveToBezierCurve ca(a),cb(b);
        if(ca.NbArcs()!=cb.NbArcs()||!ca.NbArcs())return false;
        for(int i=1;i<=ca.NbArcs();++i)if(!RuledPiece(ca.Arc(i),cb.Arc(i)))return false;
        return true;
    }
    const auto ba=occ::down_cast<Geom_BezierCurve>(first),bb=occ::down_cast<Geom_BezierCurve>(last);
    return !ba.IsNull()&&!bb.IsNull()&&RuledPiece(ba,bb);
}

bool RuledSupport(const TopoDS_Face& face,double u0,double u1,double v0,double v1,
    int& generatorDerivative,std::string& reason)
{
    const BRepAdaptor_Surface native(face,true);
    occ::handle<Geom_Curve> first,last;
    const auto positiveControlNet=[](const auto& surface) {
        for(int i=1;i<=surface->NbUPoles();++i)for(int j=1;j<=surface->NbVPoles();++j) {
            const auto p=surface->Pole(i,j);const double weight=surface->Weight(i,j);
            if(!std::isfinite(p.X())||!std::isfinite(p.Y())||!std::isfinite(p.Z())
                ||!std::isfinite(weight)||!(weight>0.))return false;
        }
        return true;
    };
    if(native.GetType()==GeomAbs_BSplineSurface) {
        const auto raw=native.BSpline();
        if(raw->IsUPeriodic()||raw->IsVPeriodic()) {
            reason="ruled-skin-exit-boundary-set-periodic-support-needs-physical-seam-quotient-proof";return false;
        }
        const bool u=raw->UDegree()==1&&raw->NbUPoles()==2,v=raw->VDegree()==1&&raw->NbVPoles()==2;
        if(u==v) {reason="ruled-skin-exit-boundary-set-support-has-no-unique-degree-one-two-pole-generator";return false;}
        double a,b,c,d;raw->Bounds(a,b,c,d);
        if(u0<a||u1>b||v0<c||v1>d) {
            reason="ruled-skin-exit-boundary-set-trim-exceeds-native-support-domain";return false;
        }
        const auto surface=occ::down_cast<Geom_BSplineSurface>(raw->Copy());
        surface->Segment(u0,u1,v0,v1,0.,0.);
        surface->Bounds(a,b,c,d);
        if(a!=u0||b!=u1||c!=v0||d!=v1) {
            reason="ruled-skin-exit-boundary-set-support-segmentation-changed-trim-domain";return false;
        }
        if(!positiveControlNet(surface)) {
            reason="ruled-skin-exit-boundary-set-trim-support-weights-or-control-net-not-positive-finite";return false;
        }
        generatorDerivative=u?1:2;
        first=u?surface->UIso(u0):surface->VIso(v0);last=u?surface->UIso(u1):surface->VIso(v1);
    } else if(native.GetType()==GeomAbs_BezierSurface) {
        const auto raw=native.Bezier();
        const bool u=raw->UDegree()==1&&raw->NbUPoles()==2,v=raw->VDegree()==1&&raw->NbVPoles()==2;
        if(u==v||u0<0.||u1>1.||v0<0.||v1>1.) {
            reason="ruled-skin-exit-boundary-set-bezier-generator-or-native-trim-domain-not-certified";return false;
        }
        const auto surface=occ::down_cast<Geom_BezierSurface>(raw->Copy());
        surface->Segment(u0,u1,v0,v1);generatorDerivative=u?1:2;
        if(!positiveControlNet(surface)) {
            reason="ruled-skin-exit-boundary-set-trim-support-weights-or-control-net-not-positive-finite";return false;
        }
        // Segment reparameterizes its private Bezier copy to [0,1]^2; both
        // native coordinates retain their positive affine orientation.
        first=u?surface->UIso(0.):surface->VIso(0.);last=u?surface->UIso(1.):surface->VIso(1.);
    } else {reason="ruled-skin-exit-boundary-set-support-is-not-native-bspline-or-bezier";return false;}
    if(first.IsNull()||last.IsNull()||!RuledEndCurves(first,last)) {
        reason="ruled-skin-exit-boundary-set-positive-weight-nonzero-generator-and-full-trim-jacobian-not-certified";return false;
    }
    return true;
}

std::uint64_t NativeEdgeId(const TopoDS_Edge& edge,const O::SOpenCascadeBRepBuildResult& built)
{
    std::uint64_t result=0;
    for(const auto& [id,candidate]:built.Edges)if(candidate.IsSame(edge)) {
        if(result)return 0;result=id;
    }
    return result;
}

bool SkinSpans(const std::uint64_t edgeId,const std::uint64_t faceId,double first,double last,
    const std::vector<const SMachiningEdgeAnalysis*>& attributes,
    std::vector<SMachiningFaceCutBoundaryWitness>& witnesses,std::string& reason)
{
    std::vector<const SMachiningEdgeAnalysis*> spans;
    for(const auto* attr:attributes)if(attr&&attr->EdgeId==edgeId&&HasFace(*attr,faceId)) {
        if(!std::isfinite(attr->FirstParameter)||!std::isfinite(attr->LastParameter)
            ||attr->FirstParameter>=attr->LastParameter) {
            reason="skin-exit-boundary-set-boundary-has-an-invalid-native-span";return false;
        }
        if(attr->LastParameter<=first||attr->FirstParameter>=last)continue;
        if(attr->FirstParameter<first||attr->LastParameter>last) {
            reason="skin-exit-boundary-set-physical-boundary-attribute-exceeds-native-range:E"+std::to_string(edgeId);return false;
        }
        if(!attr->bSkinRelationValid||attr->SkinRelation!=ESkinRelation::OnSkin) {
            reason="skin-exit-boundary-set-physical-boundary-is-not-wholly-OnSkin:E"+std::to_string(edgeId);return false;
        }
        if(!attr->bSourceSkinMembership||attr->SourceSkinFaceIds.empty()) {
            reason="skin-exit-boundary-set-physical-boundary-source-skin-membership-not-proven:E"+std::to_string(edgeId);return false;
        }
        spans.push_back(attr);
    }
    std::sort(spans.begin(),spans.end(),[](const auto* a,const auto* b) {
        return a->FirstParameter<b->FirstParameter
            ||(a->FirstParameter==b->FirstParameter&&a->LastParameter<b->LastParameter);
    });
    double covered=first;
    for(const auto* span:spans) {
        // This union is exact on the recorded source parameters. A short gap
        // remains a gap; no endpoint tolerance extends a skin witness.
        if(span->FirstParameter>covered) {
            reason="skin-exit-boundary-set-physical-boundary-has-a-native-skin-gap:E"+std::to_string(edgeId);return false;
        }
        covered=std::max(covered,span->LastParameter);
        SMachiningFaceCutBoundaryWitness witness;witness.EdgeId=edgeId;witness.SpanKey=span->Key;
        witness.FirstParameter=span->FirstParameter;witness.LastParameter=span->LastParameter;
        witness.SourceSkinFaceIds=span->SourceSkinFaceIds;witness.SkinReason=span->SkinReason;
        witnesses.push_back(std::move(witness));
    }
    if(covered!=last) {
        reason="skin-exit-boundary-set-physical-boundary-native-domain-not-fully-covered:E"+std::to_string(edgeId);return false;
    }
    return true;
}
}

bool CertifyAllSkinBoundaryExitFace(const TopoDS_Face& face,
    const O::SOpenCascadeBRepBuildResult& built,const SMachiningFaceEdgeAnalysis& boundary,
    const SMachiningPathEdgeAnalysis& source,const std::vector<const SMachiningEdgeAnalysis*>& attributes,
    const SMachiningFaceCutCoverageOptions& options,
    std::vector<SMachiningFaceCutBoundaryWitness>& witnesses,std::string& reason) try
{
    using namespace all_skin_boundary;
    const auto reject=[&](const std::string& text){reason=text;return false;};
    const BRepAdaptor_Surface support(face,true);
    const bool planeProof=support.GetType()==GeomAbs_Plane;
    const bool ruledProof=(support.GetType()==GeomAbs_BSplineSurface||support.GetType()==GeomAbs_BezierSurface)
        &&source.AxisMode==EMachiningPathAxisMode::StraightGenerator;
    if((!planeProof&&!ruledProof)||source.FaceId!=boundary.FaceId
        ||!source.bSelectedPath||!source.bOnSkinPath||source.SourceUses.empty()
        ||(planeProof&&source.AxisMode!=EMachiningPathAxisMode::PlaneTangentCrossNormal))
        return reject("skin-exit-boundary-set-requires-native-plane-or-certified-ruled-support-and-real-skin-source");
    if(!BRepCheck_Analyzer(face,true).IsValid())return reject("skin-exit-boundary-set-native-face-is-invalid");
    BRepCheck_Face faceCheck(face);faceCheck.GeometricControls(true);
    if(faceCheck.IntersectWires()!=BRepCheck_NoError||faceCheck.ClassifyWires()!=BRepCheck_NoError
        ||faceCheck.OrientationOfWires()!=BRepCheck_NoError)
        return reject("skin-exit-boundary-set-native-loops-intersect-or-lack-valid-domain-orientation");
    if(BRepTools::OuterWire(face).IsNull())return reject("skin-exit-boundary-set-has-no-finite-outer-wire");
    double u0=0.,u1=0.,v0=0.,v1=0.;BRepTools::UVBounds(face,u0,u1,v0,v1);
    if(!std::isfinite(u0)||!std::isfinite(u1)||!std::isfinite(v0)||!std::isfinite(v1)
        ||std::max({std::abs(u0),std::abs(u1),std::abs(v0),std::abs(v1)})>=1.e90
        ||!(u1>u0)||!(v1>v0))return reject("skin-exit-boundary-set-domain-is-not-finite-and-two-dimensional");
    int generatorDerivative=0;
    if(ruledProof&&!RuledSupport(face,u0,u1,v0,v1,generatorDerivative,reason))return false;
    if(BRepTopAdaptor_FClass2d(face,options.dPositionTolerance).PerformInfinitePoint()!=TopAbs_OUT)
        return reject("skin-exit-boundary-set-wire-domain-is-unbounded-or-not-proven-outside-at-infinity");
    std::vector<TopoDS_Wire> nativeWires;
    for(TopoDS_Iterator it(face,false,true);it.More();it.Next())
        if(it.Value().ShapeType()==TopAbs_WIRE)nativeWires.push_back(TopoDS::Wire(it.Value()));
    if(nativeWires.empty()||nativeWires.size()!=boundary.Wires.size())
        return reject("skin-exit-boundary-set-native-and-upstream-wire-inventories-differ");
    std::vector<SMachiningFaceCutBoundaryWitness> certified;
    std::set<std::uint64_t> physicalEdges;
    std::set<std::pair<std::uint64_t,bool>> sourceDirections;
    bool sourceWireFound=false;
    for(std::size_t wi=0;wi<nativeWires.size();++wi) {
        const auto& wire=nativeWires[wi];const auto& recorded=boundary.Wires[wi];
        if(!recorded.bOrdered)return reject("skin-exit-boundary-set-has-an-unordered-boundary-wire");
        BRepCheck_Wire wireCheck(wire);wireCheck.GeometricControls(true);TopoDS_Edge a,b;
        if(wireCheck.Closed()!=BRepCheck_NoError||wireCheck.Closed2d(face)!=BRepCheck_NoError
            ||wireCheck.Orientation(face)!=BRepCheck_NoError||wireCheck.SelfIntersect(face,a,b)!=BRepCheck_NoError)
            return reject("skin-exit-boundary-set-native-wire-is-open-self-intersecting-or-misoriented");
        std::map<std::pair<std::uint64_t,bool>,std::size_t> actual,listed;
        for(const auto& group:recorded.Groups)for(const auto& use:group.SourceUses)++listed[{use.EdgeId,use.bReversed}];
        std::size_t explorerCount=0,physicalCount=0;
        for(TopExp_Explorer it(wire,TopAbs_EDGE);it.More();it.Next())++physicalCount;
        for(BRepTools_WireExplorer ordered(wire,face);ordered.More();ordered.Next()) {
            ++explorerCount;const auto edge=ordered.Current();const auto id=NativeEdgeId(edge,built);
            if(!id||BRep_Tool::Degenerated(edge)||BRep_Tool::IsClosed(edge,face)
                ||(edge.Orientation()!=TopAbs_FORWARD&&edge.Orientation()!=TopAbs_REVERSED)
                ||!physicalEdges.insert(id).second)
                return reject("skin-exit-boundary-set-has-unlisted-degenerate-repeated-or-parametric-seam-boundary");
            const auto direction=std::make_pair(id,edge.Orientation()==TopAbs_REVERSED);++actual[direction];
            if(recorded.WireId==source.WireId)sourceDirections.insert(direction);
            double first=0.,last=0.;BRep_Tool::Range(edge,first,last);
            if(!std::isfinite(first)||!std::isfinite(last)||!(last>first))
                return reject("skin-exit-boundary-set-physical-edge-has-no-finite-positive-native-range");
            if(!SkinSpans(id,source.FaceId,first,last,attributes,certified,reason))return false;
        }
        if(explorerCount!=physicalCount||actual.empty()||actual!=listed)
            return reject("skin-exit-boundary-set-physical-coedge-inventory-is-not-completely-accounted-for");
        if(recorded.WireId==source.WireId) {
            if(sourceWireFound)return reject("skin-exit-boundary-set-source-wire-id-is-ambiguous");
            sourceWireFound=true;
        }
    }
    if(!sourceWireFound||certified.empty())return reject("skin-exit-boundary-set-source-wire-or-exit-witness-set-is-missing");
    Chart identity;std::vector<CurveUse> sourceCurves;
    for(const auto& use:source.SourceUses) {
        if(!use.bSkinRelationValid||use.SkinRelation!=ESkinRelation::OnSkin
            ||!sourceDirections.contains({use.EdgeId,use.bReversed}))
            return reject("skin-exit-boundary-set-source-use-is-not-an-original-directed-skin-coedge");
        const auto found=built.Edges.find(use.EdgeId);if(found==built.Edges.end())return reject("skin-exit-boundary-set-source-native-edge-missing");
        CurveUse curve;
        if(!ReadCurve(face,found->second,use.EdgeId,use.FirstParameter,use.LastParameter,identity,curve,reason))return false;
        if(curve.First!=use.FirstParameter||curve.Last!=use.LastParameter)
            return reject("skin-exit-boundary-set-source-native-domain-was-clamped");
        curve.bReversed=use.bReversed;
        if(planeProof?!RegularSourceUse(curve,curve.Curve->Value(curve.First),reason):
            !TransverseRuledSourceUse(curve,generatorDerivative,reason))return false;
        sourceCurves.push_back(std::move(curve));
    }
    if(source.Samples.size()!=2)return reject("skin-exit-boundary-set-source-does-not-retain-exactly-two-endpoint-records");
    gp_Vec px,py;
    if(planeProof) {
        const auto plane=support.Plane();px=gp_Vec(plane.Position().XDirection());py=gp_Vec(plane.Position().YDirection());
    }
    std::size_t unavailableEndpointAxes=0;
    for(std::size_t endpointIndex=0;endpointIndex<2;++endpointIndex) {
        const auto& endpoint=source.Samples[endpointIndex];
        const auto& use=endpointIndex?source.SourceUses.back():source.SourceUses.front();
        const auto& curve=endpointIndex?sourceCurves.back():sourceCurves.front();
        const double expectedParameter=endpointIndex?(use.bReversed?use.FirstParameter:use.LastParameter):
            (use.bReversed?use.LastParameter:use.FirstParameter);
        if(endpoint.SourceEdgeId!=use.EdgeId||endpoint.SourceSpanKey!=use.SpanKey||endpoint.Parameter!=expectedParameter)
            return reject("skin-exit-boundary-set-source-endpoint-does-not-retain-its-original-native-identity");
        if(!endpoint.bToolAxisValid) {
            // The theorem classifies regular open native intervals. An
            // isolated endpoint's existing failure remains an original fact;
            // no replacement tangent or tool axis is manufactured here.
            ++unavailableEndpointAxes;continue;
        }
        if(endpoint.Status!=EMachiningPathStatus::Ready||!endpoint.bPositionValid||!endpoint.bTangentValid)
            return reject("skin-exit-boundary-set-recorded-valid-endpoint-axis-is-not-independently-ready");
        gp_Pnt2d point;gp_Vec2d tangent;curve.Curve->D1(endpoint.Parameter,point,tangent);
        if(use.bReversed)tangent.Reverse();
        if(tangent.SquareMagnitude()<=0.||!std::isfinite(tangent.SquareMagnitude()))
            return reject("skin-exit-boundary-set-native-endpoint-tangent-is-singular");
        const auto axis=Vec(endpoint.ToolAxis);gp_Vec inward;
        if(planeProof)inward=px*(-tangent.Y())+py*tangent.X();
        else {
            // A valid recorded axis must agree with this endpoint's own
            // directed coedge interior sign. A whole closed trace need not
            // choose one global sign: its transverse derivative may change
            // sign at finitely many original parallel/critical parameters.
            const double transverse=generatorDerivative==2?tangent.X():-tangent.Y();
            if(!std::isfinite(transverse)||transverse==0.)
                return reject("ruled-skin-exit-boundary-set-valid-recorded-axis-has-no-transverse-native-endpoint-sense");
            gp_Pnt location;gp_Vec du,dv;support.D1(point.X(),point.Y(),location,du,dv);
            inward=generatorDerivative==1?du:dv;if(transverse<0.)inward.Reverse();
        }
        const double product=axis.Magnitude()*inward.Magnitude();
        if(!(product>0.)||!std::isfinite(product)
            ||axis.Dot(inward)/product<std::cos(options.dAngularToleranceRadians))
            return reject("skin-exit-boundary-set-recorded-axis-does-not-follow-the-native-coedge-inward-normal-family");
    }
    // A positive interior segment follows from the regular oriented boundary;
    // a finite first exit follows from boundedness. All possible exit boundary
    // intervals have already been retained above, including this source's far
    // closed-chain spans. The specific exit correspondence is not expanded.
    witnesses=std::move(certified);
    reason=planeProof?"bounded-valid-native-plane-domain;":
        "bounded-valid-native-ruled-domain;native-degree-one-two-pole-"+std::string(generatorDerivative==1?"u":"v")+
        "-generator;positive-weights-nondegenerate-ruling-and-full-trim-jacobian-certified;"
        "finite-isolated-source-generator-parallel-parameters-retained;";
    reason+="all-physical-native-boundary-intervals-source-attributed-OnSkin;"+
        std::string(planeProof?"source-regular-on-open-native-knot-intervals;":
            "source-regular-between-finitely-many-native-generator-critical-parameters;")+
        "original-coedge-inward-"+
        std::string(planeProof?"normal":"generator")+"-family;"
        "skin-exit-exists-on-regular-interior;exit-correspondence-not-expanded;"
        "original-endpoint-axis-records-retained;unavailable-endpoint-axes="+std::to_string(unavailableEndpointAxes);
    return true;
}
catch(const Standard_Failure& error) {
    reason="skin-exit-boundary-set-native-certificate-not-proven:"+std::string(error.GetMessageString());return false;
}
catch(const std::exception& error) {
    reason="skin-exit-boundary-set-native-certificate-not-proven:"+std::string(error.what());return false;
}
