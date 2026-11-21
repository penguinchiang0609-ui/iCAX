#pragma once
#include "SheetNestingPipeline.h"
#include <cstdint>
#include <optional>

namespace iCAX::SheetNesting
{
    struct AxisCompactionSettings
    {
        bool XFirst = false; // Default: down, then left; every angle stays unchanged.
        std::size_t MaxPasses = 32;
        std::size_t MaxCandidatesPerMove = 4096;
        std::size_t MaxContactEventsPerMove = 16384;
    };
    enum class AxisCompactionStopReason
    {
        EventNeighborhoodClosed, PassLimit, CandidateLimit, EventLimit,
        TimeLimit, AttemptLimit, Cancelled, Unsupported, InvalidInput, NumericalUncertainty, Failed
    };
    struct AxisCompactionReport
    {
        bool Closed = false;
        bool Supported = true;
        bool GeometryValidated = false;
        AxisCompactionStopReason StopReason = AxisCompactionStopReason::Failed;
        double ClosureTolerance = 0;
        std::size_t CompletedPasses = 0;
        std::size_t Moves = 0, SingleMoves = 0, TaggedGroupMoves = 0, WholeSheetMoves = 0;
        std::size_t CommonEdgeGroupMoves = 0;
        std::size_t CandidateTests = 0, ContactEvents = 0;
        std::uint64_t OutputFingerprint = 0;
        std::uint64_t ProblemFingerprint = 0;
    };
    // Independent fixed-angle translation stage. It processes each original
    // sheet, its full placement set, current common-edge components, tagged
    // groups and individual originals. Component candidates preserve tags.
    // Polygon outer/hole boundaries and analytic circles generate bounded 1-D
    // contact/distance events. Original InsideSheet/ShapesConflict, including
    // AllowCommonEdges, decide every actual emitted floating-point pose.
    // Movement follows successive valid event/end-point and open-cell probes
    // from the current pose, stopping at the first invalid probe. This bounded
    // floating-point event neighborhood does not certify every continuous
    // native clipping/tolerance transition between its probes.
    // Closed means a complete no-improvement cycle in this fixed-angle axis
    // event neighborhood to ClosureTolerance, never global nesting optimality.
    // Time, attempts, candidate/events or pass limits never report Closed.
    // Candidate material uses nominal spacing without spending validation
    // tolerance. Numerically unproved contacts retain a safe incumbent and
    // report NumericalUncertainty instead of a closure claim.
    PostProcessStage MakeAxisCompactionStage(AxisCompactionSettings settings = {});

    // Returns only a report bound to these original identities/stock/poses.
    // Logical board/placement reordering preserves the binding. Copying a
    // rejected candidate's report onto different retained poses invalidates it.
    std::optional<AxisCompactionReport> GetAxisCompactionReport(const Result& result);
    // This overload also binds original contours, holes/circles, quantities,
    // stock dimensions/materials, finite angle domains and spacing semantics.
    // Controllers use this overload before reusing any closure information.
    std::optional<AxisCompactionReport> GetAxisCompactionReport(const Problem& problem, const Result& result);
}
