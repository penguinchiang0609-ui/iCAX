#include "SheetNestingPlaneStrategies.h"
#include "SheetNestingConstructionTrace.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* reason)
{if(!value)throw std::runtime_error(reason);}

Problem OriginalFixture(bool highQuantity=false,bool mixed=false,bool budget=false)
{
    // Read the checked-in production input verbatim. The regression does not
    // simplify a contour, narrow angles, or reduce any original quantity.
    std::ifstream input(std::filesystem::path{budget?PLANE_BUDGET_INPUT:mixed?PLANE_MIXED_INPUT:highQuantity?PLANE_QUANTITY_INPUT:PLANE_DETAILED_INPUT});
    input.imbue(std::locale::classic());
    std::string version;std::size_t partCount=0,stockCount=0;
    input>>version>>partCount>>stockCount;
    Require(version=="ICAX_NESTING_PROBLEM_2"&&partCount==((mixed||budget)?9u:highQuantity?3u:2u)&&stockCount==1,"cannot read original textile fixture");
    Problem problem;input>>problem.PartGap>>problem.EdgeMargin>>problem.Tolerance>>problem.AllowCommonEdges;
    const auto ring=[&](std::size_t count)
    {std::vector<Point> points(count);for(auto& point:points)input>>point.X>>point.Y;return points;};
    for(std::size_t i=0;i<partCount;++i)
    {
        Part part;std::size_t angleCount=0,outerCount=0,holeCount=0;
        input>>std::quoted(part.ID)>>part.Quantity>>std::quoted(part.Material)>>part.FreeRotation>>angleCount;
        part.AllowedAnglesDegrees.resize(angleCount);for(auto& angle:part.AllowedAnglesDegrees)input>>angle;
        input>>part.Geometry.Radius>>part.Geometry.Center.X>>part.Geometry.Center.Y>>outerCount>>holeCount;
        part.Geometry.Outer=ring(outerCount);
        for(std::size_t j=0;j<holeCount;++j)
        {std::size_t count=0;input>>count;part.Geometry.Holes.push_back(ring(count));}
        problem.Parts.push_back(std::move(part));
    }
    for(std::size_t i=0;i<stockCount;++i)
    {
        Stock stock;input>>std::quoted(stock.ID)>>stock.Width>>stock.Height>>stock.Quantity>>stock.Cost>>std::quoted(stock.Material);
        problem.Stocks.push_back(std::move(stock));
    }
    Require(bool(input),"incomplete textile-6 original input");
    if(budget)
    {
        const std::array<std::size_t,9> quantities{16,16,16,100,100,100,158,158,158};
        const std::array<std::size_t,9> vertices{76,64,19,78,68,20,94,76,19};
        for(std::size_t i=0;i<partCount;++i)
            Require(problem.Parts[i].Quantity==quantities[i]&&problem.Parts[i].FreeRotation&&
                problem.Parts[i].AllowedAnglesDegrees.empty()&&problem.Parts[i].Geometry.Outer.size()==vertices[i]&&
                problem.Parts[i].Geometry.Holes.empty(),"textile-655 original quantity/geometry/angles changed");
        Require(problem.Stocks[0].Width==13980&&problem.Stocks[0].Height==1000000&&problem.Stocks[0].Quantity==822&&
            problem.PartGap==.2&&problem.EdgeMargin==.5&&problem.AllowCommonEdges,
            "textile-655 original inventory/clearance profile changed");
    }
    else if(mixed)
    {
        const std::array<std::size_t,9> quantities{3,3,6,3,3,6,3,3,6},vertices{188,187,72,191,189,73,205,205,72};
        for(std::size_t i=0;i<partCount;++i)
            Require(problem.Parts[i].Quantity==quantities[i]&&problem.Parts[i].Geometry.Outer.size()==vertices[i]&&
                problem.Parts[i].FreeRotation,"textile-147 original profile changed");
        Require(problem.Stocks[0].Width==17004&&problem.Stocks[0].Height==80008&&
            problem.PartGap==.2&&problem.EdgeMargin==.5&&problem.AllowCommonEdges,
            "textile-147 original stock/clearance profile changed");
    }
    else if(highQuantity)
        Require(problem.Parts[0].Quantity==44&&problem.Parts[1].Quantity==88&&problem.Parts[2].Quantity==88&&
            problem.Parts[0].Geometry.Outer.size()==70&&problem.Parts[1].Geometry.Outer.size()==37&&
            problem.Parts[2].Geometry.Outer.size()==26&&
            problem.Parts[0].FreeRotation&&problem.Parts[1].FreeRotation&&problem.Parts[2].FreeRotation&&
            problem.Stocks[0].Width==13800&&problem.Stocks[0].Height==1000000&&
            problem.PartGap==.2&&problem.EdgeMargin==.5&&problem.AllowCommonEdges,
            "textile-1 regression profile changed");
    else Require(problem.Parts[0].Quantity==13&&problem.Parts[1].Quantity==13&&
        problem.Parts[0].Geometry.Outer.size()==226&&problem.Parts[1].Geometry.Outer.size()==111&&
        problem.Parts[0].FreeRotation&&problem.Parts[1].FreeRotation&&
        problem.Stocks[0].Width==17018&&problem.Stocks[0].Height==1000000&&
        problem.PartGap==.2&&problem.EdgeMargin==.5&&problem.AllowCommonEdges,
        "textile-6 regression profile changed");
    return problem;
}

