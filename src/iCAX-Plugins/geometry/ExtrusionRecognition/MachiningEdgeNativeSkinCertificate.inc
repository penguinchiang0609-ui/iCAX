// Included in MachiningEdgeAnalysisWork.cpp's private namespace. This is a
// whole-domain native representation certificate, not sampled curve matching.
struct NativeSkinBoundaryCertificate final
{
    std::uint64_t EdgeId=0;
    std::set<std::uint64_t> FaceIds;
    double Bound=0.0,TargetTolerance=0.0,WitnessTolerance=0.0,AcceptedTolerance=0.0;
    bool bReversed=false;
    std::string Method;
    std::vector<std::pair<double,double>> ParameterPairs;
};

double NativeSkinLinePoleRoundoff(const gp_Lin& line,const double parameter,const gp_Pnt& point)
{
    const auto origin=line.Location();const auto direction=line.Direction();
    const std::array<double,3> o{origin.X(),origin.Y(),origin.Z()},d{direction.X(),direction.Y(),direction.Z()},p{point.X(),point.Y(),point.Z()};
    const double infinity=std::numeric_limits<double>::infinity();double square=0.;
    for(std::size_t axis=0;axis<3;++axis){const double product=d[axis]*parameter;
        const double lo=std::nextafter(o[axis]+std::nextafter(product,-infinity),-infinity);
        const double hi=std::nextafter(o[axis]+std::nextafter(product,infinity),infinity);
        const double error=std::max(std::nextafter(std::abs(p[axis]-lo),infinity),std::nextafter(std::abs(p[axis]-hi),infinity));
        square=std::nextafter(square+std::nextafter(error*error,infinity),infinity);}
    return std::nextafter(std::sqrt(square),infinity);
}

Handle(Geom_BSplineCurve) NativeSkinSplineBasis(const FiniteEdge& edge,double* representationError=nullptr)
{
    if(representationError)*representationError=0.;
    auto basis=edge.Curve;
    while(!basis.IsNull())
    {
        const auto trim=Handle(Geom_TrimmedCurve)::DownCast(basis);
        if(trim.IsNull())break;
        // A trim is a finite source-domain restriction, not permission to use
        // the rest of its support in an ownership certificate.
        if(edge.First<trim->FirstParameter()||edge.Last>trim->LastParameter())return {};
        basis=trim->BasisCurve();
    }
    auto native=Handle(Geom_BSplineCurve)::DownCast(basis);
    if(!native.IsNull())return native;
    const auto line=Handle(Geom_Line)::DownCast(basis);
    if(!line.IsNull()) {
        if(!std::isfinite(edge.First)||!std::isfinite(edge.Last)||Precision::IsInfinite(edge.First)||Precision::IsInfinite(edge.Last)
            ||!(edge.Last>edge.First)||!std::isfinite(edge.Last-edge.First))return {};
        NCollection_Array1<gp_Pnt> poles(1,2);poles(1)=line->Value(edge.First);poles(2)=line->Value(edge.Last);
        const double error=std::max(NativeSkinLinePoleRoundoff(line->Lin(),edge.First,poles(1)),NativeSkinLinePoleRoundoff(line->Lin(),edge.Last,poles(2)));
        if(!std::isfinite(error))return {};
        NCollection_Array1<double> knots(1,2);knots(1)=edge.First;knots(2)=edge.Last;
        NCollection_Array1<int> multiplicities(1,2);multiplicities(1)=multiplicities(2)=2;
        native=new Geom_BSplineCurve(poles,knots,multiplicities,1,false);
        if(native->FirstParameter()!=edge.First||native->LastParameter()!=edge.Last)return {};
        // L(t)=O+D*t is exactly a degree-one Bernstein polynomial on this
        // ORIGINAL finite [First,Last]. No fit, source-domain extension or
        // angular reparameterization occurs. The outward endpoint arithmetic
        // envelope also bounds the entire affine representation discrepancy.
        if(representationError)*representationError=error;
        return native;
    }
    const auto bezier=Handle(Geom_BezierCurve)::DownCast(basis);
    if(bezier.IsNull()||bezier->FirstParameter()!=0.||bezier->LastParameter()!=1.
        ||edge.First<0.||edge.Last>1.)return {};
    // This is the exact Bernstein-to-clamped-B-spline identity on [0,1].
    // Verify the representation, including every original rational weight;
    // no approximator, new parameterization or sampled correspondence is used.
    native=GeomConvert::CurveToBSplineCurve(bezier);
    if(native.IsNull()||native->FirstParameter()!=0.||native->LastParameter()!=1.
        ||native->Degree()!=bezier->Degree()||native->NbPoles()!=bezier->NbPoles()
        ||native->NbKnots()!=2||native->Knot(1)!=0.||native->Knot(2)!=1.
        ||native->Multiplicity(1)!=native->Degree()+1||native->Multiplicity(2)!=native->Degree()+1)return {};
    for(int i=1;i<=native->NbPoles();++i)
        if(native->Pole(i).Distance(bezier->Pole(i))!=0.||native->Weight(i)!=bezier->Weight(i))return {};
    return native;
}

