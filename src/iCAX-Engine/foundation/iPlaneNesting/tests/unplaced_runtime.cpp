#include "ordinary_unplaced.hpp"
#include "ordinary_supervisor.hpp"
#include <iostream>
namespace {
size_t checks=0,cases=0,placements=0,probes=0,blocked=0;
void check(bool condition,const char *message){++checks;if(!condition)throw std::runtime_error(message);}
study::Input inputFor(bool x,bool shifted=false){
    study::Input input;input.width=x?6:5;input.height=x?5:6;input.pitch=.25;
    study::PartInput first,second;first.quantity=second.quantity=1;first.poses=second.poses={{0,false}};
    first.polygon=x?study::Polygon{{0,0},{2,0},{2,3},{0,3}}:study::Polygon{{0,0},{3,0},{3,2},{0,2}};
    second.polygon=x?study::Polygon{{0,0},{1,0},{1,3},{0,3}}:study::Polygon{{0,0},{3,0},{3,1},{0,1}};
    if(shifted)for(auto *part:{&first,&second}){for(auto &point:part->polygon){point.x+=5;point.y-=4;}part->pivot={5,-4};}
    input.parts={first,second};return input;
}
study::PointsProximityGroup groupFor(bool x,bool shifted=false){
    study::PointsProximityGroup group;group.axis=x?study::ProximityAxis::X:study::ProximityAxis::Y;
    group.endpoints={study::ProximityEndpoint{0,0,x?study::Point{2,1.5}:study::Point{1.5,2}},
                     study::ProximityEndpoint{1,0,x?study::Point{0,1.5}:study::Point{1.5,0}}};
    if(shifted)for(auto &endpoint:group.endpoints){endpoint.point.x+=5;endpoint.point.y-=4;}
    return group;
}
void checkRelation(const study::Input &input,const study::Output &output,const study::PointsProximityGroup &group){
    std::array<study::Point,2> positions;
    for(size_t side=0;side<2;++side){const auto &endpoint=group.endpoints[side];const auto found=std::find_if(output.placements.begin(),output.placements.end(),[&](const auto &p){return p.group==endpoint.group&&p.ordinal==endpoint.ordinal;});check(found!=output.placements.end(),"Both concrete watched modules are placed");const auto &part=input.parts[endpoint.group];auto point=study::transform(study::Polygon{endpoint.point},part.pivot,part.poses.at(found->poseIndex)).front();point.x+=double(found->x)/1e6;point.y+=double(found->y)/1000.;positions[side]=point;}
    const double delta=group.axis==study::ProximityAxis::X?positions[0].x-positions[1].x:positions[0].y-positions[1].y;
    check(std::abs(delta)<=group.tolerance+1e-8,"Actual output obeys the original axial point-proximity interval");
    check(study::validPointsProximity(input,output,std::span<const study::PointsProximityGroup>(&group,1)),"Reusable original-band validator accepts constrained output");
}
}
int main(){using namespace study;try{
    for(const bool x:{false,true})for(const bool shifted:{false,true}){
        auto input=inputFor(x,shifted);const std::array groups{groupFor(x,shifted)};auto program=unplacedProgram(input,groups);Budget budget(20);UnplacedTrace trace;const auto output=ordinaryUnplaced(input,program,budget,&trace);
        check(output.placements.size()==2&&valid_layout(input,output),"Real Unplaced Offset completes constrained geometry");checkRelation(input,output,groups[0]);
        check(trace.evaluations&&trace.moduleProbes&&trace.blocked&&trace.restores==trace.evaluations,"Real candidate penalty probes future constrained modules and always restores the prefix");
        check(trace.committed==2,"Temporary unplaced probes do not publish extra placements");placements+=output.placements.size();probes+=trace.moduleProbes;blocked+=trace.blocked;++cases;
        ReverseTrace reverse;const auto reversed=ordinaryReverseUnplaced(input,groups,budget,{},nullptr,&reverse);check(reversed.placements.size()==2&&valid_layout(input,reversed),"Unplaced reverse20 continues real reverse geometry");checkRelation(input,reversed,groups[0]);check(reverse.factories==1&&reverse.trials==1,"Reverse20 owns one actual Unplaced child");placements+=reversed.placements.size();++cases;
    }
    {
        auto input=inputFor(false);const std::array groups{groupFor(false)};const auto program=unplacedProgram(input,groups);Budget budget(20);OrdinaryState state;OrdinaryControl initial;initial.values={6.,3.};initial.placementLimit=0;ordinaryRun(input,budget,state,&initial);ordinaryCommit(state,{0,0,0,0,0});const auto prefix=state.output.placements.front();UnplacedTrace trace;const int added=ordinaryUnplacedContinue(input,program,budget,state,&trace);
        check(added==1&&state.output.placements.size()==2&&valid_layout(input,state.output),"Unplaced Offset warm continuation fills only the remaining watched module");checkRelation(input,state.output,groups[0]);check(state.output.placements.front().group==prefix.group&&state.output.placements.front().x==prefix.x&&state.output.placements.front().y==prefix.y,"Unplaced temporary snapshot restoration preserves transferred warm prefix");placements+=state.output.placements.size();++cases;
    }
    {
        Input input;input.width=7;input.height=4;input.pitch=.25;PartInput part;part.polygon={{0,0},{3,0},{3,1},{0,1}};part.poses={{0,false}};part.quantity=2;input.parts={part};PointsProximityGroup group;group.axis=ProximityAxis::Y;group.endpoints={ProximityEndpoint{0,0,{1,0}},ProximityEndpoint{0,1,{1,0}}};const std::array groups{group};Budget budget(20);UnplacedTrace trace;const auto output=ordinaryUnplaced(input,unplacedProgram(input,groups),budget,&trace);check(output.placements.size()==2&&valid_layout(input,output),"Two watched modules can be distinct ordinals of the same part type");checkRelation(input,output,group);check(output.placements[0].ordinal!=output.placements[1].ordinal,"Unplaced monitor retains concrete instance identity");placements+=output.placements.size();++cases;
    }
    {
        Input input;input.width=3;input.height=3;input.pitch=.25;PartInput part;part.polygon={{0,0},{3,0},{3,1},{0,1}};part.poses={{0,false}};part.quantity=3;input.parts={part};PointsProximityGroup group;group.axis=ProximityAxis::Y;group.endpoints={ProximityEndpoint{0,0,{1,0}},ProximityEndpoint{0,2,{1,0}}};group.watched={false,true};input.pointGroups={group};Budget budget(10);OrdinaryState state;OrdinaryControl control;control.values={3.};control.placementLimit=0;ordinaryRun(input,budget,state,&control);ordinaryCommit(state,{0,0,0,0,0});
        ReferenceProbe representative;representative.wantFirstPositions=true;representative.firstPositionOrdinals[0]=1;ordinaryRun(input,budget,state,&control,&representative);check(representative.firstRightStrip[0].has_value(),"Unwatched ordinal one has free geometry in this group");ReferenceProbe watched;watched.wantFirstPositions=true;watched.firstPositionOrdinals[0]=2;ordinaryRun(input,budget,state,&control,&watched);check(!watched.firstRightStrip[0],"Future probe uses watched ordinal two once, without borrowing ordinal one's constraint region");check(state.output.placements.size()==1,"Concrete ordinal fit queries never commit a probe result");++cases;
    }
    {
        auto input=inputFor(false);input.pointGroups={groupFor(false)};Budget budget(10);const auto output=ordinaryUnplaced(input,unplacedProgram(input,input.pointGroups),budget);auto wrong=output;for(auto &p:wrong.placements)if(p.group==1){p.y+=1000;p.exactY=NAN;}check(valid_layout(input,wrong),"Validator negative sample remains geometrically nonoverlapping");check(!validPointsProximity(input,wrong,input.pointGroups),"Shared validator rejects real coordinate-valid layout violating a point pair");++cases;
    }
    {
        Input input;input.width=10;input.height=4;input.pitch=.25;PartInput part;part.polygon={{0,0},{2,0},{2,2},{0,2}};part.poses={{0,false}};part.quantity=10;input.parts={part};PointsProximityGroup group;group.axis=ProximityAxis::Y;group.endpoints={ProximityEndpoint{0,0,{1,0}},ProximityEndpoint{0,1,{1,0}}};const std::array groups{group};std::optional<UnplacedProgram> chosen;for(uint32_t seed=1;seed<=100;++seed){input.randomSeed=seed;auto program=unplacedProgram(input,groups);if(program.child.candidater.rightToLeft&&program.child.evaluation.reference==recovered::ReferenceKind::Normal){chosen=std::move(program);break;}}check(chosen.has_value()&&chosen->child.evaluationReferenceScale==1.,"Actual dense RTL Unplaced plan retains unscaled fallback Normal reference");Budget budget(20);UnplacedTrace trace;const auto output=ordinaryUnplaced(input,*chosen,budget,&trace);check(output.placements.size()==10&&valid_layout(input,output),"Actual dense RTL Unplaced layout fills the sheet with the correct fallback reference");checkRelation(input,output,group);placements+=output.placements.size();++cases;
    }
    for(const int manager:{10,20})for(const bool x:{false,true}){
        auto input=inputFor(x);input.pointGroups={groupFor(x)};SupervisorProgram settings;settings.initialPreset=manager==10?1000000000ULL:0;settings.reversePreset=manager==20?1000000000ULL:0;settings.fastPreset=settings.initialPreset;settings.selectionTrialLimit=2;settings.optimizationTrialLimit=0;settings.usePostOptimization=false;settings.strategyFraction=1;SupervisorTrace trace;Budget budget(15);const auto output=ordinaryDefault(input,budget,settings,&trace);check(output.placements.size()==2&&valid_layout(input,output),"Actual default Supervisor dispatches constrained Unplaced manager10 or20");checkRelation(input,output,input.pointGroups.front());check(trace.managerAttempts.size()==1&&trace.managerAttempts.at(manager)==2&&trace.unsupportedManagers.empty()&&trace.missingProviders.empty(),"Default Unplaced preset invokes the concrete strategy without missing providers");placements+=output.placements.size();++cases;
    }
    for(const bool concave:{false,true}){
        auto input=inputFor(false);if(concave)input.parts[0].polygon={{0,0},{3,0},{3,1},{1,1},{1,2},{0,2}};
        input.width=15;input.height=12;for(auto &part:input.parts){part.quantity=3;part.poses={{0,false},{90,false}};}
        OrdinaryControl original;for(const auto &part:input.parts)original.values.push_back(area(part.polygon));auto neutral=original;neutral.filterModuleRows=[](size_t,size_t,std::optional<size_t>,Rows &,int64_t,int64_t,int64_t){};neutral.evaluatePlacement=[](const Placement &,double score){return score;};Budget budget(10);OrdinaryState a,b;ordinaryRun(input,budget,a,&original);ordinaryRun(input,budget,b,&neutral);check(a.output.placements.size()==6&&b.output.placements.size()==6&&valid_layout(input,a.output)&&valid_layout(input,b.output),"No callback and neutral callback both preserve complete legacy ordinary layouts");for(size_t n=0;n<6;++n){const auto &x=a.output.placements[n],&y=b.output.placements[n];check(std::tie(x.group,x.ordinal,x.poseIndex,x.x,x.y)==std::tie(y.group,y.ordinal,y.poseIndex,y.x,y.y),"Empty specialized interfaces preserve every legacy candidate selection and placement coordinate");}
        SupervisorProgram defaults;defaults.initialPreset=1000;defaults.reversePreset=0;defaults.selectionTrialLimit=1;defaults.optimizationTrialLimit=0;defaults.usePostOptimization=false;defaults.strategyFraction=1;SupervisorTrace trace;const auto output=ordinaryDefault(input,budget,defaults,&trace);check(output.placements.size()==6&&valid_layout(input,output)&&trace.unsupportedManagers.empty(),"Actual ordinary default entry remains valid with all specialized callbacks absent");placements+=output.placements.size();++cases;
    }
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"placements\":"<<placements<<",\"futureModuleProbes\":"<<probes<<",\"blockedProbes\":"<<blocked<<",\"checks\":"<<checks<<"}\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
