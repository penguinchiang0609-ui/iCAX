#pragma once
#include "NestingInternal.h"
#include "SheetNestingNfp.h"

namespace iCAX::SheetNesting::Detail
{
    struct NfpCarrierAssessment
    {
        ShapeKind Kind=ShapeKind::Irregular;
        double Area=0,Fill=0;
        bool Filled=false;
        std::vector<Point> Boundary;
    };
    // A full basic carrier is certified by NormalizePattern's union of the
    // original material. Other groups retain their AABB density and geometry.
    NfpCarrierAssessment AssessNfpCombinationCarrier(const Problem&,const Pattern&);
    struct NfpCombinationOptions
    {
        std::size_t MaxMembers=4,MaxResults=64,MaxStates=96,MaxAttempts=12000;
        // Every assembly uses net original material / its stored-pose AABB.
        // A verified filled carrier remains a geometry classification; it
        // does not turn unused corners of that AABB into material. A certified
        // useful two-member interlock may remain below MinimumFinalFill;
        // this admission rule is independent of the members' inventory IDs.
        double MinimumIntermediateFill=.55,MinimumFinalFill=.85;
        bool RecognizeFilledBasicCarriers=true;
        // Production preparation combines copies of one original only. The
        // explicit opt-out retains the low-level mixed-geometry capability;
        // no production constructor needs to pay for cross-original search.
        bool SamePartOnly=true;
        // Reserve a bounded construction pass for repeatable self carriers.
        // The internal pass samples equivalent relative directions; the full
        // search keeps legal poses of eligible partners under SamePartOnly.
        bool PrepareSelfPairs=true,SelfPairsOnly=false;
        // False consumes compact self pairs and four-member pair-repeat cells;
        // it does not construct arbitrary three-member self assemblies. True
        // retains the bounded 2..4-member beam under the same partner policy.
        bool ExpandAssemblies=true;
        // Limits construction proposals without changing any original's hard
        // reflection permission or its final original-material validation.
        OrientationProposalPolicy OrientationProposals=OrientationProposalPolicy::AllowedReflections;
        // Optional caller-owned remaining time in the existing preparation
        // window. Self/general coverage share that same window; no new time
        // allowance is created. Omitted providers retain Stop-only budgeting.
        std::function<double()> RemainingPreparationSeconds;
        // Stops new search and geometry work. Already certified immutable
        // candidates may still be returned; committing a board remains the
        // caller's responsibility, including its cancellation check.
        std::function<bool()> Stop;
        std::function<void(const ConstructionTraceEvent&)> ConstructionTrace;
        std::string ConstructionTraceRoute,ConstructionTraceScope;
        std::shared_ptr<ConstructionTraceHealth> ConstructionObservation;
    };
    // Returns real original members and poses, never a merged substitute.
    // Search is bounded and a missing pattern proves nothing about feasibility.
    std::vector<Pattern> BuildNfpCombinationPatterns(const Problem& problem,NfpCache& cache,
        const std::vector<NfpPreparedShape>& prepared,const NfpCombinationOptions& options,
        std::size_t& attempts);
}
