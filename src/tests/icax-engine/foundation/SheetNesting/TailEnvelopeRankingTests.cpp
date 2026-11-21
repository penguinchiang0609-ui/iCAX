#include "SheetNesting.h"
#include "NestingExecution.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <type_traits>

using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    Part Box(std::string id,double w,double h,std::size_t quantity=1,std::string material="m")
    {Part p;p.ID=std::move(id);p.Material=std::move(material);p.Quantity=quantity;p.FreeRotation=false;
        p.AllowedAnglesDegrees={0,90};p.Geometry.Outer={{0,0},{w,0},{w,h},{0,h}};return p;}
    Placement At(std::string id,std::size_t instance,double x,double y,double angle=0)
    {return {std::move(id),instance,{x,y,angle},0,false};}
    Layout Sheet(const Stock& stock,std::size_t instance,std::vector<Placement> placements)
    {return {stock.ID,instance,stock.Width,stock.Height,std::move(placements),0};}
    Result Complete(std::vector<Layout> layouts)
    {Result r;r.State=Status::Complete;r.Layouts=std::move(layouts);return r;}
    JobResult Assessed(const Problem& p,Result result)
    {
        JobResult job;job.Output=std::move(result);Detail::AssessCandidate(p,job);
        if(!job.Checked.Valid)for(const auto& error:job.Checked.Errors)std::cerr<<error<<'\n';
        Require(job.Checked.Valid&&job.Checked.Complete&&job.Remnants.Measured,"ranking fixture failed complete original assessment");
        return job;
    }
    auto Key(const Problem& p,const JobResult& job,RankingPolicy policy=RankingPolicy::RegularRemnant)
    {return Detail::Quality(job,p,policy);}
    std::vector<Placement> Row(std::size_t first,std::size_t count,double step=5.)
    {std::vector<Placement> result;for(std::size_t i=0;i<count;++i)result.push_back(At("part",first+i,static_cast<double>(i)*step,0));return result;}
    Problem ReadProblem(const std::filesystem::path& path)
    {
        std::ifstream in(path);std::string tag;std::size_t np=0,ns=0;in>>tag>>np>>ns;
        Require(tag=="ICAX_NESTING_PROBLEM_1","invalid real-fixture Problem header");Problem p;
        in>>p.PartGap>>p.EdgeMargin>>p.Tolerance;p.Parts.resize(np);p.Stocks.resize(ns);
        for(auto& part:p.Parts)
        {
            std::size_t na=0,nv=0,nh=0;in>>std::quoted(part.ID)>>part.Quantity>>std::quoted(part.Material)>>part.FreeRotation>>na;
            part.AllowedAnglesDegrees.resize(na);for(auto& a:part.AllowedAnglesDegrees)in>>a;
            in>>part.Geometry.Radius>>part.Geometry.Center.X>>part.Geometry.Center.Y>>nv>>nh;
            part.Geometry.Outer.resize(nv);part.Geometry.Holes.resize(nh);
            for(auto& q:part.Geometry.Outer)in>>q.X>>q.Y;
            for(auto& hole:part.Geometry.Holes){in>>nv;hole.resize(nv);for(auto& q:hole)in>>q.X>>q.Y;}
        }
        for(auto& stock:p.Stocks)in>>std::quoted(stock.ID)>>stock.Width>>stock.Height>>stock.Quantity>>stock.Cost>>std::quoted(stock.Material);
        Require(bool(in),"truncated real-fixture Problem");return p;
    }
    Result ReadLayouts(const std::filesystem::path& path)
    {
        std::ifstream in(path);std::string tag;std::size_t count=0;in>>tag>>count;
        Require(tag=="ICAX_NESTING_LAYOUTS_1","invalid real-fixture layout header");Result r;r.State=Status::Complete;r.Layouts.resize(count);
        for(auto& l:r.Layouts)
        {std::size_t n=0;in>>std::quoted(l.StockID)>>l.StockInstance>>l.Width>>l.Height>>n;l.Placements.resize(n);
            for(auto& q:l.Placements)in>>std::quoted(q.PartID)>>q.Instance>>q.Transform.X>>q.Transform.Y>>q.Transform.AngleDegrees;}
        Require(bool(in),"truncated real-fixture layouts");return r;
    }
    void RequireFinite(const Detail::QualityKey& key)
    {
        const auto check=[](const auto& value)
        {if constexpr(std::is_arithmetic_v<std::decay_t<decltype(value)>>)Require(std::isfinite(static_cast<double>(value)),"quality has nonfinite scalar");
            else for(const auto x:value)Require(std::isfinite(x),"quality has nonfinite vector term");};
        std::apply([&](const auto&... value){(check(value),...);},key);
    }
}