void DifferentialSafety(const Problem& problem)
{
    std::size_t count=0;
    for(const auto& part:problem.Parts)for(const double angle:{0.,23.7})
    {
        const auto bounds=Detail::Bounds(part.Geometry,{0,0,angle});
        const Pose first{.5-bounds.X,.5-bounds.Y,angle};
        for(const Point shift:std::array<Point,4>{{{0,0},{bounds.Width,0},
            {bounds.Width+.2,0},{bounds.Width+10,bounds.Height+10}}})
        {
            const auto a=AxisSafety::Make(part.Geometry,first);
            const auto b=AxisSafety::Make(part.Geometry,{first.X+shift.X,first.Y+shift.Y,angle});
            Require(AxisSafety::CommonEdge(a,b,problem.Tolerance,true)==
                AxisSafety::CommonEdge(a,b,problem.Tolerance,false),"common-edge index changed the exact source-pair certificate");
            Require(AxisSafety::Pair(a,b,problem.PartGap,true,problem.Tolerance)==
                AxisSafety::PairExact(a,b,problem.PartGap,true,problem.Tolerance),"common-edge filtering changed the exact source-pair material verdict");
            ++count;
        }
    }
    std::cout<<"PASS detailed original contour safety differential cases="<<count<<'\n';
}

void EnvelopeCertificateSafety(const Problem& problem)
{
    for(const auto& part:problem.Parts)for(const double angle:{0.,90.,23.7})
    {
        const auto geometry=AxisSafety::Make(part.Geometry,{1000000.375,-999999.875,angle});
        Require(geometry.Envelope.X.Valid()&&geometry.Envelope.Y.Valid(),"ordinary original material lost its bounds certificate");
        for(const auto& ring:geometry.Rings)for(const auto& point:ring)
            Require(AxisSafety::AtLeast(point.X,AxisSafety::Number(geometry.Envelope.X.Low))&&
                AxisSafety::AtLeast(AxisSafety::Number(geometry.Envelope.X.High),point.X)&&
                AxisSafety::AtLeast(point.Y,AxisSafety::Number(geometry.Envelope.Y.Low))&&
                AxisSafety::AtLeast(AxisSafety::Number(geometry.Envelope.Y.High),point.Y),
                "world material bounds excluded an exact transformed vertex");
        if(angle==23.7)continue;
        const auto prepared=AxisSafety::Make(part.Geometry,{0,0,angle});
        const auto translated=AxisSafety::TranslateQuarterTurn(prepared,{1000000.375,-999999.875});
        const auto neighbour=AxisSafety::TranslateQuarterTurn(prepared,{1020000.375,-979999.875});
        Require(AxisSafety::Pair(translated,neighbour,problem.PartGap,true,problem.Tolerance)==
            AxisSafety::PairExact(translated,neighbour,problem.PartGap,true,problem.Tolerance),
            "translated cached bounds changed the original material verdict");
    }
    Shape tiny;tiny.Outer={{0x1p-520,0},{1,0},{1,1},{0x1p-520,1}};
    const auto unsupported=AxisSafety::Make(tiny,{});
    Require(!unsupported.Envelope.X.Valid(),"unsupported source low bits acquired an early separation certificate");
    const auto remote=AxisSafety::Make(problem.Parts.front().Geometry,{20000,20000,0});
    Require(AxisSafety::Pair(unsupported,remote,problem.PartGap,true,problem.Tolerance)==
        AxisSafety::PairExact(unsupported,remote,problem.PartGap,true,problem.Tolerance),
        "bounds shortcut bypassed unsupported original arithmetic");
    std::cout<<"PASS world bounds include original vertices and preserve translated/unsupported material verdicts\n";
}

