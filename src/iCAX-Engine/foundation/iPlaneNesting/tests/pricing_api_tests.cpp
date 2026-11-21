#include "alma_study.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
size_t checks=0,cases=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
struct Owner{PN2_Session value=PN2_CreateSession();~Owner(){PN2_DeleteSession(value);}};
const std::array<PN2_Point,4> points{{{7,-3},{11,-3},{11,1},{7,1}}};
const PN2_Point pivot{8,-1};
PN2_Shape shape(PN2_Session s){auto result=PN2_AddShape(s,points.data(),int(points.size()),pivot);check(!PN2_IsObjectError(result),"Create actual priced source shape");return result;}
PN2_Part part(PN2_Session s,PN2_Shape source,PN2_Orientation pose){auto result=PN2_AddPart(s,source,&pose,1);check(!PN2_IsObjectError(result),"Create actual priced source instance");return result;}
PN2_Part single(PN2_Session s,PN2_Result result){
    check(!PN2_IsObjectError(result),"Public PricedNest returns an owned result");double length=0;int count=0;
    check(PN2_GetResultInfos(s,result,&length,&count)==0&&count==1,"Only one actual four-by-four part fits the five-by-four stock");
    PN2_Part selected;PN2_Point position;PN2_Orientation pose;
    check(PN2_GetKthNestedPart(s,result,0,&selected,&position,&pose)==0,"Query priced output instance and pose");
    const double angle=pose.min_angle*3.14159265358979323846/180.;
    for(const auto &point:points){const double x=point.x-pivot.x,y=point.y-pivot.y;
        const double px=x*std::cos(angle)-y*std::sin(angle)+position.x;
        const double py=(pose.horizontal_flip?-1.:1.)*(x*std::sin(angle)+y*std::cos(angle))+position.y;
        check(px>=-1e-7&&px<=5.+1e-7&&py>=-1e-7&&py<=4.+1e-7,"Independent priced world vertices stay inside actual stock");}
    ++cases;return selected;
}
void error(PN2_Result result,int expected){check(PN2_IsObjectError(result)&&PN2_ErrorCodeFromObject(result)==expected,"Public priced error category");++cases;}
}
int main(){try{
    for(const int strategy:{0,1,2,3})for(const bool reflected:{false,true})for(const size_t favorite:{size_t(0),size_t(1)}){
        Owner s;check(PN2S_SetStrategy(s.value,strategy)==0,"Set another research strategy before the fixed priced entry");
        const auto pose=reflected?PN2_CreateXFlippedOrientation(0):PN2_CreateOrientation(0);
        std::array<PN2_Part,2> parts{part(s.value,shape(s.value),pose),part(s.value,shape(s.value),pose)};
        auto sheet=PN2_AddSheet(s.value,4,5);std::array<int,2> prices{favorite?1:100000,favorite?100000:1};const auto original=prices;
        const auto result=PN2_PricedNest(s.value,sheet,parts.data(),prices.data(),2,.15);
        check(single(s.value,result)==parts[favorite],"Integer profit drives recovered Chris6 selection for separate SDK shapes");
        check(prices==original,"PricedNest copies the caller's ordered integer prices");
    }
    for(const bool differentPoses:{false,true})for(const bool lastHigh:{false,true}){
        Owner s;const auto sourceA=shape(s.value),sourceB=shape(s.value);
        const auto a0=part(s.value,sourceA,PN2_CreateOrientation(0));
        const auto a1=part(s.value,sourceA,PN2_CreateOrientation(differentPoses?90.:0.));
        const auto b=part(s.value,sourceB,PN2_CreateOrientation(0));
        const std::array parts{a0,b,a1};std::array<int,3> prices{lastHigh?1:100000,100,lastHigh?100000:1};
        const auto selected=single(s.value,PN2_PricedNest(s.value,PN2_AddSheet(s.value,4,5),parts.data(),prices.data(),3,.15));
        check(lastHigh?(selected==a0||selected==a1):selected==b,"Last SDK shape price overwrites across equal or distinct pose catalogs");
    }
    {
        Owner s;const auto source=shape(s.value),other=shape(s.value);const auto pose=PN2_CreateOrientation(0);
        const std::array parts{part(s.value,source,pose),part(s.value,other,pose)};std::array<int,2> prices{1,100};
        const auto sheet=PN2_AddSheet(s.value,4,5);
        const auto saved=PN2_PricedNest(s.value,sheet,parts.data(),prices.data(),2,.15);
        check(single(s.value,saved)==parts[1],"Create baseline result for atomic priced errors");
        error(PN2_PricedNest(s.value,sheet,parts.data(),nullptr,2,.15),PN2S_INVALID);
        error(PN2_PricedNest(s.value,sheet,nullptr,prices.data(),2,.15),PN2S_INVALID);
        error(PN2_PricedNest(s.value,sheet,parts.data(),prices.data(),0,.15),PN2S_INVALID);
        for(const int invalid:{0,-1,-2147483647}){auto bad=prices;bad[1]=invalid;error(PN2_PricedNest(s.value,sheet,parts.data(),bad.data(),2,.15),PN2S_INVALID);}
        const std::array repeated{parts[0],parts[0]};error(PN2_PricedNest(s.value,sheet,repeated.data(),prices.data(),2,.15),PN2S_INVALID);
        error(PN2_PricedNest(s.value,sheet,parts.data(),prices.data(),2,0.),PN2S_INVALID);
        check(PN2S_SetRectangleGap(s.value,.25)==0,"Set a supported general gap outside current priced domain");
        error(PN2_PricedNest(s.value,sheet,parts.data(),prices.data(),2,.15),PN2S_UNSUPPORTED);
        check(PN2S_SetRectangleGap(s.value,0.)==0,"Restore ordinary priced gap");
        const auto continuous=part(s.value,other,PN2_CreateRangeOrientation(13.,1.));
        error(PN2_PricedNest(s.value,sheet,&continuous,prices.data(),1,.15),PN2S_UNSUPPORTED);
        Owner foreign;const auto foreignPart=part(foreign.value,shape(foreign.value),pose);
        error(PN2_PricedNest(s.value,sheet,&foreignPart,prices.data(),1,.15),PN2S_INVALID);
        check(single(s.value,saved)==parts[1],"Rejected priced calls preserve the previous queryable result");
    }
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"checks\":"<<checks<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