int main()try
{
    // Same complete 2200 material, two sheets. The last200/bbox250=80%
    // arrangement also leaves the larger certified reusable rectangle.
    Problem p;p.Parts={Box("part",5,20,22)};p.Stocks={{"stock",100,20,2,1,"m"}};
    auto compactFront=Complete({Sheet(p.Stocks[0],0,Row(0,20)),Sheet(p.Stocks[0],1,Row(20,2,7.5))});
    auto denseTail=Complete({Sheet(p.Stocks[0],0,Row(0,16)),Sheet(p.Stocks[0],1,Row(16,6))});
    const auto lessTail=Assessed(p,compactFront),moreTail=Assessed(p,denseTail);
    const auto lessKey=Key(p,lessTail),moreKey=Key(p,moreTail);
    Require(std::abs(std::get<7>(lessKey)-200)<.001&&std::abs(std::get<7>(moreKey)-600)<.001&&
        std::abs(std::get<8>(lessKey)+.8)<1e-5&&std::get<8>(moreKey)==-1,
        "density-trap fixture did not exercise the original requested material/bbox quantities");
    Require(lessTail.Remnants.LastSheetRegularArea>moreTail.Remnants.LastSheetRegularArea&&lessKey<moreKey,
        "tail fill displaced the larger certified reusable rectangle");
    std::cout<<"PASS larger certified tail remnant beats denser occupied-box fill\n";

    // With unequal stock sizes, equal tail free area and equal front fill do
    // not establish equal tail material. The explicit material slot is needed.
    // A dyadic tolerance makes the existing quantized 800/1600 and 600/1200
    // front ratios exactly equal, so only the newly added material slot wins.
    p={};p.Tolerance=1./1048576.;p.Parts={Box("part",10,10,10)};
    p.Stocks={{"large",80,20,1,1,"m"},{"middle",60,20,1,1,"m"},{"tail",70,20,1,1,"m"},
        {"unused",2048,2048,1,1,"m"}};
    // This unused inventory makes the shared area quantum exactly 1/256:
    // both certified ~700 rectangles tie despite conservative expansion tails.
    // It contributes neither a logical tail nor any reusable-area score.
    std::vector<Placement> eight,six;
    for(std::size_t i=0;i<8;++i)eight.push_back(At("part",i,10.*static_cast<double>(i%4),10.*static_cast<double>(i/4)));
    for(std::size_t i=0;i<6;++i)six.push_back(At("part",i,10.*static_cast<double>(i%3),10.*static_cast<double>(i/3)));
    const auto smallTail=Assessed(p,Complete({Sheet(p.Stocks[0],0,eight),
        Sheet(p.Stocks[1],0,{At("part",8,0,0),At("part",9,15,0)})}));
    const auto bigTail=Assessed(p,Complete({Sheet(p.Stocks[1],0,six),
        Sheet(p.Stocks[2],0,{At("part",6,0,0),At("part",7,10,0),At("part",8,20,0),At("part",9,30,0)})}));
    const auto smallKey=Key(p,smallTail),bigKey=Key(p,bigTail);
    Require(std::get<4>(smallKey)==std::get<4>(bigKey)&&std::get<5>(smallKey)==std::get<5>(bigKey)&&std::get<6>(smallKey)==std::get<6>(bigKey),
        "unequal-stock fixture did not tie certified remnant and consolidation priorities");
    Require(std::abs(std::get<7>(smallKey)-200)<.001&&std::abs(std::get<7>(bigKey)-400)<.001&&smallKey<bigKey,
        "different stock dimensions let denser but more heavily loaded tail win");
    std::cout<<"PASS equal free area on unequal stocks still minimizes tail material before fill\n";

    // Same original tail material: the larger certified reusable rectangle
    // precedes density. Explicit CompactArea retains its smaller-box choice.
    p={};p.Parts={Box("part",80,20,2)};p.Stocks={{"stock",200,100,1,1,"m"}};
    const auto packed=Assessed(p,Complete({Sheet(p.Stocks[0],0,{At("part",0,0,0),At("part",1,0,20)})}));
    const auto lower=Assessed(p,Complete({Sheet(p.Stocks[0],0,{At("part",0,0,0),At("part",1,90,0)})}));
    Require(packed.Remnants.LastSheetRegularArea<lower.Remnants.LastSheetRegularArea,
        "equal-material fixture did not conflict with old regular-remnant priority");
    Require(Key(p,lower)<Key(p,packed),"smaller occupied AABB displaced a larger certified reusable rectangle");
    Require(Key(p,lower,RankingPolicy::LowThenLeft)<Key(p,packed,RankingPolicy::LowThenLeft),
        "explicit low-then-left policy priority was changed");
    Require(Key(p,packed,RankingPolicy::CompactArea)<Key(p,lower,RankingPolicy::CompactArea),"explicit compact-area priority was changed");
    Require(std::get<7>(Key(p,packed,RankingPolicy::LowThenLeft))==0&&std::get<8>(Key(p,packed,RankingPolicy::CompactArea))==0&&
        std::get<2>(Key(p,packed,RankingPolicy::LowThenLeft))==0&&!std::get<3>(Key(p,packed,RankingPolicy::LowThenLeft))&&std::get<4>(Key(p,packed,RankingPolicy::CompactArea))==0,
        "new default-only priorities leaked into explicit policies");
    std::cout<<"PASS same material favors usable remnant before AABB; explicit policies preserved\n";

    p={};p.Parts={Box("part",10,10,2)};p.Stocks={{"stock",100,100,2,1,"m"}};
    const auto one=Assessed(p,Complete({Sheet(p.Stocks[0],0,{At("part",0,0,0),At("part",1,90,90)})}));
    const auto two=Assessed(p,Complete({Sheet(p.Stocks[0],0,{At("part",0,0,0)}),Sheet(p.Stocks[0],1,{At("part",1,0,0)})}));
    Require(one.Remnants.LastSheetRegularArea<two.Remnants.LastSheetRegularArea&&std::get<8>(Key(p,one))>std::get<8>(Key(p,two))&&Key(p,one)<Key(p,two),
        "higher box fill rewarded using another physical sheet");
    std::cout<<"PASS one sheet beats two even when its box fill is lower\n";

    // Net material subtracts holes; source-space origins and quarter turns
    // cannot inflate the occupied world AABB or turn a frame into solid area.
    p={};auto frame=Box("frame",10,10);
    frame.Geometry.Outer={{10,20},{20,20},{20,30},{10,30}};
    frame.Geometry.Holes={{{11,21},{19,21},{19,29},{11,29}}};
    p.Parts={frame,Box("filler",1,1)};p.Stocks={{"stock",20,20,1,1,"m"}};
    const auto holed=Assessed(p,Complete({Sheet(p.Stocks[0],0,{At("frame",0,30,-10,90),At("filler",0,4,4)})}));
    Require(std::abs(std::get<7>(Key(p,holed))-37)<1e-8&&std::abs(std::get<8>(Key(p,holed))+.37)<1e-8,
        "tail density counted hole area, a local source offset, or a rotated AABB incorrectly");
    std::cout<<"PASS tail AABB fill uses net original material with holes, source origins and rotation\n";

    // One tail per compatible material. Aggregate is total material / sum of
    // separate occupied AABBs: 600/(200+800)=60%, not a fictitious shared box.
    p={};p.Parts={Box("steel",10,10,2,"steel"),Box("aluminium",20,10,2,"aluminium")};
    p.Stocks={{"S",100,100,1,1,"steel"},{"A",100,100,1,1,"aluminium"}};
    const auto multiResult=Complete({Sheet(p.Stocks[0],0,{At("steel",0,0,0),At("steel",1,10,0)}),
        Sheet(p.Stocks[1],0,{At("aluminium",0,0,0),At("aluminium",1,60,0)})});
    const auto multi=Assessed(p,multiResult);
    Require(multi.Remnants.FinalLayoutIndices.size()==2&&std::abs(std::get<7>(Key(p,multi))-600)<1e-8&&
        std::abs(std::get<8>(Key(p,multi))+.6)<1e-8&&std::abs(std::get<4>(Key(p,multi))+multi.Remnants.LastSheetRegularArea)<.01,
        "different material tails were combined into a shared physical box or scored using preceding rectangles");
    auto renamed=p;renamed.Parts[0].Material=renamed.Stocks[0].Material="z";
    renamed.Parts[1].Material=renamed.Stocks[1].Material="a";
    const auto equivalent=Assessed(renamed,multiResult);
    Require(Key(p,multi)==Key(renamed,equivalent),"material identity labels became ranking priorities");
    std::cout<<"PASS separate compatible-material tails aggregate independent source AABBs without name priority\n";

    // Actual public TG004 A/B: same original 36 pieces and 12 stock sheets.
    // A leaves 188000 usable; B puts less net material on its tail but scatters
    // the residual and certifies only 82550. The default must select A.
    const auto fixture=std::filesystem::path(__FILE__).parent_path()/"data"/"tg004-remnant-priority";
    const auto real=ReadProblem(fixture/"original.problem");
    const auto actualA=Assessed(real,ReadLayouts(fixture/"A.layouts")),actualB=Assessed(real,ReadLayouts(fixture/"B.layouts"));
    const auto qa=Key(real,actualA),qb=Key(real,actualB);
    Require(actualA.Checked.PlacedCount==36&&actualB.Checked.PlacedCount==36&&actualA.Output.Layouts.size()==12&&actualB.Output.Layouts.size()==12,
        "TG004 real counterexample changed original complete demand or sheets");
    Require(std::abs(std::get<7>(qa)-812000)<.01&&std::abs(std::get<7>(qb)-791410)<.01&&
        std::abs(actualA.Remnants.LastSheetRegularArea-188000)<.01&&std::abs(actualB.Remnants.LastSheetRegularArea-82550)<.01&&qa<qb,
        "TG004 default did not select the original larger certified regular remnant");
    for(const auto policy:{RankingPolicy::LowThenLeft,RankingPolicy::CompactArea})
        Require(Key(real,actualB,policy)<Key(real,actualA,policy),"TG004 explicit policy lost historical material-consolidation priority");
    std::cout<<std::setprecision(17)<<"PASS actual TG004: A material=812000 remnant="<<actualA.Remnants.LastSheetRegularArea
        <<" beats B material=791410 remnant="<<actualB.Remnants.LastSheetRegularArea<<"; 36 originals, 12 sheets\n";

    // Score only selected logical tails: B has a larger sum over all sheets,
    // while A's actual tail leaves the larger single certified rectangle.
    p={};p.Parts={Box("part",10,10,9)};p.Stocks={{"stock",100,100,2,1,"m"}};
    std::vector<Placement> scattered,corner;
    const std::vector<Point> sites{{20,20},{45,20},{70,20},{20,45},{70,45},{20,70},{45,70},{70,70}};
    for(std::size_t i=0;i<8;++i){scattered.push_back(At("part",i,sites[i].X,sites[i].Y));corner.push_back(At("part",i,10.*(i%4),10.*(i/4)));}
    const auto tailA=Assessed(p,Complete({Sheet(p.Stocks[0],0,scattered),Sheet(p.Stocks[0],1,{At("part",8,0,0)})}));
    const auto tailB=Assessed(p,Complete({Sheet(p.Stocks[0],0,corner),Sheet(p.Stocks[0],1,{At("part",8,45,45)})}));
    const auto sum=[](const JobResult& job){double s=0;for(const auto& r:job.Remnants.PerLayout)s+=r.Area;return s;};
    Require(tailA.Remnants.LastSheetRegularArea>tailB.Remnants.LastSheetRegularArea&&sum(tailA)<sum(tailB)&&Key(p,tailA)<Key(p,tailB),
        "preceding boards' regular-rectangle sum displaced the selected logical tail");
    Require(tailA.Remnants.FinalLayoutIndices.size()==1&&tailA.Output.Layouts[tailA.Remnants.FinalLayoutIndices[0]].StockInstance==1,
        "new ranking changed logical tail or original stock identity");
    std::cout<<"PASS selected logical tail wins despite worse all-board rectangle sum\n";

    // Same missing count/sheet count: more placed actual material precedes
    // any rectangle, including when that better partial has no measurement.
    p={};p.Parts={Box("large",20,20),Box("small",10,10)};p.Stocks={{"stock",100,100,2,1,"m"}};
    Result largePartial;largePartial.State=Status::Partial;largePartial.Layouts={Sheet(p.Stocks[0],0,{At("large",0,40,40)})};largePartial.Remaining={{"small",1}};
    Result smallPartial;smallPartial.State=Status::Partial;smallPartial.Layouts={Sheet(p.Stocks[0],0,{At("small",0,0,0)})};smallPartial.Remaining={{"large",1}};
    JobResult largeJob,smallJob;largeJob.Output=largePartial;smallJob.Output=smallPartial;Detail::AssessCandidate(p,largeJob);Detail::AssessCandidate(p,smallJob);
    Require(largeJob.Checked.Valid&&smallJob.Checked.Valid&&!largeJob.Checked.Complete&&!smallJob.Checked.Complete&&
        largeJob.Remnants.LastSheetRegularArea<smallJob.Remnants.LastSheetRegularArea&&Key(p,largeJob)<Key(p,smallJob),
        "partial was rewarded for omitting more original material");
    auto unknownPartial=largeJob;unknownPartial.Remnants.Measured=false;
    Require(Key(p,unknownPartial)<Key(p,smallJob),"availability flag displaced partial original-material completion");
    const auto complete=Assessed(p,Complete({Sheet(p.Stocks[0],0,{At("large",0,0,0)}),Sheet(p.Stocks[0],1,{At("small",0,0,0)})}));
    Require(Key(p,complete)<Key(p,largeJob),"fewer sheets rewarded incomplete original demand");
    std::cout<<"PASS partial original material and complete demand precede remnant benefits\n";

    // Unavailable, nonfinite or negative scores are explicit unknowns, never
    // an infinity/NaN ordering trick or a fictitious usable rectangle.
    p={};p.Parts={Box("part",100,100)};p.Stocks={{"stock",100,100,1,1,"m"}};
    const auto zero=Assessed(p,Complete({Sheet(p.Stocks[0],0,{At("part",0,0,0)})}));
    Require(zero.Remnants.LastSheetRegularArea==0,"zero-certificate fixture is not full");
    for(const auto score:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),1e308})
    {
        auto unknown=zero;unknown.Remnants.Measured=false;unknown.Remnants.LastSheetRegularArea=score;
        RequireFinite(Key(p,unknown));Require(std::get<3>(Key(p,unknown))&&std::get<4>(Key(p,unknown))==0&&Key(p,zero)<Key(p,unknown),
            "unmeasured rectangle beat a certified zero or left nonfinite key terms");
        if(score<0||!std::isfinite(score)||score==1e308)
        {unknown.Remnants.Measured=true;RequireFinite(Key(p,unknown));Require(std::get<3>(Key(p,unknown))&&Key(p,zero)<Key(p,unknown),"invalid measured remnant was treated as a usable certificate");}
        for(const auto policy:{RankingPolicy::LowThenLeft,RankingPolicy::CompactArea})
            Require(Key(p,unknown,policy)==Key(p,zero,policy),"unavailable-remnant terms leaked into explicit policies");
    }
    std::cout<<"PASS certified zero beats unavailable/invalid measurement with finite keys; explicit policies unchanged\n";
    return 0;
}
catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
