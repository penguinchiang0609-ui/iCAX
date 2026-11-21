// Included inside plate_exact. The Boost cpp_int header must be included at
// global scope by the owning translation unit. This is a point-equation
// fallback only; it never changes a surface, knot, trim or geometry tolerance.
namespace contour_rational {
namespace polynomial=contour_polynomial;
using Rational=boost::multiprecision::cpp_rational;
using Integer=boost::multiprecision::cpp_int;
using Coordinates=std::array<Rational,3>;

Rational BinaryDouble(double value){
    Require(std::numeric_limits<double>::radix==2&&std::numeric_limits<double>::digits==53
        &&std::numeric_limits<double>::is_iec559&&std::isfinite(value),"PLATE_CONTOUR_RATIONAL_INPUT");
    if(value==0)return Rational(0);
    int exponent=0;const double fraction=std::frexp(std::abs(value),&exponent);
    // frexp normalizes even a subnormal input. Scaling its binary fraction
    // by 2^53 is exact and gives an integer strictly below 2^53, safely inside
    // unsigned long long. No decimal conversion or approximation is involved.
    const double scaled=std::ldexp(fraction,53);
    const auto significand=static_cast<unsigned long long>(scaled);
    Require(static_cast<double>(significand)==scaled,"PLATE_CONTOUR_RATIONAL_INPUT");
    Integer numerator(significand);if(std::signbit(value))numerator=-numerator;
    Rational result(numerator);const int shift=exponent-53;
    if(shift){Integer power(1);power<<=shift>0?shift:-shift;const Rational factor(power);if(shift>0)result*=factor;else result/=factor;}
    return result;
}

polynomial::Interval Enclose(const Rational& value){
    const double candidate=value.convert_to<double>();if(!std::isfinite(candidate))return polynomial::Whole();
    const Rational exactCandidate=BinaryDouble(candidate);
    if(exactCandidate==value)return polynomial::Exact(candidate);
    // Do not assume a rounding direction or accuracy for Boost's conversion.
    // The returned endpoints are checked AGAIN as exact binary rationals.
    if(exactCandidate<value){
        const double upper=polynomial::Up(candidate);if(!std::isfinite(upper))return polynomial::Whole();
        const Rational exactUpper=BinaryDouble(upper);if(exactUpper>=value)return {candidate,upper};
    }else{
        const double lower=polynomial::Down(candidate);if(!std::isfinite(lower))return polynomial::Whole();
        const Rational exactLower=BinaryDouble(lower);if(exactLower<=value)return {lower,candidate};
    }
    return polynomial::Whole();
}

namespace detail {
class PointBasis {
    const std::vector<Rational>& Knots;const Rational& Parameter;int Span;const Check& Cancel;
    std::map<std::pair<int,int>,Rational> Cache;
public:
    PointBasis(const std::vector<Rational>& knots,const Rational& parameter,int span,const Check& check)
        :Knots(knots),Parameter(parameter),Span(span),Cancel(check){}
    const Rational& Get(int index,int degree){
        Cancel();const auto key=std::make_pair(index,degree);if(const auto found=Cache.find(key);found!=Cache.end())return found->second;
        Require(index>=0&&degree>=0&&index+degree+1<static_cast<int>(Knots.size()),"PLATE_CONTOUR_RATIONAL_BASIS");
        Rational result(0);
        if(!degree)result=index==Span?Rational(1):Rational(0);
        else{
            Rational denominator=Knots[static_cast<size_t>(index+degree)];denominator-=Knots[static_cast<size_t>(index)];
            Require(denominator>=0,"PLATE_CONTOUR_RATIONAL_KNOTS");
            if(denominator>0){
                Rational factor=Parameter;factor-=Knots[static_cast<size_t>(index)];factor/=denominator;
                Rational term=Get(index,degree-1);term*=factor;result+=term;
            }
            denominator=Knots[static_cast<size_t>(index+degree+1)];denominator-=Knots[static_cast<size_t>(index+1)];
            Require(denominator>=0,"PLATE_CONTOUR_RATIONAL_KNOTS");
            if(denominator>0){
                Rational factor=Knots[static_cast<size_t>(index+degree+1)];factor-=Parameter;factor/=denominator;
                Rational term=Get(index+1,degree-1);term*=factor;result+=term;
            }
        }
        return Cache.emplace(key,std::move(result)).first->second;
    }
};

std::vector<Rational> ExactKnots(const std::vector<double>& knots,const Check& check){
    std::vector<Rational> result;result.reserve(knots.size());
    for(size_t i=0;i<knots.size();++i){check();Require(std::isfinite(knots[i])&&(!i||knots[i]>=knots[i-1]),"PLATE_CONTOUR_RATIONAL_KNOTS");result.push_back(BinaryDouble(knots[i]));}
    return result;
}
class Poles {
    const std::function<gp_Pnt(int,int)>& Read;int UCount,VCount;const Check& Cancel;
    std::map<std::pair<int,int>,Coordinates> Cache;
public:
    Poles(const std::function<gp_Pnt(int,int)>& read,int uCount,int vCount,const Check& check):Read(read),UCount(uCount),VCount(vCount),Cancel(check){}
    const Coordinates& Get(int u,int v){
        Cancel();Require(u>=0&&u<UCount&&v>=0&&v<VCount,"PLATE_CONTOUR_RATIONAL_CONTROL");const auto key=std::make_pair(u,v);
        if(const auto found=Cache.find(key);found!=Cache.end())return found->second;
        const gp_Pnt point=Read(u,v);Coordinates result;
        for(int k=0;k<3;++k)result[static_cast<size_t>(k)]=BinaryDouble(point.Coord(k+1));
        return Cache.emplace(key,std::move(result)).first->second;
    }
};
}

polynomial::Interval Value(int p,int q,const std::vector<double>& UK,const std::vector<double>& VK,int su,int sv,
    const std::function<gp_Pnt(int,int)>& pole0Based,const gp_Trsf& transform,double u,double v,const Check& check){
    check();Require(p>=1&&q>=1&&UK.size()<=static_cast<size_t>(std::numeric_limits<int>::max())
        &&VK.size()<=static_cast<size_t>(std::numeric_limits<int>::max()),"PLATE_CONTOUR_RATIONAL_INPUT");
    const int uCount=static_cast<int>(UK.size())-p-1,vCount=static_cast<int>(VK.size())-q-1;
    Require(uCount>p&&vCount>q&&su>=p&&su<uCount&&sv>=q&&sv<vCount&&std::isfinite(u)&&std::isfinite(v),"PLATE_CONTOUR_RATIONAL_DOMAIN");
    Require(UK[static_cast<size_t>(su)]<UK[static_cast<size_t>(su+1)]&&VK[static_cast<size_t>(sv)]<VK[static_cast<size_t>(sv+1)]
        &&u>=UK[static_cast<size_t>(su)]&&u<=UK[static_cast<size_t>(su+1)]
        &&v>=VK[static_cast<size_t>(sv)]&&v<=VK[static_cast<size_t>(sv+1)],"PLATE_CONTOUR_RATIONAL_DOMAIN");
    const auto ku=detail::ExactKnots(UK,check),kv=detail::ExactKnots(VK,check);
    // Derivative B-splines delete exactly the first and last full knots.
    const std::vector<Rational> dku(ku.begin()+1,ku.end()-1),dkv(kv.begin()+1,kv.end()-1);
    const Rational ru=BinaryDouble(u),rv=BinaryDouble(v);
    detail::PointBasis bu(ku,ru,su,check),bv(kv,rv,sv,check),bdu(dku,ru,su-1,check),bdv(dkv,rv,sv-1,check);
    detail::Poles poles(pole0Based,uCount,vCount,check);Coordinates du{},dv{};
    for(int i=su-p;i<su;++i)for(int j=sv-q;j<=sv;++j){
        check();const auto& a=poles.Get(i,j);const auto& b=poles.Get(i+1,j);
        Rational denominator=ku[static_cast<size_t>(i+p+1)];denominator-=ku[static_cast<size_t>(i+1)];Require(denominator>0,"PLATE_CONTOUR_RATIONAL_KNOTS");
        Rational factor(p);factor/=denominator;factor*=bdu.Get(i,p-1);factor*=bv.Get(j,q);
        for(int k=0;k<3;++k){Rational term=b[static_cast<size_t>(k)];term-=a[static_cast<size_t>(k)];term*=factor;du[static_cast<size_t>(k)]+=term;}
    }
    for(int i=su-p;i<=su;++i)for(int j=sv-q;j<sv;++j){
        check();const auto& a=poles.Get(i,j);const auto& b=poles.Get(i,j+1);
        Rational denominator=kv[static_cast<size_t>(j+q+1)];denominator-=kv[static_cast<size_t>(j+1)];Require(denominator>0,"PLATE_CONTOUR_RATIONAL_KNOTS");
        Rational factor(q);factor/=denominator;factor*=bu.Get(i,p);factor*=bdv.Get(j,q-1);
        for(int k=0;k<3;++k){Rational term=b[static_cast<size_t>(k)];term-=a[static_cast<size_t>(k)];term*=factor;dv[static_cast<size_t>(k)]+=term;}
    }
    const Rational scale=BinaryDouble(transform.ScaleFactor());std::array<Rational,2> xu{},xv{};
    for(int row=0;row<2;++row)for(int column=0;column<3;++column){
        check();Rational coefficient=BinaryDouble(transform.HVectorialPart().Value(row+1,column+1));coefficient*=scale;
        Rational term=coefficient;term*=du[static_cast<size_t>(column)];xu[static_cast<size_t>(row)]+=term;
        term=coefficient;term*=dv[static_cast<size_t>(column)];xv[static_cast<size_t>(row)]+=term;
    }
    Rational first=xu[0];first*=xv[1];Rational second=xu[1];second*=xv[0];first-=second;
    check();return Enclose(first);
}
}
