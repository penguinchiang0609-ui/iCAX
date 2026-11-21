#include "alma_study.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
size_t checks=0,cases=0,continuationCases=0,manualDefectCases=0,mobilityCases=0;
void check(bool value,const char *message){++checks;if(!value){std::cerr<<"DLL detail: "<<PN2S_LastErrorDetail()<<'\n';throw std::runtime_error(message);}}
uintptr_t id(PN2_Part part){return reinterpret_cast<uintptr_t>(part);}
struct Owner{PN2_Session session=PN2_CreateSession();Owner(){check(!PN2_IsObjectError(session),"Create actual predefined API session");}~Owner(){PN2_DeleteSession(session);}};
struct Item {PN2_Point position{};PN2_Orientation pose{};bool operator==(const Item &other)const{return position.x==other.position.x&&position.y==other.position.y&&pose.horizontal_flip==other.pose.horizontal_flip&&pose.min_angle==other.pose.min_angle&&pose.max_angle==other.pose.max_angle;}};
using Snapshot=std::map<uintptr_t,Item>;
Snapshot snapshot(PN2_Session session,PN2_Result result){
    check(!PN2_IsObjectError(result),"Public predefined/continuation returns an owned result");double length=0;int count=0;
    check(PN2_GetResultInfos(session,result,&length,&count)==0,"Query actual predefined result infos");Snapshot output;
    for(int index=0;index<count;++index){PN2_Part part=nullptr;PN2_Point point{};PN2_Orientation pose{};
        check(PN2_GetKthNestedPart(session,result,index,&part,&point,&pose)==0,"Query actual concrete instance/position/orientation");
        check(output.emplace(id(part),Item{point,pose}).second,"Public continuation never emits one SDK instance twice");}
    return output;
}
PN2_Shape shape(PN2_Session session,double width=3,double height=2){const PN2_Point points[]{{0,0},{width,0},{width,height},{0,height}};const auto result=PN2_AddShape(session,points,4,{0,0});check(!PN2_IsObjectError(result),"Create actual source shape");return result;}
std::vector<PN2_Part> parts(PN2_Session session,PN2_Shape source,int count){const auto pose=PN2_CreateOrientation(0);std::vector<PN2_Part> result(static_cast<size_t>(count));check(PN2_AddSeveralParts(session,source,&pose,1,count,result.data())==0,"Create actual SDK instances");return result;}
void error(PN2_Result result,int code){check(PN2_IsObjectError(result)&&PN2_ErrorCodeFromObject(result)==code,"Public predefined/continuation error category");++cases;}
void preserved(const Snapshot &initial,const Snapshot &output){for(const auto &[part,item]:initial){const auto found=output.find(part);check(found!=output.end()&&found->second==item,"Every fixed original SDK instance retains its exact world pose");}}
bool overlap(const Item &a,const Item &b,double width=3,double height=2){return std::min(a.position.x+width,b.position.x+width)>std::max(a.position.x,b.position.x)+1e-7&&std::min(a.position.y+height,b.position.y+height)>std::max(a.position.y,b.position.y)+1e-7;}
void newGeometry(const Snapshot &source,const Snapshot &output,double width,double height,double sheetWidth,double sheetHeight){
    for(const auto &[part,item]:output)if(!source.contains(part))check(item.position.x>=-1e-7&&item.position.y>=-1e-7&&item.position.x+width<=sheetWidth+1e-7&&item.position.y+height<=sheetHeight+1e-7,"Each added actual part stays in the original stock");
    for(auto first=output.begin();first!=output.end();++first)for(auto second=std::next(first);second!=output.end();++second)
        if(!source.contains(first->first)||!source.contains(second->first))check(!overlap(first->second,second->second,width,height),"Every pair involving added material clears original and added parts");
}
}
int main(){try{
    {
        Owner owner;const auto s=owner.session;check(PN2S_SetPitch(s,.5)==0,"Set actual ordinary precision");const auto all=parts(s,shape(s),6);const auto sheet=PN2_AddSheet(s,20,30);
        const std::array seed{all[3],all[0]};const std::array orientations{PN2_CreateOrientation(0),PN2_CreateOrientation(0)};const std::array<PN2_Point,2> positions{{{4,2},{12,7}}};
        const auto originalParts=seed;const auto originalPoses=orientations;const auto originalPoints=positions;
        const auto manual=PN2_PreNest(s,sheet,seed.data(),orientations.data(),positions.data(),2);const auto source=snapshot(s,manual);
        check(source.size()==2&&source.at(id(seed[0])).position.x==4&&source.at(id(seed[0])).position.y==2&&source.at(id(seed[1])).position.x==12,"PreNest places exactly the supplied concrete instances");
        check(seed==originalParts&&positions[0].x==originalPoints[0].x&&positions[0].y==originalPoints[0].y&&positions[1].x==originalPoints[1].x&&positions[1].y==originalPoints[1].y&&orientations[0].min_angle==originalPoses[0].min_angle&&orientations[1].min_angle==originalPoses[1].min_angle,"PreNest copies caller arrays");
        const std::array added{all[1],all[2],all[4],all[5]};const auto more=PN2_NestMore(s,manual,added.data(),int(added.size()),.3);const auto output=snapshot(s,more);
        check(output.size()==6,"NestMore completes all remaining requested instances");for(const auto part:all)check(output.contains(id(part)),"NestMore conserves complete original/new SDK identity set");
        preserved(source,output);newGeometry(source,output,3,2,30,20);check(snapshot(s,manual)==source,"NestMore does not mutate its source result");++cases;++continuationCases;
        check(PN2_NestMore(s,manual,nullptr,0,std::numeric_limits<double>::quiet_NaN())==manual,"Zero-count NestMore returns exactly the original result handle before time validation");++cases;
        error(PN2_NestMore(s,manual,added.data(),-1,.2),PN2S_INVALID);error(PN2_NestMore(s,manual,nullptr,1,.2),PN2S_INVALID);
        error(PN2_NestMore(s,manual,added.data(),1,0.),PN2S_INVALID);error(PN2_NestMore(s,manual,added.data(),1,std::numeric_limits<double>::quiet_NaN()),PN2S_INVALID);
        error(PN2_PreNest(s,sheet,nullptr,orientations.data(),positions.data(),1),PN2S_INVALID);
        error(PN2_PreNest(s,sheet,seed.data(),nullptr,positions.data(),1),PN2S_INVALID);error(PN2_PreNest(s,sheet,seed.data(),orientations.data(),nullptr,1),PN2S_INVALID);
        error(PN2_PreNest(s,sheet,seed.data(),orientations.data(),positions.data(),0),PN2S_INVALID);error(PN2_PreNest(s,sheet,seed.data(),orientations.data(),positions.data(),-1),PN2S_INVALID);
        const std::array duplicateManual{seed[0],seed[0]};
        error(PN2_PreNest(s,sheet,duplicateManual.data(),orientations.data(),positions.data(),2),PN2S_UNSUPPORTED);
        error(PN2_NestMore(s,manual,seed.data(),1,.2),PN2S_UNSUPPORTED);
        const std::array repeatedNew{added[0],added[0]};error(PN2_NestMore(s,manual,repeatedNew.data(),2,.2),PN2S_UNSUPPORTED);
        const std::array repeatedManual{seed[0],seed[0]};const std::array<PN2_Point,2> samePositions{{{4,2},{4,2}}};
        error(PN2_PreNest(s,sheet,repeatedManual.data(),orientations.data(),samePositions.data(),2),PN2S_UNSUPPORTED);
        Owner foreign;const auto alien=parts(foreign.session,shape(foreign.session),1);error(PN2_NestMore(s,manual,alien.data(),1,.2),PN2S_INVALID);
        error(PN2_PreNest(s,sheet,alien.data(),orientations.data(),positions.data(),1),PN2S_INVALID);
        check(PN2_SetPreNestedGlueMode(s,alien[0],ALWAYS_GLUE)==PN2S_INVALID&&PN2_SetPreNestedGlueMode(nullptr,seed[0],NEVER_GLUE)==PN2S_INVALID,"Glue setter validates actual session/part ownership");
        check(snapshot(s,manual)==source&&snapshot(s,more)==output,"Rejected calls preserve both previously published results atomically");
    }
    for(const bool reflected:{false,true}){
        Owner owner;const auto s=owner.session;const std::array<PN2_Point,4> points{{{7,-3},{11,-3},{11,0},{7,0}}};const PN2_Point pivot{8,-1};
        const auto sourceShape=PN2_AddShape(s,points.data(),int(points.size()),pivot);check(!PN2_IsObjectError(sourceShape),"Create shifted public predefined shape and pivot");
        const std::array poses{PN2_CreateOrientation(0),PN2_CreateOrientation(90),PN2_CreateXFlippedOrientation(90)};
        const auto first=PN2_AddPart(s,sourceShape,poses.data(),int(poses.size())),second=PN2_AddPart(s,sourceShape,poses.data(),int(poses.size()));
        check(!PN2_IsObjectError(first)&&!PN2_IsObjectError(second),"Create finite multi-pose public instances");
        const auto requested=reflected?poses[2]:poses[1];const PN2_Point position{8,5};const auto sheet=PN2_AddSheet(s,25,30);
        const auto manual=PN2_PreNest(s,sheet,&first,&requested,&position,1);const auto source=snapshot(s,manual);
        check(source.size()==1&&source.at(id(first)).position.x==8&&source.at(id(first)).position.y==5&&source.at(id(first)).pose.min_angle==90&&bool(source.at(id(first)).pose.horizontal_flip)==reflected,"Manual source retains requested quarter-turn/reflection with a shifted pivot");
        const auto more=PN2_NestMore(s,manual,&second,1,.3);const auto output=snapshot(s,more);check(output.size()==2,"Shifted multi-pose continuation completes actual instances");preserved(source,output);
        for(const auto &[part,item]:output){const double radians=item.pose.min_angle*3.14159265358979323846/180.;for(const auto point:points){const double x=point.x-pivot.x,y=point.y-pivot.y;
            const double wx=x*std::cos(radians)-y*std::sin(radians)+item.position.x,wy=(item.pose.horizontal_flip?-1.:1.)*(x*std::sin(radians)+y*std::cos(radians))+item.position.y;
            check(wx>=-1e-7&&wx<=30.+1e-7&&wy>=-1e-7&&wy<=25.+1e-7,"Independent transformed public vertices stay in actual stock");}}
        ++cases;++continuationCases;
    }
    {
        Owner owner;const auto s=owner.session;const auto all=parts(s,shape(s),3);const auto sheet=PN2_AddSheet(s,12,20);
        const auto initial=PN2_Nest(s,sheet,all.data(),1,.2);const auto source=snapshot(s,initial);const auto result=PN2_NestMore(s,initial,all.data()+1,2,.3);const auto output=snapshot(s,result);
        check(source.size()==1&&output.size()==3,"NestMore also accepts a prior automatic Nest result");preserved(source,output);newGeometry(source,output,3,2,20,12);check(snapshot(s,initial)==source,"Automatic source result remains immutable");++cases;++continuationCases;
    }
    for(const bool remote:{false,true}){
        Owner owner;const auto s=owner.session;check(PN2S_SetPitch(s,.5)==0,"Manual defect scene pitch");const auto all=parts(s,shape(s),4);const auto sheet=PN2_AddSheet(s,12,20);
        const std::array poses{PN2_CreateOrientation(0),PN2_CreateOrientation(0)};const PN2_Point point=remote?PN2_Point{-20,-10}:PN2_Point{-1,2};const std::array<PN2_Point,2> positions{point,point};
        const auto manual=PN2_PreNest(s,sheet,all.data(),poses.data(),positions.data(),2);const auto source=snapshot(s,manual);
        check(source.size()==2&&source.at(id(all[0])).position.x==point.x&&source.at(id(all[1])).position.y==point.y,"PreNest retains overlapping and out-of-sheet manual positions");
        const auto more=PN2_NestMore(s,manual,all.data()+2,2,.3);const auto output=snapshot(s,more);check(output.size()==4,"NestMore appends actual pieces despite grandfathered manual defects");
        preserved(source,output);newGeometry(source,output,3,2,20,12);check(snapshot(s,manual)==source,"Manual defect source remains unchanged after continuation");++cases;++continuationCases;++manualDefectCases;
    }
    for(const auto mode:{ALWAYS_GLUE,NEVER_GLUE,GLUE_ON_NEST_MORE_ONLY,static_cast<PN2_PRE_NESTED_GLUE_MODE>(uint32_t(0xffffffffu))}){
        Owner owner;const auto s=owner.session;const auto all=parts(s,shape(s),2);const auto sheet=PN2_AddSheet(s,10,25);const auto pose=PN2_CreateOrientation(0);const PN2_Point position{10,3};
        check(PN2_SetPreNestedGlueMode(s,all[0],mode)==0,"Original glue setter accepts all uint32 bit patterns without enum-range validation");
        const auto manual=PN2_PreNest(s,sheet,all.data(),&pose,&position,1);const auto source=snapshot(s,manual);
        const auto compacted=PN2_Compact(s,manual,0,.3);const auto optimized=snapshot(s,compacted);const bool canMove=mode==NEVER_GLUE||mode==GLUE_ON_NEST_MORE_ONLY;
        if(canMove)check(optimized.at(id(all[0])).position.x<9.,"Actual Compact moves NEVER_GLUE and optimizer-default pre-nested parts");
        else check(optimized.at(id(all[0]))==source.at(id(all[0])),"Actual Compact keeps always-glued and unknown-mode parts fixed");
        check(snapshot(s,manual)==source,"Compact publishes another result without changing the manual source");
        const auto more=PN2_NestMore(s,manual,all.data()+1,1,.3);const auto continuation=snapshot(s,more);check(continuation.size()==2,"Glue-mode continuation retains the original and adds a real new instance");
        if(mode!=NEVER_GLUE)preserved(source,continuation);
        check(snapshot(s,manual)==source,"Glue-mode continuation preserves the source result atomically");++cases;++continuationCases;++mobilityCases;
    }
    {
        Owner owner;const auto s=owner.session;const auto all=parts(s,shape(s),1);const auto sheet=PN2_AddSheet(s,10,25);const auto pose=PN2_CreateOrientation(0);const PN2_Point position{10,3};
        check(PN2_SetPreNestedGlueMode(s,all[0],ALWAYS_GLUE)==0,"Set initial immutable glue");const auto manual=PN2_PreNest(s,sheet,all.data(),&pose,&position,1);const auto source=snapshot(s,manual);
        check(PN2_SetPreNestedGlueMode(s,all[0],NEVER_GLUE)==0,"Change the current SDK part mode after the result exists");
        const auto compacted=PN2_Compact(s,manual,0,.3);check(snapshot(s,compacted).at(id(all[0])).position.x<9.,"Optimization reads current SDK part glue rather than a stale mode frozen in the old result");
        check(snapshot(s,manual)==source,"Changing mobility does not mutate a previous result");++cases;++mobilityCases;
    }
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"continuationCases\":"<<continuationCases<<",\"manualDefectCases\":"<<manualDefectCases
        <<",\"mobilityCases\":"<<mobilityCases<<",\"checks\":"<<checks<<"}\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
