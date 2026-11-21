#include "alma_study.h"
#include "clipper2/clipper.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
namespace clip=Clipper2Lib;
size_t checks=0,cases=0,placements=0;
void check(bool value,const char *message){++checks;if(!value){std::cerr<<PN2S_LastErrorDetail()<<'\n';throw std::runtime_error(message);}}
struct Owner{PN2_Session value=PN2_CreateSession();~Owner(){PN2_DeleteSession(value);}};
using Ring=std::vector<PN2_Point>;
Ring rectangle(double x,double y,double width,double height){return {{x,y},{x+width,y},{x+width,y+height},{x,y+height}};}
clip::PathD world(const Ring &ring,PN2_Point pivot,PN2_Point position,PN2_Orientation pose){
    const double angle=pose.min_angle*3.14159265358979323846/180.;
    const double c=std::cos(angle),s=std::sin(angle);clip::PathD result;
    for(auto p:ring){p.x-=pivot.x;p.y-=pivot.y;result.emplace_back(c*p.x-s*p.y+position.x,
        (pose.horizontal_flip?-1.:1.)*(s*p.x+c*p.y)+position.y);}return result;
}
}
int main()try{
    for(const int strategy:std::array<int,3>{-1,PN2S_DEFAULT_RECOVERED,PN2S_OFFSET_RECOVERED})
        for(int scene=0;scene<4;++scene){
            Owner owner;const auto session=owner.value;check(!PN2_IsObjectError(session),"Create public hole session");
            if(strategy>=0)check(PN2S_SetStrategy(session,strategy)==0,"Select actual recovered finite-grid strategy");
            check(PN2S_SetPitch(session,.5)==0,"Set exact physical strip pitch");
            const PN2_Point origin=scene>=2?PN2_Point{10,-20}:PN2_Point{0,0};
            const PN2_Point pivot=scene>=2?PN2_Point{12,-17}:PN2_Point{0,0};
            auto outer=rectangle(origin.x,origin.y,6,6);
            auto hole=rectangle(origin.x+.5,origin.y+1,4,3);
            if(scene&1)std::reverse(hole.begin(),hole.end());
            const auto frame=PN2_AddShape(session,outer.data(),int(outer.size()),pivot);
            check(!PN2_IsObjectError(frame)&&PN2_ShapeAddHole(session,frame,hole.data(),int(hole.size()))==0,"Create offset hole without recentering its position");
            const auto smallRing=rectangle(0,0,3,2);
            const auto small=PN2_AddShape(session,smallRing.data(),int(smallRing.size()),{1,1});
            check(!PN2_IsObjectError(small),"Create independent filler shape");
            const bool flip=scene==3;
            const std::array<PN2_Orientation,2> poses{{{int(flip),0,0},{int(flip),180,180}}};
            std::array<PN2_Part,2> parts{};
            check(PN2_AddSeveralParts(session,frame,poses.data(),int(poses.size()),1,&parts[0])==0&&
                PN2_AddSeveralParts(session,small,poses.data(),int(poses.size()),1,&parts[1])==0,"Create exact concrete frame and filler demand");
            const auto sheet=PN2_AddSheet(session,6,6);
            const auto result=PN2_Nest(session,sheet,parts.data(),int(parts.size()),.75);
            check(!PN2_IsObjectError(result),"Public default/Offset accepts finite prepared hole geometry");
            double length=0;int count=0;check(PN2_GetResultInfos(session,result,&length,&count)==0,"Read public hole result counts");
            if(count!=2){std::cerr<<"strategy="<<strategy<<" scene="<<scene<<" count="<<count<<'\n';
                for(int i=0;i<count;++i){PN2_Part part=nullptr;PN2_Point p{};PN2_Orientation pose{};PN2_GetKthNestedPart(session,result,i,&part,&p,&pose);
                    std::cerr<<(part==parts[0]?"frame":"filler")<<" @ "<<p.x<<','<<p.y<<" a="<<pose.min_angle<<" flip="<<pose.horizontal_flip<<'\n';}}
            check(count==2,"Both concrete parts must fit the single frame board");
            std::set<uintptr_t> seen;clip::PathsD frameMaterial,filler;
            for(int index=0;index<count;++index){PN2_Part part=nullptr;PN2_Point position{};PN2_Orientation pose{};
                check(PN2_GetKthNestedPart(session,result,index,&part,&position,&pose)==0,"Read actual DLL placement");
                check(seen.insert(reinterpret_cast<uintptr_t>(part)).second&&
                    std::find(parts.begin(),parts.end(),part)!=parts.end(),"Conserve unique SDK instances");
                check(pose.horizontal_flip==int(flip)&&(pose.min_angle==0||pose.min_angle==180)&&
                    pose.max_angle==pose.min_angle,"Retain allowed reflection and finite poses");
                if(part==parts[0]){auto exterior=world(outer,pivot,position,pose);auto interior=world(hole,pivot,position,pose);
                    if(clip::Area(exterior)<0)std::reverse(exterior.begin(),exterior.end());
                    if(clip::Area(interior)>0)std::reverse(interior.begin(),interior.end());frameMaterial={std::move(exterior),std::move(interior)};
                }else{auto path=world(smallRing,{1,1},position,pose);if(clip::Area(path)<0)std::reverse(path.begin(),path.end());filler={std::move(path)};}}
            const clip::PathsD board{{{0,0},{6,0},{6,6},{0,6}}};
            check(std::abs(clip::Area(clip::Difference(frameMaterial,board,clip::FillRule::NonZero,8)))<1e-8&&
                std::abs(clip::Area(clip::Difference(filler,board,clip::FillRule::NonZero,8)))<1e-8,"Independent material stays inside the board");
            check(std::abs(clip::Area(clip::Intersect(frameMaterial,filler,clip::FillRule::NonZero,8)))<1e-8,"Independent clipping finds zero material overlap inside the offset hole");
            check(std::abs(length-6)<1e-6,"Reported physical occupied length matches the frame");
            ++cases;placements+=size_t(count);
        }
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"placements\":"<<placements<<",\"checks\":"<<checks<<"}\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
