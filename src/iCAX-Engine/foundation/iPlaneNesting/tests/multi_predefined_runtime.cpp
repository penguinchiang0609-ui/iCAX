#include "ordinary_multi_predefined.hpp"
#include <iostream>
#include <set>

namespace {
size_t checks=0,cases=0,boards=0,placements=0,manualDefects=0;
void check(bool condition,const char *message){++checks;if(!condition)throw std::runtime_error(message);}
study::OrdinaryPredefinedRecord record(size_t group,size_t ordinal,double angle,bool flip,study::Point position){return {group,ordinal,{1000+group*100+ordinal,position,{int32_t(flip),angle,angle},2}};}
study::Point lowerLeft(const study::PartInput &part,study::Pose pose,double x,double y){const auto box=study::bounds(study::transform(part.polygon,part.pivot,pose));return {x-box.x0,y-box.y0};}
void ownership(study::OrdinaryMultiPredefinedScene &scene){
    std::set<uintptr_t> identities;size_t total=0;
    for(const auto &owned:scene.boards){const auto &board=*owned;check(board.lifecycle.instances.empty()&&board.lifecycle.catalog.states.empty(),"board lifecycle retains no private copy of global instances");
        check(board.nesting.placedCount13d8==int32_t(board.output.placements.size())&&board.nesting.placements.size()==board.output.placements.size(),"actual board strong placement list and original count match returned geometry");
        for(size_t n=0;n<board.output.placements.size();++n){const auto &value=board.output.placements[n];const auto &native=board.nesting.placements[n];const auto instance=size_t(native->instance18);
            check(instance<scene.instances.size()&&scene.catalog->instances[instance].placed&&scene.placementCatalog.states[instance]==1,"same concrete global catalog instance is placed in both recovered directories");
            check(scene.instances[instance].owner08==&board.nesting&&scene.instances[instance].current00.lock()==native&&native->group08==reinterpret_cast<uintptr_t>(&board.nesting),"global instance's native owner/current placement point to the actual owning board");
            check(native->geometryOwner==scene.modules[value.group][value.poseIndex].geometryOwner&&native->geometryOwner&&native->geometryOwner->size()>=3,"every board uses the one global immutable geometry module");
            check(native->x48==value.x&&native->y50==value.y&&identities.insert(scene.sdkIdentities[instance]).second,"actual shared lifecycle keeps concrete SDK IDs unique across all board poses");++total;
        }
    }
    check(total+scene.unplaced.size()==scene.sdkIdentities.size(),"global placed plus unplaced demand is conserved once across all boards");placements+=total;
}
template<class Fn>void rejects(Fn fn,int expected){try{fn();throw std::runtime_error("expected original domain rejection");}catch(const study::Failure &failure){check(failure.code==expected,"explicit global multi domain error");++cases;}}
}
int main(){using namespace study;try{
    for(const bool shifted:{false,true})for(const bool flip:{false,true})for(const int angle:{0,90,180,270}){
        Input input;input.width=30;input.height=20;input.pitch=.5;PartInput first;first.polygon={{0,0},{4,0},{4,1},{1,1},{1,3},{0,3}};first.quantity=3;
        PartInput second;second.polygon={{0,0},{3,0},{3,2},{0,2}};second.quantity=3;
        for(const bool reflected:{false,true})for(const int a:{0,90,180,270}){first.poses.emplace_back(a,reflected);second.poses.emplace_back(a,reflected);}
        if(shifted){for(auto &p:first.polygon){p.x+=7;p.y-=4;}first.pivot={8,-2};for(auto &p:second.polygon){p.x-=3;p.y+=5;}second.pivot={-1,6};}input.parts={first,second};
        std::vector<OrdinaryMultiPredefinedSheet> source(3);for(size_t n=0;n<source.size();++n){auto &sheet=source[n];sheet.sheetIdentity=2000+(n%2);sheet.width=30-double(n)*2;sheet.height=20-double(n);
            sheet.predefined.sheetOrigin={double(n)*100,-double(n)*50};const auto a=lowerLeft(first,{double(angle),flip},2,3),b=lowerLeft(second,{double(angle),flip},14,8);
            const auto origin=sheet.predefined.sheetOrigin;sheet.predefined.records={record(0,n,angle,flip,{a.x+origin.x,a.y+origin.y}),record(1,2-n,angle,flip,{b.x+origin.x,b.y+origin.y})};}
        Budget budget(10.);OrdinaryMultiPredefinedTrace trace;auto scene=ordinaryMultiPreNest(input,source,budget,&trace);
        check(trace.globalCatalogBuilds==1&&trace.globalGeometryBuilds==1&&trace.submittedGroups==1&&trace.submittedSheets==3&&trace.committed==6,"real multi manual builds global catalog/geometry once then submits all original boards");
        check(scene->boards.size()==3&&scene->unplaced.empty()&&scene->catalog->instances.size()==6,"one global exact-demand instance directory covers all supplied boards");
        for(size_t n=0;n<source.size();++n){const auto &board=*scene->boards[n];Input material=input;material.width=source[n].width;material.height=source[n].height;
            check(board.output.placements.size()==2&&valid_layout(material,board.output),"actual multi-board concave/rectangular material is valid in each board's own stock");
            check(board.source.sheetIdentity==source[n].sheetIdentity&&trace.sheets[n].committed.size()==2,"repeated stock types preserve distinct board occurrences and original order");
            for(const auto &p:board.output.placements){const auto &sdk=source[n].predefined.records[p.group].sdk;check(p.x==rounded((sdk.position.x-source[n].predefined.sheetOrigin.x)*1e6)&&p.y==rounded((sdk.position.y-source[n].predefined.sheetOrigin.y)*1000.),"global geometry keeps the requested world pose relative to each original sheet origin");}}
        ownership(*scene);boards+=source.size();++cases;
    }
    {
        Input input;input.width=10;input.height=8;input.pitch=1;input.parts={{{{0,0},{3,0},{3,2},{0,2}},{},{{0,false}},3}};
        std::vector<OrdinaryMultiPredefinedSheet> source(3);for(size_t n=0;n<3;++n){source[n].sheetIdentity=3000;source[n].width=10;source[n].height=8;source[n].predefined.records={record(0,n,0,false,{0,0})};}
        Budget budget(5.);auto scene=ordinaryMultiPreNest(input,source,budget);for(const auto &board:scene->boards)check(valid_layout(input,board->output),"three global instances may occupy identical local coordinates on three separate boards");ownership(*scene);++cases;boards+=3;
        auto repeated=source;repeated[1].predefined.records[0].sdk.partIdentity=source[0].predefined.records[0].sdk.partIdentity;
        rejects([&]{Budget time(5.);ordinaryMultiPreNest(input,repeated,time);},PN2S_UNSUPPORTED);
        repeated=source;repeated[1].predefined.records[0].ordinal=0;rejects([&]{Budget time(5.);ordinaryMultiPreNest(input,repeated,time);},PN2S_UNSUPPORTED);
        auto invalid=source;invalid[0].predefined.records[0].sdk.orientation={0,45,45};Budget rejectedTime(5.);auto partial=ordinaryMultiPreNest(input,invalid,rejectedTime);
        check(partial->boards[0]->output.placements.empty()&&partial->unplaced==std::vector<uintptr_t>{1000}&&partial->boards[1]->output.placements.size()==1&&partial->boards[2]->output.placements.size()==1,"disallowed manual pose remains unplaced globally while other boards retain their real instances");ownership(*partial);++cases;boards+=3;
        invalid=source;invalid[0].predefined.records[0].sdk.position={-4,-5};Budget defectTime(5.);auto defect=ordinaryMultiPreNest(input,invalid,defectTime);
        check(!valid_layout(input,defect->boards[0]->output)&&valid_layout(input,defect->boards[1]->output)&&defect->unplaced.empty(),"original manual out-of-sheet defect is retained only on its actual owning board");ownership(*defect);++cases;++manualDefects;boards+=3;
        invalid=source;invalid[1].predefined.records.push_back(invalid[2].predefined.records[0]);invalid[1].predefined.records.back().sdk.position={0,0};invalid[2].predefined.records.clear();Budget overlapTime(5.);auto overlap=ordinaryMultiPreNest(input,invalid,overlapTime);
        check(overlap->boards[1]->output.placements.size()==2&&!valid_layout(input,overlap->boards[1]->output)&&overlap->boards[2]->output.placements.empty(),"overlapping manual parts share one actual board while an empty supplied board remains distinct");ownership(*overlap);++cases;++manualDefects;boards+=3;
        invalid=source;invalid[0].predefined.records.clear();rejects([&]{Budget time(5.);ordinaryMultiPreNest(input,invalid,time);},PN2S_INVALID);
        input.gap=1;rejects([&]{Budget time(5.);ordinaryMultiPreNest(input,source,time);},PN2S_UNSUPPORTED);
    }
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"boards\":"<<boards<<",\"placements\":"<<placements<<",\"manualDefects\":"<<manualDefects<<",\"checks\":"<<checks<<"}\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
