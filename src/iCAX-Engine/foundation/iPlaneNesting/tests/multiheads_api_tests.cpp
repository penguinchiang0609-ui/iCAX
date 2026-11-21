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

extern "C" {
PN2S_API PN2_ErrorCode PN2S_CALL PN2_SetCuttingHeadsNumber(PN2_Session,int);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_SetCuttingHeadsMinimumGap(PN2_Session,double);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_SetCuttingHeadsMaximumGap(PN2_Session,double);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_SetAdditionnalHeadCuttingUnitCost(PN2_Session,double);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_GetPartMultipleHeadsNumber(PN2_Session,PN2_Result,PN2_Part,int *);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_GetPartMultipleHeadsLinkedPart(PN2_Session,PN2_Result,PN2_Part,int,PN2_Part *);
}

namespace {
size_t checks=0,cases=0;
size_t actualGroups=0,linkedParts=0;
void check(bool ok,const char *message){++checks;if(!ok){std::cerr<<"DLL detail: "<<PN2S_LastErrorDetail()<<'\n';throw std::runtime_error(message);}}
struct Owner{PN2_Session value=PN2_CreateSession();Owner(){check(!PN2_IsObjectError(value),"Create actual multi-head API session");}~Owner(){PN2_DeleteSession(value);}};
PN2_Shape rectangle(PN2_Session s){const PN2_Point points[]{{0,0},{3,0},{3,2},{0,2}};const auto shape=PN2_AddShape(s,points,4,{0,0});check(!PN2_IsObjectError(shape),"Create multi-head API material");return shape;}
std::vector<PN2_Part> parts(PN2_Session s,int amount){const auto shape=rectangle(s);const auto pose=PN2_CreateOrientation(0);std::vector<PN2_Part> result(size_t(amount),nullptr);check(PN2_AddSeveralParts(s,shape,&pose,1,amount,result.data())==0,"Create actual unique multi-head SDK IDs");return result;}
void singleton(PN2_Session s,PN2_Result result,PN2_Part part){int count=-7;check(PN2_GetPartMultipleHeadsNumber(s,result,part,&count)==0&&count==1,"Unlinked SDK ID uses exactly one active head");auto linked=reinterpret_cast<PN2_Part>(uintptr_t(0x12345678));check(PN2_GetPartMultipleHeadsLinkedPart(s,result,part,0,&linked)==0&&linked==part,"Unlinked index zero returns the exact supplied SDK identity");for(const auto index:{-1,1,INT32_MIN,INT32_MAX}){linked=reinterpret_cast<PN2_Part>(uintptr_t(0x12345678));check(PN2_GetPartMultipleHeadsLinkedPart(s,result,part,index,&linked)==-973&&reinterpret_cast<uintptr_t>(linked)==0x12345678,"Invalid sister index returns original -973 and leaves output unchanged");}}
struct SnapshotPart {PN2_Point position;PN2_Orientation orientation;std::vector<uintptr_t> sisters;bool operator==(const SnapshotPart &other)const{return position.x==other.position.x&&position.y==other.position.y&&orientation.horizontal_flip==other.orientation.horizontal_flip&&orientation.min_angle==other.orientation.min_angle&&orientation.max_angle==other.orientation.max_angle&&sisters==other.sisters;}};
using Snapshot=std::map<uintptr_t,SnapshotPart>;
Snapshot snapshot(PN2_Session s,PN2_Result result){check(!PN2_IsObjectError(result),"Actual multi-head result exists");double length=-1;int amount=-1;check(PN2_GetResultInfos(s,result,&length,&amount)==0&&amount>=0,"Query actual multi-head result size");Snapshot output;
    for(int n=0;n<amount;++n){PN2_Part part=nullptr;SnapshotPart record;check(PN2_GetKthNestedPart(s,result,n,&part,&record.position,&record.orientation)==0,"Query actual grouped SDK part pose");int count=-1;check(PN2_GetPartMultipleHeadsNumber(s,result,part,&count)==0&&count>=1,"Query saved active-head count");for(int k=0;k<count;++k){PN2_Part sister=nullptr;check(PN2_GetPartMultipleHeadsLinkedPart(s,result,part,k,&sister)==0,"Query saved ordered stable SDK sister ID");record.sisters.push_back(reinterpret_cast<uintptr_t>(sister));}check(std::find(record.sisters.begin(),record.sisters.end(),reinterpret_cast<uintptr_t>(part))!=record.sisters.end(),"Actual native sister list includes the queried part");check(output.emplace(reinterpret_cast<uintptr_t>(part),std::move(record)).second,"One actual concrete SDK identity per result");}
    for(const auto &[id,part]:output)for(const auto sister:part.sisters){check(output.contains(sister),"Every stored sister ID belongs to this current published result");check(output.at(sister).sisters==part.sisters,"All sisters expose the same actual ordered group");}
    return output;
}
void preservesGroups(const Snapshot &source,const Snapshot &next){check(source.size()==next.size(),"Optimization retains all actual concrete IDs");for(const auto &[id,part]:source){check(next.contains(id)&&next.at(id).sisters==part.sisters,"New result preserves exact SDK sister IDs and stored group order");if(part.sisters.size()<2)continue;const auto first=part.sisters.front();const auto &oldFirst=source.at(first),&newFirst=next.at(first);check(std::abs((part.position.x-oldFirst.position.x)-(next.at(id).position.x-newFirst.position.x))<1.1e-6&&std::abs((part.position.y-oldFirst.position.y)-(next.at(id).position.y-newFirst.position.y))<1.1e-3,"Actual Shake/Compact hard rows preserve both relative coordinates of concurrent cutting heads");}}
}
int main(){try{
    {
        Owner owner;const auto s=owner.value;for(const auto number:{0,-1,INT32_MIN})check(PN2_SetCuttingHeadsNumber(s,number)==-973,"Original multi-head count rejects nonpositive signed values");for(const auto number:{1,2,INT32_MAX,1})check(PN2_SetCuttingHeadsNumber(s,number)==0,"Positive signed head counts are preserved without a guessed hardware cap");
        using Setter=PN2_ErrorCode(PN2S_CALL *)(PN2_Session,double);const std::array<Setter,3> setters{PN2_SetCuttingHeadsMinimumGap,PN2_SetCuttingHeadsMaximumGap,PN2_SetAdditionnalHeadCuttingUnitCost};
        for(const auto setter:setters){for(const auto value:{-1.,-std::numeric_limits<double>::infinity()})check(setter(s,value)==-973,"Ordered negative hardware values return original -973");for(const auto value:{0.,-0.,1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})check(setter(s,value)==0,"Original floating setter admits signed zero, infinity and unordered NaN");check(setter(s,0.)==0,"Reset accepted raw hardware field");check(setter(nullptr,1.)==PN2S_INVALID,"Invalid session never reaches hardware state");}
        check(PN2_SetCuttingHeadsNumber(nullptr,2)==PN2S_INVALID,"Invalid count setter session");++cases;
    }
    {
        Owner owner,foreign;const auto s=owner.value;check(PN2S_SetStrategy(s,PN2S_OFFSET_RECOVERED)==0&&PN2S_SetPitch(s,.5)==0,"Explicit ordinary Offset route is not relabeled as manager nine");const auto ids=parts(s,4);const auto stock=PN2_AddSheet(s,8,12);const auto result=PN2_Nest(s,stock,ids.data(),3,15.);check(!PN2_IsObjectError(result),"Actual singleton-source result");
        for(const auto part:ids)singleton(s,result,part);singleton(s,result,nullptr);singleton(s,result,reinterpret_cast<PN2_Part>(std::numeric_limits<uintptr_t>::max()));const auto alien=parts(foreign.value,1);singleton(s,result,alien[0]);
        int output=-7;check(PN2_GetPartMultipleHeadsNumber(foreign.value,result,ids[0],&output)==PN2S_INVALID&&output==-7,"Cross-session result query leaves count unchanged");check(PN2_GetPartMultipleHeadsNumber(s,result,ids[0],nullptr)==PN2S_INVALID,"Null count output is rejected safely");auto linked=ids[3];check(PN2_GetPartMultipleHeadsLinkedPart(foreign.value,result,ids[0],0,&linked)==PN2S_INVALID&&linked==ids[3],"Cross-session linked query leaves destination unchanged");check(PN2_GetPartMultipleHeadsLinkedPart(s,result,ids[0],0,nullptr)==PN2S_INVALID,"Null linked output is rejected safely");
        const auto compact=PN2_Compact(s,result,1,15.);check(!PN2_IsObjectError(compact),"Actual compact of singleton material");for(const auto part:ids)singleton(s,compact,part);const auto shake=PN2_Shake(s,result,15.);check(!PN2_IsObjectError(shake),"Actual shake of singleton material");for(const auto part:ids)singleton(s,shake,part);++cases;
    }
    {
        Owner owner;const auto s=owner.value;check(PN2_SetCuttingHeadsNumber(s,3)==0&&PN2_SetCuttingHeadsMinimumGap(s,1.)==0&&PN2_SetCuttingHeadsMaximumGap(s,10.)==0,"Configure actual raw imported-layout head gaps");const auto ids=parts(s,4);const auto stock=PN2_AddSheet(s,12,12);const std::array poses{PN2_CreateOrientation(0),PN2_CreateOrientation(0),PN2_CreateOrientation(0),PN2_CreateOrientation(0)};const std::array<PN2_Point,4> positions{{{1,1},{1,3},{1,5},{1,7}}};const auto result=PN2_PreNest(s,stock,ids.data(),poses.data(),positions.data(),4);check(!PN2_IsObjectError(result),"Actual manual source with equal-coordinate geometry");for(const auto part:ids)singleton(s,result,part);check(PN2_SetCuttingHeadsNumber(s,1)==0,"Hardware changes do not rewrite old serialized result groups");for(const auto part:ids)singleton(s,result,part);++cases;
    }
    {
        Owner owner;const auto s=owner.value;check(PN2S_SetPitch(s,1.)==0&&PN2S_SetRandomSeed(s,1)==0,"Set concrete lattice and seed while retaining the new session's original default strategy");check(PN2_SetCuttingHeadsNumber(s,3)==0&&PN2_SetCuttingHeadsMinimumGap(s,1.)==0&&PN2_SetCuttingHeadsMaximumGap(s,2.)==0&&PN2_SetAdditionnalHeadCuttingUnitCost(s,1.)==0,"Configure public hardware and actual parallel cutting cost");const PN2_Point points[]{{0,0},{1,0},{1,1},{0,1}};const auto shape=PN2_AddShape(s,points,4,{0,0});check(!PN2_IsObjectError(shape),"Create default-preset multi-head geometry");const auto pose=PN2_CreateOrientation(0);std::array<PN2_Part,12> ids{};check(PN2_AddSeveralParts(s,shape,&pose,1,12,ids.data())==0,"Create actual default-preset concrete demand");const auto stock=PN2_AddSheet(s,3,4);const auto result=PN2_Nest(s,stock,ids.data(),12,20.);const auto source=snapshot(s,result);check(source.size()==12,"Original default public route places actual material without forcing manager nine");std::set<std::vector<uintptr_t>> groups;
        for(const auto &[id,part]:source)if(part.sisters.size()>1){check(part.sisters.size()<=3,"Actual original default result respects configured maximum heads");++linkedParts;groups.insert(part.sisters);const auto &first=source.at(part.sisters.front());for(size_t n=0;n<part.sisters.size();++n){const auto &sister=source.at(part.sisters[n]);check(sister.position.x==first.position.x&&sister.orientation.min_angle==first.orientation.min_angle&&sister.orientation.horizontal_flip==first.orientation.horizontal_flip,"Original default sisters retain actual common X and orientation");if(n){const auto gap=sister.position.y-source.at(part.sisters[n-1]).position.y;check(gap>=1.-1e-3&&gap<=2.+1e-3,"Stored native member order ascends through hardware head gaps");}}}
        check(!groups.empty()&&linkedParts>=2,"Actual default Rectangle import creates native sister owners exposed through public getters");actualGroups+=groups.size();++cases;
        check(PN2_SetCuttingHeadsNumber(s,1)==0&&PN2_SetCuttingHeadsMaximumGap(s,1.)==0,"Change current hardware after original result publication");check(snapshot(s,result)==source,"Result queries retain original serialized groups independently of later hardware changes");
        const auto bottom=snapshot(s,PN2_Compact(s,result,1,10.));preservesGroups(source,bottom);++cases;const auto top=snapshot(s,PN2_Compact(s,result,0,10.));preservesGroups(source,top);++cases;const auto shake=snapshot(s,PN2_Shake(s,result,10.));preservesGroups(source,shake);++cases;check(snapshot(s,result)==source,"All successful grouped optimization copies keep the original result unchanged");
        check(PN2_NestMore(s,result,nullptr,0,0.)==result,"Single NestMore zero-addition preserves the original grouped result handle");auto extra=PN2_AddPart(s,shape,&pose,1);const auto unsupported=PN2_NestMore(s,result,&extra,1,10.);check(PN2_IsObjectError(unsupported)&&PN2_ErrorCodeFromObject(unsupported)==PN2S_UNSUPPORTED,"Grouped nonzero single continuation is explicit until actual warm sister replay is connected");check(snapshot(s,result)==source,"Unsupported grouped continuation keeps original IDs, poses and sister lists unchanged");++cases;
    }
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"groups\":"<<actualGroups<<",\"linkedParts\":"<<linkedParts<<",\"checks\":"<<checks<<"}\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
