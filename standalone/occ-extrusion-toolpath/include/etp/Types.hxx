#pragma once

#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace etp
{
    enum class DiagnosticSeverity : std::uint8_t
    {
        Info,
        Warning,
        Error
    };

    struct Diagnostic final
    {
        DiagnosticSeverity Severity = DiagnosticSeverity::Info;
        std::string Code;
        std::string Message;
        std::optional<std::size_t> FaceId;
    };

    struct SectionCurveRef final
    {
        std::size_t WireIndex = 0;
        std::size_t EdgeIndex = 0;
        double CurveParameter = 0.0;
    };

    enum class SectionPointState : std::uint8_t
    {
        OnBoundary,
        InMaterial,
        InVoid
    };

    struct SectionHit final
    {
        SectionPointState State = SectionPointState::InVoid;
        std::optional<SectionCurveRef> Boundary;
        double BoundaryDistance = 0.0;
        double AxialStation = 0.0;
    };

    enum class SegmentRole : std::uint8_t
    {
        Unknown,
        OnSkin,
        CrossMaterial,
        FeatureInternal,
        SyntheticSplit,
        SurfaceSeam
    };

    struct CoedgeSegment final
    {
        TopoDS_Edge Edge;
        double First = 0.0;
        double Last = 0.0;
        SegmentRole Role = SegmentRole::Unknown;
        bool Reversed = false;
        bool GeometricallyStraight = false;
        std::optional<SectionCurveRef> StartSkin;
        std::optional<SectionCurveRef> EndSkin;
        std::vector<gp_Pnt> Samples;
    };

    enum class RailKind : std::uint8_t
    {
        Curve,
        Point
    };

    struct Rail final
    {
        RailKind Kind = RailKind::Curve;
        std::vector<CoedgeSegment> Segments;
        std::vector<gp_Pnt> Samples;
        std::optional<TopoDS_Vertex> CollapsedVertex;
    };

    enum class SweepTopology : std::uint8_t
    {
        Strip,
        FanToPoint,
        FanFromPoint,
        DoublePinched,
        Periodic
    };

    enum class SweepModel : std::uint8_t
    {
        LinearRuling,
        AnalyticCylinder,
        AnalyticCone,
        ApproximateLinearRuling,
        Unsupported
    };

    struct ToolPose final
    {
        gp_Pnt Position;
        gp_Dir ToolDirection = gp_Dir(0.0, 0.0, 1.0);
        gp_Dir FeedDirection = gp_Dir(1.0, 0.0, 0.0);
        gp_Dir SurfaceNormal = gp_Dir(0.0, 1.0, 0.0);
        double PathParameter = 0.0;
        double MaterialDepth = 0.0;
    };

    struct SweepUnit final
    {
        std::string Id;
        std::size_t FeatureIndex = 0;
        SweepTopology Topology = SweepTopology::Strip;
        SweepModel Model = SweepModel::Unsupported;
        std::vector<std::size_t> SourceFaceIds;
        std::vector<TopoDS_Face> SourceFaces;
        Rail EntryRail;
        Rail ExitRail;
        std::optional<CoedgeSegment> StartSide;
        std::optional<CoedgeSegment> EndSide;
        std::vector<ToolPose> Poses;
        bool Closed = false;
        double FitError = 0.0;
        double Confidence = 0.0;
    };

    struct Toolpath final
    {
        std::string Id;
        std::string SweepUnitId;
        bool Closed = false;
        std::vector<ToolPose> Poses;
    };

    struct FeatureAnalysis final
    {
        std::string Id;
        TopoDS_Shape Shape;
        std::vector<std::size_t> FaceIds;
        std::vector<SweepUnit> SweepUnits;
        std::vector<Toolpath> Toolpaths;
        std::vector<Diagnostic> Diagnostics;
    };

    struct PartAnalysis final
    {
        std::string Id;
        TopoDS_Shape Shape;
        gp_Trsf SourceToNormalized;
        gp_Trsf NormalizedToSource;
        double LengthScale = 1.0;
        gp_Ax3 SectionFrame;
        double AxialFirst = 0.0;
        double AxialLast = 0.0;
        std::vector<TopoDS_Wire> SectionWires;
        std::vector<std::size_t> ContourFaceIds;
        std::vector<FeatureAnalysis> Features;
        std::vector<Diagnostic> Diagnostics;
    };
}
