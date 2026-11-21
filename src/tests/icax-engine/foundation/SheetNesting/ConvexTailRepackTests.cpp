#include "SheetNestingConstraint.h"
#include "NestingInternal.h"
#include "NestingConvexTailIntervals.h"
#include <bit>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
    bool Has(const Result& result,const char* text)
    {for(const auto& line:result.Diagnostics)if(line.find(text)!=std::string::npos)return true;return false;}
    Part Box(std::string id,double w,double h,std::size_t quantity=1,std::string material="steel")
    {Part p;p.ID=std::move(id);p.Material=std::move(material);p.Quantity=quantity;p.FreeRotation=false;p.AllowedAnglesDegrees={0};p.Geometry.Outer={{0,0},{w,0},{w,h},{0,h}};return p;}
    void ValidateComplete(const Problem& p,Result& result)
    {
        for(auto& l:result.Layouts){l.MaterialArea=0;for(const auto& q:l.Placements)
            for(const auto& part:p.Parts)if(part.ID==q.PartID)l.MaterialArea+=Detail::Area(part.Geometry);}
        const auto checked=Validate(p,result);Require(checked.Valid&&checked.Complete,"invalid original complete fixture");result.GeometryValidated=true;
    }
    void SamePlacement(const Placement& a,const Placement& b)
    {Require(a.PartID==b.PartID&&a.Instance==b.Instance&&a.Group==b.Group&&a.GapFiller==b.GapFiller&&
       std::bit_cast<std::uint64_t>(a.Transform.X)==std::bit_cast<std::uint64_t>(b.Transform.X)&&
       std::bit_cast<std::uint64_t>(a.Transform.Y)==std::bit_cast<std::uint64_t>(b.Transform.Y)&&
       std::bit_cast<std::uint64_t>(a.Transform.AngleDegrees)==std::bit_cast<std::uint64_t>(b.Transform.AngleDegrees),"fixed original fields changed");}
    std::pair<Problem,Result> Triangles()
    {
        Problem p;p.Tolerance=1e-7;p.Stocks={{"original-steel",10,10,6,3,"steel"},{"original-copper",4,4,3,7,"copper"}};
        auto triangle=Box("arbitrary-triangle-id",10,10,2);triangle.Geometry.Outer={{0,0},{10,0},{0,10}};triangle.AllowedAnglesDegrees={0,180};
        p.Parts={triangle,Box("fixed-copper-id",4,4,1,"copper"),Box("zero-demand",1,1,0)};
        Result r;r.State=Status::Complete;r.Layouts={{"original-copper",2,4,4,{{"fixed-copper-id",0,{0,0,0},77,true}},16},
            {"original-steel",4,10,10,{{"arbitrary-triangle-id",0,{0,0,0},99,false}},50},
            {"original-steel",1,10,10,{{"arbitrary-triangle-id",1,{0,0,0},99,false}},50}};
        ValidateComplete(p,r);return {p,r};
    }
    Problem ReadProblem(const char* file)
    {
        std::ifstream in(file);std::string tag;std::size_t np=0,ns=0;in>>tag>>np>>ns;if(tag!="ICAX_NESTING_PROBLEM_1")throw std::runtime_error("bad neutral Problem version");
        Problem p;in>>p.PartGap>>p.EdgeMargin>>p.Tolerance;p.Parts.resize(np);p.Stocks.resize(ns);
        for(auto& a:p.Parts)
        {std::size_t na=0,nv=0,nh=0;in>>std::quoted(a.ID)>>a.Quantity>>std::quoted(a.Material)>>a.FreeRotation>>na;a.AllowedAnglesDegrees.resize(na);
            for(auto& angle:a.AllowedAnglesDegrees)in>>angle;in>>a.Geometry.Radius>>a.Geometry.Center.X>>a.Geometry.Center.Y>>nv>>nh;
            a.Geometry.Outer.resize(nv);a.Geometry.Holes.resize(nh);for(auto& q:a.Geometry.Outer)in>>q.X>>q.Y;
            for(auto& hole:a.Geometry.Holes){in>>nv;hole.resize(nv);for(auto& q:hole)in>>q.X>>q.Y;}}
        for(auto& s:p.Stocks)in>>std::quoted(s.ID)>>s.Width>>s.Height>>s.Quantity>>s.Cost>>std::quoted(s.Material);
        Require(bool(in),"truncated neutral Problem");return p;
    }
    Result ReadLayouts(const char* file,const Problem& p)
    {
        std::ifstream in(file);std::string tag;std::size_t n=0;in>>tag>>n;Require(tag=="ICAX_NESTING_LAYOUTS_1","bad neutral layouts version");
        Result r;r.State=Status::Complete;r.Layouts.resize(n);
        for(auto& l:r.Layouts)
        {std::size_t count=0;in>>std::quoted(l.StockID)>>l.StockInstance>>l.Width>>l.Height>>count;l.Placements.resize(count);
            for(auto& q:l.Placements)in>>std::quoted(q.PartID)>>q.Instance>>q.Transform.X>>q.Transform.Y>>q.Transform.AngleDegrees;}
        Require(bool(in),"truncated neutral layouts");ValidateComplete(p,r);return r;
    }
    void WriteLayouts(const char* file,const Result& r)
    {
        std::ofstream out(file);out<<std::setprecision(17)<<"ICAX_NESTING_LAYOUTS_1\n"<<r.Layouts.size()<<'\n';
        for(const auto& l:r.Layouts){out<<std::quoted(l.StockID)<<' '<<l.StockInstance<<' '<<l.Width<<' '<<l.Height<<' '<<l.Placements.size()<<'\n';
            for(const auto& q:l.Placements)out<<std::quoted(q.PartID)<<' '<<q.Instance<<' '<<q.Transform.X<<' '<<q.Transform.Y<<' '<<q.Transform.AngleDegrees<<'\n';}
        Require(bool(out),"failed neutral output");
    }
}
int main(int argc,char** argv)try
{
    ConvexTailRepackSettings settings;settings.MinWindowBoards=2;settings.MaxWindowBoards=2;settings.Workers=1;settings.SecondsPerWindow=.75;settings.MaxConflicts=100000;
    Options options;options.TimeLimitSeconds=5;options.MaxPlacementAttempts=20000;
    if(argc==4)
    {
        auto p=ReadProblem(argv[1]);auto before=ReadLayouts(argv[2],p);settings.MinWindowBoards=3;settings.MaxWindowBoards=8;settings.SecondsPerWindow=2;options.TimeLimitSeconds=20;
        auto after=MakeConvexTailRepackStage(settings).Factory()->Run(p,before,options);const auto check=Validate(p,after);
        for(const auto& line:after.Diagnostics)std::cout<<line<<'\n';Require(check.Valid&&check.Complete,"public original full validation failed");WriteLayouts(argv[3],after);
        std::cout<<"public-native-valid="<<check.Valid<<" complete="<<check.Complete<<" placed="<<check.PlacedCount<<" before-boards="<<before.Layouts.size()<<" after-boards="<<after.Layouts.size()
            <<" attempts="<<after.Stats.PlacementAttempts<<" attempt-limit="<<options.MaxPlacementAttempts<<" stage-wall="<<after.Stats.ElapsedSeconds<<'\n';return 0;
    }
    const auto defaults=DefaultStrategies().IDs();auto [p,before]=Triangles();const auto unchanged=before;
    using namespace ConvexTailDetail;
    Require(FloorDivide(-1,3)==-1&&CeilDivide(-1,3)==0,"signed projection endpoint division rounded toward zero");
    IntegerRange contact{0,10};Require(IntersectStrictProjection(contact,1,0,2,0,2)&&contact.Low==0&&contact.High==1&&FirstAllowedInteger(10,{contact})==2,
        "exact boundary contact was incorrectly forbidden");
    IntegerRange negative{0,10};Require(IntersectStrictProjection(negative,-1,8,10,2,4)&&negative.Low==5&&negative.High==7,"negative-normal strict interval failed");
    IntegerRange parallel{0,10};Require(!IntersectStrictProjection(parallel,0,0,2,2,4),"parallel legal contact generated a forbidden interval");
    Require(FirstAllowedInteger(10,{{2,4},{7,8}})==0&&FirstAllowedInteger(10,{{2,4},{0,1},{7,8}})==5,
        "disconnected feasible intervals were treated as monotone");
    IntegerRange fractionalContact{0,10};Require(IntersectStrictProjection(fractionalContact,3,1,3,0,4)&&fractionalContact.High==0,"strict rational endpoint was rounded outward");
    auto after=MakeConvexTailRepackStage(settings).Factory()->Run(p,before,options);const auto check=Validate(p,after);
    Require(after.GeometryValidated&&check.Valid&&check.Complete&&check.PlacedCount==3&&after.Layouts.size()==2,"original triangles did not share a sheet using material SAT");
    Require(after.Layouts[0].StockID=="original-copper"&&after.Layouts[0].StockInstance==2&&after.Layouts[0].MaterialArea==16,"fixed incompatible stock changed");
    SamePlacement(after.Layouts[0].Placements[0],before.Layouts[0].Placements[0]);
    Require(after.Layouts[1].StockID=="original-steel"&&(after.Layouts[1].StockInstance==4||after.Layouts[1].StockInstance==1),"new or foreign stock inventory was introduced");
    Require(after.Layouts[1].Placements.size()==2,"same-part original instances were lost");
    const auto& a=after.Layouts[1].Placements[0];const auto& b=after.Layouts[1].Placements[1];
    const auto aa=Detail::Bounds(p.Parts[0].Geometry,a.Transform),bb=Detail::Bounds(p.Parts[0].Geometry,b.Transform);
    Require(aa.X==bb.X&&aa.Y==bb.Y&&aa.Width==bb.Width&&aa.Height==bb.Height,"fixture did not exercise overlapping bounding boxes");
    Require(Has(after,"solver-first-solution=0")&&Has(after,"accepted=1"),"actual optimizing stage was bypassed");
    SamePlacement(before.Layouts[1].Placements[0],unchanged.Layouts[1].Placements[0]);
    Require(after.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"stage exceeded original placement verification budget");
    auto exactBudget=options;exactBudget.MaxPlacementAttempts=7;
    auto lastAttempt=MakeConvexTailRepackStage(settings).Factory()->Run(p,before,exactBudget);
    Require(lastAttempt.Layouts.size()==2&&lastAttempt.Stats.PlacementAttempts==7,"fully verified candidate on the final attempt was discarded");
    auto zeroOnly=p;zeroOnly.Parts[0].AllowedAnglesDegrees={0};auto noTurn=settings;noTurn.AllowSameSheetCount=false;
    auto forbidden=MakeConvexTailRepackStage(noTurn).Factory()->Run(zeroOnly,before,options);
    Require(Validate(zeroOnly,forbidden).Valid&&forbidden.Layouts.size()==3,"original zero-only domain widened to complementary half turn");
    Problem forward;forward.Tolerance=1e-7;forward.Stocks={{"sheet",10,10,2,1,"steel"}};
    forward.Parts={Box("A",6,10),Box("B",4,10),Box("C",2,10)};Result spread;spread.State=Status::Complete;
    spread.Layouts={{"sheet",0,10,10,{{"A",0,{0,0,0},0,false}},60},{"sheet",1,10,10,{{"B",0,{0,0,0},0,false},{"C",0,{4,0,0},0,false}},60}};ValidateComplete(forward,spread);
    auto filled=MakeConvexTailRepackStage(settings).Factory()->Run(forward,spread,options);
    Require(Validate(forward,filled).Valid&&Validate(forward,filled).Complete&&filled.Layouts.size()==2,"same-count refill lost original inventory");
    Require(filled.Layouts.back().MaterialArea==20&&filled.Layouts.front().MaterialArea==100,"same-count stage did not prioritize terminal original material");
    auto concave=p;concave.Parts[0].Geometry.Outer={{0,0},{10,0},{10,2},{2,2},{2,10},{0,10}};
    auto unsupported=MakeConvexTailRepackStage(settings).Factory()->Run(concave,before,options);
    Require(unsupported.Layouts.size()==before.Layouts.size()&&Has(unsupported,"unsupported non-strictly-convex"),"unsupported nonconvex geometry was replaced by a hull");
    auto noninteger=p;noninteger.Parts[0].Geometry.Outer[1].X=9.5;
    auto fractional=MakeConvexTailRepackStage(settings).Factory()->Run(noninteger,before,options);
    Require(Has(fractional,"unsupported noninteger original contour")&&fractional.Layouts.size()==3,"noninteger source was silently snapped");
    auto margin=p;margin.EdgeMargin=.01;auto notExpanded=MakeConvexTailRepackStage(settings).Factory()->Run(margin,before,options);
    Require(Has(notExpanded,"unsupported nonzero")&&notExpanded.Layouts.size()==3,"original edge margin was dropped");
    auto capped=settings;capped.MaxReleasedInstances=1;
    auto limit=MakeConvexTailRepackStage(capped).Factory()->Run(p,before,options);Require(limit.Layouts.size()==3&&Has(limit,"plans=0"),"release-instance cap was ignored");
    auto stop=options;stop.Cancel=[]{return true;};auto cancelled=MakeConvexTailRepackStage(settings).Factory()->Run(p,before,stop);
    Require(cancelled.State==Status::Cancelled&&cancelled.Layouts.size()==3,"cancellation exposed incomplete reconstruction");SamePlacement(cancelled.Layouts[0].Placements[0],before.Layouts[0].Placements[0]);
    auto time=options;time.TimeLimitSeconds=0;auto exhausted=MakeConvexTailRepackStage(settings).Factory()->Run(p,before,time);Require(exhausted.State==Status::Complete&&exhausted.Layouts.size()==3,"normal budget exhaustion changed original completeness");
    auto insufficient=options;insufficient.MaxPlacementAttempts=2;auto noVerify=MakeConvexTailRepackStage(settings).Factory()->Run(p,before,insufficient);Require(noVerify.Layouts.size()==3&&noVerify.Stats.PlacementAttempts<=2,"candidate published without complete original verification budget");
    auto throwing=options;throwing.Cancel=[]()->bool{throw std::runtime_error("cancel failed");};auto contained=MakeConvexTailRepackStage(settings).Factory()->Run(p,before,throwing);Require(Has(contained,"failed safely")&&contained.Layouts.size()==3,"callback exception escaped optional stage");
    auto run=[&]{return std::async(std::launch::async,[&]{return MakeConvexTailRepackStage(settings).Factory()->Run(p,before,options);});};auto one=run(),two=run();
    Require(one.get().Layouts.size()==2&&two.get().Layouts.size()==2,"independent CP jobs leaked mutable state");
    Require(DefaultStrategies().IDs()==defaults,"optional stage changed default strategies");
    auto invalid=settings;invalid.MaxWindowBoards=9;bool rejected=false;try{MakeConvexTailRepackStage(invalid);}catch(const std::invalid_argument&){rejected=true;}Require(rejected,"invalid optional settings were not rejected");
    std::cout<<"PASS convex tail original material/quarter domains/stock identity/forward material/caps/cancellation/isolation/default registry\n";return 0;
}
catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
