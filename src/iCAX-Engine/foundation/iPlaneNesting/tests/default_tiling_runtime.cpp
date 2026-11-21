#include "ordinary_supervisor.hpp"
#include <iomanip>
#include <iostream>

int main() {
    using namespace study;
    try {
        size_t cases=0, placements=0;
        for (const int manager : {7,8,18,0}) for (const bool shifted : {false,true}) {
            Input input; input.width=30.; input.height=20.; input.pitch=.1;
            PartInput part; part.quantity=20;
            part.polygon={{0,0},{6,0},{6,2},{2,2},{2,6},{0,6}};
            part.poses={{0,false},{180,false}};
            if(manager==18)part.poses={{0,false},{180,false},{0,true},{180,true}};
            if (shifted) {
                for (auto &point:part.polygon) {point.x+=7.125;point.y-=12.5;}
                part.pivot={9.25,-9.75};
            }
            input.parts.push_back(part);
            SupervisorProgram program;
            if (manager) {
                program.initialPreset=manager==7?1000000:manager==8?10000000:0;
                program.reversePreset=manager==18?10000000:0;
                program.selectionTrialLimit=1;program.optimizationTrialLimit=0;
                program.usePostOptimization=false;program.strategyFraction=1.;
            }
            SupervisorTrace trace;Budget budget(manager?3.:1.);
            const auto start=std::chrono::steady_clock::now();
            const auto result=ordinaryDefault(input,budget,program,&trace);
            const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            require(result.placements.size()==20&&valid_layout(input,result),PN2S_INTERNAL,
                    "Default tiling must publish twenty valid real L shapes");
            require(trace.missingProviders.empty()&&trace.unsupportedManagers.empty(),PN2S_INTERNAL,
                    "Default tiling scene must use concrete recovered providers");
            if(manager)require(trace.managerAttempts.size()==1&&trace.managerAttempts.contains(manager),
                               PN2S_INTERNAL,"Nonrectangular tiling must execute the selected Rectangle manager");
            Box box{INFINITY,INFINITY,-INFINITY,-INFINITY};
            for(const auto &placement:result.placements) {
                auto pose=part.poses.at(placement.poseIndex);
                if(std::isfinite(placement.solvedAngle))pose.angle=placement.solvedAngle;
                const auto member=bounds(transform(part.polygon,part.pivot,pose));
                const double x=std::isfinite(placement.exactX)?placement.exactX:double(placement.x)/1e6;
                const double y=std::isfinite(placement.exactY)?placement.exactY:double(placement.y)/1000.;
                box.x0=std::min(box.x0,member.x0+x);box.x1=std::max(box.x1,member.x1+x);
                box.y0=std::min(box.y0,member.y0+y);box.y1=std::max(box.y1,member.y1+y);
            }
            std::cout<<std::setprecision(17)<<"{\"manager\":"<<manager<<",\"shifted\":"<<shifted
                     <<",\"placements\":"<<result.placements.size()<<",\"wallSeconds\":"<<wall
                     <<",\"occupiedLength\":"<<box.x1<<",\"envelopeFill\":"<<400./((box.x1-box.x0)*(box.y1-box.y0))
                     <<",\"trials\":"<<trace.trials<<",\"valid\":true}\n";
            ++cases;placements+=result.placements.size();
        }
        {
            Input input;input.width=30.;input.height=20.;input.pitch=.1;
            PartInput part;part.quantity=20;part.polygon={{0,0},{6,0},{6,2},{2,2},{2,6},{0,6}};
            part.poses={{0,false},{180,false}};input.parts.push_back(part);
            SupervisorProgram program;program.initialPreset=0;program.reversePreset=10000000;
            program.selectionTrialLimit=1;program.optimizationTrialLimit=0;
            program.strategyFraction=1.;program.usePostOptimization=false;
            SupervisorTrace trace;Budget budget(3.);
            const auto result=ordinaryDefault(input,budget,program,&trace);
            require(result.placements.empty()&&valid_layout(input,result)&&trace.managerAttempts.contains(18)&&
                    trace.unsupportedManagers.empty()&&trace.missingProviders.empty(),PN2S_INTERNAL,
                    "Original reverse zero-module gate must return no placements without fabricating poses or an unsupported provider");
            std::cout<<"{\"manager\":18,\"zeroModuleGate\":true,\"placements\":0,\"valid\":true}\n";
            ++cases;
        }
        std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"placements\":"<<placements<<"}\n";
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
