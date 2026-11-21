#include "NestingNfpPlacement.h"
#include "NestingClearancePlacement.h"
#include "NestingPlaneHoles.h"
#include "SheetNestingConstructionTrace.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

using namespace iCAX::SheetNesting;
using namespace iCAX::SheetNesting::Detail;

namespace
{
void Require(bool value,const char* message)
{if(!value)throw std::runtime_error(message);}

Part Rectangle(const char* id,double width,double height)
{
    Part part;part.ID=id;part.Material="m";part.Quantity=1;
    part.FreeRotation=false;part.AllowedAnglesDegrees={0,90};
    part.Geometry.Outer={{0,0},{width,0},{width,height},{0,height}};
    return part;
}

Problem Input(double gap=0)
{
    Problem problem;problem.PartGap=gap;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    problem.Parts={Rectangle("fixed",6,4),Rectangle("moving",3,2)};
    problem.Stocks={{"board",30,20,1,1,"m"}};
    return problem;
}

Layout Board()
{return {"board",0,30,20,{{"fixed",0,{8,6,0},0,false}},24};}

bool SamePose(Pose a,Pose b)
{return a.X==b.X&&a.Y==b.Y&&a.AngleDegrees==b.AngleDegrees;}

void SameQuery(const NfpPlacementQuery& a,const NfpPlacementQuery& b)
{
    Require(a.Supported==b.Supported&&a.Truncated==b.Truncated&&a.Attempts==b.Attempts&&
        a.Generated==b.Generated&&a.DomainMeasured==b.DomainMeasured&&a.FreeTranslationArea==b.FreeTranslationArea,
        "observation changed query status, geometry measure or charged attempts");
    Require(a.Fits.size()==b.Fits.size()&&a.Blocked.size()==b.Blocked.size(),"observation changed candidate counts");
    for(std::size_t i=0;i<a.Fits.size();++i)Require(SamePose(a.Fits[i],b.Fits[i]),"observation changed fit order or coordinates");
    for(std::size_t i=0;i<a.Blocked.size();++i)
        Require(SamePose(a.Blocked[i].Transform,b.Blocked[i].Transform)&&a.Blocked[i].Blockers==b.Blocked[i].Blockers&&
            a.Blocked[i].OverlapArea==b.Blocked[i].OverlapArea,"observation changed blocked proposal evidence");
}

double Measure(const ConstructionTraceEvent& event,const char* name)
{
    const auto found=std::find_if(event.Measures.begin(),event.Measures.end(),[&](const auto& item){return item.first==name;});
    Require(found!=event.Measures.end(),"required native trace measure missing");return found->second;
}

void QueryObservationAndCapIdentity()
{
    const auto problem=Input();const auto board=Board();NfpPlacementGenerator placement(problem);
    Require(placement.Ready(),"trace fixture was not admitted");
    NfpPlacementLimits limits;limits.MaxCandidates=3;
    const auto plain=placement.Find(board,1,0,limits);
    Require(plain.FitsIDs.empty(),"disabled observation built candidate IDs");
    std::vector<ConstructionTraceEvent> events;
    limits.TraceHealth=std::make_shared<ConstructionTraceHealth>();
    limits.Trace=[&](const ConstructionTraceEvent& event){events.push_back(event);};
    const auto traced=placement.Find(board,1,0,limits);
    SameQuery(plain,traced);
    Require(!limits.TraceHealth->Failed.load()&&!events.empty(),"healthy observation failed");
    Require(traced.Fits.size()==3&&traced.FitsIDs.size()==traced.Fits.size(),"fit cap and returned IDs are not aligned");

    std::map<std::uint64_t,Pose> beforeCap;
    std::set<std::uint64_t> generated,dropped;
    std::vector<std::uint64_t> retained;
    bool queryLimits=false,searchTruncated=false;
    for(const auto& event:events)
    {
        Require(!event.Truncated,"search bounds were mislabeled as omitted observation events");
        if(event.Reason=="raw-candidate")
        {Require(event.CandidateID!=0&&!event.Members.empty(),"raw candidate has no stable identity/pose");generated.insert(event.CandidateID);}
        if(event.Reason=="original-material-fit")
        {Require(event.Members.size()==1,"verified fit pose missing");beforeCap[event.CandidateID]=event.Members[0].Transform;}
        if(event.Reason=="fit-retained-after-cap")retained.push_back(event.CandidateID);
        if(event.Reason=="legal-fit-candidate-limit")dropped.insert(event.CandidateID);
        if(event.Reason=="query-begin")
        {
            Require(Measure(event,"max-candidates")==3&&Measure(event,"max-raw-candidates")==4096&&
                Measure(event,"max-contact-segments")==256&&Measure(event,"stop-after-first-fit")==0&&
                Measure(event,"fast-adjacency")==0,"query limits were not recorded faithfully");
            queryLimits=true;
        }
        if(event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Complete)
        {
            Require(Measure(event,"supported")==1&&Measure(event,"fits-returned")==3,"query completion facts missing");
            searchTruncated=event.SearchTruncated&&Measure(event,"truncated")==1;
        }
    }
    Require(queryLimits&&searchTruncated&&beforeCap.size()>traced.Fits.size()&&!dropped.empty(),
        "fixture did not exercise native legal-fit cropping");
    Require(retained==traced.FitsIDs,"post-cap ID order differs from returned fit order");
    for(std::size_t i=0;i<traced.Fits.size();++i)
    {
        const auto id=traced.FitsIDs[i];
        Require(generated.contains(id)&&beforeCap.contains(id)&&!dropped.contains(id)&&SamePose(beforeCap.at(id),traced.Fits[i]),
            "returned fit lost its pre-cap source identity");
    }
    events.clear();const auto repeated=placement.Find(board,1,0,limits);
    SameQuery(traced,repeated);Require(traced.FitsIDs==repeated.FitsIDs,"identical query changed candidate identities");

    limits.MaxAttempts=1;events.clear();const auto bounded=placement.Find(board,1,0,limits);
    Require(bounded.Attempts==1&&bounded.Truncated,"native attempt fixture was not bounded");
    const auto budget=std::find_if(events.begin(),events.end(),[](const auto& event)
        {return event.Stage==ConstructionTraceStage::Budget&&event.Source=="nfp.query";});
    Require(budget!=events.end()&&Measure(*budget,"raw-unvisited")>0&&
        Measure(*budget,"raw-unvisited")==Measure(*budget,"raw-kept")-Measure(*budget,"raw-visited"),
        "unvisited native candidate count was not exposed");
    std::cout<<"PASS native Find observation preserves geometry/attempts and tracks fit cap identity\n";
}

void ClearanceIdentityAndCancellation()
{
    const auto problem=Input(.2);const auto board=Board();
    ClearanceNfpPlacement placement(problem,problem.Stocks[0],{},{});
    Require(placement.Ready(),"positive-gap trace fixture was not admitted");
    NfpPlacementLimits limits;limits.MaxCandidates=0;
    // Input geometry, gap and all angle permissions remain unchanged.
    placement.Find(board,1,0,limits,{});
    const auto plain=placement.Find(board,1,0,limits,{});
    Require(plain.Fits.size()>2,"clearance fixture needs several actual safe fits");
    std::vector<ConstructionTraceEvent> events;
    limits.TraceHealth=std::make_shared<ConstructionTraceHealth>();
    limits.Trace=[&](const ConstructionTraceEvent& event){events.push_back(event);};
    const auto traced=placement.Find(board,1,0,limits,{});
    SameQuery(plain,traced);
    Require(traced.FitsIDs.size()==traced.Fits.size(),"clearance result has a mismatched ID vector");
    std::map<std::uint64_t,Pose> accepted;
    for(const auto& event:events)if(event.Source=="nfp.original-clearance"&&event.Reason=="original-clearance-fit")
    {
        Require(event.PartIndex==1&&event.Members.size()==1&&Measure(event,"nominal-gap")==.2,
            "clearance observation did not retain the original identity/gap");
        accepted[event.CandidateID]=event.Members[0].Transform;
    }
    for(std::size_t i=0;i<traced.Fits.size();++i)
        Require(accepted.contains(traced.FitsIDs[i])&&SamePose(accepted.at(traced.FitsIDs[i]),traced.Fits[i]),
            "clearance fit is not aligned with its exact original-material observation");
    bool cancel=false;std::size_t acceptedBeforeCancel=0;events.clear();
    limits.Trace=[&](const ConstructionTraceEvent& event)
    {
        events.push_back(event);
        if(event.Source=="nfp.original-clearance"&&event.Reason=="original-clearance-fit"&&++acceptedBeforeCancel==2)
            cancel=true;
    };
    // Request cancellation after observing two real original-material fits.
    // No implementation poll counts or mutable-input injection are needed.
    const auto cancelled=placement.Find(board,1,0,limits,[&]{return cancel;});
    Require(cancelled.Attempts==traced.Attempts&&cancelled.Generated==traced.Generated,
        "cancellation fixture stopped inside core Find instead of the original gate");
    Require(cancel&&cancelled.Fits.size()==2&&cancelled.FitsIDs.size()==2,
        "clearance cancellation did not retain its two observed fits");
    for(std::size_t i=0;i<cancelled.Fits.size();++i)
        Require(SamePose(cancelled.Fits[i],traced.Fits[i])&&cancelled.FitsIDs[i]==traced.FitsIDs[i],
            "clearance filtering did not keep fit IDs synchronized");
    std::size_t rejected=0;
    for(const auto& event:events)if(event.Source=="nfp.original-clearance"&&event.Reason=="stopped-before-original-clearance")
    {++rejected;Require(event.CandidateID!=0&&std::find(cancelled.FitsIDs.begin(),cancelled.FitsIDs.end(),event.CandidateID)==cancelled.FitsIDs.end(),
        "cancelled gate lost candidate identity");}
    Require(rejected+cancelled.Fits.size()==traced.Fits.size(),"clearance filtering did not account for every removed candidate");
    Require(!limits.TraceHealth->Failed.load(),"ordinary cancellation was mislabeled as observer failure");
    std::cout<<"PASS positive-gap original gate keeps candidate IDs aligned through cancellation filtering\n";
}

void ThrowingObserversAndHealth()
{
    const auto problem=Input();const auto board=Board();NfpPlacementGenerator placement(problem);
    NfpPlacementLimits limits;limits.MaxCandidates=3;const auto plain=placement.Find(board,1,0,limits);
    limits.TraceHealth=std::make_shared<ConstructionTraceHealth>();std::size_t calls=0;
    limits.Trace=[&](const ConstructionTraceEvent&){++calls;throw std::runtime_error("observer failure");};
    const auto failed=placement.Find(board,1,0,limits);SameQuery(plain,failed);
    Require(calls==1&&limits.TraceHealth->Failed.load()&&limits.TraceHealth->Omitted.load()>1,
        "Find did not publish observer failure/omissions or continued invoking the failed observer");
    Require(failed.FitsIDs.size()==failed.Fits.size(),"observer exception corrupted returned fit identity alignment");

    const auto spaced=Input(.2);ClearanceNfpPlacement clearance(spaced,spaced.Stocks[0],{},{});
    NfpPlacementLimits original;original.MaxCandidates=0;const auto base=clearance.Find(board,1,0,original,{});
    original.TraceHealth=std::make_shared<ConstructionTraceHealth>();std::size_t gateCalls=0;
    original.Trace=[&](const ConstructionTraceEvent& event)
    {if(event.Source=="nfp.original-clearance"){++gateCalls;throw std::runtime_error("original-gate observer failure");}};
    const auto gateFailed=clearance.Find(board,1,0,original,{});SameQuery(base,gateFailed);
    Require(gateCalls==1&&original.TraceHealth->Failed.load()&&original.TraceHealth->Omitted.load()>1,
        "original gate swallowed observer failure without reporting shared health");
    Require(gateFailed.FitsIDs.size()==gateFailed.Fits.size(),"original gate observer failure corrupted fit IDs");
    std::cout<<"PASS Find and clearance observer exceptions preserve solver results and report failed/omitted health\n";
}

void HoleObservationAndIdentity()
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    auto host=Rectangle("two-hole-host",26,20);host.Quantity=2;
    host.Geometry.Holes={{{2,2},{11,2},{2,10}},{{14,2},{24,2},{24,16},{14,16}}};
    problem.Parts={host,Rectangle("hole-child",3,2)};
    problem.Stocks={{"board",64,24,1,1,"m"}};
    const Layout board{"board",0,64,24,{{host.ID,0,{1,1,0},7,false},{host.ID,1,{32,1,0},8,false}},0};
    NfpPlacementLimits limits;limits.MaxCandidates=0;limits.MaxAttempts=100000;limits.MaxRawCandidates=8192;
    const auto plain=FindPlaneHolePlacements(problem,board,1,0,limits);
    Require(plain.Supported&&!plain.Fits.empty()&&plain.FitsIDs.empty(),"multi-hole fixture did not produce ordinary native fits");
    std::vector<ConstructionTraceEvent> events;
    limits.TraceHealth=std::make_shared<ConstructionTraceHealth>();
    limits.Trace=[&](const ConstructionTraceEvent& event){events.push_back(event);};
    const auto traced=FindPlaneHolePlacements(problem,board,1,0,limits);SameQuery(plain,traced);
    Require(traced.FitsIDs.size()==traced.Fits.size()&&!limits.TraceHealth->Failed.load(),"hole observation lost fit identity or failed");

