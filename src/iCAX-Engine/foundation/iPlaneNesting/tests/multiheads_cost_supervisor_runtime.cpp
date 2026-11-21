#include "ordinary_supervisor.hpp"
#include <iomanip>
#include <iostream>
#include <set>
using namespace study;
namespace {
size_t checks=0,cases=0,placements=0,groups=0;
void check(bool value,const char *message){++checks;require(value,PN2S_INTERNAL,message);}
bool close(double a,double b){return std::abs(a-b)<=1e-9*std::max({1.,std::abs(a),std::abs(b)});}
double perimeter(const PartInput &part){
    const auto ringLength=[](const Polygon &ring){double value=0;for(size_t i=0;i<ring.size();++i){const auto a=ring[i],b=ring[(i+1)%ring.size()];value+=std::hypot(b.x-a.x,b.y-a.y);}return value;};
    double value=ringLength(part.polygon);for(const auto &hole:part.holes)value+=ringLength(hole);return value;
}
Input inputFor(bool holes=false){
    Input input;input.width=160;input.height=100;input.pitch=1;
    PartInput part;part.polygon={{0,0},{30,0},{30,20},{0,20}};part.quantity=12;part.poses={{0,false}};
    if(holes)part.holes={{{3,4},{8,4},{8,10},{3,10}},{{18,6},{20,6},{20,9},{18,9}}};
    input.parts.push_back(part);return input;
}
SupervisorProgram single(int manager){
    SupervisorProgram program;uint64_t encoded=1;for(int i=1;i<(manager>10?manager-10:manager);++i)encoded*=10;
    program.initialPreset=manager>10?0:encoded;program.reversePreset=manager>10?encoded:0;
    program.fastPreset=program.initialPreset;program.multiheads={3,-1,20,48};
    program.selectionTrialLimit=1;program.optimizationTrialLimit=0;program.strategyFraction=1.;
    program.phaseFractions={1,0,0};program.usePostOptimization=false;return program;
}
SupervisorTrace run(const Input &input,SupervisorProgram program,const char *label,bool grouped,bool requireRectangle=false){
    SupervisorTrace trace;Budget budget(8.);const auto output=ordinaryDefault(input,budget,program,&trace);
    check(output.placements.size()==12&&valid_layout(input,output),"Supervisor cost run must preserve all actual valid material placements");
    check(trace.bestCost.has_value()&&!trace.candidateCosts.empty(),"Supervisor must publish real candidate and final cost records");
    check(trace.unsupportedManagers.empty()&&trace.missingProviders.empty(),"Cost/inference run must not conceal an unsupported geometry provider");
    if(grouped!=!trace.sisterGroups.empty()){
        std::cerr<<label<<" inferenceCalls="<<trace.rectangleInferenceCalls<<" inferenceGroups="<<trace.rectangleInferredGroups<<" managers";
        for(const auto &[id,count]:trace.managerAttempts)std::cerr<<' '<<id<<':'<<count;std::cerr<<'\n';
        for(const auto &cost:trace.candidateCosts)std::cerr<<"candidate "<<cost.sourceId<<" raw="<<cost.rawLength<<" corrected="<<cost.correctedLength<<" benefit="<<cost.benefit<<" qParallel="<<cost.processQuantities[1]<<'\n';
    }
    check(grouped?!trace.sisterGroups.empty():trace.sisterGroups.empty(),"Actual sister ownership differs from recovered hardware gate");
    if(requireRectangle)check((trace.managerAttempts.contains(8)||trace.managerAttempts.contains(18))&&trace.rectangleInferenceCalls>0,
        "Supervisor must execute actual RectangleApply inference through its scheduled manager");
    std::set<std::pair<size_t,size_t>> linked,followers;
    for(const auto &group:trace.sisterGroups){check(group.size()>1,"Published sister group has a real leader and followers");
        for(size_t i=0;i<group.size();++i){check(linked.insert(group[i]).second,"Actual sister ownership is unique");if(i)followers.insert(group[i]);}}
    std::array<double,3> expected{};double allLength=0;
    for(const auto &placed:output.placements){const double length=perimeter(input.parts.at(placed.group));allLength+=length;
        expected[followers.contains({placed.group,placed.ordinal})?1:0]+=length;}
    const auto &cost=*trace.bestCost;
    check(close(cost.informer20,allLength),"Actual lifecycle informer20 must retain every head's raw outer and hole perimeter");
    for(size_t i=0;i<3;++i)check(close(cost.processQuantities[i],expected[i]),"Candidate process quantities must classify actual group58 leaders and followers");
    const double benefit=program.masterCuttingUnitCost18*allLength-
        (program.masterCuttingUnitCost18*expected[0]+program.additionalHeadCuttingUnitCost20*expected[1]);
    check(close(cost.benefit,benefit),"Real Supervisor candidate benefit must use configured cutting costs");
    check(cost.correctedLength==cost.rawLength-std::llround(((benefit/program.materialUnitCost10)/input.height)*1e6),
        "Real Supervisor corrected length must apply material/height conversion with signed round-away correction");
    for(const auto &sample:trace.candidateCosts){
        const double value=program.masterCuttingUnitCost18*sample.informer20-
            (program.masterCuttingUnitCost18*sample.processQuantities[0]+program.additionalHeadCuttingUnitCost20*sample.processQuantities[1]);
        check(close(sample.benefit,value),"Every pooled trial uses recovered process arithmetic");
        check(sample.correctedLength==sample.rawLength-std::llround(((value/program.materialUnitCost10)/input.height)*1e6),
            "Every pooled trial publishes its signed corrected cost before pool ordering");}
    ++cases;placements+=output.placements.size();groups+=trace.sisterGroups.size();
    std::cout<<std::setprecision(17)<<"{\"case\":\""<<label<<"\",\"placements\":"<<output.placements.size()
        <<",\"groups\":"<<trace.sisterGroups.size()<<",\"rectangleCalls\":"<<trace.rectangleInferenceCalls
        <<",\"rectangleGroups\":"<<trace.rectangleInferredGroups<<",\"rawLength\":"<<cost.rawLength
        <<",\"correctedLength\":"<<cost.correctedLength<<",\"informer20\":"<<cost.informer20
        <<",\"master\":"<<cost.processQuantities[0]<<",\"parallel\":"<<cost.processQuantities[1]<<",\"benefit\":"<<cost.benefit<<"}\n";
    return trace;
}
}
int main(){try{
    check(recovered::MultiheadsHardware{}.fixedDivisions50==-1,"Actual hardware default uses the recovered negative fixed-division sentinel");
    const auto input=inputFor();
    run(input,single(9),"manager9-default-cost",true);
    auto moreExpensive=single(9);moreExpensive.additionalHeadCuttingUnitCost20=24;
    const auto costly=run(input,moreExpensive,"manager9-expensive-additional-head",true);
    check(costly.bestCost->benefit<0&&costly.bestCost->correctedLength>costly.bestCost->rawLength,
        "Additional-head costs above master costs produce a signed penalty");
    auto material=single(9);material.materialUnitCost10=.1;
    const auto negative=run(input,material,"manager9-material-cost",true);
    check(negative.bestCost->correctedLength<0,"Legitimate multihead benefit must retain a negative corrected length");
    auto equal=single(9);equal.additionalHeadCuttingUnitCost20=equal.masterCuttingUnitCost18;
    const auto neutral=run(input,equal,"manager9-equal-cost",true);
    check(neutral.bestCost->benefit==0&&neutral.bestCost->rawLength==neutral.bestCost->correctedLength,
        "Equal head costs remove only the process correction");
    auto noGroup=single(8);noGroup.multiheads.maximumGap60=0;
    run(input,noGroup,"rectangle-raw-zero-max-gap",false,true);
    auto rectangle=single(8);run(input,rectangle,"rectangle-grouped",true,true);
    run(inputFor(true),rectangle,"rectangle-two-hole-perimeters",true,true);
    auto oneHead=rectangle;oneHead.multiheads.heads48=1;
    run(inputFor(true),oneHead,"rectangle-one-head-holes",false,true);
    const auto reversed=run(input,single(18),"reverse18-fresh-owner-semantics",false,true);
    check(reversed.rectangleInferredGroups>0&&reversed.bestCost->processQuantities[1]==0&&
        reversed.bestCost->correctedLength==reversed.bestCost->rawLength,
        "Original Reverse constructor clears child groups and processes rather than copying inferred ownership");
    auto reversePolish=single(18);reversePolish.strategyFraction=.5;reversePolish.usePostOptimization=true;
    const auto reversedPolished=run(input,reversePolish,"reverse18-post-optimization-fresh-owners",false,true);
    check(reversedPolished.postOptimizationTasks>0&&reversedPolished.postOptimizationHeadRows==0,
        "Original Reverse fresh owners must not fabricate simultaneous-head Shaker constraints");
    auto polish=single(9);polish.strategyFraction=.5;polish.usePostOptimization=true;
    const auto polished=run(input,polish,"manager9-post-optimization-cost",true);
    check(polished.postOptimizationTasks>0&&polished.postOptimizationHeadRows>0,
        "Real post-optimization must preserve group constraints and recalculate corrected cost");
    auto tiny=inputFor();tiny.width=4;tiny.height=3;tiny.parts[0].polygon={{0,0},{1,0},{1,1},{0,1}};
    SupervisorProgram defaults;defaults.multiheads={3,-1,1,2};defaults.selectionTrialLimit=96;
    defaults.optimizationTrialLimit=0;defaults.strategyFraction=1;defaults.phaseFractions={1,0,0};defaults.usePostOptimization=false;
    const auto inferred=run(tiny,defaults,"original-default-preset-inference",true,true);
    check(!inferred.managerAttempts.contains(9),"Original default preset must not be silently replaced by a manager9-only preset");
    defaults.multiheads.maximumGap60=0;
    run(tiny,defaults,"original-default-preset-no-inference",false,true);
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"placements\":"<<placements<<",\"sisterGroups\":"<<groups<<",\"checks\":"<<checks<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
