#pragma once
#include "SheetNesting.h"
#include <memory>

namespace iCAX::SheetNesting
{
    struct NfpCacheSettings
    {
        std::size_t MaxShapeEntries = 256;
        std::size_t MaxRegionEntries = 512;
        // Retained entry payload/capacity budget, including conservative entry
        // and shared-object bookkeeping. Fixed cache/hash-table metadata,
        // in-flight work and caller-owned result handles are outside it.
        std::size_t MaxBytes = 16 * 1024 * 1024;
    };
    struct NfpBuildOptions
    {
        double TimeLimitSeconds = 1;
        std::size_t MaxConvexPairs = 16384;
        std::function<bool()> Cancel;
    };
    struct NfpRegion
    {
        // Clipper winding: positive outer loops, negative holes. The region
        // describes forbidden relative translations before Lookup.Transform.
        std::vector<std::vector<Point>> Loops;
        std::size_t ConvexPairs = 0;
    };
    enum class NfpStatus { Ready, Unsupported, Cancelled, LimitReached };
    struct NfpConvexDecomposition
    {
        // Counterclockwise convex pieces with disjoint interiors. Their union
        // is the original polygon material; no replacement convex hull is used.
        std::vector<std::vector<Point>> Pieces;
    };
    struct NfpDecompositionLookup
    {
        NfpStatus State = NfpStatus::Unsupported;
        std::shared_ptr<const NfpConvexDecomposition> Decomposition;
        // Pieces are canonical coordinates: source point = piece point +
        // SourceOrigin. For a source placement, rotate that sum by its angle,
        // then add its placement translation. This is NOT a rotated AABB origin.
        Point SourceOrigin;
        bool CacheHit = false;
        std::string Diagnostic;
    };
    struct NfpLookup
    {
        NfpStatus State = NfpStatus::Unsupported;
        std::shared_ptr<const NfpRegion> Region;
        // Apply this pose to each region point, then add the stationary part's
        // placement translation to obtain moving-part translation candidates.
        Pose Transform;
        bool CacheHit = false;
        std::string Diagnostic;
    };
    class NfpPreparedShape
    {
    public:
        NfpStatus State = NfpStatus::Unsupported;
        std::string Diagnostic;
        explicit operator bool() const { return State==NfpStatus::Ready&&Data!=nullptr; }
    private:
        struct Impl;
        std::shared_ptr<const Impl> Data;
        friend class NfpCache;
    };
    struct NfpCacheStatistics
    {
        std::size_t ShapeHits = 0, ShapeMisses = 0, RegionHits = 0, RegionMisses = 0;
        std::size_t RotatedHits = 0, RotatedMisses = 0;
        std::size_t ReverseHits = 0;
        std::size_t RegionBuilds = 0, CoalescedWaits = 0;
        std::size_t Evictions = 0, ResidentBytes = 0, ShapeEntries = 0, RegionEntries = 0;
    };
    // Bounded, thread-safe geometry cache only: no demand, stock, placement or
    // mutable job state. Exact geometry content is compared after hashing.
    // First version: simple polygon exteriors, zero part gap; holes and circles
    // are explicitly unsupported. Final placement safety still uses Validate.
    class NfpCache
    {
    public:
        explicit NfpCache(NfpCacheSettings settings = {});
        ~NfpCache();
        NfpCache(const NfpCache&) = delete;
        NfpCache& operator=(const NfpCache&) = delete;
        NfpLookup Get(const Shape& stationary, double stationaryAngle,
                      const Shape& moving, double movingAngle,
                      const NfpBuildOptions& options = {},bool stationaryMirrored=false,bool movingMirrored=false);
        // Prepare once per immutable source shape. Repeated angle-pair queries
        // then skip ring normalization, sorting and source validation.
        // Reflection creates a private local-Y geometry copy and a distinct
        // cache identity. Part permissions remain the domain caller's duty.
        NfpPreparedShape Prepare(const Shape& shape, const NfpBuildOptions& options = {},bool mirrored=false);
        NfpLookup Get(const NfpPreparedShape& stationary, double stationaryAngle,
                      const NfpPreparedShape& moving, double movingAngle,
                      const NfpBuildOptions& options = {});
        // Reuse the same exact convex decomposition used by the NFP kernel.
        // Handles stay immutable and valid after cache eviction or Clear().
        // MaxConvexPairs limits pair NFP construction, not this single shape.
        NfpDecompositionLookup Decompose(const Shape& shape, const NfpBuildOptions& options = {});
        NfpDecompositionLookup Decompose(const NfpPreparedShape& shape, const NfpBuildOptions& options = {});
        NfpCacheStatistics Statistics() const;
        void Clear();
    private:
        struct Impl;
        std::unique_ptr<Impl> Data;
    };
}
