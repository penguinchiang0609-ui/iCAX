#pragma once
#include "recovered_placement.hpp"

namespace study::recovered {
// 3736b0: radians, cosine, sine, reflection sign, reference X/Y.
std::array<double, 6> reverseOrientationMatrix(PlacementOrientation, Point reference = {});
// 387640. Bounds use x0,y0,x1,y1 in the untransformed geometry coordinates.
Point reverseVariantOffset(std::array<double, 4> bounds, PlacementOrientation requested,
                            PlacementOrientation actual);
struct ReverseCoordinates {
    PlacementOrientation requested;
    int64_t x = 0;
    int32_t y = 0;
    bool unaligned = false;
};
// Numerical part of 3e6b50 after actual destination variant acquisition.
ReverseCoordinates reversePlacementCoordinates(PlacementOrientation source, int64_t sourceX,
    int32_t sourceY, int64_t pivot, Point variantOffset, int64_t variantLeft,
    int64_t pitch, int64_t originX);
struct ReversePlacement {
    Placement placement;
    PlacementOrientation orientation;
    int64_t x = 0;
    int32_t y = 0;
    uintptr_t variant = 0, instance = 0;
    bool unaligned = false;
    std::map<std::string, std::string> attributes;
};
struct ReverseNesting {
    Random random{0};
    uint32_t seed = 0;
    int32_t trial = 0, count = 0;
    int64_t bound = 0, maximumRight = 0;
    double leftBorder = 0, rightBorder = 0, shake = 0;
    std::vector<ReversePlacement> placements;
    std::vector<WarmLineage> lineage;
    double fraction = 0;
    std::array<uint32_t, 5> lineageFields{};
};
class ReverseHost {
public:
    virtual ~ReverseHost() = default;
    virtual ReverseNesting &side(bool reversed) = 0;
    virtual double configuredRandomSeed() = 0;
    virtual void reset(bool reversed) = 0; // Nesting virtual slot1, mode=1
    virtual bool childExists() = 0;
    virtual void createChild() = 0; // reverse problem's actual manager factory
    virtual void runChild() = 0; // 3ee340, including stop ownership
    virtual ReversePlacement clone(const ReversePlacement &, bool toReversed, int64_t pivot) = 0;
    virtual void commit(bool reversed, const ReversePlacement &) = 0;
};
// 3e6e80. Transfers only the difference in counts from the larger layout's tail.
// Properties are copied to the actual last committed placement, then shake state.
int32_t reverseTransfer(ReverseHost &, int64_t pivot);
// 40b210. Original/reverse RNG resets, lazy child, two transfers and lineage restore.
int32_t runReverse(ReverseHost &);
}
