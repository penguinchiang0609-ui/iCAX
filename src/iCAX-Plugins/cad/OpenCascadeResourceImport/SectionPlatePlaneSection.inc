// Included inside plate_exact. Recover a regular plane/surface section from
// the original surface equation, rather than the Boolean's fitted 3D edge.
// Control nets prove applicability; evaluated points are never the answer.
namespace plane_section_detail {
constexpr double RootAccuracy=1e-12;
constexpr double FitAccuracy=1e-11;
constexpr double EdgeAccuracy=1e-10;

bool Finite(const gp_Pnt& p){return std::isfinite(p.X())&&std::isfinite(p.Y())&&std::isfinite(p.Z());}
bool Finite(const gp_Vec& v){return std::isfinite(v.X())&&std::isfinite(v.Y())&&std::isfinite(v.Z());}
bool InDomain(const gp_Pnt2d& p,double u0,double u1,double v0,double v1){
    return std::isfinite(p.X())&&std::isfinite(p.Y())&&p.X()>=u0&&p.X()<=u1&&p.Y()>=v0&&p.Y()<=v1;
}
void TraceDomain(const char* phase,double parameter,const gp_Pnt2d& p,double u0,double u1,double v0,double v1){
    if(std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<std::setprecision(17)<<"fixture plane section natural domain phase="<<phase
        <<" parameterOrPole="<<parameter<<" uv="<<p.X()<<','<<p.Y()<<" natural="<<u0<<','<<u1<<','<<v0<<','<<v1
        <<" domainExcess="<<u0-p.X()<<','<<p.X()-u1<<','<<v0-p.Y()<<','<<p.Y()-v1<<std::endl;
}

bool ConstantDepth(const TopoDS_Edge& edge,double first,double last,double& depth,double& hullError){
    TopLoc_Location location;double a,b;auto geometry=BRep_Tool::Curve(edge,location,a,b);
    if(geometry.IsNull()||first<a||last>b)return false;
    if(!location.IsIdentity())geometry=occ::down_cast<Geom_Curve>(geometry->Transformed(location.Transformation()));
    const GeomAdaptor_Curve adaptor(geometry,first,last);std::vector<gp_Pnt> poles;
    if(adaptor.GetType()==GeomAbs_Line)poles={geometry->Value(first),geometry->Value(last)};
    else if(adaptor.GetType()==GeomAbs_BSplineCurve){
        auto curve=occ::down_cast<Geom_BSplineCurve>(adaptor.BSpline()->Copy());curve->Segment(first,last);
        for(int i=1;i<=curve->NbPoles();++i){if(!(curve->Weight(i)>0)||!std::isfinite(curve->Weight(i)))return false;poles.push_back(curve->Pole(i));}
    }else if(adaptor.GetType()==GeomAbs_BezierCurve){
        auto curve=occ::down_cast<Geom_BezierCurve>(adaptor.Bezier()->Copy());curve->Segment(first,last);
        for(int i=1;i<=curve->NbPoles();++i){if(!(curve->Weight(i)>0)||!std::isfinite(curve->Weight(i)))return false;poles.push_back(curve->Pole(i));}
    }else return false;
    double low=INFINITY,high=-INFINITY,scale=1.;
    for(const auto& pole:poles){if(!Finite(pole))return false;low=std::min(low,pole.Z());high=std::max(high,pole.Z());scale=std::max({scale,std::abs(pole.X()),std::abs(pole.Y()),std::abs(pole.Z())});}
    // Positive rational weights preserve the convex hull. Only arithmetic
    // roundoff is admitted here, never the source edge/vertex tolerance.
    const double width=high-low;
    if(!std::isfinite(low)||!std::isfinite(width)||width>64*std::numeric_limits<double>::epsilon()*scale)return false;
    depth=low+width/2;hullError=std::max(depth-low,high-depth);
    // Large XY coordinates must never admit a geometrically nonplanar edge
    // merely by enlarging the roundoff allowance. Account for even this
    // bounded depth uncertainty again, in space, after proving the chart.
    return std::isfinite(depth)&&std::isfinite(hullError)&&hullError>=0&&hullError<=EdgeAccuracy;
}

bool ParameterSeed(const occ::handle<Geom2d_Curve>& geometry,double first,double last,occ::handle<Geom2d_Curve>& seed,std::vector<gp_Pnt2d>& poles){
    const Geom2dAdaptor_Curve adaptor(geometry,first,last);
    if(adaptor.GetType()==GeomAbs_Line){seed=occ::down_cast<Geom2d_Curve>(geometry->Copy());poles={seed->Value(first),seed->Value(last)};}
    else{
        occ::handle<Geom2d_BSplineCurve> curve;
        if(adaptor.GetType()==GeomAbs_BSplineCurve)curve=occ::down_cast<Geom2d_BSplineCurve>(adaptor.BSpline()->Copy());
        else if(adaptor.GetType()==GeomAbs_BezierCurve){
            // A Bezier Segment reparameterizes its result to [0,1]. Convert
            // its Bernstein basis exactly to a clamped B-spline first, so
            // subsequent knot insertion preserves the original parameter t.
            const auto bezier=adaptor.Bezier();const int count=bezier->NbPoles(),degree=bezier->Degree();
            if(first<bezier->FirstParameter()||last>bezier->LastParameter())return false;
            NCollection_Array1<gp_Pnt2d> points(1,count);NCollection_Array1<double> weights(1,count),knots(1,2);NCollection_Array1<int> multiplicities(1,2);
            for(int i=1;i<=count;++i){if(!(bezier->Weight(i)>0)||!std::isfinite(bezier->Weight(i)))return false;points(i)=bezier->Pole(i);weights(i)=bezier->Weight(i);}
            knots(1)=bezier->FirstParameter();knots(2)=bezier->LastParameter();multiplicities(1)=degree+1;multiplicities(2)=degree+1;
            curve=new Geom2d_BSplineCurve(points,weights,knots,multiplicities,degree,false);
        }else return false;
        // The same private, exactly trimmed representation supplies BOTH
        // the convex-hull proof and all equation evaluations. Using its
        // control net while evaluating the old basis could disagree at an
        // endpoint by floating-point cancellation.
        curve->Segment(first,last);
        Require(!curve->IsPeriodic()&&curve->FirstParameter()==first&&curve->LastParameter()==last
            &&curve->Multiplicity(1)==curve->Degree()+1&&curve->Multiplicity(curve->NbKnots())==curve->Degree()+1,"PLATE_PLANE_SECTION_SEED_REPRESENTATION");
        seed=curve;
        for(int i=1;i<=curve->NbPoles();++i){if(!(curve->Weight(i)>0)||!std::isfinite(curve->Weight(i)))return false;poles.push_back(curve->Pole(i));}
    }
    for(const auto& p:poles)if(!std::isfinite(p.X())||!std::isfinite(p.Y()))return false;
    return !poles.empty();
}

struct DerivativeBound {
    double Low=INFINITY,High=-INFINITY,Norm=0;
    void Add(const gp_Vec& value){Require(Finite(value),"PLATE_PLANE_SECTION_CONTROL_INVALID");Low=std::min(Low,value.Z());High=std::max(High,value.Z());Norm=std::max(Norm,value.Magnitude());}
    int Sign()const{
        const double roundoff=64*std::numeric_limits<double>::epsilon()*std::max(1.,Norm);
        return Low>roundoff?1:High<-roundoff?-1:0;
    }
    double MinimumDepth()const{return Sign()?std::min(std::abs(Low),std::abs(High)):0;}
    double Quality()const{return Sign()?std::min(std::abs(Low),std::abs(High))/Norm:0;}
};

bool SurfaceBounds(const occ::handle<Adaptor3d_Surface>& surface,DerivativeBound& du,DerivativeBound& dv,const Check& check){
    if(surface->GetType()==GeomAbs_BSplineSurface){
        const auto geometry=surface->BSpline();
        if(geometry->IsUPeriodic()||geometry->IsVPeriodic()||geometry->IsURational()||geometry->IsVRational())return false;
        const double weight=geometry->Weight(1,1);if(!(weight>0)||!std::isfinite(weight))return false;
        for(int i=1;i<=geometry->NbUPoles();++i){check();for(int j=1;j<=geometry->NbVPoles();++j)if(geometry->Weight(i,j)!=weight||!Finite(geometry->Pole(i,j)))return false;}
        const auto& ku=geometry->UKnotSequence();const auto& kv=geometry->VKnotSequence();
        const int pu=geometry->UDegree(),pv=geometry->VDegree();
        for(int i=1;i<geometry->NbUPoles();++i){check();const double span=ku(ku.Lower()+i+pu)-ku(ku.Lower()+i);if(!(span>0))return false;
            for(int j=1;j<=geometry->NbVPoles();++j)du.Add(gp_Vec(geometry->Pole(i,j),geometry->Pole(i+1,j))*(pu/span));}
        for(int j=1;j<geometry->NbVPoles();++j){check();const double span=kv(kv.Lower()+j+pv)-kv(kv.Lower()+j);if(!(span>0))return false;
            for(int i=1;i<=geometry->NbUPoles();++i)dv.Add(gp_Vec(geometry->Pole(i,j),geometry->Pole(i,j+1))*(pv/span));}
    }else if(surface->GetType()==GeomAbs_BezierSurface){
        const auto geometry=surface->Bezier();if(geometry->IsURational()||geometry->IsVRational())return false;
        const double weight=geometry->Weight(1,1);if(!(weight>0)||!std::isfinite(weight))return false;
        for(int i=1;i<=geometry->NbUPoles();++i){check();for(int j=1;j<=geometry->NbVPoles();++j)if(geometry->Weight(i,j)!=weight||!Finite(geometry->Pole(i,j)))return false;}
        for(int i=1;i<geometry->NbUPoles();++i)for(int j=1;j<=geometry->NbVPoles();++j)du.Add(gp_Vec(geometry->Pole(i,j),geometry->Pole(i+1,j))*geometry->UDegree());
        for(int j=1;j<geometry->NbVPoles();++j)for(int i=1;i<=geometry->NbUPoles();++i)dv.Add(gp_Vec(geometry->Pole(i,j),geometry->Pole(i,j+1))*geometry->VDegree());
    }else return false;
    return du.Sign()!=0||dv.Sign()!=0;
}

struct NaturalRange {double First=0,Last=0,InitialDependent=0,Speed=0;bool Reversed=false;};
struct PrecisionEvidence {
    double First=0,Last=0,UNorm=0,VNorm=0,DepthHull=0,DepthSpatial=0;
    double Raw3d=std::numeric_limits<double>::quiet_NaN(),RawU=Raw3d,RawV=Raw3d;
    double Added3d=0,AddedU=0,AddedV=0,Pair=Raw3d,Total=Raw3d;
    const char* Mode="not-selected";
    bool Reversed=false;
    size_t PieceIndex=0,PieceCount=1;
    std::vector<double> UKnots,VKnots;
    void Trace(const char* phase)const{
        if(std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<std::setprecision(17)<<"fixture plane section precision failure phase="<<phase
            <<" parameter="<<Mode<<" reversedParameter="<<Reversed<<" piece="<<PieceIndex<<'/'<<PieceCount<<" range="<<First<<','<<Last
            <<" raw3d="<<Raw3d<<" rawUV="<<RawU<<','<<RawV<<" added3d="<<Added3d<<" addedUV="<<AddedU<<','<<AddedV
            <<" derivativeNormUV="<<UNorm<<','<<VNorm<<" totalPair="<<Pair<<" depthHull="<<DepthHull<<" depthSpatial="<<DepthSpatial
            <<" rootBudget="<<RootAccuracy<<" totalBudget="<<Total<<" raw3dLimit="<<FitAccuracy<<" totalLimit="<<EdgeAccuracy;
        if(std::getenv("ICAX_FIXTURE_TRACE")){
            std::cerr<<" sourceUKnots=";for(double value:UKnots)std::cerr<<value<<',';
            std::cerr<<" sourceVKnots=";for(double value:VKnots)std::cerr<<value<<',';std::cerr<<std::endl;
        }
    }
};
bool NaturalParameters(const occ::handle<Geom2d_Curve>& seed,const occ::handle<Adaptor3d_Surface>& surface,bool solveU,double depth,
    const DerivativeBound& dependent,const DerivativeBound& fixed,NaturalRange& range){
    const auto curve=occ::down_cast<Geom2d_BSplineCurve>(seed);if(curve.IsNull())return false;
    const double weight=curve->Weight(1);if(!(weight>0)||!std::isfinite(weight))return false;
    const auto coordinate=[&](const gp_Pnt2d& p){return solveU?p.Y():p.X();};
    const double begin=coordinate(curve->Pole(1)),end=coordinate(curve->Pole(curve->NbPoles()));
    if(!std::isfinite(begin)||!std::isfinite(end)||begin==end)return false;
    const int direction=end>begin?1:-1;double previous=begin,low=INFINITY,high=-INFINITY;
    for(int i=1;i<=curve->NbPoles();++i){
        if(curve->Weight(i)!=weight)return false;
        const auto p=curve->Pole(i);const double value=coordinate(p);
        if(direction*(value-previous)<0)return false;previous=value;
        low=std::min(low,solveU?p.X():p.Y());high=std::max(high,solveU?p.X():p.Y());
    }
    // A monotone control sequence proves that the continuous retained
    // coordinate has exactly the endpoint interval as its image. Verify
    // existence of the graph throughout that image as well as uniqueness:
    // the natural dependent-domain end curves must bracket the plane.
    const int sign=dependent.Sign();const auto bracketed=[&](const gp_Pnt& a,const gp_Pnt& b){return sign*(a.Z()-depth)<=0&&sign*(b.Z()-depth)>=0;};
    if(surface->GetType()==GeomAbs_BSplineSurface){
        const auto geometry=surface->BSpline();double u0,u1,v0,v1;geometry->Bounds(u0,u1,v0,v1);
        if(solveU){
            if(geometry->UMultiplicity(1)!=geometry->UDegree()+1||geometry->UMultiplicity(geometry->NbUKnots())!=geometry->UDegree()+1
                ||surface->FirstUParameter()!=u0||surface->LastUParameter()!=u1)return false;
            for(int j=1;j<=geometry->NbVPoles();++j)if(!bracketed(geometry->Pole(1,j),geometry->Pole(geometry->NbUPoles(),j)))return false;
        }else{
            if(geometry->VMultiplicity(1)!=geometry->VDegree()+1||geometry->VMultiplicity(geometry->NbVKnots())!=geometry->VDegree()+1
                ||surface->FirstVParameter()!=v0||surface->LastVParameter()!=v1)return false;
            for(int i=1;i<=geometry->NbUPoles();++i)if(!bracketed(geometry->Pole(i,1),geometry->Pole(i,geometry->NbVPoles())))return false;
        }
    }else if(surface->GetType()==GeomAbs_BezierSurface){
        const auto geometry=surface->Bezier();
        if(solveU){
            if(surface->FirstUParameter()!=0||surface->LastUParameter()!=1)return false;
            for(int j=1;j<=geometry->NbVPoles();++j)if(!bracketed(geometry->Pole(1,j),geometry->Pole(geometry->NbUPoles(),j)))return false;
        }else{
            if(surface->FirstVParameter()!=0||surface->LastVParameter()!=1)return false;
            for(int i=1;i<=geometry->NbUPoles();++i)if(!bracketed(geometry->Pole(i,1),geometry->Pole(i,geometry->NbVPoles())))return false;
        }
    }else return false;
    range.First=std::min(begin,end);range.Last=std::max(begin,end);range.Reversed=end<begin;range.InitialDependent=low+(high-low)/2;
    range.Speed=fixed.Norm+dependent.Norm*(std::max(std::abs(fixed.Low),std::abs(fixed.High))/dependent.MinimumDepth());
    return std::isfinite(range.InitialDependent)&&std::isfinite(range.Speed)&&range.Speed>0;
}

struct KnotPartition {std::vector<double> Events,UKnots,VKnots;};
KnotPartition NaturalPartition(const occ::handle<Adaptor3d_Surface>& surface,bool solveU,double depth,double first,double last,const Check& check){
    KnotPartition result;result.Events={first,last};
    if(surface->GetType()!=GeomAbs_BSplineSurface)return result;
    // Cache the transformed geometry once, not once per iso/crossing.
    const auto geometry=surface->BSpline();
    for(int i=1;i<=geometry->NbUKnots();++i)result.UKnots.push_back(geometry->UKnot(i));
    for(int i=1;i<=geometry->NbVKnots();++i)result.VKnots.push_back(geometry->VKnot(i));
    const auto& independent=solveU?result.VKnots:result.UKnots;
    for(double knot:independent)if(knot>first&&knot<last)result.Events.push_back(knot);
    const auto& dependent=solveU?result.UKnots:result.VKnots;
    const double low=solveU?surface->FirstUParameter():surface->FirstVParameter(),high=solveU?surface->LastUParameter():surface->LastVParameter();
    const occ::handle<Geom_Surface> plane=new Geom_Plane(gp_Pnt(0,0,depth),gp_Dir(0,0,1));
    const auto add=[&](double value){
        const bool valid=std::isfinite(value)&&value>=first&&value<=last;
        if(!valid&&std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<std::setprecision(17)<<"fixture plane section knot parameter="<<value<<" range="<<first<<','<<last<<std::endl;
        Require(valid,"PLATE_PLANE_SECTION_KNOT_PARAMETER");if(value>first&&value<last)result.Events.push_back(value);
    };
    for(double knot:dependent){
        check();if(knot<=low||knot>=high)continue;
        const auto iso=solveU?geometry->UIso(knot):geometry->VIso(knot);
        auto trimmed=occ::down_cast<Geom_BSplineCurve>(iso);
        Require(!trimmed.IsNull(),"PLATE_PLANE_SECTION_KNOT_ISO");trimmed->Segment(first,last);
        double z0=INFINITY,z1=-INFINITY;
        for(int i=1;i<=trimmed->NbPoles();++i){
            Require(trimmed->Weight(i)>0&&std::isfinite(trimmed->Weight(i))&&Finite(trimmed->Pole(i)),"PLATE_PLANE_SECTION_KNOT_ISO");
            z0=std::min(z0,trimmed->Pole(i).Z());z1=std::max(z1,trimmed->Pole(i).Z());
        }
        // Positive-control-net hull rejection is a proof of no crossing,
        // including knots beyond the old, only approximate pcurve hull.
        if(z0>depth||z1<depth)continue;
        // A whole iso lying in the plane contributes only the already
        // present range endpoints; the graph follows this knot boundary.
        if(z0==depth&&z1==depth)continue;
        GeomAPI_IntCS intersection(trimmed,plane);check();Require(intersection.IsDone(),"PLATE_PLANE_SECTION_KNOT_INTERSECTION");
        for(int i=1;i<=intersection.NbPoints();++i){double u,v,t;intersection.Parameters(i,u,v,t);add(t);}
        for(int i=1;i<=intersection.NbSegments();++i){const auto segment=intersection.Segment(i);Require(!segment.IsNull(),"PLATE_PLANE_SECTION_KNOT_INTERSECTION");add(segment->FirstParameter());add(segment->LastParameter());}
    }
    std::sort(result.Events.begin(),result.Events.end());result.Events.erase(std::unique(result.Events.begin(),result.Events.end()),result.Events.end());
    return result;
}

class Equation final : public Adaptor2d_Curve2d {
    occ::handle<Adaptor3d_Surface> Surface;
    occ::handle<Geom2d_Curve> Seed;
    double First,Last,DomainFirst,DomainLast,Depth,U0,U1,V0,V1,MinimumDepthDerivative,MaximumDerivative,InitialDependent,NaturalSpeed;
    bool SolveU,NaturalParameter;
    int Direction;
    Check Cancel;
public:
    Equation(const occ::handle<Adaptor3d_Surface>& surface,const occ::handle<Geom2d_Curve>& seed,double first,double last,double depth,bool solveU,const DerivativeBound& bound,bool naturalParameter,const NaturalRange& range,const Check& check)
        :Surface(surface),Seed(seed),First(first),Last(last),DomainFirst(first),DomainLast(last),Depth(depth),U0(surface->FirstUParameter()),U1(surface->LastUParameter()),V0(surface->FirstVParameter()),V1(surface->LastVParameter()),MinimumDepthDerivative(bound.MinimumDepth()),MaximumDerivative(bound.Norm),InitialDependent(range.InitialDependent),NaturalSpeed(range.Speed),SolveU(solveU),NaturalParameter(naturalParameter),Direction(bound.Sign()),Cancel(check){
        Require(std::isfinite(MinimumDepthDerivative)&&MinimumDepthDerivative>0&&std::isfinite(MaximumDerivative)&&MaximumDerivative>=MinimumDepthDerivative,"PLATE_PLANE_SECTION_CHART_INVALID");
    }
    occ::handle<Adaptor2d_Curve2d> ShallowCopy()const override{auto copy=new Equation(*this);copy->Surface=Surface->ShallowCopy();return copy;}
    occ::handle<Adaptor2d_Curve2d> Trim(double first,double last,double tolerance)const override{
        const bool valid=std::isfinite(first)&&std::isfinite(last)&&first>=DomainFirst&&last<=DomainLast&&last>first;
        // Approx_CurveOnSurface re-trims its previous adaptor to sibling
        // intervals. As with Geom2dAdaptor_Curve, retain the underlying domain
        // rather than requiring each request to nest inside the previous trim.
        if(!valid&&std::getenv("ICAX_FIXTURE_TRACE"))
            std::cerr<<std::setprecision(17)<<"fixture plane section Trim valid="<<valid<<" request="<<first<<','<<last
                <<" current="<<First<<','<<Last<<" domain="<<DomainFirst<<','<<DomainLast<<" tolerance="<<tolerance
                <<" domainExcess="<<DomainFirst-first<<','<<last-DomainLast<<std::endl;
        Require(valid,"PLATE_PLANE_SECTION_TRIM_RANGE");auto copy=new Equation(*this);copy->First=first;copy->Last=last;return copy;
    }
    double FirstParameter()const override{return First;}
    double LastParameter()const override{return Last;}
    GeomAbs_Shape Continuity()const override{return GeomAbs_C1;}
    int NbIntervals(GeomAbs_Shape shape)const override{Require(shape<=GeomAbs_C1,"PLATE_PLANE_SECTION_CONTINUITY");return 1;}
    void Intervals(NCollection_Array1<double>& values,GeomAbs_Shape shape)const override{Require(shape<=GeomAbs_C1,"PLATE_PLANE_SECTION_CONTINUITY");values(values.Lower())=First;values(values.Lower()+1)=Last;}
    bool IsClosed()const override{return false;}
    bool IsPeriodic()const override{return false;}
    GeomAbs_CurveType GetType()const override{return GeomAbs_OtherCurve;}
    double Resolution(double r)const override{return NaturalParameter?r/NaturalSpeed:Geom2dAdaptor_Curve(Seed,First,Last).Resolution(std::min(Surface->UResolution(r),Surface->VResolution(r)));}
    gp_Pnt2d Value(double t)const override{gp_Pnt2d point;D0(t,point);return point;}
    void D0(double t,gp_Pnt2d& point)const override{gp_Vec2d derivative;D1(t,point,derivative);}
    void D1(double t,gp_Pnt2d& uv,gp_Vec2d& tangent)const override{
        Cancel();const bool valid=std::isfinite(t)&&t>=DomainFirst&&t<=DomainLast;
        if(!valid&&std::getenv("ICAX_FIXTURE_TRACE"))
            std::cerr<<std::setprecision(17)<<"fixture plane section D1 t="<<t<<" current="<<First<<','<<Last
                <<" domain="<<DomainFirst<<','<<DomainLast<<" domainExcess="<<DomainFirst-t<<','<<t-DomainLast<<std::endl;
        Require(valid,"PLATE_PLANE_SECTION_EVALUATION_RANGE");gp_Pnt2d seed;double fixed,fixedDerivative,value;
        if(NaturalParameter){fixed=t;fixedDerivative=1;value=InitialDependent;seed=SolveU?gp_Pnt2d(value,fixed):gp_Pnt2d(fixed,value);}
        else{
            gp_Vec2d seedDerivative;Seed->D1(t,seed,seedDerivative);
            if(const auto spline=occ::down_cast<Geom2d_BSplineCurve>(Seed);!spline.IsNull()){
                // Exact clamped endpoint identity, not a coordinate clamp.
                if(t==spline->FirstParameter())seed=spline->Pole(1);
                else if(t==spline->LastParameter())seed=spline->Pole(spline->NbPoles());
            }
            fixed=SolveU?seed.Y():seed.X();fixedDerivative=SolveU?seedDerivative.Y():seedDerivative.X();value=SolveU?seed.X():seed.Y();
        }
        const bool fixedInDomain=std::isfinite(fixed)&&fixed>=(SolveU?V0:U0)&&fixed<=(SolveU?V1:U1);
        if(!fixedInDomain)TraceDomain(SolveU?"equation-fixed-V":"equation-fixed-U",t,seed,U0,U1,V0,V1);
        Require(fixedInDomain,"PLATE_PLANE_SECTION_FIXED_DOMAIN");
        double low=SolveU?U0:V0,high=SolveU?U1:V1;
        const auto at=[&](double value){return Surface->Value(SolveU?value:fixed,SolveU?fixed:value).Z()-Depth;};
        const double lowValue=at(low),highValue=at(high);
        Require(std::isfinite(lowValue)&&std::isfinite(highValue)&&Direction*lowValue<=0&&Direction*highValue>=0,"PLATE_PLANE_SECTION_ROOT_OUTSIDE_DOMAIN");
        if(!std::isfinite(value)||value<low||value>high)value=low+(high-low)/2;
        for(int iteration=0;iteration<100;++iteration){
            Cancel();uv=SolveU?gp_Pnt2d(value,fixed):gp_Pnt2d(fixed,value);gp_Pnt point;gp_Vec du,dv;Surface->D1(uv.X(),uv.Y(),point,du,dv);
            const auto dependent=SolveU?du:dv;const double divisor=dependent.Z(),residual=point.Z()-Depth;
            Require(Finite(point)&&Finite(du)&&Finite(dv)&&Direction*divisor>0,"PLATE_PLANE_SECTION_CHART_INVALID");
            const double step=residual/divisor,correction=std::abs(step)*dependent.Magnitude();
            // The derivative control net proves |F'|>=m and ||S'||<=L
            // everywhere in the natural domain. Thus residual*L/m bounds
            // the spatial error to the UNIQUE root, unlike a local Newton
            // correction. The maintained bracket supplies a second bound.
            const double residualBound=std::abs(residual)*(MaximumDerivative/MinimumDepthDerivative);
            const double bracketBound=(high-low)*MaximumDerivative;
            const double rootBound=std::min(residualBound,bracketBound);
            Require(std::isfinite(step)&&std::isfinite(correction)&&std::isfinite(residualBound)&&std::isfinite(bracketBound),"PLATE_PLANE_SECTION_ROOT_FAILED");
            if(std::abs(residual)<=RootAccuracy&&correction<=RootAccuracy&&rootBound<=RootAccuracy){
                const double solvedDerivative=-(SolveU?dv.Z():du.Z())*fixedDerivative/divisor;
                Require(std::isfinite(fixedDerivative)&&std::isfinite(solvedDerivative),"PLATE_PLANE_SECTION_DERIVATIVE_FAILED");
                tangent=SolveU?gp_Vec2d(solvedDerivative,fixedDerivative):gp_Vec2d(fixedDerivative,solvedDerivative);return;
            }
            if(Direction*residual<0)low=value;else high=value;
            double next=value-step;if(!std::isfinite(next)||next<=low||next>=high)next=low+(high-low)/2;
            Require(next!=value,"PLATE_PLANE_SECTION_ROOT_FAILED");value=next;
        }
        throw std::runtime_error("PLATE_PLANE_SECTION_ROOT_FAILED");
    }
};

// Keep even adjacent-ULP source knot intervals. Approximation works in a
// well-scaled parameter, while the original equation is still evaluated at
// s=a+(b-a)*tau. No interval is omitted and no geometric chord is introduced.
class UnitParameter final : public Adaptor2d_Curve2d {
    occ::handle<Adaptor2d_Curve2d> Curve;
    double A,B,Span,First=0,Last=1;
    double Parameter(double tau)const{
        const bool valid=std::isfinite(tau)&&tau>=0&&tau<=1;
        if(!valid&&std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<std::setprecision(17)<<"fixture plane section unit evaluation tau="<<tau
            <<" current="<<First<<','<<Last<<" source="<<A<<','<<B<<std::endl;
        Require(valid,"PLATE_PLANE_SECTION_UNIT_PARAMETER_RANGE");
        if(tau==0)return A;if(tau==1)return B;
        const double value=std::fma(Span,tau,A);
        Require(std::isfinite(value)&&value>=A&&value<=B,"PLATE_PLANE_SECTION_UNIT_MAPPING");return value;
    }
public:
    UnitParameter(const occ::handle<Adaptor2d_Curve2d>& curve,double first,double last):Curve(curve),A(first),B(last),Span(last-first){
        Require(std::isfinite(A)&&std::isfinite(B)&&std::isfinite(Span)&&Span>0,"PLATE_PLANE_SECTION_UNIT_PARAMETER_RANGE");
    }
    occ::handle<Adaptor2d_Curve2d> ShallowCopy()const override{auto copy=new UnitParameter(*this);copy->Curve=Curve->ShallowCopy();return copy;}
    occ::handle<Adaptor2d_Curve2d> Trim(double first,double last,double tolerance)const override{
        const bool valid=std::isfinite(first)&&std::isfinite(last)&&first>=0&&last<=1&&last>first;
        if(!valid&&std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<std::setprecision(17)<<"fixture plane section unit Trim request="<<first<<','<<last
            <<" current="<<First<<','<<Last<<" source="<<A<<','<<B<<" tolerance="<<tolerance<<std::endl;
        Require(valid,"PLATE_PLANE_SECTION_UNIT_TRIM_RANGE");auto copy=new UnitParameter(*this);copy->First=first;copy->Last=last;return copy;
    }
    double FirstParameter()const override{return First;}
    double LastParameter()const override{return Last;}
    GeomAbs_Shape Continuity()const override{return Curve->Continuity();}
    int NbIntervals(GeomAbs_Shape shape)const override{Require(shape<=GeomAbs_C1,"PLATE_PLANE_SECTION_CONTINUITY");return 1;}
    void Intervals(NCollection_Array1<double>& values,GeomAbs_Shape shape)const override{Require(shape<=GeomAbs_C1,"PLATE_PLANE_SECTION_CONTINUITY");values(values.Lower())=First;values(values.Lower()+1)=Last;}
    bool IsClosed()const override{return false;}
    bool IsPeriodic()const override{return false;}
    GeomAbs_CurveType GetType()const override{return GeomAbs_OtherCurve;}
    double Resolution(double r)const override{return Curve->Resolution(r)/Span;}
    gp_Pnt2d Value(double tau)const override{gp_Pnt2d point;D0(tau,point);return point;}
    void D0(double tau,gp_Pnt2d& point)const override{Curve->D0(Parameter(tau),point);}
    void D1(double tau,gp_Pnt2d& point,gp_Vec2d& derivative)const override{Curve->D1(Parameter(tau),point,derivative);derivative*=Span;}
};
}

std::vector<TopoDS_Edge> RegularPlaneSections(const TopoDS_Edge& edge,const TopoDS_Face& face,const Check& check){
    using namespace plane_section_detail;check();
    const occ::handle<Adaptor3d_Surface> surface=new BRepAdaptor_Surface(face,false);
    if(surface->GetType()!=GeomAbs_BSplineSurface&&surface->GetType()!=GeomAbs_BezierSurface)return {edge};
    if(surface->UContinuity()<GeomAbs_C1||surface->VContinuity()<GeomAbs_C1)return {edge};
    double sectionFirst,sectionLast;const auto pc=BRep_Tool::CurveOnSurface(edge,face,sectionFirst,sectionLast);
    if(pc.IsNull()||!std::isfinite(sectionFirst)||!std::isfinite(sectionLast)||!(sectionLast>sectionFirst)||Geom2dAdaptor_Curve(pc,sectionFirst,sectionLast).Continuity()<GeomAbs_C1)return {edge};
    double depth,depthHullError;if(!ConstantDepth(edge,sectionFirst,sectionLast,depth,depthHullError))return {edge};
    const double u0=surface->FirstUParameter(),u1=surface->LastUParameter(),v0=surface->FirstVParameter(),v1=surface->LastVParameter();
    if(!std::isfinite(u0)||!std::isfinite(u1)||!std::isfinite(v0)||!std::isfinite(v1)||!(u1>u0)||!(v1>v0))return {edge};
    DerivativeBound du,dv;if(!SurfaceBounds(surface,du,dv,check))return {edge};
    const bool solveU=du.Quality()>=dv.Quality();const auto& dependent=solveU?du:dv;
    const double depthError=depthHullError*(dependent.Norm/dependent.MinimumDepth());
    PrecisionEvidence preEvidence;preEvidence.First=sectionFirst;preEvidence.Last=sectionLast;preEvidence.UNorm=du.Norm;preEvidence.VNorm=dv.Norm;preEvidence.DepthHull=depthHullError;preEvidence.DepthSpatial=depthError;
    const bool depthCertified=std::isfinite(depthError)&&depthError+RootAccuracy<=EdgeAccuracy;
    if(!depthCertified){preEvidence.Total=depthError+RootAccuracy;preEvidence.Trace("depth-budget");}Require(depthCertified,"PLATE_PLANE_SECTION_PRECISION");
    occ::handle<Geom2d_Curve> seed;std::vector<gp_Pnt2d> controls;if(!ParameterSeed(pc,sectionFirst,sectionLast,seed,controls))return {edge};
    for(const auto& p:controls){const double fixed=solveU?p.Y():p.X();if(fixed<(solveU?v0:u0)||fixed>(solveU?v1:u1))return {edge};}
    NaturalRange range;const bool natural=NaturalParameters(seed,surface,solveU,depth,dependent,solveU?dv:du,range);
    if(natural){sectionFirst=range.First;sectionLast=range.Last;}
    KnotPartition partition;if(natural)partition=NaturalPartition(surface,solveU,depth,sectionFirst,sectionLast,check);else partition.Events={sectionFirst,sectionLast};
    const auto fitPiece=[&](double first,double last,size_t index){
    auto evidence=plane_section_detail::PrecisionEvidence{};evidence.First=first;evidence.Last=last;evidence.UNorm=du.Norm;evidence.VNorm=dv.Norm;
    evidence.DepthHull=depthHullError;evidence.DepthSpatial=depthError;evidence.Mode=natural?"natural":"original";evidence.Reversed=natural&&range.Reversed;
    evidence.UKnots=partition.UKnots;evidence.VKnots=partition.VKnots;evidence.PieceIndex=index;evidence.PieceCount=partition.Events.size()-1;
    const occ::handle<Adaptor2d_Curve2d> sourceEquation=new Equation(surface,seed,first,last,depth,solveU,dependent,natural,range,check);
    const occ::handle<Adaptor2d_Curve2d> equation=new UnitParameter(sourceEquation,first,last);
    // These are evaluations of the original surface equation, not endpoint
    // snapping. Global strict monotonicity makes every bracketed root unique.
    const auto firstUV=equation->Value(0),lastUV=equation->Value(1);
    if(!InDomain(firstUV,u0,u1,v0,v1))TraceDomain("equation-first-endpoint",first,firstUV,u0,u1,v0,v1);
    if(!InDomain(lastUV,u0,u1,v0,v1))TraceDomain("equation-last-endpoint",last,lastUV,u0,u1,v0,v1);
    Require(InDomain(firstUV,u0,u1,v0,v1)&&InDomain(lastUV,u0,u1,v0,v1),"PLATE_PLANE_SECTION_ENDPOINT_DOMAIN");
    Approx_CurveOnSurface approximation(equation,surface,0,1,FitAccuracy);approximation.Perform(512,14,GeomAbs_C1);check();
    const bool fitted=approximation.IsDone()&&!approximation.Curve3d().IsNull()&&!approximation.Curve2d().IsNull();
    if(!fitted)evidence.Trace("piece-approximation-not-done");Require(fitted,"PLATE_PLANE_SECTION_APPROXIMATION_FAILED");
    const double rawError3d=approximation.MaxError3d(),rawErrorU=approximation.MaxError2dU(),rawErrorV=approximation.MaxError2dV();
    evidence.Raw3d=rawError3d;evidence.RawU=rawErrorU;evidence.RawV=rawErrorV;
    const bool rawCertified=std::isfinite(rawError3d)&&rawError3d>=0&&rawError3d<=FitAccuracy&&std::isfinite(rawErrorU)&&rawErrorU>=0&&std::isfinite(rawErrorV)&&rawErrorV>=0;
    if(!rawCertified){evidence.Pair=rawError3d+du.Norm*rawErrorU+dv.Norm*rawErrorV;evidence.Total=evidence.Pair+RootAccuracy+depthError;evidence.Trace("raw-fit");}Require(rawCertified,"PLATE_PLANE_SECTION_PRECISION");
    auto curve3d=occ::down_cast<Geom_BSplineCurve>(approximation.Curve3d()->Copy());
    auto curve2d=occ::down_cast<Geom2d_BSplineCurve>(approximation.Curve2d()->Copy());
    bool outside=false,onlyEndpoints=true;
    for(int i=1;i<=curve3d->NbPoles();++i)Require(Finite(curve3d->Pole(i))&&std::isfinite(curve3d->Weight(i))&&curve3d->Weight(i)>0,"PLATE_PLANE_SECTION_CURVE_INVALID");
    for(int i=1;i<=curve2d->NbPoles();++i){
        Require(std::isfinite(curve2d->Weight(i))&&curve2d->Weight(i)>0&&std::isfinite(curve2d->Pole(i).X())&&std::isfinite(curve2d->Pole(i).Y()),"PLATE_PLANE_SECTION_PCURVE_INVALID");
        if(InDomain(curve2d->Pole(i),u0,u1,v0,v1))continue;
        TraceDomain("fitted-pcurve-control",i,curve2d->Pole(i),u0,u1,v0,v1);outside=true;
        if(i!=1&&i!=curve2d->NbPoles())onlyEndpoints=false;
    }
    double added3d=0,addedU=0,addedV=0;
    if(outside){
        // An outlying interior pole needs a stronger domain proof; endpoint
        // interpolation cannot establish it. Never clamp the fitted UV curve.
        Require(onlyEndpoints,"PLATE_PLANE_SECTION_PCURVE_DOMAIN");
        const bool clamped3d=!curve3d->IsPeriodic()&&curve3d->FirstParameter()==0&&curve3d->LastParameter()==1
            &&curve3d->Multiplicity(1)==curve3d->Degree()+1&&curve3d->Multiplicity(curve3d->NbKnots())==curve3d->Degree()+1;
        const bool clamped2d=!curve2d->IsPeriodic()&&curve2d->FirstParameter()==0&&curve2d->LastParameter()==1
            &&curve2d->Multiplicity(1)==curve2d->Degree()+1&&curve2d->Multiplicity(curve2d->NbKnots())==curve2d->Degree()+1;
        Require(clamped3d&&clamped2d,"PLATE_PLANE_SECTION_ENDPOINT_REPRESENTATION");
        const auto firstPoint=surface->Value(firstUV.X(),firstUV.Y()),lastPoint=surface->Value(lastUV.X(),lastUV.Y());
        Require(Finite(firstPoint)&&Finite(lastPoint)&&std::abs(firstPoint.Z()-depth)<=RootAccuracy&&std::abs(lastPoint.Z()-depth)<=RootAccuracy,"PLATE_PLANE_SECTION_ENDPOINT_PRECISION");
        const auto interpolate=[&](int i3,int i2,const gp_Pnt2d& uv,const gp_Pnt& point){
            const auto oldUV=curve2d->Pole(i2);added3d=std::max(added3d,curve3d->Pole(i3).Distance(point));
            addedU=std::max(addedU,std::abs(oldUV.X()-uv.X()));addedV=std::max(addedV,std::abs(oldUV.Y()-uv.Y()));
            curve3d->SetPole(i3,point);curve2d->SetPole(i2,uv);
        };
        // For clamped curves the end poles ARE the endpoints. Interpolate the
        // actual surface-equation solutions, never the Boolean's old vertices.
        interpolate(1,1,firstUV,firstPoint);interpolate(curve3d->NbPoles(),curve2d->NbPoles(),lastUV,lastPoint);
        // Positive weights and unchanged knots/weights bound the displacement
        // everywhere by the largest changed-pole displacement (per coordinate).
        if(std::getenv("ICAX_FIXTURE_SECTION_VERBOSE"))std::cerr<<std::setprecision(17)<<"fixture plane section equation endpoint interpolation depth="<<depth
            <<" addedError3d="<<added3d<<" addedErrorUV="<<addedU<<','<<addedV<<std::endl;
    }
    const double error3d=rawError3d+added3d,errorU=rawErrorU+addedU,errorV=rawErrorV+addedV;
    const double pairError=error3d+du.Norm*errorU+dv.Norm*errorV;
    const double certifiedError=pairError+RootAccuracy+depthError;
    evidence.Added3d=added3d;evidence.AddedU=addedU;evidence.AddedV=addedV;evidence.Pair=pairError;evidence.Total=certifiedError;
    const bool pairCertified=std::isfinite(pairError)&&std::isfinite(certifiedError)&&certifiedError<=EdgeAccuracy;
    if(!pairCertified)evidence.Trace("paired-budget");Require(pairCertified,"PLATE_PLANE_SECTION_PRECISION");
    for(int i=1;i<=curve2d->NbPoles();++i){
        const auto p=curve2d->Pole(i);if(!InDomain(p,u0,u1,v0,v1))TraceDomain("final-pcurve-control",i,p,u0,u1,v0,v1);
        Require(InDomain(p,u0,u1,v0,v1),"PLATE_PLANE_SECTION_PCURVE_DOMAIN");
    }
    // New private topology. Each piece uses the SAME parameter on both
    // representations; natural pieces are not concatenated or globally fit.
    BRep_Builder builder;TopoDS_Edge result;builder.MakeEdge(result,curve3d,EdgeAccuracy);
    TopoDS_Vertex a,b;builder.MakeVertex(a,curve3d->Value(0),EdgeAccuracy);builder.MakeVertex(b,curve3d->Value(1),EdgeAccuracy);
    builder.Add(result,a.Oriented(TopAbs_FORWARD));builder.Add(result,b.Oriented(TopAbs_REVERSED));
    builder.UpdateEdge(result,curve2d,face,EdgeAccuracy);builder.Range(result,0,1);builder.Range(result,face,0,1);
    builder.UpdateVertex(a,0,result,EdgeAccuracy);builder.UpdateVertex(b,1,result,EdgeAccuracy);builder.SameRange(result,true);builder.SameParameter(result,true);
    result.Orientation(edge.Orientation());if(natural&&range.Reversed)result.Reverse();Require(BRepCheck_Analyzer(result).IsValid(),"PLATE_PLANE_SECTION_EDGE_INVALID");
    if(std::getenv("ICAX_FIXTURE_SECTION_VERBOSE"))std::cerr<<std::setprecision(17)<<"fixture recovered plane section depth="<<depth<<" solved="<<(solveU?'U':'V')
        <<" error3d="<<error3d<<" sameParameterErrorBound="<<pairError<<" depthHullError="<<depthHullError<<" depthSpatialError="<<depthError
        <<" certifiedError="<<certifiedError<<" parameter="<<(natural?"natural":"original")<<" reversedParameter="<<(natural&&range.Reversed)<<std::endl;
    return result;
    };
    std::vector<TopoDS_Edge> result;result.reserve(partition.Events.size()-1);
    for(size_t i=1;i<partition.Events.size();++i){
        check();Require(partition.Events[i]>partition.Events[i-1],"PLATE_PLANE_SECTION_KNOT_PARAMETER");
        // Distinct knot/event values, even adjacent ULPs, are never merged.
        // Fit that exact interval or preserve the failure with its span.
        try{result.push_back(fitPiece(partition.Events[i-1],partition.Events[i],i-1));}
        catch(...){
            if(std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<std::setprecision(17)<<"fixture plane section piece exception index="<<(i-1)
                <<" count="<<(partition.Events.size()-1)<<" span="<<partition.Events[i-1]<<','<<partition.Events[i]
                <<" width="<<partition.Events[i]-partition.Events[i-1]<<std::endl;
            throw;
        }
    }
    if(natural&&range.Reversed)std::reverse(result.begin(),result.end());
    return result;
}