void RingEligibilitySafety()
{
    Shape frame;frame.Outer={{0,0},{10,0},{10,10},{0,10}};
    std::vector<Point> curvedHole;
    for(std::size_t i=0;i<64;++i)
    {
        const double angle=2*Detail::Pi*i/64;
        curvedHole.push_back({5+2*std::cos(angle),5+2*std::sin(angle)});
    }
    frame.Holes={curvedHole};
    Shape plug;plug.Outer=curvedHole;
    const auto legacy=AxisSafety::Make(frame,{}),inside=AxisSafety::Make(plug,{});
    frame.CommonEdgeRings={true,false};
    const auto masked=AxisSafety::Make(frame,{});
    Require(masked.Valid&&masked.CommonEdgeRings==frame.CommonEdgeRings&&masked.Rings.size()==2&&masked.Rings[1].size()==64,
        "common-edge eligibility changed or dropped an original material ring");
    Require(AxisSafety::TranslateQuarterTurn(masked,{1,2}).CommonEdgeRings==masked.CommonEdgeRings,
        "quarter-turn material copy lost its per-ring eligibility");
    for(const bool filtered:{false,true})
    {
        Require(AxisSafety::CommonEdge(legacy,inside,1e-7,filtered),"empty per-ring policy did not preserve existing seam eligibility");
        Require(!AxisSafety::CommonEdge(masked,inside,1e-7,filtered),"approximated curved-hole chords received a common-edge certificate");
        Require(!AxisSafety::CommonEdge(inside,masked,1e-7,filtered),"per-ring policy depended on pair order");
    }
    Require(AxisSafety::Pair(masked,inside,.2,true,1e-7)==AxisSafety::Verdict::Blocked&&
        AxisSafety::PairExact(masked,inside,.2,true,1e-7)==AxisSafety::Verdict::Blocked,
        "ineligible curved hole waived ordinary positive clearance");
    Require(AxisSafety::Pair(masked,inside,0,false,1e-7)==AxisSafety::Verdict::Safe,
        "ineligible common-edge ring was removed from the original zero-gap material model");

    Shape square;square.Outer={{0,0},{1,0},{1,1},{0,1}};
    const auto neighbour=AxisSafety::Make(square,{10,1,0});
    Require(AxisSafety::CommonEdge(masked,neighbour,1e-7,true)&&
        AxisSafety::Pair(masked,neighbour,.2,true,1e-7)==AxisSafety::Verdict::Safe&&
        AxisSafety::PairExact(masked,neighbour,.2,true,1e-7)==AxisSafety::Verdict::Safe,
        "ineligible inner ring disabled an eligible exact outer straight edge");
    square.CommonEdgeRings={false};
    const auto ineligibleNeighbour=AxisSafety::Make(square,{10,1,0});
    Require(!AxisSafety::CommonEdge(masked,ineligibleNeighbour,1e-7,true)&&
        AxisSafety::Pair(masked,ineligibleNeighbour,.2,true,1e-7)==AxisSafety::Verdict::Blocked&&
        AxisSafety::PairExact(masked,ineligibleNeighbour,.2,true,1e-7)==AxisSafety::Verdict::Blocked,
        "one eligible ring overrode the other ring's missing seam permission");
    const auto overlapping=AxisSafety::Make(square,{9.9,1,0});
    Require(AxisSafety::Pair(masked,overlapping,0,true,1e-7)==AxisSafety::Verdict::Blocked&&
        AxisSafety::PairExact(masked,overlapping,0,true,1e-7)==AxisSafety::Verdict::Blocked,
        "ineligible seam ring bypassed actual material-overlap checks");
    bool inherited=false;
    Require(AxisSafety::PairWithSource(legacy,masked,inside,inside,.2,true,1e-7,inherited)==AxisSafety::Verdict::Blocked&&!inherited,
        "changed ring eligibility inherited a stale source common-edge clearance certificate");
    frame.CommonEdgeRings={true};
    const auto invalid=AxisSafety::Make(frame,{});
    Require(!invalid.Valid&&!AxisSafety::CommonEdge(invalid,inside,1e-7,true)&&
        AxisSafety::Pair(invalid,inside,.2,true,1e-7)==AxisSafety::Verdict::Uncertain,
        "invalid per-ring eligibility was silently accepted");
    std::cout<<"PASS per-ring common-edge permissions preserve all material and ordinary gap checks\n";
}

