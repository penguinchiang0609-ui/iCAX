#pragma once

#include "ExtrusionRecognitionTypes.h"

namespace iCAX::ExtrusionRecognition
{
    enum class ESectionRegionPartitionStatus
    {
        NotRun,
        Complete,
        Unresolved,
        Unsupported,
        Failed
    };

    struct SSectionRegionCell final
    {
        std::uint64_t FaceId = 0;
        double Area = 0;
        std::vector<int> WallIds;
        int JunctionId = 0;
    };

    struct SSectionRegionWall final
    {
        int Id = 0;
        std::string Label;
        std::vector<std::uint64_t> SourceEdgeIds;
    };

    struct SSectionRegionJunction final
    {
        std::string Label;
        std::vector<int> WallIds;
    };

    struct SSectionRegionContourSpan final
    {
        // Indices in the input SSectionWiresResult, before contour fitting.
        std::size_t WireIndex = 0;
        std::size_t EdgeIndex = 0;
        std::uint64_t SourceEdgeId = 0;
        std::uint64_t SourceFaceId = 0;
        double FirstParameter = 0;
        double LastParameter = 0;
    };

    struct SSectionRegionBoundarySpan final
    {
        // Face and edge IDs refer to Geometry. Parameters are on that edge.
        std::uint64_t CellFaceId = 0;
        std::uint64_t EdgeId = 0;
        double FirstParameter = 0;
        double LastParameter = 0;
        // A material interface has an adjacent cell. A physical boundary has
        // original contour spans. Endpoint-only contacts are not interfaces.
        std::vector<std::uint64_t> AdjacentCellFaceIds;
        std::vector<SSectionRegionContourSpan> ContourSpans;
    };

    struct _EXTRUSION_RECOGNITION_EXP SSectionRegionPartitionResult final
    {
        ESectionRegionPartitionStatus Status = ESectionRegionPartitionStatus::NotRun;
        std::string Reason;
        // One compound of disjoint material cells on the aligned YZ plane.
        iCAX::GeometryData::BRepModel Geometry;
        std::vector<SSectionRegionCell> Cells;
        std::vector<SSectionRegionWall> Walls;
        std::vector<SSectionRegionJunction> Junctions;
        double SharedArea = 0;
        std::vector<SSectionRegionBoundarySpan> BoundarySpans;
        bool bBoundaryProvenanceComplete = false;
    };

    class _EXTRUSION_RECOGNITION_EXP SectionRegionPartitionWork final
    {
    public:
        static SSectionRegionPartitionResult Run(
            const SSectionWiresResult& section,
            const SSectionFaceClassificationResult& classification,
            double connectionTolerance = 1e-7);

    };
}
