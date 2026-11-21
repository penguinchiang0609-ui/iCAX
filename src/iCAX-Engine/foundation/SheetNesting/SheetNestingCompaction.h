#pragma once
#include "SheetNestingPipeline.h"

namespace iCAX::SheetNesting
{
    class NfpCache;
    enum class CompactionAxis { X, Y };
    struct NfpCompactionSettings
    {
        CompactionAxis Axis = CompactionAxis::X;
        std::size_t MaxPasses = 4;
        std::size_t MaxDirections = 16;
        std::size_t MaxGroupMembers = 4;
        std::size_t MaxCandidatesPerRay = 96;
        // Reserve one third of this stage's budget for inserting remaining
        // originals through the existing bounded placement-domain repair.
        bool RefillMissing = false;
    };
    // Optional fixed-angle translation refinement. Every group consists of
    // original placements; no bounding carrier replaces its material.
    // The pipeline still validates and compares the returned candidate.
    PostProcessStage MakeNfpCompactionStage(NfpCompactionSettings settings = {},
        std::shared_ptr<NfpCache> geometryCache = {});
}
