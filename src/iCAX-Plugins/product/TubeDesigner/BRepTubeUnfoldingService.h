#pragma once

#include "TubeDesignerExport.h"
#include "Data/Variant.h"
#include "GeometryData/GeometryData.h"
#include "Services/IService.h"
#include "TubeNesting/TubeNesting.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>

namespace iCAX::TubeDesigner
{
    /*
    * A point in the tube's own drawing space.  S is distance along the
    * recognized linear axis and U is physical arc length on one section loop.
    * Neither coordinate is an OCC UV coordinate.
    */
    struct STubeUnfoldedPoint final
    {
        double S = 0.0;
        double U = 0.0;
    };

    /*
    * The editable domain of a tube side sketch.  This is the whole lateral
    * wall, not one OCC face: S is the axial distance and U is the closed
    * section's physical arc length.  UStart and UEnd are the two copies of
    * the same seam used to render the periodic direction as a rectangle.
    */
    struct STubeUnfoldedRectangle final
    {
        bool Available = false;
        double SStart = 0.0;
        double SEnd = 0.0;
        double UStart = 0.0;
        double UEnd = 0.0;
        bool PeriodicU = true;
    };

    /*
    * An owned 2D wire.  SourceFaceIndex/SourceEdgeIndex are analysis-local
    * indices for the input snapshot; they are deliberately not persisted as
    * durable CAD identity.
    */
    struct STubeUnfoldedWire final
    {
        std::size_t ID = 0;
        std::size_t SurfaceIndex = 0;
        std::size_t SourceFaceIndex = 0;
        std::size_t SourceEdgeIndex = 0;
        std::string Role;
        bool Closed = false;
        bool WrapsAroundSeam = false;
        std::vector<STubeUnfoldedPoint> Points;
    };

    struct STubeUnfoldedPanel final
    {
        std::size_t ID = 0;
        std::size_t SurfaceIndex = 0;
        std::size_t SourceFaceIndex = 0;
        double UStart = 0.0;
        double UEnd = 0.0;
        std::vector<STubeUnfoldedWire> Boundaries;
    };

    struct STubeUnfoldedSurface final
    {
        std::size_t ID = 0;
        std::string LoopID;
        bool Inner = false;
        double UPeriod = 0.0;
        // The rectangle is the primary editing contract. Panels are kept as
        // optional source-face evidence for diagnostics and compatibility.
        STubeUnfoldedRectangle Rectangle;
        std::vector<STubeUnfoldedPanel> Panels;
        std::vector<STubeUnfoldedWire> Wires;
    };

    struct STubeBRepUnfoldingResult final
    {
        bool bOK = false;
        bool Approximate = false;
        bool Periodic = false;
        std::string CoordinateSpace = "axial-arc-length";
        std::string Method;
        std::string Diagnostic;
        std::array<double, 3> Axis{ 1.0, 0.0, 0.0 };
        std::array<double, 3> SectionX{ 0.0, 1.0, 0.0 };
        std::array<double, 3> SectionY{ 0.0, 0.0, 1.0 };
        double Length = 0.0;
        double Perimeter = 0.0;
        double ProfileMismatch = 0.0;
        double MaximumProjectionResidual = 0.0;
        std::size_t ProfileStationCount = 0;
        std::size_t PointCount = 0;
        // Convenience copy of the outer lateral rectangle. Each surface also
        // carries its own rectangle because inner loops have their own period.
        STubeUnfoldedRectangle LateralRectangle;
        std::vector<STubeUnfoldedSurface> Surfaces;
    };

    struct STubeBRepUnfoldingOptions final
    {
        double DistanceTolerance = 1.0e-3;
        double AngularToleranceRadians = 1.0e-3;
        std::size_t StationCount = 9;
        std::size_t SamplesPerCurve = 65;
        std::size_t MaximumOutputPoints = 200000;
        bool IncludeInnerSurfaces = true;
        bool IncludeBoundaryWires = true;
    };

