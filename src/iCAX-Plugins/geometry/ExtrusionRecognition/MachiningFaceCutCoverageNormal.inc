// Included in the coverage Work's private namespace. This branch certifies
// an entire normal family on a simple four-boundary planar strip. Control
// nets prove the geometry; native curve evaluations below only solve roots
// and label the already certified components between boundary events.
#include "MachiningFaceCutCoverageStraight.inc"

namespace normal_strip
{
using L=long double;
struct V final {L X=0,Y=0;};
V operator+(V a,V b){return {a.X+b.X,a.Y+b.Y};}
V operator-(V a,V b){return {a.X-b.X,a.Y-b.Y};}
V operator*(V a,L b){return {a.X*b,a.Y*b};}
L Dot(V a,V b){return a.X*b.X+a.Y*b.Y;}
L Abs(V a){return std::max(std::abs(a.X),std::abs(a.Y));}
L Guard(L scale){return 8192.L*std::numeric_limits<L>::epsilon()*std::max(1.L,scale);}
bool Finite(V a){return std::isfinite(a.X)&&std::isfinite(a.Y);}

// IEEE outward intervals track the original absolute construction terms.
// MSVC long double is double; no extended-precision assumption is made.
struct Interval final {L Lo=0.,Hi=0.;};
L Down(L value){return std::nextafter(value,-std::numeric_limits<L>::infinity());}
L Up(L value){return std::nextafter(value,std::numeric_limits<L>::infinity());}
Interval Exact(L value){return {value,value};}
bool Zero(Interval value){return value.Lo==0.&&value.Hi==0.;}
Interval operator+(Interval a,Interval b)
{
    if(Zero(a))return b;if(Zero(b))return a;
    if(a.Lo==a.Hi&&b.Lo==b.Hi&&a.Lo==-b.Lo)return {};
    return {Down(a.Lo+b.Lo),Up(a.Hi+b.Hi)};
}
Interval operator-(Interval a,Interval b)
{
    if(Zero(b))return a;if(a.Lo==a.Hi&&b.Lo==b.Hi&&a.Lo==b.Lo)return {};
    return {Down(a.Lo-b.Hi),Up(a.Hi-b.Lo)};
}
Interval operator*(Interval a,Interval b)
{
    if(Zero(a)||Zero(b))return {};
    const L p[]{a.Lo*b.Lo,a.Lo*b.Hi,a.Hi*b.Lo,a.Hi*b.Hi};
    return {Down(*std::min_element(p,p+4)),Up(*std::max_element(p,p+4))};
}
Interval operator/(Interval a,Interval b)
{
    if(!(b.Lo>0.))throw std::runtime_error("normal-strip-interval-denominator-not-positive");
    if(Zero(a))return {};
    return a*Interval{Down(1.L/b.Hi),Up(1.L/b.Lo)};
}
struct IV final {Interval X,Y;};
IV operator+(IV a,IV b){return {a.X+b.X,a.Y+b.Y};}
IV operator-(IV a,IV b){return {a.X-b.X,a.Y-b.Y};}
IV operator*(IV a,Interval b){return {a.X*b,a.Y*b};}
Interval Around(L value,L error)
{
    if(error==0.)return Exact(value);return {Down(value-error),Up(value+error)};
}
Interval DotInterval(V a,L aError,V b,L bError)
{
    return Around(a.X,aError)*Around(b.X,bError)+Around(a.Y,aError)*Around(b.Y,bError);
}
L Mid(Interval value){return value.Lo*.5L+value.Hi*.5L;}
L Error(Interval value,L center){return Up(std::max(center-value.Lo,value.Hi-center));}

L Choose(int n,int k)
{
    if(k<0||k>n)return 0.;k=std::min(k,n-k);L value=1.;
    for(int i=1;i<=k;++i)value=value*L(n-k+i)/L(i);return value;
}
template<class T> std::vector<T> IntervalDerivative(const std::vector<T>& values,Interval width)
{
    if(values.size()<2)return {T{}};std::vector<T> result(values.size()-1);
    const Interval factor=Exact(L(result.size()))/width;
    for(std::size_t i=0;i<result.size();++i)result[i]=(values[i+1]-values[i])*factor;
    return result;
}
std::vector<IV> IntervalProduct(const std::vector<IV>& a,const std::vector<Interval>& b)
{
    const int p=int(a.size())-1,q=int(b.size())-1;std::vector<IV> result(p+q+1);
    for(int k=0;k<=p+q;++k)for(int i=std::max(0,k-q);i<=std::min(p,k);++i) {
        const Interval coefficient=(Exact(Choose(p,i))*Exact(Choose(q,k-i)))/Exact(Choose(p+q,k));
        result[k]=result[k]+a[i]*(b[k-i]*coefficient);
    }
    return result;
}
template<class T> std::vector<T> Derivative(const std::vector<T>& values,L width)
{
    if(values.size()<2)return {T{}};std::vector<T> result(values.size()-1);
    for(std::size_t i=0;i<result.size();++i)result[i]=(values[i+1]-values[i])*(L(result.size())/width);
    return result;
}
std::vector<V> Product(const std::vector<V>& a,const std::vector<L>& b)
{
    const int p=int(a.size())-1,q=int(b.size())-1;std::vector<V> result(p+q+1);
    for(int k=0;k<=p+q;++k)for(int i=std::max(0,k-q);i<=std::min(p,k);++i)
        result[k]=result[k]+a[i]*(b[k-i]*Choose(p,i)*Choose(q,k-i)/Choose(p+q,k));
    return result;
}
std::vector<V> Difference(const std::vector<V>& a,const std::vector<V>& b,L factor=1.)
{
    if(a.size()!=b.size())throw std::logic_error("normal-strip-polynomial-degree-mismatch");
    auto result=a;for(std::size_t i=0;i<a.size();++i)result[i]=a[i]-b[i]*factor;return result;
}
struct Piece final
{
    occ::handle<Geom2d_BezierCurve> Curve;
    double First=0.,Last=0.;bool Reversed=false;
    std::vector<V> P,R,S;
    // Componentwise absolute coefficient enclosures, including cancellation
    // and the subtraction of the common geometric origin.
    L PError=0.,RError=0.,SError=0.;
    std::vector<bool> RExactZero;
    L WMin=1.,WMax=1.;
};
bool MakePiece(const occ::handle<Geom2d_BezierCurve>& curve,double first,double last,
    bool reversed,const gp_Pnt2d& origin,Piece& result)
{
    if(curve.IsNull()||!(last>first)||curve->Degree()<1||curve->Degree()>12)return false;
    result.Curve=curve;result.First=first;result.Last=last;result.Reversed=reversed;
    std::vector<IV> originalPoles;std::vector<Interval> w;result.WMin=std::numeric_limits<L>::infinity();result.WMax=0.;
    for(int i=1;i<=curve->NbPoles();++i) {
        const auto p=curve->Pole(i);const L weight=curve->Weight(i);
        const V point{L(p.X())-L(origin.X()),L(p.Y())-L(origin.Y())};
        if(!Finite(point)||!std::isfinite(weight)||weight<=0.)return false;
        const IV local{Exact(L(p.X()))-Exact(L(origin.X())),Exact(L(p.Y()))-Exact(L(origin.Y()))};
        result.P.push_back(point);result.PError=std::max({result.PError,Error(local.X,point.X),Error(local.Y,point.Y)});
        // Retain absolute original poles for the translation-free pair
        // differences in R. Equal original poles remain structurally zero.
        originalPoles.push_back({Exact(L(curve->Pole(i).X())),Exact(L(curve->Pole(i).Y()))});w.push_back(Exact(weight));
        result.WMin=std::min(result.WMin,weight);result.WMax=std::max(result.WMax,weight);
    }
    const Interval width=Exact(L(last))-Exact(L(first));const int degree=curve->Degree();
    std::vector<IV> r(2*degree);result.RExactZero.assign(r.size(),true);
    // N'W-NW' in Bernstein form, grouped by original pole differences:
    // each pair i<j contributes at k=i+j-1 and k=i+j with a positive
    // combinatorial coefficient. This avoids large position-term subtraction.
    for(int i=0;i<=degree;++i)for(int j=i+1;j<=degree;++j)for(const int k:{i+j-1,i+j}) {
        const IV difference=originalPoles[j]-originalPoles[i];
        const Interval coefficient=(Exact(L(j-i))*Exact(Choose(degree,i))*Exact(Choose(degree,j)))
            /Exact(Choose(2*degree-1,k))/width;
        r[k]=r[k]+difference*(w[i]*w[j]*coefficient);
        result.RExactZero[k]=result.RExactZero[k]&&Zero(difference.X)&&Zero(difference.Y);
    }
    const auto ww=IntervalDerivative(w,width);
    auto s=IntervalProduct(IntervalDerivative(r,width),w);
    const auto subtract=IntervalProduct(r,ww);
    for(std::size_t i=0;i<s.size();++i)s[i]=s[i]-subtract[i]*Exact(2.);
    for(std::size_t i=0;i<r.size();++i) {
        const V value{Mid(r[i].X),Mid(r[i].Y)};result.R.push_back(value);
        if(!result.RExactZero[i])result.RError=std::max({result.RError,Error(r[i].X,value.X),Error(r[i].Y,value.Y)});
    }
    for(const auto& coefficient:s) {
        const V value{Mid(coefficient.X),Mid(coefficient.Y)};result.S.push_back(value);
        result.SError=std::max({result.SError,Error(coefficient.X,value.X),Error(coefficient.Y,value.Y)});
    }
    if(!std::isfinite(result.RError)||!std::isfinite(result.SError)||!std::isfinite(result.PError))return false;
    return std::all_of(result.R.begin(),result.R.end(),Finite)&&std::all_of(result.S.begin(),result.S.end(),Finite);
}
occ::handle<Geom2d_Curve> Basis(occ::handle<Geom2d_Curve> curve)
{
    while(!curve.IsNull()&&curve->IsKind(STANDARD_TYPE(Geom2d_TrimmedCurve)))
        curve=occ::down_cast<Geom2d_TrimmedCurve>(curve)->BasisCurve();
    return curve;
}
bool Extract(const CurveUse& use,bool source,bool reversed,const gp_Pnt2d& origin,
    std::vector<Piece>& pieces,std::string& reason)
{
    const auto base=Basis(use.Curve);Geom2dAdaptor_Curve adaptor(base,use.First,use.Last);
    if(adaptor.GetType()==GeomAbs_Line) {
        NCollection_Array1<gp_Pnt2d> poles(1,2);poles(1)=base->Value(use.First);poles(2)=base->Value(use.Last);
        Piece piece;if(!MakePiece(new Geom2d_BezierCurve(poles),use.First,use.Last,reversed,origin,piece))return false;
        pieces.push_back(std::move(piece));return true;
    }
    if(adaptor.GetType()==GeomAbs_BezierCurve) {
        const auto curve=occ::down_cast<Geom2d_BezierCurve>(adaptor.Bezier()->Copy());
        curve->Segment(use.First,use.Last);Piece piece;
        if(!MakePiece(curve,use.First,use.Last,reversed,origin,piece))return false;
        pieces.push_back(std::move(piece));return true;
    }
    if(adaptor.GetType()!=GeomAbs_BSplineCurve) {reason="normal-strip-native-pcurve-is-not-line-bezier-or-bspline";return false;}
    const auto spline=adaptor.BSpline();
    if(spline->IsPeriodic()) {reason="normal-strip-periodic-pcurve-needs-seam-proof";return false;}
    for(int i=2;i<spline->NbKnots();++i)if(spline->Knot(i)>use.First&&spline->Knot(i)<use.Last
        &&spline->Multiplicity(i)>spline->Degree()-(source?1:0)) {
        reason=source?"normal-strip-source-pcurve-is-not-C1":"normal-strip-target-pcurve-is-not-C0";return false;
    }
    const auto trimmed=occ::down_cast<Geom2d_BSplineCurve>(spline->Copy());
    trimmed->Segment(use.First,use.Last,0.);
    if(trimmed->FirstParameter()!=use.First||trimmed->LastParameter()!=use.Last) {
        reason="normal-strip-extraction-changed-native-source-range";return false;
    }
    Geom2dConvert_BSplineCurveToBezierCurve extraction(trimmed);
    NCollection_Array1<double> knots(1,extraction.NbArcs()+1);extraction.Knots(knots);
    for(int i=1;i<=extraction.NbArcs();++i) {
        Piece piece;if(!MakePiece(extraction.Arc(i),knots(i),knots(i+1),reversed,origin,piece)) {
            reason="normal-strip-bezier-weights-or-polynomial-controls-invalid";return false;
        }
        pieces.push_back(std::move(piece));
    }
    return !pieces.empty();
}
void Refine(std::vector<Piece>& pieces,const gp_Pnt2d& origin)
{
    std::vector<Piece> result;
    for(const auto& piece:pieces)for(int half=0;half<2;++half) {
        const auto curve=occ::down_cast<Geom2d_BezierCurve>(piece.Curve->Copy());
        curve->Segment(half*.5,(half+1)*.5);const double middle=piece.First+(piece.Last-piece.First)*.5;Piece next;
        if(!MakePiece(curve,half?middle:piece.First,half?piece.Last:middle,piece.Reversed,origin,next))
            throw std::runtime_error("normal-strip-de-Casteljau-refinement-failed");
        result.push_back(std::move(next));
    }
    pieces=std::move(result);
}
std::vector<V> Tangents(const std::vector<Piece>& pieces,bool oriented)
{
    std::vector<V> result;
    for(const auto& p:pieces)for(const auto r:p.R)result.push_back(r*((oriented&&p.Reversed)?-1.L:1.L));
    return result;
}
std::pair<L,L> DotBounds(const std::vector<V>& a,const std::vector<V>& b)
{
    L lower=std::numeric_limits<L>::infinity(),upper=-lower;
    for(const auto x:a)for(const auto y:b){const L value=Dot(x,y);lower=std::min(lower,value);upper=std::max(upper,value);}
    return {lower,upper};
}
std::pair<L,L> DotBounds(const std::vector<V>& a,L aError,const std::vector<V>& b,L bError)
{
    L lower=std::numeric_limits<L>::infinity(),upper=-lower;
    for(const auto x:a)for(const auto y:b){const auto value=DotInterval(x,aError,y,bError);lower=std::min(lower,value.Lo);upper=std::max(upper,value.Hi);}
    return {lower,upper};
}
struct Certificate final {L SelfLower=0,CrossLower=0,CrossUpper=0,FuUpper=0;std::size_t Pieces=0;};
bool Certify(const std::vector<Piece>& source,const std::vector<Piece>& target,
    const std::vector<Piece>& sides,Certificate& certificate)
{
    const auto s=Tangents(source,true),t=Tangents(target,true);
    L sourceError=0.,targetError=0.;
    for(const auto& piece:source)sourceError=std::max(sourceError,piece.RError);
    for(const auto& piece:target)targetError=std::max(targetError,piece.RError);
    const auto self=DotBounds(s,sourceError,s,sourceError),cross=DotBounds(s,sourceError,t,targetError);
    const L scale=std::max({std::abs(self.first),std::abs(self.second),std::abs(cross.first),std::abs(cross.second)});
    if(self.first<=Guard(scale)||!(cross.first>Guard(scale)||cross.second<-Guard(scale)))return false;
    struct Position final {V Point;L Error=0.;};std::vector<Position> boundary;
    for(const auto* collection:{&source,&target,&sides})for(const auto& piece:*collection)
        for(const auto point:piece.P)boundary.push_back({point,piece.PError});
    L fuUpper=-std::numeric_limits<L>::infinity();
    for(const auto& piece:source) {
        const auto derivative=DotBounds(piece.R,piece.RError,piece.R,piece.RError);L maxDot=-std::numeric_limits<L>::infinity();
        for(const auto q:boundary)for(const auto c:piece.P)for(const auto second:piece.S)
        {
            const Interval dx=Around(q.Point.X,q.Error)-Around(c.X,piece.PError);
            const Interval dy=Around(q.Point.Y,q.Error)-Around(c.Y,piece.PError);
            const Interval dot=dx*Around(second.X,piece.SError)+dy*Around(second.Y,piece.SError);
            maxDot=std::max(maxDot,dot.Hi);
        }
        // W is positive. A negative dot upper bound must multiply Wmin.
        const L upper=Up((Exact(maxDot)*Interval{piece.WMin,piece.WMax}).Hi-derivative.first);
        const L localScale=std::max(std::abs(maxDot)*piece.WMax,std::abs(derivative.second));
        if(!std::isfinite(upper)||upper>=-Guard(localScale))return false;
        fuUpper=std::max(fuUpper,upper);
    }
    certificate={self.first,cross.first,cross.second,fuUpper,source.size()+target.size()+sides.size()};return true;
}
struct Boundary final
{
    const SMachiningPathEdgeAnalysis* Path=nullptr;
    CurveUse Use;
    TopoDS_Vertex FirstVertex,LastVertex;
    bool Source=false,Opposite=false;
};
TopoDS_Vertex VertexAt(const Boundary& boundary,double parameter)
{
    return parameter==boundary.Use.First?boundary.FirstVertex:
        parameter==boundary.Use.Last?boundary.LastVertex:TopoDS_Vertex{};
}
bool ReadBoundary(const TopoDS_Face& face,const O::SOpenCascadeBRepBuildResult& built,
    const SMachiningPathEdgeAnalysis& path,Boundary& result,std::string& reason)
{
    if(path.SourceUses.size()!=1||path.bClosed) {reason="normal-strip-requires-one-open-original-use-per-logical-boundary";return false;}
    const auto& use=path.SourceUses.front();const auto found=built.Edges.find(use.EdgeId);
    if(found==built.Edges.end()) {reason="normal-strip-original-edge-missing";return false;}
    double a=0.,b=0.;BRep_Tool::Range(found->second,a,b);
    if(use.FirstParameter!=a||use.LastParameter!=b) {reason="normal-strip-boundary-is-not-one-whole-original-edge";return false;}
    Chart identity;
    if(!ReadCurve(face,found->second,use.EdgeId,a,b,identity,result.Use,reason))return false;
    if(result.Use.First!=a||result.Use.Last!=b) {reason="normal-strip-read-changed-native-range";return false;}
    result.Path=&path;result.Use.bReversed=use.bReversed;
    TopExp::Vertices(found->second,result.FirstVertex,result.LastVertex,false);
    return true;
}
bool SameVertex(const TopoDS_Vertex& a,const TopoDS_Vertex& b){return !a.IsNull()&&!b.IsNull()&&a.IsSame(b);}
bool StructuralLine(const CurveUse& use)
{
    const auto curve=Basis(use.Curve);Geom2dAdaptor_Curve native(curve,use.First,use.Last);
    return native_straight_boundary::Certified(native,use.First,use.Last);
}
bool Connects(const Boundary& side,const Boundary& source,const Boundary& target)
{
    const bool a=SameVertex(side.FirstVertex,source.FirstVertex)||SameVertex(side.FirstVertex,source.LastVertex);
    const bool b=SameVertex(side.LastVertex,source.FirstVertex)||SameVertex(side.LastVertex,source.LastVertex);
    const bool c=SameVertex(side.FirstVertex,target.FirstVertex)||SameVertex(side.FirstVertex,target.LastVertex);
    const bool d=SameVertex(side.LastVertex,target.FirstVertex)||SameVertex(side.LastVertex,target.LastVertex);
    return (a&&d&&!b&&!c)||(b&&c&&!a&&!d);
}
struct Ref final {std::uint64_t Edge=0;double Parameter=0.;};
struct Event final {double SourceParameter=0.;std::vector<Ref> Refs;};
struct Seed final {gp_Pnt2d Point;TopoDS_Vertex Vertex;std::vector<Ref> Refs;bool Forced=false;double Parameter=0.;};
void AddSeed(std::vector<Seed>& seeds,const Boundary& boundary,double parameter,bool force=false)
{
    const auto vertex=VertexAt(boundary,parameter);
    for(auto& seed:seeds) {
        const bool same=SameVertex(seed.Vertex,vertex)||std::any_of(seed.Refs.begin(),seed.Refs.end(),[&](const auto& ref){
            return ref.Edge==boundary.Use.EdgeId&&ref.Parameter==parameter;});
        if(!same)continue;
        seed.Refs.push_back({boundary.Use.EdgeId,parameter});
        if(force){seed.Forced=true;seed.Parameter=parameter;seed.Point=boundary.Use.Curve->Value(parameter);}
        return;
    }
    seeds.push_back({boundary.Use.Curve->Value(parameter),vertex,{{boundary.Use.EdgeId,parameter}},force,parameter});
}
double F(const CurveUse& source,const gp_Pnt2d& point,double parameter)
{
    gp_Pnt2d p;gp_Vec2d d;source.Curve->D1(parameter,p,d);
    return gp_Vec2d(p,point).Dot(d);
}
bool ProjectionEvent(const CurveUse& source,const Seed& seed,double& parameter,bool& exists,std::string& reason)
{
    exists=false;if(seed.Forced){parameter=seed.Parameter;exists=true;return true;}
    double lower=source.First,upper=source.Last;
    double fl=F(source,seed.Point,lower),fu=F(source,seed.Point,upper);
    if(!std::isfinite(fl)||!std::isfinite(fu)) {reason="normal-strip-native-event-function-not-finite";return false;}
    // Fu<0 was proved on every source knot span, so there is at most one
    // stationary normal root. This is root isolation, never a sampling grid.
    if(fl==0.){parameter=lower;exists=true;return true;}
    if(fu==0.){parameter=upper;exists=true;return true;}
    if(fl<0.||fu>0.)return true;
    for(int iteration=0;iteration<96;++iteration) {
        const double middle=lower+(upper-lower)*.5;if(middle==lower||middle==upper)break;
        const double fm=F(source,seed.Point,middle);
        if(!std::isfinite(fm)) {reason="normal-strip-native-event-root-not-finite";return false;}
        if(fm==0.){parameter=middle;exists=true;return true;}
        if(fm>0.)lower=middle;else upper=middle;
    }
    parameter=lower+(upper-lower)*.5;exists=true;return true;
}
#include "MachiningFaceCutCoverageNormalRay.inc"

}

