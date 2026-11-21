#include "ordinary_supervisor.hpp"
#include "ordinary_predefined_validation.hpp"
#include <iostream>

int main(){using namespace study;try{
    size_t cases=0,phases=0,placements=0;
    for(const bool defective:{false,true})for(const uint64_t preset:{1ULL,100ULL}){
        Input input;input.width=30;input.height=20;input.pitch=.5;
        input.parts={{{{0,0},{3,0},{3,2},{0,2}},{},{{0,false}},6}};
        OrdinaryPredefinedProgram predefined;
        const Point first=defective?Point{-1,2}:Point{4,2};
        const Point second=defective?first:Point{12,7};
        predefined.records={{0,1,{1001,first,{0,0,0},2}},
                            {0,4,{1004,second,{0,0,0},0}}};
        Budget baselineTime(5.);const auto baseline=ordinaryPreNest(input,predefined,baselineTime);
        SupervisorProgram program;program.predefined=predefined;program.initialPreset=preset;
        program.reversePreset=0;program.fastPreset=preset;program.strategyFraction=1.;
        program.phaseFractions={.5,0,.5};program.selectionTrialLimit=2;program.optimizationTrialLimit=2;
        program.constructionPhases={0,0,1};program.usePostOptimization=false;
        SupervisorTrace trace;Budget budget(3.);const auto output=ordinaryDefault(input,budget,program,&trace);
        require(trace.predefinedPhases>=2&&trace.predefinedCommitted>=4&&trace.candidateCount>0,
                PN2S_INTERNAL,"Each real candidate reset must execute the mandatory pre-nester");
        const auto validation=validatePredefinedContinuation(input,baseline,output);
        require(validation.valid&&validation.complete&&validation.preservedOriginal==2&&output.placements.size()==6,
                PN2S_INTERNAL,"Default continuation must conserve the exact mandatory prefix and add valid actual geometry");
        for(const auto &original:baseline.placements){const auto found=std::find_if(output.placements.begin(),output.placements.end(),[&](const auto &p){return p.group==original.group&&p.ordinal==original.ordinal;});
            require(found!=output.placements.end()&&found->x==original.x&&found->y==original.y&&found->poseIndex==original.poseIndex,
                    PN2S_INTERNAL,"The default worker moved a fixed original instance");}
        require(defective?!valid_layout(input,output):valid_layout(input,output),PN2S_INTERNAL,
                "Continuation validity must distinguish grandfathered manual geometry from ordinary automatic geometry");
        ++cases;phases+=trace.predefinedPhases;placements+=output.placements.size();
    }
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"mandatoryPhases\":"<<phases<<",\"placements\":"<<placements<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
