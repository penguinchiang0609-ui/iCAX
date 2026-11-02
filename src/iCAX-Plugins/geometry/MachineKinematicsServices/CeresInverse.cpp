#include <ceres/ceres.h>
#include "KinematicsInternal.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <stdexcept>
#include <Eigen/Geometry>
#include <Eigen/LU>

namespace iCAX::GeometryServices::MachineKinematics::Detail {
namespace {
using Clock=std::chrono::steady_clock;
double Norm(const Vector& a){return std::hypot(a[0],a[1],a[2]);}
Vector Unit(Vector a){const auto n=Norm(a);if(!std::isfinite(n)||n<1e-12)throw std::invalid_argument("KINEMATICS_TARGET_DIRECTION_INVALID");for(auto& v:a)v/=n;return a;}
double Angle(const Vector& a,const Vector& b){double dot=0;for(int i=0;i<3;++i)dot+=a[i]*b[i];return std::acos(std::clamp(dot,-1.,1.));}
struct Search {
    const Prepared& M;const Request& R;const std::function<bool()>& Cancelled;
    Clock::time_point Started=Clock::now();
    std::vector<std::size_t> Axes;std::vector<double> Lower,Upper,Base,Scale;
    Vector Direction;Result Output;
    bool WasCancelled=false,TimedOut=false;
    bool Stop(){if(Cancelled&&Cancelled())WasCancelled=true;if(std::chrono::duration<double,std::milli>(Clock::now()-Started).count()>=R.MaxTimeMs)TimedOut=true;return WasCancelled||TimedOut;}
    std::vector<double> Physical(const double* y)const{auto q=Base;for(std::size_t k=0;k<Axes.size();++k)q[Axes[k]]+=y[k]*Scale[k];return q;}
};
class Residual final:public ceres::CostFunction {
    Search& S;
public:
    explicit Residual(Search& s):S(s){set_num_residuals(6);mutable_parameter_block_sizes()->push_back(int(s.Axes.size()));}
    bool Evaluate(double const* const* parameters,double* residuals,double** jacobians)const override {
        if(S.Stop())return false;++S.Output.Evaluations;
        const auto e=Detail::Evaluate(S.M,S.Physical(parameters[0]),S.R.TaskDefinition,jacobians&&jacobians[0]);
        for(int k=0;k<3;++k){residuals[k]=(e.Pose.PositionMm[k]-S.R.Goal.PositionMm[k])/S.R.PositionScaleMm;residuals[k+3]=(e.Pose.Direction[k]-S.Direction[k])/S.R.DirectionScale;}
        if(jacobians&&jacobians[0])for(int row=0;row<6;++row)for(std::size_t col=0;col<S.Axes.size();++col)
            jacobians[0][row*S.Axes.size()+col]=e.Jacobian(row,S.Axes[col])*S.Scale[col]/(row<3?S.R.PositionScaleMm:S.R.DirectionScale);
        return true;
    }
};
class Monitor final:public ceres::IterationCallback {
    Search& S;
public:explicit Monitor(Search& s):S(s){}
    ceres::CallbackReturnType operator()(const ceres::IterationSummary&) override{++S.Output.Iterations;return S.Stop()?ceres::SOLVER_ABORT:ceres::SOLVER_CONTINUE;}
};
double Halton(std::size_t index,unsigned base){double f=1,result=0;while(index){f/=base;result+=f*(index%base);index/=base;}return result;}
bool Accept(Search& s,const std::vector<double>& q,std::size_t seedIndex,const std::string& source,const std::vector<double>& seed){
    const auto e=Evaluate(s.M,q,s.R.TaskDefinition,true);double err2=0;
    for(int k=0;k<3;++k){const auto d=e.Pose.PositionMm[k]-s.R.Goal.PositionMm[k];err2+=d*d;}
    const auto pe=std::sqrt(err2),de=Angle(e.Pose.Direction,s.Direction);
    if(pe>s.R.PositionToleranceMm||de>s.R.DirectionToleranceRad)return false;
    Candidate c;c.Positions=q;c.PositionErrorMm=pe;c.DirectionErrorRad=de;c.SeedIndex=seedIndex;c.SeedSource=source;c.BranchKey="numerical-seed:"+std::to_string(seedIndex);c.MinNormalizedLimitMargin=1;
    for(std::size_t i=0;i<q.size();++i){
        if(q[i]<s.Lower[i]-1e-10||q[i]>s.Upper[i]+1e-10)return false;
        const auto range=s.Upper[i]-s.Lower[i],margin=std::max(0.,std::min(q[i]-s.Lower[i],s.Upper[i]-q[i]));c.LimitMargins.push_back(margin);
        if(range>0){c.MinNormalizedLimitMargin=std::min(c.MinNormalizedLimitMargin,margin/range);const auto distance=(q[i]-seed[i])/range;c.SeedDistance+=distance*distance;}
    }
    c.SeedDistance=std::sqrt(c.SeedDistance);
    for(const auto& prior:s.Output.Candidates){double distance=0;for(auto i:s.Axes)distance=std::max(distance,std::abs(q[i]-prior.Positions[i])/std::max(1e-12,s.Upper[i]-s.Lower[i]));if(distance<=s.R.DeduplicationTolerance)return false;}
    c.Conditioning=Condition(s.M,e.Jacobian,s.Axes,s.Scale,s.R.PositionScaleMm,s.R.DirectionScale,s.R.RelativeRankTolerance);s.Output.Candidates.push_back(std::move(c));return true;
}
#include "SerialThreePTwoR.inc"
}
Result Solve(const ModelRef& model,const Request& r,const std::function<bool()>& cancelled){
    const auto& m=Require(model);Search s{m,r,cancelled};s.Output.Provider="Pinocchio+Ceres";s.Output.ProviderVersion="Pinocchio 2.7.0; Ceres 2.1.0";s.Output.ModelDigest=m.Digest;
    if(r.TaskDefinition.Kind!=TaskKind::PositionAndDirection){s.Output.State=Status::Unsupported;s.Output.Diagnostic="FullPose is a separate task and is not enabled by this PositionAndDirection provider.";return s.Output;}
    if(!r.MaxSeeds||!r.MaxIterations||!r.MaxCandidates||!std::isfinite(r.MaxTimeMs)||r.MaxTimeMs<=0||
       !std::isfinite(r.PositionToleranceMm)||r.PositionToleranceMm<=0||!std::isfinite(r.DirectionToleranceRad)||r.DirectionToleranceRad<=0||
       !std::isfinite(r.PositionScaleMm)||r.PositionScaleMm<=0||!std::isfinite(r.DirectionScale)||r.DirectionScale<=0||
       !std::isfinite(r.RelativeRankTolerance)||r.RelativeRankTolerance<=0||!std::isfinite(r.DeduplicationTolerance)||r.DeduplicationTolerance<0)throw std::invalid_argument("KINEMATICS_SEARCH_BUDGET_INVALID");
    for(auto p:r.Goal.PositionMm)if(!std::isfinite(p))throw std::invalid_argument("KINEMATICS_TARGET_NONFINITE");
    s.Direction=Unit(r.Goal.Direction);
    for(const auto& n:m.Source.Nodes){const auto fixed=n.Type==JointType::Fixed;s.Lower.push_back(fixed?0:n.Lower);s.Upper.push_back(fixed?0:n.Upper);s.Base.push_back(fixed?0:n.ReferenceValue);}
    std::set<std::size_t> constrained;
    for(const auto& c:r.Constraints){auto it=m.IDs.find(c.AxisID);if(it==m.IDs.end()||!constrained.insert(it->second).second)throw std::invalid_argument("KINEMATICS_CONSTRAINT_AXIS_INVALID");const auto i=it->second;
        if(std::isnan(c.Lower)||std::isnan(c.Upper)||c.Lower>c.Upper||c.Lower>s.Upper[i]||c.Upper<s.Lower[i])throw std::invalid_argument("KINEMATICS_CONSTRAINT_LIMIT_INVALID");
        s.Lower[i]=std::max(s.Lower[i],c.Lower);s.Upper[i]=std::min(s.Upper[i],c.Upper);
        if(c.Fixed){if(!std::isfinite(c.FixedValue)||c.FixedValue<s.Lower[i]||c.FixedValue>s.Upper[i])throw std::invalid_argument("KINEMATICS_FIXED_VALUE_INVALID");s.Lower[i]=s.Upper[i]=c.FixedValue;}
    }
    for(std::size_t i=0;i<s.Base.size();++i){s.Base[i]=std::clamp(s.Base[i],s.Lower[i],s.Upper[i]);if(s.Lower[i]<s.Upper[i]){s.Axes.push_back(i);s.Scale.push_back(s.Upper[i]-s.Lower[i]);}}
    // Validates frame IDs/TCP/beam even when cancelled before the first solve.
    (void)Evaluate(m,s.Base,r.TaskDefinition,false);
    for(const auto& seed:r.Seeds){if(seed.size()!=s.Base.size())throw std::invalid_argument("KINEMATICS_SEED_SIZE");for(std::size_t i=0;i<seed.size();++i)if(!std::isfinite(seed[i])||(m.QIndices[i]<0&&seed[i]!=0))throw std::invalid_argument("KINEMATICS_SEED_INVALID");}
    static constexpr unsigned primes[]={2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97,101,103,107,109,113,127,131};
    const bool analytic=!s.Stop()&&SerialThreePTwoR(s);
    for(std::size_t attempt=0;!analytic&&attempt<r.MaxSeeds&&!s.Stop();++attempt){
        auto seed=s.Base;std::string source;
        if(attempt<r.Seeds.size()){seed=r.Seeds[attempt];source="Caller";}
        else if(attempt==r.Seeds.size()){source="Reference";}
        else {source="DeterministicHalton";for(std::size_t k=0;k<s.Axes.size();++k){const auto i=s.Axes[k];seed[i]=s.Lower[i]+Halton(attempt-r.Seeds.size(),primes[k%std::size(primes)])*(s.Upper[i]-s.Lower[i]);}}
        for(std::size_t i=0;i<seed.size();++i)seed[i]=std::clamp(seed[i],s.Lower[i],s.Upper[i]);
        ++s.Output.SeedsAttempted;
        if(Accept(s,seed,attempt,source,seed)){
            if(s.Output.Candidates.size()>=r.MaxCandidates)break;
            continue;
        }
        if(s.Axes.empty()){Accept(s,s.Base,attempt,source,seed);break;}
        std::vector<double> y;for(std::size_t k=0;k<s.Axes.size();++k)y.push_back((seed[s.Axes[k]]-s.Base[s.Axes[k]])/s.Scale[k]);
        ceres::Problem problem;problem.AddResidualBlock(new Residual(s),nullptr,y.data());
        for(std::size_t k=0;k<s.Axes.size();++k){const auto i=s.Axes[k];problem.SetParameterLowerBound(y.data(),int(k),(s.Lower[i]-s.Base[i])/s.Scale[k]);problem.SetParameterUpperBound(y.data(),int(k),(s.Upper[i]-s.Base[i])/s.Scale[k]);}
        Monitor monitor(s);ceres::Solver::Options options;options.max_num_iterations=int(r.MaxIterations);options.num_threads=1;options.linear_solver_type=ceres::DENSE_QR;options.minimizer_progress_to_stdout=false;options.logging_type=ceres::SILENT;
        options.function_tolerance=1e-14;options.gradient_tolerance=1e-14;options.parameter_tolerance=1e-14;options.callbacks.push_back(&monitor);
        options.max_solver_time_in_seconds=std::max(1e-6,(r.MaxTimeMs-std::chrono::duration<double,std::milli>(Clock::now()-s.Started).count())/1000);
        ceres::Solver::Summary summary;ceres::Solve(options,&problem,&summary);
        if(s.Stop())break;
        Accept(s,s.Physical(y.data()),attempt,source,seed);
        if(s.Output.Candidates.size()>=r.MaxCandidates)break;
    }
    s.Output.ElapsedMs=std::chrono::duration<double,std::milli>(Clock::now()-s.Started).count();s.Output.TimeBudgetExhausted=s.TimedOut;s.Output.SeedBudgetExhausted=s.Output.SeedsAttempted>=r.MaxSeeds;
    if(s.WasCancelled){s.Output.State=Status::Cancelled;s.Output.Diagnostic="Cancelled; any retained candidates precede cancellation.";}
    else if(s.TimedOut){s.Output.State=Status::BudgetExhausted;s.Output.Diagnostic="Time budget exhausted; retained candidates do not establish search completeness.";}
    else if(!s.Output.Candidates.empty()){s.Output.State=Status::Solved;s.Output.Diagnostic=analytic?"Topology-derived 3P-2R inverse branches accepted by independent Pinocchio FK and physical constraints; free rotary pole branches retain caller seeds.":"FK-validated candidates from finite local numerical searches; branch coverage is not complete.";}
    else {s.Output.State=Status::NoCandidate;s.Output.Diagnostic=analytic?"No FK-accepted constrained 3P-2R analytic candidate in the finite angle domain; free pole representatives remain finite.":"No accepted candidate in the finite seed/iteration budget; physical unreachability is not established.";}
    return s.Output;
}
}