void ProductionConstruction(const Problem& problem)
{
    struct Counts {std::size_t Generated=0,Accepted=0,Rejected=0,Committed=0,Budget=0;};
    std::mutex traceMutex;
    std::map<std::pair<std::string,std::size_t>,Counts> counts;
    std::map<std::tuple<std::string,std::size_t,std::string>,std::size_t> rejected;
    double firstCommit=-1;
    Options options;options.TimeLimitSeconds=60;options.MaxPlacementAttempts=8000000;
    options.EnableBasicCompaction=false;
    options.ConstructionTraceRoute="plane.initial";
    options.ConstructionTraceScope="original-profile-regression";
    options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
    options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        std::lock_guard lock(traceMutex);
        std::set<std::size_t> specifications;
        for(const auto& member:event.Members)if(member.PartIndex<problem.Parts.size())specifications.insert(member.PartIndex);
        if(specifications.empty()&&event.PartIndex<problem.Parts.size())specifications.insert(event.PartIndex);
        for(const auto part:specifications)
        {
            auto& count=counts[{event.Route,part}];
            if(event.Stage==ConstructionTraceStage::Generate)++count.Generated;
            if(event.Stage==ConstructionTraceStage::Filter)
            {
                if(event.Accepted)++count.Accepted;
                else {++count.Rejected;++rejected[{event.Route,part,event.Reason}];}
            }
            if(event.Stage==ConstructionTraceStage::Commit)
            {++count.Committed;if(firstCommit<0)firstCommit=event.ElapsedSeconds;}
            if(event.Stage==ConstructionTraceStage::Budget)++count.Budget;
        }
    };
    ParallelConstructionSettings settings;settings.Workers=4;settings.FairScheduling=true;
    const auto started=std::chrono::steady_clock::now();
    const auto result=MakePlaneInitialFactory(settings)()->Run(problem,options);
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    const auto checked=Validate(problem,result);
    std::map<std::string,std::size_t> placed;
    for(const auto& layout:result.Layouts)for(const auto& item:layout.Placements)++placed[item.PartID];
    std::size_t required=0;std::map<std::string,std::size_t> indices;
    for(std::size_t i=0;i<problem.Parts.size();++i){required+=problem.Parts[i].Quantity;indices.emplace(problem.Parts[i].ID,i);}
    std::cout<<"TEXTILE wall="<<elapsed<<" placed="<<checked.PlacedCount<<" required="<<required<<" complete="<<checked.Complete
        <<" valid="<<checked.Valid<<" first-commit-event-seconds="<<firstCommit<<'\n';
    for(const auto& part:problem.Parts)std::cout<<"FINAL "<<part.ID<<" placed="<<placed[part.ID]<<" required="<<part.Quantity<<'\n';
    for(const auto& [key,count]:counts)
        std::cout<<"TRACE route="<<key.first<<" part="<<problem.Parts[key.second].ID<<" generated="<<count.Generated
            <<" filter-accepted="<<count.Accepted<<" filter-rejected="<<count.Rejected<<" commit-events="<<count.Committed<<" budget-events="<<count.Budget<<'\n';
    for(const auto& [key,count]:rejected)
        std::cout<<"FILTER route="<<std::get<0>(key)<<" part="<<problem.Parts[std::get<1>(key)].ID
            <<" reason="<<std::get<2>(key)<<" count="<<count<<'\n';
    for(const auto& diagnostic:result.Diagnostics)std::cout<<"DIAG "<<diagnostic<<'\n';
    Require(!options.ConstructionObservation->Failed.load(),"production construction trace observer failed");
    Require(checked.Valid,"detailed contour construction failed original geometry validation");
    Require(checked.Complete&&checked.PlacedCount==required,"production initial construction starved a physically placeable original specification");
    for(const auto& layout:result.Layouts)
    {
        std::vector<AxisSafety::Geometry> geometries;
        for(const auto& item:layout.Placements)
        {
            const auto index=indices.at(item.PartID);
            const auto geometry=AxisSafety::Make(problem.Parts[index].Geometry,item.Transform);
            Require(AxisSafety::Containment(geometry,layout.Width,layout.Height,problem.EdgeMargin)==AxisSafety::Verdict::Safe,
                "final detailed original violates exact stock margin");
            for(const auto& fixed:geometries)
                Require(AxisSafety::Pair(geometry,fixed,problem.PartGap,problem.AllowCommonEdges,problem.Tolerance)==AxisSafety::Verdict::Safe,
                    "final detailed original violates exact material/clearance");
            geometries.push_back(geometry);
        }
    }
    std::cout<<"PASS detailed original production construction "<<required<<'/'<<required<<'\n';
}

