#pragma once
#include "core.hpp"

namespace study {
enum class PredefinedContinuationFailure {
    None, InvalidOriginalInstance, InvalidCandidateInstance, DuplicateOriginalInstance,
    DuplicateCandidateInstance, MissingOriginalInstance, ChangedPartOutsideSheet,
    NewMaterialCollision, InvalidGeometry
};
struct PredefinedContinuationValidation {
    bool valid = false, complete = false;
    size_t preservedOriginal = 0, movedOriginal = 0, added = 0, preservedPairs = 0;
    PredefinedContinuationFailure failure = PredefinedContinuationFailure::None;
};
// Existing source defects in geometry are grandfathered only while their
// concrete instances, actual poses and world positions remain unchanged.
// Every new/moved part is checked against the original sheet. Every pair with
// a new/moved member is checked for material collision in expanded bounds.
// A partial but valid completion is distinct from fulfilling all demand.
PredefinedContinuationValidation validatePredefinedContinuation(
    const Input &, const Output &original, const Output &candidate);
}
