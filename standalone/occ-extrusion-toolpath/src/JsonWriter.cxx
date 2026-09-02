#include "etp/Analyzer.hxx"

#include <fstream>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <string_view>

namespace etp
{
    namespace
    {
        const char* SeverityName(const DiagnosticSeverity value)
        {
            switch (value)
            {
            case DiagnosticSeverity::Info: return "info";
            case DiagnosticSeverity::Warning: return "warning";
            case DiagnosticSeverity::Error: return "error";
            }
            return "unknown";
        }

        const char* TopologyName(const SweepTopology value)
        {
            switch (value)
            {
            case SweepTopology::Strip: return "strip";
            case SweepTopology::FanToPoint: return "fan_to_point";
            case SweepTopology::FanFromPoint: return "fan_from_point";
            case SweepTopology::DoublePinched: return "double_pinched";
            case SweepTopology::Periodic: return "periodic";
            }
            return "unknown";
        }

        const char* ModelName(const SweepModel value)
        {
            switch (value)
            {
            case SweepModel::LinearRuling: return "linear_ruling";
            case SweepModel::AnalyticCylinder: return "analytic_cylinder";
            case SweepModel::AnalyticCone: return "analytic_cone";
            case SweepModel::ApproximateLinearRuling: return "approximate_linear_ruling";
            case SweepModel::Unsupported: return "unsupported";
            }
            return "unknown";
        }

        const char* SegmentRoleName(const SegmentRole value)
        {
            switch (value)
            {
            case SegmentRole::Unknown: return "unknown";
            case SegmentRole::OnSkin: return "on_skin";
            case SegmentRole::CrossMaterial: return "cross_material";
            case SegmentRole::FeatureInternal: return "feature_internal";
            case SegmentRole::SyntheticSplit: return "synthetic_split";
            case SegmentRole::SurfaceSeam: return "surface_seam";
            }
            return "unknown";
        }

        void String(std::ostream& output, const std::string_view value)
        {
            output << '"';
            for (const unsigned char ch : value)
            {
                switch (ch)
                {
                case '"': output << "\\\""; break;
                case '\\': output << "\\\\"; break;
                case '\b': output << "\\b"; break;
                case '\f': output << "\\f"; break;
                case '\n': output << "\\n"; break;
                case '\r': output << "\\r"; break;
                case '\t': output << "\\t"; break;
                default:
                    if (ch < 0x20)
                    {
                        output << "\\u" << std::hex << std::setw(4)
                            << std::setfill('0') << static_cast<int>(ch)
                            << std::dec << std::setfill(' ');
                    }
                    else
                    {
                        output << static_cast<char>(ch);
                    }
                }
            }
            output << '"';
        }

        void Point(std::ostream& output, const gp_Pnt& point)
        {
            output << '[' << point.X() << ',' << point.Y() << ',' << point.Z() << ']';
        }

        void Direction(std::ostream& output, const gp_Dir& direction)
        {
            output << '[' << direction.X() << ',' << direction.Y() << ',' << direction.Z() << ']';
        }

        void Transform(std::ostream& output, const gp_Trsf& transform)
        {
            output << '[';
            for (int row = 1; row <= 3; ++row)
            {
                if (row > 1) output << ',';
                output << '[';
                for (int column = 1; column <= 4; ++column)
                {
                    if (column > 1) output << ',';
                    output << transform.Value(row, column);
                }
                output << ']';
            }
            output << ']';
        }

        void Pose(std::ostream& output, const ToolPose& pose)
        {
            output << "{\"position\":";
            Point(output, pose.Position);
            output << ",\"toolDirection\":";
            Direction(output, pose.ToolDirection);
            output << ",\"feedDirection\":";
            Direction(output, pose.FeedDirection);
            output << ",\"surfaceNormal\":";
            Direction(output, pose.SurfaceNormal);
            output << ",\"pathParameter\":" << pose.PathParameter
                << ",\"materialDepth\":" << pose.MaterialDepth << '}';
        }

        void Points(std::ostream& output, const std::vector<gp_Pnt>& points)
        {
            output << '[';
            for (std::size_t index = 0; index < points.size(); ++index)
            {
                if (index) output << ',';
                Point(output, points[index]);
            }
            output << ']';
        }

        void RailData(std::ostream& output, const Rail& rail)
        {
            output << "{\"kind\":";
            String(output, rail.Kind == RailKind::Point ? "point" : "curve");
            output << ",\"samples\":";
            Points(output, rail.Samples);
            output << '}';
        }

        void SideData(std::ostream& output, const CoedgeSegment& side)
        {
            output << "{\"role\":";
            String(output, SegmentRoleName(side.Role));
            output << ",\"geometricallyStraight\":"
                << (side.GeometricallyStraight ? "true" : "false")
                << ",\"samples\":";
            Points(output, side.Samples);
            output << '}';
        }

        void Diagnostics(std::ostream& output, const std::vector<Diagnostic>& diagnostics)
        {
            output << '[';
            for (std::size_t index = 0; index < diagnostics.size(); ++index)
            {
                if (index) output << ',';
                const auto& diagnostic = diagnostics[index];
                output << "{\"severity\":";
                String(output, SeverityName(diagnostic.Severity));
                output << ",\"code\":";
                String(output, diagnostic.Code);
                output << ",\"message\":";
                String(output, diagnostic.Message);
                if (diagnostic.FaceId)
                    output << ",\"faceId\":" << *diagnostic.FaceId;
                output << '}';
            }
            output << ']';
        }

