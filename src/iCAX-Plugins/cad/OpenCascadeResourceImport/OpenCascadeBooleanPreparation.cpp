#include "pch.h"
#include "OpenCascadeBooleanPreparation.h"
#include "OpenCascadeCancellation.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Geom_Curve.hxx>
#include <Precision.hxx>
#include <ShapeUpgrade_ShapeDivideClosedEdges.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <stdexcept>

namespace iCAX::OpenCascade
{
namespace
{
    bool HasShiftedClosedPeriodicEdge(const TopoDS_Shape& shape)
    {
        for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next()) {
            const auto edge = TopoDS::Edge(it.Current());
            if (BRep_Tool::Degenerated(edge) || !BRep_Tool::IsClosed(edge)) continue;
            double first, last;
            const auto curve = BRep_Tool::Curve(edge, first, last);
            if (curve.IsNull() || !curve->IsPeriodic()) continue;
            const auto period = curve->Period();
            if (last-first >= period-Precision::PConfusion()
                && (first < curve->FirstParameter()-Precision::PConfusion()
                    || last > curve->LastParameter()+Precision::PConfusion())) return true;
        }
        return false;
    }
    double Volume(const TopoDS_Shape& shape)
    {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(shape, properties, 1e-10);
        return std::abs(properties.Mass());
    }
    double Difference(const TopoDS_Shape& left, const TopoDS_Shape& right)
    {
        BRepAlgoAPI_Cut cut;
        NCollection_List<TopoDS_Shape> arguments, tools;
        arguments.Append(BRepBuilderAPI_Copy(left, true, false).Shape());
        tools.Append(BRepBuilderAPI_Copy(right, true, false).Shape());
        cut.SetArguments(arguments); cut.SetTools(tools);
        cut.SetNonDestructive(true); cut.SetToFillHistory(false);
        cut.SetUseOBB(true);
        COpenCascadeCancellationScope::Build(cut);
        if (!cut.IsDone() || cut.Shape().IsNull() || !BRepCheck_Analyzer(cut.Shape()).IsValid())
            throw std::invalid_argument("Cannot verify exact closed-period edge subdivision");
        return Volume(cut.Shape());
    }
}
TopoDS_Shape PrepareOpenCascadeBooleanOperand(const TopoDS_Shape& shape)
{
    if (shape.IsNull() || !HasShiftedClosedPeriodicEdge(shape)) return shape;
    if (!BRepCheck_Analyzer(shape).IsValid())
        throw std::invalid_argument("Periodic boolean operand is invalid");
    COpenCascadeCancellationScope::ThrowIfCancellationRequested();
    // A full circle stored over e.g. [pi,3*pi] is valid STEP geometry.
    // OCCT 8's coincident-edge splitter can nevertheless retain the entire
    // source in a self-cut. Split only the edge domains, retaining the exact
    // analytic curves and surfaces; keep the caller's selection/evidence topology intact.
    ShapeUpgrade_ShapeDivideClosedEdges divide(BRepBuilderAPI_Copy(shape, true, false).Shape());
    divide.SetNbSplitPoints(1); divide.SetSurfaceSegmentMode(false);
    divide.SetPrecision(Precision::Confusion()); divide.Perform();
    auto prepared = divide.Result();
    if (prepared.IsNull()) throw std::invalid_argument("Closed-period edge subdivision failed");
    // The divider resets range/parameter flags on the new edge segments.
    BRepLib::SameParameter(prepared, Precision::Confusion(), true);
    if (!BRepCheck_Analyzer(prepared).IsValid())
        throw std::invalid_argument("Closed-period edge subdivision is invalid");
    const auto scale = std::max(Volume(shape), Volume(prepared));
    const auto difference = Difference(shape, prepared)+Difference(prepared, shape);
    if (!std::isfinite(scale) || !std::isfinite(difference)
        || difference > std::max(1e-9, scale*1e-8))
        throw std::invalid_argument("Closed-period edge subdivision changes geometry");
    return prepared;
}
}