    /*
    * A side sketch is not a planar solid.  Its x/y coordinates describe a
    * trajectory on the lateral rectangle: x is axial distance and y is
    * section arc length.  The implementation maps every sampled trajectory
    * point back to the BRep and uses that point's local outward normal to
    * construct a material-removal volume.
    */
    struct STubeSideSketchOptions final
    {
        double DistanceTolerance = 0.05;
        double AngularToleranceRadians = 1.0e-3;
        std::size_t StationCount = 33;
        std::size_t SamplesPerCurve = 65;
        std::size_t MaximumToolPatches = 20000;
        // Open trajectories need a finite manufacturing kerf. Closed
        // trajectories are filled and do not use this value.
        double TrajectoryWidth = 0.5;
        double OuterClearance = 0.2;
        double InnerClearance = 0.2;
    };

    struct STubeSideSketchResult final
    {
        bool bOK = false;
        bool Changed = false;
        bool Approximate = true;
        std::string Method;
        std::string Diagnostic;
        std::size_t ClosedLoopCount = 0;
        std::size_t OpenTrajectoryCount = 0;
        std::size_t TrajectoryPointCount = 0;
        std::size_t ToolPatchCount = 0;
        TopoDS_Shape Shape;
    };

    struct STubeUnfoldedEndFeatureCodes final
    {
        bool bOK = false;
        iCAX::TubeNesting::CutLineFeatureCode Left;
        iCAX::TubeNesting::CutLineFeatureCode Right;
        std::string Diagnostic;
    };

    class _TUBE_DESIGNER_EXP IBRepTubeUnfoldingService
        : public iCAX::Services::IService
    {
    public:
        ~IBRepTubeUnfoldingService() override = default;

        virtual STubeBRepUnfoldingResult Unfold(
            IN const TopoDS_Shape& Shape_,
            IN const STubeBRepUnfoldingOptions& Options_ = {}) const = 0;

        virtual STubeBRepUnfoldingResult Unfold(
            IN const iCAX::GeometryData::BRepModel& BRep_,
            IN const STubeBRepUnfoldingOptions& Options_ = {}) const = 0;
    };

    /*
    * Stateless convenience entry point used by SDOs and tests.  The service
    * owns no scene/resource state; callers may cache the returned DTO against
    * their BRep resource version.
    */
    _TUBE_DESIGNER_EXP STubeBRepUnfoldingResult UnfoldTubeBRepSurface(
        IN const TopoDS_Shape& Shape_,
        IN const STubeBRepUnfoldingOptions& Options_ = {});

    _TUBE_DESIGNER_EXP STubeBRepUnfoldingResult UnfoldTubeBRepSurface(
        IN const iCAX::GeometryData::BRepModel& BRep_,
        IN const STubeBRepUnfoldingOptions& Options_ = {});

    _TUBE_DESIGNER_EXP STubeSideSketchResult ApplyTubeSideSketch(
        IN const TopoDS_Shape& Shape_,
        IN const iCAX::Data::ObjectMap& Sketch_,
        IN const STubeSideSketchOptions& Options_ = {});

    _TUBE_DESIGNER_EXP STubeSideSketchResult ApplyTubeSideSketch(
        IN const iCAX::GeometryData::BRepModel& BRep_,
        IN const iCAX::Data::ObjectMap& Sketch_,
        IN const STubeSideSketchOptions& Options_ = {});

    _TUBE_DESIGNER_EXP iCAX::Data::ObjectMap SerializeTubeBRepUnfolding(
        IN const STubeBRepUnfoldingResult& Result_);

    /**
     * 从展开结果提取排样用的首端/尾端周期特征码。仅读取 outer 侧面上的
     * end-boundary 线，不修改原始 BRep，也不把 DXF/CSG 几何烘焙成不可编辑实体。
     */
    _TUBE_DESIGNER_EXP STubeUnfoldedEndFeatureCodes EncodeTubeUnfoldedEndFeatures(
        IN const STubeBRepUnfoldingResult& Result_,
        std::size_t SampleCount_ = 64);
}
