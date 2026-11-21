// Included inside plate_exact. Bernstein algebra for contour certificates.
// Each input double is an exact singleton. Every subsequent arithmetic result
// encloses its exact real value; a coefficient that merely contains zero never
// proves that the polynomial is zero, nor that it has no roots. Curve/Net
// parameters are normalized to [0,1]; derivative nets use those parameters.
namespace contour_polynomial {
struct Interval {double Lo=0,Hi=0;};
using Curve=std::vector<Interval>;
struct Net {
    int DegreeU=0,DegreeV=0;
    std::vector<Interval> Coefficients;
    Interval& At(int u,int v){return Coefficients.at(static_cast<size_t>(u)*(DegreeV+1)+v);}
    const Interval& At(int u,int v)const{return Coefficients.at(static_cast<size_t>(u)*(DegreeV+1)+v);}
};
Interval Whole(){return {-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity()};}
bool Finite(const Interval& a){return std::isfinite(a.Lo)&&std::isfinite(a.Hi)&&a.Lo<=a.Hi;}
bool Zero(const Interval& a){return a.Lo==0&&a.Hi==0;}
Interval Exact(double a){return std::isfinite(a)?Interval{a,a}:Whole();}
double Down(double a){return std::nextafter(a,-std::numeric_limits<double>::infinity());}
double Up(double a){return std::nextafter(a,std::numeric_limits<double>::infinity());}
bool SterbenzDifference(double a,double b){
    if(a==b||b==0)return true;
    return (a>0&&b>0&&a/2<=b&&b/2<=a)||(a<0&&b<0&&(-a)/2<=-b&&(-b)/2<=-a);
}
bool ExactSum(double a,double b,double sum){
    // In each equality below the subtraction is first proven exact. Merely
    // recomputing (sum-a)==b without that proof could hide another rounding.
    // Unlike an error-free TwoSum transform, this needs no rounding-mode
    // assumption beyond IEEE arithmetic with gradual underflow.
    return SterbenzDifference(a,-b)||(SterbenzDifference(sum,a)&&sum-a==b)||(SterbenzDifference(sum,b)&&sum-b==a);
}
double ResidualSafeMagnitude(){
    // A binary64 product has at most 2*digits significant bits. Above this
    // conservative floor every nonzero exact product residual is at least a
    // subnormal ulp, so an FMA residual cannot underflow silently to zero.
    return std::ldexp(std::numeric_limits<double>::min(),2*std::numeric_limits<double>::digits);
}
Interval Add(const Interval& a,const Interval& b){
    if(!Finite(a)||!Finite(b))return Whole();if(Zero(a))return b;if(Zero(b))return a;
    if(a.Lo==a.Hi&&b.Lo==b.Hi&&a.Lo==-b.Lo)return Exact(0);
    const double lo=a.Lo+b.Lo,hi=a.Hi+b.Hi;if(!std::isfinite(lo)||!std::isfinite(hi))return Whole();
    if(a.Lo==a.Hi&&b.Lo==b.Hi&&ExactSum(a.Lo,b.Lo,lo))return Exact(lo);
    return {Down(lo),Up(hi)};
}
Interval Negate(const Interval& a){return Finite(a)?Interval{-a.Hi,-a.Lo}:Whole();}
Interval Sub(const Interval& a,const Interval& b){return Add(a,Negate(b));}
Interval Multiply(const Interval& a,const Interval& b){
    if(!Finite(a)||!Finite(b))return Whole();if(Zero(a)||Zero(b))return Exact(0);
    if(a.Lo==1&&a.Hi==1)return b;if(b.Lo==1&&b.Hi==1)return a;
    const double products[]{a.Lo*b.Lo,a.Lo*b.Hi,a.Hi*b.Lo,a.Hi*b.Hi};
    for(double p:products)if(!std::isfinite(p))return Whole();
    if(a.Lo==a.Hi&&b.Lo==b.Hi&&std::abs(products[0])>=ResidualSafeMagnitude()
        &&std::fma(a.Lo,b.Lo,-products[0])==0)return Exact(products[0]);
    const auto range=std::minmax_element(products,products+4);return {Down(*range.first),Up(*range.second)};
}
Interval ScalePositive(const Interval& a,double factor){
    if(!std::isfinite(factor)||factor<0)return Whole();return Multiply(a,Exact(factor));
}
Interval DivideNonZero(const Interval& a,const Interval& b){
    if(!Finite(a)||!Finite(b)||!(b.Lo>0||b.Hi<0))return Whole();if(Zero(a))return Exact(0);
    if(b.Lo==1&&b.Hi==1)return a;
    if(a.Lo==a.Hi&&b.Lo==b.Hi&&std::abs(a.Lo)>=ResidualSafeMagnitude()){
        const double quotient=a.Lo/b.Lo;
        if(std::isfinite(quotient)&&std::fma(quotient,b.Lo,-a.Lo)==0)return Exact(quotient);
    }
    const double lo=1/b.Hi,hi=1/b.Lo;if(!std::isfinite(lo)||!std::isfinite(hi))return Whole();
    return Multiply(a,{Down(lo),Up(hi)});
}
Interval DividePositive(const Interval& a,double divisor){return divisor>0?DivideNonZero(a,Exact(divisor)):Whole();}
Interval DividePositive(const Interval& a,const Interval& divisor){return divisor.Lo>0?DivideNonZero(a,divisor):Whole();}
int Sign(const Interval& a){return Finite(a)?(a.Lo>0?1:a.Hi<0?-1:0):0;}
Interval Hull(const Interval& a,const Interval& b){return Finite(a)&&Finite(b)?Interval{std::min(a.Lo,b.Lo),std::max(a.Hi,b.Hi)}:Whole();}
Interval Bounds(const Curve& c){
    if(c.empty())return Whole();auto result=c.front();for(const auto& a:c)result=Hull(result,a);return result;
}
Interval Bounds(const Net& n){return Bounds(n.Coefficients);}
int Sign(const Curve& c){return Sign(Bounds(c));}
int Sign(const Net& n){return Sign(Bounds(n));}
bool IdenticallyZero(const Curve& c){return !c.empty()&&std::all_of(c.begin(),c.end(),[](const auto& a){return Zero(a);});}
bool Valid(const Net& n){return n.DegreeU>=0&&n.DegreeV>=0&&n.Coefficients.size()==static_cast<size_t>(n.DegreeU+1)*(n.DegreeV+1);}
Net MakeNet(int u,int v){Require(u>=0&&v>=0,"PLATE_CONTOUR_POLYNOMIAL_INVALID");return {u,v,std::vector<Interval>(static_cast<size_t>(u+1)*(v+1))};}
Interval Lerp(const Interval& a,const Interval& b,double t){
    if(!(t>=0&&t<=1))return Whole();if(t==0)return a;if(t==1)return b;
    // Equal interval hulls remain equal under a convex combination even when
    // their two enclosed coefficients have independent arithmetic errors.
    if(a.Lo==b.Lo&&a.Hi==b.Hi)return a;
    const auto s=t==.5?Exact(.5):Sub(Exact(1),Exact(t));return Add(Multiply(a,s),ScalePositive(b,t));
}
std::pair<Curve,Curve> Split(const Curve& c,double t){
    Require(!c.empty()&&t>=0&&t<=1,"PLATE_CONTOUR_POLYNOMIAL_INVALID");
    Curve work=c,left(c.size()),right(c.size());left.front()=work.front();right.back()=work.back();
    for(size_t level=1;level<c.size();++level){for(size_t i=0;i<c.size()-level;++i)work[i]=Lerp(work[i],work[i+1],t);left[level]=work.front();right[c.size()-1-level]=work[c.size()-1-level];}
    return {std::move(left),std::move(right)};
}
std::pair<Curve,Curve> SplitAtHalf(const Curve& c){return Split(c,.5);}
Curve Reverse(Curve c){std::reverse(c.begin(),c.end());return c;}
Interval Evaluate(const Curve& c,double t){
    if(c.empty()||!(t>=0&&t<=1))return Whole();if(t==0)return c.front();if(t==1)return c.back();
    auto work=c;for(size_t count=c.size();count>1;--count)for(size_t i=0;i<count-1;++i)work[i]=Lerp(work[i],work[i+1],t);return work.front();
}
Curve Derivative(const Curve& c){
    Require(!c.empty(),"PLATE_CONTOUR_POLYNOMIAL_INVALID");if(c.size()==1)return {Exact(0)};
    Curve result(c.size()-1);for(size_t i=0;i<result.size();++i)result[i]=ScalePositive(Sub(c[i+1],c[i]),static_cast<double>(c.size()-1));return result;
}
Curve Elevate(Curve c,int degree){
    Require(!c.empty()&&degree>=static_cast<int>(c.size())-1,"PLATE_CONTOUR_POLYNOMIAL_INVALID");
    while(static_cast<int>(c.size())-1<degree){const int next=static_cast<int>(c.size());Curve raised(c.size()+1);raised.front()=c.front();raised.back()=c.back();
        for(int i=1;i<next;++i)raised[i]=DividePositive(Add(ScalePositive(c[i-1],i),ScalePositive(c[i],next-i)),next);c=std::move(raised);}
    return c;
}
Curve Add(const Curve& a,const Curve& b){
    Require(!a.empty()&&!b.empty(),"PLATE_CONTOUR_POLYNOMIAL_INVALID");const int degree=static_cast<int>(std::max(a.size(),b.size()))-1;
    const auto aa=Elevate(a,degree),bb=Elevate(b,degree);Curve result(aa.size());for(size_t i=0;i<result.size();++i)result[i]=Add(aa[i],bb[i]);return result;
}
Curve Sub(const Curve& a,const Curve& b){auto negative=b;for(auto& p:negative)p=Negate(p);return Add(a,negative);}
std::vector<Interval> BinomialRow(int degree){
    Require(degree>=0,"PLATE_CONTOUR_POLYNOMIAL_INVALID");std::vector<Interval> row(degree+1,Exact(1));
    // Integer recurrence is interval arithmetic too: intermediate integer
    // products need not fit exactly in a binary64 significand.
    for(int i=1;i<degree;++i)row[i]=DividePositive(ScalePositive(row[i-1],degree-i+1),i);return row;
}
std::vector<Interval> ProductWeights(int a,int b){
    const auto aa=BinomialRow(a),bb=BinomialRow(b),sum=BinomialRow(a+b);std::vector<Interval> result(static_cast<size_t>(a+1)*(b+1));
    for(int i=0;i<=a;++i)for(int j=0;j<=b;++j)result[static_cast<size_t>(i)*(b+1)+j]=DividePositive(Multiply(aa[i],bb[j]),sum[i+j]);return result;
}
Curve Multiply(const Curve& a,const Curve& b){
    Require(!a.empty()&&!b.empty(),"PLATE_CONTOUR_POLYNOMIAL_INVALID");const int m=static_cast<int>(a.size())-1,n=static_cast<int>(b.size())-1;const auto weights=ProductWeights(m,n);Curve result(m+n+1);
    for(int i=0;i<=m;++i)for(int j=0;j<=n;++j)result[i+j]=Add(result[i+j],Multiply(Multiply(a[i],b[j]),weights[static_cast<size_t>(i)*(n+1)+j]));return result;
}
Net DerivativeU(const Net& n){
    Require(Valid(n),"PLATE_CONTOUR_POLYNOMIAL_INVALID");auto result=MakeNet(std::max(0,n.DegreeU-1),n.DegreeV);if(!n.DegreeU)return result;
    for(int u=0;u<=result.DegreeU;++u)for(int v=0;v<=result.DegreeV;++v)result.At(u,v)=ScalePositive(Sub(n.At(u+1,v),n.At(u,v)),n.DegreeU);return result;
}
Net DerivativeV(const Net& n){
    Require(Valid(n),"PLATE_CONTOUR_POLYNOMIAL_INVALID");auto result=MakeNet(n.DegreeU,std::max(0,n.DegreeV-1));if(!n.DegreeV)return result;
    for(int u=0;u<=result.DegreeU;++u)for(int v=0;v<=result.DegreeV;++v)result.At(u,v)=ScalePositive(Sub(n.At(u,v+1),n.At(u,v)),n.DegreeV);return result;
}
Net Transpose(const Net& n){
    Require(Valid(n),"PLATE_CONTOUR_POLYNOMIAL_INVALID");auto result=MakeNet(n.DegreeV,n.DegreeU);for(int u=0;u<=n.DegreeU;++u)for(int v=0;v<=n.DegreeV;++v)result.At(v,u)=n.At(u,v);return result;
}
Net ReverseU(const Net& n){
    Require(Valid(n),"PLATE_CONTOUR_POLYNOMIAL_INVALID");auto result=n;for(int u=0;u<=n.DegreeU;++u)for(int v=0;v<=n.DegreeV;++v)result.At(u,v)=n.At(n.DegreeU-u,v);return result;
}
Net ReverseV(const Net& n){return Transpose(ReverseU(Transpose(n)));}
std::pair<Net,Net> SplitU(const Net& n){
    Require(Valid(n),"PLATE_CONTOUR_POLYNOMIAL_INVALID");auto left=n,right=n;
    for(int v=0;v<=n.DegreeV;++v){Curve row(n.DegreeU+1);for(int u=0;u<=n.DegreeU;++u)row[u]=n.At(u,v);const auto halves=SplitAtHalf(row);for(int u=0;u<=n.DegreeU;++u){left.At(u,v)=halves.first[u];right.At(u,v)=halves.second[u];}}
    return {std::move(left),std::move(right)};
}
std::pair<Net,Net> SplitV(const Net& n){const auto halves=SplitU(Transpose(n));return {Transpose(halves.first),Transpose(halves.second)};}
Net Elevate(Net n,int degreeU,int degreeV){
    Require(Valid(n)&&degreeU>=n.DegreeU&&degreeV>=n.DegreeV,"PLATE_CONTOUR_POLYNOMIAL_INVALID");
    auto raisedU=MakeNet(degreeU,n.DegreeV);for(int v=0;v<=n.DegreeV;++v){Curve row(n.DegreeU+1);for(int u=0;u<=n.DegreeU;++u)row[u]=n.At(u,v);row=Elevate(std::move(row),degreeU);for(int u=0;u<=degreeU;++u)raisedU.At(u,v)=row[u];}
    auto result=MakeNet(degreeU,degreeV);for(int u=0;u<=degreeU;++u){Curve row(n.DegreeV+1);for(int v=0;v<=n.DegreeV;++v)row[v]=raisedU.At(u,v);row=Elevate(std::move(row),degreeV);for(int v=0;v<=degreeV;++v)result.At(u,v)=row[v];}return result;
}
Net Add(const Net& a,const Net& b){
    Require(Valid(a)&&Valid(b),"PLATE_CONTOUR_POLYNOMIAL_INVALID");const int u=std::max(a.DegreeU,b.DegreeU),v=std::max(a.DegreeV,b.DegreeV);auto aa=Elevate(a,u,v);const auto bb=Elevate(b,u,v);
    for(size_t i=0;i<aa.Coefficients.size();++i)aa.Coefficients[i]=Add(aa.Coefficients[i],bb.Coefficients[i]);return aa;
}
Net Sub(const Net& a,const Net& b){auto negative=b;for(auto& p:negative.Coefficients)p=Negate(p);return Add(a,negative);}
Net Multiply(const Net& a,const Net& b){
    Require(Valid(a)&&Valid(b),"PLATE_CONTOUR_POLYNOMIAL_INVALID");auto result=MakeNet(a.DegreeU+b.DegreeU,a.DegreeV+b.DegreeV);
    const auto wu=ProductWeights(a.DegreeU,b.DegreeU),wv=ProductWeights(a.DegreeV,b.DegreeV);
    for(int u=0;u<=a.DegreeU;++u)for(int v=0;v<=a.DegreeV;++v)for(int r=0;r<=b.DegreeU;++r)for(int s=0;s<=b.DegreeV;++s){
        const auto weight=Multiply(wu[static_cast<size_t>(u)*(b.DegreeU+1)+r],wv[static_cast<size_t>(v)*(b.DegreeV+1)+s]);
        result.At(u+r,v+s)=Add(result.At(u+r,v+s),Multiply(Multiply(a.At(u,v),b.At(r,s)),weight));}
    return result;
}

struct Root {double First=0,Last=0;};
struct RootIsolation {bool Certified=false,IdenticallyZero=false;std::vector<Root> Roots;};
// CoarseUnique certifies existence, uniqueness and disjointness, not the
// requested parameter width. Its caller must subsequently refine every root
// against its original equation and final spatial error budget.
enum class RootIsolationMode {Refined,CoarseUnique};
namespace root_detail {
bool MonotoneRoot(const Curve& c,double tolerance,Root& root,const Check& check,RootIsolationMode mode){
    const auto derivative=Bounds(Derivative(c));const int direction=Sign(derivative);
    if(!direction||Sign(c.front())!=-direction||Sign(c.back())!=direction)return false;
    // Strict endpoint signs and a strict derivative sign establish existence
    // and uniqueness BEFORE numerical contraction. An uncertain evaluation
    // contracts with the mean-value theorem; it is never called an exact root.
    double lo=0,hi=1;
    const auto stalled=[&]{if(mode!=RootIsolationMode::CoarseUnique)return false;root={lo,hi};return true;};
    for(int iteration=0;iteration<128;++iteration){check();if(hi-lo<=tolerance){root={lo,hi};return true;}const double middle=lo+(hi-lo)/2;if(!(middle>lo&&middle<hi))return stalled();
        const auto value=Evaluate(c,middle);const int sign=Sign(value);
        if(sign==direction)hi=middle;else if(sign==-direction)lo=middle;
        else{
            const auto enclosure=Sub(Exact(middle),DivideNonZero(value,derivative));if(!Finite(enclosure))return stalled();
            const double nextLo=std::max(lo,enclosure.Lo),nextHi=std::min(hi,enclosure.Hi);
            if(!(nextLo<=nextHi))return false;if(nextLo==lo&&nextHi==hi)return stalled();lo=nextLo;hi=nextHi;
        }
    }return stalled();
}
Curve DeflateLeft(const Curve& c){
    const int degree=static_cast<int>(c.size())-1;Curve result(degree);for(int i=0;i<degree;++i)result[i]=DividePositive(ScalePositive(c[i+1],degree),i+1);return result;
}
Curve DeflateRight(const Curve& c){
    const int degree=static_cast<int>(c.size())-1;Curve result(degree);for(int i=0;i<degree;++i)result[i]=DividePositive(ScalePositive(c[i],degree),degree-i);return result;
}
struct Search {
    const Check& Cancel;double Tolerance;size_t MaximumNodes,Nodes=0;int MaximumDepth;RootIsolationMode Mode;std::vector<Root> Roots;
    bool Visit(Curve c,double first,double last,int depth){
        Cancel();if(++Nodes>MaximumNodes||depth>MaximumDepth||!Finite(Bounds(c)))return false;
        if(Sign(c))return true;
        // Exact endpoint factors are removed algebraically. This includes
        // repeated endpoint roots without treating an interval around zero as
        // an exact factor. A constant zero polynomial remains uncertified.
        bool left=false,right=false;
        while(c.size()>1&&Zero(c.front())){left=true;c=DeflateLeft(c);}
        while(c.size()>1&&Zero(c.back())){right=true;c=DeflateRight(c);}
        if(left)Roots.push_back({first,first});if(right)Roots.push_back({last,last});
        if(Sign(c))return true;if(c.size()==1||IdenticallyZero(c))return false;
        const int derivative=Sign(Derivative(c));
        if(derivative){
            // Monotonicity can exclude a root even if the convex hull alone
            // straddles zero. Endpoint uncertainty is not exclusion evidence.
            if((derivative>0&&(c.front().Lo>0||c.back().Hi<0))||(derivative<0&&(c.front().Hi<0||c.back().Lo>0)))return true;
            Root local;const double width=last-first;
            // Leave room for the outward-rounded affine map back to the
            // enclosing subinterval. Its final width is checked separately.
            if(!MonotoneRoot(c,std::min(1.,Tolerance/(4*width)),local,Cancel,Mode))return false;
            const auto mapped=Add(Exact(first),Multiply(Sub(Exact(last),Exact(first)),Interval{local.First,local.Last}));
            if(!Finite(mapped))return false;const Root root{std::max(first,mapped.Lo),std::min(last,mapped.Hi)};
            if(Mode==RootIsolationMode::Refined&&root.Last-root.First>Tolerance)return false;Roots.push_back(root);return true;
        }
        const double middle=first+(last-first)/2;if(!(middle>first&&middle<last))return false;
        const auto halves=SplitAtHalf(c);return Visit(halves.first,first,middle,depth+1)&&Visit(halves.second,middle,last,depth+1);
    }
};
}
RootIsolation IsolateRoots(const Curve& c,const Check& check,double parameterTolerance=1e-12,size_t maximumNodes=16384,int maximumDepth=64,RootIsolationMode mode=RootIsolationMode::Refined){
    RootIsolation result;if(c.empty()||!std::isfinite(parameterTolerance)||!(parameterTolerance>0)||!maximumNodes||maximumDepth<0)return result;
    result.IdenticallyZero=IdenticallyZero(c);if(result.IdenticallyZero)return result;
    root_detail::Search search{check,parameterTolerance,maximumNodes,0,maximumDepth,mode,{}};
    if(!search.Visit(c,0,1,0))return result;
    std::sort(search.Roots.begin(),search.Roots.end(),[](const auto& a,const auto& b){return a.First<b.First||(a.First==b.First&&a.Last<b.Last);});
    for(const auto& root:search.Roots){
        if(!result.Roots.empty()&&root.First<=result.Roots.back().Last){
            const auto& previous=result.Roots.back();if(root.First==root.Last&&previous.First==previous.Last&&root.First==previous.First)continue;
            // Two enclosures that overlap are not proof of one common root.
            // In particular, never merge two unresolved nearby simple roots.
            result.Roots.clear();return result;
        }result.Roots.push_back(root);
    }
    result.Certified=true;return result;
}
}
