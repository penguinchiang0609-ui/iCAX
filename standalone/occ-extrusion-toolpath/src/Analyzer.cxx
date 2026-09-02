#include "etp/Analyzer.hxx"

#include "etp/Pipeline.hxx"

#include "SectionRegion.hxx"
#include "RulingToolpath.hxx"
#include "SweepCompiler.hxx"

#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <Standard_Failure.hxx>

#include <exception>
#include <stdexcept>
#include <utility>

namespace etp
{
    ExtrusionToolpathAnalyzer::ExtrusionToolpathAnalyzer(AnalyzerOptions options)
        : m_Options(std::move(options))
    {
        if (m_Options.DistanceTolerance <= 0.0
            || m_Options.AngularToleranceRadians <= 0.0
            || m_Options.ToolpathSamples < 2
            || m_Options.InputLengthScale <= 0.0)
        {
            throw std::invalid_argument("invalid analyzer tolerance, scale, or sample count");
        }
    }

    PartAnalysis ExtrusionToolpathAnalyzer::Analyze(const TopoDS_Shape& part) const
    {
        AnalysisJob job;
        job.Id = "part-0";
        job.Options = m_Options;
        job.SourceShape = part;
        auto pipe = MakeDefaultPipe();
        return pipe.Run(job);
    }

    std::vector<PartAnalysis> ExtrusionToolpathAnalyzer::AnalyzeAssembly(
        const TopoDS_Shape& assembly) const
    {
        std::vector<PartAnalysis> result;
        TopTools_IndexedMapOfShape solids;
        TopExp::MapShapes(assembly, TopAbs_SOLID, solids);

        if (solids.IsEmpty())
        {
            result.push_back(Analyze(assembly));
            return result;
        }

        result.reserve(static_cast<std::size_t>(solids.Extent()));
        for (Standard_Integer index = 1; index <= solids.Extent(); ++index)
        {
            AnalysisJob job;
            job.Id = "part-" + std::to_string(index - 1);
            job.Options = m_Options;
            job.SourceShape = solids(index);
            auto pipe = MakeDefaultPipe();
            result.push_back(pipe.Run(job));
        }
        return result;
    }

    FeatureAnalysis ExtrusionToolpathAnalyzer::CompileFeatureGroup(
        const TopoDS_Shape& featureGroup,
        const std::vector<TopoDS_Wire>& sectionWires,
        const std::size_t featureIndex) const
    {
        const auto frame = detail::InferSectionFrame(
            sectionWires, m_Options.DistanceTolerance);
        return CompileFeatureGroup(featureGroup, sectionWires, frame, featureIndex);
    }

    FeatureAnalysis ExtrusionToolpathAnalyzer::CompileFeatureGroup(
        const TopoDS_Shape& featureGroup,
        const std::vector<TopoDS_Wire>& sectionWires,
        const gp_Ax3& sectionFrame,
        const std::size_t featureIndex) const
    {
        const detail::SectionRegion section(sectionWires, sectionFrame, m_Options);
        return detail::CompileSweepFeature(
            featureGroup, section, m_Options, featureIndex);
    }

    bool ExtrusionToolpathAnalyzer::TryBuildToolpath(
        const TopoDS_Face& face,
        const TopoDS_Wire& cuttingTrajectory,
        const std::vector<TopoDS_Wire>& sectionWires,
        Toolpath& outToolpath) const
    {
        outToolpath = {};
        try
        {
            const auto frame = detail::InferSectionFrame(
                sectionWires, m_Options.DistanceTolerance);
            return TryBuildToolpath(
                face,
                cuttingTrajectory,
                sectionWires,
                frame,
                outToolpath);
        }
        catch (const Standard_Failure&)
        {
            return false;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    bool ExtrusionToolpathAnalyzer::TryBuildToolpath(
        const TopoDS_Face& face,
        const TopoDS_Wire& cuttingTrajectory,
        const std::vector<TopoDS_Wire>& sectionWires,
        const gp_Ax3& sectionFrame,
        Toolpath& outToolpath) const
    {
        outToolpath = {};
        try
        {
            const detail::SectionRegion section(
                sectionWires, sectionFrame, m_Options);
            return detail::TryBuildRulingToolpath(
                face, cuttingTrajectory, section, m_Options, outToolpath);
        }
        catch (const Standard_Failure&)
        {
            return false;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    const AnalyzerOptions& ExtrusionToolpathAnalyzer::Options() const noexcept
    {
        return m_Options;
    }
}