Handle(Geom_BSplineCurve) NativeSkinSpline(const FiniteEdge& edge,bool reverse,double* representationError=nullptr)
{
    const auto native=NativeSkinSplineBasis(edge,representationError);
    // Segment's kernel tolerance can clamp a slightly exterior trim endpoint.
    // Such domains require the explicit affine Bernstein certificate below;
    // they may not silently lose their exterior parameter interval.
    if(native.IsNull()||edge.First<native->FirstParameter()||edge.Last>native->LastParameter())return {};
    auto copy=Handle(Geom_BSplineCurve)::DownCast(native->Copy());
    copy->Segment(edge.First,edge.Last);
    if(reverse)copy->Reverse();
    NCollection_Array1<double> knots(copy->Knots());
    BSplCLib::Reparametrize(0.,1.,knots);copy->SetKnots(knots);
    return copy;
}

long double NativeSkinBinomial(int n,int k)
{
    if(k<0||k>n)return 0.;k=std::min(k,n-k);long double value=1.;
    for(int i=1;i<=k;++i)value=value*static_cast<long double>(n-k+i)/i;
    return value;
}

Handle(Geom_BezierCurve) NativeSkinBezierPiece(GeomConvert_BSplineCurveToBezierCurve& pieces,
    double first,double last)
{
    NCollection_Array1<double> knots(1,pieces.NbArcs()+1);pieces.Knots(knots);
    const double lo=std::min(first,last),hi=std::max(first,last);
    double middle=lo+(hi-lo)*.5;if(middle==hi)middle=lo;
    int span=1;
    while(span<pieces.NbArcs()&&middle>=knots(span+1))++span;
    // Geom_BezierCurve::Segment explicitly supports parameters outside [0,1].
    // Extending the end polynomial/rational span is the native B-spline's
    // actual extrapolation; this does not clamp a trim or fit a replacement.
    const double a=knots(span),b=knots(span+1);
    // A native internal knot is a real polynomial boundary. Round-tripping a
    // normalized event may not authorize extension across it. Only the first
    // or last global span may extend to a source's actual exterior trim.
    if(!(hi>lo)||(span>1&&lo<a)||(span<pieces.NbArcs()&&hi>b))return {};
    auto result=Handle(Geom_BezierCurve)::DownCast(pieces.Arc(span)->Copy());
    result->Segment((lo-a)/(b-a),(hi-a)/(b-a));
    if(first>last)result->Reverse();
    return result;
}

