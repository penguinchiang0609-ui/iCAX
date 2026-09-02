#pragma once

#include "etp/Analyzer.hxx"

#include <gp_Pnt2d.hxx>

#include <vector>

namespace etp::detail
{
    struct SectionPolyline final
    {
        std::size_t WireIndex = 0;
        std::vector<gp_Pnt2d> Points;
        std::vector<std::size_t> SegmentEdgeIndices;
    };

    class SectionRegion final
    {
    public:
        SectionRegion(
            const std::vector<TopoDS_Wire>& wires,
            const gp_Ax3& frame,
            const AnalyzerOptions& options);

        [[nodiscard]] SectionHit Classify(const gp_Pnt& point) const;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] const gp_Ax3& Frame() const noexcept;
        [[nodiscard]] const std::vector<SectionPolyline>& Loops() const noexcept;

    private:
        gp_Ax3 m_Frame;
        AnalyzerOptions m_Options;
        std::vector<SectionPolyline> m_Loops;
    };

    [[nodiscard]] gp_Ax3 InferSectionFrame(
        const std::vector<TopoDS_Wire>& wires,
        double tolerance);
}
