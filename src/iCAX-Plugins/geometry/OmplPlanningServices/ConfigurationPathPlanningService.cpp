#include <GeometryServices/ConfigurationPathPlanningService.h>
#include <Services/ServicesHelper.h>
#include <ompl/base/SpaceInformation.h>
#include <ompl/base/ProblemDefinition.h>
#include <ompl/base/MotionValidator.h>
#include <ompl/base/ScopedState.h>
#include <ompl/base/spaces/RealVectorStateSpace.h>
#include <ompl/base/spaces/SO2StateSpace.h>
#include <ompl/geometric/planners/rrt/RRTConnect.h>
#include <ompl/geometric/PathGeometric.h>
#include <ompl/geometric/PathSimplifier.h>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace {
using namespace iCAX::GeometryServices;
namespace ob=ompl::base;namespace og=ompl::geometric;
using Clock=std::chrono::steady_clock;
struct Deadline{};struct Cancelled{};
struct Context {
    const ConfigurationPathQuery& Q;const std::function<bool()>& Cancel;
    ConfigurationPathResult Result;Clock::time_point Started=Clock::now();
    double Elapsed()const{return std::chrono::duration<double,std::milli>(Clock::now()-Started).count();}
    void Check()const{if(Cancel&&Cancel())throw Cancelled{};if(Elapsed()>=Q.MaximumSeconds*1000)throw Deadline{};}
    PathConfiguration Read(const ob::State* state)const{
        PathConfiguration values;values.reserve(Q.Axes.size());const auto compound=state->as<ob::CompoundState>();
        for(std::size_t i=0;i<Q.Axes.size();++i)values.push_back(Q.Axes[i].Periodic?compound->as<ob::SO2StateSpace::StateType>(i)->value:compound->as<ob::RealVectorStateSpace::StateType>(i)->values[0]);
        return values;
    }
    void Write(ob::State* state,const PathConfiguration& values)const{
        auto compound=state->as<ob::CompoundState>();for(std::size_t i=0;i<values.size();++i)
            if(Q.Axes[i].Periodic)compound->as<ob::SO2StateSpace::StateType>(i)->value=values[i];else compound->as<ob::RealVectorStateSpace::StateType>(i)->values[0]=values[i];
    }
    bool State(const PathConfiguration& state){Check();++Result.StateChecks;const bool valid=Q.CheckState(state);Check();return valid;}
    bool Motion(const PathConfiguration& a,const PathConfiguration& b){Check();++Result.MotionChecks;const bool valid=Q.CheckMotion(a,b);Check();return valid;}
};
class MotionValidator final:public ob::MotionValidator {
    Context& C;
public:
    MotionValidator(const ob::SpaceInformationPtr& si,Context& context):ob::MotionValidator(si),C(context){}
    bool checkMotion(const ob::State* a,const ob::State* b)const override{
        const bool valid=C.Motion(C.Read(a),C.Read(b));if(valid)++valid_;else ++invalid_;return valid;
    }
    bool checkMotion(const ob::State* a,const ob::State* b,std::pair<ob::State*,double>& last)const override{
        if(checkMotion(a,b))return true;
        // The start is valid by the MotionValidator contract. Returning t=0
        // avoids making an unproved partial-segment validity assertion.
        last.second=0;if(last.first)si_->copyState(last.first,a);return false;
    }
};
void Validate(const ConfigurationPathQuery& q){
    if(q.Axes.empty()||q.Axes.size()>64||q.Start.size()!=q.Axes.size()||q.Goal.size()!=q.Axes.size()||!q.CheckState||!q.CheckMotion||
       !std::isfinite(q.MaximumSeconds)||q.MaximumSeconds<=0||q.MaximumSeconds>600)throw std::invalid_argument("PATH_QUERY_INVALID");
    constexpr double pi=3.14159265358979323846;
    for(std::size_t i=0;i<q.Axes.size();++i){const auto& a=q.Axes[i];
        if(!std::isfinite(a.Minimum)||!std::isfinite(a.Maximum)||!std::isfinite(a.Weight)||a.Maximum<=a.Minimum||a.Weight<=0||
           !std::isfinite(q.Start[i])||!std::isfinite(q.Goal[i])||(a.Periodic&&(std::abs(a.Minimum+pi)>1e-9||std::abs(a.Maximum-pi)>1e-9)))throw std::invalid_argument("PATH_AXIS_INVALID");
    }
}
class Service final:public IConfigurationPathPlanningService {
    AUTO_REGIST_SERVICE(IConfigurationPathPlanningService,Service)
public:
    void OnLoad()override{}void OnUnload()override{}
    ConfigurationPathResult Plan(const ConfigurationPathQuery& q,const std::function<bool()>& cancelled)const override{
        Validate(q);Context c{q,cancelled};
        try{
            c.Check();auto space=std::make_shared<ob::CompoundStateSpace>();
            for(const auto& axis:q.Axes){
                if(axis.Periodic)space->addSubspace(std::make_shared<ob::SO2StateSpace>(),axis.Weight);
                else{auto scalar=std::make_shared<ob::RealVectorStateSpace>(1);ob::RealVectorBounds bounds(1);bounds.setLow(axis.Minimum);bounds.setHigh(axis.Maximum);scalar->setBounds(bounds);space->addSubspace(scalar,axis.Weight);}
            }
            space->lock();auto si=std::make_shared<ob::SpaceInformation>(space);
            si->setStateValidityChecker([&](const ob::State* state){return c.State(c.Read(state));});
            si->setMotionValidator(std::make_shared<MotionValidator>(si,c));si->setup();
            ob::ScopedState<> start(space),goal(space);c.Write(start.get(),q.Start);c.Write(goal.get(),q.Goal);
            if(!space->satisfiesBounds(start.get())||!c.State(q.Start))c.Result.Status="InvalidStart";
            else if(!space->satisfiesBounds(goal.get())||!c.State(q.Goal))c.Result.Status="InvalidGoal";
            else if(c.Motion(q.Start,q.Goal)){c.Result.Status="Solved";c.Result.Method="Direct";c.Result.Path={q.Start,q.Goal};}
            else{
                c.Result.Method="RRTConnect";
                auto problem=std::make_shared<ob::ProblemDefinition>(si);problem->setStartAndGoalStates(start,goal);
                auto planner=std::make_shared<og::RRTConnect>(si);planner->setProblemDefinition(problem);planner->setup();
                const ob::PlannerTerminationCondition stop([&]{c.Check();return false;});
                planner->solve(stop);c.Check();
                if(!problem->hasExactSolution())c.Result.Status="Timeout";
                else{
                    auto path=std::dynamic_pointer_cast<og::PathGeometric>(problem->getSolutionPath());
                    if(!path)throw std::runtime_error("PATH_RESULT_INVALID");
                    og::PathSimplifier simplify(si);simplify.reduceVertices(*path,16,8,1.);
                    for(const auto* state:path->getStates())c.Result.Path.push_back(c.Read(state));
                    // Recheck the returned path through the caller's continuous
                    // validator. Approximate goals are never promoted to solved.
                    for(std::size_t i=1;i<c.Result.Path.size();++i)if(!c.Motion(c.Result.Path[i-1],c.Result.Path[i]))throw std::runtime_error("PATH_RECHECK_FAILED");
                    c.Result.Status="Solved";
                }
            }
        }catch(const Deadline&){c.Result.Status="Timeout";c.Result.Path.clear();}
        catch(const Cancelled&){c.Result.Status="Cancelled";c.Result.Path.clear();}
        c.Result.ElapsedMs=c.Elapsed();return c.Result;
    }
};
}
