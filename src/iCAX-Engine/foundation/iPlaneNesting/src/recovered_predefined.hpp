#pragma once
#include "recovered_placement.hpp"

namespace study::recovered {
enum class PredefinedCountAction { Invalid, Proceed, ReturnExisting };
bool preNestCountValid(int32_t);
PredefinedCountAction nestMoreCountAction(int32_t);
bool predefinedOrientationValid(SdkOrientation); // 527240
bool predefinedMovable(uint32_t glueMode, bool moveDefault); // 5d15b4..5d15cf
struct PredefinedSdkRecord {
    uintptr_t partIdentity = 0;
    Point position{};
    SdkOrientation orientation;
    uint32_t glueMode = 2;
};
struct PredefinedConstraintRecord {
    uintptr_t partIdentity = 0;
    int64_t x = 0;
    int32_t y = 0;
    PlacementOrientation orientation;
    bool movable = false;
};
PredefinedConstraintRecord predefinedConstraints(const PredefinedSdkRecord &, Point sheetOrigin,
                                                bool moveDefault);
double predefinedRound(double); // c8a5d0, round-half-away with original bits
int32_t predefinedMinimumDifficulty(double xWeight = 2, double yWeight = .5); // 3f2990
struct PredefinedDifficulty {
    bool disabled = false;
    std::array<int32_t,3> cached{};
    std::array<std::vector<int32_t>,3> dynamic;
};
int32_t predefinedDifficulty(const PredefinedDifficulty &, std::array<double,3> weights = {2,.5,2},
                             bool includeOrientation = false); // 420f90
struct PredefinedVariant {uintptr_t identity = 0; int64_t leftE0 = 0;};
struct PredefinedNesterOptions {
    int32_t minimumDifficulty = 125;
    bool checkConstraints4e0 = false, flag68 = false;
    int64_t pitch17f0 = 1000000, origin17f8 = 0;
};
bool predefinedUnaligned(int64_t x, int64_t leftE0, int64_t pitch, int64_t origin);
class PredefinedNesterHost {
public:
    virtual ~PredefinedNesterHost() = default;
    virtual std::vector<uintptr_t> instances(uintptr_t part) = 0;
    virtual int32_t difficulty(uintptr_t instance) = 0;
    virtual bool placed(uintptr_t instance) = 0;
    virtual std::vector<PredefinedVariant> variants(uintptr_t instance) = 0;
    virtual std::optional<int64_t> solveX(uintptr_t instance, const PredefinedVariant &, bool, bool) = 0;
    virtual std::optional<int32_t> solveY(uintptr_t instance, const PredefinedVariant &, bool, bool) = 0;
    virtual void commit(uintptr_t instance, const PredefinedVariant &, int64_t x, int32_t y, bool unaligned) = 0;
};
// The original 3f2640 controller performs no material collision or sheet-bound
// check. Real X/Y constraint solvers and instance/variant catalog are explicit
// host dependencies. All qualifying instances are reconsidered until no commit.
uint32_t runPredefinedNester(PredefinedNesterHost &, std::span<const uintptr_t> orderedParts,
                             const PredefinedNesterOptions & = {});
}
