#pragma once
#include "NestingInternal.h"
#include "SheetNestingNfp.h"

namespace iCAX::SheetNesting::Detail
{
enum class FeaturePolarity
{
    Recess,
    Protrusion
};
enum class FeatureCarrierKind
{
    ConvexHull,
    OuterRectangle,
    CoreRectangle
};

// All coordinates remain in the original part's local coordinate system.
// Into points from this carrier interface into the residual region.
struct FeatureMouth
{
    Point Start, End, Into;
    double Width = 0, Depth = 0;
};

struct EncodedShapeFeature
{
    FeaturePolarity Polarity = FeaturePolarity::Recess;
    // Signed material-region boundaries, with opposite hole winding.
    std::vector<std::vector<Point>> Rings;
    std::vector<FeatureMouth> Mouths;
    Rect Bounds;
    double Area = 0;
};

struct FeatureCarrier
{
    FeatureCarrierKind Kind = FeatureCarrierKind::ConvexHull;
    Shape Geometry;
    std::vector<EncodedShapeFeature> Features;
    double ReconstructionErrorArea = 0;
    bool ReconstructionVerified = false;
};

struct ShapeFeatureModel
{
    std::vector<FeatureCarrier> Carriers;
    bool Supported = false;
    bool Truncated = false;
    std::size_t RetainedBytes = 0;
    std::string Diagnostic;
};

// Existing constructors keep their positive-OBB candidate sequence. New
// placement consumers may retain the board-axis width/height tradeoffs too.
enum class ShapeFeatureMatchMode
{
    LegacyPositiveObb,
    ContactFrontier
};

struct ShapeFeatureLimits
{
    std::size_t MaxCarriers = 4;
    std::size_t MaxFeaturesPerCarrier = 32;
    std::size_t MaxPartnersPerFeature = 8;
    std::size_t MaxProposals = 256;
    // Absolute ceiling for the shared cumulative trial counter.
    std::size_t MaxAttempts = 200000;
    // Retained owned heap capacity, excluding the returned object itself.
    // Diagnostic heap storage is included. Temporary geometry has separate
    // vertex, feature, index and trial bounds.
    std::size_t MaxBytes = 16 * 1024 * 1024;
    ShapeFeatureMatchMode MatchMode = ShapeFeatureMatchMode::ContactFrontier;
};

struct FeatureMatch
{
    // Original members and legal absolute angles, ready for placement.
    Pattern Value;
    std::size_t FirstCarrier = 0, FirstFeature = 0;
    std::size_t SecondCarrier = 0, SecondFeature = 0;
    // Sum of solo minimum-OBB areas minus the combined minimum-OBB area.
    double FootprintSaving = 0;
    // Original material area divided by the combined minimum-OBB area.
    double CarrierFill = 0;
};

struct ShapeFeatureMatches
{
    std::vector<FeatureMatch> Matches;
    bool Truncated = false;
    std::size_t RetainedBytes = 0;
};

// Shared original-polygon footprint metric for singles, pairs and arrays.
// Empty means unsupported geometry, invalid members or cooperative stop.
std::optional<double> MinimumPatternObbArea(const Pattern &pattern, const Problem &problem,
                                            const std::function<bool()> &stop = {});

// Carrier differences encode proposals. The original Shape stays authoritative.
ShapeFeatureModel EncodeShapeFeatures(const Shape &shape, double tolerance, const ShapeFeatureLimits &limits = {},
                                      const std::function<bool()> &stop = {});

// A bounded feature index proposes complementary pairs. Every returned pair
// preserves original inventory, angles and original-material safety.
ShapeFeatureMatches BuildShapeFeatureMatches(const Problem &problem, const std::vector<ShapeFeatureModel> &models,
                                             const ShapeFeatureLimits &limits, std::size_t &attempts,
                                             const std::function<bool()> &stop = {},
                                             std::shared_ptr<NfpCache> sharedCache = {});
} // namespace iCAX::SheetNesting::Detail
