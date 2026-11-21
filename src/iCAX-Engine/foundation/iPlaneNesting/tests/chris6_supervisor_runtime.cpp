#include "ordinary_supervisor.hpp"
#include "ordinary_chris6.hpp"
#include <iostream>

namespace {
size_t checks=0,cases=0,placements=0,warmCases=0,reverseCases=0,nullCases=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
study::Input inputFor(bool concave,bool shifted){
    study::Input input;input.width=15;input.height=12;input.pitch=.5;
    study::PartInput first,second;
    first.polygon={{0,0},{3,0},{3,2},{0,2}};first.quantity=3;
    first.poses={{0,false},{90,false},{0,true},{90,true}};
    second.polygon=concave?study::Polygon{{0,0},{3,0},{3,1},{1,1},{1,3},{0,3}}
                          :study::Polygon{{0,0},{2,0},{2,4},{0,4}};
    second.quantity=3;second.poses={{0,false},{90,false},{180,false},{270,false}};
    if(shifted){for(auto &point:first.polygon){point.x+=6;point.y-=3;}first.pivot={7,-2};
        for(auto &point:second.polygon){point.x-=4;point.y+=5;}second.pivot={-3,7};}
    input.parts={first,second};return input;
}
study::SupervisorProgram settingsFor(int manager,bool warm,bool alternate){
    study::SupervisorProgram settings;
    settings.initialPreset=manager==6?100000:0;
    settings.reversePreset=manager==16?100000:0;
    settings.fastPreset=settings.initialPreset;
    settings.selectionTrialLimit=warm?1:2;settings.optimizationTrialLimit=warm?1:0;
    settings.constructionPhases=warm?std::array<int32_t,3>{0,1,1}:std::array<int32_t,3>{0,0,0};
    settings.usePostOptimization=false;settings.strategyFraction=1;
    settings.problemEvaluation=study::recovered::UserEvaluation{alternate?std::vector<double>{1.,1000.}:std::vector<double>{1000.,1.}};
    return settings;
}
}
int main(){using namespace study;try{
    for(const int manager:{6,16})for(const bool concave:{false,true})for(const bool shifted:{false,true}){
        auto input=inputFor(concave,shifted);auto settings=settingsFor(manager,false,shifted);
        SupervisorTrace trace;Budget budget(10.);const auto output=ordinaryDefault(input,budget,settings,&trace);
        check(output.placements.size()==6&&valid_layout(input,output),"Chris6 supervisor publishes complete valid real geometry");
        check(trace.managerAttempts.size()==1&&trace.managerAttempts.at(manager)==2&&trace.trials==2,
              "Chris6-only preset invokes the actual selected direct or reverse manager twice");
        check(trace.unsupportedManagers.empty()&&trace.missingProviders.empty(),"Supplied problem evaluator resolves the real Chris6 provider");
        check(trace.localAccepted&&trace.globalAccepted&&trace.candidateCount,"Chris6 enters both actual candidate pools");
        check(trace.managerPlacements.at(manager)>=6,"Chris6 records placement production under the selected manager identity");
        check(trace.warmSelections==0,"Phase-zero Chris6 trials do not inject warm placements");
        placements+=output.placements.size();++cases;if(manager==16)++reverseCases;
    }
    for(const int manager:{6,16})for(const bool shifted:{false,true}){
        auto input=inputFor(true,shifted);input.width=30;input.height=20;for(auto &part:input.parts)part.quantity=20;
        auto settings=settingsFor(manager,true,shifted);SupervisorTrace trace;Budget budget(15.);
        const auto output=ordinaryDefault(input,budget,settings,&trace);
        check(output.placements.size()==40&&valid_layout(input,output),"Chris6 warm supervisor completes the real forty-part layout");
        check(trace.stages.size()==3&&trace.trials==3&&trace.warmSelections==2&&trace.warmRetained>0,
              "Chris6 warm phases transfer recovered prefixes before continuing direct or reverse construction");
        check(trace.unsupportedManagers.empty()&&trace.missingProviders.empty(),"Warm Chris6 has no missing provider");
        check(trace.managerAttempts.size()==1&&trace.managerAttempts.at(manager)==3,
              "Warm selected-preset refinement preserves Chris6 manager identity");
        placements+=output.placements.size();++cases;++warmCases;if(manager==16)++reverseCases;
    }
    for(const int manager:{6,16}){
        auto input=inputFor(true,false);auto settings=settingsFor(manager,false,false);settings.problemEvaluation.reset();
        SupervisorTrace trace;Budget budget(3.);bool rejected=false;
        try{ordinaryDefault(input,budget,settings,&trace);}catch(const Failure &error){rejected=error.code==PN2S_UNSUPPORTED;}
        check(rejected,"Explicit Chris6 without original problem evaluator reports unsupported");
        check(trace.unsupportedManagers==std::set<int>{manager}&&!trace.candidateCount,
              "Missing evaluator never substitutes another manager or publishes its result as Chris6");
        ++cases;++nullCases;
    }
    {
        auto input=inputFor(false,false);auto settings=settingsFor(6,false,false);
        settings.problemEvaluation=recovered::UserEvaluation{{1.}};SupervisorTrace trace;Budget budget(3.);bool rejected=false;
        try{ordinaryDefault(input,budget,settings,&trace);}catch(const Failure &error){rejected=error.code==PN2S_INVALID;}
        check(rejected,"Mismatched supplied evaluator indices are rejected before construction");++cases;
    }
    {
        auto input=inputFor(false,false);SupervisorProgram settings;settings.initialPreset=1000;settings.reversePreset=0;
        settings.selectionTrialLimit=1;settings.optimizationTrialLimit=0;settings.usePostOptimization=false;settings.strategyFraction=1;
        SupervisorTrace trace;Budget budget(5.);const auto output=ordinaryDefault(input,budget,settings,&trace);
        check(output.placements.size()==6&&valid_layout(input,output)&&trace.unsupportedManagers.empty(),
              "Ordinary non-priced Offset manager remains available without a problem UserEvaluation");
        check(!trace.managerAttempts.contains(6)&&!trace.managerAttempts.contains(16),"No evaluator does not implicitly add Chris6 to ordinary presets");
        placements+=output.placements.size();++cases;
    }
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"warmCases\":"<<warmCases<<",\"reverseCases\":"<<reverseCases
             <<",\"nullEvaluationCases\":"<<nullCases<<",\"placements\":"<<placements<<",\"checks\":"<<checks<<"}\n";
    return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
