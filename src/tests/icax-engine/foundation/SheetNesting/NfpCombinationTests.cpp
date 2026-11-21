#include "NestingNfpCombinations.h"
#include "NestingNfpContactSamples.h"
#include "NestingAxisSafety.h"
#include "SheetNestingConstructionTrace.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* message)
{if(!value)throw std::runtime_error(message);}

Problem ComplementaryTriangles(double gap,std::size_t quantity=1)
{
    Problem p;p.PartGap=gap;p.EdgeMargin=.5;p.Tolerance=1e-7;
    Part a;a.ID="left-original";a.Quantity=quantity;a.FreeRotation=false;
    a.AllowedAnglesDegrees={0};a.Geometry.Outer={{0,0},{6,0},{0,6}};
    auto b=a;b.ID="right-original";b.AllowedAnglesDegrees={180};
    p.Parts={a,b};p.Stocks={{"stock",30,20,1}};return p;
}

std::vector<Detail::Pattern> Build(const Problem& p,Detail::NfpCombinationOptions options,
    std::size_t& attempts)
{
    NfpCache cache;std::vector<NfpPreparedShape> prepared;
    for(const auto& part:p.Parts)prepared.push_back(cache.Prepare(part.Geometry));
    return Detail::BuildNfpCombinationPatterns(p,cache,prepared,options,attempts);
}

void Verify(const Problem& p,const std::vector<Detail::Pattern>& patterns,std::size_t maxMembers)
{
    for(const auto& pattern:patterns)
    {
        Require(pattern.Members.size()>=2&&pattern.Members.size()<=maxMembers,
            "combination exceeded original-member bounds");
        std::map<std::size_t,std::size_t> demand;double area=0;
        for(const auto& member:pattern.Members)
        {
            Require(member.PartIndex<p.Parts.size(),"private clearance contour escaped as an original");
            const auto& part=p.Parts[member.PartIndex];
            Require(++demand[member.PartIndex]<=part.Quantity,"combination exceeded original demand");
            Require(Detail::PoseAllowed(part,member.Transform,p.Tolerance),
                "combination widened finite rotation or mirror constraints");
            auto enclosed=member.Transform;enclosed.X+=1e-6;enclosed.Y+=1e-6;
            const auto geometry=AxisSafety::Make(part.Geometry,enclosed);
            Require(AxisSafety::Containment(geometry,pattern.Width+2e-6,pattern.Height+2e-6,0)==AxisSafety::Verdict::Safe,
                "combination envelope omitted original material");
            area+=Detail::Area(part.Geometry);
        }
        Require(std::abs(pattern.MaterialArea-area)<1e-8,"combination counted expanded contours as material");
        for(std::size_t i=0;i<pattern.Members.size();++i)
        for(std::size_t j=i+1;j<pattern.Members.size();++j)
        {
            const auto& a=pattern.Members[i];const auto& b=pattern.Members[j];
            const auto& ga=p.Parts[a.PartIndex].Geometry;const auto& gb=p.Parts[b.PartIndex].Geometry;
            Require(!Detail::ShapesConflict(ga,a.Transform,gb,b.Transform,p.PartGap,p.Tolerance,p.AllowCommonEdges),
                "combination conflicts in original-material validation");
            Require(AxisSafety::Pair(AxisSafety::Make(ga,a.Transform),AxisSafety::Make(gb,b.Transform),
                p.PartGap,p.AllowCommonEdges,p.Tolerance)==AxisSafety::Verdict::Safe,
                "combination violated exact nominal clearance, including cross-copy neighbors");
        }
        if(p.PartGap>0)
        {
            Require(pattern.Kind!=ShapeKind::Rectangle&&pattern.Kind!=ShapeKind::Parallelogram,
                "positive-gap voids became a filled basic carrier");
            Require(!Detail::AssessNfpCombinationCarrier(p,pattern).Filled,
                "expanded clearance contours certified filled original material");
        }
    }
}

