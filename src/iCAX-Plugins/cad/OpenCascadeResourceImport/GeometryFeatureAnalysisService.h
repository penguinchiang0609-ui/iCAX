#pragma once
#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/GeometryData.h"
#include "Services/IService.h"
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace iCAX::Geometry
{
    struct SBoundaryCurveSpan
    {
        std::uint64_t EdgeID=0, CurveID=0;
        double First=0, Last=0;
        bool Reversed=false;
        std::uint64_t FaceID=0, WireID=0;
        std::size_t CoedgeIndex=0;
    };
    struct SAnalyticBoundary
    {
        std::uint64_t FaceID=0;
        std::vector<SBoundaryCurveSpan> Spans;
        // Geometric trimmed-face relation only, not a CAM hole/passage verdict.
        std::string RegionRelation="Unknown", GeometryKind="GeneralContour", Classification="NotStandard";
        bool Closed=false, Valid=false;
        // Display tessellation only; source spans remain the authoritative geometry.
        std::vector<std::array<double,3>> PreviewPoints;
        bool TopologyClosed=false;
        std::array<double,3> Center{}, Normal{}, XDirection{};
        double RadiusMm=0, WidthMm=0, LengthMm=0, PerimeterMm=0;
        // Full-interval deviation to the reported analytic shape, not sampled RMS.
        double StandardDeviationUpperMm=0;
        bool StandardBoundProven=false;
        std::vector<std::string> Diagnostics;
    };
    struct SFaceBoundaryAnalysis
    {
        std::uint64_t FaceID=0;
        bool Planar=false, Complete=false;
        std::vector<SAnalyticBoundary> Boundaries;
        std::vector<std::string> Diagnostics;
    };
    struct SFacePatchAnalysis
    {
        std::vector<std::uint64_t> SelectedFaces;
        std::vector<SAnalyticBoundary> Boundaries;
        std::vector<std::string> Diagnostics;
        std::size_t CancelledInternalUses=0, DegenerateUses=0;
        std::size_t ConnectedComponents=0;
        bool Complete=false;
    };
    // Neutral evidence about a region swept between two planar sections. A
    // consumer decides whether these facts describe a machinable CAM feature.
    struct SBoundarySweepAnalysis
    {
        SAnalyticBoundary Entrance, Terminal;
        std::vector<std::uint64_t> SideFaces, TerminalFaces;
        std::vector<std::string> Diagnostics;
        std::array<double,3> Direction{};
        double LengthMm=0, ConeHalfAngleRad=0;
        std::string WallGeometry="Unresolved", TerminalRelation="Unknown";
        bool SingleSolid=false, Complete=false, SideWallCoverage=false;
        bool SweptInteriorEmpty=false, VolumeTestComplete=false;
    };
    struct SGeometryAnalysisOptions
    {
        double DistanceToleranceMm=1e-6, AngleToleranceRad=1e-7;
        std::size_t MaximumEdgeUses=10000;
        std::function<bool()> IsCancelled;
    };
    struct SBoundaryTermination
    {
        std::vector<std::uint64_t> EntranceEdges, SideFaces, OppositeEdges, TerminalFaces;
        // Oriented dihedrals of the two material faces at each shared edge.
        // No manufacturing semantics or default-through assumption.
        std::string EntranceRelation="Unknown", OppositeRelation="Unknown";
        std::vector<std::string> Diagnostics;
        bool Complete=false;
        // The omitted side is one planar, single-wire face whose complete
        // nondegenerate boundary equals this selected-patch cycle.
        bool SingleFaceCap=false;
    };
    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP IGeometryFeatureAnalysisService : public iCAX::Services::IService
    {
    public:
        virtual SAnalyticBoundary AnalyzeEdgeLoop(const iCAX::GeometryData::BRepModel&,
            const std::vector<SBoundaryCurveSpan>&,const SGeometryAnalysisOptions&) const=0;
        virtual SFaceBoundaryAnalysis AnalyzeFace(const iCAX::GeometryData::BRepModel&,std::uint64_t,
            const SGeometryAnalysisOptions&) const=0;
        virtual SFacePatchAnalysis AnalyzePatch(const iCAX::GeometryData::BRepModel&,const std::vector<std::uint64_t>&,
            const SGeometryAnalysisOptions&) const=0;
        virtual SBoundarySweepAnalysis AnalyzeBoundarySweep(const iCAX::GeometryData::BRepModel&,std::uint64_t faceID,
            const std::vector<std::uint64_t>& boundaryEdges,const SGeometryAnalysisOptions&) const=0;
        // Follow side walls from closed components of a selected face patch;
        // its internal shared edges are excluded from the entrance boundary.
        virtual std::vector<SBoundaryTermination> AnalyzeBoundaryTerminations(const iCAX::GeometryData::BRepModel&,
            const std::vector<std::uint64_t>& selectedFaces,const std::vector<SAnalyticBoundary>&,const SGeometryAnalysisOptions&) const=0;
    };
}