bool NativeSkinBezierDifferenceBound(const Handle(Geom_BezierCurve)& a,
    const Handle(Geom_BezierCurve)& b,double& bound)
{
    if(a.IsNull()||b.IsNull())return false;
    const int p=a->Degree(),q=b->Degree();
    long double minimumA=std::numeric_limits<long double>::infinity(),minimumB=minimumA;
    for(int i=1;i<=a->NbPoles();++i)
    {const double w=a->Weight(i);if(!std::isfinite(w)||w<=0.)return false;minimumA=std::min(minimumA,static_cast<long double>(w));}
    for(int i=1;i<=b->NbPoles();++i)
    {const double w=b->Weight(i);if(!std::isfinite(w)||w<=0.)return false;minimumB=std::min(minimumB,static_cast<long double>(w));}
    // Work in a common translated frame before forming the homogeneous
    // numerator N1*W2-N2*W1, so a distant CAD origin does not amplify cancellation.
    const auto origin=a->Pole(1);
    long double maximum=0.;
    for(int k=0;k<=p+q;++k)
    {
        std::array<long double,3> sum{},magnitude{};
        for(int i=std::max(0,k-q);i<=std::min(p,k);++i)
        {
            const int j=k-i;const auto pa=a->Pole(i+1),pb=b->Pole(j+1);
            const long double factor=NativeSkinBinomial(p,i)*NativeSkinBinomial(q,j)/NativeSkinBinomial(p+q,k)
                *static_cast<long double>(a->Weight(i+1))*b->Weight(j+1);
            const std::array<long double,3> x{static_cast<long double>(pa.X())-origin.X(),
                static_cast<long double>(pa.Y())-origin.Y(),static_cast<long double>(pa.Z())-origin.Z()};
            const std::array<long double,3> y{static_cast<long double>(pb.X())-origin.X(),
                static_cast<long double>(pb.Y())-origin.Y(),static_cast<long double>(pb.Z())-origin.Z()};
            for(std::size_t axis=0;axis<3;++axis)
            {sum[axis]+=factor*(x[axis]-y[axis]);magnitude[axis]+=std::abs(factor)*(std::abs(x[axis])+std::abs(y[axis]));}
        }
        long double square=0.;
        for(std::size_t axis=0;axis<3;++axis)
        {
            const long double guard=256.*std::numeric_limits<long double>::epsilon()*magnitude[axis];
            const long double value=std::abs(sum[axis])+guard;square+=value*value;
        }
        maximum=std::max(maximum,std::sqrt(square));
    }
    const long double denominator=std::nextafter(minimumA*minimumB,0.L);
    if(!(denominator>0.)||!std::isfinite(maximum))return false;
    const long double result=maximum/denominator;
    if(!std::isfinite(result))return false;
    bound=std::nextafter(static_cast<double>(result),std::numeric_limits<double>::infinity());
    return std::isfinite(bound);
}

bool NativeSkinAffineBernsteinBound(const FiniteEdge& target,const FiniteEdge& witness,
    bool reverse,double& bound,std::vector<std::pair<double,double>>& parameterPairs) try
{
    double targetError=0.,witnessError=0.;
    const auto a=NativeSkinSplineBasis(target,&targetError),b=NativeSkinSplineBasis(witness,&witnessError);
    if(a.IsNull()||b.IsNull()||!(target.Last>target.First)||!(witness.Last>witness.First))return false;
    if((a->IsPeriodic()&&(target.First<a->FirstParameter()||target.Last>a->LastParameter()))
        ||(b->IsPeriodic()&&(witness.First<b->FirstParameter()||witness.Last>b->LastParameter())))return false;
    GeomConvert_BSplineCurveToBezierCurve piecesA(a),piecesB(b);
    struct Event final {double A=0.,B=0.;bool bA=false,bB=false;};
    std::map<double,Event> events;
    events.emplace(0.,Event{target.First,reverse?witness.Last:witness.First,true,true});
    events.emplace(1.,Event{target.Last,reverse?witness.First:witness.Last,true,true});
    for(int i=1;i<=a->NbKnots();++i)if(a->Knot(i)>target.First&&a->Knot(i)<target.Last)
    {
        const double s=(a->Knot(i)-target.First)/(target.Last-target.First);
        if(!(s>0.&&s<1.))return false;
        auto& event=events[s];if(event.bA&&event.A!=a->Knot(i))return false;
        event.A=a->Knot(i);event.bA=true;
    }
    for(int i=1;i<=b->NbKnots();++i)if(b->Knot(i)>witness.First&&b->Knot(i)<witness.Last)
    {
        double s=(b->Knot(i)-witness.First)/(witness.Last-witness.First);if(reverse)s=1.-s;
        if(!(s>0.&&s<1.))return false;
        auto& event=events[s];if(event.bB&&event.B!=b->Knot(i))return false;
        event.B=b->Knot(i);event.bB=true;
    }
    for(auto& [s,event]:events)
    {
        if(!event.bA)event.A=target.First+s*(target.Last-target.First);
        if(!event.bB)event.B=reverse?witness.Last-s*(witness.Last-witness.First):witness.First+s*(witness.Last-witness.First);
    }
    bound=0.;
    parameterPairs.clear();
    for(const auto& [s,event]:events)parameterPairs.emplace_back(event.A,event.B);
    for(auto previous=events.begin(),current=std::next(previous);current!=events.end();previous=current,++current)
    {
        const double af=previous->second.A,al=current->second.A,bf=previous->second.B,bl=current->second.B;
        if(!(al>af)||(reverse?!(bf>bl):!(bl>bf)))return false;
        double piece=0.;
        if(!NativeSkinBezierDifferenceBound(NativeSkinBezierPiece(piecesA,af,al),NativeSkinBezierPiece(piecesB,bf,bl),piece))return false;
        bound=std::max(bound,piece);
    }
    if(targetError>0.||witnessError>0.)bound=std::nextafter(bound+std::nextafter(targetError+witnessError,std::numeric_limits<double>::infinity()),std::numeric_limits<double>::infinity());
    return std::isfinite(bound);
}
catch(const Standard_Failure&) {return false;}

