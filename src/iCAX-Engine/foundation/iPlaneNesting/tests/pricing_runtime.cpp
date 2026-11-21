#include "ordinary_pricing.hpp"
#include <iostream>

namespace {
size_t checks=0,cases=0,partialCases=0,warmCases=0,placements=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
study::SupervisorProgram bounded(bool warm=false){
    study::SupervisorProgram p;p.selectionTrialLimit=warm?1:2;p.optimizationTrialLimit=warm?1:0;
    p.constructionPhases=warm?std::array<int32_t,3>{0,1,1}:std::array<int32_t,3>{0,0,0};
    return p;
}
study::Input completeInput(bool shifted,bool concave){
    study::Input input;input.width=15;input.height=12;input.pitch=.5;
    study::PartInput first,second;first.polygon={{0,0},{3,0},{3,2},{0,2}};first.quantity=3;
    first.poses={{0,false},{90,false},{0,true},{90,true}};
    second.polygon=concave?study::Polygon{{0,0},{3,0},{3,1},{1,1},{1,3},{0,3}}
        :study::Polygon{{0,0},{2,0},{2,4},{0,4}};
    second.quantity=3;second.poses={{0,false},{90,false},{180,false},{270,false}};
    if(shifted){for(auto &point:first.polygon){point.x+=6;point.y-=3;}first.pivot={7,-2};
        for(auto &point:second.polygon){point.x-=4;point.y+=5;}second.pivot={-3,7};}
    input.parts={first,second};return input;
}
}
int main(){using namespace study;using recovered::PricedSdkPart;try{
    for(const bool shifted:{false,true})for(const bool concave:{false,true}){
        auto input=completeInput(shifted,concave);std::array<PricedSdkPart,2> parts{{{0,1000},{1,1}}};
        const auto program=pricedProgram(input.parts.size(),parts,bounded());
        check(program.initialPreset==100000&&program.reversePreset==0,"real priced entry uses only direct Chris6 initially");
        check(program.problemEvaluation->values==std::vector<double>({1000.,1.}),"real priced entry supplies the normalized problem evaluator");
        check(program.engineBudget.usePostop9b0==0&&!program.usePostOptimization,"real priced entry zeroes original postop controls");
        check(program.fastPreset==1000,"priced entry retains the configuration's separate fast preset");
        SupervisorTrace trace;Budget budget(10.);const auto output=ordinaryPricedNest(input,budget,parts,bounded(),&trace);
        check(output.placements.size()==6&&valid_layout(input,output),"priced Supervisor constructs real complete geometry");
        check(trace.managerAttempts==std::map<int,size_t>{{6,2}}&&trace.trials==2,"priced Supervisor calls the actual manager6, not an alias");
        check(trace.unsupportedManagers.empty()&&trace.missingProviders.empty(),"priced Supervisor providers are present");
        check(trace.globalAccepted&&trace.localAccepted&&trace.candidateCount,"priced candidates enter both actual pools");
        check(trace.postOptimizationTasks==0,"priced entry suppresses Shaker postop");
        placements+=output.placements.size();++cases;
    }
    for(const bool shifted:{false,true})for(const bool reflected:{false,true})for(const size_t favorite:{size_t(0),size_t(1)}){
        Input input;input.width=5;input.height=4;input.pitch=.5;
        PartInput a;a.polygon={{0,0},{4,0},{4,4},{0,4}};a.quantity=1;a.poses={{0,reflected}};
        if(shifted){for(auto &q:a.polygon){q.x+=7;q.y-=3;}a.pivot={8,-1};}
        input.parts={a,a};std::array<PricedSdkPart,2> prices{{{0,favorite?1:100000},{1,favorite?100000:1}}};
        SupervisorTrace trace;Budget budget(5.);const auto output=ordinaryPricedNest(input,budget,prices,bounded(),&trace);
        check(output.placements.size()==1&&valid_layout(input,output),"priced partial sheet is geometrically valid");
        check(output.placements.front().group==favorite,"actual Chris6 supplied profit changes the selected otherwise-identical part");
        check(trace.managerAttempts==std::map<int,size_t>{{6,2}}&&trace.unsupportedManagers.empty(),"partial pricing retains manager6 and accepted pool result");
        placements+=output.placements.size();++cases;++partialCases;
    }
    for(const bool shifted:{false,true}){
        auto input=completeInput(shifted,true);input.width=30;input.height=20;for(auto &part:input.parts)part.quantity=20;
        // SDK records retain their own order even after mapping to a normalized
        // native type. The repeated final record for type zero overwrites it.
        const std::array<PricedSdkPart,3> prices{{{0,100000},{1,3},{0,2}}};
        const auto program=pricedProgram(input.parts.size(),prices,bounded(true));
        check(program.problemEvaluation->values==std::vector<double>({2.,3.}),"real priced entry preserves last duplicate normalized profit");
        SupervisorTrace trace;Budget budget(15.);const auto output=ordinaryPricedNest(input,budget,prices,bounded(true),&trace);
        check(output.placements.size()==40&&valid_layout(input,output),"priced warm Supervisor completes forty real parts");
        check(trace.warmSelections==2&&trace.warmRetained>0&&trace.trials==3,"priced warm phases retain real recovered prefixes");
        check(trace.managerAttempts==std::map<int,size_t>{{6,3}}&&trace.unsupportedManagers.empty(),"priced refinement preserves Chris6 identity");
        placements+=output.placements.size();++cases;++warmCases;
    }
    const std::array<std::vector<PricedSdkPart>,5> invalid{{{},{{0,0}},{{0,-1}},{{0,1},{1,INT32_MIN}},{{2,1}}}};
    for(const auto &parts:invalid){bool rejected=false;try{pricedProgram(2,parts);}catch(const Failure &error){rejected=error.code==PN2S_INVALID;}
        check(rejected,"priced internal entry rejects empty/nonpositive prices or out-of-range normalized identity");++cases;}
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"partialCases\":"<<partialCases<<",\"warmCases\":"<<warmCases
             <<",\"placements\":"<<placements<<",\"checks\":"<<checks<<"}\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
