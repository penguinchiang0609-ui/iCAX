// Included inside plate_exact, after the polynomial and source-patch helpers.
// Discovery is by Bernstein sign/regularity certificates on ORIGINAL knot
// cells, not by a grid of evaluated points. All final evaluations use the
// original surface and its owning knot span.
namespace regular_contour {
namespace polynomial=contour_polynomial;
using I=polynomial::Interval;
using Net=polynomial::Net;
using Curve=polynomial::Curve;
using Patch=contour_patch::Patch;
constexpr double RootError=1e-12,FitError=1e-11,EdgeError=1e-10;

double Magnitude(const I& a){return std::max(std::abs(a.Lo),std::abs(a.Hi));}
double Minimum(const I& a){return polynomial::Sign(a)?std::min(std::abs(a.Lo),std::abs(a.Hi)):0;}
I Width(double a,double b){return polynomial::Sub(polynomial::Exact(b),polynomial::Exact(a));}
double DerivativeNorm(const Net& x,const Net& y,const Net& z){
    const double value=std::hypot(Magnitude(polynomial::Bounds(x)),Magnitude(polynomial::Bounds(y)),Magnitude(polynomial::Bounds(z)));
    return polynomial::Up(value*(1+64*std::numeric_limits<double>::epsilon()));
}
Curve Boundary(const Net& net,bool solveU,bool high){
    Curve result(static_cast<size_t>((solveU?net.DegreeV:net.DegreeU)+1));
    for(size_t i=0;i<result.size();++i)result[i]=solveU?net.At(high?net.DegreeU:0,static_cast<int>(i)):net.At(static_cast<int>(i),high?net.DegreeV:0);
    return result;
}
std::pair<Net,Net> Split(const Net& net,bool alongU,const I& fraction){
    auto left=polynomial::MakeNet(net.DegreeU,net.DegreeV),right=left;
    const int degree=alongU?net.DegreeU:net.DegreeV,count=alongU?net.DegreeV:net.DegreeU;
    for(int j=0;j<=count;++j){
        Curve work(degree+1),a(degree+1),b(degree+1);
        for(int i=0;i<=degree;++i)work[i]=alongU?net.At(i,j):net.At(j,i);
        a[0]=work[0];b[degree]=work[degree];
        for(int level=1;level<=degree;++level){
            for(int i=0;i<=degree-level;++i)work[i]=polynomial::Add(polynomial::Multiply(polynomial::Sub(polynomial::Exact(1),fraction),work[i]),polynomial::Multiply(fraction,work[i+1]));
            a[level]=work[0];b[degree-level]=work[degree-level];
        }
        for(int i=0;i<=degree;++i){if(alongU){left.At(i,j)=a[i];right.At(i,j)=b[i];}else{left.At(j,i)=a[i];right.At(j,i)=b[i];}}
    }
    return {std::move(left),std::move(right)};
}

struct Evaluation {gp_Pnt Point;gp_Vec Du,Dv,Duu,Dvv,Duv;double F=0,Fu=0,Fv=0;};
class OriginalSurface {
    occ::handle<Geom_BSplineSurface> Spline;
    occ::handle<Geom_BezierSurface> Bezier;
    gp_Trsf Transform;
    contour_patch::SourceEquation Certificate;
public:
    explicit OriginalSurface(const TopoDS_Face& face):Certificate(face){
        TopLoc_Location location;auto geometry=BRep_Tool::Surface(face,location);Transform=location.Transformation();
        while(const auto trimmed=occ::down_cast<Geom_RectangularTrimmedSurface>(geometry))geometry=trimmed->BasisSurface();
        Spline=occ::down_cast<Geom_BSplineSurface>(geometry);Bezier=occ::down_cast<Geom_BezierSurface>(geometry);
        Require(!Spline.IsNull()||!Bezier.IsNull(),"PLATE_CONTOUR_SOURCE_UNSUPPORTED");
    }
    I Certified(const Patch& patch,double u,double v,const Check& check)const{return Certificate.Value(patch,u,v,check);}
    I ExactCertified(const Patch& patch,double u,double v,const Check& check)const{return Certificate.ExactValue(patch,u,v,check);}
    contour_patch::DerivativeNets Polynomials(const Patch& patch,const Check& check)const{return Certificate.Polynomials(patch,check);}
    bool Cusp(bool constantU,double value)const{
        if(Spline.IsNull())return false;
        const int count=constantU?Spline->NbUKnots():Spline->NbVKnots(),degree=constantU?Spline->UDegree():Spline->VDegree();
        for(int i=2;i<count;++i)if((constantU?Spline->UKnot(i):Spline->VKnot(i))==value&&(constantU?Spline->UMultiplicity(i):Spline->VMultiplicity(i))==degree)return true;
        return false;
    }
    Evaluation At(const Patch& patch,double u,double v)const{
        Evaluation result;gp_Vec duu,dvv,duv;
        if(!Spline.IsNull())Spline->LocalD2(u,v,patch.UFirst,patch.ULast,patch.VFirst,patch.VLast,result.Point,result.Du,result.Dv,duu,dvv,duv);
        else Bezier->D2(u,v,result.Point,result.Du,result.Dv,duu,dvv,duv);
        result.Point.Transform(Transform);result.Du.Transform(Transform);result.Dv.Transform(Transform);duu.Transform(Transform);dvv.Transform(Transform);duv.Transform(Transform);
        result.Duu=duu;result.Dvv=dvv;result.Duv=duv;
        result.F=result.Du.Crossed(result.Dv).Z();result.Fu=(duu.Crossed(result.Dv)+result.Du.Crossed(duv)).Z();result.Fv=(duv.Crossed(result.Dv)+result.Du.Crossed(dvv)).Z();
        Require(std::isfinite(result.F)&&std::isfinite(result.Fu)&&std::isfinite(result.Fv),"PLATE_CONTOUR_EQUATION_INVALID");return result;
    }
};

// The original polynomial is smooth inside this ONE original knot span,
// even when insertion made the whole B-spline nominally C0. This adaptor
// supplies one-sided derivatives at its bounds without changing geometry.
class CellSurface final : public Adaptor3d_Surface {
    std::shared_ptr<const OriginalSurface> Source;Patch Data;double Mu,Mv;
public:
    CellSurface(std::shared_ptr<const OriginalSurface> source,const Patch& patch,double mu,double mv):Source(std::move(source)),Data(patch),Mu(mu),Mv(mv){}
    occ::handle<Adaptor3d_Surface> ShallowCopy()const override{return new CellSurface(*this);}
    double FirstUParameter()const override{return Data.U0;}double LastUParameter()const override{return Data.U1;}
    double FirstVParameter()const override{return Data.V0;}double LastVParameter()const override{return Data.V1;}
    GeomAbs_Shape UContinuity()const override{return GeomAbs_CN;}GeomAbs_Shape VContinuity()const override{return GeomAbs_CN;}
    int NbUIntervals(GeomAbs_Shape)const override{return 1;}int NbVIntervals(GeomAbs_Shape)const override{return 1;}
    void UIntervals(NCollection_Array1<double>& values,GeomAbs_Shape)const override{values(values.Lower())=Data.U0;values(values.Lower()+1)=Data.U1;}
    void VIntervals(NCollection_Array1<double>& values,GeomAbs_Shape)const override{values(values.Lower())=Data.V0;values(values.Lower()+1)=Data.V1;}
    occ::handle<Adaptor3d_Surface> UTrim(double first,double last,double)const override{Require(first>=Data.U0&&last<=Data.U1&&last>first,"PLATE_CONTOUR_PATCH_DOMAIN");auto copy=new CellSurface(*this);copy->Data.U0=first;copy->Data.U1=last;return copy;}
    occ::handle<Adaptor3d_Surface> VTrim(double first,double last,double)const override{Require(first>=Data.V0&&last<=Data.V1&&last>first,"PLATE_CONTOUR_PATCH_DOMAIN");auto copy=new CellSurface(*this);copy->Data.V0=first;copy->Data.V1=last;return copy;}
    bool IsUClosed()const override{return false;}bool IsVClosed()const override{return false;}
    bool IsUPeriodic()const override{return false;}bool IsVPeriodic()const override{return false;}
    GeomAbs_SurfaceType GetType()const override{return GeomAbs_OtherSurface;}
    double UResolution(double r)const override{return r/Mu;}double VResolution(double r)const override{return r/Mv;}
    gp_Pnt EvalD0(double u,double v)const override{return Source->At(Data,u,v).Point;}
    Geom_Surface::ResD1 EvalD1(double u,double v)const override{const auto p=Source->At(Data,u,v);Geom_Surface::ResD1 result;result.Point=p.Point;result.D1U=p.Du;result.D1V=p.Dv;return result;}
    Geom_Surface::ResD2 EvalD2(double u,double v)const override{const auto p=Source->At(Data,u,v);Geom_Surface::ResD2 result;result.Point=p.Point;result.D1U=p.Du;result.D1V=p.Dv;result.D2U=p.Duu;result.D2V=p.Dvv;result.D2UV=p.Duv;return result;}
};

struct Cell {double U0,U1,V0,V1;Net F;int Depth=0;};
struct Event {double Value=0,First=0,Last=0;int Side=-1;double Error=0;};
struct GraphDiagnostic {
    const char* Reason="not-attempted";
    int Side=-1;size_t EventIndex=0;
    I Fu,Fv,Edge,EdgeDerivative,NativeGradient,Residual,Candidate,TopologyLow,TopologyHigh;
    double MU=0,MV=0,RootFirst=0,RootLast=0,NativeFirst=0,NativeLast=0,ProofFirst=0,ProofLast=0,Error=0,Fixed=0,Parameter=0;
    Event Previous,Next;
    bool ExactResidual=false;
    bool Fail(const char* reason){Reason=reason;return false;}
};
struct Graph {
    Cell CellData;bool SolveU=false;int Sign=0;double M=0,Mu=0,Mv=0,Speed=0;
    I Derivative;
    bool Iso=false,Owned=true;double Constant=0;
    std::vector<Event> Events;
};
std::pair<Curve,Curve> SplitCurve(const Curve& curve,const I& fraction);
I LocalGradient(const Patch& patch,const Graph& graph,double fixed,double low,double high,const Check& check){
    const auto& cell=graph.CellData;
    const bool solveU=graph.SolveU;
    Require(low<=high&&low>=(solveU?cell.U0:cell.V0)&&high<=(solveU?cell.U1:cell.V1)
        &&fixed>=(solveU?cell.V0:cell.U0)&&fixed<=(solveU?cell.V1:cell.U1),"PLATE_CONTOUR_ROOT_DOMAIN");
    const auto derivative=solveU?polynomial::DerivativeU(cell.F):polynomial::DerivativeV(cell.F);
    const auto normalized=[](double value,double first,double last){
        if(value==first)return polynomial::Exact(0);
        if(value==last)return polynomial::Exact(1);
        const auto result=polynomial::DividePositive(polynomial::Sub(polynomial::Exact(value),polynomial::Exact(first)),Width(first,last));
        return I{std::max(0.,result.Lo),std::min(1.,result.Hi)};
    };
    const auto fixedFraction=solveU?normalized(fixed,cell.V0,cell.V1):normalized(fixed,cell.U0,cell.U1);
    const int fixedDegree=solveU?derivative.DegreeV:derivative.DegreeU,dependentDegree=solveU?derivative.DegreeU:derivative.DegreeV;
    Curve curve(static_cast<size_t>(dependentDegree+1));
    // Collapse the fixed coordinate with outward de Casteljau, then restrict
    // the dependent derivative to a bracket containing BOTH value and root.
    for(int i=0;i<=dependentDegree;++i){
        check();Curve work(static_cast<size_t>(fixedDegree+1));
        for(int j=0;j<=fixedDegree;++j)work[static_cast<size_t>(j)]=solveU?derivative.At(i,j):derivative.At(j,i);
        for(int level=1;level<=fixedDegree;++level)for(int j=0;j<=fixedDegree-level;++j)
            work[static_cast<size_t>(j)]=polynomial::Add(polynomial::Multiply(polynomial::Sub(polynomial::Exact(1),fixedFraction),work[static_cast<size_t>(j)]),polynomial::Multiply(fixedFraction,work[static_cast<size_t>(j+1)]));
        curve[static_cast<size_t>(i)]=work.front();
    }
    const auto first=solveU?normalized(low,cell.U0,cell.U1):normalized(low,cell.V0,cell.V1);
    const auto last=solveU?normalized(high,cell.U0,cell.U1):normalized(high,cell.V0,cell.V1);
    // An interval denominator containing zero cannot safely reparameterize
    // this restriction. The fixed-coordinate hull still bounds the bracket.
    if(last.Lo>0){
        if(!(last.Lo==1&&last.Hi==1))curve=SplitCurve(curve,last).first;
        if(!(first.Lo==0&&first.Hi==0))curve=SplitCurve(curve,polynomial::DividePositive(first,last)).second;
    }
    const auto area=polynomial::Multiply(Width(patch.U0,patch.U1),Width(patch.V0,patch.V1));
    return polynomial::DividePositive(polynomial::Bounds(curve),polynomial::Multiply(area,solveU?Width(cell.U0,cell.U1):Width(cell.V0,cell.V1)));
}
double NativeDerivative(const I& bound,const I& area,const I& dependentWidth){
    return polynomial::DividePositive(polynomial::Exact(Minimum(bound)),polynomial::Multiply(area,dependentWidth)).Lo;
}
double Coordinate(double a,double b,double t){return t==0?a:t==1?b:std::fma(b-a,t,a);}
std::pair<Curve,Curve> SplitCurve(const Curve& curve,const I& fraction){
    auto work=curve;Curve a(curve.size()),b(curve.size());a.front()=work.front();b.back()=work.back();
    for(size_t level=1;level<curve.size();++level){for(size_t i=0;i<curve.size()-level;++i)work[i]=polynomial::Add(polynomial::Multiply(polynomial::Sub(polynomial::Exact(1),fraction),work[i]),polynomial::Multiply(fraction,work[i+1]));a[level]=work.front();b[curve.size()-1-level]=work[curve.size()-1-level];}
    return {std::move(a),std::move(b)};
}
Curve Restricted(Curve curve,double first,double last){
    if(last!=1)curve=SplitCurve(curve,polynomial::Exact(last)).first;
    if(first!=0)curve=SplitCurve(curve,polynomial::DividePositive(polynomial::Exact(first),polynomial::Exact(last))).second;
    return curve;
}
bool RefineBoundary(const OriginalSurface& surface,const Patch& patch,const Cell& cell,bool solveU,bool high,const polynomial::Root& root,double speed,Event& event,GraphDiagnostic& diagnostic,const Check& check){
    diagnostic.Side=high?1:0;diagnostic.RootFirst=root.First;diagnostic.RootLast=root.Last;
    diagnostic.ExactResidual=false;
    const double a=solveU?cell.V0:cell.U0,b=solveU?cell.V1:cell.U1,dependent=solveU?(high?cell.U1:cell.U0):(high?cell.V1:cell.V0);
    const auto uv=[&](double s){return solveU?gp_Pnt2d(dependent,s):gp_Pnt2d(s,dependent);};
    const auto interval=[&](double t){return t==0?polynomial::Exact(a):t==1?polynomial::Exact(b):polynomial::Add(polynomial::Exact(a),polynomial::Multiply(Width(a,b),polynomial::Exact(t)));};
    double low=std::max(a,interval(root.First).Lo),up=std::min(b,interval(root.Last).Hi);
    diagnostic.NativeFirst=low;diagnostic.NativeLast=up;
    const auto errorBound=[&]{return polynomial::Multiply(Width(low,up),polynomial::Exact(speed)).Hi;};
    if(errorBound()<=RootError){event.Value=low+(up-low)/2;event.First=low;event.Last=up;event.Side=high?1:0;event.Error=errorBound();return true;}
    const auto edge=Boundary(cell.F,solveU,high),derivative=polynomial::Derivative(edge);
    const auto normalizedLow=polynomial::DividePositive(polynomial::Sub(polynomial::Exact(low),polynomial::Exact(a)),Width(a,b));
    const auto normalizedHigh=polynomial::DividePositive(polynomial::Sub(polynomial::Exact(up),polynomial::Exact(a)),Width(a,b));
    const double proofFirst=std::max(0.,normalizedLow.Lo),proofLast=std::min(1.,normalizedHigh.Hi);
    diagnostic.ProofFirst=proofFirst;diagnostic.ProofLast=proofLast;
    const auto gradient=polynomial::Bounds(Restricted(derivative,proofFirst,proofLast));
    const auto area=polynomial::Multiply(Width(patch.U0,patch.U1),Width(patch.V0,patch.V1));
    const auto nativeGradient=polynomial::DividePositive(gradient,polynomial::Multiply(area,Width(a,b)));
    diagnostic.NativeGradient=nativeGradient;
    if(!polynomial::Sign(nativeGradient))return diagnostic.Fail("boundary-refine-gradient-not-separated");
    bool useExact=false;
    for(int i=0;i<100;++i){
        check();const double middle=low+(up-low)/2;const auto point=uv(middle);
        const double error=errorBound();
        diagnostic.NativeFirst=low;diagnostic.NativeLast=up;diagnostic.Error=error;
        if(error<=RootError){event.Value=middle;event.First=low;event.Last=up;event.Side=high?1:0;event.Error=error;return true;}
        auto residual=useExact?surface.ExactCertified(patch,point.X(),point.Y(),check):surface.Certified(patch,point.X(),point.Y(),check);
        auto candidate=polynomial::Sub(polynomial::Exact(middle),polynomial::DivideNonZero(residual,nativeGradient));
        double nextLow=std::max(low,candidate.Lo),nextHigh=std::min(up,candidate.Hi);
        if(!useExact&&nextLow==low&&nextHigh==up&&!polynomial::Sign(residual)){
            // The binary64 interval may hit a cancellation floor. Re-evaluate
            // the SAME original equation at the SAME point exactly; preserve
            // this bracket, its derivative certificate, and the error target.
            const auto binaryResidual=residual;useExact=true;diagnostic.ExactResidual=true;
            residual=surface.ExactCertified(patch,point.X(),point.Y(),check);
            candidate=polynomial::Sub(polynomial::Exact(middle),polynomial::DivideNonZero(residual,nativeGradient));
            nextLow=std::max(low,candidate.Lo);nextHigh=std::min(up,candidate.Hi);
            if(std::getenv("ICAX_FIXTURE_TRACE")||std::getenv("ICAX_FIXTURE_CONTOUR_TRACE")){
                try{std::cerr<<std::setprecision(17)<<"fixture regular contour boundary exact residual uv="<<point.X()<<','<<point.Y()
                    <<" binaryResidual="<<binaryResidual.Lo<<','<<binaryResidual.Hi<<" exactResidual="<<residual.Lo<<','<<residual.Hi
                    <<" bracket="<<low<<','<<up<<" nativeGradient="<<nativeGradient.Lo<<','<<nativeGradient.Hi<<std::endl;
                }catch(...){std::cerr<<"fixture regular contour exact residual diagnostic unavailable"<<std::endl;}
            }
        }
        diagnostic.Residual=residual;diagnostic.Candidate=candidate;
        if(nextLow>nextHigh)return diagnostic.Fail("boundary-refine-empty-newton-intersection");
        if(nextLow>low||nextHigh<up){low=nextLow;up=nextHigh;continue;}
        const int sign=polynomial::Sign(residual);
        if(!sign)return diagnostic.Fail("boundary-refine-unseparated-residual-stagnation");
        if(middle==low||middle==up)return diagnostic.Fail("boundary-refine-parameter-stagnation");
        if(sign==polynomial::Sign(nativeGradient))up=middle;else low=middle;
    }return diagnostic.Fail("boundary-refine-iteration-budget");
}
bool PrepareGraph(const OriginalSurface& source,const Patch& patch,const Cell& cell,double mu,double mv,Graph& graph,GraphDiagnostic& diagnostic,const Check& check){
    const auto fu=polynomial::Bounds(polynomial::DerivativeU(cell.F)),fv=polynomial::Bounds(polynomial::DerivativeV(cell.F));
    const auto area=polynomial::Multiply(Width(patch.U0,patch.U1),Width(patch.V0,patch.V1));
    const double mU=NativeDerivative(fu,area,Width(cell.U0,cell.U1)),mV=NativeDerivative(fv,area,Width(cell.V0,cell.V1));
    diagnostic.Fu=fu;diagnostic.Fv=fv;diagnostic.MU=mU;diagnostic.MV=mV;
    if(!(mU>0||mV>0))return diagnostic.Fail("gradient-no-regular-chart");
    graph.CellData=cell;graph.SolveU=mU/mu>=mV/mv;graph.Sign=polynomial::Sign(graph.SolveU?fu:fv);graph.M=graph.SolveU?mU:mV;graph.Mu=mu;graph.Mv=mv;
    graph.Derivative=polynomial::DividePositive(graph.SolveU?fu:fv,polynomial::Multiply(area,graph.SolveU?Width(cell.U0,cell.U1):Width(cell.V0,cell.V1)));
    const auto fixedBound=graph.SolveU?fv:fu;
    const auto fixedWidth=graph.SolveU?Width(cell.V0,cell.V1):Width(cell.U0,cell.U1);
    const double fixedSlope=polynomial::DividePositive(polynomial::Exact(Magnitude(fixedBound)),polynomial::Multiply(area,fixedWidth)).Hi;
    const double fixedNorm=graph.SolveU?mv:mu,dependentNorm=graph.SolveU?mu:mv;
    graph.Speed=polynomial::Add(polynomial::Exact(fixedNorm),polynomial::Multiply(polynomial::Exact(dependentNorm),polynomial::DividePositive(polynomial::Exact(fixedSlope),polynomial::Exact(graph.M)))).Hi;
    const double first=graph.SolveU?cell.V0:cell.U0,last=graph.SolveU?cell.V1:cell.U1;
    graph.Events={{first,first,first,-1,0},{last,last,last,-1,0}};
    for(bool high:{false,true}){
        const auto edge=Boundary(cell.F,graph.SolveU,high);diagnostic.Side=high?1:0;diagnostic.Edge=polynomial::Bounds(edge);diagnostic.EdgeDerivative=polynomial::Bounds(polynomial::Derivative(edge));
        // Discovery needs a complete, disjoint set of UNIQUE enclosures.
        // The original-source interval equation below, not the rounded
        // Bernstein coefficients, certifies final spatial root precision.
        const auto roots=polynomial::IsolateRoots(edge,check,1e-12,16384,64,polynomial::RootIsolationMode::CoarseUnique);
        if(roots.IdenticallyZero){graph.Iso=true;graph.Owned=!high;graph.Constant=graph.SolveU?(high?cell.U1:cell.U0):(high?cell.V1:cell.V0);return true;}
        if(!roots.Certified)return diagnostic.Fail("boundary-polynomial-root-isolation");
        for(const auto& root:roots.Roots){Event event;if(!RefineBoundary(source,patch,cell,graph.SolveU,high,root,graph.Speed,event,diagnostic,check))return false;graph.Events.push_back(event);}
    }
    std::sort(graph.Events.begin(),graph.Events.end(),[](const auto& a,const auto& b){return a.First<b.First;});
    std::vector<Event> unique;
    for(const auto& event:graph.Events){
        if(!unique.empty()&&event.First<=unique.back().Last){
            const bool corner=event.First==event.Last&&unique.back().First==unique.back().Last&&event.First==unique.back().First;
            if(!corner||(event.Side>=0&&unique.back().Side>=0)){diagnostic.Previous=unique.back();diagnostic.Next=event;return diagnostic.Fail("boundary-event-enclosures-overlap");}
            if(event.Side>=0)unique.back()=event;
        }else unique.push_back(event);
    }
    graph.Events=std::move(unique);diagnostic.Reason="graph-prepared";return true;
}

void TraceUncertified(const TopoDS_Face& face,const OriginalSurface& source,const Patch& patch,const Cell& cell,const Graph& graph,const GraphDiagnostic& d,const Check& check){
    if(std::getenv("ICAX_FIXTURE_TRACE")||std::getenv("ICAX_FIXTURE_CONTOUR_TRACE")){
        try{
            const auto f=polynomial::Bounds(cell.F);double uncertainty=0;size_t containsZero=0,exactZero=0;
            for(const auto& value:cell.F.Coefficients){uncertainty=std::max(uncertainty,value.Hi-value.Lo);if(value.Lo<=0&&value.Hi>=0)++containsZero;if(polynomial::Zero(value))++exactZero;}
            std::cerr<<std::setprecision(17)<<"fixture regular contour uncertified reason="<<d.Reason<<" cell="<<cell.U0<<','<<cell.U1<<','<<cell.V0<<','<<cell.V1
                <<" owningPatch="<<patch.U0<<','<<patch.U1<<','<<patch.V0<<','<<patch.V1<<" knotSpans="<<patch.UFirst<<','<<patch.ULast<<','<<patch.VFirst<<','<<patch.VLast
                <<" depth="<<cell.Depth<<" degreeF="<<cell.F.DegreeU<<','<<cell.F.DegreeV<<" F="<<f.Lo<<','<<f.Hi<<" Fu="<<d.Fu.Lo<<','<<d.Fu.Hi<<" Fv="<<d.Fv.Lo<<','<<d.Fv.Hi
                <<" coefficientMaxWidth="<<uncertainty<<" zeroContainingCoefficients="<<containsZero<<" exactZeroCoefficients="<<exactZero
                <<" mUV="<<d.MU<<','<<d.MV<<" solved="<<(graph.SolveU?'U':'V')<<" speed="<<graph.Speed<<" side="<<d.Side
                <<" boundary="<<d.Edge.Lo<<','<<d.Edge.Hi<<" boundaryDerivative="<<d.EdgeDerivative.Lo<<','<<d.EdgeDerivative.Hi
                <<" root="<<d.RootFirst<<','<<d.RootLast<<" nativeRoot="<<d.NativeFirst<<','<<d.NativeLast<<" gradientProof="<<d.ProofFirst<<','<<d.ProofLast
                <<" nativeGradient="<<d.NativeGradient.Lo<<','<<d.NativeGradient.Hi<<" residual="<<d.Residual.Lo<<','<<d.Residual.Hi<<" exactResidual="<<d.ExactResidual<<" candidate="<<d.Candidate.Lo<<','<<d.Candidate.Hi<<" spatialError="<<d.Error
                <<" eventIndex="<<d.EventIndex<<" previous="<<d.Previous.First<<','<<d.Previous.Last<<':'<<d.Previous.Side<<" next="<<d.Next.First<<','<<d.Next.Last<<':'<<d.Next.Side
                <<" fixed="<<d.Fixed<<" normalized="<<d.Parameter<<" topologyLow="<<d.TopologyLow.Lo<<','<<d.TopologyLow.Hi<<" topologyHigh="<<d.TopologyHigh.Lo<<','<<d.TopologyHigh.Hi<<std::endl;
        }catch(...){std::cerr<<"fixture regular contour numerical diagnostic unavailable"<<std::endl;}
        try{const double u=cell.U0+(cell.U1-cell.U0)/2,v=cell.V0+(cell.V1-cell.V0)/2;const auto p=source.At(patch,u,v);const auto f=source.Certified(patch,u,v,check);
            std::cerr<<std::setprecision(17)<<"fixture regular contour center uv="<<u<<','<<v<<" originalF="<<p.F<<" originalFu="<<p.Fu<<" originalFv="<<p.Fv<<" certifiedF="<<f.Lo<<','<<f.Hi<<std::endl;
        }catch(...){std::cerr<<"fixture regular contour source diagnostic unavailable"<<std::endl;}
    }
    if(std::getenv("ICAX_FIXTURE_CONTOUR_CELL_DUMP"))if(const auto directory=std::getenv("ICAX_FIXTURE_BREP_DEBUG_DIR")){
        try{static std::atomic_size_t sequence{0};const auto stem=std::string(directory)+"/contour-uncertified-"+std::to_string(sequence.fetch_add(1));
            BRepTools::Write(face,(stem+"-face.brep").c_str());std::ofstream file(stem+"-cell.txt");file<<std::setprecision(17)<<patch.U0<<' '<<patch.U1<<' '<<patch.V0<<' '<<patch.V1<<'\n'<<cell.U0<<' '<<cell.U1<<' '<<cell.V0<<' '<<cell.V1<<'\n'<<patch.UFirst<<' '<<patch.ULast<<' '<<patch.VFirst<<' '<<patch.VLast<<'\n';
            std::cerr<<"fixture regular contour failed cell dump="<<stem<<std::endl;
        }catch(...){std::cerr<<"fixture regular contour cell dump unavailable"<<std::endl;}
    }
}

struct RootDiagnostic {
    int Iteration=0;
    double Value=0,Low=0,High=0,ResidualBound=INFINITY,BracketBound=INFINITY,RawStepBound=INFINITY,RawF=0,RawSlope=0,Next=0;
    I Residual;
    double NextLow=0,NextHigh=0,ContractedBracketBound=INFINITY;
    I LocalGradient;
    double LocalFirst=0,LocalLast=0,LocalMinimum=0,LocalResidualBound=INFINITY;
};
class Equation final : public Adaptor2d_Curve2d {
    std::shared_ptr<const OriginalSurface> Source;Patch PatchData;Graph GraphData;Event A,B;Check Cancel;double First=0,Last=1;
    [[noreturn]] void RootFailed(const char* reason,double t,double fixed,const RootDiagnostic& d)const{
        if(std::getenv("ICAX_FIXTURE_TRACE")||std::getenv("ICAX_FIXTURE_CONTOUR_TRACE")){
            try{
                const auto& cell=GraphData.CellData;
                std::cerr<<std::setprecision(17)<<"fixture regular contour root failed reason="<<reason<<" iteration="<<d.Iteration
                    <<" owningPatch="<<PatchData.U0<<','<<PatchData.U1<<','<<PatchData.V0<<','<<PatchData.V1
                    <<" knotSpans="<<PatchData.UFirst<<','<<PatchData.ULast<<','<<PatchData.VFirst<<','<<PatchData.VLast
                    <<" cell="<<cell.U0<<','<<cell.U1<<','<<cell.V0<<','<<cell.V1<<" solved="<<(GraphData.SolveU?'U':'V')
                    <<" t="<<t<<" adaptorRange="<<First<<','<<Last<<" fixed="<<fixed
                    <<" eventA="<<A.Value<<','<<A.First<<','<<A.Last<<':'<<A.Side<<':'<<A.Error
                    <<" eventB="<<B.Value<<','<<B.First<<','<<B.Last<<':'<<B.Side<<':'<<B.Error
                    <<" value="<<d.Value<<" bracket="<<d.Low<<','<<d.High<<" residual="<<d.Residual.Lo<<','<<d.Residual.Hi
                    <<" residualBound="<<d.ResidualBound<<" bracketBound="<<d.BracketBound<<" rawstepBound="<<d.RawStepBound
                    <<" rawF="<<d.RawF<<" rawSlope="<<d.RawSlope<<" next="<<d.Next
                    <<" contractedBracket="<<d.NextLow<<','<<d.NextHigh<<" contractedBracketBound="<<d.ContractedBracketBound
                    <<" localGradientDomain="<<d.LocalFirst<<','<<d.LocalLast<<" localGradient="<<d.LocalGradient.Lo<<','<<d.LocalGradient.Hi
                    <<" localGradientLower="<<d.LocalMinimum<<" localResidualBound="<<d.LocalResidualBound
                    <<" gradientLower="<<GraphData.M<<" gradientInterval="<<GraphData.Derivative.Lo<<','<<GraphData.Derivative.Hi
                    <<" surfaceNorm="<<GraphData.Mu<<','<<GraphData.Mv<<" graphSpeed="<<GraphData.Speed<<" rootError="<<RootError<<std::endl;
            }catch(...){std::cerr<<"fixture regular contour root diagnostic unavailable"<<std::endl;}
        }
        throw std::runtime_error("PLATE_CONTOUR_ROOT_FAILED");
    }
public:
    Equation(std::shared_ptr<const OriginalSurface> source,const Patch& patch,const Graph& graph,const Event& a,const Event& b,const Check& check)
        :Source(std::move(source)),PatchData(patch),GraphData(graph),A(a),B(b),Cancel(check){}
    occ::handle<Adaptor2d_Curve2d> ShallowCopy()const override{return new Equation(*this);}
    occ::handle<Adaptor2d_Curve2d> Trim(double first,double last,double)const override{Require(first>=0&&last<=1&&last>first,"PLATE_CONTOUR_PARAMETER_RANGE");auto result=new Equation(*this);result->First=first;result->Last=last;return result;}
    double FirstParameter()const override{return First;}double LastParameter()const override{return Last;}
    GeomAbs_Shape Continuity()const override{return GeomAbs_C1;}
    int NbIntervals(GeomAbs_Shape shape)const override{Require(shape<=GeomAbs_C1,"PLATE_CONTOUR_CONTINUITY");return 1;}
    void Intervals(NCollection_Array1<double>& values,GeomAbs_Shape shape)const override{Require(shape<=GeomAbs_C1,"PLATE_CONTOUR_CONTINUITY");values(values.Lower())=First;values(values.Lower()+1)=Last;}
    bool IsClosed()const override{return false;}bool IsPeriodic()const override{return false;}GeomAbs_CurveType GetType()const override{return GeomAbs_OtherCurve;}
    double Resolution(double r)const override{return r/(GraphData.Speed*(B.Value-A.Value));}
    gp_Pnt2d Value(double t)const override{gp_Pnt2d uv;D0(t,uv);return uv;}
    void D0(double t,gp_Pnt2d& uv)const override{gp_Vec2d derivative;D1(t,uv,derivative);}
    void D1(double t,gp_Pnt2d& uv,gp_Vec2d& derivative)const override{
        Cancel();Require(std::isfinite(t)&&t>=0&&t<=1,"PLATE_CONTOUR_PARAMETER_RANGE");
        const auto& cell=GraphData.CellData;const bool solveU=GraphData.SolveU;
        const double fixed=Coordinate(A.Value,B.Value,t),span=B.Value-A.Value;
        if(GraphData.Iso)uv=solveU?gp_Pnt2d(GraphData.Constant,fixed):gp_Pnt2d(fixed,GraphData.Constant);
        else{
            double low=solveU?cell.U0:cell.V0,high=solveU?cell.U1:cell.V1,value=low+(high-low)/2;
            // Complete boundary-root isolation and the selected existence-side
            // interval certify that this bracket contains exactly one root.
            bool done=false;RootDiagnostic diagnostic;
            for(int i=0;i<100;++i){
                Cancel();uv=solveU?gp_Pnt2d(value,fixed):gp_Pnt2d(fixed,value);const auto p=Source->At(PatchData,uv.X(),uv.Y());
                const double slope=solveU?p.Fu:p.Fv,normal=solveU?GraphData.Mu:GraphData.Mv;
                const auto residual=Source->Certified(PatchData,uv.X(),uv.Y(),Cancel);
                const double residualBound=polynomial::DividePositive(polynomial::Multiply(polynomial::Exact(Magnitude(residual)),polynomial::Exact(normal)),polynomial::Exact(GraphData.M)).Hi;
                const double bracketBound=polynomial::Multiply(Width(low,high),polynomial::Exact(normal)).Hi;
                // Only original-equation interval bounds certify precision.
                // The floating LocalD2 Newton step below is a proposal only.
                if(std::min(residualBound,bracketBound)<=RootError){done=true;break;}
                const double step=p.F/slope;
                diagnostic={i,value,low,high,residualBound,bracketBound,std::abs(step)*normal,p.F,slope,value,residual};
                const int sign=polynomial::Sign(residual);
                if(sign){if(GraphData.Sign*sign<0)low=value;else high=value;}
                else{
                    // A certified residual containing zero can make floating
                    // Newton proposals alternate between adjacent doubles.
                    // Tighten the slope certificate before waiting for exact
                    // parameter stagnation; the precision target is unchanged.
                    auto gradient=GraphData.Derivative;
                    diagnostic.LocalFirst=std::min(low,value);diagnostic.LocalLast=std::max(high,value);
                    diagnostic.LocalGradient=LocalGradient(PatchData,GraphData,fixed,diagnostic.LocalFirst,diagnostic.LocalLast,Cancel);
                    diagnostic.LocalMinimum=Minimum(diagnostic.LocalGradient);
                    if(polynomial::Sign(diagnostic.LocalGradient)==GraphData.Sign&&diagnostic.LocalMinimum>GraphData.M){
                        gradient=diagnostic.LocalGradient;
                        diagnostic.LocalResidualBound=polynomial::DividePositive(polynomial::Multiply(polynomial::Exact(Magnitude(residual)),polynomial::Exact(normal)),polynomial::Exact(diagnostic.LocalMinimum)).Hi;
                        if(diagnostic.LocalResidualBound<=RootError){done=true;break;}
                    }
                    const auto candidate=polynomial::Sub(polynomial::Exact(value),polynomial::DivideNonZero(residual,gradient));
                    low=std::max(low,candidate.Lo);high=std::min(high,candidate.Hi);Require(low<=high,"PLATE_CONTOUR_ROOT_BRACKET");
                }
                diagnostic.NextLow=low;diagnostic.NextHigh=high;
                diagnostic.ContractedBracketBound=polynomial::Multiply(Width(low,high),polynomial::Exact(normal)).Hi;
                if(value>=low&&value<=high&&diagnostic.ContractedBracketBound<=RootError){done=true;break;}
                double next=value-step;if(!std::isfinite(next)||next<=low||next>=high)next=low+(high-low)/2;
                diagnostic.Next=next;
                if(next==value){
                    diagnostic.LocalFirst=std::min(low,value);diagnostic.LocalLast=std::max(high,value);
                    diagnostic.LocalGradient=LocalGradient(PatchData,GraphData,fixed,diagnostic.LocalFirst,diagnostic.LocalLast,Cancel);
                    diagnostic.LocalMinimum=Minimum(diagnostic.LocalGradient);
                    if(polynomial::Sign(diagnostic.LocalGradient)==GraphData.Sign&&diagnostic.LocalMinimum>GraphData.M){
                        diagnostic.LocalResidualBound=polynomial::DividePositive(polynomial::Multiply(polynomial::Exact(Magnitude(residual)),polynomial::Exact(normal)),polynomial::Exact(diagnostic.LocalMinimum)).Hi;
                        if(diagnostic.LocalResidualBound<=RootError){done=true;break;}
                    }
                    RootFailed("parameter-stagnation",t,fixed,diagnostic);
                }
                value=next;
            }if(!done)RootFailed("iteration-budget",t,fixed,diagnostic);
        }
        const auto p=Source->At(PatchData,uv.X(),uv.Y());const double divisor=solveU?p.Fu:p.Fv;
        Require(GraphData.Sign*divisor>0,"PLATE_CONTOUR_CHART_INVALID");
        const double slope=GraphData.Iso?0:-(solveU?p.Fv:p.Fu)/divisor;
        derivative=solveU?gp_Vec2d(slope*span,span):gp_Vec2d(span,slope*span);
    }
};

void AppendTrimmed(std::vector<TopoDS_Edge>& result,const TopoDS_Edge& edge,const TopoDS_Face& face,const occ::handle<Geom2d_Curve>& pc,const Check& check){
    TopLoc_Location originalLocation;double originalFirst,originalLast;auto original=BRep_Tool::Curve(edge,originalLocation,originalFirst,originalLast);
    while(const auto trimmed=occ::down_cast<Geom_TrimmedCurve>(original))original=trimmed->BasisCurve();
    const auto clipped=Common(edge,face,check);Require(BRepCheck_Analyzer(clipped).IsValid(),"PLATE_CONTOUR_TRIM_INVALID");
    for(TopExp_Explorer e(clipped,TopAbs_EDGE);e.More();e.Next()){
        auto part=TopoDS::Edge(e.Current());TopLoc_Location location;double first,last;auto curve=BRep_Tool::Curve(part,location,first,last);Require(!curve.IsNull(),"PLATE_CONTOUR_CURVE_FAILED");
        while(const auto trimmed=occ::down_cast<Geom_TrimmedCurve>(curve))curve=trimmed->BasisCurve();
        Require(curve==original&&location.IsEqual(originalLocation)&&first>=originalFirst&&last<=originalLast&&last>first,"PLATE_CONTOUR_TRIM_PARAMETER_CHANGED");
        BRep_Builder builder;builder.UpdateEdge(part,pc,face,EdgeError);builder.Range(part,face,first,last);builder.SameRange(part,true);builder.SameParameter(part,true);
        Require(BRepCheck_Analyzer(part).IsValid(),"PLATE_CONTOUR_TRIM_INVALID");result.push_back(part);
    }
}
void Fit(std::vector<TopoDS_Edge>& result,const std::shared_ptr<const OriginalSurface>& source,const occ::handle<Adaptor3d_Surface>& surface,const Patch& patch,const Graph& graph,const Event& a,const Event& b,const TopoDS_Face& face,const Check& check)try{
    const occ::handle<Adaptor2d_Curve2d> equation=new Equation(source,patch,graph,a,b,check);
    const occ::handle<Adaptor3d_Surface> cellSurface=new CellSurface(source,patch,graph.Mu,graph.Mv);
    Approx_CurveOnSurface fit(equation,cellSurface,0,1,FitError);fit.Perform(512,14,GeomAbs_C1);check();
    const double raw3=fit.MaxError3d(),rawU=fit.MaxError2dU(),rawV=fit.MaxError2dV();
    double bound=raw3+graph.Mu*rawU+graph.Mv*rawV+RootError+std::max(a.Error,b.Error);
    if(!(fit.IsDone()&&std::isfinite(raw3)&&raw3>=0&&raw3<=FitError&&std::isfinite(rawU)&&rawU>=0&&std::isfinite(rawV)&&rawV>=0&&std::isfinite(bound)&&bound<=EdgeError)){
        if(std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<std::setprecision(17)<<"fixture regular contour fit failed uvCell="<<graph.CellData.U0<<','<<graph.CellData.U1<<','<<graph.CellData.V0<<','<<graph.CellData.V1
            <<" solved="<<(graph.SolveU?'U':'V')<<" interval="<<a.Value<<','<<b.Value<<" error3d="<<fit.MaxError3d()<<" errorUV="<<fit.MaxError2dU()<<','<<fit.MaxError2dV()<<" bound="<<bound<<std::endl;
        throw std::runtime_error("PLATE_CONTOUR_PRECISION");
    }
    Require(!fit.Curve3d().IsNull()&&!fit.Curve2d().IsNull(),"PLATE_CONTOUR_CURVE_FAILED");
    const auto pc=occ::down_cast<Geom2d_BSplineCurve>(fit.Curve2d()->Copy());const auto curve=occ::down_cast<Geom_BSplineCurve>(fit.Curve3d()->Copy());
    const auto inside=[&](const gp_Pnt2d& p){return p.X()>=patch.U0&&p.X()<=patch.U1&&p.Y()>=patch.V0&&p.Y()<=patch.V1;};
    bool endpointRepair=false;
    for(int i=1;i<=pc->NbPoles();++i){
        Require(std::isfinite(pc->Pole(i).X())&&std::isfinite(pc->Pole(i).Y())&&pc->Weight(i)>0,"PLATE_CONTOUR_NATURAL_DOMAIN");
        if(!inside(pc->Pole(i))){Require(i==1||i==pc->NbPoles(),"PLATE_CONTOUR_PATCH_DOMAIN");endpointRepair=true;}
    }
    if(endpointRepair){
        Require(!curve->IsPeriodic()&&!pc->IsPeriodic()&&curve->Multiplicity(1)==curve->Degree()+1&&curve->Multiplicity(curve->NbKnots())==curve->Degree()+1
            &&pc->Multiplicity(1)==pc->Degree()+1&&pc->Multiplicity(pc->NbKnots())==pc->Degree()+1,"PLATE_CONTOUR_ENDPOINT_REPRESENTATION");
        double d3=0,du=0,dv=0;
        for(bool high:{false,true}){
            const auto uv=equation->Value(high?1:0);Require(inside(uv),"PLATE_CONTOUR_PATCH_DOMAIN");const auto point=source->At(patch,uv.X(),uv.Y()).Point;
            const int i=high?curve->NbPoles():1,j=high?pc->NbPoles():1;const auto old=pc->Pole(j);
            d3=std::max(d3,curve->Pole(i).Distance(point));du=std::max(du,std::abs(old.X()-uv.X()));dv=std::max(dv,std::abs(old.Y()-uv.Y()));curve->SetPole(i,point);pc->SetPole(j,uv);
        }bound+=d3+graph.Mu*du+graph.Mv*dv;Require(std::isfinite(bound)&&bound<=EdgeError,"PLATE_CONTOUR_PRECISION");
    }
    for(int i=1;i<=pc->NbPoles();++i)Require(inside(pc->Pole(i)),"PLATE_CONTOUR_PATCH_DOMAIN");
    BRep_Builder builder;TopoDS_Edge edge;builder.MakeEdge(edge,curve,EdgeError);TopoDS_Vertex first,last;
    builder.MakeVertex(first,curve->Value(0),EdgeError);builder.MakeVertex(last,curve->Value(1),EdgeError);
    builder.Add(edge,first.Oriented(TopAbs_FORWARD));builder.Add(edge,last.Oriented(TopAbs_REVERSED));builder.UpdateEdge(edge,pc,face,EdgeError);builder.Range(edge,0,1);builder.Range(edge,face,0,1);
    builder.UpdateVertex(first,0,edge,EdgeError);builder.UpdateVertex(last,1,edge,EdgeError);builder.SameRange(edge,true);builder.SameParameter(edge,true);
    Require(BRepCheck_Analyzer(edge).IsValid(),"PLATE_CONTOUR_CURVE_FAILED");AppendTrimmed(result,edge,face,pc,check);
}catch(...){
    if(std::getenv("ICAX_FIXTURE_TRACE")||std::getenv("ICAX_FIXTURE_CONTOUR_TRACE")){
        try{
            std::cerr<<std::setprecision(17)<<"fixture regular contour fit exception owningPatch="<<patch.U0<<','<<patch.U1<<','<<patch.V0<<','<<patch.V1
                <<" solved="<<(graph.SolveU?'U':'V')<<" eventA="<<a.Value<<','<<a.First<<','<<a.Last<<':'<<a.Side<<':'<<a.Error
                <<" eventB="<<b.Value<<','<<b.First<<','<<b.Last<<':'<<b.Side<<':'<<b.Error<<std::endl;
            if(const auto directory=std::getenv("ICAX_FIXTURE_BREP_DEBUG_DIR")){
                static std::atomic_size_t sequence{0};const auto stem=std::string(directory)+"/contour-fit-failed-"+std::to_string(sequence.fetch_add(1));
                BRepTools::Write(face,(stem+"-face.brep").c_str());
                std::cerr<<"fixture regular contour fit failure face dump="<<stem<<"-face.brep"<<std::endl;
            }
        }catch(...){std::cerr<<"fixture regular contour fit failure diagnostic unavailable"<<std::endl;}
    }
    throw;
}

void AddCuspIsos(std::vector<TopoDS_Edge>& result,const TopoDS_Face& face,const Check& check){
    TopLoc_Location location;auto geometry=BRep_Tool::Surface(face,location);
    while(const auto trimmed=occ::down_cast<Geom_RectangularTrimmedSurface>(geometry))geometry=trimmed->BasisSurface();
    const auto spline=occ::down_cast<Geom_BSplineSurface>(geometry);if(spline.IsNull())return;
    double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
    const auto append=[&](bool constantU,double value){
        check();const double first=constantU?v0:u0,last=constantU?v1:u1;
        auto curve=constantU?spline->UIso(value):spline->VIso(value);
        if(!location.IsIdentity())curve=occ::down_cast<Geom_Curve>(curve->Transformed(location.Transformation()));
        const occ::handle<Geom2d_Curve> pc=constantU?new Geom2d_Line(gp_Pnt2d(value,0),gp_Dir2d(0,1)):new Geom2d_Line(gp_Pnt2d(0,value),gp_Dir2d(1,0));
        BRep_Builder builder;TopoDS_Edge edge;builder.MakeEdge(edge,curve,EdgeError);TopoDS_Vertex a,b;
        builder.MakeVertex(a,curve->Value(first),EdgeError);builder.MakeVertex(b,curve->Value(last),EdgeError);builder.Add(edge,a.Oriented(TopAbs_FORWARD));builder.Add(edge,b.Oriented(TopAbs_REVERSED));
        builder.UpdateEdge(edge,pc,face,EdgeError);builder.Range(edge,first,last);builder.Range(edge,face,first,last);builder.UpdateVertex(a,first,edge,EdgeError);builder.UpdateVertex(b,last,edge,EdgeError);builder.SameRange(edge,true);builder.SameParameter(edge,true);
        Require(BRepCheck_Analyzer(edge).IsValid(),"PLATE_CONTOUR_CUSP_INVALID");AppendTrimmed(result,edge,face,pc,check);
    };
    for(int i=2;i<spline->NbUKnots();++i)if(spline->UMultiplicity(i)==spline->UDegree()&&spline->UKnot(i)>u0&&spline->UKnot(i)<u1)append(true,spline->UKnot(i));
    for(int i=2;i<spline->NbVKnots();++i)if(spline->VMultiplicity(i)==spline->VDegree()&&spline->VKnot(i)>v0&&spline->VKnot(i)<v1)append(false,spline->VKnot(i));
}

std::optional<std::vector<TopoDS_Edge>> Build(const TopoDS_Face& face,const Check& check){
    const occ::handle<Adaptor3d_Surface> surface=new BRepAdaptor_Surface(face,false);
    const bool trace=std::getenv("ICAX_FIXTURE_CONTOUR_TRACE")!=nullptr;
    if(trace)std::cerr<<"fixture regular contour extraction begin"<<std::endl;
    const auto patches=contour_patch::Extract(face,check);if(!patches)return std::nullopt;
    if(trace)std::cerr<<"fixture regular contour extraction complete patches="<<patches->size()<<std::endl;
    const auto source=std::make_shared<const OriginalSurface>(face);
    std::vector<TopoDS_Edge> result;size_t nodes=0;
    size_t patchIndex=0;
    for(const auto& patch:*patches){
        const size_t beginNodes=nodes,beginEdges=result.size();
        if(trace)std::cerr<<std::setprecision(17)<<"fixture regular contour patch start index="<<patchIndex<<" uv="<<patch.U0<<','<<patch.U1<<','<<patch.V0<<','<<patch.V1<<std::endl;
        const auto native=source->Polynomials(patch,check);
        auto f=polynomial::Sub(polynomial::Multiply(native.Ux,native.Vy),polynomial::Multiply(native.Uy,native.Vx));
        // Keep the original patch-area scaling used by every cell derivative
        // certificate, while forming F from original D1 control differences.
        const auto area=polynomial::Multiply(Width(patch.U0,patch.U1),Width(patch.V0,patch.V1));
        for(auto& coefficient:f.Coefficients)coefficient=polynomial::Multiply(coefficient,area);
        // Identically rank-deficient projections are not covered by this
        // regular-graph certificate. Delegate the face to the existing path;
        // this is never interpreted as an empty contour certificate.
        if(polynomial::IdenticallyZero(f.Coefficients))return std::nullopt;
        const double mu=DerivativeNorm(native.Ux,native.Uy,native.Uz),mv=DerivativeNorm(native.Vx,native.Vy,native.Vz);
        Require(std::isfinite(mu)&&mu>0&&std::isfinite(mv)&&mv>0,"PLATE_CONTOUR_PATCH_INVALID");
        std::vector<Cell> pending{{patch.U0,patch.U1,patch.V0,patch.V1,f,0}};
        while(!pending.empty()){
            check();auto cell=std::move(pending.back());pending.pop_back();Require(++nodes<=32768,"PLATE_CONTOUR_CERTIFICATE_BUDGET");
            if(polynomial::Sign(cell.F))continue;
            Graph graph;GraphDiagnostic diagnostic;
            if(PrepareGraph(*source,patch,cell,mu,mv,graph,diagnostic,check)){
                if(graph.Iso){if(graph.Owned&&!source->Cusp(graph.SolveU,graph.Constant))Fit(result,source,surface,patch,graph,graph.Events.front(),graph.Events.back(),face,check);continue;}
                const auto lower=Boundary(cell.F,graph.SolveU,false),upper=Boundary(cell.F,graph.SolveU,true);
                const double first=graph.SolveU?cell.V0:cell.U0,last=graph.SolveU?cell.V1:cell.U1;
                bool certified=true;std::vector<std::pair<Event,Event>> active;
                for(size_t i=1;i<graph.Events.size();++i){
                    auto a=graph.Events[i-1],b=graph.Events[i];a.Value=a.Last;b.Value=b.First;
                    diagnostic.EventIndex=i;diagnostic.Previous=a;diagnostic.Next=b;
                    if(!(b.Value>a.Value)){diagnostic.Reason="topology-no-separated-existence-span";certified=false;break;}
                    const double parameter=((a.Value+(b.Value-a.Value)/2)-first)/(last-first);
                    const auto lowValue=polynomial::Evaluate(lower,parameter),highValue=polynomial::Evaluate(upper,parameter);
                    diagnostic.Fixed=a.Value+(b.Value-a.Value)/2;diagnostic.Parameter=parameter;diagnostic.TopologyLow=lowValue;diagnostic.TopologyHigh=highValue;
                    const int low=polynomial::Sign(lowValue),high=polynomial::Sign(highValue);
                    if(!low||!high){diagnostic.Reason="topology-midpoint-interval-sign-unresolved";certified=false;break;}
                    if(graph.Sign*low<0&&graph.Sign*high>0)active.emplace_back(a,b);
                    else if(low!=high){diagnostic.Reason="topology-sign-order-inconsistent";certified=false;break;}
                }
                if(certified){for(const auto& interval:active)Fit(result,source,surface,patch,graph,interval.first,interval.second,face,check);continue;}
            }
            if(cell.Depth>=24){
                TraceUncertified(face,*source,patch,cell,graph,diagnostic,check);
                throw std::runtime_error("PLATE_CONTOUR_CELL_UNCERTIFIED");
            }
            const bool alongU=(cell.U1-cell.U0)/(patch.U1-patch.U0)>=(cell.V1-cell.V0)/(patch.V1-patch.V0);
            const double low=alongU?cell.U0:cell.V0,high=alongU?cell.U1:cell.V1,middle=low+(high-low)/2;
            if(!(middle>low&&middle<high)){diagnostic.Reason="subdivision-parameter-stagnation";TraceUncertified(face,*source,patch,cell,graph,diagnostic,check);throw std::runtime_error("PLATE_CONTOUR_CELL_UNCERTIFIED");}
            const auto fraction=polynomial::DividePositive(polynomial::Sub(polynomial::Exact(middle),polynomial::Exact(low)),Width(low,high));
            const auto halves=Split(cell.F,alongU,fraction);auto a=cell,b=cell;a.F=halves.first;b.F=halves.second;a.Depth++;b.Depth++;
            if(alongU){a.U1=middle;b.U0=middle;}else{a.V1=middle;b.V0=middle;}pending.push_back(std::move(b));pending.push_back(std::move(a));
        }
        if(trace)std::cerr<<"fixture regular contour patch complete index="<<patchIndex<<" cells="<<(nodes-beginNodes)<<" edges="<<(result.size()-beginEdges)<<std::endl;
        ++patchIndex;
    }
    AddCuspIsos(result,face,check);
    if(std::getenv("ICAX_FIXTURE_CONTOUR_TRACE"))std::cerr<<"fixture regular contour certified patches="<<patches->size()<<" cells="<<nodes<<" edges="<<result.size()<<std::endl;
    return result;
}
}
