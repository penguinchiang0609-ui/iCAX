#pragma once

#include "etp/Types.hxx"

#include <filesystem>
#include <optional>
#include <vector>

namespace etp
{
    struct AnalyzerOptions final
    {
        double DistanceTolerance = 1.0e-3;
        double AngularToleranceRadians = 1.0e-3;
        double SamplingDeflection = 5.0e-2;
        std::size_t MinimumEdgeSamples = 9;
        std::size_t ToolpathSamples = 33;
        std::size_t MaximumAxisCandidates = 24;
        // Uniform conversion applied while normalizing the part. Use 1.0 for
        // millimetres, 25.4 when the supplied BRep coordinates are inches.
        double InputLengthScale = 1.0;
        bool MaterializeFaceSplits = true;
    };

    class ExtrusionToolpathAnalyzer final
    {
    public:
        explicit ExtrusionToolpathAnalyzer(AnalyzerOptions options = {});

        [[nodiscard]] PartAnalysis Analyze(const TopoDS_Shape& part) const;

        // Splits an assembly/compound into non-overlapping solid parts before
        // running the same recognition pipeline on each part.
        [[nodiscard]] std::vector<PartAnalysis> AnalyzeAssembly(
            const TopoDS_Shape& assembly) const;

        // Exact API requested by the feature recognizer: the feature group and
        // the cross-section wires are sufficient when the wires are planar.
        [[nodiscard]] FeatureAnalysis CompileFeatureGroup(
            const TopoDS_Shape& featureGroup,
            const std::vector<TopoDS_Wire>& sectionWires,
            std::size_t featureIndex = 0) const;

        // Explicit-frame overload avoids the sign ambiguity of a recovered
        // section plane and is used by the whole-part analyzer.
        [[nodiscard]] FeatureAnalysis CompileFeatureGroup(
            const TopoDS_Shape& featureGroup,
            const std::vector<TopoDS_Wire>& sectionWires,
            const gp_Ax3& sectionFrame,
            std::size_t featureIndex = 0) const;

        // Attempts to compile one already-normalized machining face from the
        // caller-selected cutting trajectory.  The contract is intentionally
        // small: false clears outToolpath; true returns a complete XYZ + IJK
        // path.  Surface/ruling diagnostics remain implementation details.
        [[nodiscard]] bool TryBuildToolpath(
            const TopoDS_Face& face,
            const TopoDS_Wire& cuttingTrajectory,
            const std::vector<TopoDS_Wire>& sectionWires,
            Toolpath& outToolpath) const;

        [[nodiscard]] bool TryBuildToolpath(
            const TopoDS_Face& face,
            const TopoDS_Wire& cuttingTrajectory,
            const std::vector<TopoDS_Wire>& sectionWires,
            const gp_Ax3& sectionFrame,
            Toolpath& outToolpath) const;

        [[nodiscard]] const AnalyzerOptions& Options() const noexcept;

    private:
        AnalyzerOptions m_Options;
    };

    [[nodiscard]] TopoDS_Shape ReadCadFile(const std::filesystem::path& path);
    void WriteAnalysisJson(
        const PartAnalysis& analysis,
        const std::filesystem::path& path);
    void WriteAnalysisJson(
        const std::vector<PartAnalysis>& analyses,
        const std::filesystem::path& path);
}
