#pragma once
#include "OpenCascadeResourceImport/GeometryFeatureAnalysisService.h"
#include "Resources/ResourceInfo.h"
#include <optional>

#ifdef _CUT_RECOGNITION
#define CUT_RECOGNITION_API __declspec(dllexport)
#else
#define CUT_RECOGNITION_API __declspec(dllimport)
#endif

namespace iCAX::CAM::Recognition
{
    struct SFaceUse {std::uint64_t FaceID=0;std::vector<std::uint64_t> UsePath;};
    CUT_RECOGNITION_API std::vector<SFaceUse> EnumerateFaceUses(const iCAX::GeometryData::BRepModel&);
    struct SSizeRange {double MinimumMm=0.001,MaximumMm=100000;};
    struct SRecognitionProfile
    {
        iCAX::Geometry::SGeometryAnalysisOptions Geometry;
        double ShapeToleranceMm=0.01;
        SSizeRange Radius,Width,Length;
        std::size_t MaximumFaces=256,MaximumCandidates=4096;
    };
    struct SRecognitionInput
    {
        std::uint32_t SchemaVersion=1;
        std::shared_ptr<const iCAX::GeometryData::BRepModel> Geometry;
        iCAX::Resource::CResourceReference GeometryRef,TopologyMapRef,PlanningProfileRef;
        std::string PlanningProfileRevision;
        std::vector<std::uint64_t> OccurrencePath,SelectedFaces;
        std::vector<iCAX::Geometry::SBoundaryCurveSpan> SelectedEdges;
        std::string SelectionMode="Faces";
        std::vector<std::string> RequestedKinds={"GeneralContour","Circle","Ellipse","Rectangle","Slot"};
        std::string ModelFrame="Workpiece",Unit="mm",Representation="Solid",WorkpieceFamily="GeneralSolid";
        // Surface mode is an explicit user choice; it never proves a solid
        // passage. Side is relative to the oriented selected support face.
        double DeclaredThicknessMm=0;
        int OutwardSign=1;
        // Workpiece-frame viewing ray, from the observer towards the material.
        // Publication uses it to choose the machining side of each boundary.
        std::optional<std::array<double,3>> ObservationDirection;
        // Create editable geometric cutting lines without asserting machining readiness.
        bool GeometryOnly=false;
        std::optional<double> SourceErrorUpperBoundMm;
        SRecognitionProfile Profile;
    };
    struct SRecognitionPredicate
    {
        std::string ID,State,Method;
        std::vector<std::uint64_t> Faces;
    };
    struct SCutCandidate
    {
        std::string LocalID,SourceKey,BoundaryRole="Unknown",GeometryKind="GeneralContour",ClassificationStatus="Unresolved";
        std::string PassageKind="Unknown",ConfirmationEligibility="Blocked",ProcessEligibility="Blocked",Proof="None";
        std::string SourceErrorCoverage="RelativeToStoredBRepOnly";
        iCAX::Geometry::SAnalyticBoundary Boundary;
        iCAX::Geometry::SBoundarySweepAnalysis Sweep;
        iCAX::Geometry::SBoundaryTermination Termination;
        std::string MachiningKind="Unknown";
        std::vector<SRecognitionPredicate> Predicates;
        std::vector<std::string> Issues;
    };
    struct SRecognitionResult
    {
        std::string InputDigest,AlgorithmIdentity="icax.cut-recognition/boundary-termination-v2";
        std::vector<std::uint64_t> RequestedFaces,CheckedFaces;
        std::vector<SCutCandidate> Candidates;
        std::vector<std::string> Issues;
        bool Complete=false;
    };
    struct SRetainedFaceSelectionInput {
        std::shared_ptr<const iCAX::GeometryData::BRepModel> Geometry;
        std::array<double,3> ObservationDirection{0,0,-1};
        iCAX::Geometry::SGeometryAnalysisOptions Options;
    };
    class CUT_RECOGNITION_API ICamCutRecognitionService : public iCAX::Services::IService
    {
    public:
        virtual SRecognitionResult Recognize(const SRecognitionInput&) const=0;
        virtual std::vector<std::uint64_t> SelectRetainedFaces(const SRetainedFaceSelectionInput&) const=0;
    };
    CUT_RECOGNITION_API std::uint32_t GetRecognitionContractVersion() noexcept;
}
