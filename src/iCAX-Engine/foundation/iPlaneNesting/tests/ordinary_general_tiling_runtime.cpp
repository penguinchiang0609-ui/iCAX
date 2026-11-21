#include "ordinary_general_tiling.hpp"
#include "ordinary_rectangle_tiling.hpp"
#include "recovered_general_tiling_amo.hpp"
#include "clipper2/clipper.h"
#include <iostream>
using namespace study;
namespace {
double overlaps(const Input &input,const RectangleTilingModel &model){
    std::vector<Clipper2Lib::PathD> paths;double maximum=0.;
    for(const auto &member:model.members){const auto &part=input.parts.at(member.group);
        auto pose=part.poses.at(member.poseIndex);if(std::isfinite(member.solvedAngle))pose.angle=member.solvedAngle;
        const auto ring=transform(part.polygon,part.pivot,pose);Clipper2Lib::PathD path;
        const auto x=std::isfinite(member.exactX)?member.exactX:double(member.x)/1e6;
        const auto y=std::isfinite(member.exactY)?member.exactY:double(member.y)/1000.;
        for(const auto p:ring)path.push_back({p.x+x,p.y+y});
        for(const auto &prior:paths)maximum=std::max(maximum,std::fabs(Clipper2Lib::Area(Clipper2Lib::Intersect({path},{prior},Clipper2Lib::FillRule::NonZero,8))));
        paths.push_back(std::move(path));
    }
    return maximum;
}
}
int main(){try{size_t scenes=0,models=0,placements=0;double maximumOverlap=0.;
    for(size_t trial=0;trial<12;++trial){
        Input input;input.width=30.;input.height=20.;input.pitch=.1;
        PartInput part;part.polygon={{0,0},{6,0},{6,2},{2,2},{2,6},{0,6}};part.quantity=20;
        part.poses={{0,false},{180,false}};
        if(trial==1){for(auto &p:part.polygon){p.x+=7.125;p.y-=12.5;}part.pivot={9.25,-9.75};}
        if(trial==2){part.poses={{0,true},{180,true}};for(auto &p:part.polygon)p.x+=3.5;}
        if(trial==3||trial==9)part.poses={{0,false,360.}};
        if(trial==4)part.quantity=9;
        if(trial==5)part.poses={{90,false},{270,false}};
        if(trial==7){part.polygon={{0,0},{6,0},{6,1},{2,1},{2,5},{6,5},{6,6},{0,6}};part.poses={{0,true},{180,true}};}
        if(trial==8)input.parts.push_back({{{0,0},{4,0},{4,4},{0,4}},{},{{0,false},{90,false}},2});
        if(trial>=10)part.holes={{{.5,.5},{1.5,.5},{1.5,1.5},{.5,1.5}}};
        if(trial==11){for(auto *ring:{&part.polygon,&part.holes.front()})for(auto &p:*ring){p.x+=7.125;p.y-=12.5;}part.pivot={9.25,-9.75};}
        input.parts.push_back(part);
        Budget budget(20.);OrdinaryGeneralTilingTrace trace;
        const bool longContext=trial==6||trial==9;
        auto supplied=ordinaryGeneralTilingModelsForInput(input,budget,longContext?30.:19.,&trace);
        require(trace.candidateLimit==(longContext?8u:0u)&&trace.toroidalRefinementLevel==(longContext?1:0)&&trace.missingProviders.empty(),PN2S_INTERNAL,"Ordinary bridge lost real time-dependent provider configuration");
        if(trial==9)require(trace.toroidalCandidates>0&&trace.toroidalSolves>0,PN2S_INTERNAL,"Continuous general source must execute the actual specialized Toroidal LP provider");
        if(trial==4||trial==5){require(supplied.empty(),PN2S_INTERNAL,"Actual tiling minimum-parts/current-zero-module gate was bypassed");++scenes;continue;}
        if(trial==2){require(supplied.empty(),PN2S_INTERNAL,"Reflected-only shape fabricated a zero module");++scenes;continue;}
        require(!supplied.empty()&&trace.generalGroups==1&&trace.providerGroups==1,PN2S_INTERNAL,"Ordinary real nonrectangular source did not supply general models");
        for(const auto &model:supplied){const auto overlap=overlaps(input,model);maximumOverlap=std::max(maximumOverlap,overlap);require(overlap<=.005,PN2S_INTERNAL,"Ordinary model pivot/allowed-pose conversion introduced overlap");}
        RectangleCoreOptions options;options.modules=rectangleExpandModelLimits(supplied);
        const auto result=rectangleCore(input,budget,options);
        require(result.hasTree&&!result.output.placements.empty()&&result.output.placements.size()<=20,PN2S_INTERNAL,"Ordinary nonrectangular models were not usable by actual Rectangle core");
        RectangleTilingModel placed;placed.members=result.output.placements;const auto box=rectangleModelBounds(input,placed);
        std::cout<<"{\"scene\":"<<trial<<",\"models\":"<<supplied.size()<<",\"placements\":"<<result.output.placements.size()<<",\"complete\":"<<result.complete<<",\"validLayout\":"<<valid_layout(input,result.output)<<",\"outputOverlap\":"<<overlaps(input,placed)<<",\"box\":["<<box.x0<<','<<box.y0<<','<<box.x1<<','<<box.y1<<"],\"toroidalCandidates\":"<<trace.toroidalCandidates<<",\"toroidalSolves\":"<<trace.toroidalSolves<<",\"maximumRoundedOverlapArea\":"<<maximumOverlap<<"}\n";
        if(trial!=3&&trial!=9)require(valid_layout(input,result.output),PN2S_INTERNAL,"Native integer-coordinate ordinary layout is invalid");
        ++scenes;models+=supplied.size();placements+=result.output.placements.size();
    }
    Input expired;expired.width=30.;expired.height=20.;expired.parts.push_back({{{0,0},{6,0},{6,2},{2,2},{2,6},{0,6}},{},{{0,false},{180,false}},20});
    Budget stopped(0.);bool timedOut=false;try{ordinaryGeneralTilingModelsForInput(expired,stopped,19.);}catch(const Failure &error){timedOut=error.code==PN2S_TIMEOUT;}
    require(timedOut,PN2S_INTERNAL,"Expired ordinary model preparation ignored its shared budget");
    recovered::GeneralTilingAmoSource source;source.part=1;source.required=20;source.sheets={{{30.,20.},0,false}};
    source.orientations={{0,36000000,0}};source.originalBounds={0,0,6,6};
    source.variant=[&](recovered::PlacementOrientation pose){auto value=std::make_shared<recovered::GeneralTilingVariant>();value->orientation=pose;
        const auto matrix=recovered::reverseOrientationMatrix(pose);recovered::GeneralTilingComponent component;component.signedArea=20.;
        for(const auto p:expired.parts.front().polygon)component.points.push_back({p.x*matrix[1]-p.y*matrix[2],(p.y*matrix[1]+p.x*matrix[2])*matrix[3]});value->bounds=bounds(component.points);value->components.push_back(std::move(component));return value;};
    source.moduleOrientations=[&]{return source.orientations;};size_t cancellationChecks=0;
    source.check=[&]{if(++cancellationChecks==20)throw Failure(PN2S_TIMEOUT,"Runtime cancellation during Amo family production");};
    bool interrupted=false;try{recovered::generalTilingOrientationPatterns(source,{},{});}catch(const Failure &error){interrupted=error.code==PN2S_TIMEOUT;}
    require(interrupted&&cancellationChecks==20,PN2S_INTERNAL,"Amo family search did not honor its live host cancellation");
    std::cout<<"{\"pass\":true,\"scenes\":"<<scenes<<",\"models\":"<<models<<",\"placements\":"<<placements<<",\"maximumRoundedOverlapArea\":"<<maximumOverlap<<",\"expiredBudgetRejected\":true,\"liveAmoCancellationChecks\":"<<cancellationChecks<<"}\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