bool HasMixed(const std::vector<Detail::Pattern>& patterns,std::size_t count)
{
    return std::any_of(patterns.begin(),patterns.end(),[&](const auto& pattern)
    {
        if(pattern.Members.size()!=count)return false;
        const auto first=pattern.Members.front().PartIndex;
        return std::any_of(pattern.Members.begin(),pattern.Members.end(),
            [&](const auto& member){return member.PartIndex!=first;});
    });
}
double ActualUtilization(const Problem& p,const Detail::Pattern& pattern)
{
    double material=0;for(const auto& member:pattern.Members)material+=Detail::Area(p.Parts[member.PartIndex].Geometry);
    return material/(pattern.Width*pattern.Height);
}
void UtilizationWinsOverTheFormerSavedEnvelopeScore()
{
    auto p=ComplementaryTriangles(.2);p.Stocks[0].Width=250;p.Stocks[0].Height=100;
    Part rectangle;rectangle.ID="dense-original";rectangle.Quantity=2;rectangle.FreeRotation=false;rectangle.AllowedAnglesDegrees={0};
    rectangle.Geometry.Outer={{0,0},{100,0},{100,10},{0,10}};p.Parts.push_back(rectangle);
    Detail::NfpCombinationOptions options;options.MaxMembers=2;options.MaxResults=1;
    options.SamePartOnly=false; // Explicit low-level comparison with unlike originals.
    options.MaxStates=24;options.MaxAttempts=6000;options.PrepareSelfPairs=false;
    double densestCertified=0,bestFormerScore=-1e100,formerWinnerUtilization=0;
    options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source!="preparation.certified-pattern")return;
        double material=0,separate=0;
        for(const auto& member:event.Members)
        {
            const auto& shape=p.Parts[member.PartIndex].Geometry;
            material+=Detail::Area(shape);const auto box=Detail::Bounds(shape,member.Transform);
            separate+=box.Width*box.Height;
        }
        const double envelope=event.Width*event.Height,fill=material/envelope;
        densestCertified=std::max(densestCertified,fill);
        const double former=1-envelope/separate+.1*fill;
        if(former>bestFormerScore){bestFormerScore=former;formerWinnerUtilization=fill;}
    };
    std::size_t attempts=0;const auto patterns=Build(p,options,attempts);Verify(p,patterns,2);
    Require(patterns.size()==1&&patterns.front().Members[0].PartIndex==2&&patterns.front().Members[1].PartIndex==2,
        "a larger saved-envelope fraction displaced the tighter certified original-material pair");
    const double actual=ActualUtilization(p,patterns.front());
    Require(actual>.98&&actual==densestCertified&&formerWinnerUtilization<actual-.01,
        "utilization fixture did not distinguish tightness from the former weighted saving score");
    Require(attempts<=options.MaxAttempts,"density retention increased the shared search budget");
}
void DenserLaterDepthReplacesAnEarlierFullReservoir()
{
    Problem p;p.Tolerance=1e-7;p.EdgeMargin=.5;p.Stocks={{"stock",30,20,1}};
    Part body;body.ID="notched-body";body.FreeRotation=false;body.AllowedAnglesDegrees={0};
    body.Geometry.Outer={{0,0},{10,0},{10,4},{8,4},{8,2},{6,2},{6,4},{4,4},{4,3},{0,3}};
    // Positive dyadic slack keeps this a retention test rather than requiring
    // isolated zero-dimensional placements swallowed by NFP regularization.
    Part wide;wide.ID="wide-fill";wide.FreeRotation=false;wide.AllowedAnglesDegrees={0};wide.Geometry.Outer={{0,0},{3.5,0},{3.5,.5},{0,.5}};
    Part square;square.ID="square-fill";square.FreeRotation=false;square.AllowedAnglesDegrees={0};square.Geometry.Outer={{0,0},{1.5,0},{1.5,1.5},{0,1.5}};
    p.Parts={body,wide,square};
    Detail::NfpCombinationOptions options;options.MaxMembers=2;options.MaxResults=1;
    options.SamePartOnly=false;
    options.MaxStates=24;options.MaxAttempts=6000;options.PrepareSelfPairs=false;
    std::size_t attempts=0;const auto pair=Build(p,options,attempts);Verify(p,pair,2);
    Require(pair.size()==1&&ActualUtilization(p,pair.front())>.85&&ActualUtilization(p,pair.front())<.86,
        "later-depth fixture did not first fill its one-result reservoir with a partial carrier");
    std::size_t certifiedTriples=0;double bestTripleUtilization=0;ConstructionTraceEvent bestTriple;
    options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source!="preparation.certified-pattern"||event.Members.size()!=3)return;
        ++certifiedTriples;
        if(event.Fill>bestTripleUtilization){bestTripleUtilization=event.Fill;bestTriple=event;}
    };
    options.MaxMembers=3;attempts=0;const auto filled=Build(p,options,attempts);Verify(p,filled,3);
    if(filled.size()!=1||filled.front().Members.size()!=3||ActualUtilization(p,filled.front())<=.899999)
    {
        std::cerr<<"later-depth diagnostics attempts="<<attempts<<" certifiedTriples="<<certifiedTriples
            <<" bestTripleU="<<bestTripleUtilization<<" returned="<<filled.size()<<'\n';
        for(const auto& pattern:filled)
        {
            std::cerr<<"returned members="<<pattern.Members.size()<<" U="<<ActualUtilization(p,pattern)
                <<" box="<<pattern.Width<<'x'<<pattern.Height<<'\n';
            for(const auto& member:pattern.Members)std::cerr<<member.PartIndex<<':'<<member.Transform.X
                <<','<<member.Transform.Y<<','<<member.Transform.AngleDegrees<<'\n';
        }
        for(const auto& member:bestTriple.Members)std::cerr<<"best-certified "<<member.PartIndex<<':'
            <<member.Transform.X<<','<<member.Transform.Y<<','<<member.Transform.AngleDegrees<<'\n';
    }
    Require(certifiedTriples>0&&filled.size()==1&&filled.front().Members.size()==3&&
        ActualUtilization(p,filled.front())>.899999&&
        ActualUtilization(p,filled.front())>ActualUtilization(p,pair.front())+.04,
        "an already full result reservoir kept a looser pair instead of the tighter certified later-depth assembly");
    Require(attempts<=options.MaxAttempts,"cross-depth density replacement escaped the existing search budget");
}
void FilledCarrierProofDoesNotInventAabbMaterial()
{
    auto p=ComplementaryTriangles(0);p.Parts[0].Geometry.Outer=p.Parts[1].Geometry.Outer={{0,0},{6,0},{8,4}};
    Detail::NfpCombinationOptions options;options.MaxMembers=2;options.MaxResults=16;
    options.SamePartOnly=false;
    options.MaxStates=24;options.MaxAttempts=2000;options.PrepareSelfPairs=false;
    options.MinimumIntermediateFill=.5;options.MinimumFinalFill=.85;
    std::size_t attempts=0;const auto carriers=Build(p,options,attempts);Verify(p,carriers,2);
    Require(std::any_of(carriers.begin(),carriers.end(),[&](const auto& pattern)
    {
        if(pattern.Kind!=ShapeKind::Parallelogram)return false;
        const auto proof=Detail::AssessNfpCombinationCarrier(p,pattern);
        return proof.Filled&&proof.Fill==1&&std::abs(proof.Area-pattern.MaterialArea)<1e-8&&
            ActualUtilization(p,pattern)<.8;
    }),"a useful mixed slanted carrier was discarded or counted its empty AABB corners as original material");
    Require(attempts<=options.MaxAttempts,"useful mixed-carrier admission expanded the original search budget");
}
void UsefulTwoOriginalInterlocksDoNotDependOnSharedInventoryIdentity()
{
    Problem mixed;mixed.Tolerance=1e-7;mixed.PartGap=.2;mixed.EdgeMargin=.5;mixed.Stocks={{"stock",30,20,1}};
    Part a;a.ID="first-original";a.FreeRotation=false;a.AllowedAnglesDegrees={0,180};
    a.Geometry.Outer={{0,0},{6,0},{8,4}};auto b=a;b.ID="second-original";mixed.Parts={a,b};
    auto repeated=mixed;repeated.Parts.resize(1);repeated.Parts[0].Quantity=2;
    Detail::NfpCombinationOptions options;options.MaxMembers=2;options.MaxResults=16;
    std::size_t defaultAttempts=0;
    Require(Build(mixed,options,defaultAttempts).empty()&&defaultAttempts==0,
        "production preparation searched unlike originals without repeat demand");
    options.SamePartOnly=false; // Geometry admission remains testable by explicit opt-in.
    options.MaxStates=24;options.MaxAttempts=2000;options.PrepareSelfPairs=false;
    const auto containsUsefulBelowThreshold=[&](const Problem& problem,const auto& patterns)
    {
        return std::any_of(patterns.begin(),patterns.end(),[&](const auto& pattern)
        {
            if(pattern.Members.size()!=2||ActualUtilization(problem,pattern)>=options.MinimumFinalFill)return false;
            double separate=0;for(const auto& member:pattern.Members)
            {const auto box=Detail::Bounds(problem.Parts[member.PartIndex].Geometry,member.Transform);separate+=box.Width*box.Height;}
            return pattern.Width*pattern.Height<separate-1e-6&&ActualUtilization(problem,pattern)>.65;
        });
    };
    std::size_t attempts=0;const auto mixedPatterns=Build(mixed,options,attempts);Verify(mixed,mixedPatterns,2);
    Require(containsUsefulBelowThreshold(mixed,mixedPatterns)&&HasMixed(mixedPatterns,2)&&attempts<=options.MaxAttempts,
        "useful different-ID interlock was denied the below-threshold admission allowed to repeated originals");
    attempts=0;const auto repeatedPatterns=Build(repeated,options,attempts);Verify(repeated,repeatedPatterns,2);
    Require(containsUsefulBelowThreshold(repeated,repeatedPatterns)&&attempts<=options.MaxAttempts,
        "identity-neutral pair admission lost an equally useful repeating-original interlock");
    Require(mixed.Parts.size()==2&&mixed.Parts[0].Quantity==1&&mixed.Parts[1].Quantity==1&&
        repeated.Parts.size()==1&&repeated.Parts[0].Quantity==2&&
        mixed.Parts[0].Geometry.Outer.size()==3&&mixed.Parts[1].Geometry.Outer.size()==3,
        "useful-pair admission substituted original inventory or geometry");
}
void SelfCoverageReturnsTheExistingTimeWindowToGeneralConstruction()
{
    auto p=ComplementaryTriangles(.2);
    Part channel;channel.ID="repeat-channel";channel.Quantity=2;channel.FreeRotation=true;
    channel.Geometry.Outer={{-7,-2.5},{-5,-2.5},{-5,.5},{5,.5},{5,-2.5},{7,-2.5},{7,2.5},{-7,2.5}};
    p.Parts.push_back(channel);
    Detail::NfpCombinationOptions options;options.MaxMembers=2;options.MaxResults=64;
    options.SamePartOnly=false;
    options.MaxStates=24;options.MaxAttempts=2000;
    double remaining=1;bool selfFinished=false,selfCertified=false;
    std::vector<ConstructionTraceEvent> summaries;
    // A deterministic caller budget advances at the actual certificate
    // boundary. The self lease expires, but the shared window still has time.
    options.RemainingPreparationSeconds=[&]{return remaining;};
    options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="preparation.certified-pattern"&&!selfFinished)
        {selfCertified=true;remaining=.4;}
        if(event.Source=="preparation.combination-summary")
        {summaries.push_back(event);if(event.Scope.ends_with("/nfp-self"))selfFinished=true;}
    };
    std::size_t attempts=0;const auto patterns=Build(p,options,attempts);Verify(p,patterns,2);
    Require(selfCertified&&selfFinished&&HasMixed(patterns,2)&&attempts<=options.MaxAttempts&&remaining==.4,
        "self preparation consumed or cancelled the general sibling's remaining original preparation window");
    Require(std::any_of(patterns.begin(),patterns.end(),[](const auto& pattern)
        {return pattern.Members.size()==2&&pattern.Members[0].PartIndex==2&&pattern.Members[1].PartIndex==2;}),
        "returning a local time lease discarded the already certified immutable self carrier");
    const auto general=std::find_if(summaries.begin(),summaries.end(),[](const auto& event)
        {return event.Scope.ends_with("/nfp-general");});
    Require(general!=summaries.end(),"general construction omitted its preparation family summary");
    const auto measure=[](const ConstructionTraceEvent& event,const char* name)
    {
        const auto found=std::find_if(event.Measures.begin(),event.Measures.end(),[&](const auto& item){return item.first==name;});
        Require(found!=event.Measures.end(),"fair preparation omitted its measured family lease");return found->second;
    };
    Require(measure(*general,"preparationFamilies")==2&&measure(*general,"remainingPreparationSecondsAtStart")==1&&
        measure(*general,"selfTimeAllowanceSeconds")==.5&&measure(*general,"reservedGeneralTimeSeconds")==.5&&
        measure(*general,"pairRegionRequests")>0&&measure(*general,"exactAttempts")>0&&
        measure(*general,"nestedSelfAttempts")<=512,
        "self/general cost isolation changed the total budget or left general construction without real NFP work");

    // An input with one repeatable specification has no mixed sibling to
    // reserve for. Keep its whole existing preparation window available.
    p.Parts={channel};remaining=1;summaries.clear();options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {if(event.Source=="preparation.combination-summary")summaries.push_back(event);};
    attempts=0;const auto singleFamily=Build(p,options,attempts);Verify(p,singleFamily,2);
    const auto only=std::find_if(summaries.begin(),summaries.end(),[](const auto& event)
        {return event.Scope.ends_with("/nfp-general");});
    Require(!singleFamily.empty()&&only!=summaries.end()&&measure(*only,"preparationFamilies")==1&&
        measure(*only,"selfTimeAllowanceSeconds")==1&&measure(*only,"reservedGeneralTimeSeconds")==0&&
        attempts<=options.MaxAttempts,"a nonexistent mixed family arbitrarily shortened existing self construction");
    options.RemainingPreparationSeconds=[]{return 0.;};attempts=0;
    Require(Build(p,options,attempts).empty()&&attempts==0,
        "exhausted caller preparation time started new original-geometry work");
}
void BasicMixedCoverageCertifiesBeforeRefiningAnEarlyRectangularSeed()
{
    Problem source;source.Tolerance=1e-7;source.PartGap=.2;source.EdgeMargin=.5;source.AllowCommonEdges=true;
    source.Stocks={{"stock",30,20,1}};
    for(std::size_t i=0;i<12;++i)
    {
        Part rectangle;rectangle.ID="early-rectangle-"+std::to_string(i);rectangle.FreeRotation=false;rectangle.AllowedAnglesDegrees={0};
        const double width=1+i*.01;rectangle.Geometry.Outer={{0,0},{width,0},{width,1},{0,1}};source.Parts.push_back(rectangle);
    }
    Part a;a.ID="late-chiral-a";a.FreeRotation=false;a.AllowMirror=true;a.AllowedAnglesDegrees={0};
    a.Geometry.Outer={{0,0},{6,0},{0,4}};auto b=a;b.ID="late-chiral-b";b.Geometry.Outer={{0,0},{-6,0},{0,4}};
    source.Parts.push_back(a);source.Parts.push_back(b);
    for(const bool reverse:{false,true})
    {
        auto p=source;if(reverse)
        {std::reverse(p.Parts.begin(),p.Parts.end());for(auto& part:p.Parts)std::rotate(part.Geometry.Outer.begin(),part.Geometry.Outer.begin()+1,part.Geometry.Outer.end());}
        const auto desired=[&](const auto& members)
        {
            if(members.size()!=2)return false;std::set<std::string> ids;
            for(const auto& member:members)ids.insert(p.Parts[member.PartIndex].ID);
            return ids==std::set<std::string>{a.ID,b.ID};
        };
        Detail::NfpCombinationOptions options;options.MaxMembers=2;options.MaxStates=4;options.MaxResults=16;
        options.SamePartOnly=false;
        options.MaxAttempts=2000;options.PrepareSelfPairs=false;
        bool stop=false,certified=false;std::size_t basicQueries=0;ConstructionTraceEvent summary;
        options.Stop=[&]{return stop;};options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            if(event.Source=="preparation.basic-pair")++basicQueries;
            if(event.Source=="preparation.certified-pattern"&&desired(event.Members)&&
                std::any_of(event.Members.begin(),event.Members.end(),[](const auto& member){return member.Transform.Mirrored;}))
            {certified=true;stop=true;}
            if(event.Source=="preparation.combination-summary")summary=event;
        };
        std::size_t attempts=0;const auto patterns=Build(p,options,attempts);Verify(p,patterns,2);
        Require(certified&&basicQueries>0&&attempts<=options.MaxAttempts&&
            std::any_of(patterns.begin(),patterns.end(),[&](const auto& pattern)
                {return desired(pattern.Members)&&std::any_of(pattern.Members.begin(),pattern.Members.end(),
                    [](const auto& member){return member.Transform.Mirrored;});}),
            "bounded basic coverage left a natural complementary mixed pair behind early rectangular beam seeds");
        const auto retained=std::find_if(summary.Measures.begin(),summary.Measures.end(),[](const auto& item)
            {return item.first=="basicCoverageRetained";});
        Require(retained!=summary.Measures.end()&&retained->second>0&&retained->second<=patterns.size(),
            "basic coverage reported attempted certificates instead of actual final retained original poses");
    }
}
void BasicStationaryCoverageUsesCompactNonOrthogonalOriginalPoses()
{
    Problem p;p.Tolerance=1e-7;p.PartGap=.2;p.EdgeMargin=.5;p.Stocks={{"stock",30,20,1}};
    Part a;a.ID="tilted-a";a.AllowMirror=true;a.FreeRotation=true;a.Geometry.Outer={{0,0},{6,0},{0,4}};
    auto b=a;b.ID="tilted-b";b.Geometry.Outer={{0,0},{-6,0},{0,4}};
    for(auto* part:{&a,&b})for(auto& point:part->Geometry.Outer)point=Detail::Transform(point,{13,-8,23.7});p.Parts={a,b};
    Detail::NfpCombinationOptions options;options.MaxMembers=2;options.MaxStates=2;options.MaxResults=16;
    options.SamePartOnly=false;
    options.MaxAttempts=2000;options.PrepareSelfPairs=false;bool compactNonOrthogonal=false;
    options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source!="preparation.basic-pair"||event.Members.empty())return;
        const auto& member=event.Members[0];const auto& shape=p.Parts[member.PartIndex].Geometry;
        const auto actual=Detail::Bounds(shape,member.Transform),original=Detail::Bounds(shape);
        const auto angle=Detail::NormalizeAngle(member.Transform.AngleDegrees);
        compactNonOrthogonal|=std::abs(angle-std::round(angle/90)*90)>1e-5&&
            actual.Width*actual.Height<original.Width*original.Height-1e-6;
    };
    std::size_t attempts=0;const auto patterns=Build(p,options,attempts);Verify(p,patterns,2);
    Require(compactNonOrthogonal&&!patterns.empty()&&attempts<=options.MaxAttempts,
        "a small state pool covered only initial cardinal angles instead of compact legal stationary originals");
}
void MirroredPairsUseSharedOriginalDemandAndAllParities()
{
    Problem p;p.Tolerance=1e-7;p.EdgeMargin=.5;p.AllowCommonEdges=true;p.Stocks={{"stock",7,5,1}};
    Part a;a.ID="left";a.FreeRotation=false;a.AllowMirror=true;a.AllowedAnglesDegrees={0};
    a.Geometry.Outer={{0,0},{6,0},{0,4}};
    auto b=a;b.ID="right";b.Geometry.Outer={{0,0},{-6,0},{0,4}};p.Parts={a,b};const auto original=p;
    Detail::NfpCombinationOptions options;options.MaxMembers=2;options.MaxResults=64;options.MaxStates=16;
    options.SamePartOnly=false;
    options.MaxAttempts=4000;options.PrepareSelfPairs=false;
    std::set<std::pair<bool,bool>> parities;
    options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="preparation.orientation-pair"&&event.Members.size()==2)
            parities.emplace(event.Members[0].Transform.Mirrored,event.Members[1].Transform.Mirrored);
    };
    std::size_t attempts=0;const auto all=Build(p,options,attempts);Verify(p,all,2);
    Require(parities.size()==4&&attempts<=options.MaxAttempts,
        "bounded NFP pair construction starved a stationary/moving reflection family");
    Require(std::any_of(all.begin(),all.end(),[](const auto& pattern)
        {return std::any_of(pattern.Members.begin(),pattern.Members.end(),[](const auto& member){return member.Transform.Mirrored;});}),
        "NFP pair publication erased the certified reflection parity");

    p.Parts[0].AllowMirror=false;attempts=0;
    const auto required=Build(p,options,attempts);Verify(p,required,2);
    Require(!required.empty()&&std::all_of(required.begin(),required.end(),[](const auto& pattern)
        {return std::all_of(pattern.Members.begin(),pattern.Members.end(),[](const auto& member)
            {return member.PartIndex==0?!member.Transform.Mirrored:member.Transform.Mirrored;});}),
        "the only legal reflected complementary carrier was lost or reflected a forbidden original");
    Options ordinary;ordinary.EnableCombinations=true;ordinary.MaxPatternMembers=2;ordinary.MaxPatterns=64;
    ordinary.MaxPlacementAttempts=20000;const auto motifs=Detail::BuildPatterns(p,ordinary,{});
    Require(std::none_of(motifs.begin(),motifs.end(),[](const auto& pattern)
        {return pattern.Members.size()>1&&!Detail::HasSinglePartType(pattern.Members);}),
        "ordinary production preparation still constructed unlike-original motifs");
    std::vector<Detail::Pattern> pairs;for(const auto& pattern:motifs)if(pattern.Members.size()>1)pairs.push_back(pattern);
    Verify(p,pairs,2);
    auto forbidden=p;forbidden.Parts[1].AllowMirror=false;attempts=0;
    Require(Build(forbidden,options,attempts).empty(),"pair construction invented a reflected substitute for an explicitly forbidden original");
    for(std::size_t i=0;i<p.Parts.size();++i)
        Require(p.Parts[i].ID==original.Parts[i].ID&&p.Parts[i].Quantity==1&&
            p.Parts[i].Geometry.Outer.size()==original.Parts[i].Geometry.Outer.size()&&
            std::equal(p.Parts[i].Geometry.Outer.begin(),p.Parts[i].Geometry.Outer.end(),original.Parts[i].Geometry.Outer.begin(),
                [](Point x,Point y){return x.X==y.X&&x.Y==y.Y;}),"mirror pair preparation rewrote geometry or duplicated original inventory");
}
void ProposalPolicyKeepsTheSameOriginalProblemAndIndependentSearch()
{
    Problem p;p.Tolerance=1e-7;p.EdgeMargin=.5;p.AllowCommonEdges=true;p.Stocks={{"stock",20,12,1}};
    Part a;a.ID="left";a.FreeRotation=false;a.AllowMirror=true;a.AllowedAnglesDegrees={0};
    a.Geometry.Outer={{0,0},{6,0},{0,4}};
    auto b=a;b.ID="right";b.Geometry.Outer={{0,0},{-6,0},{0,4}};p.Parts={a,b};const auto original=p;
    Detail::NfpCombinationOptions options;options.MaxMembers=2;options.MaxResults=64;options.MaxStates=16;
    options.SamePartOnly=false;
    options.MaxAttempts=2000;options.PrepareSelfPairs=false;
    options.MinimumIntermediateFill=options.MinimumFinalFill=.4;
    options.OrientationProposals=OrientationProposalPolicy::OriginalOnly;
    bool reflectedProposal=false;
    options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source!="preparation.orientation-pair"&&event.Source!="preparation.certified-pattern")return;
        for(const auto& member:event.Members)reflectedProposal|=member.Transform.Mirrored;
    };
    const auto allOriginal=[](const auto& patterns)
    {
        return std::all_of(patterns.begin(),patterns.end(),[](const auto& pattern)
        {return std::all_of(pattern.Members.begin(),pattern.Members.end(),[](const auto& member)
            {return !member.Transform.Mirrored;});});
    };
    std::size_t attempts=0;const auto originals=Build(p,options,attempts);Verify(p,originals,2);
    Require(!originals.empty()&&allOriginal(originals)&&!reflectedProposal&&attempts<=options.MaxAttempts,
        "original-only combination construction proposed or published a reflected member");

    options.OrientationProposals=OrientationProposalPolicy::AllowedReflections;options.ConstructionTrace={};
    attempts=0;const auto reflections=Build(p,options,attempts);Verify(p,reflections,2);
    Require(!allOriginal(reflections)&&std::any_of(reflections.begin(),reflections.end(),[&](const auto& pattern)
        {return ActualUtilization(p,pattern)>.999999;})&&attempts<=options.MaxAttempts,
        "independent allowed-reflection construction lost the mirrored complementary carrier");

    // The nested self pass and whole-pair orientation variants must inherit
    // the proposal policy while preserving the same hard mirror permission.
    auto self=p;self.PartGap=.2;self.Parts.resize(1);self.Parts[0].Quantity=2;
    self.Parts[0].AllowedAnglesDegrees={0,90,180,270};
    options.OrientationProposals=OrientationProposalPolicy::OriginalOnly;options.PrepareSelfPairs=true;
    options.SamePartOnly=true;
    attempts=0;const auto repeating=Build(self,options,attempts);Verify(self,repeating,2);
    Require(!repeating.empty()&&allOriginal(repeating)&&attempts<=options.MaxAttempts&&self.Parts[0].AllowMirror,
        "self-pair preparation or its derived orientations escaped original-only proposal policy");
    for(std::size_t i=0;i<p.Parts.size();++i)
        Require(p.Parts[i].AllowMirror==original.Parts[i].AllowMirror&&
            p.Parts[i].Quantity==original.Parts[i].Quantity&&
            p.Parts[i].AllowedAnglesDegrees==original.Parts[i].AllowedAnglesDegrees&&
            p.Parts[i].Geometry.Outer.size()==original.Parts[i].Geometry.Outer.size()&&
            std::equal(p.Parts[i].Geometry.Outer.begin(),p.Parts[i].Geometry.Outer.end(),original.Parts[i].Geometry.Outer.begin(),
                [](Point x,Point y){return x.X==y.X&&x.Y==y.Y;}),
            "proposal policy rewrote original permissions, geometry, directions or demand");
}

