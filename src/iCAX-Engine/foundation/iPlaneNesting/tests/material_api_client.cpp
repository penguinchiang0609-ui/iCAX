#include "alma_study.h"
#include "clipper2/clipper.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace clip=Clipper2Lib;
using Ring=std::vector<PN2_Point>;
size_t checks=0,cases=0,verifiedPlacements=0,continuousInterior=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(std::string(message)+": "+PN2S_LastErrorDetail());}
void code(int value,const char *message){check(value>=0,message);}
template<class T>T object(T value,const char *message){check(!PN2_IsObjectError(value),message);return value;}
Ring rectangle(double w,double h,double x=0,double y=0){return {{x,y},{x+w,y},{x+w,y+h},{x,y+h}};}
Ring circle(double x,double y,double radius,int count){
    Ring result;for(int i=0;i<count;++i){const double angle=2*std::acos(-1.)*i/count;
        result.push_back({x+radius*std::cos(angle),y+radius*std::sin(angle)});}return result;
}
struct PartData {Ring outer;std::vector<Ring> holes;PN2_Point pivot{};std::vector<PN2_Orientation> poses{{0,0,0}};};
struct StockData {Ring outer;std::vector<Ring> holes;double margin=0;std::vector<double> offsets;};
struct Row {PN2_Part id;double x,y,angle;int flip;bool operator==(const Row &)const=default;};
struct Snapshot {double length=0;std::vector<Row> rows;bool operator==(const Snapshot &)const=default;};
struct Fixture {
    PN2_Session session=object(PN2_CreateSession(),"Create C++ SDK session");
    std::map<PN2_Part,PartData> parts;
    std::map<PN2_Sheet,StockData> stocks;
    ~Fixture(){PN2_DeleteSession(session);}
    PN2_Sheet sheet(StockData stock){
        const auto result=object(PN2_AddPolygonalSheetWithBorderGap(session,stock.outer.data(),int(stock.outer.size()),stock.margin),"Standard polygon-stock creation");
        for(size_t i=0;i<stock.holes.size();++i)code(PN2_SheetAddDefectWithOffset(session,result,stock.holes[i].data(),int(stock.holes[i].size()),stock.offsets[i]),"Standard defect creation");
        stocks.emplace(result,std::move(stock));return result;
    }
    std::vector<PN2_Part> add(PartData part,int count,bool typed=false){
        const auto shape=object(PN2_AddShape(session,part.outer.data(),int(part.outer.size()),part.pivot),"Create actual part geometry");
        for(const auto &hole:part.holes)code(PN2_ShapeAddHole(session,shape,hole.data(),int(hole.size())),"Create actual part hole");
        std::vector<PN2_Part> ids(size_t(count),nullptr);
        code(typed?PN2S_AddMaterialParts(session,shape,part.poses.data(),int(part.poses.size()),count,ids.data()):
            PN2_AddSeveralParts(session,shape,part.poses.data(),int(part.poses.size()),count,ids.data()),"Create concrete SDK instances");
        for(const auto id:ids)parts.emplace(id,part);return ids;
    }
    Snapshot snapshot(PN2_Result result){
        Snapshot out;int count=0;code(PN2_GetResultInfos(session,result,&out.length,&count),"Read actual result count");
        for(int i=0;i<count;++i){PN2_Part id=nullptr;PN2_Point point{};PN2_Orientation pose{};
            code(PN2_GetKthNestedPart(session,result,i,&id,&point,&pose),"Read actual SDK identity and orientation");
            check(pose.min_angle==pose.max_angle,"Published material orientation is the actual solved angle");
            out.rows.push_back({id,point.x,point.y,pose.min_angle,pose.horizontal_flip});}
        return out;
    }
};
PN2S_MaterialResultInfo information(PN2_Session session,const void *result){
    PN2S_MaterialResultInfo info{};info.struct_size=sizeof(info);code(PN2S_GetMaterialResultInfos(session,result,&info),"Read immutable typed material metadata");return info;
}
clip::PathD clipRing(const Ring &ring,bool positive){
    clip::PathD path;for(const auto p:ring)path.push_back({p.x,p.y});
    if((clip::Area(path)>0)!=positive)std::reverse(path.begin(),path.end());return path;
}
clip::PathsD paths(const Ring &outer,const std::vector<Ring> &holes){
    clip::PathsD out{clipRing(outer,true)};for(const auto &hole:holes)out.push_back(clipRing(hole,false));return out;
}
double pointSegment(PN2_Point p,PN2_Point a,PN2_Point b){
    const double dx=b.x-a.x,dy=b.y-a.y,denom=dx*dx+dy*dy;
    const double t=denom?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/denom,0.,1.):0.;
    return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
}
double cross(PN2_Point a,PN2_Point b,PN2_Point c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
double segmentDistance(PN2_Point a,PN2_Point b,PN2_Point c,PN2_Point d){
    const double ab1=cross(a,b,c),ab2=cross(a,b,d),cd1=cross(c,d,a),cd2=cross(c,d,b);
    if(((ab1<=0&&ab2>=0)||(ab1>=0&&ab2<=0))&&((cd1<=0&&cd2>=0)||(cd1>=0&&cd2<=0))&&
        std::max(std::min(a.x,b.x),std::min(c.x,d.x))<=std::min(std::max(a.x,b.x),std::max(c.x,d.x))&&
        std::max(std::min(a.y,b.y),std::min(c.y,d.y))<=std::min(std::max(a.y,b.y),std::max(c.y,d.y)))return 0;
    return std::min({pointSegment(a,c,d),pointSegment(b,c,d),pointSegment(c,a,b),pointSegment(d,a,b)});
}
double distance(const std::vector<Ring> &first,const std::vector<Ring> &second){
    double result=std::numeric_limits<double>::infinity();
    for(const auto &a:first)for(const auto &b:second)for(size_t i=0;i<a.size();++i)for(size_t j=0;j<b.size();++j)
        result=std::min(result,segmentDistance(a[i],a[(i+1)%a.size()],b[j],b[(j+1)%b.size()]));return result;
}
std::vector<Ring> world(const PartData &part,const Row &row){
    const auto angle=row.angle*std::acos(-1.)/180.,c=std::cos(angle),s=std::sin(angle);std::vector<Ring> rings{part.outer};
    rings.insert(rings.end(),part.holes.begin(),part.holes.end());
    for(auto &ring:rings)for(auto &p:ring){const double x=p.x-part.pivot.x,y=p.y-part.pivot.y;
        p={c*x-s*y+row.x,(row.flip?-1.:1.)*(s*x+c*y)+row.y};}return rings;
}
void independent(Fixture &fixture,PN2_Sheet sheet,const Snapshot &result,double gap){
    const auto &stock=fixture.stocks.at(sheet);const auto stockPaths=paths(stock.outer,stock.holes);
    std::vector<clip::PathsD> oldPaths;std::vector<std::vector<Ring>> oldRings;std::set<PN2_Part> seen;
    for(const auto &row:result.rows){
        check(fixture.parts.contains(row.id)&&seen.insert(row.id).second,"Concrete part is known and appears once");
        const auto &part=fixture.parts.at(row.id);
        check(std::any_of(part.poses.begin(),part.poses.end(),[&](auto pose){return pose.horizontal_flip==row.flip&&
            row.angle>=pose.min_angle-1e-8&&row.angle<=pose.max_angle+1e-8;}),"Actual angle and reflection obey a supplied source constraint");
        const auto rings=world(part,row);auto material=paths(rings.front(),{rings.begin()+1,rings.end()});
        check(std::abs(clip::Area(clip::Difference(material,stockPaths,clip::FillRule::NonZero,8)))<=1e-6,"Independent clipping finds no material outside true stock or inside defects");
        check(distance(rings,{stock.outer})>=stock.margin-1e-6,"Independent outer-boundary Euclidean margin");
        for(size_t i=0;i<stock.holes.size();++i)check(distance(rings,{stock.holes[i]})>=stock.offsets[i]-1e-6,"Independent per-defect Euclidean offset");
        for(size_t i=0;i<oldPaths.size();++i){
            check(std::abs(clip::Area(clip::Intersect(material,oldPaths[i],clip::FillRule::NonZero,8)))<=1e-6,"Independent material intersection is zero");
            check(distance(rings,oldRings[i])>=gap-1e-6,"Independent part gap");}
        oldPaths.push_back(std::move(material));oldRings.push_back(rings);++verifiedPlacements;
    }
}
void holeMarginsAndAtomicMutation(){
    Fixture f;const StockData stock{rectangle(10,10),{rectangle(2,2,4,4)},1,{0}};
    const auto sheet=f.sheet(stock);const auto ids=f.add({rectangle(8,8),{rectangle(2,2,3,3)}},1);
    const auto result=object(PN2_Nest(f.session,sheet,ids.data(),1,.5),"Native stock hole with independent outer/defect margins");
    const auto original=f.snapshot(result);check(original.rows.size()==1,"Zero defect offset is not incorrectly expanded by the outer margin");
    independent(f,sheet,original,0);const auto actual=world(f.parts.at(ids[0]),original.rows[0]);
    check(distance(actual,stock.holes)<.5,"Independent defect distance proves outer gap was not added to defect offset");
    const auto info=information(f.session,result);check(info.is_material&&info.warning_utf8,"Single result has live immutable material metadata");
    const auto outside=rectangle(2,2,20,20);
    check(PN2_SheetAddDefect(f.session,sheet,outside.data(),4)==PN2S_INVALID,"Outside defect fails atomically");
    for(const auto &invalid:std::vector<Ring>{rectangle(2,2,5,5),rectangle(.5,.5,4.5,4.5),rectangle(2,2,6,4)})
        check(PN2_SheetAddDefect(f.session,sheet,invalid.data(),int(invalid.size()))==PN2S_INVALID,"Overlapping, contained or touching new defects fail against existing defects");
    check(f.snapshot(result)==original,"Failed mutation does not change a prior result");
    const auto again=object(PN2_Nest(f.session,sheet,ids.data(),1,.5),"Live stock remains usable after rejected defect");
    check(f.snapshot(again).rows.size()==1,"Rejected defect was not attached to the live sheet");++cases;
}
void physicalStockAndMetadata(){
    Fixture f;const auto a=f.sheet({rectangle(12,8),{},1,{}}),b=f.sheet({rectangle(12,8),{},1,{}});
    const auto ids=f.add({rectangle(10,6)},2);const std::array<PN2_Sheet,2> sheets{a,b};std::array<int,2> inventory{1,1};
    const auto multi=object(PN2_MultiNest(f.session,sheets.data(),2,inventory.data(),ids.data(),2,1.),"Actual material multi-stock call");
    int count=0;code(PN2_GetMultiResultInfos(f.session,multi,&count),"Read physical board count");check(count==2&&inventory==std::array<int,2>{1,1},"Two physical stocks are used without modifying input inventory");
    std::set<PN2_Part> seen;std::set<PN2_Sheet> stockIds;std::vector<std::pair<PN2_Result,Snapshot>> previous;
    const auto meta=information(f.session,multi);const std::string warning(meta.warning_utf8);const char *stable=meta.warning_utf8;
    for(int i=0;i<count;++i){PN2_Sheet actual=nullptr;const auto result=object(PN2_GetResult(f.session,multi,i,&actual),"Read actual stock handle");
        check(stockIds.insert(actual).second&&(actual==a||actual==b),"Concrete physical stock retains the original SDK sheet ID");
        const auto snapshot=f.snapshot(result);check(snapshot.rows.size()==1,"Each stock fits exactly one required concrete part");independent(f,actual,snapshot,0);
        for(const auto &row:snapshot.rows)check(seen.insert(row.id).second,"Cross-board identities are unique");previous.push_back({result,snapshot});
        const auto boardMeta=information(f.session,result);check(boardMeta.is_material&&boardMeta.warning_utf8==stable,"Board and Multi share one immutable metadata owner");}
    check(seen==std::set<PN2_Part>(ids.begin(),ids.end()),"Multi conserves the exact concrete demand");
    const auto timed=PN2_MultiNest(f.session,sheets.data(),2,inventory.data(),ids.data(),2,1e-12);
    check(PN2_IsObjectError(timed)&&PN2_ErrorCodeFromObject(timed)==PN2S_TIMEOUT,"No evaluated candidate at a sub-clock budget remains atomic timeout");
    for(const auto &[result,snapshot]:previous)check(f.snapshot(result)==snapshot,"Later failed solve keeps old result positions and identities immutable");
    check(std::string(stable)==warning,"Subsequent SDK queries/failures do not invalidate material warning storage");
    PN2S_MaterialResultInfo sentinel{};sentinel.struct_size=sizeof(sentinel);sentinel.is_material=71;sentinel.stopped=83;sentinel.trials=991;sentinel.warning_utf8="unchanged";
    const auto before=sentinel;Fixture other;
    check(PN2S_GetMaterialResultInfos(other.session,multi,&sentinel)==PN2S_INVALID&&std::memcmp(&sentinel,&before,sizeof(sentinel))==0,"Cross-session metadata query fails without output mutation");
    PN2_Orientation pose{0,0,0};PN2_Point position{};
    check(PN2_ErrorCodeFromObject(PN2_PreNest(f.session,a,ids.data(),&pose,&position,1))==PN2S_UNSUPPORTED,"Material pre-nesting is explicitly rejected instead of using a rectangle envelope");
    check(PN2_ErrorCodeFromObject(PN2_Shake(f.session,previous[0].first,.1))==PN2S_UNSUPPORTED,"Legacy Shaker technology is explicitly rejected for material layouts");++cases;
}
void typed64AndHundredTypes(){
    Fixture f;code(PN2S_SetMaterialGap(f.session,.1234567),"Arbitrary finite material gap");
    check(PN2S_SetRectangleGap(f.session,.1234567)==PN2S_UNSUPPORTED,"Legacy strip-grid setter was not relaxed");
    PartData data{rectangle(2,1)};data.poses.clear();for(int angle=0;angle<32;++angle)for(int flip=0;flip<2;++flip)data.poses.push_back({flip,double(angle),double(angle)});
    const auto shape=object(PN2_AddShape(f.session,data.outer.data(),4,{0,0}),"Create typed extension shape");
    PN2_Part sentinel=reinterpret_cast<PN2_Part>(uintptr_t(0x1234));
    check(PN2_AddSeveralParts(f.session,shape,data.poses.data(),64,1,&sentinel)==PN2S_INVALID&&sentinel==reinterpret_cast<PN2_Part>(uintptr_t(0x1234)),"Legacy 32-pose admission and atomic output contract remain intact");
    const auto ids=f.add(data,5,true);std::vector<PN2_Sheet> sheets;std::vector<int> inventory(100);
    for(int i=0;i<100;++i)sheets.push_back(f.sheet({rectangle(12,10),{},.2,{}}));inventory.back()=1;
    const auto originalInventory=inventory;const auto multi=object(PN2_MultiNest(f.session,sheets.data(),100,inventory.data(),ids.data(),5,1.5),"100 material stock types with all 64 finite/mirror poses");
    int boards=0;code(PN2_GetMultiResultInfos(f.session,multi,&boards),"Read typed multi result");check(boards==1&&inventory==originalInventory,"Zero-stock placeholders preserve 100-type indexing and exact inventory");
    PN2_Sheet actual=nullptr;const auto result=object(PN2_GetResult(f.session,multi,0,&actual),"Read final nonzero stock type");
    check(actual==sheets.back(),"Material publication did not relabel stock100 as another sheet");const auto snapshot=f.snapshot(result);
    check(snapshot.rows.size()==5,"Typed 64-pose concrete demand is fully retained");independent(f,actual,snapshot,.1234567);++cases;
}
void actualContinuousInterval(){
    Fixture f;const double a=17.3*std::acos(-1.)/180.,c=std::cos(a),s=std::sin(a);
    StockData stock{{{s,0},{10*c+s,10*s},{10*c,10*s+c},{0,c}},{},0,{}};
    const auto sheet=f.sheet(stock);PartData part{rectangle(9,.6)};
    const auto range=PN2_CreateRangeOrientation(18,10);part.poses={range};
    const auto shape=object(PN2_AddShape(f.session,part.outer.data(),int(part.outer.size()),{0,0}),"Standard continuous shape");
    const auto id=object(PN2_AddPart(f.session,shape,&range,1),"Standard AddPart preserves its actual source interval");f.parts.emplace(id,part);
    const auto result=object(PN2_Nest(f.session,sheet,&id,1,1.),"Continuous interval on a noncardinal narrow actual polygon stock");
    const auto snapshot=f.snapshot(result);check(snapshot.rows.size()==1,"Continuous geometry places a part whose interval endpoints cannot fit");
    const auto angle=snapshot.rows[0].angle;check(angle>8+1e-4&&angle<28-1e-4&&std::abs(angle-std::round(angle/5.)*5.)>.1,"Actual solved angle is inside the source interval and outside common fixed angle tables");
    independent(f,sheet,snapshot,0);++continuousInterior;
    const int inventory=1;const auto multi=object(PN2_MultiNest(f.session,&sheet,1,&inventory,&id,1,1.),"Standard continuous source interval also reaches actual MultiNest");
    int boardCount=0;code(PN2_GetMultiResultInfos(f.session,multi,&boardCount),"Continuous Multi board count");check(boardCount==1,"Continuous Multi uses one actual physical stock");
    PN2_Sheet actual=nullptr;const auto child=object(PN2_GetResult(f.session,multi,0,&actual),"Continuous Multi board identity");check(actual==sheet,"Continuous Multi retains the supplied stock handle");
    const auto batchSnapshot=f.snapshot(child);check(batchSnapshot.rows.size()==1&&batchSnapshot.rows[0].angle>8+1e-4&&batchSnapshot.rows[0].angle<28-1e-4,"Multi publishes an actual interior continuous angle");
    independent(f,actual,batchSnapshot,0);check(f.snapshot(result)==snapshot,"A later continuous Multi call keeps the first Result immutable");++continuousInterior;++cases;
}
void lastTypedPoseReallyUsed(){
    Fixture f;const auto sheet=f.sheet({rectangle(10,3),{},.2,{}});PartData part{rectangle(8,1)};
    part.poses.assign(63,PN2_Orientation{0,90,90});part.poses.push_back({1,0,0});const auto ids=f.add(part,1,true);
    const auto result=object(PN2_Nest(f.session,sheet,ids.data(),1,.6),"Only the final supplied pose64 can fit this physical stock");
    const auto snapshot=f.snapshot(result);check(snapshot.rows.size()==1&&snapshot.rows[0].angle==0&&snapshot.rows[0].flip==1,"Material engine actually evaluates pose64 rather than truncating after pose32 or pose63");
    independent(f,sheet,snapshot,0);++cases;
}
void legacyMetadataAndUnsupportedTechnology(){
    Fixture f;code(PN2S_SetStrategy(f.session,PN2S_MAP_GREEDY),"Choose bounded legacy strategy");
    const auto ids=f.add({rectangle(2,1)},1);const auto legacy=object(PN2_AddSheet(f.session,5,8),"Legacy rectangle");
    const auto old=object(PN2_Nest(f.session,legacy,ids.data(),1,1.),"Old rectangular PN2 entry remains available");
    const auto info=information(f.session,old);check(!info.is_material&&!info.stopped&&!info.trials&&std::string(info.warning_utf8).empty(),"Legacy result is not mislabelled material");
    const auto polygon=f.sheet({rectangle(8,5),{},0,{}});code(PN2_SetCuttingHeadsNumber(f.session,2),"Configure original cutting-head setter");
    check(PN2_ErrorCodeFromObject(PN2_Nest(f.session,polygon,ids.data(),1,.2))==PN2S_UNSUPPORTED,"Material domain does not silently ignore cutting-head technology");
    code(PN2_SetCuttingHeadsNumber(f.session,1),"Restore one cutting head");code(PN2_ActivateLOffcut(f.session,1),"Configure original L-offcut flag");
    check(PN2_ErrorCodeFromObject(PN2_Nest(f.session,polygon,ids.data(),1,.2))==PN2S_UNSUPPORTED,"Material domain does not silently ignore L-offcut technology");++cases;
}
void zeroPartialConservesDemand(){
    Fixture f;Ring outer;const auto corners=rectangle(200,2000);
    for(size_t side=0;side<corners.size();++side)for(int step=0;step<250;++step){
        const auto a=corners[side],b=corners[(side+1)%corners.size()];const double t=double(step)/250.;
        outer.push_back({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t});}
    StockData stock{std::move(outer),{},0,{}};
    for(int i=0;i<192;++i){stock.holes.push_back(circle(100+.003*i,10+10*i,2,16));stock.offsets.push_back(0);}
    const auto setupStart=std::chrono::steady_clock::now();const auto sheet=f.sheet(std::move(stock));
    const double setupSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-setupStart).count();
    if(setupSeconds>=3.)std::cerr<<"192-hole setup seconds="<<setupSeconds<<'\n';
    check(setupSeconds<3.,"Strict setup of a 1000-vertex outer and 192 real holes stays within a generous three-second bound");
    std::cout<<"{\"outerVertices\":1000,\"holeCount\":192,\"holeSetupMs\":"<<setupSeconds*1000.<<"}\n";
    const auto ids=f.add({rectangle(1,1)},1);const int inventory=1;
    const auto multi=object(PN2_MultiNest(f.session,&sheet,1,&inventory,ids.data(),1,5.),"Valid many-hole stock publishes its bounded zero-layout search outcome");
    int boards=-1,missing=-1;code(PN2_GetMultiResultInfos(f.session,multi,&boards),"Read zero-partial physical board count");
    code(PN2S_GetMultiUnplacedCount(f.session,multi,&missing),"Read zero-partial exact missing demand");
    check(boards==0&&missing==1,"Zero partial consumes no stock and preserves every required instance as unplaced");
    PN2_Part actual=nullptr;code(PN2S_GetMultiUnplacedPart(f.session,multi,0,&actual),"Read real unplaced instance after bounded geometry search");
    check(actual==ids[0]&&inventory==1,"Zero partial preserves source instance and physical stock inventory");
    const auto info=information(f.session,multi);
    check(info.is_material&&info.stopped&&info.warning_utf8&&*info.warning_utf8,"Zero partial is explicitly stopped with an owned reason instead of claiming infeasibility");++cases;
}
void actualDemoDefaultBaseline(){
    Fixture f;code(PN2S_SetMaterialGap(f.session,2),"Default sample actual material gap");
    const auto sheet=f.sheet({rectangle(300,200),{},5,{}});const std::vector<PN2_Orientation> poses{{0,0,0},{0,90,90},{0,180,180},{0,270,270}};
    PartData corner{{{0,0},{70,0},{70,20},{25,20},{25,65},{0,65}}};corner.poses=poses;
    PartData frame{rectangle(65,55),{rectangle(35,25,15,15)}};frame.poses=poses;
    PartData small{rectangle(30,20)};small.poses=poses;
    auto ids=f.add(corner,8);const auto frames=f.add(frame,4),smalls=f.add(small,12);
    ids.insert(ids.end(),frames.begin(),frames.end());ids.insert(ids.end(),smalls.begin(),smalls.end());const int inventory=2;
    const auto multi=object(PN2_MultiNest(f.session,&sheet,1,&inventory,ids.data(),int(ids.size()),5.),"Actual UI default sample through the public unified SDK");
    int boards=0,missing=-1;code(PN2_GetMultiResultInfos(f.session,multi,&boards),"Default sample physical board count");
    code(PN2S_GetMultiUnplacedCount(f.session,multi,&missing),"Default sample real unplaced count");
    size_t placed=0;std::set<PN2_Part> seen;
    for(int i=0;i<boards;++i){PN2_Sheet actual=nullptr;const auto result=object(PN2_GetResult(f.session,multi,i,&actual),"Default sample actual stock identity");
        check(actual==sheet,"Default sample retains its original stock type");const auto snapshot=f.snapshot(result);placed+=snapshot.rows.size();
        independent(f,actual,snapshot,2);for(const auto &row:snapshot.rows)check(seen.insert(row.id).second,"Default sample concrete identities are unique across physical boards");}
    if(!(placed==24&&boards==1&&missing==0))std::cerr<<"default sample placed="<<placed<<" boards="<<boards<<" missing="<<missing<<'\n';
    check(placed==24&&boards==1&&missing==0&&seen==std::set<PN2_Part>(ids.begin(),ids.end()),"Actual 24-part default preserves its known one-board complete result");
    std::cout<<"{\"defaultPlaced\":"<<placed<<",\"defaultBoards\":"<<boards<<",\"defaultUnplaced\":"<<missing<<"}\n";++cases;
}
void retainedPartialMetadata(){
    Fixture f;code(PN2S_SetMaterialGap(f.session,.25),"Partial case material gap");const auto sheet=f.sheet({rectangle(60,60),{},1,{}});
    const auto ids=f.add({rectangle(5,5)},500);PN2_Result retained=nullptr;
    for(const double seconds:{.1,.3,.8}){
        const auto attempt=PN2_Nest(f.session,sheet,ids.data(),int(ids.size()),seconds);
        if(!PN2_IsObjectError(attempt)){
            if(!f.snapshot(attempt).rows.empty()){retained=attempt;break;}
            const auto info=information(f.session,attempt);
            check(info.is_material&&info.stopped&&info.warning_utf8&&*info.warning_utf8,"An empty bounded search is a labelled stopped partial outcome");continue;
        }
        check(PN2_ErrorCodeFromObject(attempt)==PN2S_TIMEOUT||PN2_ErrorCodeFromObject(attempt)==PN2S_RESOURCE_LIMIT,"Short incomplete material search has an explicit resource/time error");
    }
    check(retained!=nullptr,"A bounded material search retains a verified partial board");const auto snapshot=f.snapshot(retained);
    check(!snapshot.rows.empty()&&snapshot.rows.size()<ids.size(),"Retained partial result does not fabricate completion");const auto info=information(f.session,retained);
    if(!(info.is_material&&info.stopped&&info.warning_utf8&&*info.warning_utf8&&info.trials>0))
        std::cerr<<"partial metadata placed="<<snapshot.rows.size()<<" material="<<info.is_material<<" stopped="<<info.stopped<<" trials="<<info.trials<<" warning="<<(info.warning_utf8?info.warning_utf8:"<null>")<<'\n';
    check(info.is_material&&info.stopped&&info.warning_utf8&&*info.warning_utf8&&info.trials>0,"Partial search publishes its real stop state and owned explanation");
    const std::string warning(info.warning_utf8);code(PN2S_SetRandomSeed(f.session,99),"Subsequent unrelated SDK operation");
    check(std::string(info.warning_utf8)==warning,"Owned warning survives later session calls");independent(f,sheet,snapshot,.25);++cases;
}
}
int main(){try{
    static_assert(sizeof(PN2_Orientation)==24);static_assert(sizeof(PN2S_MaterialResultInfo)==32);
    holeMarginsAndAtomicMutation();physicalStockAndMetadata();typed64AndHundredTypes();lastTypedPoseReallyUsed();actualContinuousInterval();legacyMetadataAndUnsupportedTechnology();zeroPartialConservesDemand();actualDemoDefaultBaseline();retainedPartialMetadata();
    std::cout<<"{\"pass\":true,\"language\":\"C++\",\"cases\":"<<cases<<",\"checks\":"<<checks<<",\"independentPlacements\":"<<verifiedPlacements<<",\"continuousInteriorAngles\":"<<continuousInterior<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
