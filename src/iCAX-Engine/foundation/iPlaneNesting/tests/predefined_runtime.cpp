#include "ordinary_predefined.hpp"
#include "ordinary_unplaced.hpp"
#include "shaker_runtime.hpp"
#include "recovered_rich_conversion.hpp"
#include <iostream>

namespace {
size_t checks=0,cases=0,continuations=0,placements=0,manualInvalidCases=0,lpCases=0,lpSolves=0;
void check(bool condition,const char *message){++checks;if(!condition)throw std::runtime_error(message);}
study::OrdinaryPredefinedRecord record(size_t group,size_t ordinal,double angle,bool flip,study::Point position,uint32_t glue=2){
    return {group,ordinal,{1000+group*100+ordinal,position,{int32_t(flip),angle,angle},glue}};
}
study::Point lowerLeft(const study::PartInput &part,study::Pose pose,double x,double y){const auto box=study::bounds(study::transform(part.polygon,part.pivot,pose));return {x-box.x0,y-box.y0};}
bool samePlacement(const study::Placement &a,const study::Placement &b){return a.group==b.group&&a.ordinal==b.ordinal&&a.poseIndex==b.poseIndex&&a.x==b.x&&a.y==b.y;}
}
int main(){using namespace study;using namespace study::recovered;try{
    for(const bool shifted:{false,true})for(const bool flip:{false,true})for(const int angle:{0,90,180,270}){
        Input input;input.width=30;input.height=20;input.pitch=.5;
        PartInput first;first.polygon={{0,0},{4,0},{4,1},{1,1},{1,3},{0,3}};first.quantity=3;
        for(const bool reflected:{false,true})for(const int a:{0,90,180,270})first.poses.emplace_back(a,reflected);
        PartInput second;second.polygon={{0,0},{2,0},{2,4},{0,4}};second.quantity=3;second.poses={{0,false},{90,false}};
        if(shifted){for(auto &p:first.polygon){p.x+=6;p.y-=3;}first.pivot={7,-2};for(auto &p:second.polygon){p.x-=4;p.y+=5;}second.pivot={-3,7};}
        input.parts={first,second};OrdinaryPredefinedProgram program;
        program.records={record(0,1,angle,flip,lowerLeft(first,Pose(angle,flip),6,3)),record(1,0,90,false,lowerLeft(second,Pose(90,false),16,8))};
        Budget manualTime(5.);OrdinaryPredefinedTrace manualTrace;const auto manual=ordinaryPreNest(input,program,manualTime,&manualTrace);
        check(manual.placements.size()==2&&valid_layout(input,manual),"manual predefined phase commits the exact requested real geometry");
        check(manualTrace.committed.size()==2&&manualTrace.xQueries==2&&manualTrace.yQueries==2,"manual controller runs X and Y constraints once per admitted instance");
        check(manual.placements[0].group==0&&manual.placements[0].ordinal==1&&manual.placements[1].group==1,"manual phase retains concrete nonzero SDK instance ordinal");
        const auto offset=offsetProgram(input);bool sawPrefix=false;
        ReverseRunner runner=[&](const Input &source,Budget &time,OrdinaryState &state){sawPrefix=state.output.placements.size()==2&&state.placementHost&&state.catalog;
            check(state.catalog->instances[1].placed&&state.placementHost->lifecycle.catalog.states[1]==1,"continuation receives the same actual placed-instance catalog state");
            return ordinaryOffsetContinue(source,offset,time,state);};
        Budget moreTime(10.);OrdinaryPredefinedTrace moreTrace;const auto completed=ordinaryNestMore(input,program,moreTime,runner,&moreTrace);
        check(sawPrefix&&completed.placements.size()==6&&valid_layout(input,completed),"actual Offset continuation fills remaining demand around mandatory placements");
        for(size_t n=0;n<manual.placements.size();++n)check(samePlacement(manual.placements[n],completed.placements[n]),"NestMore retains exact initial coordinates/angle/instance and ordering");
        std::set<std::pair<size_t,size_t>> identities;for(const auto &p:completed.placements)identities.insert({p.group,p.ordinal});check(identities.size()==6,"predefined continuation conserves all concrete instances without duplicates");
        const auto locks=ordinaryPredefinedLocks(completed,program,false);check(locks.size()==2&&locks[0].fixed&&locks[1].fixed,"default glue2 locks the initial prefix during NestMore");
        placements+=completed.placements.size();++cases;++continuations;
    }
    {
        Input input;input.width=10;input.height=8;input.pitch=1;input.parts={{{{0,0},{4,0},{4,3},{0,3}},{},{{0,false}},3}};
        OrdinaryPredefinedProgram program;program.records={record(0,0,0,false,{-10,-10}),record(0,1,0,false,{0,0}),record(0,2,0,false,{0,0})};
        Budget time(5.);const auto output=ordinaryPreNest(input,program,time);
        check(output.placements.size()==3&&!valid_layout(input,output),"actual manual pre-nesting accepts out-of-sheet and mutually overlapping pieces");
        check(output.placements[0].x==-10000000&&output.placements[0].y==-10000,"manual outside coordinates are preserved verbatim after original quantization");
        placements+=output.placements.size();++cases;++manualInvalidCases;
    }
    {
        Input input;input.width=10;input.height=8;input.pitch=1;input.parts={{{{0,0},{4,0},{4,3},{0,3}},{},{{0,false}},1}};
        OrdinaryPredefinedProgram program;program.records={record(0,0,45,false,{0,0})};Budget time(5.);const auto output=ordinaryPreNest(input,program,time);
        check(output.placements.empty(),"manual requested orientation outside original finite allowed variants is skipped by constraints");++cases;
        program.records={record(0,0,0,false,{1,0}),record(0,0,0,false,{2,0})};Budget contradictoryTime(5.);OrdinaryPredefinedTrace trace;const auto contradictory=ordinaryPreNest(input,program,contradictoryTime,&trace);
        check(contradictory.placements.empty()&&trace.conflicting>0,"contradictory mandatory coordinates for one instance are not silently replaced by the first or last record");++cases;
    }
    for(const bool matching:{false,true}){
        Input input;input.width=20;input.height=10;input.pitch=1;
        PartInput part;part.polygon={{0,0},{2,0},{2,2},{0,2}};part.quantity=1;part.poses={{0,false}};input.parts={part,part};
        PointsProximityGroup pair;pair.axis=ProximityAxis::X;pair.endpoints={ProximityEndpoint{0,0,{0,0}},ProximityEndpoint{1,0,{0,0}}};pair.tolerance=0.;input.pointGroups={pair};
        OrdinaryPredefinedProgram program;program.records={record(0,0,0,false,{0,0}),record(1,0,0,false,{matching?0.:5.,4})};Budget time(5.);const auto output=ordinaryPreNest(input,program,time);
        check(output.placements.size()==size_t(matching?2:1)&&validPointsProximity(input,output,input.pointGroups),"manual placement respects actual point-proximity constraints while allowing absent rejected endpoints");placements+=output.placements.size();++cases;
    }
    for(const uint32_t mode:{0u,1u,2u,17u})for(const bool moveDefault:{false,true}){
        Input input;input.width=25;input.height=10;input.pitch=.5;PartInput part;part.polygon={{0,0},{3,0},{3,2},{0,2}};part.quantity=1;part.poses={{0,false}};input.parts={part};
        OrdinaryPredefinedProgram program;program.records={record(0,0,0,false,{10,3},mode)};Budget time(5.);const auto manual=ordinaryPreNest(input,program,time);
        const auto locks=ordinaryPredefinedLocks(manual,program,moveDefault);const bool fixed=!predefinedMovable(mode,moveDefault);check(locks.size()==1&&locks[0].fixed==fixed,"actual glue enum and operation context select fixed or movable pre-nested part");
        ShakerInputScene scene;ConvolutionCurves curves;shakerConfigureOrdinaryRelations(scene.relations);const auto shape=shakerCreateGeometry(scene,{});ShakerPolygonInput polygon;
        for(auto p:part.polygon)polygon.outer.push_back({p.x,p.y});shakerAppendPolygonComponents(scene,shape,polygon,curves);const auto placed=shakerCreatePart(scene,shape,{10,3},0);
        shakerSetMobility(scene.parts[placed],{fixed?1:0,fixed?1:0,1},scene.parts[placed].bounds,{0,0});shakerCreateBoundaryGeometry(scene,100,100,25);check(shakerSetBounds(scene,0,25,0,10)==0,"predefined actual LP bounds");
        const auto result=compactShakerFixedScene(scene,0,2,4.,0);check(result.status==0&&result.iteration.success,"actual LP accepts fixed/movable predefined scene");
        if(fixed)check(std::abs(scene.parts[placed].position[0]-10)<1e-10&&std::abs(scene.parts[placed].position[1]-3)<1e-10,"actual Shaker preserves always/default-glued coordinates");
        else check(scene.parts[placed].position[0]<9&&scene.parts[placed].position[1]<2,"actual Shaker can compact NEVER_GLUE and optimizer-movable default parts");
        lpSolves+=result.solves;++lpCases;++cases;
    }
    check(!preNestCountValid(0)&&!preNestCountValid(-1)&&preNestCountValid(1),"PreNest rejects nonpositive counts");
    check(nestMoreCountAction(0)==PredefinedCountAction::ReturnExisting&&nestMoreCountAction(-1)==PredefinedCountAction::Invalid&&nestMoreCountAction(1)==PredefinedCountAction::Proceed,"NestMore returns original result for zero count and rejects negative count");
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"continuations\":"<<continuations<<",\"manualInvalidGeometryCases\":"<<manualInvalidCases
        <<",\"lpMobilityCases\":"<<lpCases<<",\"lpSolves\":"<<lpSolves<<",\"placements\":"<<placements<<",\"checks\":"<<checks<<"}\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
