#pragma once

#include "MachiningEdgeAnalysisWork.h"

namespace iCAX::ExtrusionRecognition
{
    enum class EMachiningPathAxisMode : std::uint8_t
    {
        None,
        // T x N defines the plane's axis line. Its sign is selected toward
        // the trimmed machining face, independently of the boundary walk.
        PlaneTangentCrossNormal,
        StraightGenerator
    };

    enum class EMachiningPathStatus : std::uint8_t
    {
        NotSelected,
        Ready,
        ParallelToGenerator,
        PartiallyParallelToGenerator,
        UnsupportedSurface,
        InvalidGeometry
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningPathSample final
    {
        // Endpoint evaluation only. No intermediate edge parameters are
        // analyzed by this Work.
        double Parameter = 0.0;
        std::uint64_t SourceEdgeId = 0;
        std::string SourceSpanKey;
        iCAX::GeometryData::Point3 Position;
        bool bPositionValid = false;
        // Original relative-wire boundary tangent, not a machine's chosen
        // cutting traversal. (dU x dV) x Tangent points into the trimmed domain;
        // material-oriented FaceNormal may have the opposite support sense.
        iCAX::GeometryData::Vector3 Tangent;
        bool bTangentValid = false;
        iCAX::GeometryData::Vector3 FaceNormal;
        // Original parametric generator direction before selecting its sign.
        iCAX::GeometryData::Vector3 Generator;
        // Axis oriented from this boundary into its own machining face.
        iCAX::GeometryData::Vector3 ToolAxis;
        bool bToolAxisValid = false;
        EMachiningPathStatus Status = EMachiningPathStatus::NotSelected;
        std::string Reason;
    };

    // Ordered, unchanged native support/range references forming a logical
    // curve. The skin contact flags follow this face's traversal direction.
    struct _EXTRUSION_RECOGNITION_EXP SMachiningPathSourceUse final
    {
        std::uint64_t EdgeId = 0;
        std::string SpanKey;
        std::size_t UseIndex = 0;
        double FirstParameter = 0.0;
        double LastParameter = 0.0;
        bool bReversed = false;
        ESkinRelation SkinRelation = ESkinRelation::OffSkin;
        EEdgeConvexity Convexity = EEdgeConvexity::Free;
        bool bSkinRelationValid = false;
        bool bConvexityValid = false;
        bool bStartOnSkin = false;
        bool bEndOnSkin = false;
        bool bSourceSkinMembership = false;
        std::vector<std::uint64_t> SourceSkinFaceIds;
        double MinSkinDistance = 0.0;
        double MaxSkinDistance = 0.0;
        std::string SkinReason;
    };

    // One face's geometric continuity group, split only where the actual
    // whole-skin path eligibility changes. Per-use convexity remains a boundary
    // fact and must not fragment a continuous non-skin group. Topological seams
    // do not create path records or extra axes. Shared curves stay face-local.
    struct _EXTRUSION_RECOGNITION_EXP SMachiningPathEdgeAnalysis final
    {
        std::string Key;
        std::uint64_t FaceId = 0;
        std::uint64_t WireId = 0;
        std::size_t GroupId = 0;
        std::size_t PartIndex = 1;
        bool bClosed = false;
        std::string CurveKind;
        std::vector<std::uint64_t> SourceEdgeIds;
        std::vector<std::string> SourceSpanKeys;
        std::vector<SMachiningPathSourceUse> SourceUses;
        bool bSelectedPath = false;
        bool bOnSkinPath = false;
        // Whole-group convexity fact, retained independently of path selection.
        bool bConvexPath = false;
        // At least one known non-skin SourceUse is convex. This is a coverage
        // witness, including a convex side boundary, never another walk source.
        bool bHasConvexEvidence = false;
        // Only an open logical segment can be a side. A closed trace's native
        // parameter seam is not a pair of physical skin-contact endpoints.
        bool bSide = false;
        ESkinRelation SkinRelation = ESkinRelation::OffSkin;
        EEdgeConvexity Convexity = EEdgeConvexity::Free;
        bool bSkinRelationValid = false;
        bool bConvexityValid = false;
        bool bMixedSourceSkinRelations = false;
        bool bMixedSourceConvexities = false;
        EMachiningPathAxisMode AxisMode = EMachiningPathAxisMode::None;
        EMachiningPathStatus Status = EMachiningPathStatus::NotSelected;
        std::string SurfaceKind;
        std::string Reason;
        // Exactly two endpoints in face-coedge traversal order: start, end.
        // Non-skin records have position/tangent facts only, with no tool axis.
        // Endpoint statuses make no claim about intermediate edge parameters.
        std::vector<SMachiningPathSample> Samples;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningPathAnalysisOptions final
    {
        double dAngularToleranceRadians = 0.001;
        double dSupportTolerance = 1.e-7;
        std::function<void(const std::string&)> Progress;
    };

    // GroupFace's fallback groups on an unordered wire are not geometric
    // logical edges. Retain their complete source identity for review instead
    // of manufacturing path endpoints at every fallback topological edge.
    struct _EXTRUSION_RECOGNITION_EXP SMachiningPathUnresolvedBoundary final
    {
        std::uint64_t FaceId = 0;
        std::uint64_t WireId = 0;
        std::vector<std::uint64_t> SourceEdgeIds;
        std::string Reason;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningPathAnalysisResult final
    {
        bool bSuccess = false;
        // Every source face-edge occurrence was processed. Deliberately
        // pending parallel/unsupported axes remain explicit successful records.
        bool bComplete = false;
        std::vector<SMachiningPathEdgeAnalysis> Edges;
        std::vector<SMachiningPathUnresolvedBoundary> UnresolvedBoundaries;
        std::vector<std::string> Diagnostics;
    };

    class _EXTRUSION_RECOGNITION_EXP MachiningPathAnalysisWork final
    {
    public:
        SMachiningPathAnalysisResult Run(
            const SSectionFaceClassificationResult& classification,
            const SSectionWiresResult& section,
            const SMachiningEdgeAnalysisResult& edgeAnalysis,
            const SMachiningPathAnalysisOptions& options = {}) const;
    };
}
