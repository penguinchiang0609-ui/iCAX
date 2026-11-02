#include <GeometryServices/MachineMotionPlanningService.h>
#include <GeometryServices/PathOrientationPlanningService.h>
#include <Services/ServicesHelper.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <copp/copp.h>
namespace {
using namespace iCAX::GeometryServices;
namespace MM=MachineMotion;
namespace MK=MachineKinematics;
using Clock=std::chrono::steady_clock;
constexpr auto NoStation=std::numeric_limits<std::size_t>::max();
constexpr double Pi=3.14159265358979323846;
double Length(const MK::Vector& a){return std::hypot(a[0],a[1],a[2]);}
double Distance(const MK::Vector& a,const MK::Vector& b){return std::hypot(a[0]-b[0],a[1]-b[1],a[2]-b[2]);}
double Angle(const MK::Vector& a,const MK::Vector& b){double dot=0,cross=0;for(int k=0;k<3;++k){dot+=a[k]*b[k];const auto c=a[(k+1)%3]*b[(k+2)%3]-a[(k+2)%3]*b[(k+1)%3];cross+=c*c;}return std::atan2(std::sqrt(cross),std::clamp(dot,-1.,1.));}
MK::Vector Unit(MK::Vector d){const auto n=Length(d);if(!std::isfinite(n)||n<1e-12)throw std::invalid_argument("MOTION_DIRECTION_INVALID");for(auto& v:d)v/=n;return d;}
struct Context {
    const MM::Query& Q;const std::function<bool()>& Cancel;IMachineKinematicsService& Kinematics;IPathOrientationPlanningService& Graph;
    MK::ModelRef Model;MM::Result R;Clock::time_point Started=Clock::now();
    std::vector<std::size_t> Active;std::vector<double> Lower,Upper;
    std::vector<std::vector<MK::Candidate>> Candidates;
    struct Segment {double First=0,Last=1;std::vector<double> A,B;std::vector<MM::Sample> Samples;};
    using EdgeKey=std::tuple<std::size_t,std::vector<double>,std::vector<double>>;
    std::map<EdgeKey,std::vector<Segment>> Paths;
    std::set<std::pair<std::size_t,std::vector<double>>> GoodStates;
    std::size_t EdgeRefinementCalls=0;
    bool RefinementSearchLimited=false;
    double Elapsed()const{return std::chrono::duration<double,std::milli>(Clock::now()-Started).count();}
    bool Stop(){if(Cancel&&Cancel()){R.State=MM::Status::Cancelled;R.Diagnostic="Cancelled; no partial plan published.";return true;}if(Elapsed()>=Q.MaximumSeconds*1000){R.State=MM::Status::BudgetExhausted;R.Diagnostic="Overall planning budget exhausted; no partial plan published.";return true;}return false;}
    std::vector<double> ToActive(const std::vector<double>& full)const{std::vector<double> a;for(auto i:Active)a.push_back(full[i]);return a;}
    std::vector<double> ToFull(const std::vector<double>& a)const{auto q=Lower;for(std::size_t i=0;i<Active.size();++i)q[Active[i]]=a[i];return q;}
    double SeedDistance(const std::vector<double>& a,const std::vector<double>& b)const{double d=0;for(auto i:Active){const auto x=(a[i]-b[i])/(Upper[i]-Lower[i]);d+=x*x;}return d;}
    bool Limits(const std::vector<double>& q)const{if(q.size()!=Lower.size())return false;for(std::size_t i=0;i<q.size();++i)if(!std::isfinite(q[i])||q[i]<Lower[i]-1e-9||q[i]>Upper[i]+1e-9)return false;return true;}
    bool Validation(const MM::Validation& v,std::size_t station,std::size_t edge){if(v.State==MM::Verdict::Accepted)return true;R.FailedStation=station;R.FailedEdge=edge;R.State=v.State==MM::Verdict::Unresolved?MM::Status::Unresolved:MM::Status::Rejected;R.Diagnostic=v.Diagnostic;return false;}
    bool State(std::size_t i,const std::vector<double>& q){if(Stop())return false;if(GoodStates.contains({i,q}))return true;++R.StateChecks;if(!Limits(q))return Validation({MM::Verdict::Rejected,"Physical axis limits violated."},i,NoStation);const auto valid=Q.CheckState(i,q);if(Stop())return false;if(!Validation(valid,i,NoStation))return false;GoodStates.emplace(i,q);return true;}
    MK::Result Inverse(const MK::Target& target,const std::vector<std::vector<double>>& seeds){
        MK::Request request;request.TaskDefinition=Q.TaskDefinition;request.Goal=target;request.Seeds=seeds;request.Constraints=Q.Constraints;
        request.MaxSeeds=Q.IKSeedsPerTarget;request.MaxCandidates=Q.IKCandidatesPerTarget;request.MaxTimeMs=std::max(1e-6,std::min(Q.IKTimeMsPerTarget,Q.MaximumSeconds*1000-Elapsed()));
        // Interior refinement needs numerical headroom: accepting IK at the
        // final contour tolerance can keep every smaller chord above the 1/4
        // refinement trigger even when subdivision cannot improve that error.
        request.PositionToleranceMm=Q.PositionToleranceMm*.1;request.DirectionToleranceRad=Q.DirectionToleranceRad*.1;request.PositionScaleMm=Q.PositionScaleMm;request.DirectionScale=Q.DirectionScale;
        ++R.IKCalls;auto result=Kinematics.InverseCandidates(Model,request,[&]{return Stop();});R.IKSeeds+=result.SeedsAttempted;
        R.CandidateSearchBudgetExhausted|=result.State==MK::Status::BudgetExhausted||result.SeedBudgetExhausted;return result;
    }
    MM::Sample Sample(std::size_t edge,double global,double local,const std::vector<double>& a,const std::vector<double>& b,const MK::Vector& da,const MK::Vector& db){
        MM::Sample s;s.SourceStation=edge;s.SourceFraction=global;s.DirectionChecked=false;s.Positions.resize(a.size());for(std::size_t i=0;i<a.size();++i)s.Positions[i]=a[i]+local*(b[i]-a[i]);
        const auto fk=Kinematics.Forward(Model,s.Positions,Q.TaskDefinition);s.TCP=fk.PositionMm;s.Direction=fk.Direction;
        if(edge){const auto ref=Q.ReferenceAt(edge,global,da,db);for(auto x:ref.Goal.PositionMm)if(!std::isfinite(x))throw std::invalid_argument("MOTION_REFERENCE_NONFINITE");s.PositionErrorMm=Distance(s.TCP,ref.Goal.PositionMm);s.DirectionChecked=ref.CheckDirection;if(ref.CheckDirection)s.DirectionErrorRad=Angle(s.Direction,Unit(ref.Goal.Direction));}
        return s;
    }
    bool Probe(std::size_t edge,double first,double last,const std::vector<double>& a,const std::vector<double>& b,
        const MK::Vector& da,const MK::Vector& db,bool& refine,bool& outside,std::vector<MM::Sample>& samples){
        std::map<double,MM::Sample> ordered;refine=false;outside=false;
        std::function<bool(double,double,std::size_t)> visit=[&](double left,double right,std::size_t depth){
            if(Stop())return false;if(R.VerificationSamples>=Q.MaximumVerificationSamples){R.State=MM::Status::BudgetExhausted;R.Diagnostic="Adaptive FK verification sample budget exhausted.";R.FailedEdge=edge;return false;}
            const auto local=(left+right)/2,global=first+(last-first)*local;auto s=Sample(edge,global,local,a,b,da,db);++R.VerificationSamples;
            R.MaximumPositionErrorMm=std::max(R.MaximumPositionErrorMm,s.PositionErrorMm);R.MaximumDirectionErrorRad=std::max(R.MaximumDirectionErrorRad,s.DirectionErrorRad);
            outside|=s.PositionErrorMm>Q.PositionToleranceMm||s.DirectionErrorRad>Q.DirectionToleranceRad;
            refine|=s.PositionErrorMm>Q.PositionToleranceMm*.25||s.DirectionErrorRad>Q.DirectionToleranceRad*.25;
            ordered.emplace(local,std::move(s));
            if(depth<Q.MinimumVerificationDepth&&(!visit(left,local,depth+1)||!visit(local,right,depth+1)))return false;return true;
        };
        if(!visit(0,1,0))return false;for(auto& [_,sample]:ordered)samples.push_back(std::move(sample));return true;
    }
    bool CertifyLinear(std::size_t edge,double first,double last,const std::vector<double>& a,const std::vector<double>& b,
        const MK::Vector& da,const MK::Vector& db,std::size_t proofDepth,std::vector<Segment>& output,std::vector<MM::Sample> samples,
        const std::string& endSeed,const std::string& endBranch){
        if(Stop())return false;++R.MotionChecks;const auto valid=Q.CheckMotion(edge,a,b);if(Stop())return false;
        constexpr std::size_t MaximumCertificateDepth=12;
        if(valid.State==MM::Verdict::Unresolved&&proofDepth<MaximumCertificateDepth&&a!=b){
            // Independent proof depth: geometry/IK subdivision may already
            // have used its own allowance. Preserve THIS physical axis line;
            // this function never calls inverse kinematics or Refine.
            const auto middle=(first+last)/2;auto midpoint=a;for(std::size_t i=0;i<a.size();++i)midpoint[i]=(a[i]+b[i])*.5;
            if(midpoint==a||midpoint==b)return Validation(valid,edge,edge);
            if(!State(edge,midpoint))return false;std::vector<Segment> children;
            const auto half=[&](double lo,double hi,const auto& x,const auto& y,const auto& seed){
                bool refine=false,outside=false;std::vector<MM::Sample> probes;
                if(!Probe(edge,lo,hi,x,y,da,db,refine,outside,probes))return false;
                if(outside)return Validation({MM::Verdict::Unresolved,"Fixed joint-linear certificate subdivision exposed an FK reference error beyond the unchanged tolerance."},edge,edge);
                return CertifyLinear(edge,lo,hi,x,y,da,db,proofDepth+1,children,std::move(probes),seed,endBranch);
            };
            if(half(first,middle,a,midpoint,std::string("CertifiedAxisMidpoint"))&&half(middle,last,midpoint,b,endSeed)){
                output.insert(output.end(),std::make_move_iterator(children.begin()),std::make_move_iterator(children.end()));return true;
            }return false;
        }
        if(!Validation(valid,edge,edge))return false;
        auto endpoint=Sample(edge,last,1,a,b,da,db);endpoint.SeedSource=endSeed;endpoint.BranchKey=endBranch;samples.push_back(std::move(endpoint));
        output.push_back({first,last,a,b,std::move(samples)});return true;
    }
    bool Refine(std::size_t edge,double first,double last,const std::vector<double>& a,const std::vector<double>& b,
        const MK::Vector& da,const MK::Vector& db,std::size_t depth,std::vector<Segment>& output,
        const std::string& endSeed={},const std::string& endBranch={}){
        if(Stop())return false;
        // Depth alone permits exponential backtracking over inverse branches.
        // Bound each candidate edge independently, leaving the graph its
        // remaining budget to try other endpoint candidates. This is an
        // unresolved finite search, never a collision/unreachability proof.
        const auto edgeLimit=std::max<std::size_t>(64,std::size_t(1)<<std::min<std::size_t>(Q.MaximumVerificationDepth,10));
        if(++EdgeRefinementCalls>edgeLimit){RefinementSearchLimited=true;R.CandidateSearchBudgetExhausted=true;return Validation({MM::Verdict::Unresolved,"Candidate edge refinement budget exhausted; alternate graph candidates remain eligible."},edge,edge);}
        if(Q.Progress)Q.Progress("Refining",edge,Q.Stations.size());
        bool refine=false,outside=false;std::vector<MM::Sample> samples;
        if(!Probe(edge,first,last,a,b,da,db,refine,outside,samples))return false;
        bool axisStep=false;for(auto i:Active)axisStep|=std::abs(b[i]-a[i])>Q.MaximumAxisSteps[i]+1e-12;
        if((refine||axisStep)&&depth<Q.MaximumVerificationDepth){
            const auto middle=(first+last)/2;std::vector<double> interpolated(a.size());for(std::size_t i=0;i<a.size();++i)interpolated[i]=(a[i]+b[i])*.5;
            std::vector<MK::Candidate> candidates;
            if(!edge){MK::Candidate candidate;candidate.Positions=interpolated;candidate.SeedSource="InitialAxisInterpolation";candidates.push_back(std::move(candidate));}
            else{
                const auto reference=Q.ReferenceAt(edge,middle,da,db);auto goal=reference.Goal;goal.Direction=Unit(goal.Direction);
                const auto result=Inverse(goal,{interpolated,a,b});if(Stop())return false;
                if(result.State==MK::Status::Unsupported){R.State=MM::Status::Unsupported;R.Diagnostic=result.Diagnostic;R.FailedEdge=edge;return false;}
                if(result.Candidates.empty()&&result.State==MK::Status::BudgetExhausted){R.State=MM::Status::BudgetExhausted;R.FailedStation=edge;R.FailedEdge=edge;R.Diagnostic="Interior inverse search budget exhausted; no partial plan published.";return false;}
                candidates=result.Candidates;std::stable_sort(candidates.begin(),candidates.end(),[&](const auto& x,const auto& y){return SeedDistance(x.Positions,interpolated)<SeedDistance(y.Positions,interpolated);});
            }
            if(candidates.empty())return Validation({MM::Verdict::Rejected,"No FK-accepted inverse candidate at a required interior TCP reference; no partial plan published."},edge,edge);
            for(const auto& candidate:candidates){
                if(Stop())return false;if(!State(edge,candidate.Positions)){if(R.State==MM::Status::Cancelled||R.State==MM::Status::BudgetExhausted)return false;continue;}
                std::vector<Segment> children;
                if(Refine(edge,first,middle,a,candidate.Positions,da,db,depth+1,children,candidate.SeedSource,candidate.BranchKey)&&
                   Refine(edge,middle,last,candidate.Positions,b,da,db,depth+1,children,endSeed,endBranch)){
                    output.insert(output.end(),std::make_move_iterator(children.begin()),std::make_move_iterator(children.end()));return true;
                }
                if(R.State==MM::Status::Cancelled||R.State==MM::Status::BudgetExhausted||R.State==MM::Status::Unsupported)return false;
            }return false;
        }
        if(outside||axisStep)return Validation({MM::Verdict::Unresolved,"Adaptive inverse subdivision depth exhausted before physical-axis interpolation met the specified reference/axis-step tolerance."},edge,edge);
        // Certify the ACTUAL piecewise joint-linear route, after solving its
        // reference subdivision. An unsafe coarse chord is not the executed path.
        return CertifyLinear(edge,first,last,a,b,da,db,0,output,std::move(samples),endSeed,endBranch);
    }
    bool Motion(std::size_t edge,const std::vector<double>& a,const std::vector<double>& b,const MK::Vector& da,const MK::Vector& db){
        if(Stop())return false;const EdgeKey key{edge,a,b};if(Paths.contains(key))return true;
        EdgeRefinementCalls=0;
        std::vector<Segment> path;
        if(Refine(edge,0,1,a,b,da,db,0,path)){Paths.emplace(key,std::move(path));return true;}
        if(edge||Stop()||R.State==MM::Status::Unsupported)return false;
        // Initial positioning has no authored TCP contour. A rejected direct
        // axis chord must not make an otherwise valid first pose unreachable.
        // Try finite axis staging; every actual submove and the final quintic
        // still pass the same complete collision and physical-axis checks.
        std::vector<std::vector<double>> bridges;
        auto rotations=a,translations=a;
        for(auto i:Active){if(Q.Machine.Nodes[i].Type==MK::JointType::Prismatic)translations[i]=b[i];else rotations[i]=b[i];}
        bridges.push_back(rotations);bridges.push_back(translations);
        for(auto i:Active){auto first=a,last=b;first[i]=b[i];last[i]=a[i];bridges.push_back(std::move(first));bridges.push_back(std::move(last));}
        std::set<std::vector<double>> visited;
        for(const auto& via:bridges){
            if(Stop())return false;if(via==a||via==b||!visited.insert(via).second)continue;
            if(!State(0,via))continue;
            std::vector<Segment> staged;
            if(Refine(0,0,.5,a,via,da,db,0,staged,"InitialAxisStaging")&&Refine(0,.5,1,via,b,da,db,0,staged,"InitialAxisStaging")){
                Paths.emplace(key,std::move(staged));R.Coverage="FiniteCandidateGraph;InitialAxisStaging";return true;
            }
        }
        return false;
    }
};
#include "MachineTrajectoryTiming.inc"
void Validate(Context& c){const auto& q=c.Q;
    if(q.Stations.empty()||q.Stations.size()>32768||!q.CheckState||!q.CheckMotion||!q.CheckTrajectory||!q.ReferenceAt||q.TaskDefinition.Kind!=MK::TaskKind::PositionAndDirection||
       !std::isfinite(q.MaximumSeconds)||q.MaximumSeconds<=0||q.MaximumSeconds>600||!std::isfinite(q.IKTimeMsPerTarget)||q.IKTimeMsPerTarget<=0||
       !q.IKSeedsPerTarget||q.IKSeedsPerTarget>128||!q.IKCandidatesPerTarget||q.IKCandidatesPerTarget>128||!q.MaximumCandidatesPerStation||q.MaximumCandidatesPerStation>256||
       q.MinimumVerificationDepth>q.MaximumVerificationDepth||q.MaximumVerificationDepth>20||!q.MaximumVerificationSamples||
       !std::isfinite(q.PositionToleranceMm)||q.PositionToleranceMm<=0||!std::isfinite(q.DirectionToleranceRad)||q.DirectionToleranceRad<=0||
       !std::isfinite(q.PositionScaleMm)||q.PositionScaleMm<=0||!std::isfinite(q.DirectionScale)||q.DirectionScale<=0)throw std::invalid_argument("MOTION_QUERY_INVALID");
    const auto size=q.Machine.Nodes.size();if(q.InitialPositions.size()!=size||q.MaximumVelocities.size()!=size||q.MaximumAccelerations.size()!=size||q.MaximumJerks.size()!=size||q.MaximumAxisSteps.size()!=size)throw std::invalid_argument("MOTION_AXIS_VECTOR_SIZE");
    for(std::size_t i=0;i<size;++i){const auto& n=q.Machine.Nodes[i];const auto fixed=n.Type==MK::JointType::Fixed;c.Lower.push_back(fixed?0:n.Lower);c.Upper.push_back(fixed?0:n.Upper);}
    std::set<std::size_t> constrained;
    for(const auto& constraint:q.Constraints){const auto it=std::find_if(q.Machine.Nodes.begin(),q.Machine.Nodes.end(),[&](const auto& n){return n.ID==constraint.AxisID;});if(it==q.Machine.Nodes.end())throw std::invalid_argument("MOTION_CONSTRAINT_AXIS_UNKNOWN");const auto i=it-q.Machine.Nodes.begin();
        if(!constrained.insert(i).second||std::isnan(constraint.Lower)||std::isnan(constraint.Upper)||constraint.Lower>constraint.Upper)throw std::invalid_argument("MOTION_CONSTRAINT_INVALID");c.Lower[i]=std::max(c.Lower[i],constraint.Lower);c.Upper[i]=std::min(c.Upper[i],constraint.Upper);
        if(c.Lower[i]>c.Upper[i])throw std::invalid_argument("MOTION_CONSTRAINT_DISJOINT");if(constraint.Fixed){if(!std::isfinite(constraint.FixedValue)||constraint.FixedValue<c.Lower[i]||constraint.FixedValue>c.Upper[i])throw std::invalid_argument("MOTION_FIXED_AXIS_INVALID");c.Lower[i]=c.Upper[i]=constraint.FixedValue;}}
    for(std::size_t i=0;i<size;++i)if(c.Lower[i]<c.Upper[i]){if(!std::isfinite(q.MaximumVelocities[i])||q.MaximumVelocities[i]<=0||std::isnan(q.MaximumAccelerations[i])||q.MaximumAccelerations[i]<=0||std::isnan(q.MaximumJerks[i])||q.MaximumJerks[i]<=0||!std::isfinite(q.MaximumAxisSteps[i])||q.MaximumAxisSteps[i]<=0)throw std::invalid_argument("MOTION_AXIS_DYNAMICS_INVALID");c.Active.push_back(i);}
    if(!c.Limits(q.InitialPositions))throw std::invalid_argument("MOTION_INITIAL_LIMIT_INVALID");
    for(const auto& s:q.Stations){if(s.AlternativeDirections.size()>64||std::isnan(s.FeedMmPerSecond)||s.FeedMmPerSecond<=0)throw std::invalid_argument("MOTION_STATION_INVALID");for(auto x:s.Goal.PositionMm)if(!std::isfinite(x))throw std::invalid_argument("MOTION_STATION_NONFINITE");(void)Unit(s.Goal.Direction);for(auto d:s.AlternativeDirections)(void)Unit(d);}
}
class Service final:public IMachineMotionPlanningService {
    AUTO_REGIST_SERVICE(IMachineMotionPlanningService,Service,IMachineKinematicsService,IPathOrientationPlanningService)
    std::shared_ptr<IMachineKinematicsService> Kinematics;std::shared_ptr<IPathOrientationPlanningService> Graph;
public:
    Service(std::shared_ptr<IMachineKinematicsService> k,std::shared_ptr<IPathOrientationPlanningService> g):Kinematics(std::move(k)),Graph(std::move(g)){}
    void OnLoad()override{}void OnUnload()override{}
    MM::Result Plan(const MM::Query& q,const std::function<bool()>& cancelled)const override{
        if(!Kinematics||!Graph)throw std::runtime_error("MOTION_PROVIDER_DEPENDENCY_MISSING");Context c{q,cancelled,*Kinematics,*Graph};c.R.Provider="Pinocchio+Ceres / DescartesLight / COPP-0.2.3 TOPP3-SOCP";
        const auto finish=[&](){c.R.ElapsedMs=c.Elapsed();if(c.R.State!=MM::Status::Solved){c.R.Samples.clear();c.R.Segments.clear();c.R.DurationSeconds=0;}return c.R;};
        if(q.TaskDefinition.Kind!=MK::TaskKind::PositionAndDirection){c.R.State=MM::Status::Unsupported;c.R.Diagnostic="Only PositionAndDirection is enabled.";return finish();}
        Validate(c);for(auto axis:c.Active)if(!std::isfinite(q.MaximumAccelerations[axis])||!std::isfinite(q.MaximumJerks[axis]))c.R.TimingQualification="ConfiguredAxisLimits;BlankIsUnbounded";
        c.Model=Kinematics->BuildModel(q.Machine);if(c.Stop())return finish();
        if(q.Progress)q.Progress("CheckingMotion",0,q.Stations.size());
        if(!c.State(NoStation,q.InitialPositions))return finish();
        c.Candidates.resize(q.Stations.size());std::vector<std::vector<double>> warm{q.InitialPositions};
        for(std::size_t i=0;i<q.Stations.size();++i){c.R.FailedStation=i;if(c.Stop())return finish();if(q.Progress)q.Progress("Solving",i,q.Stations.size());const auto& station=q.Stations[i];auto directions=station.AlternativeDirections;if(directions.empty())directions.push_back(station.Goal.Direction);
            for(const auto& direction:directions){if(c.Stop())return finish();auto target=station.Goal;target.Direction=Unit(direction);const auto result=c.Inverse(target,warm);
                if(result.State==MK::Status::Cancelled||c.Stop())return finish();if(result.State==MK::Status::Unsupported){c.R.State=MM::Status::Unsupported;c.R.Diagnostic=result.Diagnostic;return finish();}
                for(const auto& candidate:result.Candidates){if(std::none_of(c.Candidates[i].begin(),c.Candidates[i].end(),[&](const auto& old){return c.SeedDistance(old.Positions,candidate.Positions)<1e-14;}))c.Candidates[i].push_back(candidate);if(c.Candidates[i].size()>=q.MaximumCandidatesPerStation)break;}
                if(c.Candidates[i].size()>=q.MaximumCandidatesPerStation)break;
            }
            if(c.Candidates[i].empty()){c.R.State=c.R.CandidateSearchBudgetExhausted?MM::Status::BudgetExhausted:MM::Status::NoCandidate;c.R.Diagnostic="No FK-accepted candidate at the station in the finite IK seed/time budget; unreachability is not established.";return finish();}
            std::stable_sort(c.Candidates[i].begin(),c.Candidates[i].end(),[&](const auto& a,const auto& b){return c.SeedDistance(a.Positions,warm.front())<c.SeedDistance(b.Positions,warm.front());});
            warm.clear();for(const auto& candidate:c.Candidates[i]){warm.push_back(candidate.Positions);if(warm.size()>=q.IKSeedsPerTarget)break;}c.R.CandidateCount+=c.Candidates[i].size();
        }
        // Forward-only warm seeds freeze a free rotary coordinate at a pole.
        // The first non-pole target can then demand a large branch jump while
        // already tilting. Propagate FUTURE seeds backwards only at rank-loss
        // stations, adding bounded FK-accepted representatives of the SAME
        // owner-authorized TCP/directions. The graph still checks all states
        // and every complete physical edge, including the relocated free spin.
        const auto expectedRank=std::min<std::size_t>(5,c.Active.size());
        for(std::size_t end=q.Stations.size();end>1;--end){const auto i=end-2;if(c.Stop())return finish();
            if(c.Candidates[i].size()>=q.MaximumCandidatesPerStation||std::none_of(c.Candidates[i].begin(),c.Candidates[i].end(),[&](const auto& candidate){return candidate.Conditioning.Rank<expectedRank;}))continue;
            if(q.Progress)q.Progress("SeedingContinuity",i,q.Stations.size());std::vector<std::vector<double>> future;
            for(const auto& candidate:c.Candidates[i+1]){future.push_back(candidate.Positions);if(future.size()>=q.IKSeedsPerTarget)break;}
            std::vector<std::vector<double>> blended;for(const auto& seed:future){auto middle=seed;for(std::size_t axis=0;axis<middle.size();++axis)middle[axis]=(middle[axis]+c.Candidates[i].front().Positions[axis])*.5;blended.push_back(std::move(middle));}
            auto directions=q.Stations[i].AlternativeDirections;if(directions.empty())directions.push_back(q.Stations[i].Goal.Direction);
            for(const auto& direction:directions){if(c.Stop())return finish();const auto unit=Unit(direction);
                const bool pole=std::any_of(c.Candidates[i].begin(),c.Candidates[i].end(),[&](const auto& candidate){return candidate.Conditioning.Rank<expectedRank&&Angle(Kinematics->Forward(c.Model,candidate.Positions,q.TaskDefinition).Direction,unit)<=q.DirectionToleranceRad;});if(!pole)continue;
                auto target=q.Stations[i].Goal;target.Direction=unit;
                // Bounded intermediate seeds let a run of equivalent pole
                // stations share the free motion, instead of a squared-cost
                // tie postponing its entire turn to the first tilted edge.
                // These are seeds only: inverse/FK acceptance still targets the
                // unchanged exact source TCP and authorized signed direction.
                for(const auto* seeds:{&future,&blended}){const auto result=c.Inverse(target,*seeds);if(c.Stop())return finish();
                    if(result.State==MK::Status::Unsupported){c.R.State=MM::Status::Unsupported;c.R.Diagnostic=result.Diagnostic;return finish();}
                    for(auto candidate:result.Candidates){if(std::any_of(c.Candidates[i].begin(),c.Candidates[i].end(),[&](const auto& old){return c.SeedDistance(old.Positions,candidate.Positions)<1e-14;}))continue;
                        candidate.SeedSource="FutureStationSeed:"+candidate.SeedSource;c.Candidates[i].push_back(std::move(candidate));++c.R.CandidateCount;if(c.Candidates[i].size()>=q.MaximumCandidatesPerStation)break;
                    }
                    if(c.Candidates[i].size()>=q.MaximumCandidatesPerStation)break;
                }
                if(c.Candidates[i].size()>=q.MaximumCandidatesPerStation)break;
            }
            // Equal-cost paths should align a free coordinate before leaving
            // its rank-loss interval, rather than defer the same rotation to
            // the first direction-constrained edge merely by insertion order.
            std::stable_sort(c.Candidates[i].begin(),c.Candidates[i].end(),[&](const auto& a,const auto& b){
                const auto distance=[&](const auto& candidate){double best=std::numeric_limits<double>::infinity();for(const auto& seed:future)best=std::min(best,c.SeedDistance(candidate.Positions,seed));return best;};return distance(a)<distance(b);
            });
        }
          // First finish all pole/future seeding. Then reject invalid endpoint
          // poses before expensive interior IK and graph refinement. Preserve
          // unresolved state checks instead of reporting a rejected alternative
          // as the final cause when no candidate has a complete certificate.
          for(std::size_t i=0;i<c.Candidates.size();++i){
              c.R.FailedStation=i;std::vector<MK::Candidate> validCandidates;std::string unresolved;
              for(auto& candidate:c.Candidates[i]){
                  if(c.State(i,candidate.Positions))validCandidates.push_back(std::move(candidate));
                  else{if(c.Stop())return finish();if(c.R.State==MM::Status::Unresolved)unresolved=c.R.Diagnostic;}
              }
              c.Candidates[i]=std::move(validCandidates);
              if(c.Candidates[i].empty()){if(!unresolved.empty()){c.R.State=MM::Status::Unresolved;c.R.Diagnostic=unresolved;}c.R.FailedStation=i;c.R.FailedEdge=NoStation;return finish();}
          }
          if(q.Progress)q.Progress("Selecting",q.Stations.size(),q.Stations.size());
        PathOrientationQuery graph;graph.AllWaypointsEditable=true;graph.MaximumSeconds=std::max(1e-6,q.MaximumSeconds-c.Elapsed()/1000);graph.MaximumGraphEdges=q.MaximumGraphEdges;graph.MaximumSearchIterations=q.MaximumGraphSearchIterations;
        // Source-station gaps may exceed one executable axis step. The motion
        // callback inserts and certifies actual submoves before accepting them.
        for(auto i:c.Active)graph.Axes.push_back({c.Lower[i],c.Upper[i],c.Upper[i]-c.Lower[i],1/std::pow(c.Upper[i]-c.Lower[i],2)});
        const auto initial=Kinematics->Forward(c.Model,q.InitialPositions,q.TaskDefinition);OrientationWaypoint first;first.Position=initial.PositionMm;first.Original={initial.Direction,c.ToActive(q.InitialPositions)};first.Locked=true;graph.Waypoints.push_back(first);
        for(std::size_t i=0;i<q.Stations.size();++i){OrientationWaypoint waypoint;waypoint.Position=q.Stations[i].Goal.PositionMm;waypoint.MaximumDeviationRadians=Pi;
            for(const auto& candidate:c.Candidates[i]){const auto fk=Kinematics->Forward(c.Model,candidate.Positions,q.TaskDefinition);waypoint.Candidates.push_back({fk.Direction,c.ToActive(candidate.Positions)});}
            // Keep the requested direction as the graph's preference. Warm
            // seed sorting can put a free-direction alternative first; making
            // that arbitrary candidate the Original discarded the source
            // orientation before any collision required changing it.
            waypoint.Original=*std::min_element(waypoint.Candidates.begin(),waypoint.Candidates.end(),[&](const auto& a,const auto& b){return Angle(a.Direction,q.Stations[i].Goal.Direction)<Angle(b.Direction,q.Stations[i].Goal.Direction);});
            graph.Waypoints.push_back(std::move(waypoint));}
        graph.CheckState=[&](std::size_t i,const auto& p){return c.State(i?i-1:NoStation,c.ToFull(p.Axes));};
        graph.CheckMotion=[&](std::size_t edge,const auto& a,const auto& b){return c.Motion(edge,c.ToFull(a.Axes),c.ToFull(b.Axes),a.Direction,b.Direction);};
        const auto planned=Graph->Plan(graph,[&]{return c.Stop();});if(c.Stop())return finish();
        if(planned.Status!="Solved"){const bool graphBudget=planned.Status=="Timeout"||planned.Status.find("Budget")!=std::string::npos;if(graphBudget){c.R.State=MM::Status::BudgetExhausted;c.R.Diagnostic="Finite candidate graph budget exhausted: "+planned.Status;}else if(c.RefinementSearchLimited){c.R.State=MM::Status::BudgetExhausted;c.R.Diagnostic="Candidate edge refinement budget exhausted; no fully certified route found. A rejected alternative is not a collision verdict for the source path.";}else if(c.R.Diagnostic.empty()){c.R.State=MM::Status::Rejected;c.R.Diagnostic="Finite candidate graph: "+planned.Status;}if(planned.FailedWaypoint!=NoStation)c.R.FailedStation=planned.FailedWaypoint?planned.FailedWaypoint-1:NoStation;if(planned.FailedEdge!=NoStation)c.R.FailedEdge=planned.FailedEdge;return finish();}
        // Publish only the selected certified graph. Failed optimistic branch
        // diagnostics/counters remain useful but never become a partial plan.
        c.R.FailedStation=c.R.FailedEdge=NoStation;c.R.MaximumPositionErrorMm=c.R.MaximumDirectionErrorRad=0;
        MM::Sample start;start.Positions=q.InitialPositions;start.Velocities.assign(start.Positions.size(),0);start.Accelerations=start.Velocities;start.TCP=initial.PositionMm;start.Direction=initial.Direction;start.SeedSource="Initial";start.DirectionChecked=false;c.R.Samples.push_back(start);std::vector<SelectedMove> selected;
        for(std::size_t edge=0;edge<q.Stations.size();++edge){if(c.Stop())return finish();const auto a=c.ToFull(planned.Orientations[edge].Axes),b=c.ToFull(planned.Orientations[edge+1].Axes);const auto& da=planned.Orientations[edge].Direction;const auto& db=planned.Orientations[edge+1].Direction;
            const auto path=c.Paths.find({edge,a,b});if(path==c.Paths.end())throw std::runtime_error("MOTION_SELECTED_EDGE_CERTIFICATE_MISSING");const auto& station=q.Stations[edge];
            std::string seed,branch;for(const auto& candidate:c.Candidates[edge])if(c.SeedDistance(b,candidate.Positions)<1e-14){seed=candidate.SeedSource;branch=candidate.BranchKey;break;}
            for(const auto& segment:path->second)selected.push_back({edge,segment.First,segment.Last,segment.A,segment.B,da,db,seed,branch});
        }
        try{if(!CompileSmoothTrajectory(c,selected))return finish();}catch(const std::exception& error){c.R.State=MM::Status::Unresolved;c.R.Diagnostic=error.what();return finish();}
        if(c.Stop())return finish();c.R.State=MM::Status::Solved;c.R.Diagnostic="COPP third-order time law compiled to a C2 quintic axis trajectory; complete polynomial collision callbacks and all configured derivative bounds passed; unspecified acceleration/jerk limits are unbounded. Contour/direction/feed checks remain sampled; controller and machine calibration are unqualified.";return finish();
    }
};
}
extern "C" __declspec(dllexport) iCAX::GeometryServices::IMachineMotionPlanningService* CreateMachineMotionPlanningService(iCAX::GeometryServices::IMachineKinematicsService* k,iCAX::GeometryServices::IPathOrientationPlanningService* g){return new Service(std::shared_ptr<iCAX::GeometryServices::IMachineKinematicsService>(k,[](auto*){}),std::shared_ptr<iCAX::GeometryServices::IPathOrientationPlanningService>(g,[](auto*){}));}
extern "C" __declspec(dllexport) void DestroyMachineMotionPlanningService(iCAX::GeometryServices::IMachineMotionPlanningService* s){delete s;}
