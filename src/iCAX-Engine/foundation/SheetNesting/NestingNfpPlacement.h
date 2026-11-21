#pragma once
#include "SheetNesting.h"
#include "SheetNestingNfp.h"
#include <array>
#include <memory>
#include <optional>
#include <set>

namespace iCAX::SheetNesting { class NfpCache; }
namespace iCAX::SheetNesting::AxisSafety { struct Geometry; }
namespace iCAX::SheetNesting::Detail
{
    struct Member;
    struct NfpPlacementLimits
    {
        std::size_t MaxCandidates = 128; // Zero retains all verified candidates.
        std::size_t MaxRawCandidates = 4096;
        std::size_t MaxContactSegments = 256;
        std::size_t MaxAttempts = 100000;
        std::size_t MaxFixedPlacements = 4096;
        // Optional raw-proposal translation domain, intersected with the
        // physical member/sheet domain before event generation and raw caps.
        // XYWH means absolute pose XY for Find, pure delta XY for rigid Find.
        // Zero width/height retains a line/point domain. Finite retreats keep
        // the existing hard gates; callers still certify final material bounds.
        // Unset preserves the original complete bounded proposal stream.
        std::optional<Rect> TranslationWindow;
        bool IncludeContactChannels = true;
        // Proposal scheduling only: false defers optional common-edge work.
        // Original AllowCommonEdges and final material checks stay unchanged.
        bool IncludeCommonEdgeProposals = true;
        // The queried moving orientation; fixed orientations are read from
        // each layout pose. The source part must explicitly allow reflection.
        bool Mirrored = false;
        // Feasibility lookahead: return after the first original-material-safe
        // fit, without validating the rest of the proposed translation events.
        bool StopAfterFirstFit = false;
        // Greedy construction may retain a small ordered pool rather than
        // validating every remaining raw event before applying the fit cap.
        // Zero MaxCandidates still means unlimited; false preserves the
        // complete bounded-query oracle and its final diverse fit selection.
        bool StopAfterCandidateLimit = false;
        // Use the bounded source-edge index for the strict-interior rejection
        // certificate during a complete query too. Candidate contact channels
        // and all final material gates are unchanged. First-fit ranked queries
        // already use this representation; false retains the explicit oracle.
        bool IndexedMaterialProof = false;
        // Optional displacement proposals. Fits remains the zero-blocker safe
        // set. Blocked records original-material conflicts by layout index;
        // the caller must remove every blocker before placing that proposal.
        std::size_t MaxBlockers = 0;
        std::size_t MaxBlockedCandidates = 256;
        // Optional stronger final-material clearance. The proposal domain and
        // source contours stay unchanged. Only zero-blocker queries support
        // this override; it must be finite and at least the problem's gap.
        // Unset preserves the established verification and retreat behavior.
        // A stronger gate's blocked/uncertain pose permits the same bounded
        // actual retreat probes; no separate zero-gap diagnosis is repeated.
        std::optional<double> VerificationGap;
        // Opt-in cheap adjacency proposals before NFP construction. Only
        // StopAfterFirstFit queries without blockers use this path.
        bool FastAdjacency = false;
        // Pure NFP construction can first score actual free-domain vertices.
        // Midpoint/contact-channel sampling is deferred; an empty verified set
        // must be retried with this flag false before treating this angle as empty.
        // Default callers retain the complete established proposal stream.
        bool BoundaryVerticesOnly = false;
        // Optional domain-derived first repair. Strict inside/outside probes
        // choose its side; the established retreat sequence remains fallback.
        bool PrioritizeFreeDomainRetreat = false;
        // Optional caller order for the complete bounded proposal stream.
        // Smaller finite keys lead; each key is evaluated once per raw pose.
        // This disables the adjacency-only early return. A representable
        // retreat still passes all guards but is not a global-priority proof.
        std::function<std::array<double,4>(Pose)> CandidatePriority;
        // Cheap caller score rejection before numerical geometry preparation.
        // Receives a final stored pose (single) or pure delta (rigid). False
        // rejects that event, retaining single-part finite retreat; exceptions
        // retain the complete hard checks. Passing never certifies material.
        // Keep geometric NecessaryFit screens after the cheap NFP rejection.
        // Score-only rejects have separate counters and do not consume the
        // material-attempt cap; raw-event/retreat, time and cancellation bounds
        // still apply. Passing/unproved poses keep the existing attempt charge.
        std::function<bool(Pose,std::uint64_t)> CompetitivePose;
        // Optional geometric reject-only condition after cheap domain checks
        // and before deep material verification.
        // Single-part queries pass the final stored pose; rigid queries pass
        // the final pure translation delta. False rejects only that event;
        // single-part queries retain finite retreat, true certifies nothing.
        // All original hard gates and AcceptFit must still approve every fit.
        // An unproved/throwing screen also continues to those hard gates.
        // The ID follows Trace/FitsIDs, or is zero without tracing.
        std::function<bool(Pose,std::uint64_t)> NecessaryFit;
        // Additional original-material gate after this service's own complete
        // safety checks. False permits the existing bounded retreat and later
        // proposals; it never publishes a fit or an invented blocker.
        // The ID matches Trace/FitsIDs, or is zero without tracing.
        std::function<bool(Pose,std::uint64_t)> AcceptFit;
        // Remaining wall time of this query, independent of the job's Cancel.
        // Clearance uses a basic ordinary window followed by bounded optional
        // common-edge work. An empty provider preserves legacy caller timing.
        std::function<double()> RemainingTimeSeconds;
        // Observation only. Candidate IDs are local to this Find query; the
        // caller supplies route/query identity when forwarding the event.
        std::function<void(const ConstructionTraceEvent&)> Trace;
        std::shared_ptr<ConstructionTraceHealth> TraceHealth;
        // Rigid queries keep every original member. Exceeding a geometry-work
        // bound reports truncation/unsupported, never geometric infeasibility.
        std::size_t MaxRigidMembers = 64;
        std::size_t MaxRigidPairQueries = 4096;
        std::size_t MaxRigidRegionVertices = 262144;
        std::size_t MaxRigidContactIntersections = 16384;
        // Retain each moving member's exact source-edge conversion within one
        // rigid query. False keeps the original per-obstacle oracle path.
        bool ReuseRigidMovingEdges = true;
        // Optional immutable preparation for a fixed original at this exact
        // stored pose. Providers must return the unchanged AxisSafety::Make
        // geometry for the bound original index and complete pose. A null or
        // throwing provider falls back to Make; all material checks still run.
        std::function<std::shared_ptr<const AxisSafety::Geometry>(std::size_t,Pose)> RigidFixedGeometry;
        // Optional immutable loops after the exact Transform(point,
        // lookup.Transform), before adding fixed or subtracting member poses.
        // Preserve every loop/vertex and its order. Providers must bind the
        // actual Region owner and complete Transform bits, never normalized
        // carrier translations or mutable query state. Null/throwing providers
        // retain the original conversion; logical pair/vertex budgets remain.
        std::function<std::shared_ptr<const std::vector<std::vector<Point>>>(const NfpLookup&)> RigidRegionLoops;
    };
    struct NfpBlockedPlacement
    {
        Pose Transform;
        std::vector<std::size_t> Blockers;
        double OverlapArea = 0; // Actual original-material intersection, not envelopes.
    };
    struct NfpPlacementQuery
    {
        std::vector<Pose> Fits;
        // Same order as Fits when tracing is enabled; otherwise may be empty.
        std::vector<std::uint64_t> FitsIDs;
        std::vector<NfpBlockedPlacement> Blocked;
        std::size_t Attempts = 0;
        std::size_t Generated = 0;
        // Heuristic translation-domain measure only. Regularized subtraction
        // drops valid touch-only channels, so area zero is not infeasibility.
        double FreeTranslationArea = 0;
        bool DomainMeasured = false;
        bool Truncated = false;
        bool Supported = false;
    };
    using RigidNfpRegionLookup = std::function<NfpLookup(std::size_t,Pose,std::size_t,Pose,bool)>;
    // Build the intersection of member sheet domains minus the union of all
    // fixed-original/member NFP domains. Fits are pure delta translations:
    // {local.X+delta.X, local.Y+delta.Y, local.Angle, local.Mirrored}.
    // Boundary/contact event enumeration is bounded; zero area or no returned
    // event is not a proof of infeasibility. No member is replaced by its AABB.
    NfpPlacementQuery FindRigidNfpPlacements(const Problem&,const Layout&,const std::vector<Member>&,
        const NfpPlacementLimits&,const RigidNfpRegionLookup&,const std::function<bool()>& stop={},
        const std::set<std::string>& zeroGapObstacles={});
    // Job-local immutable-input geometry preparation. Cache contents contain
    // pair geometry only: no board, inventory, or mutable search-node state.
    // Every returned pose is checked against the original polygon material.
    class NfpPlacementGenerator
    {
    public:
        explicit NfpPlacementGenerator(const Problem& problem,
            std::shared_ptr<NfpCache> cache = {}, const std::function<bool()>& stop = {});
        ~NfpPlacementGenerator();
        NfpPlacementGenerator(const NfpPlacementGenerator&) = delete;
        NfpPlacementGenerator& operator=(const NfpPlacementGenerator&) = delete;
        bool Ready() const;
        NfpPlacementQuery Find(const Layout& layout, std::size_t partIndex, double angle,
            const NfpPlacementLimits& limits = {}, const std::function<bool()>& stop = {});
    private:
        struct Impl;
        std::unique_ptr<Impl> Data;
    };
}