void EnvelopeConstruction(const Problem& problem,bool requireOneBoard=true,std::size_t attemptCap=8000000,
    const std::string& expectedTrial={})
{
    Options options;options.TimeLimitSeconds=10;options.MaxPlacementAttempts=attemptCap;options.EnableBasicCompaction=false;
    std::size_t generated=0,filtered=0,committed=0,required=0;
    for(const auto& part:problem.Parts)required+=part.Quantity;
    using BoardKey=std::tuple<std::string,std::string,std::size_t>;
    std::map<BoardKey,std::uint64_t> states;
    std::map<BoardKey,Layout> snapshots;
    std::set<BoardKey> completedBoards;
    std::set<std::pair<std::string,std::uint64_t>> acceptedCandidates,committedStates;
    std::vector<ConstructionTraceEvent> selections;
    std::string traceFailure;
    const auto checkTrace=[&](bool valid,const char* why){if(!valid&&traceFailure.empty())traceFailure=why;};
    const auto samePose=[](Pose a,Pose b)
    {return a.X==b.X&&a.Y==b.Y&&a.AngleDegrees==b.AngleDegrees&&a.Mirrored==b.Mirrored;};
    const auto samePlacement=[&](const Placement& a,const Placement& b)
    {return a.PartID==b.PartID&&a.Instance==b.Instance&&a.Group==b.Group&&a.GapFiller==b.GapFiller&&samePose(a.Transform,b.Transform);};
    const auto sameLayout=[&](const Layout& a,const Layout& b)
    {return a.StockID==b.StockID&&a.StockInstance==b.StockInstance&&a.Width==b.Width&&a.Height==b.Height&&
        a.MaterialArea==b.MaterialArea&&a.Placements.size()==b.Placements.size()&&
        std::equal(a.Placements.begin(),a.Placements.end(),b.Placements.begin(),samePlacement);};
    options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
    options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {if(event.Stage==ConstructionTraceStage::Generate)++generated;
        if(event.Stage==ConstructionTraceStage::Filter&&event.Accepted)
        {++filtered;acceptedCandidates.emplace(event.Scope,event.CandidateID);}
        if(event.Stage==ConstructionTraceStage::Commit)
        {
            committed+=event.Members.size();
            checkTrace(event.Layouts.size()==1&&event.Members.size()==1,"envelope commit omitted its real board snapshot or original delta");
            if(event.Layouts.size()!=1||event.Members.size()!=1)return;
            const auto& board=event.Layouts.front();const BoardKey key{event.Scope,board.StockID,board.StockInstance};
            checkTrace(!event.Scope.empty()&&event.StateID>0&&event.ParentStateID==states[key]&&
                committedStates.emplace(event.Scope,event.StateID).second,"envelope commit broke its same-scope physical-board parent chain");
            checkTrace(acceptedCandidates.contains({event.Scope,event.CandidateID}),"envelope commit lacked its real original-material certificate");
            const auto old=snapshots.find(key);
            const auto before=old==snapshots.end()?0:old->second.Placements.size();
            checkTrace(board.Placements.size()==before+1,"envelope commit snapshot skipped or invented a placement");
            if(old!=snapshots.end())checkTrace(board.Placements.size()>=before&&
                std::equal(old->second.Placements.begin(),old->second.Placements.end(),board.Placements.begin(),samePlacement),
                "envelope commit changed an already committed original pose");
            if(!board.Placements.empty())
            {
                const auto& placed=board.Placements.back();const auto& delta=event.Members.front();
                checkTrace(delta.PartIndex<problem.Parts.size()&&problem.Parts[delta.PartIndex].ID==placed.PartID&&
                    delta.Instance==placed.Instance&&delta.Group==placed.Group&&samePose(delta.Transform,placed.Transform),
                    "envelope real commit delta does not match its actual snapshot");
            }
            states[key]=event.StateID;snapshots[key]=board;
        }
        if(event.Source=="board.raw-construction")
        {
            checkTrace(event.Stage==ConstructionTraceStage::Complete&&event.Layouts.size()==1,"envelope raw-board completion omitted its real snapshot");
            if(event.Layouts.size()!=1)return;
            const auto& board=event.Layouts.front();const BoardKey key{event.Scope,board.StockID,board.StockInstance};
            const auto last=snapshots.find(key);
            checkTrace(last!=snapshots.end()&&event.StateID==states[key]&&sameLayout(last->second,board),
                "envelope raw-board completion does not end the recorded real commit chain");
            completedBoards.insert(key);
        }
        if(event.Source=="strategy.construction-selection")selections.push_back(event);
    };
    const auto started=std::chrono::steady_clock::now();
    const auto result=MakePlaneEnvelopeInitialFactory()()->Run(problem,options);
    const auto checked=Validate(problem,result);
    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    Require(result.Stats.ElapsedSeconds>=result.Stats.VerificationSeconds&&result.Stats.VerificationSeconds>=0&&
        result.Stats.PreparationSeconds+result.Stats.PlacementSeconds+result.Stats.VerificationSeconds<=result.Stats.ElapsedSeconds+1e-6&&
        result.Stats.ElapsedSeconds<=seconds+1e-6,"envelope timing omitted validation or double-counted sequential construction stages");
    Require(checked.Valid&&checked.Complete&&checked.PlacedCount==required,"independent envelope initial seed did not conserve complete original demand");
    if(requireOneBoard)Require(result.Layouts.size()==1,"envelope initial seed consumed unnecessary boards for the real single-board regression");
    Require(committed>=required&&generated>=committed&&filtered>=committed&&!options.ConstructionObservation->Failed.load(),
        "envelope trace is not the actual generated/checked/committed original history");
    Require(traceFailure.empty(),traceFailure.c_str());
    Require(selections.size()==1&&selections.front().Stage==ConstructionTraceStage::Selected&&selections.front().Accepted,
        "envelope did not explicitly select one actual construction trial");
    const auto& selected=selections.front();
    Require(expectedTrial.empty()||selected.Scope.ends_with("/"+expectedTrial),"envelope selected the wrong actual guarded or nominal trial scope");
    Require(selected.Layouts.size()==result.Layouts.size(),"envelope selected-trial snapshot differs from the returned physical boards");
    for(std::size_t i=0;i<result.Layouts.size();++i)
    {
        const auto& board=result.Layouts[i];const BoardKey key{selected.Scope,board.StockID,board.StockInstance};
        const auto final=snapshots.find(key);
        Require(final!=snapshots.end()&&completedBoards.contains(key)&&sameLayout(final->second,board)&&sameLayout(selected.Layouts[i],board),
            "envelope returned a board without its selected-scope real commit history and raw completion");
    }
    Require(generated==result.Stats.PlacementAttempts&&result.Stats.PlacementAttempts<=attemptCap,
        "envelope shared placement lease differs from actual generated original proposals");
    std::map<std::string,std::size_t> indices;
    for(std::size_t i=0;i<problem.Parts.size();++i)indices.emplace(problem.Parts[i].ID,i);
    for(const auto& layout:result.Layouts)
    {
        std::vector<AxisSafety::Geometry> world;
        for(const auto& placed:layout.Placements)
        {
            auto material=AxisSafety::Make(problem.Parts[indices.at(placed.PartID)].Geometry,placed.Transform);
            Require(AxisSafety::Containment(material,layout.Width,layout.Height,problem.EdgeMargin)==AxisSafety::Verdict::Safe,
                "envelope output failed exact final original containment");
            for(const auto& fixed:world)Require(AxisSafety::Pair(material,fixed,problem.PartGap,problem.AllowCommonEdges,problem.Tolerance)==AxisSafety::Verdict::Safe,
                "envelope output failed exact final original pair clearance");
            world.push_back(std::move(material));
        }
    }
    std::cout<<"ENVELOPE required="<<required<<" boards="<<result.Layouts.size()<<" seconds="<<seconds
        <<" generated="<<generated<<" accepted="<<filtered<<" committed="<<committed<<'\n';
}

