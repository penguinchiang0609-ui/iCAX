#pragma once

#include "etp/Analyzer.hxx"

#include <gp_Pnt2d.hxx>
#include <gp_Trsf.hxx>

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace etp
{
    enum class FaceRole : std::uint8_t
    {
        Unknown,
        Contour,
        Machining
    };

    struct FacePatch final
    {
        std::size_t Id = 0;
        std::size_t SourceFaceId = 0;
        TopoDS_Face Face;
        FaceRole Role = FaceRole::Unknown;
        bool AxisParallel = false;
        bool MaterializedSplit = false;
        std::vector<gp_Pnt2d> AppliedSectionBreaks;
    };

    struct FeatureGroupJob final
    {
        std::size_t Id = 0;
        TopoDS_Shape Shape;
        std::vector<std::size_t> FacePatchIds;
    };

    struct WorkReport final
    {
        std::string Name;
        bool Succeeded = false;
        double ElapsedMilliseconds = 0.0;
        std::size_t DiagnosticsBefore = 0;
        std::size_t DiagnosticsAfter = 0;
    };

    // Mutable state carried through the pipe. A Work reads fields produced by
    // earlier Works and writes only the fields owned by its stage.
    struct AnalysisJob final
    {
        std::string Id = "part-0";
        AnalyzerOptions Options;
        TopoDS_Shape SourceShape;
        TopoDS_Shape WorkingShape;

        gp_Trsf SourceToNormalized;
        gp_Trsf NormalizedToSource;
        double LengthScale = 1.0;

        std::optional<gp_Dir> ExtrusionAxis;
        std::optional<gp_Ax3> SectionFrame;
        double AxialFirst = 0.0;
        double AxialLast = 0.0;

        std::vector<TopoDS_Face> AxisParallelFaces;
        std::vector<TopoDS_Wire> SectionWires;
        std::vector<gp_Pnt2d> SectionBreakPoints;
        std::vector<FacePatch> FacePatches;
        std::vector<FeatureGroupJob> FeatureGroups;

        PartAnalysis Result;
        std::vector<Diagnostic> Diagnostics;
        std::vector<WorkReport> Reports;

        [[nodiscard]] bool HasErrors() const noexcept;
    };

    class Work
    {
    public:
        virtual ~Work() = default;
        [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
        virtual void Execute(AnalysisJob& job) const = 0;
    };

    class Pipe final
    {
    public:
        Pipe& Add(std::unique_ptr<Work> work);
        [[nodiscard]] PartAnalysis Run(AnalysisJob& job) const;
        [[nodiscard]] const std::vector<std::unique_ptr<Work>>& Works() const noexcept;

    private:
        std::vector<std::unique_ptr<Work>> m_Works;
    };

    class PreprocessWork final : public Work
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override;
        void Execute(AnalysisJob& job) const override;
    };

    class NormalizeWork final : public Work
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override;
        void Execute(AnalysisJob& job) const override;
    };

    class SectionProfileWork final : public Work
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override;
        void Execute(AnalysisJob& job) const override;
    };

    class FaceSplitClassifyWork final : public Work
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override;
        void Execute(AnalysisJob& job) const override;
    };

    // Uses OCCT Sewing to canonicalize shared machining boundaries and takes
    // each independent Face/Shell in SewedShape() as one feature group.
    class FeatureGroupingWork final : public Work
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override;
        void Execute(AnalysisJob& job) const override;
    };

    // Materializes manufacturing-semantic boundaries inside each already-sewn
    // feature group. Running group-wide preserves shared replacement Edges.
    // A C1 composite B-spline that represents line/arc/line/arc spans, for
    // example, is divided at its C2 breaks so each resulting Face owns one
    // continuous sweep law and explicit boundary coedges.
    class SweepFaceDecomposeWork final : public Work
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override;
        void Execute(AnalysisJob& job) const override;
    };

    class ToolpathWork final : public Work
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override;
        void Execute(AnalysisJob& job) const override;
    };

    [[nodiscard]] Pipe MakeDefaultPipe();
}
