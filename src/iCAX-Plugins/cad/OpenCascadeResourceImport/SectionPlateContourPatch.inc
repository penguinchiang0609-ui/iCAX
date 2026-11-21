// Included inside plate_exact after SectionPlateContourPolynomial.inc.
// Restrict the ORIGINAL polynomial B-spline to each retained knot cell.
// Cox-de Boor is evaluated as Bernstein polynomial algebra with outward
// intervals, so no unbounded floating-point knot-insertion copy is used.
namespace contour_patch {
namespace polynomial=contour_polynomial;
struct Patch {
    double U0=0,U1=0,V0=0,V1=0;
    int UFirst=0,ULast=0,VFirst=0,VLast=0;
    polynomial::Net X,Y,Z,RelativeX,RelativeY,RelativeZ;
};
namespace detail {
struct Axis {
    int Degree=0,Poles=0;
    std::vector<double> Knots,Unique;
    std::vector<int> Multiplicities;
};
struct Span {double First=0,Last=0;int Flat=0,UniqueFirst=0,UniqueLast=0;};
std::vector<Span> Spans(Axis& axis,double from,double to,bool bezier){
    Require(axis.Degree>=1&&axis.Poles>axis.Degree&&axis.Unique.size()>=2
        &&axis.Unique.size()==axis.Multiplicities.size()&&std::isfinite(from)&&std::isfinite(to)&&from<to,
        "PLATE_CONTOUR_PATCH_DOMAIN");
    axis.Knots.clear();
    for(size_t i=0;i<axis.Unique.size();++i){
        Require(std::isfinite(axis.Unique[i])&&axis.Multiplicities[i]>0
            &&(!i||axis.Unique[i]>axis.Unique[i-1]),"PLATE_CONTOUR_PATCH_KNOTS");
        for(int repeat=0;repeat<axis.Multiplicities[i];++repeat)axis.Knots.push_back(axis.Unique[i]);
    }
    Require(axis.Knots.size()==static_cast<size_t>(axis.Poles+axis.Degree+1),"PLATE_CONTOUR_PATCH_KNOTS");
    Require(from>=axis.Knots[axis.Degree]&&to<=axis.Knots[axis.Poles],"PLATE_CONTOUR_PATCH_DOMAIN");
    std::vector<Span> result;int after=0;
    for(size_t i=0;i+1<axis.Unique.size();++i){
        after+=axis.Multiplicities[i];const int flat=after-1;
        const double a=std::max(from,axis.Unique[i]),b=std::min(to,axis.Unique[i+1]);
        if(!(a<b))continue;
        Require(flat>=axis.Degree&&flat<axis.Poles,"PLATE_CONTOUR_PATCH_KNOTS");
        result.push_back({a,b,flat,bezier?0:static_cast<int>(i+1),bezier?0:static_cast<int>(i+2)});
    }
    Require(!result.empty()&&result.front().First==from&&result.back().Last==to,"PLATE_CONTOUR_PATCH_DOMAIN");
    for(size_t i=1;i<result.size();++i)Require(result[i-1].Last==result[i].First,"PLATE_CONTOUR_PATCH_DOMAIN");
    return result;
}
class Basis {
    const Axis& Source;const Span& Cell;const Check& Cancel;
    std::map<std::pair<int,int>,polynomial::Curve> Cache;
public:
    Basis(const Axis& source,const Span& cell,const Check& check):Source(source),Cell(cell),Cancel(check){}
    const polynomial::Curve& Get(int index,int degree){
        Cancel();const auto key=std::make_pair(index,degree);if(const auto found=Cache.find(key);found!=Cache.end())return found->second;
        using namespace polynomial;polynomial::Curve result;
        if(!degree)result={Exact(index==Cell.Flat?1.:0.)};
        else{
            Require(index>=0&&index+degree+1<static_cast<int>(Source.Knots.size()),"PLATE_CONTOUR_PATCH_BASIS");
            result.assign(static_cast<size_t>(degree+1),Exact(0));
            const double left0=Source.Knots[index],left1=Source.Knots[index+degree];
            if(left1>left0){
                const auto denominator=Sub(Exact(left1),Exact(left0));Require(denominator.Lo>0,"PLATE_CONTOUR_PATCH_KNOT_RANGE");
                const polynomial::Curve factor{DividePositive(Sub(Exact(Cell.First),Exact(left0)),denominator),
                    DividePositive(Sub(Exact(Cell.Last),Exact(left0)),denominator)};
                result=Add(result,Multiply(factor,Get(index,degree-1)));
            }else Require(left1==left0,"PLATE_CONTOUR_PATCH_KNOTS");
            const double right0=Source.Knots[index+1],right1=Source.Knots[index+degree+1];
            if(right1>right0){
                const auto denominator=Sub(Exact(right1),Exact(right0));Require(denominator.Lo>0,"PLATE_CONTOUR_PATCH_KNOT_RANGE");
                const polynomial::Curve factor{DividePositive(Sub(Exact(right1),Exact(Cell.First)),denominator),
                    DividePositive(Sub(Exact(right1),Exact(Cell.Last)),denominator)};
                result=Add(result,Multiply(factor,Get(index+1,degree-1)));
            }else Require(right1==right0,"PLATE_CONTOUR_PATCH_KNOTS");
        }
        return Cache.emplace(key,std::move(result)).first->second;
    }
};
std::array<polynomial::Interval,3> Located(const gp_Pnt& point,const gp_Trsf& transform,const gp_Pnt* origin=nullptr){
    using namespace polynomial;std::array<Interval,3> result;
    for(int row=1;row<=3;++row){
        auto value=Exact(0);
        for(int column=1;column<=3;++column){
            const auto coordinate=origin?Sub(Exact(point.Coord(column)),Exact(origin->Coord(column))):Exact(point.Coord(column));
            value=Add(value,Multiply(Exact(transform.HVectorialPart().Value(row,column)),coordinate));
        }
        // gp_Trsf::Value would round scale*matrix before it reached the
        // interval. Keep the stored scale multiplication inside the bound.
        value=Multiply(Exact(transform.ScaleFactor()),value);
        if(!origin)value=Add(value,Exact(transform.TranslationPart().Coord(row)));
        Require(Finite(value),"PLATE_CONTOUR_PATCH_CONTROL");result[static_cast<size_t>(row-1)]=value;
    }return result;
}
}
std::optional<std::vector<Patch>> Extract(const TopoDS_Face& face,const Check& check){
    TopLoc_Location location;auto geometry=BRep_Tool::Surface(face,location);
    for(;;){const auto trimmed=occ::down_cast<Geom_RectangularTrimmedSurface>(geometry);if(trimmed.IsNull())break;geometry=trimmed->BasisSurface();}
    const auto spline=occ::down_cast<Geom_BSplineSurface>(geometry);const auto bezier=occ::down_cast<Geom_BezierSurface>(geometry);
    if(spline.IsNull()&&bezier.IsNull())return std::nullopt;
    if((!spline.IsNull()&&(spline->IsUPeriodic()||spline->IsVPeriodic()||spline->IsURational()||spline->IsVRational()))
        ||(!bezier.IsNull()&&(bezier->IsURational()||bezier->IsVRational())))return std::nullopt;
    detail::Axis u,v;const bool isBezier=spline.IsNull();
    if(!isBezier){
        u.Degree=spline->UDegree();u.Poles=spline->NbUPoles();v.Degree=spline->VDegree();v.Poles=spline->NbVPoles();
        for(int i=1;i<=spline->NbUKnots();++i){u.Unique.push_back(spline->UKnot(i));u.Multiplicities.push_back(spline->UMultiplicity(i));}
        for(int i=1;i<=spline->NbVKnots();++i){v.Unique.push_back(spline->VKnot(i));v.Multiplicities.push_back(spline->VMultiplicity(i));}
    }else{
        u.Degree=bezier->UDegree();u.Poles=bezier->NbUPoles();v.Degree=bezier->VDegree();v.Poles=bezier->NbVPoles();
        u.Unique=v.Unique={0,1};u.Multiplicities={u.Degree+1,u.Degree+1};v.Multiplicities={v.Degree+1,v.Degree+1};
    }
    double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
    const auto us=detail::Spans(u,u0,u1,isBezier),vs=detail::Spans(v,v0,v1,isBezier);const auto transform=location.Transformation();
    const double weight=isBezier?bezier->Weight(1,1):spline->Weight(1,1);if(!(std::isfinite(weight)&&weight>0))return std::nullopt;
    std::vector<Patch> result;result.reserve(us.size()*vs.size());
    for(const auto& su:us){
        detail::Basis ub(u,su,check);std::vector<polynomial::Curve> basisU;
        for(int i=su.Flat-u.Degree;i<=su.Flat;++i)basisU.push_back(ub.Get(i,u.Degree));
        for(const auto& sv:vs){
            check();detail::Basis vb(v,sv,check);std::vector<polynomial::Curve> basisV;
            for(int j=sv.Flat-v.Degree;j<=sv.Flat;++j)basisV.push_back(vb.Get(j,v.Degree));
            Patch patch{su.First,su.Last,sv.First,sv.Last,su.UniqueFirst,su.UniqueLast,sv.UniqueFirst,sv.UniqueLast,
                polynomial::MakeNet(u.Degree,v.Degree),polynomial::MakeNet(u.Degree,v.Degree),polynomial::MakeNet(u.Degree,v.Degree),
                polynomial::MakeNet(u.Degree,v.Degree),polynomial::MakeNet(u.Degree,v.Degree),polynomial::MakeNet(u.Degree,v.Degree)};
            const int referenceU=su.Flat-u.Degree+1,referenceV=sv.Flat-v.Degree+1;
            const auto reference=isBezier?bezier->Pole(referenceU,referenceV):spline->Pole(referenceU,referenceV);
            const auto offset=detail::Located(reference,transform);
            // On the natural domain the B-spline basis is a partition of
            // unity. Translate its control hull algebraically before the
            // contraction, so a common origin/constant coordinate does not
            // acquire an artificial derivative from interval dependency.
            for(int i=0;i<=u.Degree;++i)for(int j=0;j<=v.Degree;++j){
                check();const int pu=su.Flat-u.Degree+i+1,pv=sv.Flat-v.Degree+j+1;
                const double currentWeight=isBezier?bezier->Weight(pu,pv):spline->Weight(pu,pv);if(currentWeight!=weight)return std::nullopt;
                const auto point=isBezier?bezier->Pole(pu,pv):spline->Pole(pu,pv);const auto located=detail::Located(point,transform,&reference);
                for(int a=0;a<=u.Degree;++a)for(int b=0;b<=v.Degree;++b){
                    const auto basis=polynomial::Multiply(basisU[static_cast<size_t>(i)][static_cast<size_t>(a)],basisV[static_cast<size_t>(j)][static_cast<size_t>(b)]);
                    patch.RelativeX.At(a,b)=polynomial::Add(patch.RelativeX.At(a,b),polynomial::Multiply(located[0],basis));
                    patch.RelativeY.At(a,b)=polynomial::Add(patch.RelativeY.At(a,b),polynomial::Multiply(located[1],basis));
                    patch.RelativeZ.At(a,b)=polynomial::Add(patch.RelativeZ.At(a,b),polynomial::Multiply(located[2],basis));
                }
            }
            for(int a=0;a<=u.Degree;++a)for(int b=0;b<=v.Degree;++b){
                patch.X.At(a,b)=polynomial::Add(offset[0],patch.RelativeX.At(a,b));
                patch.Y.At(a,b)=polynomial::Add(offset[1],patch.RelativeY.At(a,b));
                patch.Z.At(a,b)=polynomial::Add(offset[2],patch.RelativeZ.At(a,b));
            }
            Require(polynomial::Finite(polynomial::Bounds(patch.X))&&polynomial::Finite(polynomial::Bounds(patch.Y))
                &&polynomial::Finite(polynomial::Bounds(patch.Z)),"PLATE_CONTOUR_PATCH_CONTROL");result.push_back(std::move(patch));
        }
    }return result;
}
namespace detail {
polynomial::Interval Difference(double a,double b){
    using namespace polynomial;
    if(!std::isfinite(a)||!std::isfinite(b))return Whole();
    // Sterbenz's lemma: these binary64 differences are exact, including
    // gradual underflow. Other differences retain outward-rounding bounds.
    if(a==b||b==0)return Exact(a-b);
    if((a>0&&b>0&&a/2<=b&&b/2<=a)||(a<0&&b<0&&(-a)/2<=-b&&(-b)/2<=-a))return Exact(a-b);
    return Sub(Exact(a),Exact(b));
}
class PointBasis {
    const Axis& Source;int Span;double Parameter;const Check& Cancel;
    std::map<std::pair<int,int>,polynomial::Interval> Cache;
public:
    PointBasis(const Axis& source,int span,double parameter,const Check& check):Source(source),Span(span),Parameter(parameter),Cancel(check){}
    polynomial::Interval Get(int index,int degree){
        Cancel();const auto key=std::make_pair(index,degree);if(const auto found=Cache.find(key);found!=Cache.end())return found->second;
        using namespace polynomial;auto result=Exact(0);
        if(!degree)result=Exact(index==Span?1.:0.);
        else{
            Require(index>=0&&index+degree+1<static_cast<int>(Source.Knots.size()),"PLATE_CONTOUR_PATCH_BASIS");
            const double left0=Source.Knots[index],left1=Source.Knots[index+degree];
            if(left1>left0)result=Add(result,Multiply(DividePositive(Difference(Parameter,left0),Difference(left1,left0)),Get(index,degree-1)));
            else Require(left1==left0,"PLATE_CONTOUR_PATCH_KNOTS");
            const double right0=Source.Knots[index+1],right1=Source.Knots[index+degree+1];
            if(right1>right0)result=Add(result,Multiply(DividePositive(Difference(right1,Parameter),Difference(right1,right0)),Get(index+1,degree-1)));
            else Require(right1==right0,"PLATE_CONTOUR_PATCH_KNOTS");
        }
        Cache.emplace(key,result);return result;
    }
};
int FlatSpan(const Axis& axis,int first,int last){
    if(!first&&!last)return axis.Degree;
    Require(first>=1&&last==first+1&&last<=static_cast<int>(axis.Unique.size()),"PLATE_CONTOUR_PATCH_KNOTS");
    int after=0;for(int i=0;i<first;++i)after+=axis.Multiplicities[static_cast<size_t>(i)];return after-1;
}
}
struct DerivativeNets {polynomial::Net Ux,Uy,Uz,Vx,Vy,Vz;};
class SourceEquation {
    occ::handle<Geom_BSplineSurface> Spline;
    occ::handle<Geom_BezierSurface> Bezier;
    detail::Axis U,V,DU,DV;
    gp_Trsf LocationTransform;
    std::array<std::array<polynomial::Interval,3>,3> Linear;
    gp_Pnt Pole(int u,int v)const{return Spline.IsNull()?Bezier->Pole(u+1,v+1):Spline->Pole(u+1,v+1);}
public:
    explicit SourceEquation(const TopoDS_Face& face){
        TopLoc_Location location;auto geometry=BRep_Tool::Surface(face,location);
        for(;;){const auto trimmed=occ::down_cast<Geom_RectangularTrimmedSurface>(geometry);if(trimmed.IsNull())break;geometry=trimmed->BasisSurface();}
        Spline=occ::down_cast<Geom_BSplineSurface>(geometry);Bezier=occ::down_cast<Geom_BezierSurface>(geometry);
        Require(!Spline.IsNull()||!Bezier.IsNull(),"PLATE_CONTOUR_SOURCE_UNSUPPORTED");
        if(!Spline.IsNull()){
            Require(!Spline->IsUPeriodic()&&!Spline->IsVPeriodic()&&!Spline->IsURational()&&!Spline->IsVRational(),"PLATE_CONTOUR_SOURCE_UNSUPPORTED");
            U.Degree=Spline->UDegree();U.Poles=Spline->NbUPoles();V.Degree=Spline->VDegree();V.Poles=Spline->NbVPoles();
            for(int i=1;i<=Spline->NbUKnots();++i){U.Unique.push_back(Spline->UKnot(i));U.Multiplicities.push_back(Spline->UMultiplicity(i));}
            for(int i=1;i<=Spline->NbVKnots();++i){V.Unique.push_back(Spline->VKnot(i));V.Multiplicities.push_back(Spline->VMultiplicity(i));}
        }else{
            Require(!Bezier->IsURational()&&!Bezier->IsVRational(),"PLATE_CONTOUR_SOURCE_UNSUPPORTED");
            U.Degree=Bezier->UDegree();U.Poles=Bezier->NbUPoles();V.Degree=Bezier->VDegree();V.Poles=Bezier->NbVPoles();
            U.Unique=V.Unique={0,1};U.Multiplicities={U.Degree+1,U.Degree+1};V.Multiplicities={V.Degree+1,V.Degree+1};
        }
        const auto expand=[](detail::Axis& axis){for(size_t i=0;i<axis.Unique.size();++i)for(int j=0;j<axis.Multiplicities[i];++j)axis.Knots.push_back(axis.Unique[i]);};
        expand(U);expand(V);DU=U;DV=V;DU.Degree--;DU.Poles--;DV.Degree--;DV.Poles--;
        DU.Knots=std::vector<double>(U.Knots.begin()+1,U.Knots.end()-1);DV.Knots=std::vector<double>(V.Knots.begin()+1,V.Knots.end()-1);
        LocationTransform=location.Transformation();const auto& transform=LocationTransform;
        for(int row=0;row<3;++row)for(int column=0;column<3;++column)Linear[row][column]=polynomial::Multiply(polynomial::Exact(transform.ScaleFactor()),polynomial::Exact(transform.HVectorialPart().Value(row+1,column+1)));
    }
    polynomial::Interval Value(const Patch& patch,double u,double v,const Check& check)const{
        using namespace polynomial;
        Require(std::isfinite(u)&&std::isfinite(v)&&u>=patch.U0&&u<=patch.U1&&v>=patch.V0&&v<=patch.V1,"PLATE_CONTOUR_EVALUATION_DOMAIN");
        const int su=detail::FlatSpan(U,patch.UFirst,patch.ULast),sv=detail::FlatSpan(V,patch.VFirst,patch.VLast);
        detail::PointBasis bu(U,su,u,check),bv(V,sv,v,check),bdu(DU,su-1,u,check),bdv(DV,sv-1,v,check);
        std::array<Interval,3> du{},dv{};
        for(int i=su-U.Degree;i<su;++i)for(int j=sv-V.Degree;j<=sv;++j){
            check();const auto a=Pole(i,j),b=Pole(i+1,j);
            const auto factor=Multiply(DividePositive(Exact(U.Degree),detail::Difference(U.Knots[static_cast<size_t>(i+U.Degree+1)],U.Knots[static_cast<size_t>(i+1)])),Multiply(bdu.Get(i,DU.Degree),bv.Get(j,V.Degree)));
            for(int k=0;k<3;++k)du[k]=Add(du[k],Multiply(detail::Difference(b.Coord(k+1),a.Coord(k+1)),factor));
        }
        for(int i=su-U.Degree;i<=su;++i)for(int j=sv-V.Degree;j<sv;++j){
            check();const auto a=Pole(i,j),b=Pole(i,j+1);
            const auto factor=Multiply(DividePositive(Exact(V.Degree),detail::Difference(V.Knots[static_cast<size_t>(j+V.Degree+1)],V.Knots[static_cast<size_t>(j+1)])),Multiply(bu.Get(i,U.Degree),bdv.Get(j,DV.Degree)));
            for(int k=0;k<3;++k)dv[k]=Add(dv[k],Multiply(detail::Difference(b.Coord(k+1),a.Coord(k+1)),factor));
        }
        std::array<Interval,2> xu{},xv{};
        for(int row=0;row<2;++row)for(int k=0;k<3;++k){xu[row]=Add(xu[row],Multiply(Linear[row][k],du[k]));xv[row]=Add(xv[row],Multiply(Linear[row][k],dv[k]));}
        const auto result=Sub(Multiply(xu[0],xv[1]),Multiply(xu[1],xv[0]));Require(Finite(result),"PLATE_CONTOUR_EQUATION_INVALID");return result;
    }
    polynomial::Interval ExactValue(const Patch& patch,double u,double v,const Check& check)const{
        Require(std::isfinite(u)&&std::isfinite(v)&&u>=patch.U0&&u<=patch.U1&&v>=patch.V0&&v<=patch.V1,"PLATE_CONTOUR_EVALUATION_DOMAIN");
        const int su=detail::FlatSpan(U,patch.UFirst,patch.ULast),sv=detail::FlatSpan(V,patch.VFirst,patch.VLast);
        const std::function<gp_Pnt(int,int)> pole=[this](int i,int j){return Pole(i,j);};
        return contour_rational::Value(U.Degree,V.Degree,U.Knots,V.Knots,su,sv,pole,LocationTransform,u,v,check);
    }
    DerivativeNets Polynomials(const Patch& patch,const Check& check)const{
        using namespace polynomial;
        const int su=detail::FlatSpan(U,patch.UFirst,patch.ULast),sv=detail::FlatSpan(V,patch.VFirst,patch.VLast);
        const detail::Span us{patch.U0,patch.U1,su,patch.UFirst,patch.ULast},vs{patch.V0,patch.V1,sv,patch.VFirst,patch.VLast};
        const detail::Span dus{patch.U0,patch.U1,su-1,0,0},dvs{patch.V0,patch.V1,sv-1,0,0};
        detail::Basis ub(U,us,check),vb(V,vs,check),dub(DU,dus,check),dvb(DV,dvs,check);
        std::vector<polynomial::Curve> bu,bv,bdu,bdv;
        for(int i=su-U.Degree;i<=su;++i)bu.push_back(ub.Get(i,U.Degree));
        for(int j=sv-V.Degree;j<=sv;++j)bv.push_back(vb.Get(j,V.Degree));
        for(int i=su-U.Degree;i<su;++i)bdu.push_back(dub.Get(i,DU.Degree));
        for(int j=sv-V.Degree;j<sv;++j)bdv.push_back(dvb.Get(j,DV.Degree));
        DerivativeNets result{MakeNet(DU.Degree,V.Degree),MakeNet(DU.Degree,V.Degree),MakeNet(DU.Degree,V.Degree),
            MakeNet(U.Degree,DV.Degree),MakeNet(U.Degree,DV.Degree),MakeNet(U.Degree,DV.Degree)};
        const auto located=[&](const gp_Pnt& a,const gp_Pnt& b,const Interval& factor){
            std::array<Interval,3> delta{},value{};
            for(int k=0;k<3;++k)delta[k]=Multiply(detail::Difference(b.Coord(k+1),a.Coord(k+1)),factor);
            for(int row=0;row<3;++row)for(int k=0;k<3;++k)value[row]=Add(value[row],Multiply(Linear[row][k],delta[k]));
            for(const auto& coordinate:value)Require(Finite(coordinate),"PLATE_CONTOUR_DERIVATIVE_CONTROL");return value;
        };
        // Differentiate ORIGINAL control points before any interval basis
        // contraction. Differentiating an already bounded position net would
        // lose common-point correlation, including an exact symmetry iso.
        for(int i=su-U.Degree;i<su;++i)for(int j=sv-V.Degree;j<=sv;++j){
            check();const auto factor=DividePositive(Exact(U.Degree),detail::Difference(U.Knots[static_cast<size_t>(i+U.Degree+1)],U.Knots[static_cast<size_t>(i+1)]));
            const auto control=located(Pole(i,j),Pole(i+1,j),factor);
            const auto& cu=bdu[static_cast<size_t>(i-(su-U.Degree))];const auto& cv=bv[static_cast<size_t>(j-(sv-V.Degree))];
            for(int a=0;a<=DU.Degree;++a)for(int b=0;b<=V.Degree;++b){
                const auto basis=Multiply(cu[static_cast<size_t>(a)],cv[static_cast<size_t>(b)]);
                result.Ux.At(a,b)=Add(result.Ux.At(a,b),Multiply(control[0],basis));
                result.Uy.At(a,b)=Add(result.Uy.At(a,b),Multiply(control[1],basis));
                result.Uz.At(a,b)=Add(result.Uz.At(a,b),Multiply(control[2],basis));
            }
        }
        for(int i=su-U.Degree;i<=su;++i)for(int j=sv-V.Degree;j<sv;++j){
            check();const auto factor=DividePositive(Exact(V.Degree),detail::Difference(V.Knots[static_cast<size_t>(j+V.Degree+1)],V.Knots[static_cast<size_t>(j+1)]));
            const auto control=located(Pole(i,j),Pole(i,j+1),factor);
            const auto& cu=bu[static_cast<size_t>(i-(su-U.Degree))];const auto& cv=bdv[static_cast<size_t>(j-(sv-V.Degree))];
            for(int a=0;a<=U.Degree;++a)for(int b=0;b<=DV.Degree;++b){
                const auto basis=Multiply(cu[static_cast<size_t>(a)],cv[static_cast<size_t>(b)]);
                result.Vx.At(a,b)=Add(result.Vx.At(a,b),Multiply(control[0],basis));
                result.Vy.At(a,b)=Add(result.Vy.At(a,b),Multiply(control[1],basis));
                result.Vz.At(a,b)=Add(result.Vz.At(a,b),Multiply(control[2],basis));
            }
        }
        for(const auto* net:{&result.Ux,&result.Uy,&result.Uz,&result.Vx,&result.Vy,&result.Vz})Require(Finite(Bounds(*net)),"PLATE_CONTOUR_DERIVATIVE_CONTROL");
        return result;
    }
};
polynomial::Interval OriginalEquation(const TopoDS_Face& face,const Patch& patch,double u,double v,const Check& check){return SourceEquation(face).Value(patch,u,v,check);}
}