void EnvelopeAttemptBudget()
{
    // Reproduce the actual fair scheduler's local lease. No part, direction,
    // stock, source contour or global/local attempt cap is relaxed.
    EnvelopeConstruction(OriginalFixture(false,false,true),true,363637);
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.AllowCommonEdges=true;
    Part tile;tile.ID="tile";tile.Quantity=2;tile.FreeRotation=false;tile.AllowedAnglesDegrees={0};
    tile.Geometry.Outer={{0,0},{4,0},{4,3},{0,3}};
    problem.Parts={tile};problem.Stocks={{"finite",40,40,1}};
    for(const std::size_t cap:{1u,2u,3u,8u})
    {
        Options options;options.TimeLimitSeconds=5;options.MaxPlacementAttempts=cap;options.EnableBasicCompaction=false;
        std::size_t generated=0,filtered=0;
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {generated+=event.Stage==ConstructionTraceStage::Generate;filtered+=event.Stage==ConstructionTraceStage::Filter;};
        const auto result=MakePlaneEnvelopeInitialFactory()()->Run(problem,options);const auto checked=Validate(problem,result);
        Require(checked.Valid&&checked.PlacedCount==(cap==1?1u:2u)&&checked.Complete==(cap>1),
            "last leased envelope attempt did not finish its own original pair check");
        Require(result.Stats.PlacementAttempts<=cap&&generated==result.Stats.PlacementAttempts&&filtered==generated,
            "small envelope lease hid a generated proposal or charged a cheap planning comparison");
    }
    bool cancelled=false;std::size_t generated=0;
    Options options;options.TimeLimitSeconds=5;options.MaxPlacementAttempts=2;options.EnableBasicCompaction=false;
    options.Cancel=[&]{return cancelled;};
    options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {if(event.Stage==ConstructionTraceStage::Generate&&++generated==2)cancelled=true;};
    const auto result=MakePlaneEnvelopeInitialFactory()()->Run(problem,options);const auto checked=Validate(problem,result);
    Require(result.State==Status::Cancelled&&checked.Valid&&checked.PlacedCount==1&&generated==2&&
        result.Stats.PlacementAttempts==2,"final leased candidate ignored original caller cancellation");
    problem.Parts.front().Quantity=1;problem.Stocks.front().Width=5;problem.Stocks.front().Height=4;
    for(const auto stage:{ConstructionTraceStage::Generate,ConstructionTraceStage::Filter,ConstructionTraceStage::Score})
    {
        cancelled=false;generated=0;
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {generated+=event.Stage==ConstructionTraceStage::Generate;if(event.Stage==stage)cancelled=true;};
        const auto emptyBoardCancelled=MakePlaneEnvelopeInitialFactory()()->Run(problem,options);
        const auto emptyBoardChecked=Validate(problem,emptyBoardCancelled);
        Require(emptyBoardCancelled.State==Status::Cancelled&&emptyBoardChecked.Valid&&emptyBoardChecked.PlacedCount==0&&
            emptyBoardCancelled.Layouts.empty()&&generated==1&&emptyBoardCancelled.Stats.PlacementAttempts==1,
            "exact-width empty board committed after final candidate observer requested cancellation");
    }
    options={};options.EnableBasicCompaction=false;options.TimeLimitSeconds=1e-9;options.MaxPlacementAttempts=2;
    const auto timed=MakePlaneEnvelopeInitialFactory()()->Run(problem,options);
    Require(timed.Stats.PlacementAttempts==0&&Validate(problem,timed).Valid,
        "planning prefilter ignored the construction wall deadline");
    std::cout<<"PASS actual envelope lease: complete textile-655 under cap=363637; final-attempt source pairs, trace counts, cancellation and wall deadline\n";
}

