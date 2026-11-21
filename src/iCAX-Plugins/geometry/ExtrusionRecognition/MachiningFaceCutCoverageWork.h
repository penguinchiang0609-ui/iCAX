#pragma once

#include "MachiningPathAnalysisWork.h"

namespace iCAX::ExtrusionRecognition
{
    // Coverage is classified on the real skin trace's native source domain.
    // Boundary evidence is never another machining pass.
    enum class EFaceCutCoverageKind : std::uint8_t
    {
        Partial,
        Through,
        // This interval has no complete first-boundary proof yet.
        Pending
    };

    enum class EFaceCutEvidenceKind : std::uint8_t
    {
        SkinExit,
        // A bounded valid planar material domain has only stock-skin
        // boundaries. Its inward regular source rays must exit that set;
        // this proves Through without inventing a point correspondence.
        SkinExitBoundarySet,
        ConvexBoundary,
        SideCompletion,
        ConcaveTermination,
        OtherBoundary
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningFaceCutSource final
    {
        std::uint64_t FaceId = 0;
        std::string SourcePathKey;
        std::vector<std::uint64_t> SourceEdgeIds;
        bool bComplete = false;
        bool bExitCorrespondenceExpanded = false;
        std::string Method;
        std::string Reason;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningFaceCutArrival final
    {
        std::uint64_t FaceId = 0;
        std::string SourcePathKey;
        std::uint64_t SourceEdgeId = 0;
        // Directed source parameters paired with increasing target parameters;
        // these may descend. No interpolated intermediate tool axes are stored.
        double SourceFirstParameter = 0.0;
        double SourceLastParameter = 0.0;
        std::uint64_t TargetEdgeId = 0;
        double TargetFirstParameter = 0.0;
        double TargetLastParameter = 0.0;
        std::string TargetSpanKey;
        ESkinRelation TargetSkinRelation = ESkinRelation::OffSkin;
        EEdgeConvexity TargetConvexity = EEdgeConvexity::Free;
        bool bTargetSkinRelationValid = false;
        bool bTargetConvexityValid = false;
        std::string Method;
        std::string Reason;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningFaceCutCoverageSpan final
    {
        std::uint64_t FaceId = 0;
        std::string SourcePathKey;
        std::uint64_t SourceEdgeId = 0;
        // The covered skin edge. Equals SourceEdgeId; retained as the
        // projection destination name for exporters and presentation.
        std::uint64_t TargetEdgeId = 0;
        // A finite, increasing interval in this unchanged Topo edge's native
        // parameter domain. A periodic seam never makes a whole loop zero.
        double FirstParameter = 0.0;
        double LastParameter = 0.0;
        std::vector<std::string> SourcePathKeys;
        std::vector<std::string> BaselineSourcePathKeys;
        std::vector<std::size_t> ArrivalIndices;
        std::vector<std::size_t> EvidenceIndices;
        EFaceCutCoverageKind Kind = EFaceCutCoverageKind::Pending;
        bool bMappingComplete = false;
        bool bFaceMappingComplete = false;
        bool bExitCorrespondenceExpanded = false;
        std::string Reason;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningFaceCutBoundaryWitness final
    {
        std::uint64_t EdgeId = 0;
        std::string SpanKey;
        double FirstParameter = 0.0;
        double LastParameter = 0.0;
        std::vector<std::uint64_t> SourceSkinFaceIds;
        std::string SkinReason;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningFaceCutEvidence final
    {
        std::uint64_t FaceId = 0;
        std::string SourcePathKey;
        std::uint64_t SourceEdgeId = 0;
        double SourceFirstParameter = 0.0;
        double SourceLastParameter = 0.0;
        std::uint64_t WitnessEdgeId = 0;
        std::string WitnessLogicEdgeId;
        std::string WitnessSpanKey;
        double WitnessFirstParameter = 0.0;
        double WitnessLastParameter = 0.0;
        std::size_t ArrivalIndex = 0;
        bool bHasArrival = true;
        bool bExitCorrespondenceExpanded = true;
        std::vector<SMachiningFaceCutBoundaryWitness> BoundaryWitnesses;
        EFaceCutEvidenceKind Kind = EFaceCutEvidenceKind::OtherBoundary;
        bool bThrough = false;
        bool bComplete = false;
        std::string Reason;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningFaceCutCoverageOptions final
    {
        double dPositionTolerance = 1.e-7;
        double dAngularToleranceRadians = 0.001;
        std::function<void(const std::string&)> Progress;
    };

    // A reference to an already proved arrival on a logical side of this same
    // face. This records correspondence only and never adds a source or pass.
    struct _EXTRUSION_RECOGNITION_EXP SMachiningFaceCutSideCorrespondence final
    {
        std::size_t ArrivalIndex = 0;
        std::string SidePathKey;
        std::string SideSpanKey;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningFaceCutCoverageResult final
    {
        bool bSuccess = false;
        bool bComplete = false;
        std::vector<SMachiningFaceCutSource> Sources;
        std::vector<SMachiningFaceCutArrival> Arrivals;
        std::vector<SMachiningFaceCutEvidence> Evidence;
        std::vector<SMachiningFaceCutCoverageSpan> Coverage;
        std::vector<SMachiningFaceCutSideCorrespondence> SideCorrespondences;
        std::vector<std::uint64_t> IncompleteFaceIds;
        std::vector<std::string> Diagnostics;
    };

    class _EXTRUSION_RECOGNITION_EXP MachiningFaceCutCoverageWork final
    {
    public:
        // Retains only real OnSkin sources and their per-face identities.
        // Maps into the first actual boundary reached inside each finite face;
        // a neighboring slanted edge is a valid destination. Existing logical
        // sides may prove end completion only at a shared open skin-trace end,
        // certified by a TopoVertex or the exact same edge/native event.
        // No global material-ray search or additional side path is performed.
        SMachiningFaceCutCoverageResult Run(
            const SSectionFaceClassificationResult& classification,
            const SMachiningEdgeAnalysisResult& edgeAnalysis,
            const SMachiningPathAnalysisResult& pathAnalysis,
            const SMachiningFaceCutCoverageOptions& options = {}) const;
    };
}
