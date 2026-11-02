#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace iCAX::GeometryServices::MachineMotion {
using Quintic=std::array<double,6>;
struct AxisSegment {
    // q(u), u=(t-TimeSeconds)/DurationSeconds. Derivatives carry physical seconds.
    double TimeSeconds=0,DurationSeconds=1;
    std::size_t SourceStation=std::numeric_limits<std::size_t>::max();
    double SourceFirst=0,SourceLast=1;
    std::vector<Quintic> Coefficients;
};
inline double Binomial(unsigned n,unsigned k){double v=1;for(unsigned j=1;j<=k;++j)v=v*(n+1-j)/j;return v;}
inline double Polynomial(const Quintic& c,double u,unsigned derivative=0){
    if(derivative>5)return 0;double v=0;for(int k=5;k>=int(derivative);--k){double f=1;for(unsigned j=0;j<derivative;++j)f*=k-j;v=v*u+c[k]*f;}return v;
}
inline std::array<double,2> PolynomialBounds(const Quintic& c,double first=0,double last=1,unsigned derivative=0){
    if(derivative>5)return {0,0};
    const unsigned degree=5-derivative;std::array<double,6> power{},bernstein{};
    for(unsigned k=derivative;k<=5;++k){double d=c[k];for(unsigned r=0;r<derivative;++r)d*=k-r;const unsigned n=k-derivative;for(unsigned j=0;j<=n;++j)power[j]+=d*Binomial(n,j)*std::pow(first,int(n-j))*std::pow(last-first,int(j));}
    double low=std::numeric_limits<double>::infinity(),high=-low,norm=0;
    for(unsigned i=0;i<=degree;++i){for(unsigned j=0;j<=i;++j)bernstein[i]+=power[j]*Binomial(i,j)/Binomial(degree,j);low=std::min(low,bernstein[i]);high=std::max(high,bernstein[i]);norm+=std::abs(bernstein[i]);}
    const double rounding=64*std::numeric_limits<double>::epsilon()*std::max(norm,1e-300);return {low-rounding,high+rounding};
}
inline std::vector<double> Evaluate(const AxisSegment& s,double u,unsigned derivative=0){
    if(!std::isfinite(s.DurationSeconds)||s.DurationSeconds<=0)throw std::invalid_argument("MOTION_SEGMENT_DURATION_INVALID");
    std::vector<double> v;v.reserve(s.Coefficients.size());const double scale=std::pow(s.DurationSeconds,int(derivative));for(const auto& c:s.Coefficients)v.push_back(Polynomial(c,std::clamp(u,0.,1.),derivative)/scale);return v;
}
inline Quintic Hermite(double p0,double v0,double a0,double p1,double v1,double a1,double duration){
    const double d=p1-p0,v=v0*duration,w=v1*duration,a=a0*duration*duration,b=a1*duration*duration;
    return {p0,v,a/2,10*d-6*v-4*w-1.5*a+.5*b,-15*d+8*v+7*w+1.5*a-b,6*d-3*v-3*w-.5*a+.5*b};
}
inline AxisSegment Restrict(const AxisSegment& s,double first,double last){
    AxisSegment out=s;out.TimeSeconds+=s.DurationSeconds*first;out.DurationSeconds*=last-first;
    for(std::size_t i=0;i<s.Coefficients.size();++i){out.Coefficients[i]={};for(unsigned k=0;k<=5;++k)for(unsigned j=0;j<=k;++j)out.Coefficients[i][j]+=s.Coefficients[i][k]*Binomial(k,j)*std::pow(first,int(k-j))*std::pow(last-first,int(j));}return out;
}
}