void ConcurrentEnvelopeConstruction()
{
    const auto mixed=OriginalFixture(false,true),quantity=OriginalFixture(true),detailed=OriginalFixture();
    auto a=std::async(std::launch::async,[&]{EnvelopeConstruction(mixed);});
    auto b=std::async(std::launch::async,[&]{EnvelopeConstruction(quantity);});
    auto c=std::async(std::launch::async,[&]{EnvelopeConstruction(detailed);});
    a.get();b.get();c.get();
    Problem material;material.PartGap=.2;material.EdgeMargin=.5;material.AllowCommonEdges=true;
    Part frame;frame.ID="frame";frame.Quantity=2;frame.FreeRotation=false;frame.AllowedAnglesDegrees={0,90};
    frame.Geometry.Outer={{-6,-5},{6,-5},{6,5},{-6,5}};frame.Geometry.Holes={{{-2,-2},{2,-2},{2,2},{-2,2}}};frame.Geometry.CommonEdgeRings={true,false};
    Part triangle;triangle.ID="sloped";triangle.Quantity=3;triangle.FreeRotation=false;triangle.AllowedAnglesDegrees={37};
    triangle.Geometry.Outer={{10,20},{16,20},{10,27}};
    Part disc;disc.ID="analytic-disc";disc.Quantity=3;disc.FreeRotation=true;disc.Geometry.Radius=2;disc.Geometry.Center={-11,17};
    material.Parts={frame,triangle,disc};material.Stocks={{"large",60,50,1}};EnvelopeConstruction(material);
    Problem finite;finite.PartGap=.2;finite.EdgeMargin=.5;Part tile;tile.ID="tile";tile.Quantity=20;tile.FreeRotation=false;tile.AllowedAnglesDegrees={0};
    tile.Geometry.Outer={{0,0},{4,0},{4,3},{0,3}};finite.Parts={tile};finite.Stocks={{"finite",10,10,2}};
    Options options;options.TimeLimitSeconds=5;options.EnableBasicCompaction=false;
    const auto limited=MakePlaneEnvelopeInitialFactory()()->Run(finite,options);const auto checked=Validate(finite,limited);
    Require(checked.Valid&&!checked.Complete&&limited.Layouts.size()==2&&checked.PlacedCount>2,
        "envelope finite inventory was expanded or degraded to one original per board");
    finite.Stocks[0].Quantity=6;EnvelopeConstruction(finite,false,8000000,"guarded-envelope");
    Problem exact;exact.PartGap=.2;exact.EdgeMargin=.5;tile.Quantity=1;tile.Geometry.Outer={{0,0},{40,0},{40,40},{0,40}};
    exact.Parts={tile};exact.Stocks={{"exact",41,41,1}};EnvelopeConstruction(exact,true,8000000,"nominal-boundary-envelope");
    exact.Parts[0].Quantity=2;exact.Stocks[0].Width=81.2;EnvelopeConstruction(exact,true,8000000,"nominal-boundary-envelope");
    exact.Parts[0].Quantity=1;exact.Parts[0].Geometry.Outer={{0,0},{20,0},{20,40},{0,40}};
    exact.Parts[0].AllowedAnglesDegrees={90};exact.Stocks[0].Width=41;exact.Stocks[0].Height=21;
    EnvelopeConstruction(exact,true,8000000,"nominal-boundary-envelope");
    options.Cancel=[] {return true;};const auto cancelled=MakePlaneEnvelopeInitialFactory()()->Run(finite,options);
    Require(cancelled.State==Status::Cancelled&&cancelled.Layouts.empty()&&Validate(finite,cancelled).Valid,
        "envelope cancellation consumed original demand or physical stock");
    std::cout<<"PASS independent envelope construction: three concurrent real tasks, final original pairs, curves/holes/angles, finite stock and cancellation\n";
}
}

int main(int argc,char** argv)try
{
    const std::string mode=argc>1?argv[1]:"all";
    if(mode=="--envelope-budget-only"){EnvelopeAttemptBudget();return 0;}
    if(mode=="--envelope-only"){ConcurrentEnvelopeConstruction();return 0;}
    const auto problem=OriginalFixture(mode=="--quantity-construction-only",mode=="--mixed-construction-only");
    if(mode=="--quantity-construction-only"||mode=="--mixed-construction-only"){ProductionConstruction(problem);return 0;}
    if(mode!="--construction-only"){DifferentialSafety(problem);EnvelopeCertificateSafety(problem);RingEligibilitySafety();}
    if(mode!="--differential-only")ProductionConstruction(problem);
    return 0;
}
catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
