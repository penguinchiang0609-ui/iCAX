#include "ordinary_supervisor.hpp"
#include "ordinary_nomap.hpp"
#include <iostream>

int main(){using namespace study;try{
    size_t cases=0,placements=0,queries=0;
    for(const bool shifted:{false,true})for(const uint64_t preset:{1ULL,1000ULL}){
        Input input;input.width=40;input.height=30;input.pitch=.5;
        for(size_t group=0;group<10;++group){
            PartInput part;part.polygon={{0,0},{4,0},{4,1},{1,1},{1,4},{0,4}};
            part.quantity=1;part.poses={{0},{90},{180},{270}};
            if(shifted){for(auto &p:part.polygon){p.x+=7;p.y-=3;}part.pivot={8,-2};}
            input.parts.push_back(part);
        }
        SupervisorProgram program;program.initialPreset=preset;program.fastPreset=preset;program.reversePreset=0;
        program.technologyMemoryLimitD0=.001;program.usePostOptimization=false;
        program.selectionTrialLimit=1;program.optimizationTrialLimit=1;
        program.strategyFraction=1.;program.phaseFractions={.5,0,.5};program.constructionPhases={0,0,1};
        SupervisorTrace trace;Budget budget(4.);const auto output=ordinaryDefault(input,budget,program,&trace);
        require(trace.orientationPopulation>=40&&trace.noMapCandidateQueries>0&&trace.noMapCommitted>=10,
                PN2S_INTERNAL,"Actual memory eligibility must reach the original NoMap factory and commit pipeline");
        require(output.placements.size()==10&&valid_layout(input,output),PN2S_INTERNAL,
                "The real NoMap candidate/score/commit chain must place all requested material legally");
        require(trace.unsupportedManagers.empty(),PN2S_INTERNAL,"NoMap continuation unexpectedly rejected its ordinary manager");
        ++cases;placements+=output.placements.size();queries+=trace.noMapCandidateQueries;
    }
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"placements\":"<<placements<<",\"candidateQueries\":"<<queries<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