bool CertifyNativeSkinBoundary(const FiniteEdge& target,const FiniteEdge& witness,
    NativeSkinBoundaryCertificate& proof) try
{
    const double allowed=std::min(target.DeclaredTolerance,witness.DeclaredTolerance);
    if(!std::isfinite(allowed)||allowed<=0.)return false;
    double targetError=0.;const auto nativeSource=NativeSkinSpline(target,false,&targetError);
    bool found=false;double best=std::numeric_limits<double>::infinity();bool reversed=false;bool hasNativeLine=targetError>0.;
    std::string method;
    std::vector<std::pair<double,double>> parameterPairs;
    for(const bool reverse:{false,true})
    {
        try
        {
        if(nativeSource.IsNull())continue;
        const auto source=Handle(Geom_BSplineCurve)::DownCast(nativeSource->Copy());
        double witnessError=0.;const auto other=NativeSkinSpline(witness,reverse,&witnessError);
        hasNativeLine=hasNativeLine||witnessError>0.;
        if(other.IsNull())continue;
        // Exact degree elevation and knot insertion only change the native
        // representation. They do not fit or sample a replacement curve.
        const int degree=std::max(source->Degree(),other->Degree());
        source->IncreaseDegree(degree);other->IncreaseDegree(degree);
        std::map<double,int> knots;
        for(const auto& curve:{source,other})for(int i=1;i<=curve->NbKnots();++i)
            knots[curve->Knot(i)]=std::max(knots[curve->Knot(i)],curve->Multiplicity(i));
        for(const auto& [knot,multiplicity]:knots)if(knot>0.&&knot<1.)
            for(const auto& curve:{source,other})
            {
                bool present=false;
                for(int i=1;i<=curve->NbKnots();++i)if(curve->Knot(i)==knot&&curve->Multiplicity(i)>=multiplicity)
                {present=true;break;}
                if(!present)curve->InsertKnot(knot,multiplicity,0.,false);
            }
        if(source->NbPoles()!=other->NbPoles()||source->NbKnots()!=other->NbKnots())continue;
        bool basis=true;
        for(int i=1;i<=source->NbKnots();++i)
            basis=basis&&source->Knot(i)==other->Knot(i)&&source->Multiplicity(i)==other->Multiplicity(i);
        // Equal positive weights give the same nonnegative partition of unity.
        // Different weight sequences are deliberately left to another solver;
        // no near-equality of weights or knots is silently accepted here.
        for(int i=1;i<=source->NbPoles();++i)
            basis=basis&&std::isfinite(source->Weight(i))&&source->Weight(i)>0.
                &&source->Weight(i)==other->Weight(i);
        if(!basis)continue;
        double bound=0.;
        for(int i=1;i<=source->NbPoles();++i)
            bound=std::max(bound,source->Pole(i).Distance(other->Pole(i)));
        if(bound>0.)bound=std::nextafter(bound,std::numeric_limits<double>::infinity());
        if(targetError>0.||witnessError>0.)bound=std::nextafter(bound+std::nextafter(targetError+witnessError,std::numeric_limits<double>::infinity()),std::numeric_limits<double>::infinity());
        if(std::isfinite(bound)&&bound<=allowed&&bound<best)
        {
            found=true;best=bound;reversed=reverse;method=hasNativeLine?"native-line-original-parameter-degree-one-positive-common-basis-control-pole-bound":"positive-common-basis-control-pole-bound";
            parameterPairs={{target.First,reverse?witness.Last:witness.First},{target.Last,reverse?witness.First:witness.Last}};
        }
        }
        catch(const Standard_Failure&) { /* This candidate has no native certificate. */ }
    }
    if(!found)for(const bool reverse:{false,true})
    {
        double bound=0.;std::vector<std::pair<double,double>> pairs;
        if(NativeSkinAffineBernsteinBound(target,witness,reverse,bound,pairs)&&bound<=allowed&&bound<best)
        {found=true;best=bound;reversed=reverse;method=hasNativeLine?"native-line-original-parameter-degree-one-homogeneous-bernstein-native-event-bound":"positive-weight-homogeneous-bernstein-native-event-piecewise-affine-bound";parameterPairs=std::move(pairs);}
    }
    if(!found)return false;
    proof.EdgeId=witness.Id;proof.Bound=best;proof.TargetTolerance=target.DeclaredTolerance;
    proof.WitnessTolerance=witness.DeclaredTolerance;proof.AcceptedTolerance=allowed;proof.bReversed=reversed;
    proof.Method=method;
    proof.ParameterPairs=std::move(parameterPairs);
    return true;
}
catch(const Standard_Failure&) {return false;}

