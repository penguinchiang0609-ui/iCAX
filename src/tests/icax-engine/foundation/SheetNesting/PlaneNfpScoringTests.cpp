#include "SheetNestingPlaneStrategies.h"
#include "SheetNestingConstructionTrace.h"
#include "NestingClearancePlacement.h"
#include "NestingPlaneHoles.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
double RankValue(const ConstructionTraceEvent& event,const char* name)
{
    const auto found=std::find_if(event.Rank.begin(),event.Rank.end(),[&](const auto& item){return item.first==name;});
    Require(found!=event.Rank.end(),"single-placement score omitted a named comparison component");
    return found->second;
}
Part Rectangle(const char* id,double width,double height,std::size_t quantity=1)
{
    Part part;part.ID=id;part.Quantity=quantity;part.FreeRotation=false;part.AllowedAnglesDegrees={0};
    part.Geometry.Outer={{-7,11},{width-7,11},{width-7,height+11},{-7,height+11}};return part;
}
Options Limits()
{
    Options options;options.TimeLimitSeconds=5;options.MaxPlacementAttempts=200000;
    options.EnableBasicCompaction=false;options.EnableCombinations=false;return options;
}
Result Construct(const Problem& problem,std::size_t trials=1)
{
    PlaneStrategySettings settings;settings.Kind=PlaneStrategyKind::NfpAllSpecifications;settings.NfpTrials=trials;
    // Isolate the NFP leaf scoring rule from the parallel initial race and
    // complete-layout refinement selected by the full workflow.
    settings.EnableLayoutRefinement=false;
    auto result=MakePlaneStrategyFactory(settings)()->Run(problem,Limits());
    Require(Validate(problem,result).Valid,"constructor must retain original geometry and identities");return result;
}
void AuthorizedCommonEdgeProposals()
{
    Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.AllowCommonEdges=true;p.Stocks={{"board",30,10,2}};
    auto fixed=Rectangle("fixed",4,4),moving=Rectangle("moving",4,4);
    fixed.Geometry.Outer={{0,0},{4,0},{4,4},{0,4}};moving.Geometry=fixed.Geometry;p.Parts={fixed,moving};
    Layout board{"board",0,30,10,{{"fixed",0,{.5,.5,0},0,false}},16};
    const auto find=[&](const Problem& problem,const Layout& layout,double angle,std::size_t& direct,std::size_t& sameSide)
    {
        Detail::ClearanceNfpPlacement generator(problem,problem.Stocks[0],{},{});
        Require(generator.Ready(),"common-edge proposal fixture is unsupported");
        Detail::NfpPlacementLimits limits;limits.BoundaryVerticesOnly=true;
        bool orphanDuplicate=false;
        limits.Trace=[&](const ConstructionTraceEvent& event)
        {
            if(event.Stage==ConstructionTraceStage::Generate&&event.Reason=="source-edge-alignment-proposal")++direct;
            if(event.Reason=="duplicate-source-edge-proposal"&&
                (event.CandidateID!=0||event.Stage!=ConstructionTraceStage::Analysis))orphanDuplicate=true;
            for(const auto& [name,value]:event.Measures)if(name=="same-material-side-pairs-rejected")sameSide+=static_cast<std::size_t>(value);
        };
        auto result=generator.Find(layout,1,angle,limits,{});
        Require(!orphanDuplicate,"duplicate source-edge observation invented an ungenerated candidate identity");
        return result;
    };
    p.Parts[0].Geometry.CommonEdgeRings={false};p.Parts[1].Geometry.CommonEdgeRings={false};
    std::size_t direct=0,sameSide=0;auto noSeam=find(p,board,0,direct,sameSide);
    Require(direct==0&&!noSeam.Fits.empty(),"ineligible rings emitted shared-edge proposals or lost ordinary-gap placements");
    for(const auto pose:noSeam.Fits)
        Require(AxisSafety::Pair(AxisSafety::Make(p.Parts[1].Geometry,pose),AxisSafety::Make(p.Parts[0].Geometry,{.5,.5,0}),
            .2,true,p.Tolerance)==AxisSafety::Verdict::Safe,"ineligible ring waived ordinary clearance");
    p.Parts[0].Geometry.CommonEdgeRings={true};p.Parts[1].Geometry.CommonEdgeRings={true};
    for(const bool reversed:{false,true})
    {
        if(reversed)std::reverse(p.Parts[1].Geometry.Outer.begin(),p.Parts[1].Geometry.Outer.end());
        direct=0;sameSide=0;const auto query=find(p,board,0,direct,sameSide);bool seam=false;
        for(const auto pose:query.Fits)seam|=AxisSafety::CommonEdge(AxisSafety::Make(p.Parts[1].Geometry,pose),
            AxisSafety::Make(p.Parts[0].Geometry,{.5,.5,0}),p.Tolerance,true);
        Require(direct>0&&sameSide>0&&seam,"exact same-side pruning removed lawful opposite-side shared edges");
    }
    Problem hole;hole.Tolerance=1e-7;hole.PartGap=.2;hole.EdgeMargin=.5;hole.AllowCommonEdges=true;hole.Stocks={{"board",12,12,1}};
    auto frame=Rectangle("frame",10,10),plug=Rectangle("plug",6,6);
    frame.Geometry.Outer={{0,0},{10,0},{10,10},{0,10}};frame.Geometry.Holes={{{2,2},{8,2},{8,8},{2,8}}};
    frame.Geometry.CommonEdgeRings={true,true};plug.Geometry.Outer={{0,0},{6,0},{6,6},{0,6}};hole.Parts={frame,plug};
    Layout enclosed{"board",0,12,12,{{"frame",0,{.5,.5,0},0,false}},64};
    for(const bool reversed:{false,true})
    {
        if(reversed)std::reverse(hole.Parts[0].Geometry.Holes[0].begin(),hole.Parts[0].Geometry.Holes[0].end());
        direct=0;sameSide=0;const auto query=find(hole,enclosed,0,direct,sameSide);bool inside=false;
        for(const auto pose:query.Fits)if(pose.X==2.5&&pose.Y==2.5)inside=true;
        Require(inside,"hole material-side parity lost an authorized internal common edge");
    }
    hole.Parts[0].Geometry.CommonEdgeRings={true,false};direct=0;sameSide=0;
    Require(find(hole,enclosed,0,direct,sameSide).Fits.empty(),"forbidden hole chords acquired a zero-gap shared-edge proposal");
}
Problem ReadOriginalProblem(const std::filesystem::path& path)
{
    std::ifstream input(path);input.imbue(std::locale::classic());
    std::string version;std::size_t partCount=0,stockCount=0;input>>version>>partCount>>stockCount;
    Require(version=="ICAX_NESTING_PROBLEM_2"||version=="ICAX_NESTING_PROBLEM_3"||version=="ICAX_NESTING_PROBLEM_4","unsupported original native problem version");
    Problem problem;input>>problem.PartGap>>problem.EdgeMargin>>problem.Tolerance>>problem.AllowCommonEdges;
    const auto ring=[&](std::size_t count){std::vector<Point> points(count);for(auto& p:points)input>>p.X>>p.Y;return points;};
    for(std::size_t i=0;i<partCount;++i)
    {
        Part part;std::size_t angles=0,outer=0,holes=0;
        input>>std::quoted(part.ID)>>part.Quantity>>std::quoted(part.Material)>>part.FreeRotation;
        if(version=="ICAX_NESTING_PROBLEM_4")input>>part.AllowMirror;
        input>>angles;
        part.AllowedAnglesDegrees.resize(angles);for(auto& angle:part.AllowedAnglesDegrees)input>>angle;
        input>>part.Geometry.Radius>>part.Geometry.Center.X>>part.Geometry.Center.Y>>outer>>holes;
        part.Geometry.Outer=ring(outer);for(std::size_t j=0;j<holes;++j){std::size_t n=0;input>>n;part.Geometry.Holes.push_back(ring(n));}
        if(version=="ICAX_NESTING_PROBLEM_3"||version=="ICAX_NESTING_PROBLEM_4")
        {std::size_t count=0;input>>count;for(std::size_t j=0;j<count;++j){bool allowed=false;input>>allowed;part.Geometry.CommonEdgeRings.push_back(allowed);}}
        problem.Parts.push_back(std::move(part));
    }
    for(std::size_t i=0;i<stockCount;++i)
    {Stock stock;input>>std::quoted(stock.ID)>>stock.Width>>stock.Height>>stock.Quantity>>stock.Cost>>std::quoted(stock.Material);problem.Stocks.push_back(std::move(stock));}
    Require(bool(input),"incomplete original native problem");return problem;
}
int ProfileOriginalNfp(const std::filesystem::path& path)
{
    const auto problem=ReadOriginalProblem(path);
    std::size_t required=0;for(const auto& part:problem.Parts)required+=part.Quantity;
    struct Measurement
    {
        std::size_t Placed=0,Generated=0,Committed=0,Scored=0,Fallback=0;bool Complete=false,Valid=false;
        double Seconds=0,QuerySeconds=0,ContactSeconds=0,CommonSeconds=0,OrdinarySeconds=0,CommonSetup=0,OrdinarySetup=0;
        std::map<std::string,std::size_t> Sources,ClearanceFilters,Analyses;
        std::map<std::pair<std::uint64_t,int>,double> QueryStarts;
        std::map<std::string,double> DirectSummary;
        std::map<double,std::size_t> CommittedAngles;
    };
    for(const auto [name,kind]:{std::pair("closest",PlaneStrategyKind::NfpClosest),
        std::pair("directional",PlaneStrategyKind::NfpDirectional),std::pair("strip",PlaneStrategyKind::NfpStrip)})
    {
        const auto run=[&]
        {
            Measurement m;auto work=Limits();work.TimeLimitSeconds=10;work.MaxPlacementAttempts=8000000;
            work.ConstructionTrace=[&](const ConstructionTraceEvent& event)
            {
                int stream=-1;for(const auto& [name,value]:event.Measures)if(name=="nominal-common-domain")stream=static_cast<int>(value);
                if(event.Stage==ConstructionTraceStage::Generate){++m.Generated;++m.Sources[event.Source+":"+event.Reason+":stream="+std::to_string(stream)];}
                if(event.Stage==ConstructionTraceStage::Commit){++m.Committed;for(const auto& member:event.Members)++m.CommittedAngles[member.Transform.AngleDegrees];}
                if(event.Stage==ConstructionTraceStage::Score)++m.Scored;if(event.Source=="nfp.boundary-fallback")++m.Fallback;
                if(event.Stage==ConstructionTraceStage::Filter&&event.Source=="nfp.original-clearance")++m.ClearanceFilters[event.Reason];
                if(event.Stage==ConstructionTraceStage::Analysis&&event.Source=="nfp.original-clearance")++m.Analyses[event.Reason];
                if(event.Source=="nfp.source-edge-summary")for(const auto& [name,value]:event.Measures)m.DirectSummary[name]+=value;
                if(event.Stage==ConstructionTraceStage::Begin&&event.Source=="nfp.query")m.QueryStarts[{event.ProposalID,stream}]=event.ElapsedSeconds;
                if(event.Stage==ConstructionTraceStage::Complete&&event.Source=="nfp.query")
                    if(const auto begin=m.QueryStarts.find({event.ProposalID,stream});begin!=m.QueryStarts.end())
                    {if(stream==1)m.CommonSeconds+=event.ElapsedSeconds-begin->second;if(stream==0)m.OrdinarySeconds+=event.ElapsedSeconds-begin->second;}
                if(event.Stage==ConstructionTraceStage::Analysis&&event.Source=="nfp.free-domain")
                    if(const auto begin=m.QueryStarts.find({event.ProposalID,stream});begin!=m.QueryStarts.end())
                    {if(stream==1)m.CommonSetup+=event.ElapsedSeconds-begin->second;if(stream==0)m.OrdinarySetup+=event.ElapsedSeconds-begin->second;}
                for(const auto& [name,value]:event.Measures)
                {if(name=="placementQuerySeconds")m.QuerySeconds+=value;if(name=="contactScoreSeconds")m.ContactSeconds+=value;}};
            PlaneStrategySettings settings;settings.Kind=kind;settings.EnableLayoutRefinement=false;
            const auto started=std::chrono::steady_clock::now();const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,work);
            m.Seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            const auto checked=Validate(problem,result);m.Valid=checked.Valid;m.Complete=checked.Complete;m.Placed=checked.PlacedCount;
            return m;
        };
        auto first=std::async(std::launch::async,run),second=std::async(std::launch::async,run);
        std::size_t instance=0;for(const auto m:{first.get(),second.get()})
        {
            std::cout<<"NFP-PROFILE route="<<name<<" concurrency=2 instance="<<instance++<<" seconds="<<m.Seconds
                <<" placed="<<m.Placed<<" required="<<required<<" complete="<<m.Complete<<" valid="<<m.Valid
                <<" generate-events="<<m.Generated<<" commit-events="<<m.Committed<<" score-events="<<m.Scored
                <<" boundary-fallbacks="<<m.Fallback<<" query-wall-seconds="<<m.QuerySeconds<<" contact-wall-seconds="<<m.ContactSeconds
                <<" nominal-common-find-seconds="<<m.CommonSeconds<<" ordinary-find-seconds="<<m.OrdinarySeconds
                <<" nominal-domain-setup-seconds="<<m.CommonSetup<<" ordinary-domain-setup-seconds="<<m.OrdinarySetup<<std::endl;
            for(const auto& [source,count]:m.Sources)std::cout<<"NFP-SOURCE route="<<name<<" instance="<<instance-1<<" source="<<source<<" generated="<<count<<std::endl;
            for(const auto& [reason,count]:m.ClearanceFilters)std::cout<<"NFP-CLEARANCE-FILTER route="<<name<<" instance="<<instance-1<<" reason="<<reason<<" events="<<count<<std::endl;
            for(const auto& [reason,count]:m.Analyses)std::cout<<"NFP-CLEARANCE-ANALYSIS route="<<name<<" instance="<<instance-1<<" reason="<<reason<<" events="<<count<<std::endl;
            for(const auto& [measure,value]:m.DirectSummary)std::cout<<"NFP-DIRECT-SUMMARY route="<<name<<" instance="<<instance-1<<" measure="<<measure<<" value="<<value<<std::endl;
            for(const auto& [angle,count]:m.CommittedAngles)std::cout<<"NFP-COMMIT-ANGLE route="<<name<<" instance="<<instance-1<<" angle="<<angle<<" members="<<count<<std::endl;
            Require(m.Valid,"profile generated invalid original material");
        }
    }
    return 0;
}
}
void OrderedClearanceKeepsTheWholeOriginalGate()
{
    Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.AllowCommonEdges=false;
    auto part=Rectangle("piece",4,3);part.Geometry.Outer={{0,0},{4,0},{4,3},{0,3}};
    p.Parts={part};p.Stocks={{"sheet",30,20,1}};
    Layout empty{"sheet",0,30,20,{},0};
    Detail::ClearanceNfpPlacement generator(p,p.Stocks[0],{},{});
    Detail::NfpPlacementLimits limits;limits.StopAfterFirstFit=true;
    limits.CandidatePriority=[](Pose pose){return std::array<double,4>{pose.Y,pose.X,0,0};};
    std::size_t rejected=0;std::set<std::uint64_t> originalFits;
    limits.AcceptFit=[&](Pose pose,std::uint64_t){if(pose.X<5){++rejected;return false;}return true;};
    limits.Trace=[&](const ConstructionTraceEvent& event)
    {if(event.Source=="nfp.original-clearance"&&event.Reason=="original-clearance-fit"&&event.Accepted)
        originalFits.insert(event.CandidateID);};
    const auto query=generator.Find(empty,0,0,limits,{});
    Require(rejected>0&&query.Fits.size()==1,"ordered clearance stopped before finding a fully accepted original pose");
    Require(query.Fits.front().X>=5&&query.FitsIDs.size()==1&&originalFits.contains(query.FitsIDs.front()),
        "stored first-safe pose skipped the original guard or lost its observed identity");
    Require(AxisSafety::Containment(AxisSafety::Make(part.Geometry,query.Fits.front()),30,20,.5)==AxisSafety::Verdict::Safe,
        "ordered clearance weakened original containment");
}
void BoundaryViewsRetainOriginalWorldSemantics()
{
    Shape source;source.Outer={{-7,11},{13,11},{13,31},{-7,31}};
    source.Holes={{{-2,16},{3,16},{3,21},{-2,21}}};source.CommonEdgeRings={true,false};
    for(const double angle:{0.,90.,180.,270.,23.7,203.7})
    {
        const Pose pose{1000000.375,-999999.875,angle};
        const auto boundary=AxisSafety::MakeBoundary(source,pose),full=AxisSafety::MakeExact(source,pose);
        Require(boundary.Valid&&full.Valid&&boundary.Roundoff==full.Roundoff&&
            boundary.CommonEdgeRings==full.CommonEdgeRings&&boundary.Rings.size()==full.Rings.size(),
            "boundary view changed original permissions, rounding or ring inventory");
        for(std::size_t r=0;r<full.Rings.size();++r)
        {
            Require(boundary.Rings[r].size()==full.Rings[r].size(),"boundary view simplified an original material ring");
            for(std::size_t i=0;i<full.Rings[r].size();++i)
                Require((boundary.Rings[r][i].X-full.Rings[r][i].X).Sign()==0&&
                    (boundary.Rings[r][i].Y-full.Rings[r][i].Y).Sign()==0,
                    "boundary view did not retain exact-quarter or stored-F64 arbitrary-angle coordinates");
        }
    }
    // Different source triangles share identical transformed source endpoints.
    // This guarantees a real arbitrary-angle seam without a near-line test.
    Shape upper,lower;upper.Outer={{0,0},{10,0},{0,10}};lower.Outer={{0,0},{0,-10},{10,0}};
    const Pose oblique{10,20,23.7};
    const auto a=AxisSafety::MakeBoundary(upper,oblique),b=AxisSafety::MakeBoundary(lower,oblique);
    Require(AxisSafety::CommonEdge(a,b,1e-7,true)&&
        AxisSafety::PairExact(AxisSafety::MakeExact(upper,oblique),AxisSafety::MakeExact(lower,oblique),.2,true,1e-7)==
            AxisSafety::Verdict::Safe,"boundary preparation lost a genuine stored-coordinate arbitrary-angle seam");
    Shape frame,plug;frame.Outer={{0,0},{20,0},{20,20},{0,20}};
    frame.Holes={{{5,5},{10,5},{10,10},{5,10}}};plug.Outer={{0,0},{5,0},{5,5},{0,5}};
    for(const bool allowed:{false,true})
    {
        frame.CommonEdgeRings={true,allowed};
        const auto host=AxisSafety::MakeBoundary(frame,{.5,.5,0}),insert=AxisSafety::MakeBoundary(plug,{5.5,5.5,0});
        Require(AxisSafety::CommonEdge(host,insert,1e-7,true)==allowed,
            "boundary view ignored original hole seam permission");
        const auto reference=AxisSafety::PairExact(AxisSafety::MakeExact(frame,{.5,.5,0}),
            AxisSafety::MakeExact(plug,{5.5,5.5,0}),.2,true,1e-7);
        Require(reference==(allowed?AxisSafety::Verdict::Safe:AxisSafety::Verdict::Blocked),
            "hole permission fixture does not distinguish the complete original gap rule");
    }
}
void CommonGapScreenRejectsOnlyImpossibleStoredPoses()
{
    for(const double angle:{0.,90.,23.7})
    {
        Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.AllowCommonEdges=true;
        auto fixed=Rectangle("fixed",8,6),moving=Rectangle("moving",3,4);
        fixed.Geometry.Outer={{0,0},{8,0},{0,6}};moving.Geometry.Outer={{0,0},{3,0},{3,4},{0,4}};
        // A real source hole excludes the identical-common native certificate;
        // this retained fallback still needs the necessary-gap screen.
        fixed.Geometry.Holes={{{1,1},{2,1},{1,2}}};
        fixed.Geometry.CommonEdgeRings={true,false};moving.Geometry.CommonEdgeRings={true};
        fixed.FreeRotation=true;moving.FreeRotation=true;
        p.Parts={fixed,moving};p.Stocks={{"sheet",40,40,1}};
        Layout board{"sheet",0,40,40,{{"fixed",0,{10,10,angle},0,false}},24};
        Detail::ClearanceNfpPlacement generator(p,p.Stocks[0],{},{});
        Require(generator.Ready(),"necessary-gap screen fixture is unsupported");
        Detail::NfpPlacementLimits limits;limits.StopAfterFirstFit=true;limits.MaxCandidates=0;
        limits.MaxAttempts=30000;limits.MaxRawCandidates=512;
        const auto target=Detail::Transform({4,3},board.Placements[0].Transform);
        limits.CandidatePriority=[&](Pose pose)
        {
            const auto box=Detail::Bounds(moving.Geometry,pose);
            return std::array<double,4>{std::hypot(box.X+box.Width/2-target.X,box.Y+box.Height/2-target.Y),pose.Y,pose.X,0};
        };
        std::vector<Pose> screened;std::size_t commonRetreats=0;
        std::map<std::uint64_t,std::size_t> commonRetreatFamilies;
        std::map<std::string,double> summary;
        limits.Trace=[&](const ConstructionTraceEvent& event)
        {
            double stream=-1;for(const auto& [name,value]:event.Measures)if(name=="nominal-common-domain")stream=value;
            if(event.Reason=="original-clearance-common-gap-witness"&&!event.Members.empty())
                screened.push_back(event.Members[0].Transform);
            if(stream==1&&event.Source=="nfp.roundoff-retreat"&&event.Stage==ConstructionTraceStage::Generate)
            {++commonRetreats;++commonRetreatFamilies[event.CandidateID];}
            if(event.Source=="nfp.original-clearance"&&event.Stage==ConstructionTraceStage::Complete)
                for(const auto& [name,value]:event.Measures)summary[name]=value;
        };
        const auto query=generator.Find(board,1,angle,limits,{});
        if(screened.empty()||commonRetreats==0||query.Fits.empty())
        {
            std::cerr<<"gap-fixture angle="<<angle<<" screened="<<screened.size()<<" retreats="<<commonRetreats
                <<" fits="<<query.Fits.size()<<" attempts="<<query.Attempts<<" summary:";
            for(const auto& [name,value]:summary)if(name.starts_with("common")||name.starts_with("nativeOriginal"))
                std::cerr<<' '<<name<<'='<<value;
            std::cerr<<'\n';
        }
        Require(!screened.empty()&&commonRetreats>0&&!query.Fits.empty(),
            "necessary-gap fixture did not exercise screening, finite common retries and a later legal fit");
        Require(std::any_of(commonRetreatFamilies.begin(),commonRetreatFamilies.end(),[](const auto& family){return family.second==24;}),
            "necessary-gap screening cut an established 8-direction/3-level common retreat family");
        Require(summary["commonGapScreenRejected"]==double(screened.size())&&summary["commonGapScreenCalls"]>=double(screened.size()),
            "complete query omitted exact necessary-screen accounting");
        const auto rejectedTotal=summary["commonGapScreenRejected"]+summary["originalGuardStopped"]+
            summary["originalGuardRejectedCaller"]+summary["originalGuardRejectedSheet"]+
            summary["originalGuardRejectedContainment"]+summary["originalGuardRejectedUnknownID"]+
            summary["originalGuardRejectedMaterial"]+summary["originalGuardRejectedPair"]+summary["originalGuardAccepted"];
        Require(rejectedTotal==summary["originalGuardCalls"],"complete query lost original-guard rejection categories");
        const auto fixedExact=AxisSafety::MakeExact(fixed.Geometry,board.Placements[0].Transform);
        bool sourceValidatorRejected=false;
        for(const auto pose:screened)
        {
            // Independent full preparation and unfiltered topology/distance
            // checks must reject every actual fast-screened stored pose.
            Require(AxisSafety::PairExact(AxisSafety::MakeExact(moving.Geometry,pose),fixedExact,.2,true,p.Tolerance)==
                AxisSafety::Verdict::Blocked,"necessary-gap screen rejected a pose accepted by the complete exact original pair guard");
            if(!sourceValidatorRejected)
            {
                Result candidate;candidate.State=Status::Complete;candidate.Layouts={board};
                candidate.Layouts[0].Placements.push_back({"moving",0,pose,0,false});
                sourceValidatorRejected=!Validate(p,candidate).Valid;
            }
        }
        Require(sourceValidatorRejected,"screen fixture never reached a real source-material gap rejection");
        for(const auto pose:query.Fits)
        {
            Result candidate;candidate.State=Status::Complete;candidate.Layouts={board};
            candidate.Layouts[0].Placements.push_back({"moving",0,pose,0,false});
            Require(Validate(p,candidate).Valid&&AxisSafety::PairExact(AxisSafety::MakeExact(moving.Geometry,pose),
                fixedExact,.2,true,p.Tolerance)==AxisSafety::Verdict::Safe,
                "screened common query published a private-only or invalid original-material fit");
        }
    }
}
void NativeCommonVerificationRetainsRepairableProposals()
{
    Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.AllowCommonEdges=true;
    auto fixed=Rectangle("fixed",2,2),moving=Rectangle("moving",2,2);
    fixed.Geometry.Outer={{0,0},{2,0},{2,2},{0,2}};moving.Geometry=fixed.Geometry;
    fixed.Geometry.CommonEdgeRings={true};moving.Geometry.CommonEdgeRings={true};p.Parts={fixed,moving};
    p.Stocks={{"sheet",30,30,1}};Layout board{"sheet",0,30,30,{{"fixed",0,{20,20,0},0,false}},4};
    Detail::ClearanceNfpPlacement generator(p,p.Stocks[0],{},{});
    Detail::NfpPlacementLimits limits;limits.StopAfterFirstFit=true;
    limits.CandidatePriority=[](Pose pose){return std::array<double,4>{pose.Y,pose.X,0,0};};
    // The low/left candidate is rejected by an additional domain gate, and a
    // finite diagonal representable retry is legal. This must remain a retry
    // when the same-source common stream verifies the original positive gap.
    limits.AcceptFit=[](Pose pose,std::uint64_t){return pose.X>.5&&pose.Y>.5;};
    std::size_t commonRetreats=0;bool verifiedCommon=false;
    limits.Trace=[&](const ConstructionTraceEvent& event)
    {
        double stream=-1;for(const auto& [name,value]:event.Measures)if(name=="nominal-common-domain")stream=value;
        if(stream==1&&event.Source=="nfp.roundoff-retreat"&&event.Stage==ConstructionTraceStage::Generate)++commonRetreats;
        if(event.Source=="nfp.original-clearance"&&event.Stage==ConstructionTraceStage::Complete)
            for(const auto& [name,value]:event.Measures)if(name=="nativeOriginalVerificationQueries"&&value>0)verifiedCommon=true;
    };
    const auto query=generator.Find(board,1,0,limits,{});
    Require(commonRetreats>0&&verifiedCommon&&std::any_of(query.Fits.begin(),query.Fits.end(),[](Pose pose)
        {return pose.X>.5&&pose.X<.5001&&pose.Y>.5&&pose.Y<.5001;}),
        "native original-gap verification removed the finite repair of an original proposal");
}
void IdenticalCommonNativeCertificateReplacesOnlyTheDuplicateGate()
{
    for(const double angle:{0.,90.,23.7})for(const int excluded:{0,1,2,3,4})
    {
        Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.AllowCommonEdges=true;
        auto fixed=Rectangle("fixed",8,6),moving=Rectangle("moving",3,4);
        fixed.Geometry.Outer={{0,0},{8,0},{0,6}};moving.Geometry.Outer={{0,0},{3,0},{3,4},{0,4}};
        // Keep real authorized parallel directions in every case. Excluded
        // originals must enter the common fallback and decline only the native
        // duplicate-gate certificate, rather than skip common by empty edges.
        fixed.Geometry.CommonEdgeRings={true};moving.Geometry.CommonEdgeRings={true};
        fixed.FreeRotation=true;moving.FreeRotation=true;
        p.Parts={fixed,moving};p.Stocks={{"sheet",40,40,1}};
        Layout board{"sheet",0,40,40,{{"fixed",0,{10,10,angle},0,false}},24};
        std::set<std::string> zeroGap;
        if(excluded==1||excluded==2)
        {
            auto extra=Rectangle("other-fixed",4,4);extra.Geometry.Outer={{0,0},{4,0},{4,4},{0,4}};
            if(excluded==1)
            {extra.Geometry.Holes={{{1,1},{3,1},{3,3},{1,3}}};extra.Geometry.CommonEdgeRings={false,false};}
            else {extra.Geometry={};extra.Geometry.Radius=1;}
            p.Parts.push_back(extra);board.Placements.push_back({"other-fixed",0,{30,30,0},0,false});
        }
        if(excluded==3)
        {p.Parts[1].Geometry.Holes={{{1,1},{2,1},{2,2},{1,2}}};p.Parts[1].Geometry.CommonEdgeRings={true,false};}
        if(excluded==4)zeroGap.insert("immutable-domain-obstacle");
        Detail::ClearanceNfpPlacement generator(p,p.Stocks[0],{},{},zeroGap);
        Require(generator.Ready(),"native original certificate fixture is unsupported");
        Detail::NfpPlacementLimits limits;limits.StopAfterFirstFit=true;limits.MaxCandidates=0;
        limits.MaxAttempts=30000;limits.MaxRawCandidates=512;
        const auto target=Detail::Transform({4,3},board.Placements[0].Transform);
        limits.CandidatePriority=[&](Pose pose)
        {
            const auto box=Detail::Bounds(moving.Geometry,pose);
            return std::array<double,4>{std::hypot(box.X+box.Width/2-target.X,box.Y+box.Height/2-target.Y),pose.Y,pose.X,0};
        };
        std::map<std::string,double> summary;std::vector<Pose> nativeFits,earlyScreened;
        bool sawNativeGap=false;std::size_t commonQueries=0;std::map<std::uint64_t,std::size_t> commonRetreats;
        limits.Trace=[&](const ConstructionTraceEvent& event)
        {
            std::map<std::string,double> measures;for(const auto& [name,value]:event.Measures)measures[name]=value;
            if(event.Source=="nfp.original-clearance"&&event.Stage==ConstructionTraceStage::Complete)summary=measures;
            if(event.Source=="nfp.original-clearance"&&event.Reason=="original-clearance-fit"&&
                measures["native-original-verification"]==1&&!event.Members.empty())nativeFits.push_back(event.Members[0].Transform);
            if(event.Source=="nfp.original-clearance"&&event.Reason=="original-clearance-common-gap-witness"&&
                measures["native-original-verification"]==1&&!event.Members.empty())earlyScreened.push_back(event.Members[0].Transform);
            if(event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Begin&&measures["nominal-common-domain"]==1)
            {
                ++commonQueries;
                if(measures["verification-gap-override"]==1&&measures["proposal-gap"]==0&&measures["verification-gap"]==.2)
                    sawNativeGap=true;
            }
            if(event.Source=="nfp.roundoff-retreat"&&event.Stage==ConstructionTraceStage::Generate&&measures["nominal-common-domain"]==1)
                ++commonRetreats[event.CandidateID];
        };
        const auto query=generator.Find(board,1,angle,limits,{});
        if(commonQueries!=1||summary["commonDomainSkippedByDirectionProof"]!=0||
            (excluded==0?(!sawNativeGap||nativeFits.empty()):sawNativeGap))
            std::cerr<<"native-fixture angle="<<angle<<" exclusion="<<excluded<<" commonQueries="<<commonQueries
                <<" proofSkip="<<summary["commonDomainSkippedByDirectionProof"]<<" nativeQueries="
                <<summary["nativeOriginalVerificationQueries"]<<" nativeFits="<<nativeFits.size()
                <<" retreatFamilies="<<commonRetreats.size()<<" fits="<<query.Fits.size()<<'\n';
        Require(!query.Fits.empty(),"single original native verification lost all original legal placements");
        Require(commonQueries==1&&summary["commonDomainSkippedByDirectionProof"]==0,
            "native certificate boundary fixture skipped the actual authorized common domain");
        if(excluded==0)
        {
            Require(sawNativeGap&&summary["nativeOriginalVerificationQueries"]==1&&
                summary["nativeOriginalVerifiedFits"]>=1&&!nativeFits.empty()&&summary["commonGapScreenCalls"]>0&&
                !earlyScreened.empty(),
                "identical common stream failed to replace the duplicate geometry gate with an explicit original-gap certificate");
            Require(std::any_of(commonRetreats.begin(),commonRetreats.end(),[](const auto& family){return family.second==24;}),
                "native necessary-gap screening swallowed the complete established finite representable retry family");
            for(const auto pose:earlyScreened)
                Require(AxisSafety::PairExact(AxisSafety::MakeExact(p.Parts[1].Geometry,pose),
                    AxisSafety::MakeExact(p.Parts[0].Geometry,board.Placements[0].Transform),.2,true,p.Tolerance)==AxisSafety::Verdict::Blocked,
                    "native necessary-gap screening rejected a pose accepted by the full exact original pair gate");
        }
        else Require(!sawNativeGap&&summary["nativeOriginalVerificationQueries"]==0&&nativeFits.empty(),
            "native original certificate crossed a source-hole, circle or typed zero-gap boundary");
        if(excluded==1||excluded==3)
            Require(summary["commonGapScreenCalls"]>0,"source-hole exclusion failed to exercise the original-material fallback gate");
        const auto guardDecisions=summary["commonGapScreenRejected"]+summary["originalGuardStopped"]+
            summary["originalGuardRejectedCaller"]+summary["originalGuardRejectedSheet"]+
            summary["originalGuardRejectedContainment"]+summary["originalGuardRejectedUnknownID"]+
            summary["originalGuardRejectedMaterial"]+summary["originalGuardRejectedPair"]+summary["originalGuardAccepted"];
        Require(guardDecisions==summary["originalGuardCalls"],
            "native necessary screens lost the exact original-guard decision accounting or approved a screened pose");
        for(const auto pose:query.Fits)
        {
            Result candidate;candidate.State=Status::Complete;candidate.Layouts={board};
            candidate.Layouts[0].Placements.push_back({"moving",0,pose,0,false});
            Require(Validate(p,candidate).Valid,"single original native verification changed source demand, material or final coordinates");
            for(const auto& obstacle:board.Placements)
            {
                const auto source=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const auto& part){return part.ID==obstacle.PartID;});
                Require(source!=p.Parts.end()&&AxisSafety::PairExact(AxisSafety::MakeExact(p.Parts[1].Geometry,pose),
                    AxisSafety::MakeExact(source->Geometry,obstacle.Transform),.2,true,p.Tolerance)==AxisSafety::Verdict::Safe,
                    "reused native certificate accepted a final pose rejected by the independent full exact original guard");
            }
        }
    }
}
void NativeCommonFullBudgetRetainsLegacySafeFits()
{
    for(const double angle:{0.,23.7})
    {
        Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.AllowCommonEdges=true;
        auto fixed=Rectangle("fixed",8,6),moving=Rectangle("moving",3,4);
        fixed.Geometry.Outer={{0,0},{8,0},{0,6}};moving.Geometry.Outer={{0,0},{3,0},{3,4},{0,4}};
        fixed.Geometry.CommonEdgeRings={false};moving.Geometry.CommonEdgeRings={false};moving.FreeRotation=true;
        p.Parts={fixed,moving};p.Stocks={{"sheet",40,40,1}};
        Layout board{"sheet",0,40,40,{{"fixed",0,{10,10,0},0,false}},24};
        Detail::NfpPlacementLimits limits;limits.MaxCandidates=0;limits.MaxAttempts=200000;limits.MaxRawCandidates=1024;
        // A typed obstacle set with no matching source identity changes no
        // geometry or pair gap, but conservatively keeps the legacy two-gate
        // wrapper path. No production compatibility switch is introduced.
        Detail::ClearanceNfpPlacement legacy(p,p.Stocks[0],{},{},{"unused-domain-control"});
        Detail::ClearanceNfpPlacement native(p,p.Stocks[0],{},{});
        const auto before=legacy.Find(board,1,angle,limits,{}),after=native.Find(board,1,angle,limits,{});
        Require(!before.Fits.empty(),"legacy safe-set fixture has no original legal placements");
        std::set<std::tuple<double,double,double>> actual;
        for(const auto pose:after.Fits)
        {
            actual.emplace(pose.X,pose.Y,pose.AngleDegrees);
            Require(AxisSafety::PairExact(AxisSafety::MakeExact(moving.Geometry,pose),
                AxisSafety::MakeExact(fixed.Geometry,board.Placements[0].Transform),.2,true,p.Tolerance)==AxisSafety::Verdict::Safe,
                "new finite native proposals escaped the complete exact original pair rule");
        }
        for(const auto pose:before.Fits)
            Require(actual.contains({pose.X,pose.Y,pose.AngleDegrees}),
                "single native original certificate lost a previously safe stored pose with sufficient search budget");
    }
}
void ArbitraryAnglePairUsesASafePlanningCarrier()
{
    Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.AllowCommonEdges=false;
    Part part;part.ID="oblique";part.Quantity=2;part.FreeRotation=false;
    part.AllowedAnglesDegrees={23.7,203.7};
    part.Geometry.Outer={{2000000,-3000000},{2000008,-3000000},{2000000,-2999994}};
    p.Parts={part};p.Stocks={{"sheet",11.2,10.2,1}};
    auto work=Limits();work.EnableCombinations=true;work.EnableGapFill=false;work.MaxPatternMembers=2;
    work.MaxPlacementAttempts=500000;
    PlaneStrategySettings settings;settings.Kind=PlaneStrategyKind::NfpArray;
    settings.EnableContourSettling=false;settings.EnableSheetTemplateReuse=false;
    settings.EnableIndependentPairArrays=false;settings.EnableHoleNesting=false;
    const auto result=MakePlaneStrategyFactory(settings)()->Run(p,work);
    Require(Validate(p,result).Complete&&result.Layouts.size()==1,
        "arbitrary-angle self pair was rejected at its normalized carrier boundary");
    const auto& board=result.Layouts.front();Require(board.Placements.size()==2,"pair changed original quantity");
    Require(board.Placements[0].Group==board.Placements[1].Group&&
        std::abs(std::abs(std::remainder(board.Placements[0].Transform.AngleDegrees-
            board.Placements[1].Transform.AngleDegrees,360.))-180)<1e-7,
        "fixture did not consume an actual opposite-angle pair template");
    for(const auto& item:board.Placements)
        Require(AxisSafety::Containment(AxisSafety::Make(part.Geometry,item.Transform),board.Width,board.Height,p.EdgeMargin)==
            AxisSafety::Verdict::Safe,"planning inset weakened final nominal margin validation");
    Require(AxisSafety::Pair(AxisSafety::Make(part.Geometry,board.Placements[0].Transform),
        AxisSafety::Make(part.Geometry,board.Placements[1].Transform),p.PartGap,false,p.Tolerance)==AxisSafety::Verdict::Safe,
        "planning carrier changed the original member gap");
}
void PlacementsMeasureTheWholeStockWidthUsedSection()
{
    Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.AllowCommonEdges=true;
    Part first;first.ID="repeat-triangle";first.Quantity=4;first.Material="steel";first.FreeRotation=false;
    first.AllowedAnglesDegrees={0,180};first.Geometry.Outer={{0,0},{6,0},{0,6}};
    auto other=first;other.ID="different-original";other.Quantity=1;other.AllowedAnglesDegrees={0};
    other.Geometry.Outer={{0,0},{.125,0},{.125,.125},{0,.125}};
    p.Parts={first,other};p.Stocks={{"sheet",22,14,1,1,"steel"}};const auto original=p;
    for(const auto kind:{PlaneStrategyKind::NfpArray,PlaneStrategyKind::NfpMultiArray})
    {
        auto work=Limits();work.TimeLimitSeconds=10;work.MaxPlacementAttempts=500000;
        work.EnableCombinations=true;work.EnableGapFill=false;work.MaxPatternMembers=4;work.MaxPatterns=64;
        PlaneStrategySettings settings;settings.Kind=kind;settings.EnableContourSettling=false;
        settings.EnableLayoutRefinement=false;settings.EnableSheetTemplateReuse=false;settings.EnableHoleNesting=false;
        bool rankFormula=true;
        std::size_t pairTemplates=0,fourTemplates=0,mixedTemplates=0,mixedCandidates=0;
        std::set<std::size_t> selfTemplates;
        std::vector<ConstructionTraceMember> denseSelfPair;
        std::set<std::uint64_t> selfCandidates;
        std::map<std::uint64_t,std::pair<std::size_t,double>> ranked;
        std::map<std::uint64_t,double> bboxFill;
        std::uint64_t firstCommit=0;
        const auto oneOriginal=[](const auto& members)
        {return !members.empty()&&std::all_of(members.begin(),members.end(),[&](const auto& m){return m.PartIndex==members.front().PartIndex;});};
        work.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            if(event.Source=="preparation.retained-template")
            {
                if(!oneOriginal(event.Members))++mixedTemplates;
                if(event.Members.size()==2&&oneOriginal(event.Members))
                {
                    ++pairTemplates;
                    if(event.Members.front().PartIndex==0)
                    {
                        selfTemplates.insert(event.ProposalID-1);
                        if(event.Width>0&&event.Height>0&&event.MaterialArea/(event.Width*event.Height)>.999999&&
                            denseSelfPair.empty())denseSelfPair=event.Members;
                    }
                }
                if(event.Members.size()==4&&oneOriginal(event.Members)&&event.Members.front().PartIndex==0)
                    ++fourTemplates;
            }
            if((event.Source=="array.rank"||event.Source=="array.grid"||event.Source=="array.contour-pose"||
                event.Source=="array.independent-pair"||event.Source=="array.tail-partition"||
                event.Source=="array.seed-growth"||event.Source=="carrier.merge")&&
                event.Members.size()>1&&!oneOriginal(event.Members))++mixedCandidates;
            if(event.Source=="array.grid"||event.Source=="array.contour-pose"||event.Source=="array.independent-pair"||event.Source=="array.tail-partition")
            {
                if(event.Accepted&&event.Stage==ConstructionTraceStage::Filter)
                    for(const auto& [name,value]:event.Measures)
                        if(name=="template"&&selfTemplates.contains(static_cast<std::size_t>(value)))selfCandidates.insert(event.CandidateID);
            }
            if(event.Source=="array.rank"&&!firstCommit)
            {
                std::map<std::string,double> measures;for(const auto& [name,value]:event.Measures)measures[name]=value;
                double material=0;for(const auto& member:event.Members)material+=Detail::Area(p.Parts[member.PartIndex].Geometry);
                double top=0;
                for(const auto& member:event.Members)
                {const auto box=Detail::Bounds(p.Parts[member.PartIndex].Geometry,member.Transform);top=std::max(top,box.Y+box.Height);}
                const double section=p.Stocks[0].Width*std::clamp(top+p.EdgeMargin,0.,p.Stocks[0].Height);
                const double expected=material/measures["usedSectionArea"];
                rankFormula&=std::abs(expected-measures["localUtilization"])<1e-12&&
                    std::abs(section-measures["usedSectionArea"])<1e-5&&
                    std::abs(material-measures["candidateNetMaterialArea"])<1e-12&&
                    !event.Rank.empty()&&event.Rank.front().first=="negativeUsedSectionUtilization"&&
                    std::abs(event.Rank.front().second+expected)<1e-12;
                ranked[event.CandidateID]={event.Members.size(),expected};
                bboxFill[event.CandidateID]=material/measures["occupiedPlanningArea"];
            }
            if(event.Source=="constructor.commit")
            {
                if(!firstCommit)firstCommit=event.CandidateID;
            }
        };
        const auto result=MakePlaneStrategyFactory(settings)()->Run(p,work);
        const auto checked=Validate(p,result);
        Require(checked.Valid&&checked.Complete&&checked.PlacedCount==5,
            "utilization-ranked array lost original demand or material safety");
        Require(rankFormula&&mixedTemplates==0&&mixedCandidates==0&&ranked.contains(firstCommit),
            "self-only candidates mixed original identities or changed the real whole-width utilization formula");
        const auto chosen=ranked.at(firstCommit);
        Require(std::all_of(ranked.begin(),ranked.end(),[&](const auto& item)
            {return item.second.second<=chosen.second+1e-12;}),
            "an AABB score displaced the candidate with better actual whole-width used-section utilization");
        // Compare a real certified two-original carrier with this run's real
        // four-original array candidates. Its local full AABB is not the
        // amount of fixed-width stock consumed by that small partial order.
        Require(!denseSelfPair.empty(),"self-only preparation lost its dense two-original counterexample");
        Result pairWitness;pairWitness.State=Status::Partial;
        Layout pairBoard{"sheet",0,22,14,{},0};double pairTop=0;
        std::optional<Rect> pairBounds;
        for(std::size_t i=0;i<denseSelfPair.size();++i)
        {
            auto pose=denseSelfPair[i].Transform;pose.X+=p.EdgeMargin+1e-6;pose.Y+=p.EdgeMargin+1e-6;
            pairBoard.Placements.push_back({first.ID,i,pose,1,false});pairBoard.MaterialArea+=Detail::Area(first.Geometry);
            const auto box=Detail::Bounds(first.Geometry,pose);pairTop=std::max(pairTop,box.Y+box.Height);
            if(!pairBounds)pairBounds=box;
            else
            {
                const auto x=std::min(pairBounds->X,box.X),y=std::min(pairBounds->Y,box.Y);
                pairBounds=Rect{x,y,std::max(pairBounds->X+pairBounds->Width,box.X+box.Width)-x,
                    std::max(pairBounds->Y+pairBounds->Height,box.Y+box.Height)-y};
            }
        }
        const double pairUtilization=pairBoard.MaterialArea/(p.Stocks[0].Width*(pairTop+p.EdgeMargin));
        const double pairAabbFill=pairBoard.MaterialArea/(pairBounds->Width*pairBounds->Height);
        pairWitness.Layouts={pairBoard};pairWitness.Remaining={{first.ID,first.Quantity-denseSelfPair.size()},{other.ID,other.Quantity}};
        const auto pairCheck=Validate(p,pairWitness);
        bool sideBlankCounterexample=false;
        for(const auto& candidate:ranked)if(candidate.second.first==4&&
            bboxFill[candidate.first]+1e-8<pairAabbFill&&candidate.second.second>pairUtilization+1e-8)
            sideBlankCounterexample=true;
        Require(pairTemplates>0&&fourTemplates>0&&!selfCandidates.empty()&&pairCheck.Valid&&
            pairCheck.PlacedCount==2&&pairAabbFill>.999999&&sideBlankCounterexample,
            "fixture lost the legal lower-AABB-density self array with better full-width stock-section utilization");
        for(const auto& board:result.Layouts)for(std::size_t i=0;i<board.Placements.size();++i)
        {
            const auto& placed=board.Placements[i];const auto index=placed.PartID==first.ID?0u:1u;
            Require(Detail::PoseAllowed(p.Parts[index],placed.Transform,p.Tolerance),
                "self-only scoring widened an original rotation or reflection permission");
            const auto geometry=AxisSafety::MakeExact(p.Parts[index].Geometry,placed.Transform);
            Require(AxisSafety::Containment(geometry,board.Width,board.Height,p.EdgeMargin)==AxisSafety::Verdict::Safe,
                "same-specification array weakened original board margin");
            for(std::size_t j=0;j<i;++j)
            {
                const auto& fixed=board.Placements[j];const auto fixedIndex=fixed.PartID==first.ID?0u:1u;
                Require(AxisSafety::PairExact(geometry,AxisSafety::MakeExact(p.Parts[fixedIndex].Geometry,fixed.Transform),
                    p.PartGap,p.AllowCommonEdges,p.Tolerance)==AxisSafety::Verdict::Safe,
                    "same-specification array weakened exact original gap or seam permission");
            }
        }
    }
    // Equal source contours with different inventory IDs remain different
    // types. Each may form its own self carrier, never a cross-ID carrier.
    auto mixedProblem=p;mixedProblem.Parts[1]=first;mixedProblem.Parts[1].ID="complementary-original";
    auto work=Limits();work.TimeLimitSeconds=10;work.MaxPlacementAttempts=500000;
    work.EnableCombinations=true;work.EnableGapFill=false;work.MaxPatternMembers=4;work.MaxPatterns=64;
    std::size_t mixedTemplates=0,mixedCandidates=0,selfFour=0;
    work.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        const bool mixed=event.Members.size()>1&&std::any_of(event.Members.begin(),event.Members.end(),
            [&](const auto& member){return member.PartIndex!=event.Members.front().PartIndex;});
        if(event.Source=="preparation.retained-template")
        {
            if(mixed)++mixedTemplates;
            else if(event.Members.size()==4)++selfFour;
        }
        if((event.Source=="array.rank"||event.Source=="array.grid"||event.Source=="array.contour-pose"||
            event.Source=="array.independent-pair"||event.Source=="array.tail-partition"||
            event.Source=="array.seed-growth"||event.Source=="carrier.merge")&&mixed)++mixedCandidates;
    };
    PlaneStrategySettings settings;settings.Kind=PlaneStrategyKind::ArrayMultiSpecificationSearch;
    settings.EnableContourSettling=false;settings.EnableLayoutRefinement=false;settings.EnableSheetTemplateReuse=false;
    settings.EnableHoleNesting=false;settings.MaxNodes=8;settings.MaxCandidates=8;settings.BranchWidth=2;
    const auto mixed=MakePlaneStrategyFactory(settings)()->Run(mixedProblem,work);
    Require(Validate(mixedProblem,mixed).Valid&&mixedTemplates==0&&mixedCandidates==0&&selfFour>0,
        "identical geometry under different original IDs was mixed or lost its legal same-ID four-original carrier");
    for(std::size_t i=0;i<p.Parts.size();++i)
        Require(p.Parts[i].ID==original.Parts[i].ID&&p.Parts[i].Quantity==original.Parts[i].Quantity&&
            p.Parts[i].Material==original.Parts[i].Material&&p.Parts[i].AllowedAnglesDegrees==original.Parts[i].AllowedAnglesDegrees&&
            std::equal(p.Parts[i].Geometry.Outer.begin(),p.Parts[i].Geometry.Outer.end(),original.Parts[i].Geometry.Outer.begin(),
                [](Point a,Point b){return a.X==b.X&&a.Y==b.Y;}),"array template policy changed an immutable original");
}
void CompleteChannelsCompeteWithAlreadyLegalVertices()
{
    Problem p;p.Tolerance=1e-7;p.PartGap=0;p.AllowCommonEdges=false;p.Stocks={{"sheet",30,5,1}};
    auto host=Rectangle("host",14,5),plug=Rectangle("plug",10,3);
    host.Geometry.Outer={{0,0},{2,0},{2,3},{12,3},{12,0},{14,0},{14,5},{0,5}};
    plug.Geometry.Outer={{0,0},{10,0},{10,3},{0,3}};p.Parts={host,plug};
    Layout fixed{"sheet",0,30,5,{{"host",0,{0,0,0},0,false}},40};
    Detail::ClearanceNfpPlacement generator(p,p.Stocks[0],{},{});
    Detail::NfpPlacementLimits vertices;vertices.BoundaryVerticesOnly=true;vertices.MaxCandidates=0;
    const auto ordinary=generator.Find(fixed,1,0,vertices,{});
    Require(!ordinary.Fits.empty(),"fixture must already have legal outside free-domain vertices");
    Require(std::none_of(ordinary.Fits.begin(),ordinary.Fits.end(),[](Pose pose)
        {return std::abs(pose.X-2)<1e-6&&std::abs(pose.Y)<1e-6;}),
        "fixture no longer separates the better collapsed channel from ordinary vertices");
    std::size_t fullQueries=0,vertexQueries=0;auto work=Limits();
    work.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Begin)
            for(const auto& [name,value]:event.Measures)if(name=="boundary-vertices-only")
            {if(value==0)++fullQueries;else ++vertexQueries;}
    };
    Result initial;initial.State=Status::Partial;initial.Layouts={fixed};initial.Remaining={{"plug",1}};
    const auto result=MakePlaneNfpFillStage().Factory()->Run(p,initial,work);
    Require(Validate(p,result).Valid&&Validate(p,result).Complete,
        "full-contact low baseline must construct both originals");
    const auto placed=std::find_if(result.Layouts.front().Placements.begin(),result.Layouts.front().Placements.end(),
        [](const Placement& item){return item.PartID=="plug";});
    Require(placed!=result.Layouts.front().Placements.end()&&std::abs(placed->Transform.X-2)<1e-6&&
        std::abs(placed->Transform.Y)<1e-6,"an existing legal vertex suppressed the better inner contact channel");
    Require(fullQueries>0&&vertexQueries==0,"constructor did not use one complete contact stream per query");
}
void LowBaselineCannotBeAttractedToTheFarStockEdge()
{
    Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.Stocks={{"sheet",40,1000000,1}};
    auto base=Rectangle("base",10,10),wedge=Rectangle("wedge",12,10);
    base.Geometry.Outer={{0,0},{10,0},{10,10},{0,10}};
    wedge.Geometry.Outer={{0,0},{12,10},{0,10}};p.Parts={base,wedge};
    const auto result=Construct(p);
    Require(Validate(p,result).Complete,"long-stock low baseline did not place both originals");
    double top=0;for(const auto& item:result.Layouts.front().Placements)
        top=std::max(top,item.Transform.Y+10);
    Require(top<25,"contact with the million-mm far edge defeated the low skyline baseline");
}
void FirstGreedyTrialKeepsItsConstructionQuota()
{
    Problem p;p.PartGap=.2;p.EdgeMargin=.5;p.Parts={Rectangle("piece",4,3,3)};p.Stocks={{"sheet",30,10,1}};
    auto work=Limits();std::size_t firstCap=0,constructions=0,legacyTrials=0;
    work.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="nfp-trial.budget")++legacyTrials;
        if(event.Source=="constructor"&&event.Stage==ConstructionTraceStage::Begin)
        {++constructions;for(const auto& [name,value]:event.Measures)if(name=="attemptLimit")firstCap=static_cast<std::size_t>(value);}
    };
    PlaneStrategySettings settings;settings.Kind=PlaneStrategyKind::NfpAllSpecifications;
    settings.NfpTrials=6;settings.EnableLayoutRefinement=false;
    const auto result=MakePlaneStrategyFactory(settings)()->Run(p,work);
    Require(Validate(p,result).Complete,"the original-demand greedy construction did not finish");
    Require(firstCap==work.MaxPlacementAttempts,"greedy construction quota was divided among hidden duplicate trials");
    Require(constructions==1&&legacyTrials==0,"legacy trial count still reruns the same NFP construction");
}
void DenseSmallSeedBeatsMoreSparseMaterial()
{
    Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.Stocks={{"sheet",30,20,1}};
    auto sparse=Rectangle("large-sparse",10,10),dense=Rectangle("small-dense",20,2);
    sparse.Geometry.Outer={{0,0},{10,0},{0,10}};dense.Geometry.Outer={{0,0},{20,0},{20,2},{0,2}};
    p.Parts={sparse,dense};auto work=Limits();std::size_t first=std::numeric_limits<std::size_t>::max();
    bool correctMetric=false;
    work.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="constructor.commit"&&first==std::numeric_limits<std::size_t>::max()&&!event.Members.empty())
            first=event.Members.front().PartIndex;
        if(event.Source=="placement.rank"&&event.PartIndex==1)
            for(const auto& [name,value]:event.Measures)if(name=="usedSectionUtilization"&&std::abs(value-40./90)<1e-12)correctMetric=true;
    };
    PlaneStrategySettings settings;settings.Kind=PlaneStrategyKind::NfpAllSpecifications;
    settings.NfpTrials=1;settings.EnableLayoutRefinement=false;
    const auto result=MakePlaneStrategyFactory(settings)()->Run(p,work);
    Require(Validate(p,result).Valid&&Validate(p,result).Complete&&first==1&&correctMetric,
        "area-first seed pruning displaced a denser original with less net material");
}
void EmptyBoardIntrinsicTiesIgnoreWorldTranslation()
{
    Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.Stocks={{"sheet",30,20,1}};
    Part part;part.ID="translated-original";part.FreeRotation=false;part.AllowMirror=true;
    part.AllowedAnglesDegrees={23.7};part.Geometry.Outer={{0,0},{6,0},{1,4}};p.Parts={part};
    std::map<bool,std::pair<double,double>> intrinsic;
    std::map<bool,std::set<std::pair<double,double>>> translations;
    bool measuredWorldArea=false;
    auto work=Limits();work.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
    work.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source!="placement.rank"||event.Members.empty())return;
        const auto pose=event.Members[0].Transform;
        const auto ties=std::pair(RankValue(event,"intrinsicOrientationEnvelopeArea"),
            RankValue(event,"intrinsicFirstMaterialCornerDistance"));
        const auto [found,inserted]=intrinsic.emplace(pose.Mirrored,ties);
        Require(inserted||found->second==ties,
            "the same original orientation changed intrinsic scores under a world translation");
        translations[pose.Mirrored].emplace(pose.X,pose.Y);
        std::map<std::string,double> measures(event.Measures.begin(),event.Measures.end());
        const auto world=Detail::Bounds(part.Geometry,pose);
        const auto section=30*std::clamp(world.Y+world.Height+.5,0.,20.);
        Require(measures.at("occupiedPlanningArea")==world.Width*world.Height&&
            measures.at("usedSectionArea")==section&&
            std::abs(measures.at("usedSectionUtilization")-12/section)<1e-12,
            "intrinsic tie repair changed the final-world area or true material utilization");
        measuredWorldArea=true;
    };
    PlaneStrategySettings settings;settings.Kind=PlaneStrategyKind::NfpClosest;settings.NfpTrials=1;
    settings.EnableLayoutRefinement=false;settings.EnableContourSettling=false;
    settings.EnableSheetTemplateReuse=false;settings.EnableHoleNesting=false;
    const auto result=MakePlaneStrategyFactory(settings)()->Run(p,work);
    const auto checked=Validate(p,result);
    Require(checked.Valid&&checked.Complete&&!work.ConstructionObservation->Failed.load()&&
        work.ConstructionObservation->Omitted.load()==0&&measuredWorldArea&&intrinsic.size()==2&&
        translations[false].size()>=3&&translations[true].size()>=3,
        "translation-invariance regression did not score several real original and mirrored poses");
}
void HigherRealUtilizationPrecedesIntrinsicTies()
{
    Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.Stocks={{"sheet",30,20,1}};
    auto compact=Rectangle("small-intrinsic-envelope",4,4),wide=Rectangle("higher-real-utilization",20,2);
    compact.Geometry.Outer={{0,0},{4,0},{0,4}};p.Parts={compact,wide};
    struct Score{double Utilization=-1,Envelope=0,Corner=0;};
    std::map<std::size_t,Score> scores;std::size_t first=std::numeric_limits<std::size_t>::max();
    auto work=Limits();work.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="constructor.commit"&&first==std::numeric_limits<std::size_t>::max()&&!event.Members.empty())
            first=event.Members[0].PartIndex;
        if(first!=std::numeric_limits<std::size_t>::max()||event.Source!="placement.rank")return;
        const auto u=-RankValue(event,"negativeUsedSectionUtilization");
        auto& score=scores[event.PartIndex];
        if(u>score.Utilization)score={u,RankValue(event,"intrinsicOrientationEnvelopeArea"),
            RankValue(event,"intrinsicFirstMaterialCornerDistance")};
    };
    PlaneStrategySettings settings;settings.Kind=PlaneStrategyKind::NfpAllSpecifications;settings.NfpTrials=1;
    settings.EnableLayoutRefinement=false;settings.EnableContourSettling=false;
    settings.EnableSheetTemplateReuse=false;settings.EnableHoleNesting=false;
    const auto result=MakePlaneStrategyFactory(settings)()->Run(p,work);const auto checked=Validate(p,result);
    Require(checked.Valid&&checked.Complete&&scores.size()==2&&scores.at(1).Utilization>scores.at(0).Utilization&&
        scores.at(1).Envelope>scores.at(0).Envelope&&scores.at(1).Corner>scores.at(0).Corner&&first==1,
        "a better intrinsic envelope or centroid outranked higher final-world utilization");
}
void NonOrthogonalPhaseCompetesWithEveryRetainedOriginalSeed()
{
    constexpr double angle=23.7,side=3;
    const auto radians=angle*Detail::Pi/180,c=std::cos(radians),s=std::sin(radians);
    Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.AllowCommonEdges=true;
    auto part=Rectangle("oblique-original",side,side,2);part.Geometry.Outer={{0,0},{side,0},{side,side},{0,side}};
    part.AllowedAnglesDegrees={angle};p.Parts={part};p.Stocks={{"sheet",6*c+3*s+1+2e-5,6*s+3*c+1+2e-5,1}};
    using PoseKey=std::tuple<double,double,double,bool>;
    const auto poseKey=[](Pose pose){return PoseKey(pose.X,pose.Y,pose.AngleDegrees,pose.Mirrored);};
    std::map<std::uint64_t,std::set<PoseKey>> originals,phases,ranked;
    std::map<PoseKey,std::array<double,4>> firstRanks;
    std::uint64_t firstState=0;bool committed=false;std::optional<Pose> firstPose;
    auto work=Limits();work.TimeLimitSeconds=10;work.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
    work.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="constructor.commit"&&!committed&&!event.Members.empty())
        {committed=true;firstPose=event.Members[0].Transform;return;}
        if(committed||event.Members.empty())return;
        const auto pose=poseKey(event.Members[0].Transform);
        if(event.Source=="nfp.validation"&&event.Reason=="original-material-fit"&&event.Accepted)
            originals[event.StateID].insert(pose);
        if(event.Source=="nfp.original-clearance"&&event.Reason=="first-seed-certified-common-continuation"&&event.Accepted)
            phases[event.StateID].insert(pose);
        if(event.Source=="placement.rank")
        {
            firstState=event.StateID;ranked[firstState].insert(pose);
            firstRanks[pose]={RankValue(event,"negativeUsedSectionUtilization"),RankValue(event,"negativeNetMaterial"),
                RankValue(event,"intrinsicOrientationEnvelopeArea"),RankValue(event,"intrinsicFirstMaterialCornerDistance")};
        }
    };
    PlaneStrategySettings settings;settings.Kind=PlaneStrategyKind::NfpClosest;settings.NfpTrials=1;
    settings.EnableLayoutRefinement=false;settings.EnableContourSettling=false;
    settings.EnableSheetTemplateReuse=false;settings.EnableHoleNesting=false;
    const auto result=MakePlaneStrategyFactory(settings)()->Run(p,work);const auto checked=Validate(p,result);
    Require(checked.Valid&&checked.Complete&&!work.ConstructionObservation->Failed.load()&&
        work.ConstructionObservation->Omitted.load()==0&&result.Layouts.size()==1&&result.Layouts[0].Placements.size()==2,
        "translation-free assessment failed to construct the strict 23.7-degree original common edge");
    const auto& originalSeeds=originals[firstState];const auto& certified=phases[firstState];const auto& scored=ranked[firstState];
    Require(!originalSeeds.empty()&&!certified.empty()&&firstPose&&certified.contains(poseKey(*firstPose)),
        "the selected first pose did not come from an actually certified common-edge phase");
    for(const auto& pose:originalSeeds)Require(scored.contains(pose),
        "first-placement evaluation removed an original safe seed instead of comparing it");
    for(const auto& pose:certified)Require(scored.contains(pose),
        "first-placement evaluation omitted a certified phase from the same candidate comparison");
    bool lowerEqualIntrinsicPhase=false;
    for(const auto& phase:certified)for(const auto& seed:originalSeeds)
        if(std::get<0>(phase)==std::get<0>(seed)&&std::get<1>(phase)<std::get<1>(seed)&&
            firstRanks.at(phase)==firstRanks.at(seed))lowerEqualIntrinsicPhase=true;
    Require(lowerEqualIntrinsicPhase,"an F64 world-envelope or centroid artifact still defeated the lower phase");
    const auto& placed=result.Layouts[0].Placements;
    for(const auto& item:placed)Require(item.Transform.AngleDegrees==angle&&!item.Transform.Mirrored,
        "the scoring fix changed original angle or mirror permissions");
    const auto first=AxisSafety::Make(part.Geometry,placed[0].Transform),second=AxisSafety::Make(part.Geometry,placed[1].Transform);
    Require(AxisSafety::CommonEdge(first,second,p.Tolerance,false)&&
        AxisSafety::PairExact(first,second,p.PartGap,true,p.Tolerance)==AxisSafety::Verdict::Safe,
        "the selected final world poses lost their exact source-material seam or clearance");
}
void OrderPublishesCheckedImprovementsBeforeLaterOrders()
{
    Problem p;p.Parts={Rectangle("long",10,1),Rectangle("dense",4,4),Rectangle("wide",6,2)};
    p.Stocks={{"sheet",30,30,1}};for(auto& part:p.Parts)part.AllowMirror=true;
    const auto original=p;
    class Child final:public IStrategy
    {
        std::function<Result(const Problem&,const Options&)> Work;
    public:
        explicit Child(std::function<Result(const Problem&,const Options&)> work):Work(std::move(work)){}
        bool OwnsRefinementLifecycle()const noexcept override{return true;}
        bool IsPureConstruction()const noexcept override{return true;}
        Result Run(const Problem& input,const Options& options)override{return Work(input,options);}
    };
    for(const bool throwPublisher:{false,true})
    {
        std::size_t calls=0,published=0;bool inherited=true,immediate=true,publicationValid=true;
        std::vector<std::size_t> publicationAt;std::vector<Result> snapshots;
        auto options=Limits();options.TimeLimitSeconds=10;options.Ranking=RankingPolicy::Utilization;
        options.OrientationProposals=OrientationProposalPolicy::OriginalOnly;options.ContinuousConstructionTime=true;
        options.PublishConstructionCandidate=[&](const Result& candidate)
        {
            ++published;publicationAt.push_back(calls);
            const auto checked=Validate(p,candidate);publicationValid&=checked.Valid&&checked.Complete;
            snapshots.push_back(candidate);
            if(throwPublisher)throw std::runtime_error("publication subscriber failed");
        };
        auto factory=[&]
        {
            return std::make_unique<Child>([&](const Problem& input,const Options& childOptions)
            {
                const auto run=calls++;
                inherited&=childOptions.OrientationProposals==OrientationProposalPolicy::OriginalOnly&&
                    childOptions.ContinuousConstructionTime&&static_cast<bool>(childOptions.PublishConstructionCandidate);
                const auto expected=throwPublisher?(run?1u:0u):(run<1?0u:run<3?1u:2u);
                immediate&=published==expected;
                auto result=MakePlaneEnvelopeInitialFactory()()->Run(input,childOptions);
                for(auto& board:result.Layouts)for(auto& item:board.Placements)
                    item.Transform.Y+=run==0?4.:run==2?2.:6.;
                if(run==1&&!result.Layouts.empty())result.Layouts[0].Placements[0].Transform.X=-100;
                result.GeometryValidated=false;return result;
            });
        };
        const auto result=MakePlaneOrderPortfolioFactory(factory,4)()->Run(p,options);
        const auto checked=Validate(p,result);
        Require(checked.Valid&&checked.Complete&&calls==4&&inherited&&immediate&&publicationValid&&
            publicationAt==(throwPublisher?std::vector<std::size_t>{1}:std::vector<std::size_t>{1,3}),
            "order improvements were delayed until route completion, bypassed assessment, lost options or stopped later orders");
        if(!throwPublisher)
        {
            const auto first=Detail::Bounds(p.Parts[0].Geometry,snapshots[0].Layouts[0].Placements[0].Transform);
            const auto second=Detail::Bounds(p.Parts[0].Geometry,snapshots[1].Layouts[0].Placements[0].Transform);
            Require(second.Y<first.Y,"published snapshots failed to retain the actual earlier complete candidate");
        }
        Require(p.Parts.size()==original.Parts.size()&&p.Stocks[0].Quantity==original.Stocks[0].Quantity&&
            p.Parts[0].AllowMirror&&p.Parts[0].Quantity==original.Parts[0].Quantity,
            "streaming order assessment changed source demand, hard permissions or stock");
    }
}
void MirroredOriginalsReachIndependentInitialConstructors()
{
    Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;
    Part part;part.ID="chiral-original";part.Quantity=1;part.FreeRotation=false;part.AllowMirror=true;
    part.AllowedAnglesDegrees={23.7};part.Geometry.Outer={{0,0},{6,0},{1,4}};p.Parts={part};
    const auto reflected=Detail::Bounds(part.Geometry,{0,0,23.7,true}),plain=Detail::Bounds(part.Geometry,{0,0,23.7,false});
    Require(plain.Width>reflected.Width+.1,"mirror-only board fixture lost its asymmetric orientation bounds");
    p.Stocks={{"sheet",reflected.Width+1.0001,reflected.Height+1.0001,1}};
    for(const auto kind:{PlaneStrategyKind::NfpAllSpecifications,PlaneStrategyKind::NfpClosest,
        PlaneStrategyKind::NfpDirectional,PlaneStrategyKind::NfpArray,PlaneStrategyKind::NfpMultiArray,
        PlaneStrategyKind::ArrayGeometryDeferred})
    {
        auto work=Limits();work.EnableCombinations=false;work.EnableGapFill=false;
        PlaneStrategySettings settings;settings.Kind=kind;settings.NfpTrials=1;settings.EnableLayoutRefinement=false;
        settings.EnableContourSettling=false;settings.EnableSheetTemplateReuse=false;settings.EnableHoleNesting=false;
        const auto result=MakePlaneStrategyFactory(settings)()->Run(p,work);
        Require(Validate(p,result).Valid&&Validate(p,result).Complete&&result.Layouts.size()==1&&
            result.Layouts[0].Placements.size()==1&&result.Layouts[0].Placements[0].Transform.Mirrored,
            "independent initial constructor required a combination or lost the only legal mirrored original");
        auto originalProposals=work;originalProposals.OrientationProposals=OrientationProposalPolicy::OriginalOnly;
        const auto originalOnly=MakePlaneStrategyFactory(settings)()->Run(p,originalProposals);
        Require(Validate(p,originalOnly).Valid&&!Validate(p,originalOnly).Complete&&originalOnly.Layouts.empty()&&p.Parts[0].AllowMirror,
            "original-only proposals changed hard mirror permissions or leaked a mirrored initial candidate");
        auto forbidden=p;forbidden.Parts[0].AllowMirror=false;
        const auto denied=MakePlaneStrategyFactory(settings)()->Run(forbidden,work);
        Require(Validate(forbidden,denied).Valid&&!Validate(forbidden,denied).Complete&&denied.Layouts.empty(),
            "initial constructor bypassed an original's explicit mirror prohibition");
    }
    const auto envelope=MakePlaneEnvelopeInitialFactory()()->Run(p,Limits());
    Require(Validate(p,envelope).Complete&&envelope.Layouts[0].Placements[0].Transform.Mirrored,
        "envelope initial construction discarded the real mirrored direction");
    auto originalEnvelope=Limits();originalEnvelope.OrientationProposals=OrientationProposalPolicy::OriginalOnly;
    const auto plainEnvelope=MakePlaneEnvelopeInitialFactory()()->Run(p,originalEnvelope);
    Require(Validate(p,plainEnvelope).Valid&&!Validate(p,plainEnvelope).Complete&&plainEnvelope.Layouts.empty(),
        "envelope proposals ignored the original-only execution policy");
    StrategyRegistry registry;RegisterPlaneStrategies(registry);
    auto restricted=Limits();restricted.OrientationProposals=OrientationProposalPolicy::OriginalOnly;
    const auto originalOrder=registry.Create("plane.order-ultra")->Run(p,restricted);
    const auto reflectionOrder=registry.Create("plane.order-ultra")->Run(p,Limits());
    Require(Validate(p,originalOrder).Valid&&!Validate(p,originalOrder).Complete&&originalOrder.Layouts.empty()&&
        Validate(p,reflectionOrder).Complete&&reflectionOrder.Layouts.size()==1&&
        reflectionOrder.Layouts[0].Placements[0].Transform.Mirrored&&p.Parts[0].AllowMirror,
        "one order constructor changed hard permissions or ignored its dynamic orientation proposal policy");
    Require(p.Parts.size()==1&&p.Parts[0].ID==part.ID&&p.Parts[0].Quantity==1&&p.Parts[0].Geometry.Outer.size()==3&&
        std::equal(p.Parts[0].Geometry.Outer.begin(),p.Parts[0].Geometry.Outer.end(),part.Geometry.Outer.begin(),
            [](Point a,Point b){return a.X==b.X&&a.Y==b.Y;}),"mirror construction rewrote or duplicated original material");
}
void MidConstructionAndHostHolesUseRealMirroredPoses()
{
    Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.AllowCommonEdges=true;p.Stocks={{"sheet",7,5,1}};
    Part a;a.ID="fixed-original";a.Quantity=1;a.FreeRotation=false;a.AllowedAnglesDegrees={0};
    a.Geometry.Outer={{0,0},{6,0},{0,4}};
    Part b=a;b.ID="moving-original";b.AllowMirror=true;b.Geometry.Outer={{0,0},{-6,0},{0,4}};p.Parts={a,b};
    Result initial;initial.State=Status::Partial;initial.Layouts={{"sheet",0,7,5,{{a.ID,0,{.5,.5,0},0,false}},12}};
    initial.Remaining={{b.ID,1}};auto work=Limits();work.EnableCombinations=false;
    const auto result=MakePlaneNfpFillStage().Factory()->Run(p,initial,work);
    Require(Validate(p,result).Valid&&Validate(p,result).Complete&&result.Layouts[0].Placements.size()==2&&
        result.Layouts[0].Placements[1].Transform.Mirrored,
        "an intermediate single-piece NFP query failed the only legal mirrored common-edge continuation");
    auto forbidden=p;forbidden.Parts[1].AllowMirror=false;
    const auto denied=MakePlaneNfpFillStage().Factory()->Run(forbidden,initial,work);
    Require(Validate(forbidden,denied).Valid&&!Validate(forbidden,denied).Complete,
        "intermediate construction treated a forbidden reflection as an allowed angle");

    Problem hole;hole.Tolerance=1e-7;hole.PartGap=.2;hole.EdgeMargin=.5;hole.AllowCommonEdges=true;
    Part frame;frame.ID="frame";frame.FreeRotation=false;frame.AllowMirror=true;frame.AllowedAnglesDegrees={0};
    frame.Geometry.Outer={{0,0},{12,0},{12,10},{0,10}};
    frame.Geometry.Holes={{{8,6},{2,6},{8,2}}};frame.Geometry.CommonEdgeRings={false,true};
    hole.Parts={frame,b};hole.Stocks={{"host-sheet",13,11,1}};
    for(const bool hostMirror:{false,true})
    {
        Layout board{"host-sheet",0,13,11,{{frame.ID,0,{.5,hostMirror?10.5:.5,0,hostMirror},0,false}},108};
        Detail::NfpPlacementLimits limits;limits.Mirrored=!hostMirror;
        const auto query=Detail::FindPlaneHolePlacements(hole,board,1,0,limits,{});
        Require(query.Supported&&!query.Fits.empty(),"hole domain lost the source host or moving reflection parity");
        for(const auto pose:query.Fits)
            Require(pose.Mirrored==!hostMirror&&AxisSafety::PairExact(AxisSafety::MakeExact(b.Geometry,pose),
                AxisSafety::MakeExact(frame.Geometry,board.Placements[0].Transform),.2,true,hole.Tolerance)==AxisSafety::Verdict::Safe,
                "mirrored hole fit disagreed with complete original material and hole permissions");
        limits.Mirrored=hostMirror;
        Require(Detail::FindPlaneHolePlacements(hole,board,1,0,limits,{}).Fits.empty(),
            "hole domain confused a mirrored triangle with its nonmirrored material");
        auto masked=hole;masked.Parts[0].Geometry.CommonEdgeRings={false,false};limits.Mirrored=!hostMirror;
        Require(Detail::FindPlaneHolePlacements(masked,board,1,0,limits,{}).Fits.empty(),
            "a mirrored exact hole fit waived a forbidden inner-ring seam");
    }
}
void SmallCombinationsCompeteOnAnOccupiedBoard()
{
    for(const auto kind:{PlaneStrategyKind::NfpArray,PlaneStrategyKind::NfpMultiArray})
    for(const double angle:{0.,180.})
    for(const bool allowMirror:{true,false})
    {
        Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.AllowCommonEdges=true;
        auto carrier=Rectangle("carrier",13,7);
        carrier.Geometry.Outer={{0,0},{13,0},{13,7},{0,7}};
        // A T and its actual vertical reflection interlock at (2,2):
        // their union occupies bottom [0,4] and top [1,5], not its 5x2 box.
        // Each true stepped void has exactly the two originals' net area.
        carrier.Geometry.Holes={{{1,1},{5,1},{5,2},{6,2},{6,3},{2,3},{2,2},{1,2}},
            {{7,1},{11,1},{11,2},{12,2},{12,3},{8,3},{8,2},{7,2}}};
        auto moving=Rectangle("moving-T",3,2,6);
        moving.Geometry.Outer={{0,0},{3,0},{3,1},{2,1},{2,2},{1,2},{1,1},{0,1}};
        moving.AllowedAnglesDegrees={angle};moving.AllowMirror=allowMirror;
        // The second variant has identical physical fits but requires the real
        // stored 180-degree turn instead of rewriting the original contour.
        if(angle==180.)for(auto& point:moving.Geometry.Outer)
        {point.X=-point.X;point.Y=-point.Y;}
        p.Parts={carrier,moving};p.Stocks={{"sheet",14,11,1}};
        const auto original=p;
        std::ostringstream diagnostics;diagnostics<<std::setprecision(17);
        std::size_t diagnosticEvents=0;
        const auto require=[&](bool value,const char* why)
        {
            if(!value)throw std::runtime_error(std::string(why)+"; kind="+std::to_string(static_cast<int>(kind))+
                " angle="+std::to_string(angle)+" mirror="+std::to_string(allowMirror)+"\n"+diagnostics.str());
        };
        // Independent complete witnesses establish that neither the useful
        // mirrored pair nor the mirror-forbidden inventory is infeasible.
        Result witness;witness.State=Status::Complete;
        Layout known{"sheet",0,14,11,{{"carrier",0,{.5,.5,0},1,false}},99};
        if(allowMirror)
        {
            for(std::size_t i=0;i<3;++i)
            {
                const double x=i==0?1.5:i==1?7.5:.5,y=i<2?1.5:7.75;
                known.Placements.push_back({moving.ID,2*i,{x,y,angle,false},i+2,false});
                known.Placements.push_back({moving.ID,2*i+1,{x+2,y+2,angle,true},i+2,false});
            }
        }
        else
        {
            known.Placements.push_back({moving.ID,0,{1.5,1.5,angle,false},2,false});
            known.Placements.push_back({moving.ID,1,{7.5,1.5,angle,false},3,false});
            for(std::size_t i=0;i<4;++i)
                known.Placements.push_back({moving.ID,i+2,{.5+i*3.25,7.75,angle,false},i+4,false});
        }
        witness.Layouts={known};
        const auto witnessed=Validate(p,witness);
        require(witnessed.Valid&&witnessed.Complete,"small-combination physical witness is invalid");

        bool carrierCommitted=false,carrierWasFirstOriginal=false,continuationSeen=false,continuationContainsPair=false,continuationInHoles=false;
        std::uint64_t occupiedState=0;
        std::string occupiedRoute,occupiedScope;
        Pose occupiedCarrier;
        std::vector<ConstructionTraceEvent> pairCertificates;
        std::vector<Placement> committedPrefix;
        const auto samePose=[](Pose a,Pose b)
        {return a.X==b.X&&a.Y==b.Y&&a.AngleDegrees==b.AngleDegrees&&a.Mirrored==b.Mirrored;};
        const auto permittedMember=[&](const ConstructionTraceMember& member)
        {return member.PartIndex==1&&member.Transform.AngleDegrees==angle&&
            (!member.Transform.Mirrored||allowMirror);};
        const auto inHole=[&](const ConstructionTraceMember& member,Pose frame,double holeX)
        {
            if(member.PartIndex>=p.Parts.size())return false;
            const auto box=Detail::Bounds(p.Parts[member.PartIndex].Geometry,member.Transform);
            return box.X>=frame.X+holeX-p.Tolerance&&box.X+box.Width<=frame.X+holeX+5+p.Tolerance&&
                box.Y>=frame.Y+1-p.Tolerance&&box.Y+box.Height<=frame.Y+3+p.Tolerance&&
                AxisSafety::Pair(AxisSafety::Make(p.Parts[member.PartIndex].Geometry,member.Transform),
                    AxisSafety::Make(carrier.Geometry,frame),p.PartGap,p.AllowCommonEdges,p.Tolerance)==AxisSafety::Verdict::Safe;
        };
        auto work=Limits();work.TimeLimitSeconds=std::numeric_limits<double>::max();work.MaxPlacementAttempts=500000;
        work.EnableCombinations=true;work.MaxPatternMembers=2;work.EnableGapFill=false;
        work.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        work.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            const bool preparation=event.Source=="preparation.retained-template"||
                event.Source=="preparation.pair-summary"||event.Source=="preparation.combination-summary";
            const bool firstPairDecision=carrierCommitted&&!continuationSeen&&
                event.Source=="placement.local-pair-result";
            const bool firstCommits=event.Stage==ConstructionTraceStage::Commit&&!continuationSeen;
            if(preparation||firstPairDecision||firstCommits||
                (event.Source=="constructor"&&event.Stage==ConstructionTraceStage::Begin))
            {
                if(diagnosticEvents++<256)
                {
                    diagnostics<<"SMALL-GROUP source="<<event.Source<<" reason="<<event.Reason<<" state="<<event.StateID
                        <<" candidate="<<event.CandidateID<<" proposal="<<event.ProposalID
                        <<" accepted="<<event.Accepted<<" width="<<event.Width<<" height="<<event.Height
                        <<" members="<<event.Members.size();
                    for(const auto& [name,value]:event.Measures)diagnostics<<' '<<name<<'='<<value;
                    for(const auto& [name,value]:event.Rank)diagnostics<<" rank."<<name<<'='<<value;
                    for(const auto& member:event.Members)diagnostics<<" member=(part="<<member.PartIndex
                        <<",instance="<<member.Instance<<",x="<<member.Transform.X<<",y="<<member.Transform.Y
                        <<",angle="<<member.Transform.AngleDegrees<<",mirror="<<member.Transform.Mirrored<<')';
                    diagnostics<<'\n';
                }
                else if(diagnosticEvents==257)diagnostics<<"SMALL-GROUP diagnostic event cap reached\n";
            }
            if(firstPairDecision&&event.Accepted&&event.StateID==occupiedState&&
                event.Route==occupiedRoute&&event.Scope==occupiedScope&&event.Members.size()==2)
            {
                const auto& a=event.Members[0];const auto& b=event.Members[1];
                const bool sameOriginal=a.PartIndex==1&&b.PartIndex==1;
                const bool complementaryParity=a.Transform.Mirrored!=b.Transform.Mirrored;
                const bool sameHole=(inHole(a,occupiedCarrier,1.)&&inHole(b,occupiedCarrier,1.))||
                    (inHole(a,occupiedCarrier,7.)&&inHole(b,occupiedCarrier,7.));
                if(sameOriginal&&complementaryParity&&permittedMember(a)&&permittedMember(b)&&sameHole&&
                    !Detail::ShapesConflict(p.Parts[a.PartIndex].Geometry,a.Transform,p.Parts[b.PartIndex].Geometry,
                        b.Transform,p.PartGap,p.Tolerance,p.AllowCommonEdges)&&
                    AxisSafety::Pair(AxisSafety::Make(moving.Geometry,a.Transform),AxisSafety::Make(moving.Geometry,b.Transform),
                        p.PartGap,p.AllowCommonEdges,p.Tolerance)==AxisSafety::Verdict::Safe)
                    pairCertificates.push_back(event);
            }
            if(event.Stage!=ConstructionTraceStage::Commit||event.Layouts.empty())return;
            const auto& board=event.Layouts.front();
            const auto frame=std::find_if(board.Placements.begin(),board.Placements.end(),
                [](const Placement& item){return item.PartID=="carrier";});
            if(carrierCommitted&&!continuationSeen)
            {
                continuationSeen=true;
                continuationContainsPair=std::any_of(pairCertificates.begin(),pairCertificates.end(),[&](const auto& certificate)
                {
                    return certificate.StateID==event.ParentStateID&&certificate.Route==event.Route&&
                        certificate.Scope==event.Scope&&std::all_of(certificate.Members.begin(),certificate.Members.end(),[&](const auto& certified)
                        {
                            return std::any_of(event.Members.begin(),event.Members.end(),[&](const auto& member)
                            {return member.PartIndex==certified.PartIndex&&samePose(member.Transform,certified.Transform);});
                        });
                });
                continuationInHoles=frame!=board.Placements.end()&&!event.Members.empty()&&
                    samePose(frame->Transform,occupiedCarrier)&&
                    std::all_of(event.Members.begin(),event.Members.end(),[&](const auto& member)
                    {return permittedMember(member)&&(inHole(member,occupiedCarrier,1.)||inHole(member,occupiedCarrier,7.));});
                committedPrefix=board.Placements;
            }
            if(!carrierCommitted&&frame!=board.Placements.end())
            {
                occupiedState=event.StateID;occupiedRoute=event.Route;occupiedScope=event.Scope;
                occupiedCarrier=frame->Transform;committedPrefix=board.Placements;
                carrierWasFirstOriginal=board.Placements.size()==1&&event.Members.size()==1&&event.Members[0].PartIndex==0;
            }
            carrierCommitted|=frame!=board.Placements.end();
        };
        PlaneStrategySettings settings;settings.Kind=kind;settings.EnableLayoutRefinement=false;
        settings.EnableContourSettling=false;settings.EnableAllowedAngleSettling=false;
        settings.EnableSheetTemplateReuse=false;settings.EnableHoleNesting=false;
        const auto result=MakePlaneStrategyFactory(settings)()->Run(p,work);const auto checked=Validate(p,result);
        require(checked.Valid&&checked.Complete&&checked.PlacedCount==7&&result.Layouts.size()==1,
            "occupied-board small combinations lost legal complete original demand");
        require(!work.ConstructionObservation->Failed.load(),"small-combination observer failed");
        require(carrierCommitted&&carrierWasFirstOriginal&&continuationSeen,
            "small-combination fixture did not first construct the actual carrier before continuing from its occupied state");
        if(allowMirror)require(carrierCommitted&&continuationSeen&&continuationContainsPair&&continuationInHoles,
            "the first occupied-board continuation lost its same-state certified mirrored pair or left the true holes");
        else require(pairCertificates.empty(),"mirror-forbidden self demand acquired a reflected pair certificate");
        for(const auto& committed:committedPrefix)
        {
            const auto& placements=result.Layouts.front().Placements;
            const auto final=std::find_if(placements.begin(),placements.end(),[&](const auto& placed)
                {return placed.PartID==committed.PartID&&placed.Instance==committed.Instance;});
            require(final!=placements.end()&&samePose(final->Transform,committed.Transform),
                "small combination changed an already committed original pose");
        }
        std::map<std::string,std::set<std::size_t>> instances;
        for(const auto& board:result.Layouts)for(const auto& placed:board.Placements)
        {
            require(instances[placed.PartID].insert(placed.Instance).second,"small combination duplicated an original instance");
            if(placed.PartID=="carrier")require(placed.Transform.AngleDegrees==0&&!placed.Transform.Mirrored,
                "small combination changed the fixed carrier orientation");
            else require(placed.PartID==moving.ID&&placed.Transform.AngleDegrees==angle&&
                (!placed.Transform.Mirrored||allowMirror),
                "small combination bypassed original turn or mirror permissions");
        }
        require(instances.size()==2&&instances["carrier"]==std::set<std::size_t>{0}&&
            instances[moving.ID]==std::set<std::size_t>{0,1,2,3,4,5},"small combination consumed the wrong original inventory");
        for(std::size_t i=0;i<p.Parts.size();++i)
            require(p.Parts[i].ID==original.Parts[i].ID&&p.Parts[i].Quantity==original.Parts[i].Quantity&&
                p.Parts[i].AllowMirror==original.Parts[i].AllowMirror&&
                p.Parts[i].FreeRotation==original.Parts[i].FreeRotation&&
                p.Parts[i].AllowedAnglesDegrees==original.Parts[i].AllowedAnglesDegrees&&
                p.Parts[i].Geometry.Outer.size()==original.Parts[i].Geometry.Outer.size()&&
                std::equal(p.Parts[i].Geometry.Outer.begin(),p.Parts[i].Geometry.Outer.end(),original.Parts[i].Geometry.Outer.begin(),
                    [](Point a,Point b){return a.X==b.X&&a.Y==b.Y;}),"small combination rewrote the original inputs");
        require(p.Parts[0].Geometry.Holes.size()==original.Parts[0].Geometry.Holes.size(),
            "small combination changed the original carrier holes");
        for(std::size_t i=0;i<carrier.Geometry.Holes.size();++i)
            require(p.Parts[0].Geometry.Holes[i].size()==original.Parts[0].Geometry.Holes[i].size()&&
                std::equal(p.Parts[0].Geometry.Holes[i].begin(),p.Parts[0].Geometry.Holes[i].end(),original.Parts[0].Geometry.Holes[i].begin(),
                    [](Point a,Point b){return a.X==b.X&&a.Y==b.Y;}),"small combination rewrote an original inner ring");
    }
}
int main(int argc,char** argv)
{
try
{
    if(argc==3&&std::string(argv[1])=="--nfp-real")return ProfileOriginalNfp(argv[2]);
    AuthorizedCommonEdgeProposals();
    OrderedClearanceKeepsTheWholeOriginalGate();
    BoundaryViewsRetainOriginalWorldSemantics();
    CommonGapScreenRejectsOnlyImpossibleStoredPoses();
    NativeCommonVerificationRetainsRepairableProposals();
    IdenticalCommonNativeCertificateReplacesOnlyTheDuplicateGate();
    NativeCommonFullBudgetRetainsLegacySafeFits();
    ArbitraryAnglePairUsesASafePlanningCarrier();
    PlacementsMeasureTheWholeStockWidthUsedSection();
    CompleteChannelsCompeteWithAlreadyLegalVertices();
    LowBaselineCannotBeAttractedToTheFarStockEdge();
    DenseSmallSeedBeatsMoreSparseMaterial();
    EmptyBoardIntrinsicTiesIgnoreWorldTranslation();
    HigherRealUtilizationPrecedesIntrinsicTies();
    NonOrthogonalPhaseCompetesWithEveryRetainedOriginalSeed();
    OrderPublishesCheckedImprovementsBeforeLaterOrders();
    MirroredOriginalsReachIndependentInitialConstructors();
    MidConstructionAndHostHolesUseRealMirroredPoses();
    SmallCombinationsCompeteOnAnOccupiedBoard();
    FirstGreedyTrialKeepsItsConstructionQuota();
    // Equal-area originals retain the complete low/left, material-centroid and
    // original-index tie-break, even when a smaller original appears first.
    Problem tie;tie.Parts={Rectangle("small",2,2),Rectangle("first-large",4,3),Rectangle("second-large",3,4)};
    tie.Stocks={{"sheet",12,10,1}};tie.PartGap=.2;tie.EdgeMargin=.5;
    auto tied=Construct(tie);
    Require(Validate(tie,tied).Complete,"equal-area mixed originals should all fit");
    Require(tied.Layouts.front().Placements.front().PartID=="first-large","area pruning must retain equal-area original-index ties");

    // A larger unavailable candidate cannot prevent smaller legal originals
    // from supplying an initial solution on the actual stock.
    Problem tooLarge;tooLarge.Parts={Rectangle("oversize",20,20),Rectangle("usable",3,2)};
    tooLarge.Stocks={{"sheet",8,6,1}};tooLarge.PartGap=.2;tooLarge.EdgeMargin=.5;
    auto partial=Construct(tooLarge);
    Require(partial.State==Status::Partial&&partial.Layouts.size()==1&&partial.Layouts[0].Placements.size()==1&&
        partial.Layouts[0].Placements[0].PartID=="usable","largest-area failure must continue to a smaller feasible original");
    Require(partial.Remaining.size()==1&&partial.Remaining[0].PartID=="oversize","unplaceable original demand must remain explicit");

    // Successive boards use equal-sized fixed lists at different world poses.
    // Reused scoring state must therefore key by original and full pose rather
    // than a State address, placement count, group tag, or stock dimensions.
    Problem fill;fill.Parts={Rectangle("fixed",3,4,2),Rectangle("filler",3,4,4)};
    fill.Stocks={{"sheet",10,4,2}};fill.PartGap=.2;
    Result fixed;fixed.State=Status::Partial;
    fixed.Layouts={{"sheet",0,10,4,{{"fixed",0,{7,-11,0},41,false}},12},
        {"sheet",1,10,4,{{"fixed",1,{14,-11,0},72,false}},12}};
    fixed.Remaining={{"filler",4}};
    Require(Validate(fill,fixed).Valid,"fixed-board fixture must use valid original contours");
    auto filled=MakePlaneNfpFillStage().Factory()->Run(fill,fixed,Limits());
    Require(Validate(fill,filled).Complete,"contact geometry must refresh across changed fixed poses");
    Require(filled.Layouts[0].Placements[0].Transform.X==7&&filled.Layouts[1].Placements[0].Transform.X==14&&
        filled.Layouts[0].Placements[0].Group==41&&filled.Layouts[1].Placements[0].Group==72,"scoring caches must preserve fixed originals");
    for(const auto& board:filled.Layouts)
    {
        std::set<std::size_t> instances;
        for(const auto& member:board.Placements)if(member.PartID=="filler")instances.insert(member.Instance);
        Require(instances.size()==2,"each physical board should receive exactly its two available fillers");
    }
    Problem channel;channel.Tolerance=1e-7;channel.Stocks={{"sheet",14,5,1}};
    auto u=Rectangle("U",14,5);u.Geometry.Outer={{0,0},{2,0},{2,3},{12,3},{12,0},{14,0},{14,5},{0,5}};
    auto plug=Rectangle("plug",10,3);plug.Geometry.Outer={{0,0},{10,0},{10,3},{0,3}};channel.Parts={u,plug};
    std::size_t fullQueryEvents=0,localScoreEvents=0;
    auto work=Limits();work.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Begin)
            for(const auto& [name,value]:event.Measures)if(name=="boundary-vertices-only"&&value==0)++fullQueryEvents;
        if(event.Stage==ConstructionTraceStage::Score)
            for(const auto& [name,value]:event.Measures)if(name=="localUtilization"&&value>0)++localScoreEvents;
    };
    PlaneStrategySettings greedy;greedy.Kind=PlaneStrategyKind::NfpClosest;greedy.EnableLayoutRefinement=false;
    const auto packed=MakePlaneStrategyFactory(greedy)()->Run(channel,work);
    Require(Validate(channel,packed).Complete&&Validate(channel,packed).Valid,
        "full-contact greedy lost the original touch-only cavity");
    Require(fullQueryEvents>0&&localScoreEvents>0,
        "greedy did not record the complete contact stream and original-material utilization");
    std::cout<<"PlaneNfpScoringTests passed: area ties, rejected large candidates and changed fixed scoring geometry\n";return 0;
}
catch(const std::exception& error){std::cerr<<"PlaneNfpScoringTests failed: "<<error.what()<<'\n';return 1;}
}
