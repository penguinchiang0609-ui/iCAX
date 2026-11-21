#include "ordinary_supervisor.hpp"
#include <iostream>

int main(){using namespace study;try{
    Input input;input.width=160;input.height=100;input.pitch=1;
    input.parts={{{{0,0},{30,0},{30,20},{0,20}},{},{{0}},12}};
    SupervisorProgram program;program.initialPreset=100000000;program.fastPreset=100000000;program.reversePreset=0;
    program.multiheads={3,0,22,48};program.strategyFraction=.5;program.phaseFractions={1,0,0};
    program.selectionTrialLimit=1;program.optimizationTrialLimit=0;program.constructionPhases={0,0,0};
    SupervisorTrace trace;Budget budget(3.);const auto output=ordinaryDefault(input,budget,program,&trace);
    require(trace.managerAttempts.contains(9)&&trace.unsupportedManagers.empty()&&trace.candidateCount>0,
            PN2S_INTERNAL,"Real default manager9 must execute both specialized children through its concrete factory");
    require(output.placements.size()==12&&valid_layout(input,output)&&!trace.sisterGroups.empty(),
            PN2S_INTERNAL,"The supervisor discarded simultaneous-head ownership or valid material");
    require(trace.postOptimizationHeadRows>0,PN2S_INTERNAL,"Actual supervisor Shaker did not receive the sister X/Y hard rows");
    std::map<std::pair<size_t,size_t>,Placement> placed;for(const auto &p:output.placements)placed[{p.group,p.ordinal}]=p;
    size_t members=0;for(const auto &group:trace.sisterGroups){const auto &first=placed.at(group.front());
        for(size_t index=0;index<group.size();++index){const auto &p=placed.at(group[index]);
            require(p.x==first.x,PN2S_INTERNAL,"Default Shaker independently shifted a sister along X");
            if(index){const auto &previous=placed.at(group[index-1]);const double pitch=std::abs(double(p.y-previous.y)/1000.);
                require(pitch>=22.-.001&&pitch<=48.+.001,PN2S_INTERNAL,"Default Shaker broke physical inter-head spacing");}++members;}}
    for(const int heads:{1,3}){
        SupervisorProgram automatic;automatic.initialPreset=1000;automatic.fastPreset=1000;automatic.reversePreset=0;
        automatic.multiheads={heads,0,22,48};automatic.usePostOptimization=false;
        automatic.selectionTrialLimit=11;automatic.optimizationTrialLimit=1;automatic.selectionPoolGrowth=0;
        automatic.strategyFraction=1.;automatic.phaseFractions={.4,.3,.3};
        SupervisorTrace phases;Budget time(2.);const auto layout=ordinaryDefault(input,time,automatic,&phases);
        require(phases.stages.size()==3&&phases.stages[1].phase==(heads<2?1:0)&&phases.stages[2].phase==(heads<2?1:0),
                PN2S_INTERNAL,"Original automatic partial-warm gate must remain disabled for multiple cutting heads");
        require(layout.placements.size()==12&&valid_layout(input,layout)&&
                !phases.missingProviders.contains("warm.multiheadsPartialGroupSelection"),PN2S_INTERNAL,
                "Default multihead stages must preserve valid layout without entering unsupported partial group replay");
        if(heads>=2)require(phases.warmSelections==0,PN2S_INTERNAL,"Default multihead dispatch entered partial warm replay");
    }
    std::cout<<"{\"pass\":true,\"automaticHeadGateCases\":2,\"placements\":"<<output.placements.size()<<",\"groups\":"<<trace.sisterGroups.size()
        <<",\"linkedMembers\":"<<members<<",\"shakerHardRows\":"<<trace.postOptimizationHeadRows<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