// An arbitrary section can have an exact X-extruded B-spline support without
// being a plane or cylinder. Prove its generator from the tensor-product
// controls and prove the finite profile against section curves traced to this
// face's own original coedges. A recess floor with no section provenance does
// not become skin merely because it has an X-parallel tangent.
bool IsCertifiedNativeExtrusionSkinFace(const TopoDS_Face& face,
    const std::vector<SectionCurve>& section,std::string* nativeProof)
{
    const auto surface=Handle(Geom_BSplineSurface)::DownCast(BRep_Tool::Surface(face));
    if(surface.IsNull())return false;
    const bool u=surface->UDegree()==1&&surface->NbUPoles()==2&&!surface->IsUPeriodic();
    const bool v=surface->VDegree()==1&&surface->NbVPoles()==2&&!surface->IsVPeriodic();
    if(u==v)return false;
    const int profiles=u?surface->NbVPoles():surface->NbUPoles();
    int sense=0;
    for(int i=1;i<=profiles;++i)
    {
        const auto a=u?surface->Pole(1,i):surface->Pole(i,1);
        const auto b=u?surface->Pole(2,i):surface->Pole(i,2);
        const double wa=u?surface->Weight(1,i):surface->Weight(i,1);
        const double wb=u?surface->Weight(2,i):surface->Weight(i,2);
        if(a.Y()!=b.Y()||a.Z()!=b.Z()||!std::isfinite(wa)||wa<=0.||wa!=wb)return false;
        const double delta=b.X()-a.X();if(!std::isfinite(delta)||delta==0.)return false;
        const int current=delta>0.?1:-1;if(sense&&sense!=current)return false;sense=current;
    }
    double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
    const auto profile=Handle(Geom_BSplineCurve)::DownCast(u?surface->UIso(u0):surface->VIso(v0));
    if(profile.IsNull())return false;
    const auto projected=Handle(Geom_BSplineCurve)::DownCast(profile->Copy());
    for(int i=1;i<=projected->NbPoles();++i)
    {auto p=projected->Pole(i);p.SetX(0.);projected->SetPole(i,p);}
    const double first=u?v0:u0,last=u?v1:u1;
    const gp_Pln yz(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(1,0,0),gp_Dir(0,1,0)));
    std::vector<std::pair<double,double>> intervals;std::set<std::uint64_t> sourceEdges;
    struct ProfileWitness final {double First=0.,Last=0.,Bound=0.;bool bReversed=false;
        std::string Method;std::set<std::pair<double,double>> FiniteIntervals;
        std::vector<std::pair<double,double>> ParameterPairs;};
    std::map<std::uint64_t,ProfileWitness> parameterMaps;
    double maximumBound=0.,minimumAllowed=std::numeric_limits<double>::infinity();
    for(const auto& item:section)
    {
        if(item.SourceNativeEdge.IsNull()||!std::isfinite(item.DeclaredTolerance)||item.DeclaredTolerance<=0.)continue;
        bool own=false;
        for(TopExp_Explorer edges(face,TopAbs_EDGE);edges.More();edges.Next())
            if(item.SourceNativeEdge.IsSame(edges.Current())){own=true;break;}
        if(!own)continue;
        const auto basis=Handle(Geom2d_BSplineCurve)::DownCast(skin_spans::Basis(item.Curve));
        if(basis.IsNull())continue;
        FiniteEdge target;target.Curve=projected;target.First=first;target.Last=last;
        target.DeclaredTolerance=item.DeclaredTolerance;
        FiniteEdge witness;witness.Id=item.SourceEdgeId;witness.Curve=GeomAPI::To3d(basis,yz);
        witness.First=basis->FirstParameter();witness.Last=basis->LastParameter();witness.DeclaredTolerance=item.DeclaredTolerance;
        NativeSkinBoundaryCertificate proof;if(!CertifyNativeSkinBoundary(target,witness,proof))continue;
        const auto map=[&](double parameter){
            for(const auto& pair:proof.ParameterPairs)if(parameter==pair.second)return pair.first;
            for(std::size_t i=1;i<proof.ParameterPairs.size();++i)
            {
                const auto& a=proof.ParameterPairs[i-1];const auto& b=proof.ParameterPairs[i];
                if(parameter>std::min(a.second,b.second)&&parameter<std::max(a.second,b.second))
                    return a.first+(parameter-a.second)/(b.second-a.second)*(b.first-a.first);
            }
            return std::numeric_limits<double>::quiet_NaN();
        };
        const double a=map(item.First),b=map(item.Last);
        if(!std::isfinite(a)||!std::isfinite(b))continue;
        const double lo=std::max(first,std::min(a,b)),hi=std::min(last,std::max(a,b));
        if(!(hi>lo))continue;
        intervals.emplace_back(lo,hi);sourceEdges.insert(item.SourceEdgeId);
        auto& mapping=parameterMaps[item.SourceEdgeId];mapping.First=witness.First;mapping.Last=witness.Last;
        mapping.Bound=std::max(mapping.Bound,proof.Bound);mapping.bReversed=proof.bReversed;mapping.Method=proof.Method;
        mapping.ParameterPairs=proof.ParameterPairs;
        mapping.FiniteIntervals.emplace(item.First,item.Last);
        maximumBound=std::max(maximumBound,proof.Bound);minimumAllowed=std::min(minimumAllowed,proof.AcceptedTolerance);
    }
    std::sort(intervals.begin(),intervals.end());double covered=first;
    for(const auto& interval:intervals)
    {if(interval.first>covered)return false;covered=std::max(covered,interval.second);}
    if(covered<last)return false;
    if(nativeProof)
    {
        std::ostringstream proof;proof<<std::setprecision(17)<<"strict-native-x-generator-and-finite-section-common-basis;profile-range="
            <<first<<","<<last<<";section-edges=";
        for(const auto edge:sourceEdges)proof<<edge<<",";
        proof<<";profile-certificate-bound-mm="<<maximumBound<<";section-declared-tolerance-mm="<<minimumAllowed;
        for(const auto& [edge,mapping]:parameterMaps)
        {
            proof<<";section-witness-edge="<<edge<<";profile-to-section-native-domain-map="<<first<<","<<last
                <<"=>"<<(mapping.bReversed?mapping.Last:mapping.First)<<","<<(mapping.bReversed?mapping.First:mapping.Last)
                <<";finite-section-witness-native-intervals=";
            for(const auto& range:mapping.FiniteIntervals)proof<<"["<<range.first<<","<<range.second<<"],";
            proof<<";profile-witness-bound-mm="<<mapping.Bound<<";profile-witness-method="<<mapping.Method;
            proof<<";profile-to-section-native-event-pairs=";
            for(const auto& pair:mapping.ParameterPairs)proof<<"["<<pair.first<<","<<pair.second<<"],";
        }
        *nativeProof=proof.str();
    }
    return true;
}
