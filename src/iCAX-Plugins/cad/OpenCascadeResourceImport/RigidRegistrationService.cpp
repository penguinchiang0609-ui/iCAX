#include "pch.h"
#include "RigidRegistrationService.h"
#include "Services/ServicesHelper.h"
#include <MathLin_SVD.hxx>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace
{
    using namespace iCAX::Geometry;
    using P = RegistrationPoint;
    using Request = SRigidRegistrationRequest;
    P Add(P a, P b) { for (int i=0;i<3;++i) a[i]+=b[i]; return a; }
    P Sub(P a, P b) { for (int i=0;i<3;++i) a[i]-=b[i]; return a; }
    P Scale(P a, double s) { for (auto& v:a) v*=s; return a; }
    double Dot(P a,P b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
    double Length(P a) { return std::sqrt(Dot(a,a)); }
    P Cross(P a,P b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
    void Check(P p) { for(double v:p) if(!std::isfinite(v)||std::abs(v)>1e9) throw std::invalid_argument("REGISTRATION_INVALID_COORDINATE"); }
    P Unit(P p) { auto n=Length(p); if(n<1e-12) throw std::invalid_argument("REGISTRATION_ZERO_DIRECTION"); return Scale(p,1/n); }
    double Det(const math_Matrix& r)
    { return r(1,1)*(r(2,2)*r(3,3)-r(2,3)*r(3,2))-r(1,2)*(r(2,1)*r(3,3)-r(2,3)*r(3,1))+r(1,3)*(r(2,1)*r(3,2)-r(2,2)*r(3,1)); }
    P Apply(const math_Matrix& r,P p) { P out{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)out[i]+=r(i+1,j+1)*p[j];return out; }
    void Matrix(SRigidRegistrationResult& out,const math_Matrix& r,P t)
    { for(int i=0;i<3;++i){for(int j=0;j<3;++j)out.Transform[4*i+j]=r(i+1,j+1);out.Transform[4*i+3]=t[i];}out.Transform[15]=1; }
    double Condition(const std::vector<P>& points,const std::vector<double>& weights,P center,const Request& request)
    {
        math_Matrix covariance(1,3,1,3,0.0);
        for(size_t k=0;k<points.size();++k)
        {
            const auto p=Sub(points[k],center);
            for(int i=0;i<3;++i)for(int j=0;j<3;++j)covariance(i+1,j+1)+=weights[k]*p[i]*p[j];
        }
        const auto svd=MathLin::SVD(covariance);
        if(!svd.IsDone())throw std::runtime_error("REGISTRATION_SVD_FAILED");
        std::array<double,3> spread{(*svd.SingularValues)(1),(*svd.SingularValues)(2),(*svd.SingularValues)(3)};
        std::sort(spread.begin(),spread.end(),std::greater<double>());
        const double second=std::sqrt(std::max(0.,spread[1]));
        const double ratio=spread[0]>0?std::sqrt(std::max(0.,spread[1]/spread[0])):0;
        if(second<request.MinimumSpreadMm||ratio<request.MinimumConditionRatio)
            throw std::invalid_argument("REGISTRATION_DEGENERATE_POINTS: insufficient two-dimensional point spread");
        return ratio;
    }
    class CRigidRegistrationService final : public IRigidRegistrationService
    {
        AUTO_REGIST_SERVICE(IRigidRegistrationService, CRigidRegistrationService)
    public:
        void OnLoad() override {}
        void OnUnload() override {}
        SRigidRegistrationResult Solve(const Request& q) const override
        {
            if(!std::isfinite(q.MinimumSpreadMm)||q.MinimumSpreadMm<=0||!std::isfinite(q.MinimumConditionRatio)
                ||q.MinimumConditionRatio<=0||q.MinimumConditionRatio>1||!std::isfinite(q.MaximumResidualMm)||q.MaximumResidualMm<=0)
                throw std::invalid_argument("REGISTRATION_INVALID_TOLERANCE");
            SRigidRegistrationResult out;
            math_Matrix rotation(1,3,1,3,0.0);
            if(q.Kind!=Request::Method::PairedPoints)
            {
                Check(q.Origin);Check(q.XReference);Check(q.PlaneReference);
                P x=q.XReference, plane=q.PlaneReference;
                if(q.Kind==Request::Method::Points)
                {
                    x=Sub(x,q.Origin);plane=Sub(plane,q.Origin);
                    if(Length(x)<q.MinimumSpreadMm||Length(plane)<q.MinimumSpreadMm)
                        throw std::invalid_argument("REGISTRATION_DEGENERATE_POINTS");
                }
                x=Unit(x);plane=Unit(plane);
                P z{};
                if(q.Kind==Request::Method::Plane)
                {
                    z=plane;
                    const auto projected=Sub(x,Scale(z,Dot(x,z)));
                    if(Length(projected)<q.MinimumConditionRatio)throw std::invalid_argument("REGISTRATION_PARALLEL_REFERENCES");
                    x=Unit(projected);
                }
                else
                {
                    const auto cross=Cross(x,plane);
                    if(Length(cross)<q.MinimumConditionRatio)throw std::invalid_argument("REGISTRATION_COLLINEAR_REFERENCES");
                    z=Unit(cross);
                }
                const auto y=Cross(z,x);
                // The selected coordinate frame lives in Workpiece; invert it.
                const std::array<P,3> rows{x,y,z};
                for(int i=0;i<3;++i)for(int j=0;j<3;++j)rotation(i+1,j+1)=rows[i][j];
                Matrix(out,rotation,Scale(Apply(rotation,q.Origin),-1));
                out.Qualified=true;
                return out;
            }
            const auto n=q.ModelPoints.size();
            if(n<3||n>10000||q.MeasuredPoints.size()!=n||(!q.Weights.empty()&&q.Weights.size()!=n))
                throw std::invalid_argument("REGISTRATION_POINT_COUNT");
            std::vector<double> weights=q.Weights.empty()?std::vector<double>(n,1.):q.Weights;
            double maxWeight=0;
            for(auto w:weights) { if(!std::isfinite(w)||w<=0)throw std::invalid_argument("REGISTRATION_INVALID_WEIGHT");maxWeight=std::max(maxWeight,w); }
            for(auto& w:weights)w/=maxWeight;
            const auto sum=std::accumulate(weights.begin(),weights.end(),0.);
            for(auto& w:weights)w/=sum;
            P modelCenter{}, measuredCenter{};
            for(size_t k=0;k<n;++k)
            {
                Check(q.ModelPoints[k]);Check(q.MeasuredPoints[k]);
                modelCenter=Add(modelCenter,Scale(q.ModelPoints[k],weights[k]));
                measuredCenter=Add(measuredCenter,Scale(q.MeasuredPoints[k],weights[k]));
            }
            out.ModelConditionRatio=Condition(q.ModelPoints,weights,modelCenter,q);
            out.MeasuredConditionRatio=Condition(q.MeasuredPoints,weights,measuredCenter,q);
            math_Matrix h(1,3,1,3,0.0);
            for(size_t k=0;k<n;++k)
            {
                const auto p=Sub(q.ModelPoints[k],modelCenter), m=Sub(q.MeasuredPoints[k],measuredCenter);
                for(int i=0;i<3;++i)for(int j=0;j<3;++j)h(i+1,j+1)+=weights[k]*p[i]*m[j];
            }
            auto svd=MathLin::SVD(h);
            if(!svd.IsDone())throw std::runtime_error("REGISTRATION_SVD_FAILED");
            auto& v=*svd.V;const auto& u=*svd.U;
            rotation=v*u.Transposed();
            if(Det(rotation)<0)
            {
                // The underlying SVD does not guarantee singular value order.
                int minimum=1;for(int i=2;i<=3;++i)if((*svd.SingularValues)(i)<(*svd.SingularValues)(minimum))minimum=i;
                for(int i=1;i<=3;++i)v(i,minimum)=-v(i,minimum);
                rotation=v*u.Transposed();out.ReflectionCorrected=true;
            }
            const auto translation=Sub(measuredCenter,Apply(rotation,modelCenter));
            Matrix(out,rotation,translation);
            double squared=0;
            for(size_t k=0;k<n;++k)
            {
                const auto residual=Sub(Add(Apply(rotation,q.ModelPoints[k]),translation),q.MeasuredPoints[k]);
                const auto error=Length(residual);out.ResidualVectors.push_back(residual);out.Residuals.push_back(error);
                squared+=weights[k]*error*error;out.MaximumResidualMm=std::max(out.MaximumResidualMm,error);
            }
            out.RmsMm=std::sqrt(squared);out.Qualified=out.MaximumResidualMm<=q.MaximumResidualMm;
            if(!out.Qualified)out.Diagnostic="REGISTRATION_RESIDUAL_EXCEEDED";
            return out;
        }
    };
}
