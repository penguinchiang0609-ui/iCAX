#pragma once

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iCAX::ExtrusionRecognition
{
    struct SSectionRegionPartitionResult;
}

namespace iCAX::ExtrusionRecognition::section_partition_detail
{
    using Point = std::array<double, 2>;

    struct Curve
    {
        int Id = 0;
        bool IsArc = false;
        Point Start{}, End{}, Center{};
        double Radius = 0, StartAngle = 0, Sweep = 0;
        // A straight original trim belonging to a certified C0 faceted turn.
        bool IsFacetedTransition = false;
    };

    struct Ring
    {
        bool IsInner = false;
        std::vector<Curve> Curves;
    };

    struct Wall
    {
        std::string Label;
        std::vector<int> GuideIds, SupportIds;
        // Empty means the whole section is one continuous wall.
        std::vector<Point> Polygon;
    };

    struct Junction
    {
        std::string Label;
        std::vector<int> WallIds;
        std::optional<int> SourceCurveId;
    };

    struct Patch
    {
        int CurveId = 0;
        std::vector<int> AdjacentWallIds;
        std::size_t WallIndex = 0;
    };

    struct Plan
    {
        std::vector<Wall> Walls;
        std::vector<Junction> Junctions;
        std::vector<Patch> Patches;
        std::optional<std::size_t> RemainderWallIndex;
        // Finite normal cuts with each cell identified by its guide boundary.
        std::vector<std::array<Point, 2>> Crosscuts;
    };

    struct NativeCell
    {
        TopoDS_Face Face;
        std::vector<int> WallIds;
        double Area = 0;
        std::optional<std::size_t> JunctionIndex;
    };

    struct NativeContourEdge
    {
        TopoDS_Edge Edge;
        std::size_t WireIndex = 0;
        std::size_t EdgeIndex = 0;
        std::uint64_t SourceEdgeId = 0;
        std::uint64_t SourceFaceId = 0;
        double FirstParameter = 0;
        double LastParameter = 0;
    };

    std::optional<Plan> PlanNormalSweep(const std::vector<Ring>& rings, double tolerance);
    std::optional<std::vector<std::array<Point, 2>>> PlanNormalBoundaryCuts(
        const std::vector<Ring>& rings, double tolerance);
    std::optional<Plan> PlanMultipleInnerRings(
        const TopoDS_Face& material, const std::vector<Ring>& rings,
        const std::vector<TopoDS_Shape>& originalCurves, double tolerance);
    std::vector<NativeCell> BuildMaterialCells(
        const TopoDS_Face& material, const std::vector<TopoDS_Shape>& originalCurves,
        const Plan& plan, double tolerance);
    void BuildBoundaryProvenance(SSectionRegionPartitionResult& result,
        const std::vector<NativeContourEdge>& contourEdges, double tolerance);
}
