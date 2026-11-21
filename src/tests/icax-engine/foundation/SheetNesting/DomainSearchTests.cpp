#include "SheetNestingDomainSearch.h"
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
using namespace iCAX::SheetNesting;
static void require(bool v,const char* s){if(!v)throw std::runtime_error(s);}

static Part rectangle(const std::string& id,double width,double height,Point origin={})
{
    Part part;part.ID=id;part.Material="m";part.FreeRotation=false;part.AllowedAnglesDegrees={0,180};
    part.Geometry.Outer={{origin.X,origin.Y},{origin.X+width,origin.Y},
        {origin.X+width,origin.Y+height},{origin.X,origin.Y+height}};
    return part;
}

static void nominalCandidates(const Problem& problem,const Layout& layout,std::size_t target,
    std::size_t maximumBlockers,const Detail::PlacementDomainQueryResult& result)
{
    const auto& original=problem.Parts[target];
    for(const auto& candidate:result.Candidates)
    {
        require(Detail::AngleAllowed(original,candidate.Transform.AngleDegrees,problem.Tolerance),"probe changed original angle domain");
        const auto moving=AxisSafety::Make(original.Geometry,candidate.Transform);
        require(AxisSafety::Inside(moving,problem.Stocks[0].Width,problem.Stocks[0].Height,problem.EdgeMargin),
            "probe accepted nominal original material beyond the stock boundary");
        std::set<std::size_t> blockers(candidate.Blockers.begin(),candidate.Blockers.end());
        require(blockers.size()==candidate.Blockers.size()&&blockers.size()<=maximumBlockers,"probe lost or duplicated blocker indices");
        require(blockers.empty()||*blockers.rbegin()<layout.Placements.size(),"probe blocker index is outside the original layout");
        Result proposal;proposal.State=Status::Partial;
        auto kept=layout;kept.Placements.clear();
        for(std::size_t i=0;i<layout.Placements.size();++i)
        {
            const auto& placed=layout.Placements[i];
            const auto part=std::find_if(problem.Parts.begin(),problem.Parts.end(),[&](const Part& p){return p.ID==placed.PartID;});
            require(part!=problem.Parts.end(),"test source contains an unknown original");
            if(blockers.count(i))continue;
            const auto fixed=AxisSafety::Make(part->Geometry,placed.Transform);
            require(AxisSafety::Pair(moving,fixed,0,problem.AllowCommonEdges,problem.Tolerance)==AxisSafety::Verdict::Safe,
                "probe accepted original material intrusion against an unremoved obstacle");
            kept.Placements.push_back(placed);
        }
        kept.Placements.push_back({original.ID,0,candidate.Transform,0,false});
        std::map<std::string,std::size_t> counts;
        for(const auto& placed:kept.Placements)++counts[placed.PartID];
        for(const auto& part:problem.Parts)if(counts[part.ID]<part.Quantity)
            proposal.Remaining.push_back({part.ID,part.Quantity-counts[part.ID]});
        proposal.Layouts.push_back(std::move(kept));
        require(Validate(problem,proposal).Valid,"nominal probe proposal failed native original validation after removing its blockers");
    }
}

