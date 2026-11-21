#include "recovered_shaker_step.hpp"
#include <cmath>
#include <bit>
#include <stdexcept>
#include <numbers>
namespace study::recovered {
namespace {
double at(const std::vector<double> &values,int32_t index){if(index<0||size_t(index)>=values.size())throw std::out_of_range("Shaker result column/row");return values[size_t(index)];}
bool interior(const ShakerAnchor &vector,ShakerPoint value){
    if(!vector.valid)throw std::invalid_argument("Missing Shaker direction vector");
    const auto bounds=shakerSplitVector(vector.point);
    const double width=std::sqrt(vector.point[1]*vector.point[1]+vector.point[0]*vector.point[0])/144;
    const double limit=std::bit_cast<double>(std::bit_cast<uint64_t>(width)^0x8000000000000000ULL);
    return !(bounds[1]-value[1]>limit||value[1]-bounds[3]>limit||bounds[0]-value[0]>limit||value[0]-bounds[2]>limit);
}
}
ShakerStepReport shakerStepReport(const ShakerProblem &problem,const ShakerReportState &state,
    ShakerStepReport &report,const ShakerSolutionQueries &query){
    if(problem.slack49){const auto values=query.primal();if(!values.empty())report.slack=at(values,problem.slackCC);}
    if(problem.policy0)report.objective=state.objective;
    if(problem.hasActiveSpecialRow)report.special=problem.activeSpecialRow;else report.special.reset();
    if(problem.enabled58||problem.enabled59){const auto values=query.primal();if(!values.empty()){
        if(problem.enabled58)report.firstDirection=ShakerPoint{at(values,problem.auxiliaryDC[0]),at(values,problem.auxiliaryDC[1])};
        if(problem.enabled59)report.secondDirection=ShakerPoint{at(values,problem.auxiliaryDC[2]),at(values,problem.auxiliaryDC[3])};
        const auto dual=query.dual();if(!state.directionalRows[0]||!state.directionalRows[1])throw std::invalid_argument("Missing Shaker directional row identifiers");
        const bool firstDual=at(dual,*state.directionalRows[0])!=0,secondDual=at(dual,*state.directionalRows[1])!=0;
        const bool active=firstDual||secondDual;
        const bool first=problem.enabled58?interior(problem.vector60,*report.firstDirection):false;
        const bool second=problem.enabled59?interior(problem.vector78,*report.secondDirection):false;
        report.constrained=active||first||second;
    }}
    return report;
}
ShakerStepReport shakerSolveStep(const ShakerProblem &problem,ShakerReportState &state,
    std::optional<double> admission,double seconds,double &initialObjective,
    double &solvedObjective,double &maximumViolation,const ShakerStepRuntime &runtime){
    maximumViolation=0;runtime.prepareScene();runtime.resetSolver();runtime.solverOption(state.solverOption1B8);runtime.solverTolerance(1e-5);
    const auto time=[&](){return double(runtime.clockNanoseconds()/1000000)/1000;};
    const double start=time();initialObjective=runtime.variables();const auto built=runtime.buildRows(admission,seconds,maximumViolation);
    state.buildSeconds=(time()-start)+state.buildSeconds;ShakerStepReport report;
    if(built){report.status=built;return report;}
    double remaining=seconds;
    if(seconds>=0){const double difference=seconds-(time()-start);remaining=0>difference?0:difference;}
    if(runtime.columns()==0||(runtime.columns()==1&&runtime.rows()==0))state.objective=initialObjective;
    else if(remaining==0)report.status=1;
    else report.status=runtime.optimize(remaining,state.objective);
    if(problem.hasActiveSpecialRow)report.special=problem.activeSpecialRow;
    if(report.status==0){solvedObjective=state.objective;return shakerStepReport(problem,state,report,runtime.solution);}
    return report;
}
int32_t shakerBuildStep(ShakerProblem &problem,std::span<ShakerPart> parts,double broadRadius,
    std::optional<double> admission,double seconds,double &maximumViolation,const ShakerBuildRuntime &runtime){
    const auto time=[&](){return double(runtime.clockNanoseconds()/1000000)/1000;};
    const double start=time();const auto pairs=runtime.broadphase(broadRadius);
    if(problem.policy3){problem.deferred.clear();problem.specialRows.clear();}
    maximumViolation=0;for(auto &part:parts)part.rows.clear();
    for(const auto &constraint:runtime.constraints)constraint(problem.rotation);
    const double span=runtime.sceneSpan();uint32_t index=0;
    for(const auto &pair:pairs){
        if(seconds>=0&&time()-start>=seconds)return 1;
        const auto status=runtime.pairRows(pair,std::bit_cast<int32_t>(index++),admission,seconds,maximumViolation,span);
        if(status)return status;
    }
    runtime.finalize();return 0;
}
int32_t shakerCheckPlacement(std::span<const ShakerPart> parts,std::span<const ShakerCheckItem> items,
    double seconds,double tolerance,const ShakerCheckRuntime &runtime){
    runtime.prepareScene();const auto pairs=runtime.broadphase(tolerance+tolerance);
    const auto time=[&](){return double(runtime.clockNanoseconds()/1000000)/1000;};const double start=time();
    for(const auto &pair:pairs){
        if(seconds>=0&&seconds<time()-start)return 2;
        const auto &first=items[pair[0]],&second=items[pair[1]];const auto &a=parts[first.part],&b=parts[second.part];
        const ShakerPoint relative{b.position[0]-a.position[0],b.position[1]-a.position[1]};
        if(runtime.excluded(false,pair))continue;
        ShakerConvolutionKey key;key.poses[0]={first.geometry,a.angle,a.mirror,1};
        key.poses[1]={second.geometry,b.angle,{uint8_t(!b.mirror[0]),uint8_t(!b.mirror[1])},2};
        const int32_t mode=runtime.geometryModeEnabled?(runtime.hasGeometryMode?runtime.geometryMode:1):0;
        const auto boundary=runtime.convolution(key,mode);const double winding=shakerWinding(boundary,relative,tolerance);
        if(winding>std::numbers::pi||winding< -std::numbers::pi)return 0;
    }
    return 1;
}
}
