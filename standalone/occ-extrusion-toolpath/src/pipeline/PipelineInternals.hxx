#pragma once

#include "etp/Pipeline.hxx"

#include "../SectionRegion.hxx"

#include <utility>

namespace etp::pipeline_detail
{
    [[nodiscard]] gp_Dir RecognizeAxis(
        const TopoDS_Shape& shape,
        const AnalyzerOptions& options,
        std::vector<Diagnostic>& diagnostics);

    [[nodiscard]] gp_Ax3 BuildSourceFrame(
        const TopoDS_Shape& shape,
        const gp_Dir& axis,
        double tolerance);

    [[nodiscard]] std::pair<double, double> AxialRange(
        const TopoDS_Shape& shape,
        const gp_Ax3& frame);

    [[nodiscard]] bool IsCapFace(
        const TopoDS_Face& face,
        const gp_Dir& axis,
        double angularTolerance);

    [[nodiscard]] bool IsAxisInvariantSurface(
        const TopoDS_Face& face,
        const gp_Dir& axis,
        double angularTolerance);

    [[nodiscard]] std::vector<TopoDS_Wire> RecoverSectionWires(
        const TopoDS_Shape& shape,
        const std::vector<TopoDS_Face>& axialFaces,
        const gp_Ax3& frame,
        const AnalyzerOptions& options,
        std::vector<Diagnostic>& diagnostics);

    [[nodiscard]] std::vector<gp_Pnt2d> CollectSectionBreakPoints(
        const std::vector<TopoDS_Wire>& wires,
        const gp_Ax3& frame,
        double tolerance);

    [[nodiscard]] bool EveryBoundaryPointOnSection(
        const TopoDS_Face& face,
        const detail::SectionRegion& section,
        const AnalyzerOptions& options);

    [[nodiscard]] TopoDS_Shape MakeFaceGroup(
        const std::vector<FacePatch>& patches,
        const std::vector<std::size_t>& patchIds);
}