void ProductionPreparationDoesNoCrossOriginalWork()
{
    auto p=ComplementaryTriangles(0,4);
    for(auto& part:p.Parts){part.AllowedAnglesDegrees={0,180};part.AllowMirror=true;}
    const auto original=p;
    const auto measure=[](const ConstructionTraceEvent& event,const char* name)
    {
        const auto found=std::find_if(event.Measures.begin(),event.Measures.end(),
            [&](const auto& item){return item.first==name;});
        Require(found!=event.Measures.end(),"same-original preparation lost its actual-work counter");
        return found->second;
    };
    for(const bool expand:{false,true})
    {
        Detail::NfpCombinationOptions options;options.ExpandAssemblies=expand;
        options.MaxMembers=4;options.MaxResults=64;options.MaxStates=16;options.MaxAttempts=2000;
        Require(options.SamePartOnly,"production combination policy must default to one original type");
        std::size_t summaries=0,directions=0;double queries=0;bool crossOriginalEvent=false;
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            if(event.Source=="preparation.orientation-pair"||event.Source=="preparation.basic-pair"||
                event.Source=="preparation.certified-pattern")
            {
                if(event.Source=="preparation.orientation-pair")++directions;
                if(!event.Members.empty())for(const auto& member:event.Members)
                    crossOriginalEvent|=member.PartIndex!=event.Members.front().PartIndex;
            }
            if(event.Source!="preparation.combination-summary")return;
            ++summaries;queries+=measure(event,"pairRegionRequests");
            Require(measure(event,"samePartOnly")==1&&measure(event,"crossPartRegionRequests")==0&&
                measure(event,"crossPartBeamChoices")==0&&measure(event,"basicCoverageSeeds")==0&&
                measure(event,"basicCoverageQueries")==0,
                "production preparation performed unlike-original preprocessing, NFP queries or beam expansion");
        };
        std::size_t attempts=0;const auto patterns=Build(p,options,attempts);Verify(p,patterns,4);
        Require(!patterns.empty()&&summaries>0&&directions>0&&queries>0&&!crossOriginalEvent&&
            attempts>0&&attempts<=options.MaxAttempts,
            "same-original policy removed real self contact work instead of excluding only unlike originals");
        Require(std::all_of(patterns.begin(),patterns.end(),[](const auto& pattern)
            {return Detail::HasSinglePartType(pattern.Members);})&&
            std::any_of(patterns.begin(),patterns.end(),[](const auto& pattern){return pattern.Members.size()==4;}),
            "same-original preparation mixed stock identities or lost its legal self-pair repeat cell");
    }
    // Allowed 0/270-degree turns keep both right-angle corners on the left.
    // Reflection negates Y before rotation: the mirrored 270-degree copy
    // maps (x,y) to (-y,-x), fills the top-right corner and completes
    // the exact 6 x 6 carrier. Whole-pair reflection cannot replace this pose.
    auto reflected=p;reflected.Parts.resize(1);reflected.Parts[0].Quantity=2;
    reflected.Parts[0].AllowedAnglesDegrees={0,270};reflected.Stocks[0].Width=7;reflected.Stocks[0].Height=7;
    Detail::NfpCombinationOptions mirrorOptions;mirrorOptions.MaxMembers=2;mirrorOptions.MaxAttempts=2000;
    mirrorOptions.ExpandAssemblies=false;
    Result mirrorWitness;mirrorWitness.State=Status::Complete;
    mirrorWitness.Layouts={{"stock",0,7,7,{{reflected.Parts[0].ID,0,{.5,.5,0,false},1,false},
        {reflected.Parts[0].ID,1,{6.5,6.5,270,true},1,false}},36}};
    Require(Validate(reflected,mirrorWitness).Valid&&Validate(reflected,mirrorWitness).Complete,
        "necessary self-reflection fixture has no legal original-member witness");
    std::size_t mirrorAttempts=0;const auto mirrorPairs=Build(reflected,mirrorOptions,mirrorAttempts);
    Verify(reflected,mirrorPairs,2);
    Require(std::any_of(mirrorPairs.begin(),mirrorPairs.end(),[](const auto& pattern)
        {return Detail::HasSinglePartType(pattern.Members)&&std::any_of(pattern.Members.begin(),pattern.Members.end(),
            [](const auto& member){return member.Transform.Mirrored&&member.Transform.AngleDegrees==270;});})&&
        mirrorAttempts<=mirrorOptions.MaxAttempts,
        "same-original preparation removed a necessary permitted self reflection");
    reflected.Parts[0].AllowMirror=false;mirrorAttempts=0;
    Require(Build(reflected,mirrorOptions,mirrorAttempts).empty()&&mirrorAttempts<=mirrorOptions.MaxAttempts,
        "same-original preparation bypassed a forbidden reflection with fixed 0/270-degree turns");
    // Identical contours with different inventory IDs are still different
    // original types. With one copy each there is no eligible self query.
    auto singleton=p;for(auto& part:singleton.Parts)part.Quantity=1;
    Detail::NfpCombinationOptions options;options.MaxAttempts=2000;
    double queries=0,crossQueries=0;
    options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source!="preparation.combination-summary")return;
        queries+=measure(event,"pairRegionRequests");crossQueries+=measure(event,"crossPartRegionRequests");
    };
    std::size_t attempts=0;const auto excluded=Build(singleton,options,attempts);
    Require(excluded.empty()&&attempts==0&&queries==0&&crossQueries==0,
        "different original IDs were combined despite having no repeat demand");
    // Positive control verifies that zero means no query was made, rather
    // than an unwired counter or final-result-only filtering.
    options.SamePartOnly=false;attempts=0;queries=crossQueries=0;
    const auto explicitMixed=Build(singleton,options,attempts);Verify(singleton,explicitMixed,4);
    Require(HasMixed(explicitMixed,2)&&queries>0&&crossQueries>0,
        "explicit low-level mixed control did not exercise the unlike-original query counter");
    for(std::size_t i=0;i<p.Parts.size();++i)
        Require(p.Parts[i].Quantity==original.Parts[i].Quantity&&p.Parts[i].AllowMirror==original.Parts[i].AllowMirror&&
            p.Parts[i].AllowedAnglesDegrees==original.Parts[i].AllowedAnglesDegrees&&
            p.Parts[i].Geometry.Outer.size()==original.Parts[i].Geometry.Outer.size()&&
            std::equal(p.Parts[i].Geometry.Outer.begin(),p.Parts[i].Geometry.Outer.end(),original.Parts[i].Geometry.Outer.begin(),
                [](Point a,Point b){return a.X==b.X&&a.Y==b.Y;}),
            "self-only preparation rewrote source inventory, geometry or orientation permissions");
}
void ExplicitMixedCompactPreparationRetainsItsGeometryCertificates()
{
    auto p=ComplementaryTriangles(0,4);p.Parts[1].Geometry=p.Parts[0].Geometry;
    for(auto& part:p.Parts){part.AllowedAnglesDegrees={0,180};part.AllowMirror=true;}
    const auto original=p;
    Detail::NfpCombinationOptions options;
    Require(options.SamePartOnly,"production combination defaults unexpectedly enable unlike originals");
    options.SamePartOnly=false;
    Require(options.ExpandAssemblies,"independent combination API no longer defaults to its full assembly search");
    options.MaxMembers=4;options.MaxResults=64;options.MaxStates=16;options.MaxAttempts=2000;
    options.ExpandAssemblies=false;
    struct Observed
    {
        bool SelfFinished=false,SelfCertified=false,SelfRetained=false;
        std::size_t SelfDirections=0,GeneralDirections=0,Certified=0,BasicPairs=0;
        ConstructionTraceEvent General;
    } observed;
    const auto measure=[](const ConstructionTraceEvent& event,const char* name)
    {
        const auto value=std::find_if(event.Measures.begin(),event.Measures.end(),[&](const auto& pair){return pair.first==name;});
        Require(value!=event.Measures.end(),"compact preparation omitted its mechanism summary");return value->second;
    };
    options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="preparation.orientation-pair")
        {if(observed.SelfFinished)++observed.GeneralDirections;else ++observed.SelfDirections;}
        if(event.Source=="preparation.basic-pair")++observed.BasicPairs;
        if(event.Source=="preparation.certified-pattern")
        {++observed.Certified;if(!observed.SelfFinished)observed.SelfCertified=true;}
        if(event.Source=="preparation.combination-summary")
        {
            if(event.Scope.ends_with("/nfp-self"))
            {observed.SelfFinished=true;observed.SelfRetained=event.Accepted;}
            else if(event.Scope.ends_with("/nfp-general"))observed.General=event;
        }
    };
    std::size_t attempts=0;const auto compact=Build(p,options,attempts);Verify(p,compact,4);
    const auto self=[](const Detail::Pattern& pattern)
    {
        return pattern.Members.size()==2&&pattern.Members[0].PartIndex==pattern.Members[1].PartIndex;
    };
    Require(observed.SelfFinished&&observed.SelfCertified&&observed.SelfRetained&&observed.SelfDirections>0&&
        observed.GeneralDirections==0&&observed.Certified>0&&observed.BasicPairs>0&&HasMixed(compact,2)&&
        std::any_of(compact.begin(),compact.end(),self),
        "compact preparation removed self/mixed original certificates or still entered the general orientation beam");
    Require(std::any_of(compact.begin(),compact.end(),[](const auto& pattern){return pattern.Members.size()==4;})&&
        attempts>0&&attempts<=options.MaxAttempts,
        "compact preparation lost certified four-original pair-repeat cells or shared work accounting");
    Require(observed.General.Reason=="compact-neighbour-coverage-and-pair-repeat-cells"&&observed.General.SearchTruncated&&
        measure(observed.General,"expandAssemblies")==0&&measure(observed.General,"coverageTruncated")==1&&
        measure(observed.General,"basicCoverageRetained")>0,
        "compact family was reported as exhaustive or omitted its returned mixed-original evidence");

    // A hard quantity/pose restriction must remain a restriction even though
    // the compact path now returns its certificates before general expansion.
    auto restricted=p;restricted.Parts[0].Quantity=1;restricted.Parts[0].AllowMirror=false;
    options.ConstructionTrace={};attempts=0;const auto limited=Build(restricted,options,attempts);Verify(restricted,limited,4);
    Require(HasMixed(limited,2)&&std::none_of(limited.begin(),limited.end(),[](const auto& pattern)
    {
        std::size_t used=0;for(const auto& member:pattern.Members)if(member.PartIndex==0)
        {if(member.Transform.Mirrored)return true;++used;}return used>1;
    }),"compact mixed coverage or pair repeats widened original reflection permission or inventory");

    // With no self demand, every direction-pair beam event would be general.
    // These complementary originals need an actual reflected original for a
    // full carrier at their sole allowed angle, rather than a widened angle.
    Problem reflected;reflected.Tolerance=1e-7;reflected.EdgeMargin=.5;reflected.AllowCommonEdges=true;
    reflected.Stocks={{"stock",20,12,1}};
    Part a;a.ID="left";a.AllowMirror=true;a.FreeRotation=false;a.AllowedAnglesDegrees={0};
    a.Geometry.Outer={{0,0},{6,0},{0,4}};auto b=a;b.ID="right";b.Geometry.Outer={{0,0},{-6,0},{0,4}};
    reflected.Parts={a,b};std::size_t directions=0;
    options.MaxMembers=2;options.PrepareSelfPairs=false;options.MinimumIntermediateFill=options.MinimumFinalFill=.4;
    options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {if(event.Source=="preparation.orientation-pair")++directions;};
    attempts=0;const auto mirrors=Build(reflected,options,attempts);Verify(reflected,mirrors,2);
    Require(directions==0&&HasMixed(mirrors,2)&&std::any_of(mirrors.begin(),mirrors.end(),[&](const auto& pattern)
    {
        return ActualUtilization(reflected,pattern)>.999999&&std::any_of(pattern.Members.begin(),pattern.Members.end(),
            [](const auto& member){return member.Transform.Mirrored;});
    }),"compact mixed coverage failed to certify its necessary allowed reflection without the general beam");

    Detail::NfpCombinationOptions full;full.MaxMembers=4;full.MaxResults=64;full.MaxStates=16;full.MaxAttempts=2000;
    full.SamePartOnly=false;
    bool selfFinished=false;std::size_t generalDirections=0;ConstructionTraceEvent summary;
    full.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="preparation.combination-summary"&&event.Scope.ends_with("/nfp-self"))selfFinished=true;
        if(event.Source=="preparation.orientation-pair"&&selfFinished)++generalDirections;
        if(event.Source=="preparation.combination-summary"&&event.Scope.ends_with("/nfp-general"))summary=event;
    };
    attempts=0;const auto expanded=Build(p,full,attempts);Verify(p,expanded,4);
    Require(full.ExpandAssemblies&&generalDirections>0&&measure(summary,"expandAssemblies")==1&&
        summary.Reason!="compact-neighbour-coverage-and-pair-repeat-cells"&&!expanded.empty()&&attempts<=full.MaxAttempts,
        "explicit mixed combination construction no longer executes its full general assembly path");
    for(std::size_t i=0;i<p.Parts.size();++i)
        Require(p.Parts[i].Quantity==original.Parts[i].Quantity&&p.Parts[i].AllowMirror==original.Parts[i].AllowMirror&&
            p.Parts[i].AllowedAnglesDegrees==original.Parts[i].AllowedAnglesDegrees&&
            p.Parts[i].Geometry.Outer.size()==original.Parts[i].Geometry.Outer.size()&&
            std::equal(p.Parts[i].Geometry.Outer.begin(),p.Parts[i].Geometry.Outer.end(),original.Parts[i].Geometry.Outer.begin(),
                [](Point x,Point y){return x.X==y.X&&x.Y==y.Y;}),
            "compact/full preparation modified original quantities, reflection policy, angles or geometry");
}