static void nominalProbeControls()
{
    Options options;options.TimeLimitSeconds=3;options.MaxPlacementAttempts=100000;
    Problem problem;problem.Tolerance=1e-6;problem.Stocks.push_back({"board",200,100,1,1,"m"});
    problem.Parts={rectangle("anchor",40.1,20.7),rectangle("target",20.1,14.7,{1000000.1,-999999.8})};
    const Layout layout{"board",0,200,100,{{"anchor",0,{0,0,0},9,false}},40.1*20.7};
    auto probe=Detail::MakePlacementDomainProbe();
    const auto safe=probe->Find(problem,layout,1,0,options);
    require(!safe.Candidates.empty(),"large source origin lost all legal nominal placements");
    nominalCandidates(problem,layout,1,0,safe);

    const auto displaced=probe->Find(problem,layout,1,1,options);
    nominalCandidates(problem,layout,1,1,displaced);
    bool sawNominalBlocker=false;
    for(const auto& candidate:displaced.Candidates)if(!candidate.Blockers.empty())
    {
        const auto material=AxisSafety::Make(problem.Parts[1].Geometry,candidate.Transform);
        const auto fixed=AxisSafety::Make(problem.Parts[0].Geometry,layout.Placements[0].Transform);
        if(!Detail::ShapesConflict(problem.Parts[0].Geometry,layout.Placements[0].Transform,
            problem.Parts[1].Geometry,candidate.Transform,0,problem.Tolerance)&&
            AxisSafety::Pair(material,fixed,0,false,problem.Tolerance)==AxisSafety::Verdict::Blocked)
            sawNominalBlocker=true;
    }
    require(sawNominalBlocker,"a sub-tolerance material conflict disappeared from the original blocker set");
    for(const std::size_t cap:{0,1,2,5})
    {
        auto limited=options;limited.MaxPlacementAttempts=cap;
        const auto result=probe->Find(problem,layout,1,1,limited);
        require(result.Attempts<=cap,"micro-retreat exceeded the shared attempt budget");
        nominalCandidates(problem,layout,1,1,result);
        require(result.Truncated,"exhausted probe failed to report truncation");
    }
    auto noTime=options;noTime.TimeLimitSeconds=0;
    const auto timed=probe->Find(problem,layout,1,1,noTime);
    require(timed.Candidates.empty()&&timed.Attempts==0&&timed.Truncated,"zero-time probe produced a placement");
    std::size_t polls=0;auto once=options;once.Cancel=[&]{return ++polls==1;};
    const auto cancelled=probe->Find(problem,layout,1,1,once);
    require(cancelled.Candidates.empty()&&cancelled.Attempts==0&&cancelled.Truncated&&polls==1,
        "one-shot cancellation was not latched across a reused probe");
    const auto reused=probe->Find(problem,layout,1,0,options);
    require(!reused.Candidates.empty(),"cancelled probe contaminated the next original query");
    nominalCandidates(problem,layout,1,0,reused);

    Problem blocked;blocked.Tolerance=1e-7;blocked.Stocks.push_back({"board",10,4,1,1,"m"});
    auto wall=rectangle("wall",2,4);wall.Quantity=3;wall.AllowedAnglesDegrees={0};
    auto target=rectangle("target",10,4);target.AllowedAnglesDegrees={0};blocked.Parts={wall,target};
    const Layout occupied{"board",0,10,4,{{"wall",0,{0,0,0},1,false},{"wall",1,{3,0,0},2,false},
        {"wall",2,{6,0,0},3,false}},24};
    require(probe->Find(blocked,occupied,1,2,options).Candidates.empty(),"blocker cap silently omitted a third conflicting original");
    const auto allBlockers=probe->Find(blocked,occupied,1,3,options);
    require(!allBlockers.Candidates.empty(),"three-blocker original placement was lost");
    for(const auto& candidate:allBlockers.Candidates)
        require(candidate.Blockers==std::vector<std::size_t>{0,1,2},"original blocker indices were reordered or omitted");
    nominalCandidates(blocked,occupied,1,3,allBlockers);

    Problem free;free.Tolerance=1e-7;free.Stocks.push_back({"board",30,25,1,1,"m"});
    auto skew=rectangle("skew",1,1);skew.FreeRotation=true;skew.AllowedAnglesDegrees.clear();
    skew.Geometry.Outer={{.25,-.375},{8.25,2.625},{7.25,6.625},{-.75,3.625}};free.Parts={skew};
    const Layout empty{"board",0,30,25,{},0};
    const auto freeAngles=probe->Find(free,empty,0,0,options);
    nominalCandidates(free,empty,0,0,freeAngles);
    require(std::any_of(freeAngles.Candidates.begin(),freeAngles.Candidates.end(),[](const auto& candidate)
        {return std::abs(std::remainder(candidate.Transform.AngleDegrees,90.))>1e-6;}),
        "free original edge-alignment directions were lost");

    Problem tiny; tiny.Tolerance=1e-7;tiny.Stocks.push_back({"board",20,20,1,1,"m"});
    auto uncertain=rectangle("uncertain",5,5);uncertain.AllowedAnglesDegrees={0};
    const double low=std::ldexp(1.,-520);
    uncertain.Geometry.Outer={{low,low},{5,low},{5,5},{low,5}};tiny.Parts={uncertain};
    const Layout unplaced{"board",0,20,20,{},0};
    const auto unsupportedArithmetic=probe->Find(tiny,unplaced,0,0,options);
    require(unsupportedArithmetic.Candidates.empty()&&unsupportedArithmetic.Truncated,
        "uncertain nominal arithmetic became a usable probe candidate");
    std::cout<<"nominal world poses, blockers, finite/free angles and shared probe limits passed\n";
}
int main() try {
    nominalProbeControls();
    Problem p;p.Tolerance=1e-7;p.Stocks.push_back({"board",10,14,1,1,"m"});
    Part lower;lower.ID="lower";lower.Material="m";lower.FreeRotation=false;lower.AllowedAnglesDegrees={0};lower.Geometry.Outer={{2,0},{8,0},{8,5},{2,5}};
    Part upper=lower;upper.ID="upper";upper.Geometry.Outer={{2,13.3},{8,13.3},{8,14},{2,14}};
    Part target=lower;target.ID="target";target.Geometry.Outer={{0,8},{10,0},{10,10},{0,9}};
    p.Parts={lower,upper,target};Layout layout{"board",0,10,14,{{"lower",0,{0,0,0},0,false},{"upper",0,{0,0,0},0,false}},34.2};
    Options o;o.TimeLimitSeconds=3;o.MaxPlacementAttempts=100000;
    const auto poses=Detail::ProbeDomainCandidates(p,layout,2,o);bool found=false;
    for(const auto& pose:poses){
        require(Detail::InsideSheet(target.Geometry,pose,10,14,0,p.Tolerance),"candidate outside stock");
        require(!Detail::ShapesConflict(lower.Geometry,{0,0,0},target.Geometry,pose,0,p.Tolerance),"lower collision");
        require(!Detail::ShapesConflict(upper.Geometry,{0,0,0},target.Geometry,pose,0,p.Tolerance),"upper collision");
        if(std::abs(pose.X)<p.Tolerance&&pose.Y>=3.4-p.Tolerance&&pose.Y<=3.5+p.Tolerance)found=true;
    }
    require(found,"missed feasible zero-width translation domain");
    Problem channel;channel.Tolerance=1e-7;channel.Stocks.push_back({"board",25,20,1,1,"m"});
    Part u=lower;u.ID="U";u.Geometry.Outer={{0,0},{2,0},{2,3},{12,3},{12,0},{14,0},{14,5},{0,5}};
    Part fill=lower;fill.ID="fill";fill.Geometry.Outer={{0,0},{10,0},{10,3},{0,3}};channel.Parts={u,fill};
    Layout placed{"board",0,25,20,{{"U",0,{6,5,0},0,false}},40};
    const auto channelPoses=Detail::ProbeDomainCandidates(channel,placed,1,o);bool insideChannel=false;
    for(const auto pose:channelPoses)if(std::abs(pose.X-8)<1e-6&&pose.Y>2.01&&pose.Y<4.99&&
        !Detail::ShapesConflict(u.Geometry,{6,5,0},fill.Geometry,pose,0,channel.Tolerance))insideChannel=true;
    require(insideChannel,"missed touch-only channel inside regularized NFP");
    Problem cut;cut.Tolerance=1e-7;cut.Stocks.push_back({"board",14,8,1,1,"m"});
    Part cu=lower;cu.ID="cu";cu.Geometry.Outer={{0,3},{2,3},{2,6},{12,6},{12,3},{14,3},{14,8},{0,8}};
    Part cb=lower;cb.ID="cb";cb.Geometry.Outer={{9,0},{9,.5},{3,.5},{3,0}};
    Part ct=lower;ct.ID="ct";ct.Geometry.Outer={{11,3.8},{11,4},{4,4},{4,3.8}};
    Part cm=lower;cm.ID="cm";cm.Geometry.Outer={{10,0},{10,3},{0,3},{0,0}};cut.Parts={cu,cb,ct,cm};
    Layout cutLayout{"board",0,14,8,{{"cu",0,{0,0,0},0,false},{"cb",0,{0,0,0},0,false},{"ct",0,{0,0,0},0,false}},0};
    const auto cutPoses=Detail::ProbeDomainCandidates(cut,cutLayout,3,o);bool eventInterval=false;
    for(const auto pose:cutPoses)if(std::abs(pose.X-2)<1e-6&&pose.Y>.5001&&pose.Y<.7999&&
        !Detail::ShapesConflict(cu.Geometry,{0,0,0},cm.Geometry,pose,0,cut.Tolerance)&&
        !Detail::ShapesConflict(cb.Geometry,{0,0,0},cm.Geometry,pose,0,cut.Tolerance)&&
        !Detail::ShapesConflict(ct.Geometry,{0,0,0},cm.Geometry,pose,0,cut.Tolerance))eventInterval=true;
    require(eventInterval,"missed interior of constrained touch-only channel");
    o.Cancel=[] {return true;};require(Detail::ProbeDomainCandidates(p,layout,2,o).empty(),"cancel ignored");
    Problem simple;simple.Tolerance=1e-7;simple.Stocks.push_back({"board",10,10,1,1,"m"});
    Part tile=lower;tile.ID="tile";tile.Quantity=2;tile.Geometry.Outer={{0,0},{5,0},{5,10},{0,10}};simple.Parts.push_back(tile);
    o.Cancel={};auto solver=MakeDomainSearchFactory()();auto result=solver->Run(simple,o);const auto check=Validate(simple,result);
    require(check.Valid&&check.Complete&&check.PlacedCount==2,"simple tiling failed");
    require(result.Stats.PlacementAttempts<=o.MaxPlacementAttempts,"attempt budget exceeded");
    auto cancelled=o;cancelled.Cancel=[] {return true;};result=solver->Run(simple,cancelled);
    require(result.State==Status::Cancelled&&Validate(simple,result).Valid,"job cancellation failed");
    result=solver->Run(simple,o);require(Validate(simple,result).Complete,"cancelled job contaminated reused worker");
    auto invalid=simple;invalid.Parts.push_back(invalid.Parts.front());result=solver->Run(invalid,o);
    require(result.State==Status::InvalidInput,"duplicate ID accepted");
    std::cout<<"domain candidates and controls passed\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