    using Path=std::pair<std::size_t,std::size_t>;
    std::map<std::uint64_t,Path> owners;
    std::map<std::uint64_t,Pose> verified,final;
    std::map<Path,std::size_t> seeds;
    std::set<std::uint64_t> innerSources;
    std::size_t duplicates=0;
    bool inner=false,decomposed=false;
    for(const auto& event:events)
    {
        Require(!event.Truncated,"hole search bounds were mislabeled as observation loss");
        if(event.Source=="plane.hole.decomposition")decomposed=true;
        if(event.Source.starts_with("plane.hole.inner-nfp/"))inner=true;
        if(!event.CandidateID)continue;
        const Path path{static_cast<std::size_t>(Measure(event,"host-index")),static_cast<std::size_t>(Measure(event,"hole-index"))};
        const auto found=owners.find(event.CandidateID);
        if(found==owners.end())owners.emplace(event.CandidateID,path);
        else Require(found->second==path,"a native hole candidate ID changed host/hole ownership");
        Require(event.PartIndex==1,"hole event leaked an internal/private part index");
        for(const auto& member:event.Members)Require(member.PartIndex==1,"hole member leaked an internal/private part index");
        if(event.Source=="plane.hole.seed"&&event.Reason=="actual-hole-seed")++seeds[path];
        if(event.Source.starts_with("plane.hole.inner-nfp/"))
        {
            innerSources.insert(event.CandidateID);
            if(event.Reason=="raw-duplicate")
            {
                const auto target=static_cast<std::uint64_t>(Measure(event,"duplicate-of"));
                Require(target!=0&&owners.contains(target)&&owners.at(target)==path,
                    "inner NFP duplicate reference was not remapped into the outer hole query");
                ++duplicates;
            }
        }
        if(event.Reason=="actual-original-hole-fit")
        {Require(event.Members.size()==1,"accepted hole event has no actual original pose");verified[event.CandidateID]=event.Members[0].Transform;}
        if(event.Reason=="hole-fit-final-order")
        {Require(event.Members.size()==1,"final sorted hole event has no actual pose");final[event.CandidateID]=event.Members[0].Transform;}
    }
    Require(seeds.size()==4&&std::all_of(seeds.begin(),seeds.end(),[](const auto& item){return item.second==9;}),
        "the actual nine seeds were not observed independently for all host/hole paths");
    Require(inner&&decomposed&&!innerSources.empty()&&duplicates>0,"hole fixture did not exercise domain decomposition and nested NFP identity");
    Require(final.size()==traced.Fits.size(),"final sorted hole output did not account for each fit");
    for(std::size_t i=0;i<traced.Fits.size();++i)
    {
        const auto id=traced.FitsIDs[i];
        Require(id!=0&&verified.contains(id)&&final.contains(id)&&SamePose(verified.at(id),traced.Fits[i])&&SamePose(final.at(id),traced.Fits[i]),
            "hole final sorting broke the verified native candidate identity/pose mapping");
    }
    events.clear();const auto repeated=FindPlaneHolePlacements(problem,board,1,0,limits);
    SameQuery(traced,repeated);Require(repeated.FitsIDs==traced.FitsIDs,"repeated hole query changed stable candidate IDs");

    limits.TraceHealth=std::make_shared<ConstructionTraceHealth>();std::size_t failures=0;
    limits.Trace=[&](const ConstructionTraceEvent&){++failures;throw std::runtime_error("hole observation failure");};
    const auto failed=FindPlaneHolePlacements(problem,board,1,0,limits);SameQuery(plain,failed);
    Require(failures==1&&limits.TraceHealth->Failed.load()&&limits.TraceHealth->Omitted.load()>1&&failed.FitsIDs.size()==failed.Fits.size(),
        "hole observer failure changed geometry/attempts, corrupted IDs or failed to publish observation health");
    std::cout<<"PASS actual nine hole seeds, nested NFP paths, final sorted IDs and observer health preserve native construction\n";
}
}

int main()try
{
    QueryObservationAndCapIdentity();ClearanceIdentityAndCancellation();ThrowingObserversAndHealth();HoleObservationAndIdentity();
    return 0;
}
catch(const std::exception& error)
{std::cerr<<"NfpPlacementTraceTests: "<<error.what()<<'\n';return 1;}