void PreparedVertexDifferencesPreserveEveryAlignmentCut()
{
    struct Edge{Point A,B;};
    const auto reference=[](const std::vector<Point>& anchor,Pose fixed,const std::vector<Point>& moving,Pose next,
        Edge edge,double tolerance,std::size_t maxPairs)
    {
        std::vector<double> cuts;std::size_t pairs=0;
        for(const auto va:anchor)for(const auto vb:moving)
        {
            if(++pairs>maxPairs)break;
            const auto a=Detail::Transform(va,fixed),b=Detail::Transform(vb,next);
            if(std::abs(edge.B.X-edge.A.X)>tolerance)
            {const auto t=(a.X-b.X-edge.A.X)/(edge.B.X-edge.A.X);if(t>0&&t<1)cuts.push_back(t);}
            if(std::abs(edge.B.Y-edge.A.Y)>tolerance)
            {const auto t=(a.Y-b.Y-edge.A.Y)/(edge.B.Y-edge.A.Y);if(t>0&&t<1)cuts.push_back(t);}
        }
        return cuts;
    };
    const auto same=[](const auto& a,const auto& b)
    {
        return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](double x,double y)
            {return std::bit_cast<std::uint64_t>(x)==std::bit_cast<std::uint64_t>(y);});
    };
    const auto finalize=[](std::vector<double> cuts)
    {
        std::sort(cuts.begin(),cuts.end());
        cuts.erase(std::unique(cuts.begin(),cuts.end(),[](double x,double y){return std::abs(x-y)<1e-9;}),cuts.end());
        return cuts;
    };
    const std::vector<Edge> edges{{{-50,-50},{50,50}},{{50,50},{-50,-50}},{{0,-50},{0,50}},
        {{-50,0},{50,0}},{{3,4},{3,4}},{{0,0},{1e-8,100}},{{-1,-1},{3,3}},
        {{std::nextafter(1.,0.),-2},{std::nextafter(1.,2.),2}}};
    const std::vector<std::pair<Pose,Pose>> orientations{{{0,0,0},{0,0,0}},{{0,0,90},{0,0,270}},
        {{0,0,37},{0,0,23.7}},{{0,0,-23.7},{0,0,359.65452870729825}}};
    std::vector<Point> anchor,moving;
    for(std::size_t i=0;i<73;++i)
    {const auto a=2*Detail::Pi*double(i)/73,r=i%2?23.:20.;anchor.push_back({r*std::cos(a),r*std::sin(a)});}
    for(std::size_t i=0;i<37;++i)
    {const auto a=2*Detail::Pi*double(i)/37,r=i%2?17.:13.;moving.push_back({r*std::cos(a),r*std::sin(a)});}
    const auto verify=[&](const std::vector<Point>& first,const std::vector<Point>& second,const std::vector<Edge>& segments)
    {
        for(auto [fixed,next]:orientations)for(const bool firstMirrored:{false,true})for(const bool nextMirrored:{false,true})
        for(const std::size_t limit:{std::size_t{},std::size_t{1},std::size_t{7},std::size_t{2048}})
        {
            fixed.Mirrored=firstMirrored;next.Mirrored=nextMirrored;
            const auto prepared=Detail::PrepareNfpVertexPairDifferences(first,fixed,second,next,limit,[]{return false;});
            Require(prepared.Complete&&prepared.Values.size()==std::min(limit,first.size()*second.size()),
                "cached vertex differences changed the original anchor-major sample prefix");
            for(const auto edge:segments)for(const double tolerance:{0.,1e-7})
            {
                std::vector<double> actual;
                Require(Detail::AppendNfpVertexPairCuts(prepared,edge.A,edge.B,tolerance,actual,[]{return false;}),
                    "complete cached vertex differences reported cancellation");
                const auto expected=reference(first,fixed,second,next,edge,tolerance,limit);
                Require(same(actual,expected)&&same(finalize(actual),finalize(expected)),
                    "cached vertex differences changed exact t values, ordering or final alignment cuts");
                std::vector<double> ranged;
                Require(Detail::AppendNfpVertexPairIntervalCuts(prepared,edge.A,edge.B,tolerance,ranged,[]{return false;})&&
                    same(finalize(ranged),finalize(expected)),
                    "strict axis-difference range omitted or changed an original final alignment cut");
            }
        }
    };
    verify(anchor,moving,edges);
    const double large=.75*std::numeric_limits<double>::max(),tiny=std::numeric_limits<double>::denorm_min();
    verify({{large,large},{-large,-large},{1e16,1e16},{1e150,1e-150},{0,tiny},{-0.,0}},
        {{large,-large},{-large,large},{1e16,1e16},{-1e150,1e-150},{tiny,0}},
        {{{-large,0},{large,1}},{{-1e150,-1e-150},{1e150,1e-150}},{{-1,-1},{3,3}},{{0,0},{tiny,tiny}}});
    verify({{1,1},{std::nextafter(1.,2.),std::nextafter(1.,2.)},
            {std::nextafter(2.,1.),std::nextafter(2.,1.)},{2,2},
            {std::nextafter(1.,0.),std::nextafter(1.,0.)},{std::nextafter(2.,3.),std::nextafter(2.,3.)},
            {0,0},{-0.,-0.}},{{0,0}},{{{1,1},{2,2}},{{2,2},{1,1}},{{-1,-1},{1,1}}});
    const auto interrupted=Detail::PrepareNfpVertexPairDifferences(anchor,{},moving,{},2048,[]{return true;});
    Require(!interrupted.Complete&&interrupted.Values.empty(),"cached vertex preparation ignored caller cancellation");
    const auto prepared=Detail::PrepareNfpVertexPairDifferences(anchor,{},moving,{},2048,[]{return false;});
    std::vector<double> stopped{.25};
    Require(!Detail::AppendNfpVertexPairCuts(prepared,{-50,-50},{50,50},0,stopped,[]{return true;})&&
        stopped.size()==1&&stopped.front()==.25,"cached vertex edge sampling ignored caller cancellation");
    Require(!Detail::AppendNfpVertexPairIntervalCuts(prepared,{-50,-50},{50,50},0,stopped,[]{return true;})&&
        stopped.size()==1&&stopped.front()==.25,"interval vertex edge sampling ignored entry cancellation");
    std::size_t checks=0;std::vector<double> partial;
    Require(!Detail::AppendNfpVertexPairIntervalCuts(prepared,{-50,-50},{50,50},0,partial,[&]{return ++checks>=2;}),
        "interval vertex edge sampling ignored cancellation in its selected differences");
}
}

