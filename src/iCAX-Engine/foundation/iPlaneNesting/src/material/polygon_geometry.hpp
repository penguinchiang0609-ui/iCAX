#pragma once
#include "core.hpp"
#include <string>

namespace icax::iplane::geometry {
struct Board {
    study::Polygon outer;
    std::vector<study::Polygon> holes;
    double margin = 0;
    // Empty: uniform margin on outer and hole boundaries. Otherwise each hole
    // has its own offset, independently from the outer border margin.
    std::vector<double> holeMargins;
};
struct RunResult {
    study::Output output;
    bool stopped = false;
    std::string warning;
    // Native evidence only; these do not add user-facing controls.
    size_t searchPasses = 0, bestPass = 0, pairCacheHits = 0, pairCacheMisses = 0;
    size_t continuousEvaluations = 0, continuousRefinements = 0;
    size_t continuousIntervalMinimizations = 0, continuousSubdivisions = 0;
};
struct Validation {
    bool valid = true;
    double maximumOverlapArea = 0, outsideArea = 0;
    double minimumPartGap = std::numeric_limits<double>::infinity();
    double minimumBoardMargin = std::numeric_limits<double>::infinity();
};
double materialArea(const study::Polygon &, const std::vector<study::Polygon> &);
void validatePrecision(const study::Polygon &, const std::vector<study::Polygon> &,
                       study::Budget * = nullptr);
void validateMaterial(const study::Polygon &, const std::vector<study::Polygon> &,
                      study::Budget * = nullptr);
// Incrementally add one hole to immutable material whose outer and existing
// holes have already passed validateMaterial. Rechecks the new ring and its
// relations plus total precision/net area; it does not revalidate old topology.
// Inputs are not modified. This is not a validator for untrusted old material.
void validateAdditionalHole(const study::Polygon &outer,
                            const std::vector<study::Polygon> &alreadyValidatedHoles,
                            const study::Polygon &newHole, study::Budget * = nullptr);
RunResult nest(const study::Input &, const Board &, study::Budget &);
// PartInput.poses retains source intervals: angle <= actual <= upper, at most
// one turn. Cross-zero ranges use an unwrapped interval, e.g.350..370.
// Placement.poseIndex identifies that interval; solvedAngle is the actual pose.
Validation validate(const study::Input &, const Board &, const study::Output &,
                    study::Budget * = nullptr);
} // namespace icax::iplane::geometry
