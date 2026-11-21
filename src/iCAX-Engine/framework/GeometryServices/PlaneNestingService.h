#pragma once
#include <Services/IService.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace iCAX::GeometryServices::PlaneNesting
{
    struct Point { double X = 0, Y = 0; };
    using Ring = std::vector<Point>;
    struct AngleInterval { double MinDeg = 0, MaxDeg = 0; };

    // Polygon calculation snapshots. Source CAD curves and approximation
    // provenance remain owned by the caller; this service never edits them.
    struct Part
    {
        std::string Id;
        Ring Outer;
        std::vector<Ring> Holes;
        Point Pivot;
        int Quantity = 1;
        // Union of the discrete angles and closed intervals; empty means 0 deg.
        std::vector<double> AnglesDeg;
        std::vector<AngleInterval> RotationIntervals;
        bool AllowMirror = false;
    };
    struct Stock
    {
        // One physical board per entry, even when several boards have the same
        // outline. The caller expands inventory quantities into unique IDs.
        std::string Id;
        Ring Outline;
        std::vector<Ring> Holes;
    };
    struct Request
    {
        std::string InputKey;
        std::vector<Part> Parts;
        std::vector<Stock> Stocks;
        double PartGap = 0;
        // Applied to both the stock outer boundary and hole boundaries.
        double StockMargin = 0;
        double TimeLimitSeconds = 1;
        std::uint32_t RandomSeed = 0;
    };
    enum class Status { Completed, Partial, Cancelled, BudgetExhausted, InvalidInput, Unsupported, ResourceLimit, Failed };
    struct Placement
    {
        std::string PhysicalStockId, PartId;
        // Zero-based concrete ordinal within the source Part.Quantity.
        std::size_t InstanceIndex = 0;
        // In the original stock coordinate frame:
        // p' = ReflectY(RotateCCW(p - Pivot, AngleDeg)) + (X,Y).
        double X = 0, Y = 0, AngleDeg = 0;
        bool Flip = false;
    };
    struct Unplaced
    {
        std::string PartId;
        std::size_t InstanceIndex = 0;
        std::string Reason;
    };
    struct BoardUsage
    {
        std::string PhysicalStockId;
        std::size_t PlacedCount = 0;
        double StockNetArea = 0, PlacedMaterialArea = 0, Utilization = 0;
    };
    struct Metrics
    {
        std::size_t RequiredCount = 0, PlacedCount = 0, UnplacedCount = 0, UsedStockCount = 0;
        double StockNetArea = 0, PlacedMaterialArea = 0, Utilization = 0, ElapsedMs = 0;
        std::uint64_t Trials = 0;
    };
    struct Validation
    {
        bool InputAccepted = false, GeometryValid = false;
        bool DemandConserved = false, StockConserved = false, OrientationsValid = false;
        // Covers the supplied polygon material, including holes and clearances;
        // it does not certify an unprovided original CAD curve.
        std::string Scope = "PolygonMaterial";
        double LinearTolerance = 1e-6;
        std::vector<std::string> Diagnostics;
    };
    struct Result
    {
        Status State = Status::Failed;
        std::string InputKey, StopReason, Diagnostic, ProviderVersion;
        bool Valid = false;
        std::vector<Placement> Placements;
        std::vector<Unplaced> UnplacedParts;
        std::vector<BoardUsage> Boards;
        Metrics Statistics;
        Validation Checks;
    };
}

namespace iCAX::GeometryServices
{
    // Application/product scoped IService; per-call data only. No Scene,
    // Repository, resources, SDO, inventory mutation or publication is involved.
    class IPlaneNestingService : public Services::IService
    {
    public:
        // Synchronous. Budget includes waiting for this adapter's SDK mutex.
        // Cancellation is checked while waiting and around the SDK call. The
        // current C ABI cannot interrupt an executing solve; cancelled output
        // is discarded once that call returns.
        virtual PlaneNesting::Result Solve(const PlaneNesting::Request& request,
            const std::function<bool()>& cancelled = {}) const = 0;
    };
}