int main()try
{
    ProductionPreparationDoesNoCrossOriginalWork();
    ExplicitMixedCompactPreparationRetainsItsGeometryCertificates();
    PreparedVertexDifferencesPreserveEveryAlignmentCut();
    ProposalPolicyKeepsTheSameOriginalProblemAndIndependentSearch();
    MirroredPairsUseSharedOriginalDemandAndAllParities();
    UtilizationWinsOverTheFormerSavedEnvelopeScore();
    DenserLaterDepthReplacesAnEarlierFullReservoir();
    FilledCarrierProofDoesNotInventAabbMaterial();
    UsefulTwoOriginalInterlocksDoNotDependOnSharedInventoryIdentity();
    SelfCoverageReturnsTheExistingTimeWindowToGeneralConstruction();
    BasicMixedCoverageCertifiesBeforeRefiningAnEarlyRectangularSeed();
    BasicStationaryCoverageUsesCompactNonOrthogonalOriginalPoses();
    Detail::NfpCombinationOptions options;options.MaxAttempts=6000;options.MaxStates=24;
    options.MaxResults=64;options.MaxMembers=2;
    auto p=ComplementaryTriangles(.2);std::size_t attempts=0;
    Require(Build(p,options,attempts).empty()&&attempts==0,
        "default preparation combined two different one-copy original types");
    options.SamePartOnly=false;
    const auto mixed=Build(p,options,attempts);Verify(p,mixed,2);
    Require(HasMixed(mixed,2),"positive-gap contacts did not construct unlike complementary originals");
    Require(attempts>0&&attempts<=options.MaxAttempts,"combination attempts escaped the shared bound");
    bool overlappingBoxes=false;
    for(const auto& pattern:mixed)
    {
        const auto& a=pattern.Members[0];const auto& b=pattern.Members[1];
        const auto first=Detail::Bounds(p.Parts[a.PartIndex].Geometry,a.Transform);
        const auto second=Detail::Bounds(p.Parts[b.PartIndex].Geometry,b.Transform);
        overlappingBoxes|=std::min(first.X+first.Width,second.X+second.Width)>std::max(first.X,second.X)&&
            std::min(first.Y+first.Height,second.Y+second.Height)>std::max(first.Y,second.Y);
    }
    Require(overlappingBoxes,"positive-gap combinations fell back to disjoint bounding boxes");

    auto shifted=p;shifted.Parts[0].AllowedAnglesDegrees={90};shifted.Parts[1].AllowedAnglesDegrees={270};
    for(auto& point:shifted.Parts[0].Geometry.Outer){point.X-=7;point.Y+=11;}
    for(auto& point:shifted.Parts[1].Geometry.Outer){point.X+=13;point.Y-=5;}
    attempts=0;const auto moved=Build(shifted,options,attempts);Verify(shifted,moved,2);
    Require(HasMixed(moved,2),"clearance contacts lost unlike local origins and quarter-turn poses");

    auto asymmetric=ComplementaryTriangles(.2,2);asymmetric.Parts[1].Quantity=1;
    auto threeOptions=options;threeOptions.MaxMembers=3;threeOptions.MinimumIntermediateFill=.45;
    threeOptions.MinimumFinalFill=.45;attempts=0;
    const auto three=Build(asymmetric,threeOptions,attempts);Verify(asymmetric,three,3);
    Require(HasMixed(three,3),"three-member combination did not retain asymmetric 2A+1B demand");

    auto repeated=ComplementaryTriangles(.2,2);auto fourOptions=options;fourOptions.MaxMembers=4;
    attempts=0;const auto four=Build(repeated,fourOptions,attempts);Verify(repeated,four,4);
    Require(HasMixed(four,4),"dense positive-gap pair could not repeat as a real four-member unit");

    auto zero=p;zero.PartGap=0;attempts=0;
    const auto touching=Build(zero,options,attempts);Verify(zero,touching,2);
    Require(HasMixed(touching,2)&&std::any_of(touching.begin(),touching.end(),[](const auto& pattern)
        {return pattern.Kind==ShapeKind::Rectangle;}),"zero-gap complementary carrier behavior regressed");

    auto limited=options;limited.MaxAttempts=3;attempts=0;
    const auto bounded=Build(p,limited,attempts);Verify(p,bounded,2);
    Require(attempts<=3,"small positive-gap combination attempt cap was exceeded");
    // The complementary orientation is deliberately later than same-angle
    // contacts. Geometry proposals must compare all legal relative angles
    // before a tiny exact-validation allowance is spent on the first edge.
    auto scarce=p;scarce.Stocks[0].Width=scarce.Stocks[0].Height=7.5;
    scarce.Parts[1].AllowedAnglesDegrees={270,90,0,180};
    for(bool reverse:{false,true})
    {
        auto reordered=scarce;
        if(reverse)
        {
            std::reverse(reordered.Parts.begin(),reordered.Parts.end());
            for(auto& part:reordered.Parts)
            {
                std::reverse(part.AllowedAnglesDegrees.begin(),part.AllowedAnglesDegrees.end());
                std::rotate(part.Geometry.Outer.begin(),part.Geometry.Outer.begin()+1,part.Geometry.Outer.end());
            }
        }
        attempts=0;const auto compact=Build(reordered,limited,attempts);Verify(reordered,compact,2);
        Require(attempts<=3&&HasMixed(compact,2),
            "tiny validation allowance starved a later legal relative angle or changed with input order");
        Require(std::any_of(compact.begin(),compact.end(),[](const auto& pattern)
            {return pattern.MaterialArea/(pattern.Width*pattern.Height)>.85;}),
            "tiny allowance returned adjacent boxes rather than a real sloped interlock");
    }
    auto cancelled=options;cancelled.Stop=[]{return true;};attempts=0;
    Require(Build(p,cancelled,attempts).empty()&&attempts==0,"cancelled clearance preparation produced work");
    Problem sparse;sparse.PartGap=.2;sparse.EdgeMargin=.5;sparse.Tolerance=1e-7;
    sparse.Stocks={{"stock",100,100,1}};
    for(std::size_t i=0;i<24;++i)
    {
        Part other;other.ID="single-"+std::to_string(i);other.Quantity=1;
        other.Geometry.Outer={{0,0},{2,0},{2,1},{0,1}};sparse.Parts.push_back(other);
    }
    Part channel;channel.ID="repeat-channel";channel.Quantity=2;channel.FreeRotation=true;
    channel.Geometry.Outer={{-7,-2.5},{-5,-2.5},{-5,.5},{5,.5},{5,-2.5},{7,-2.5},{7,2.5},{-7,2.5}};
    for(auto& point:channel.Geometry.Outer)point=Detail::Transform(point,{13,-8,17.25});
    sparse.Parts.push_back(channel);
    auto sparseOptions=options;sparseOptions.MaxStates=4;sparseOptions.MaxAttempts=2000;
    std::vector<ConstructionTraceEvent> summaries,sparsePreparationSummaries;
    sparseOptions.ConstructionTrace=[&](const auto& event)
    {
        if(event.Source=="preparation.pair-summary")summaries.push_back(event);
        if(event.Source=="preparation.combination-summary")sparsePreparationSummaries.push_back(event);
    };
    attempts=0;const auto carriers=Build(sparse,sparseOptions,attempts);Verify(sparse,carriers,2);
    Require(std::any_of(carriers.begin(),carriers.end(),[&](const auto& pattern)
    {
        if(pattern.Members.size()!=2||pattern.Members[0].PartIndex!=24||pattern.Members[1].PartIndex!=24)return false;
        double separate=0;for(const auto& member:pattern.Members)
        {const auto b=Detail::Bounds(channel.Geometry,member.Transform);separate+=b.Width*b.Height;}
        return pattern.Width*pattern.Height<.9*separate&&pattern.MaterialArea/(pattern.Width*pattern.Height)<.85;
    }),"later sparse self pair was lost to early single specifications or an absolute density gate");
    Require(attempts<=sparseOptions.MaxAttempts,"self-carrier construction exceeded its shared attempt budget");
    Require(carriers.size()<=sparseOptions.MaxResults,"self-pair preparation coverage expanded the finite result pool");
    double bestSelfUtilization=0;
    for(std::size_t i=0;i<carriers.size();++i)
    {
        const double fill=ActualUtilization(sparse,carriers[i]);
        if(i)Require(ActualUtilization(sparse,carriers[i-1])>=fill,
            "preparation coverage awarded a self pair a higher quality order than its real utilization");
        if(carriers[i].Members.size()==2&&carriers[i].Members[0].PartIndex==24&&carriers[i].Members[1].PartIndex==24)
            bestSelfUtilization=std::max(bestSelfUtilization,fill);
    }
    Require(!carriers.empty()&&ActualUtilization(sparse,carriers.front())>bestSelfUtilization+.01,
        "coverage fixture did not retain a better-utilization mixed alternative ahead of its protected sparse self pair");
    const auto generalPreparation=std::find_if(sparsePreparationSummaries.begin(),sparsePreparationSummaries.end(),[](const auto& event)
        {return event.Scope.ends_with("/nfp-general");});
    Require(generalPreparation!=sparsePreparationSummaries.end(),"general preparation omitted its bounded coverage summary");
    const auto coverage=std::find_if(generalPreparation->Measures.begin(),generalPreparation->Measures.end(),[](const auto& item)
        {return item.first=="selfCoverageSlots";});
    Require(coverage!=generalPreparation->Measures.end()&&coverage->second>0&&
        coverage->second<=sparseOptions.MaxResults/4,
        "self-pair coverage was not confined to its existing one-quarter preparation result budget");
    const auto summary=std::find_if(summaries.begin(),summaries.end(),[](const auto& event)
        {return event.PartIndex==24&&event.Scope.ends_with("/nfp-self");});
    Require(summary!=summaries.end()&&summary->Members.size()==2&&summary->Accepted&&summary->Saving>.1,
        "NFP summary omitted a later retained self carrier or its real saving");

    // A local preparation deadline can expire after a complete original-pair
    // certificate. Returning that immutable private template needs no new
    // geometry and is independent of the caller's later board-commit gate.
    auto stopping=sparse;stopping.Parts={channel};
    auto stoppingOptions=sparseOptions;stoppingOptions.PrepareSelfPairs=false;stoppingOptions.SelfPairsOnly=true;
    bool searchStopped=false;std::size_t certifiedAttempts=0,certificatesAfterStop=0;
    ConstructionTraceEvent certificate;std::vector<ConstructionTraceEvent> stoppedSummaries,preparationSummaries;
    stoppingOptions.Stop=[&]{return searchStopped;};
    stoppingOptions.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="preparation.certified-pattern")
        {
            if(searchStopped){++certificatesAfterStop;return;}
            certificate=event;certifiedAttempts=attempts;searchStopped=true;
        }
        if(event.Source=="preparation.pair-summary")stoppedSummaries.push_back(event);
        if(event.Source=="preparation.combination-summary")preparationSummaries.push_back(event);
    };
    attempts=0;const auto publishedAfterStop=Build(stopping,stoppingOptions,attempts);
    Require(searchStopped&&certifiedAttempts>0&&attempts==certifiedAttempts&&certificatesAfterStop==0,
        "a search stop after certification allowed new geometric attempts or failed to exercise the publication boundary");
    Require(publishedAfterStop.size()==1&&certificate.Accepted&&certificate.Members.size()==2,
        "a local search stop discarded an already certified immutable self pair");
    const auto same=[](double a,double b)
        {return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);};
    const auto& published=publishedAfterStop.front();
    Require(same(published.Width,certificate.Width)&&same(published.Height,certificate.Height)&&
        same(published.MaterialArea,certificate.MaterialArea)&&published.Members.size()==certificate.Members.size(),
        "publication after stopping rebuilt a certified carrier's stored envelope or member set");
    for(std::size_t i=0;i<published.Members.size();++i)
    {
        const auto& actual=published.Members[i];const auto& sealed=certificate.Members[i];
        Require(actual.PartIndex==sealed.PartIndex&&same(actual.Transform.X,sealed.Transform.X)&&
            same(actual.Transform.Y,sealed.Transform.Y)&&same(actual.Transform.AngleDegrees,sealed.Transform.AngleDegrees),
            "publication after stopping translated or altered an already certified original pose");
    }
    Verify(stopping,publishedAfterStop,2);
    Require(std::any_of(stoppedSummaries.begin(),stoppedSummaries.end(),[](const auto& event)
        {return event.PartIndex==0&&event.Accepted&&event.SearchTruncated&&event.Reason=="retained";}),
        "pair summary reported a lost candidate instead of the retained result at the search boundary");
    const auto measure=[](const ConstructionTraceEvent& event,const char* name)
    {
        const auto value=std::find_if(event.Measures.begin(),event.Measures.end(),
            [&](const auto& item){return item.first==name;});
        Require(value!=event.Measures.end(),"combination preparation summary omitted a required counter");
        return value->second;
    };
    Require(preparationSummaries.size()==1&&preparationSummaries[0].Stage==ConstructionTraceStage::Complete&&
        preparationSummaries[0].Accepted&&preparationSummaries[0].SearchTruncated&&
        measure(preparationSummaries[0],"pairRegionRequests")>0&&
        measure(preparationSummaries[0],"uniqueProposals")>0&&
        measure(preparationSummaries[0],"exactAttempts")==certifiedAttempts&&
        measure(preparationSummaries[0],"certifiedCandidates")>=1&&
        measure(preparationSummaries[0],"retainedPatterns")==1&&
        measure(preparationSummaries[0],"highestRetainedUtilization")==certificate.Fill&&
        measure(preparationSummaries[0],"wallSeconds")>=0,
        "a stopped build lost its once-only direct preparation cost and retained-result summary");
    auto invalidOptions=stoppingOptions;invalidOptions.MaxMembers=1;invalidOptions.Stop={};
    preparationSummaries.clear();invalidOptions.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {if(event.Source=="preparation.combination-summary")preparationSummaries.push_back(event);};
    attempts=0;Require(Build(stopping,invalidOptions,attempts).empty()&&preparationSummaries.size()==1&&
        preparationSummaries[0].Reason=="unsupported-combination-query"&&
        measure(preparationSummaries[0],"pairRegionRequests")==0&&
        measure(preparationSummaries[0],"exactAttempts")==0&&
        measure(preparationSummaries[0],"retainedPatterns")==0,
        "early unsupported return omitted its one preparation summary or invented geometry work");
    std::cout<<"PASS positive-gap NFP combinations preserve originals, clearance, rotations, demand and budgets\n";
    return 0;
}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
