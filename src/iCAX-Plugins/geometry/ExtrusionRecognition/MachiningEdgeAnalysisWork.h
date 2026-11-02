#pragma once

#include "ExtrusionRecognitionTypes.h"

namespace iCAX::ExtrusionRecognition
{
    // Independent facts: neither enum implies a machining strategy.
    enum class ESkinRelation : std::uint8_t
    {
        OnSkin,
        OffSkin,
        PartialOnSkin
    };

    enum class EEdgeConvexity : std::uint8_t
    {
        Concave,
        Convex,
        // Zero local dihedral along this span, including smooth curved joins.
        // This is not a certificate that whole faces can be merged as planes.
        Coplanar,
        Free
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningEdgeSample final
    {
        double Parameter = 0.0;
        iCAX::GeometryData::Point3 Position;
        double SkinDistance = 0.0;
        bool bOnSkin = false;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningEdgeAngleSample final
    {
        double Parameter = 0.0;
        // Positive is convex, negative is concave, in the first face's
        // oriented coedge frame. Changing the first face preserves the sign.
        double SignedAngleRadians = 0.0;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningEdgeAnalysis final
    {
        // Analysis spans reference an unchanged original edge parameter range.
        std::string Key;
        std::uint64_t EdgeId = 0;
        std::size_t SegmentId = 1;
        double FirstParameter = 0.0;
        double LastParameter = 0.0;
        std::vector<std::string> MatchedSpanKeys;
        // Original source owners, followed by resolved machining neighbors.
        std::vector<std::uint64_t> SourceMachiningFaceIds;
        std::vector<std::uint64_t> MachiningFaceIds;
        // Whole-edge sampled correspondence only; includes EdgeId itself.
        std::vector<std::uint64_t> MatchedEdgeIds;
        std::vector<std::uint64_t> PartialOverlapEdgeIds;
        std::vector<std::uint64_t> RejectedCoincidentEdgeIds;
        std::string AdjacencyMethod;
        std::string AdjacencyReason;
        double MaxAdjacencyGap = 0.0;
        bool bOrientationRepaired = false;
        std::uint64_t OrientationAnchorFaceId = 0;
        std::vector<std::uint64_t> OrientationFlippedFaceIds;
        std::string OrientationMethod;
        ESkinRelation SkinRelation = ESkinRelation::OffSkin;
        EEdgeConvexity Convexity = EEdgeConvexity::Free;
        // A failed or mixed classification has no enum value. Callers must
        // inspect these flags rather than render the initialized enum values.
        bool bSkinRelationValid = false;
        bool bConvexityValid = false;
        std::string SkinReason;
        std::string ConvexityReason;
        bool bStartOnSkin = false;
        bool bEndOnSkin = false;
        double MinSkinDistance = 0.0;
        double MaxSkinDistance = 0.0;
        bool bSourceSkinMembership = false;
        std::vector<std::uint64_t> SourceSkinFaceIds;
        double MinSignedAngleRadians = 0.0;
        double MaxSignedAngleRadians = 0.0;
        std::vector<SMachiningEdgeSample> Samples;
        std::vector<SMachiningEdgeAngleSample> AngleSamples;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningBoundaryUse final
    {
        std::uint64_t EdgeId = 0;
        bool bReversed = false;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningBoundaryGroup final
    {
        std::size_t GroupId = 0;
        std::string CurveKind;
        bool bClosed = false;
        // Geometry-continuous parent group. Its source edges retain their
        // own two enum classifications, including mixed-role parent groups.
        std::vector<SMachiningBoundaryUse> SourceUses;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningBoundaryWire final
    {
        std::uint64_t WireId = 0;
        bool bOrdered = true;
        std::string Reason;
        std::vector<SMachiningBoundaryGroup> Groups;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningFaceEdgeAnalysis final
    {
        std::uint64_t FaceId = 0;
        std::vector<SMachiningBoundaryWire> Wires;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningEdgeAnalysisOptions final
    {
        double dSkinDistanceTolerance = 0.001;
        double dConnectionTolerance = 0.001;
        double dAngularToleranceRadians = 0.001;
        std::size_t nCurveSampleCount = 49;
        std::size_t nAngleSampleCount = 9;
        std::function<void(const std::string&)> Progress;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningEdgeAnalysisResult final
    {
        bool bSuccess = false;
        bool bComplete = false;
        std::vector<SMachiningEdgeAnalysis> Edges;
        // Parametric seams and degenerate edges are source topology records,
        // not physical machining boundaries; they have no business enum pair.
        std::vector<SMachiningEdgeAnalysis> ExcludedEdges;
        std::vector<SMachiningFaceEdgeAnalysis> Faces;
        std::vector<std::string> Diagnostics;
    };

    class _EXTRUSION_RECOGNITION_EXP MachiningEdgeAnalysisWork final
    {
    public:
        // Reads the classified geometry without modifying or sewing it.
        // Convexity adjacency is restricted to machining faces. The finite
        // X-extruded YZ section supplies the independent skin relationship.
        SMachiningEdgeAnalysisResult Run(
            const SSectionFaceClassificationResult& classification,
            const SSectionWiresResult& section,
            const SMachiningEdgeAnalysisOptions& options = {}) const;
    };
}
