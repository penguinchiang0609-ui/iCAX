#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "alma_study.h"
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
size_t checks=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
struct Sdk {
    HMODULE module;
#define SDK_FN(name) decltype(&::name) name=nullptr;
    SDK_FN(PN2_CreateSession) SDK_FN(PN2_DeleteSession) SDK_FN(PN2S_SetStrategy)
    SDK_FN(PN2S_SetRandomSeed) SDK_FN(PN2S_SetRectangleGap) SDK_FN(PN2_AddShape)
    SDK_FN(PN2_ShapeAddHole) SDK_FN(PN2_AddSeveralParts) SDK_FN(PN2_AddSheet)
    SDK_FN(PN2_Nest) SDK_FN(PN2_MultiNest) SDK_FN(PN2_GetMultiResultInfos)
    SDK_FN(PN2_GetResult) SDK_FN(PN2_GetResultInfos) SDK_FN(PN2_GetKthNestedPart)
    SDK_FN(PN2S_GetMultiUnplacedCount) SDK_FN(PN2S_GetMultiUnplacedPart)
    SDK_FN(PN2_IsObjectError) SDK_FN(PN2_ErrorCodeFromObject) SDK_FN(PN2S_LastErrorDetail)
#undef SDK_FN
    explicit Sdk(const wchar_t *path):module(LoadLibraryW(path)){
        check(module!=nullptr,"Load exact SDK binary");
#define SDK_FN(name) name=reinterpret_cast<decltype(name)>(GetProcAddress(module,#name));check(name!=nullptr,#name " export");
        SDK_FN(PN2_CreateSession) SDK_FN(PN2_DeleteSession) SDK_FN(PN2S_SetStrategy)
        SDK_FN(PN2S_SetRandomSeed) SDK_FN(PN2S_SetRectangleGap) SDK_FN(PN2_AddShape)
        SDK_FN(PN2_ShapeAddHole) SDK_FN(PN2_AddSeveralParts) SDK_FN(PN2_AddSheet)
        SDK_FN(PN2_Nest) SDK_FN(PN2_MultiNest) SDK_FN(PN2_GetMultiResultInfos)
        SDK_FN(PN2_GetResult) SDK_FN(PN2_GetResultInfos) SDK_FN(PN2_GetKthNestedPart)
        SDK_FN(PN2S_GetMultiUnplacedCount) SDK_FN(PN2S_GetMultiUnplacedPart)
        SDK_FN(PN2_IsObjectError) SDK_FN(PN2_ErrorCodeFromObject) SDK_FN(PN2S_LastErrorDetail)
#undef SDK_FN
    }
    ~Sdk(){if(module)FreeLibrary(module);}
    template<class T>T object(T value,const char *where){check(!PN2_IsObjectError(value),where);return value;}
    void code(int value,const char *where){check(value>=0,where);}
};
struct Session {
    Sdk &sdk;PN2_Session value;
    explicit Session(Sdk &api):sdk(api),value(sdk.object(sdk.PN2_CreateSession(),"Create session")){}
    ~Session(){sdk.PN2_DeleteSession(value);}
};
struct Snapshot {
    double length=0;
    std::vector<PN2_Part> parts;
    std::vector<std::array<double,5>> poses;
    bool operator==(const Snapshot &)const=default;
};
Snapshot snapshot(Sdk &sdk,PN2_Session session,PN2_Result result){
    Snapshot out;int count=0;sdk.code(sdk.PN2_GetResultInfos(session,result,&out.length,&count),"Read result");
    for(int i=0;i<count;++i){PN2_Part part=nullptr;PN2_Point p{};PN2_Orientation o{};
        sdk.code(sdk.PN2_GetKthNestedPart(session,result,i,&part,&p,&o),"Read concrete instance");
        out.parts.push_back(part);out.poses.push_back({p.x,p.y,double(o.horizontal_flip),o.min_angle,o.max_angle});}
    return out;
}
std::vector<PN2_Part> defaultOrder(Sdk &sdk,PN2_Session session){
    const std::vector<std::vector<PN2_Point>> rings{
        {{0,0},{70,0},{70,20},{25,20},{25,65},{0,65}},
        {{0,0},{65,0},{65,55},{0,55}},{{0,0},{30,0},{30,20},{0,20}}};
    const std::array<int,3> counts{8,4,12};
    const std::array<PN2_Orientation,4> poses{{{0,0,0},{0,90,90},{0,180,180},{0,270,270}}};
    std::vector<PN2_Part> parts;
    for(size_t group=0;group<rings.size();++group){
        const auto shape=sdk.object(sdk.PN2_AddShape(session,rings[group].data(),int(rings[group].size()),{0,0}),"Create default sample shape");
        if(group==1){const PN2_Point hole[]{{15,15},{50,15},{50,40},{15,40}};sdk.code(sdk.PN2_ShapeAddHole(session,shape,hole,4),"Create actual frame hole");}
        const auto start=parts.size();parts.resize(start+size_t(counts[group]));
        sdk.code(sdk.PN2_AddSeveralParts(session,shape,poses.data(),int(poses.size()),counts[group],parts.data()+start),"Create default sample instances");
    }
    return parts;
}
void run(Sdk &sdk,double seconds,bool requireSuccess){
    Session session(sdk);sdk.code(sdk.PN2S_SetStrategy(session.value,PN2S_MAP_GREEDY),"Select same material SDK route as demo");
    sdk.code(sdk.PN2S_SetRandomSeed(session.value,1),"Seed");sdk.code(sdk.PN2S_SetRectangleGap(session.value,2),"Actual pair gap");
    const auto parts=defaultOrder(sdk,session.value);
    const auto sheet=sdk.object(sdk.PN2_AddSheet(session.value,190,290),"Actual margin-shrunk 300x200 stock");
    int inventory=2;
    const auto previous=sdk.object(sdk.PN2_Nest(session.value,sheet,parts.data(),1,2.),"Create independent prior result");
    const auto prior=snapshot(sdk,session.value,previous);
    const auto started=std::chrono::steady_clock::now();
    const auto multi=sdk.PN2_MultiNest(session.value,&sheet,1,&inventory,parts.data(),int(parts.size()),seconds);
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    const std::string detail=sdk.PN2S_LastErrorDetail();
    const int error=sdk.PN2_IsObjectError(multi)?sdk.PN2_ErrorCodeFromObject(multi):0;
    int boardCount=0,placed=0,unplaced=0;
    std::set<PN2_Part> seen;const std::set<PN2_Part> requested(parts.begin(),parts.end());
    if(!error){
        sdk.code(sdk.PN2_GetMultiResultInfos(session.value,multi,&boardCount),"Read boards");
        check(boardCount<=inventory,"Published result respects concrete stock inventory");
        for(int b=0;b<boardCount;++b){PN2_Sheet actual=nullptr;const auto result=sdk.object(sdk.PN2_GetResult(session.value,multi,b,&actual),"Read stock result");
            check(actual==sheet,"Published board retains actual input stock handle");
            const auto board=snapshot(sdk,session.value,result);placed+=int(board.parts.size());
            for(const auto part:board.parts)check(requested.contains(part)&&seen.insert(part).second,"Placed concrete ID exists and is unique across boards");}
        sdk.code(sdk.PN2S_GetMultiUnplacedCount(session.value,multi,&unplaced),"Read unplaced count");
        for(int i=0;i<unplaced;++i){PN2_Part part=nullptr;sdk.code(sdk.PN2S_GetMultiUnplacedPart(session.value,multi,i,&part),"Read unplaced concrete ID");
            check(requested.contains(part)&&seen.insert(part).second,"Unplaced concrete ID is unique and disjoint from all boards");}
        check(seen==requested,"Published placed/unplaced queues conserve every concrete input ID");
    }
    check(inventory==2,"Caller stock inventory array remains unchanged");
    check(snapshot(sdk,session.value,previous)==prior,"Prior result remains immutable after a later complete/timeout MultiNest");
    if(requireSuccess&&seconds>=5.)check(!error&&placed==24&&unplaced==0,"Default demo order must publish its completed native plan");
    if(seconds<1e-8)check(error==PN2S_TIMEOUT,"No evaluated layout exists at sub-clock budget, so timeout remains atomic");
    std::cout<<std::setprecision(9)<<"{\"seconds\":"<<seconds<<",\"elapsedSeconds\":"<<elapsed<<",\"error\":"<<error
        <<",\"boards\":"<<boardCount<<",\"placed\":"<<placed<<",\"unplaced\":"<<unplaced<<",\"detail\":\"";
    for(const char c:detail){if(c=='"'||c=='\\')std::cout<<'\\';if(c=='\n'||c=='\r')std::cout<<' ';else std::cout<<c;}
    std::cout<<"\"}\n"<<std::flush;
}
}
int wmain(int argc,wchar_t **argv){try{
    check(argc>=2,"Pass exact DLL path");Sdk sdk(argv[1]);
    const bool expectFixed=argc<3||std::wstring_view(argv[2])==L"--expect-fixed";
    if(argc>3&&std::wstring_view(argv[2])==L"--seconds")run(sdk,std::stod(argv[3]),false);
    else for(const double seconds:std::array<double,5>{1e-12,2.5,5.,10.,20.})run(sdk,seconds,expectFixed);
    std::cout<<"{\"pass\":true,\"checks\":"<<checks<<",\"expectedFixed\":"<<(expectFixed?"true":"false")<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
