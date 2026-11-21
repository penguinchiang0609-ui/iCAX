#include "alma_study.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

// The exact supplier declaration is also accepted if the central public
// header is rebuilt after this independent test source was prepared.
PN2S_API PN2_MultiResult PN2S_CALL PN2_MultiPreNest(PN2_Session,int,const PN2_Sheet *,
    const int *,const PN2_Part *,const PN2_Orientation *,const PN2_Point *);

namespace {
size_t checks=0,cases=0,boardCases=0,placed=0,manualDefects=0;
void check(bool ok,const char *message){++checks;if(!ok){std::cerr<<"DLL detail: "<<PN2S_LastErrorDetail()<<'\n';throw std::runtime_error(message);}}
uintptr_t id(const void *p){return reinterpret_cast<uintptr_t>(p);}
struct Owner{PN2_Session session=PN2_CreateSession();Owner(){check(!PN2_IsObjectError(session),"Create actual multi manual session");}~Owner(){PN2_DeleteSession(session);}};
struct Item{uintptr_t part=0;PN2_Point position{};PN2_Orientation pose{};bool operator==(const Item &p)const{return part==p.part&&position.x==p.position.x&&position.y==p.position.y&&pose.horizontal_flip==p.pose.horizontal_flip&&pose.min_angle==p.pose.min_angle&&pose.max_angle==p.pose.max_angle;}};
struct Board{uintptr_t sheet=0;std::vector<Item> parts;bool operator==(const Board &)const=default;};
std::vector<Board> snapshot(PN2_Session session,PN2_MultiResult multi){check(!PN2_IsObjectError(multi),"Actual MultiPreNest publishes an owned multi result");int count=-1;check(PN2_GetMultiResultInfos(session,multi,&count)==0&&count>=0,"Query actual multi result board count");std::vector<Board> output;
    for(int n=0;n<count;++n){PN2_Sheet sheet=nullptr;const auto result=PN2_GetResult(session,multi,n,&sheet);check(!PN2_IsObjectError(result)&&sheet,"Query actual original sheet identity for every board occurrence");double length=-1;int amount=-1;check(PN2_GetResultInfos(session,result,&length,&amount)==0&&amount>=0,"Query each actual manual board");Board board{id(sheet),{}};
        for(int k=0;k<amount;++k){PN2_Part part=nullptr;PN2_Point position{};PN2_Orientation pose{};check(PN2_GetKthNestedPart(session,result,k,&part,&position,&pose)==0,"Query actual multi manual concrete part/pose");board.parts.push_back({id(part),position,pose});}output.push_back(std::move(board));}
    return output;
}
PN2_Shape rectangle(PN2_Session session){const PN2_Point points[]{{0,0},{3,0},{3,2},{0,2}};const auto result=PN2_AddShape(session,points,4,{0,0});check(!PN2_IsObjectError(result),"Create multi manual rectangle");return result;}
std::vector<PN2_Part> instances(PN2_Session session,PN2_Shape shape,int count){const auto pose=PN2_CreateOrientation(0);std::vector<PN2_Part> result(static_cast<size_t>(count));check(PN2_AddSeveralParts(session,shape,&pose,1,count,result.data())==0,"Create distinct SDK multi manual instances");return result;}
void error(PN2_MultiResult result,int code){check(PN2_IsObjectError(result)&&PN2_ErrorCodeFromObject(result)==code,"Actual MultiPreNest explicit error category");++cases;}
std::set<uintptr_t> identities(const std::vector<Board> &boards){std::set<uintptr_t> output;for(const auto &board:boards)for(const auto &part:board.parts)check(output.insert(part.part).second,"One concrete SDK instance has only one owning board across the global multi result");return output;}
}
int main(){try{
    {
        Owner owner;const auto s=owner.session;check(PN2S_SetPitch(s,.5)==0,"Actual multi manual pitch");const auto all=instances(s,rectangle(s),6);const auto a=PN2_AddSheet(s,10,20),b=PN2_AddSheet(s,8,12);const std::array sheets{a,b,a};const std::array counts{2,0,2};
        const std::array parts{all[4],all[1],all[5],all[0]};const std::array poses{PN2_CreateOrientation(0),PN2_CreateOrientation(0),PN2_CreateOrientation(0),PN2_CreateOrientation(0)};const std::array<PN2_Point,4> positions{{{0,0},{8,4},{0,0},{8,4}}};
        const auto savedParts=parts;const auto savedSheets=sheets;const auto savedCounts=counts;const auto savedPoses=poses;const auto savedPositions=positions;
        const auto manual=PN2_MultiPreNest(s,3,sheets.data(),counts.data(),parts.data(),poses.data(),positions.data());const auto source=snapshot(s,manual);
        check(source.size()==3&&source[0].sheet==id(a)&&source[1].sheet==id(b)&&source[2].sheet==id(a),"Flat manual arrays preserve original board order and repeated stock handles");
        check(source[0].parts.size()==2&&source[1].parts.empty()&&source[2].parts.size()==2,"Zero-count middle board does not shift subsequent flat input slices");
        const auto ids=identities(source);check(ids==std::set<uintptr_t>{id(all[4]),id(all[1]),id(all[5]),id(all[0])},"Global manual directory contains exactly the requested concrete demand");
        for(const auto n:{size_t(0),size_t(2)}){std::set<std::pair<double,double>> points;for(const auto &p:source[n].parts)points.emplace(p.position.x,p.position.y);check(points==std::set<std::pair<double,double>>{{0,0},{8,4}},"Distinct physical boards can use identical local part positions");}
        check(parts==savedParts&&sheets==savedSheets&&counts==savedCounts,"Actual multi entry copies flat identity/count arrays");for(size_t n=0;n<4;++n)check(positions[n].x==savedPositions[n].x&&positions[n].y==savedPositions[n].y&&poses[n].min_angle==savedPoses[n].min_angle&&poses[n].max_angle==savedPoses[n].max_angle&&poses[n].horizontal_flip==savedPoses[n].horizontal_flip,"Actual multi entry preserves caller-owned position/orientation arrays");
        int unplaced=-1;check(PN2S_GetMultiUnplacedCount(s,manual,&unplaced)==0&&unplaced==0,"Exact requested multi manual demand is complete");++cases;boardCases+=3;placed+=4;
        auto duplicated=parts;duplicated[2]=parts[0];error(PN2_MultiPreNest(s,3,sheets.data(),counts.data(),duplicated.data(),poses.data(),positions.data()),PN2S_UNSUPPORTED);
        duplicated=parts;duplicated[1]=parts[0];error(PN2_MultiPreNest(s,3,sheets.data(),counts.data(),duplicated.data(),poses.data(),positions.data()),PN2S_UNSUPPORTED);
        const std::array negative{2,-1,2};error(PN2_MultiPreNest(s,3,sheets.data(),negative.data(),parts.data(),poses.data(),positions.data()),PN2S_INVALID);
        error(PN2_MultiPreNest(s,3,nullptr,counts.data(),parts.data(),poses.data(),positions.data()),PN2S_INVALID);error(PN2_MultiPreNest(s,3,sheets.data(),nullptr,parts.data(),poses.data(),positions.data()),PN2S_INVALID);
        error(PN2_MultiPreNest(s,3,sheets.data(),counts.data(),nullptr,poses.data(),positions.data()),PN2S_INVALID);error(PN2_MultiPreNest(s,3,sheets.data(),counts.data(),parts.data(),nullptr,positions.data()),PN2S_INVALID);error(PN2_MultiPreNest(s,3,sheets.data(),counts.data(),parts.data(),poses.data(),nullptr),PN2S_INVALID);
        error(PN2_MultiPreNest(s,0,sheets.data(),counts.data(),parts.data(),poses.data(),positions.data()),PN2S_INVALID);
        Owner foreign;const auto alien=instances(foreign.session,rectangle(foreign.session),1);duplicated=parts;duplicated[3]=alien[0];error(PN2_MultiPreNest(s,3,sheets.data(),counts.data(),duplicated.data(),poses.data(),positions.data()),PN2S_INVALID);
        auto foreignSheets=sheets;foreignSheets[1]=PN2_AddSheet(foreign.session,8,12);error(PN2_MultiPreNest(s,3,foreignSheets.data(),counts.data(),parts.data(),poses.data(),positions.data()),PN2S_INVALID);
        check(snapshot(s,manual)==source,"Rejected multi manual calls leave the entire previously published multi result immutable");
    }
    {
        Owner owner;const auto s=owner.session;const auto sheet=PN2_AddSheet(s,8,12);const std::array sheets{sheet,sheet};const std::array counts{0,0};const auto result=PN2_MultiPreNest(s,2,sheets.data(),counts.data(),nullptr,nullptr,nullptr);const auto output=snapshot(s,result);
        check(output.size()==2&&output[0].parts.empty()&&output[1].parts.empty()&&output[0].sheet==id(sheet)&&output[1].sheet==id(sheet),"All-empty manual inputs retain distinct supplied board occurrences and accept absent flat arrays");int unplaced=-1;check(PN2S_GetMultiUnplacedCount(s,result,&unplaced)==0&&unplaced==0,"All-empty multi manual result has no missing demand");++cases;boardCases+=2;
    }
    {
        Owner owner;const auto s=owner.session;const auto all=instances(s,rectangle(s),4);const auto sheet=PN2_AddSheet(s,8,12);const std::array sheets{sheet,sheet};const std::array counts{2,2};const std::array poses{PN2_CreateOrientation(0),PN2_CreateOrientation(0),PN2_CreateOrientation(0),PN2_CreateOrientation(0)};const std::array<PN2_Point,4> positions{{{-4,-3},{-4,-3},{1,1},{1,1}}};
        const auto output=snapshot(s,PN2_MultiPreNest(s,2,sheets.data(),counts.data(),all.data(),poses.data(),positions.data()));check(output.size()==2&&output[0].parts.size()==2&&output[1].parts.size()==2,"Actual manual multi retains both overlapping and out-of-stock supplied parts");identities(output);
        for(const auto &p:output[0].parts)check(p.position.x==-4&&p.position.y==-3,"Manual negative world coordinates survive global ownership without geometric repair");for(const auto &p:output[1].parts)check(p.position.x==1&&p.position.y==1,"Manual overlap stays confined to the requested owning board");++cases;++manualDefects;boardCases+=2;placed+=4;
    }
    {
        Owner owner;const auto s=owner.session;const auto all=instances(s,rectangle(s),2);const auto sheet=PN2_AddSheet(s,8,12);const std::array sheets{sheet,sheet};const std::array counts{1,1};const std::array poses{PN2_CreateOrientation(45),PN2_CreateOrientation(0)};const std::array<PN2_Point,2> positions{{{1,1},{1,1}}};
        const auto result=PN2_MultiPreNest(s,2,sheets.data(),counts.data(),all.data(),poses.data(),positions.data());const auto output=snapshot(s,result);check(output.size()==2&&output[0].parts.empty()&&output[1].parts.size()==1&&output[1].parts[0].part==id(all[1]),"Disallowed pose is skipped in the global mandatory phase while the other board succeeds");
        int count=-1;PN2_Part missing=nullptr;check(PN2S_GetMultiUnplacedCount(s,result,&count)==0&&count==1&&PN2S_GetMultiUnplacedPart(s,result,0,&missing)==0&&missing==all[0],"Global unplaced queries retain the exact rejected SDK identity");++cases;boardCases+=2;++placed;
    }
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"boards\":"<<boardCases<<",\"placements\":"<<placed<<",\"manualDefects\":"<<manualDefects<<",\"checks\":"<<checks<<"}\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
