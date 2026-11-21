#include "SheetNestingBeam.h"
#include "SheetNestingNfp.h"
#include "NestingInternal.h"
#include "NestingExecution.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
void require(bool x,const char* msg){if(!x)throw std::runtime_error(msg);}
Part part(const char* id,std::vector<Point> ring,std::size_t quantity=1)
{Part p;p.ID=id;p.Material="m";p.Geometry.Outer=std::move(ring);p.Quantity=quantity;p.FreeRotation=false;p.AllowedAnglesDegrees={0};return p;}
namespace
{
    double Field(const Result& result,const std::string& name)
    {
        for(const auto& note:result.Diagnostics)
            if(const auto field=note.find(name+'=');field!=std::string::npos)
                return std::stod(note.substr(field+name.size()+1));
        throw std::runtime_error("Missing beam diagnostic field "+name);
    }
    Problem ProgressOrder()
    {
        Problem p;p.Tolerance=1e-7;p.Stocks={{"original-sheet",120,60,1,1,"m"}};
        for(std::size_t i=0;i<8;++i)
        {
            const double width=8+double(i),height=5+double(i%3);
            const auto id="type-"+std::to_string(i);
            auto q=part(id.c_str(),{{0,0},{width,0},{width,2},{2,2},{2,height},{0,height}},8);
            q.AllowedAnglesDegrees={0,180};p.Parts.push_back(std::move(q));
        }
        return p;
    }
    void OriginalGeometry(const Problem& input,const Result& result)
    {
        require(Validate(input,result).Valid,"beam result lost original inventory or geometry");
        std::map<std::string,const Part*> parts;
        for(const auto& p:input.Parts)parts.emplace(p.ID,&p);
        std::set<std::pair<std::string,std::size_t>> identities;
        for(const auto& layout:result.Layouts)
        {
            require(layout.StockID==input.Stocks[0].ID&&layout.StockInstance==0&&
                layout.Width==input.Stocks[0].Width&&layout.Height==input.Stocks[0].Height,
                "beam changed the original finite stock");
            std::vector<AxisSafety::Geometry> geometries;
            for(const auto& pose:layout.Placements)
            {
                const auto& source=*parts.at(pose.PartID);
                require(pose.Instance<source.Quantity&&identities.emplace(pose.PartID,pose.Instance).second,
                    "beam changed an original instance identity");
                require(Detail::AngleAllowed(source,pose.Transform.AngleDegrees,input.Tolerance),
                    "beam widened an original angle domain");
                const auto geometry=AxisSafety::Make(source.Geometry,pose.Transform);
                require(AxisSafety::Inside(geometry,layout.Width,layout.Height,input.EdgeMargin),
                    "beam crossed the nominal original stock boundary");
                for(const auto& other:geometries)
                    require(AxisSafety::Pair(geometry,other,input.PartGap,input.AllowCommonEdges,input.Tolerance)==
                        AxisSafety::Verdict::Safe,"beam violated nominal original material");
                geometries.push_back(geometry);
            }
        }
    }
    void QuickPassControls(const Problem& completeOrder,NfpBeamQuickPolicy policy)
    {
        const auto input=ProgressOrder();
        Options budget;budget.TimeLimitSeconds=10;budget.MaxPlacementAttempts=4000;
        NfpBeamSettings wide;wide.QuickPolicy=policy;wide.BeamWidth=32;wide.MaxNarrowingRounds=0;wide.QuickPassBudgetFraction=0;
        const auto baseline=MakeNfpBeamFactory(wide)()->Run(input,budget);
        auto quick=wide;quick.QuickPassBudgetFraction=.5;
        auto reusable=MakeNfpBeamFactory(quick)();
        const auto improved=reusable->Run(input,budget);
        OriginalGeometry(input,baseline);OriginalGeometry(input,improved);
        require(Validate(input,improved).PlacedCount>Validate(input,baseline).PlacedCount,
            "quick construction did not reach deeper original inventory than the wide beam");
        require(Field(baseline,"quickAttempts")==0&&Field(improved,"quickAttempts")>0&&
            Field(improved,"quickAttempts")<=2000,"quick construction exceeded its reserved attempt share");
        require(Field(improved,"rounds")==1&&Field(improved,"mainPlaced")>0,
            "quick construction consumed the caller's sole configured search round");
        require(improved.Stats.PlacementAttempts<=budget.MaxPlacementAttempts&&improved.State!=Status::Cancelled,
            "local quick-pass exhaustion changed the global budget or cancellation state");
        require(Field(improved,"selectedQuick")==1,"configured search discarded the better quick incumbent");
        auto lookahead=quick;lookahead.EndgameDepth=4;lookahead.RemainingLookahead=2;
        auto lookaheadBudget=budget;lookaheadBudget.MaxPlacementAttempts=1000;
        const auto endgame=MakeNfpBeamFactory(lookahead)()->Run(completeOrder,lookaheadBudget);
        OriginalGeometry(completeOrder,endgame);
        require(Field(endgame,"quickAttempts")<=500&&endgame.Stats.PlacementAttempts<=1000,
            "quick lookahead or pair completion exceeded the phase attempt limit");
        std::cout<<"PASS beam quick construction advances original inventory within shared attempts\n";

        auto tiny=budget;tiny.MaxPlacementAttempts=1;
        const auto limited=reusable->Run(input,tiny);OriginalGeometry(input,limited);
        require(limited.Stats.PlacementAttempts<=1&&Field(limited,"quickAttempts")==0,
            "one-attempt budget was rounded up for the optional quick pass");
        std::size_t polls=0;
        auto cancel=budget;cancel.Cancel=[&]{return ++polls==500;};
        const auto cancelled=reusable->Run(input,cancel);OriginalGeometry(input,cancelled);
        require(cancelled.State==Status::Cancelled&&polls==500,
            "one-shot cancellation was lost when the quick phase handed over");
        const auto rerun=reusable->Run(input,budget);OriginalGeometry(input,rerun);
        require(rerun.State!=Status::Cancelled&&Validate(input,rerun).PlacedCount==Validate(input,improved).PlacedCount,
            "reused beam retained an earlier job's cancellation or phase budget");
        std::cout<<"PASS beam local limits, cancellation and reusable job state remain separate\n";

        auto timed=budget;timed.TimeLimitSeconds=.15;timed.MaxPlacementAttempts=10000000;
        const auto timeLimited=reusable->Run(input,timed);OriginalGeometry(input,timeLimited);
        require(Field(timeLimited,"quickSeconds")>0&&Field(timeLimited,"rounds")==1&&
            timeLimited.State!=Status::Cancelled,"quick time limit prevented the configured search or became cancellation");
        require(timeLimited.Stats.PlacementAttempts<=timed.MaxPlacementAttempts,"timed quick pass exceeded attempts");
        for(const double invalid:{-.1,.5001,std::numeric_limits<double>::quiet_NaN()})
        {
            auto invalidSettings=quick;invalidSettings.QuickPassBudgetFraction=invalid;
            const auto rejected=MakeNfpBeamFactory(invalidSettings)()->Run(input,budget);
            require(rejected.Layouts.empty()&&Validate(input,rejected).Valid,
                "invalid quick-pass fraction entered construction or lost demand");
        }
        std::cout<<"PASS beam time partition and bounded quick-pass configuration\n";

        auto ample=budget;ample.MaxPlacementAttempts=500000;ample.TimeLimitSeconds=2;
        for(const auto policy:{RankingPolicy::RegularRemnant,RankingPolicy::LowThenLeft,RankingPolicy::CompactArea})
        {
            ample.Ranking=policy;
            JobResult old,newResult;
            old.Output=MakeNfpBeamFactory(wide)()->Run(completeOrder,ample);
            newResult.Output=MakeNfpBeamFactory(quick)()->Run(completeOrder,ample);
            Detail::AssessCandidate(completeOrder,old);Detail::AssessCandidate(completeOrder,newResult);
            require(old.Checked.Complete&&newResult.Checked.Complete&&
                Detail::Quality(newResult,completeOrder,policy)<=Detail::Quality(old,completeOrder,policy),
                "quick and configured complete candidates were not retained under common quality");
            OriginalGeometry(completeOrder,newResult.Output);
        }
        require(input.Parts[0].Quantity==8&&input.Parts[0].Geometry.Outer[0].X==0,
            "quick construction mutated the original input");
        std::cout<<"PASS beam phase finalists retain common-quality complete results\n";
    }
    void QuickSpecificationProgress()
    {
        Problem input;input.Tolerance=1e-7;input.Stocks={{"original-sheet",500,500,1,1,"m"}};
        for(std::size_t index=0;index<96;++index)
        {
            const double width=5+double(index)*.01;
            const auto id="original-"+std::to_string(index);
            input.Parts.push_back(part(id.c_str(),{{0,0},{width,0},{width,2},{0,2}}));
        }
        Options budget;budget.TimeLimitSeconds=3;budget.MaxPlacementAttempts=1000;
        NfpBeamSettings settings;settings.QuickPolicy=NfpBeamQuickPolicy::HullGreedy;settings.MaxNarrowingRounds=0;
        const auto result=MakeNfpBeamFactory(settings)()->Run(input,budget);
        OriginalGeometry(input,result);
        require(Field(result,"quickPlaced")>=12,
            "quick construction still exhausts breadth before advancing original specifications");
        require(Field(result,"quickAttempts")<=500&&result.Stats.PlacementAttempts<=1000&&
            Field(result,"rounds")==1,"greedy quick pass consumed the configured main-search budget");
        require(!result.Layouts.empty()&&result.Layouts[0].Placements.front().PartID=="original-95",
            "quick construction ignored its deterministic original hull-area preference");
        require(input.Parts[0].ID=="original-0"&&input.Parts[95].ID=="original-95",
            "quick construction reordered the caller's original specifications");
        std::cout<<"PASS quick construction advances 96 independent original specifications\n";
    }
    void SharedGeometryCache()
    {
        Problem input;input.Tolerance=1e-7;input.Stocks={{"first-sheet",18,18,1,1,"m"}};
        input.Parts={part("first-diamond",{{6,0},{12,6},{6,12},{0,6}},2)};
        Options budget;budget.EnableBasicCompaction=false;budget.TimeLimitSeconds=2;budget.MaxPlacementAttempts=20000;
        NfpBeamSettings settings;settings.BeamWidth=16;settings.MaxNarrowingRounds=0;settings.QuickPassBudgetFraction=0;
        auto cache=std::make_shared<NfpCache>();
        const auto first=MakeNfpBeamFactory(settings,cache)()->Run(input,budget);
        OriginalGeometry(input,first);
        require(Validate(input,first).Complete,"shared-cache first construction failed");
        const auto cold=cache->Statistics();
        require(cold.RegionBuilds>0,"beam did not populate its supplied geometry cache");

        auto next=input;next.Parts[0].ID="second-diamond";next.Parts[0].Quantity=3;
        next.Stocks[0].ID="second-sheet";next.Stocks[0].Width=36;
        auto independent=MakeNfpBeamFactory(settings,cache)();
        const auto second=independent->Run(next,budget);
        OriginalGeometry(next,second);
        require(Validate(next,second).Complete&&Validate(next,second).PlacedCount==3,
            "shared geometry retained an earlier job's identities, inventory or stock");
        const auto warm=cache->Statistics();
        require(warm.RegionBuilds==cold.RegionBuilds&&warm.RegionHits>cold.RegionHits,
            "independent beam constructions rebuilt the same immutable NFP");

        auto cancelledBudget=budget;cancelledBudget.Cancel=[]{return true;};
        const auto cancelled=independent->Run(next,cancelledBudget);
        require(cancelled.State==Status::Cancelled&&cancelled.Layouts.empty()&&
            cancelled.Remaining.size()==1&&cancelled.Remaining[0].Quantity==3,
            "a warm geometry cache bypassed job cancellation or lost demand");
        const auto resumed=independent->Run(input,budget);
        OriginalGeometry(input,resumed);
        require(Validate(input,resumed).Complete&&Validate(input,resumed).PlacedCount==2,
            "shared-cache beam retained the preceding job's cancellation or demand");
        std::cout<<"PASS independent constructors share only immutable pair geometry\n";
    }
}
int main()try
{
    Options opts;opts.TimeLimitSeconds=2;opts.MaxPlacementAttempts=500000;
    NfpBeamSettings config;config.BeamWidth=32;config.MaxNarrowingRounds=1;
    auto strategy=MakeNfpBeamFactory(config)();
    Problem diamond;diamond.Tolerance=1e-7;diamond.Stocks={{"s",18,18,1,1,"m"}};
    diamond.Parts={part("diamond",{{6,0},{12,6},{6,12},{0,6}},2)};
    auto r=strategy->Run(diamond,opts);auto checked=Validate(diamond,r);
    require(checked.Valid&&checked.Complete&&checked.PlacedCount==2,"diagonal dense diamond pair lost");
    const auto a=Detail::Bounds(diamond.Parts[0].Geometry,r.Layouts[0].Placements[0].Transform);
    const auto b=Detail::Bounds(diamond.Parts[0].Geometry,r.Layouts[0].Placements[1].Transform);
    require(a.X<b.X+b.Width&&b.X<a.X+a.Width&&a.Y<b.Y+b.Height&&b.Y<a.Y+a.Height,"used disjoint envelopes for diagonal pair");
    require(diamond.Parts[0].Geometry.Outer[0].X==6&&diamond.Parts[0].Quantity==2,"original input changed");
    Problem interlock;interlock.Tolerance=1e-7;interlock.Stocks={{"s",14,5,1,1,"m"}};
    interlock.Parts={part("U",{{0,0},{2,0},{2,3},{12,3},{12,0},{14,0},{14,5},{0,5}}),
        part("plug",{{0,0},{10,0},{10,3},{0,3}})};
    r=strategy->Run(interlock,opts);require(Validate(interlock,r).Complete,"different original types failed to interlock");
    require(r.Layouts[0].Width==14&&r.Layouts[0].Height==5,"changed original stock dimensions");
    // Three U/plug pairs exactly fill the stock. Near completion the identical
    // remaining inventory must still retain different original-material
    // neighbourhoods, and the necessary-fit probes must share the job budget.
    auto repeated=interlock;repeated.Stocks[0].Height=15;
    for(auto& p:repeated.Parts)p.Quantity=3;
    NfpBeamSettings endgame=config;endgame.MaxNarrowingRounds=0;endgame.EndgameDepth=4;endgame.RemainingLookahead=2;
    const auto repeatedResult=MakeNfpBeamFactory(endgame)()->Run(repeated,opts);
    const auto repeatedCheck=Validate(repeated,repeatedResult);
    require(repeatedCheck.Valid&&repeatedCheck.Complete&&repeatedCheck.PlacedCount==6,
        "endgame diversity lost a repeated exact interlock");
    std::size_t geometries=0,probes=0;
    for(const auto& note:repeatedResult.Diagnostics)
    {
        if(const auto field=note.find("endgameGeometry=");field!=std::string::npos)
            geometries=std::stoull(note.substr(field+16));
        if(const auto field=note.find("forwardQueries=");field!=std::string::npos)
            probes=std::stoull(note.substr(field+15));
    }
    require(geometries>4&&probes>0,"endgame collapsed its inventory geometries or skipped forward checks");
    require(repeatedResult.Stats.PlacementAttempts<=opts.MaxPlacementAttempts,
        "remaining-fit probes exceeded the shared attempt budget");
    auto plugFirst=config;plugFirst.FirstPartIndex=1;plugFirst.MaxNarrowingRounds=0;
    plugFirst.EndgameDepth=4;plugFirst.RemainingLookahead=2;
    const auto seeded=MakeNfpBeamFactory(plugFirst)()->Run(interlock,opts);
    require(Validate(interlock,seeded).Valid&&Validate(interlock,seeded).Complete&&
        seeded.Layouts[0].Placements.front().PartID=="plug",
        "independent first-specification job lost its seed or original interlock");
    auto unknownFirst=config;unknownFirst.FirstPartIndex=interlock.Parts.size();
    const auto rejectedSeed=MakeNfpBeamFactory(unknownFirst)()->Run(interlock,opts);
    require(rejectedSeed.Layouts.empty()&&Validate(interlock,rejectedSeed).Valid&&rejectedSeed.Remaining.size()==2,
        "unknown first-specification index lost original demand");
    auto tiny=opts;tiny.MaxPlacementAttempts=1;r=strategy->Run(diamond,tiny);
    require(r.Stats.PlacementAttempts<=1&&Validate(diamond,r).Valid,"attempt budget or partial demand broken");
    auto cancel=opts;cancel.Cancel=[]{return true;};r=strategy->Run(diamond,cancel);
    require(r.State==Status::Cancelled&&r.Remaining.size()==1&&r.Remaining[0].Quantity==2,"cancelled demand lost");
    auto invalid=diamond;invalid.Parts[0].Geometry.Outer.clear();r=strategy->Run(invalid,opts);
    require(r.State==Status::InvalidInput,"invalid geometry entered hull preparation");
    auto gap=diamond;gap.PartGap=.1;r=strategy->Run(gap,opts);
    require(r.State==Status::Partial&&r.Remaining[0].Quantity==2&&r.Layouts.empty(),"unsupported gap silently approximated");
    Problem free;free.Tolerance=1e-7;free.Stocks={{"sheet",8.1,3.1,1,1,"m"}};
    auto oblique=part("oblique",{{0,0},{8,0},{8,3},{0,3}});
    for(auto& point:oblique.Geometry.Outer)point=Detail::Transform(point,{0,0,37});
    oblique.FreeRotation=true;oblique.AllowedAnglesDegrees.clear();free.Parts={oblique};
    const auto freeResult=strategy->Run(free,opts);OriginalGeometry(free,freeResult);
    require(Validate(free,freeResult).Complete,"free-rotation input was rejected before constructive search");
    require(std::abs(std::remainder(freeResult.Layouts[0].Placements[0].Transform.AngleDegrees,90.))>1,
        "free-rotation construction did not use a legal oblique direction");
    require(free.Parts[0].FreeRotation&&free.Parts[0].AllowedAnglesDegrees.empty(),
        "constructive direction sampling narrowed the original free-rotation domain");
    auto defaults=DefaultStrategies().IDs();require(std::none_of(defaults.begin(),defaults.end(),[](const auto& id){return id.find("nfp.beam")==0;}),"optional experiment expanded default portfolio");
    std::cout<<"NFP beam dense nonrectangular combinations, inventory, original bounds, budgets and cancellation passed\n";
    require(NfpBeamSettings{}.QuickPolicy==NfpBeamQuickPolicy::Scored,"original scored quick policy is not the default");
    auto invalidPolicy=config;invalidPolicy.QuickPolicy=static_cast<NfpBeamQuickPolicy>(99);
    const auto unsupportedPolicy=MakeNfpBeamFactory(invalidPolicy)()->Run(interlock,opts);
    require(unsupportedPolicy.Layouts.empty()&&Validate(interlock,unsupportedPolicy).Valid,
        "unknown quick construction policy entered search or lost original demand");
    for(const auto policy:{NfpBeamQuickPolicy::Scored,NfpBeamQuickPolicy::HullGreedy})QuickPassControls(interlock,policy);
    QuickSpecificationProgress();
    SharedGeometryCache();
}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