        void Unit(std::ostream& output, const SweepUnit& unit)
        {
            output << "{\"id\":";
            String(output, unit.Id);
            output << ",\"topology\":";
            String(output, TopologyName(unit.Topology));
            output << ",\"model\":";
            String(output, ModelName(unit.Model));
            output << ",\"closed\":" << (unit.Closed ? "true" : "false")
                << ",\"fitError\":" << unit.FitError
                << ",\"confidence\":" << unit.Confidence
                << ",\"entryRail\":";
            RailData(output, unit.EntryRail);
            output << ",\"exitRail\":";
            RailData(output, unit.ExitRail);
            if (unit.StartSide)
            {
                output << ",\"startSide\":";
                SideData(output, *unit.StartSide);
            }
            if (unit.EndSide)
            {
                output << ",\"endSide\":";
                SideData(output, *unit.EndSide);
            }
            output << ",\"sourceFaceIds\":[";
            for (std::size_t index = 0; index < unit.SourceFaceIds.size(); ++index)
            {
                if (index) output << ',';
                output << unit.SourceFaceIds[index];
            }
            output << "],\"poses\":[";
            for (std::size_t index = 0; index < unit.Poses.size(); ++index)
            {
                if (index) output << ',';
                Pose(output, unit.Poses[index]);
            }
            output << "]}";
        }

        void Path(std::ostream& output, const Toolpath& path)
        {
            output << "{\"id\":";
            String(output, path.Id);
            output << ",\"sweepUnitId\":";
            String(output, path.SweepUnitId);
            output << ",\"closed\":" << (path.Closed ? "true" : "false")
                << ",\"poses\":[";
            for (std::size_t index = 0; index < path.Poses.size(); ++index)
            {
                if (index) output << ',';
                Pose(output, path.Poses[index]);
            }
            output << "]}";
        }

        void Feature(std::ostream& output, const FeatureAnalysis& feature)
        {
            output << "{\"id\":";
            String(output, feature.Id);
            output << ",\"faceIds\":[";
            for (std::size_t index = 0; index < feature.FaceIds.size(); ++index)
            {
                if (index) output << ',';
                output << feature.FaceIds[index];
            }
            output << "],\"sweepUnits\":[";
            for (std::size_t index = 0; index < feature.SweepUnits.size(); ++index)
            {
                if (index) output << ',';
                Unit(output, feature.SweepUnits[index]);
            }
            output << "],\"toolpaths\":[";
            for (std::size_t index = 0; index < feature.Toolpaths.size(); ++index)
            {
                if (index) output << ',';
                Path(output, feature.Toolpaths[index]);
            }
            output << "],\"diagnostics\":";
            Diagnostics(output, feature.Diagnostics);
            output << '}';
        }

        void Part(std::ostream& output, const PartAnalysis& part)
        {
            output << "{\"id\":";
            String(output, part.Id);
            output << ",\"lengthScale\":" << part.LengthScale
                << ",\"sourceToNormalized\":";
            Transform(output, part.SourceToNormalized);
            output << ",\"normalizedToSource\":";
            Transform(output, part.NormalizedToSource);
            output << ",\"sectionFrame\":{\"origin\":";
            Point(output, part.SectionFrame.Location());
            output << ",\"axis\":";
            Direction(output, part.SectionFrame.Direction());
            output << ",\"xDirection\":";
            Direction(output, part.SectionFrame.XDirection());
            output << ",\"yDirection\":";
            Direction(output, part.SectionFrame.YDirection());
            output << "},\"axialRange\":[" << part.AxialFirst << ',' << part.AxialLast
                << "],\"sectionWireCount\":" << part.SectionWires.size()
                << ",\"contourFaceIds\":[";
            for (std::size_t index = 0; index < part.ContourFaceIds.size(); ++index)
            {
                if (index) output << ',';
                output << part.ContourFaceIds[index];
            }
            output << "],\"features\":[";
            for (std::size_t index = 0; index < part.Features.size(); ++index)
            {
                if (index) output << ',';
                Feature(output, part.Features[index]);
            }
            output << "],\"diagnostics\":";
            Diagnostics(output, part.Diagnostics);
            output << '}';
        }

        std::ofstream OpenOutput(const std::filesystem::path& path)
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output)
                throw std::runtime_error("failed to open JSON output: " + path.string());
            output << std::setprecision(17);
            return output;
        }
    }

    void WriteAnalysisJson(
        const PartAnalysis& analysis,
        const std::filesystem::path& path)
    {
        auto output = OpenOutput(path);
        output << "{\"schema\":\"etp.analysis.v1\",\"parts\":[";
        Part(output, analysis);
        output << "]}\n";
    }

    void WriteAnalysisJson(
        const std::vector<PartAnalysis>& analyses,
        const std::filesystem::path& path)
    {
        auto output = OpenOutput(path);
        output << "{\"schema\":\"etp.analysis.v1\",\"parts\":[";
        for (std::size_t index = 0; index < analyses.size(); ++index)
        {
            if (index) output << ',';
            Part(output, analyses[index]);
        }
        output << "]}\n";
    }
}