bool MapNormalStripSource(const TopoDS_Face& face,const O::SOpenCascadeBRepBuildResult& built,
    const SMachiningFaceEdgeAnalysis& boundary,const SMachiningPathEdgeAnalysis& path,
    const SMachiningPathAnalysisResult& allPaths,const std::vector<const SMachiningEdgeAnalysis*>& attributes,
    const SMachiningFaceCutCoverageOptions& options,std::vector<SMachiningFaceCutArrival>& arrivals,
    std::string& method,std::string& reason)
{
    using namespace normal_strip;
    method="plane-certified-normal-strip-first-boundary";
    const auto reject=[&](const std::string& message){reason=message;return false;};
    if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane||path.bClosed||!path.bSelectedPath)
        return reject("normal-strip-requires-open-native-plane-skin-source");
    if(boundary.Wires.size()!=1||!boundary.Wires.front().bOrdered||boundary.Wires.front().WireId!=path.WireId)
        return reject("normal-strip-requires-one-ordered-wire-without-holes");
    if(!BRepCheck_Analyzer(face).IsValid())return reject("normal-strip-native-trimmed-face-is-invalid");
    std::vector<const SMachiningPathEdgeAnalysis*> records,skins,sides;
    for(const auto& record:allPaths.Edges)if(record.FaceId==path.FaceId&&record.WireId==path.WireId) {
        records.push_back(&record);if(record.bSelectedPath&&record.bOnSkinPath)skins.push_back(&record);
        else if(record.bSide&&!record.bClosed)sides.push_back(&record);
    }
    if(records.size()!=4||skins.size()!=2||sides.size()!=2)
        return reject("normal-strip-requires-exactly-two-skin-chains-and-two-open-sides");
    std::vector<Boundary> boundaries(4);const auto opposite=skins[0]->Key==path.Key?skins[1]:skins[0];
    if(!ReadBoundary(face,built,path,boundaries[0],reason)||!ReadBoundary(face,built,*opposite,boundaries[1],reason)
        ||!ReadBoundary(face,built,*sides[0],boundaries[2],reason)||!ReadBoundary(face,built,*sides[1],boundaries[3],reason))return false;
    boundaries[0].Source=true;boundaries[1].Opposite=true;
    std::set<std::uint64_t> ids;for(const auto& b:boundaries)ids.insert(b.Use.EdgeId);
    std::set<std::uint64_t> actual;
    for(const auto& group:boundary.Wires.front().Groups)for(const auto& use:group.SourceUses)actual.insert(use.EdgeId);
    if(ids.size()!=4||actual!=ids)return reject("normal-strip-physical-wire-is-not-exactly-four-original-edges");
    for(int i=2;i<4;++i) {
        if(!native_straight_boundary::Certified(BRepAdaptor_Curve(built.Edges.at(boundaries[i].Use.EdgeId)))
            ||!StructuralLine(boundaries[i].Use)||!Connects(boundaries[i],boundaries[0],boundaries[1]))
            return reject("normal-strip-sides-are-not-native-lines-joining-the-real-skin-chain-endpoints");
    }
    for(const auto& b:boundaries)for(const auto parameter:{b.Use.First,b.Use.Last}) {
        const auto vertex=VertexAt(b,parameter);
        if(vertex.IsNull())return reject("normal-strip-physical-corner-has-no-original-topological-vertex");
        const auto uv=b.Use.Curve->Value(parameter);const auto point=BRepAdaptor_Surface(face).Value(uv.X(),uv.Y());
        if(point.Distance(BRep_Tool::Pnt(vertex))>std::min(BRep_Tool::Tolerance(vertex),BRep_Tool::Tolerance(built.Edges.at(b.Use.EdgeId))))
            return reject("normal-strip-pcurve-corner-exceeds-original-edge-or-vertex-tolerance");
    }
    if(path.Samples.size()!=2||!path.Samples.front().bToolAxisValid||!path.Samples.back().bToolAxisValid)
        return reject("normal-strip-source-has-no-two-recorded-ready-axes");
    const auto plane=BRepAdaptor_Surface(face).Plane();double direction=0.;
    for(const auto& endpoint:path.Samples) {
        gp_Pnt2d point;gp_Vec2d tangent;boundaries[0].Use.Curve->D1(endpoint.Parameter,point,tangent);
        const auto axis=Vec(endpoint.ToolAxis);const gp_Vec2d uvAxis(axis.Dot(gp_Vec(plane.Position().XDirection())),axis.Dot(gp_Vec(plane.Position().YDirection())));
        gp_Vec2d normal(-tangent.Y(),tangent.X());
        if(normal.SquareMagnitude()<=1.e-30||uvAxis.SquareMagnitude()<=1.e-30)return reject("normal-strip-recorded-normal-frame-is-singular");
        const double alignment=normal.Dot(uvAxis)/(normal.Magnitude()*uvAxis.Magnitude());
        if(std::abs(alignment)<std::cos(options.dAngularToleranceRadians))return reject("normal-strip-recorded-axis-is-not-the-native-plane-normal-family");
        const double sign=alignment>0.?1.:-1.;if(direction&&direction!=sign)return reject("normal-strip-recorded-endpoint-axis-senses-disagree");direction=sign;
    }
    if(direction!=(path.SourceUses.front().bReversed?-1.:1.))
        return reject("normal-strip-recorded-axis-is-not-the-original-wire-interior-conormal");
    const auto origin=boundaries[0].Use.Curve->Value(boundaries[0].Use.First);
    std::vector<Piece> sourcePieces,targetPieces,sidePieces;
    if(!Extract(boundaries[0].Use,true,path.SourceUses.front().bReversed,origin,sourcePieces,reason)
        ||!Extract(boundaries[1].Use,false,opposite->SourceUses.front().bReversed,origin,targetPieces,reason))return false;
    for(int i=2;i<4;++i)if(!Extract(boundaries[i].Use,false,boundaries[i].Path->SourceUses.front().bReversed,origin,sidePieces,reason))return false;
    Certificate certificate;bool certified=false;
    for(int refinement=0;refinement<=4;++refinement) {
        if(Certify(sourcePieces,targetPieces,sidePieces,certificate)){certified=true;break;}
        if(refinement==4||sourcePieces.size()+targetPieces.size()+sidePieces.size()>128)break;
        Refine(sourcePieces,origin);Refine(targetPieces,origin);Refine(sidePieces,origin);
    }
    if(!certified)return reject("normal-strip-continuous-control-net-certificate-not-proven:self-tangent-cone-cross-tangent-or-Fu");
    std::vector<Seed> seeds;
    AddSeed(seeds,boundaries[0],boundaries[0].Use.First,true);AddSeed(seeds,boundaries[0],boundaries[0].Use.Last,true);
    for(std::size_t i=1;i<boundaries.size();++i) {
        const auto& b=boundaries[i];AddSeed(seeds,b,b.Use.First);AddSeed(seeds,b,b.Use.Last);
        for(const auto* attr:attributes)if(attr->EdgeId==b.Use.EdgeId&&HasFace(*attr,path.FaceId)) {
            if(attr->FirstParameter>b.Use.First&&attr->FirstParameter<b.Use.Last)AddSeed(seeds,b,attr->FirstParameter);
            if(attr->LastParameter>b.Use.First&&attr->LastParameter<b.Use.Last)AddSeed(seeds,b,attr->LastParameter);
        }
    }
    std::vector<Event> events;
    for(const auto& seed:seeds) {
        double parameter=0.;bool exists=false;
        if(!ProjectionEvent(boundaries[0].Use,seed,parameter,exists,reason))return false;
        if(exists)events.push_back({parameter,seed.Refs});
    }
    std::sort(events.begin(),events.end(),[](const auto& a,const auto& b){return a.SourceParameter<b.SourceParameter;});
    std::vector<Event> unique;
    for(auto& event:events) {
        if(!unique.empty()&&unique.back().SourceParameter==event.SourceParameter)
            unique.back().Refs.insert(unique.back().Refs.end(),event.Refs.begin(),event.Refs.end());
        else unique.push_back(std::move(event));
    }
    std::vector<SMachiningFaceCutArrival> proven;bool complete=true;
    for(std::size_t i=1;i<unique.size();++i) {
        const double first=unique[i-1].SourceParameter,last=unique[i].SourceParameter;
        if(!(last>first))continue;
        const double witness=first+(last-first)*.5;
        if(witness==first||witness==last){complete=false;reason="normal-strip-positive-native-component-has-no-representable-interior-witness";continue;}
        std::vector<Hit> hits;std::string intervalReason;
        if(!RayHits(boundaries[0].Use,witness,direction,boundaries,options,hits,intervalReason)||hits.empty()) {
            complete=false;reason=intervalReason.empty()?"normal-strip-certified-component-has-no-positive-first-boundary":intervalReason;continue;
        }
        std::sort(hits.begin(),hits.end(),[](const auto& a,const auto& b){return a.Depth<b.Depth;});
        if(hits.size()>1&&hits[0].Depth==hits[1].Depth) {complete=false;reason="normal-strip-first-boundary-tie-on-certified-component";continue;}
        const auto& target=*hits.front().Target;double a=0.,b=0.;
        if(!AtEvent(boundaries[0].Use,unique[i-1],target,direction,boundaries,options,a,intervalReason)
            ||!AtEvent(boundaries[0].Use,unique[i],target,direction,boundaries,options,b,intervalReason)) {
            complete=false;reason=intervalReason;continue;
        }
        if(a==b) {complete=false;reason="normal-strip-positive-source-component-collapsed-on-native-target";continue;}
        const double lo=std::min(a,b),hi=std::max(a,b);const auto attr=std::find_if(attributes.begin(),attributes.end(),[&](const auto* e){
            return e->EdgeId==target.Use.EdgeId&&HasFace(*e,path.FaceId)&&e->FirstParameter<=lo&&e->LastParameter>=hi;});
        if(attr==attributes.end()) {complete=false;reason="normal-strip-component-crosses-an-unresolved-native-target-attribute-event";continue;}
        SMachiningFaceCutArrival arrival;arrival.FaceId=path.FaceId;arrival.SourcePathKey=path.Key;
        arrival.SourceEdgeId=boundaries[0].Use.EdgeId;arrival.SourceFirstParameter=a<b?first:last;arrival.SourceLastParameter=a<b?last:first;
        arrival.TargetEdgeId=target.Use.EdgeId;arrival.TargetFirstParameter=lo;arrival.TargetLastParameter=hi;
        arrival.TargetSpanKey=(*attr)->Key;arrival.TargetSkinRelation=(*attr)->SkinRelation;arrival.TargetConvexity=(*attr)->Convexity;
        arrival.bTargetSkinRelationValid=(*attr)->bSkinRelationValid;arrival.bTargetConvexityValid=(*attr)->bConvexityValid;
        arrival.Method=method;arrival.Reason="continuous-positive-weight-Bezier-control-net-normal-strip-certificate;native-boundary-event-component-first-hit";
        proven.push_back(std::move(arrival));
    }
    arrivals.insert(arrivals.end(),proven.begin(),proven.end());
    if(complete)reason="all-source-intervals-have-continuous-Bezier-normal-family-and-first-physical-boundary-proof;control-net-pieces="+std::to_string(certificate.Pieces);
    return complete;
}
