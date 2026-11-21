#include "ordinary_supervisor.hpp"
#include "ordinary_pair_map.hpp"
#include "clipper2/clipper.h"
#include <iostream>
using namespace study;
namespace {
double maximumMaterialOverlap=0.;
void checkMaterialOverlap(const Input &input,const Output &output) {
    using namespace Clipper2Lib;
    std::vector<PathsD> materials;
    for(const auto &placement:output.placements){const auto &part=input.parts[placement.group];
        auto pose=part.poses[placement.poseIndex];if(std::isfinite(placement.solvedAngle))pose.angle=placement.solvedAngle;
        const double x=std::isfinite(placement.exactX)?placement.exactX:double(placement.x)/1e6;
        const double y=std::isfinite(placement.exactY)?placement.exactY:double(placement.y)/1000.;
        PathsD material;const auto append=[&](const Polygon &ring,bool hole){PathD path;
            for(const auto p:transform(ring,part.pivot,pose))path.push_back({p.x+x,p.y+y});
            if((Area(path)<0.)!=hole)std::reverse(path.begin(),path.end());material.push_back(std::move(path));};
        append(part.polygon,false);for(const auto &hole:part.holes)append(hole,true);
        for(const auto &prior:materials)maximumMaterialOverlap=std::max(maximumMaterialOverlap,
            std::abs(Area(Intersect(material,prior,FillRule::NonZero,8))));
        materials.push_back(std::move(material));
    }
    require(maximumMaterialOverlap==0.,PN2S_INTERNAL,
            "Independent material clipping found a positive-area Supervisor overlap");
}
uint64_t encoded(int base) { uint64_t result=1; while (--base) result*=10; return result; }
void shift(PartInput &part) {
    for (auto &p:part.polygon) {p.x+=7.;p.y-=3.;}
    for (auto &hole:part.holes) for (auto &p:hole) {p.x+=7.;p.y-=3.;}
    part.pivot={8.,-2.};
}
Input frameOrder(bool shifted,bool reverseWinding) {
    Input input;input.width=input.height=12.;input.pitch=.5;
    PartInput frame;frame.polygon={{0,0},{12,0},{12,12},{0,12}};
    frame.holes={{{1,1},{4,1},{4,4},{1,4}},{{7,7},{10,7},{10,10},{7,10}}};
    frame.quantity=1;frame.poses={{0},{90},{180},{270},{0,true},{90,true},{180,true},{270,true}};
    PartInput small;small.polygon={{0,0},{1,0},{1,1},{0,1}};small.quantity=8;small.poses={{0}};
    if(reverseWinding)for(auto &hole:frame.holes)std::reverse(hole.begin(),hole.end());
    if(shifted){shift(frame);shift(small);}input.parts={frame,small};return input;
}
size_t insideHoles(const Input &input,const Output &output) {
    require(output.placements.size()==9&&valid_layout(input,output),PN2S_INTERNAL,
            "Actual Supervisor must retain the frame and place all eight pieces legally");
    checkMaterialOverlap(input,output);
    const auto frame=std::find_if(output.placements.begin(),output.placements.end(),[](const auto &p){return p.group==0;});
    require(frame!=output.placements.end(),PN2S_INTERNAL,"Supervisor discarded the frame to report small-piece completion");
    const auto worldBox=[&](const Polygon &ring,const Placement &p){
        const auto &part=input.parts[p.group];auto pose=part.poses[p.poseIndex];
        if(std::isfinite(p.solvedAngle))pose.angle=p.solvedAngle;
        auto box=bounds(transform(ring,part.pivot,pose));
        const double x=std::isfinite(p.exactX)?p.exactX:double(p.x)/1e6;
        const double y=std::isfinite(p.exactY)?p.exactY:double(p.y)/1000.;
        box.x0+=x;box.x1+=x;box.y0+=y;box.y1+=y;return box;
    };
    std::vector<Box> voids;for(const auto &hole:input.parts[0].holes)voids.push_back(worldBox(hole,*frame));
    size_t result=0;for(const auto &p:output.placements)if(p.group>0){
        const auto box=worldBox(input.parts[p.group].polygon,p);
        require(std::any_of(voids.begin(),voids.end(),[&](const auto &hole){return box.x0>=hole.x0-1e-9&&box.y0>=hole.y0-1e-9&&box.x1<=hole.x1+1e-9&&box.y1<=hole.y1+1e-9;}),
                PN2S_INTERNAL,"A small Supervisor placement must actually lie inside a transformed frame hole");++result;
    }return result;
}
void actualProviders(const SupervisorTrace &trace) {
    require(trace.unsupportedManagers.empty()&&trace.missingProviders.empty()&&trace.globalAccepted>0&&trace.candidateCount>0,
            PN2S_INTERNAL,"Supervisor hole acceptance requires actual strategy, provider and candidate-pool publication");
    require(trace.noMapCandidateQueries==0&&trace.noMapCommitted==0,PN2S_INTERNAL,
            "This small hole order must use the real default Context Map eligibility without a fake gate");
}
}
int main(){try{
    size_t cases=0,placements=0,holePlacements=0,trials=0,postTasks=0,tilingCases=0,noMapCases=0,noMapQueries=0;
    std::set<int> covered;
    for(const int manager:{1,2,3,4,5,11,12,13,14,15})for(const bool shifted:{false,true}){
        const auto input=frameOrder(shifted,shifted);SupervisorProgram program;
        program.initialPreset=manager<10?encoded(manager):0;program.reversePreset=manager>10?encoded(manager-10):0;
        program.fastPreset=program.initialPreset;program.selectionTrialLimit=1;program.optimizationTrialLimit=0;
        program.strategyFraction=1.;program.usePostOptimization=false;SupervisorTrace trace;Budget time(10.);
        const auto output=ordinaryDefault(input,time,program,&trace);actualProviders(trace);
        require(trace.managerAttempts.size()==1&&trace.managerAttempts.contains(manager),PN2S_INTERNAL,
                "Explicit Supervisor fixture did not execute the requested real strategy family");
        holePlacements+=insideHoles(input,output);placements+=output.placements.size();trials+=trace.trials;
        covered.insert(manager);++cases;
    }
    for(const int manager:{7,8,18})for(const bool shifted:{false,true}){
        Input input;input.width=30.;input.height=20.;input.pitch=.1;
        PartInput part;part.polygon={{0,0},{6,0},{6,2},{2,2},{2,6},{0,6}};
        part.holes={{{.5,.5},{1.5,.5},{1.5,1.5},{.5,1.5}}};part.quantity=20;
        part.poses={{0},{180},{0,true},{180,true}};
        if(shifted){shift(part);std::reverse(part.holes[0].begin(),part.holes[0].end());}input.parts={part};
        SupervisorProgram program;program.initialPreset=manager==18?0:encoded(manager);program.reversePreset=manager==18?encoded(8):0;
        program.selectionTrialLimit=1;program.optimizationTrialLimit=0;program.strategyFraction=1.;program.usePostOptimization=false;
        SupervisorTrace trace;Budget time(10.);const auto output=ordinaryDefault(input,time,program,&trace);actualProviders(trace);
        require(output.placements.size()==20&&valid_layout(input,output)&&trace.managerAttempts.contains(manager),
                PN2S_INTERNAL,"Actual Supervisor Rectangle family must consume hole-aware tiling models and publish all twenty parts");
        checkMaterialOverlap(input,output);
        placements+=output.placements.size();trials+=trace.trials;covered.insert(manager);++tilingCases;++cases;
    }
    for(const bool shifted:{false,true}){
        auto input=frameOrder(shifted,shifted);input.parts.resize(1);
        for(size_t group=0;group<8;++group){PartInput small;
            small.polygon={{0,0},{1,0},{1,.5},{.5,.5},{.5,1},{0,1}};small.quantity=1;
            small.poses={{0},{90},{180},{270}};if(shifted)shift(small);input.parts.push_back(std::move(small));
        }
        SupervisorProgram program;program.initialPreset=encoded(1);program.reversePreset=0;
        program.selectionTrialLimit=1;program.optimizationTrialLimit=0;program.strategyFraction=1.;
        program.usePostOptimization=false;program.technologyMemoryLimitD0=0.;
        SupervisorTrace trace;Budget time(10.);const auto output=ordinaryDefault(input,time,program,&trace);
        require(trace.unsupportedManagers.empty()&&trace.missingProviders.empty()&&trace.globalAccepted&&trace.candidateCount,
                PN2S_INTERNAL,"Real memory-rejected hole order needs concrete NoMap candidate publication");
        require(trace.orientationPopulation>=40&&trace.noMapCandidateQueries>0&&trace.noMapCommitted>=9,PN2S_INTERNAL,
                "Actual catalog scale and zero memory budget must reject Map and commit the real multi-ring NoMap candidates");
        // Rebuild the same scalar eligibility decision from genuine catalog
        // population, measured geometry and the actual Context pitch service.
        const auto context=ordinaryContext(input);const std::array<recovered::PitchSheet,1> sheets{{{input.width,0,0}}};
        std::vector<recovered::EligibilityPart> parts;
        for(const auto &part:input.parts){const auto box=bounds(part.polygon);recovered::EligibilityPart item;
            item.minX=box.x0;item.minY=box.y0;item.maxX=box.x1;item.maxY=box.y1;parts.push_back(item);}
        recovered::MapEligibilityInput eligibility;eligibility.parts=parts;eligibility.typeLimit188=200;
        eligibility.problemScale=trace.orientationPopulation;eligibility.memoryLimitD0=0.;
        eligibility.pitch230=recovered::contextPitch(sheets,input.pitch,context.width218,50.,false);
        recovered::MapEligibilityState state;
        require(!recovered::mapEligible(state,eligibility)&&state.status==1,PN2S_INTERNAL,
                "NoMap fixture must demonstrate original memory-estimate rejection rather than a hole-specific forced gate");
        holePlacements+=insideHoles(input,output);placements+=output.placements.size();trials+=trace.trials;
        noMapQueries+=trace.noMapCandidateQueries;++noMapCases;++cases;
    }
    // Unmodified public-default overload: actual recovered preset, real Context
    // eligibility, timed manager scheduling and post optimization all execute.
    for(const bool shifted:{false,true})for(const bool winding:{false,true}){
        const auto input=frameOrder(shifted,winding);SupervisorTrace trace;Budget time(1.2);
        const auto output=ordinaryDefault(input,time,&trace);actualProviders(trace);
        holePlacements+=insideHoles(input,output);
        require(trace.trials>1&&trace.postOptimizationTasks>0,PN2S_INTERNAL,
                "Unmodified default must execute timed selection and actual material-aware post optimization");
        for(const auto &[id,count]:trace.managerAttempts)if(count)covered.insert(id);
        placements+=output.placements.size();trials+=trace.trials;postTasks+=trace.postOptimizationTasks;++cases;
    }
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"placements\":"<<placements<<",\"insideHolePlacements\":"<<holePlacements
             <<",\"tilingCases\":"<<tilingCases<<",\"trials\":"<<trials<<",\"postOptimizationTasks\":"<<postTasks
             <<",\"noMapCases\":"<<noMapCases<<",\"noMapCandidateQueries\":"<<noMapQueries
             <<",\"maximumMaterialOverlapArea\":"<<maximumMaterialOverlap<<",\"managers\":[";
    bool first=true;for(const auto id:covered){if(!first)std::cout<<',';std::cout<<id;first=false;}std::cout<<"]}\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
