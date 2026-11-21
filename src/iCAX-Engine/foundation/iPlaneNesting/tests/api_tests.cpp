#include "alma_study.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cmath>
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
static int checks = 0;
static void check(bool b,const std::source_location location=std::source_location::current()) {
    checks++;
    if (!b)
        throw std::runtime_error("API contract check " + std::to_string(checks) + " failed at line "+
            std::to_string(location.line())+": "+PN2S_LastErrorDetail());
}
struct ResultSnapshot {
    double length = 0;
    std::vector<PN2_Part> parts;
    std::vector<std::array<double, 5>> poses;
    bool operator==(const ResultSnapshot &) const = default;
};
static ResultSnapshot snapshotResult(PN2_Session session, PN2_Result result) {
    ResultSnapshot snapshot;
    int count = 0;
    check(PN2_GetResultInfos(session, result, &snapshot.length, &count) == 0);
    for (int index = 0; index < count; ++index) {
        PN2_Part part;
        PN2_Point point;
        PN2_Orientation orientation;
        check(PN2_GetKthNestedPart(session, result, index, &part, &point, &orientation) == 0);
        snapshot.parts.push_back(part);
        snapshot.poses.push_back({point.x, point.y, double(orientation.horizontal_flip),
                                  orientation.min_angle, orientation.max_angle});
    }
    return snapshot;
}
static void checkConcurrentSessionDestruction() {
    // Each operation is atomic with session deletion. A successful returned
    // handle may expire before the caller's subsequent query.
    for (int scene = 0; scene < 48; ++scene) {
        const auto session = PN2_CreateSession();
        // This test targets session lifetime races with a bounded single pass.
        // The timed default supervisor is exercised by validate-default.mjs.
        check(PN2S_SetStrategy(session, PN2S_MAP_GREEDY) == 0);
        const PN2_Point contour[]{{0,0}, {2,0}, {2,1}, {0,1}};
        const auto shape = PN2_AddShape(session, contour, 4, {0,0});
        const auto orientation = PN2_CreateOrientation(0);
        const auto part = PN2_AddPart(session, shape, &orientation, 1);
        const auto sheet = PN2_AddSheet(session, 8, 10);
        const auto original = PN2_Nest(session, sheet, &part, 1, 2);
        check(!PN2_IsObjectError(original));
        std::atomic<int> failures = 0;
        std::barrier start(4), destroyed(4);
        std::array<std::thread, 3> workers;
        for (int worker = 0; worker < 3; ++worker) {
            workers[worker] = std::thread([&, worker] {
                start.arrive_and_wait();
                for (int request = 0; request < 12; ++request) {
                    const auto result = worker == 0 ? PN2_Nest(session, sheet, &part, 1, 2)
                        : worker == 1 ? PN2_Compact(session, original, request % 2, 2)
                                      : PN2_Shake(session, original, 2);
                    if (PN2_IsObjectError(result)) {
                        if (PN2_ErrorCodeFromObject(result) != PN2S_INVALID)
                            ++failures;
                    } else {
                        double length = -17;
                        int count = -23;
                        const auto status = PN2_GetResultInfos(session, result, &length, &count);
                        if ((status == 0 && (count != 1 || length < 2 || length > 10)) ||
                            (status == PN2S_INVALID && (length != -17 || count != -23)) ||
                            (status != 0 && status != PN2S_INVALID))
                            ++failures;
                    }
                }
                destroyed.arrive_and_wait();
                double length = -17;
                int count = -23;
                if (PN2_GetResultInfos(session, original, &length, &count) != PN2S_INVALID ||
                    length != -17 || count != -23 ||
                    PN2_ErrorCodeFromObject(PN2_AddPart(session, shape, &orientation, 1)) != PN2S_INVALID)
                    ++failures;
            });
        }
        start.arrive_and_wait();
        const auto deleteStatus = PN2_DeleteSession(session);
        destroyed.arrive_and_wait();
        for (auto &worker : workers)
            worker.join();
        check(deleteStatus == 1);
        check(failures == 0);
        check(PN2_DeleteSession(session) == PN2S_INVALID);
        const auto replacement = PN2_CreateSession();
        check(replacement != session && !PN2_IsObjectError(replacement));
        check(PN2_ErrorCodeFromObject(PN2_AddPart(replacement, shape, &orientation, 1)) == PN2S_INVALID);
        check(PN2_DeleteSession(replacement) == 1);
    }
    std::barrier errorReady(2), otherThreadDone(2);
    std::atomic<bool> preserved = false;
    std::thread failing([&] {
        const auto result = PN2_AddSheet(nullptr, 2, 3);
        const std::string text = PN2S_LastErrorDetail();
        errorReady.arrive_and_wait();
        otherThreadDone.arrive_and_wait();
        preserved = PN2_ErrorCodeFromObject(result) == PN2S_INVALID &&
                    !text.empty() && text == PN2S_LastErrorDetail();
    });
    errorReady.arrive_and_wait();
    const auto independent = PN2_CreateSession();
    const auto deleteStatus = PN2_DeleteSession(independent);
    otherThreadDone.arrive_and_wait();
    failing.join();
    check(deleteStatus == 1);
    check(preserved);
}
static void checkLOffcutApi() {
    check(PN2_ActivateLOffcut(nullptr,1)==PN2S_INVALID);
    check(PN2_SetLOffcutMinimumUsableHeight(nullptr,0)==PN2S_INVALID);
    const auto owner=PN2_CreateSession();check(!PN2_IsObjectError(owner));
    check(PN2_SetLOffcutMinimumUsableHeight(owner,std::numeric_limits<double>::quiet_NaN())==0);
    check(PN2_SetLOffcutMinimumUsableHeight(owner,-.0)==0);
    check(PN2_SetLOffcutMinimumUsableHeight(owner,.25)==0);
    check(PN2_ActivateLOffcut(owner,-7)==0);
    check(PN2S_SetPitch(owner,.5)==0);
    const PN2_Point polygon[]{{0,0},{3,0},{3,4},{0,4}};
    const auto shape=PN2_AddShape(owner,polygon,4,{0,0});const auto pose=PN2_CreateOrientation(0);
    PN2_Part parts[3];check(PN2_AddSeveralParts(owner,shape,&pose,1,3,parts)==0);
    const auto sheet=PN2_AddSheet(owner,12,15);
    const auto result=PN2_Nest(owner,sheet,parts,3,1.);
    if(PN2_IsObjectError(result))std::cerr<<"L-offcut single: "<<PN2S_LastErrorDetail()<<'\n';
    check(!PN2_IsObjectError(result));const auto saved=snapshotResult(owner,result);check(saved.parts.size()==3);
    check(PN2S_SetStrategy(owner,PN2S_MAP_GREEDY)==0);
    check(PN2_ErrorCodeFromObject(PN2_Nest(owner,sheet,parts,3,1.))==PN2S_UNSUPPORTED);
    check(PN2_ActivateLOffcut(owner,0)==0);
    check(!PN2_IsObjectError(PN2_Nest(owner,sheet,parts,3,1.)));
    check(snapshotResult(owner,result)==saved);
    check(PN2S_SetStrategy(owner,PN2S_DEFAULT_RECOVERED)==0);
    check(PN2_ActivateLOffcut(owner,1)==0);
    const auto small=PN2_AddSheet(owner,6,6);const int inventory=2;
    const auto multi=PN2_MultiNest(owner,&small,1,&inventory,parts,3,3.);
    if(PN2_IsObjectError(multi))std::cerr<<"L-offcut multi: "<<PN2S_LastErrorDetail()<<'\n';
    check(!PN2_IsObjectError(multi));int boards=0,unplaced=0;
    check(PN2_GetMultiResultInfos(owner,multi,&boards)==0 && boards==2);
    check(PN2S_GetMultiUnplacedCount(owner,multi,&unplaced)==0 && unplaced==0);
    std::vector<PN2_Part> placed;
    for(int index=0;index<boards;++index){PN2_Sheet actual;const auto layout=PN2_GetResult(owner,multi,index,&actual);check(actual==small && !PN2_IsObjectError(layout));const auto board=snapshotResult(owner,layout);placed.insert(placed.end(),board.parts.begin(),board.parts.end());}
    std::sort(placed.begin(),placed.end());std::vector<PN2_Part> expected(parts,parts+3);std::sort(expected.begin(),expected.end());check(placed==expected);
    check(snapshotResult(owner,result)==saved);
    const auto narrow=PN2_AddSheet(owner,6,3);
    const PN2_Sheet types[]{small,narrow};const int counts[]{1,1};
    const auto varied=PN2_MultiNest(owner,types,2,counts,parts,3,3.);
    if(PN2_IsObjectError(varied))std::cerr<<"L-offcut stock types: "<<PN2S_LastErrorDetail()<<'\n';
    check(!PN2_IsObjectError(varied));
    check(PN2_GetMultiResultInfos(owner,varied,&boards)==0 && boards==2);
    check(PN2S_GetMultiUnplacedCount(owner,varied,&unplaced)==0 && unplaced==0);
    std::array<int,2> used{};
    for(int index=0;index<boards;++index){PN2_Sheet stock;const auto layout=PN2_GetResult(owner,varied,index,&stock);check(!PN2_IsObjectError(layout));check(stock==small || stock==narrow);++used[stock==small?0:1];}
    check(used==std::array<int,2>{1,1});
    check(snapshotResult(owner,result)==saved);
    for(double seconds:{.02,.05,.1}) {
        const auto bounded=PN2_MultiNest(owner,&small,1,&inventory,parts,3,seconds);
        if(PN2_IsObjectError(bounded))check(PN2_ErrorCodeFromObject(bounded)==PN2S_TIMEOUT);
        else {check(PN2_GetMultiResultInfos(owner,bounded,&boards)==0 && boards<=2);check(PN2S_GetMultiUnplacedCount(owner,bounded,&unplaced)==0 && unplaced<=3);}
        check(snapshotResult(owner,result)==saved);
    }
    check(PN2_DeleteSession(owner)==1);
}
int main(int argc,char **argv) {
    try {
        if(argc==2 && std::string(argv[1])=="loffcut") {
            checkLOffcutApi();std::cout<<checks<<" L-offcut API checks passed\n";return 0;
        }
        auto s = PN2_CreateSession(), other = PN2_CreateSession();
        check(!PN2_IsObjectError(s));
        PN2_Point p[] = {{0, 0}, {2, 0}, {2, 1}, {0, 1}};
        auto sh = PN2_AddShape(s, p, 4, {0, 0});
        check(!PN2_IsObjectError(sh));
        auto pose = PN2_CreateOrientation(0);
        PN2_Part parts[3];
        check(PN2_AddSeveralParts(s, sh, &pose, 1, 3, parts) == 0);
        auto sheet = PN2_AddSheet(s, 2, 6);
        auto r = PN2_Nest(s, sheet, parts, 3, 10);
        check(!PN2_IsObjectError(r));
        double length = 0;
        int n = 0;
        check(PN2_GetResultInfos(s, r, &length, &n) == 0 && n == 3);
        PN2S_Direction14Options bt{sizeof(PN2S_Direction14Options),1,0,0,2,6,6,0,0};
        double btValues[]{2,3,2}; // Same geometry with different values must keep distinct groups.
        auto btResult=PN2S_NestDirection14(s,sheet,parts,3,btValues,&bt,10);
        check(!PN2_IsObjectError(btResult));
        check(PN2_GetResultInfos(s,btResult,&length,&n)==0 && n==3);
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection14(other,sheet,parts,3,btValues,&bt,10))==PN2S_INVALID);
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection14(s,sheet,parts,3,nullptr,&bt,10))==PN2S_INVALID);
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection14(s,sheet,parts,3,btValues,nullptr,10))==PN2S_INVALID);
        bt.struct_size=0;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection14(s,sheet,parts,3,btValues,&bt,10))==PN2S_INVALID);
        bt.struct_size=sizeof bt;bt.batch_size=0;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection14(s,sheet,parts,3,btValues,&bt,10))==PN2S_INVALID);
        bt.batch_size=1;bt.length_mode=2;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection14(s,sheet,parts,3,btValues,&bt,10))==PN2S_INVALID);
        bt.length_mode=0;btValues[0]=std::nan("");
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection14(s,sheet,parts,3,btValues,&bt,10))==PN2S_INVALID);
        btValues[0]=2;bt.reference_kind=3;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection14(s,sheet,parts,3,btValues,&bt,10))==PN2S_INVALID);
        bt.reference_kind=0;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection14(s,sheet,parts,3,btValues,&bt,1e-12))==PN2S_TIMEOUT);
        check(PN2S_SetRectangleGap(s,1)==0);
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection14(s,sheet,parts,3,btValues,&bt,10))==PN2S_UNSUPPORTED);
        check(PN2S_SetRectangleGap(s,0)==0);
        auto btFreePose=PN2_CreateFreeOrientation(0);auto btFreePart=PN2_AddPart(s,sh,&btFreePose,1);
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection14(s,sheet,&btFreePart,1,btValues,&bt,10))==PN2S_UNSUPPORTED);
        auto btEmpty=PN2S_NestDirection14(s,sheet,nullptr,0,nullptr,&bt,10);
        check(!PN2_IsObjectError(btEmpty) && PN2_GetResultInfos(s,btEmpty,&length,&n)==0 && n==0);
        check(PN2_GetResultInfos(s,btResult,&length,&n)==0 && n==3); // Failed calls did not alter prior result.
        PN2S_Direction18Options bt18{sizeof(PN2S_Direction18Options),1,0,0,2,6,6,0,0,0};
        auto bt18Result=PN2S_NestDirection18(s,sheet,parts,3,btValues,&bt18,10);
        check(!PN2_IsObjectError(bt18Result));
        check(PN2_GetResultInfos(s,bt18Result,&length,&n)==0 && n==1); // 2+1 groups, only the two-instance group is eligible.
        PN2_Point tailPos;PN2_Orientation tailPose;
        check(PN2_GetNestedPart(s,bt18Result,parts[1],&tailPos,&tailPose)==0);
        for(int limit:{-1,0,10,11,2147483647}) {
            bt18.candidate_limit_18=limit;
            auto result=PN2S_NestDirection18(s,sheet,parts,3,btValues,&bt18,10);
            check(!PN2_IsObjectError(result) && PN2_GetResultInfos(s,result,&length,&n)==0 && n==1);
        }
        bt18.batch_size=2;
        auto belowThreshold=PN2S_NestDirection18(s,sheet,parts,3,btValues,&bt18,10);
        check(!PN2_IsObjectError(belowThreshold) && PN2_GetResultInfos(s,belowThreshold,&length,&n)==0 && n==0);
        bt18.batch_size=1;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection18(other,sheet,parts,3,btValues,&bt18,10))==PN2S_INVALID);
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection18(s,sheet,parts,3,btValues,nullptr,10))==PN2S_INVALID);
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection18(s,sheet,parts,3,nullptr,&bt18,10))==PN2S_INVALID);
        bt18.struct_size=sizeof bt;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection18(s,sheet,parts,3,btValues,&bt18,10))==PN2S_INVALID);
        bt18.struct_size=sizeof bt18;bt18.batch_size=0;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection18(s,sheet,parts,3,btValues,&bt18,10))==PN2S_INVALID);
        bt18.batch_size=1;bt18.direction_scale_218=std::nan("");
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection18(s,sheet,parts,3,btValues,&bt18,10))==PN2S_INVALID);
        bt18.direction_scale_218=2;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection18(s,sheet,parts,3,btValues,&bt18,1e-12))==PN2S_TIMEOUT);
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection18(s,sheet,&btFreePart,1,btValues,&bt18,10))==PN2S_UNSUPPORTED);
        check(PN2S_SetRectangleGap(s,1)==0);
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection18(s,sheet,parts,3,btValues,&bt18,10))==PN2S_UNSUPPORTED);
        check(PN2S_SetRectangleGap(s,0)==0);
        auto bt18Empty=PN2S_NestDirection18(s,sheet,nullptr,0,nullptr,&bt18,10);
        check(!PN2_IsObjectError(bt18Empty) && PN2_GetResultInfos(s,bt18Empty,&length,&n)==0 && n==0);
        check(PN2_GetResultInfos(s,bt18Result,&length,&n)==0 && n==1);
        PN2S_BtOptions allBt{sizeof(PN2S_BtOptions),1,0,0,2,6,6,0,0,2,7,10,3,.15,.25,.5,.05,0,0,1,0};
        auto combined=PN2S_NestBt(s,sheet,parts,3,btValues,&allBt,10);
        check(!PN2_IsObjectError(combined) && PN2_GetResultInfos(s,combined,&length,&n)==0 && n==3);
        check(PN2_ErrorCodeFromObject(PN2S_NestBt(other,sheet,parts,3,btValues,&allBt,10))==PN2S_INVALID);
        check(PN2_ErrorCodeFromObject(PN2S_NestBt(s,sheet,parts,3,nullptr,&allBt,10))==PN2S_INVALID);
        check(PN2_ErrorCodeFromObject(PN2S_NestBt(s,sheet,parts,3,btValues,nullptr,10))==PN2S_INVALID);
        check(PN2_ErrorCodeFromObject(PN2S_NestBt(s,sheet,&btFreePart,1,btValues,&allBt,10))==PN2S_UNSUPPORTED);
        check(PN2_ErrorCodeFromObject(PN2S_NestBt(s,sheet,parts,3,btValues,&allBt,1e-12))==PN2S_TIMEOUT);
        auto disabled=allBt;disabled.count_10=disabled.flag_14=disabled.limit_18=disabled.count_1c=0;
        auto none=PN2S_NestBt(s,sheet,parts,3,btValues,&disabled,10);
        check(!PN2_IsObjectError(none) && PN2_GetResultInfos(s,none,&length,&n)==0 && n==0);
        for(auto part:parts)check(PN2_GetNestedPart(s,none,part,&tailPos,&tailPose)==0);
        auto noOrder=PN2S_NestBt(s,sheet,nullptr,0,nullptr,&allBt,10);
        check(!PN2_IsObjectError(noOrder) && PN2_GetResultInfos(s,noOrder,&length,&n)==0 && n==0);
        for(int field=0;field<8;++field) {
            auto bad=allBt;
            switch(field) {
            case 0:bad.struct_size=0;break;
            case 1:bad.batch_size=0;break;
            case 2:bad.count_1c=10001;break;
            case 3:bad.fixed_front=std::nan("");break;
            case 4:bad.force_process_inspection=2;break;
            case 5:bad.replay_previous=-1;break;
            case 6:bad.filter_replay_front=2;break;
            case 7:bad.perimeter_probability_1d8=2;break;
            }
            check(PN2_ErrorCodeFromObject(PN2S_NestBt(s,sheet,parts,3,btValues,&bad,10))==PN2S_INVALID);
        }
        check(PN2_GetResultInfos(s,combined,&length,&n)==0 && n==3);
        PN2S_Direction10Options bt10{sizeof(PN2S_Direction10Options),1,0,0,2,6,6,0,0,3,.15,.25,.5,.05};
        auto bt10Result=PN2S_NestDirection10(s,sheet,parts,3,btValues,&bt10,10);
        check(!PN2_IsObjectError(bt10Result) && PN2_GetResultInfos(s,bt10Result,&length,&n)==0 && n==3);
        for(int count:{-1,0}) {
            bt10.candidate_count_10=count;auto result=PN2S_NestDirection10(s,sheet,parts,3,btValues,&bt10,10);
            check(!PN2_IsObjectError(result) && PN2_GetResultInfos(s,result,&length,&n)==0 && n==0);
        }
        bt10.candidate_count_10=10001;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection10(s,sheet,parts,3,btValues,&bt10,10))==PN2S_INVALID);
        bt10.candidate_count_10=3;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection10(other,sheet,parts,3,btValues,&bt10,10))==PN2S_INVALID);
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection10(s,sheet,parts,3,btValues,nullptr,10))==PN2S_INVALID);
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection10(s,sheet,parts,3,nullptr,&bt10,10))==PN2S_INVALID);
        bt10.struct_size=sizeof bt;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection10(s,sheet,parts,3,btValues,&bt10,10))==PN2S_INVALID);
        bt10.struct_size=sizeof bt10;bt10.alpha_50=std::nan("");
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection10(s,sheet,parts,3,btValues,&bt10,10))==PN2S_INVALID);
        bt10.alpha_50=.15;bt10.perimeter_probability_1d8=1.1;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection10(s,sheet,parts,3,btValues,&bt10,10))==PN2S_INVALID);
        bt10.perimeter_probability_1d8=.5;bt10.perimeter_scale_1e0=-1;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection10(s,sheet,parts,3,btValues,&bt10,10))==PN2S_INVALID);
        bt10.perimeter_scale_1e0=.05;
        bt10.gamma_58=1e308;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection10(s,sheet,parts,3,btValues,&bt10,10))==PN2S_INVALID);
        bt10.gamma_58=.25;
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection10(s,sheet,parts,3,btValues,&bt10,1e-12))==PN2S_TIMEOUT);
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection10(s,sheet,&btFreePart,1,btValues,&bt10,10))==PN2S_UNSUPPORTED);
        check(PN2S_SetRectangleGap(s,1)==0);
        check(PN2_ErrorCodeFromObject(PN2S_NestDirection10(s,sheet,parts,3,btValues,&bt10,10))==PN2S_UNSUPPORTED);
        check(PN2S_SetRectangleGap(s,0)==0);
        auto bt10Empty=PN2S_NestDirection10(s,sheet,nullptr,0,nullptr,&bt10,10);
        check(!PN2_IsObjectError(bt10Empty) && PN2_GetResultInfos(s,bt10Empty,&length,&n)==0 && n==0);
        check(PN2_GetResultInfos(s,bt10Result,&length,&n)==0 && n==3);
        PN2_Part out;
        PN2_Point pos;
        PN2_Orientation o;
        check(PN2_GetKthNestedPart(s, r, 3, &out, &pos, &o) == PN2S_INVALID);
        check(PN2_GetResultInfos(other, r, &length, &n) == PN2S_INVALID);
        check(PN2_GetResultInfos(s, r, nullptr, &n) == PN2S_INVALID);
        PN2_Part dup[] = {parts[0], parts[0]};
        check(PN2_ErrorCodeFromObject(PN2_Nest(s, sheet, dup, 2, 1)) == PN2S_INVALID);
        check(PN2_ErrorCodeFromObject(PN2_Nest(s, sheet, parts, 3, 0)) == PN2S_INVALID);
        auto free = PN2_CreateFreeOrientation(0);
        check(!PN2_IsObjectError(PN2_AddPart(s, sh, &free, 1)));
        auto shaken = PN2_Shake(s, r, 1);
        check(!PN2_IsObjectError(shaken));
        check(PN2_GetResultInfos(s, shaken, &length, &n) == 0 && n == 3);
        int qty = 1;
        auto mr = PN2_MultiNest(s, &sheet, 1, &qty, parts, 3, 10);
        check(!PN2_IsObjectError(mr));
        check(PN2_GetMultiResultInfos(s, mr, &n) == 0 && n == 1);
        PN2_Sheet outSheet;
        check(!PN2_IsObjectError(PN2_GetResult(s, mr, 0, &outSheet)) && outSheet == sheet);
        qty = 2;
        check(!PN2_IsObjectError(PN2_MultiNest(s, &sheet, 1, &qty, parts, 3, 10)));
        check(PN2S_SetRectangleGap(s, .0006) == PN2S_UNSUPPORTED);
        check(PN2_ShapeAddHole(s, sh, p, 4) == PN2S_INVALID);       // coincident hole is invalid
        check(!PN2_IsObjectError(PN2_Nest(s, sheet, parts, 3, 1))); // rejection is atomic
        check(PN2S_GetMultiUnplacedCount(s, mr, &n) == 0 && n == 0);
        check(PN2S_GetMultiUnplacedCount(other, mr, &n) == PN2S_INVALID);
        check(PN2S_GetMultiUnplacedPart(s, mr, 0, &out) == PN2S_INVALID);
        qty = 0;
        auto shortage = PN2_MultiNest(s, &sheet, 1, &qty, parts, 3, 10);
        check(!PN2_IsObjectError(shortage));
        check(PN2_GetMultiResultInfos(s, shortage, &n) == 0 && n == 0);
        check(PN2S_GetMultiUnplacedCount(s, shortage, &n) == 0 && n == 3);
        check(PN2S_GetMultiUnplacedPart(s, shortage, 2, &out) == 0 && out == parts[2]);
        qty = -1;
        check(PN2_ErrorCodeFromObject(PN2_MultiNest(s, &sheet, 1, &qty, parts, 3, 10)) == PN2S_INVALID);
        PN2_Orientation reversed{0, 30, 20};
        check(PN2_ErrorCodeFromObject(PN2_AddPart(s, sh, &reversed, 1)) == PN2S_INVALID);
        check(PN2_DeleteSession(s) == 1);
        check(PN2_DeleteSession(s) == PN2S_INVALID);
        check(PN2_GetResultInfos(s, r, &length, &n) == PN2S_INVALID);
        auto fresh = PN2_CreateSession();
        check(fresh != s);
        check(PN2_ErrorCodeFromObject(PN2_AddPart(fresh, sh, &pose, 1)) == PN2S_INVALID);
        check(!PN2_IsObjectError(PN2_AddSheet(fresh, 8.0006, 8.0000003)));
        check(PN2_ErrorCodeFromObject(PN2_AddSheet(fresh, 8, 1000000.001)) == PN2S_UNSUPPORTED);
        check(PN2_ErrorCodeFromObject(PN2_AddSheet(fresh, std::numeric_limits<double>::infinity(), 8)) == PN2S_INVALID);
        auto sh2 = PN2_AddShape(fresh, p, 4, {0, 0});
        auto oblique = PN2_CreateOrientation(13);
        auto pp = PN2_AddPart(fresh, sh2, &oblique, 1);
        auto ss = PN2_AddSheet(fresh, 8, 8);
        check(!PN2_IsObjectError(PN2_Nest(fresh, ss, &pp, 1, 10)));
        PN2_Point bow[] = {{0, 0}, {2, 2}, {0, 2}, {2, 0}};
        check(PN2_ErrorCodeFromObject(PN2_AddShape(fresh, bow, 4, {0, 0})) == PN2S_INVALID);
        auto empty = PN2_Nest(fresh, ss, nullptr, 0, 1);
        check(!PN2_IsObjectError(empty));
        check(PN2_GetResultInfos(fresh, empty, &length, &n) == 0 && n == 0 && length == 0);
        auto valid = PN2_AddPart(fresh, sh2, &pose, 1);
        PN2S_MultiStrategyOptions multi{sizeof(PN2S_MultiStrategyOptions),1,0,0,8,8,16,0,0,.5,.05,.3,.3,0,0,1,0};
        auto multiRun=[&](const PN2S_MultiStrategyOptions *options){return PN2S_NestMultiStrategy(fresh,ss,&valid,1,options,10);};
        check(PN2_ErrorCodeFromObject(multiRun(nullptr))==PN2S_INVALID);
        auto multiGood=multiRun(&multi);check(!PN2_IsObjectError(multiGood));
        check(PN2_GetResultInfos(fresh,multiGood,&length,&n)==0 && n==1);
        auto multiBad=multi;multiBad.struct_size--;check(PN2_ErrorCodeFromObject(multiRun(&multiBad))==PN2S_INVALID);
        for(int batch:{0,-1,3334}){multiBad=multi;multiBad.context_batch_238=batch;check(PN2_ErrorCodeFromObject(multiRun(&multiBad))==PN2S_INVALID);}
        for(int ref:{-1,3}){multiBad=multi;multiBad.current_reference_kind=ref;check(PN2_ErrorCodeFromObject(multiRun(&multiBad))==PN2S_INVALID);}
        multiBad=multi;multiBad.replay_previous=2;check(PN2_ErrorCodeFromObject(multiRun(&multiBad))==PN2S_INVALID);
        multiBad=multi;multiBad.alpha_limit_260=std::numeric_limits<double>::quiet_NaN();check(PN2_ErrorCodeFromObject(multiRun(&multiBad))==PN2S_INVALID);
        multiBad=multi;multiBad.perimeter_probability_1d8=1.1;check(PN2_ErrorCodeFromObject(multiRun(&multiBad))==PN2S_INVALID);
        multiBad=multi;multiBad.fixed_front=-1;check(PN2_ErrorCodeFromObject(multiRun(&multiBad))==PN2S_INVALID);
        check(PN2_ErrorCodeFromObject(PN2S_NestMultiStrategy(fresh,ss,&pp,1,&multi,10))==PN2S_UNSUPPORTED);
        check(PN2_ErrorCodeFromObject(PN2S_NestMultiStrategy(fresh,ss,&valid,1,&multi,1e-12))==PN2S_TIMEOUT);
        check(PN2_GetResultInfos(fresh,multiGood,&length,&n)==0 && n==1);
        auto multiEmpty=PN2S_NestMultiStrategy(fresh,ss,nullptr,0,&multi,10);
        check(PN2_GetResultInfos(fresh,multiEmpty,&length,&n)==0 && n==0 && length==0);
        check(PN2_GetNestedPart(fresh, empty, valid, &pos, &o) == 0);
        auto once = PN2_Nest(fresh, ss, &valid, 1, 10);
        check(PN2_GetNestedPart(fresh, once, valid, &pos, &o) == 1);
        const auto originalOnce = snapshotResult(fresh, once);
        auto compacted = PN2_Compact(fresh, once, 0, 1);
        check(!PN2_IsObjectError(compacted));
        check(PN2_GetResultInfos(fresh, compacted, &length, &n) == 0 && n == 1);
        check(PN2_ErrorCodeFromObject(PN2_Shake(other, once, 1)) == PN2S_INVALID);
        check(PN2_ErrorCodeFromObject(PN2_Compact(fresh, once, 1, 0)) == PN2S_INVALID);
        check(PN2_ErrorCodeFromObject(PN2_Shake(fresh, once, 1e-12)) == PN2S_TIMEOUT);
        check(snapshotResult(fresh, once) == originalOnce);
        auto compactEmpty = PN2_Compact(fresh, empty, 1, 1);
        check(PN2_GetResultInfos(fresh, compactEmpty, &length, &n) == 0 && n == 0 && length == 0);
        check(PN2_ErrorCodeFromObject(PN2_Nest(fresh, ss, &valid, 1, 1e-12)) == PN2S_TIMEOUT);
        check(PN2_GetResultInfos(fresh, once, &length, &n) == 0 &&
              n == 1); // earlier results survive a failed solve
        PN2_Part repeated[6];
        check(PN2_AddSeveralParts(fresh, sh2, &pose, 1, 6, repeated) == 0);
        // Exact replay belongs to the explicit deterministic single-pass route.
        check(PN2S_SetStrategy(fresh, PN2S_MAP_GREEDY) == 0);
        std::vector<std::array<double, 4>> baseline;
        for (int repeat = 0; repeat < 8; repeat++) {
            auto result = PN2_Nest(fresh, ss, repeated, 6, 10);
            check(!PN2_IsObjectError(result));
            check(PN2_GetResultInfos(fresh, result, &length, &n) == 0 && n == 6);
            std::vector<std::array<double, 4>> snapshot;
            for (int i = 0; i < n; i++) {
                check(PN2_GetKthNestedPart(fresh, result, i, &out, &pos, &o) == 0);
                int instance = -1;
                for (int j = 0; j < 6; j++)
                    if (out == repeated[j])
                        instance = j;
                snapshot.push_back({double(instance), pos.x, pos.y, o.min_angle});
            }
            if (repeat == 0)
                baseline = snapshot;
            else
                check(snapshot == baseline);
            const auto original = snapshotResult(fresh, result);
            for (int operation = 0; operation < 3; ++operation) {
                const auto optimized = operation == 0 ? PN2_Shake(fresh, result, 1)
                    : PN2_Compact(fresh, result, operation - 1, 1);
                check(!PN2_IsObjectError(optimized) && optimized != result);
                check(snapshotResult(fresh, result) == original);
                const auto optimizedSnapshot = snapshotResult(fresh, optimized);
                check(optimizedSnapshot.parts == original.parts);
                const auto again = PN2_Shake(fresh, optimized, 1);
                check(!PN2_IsObjectError(again) && again != optimized && again != result);
                check(snapshotResult(fresh, optimized) == optimizedSnapshot);
                check(snapshotResult(fresh, result) == original);
                check(PN2_ErrorCodeFromObject(PN2_Compact(fresh, optimized, 1, 1e-12)) == PN2S_TIMEOUT);
                check(PN2_ErrorCodeFromObject(PN2_Shake(fresh, optimized, std::nan(""))) == PN2S_INVALID);
                check(snapshotResult(fresh, optimized) == optimizedSnapshot);
                check(snapshotResult(fresh, result) == original);
            }
        }
        check(PN2S_SetStrategy(fresh, PN2S_RECTANGLE_RECURSIVE) == 0);
        auto rot = PN2_CreateOrientation(90);
        PN2_Point thin[] = {{0, 0}, {1.000001, 0}, {1.000001, 1}, {0, 1}};
        auto thinShape = PN2_AddShape(fresh, thin, 4, {0, 0});
        auto thinPart = PN2_AddPart(fresh, thinShape, &rot, 1);
        check(!PN2_IsObjectError(PN2_Nest(fresh, ss, &thinPart, 1, 10)));
        check(PN2_ShapeSetProtectionOffset(fresh, sh2, 1) == 0);
        check(!PN2_IsObjectError(PN2_Nest(fresh, ss, &valid, 1, 10)));
        check(PN2_ShapeSetProtectionOffset(fresh, sh2, -1) == PN2S_INVALID);
        check(PN2_ShapeSetProtectionOffset(fresh, sh2, std::nan("")) == PN2S_INVALID);
        check(PN2_ShapeSetProtectionOffset(fresh, sh2, 0) == 0);
        check(!PN2_IsObjectError(PN2_Nest(fresh, ss, &valid, 1, 10)));
        check(PN2_IsObjectError(nullptr));
        check(PN2_ErrorCodeFromObject(nullptr) == PN2S_INVALID);
        PN2_DeleteSession(fresh);
        PN2_DeleteSession(other);
        checkLOffcutApi();
        checkConcurrentSessionDestruction();
        std::cout << checks << " API checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
