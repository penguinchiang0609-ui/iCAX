#include "SheetNesting.h"
#include "SheetNestingPlaneStrategies.h"
#include "NativeConstructionTraceWriter.h"
#ifdef ICAX_SHEET_NESTING_WITH_CP_SAT
#include "SheetNestingSheetCount.h"
#include "SheetNestingSheetCountRefill.h"
#include "NestingExecution.h"
#endif

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

// A task contains only original demand, stock and process settings.  There is
// deliberately no placement/seed input or case-specific source in this tool.
using namespace iCAX::SheetNesting;

namespace
{
    constexpr std::size_t MaxRows = 100000;
    constexpr std::size_t MaxGeometryPoints = 1000000;
    constexpr std::size_t MaxAngles = 4096;

#ifdef ICAX_SHEET_NESTING_WITH_CP_SAT
    struct ConstructionCapture
    {
        std::optional<Result> Output;
        double ActualBudgetSeconds = 0;
    };

    class CurrentTaskConstructor final : public IStrategy
    {
    public:
        explicit CurrentTaskConstructor(SheetCountSettings settings) : Settings(settings) {}

        Result Run(const Problem& input, const Options& options) override
        {
            const auto started = std::chrono::steady_clock::now();
            bool cancelled = false;
            const auto elapsed = [&]
                { return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(); };
            const auto pollCancel = [&]
            {
                if (!cancelled && options.Cancel && options.Cancel()) cancelled = true;
                return cancelled;
            };
            const auto stop = [&] { return pollCancel() || elapsed() >= options.TimeLimitSeconds; };
            Statistics work;
            work.PreparationSeconds = work.PlacementSeconds = work.VerificationSeconds = work.EvaluationSeconds = 0;
            const auto observe = [&](const Result& output)
            {
                work.PlacementAttempts += output.Stats.PlacementAttempts;
                work.CandidatePatterns += output.Stats.CandidatePatterns;
                work.PreparationSeconds += std::max(0., output.Stats.PreparationSeconds);
                work.PlacementSeconds += std::max(0., output.Stats.PlacementSeconds);
                work.VerificationSeconds += std::max(0., output.Stats.VerificationSeconds);
                work.EvaluationSeconds += std::max(0., output.Stats.EvaluationSeconds);
            };
            std::vector<std::string> notes;
            std::optional<JobResult> best;
            if (!stop())
            {
                auto initialOptions = options;
                initialOptions.EnableBasicCompaction = false;
                initialOptions.Cancel = pollCancel;
                auto initialSettings = Settings;
                initialSettings.StopAfterFirstComplete = true;
                JobResult initial;
                initial.Output = MakeSheetCountStrategy(initialSettings)()->Run(input, initialOptions);
                Detail::AssessCandidate(input, initial);
                observe(initial.Output);
                notes.insert(notes.end(), initial.Output.Diagnostics.begin(), initial.Output.Diagnostics.end());
                if (Detail::Eligible(initial)) best = std::move(initial);
            }
            if (!stop() && work.PlacementAttempts < options.MaxPlacementAttempts)
            {
                auto searchOptions = options;
                searchOptions.EnableBasicCompaction = false;
                searchOptions.TimeLimitSeconds = options.TimeLimitSeconds - elapsed();
                searchOptions.MaxPlacementAttempts -= work.PlacementAttempts;
                searchOptions.Cancel = pollCancel;
                auto factory = best && best->Checked.Complete
                    ? MakeSheetCountRepairStrategy({input, best->Output}, Settings)
                    : MakeSheetCountStrategy(Settings);
                JobResult candidate;
                candidate.Output = factory()->Run(input, searchOptions);
                Detail::AssessCandidate(input, candidate);
                observe(candidate.Output);
                notes.insert(notes.end(), candidate.Output.Diagnostics.begin(), candidate.Output.Diagnostics.end());
                if (Detail::Eligible(candidate) && (!best ||
                    Detail::Quality(candidate, input, options.Ranking) < Detail::Quality(*best, input, options.Ranking)))
                    best = std::move(candidate);
            }
            Result output;
            if (best) output = std::move(best->Output);
            else
            {
                output.State = Status::Partial;
                for (const auto& part : input.Parts)
                    if (part.Quantity) output.Remaining.push_back({part.ID, part.Quantity});
            }
            work.CombinationGroups = output.Stats.CombinationGroups;
            work.CircleLatticeGroups = output.Stats.CircleLatticeGroups;
            work.GapFilledParts = output.Stats.GapFilledParts;
            output.Stats = work;
            output.Stats.ElapsedSeconds = elapsed();
            if (cancelled) output.State = Status::Cancelled;
            notes.push_back("constructor.current-task: prior-layout-input=0; common-edge-objective=0; retained=validated-best; hint-source-policy=current-run-only");
            output.Diagnostics = std::move(notes);
            return output;
        }

    private:
        SheetCountSettings Settings;
    };

    class CaptureStrategy final : public IStrategy
    {
    public:
        CaptureStrategy(std::unique_ptr<IStrategy> inner, std::shared_ptr<ConstructionCapture> captured,
            double budgetFraction)
            : Inner(std::move(inner)), Captured(std::move(captured)), BudgetFraction(budgetFraction)
        {
            if (!Inner) throw std::runtime_error("Sheet-count strategy factory returned null");
        }

        Result Run(const Problem& input, const Options& options) override
        {
            Captured->Output.reset();
            auto constructionOptions = options;
            constructionOptions.TimeLimitSeconds *= BudgetFraction;
            Captured->ActualBudgetSeconds = constructionOptions.TimeLimitSeconds;
            auto result = Inner->Run(input, constructionOptions);
            Captured->Output = result;
            return result;
        }

        bool OwnsRefinementLifecycle() const noexcept override
        {
            return Inner->OwnsRefinementLifecycle();
        }

    private:
        std::unique_ptr<IStrategy> Inner;
        std::shared_ptr<ConstructionCapture> Captured;
        double BudgetFraction;
    };
#endif

    std::string Token(std::istream& in, const char* field)
    {
        std::string value;
        if (!(in >> value)) throw std::runtime_error(std::string("Missing or invalid ") + field);
        return value;
    }

    std::size_t Count(std::istream& in, const char* field, std::size_t limit)
    {
        const auto text = Token(in, field);
        std::uint64_t value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value > limit)
            throw std::runtime_error(std::string("Invalid or excessive ") + field);
        return static_cast<std::size_t>(value);
    }

    bool Boolean(std::istream& in, const char* field)
    {
        const auto value = Token(in, field);
        if (value == "0") return false;
        if (value == "1") return true;
        throw std::runtime_error(std::string(field) + " must be 0 or 1");
    }

    double Real(std::istream& in, const char* field)
    {
        const auto text = Token(in, field);
        std::istringstream parser(text);
        parser.imbue(std::locale::classic());
        double value = 0;
        if (!(parser >> value) || !parser.eof() || !std::isfinite(value))
            throw std::runtime_error(std::string("Invalid finite number for ") + field);
        return value;
    }

    std::string Quoted(std::istream& in, const char* field)
    {
        in >> std::ws;
        if (in.peek() != '"') throw std::runtime_error(std::string(field) + " must be quoted");
        std::string value;
        if (!(in >> std::quoted(value))) throw std::runtime_error(std::string("Invalid ") + field);
        return value;
    }

    Problem ReadProblem(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        in.imbue(std::locale::classic());
        if (!in) throw std::runtime_error("Cannot open task input");
        const auto version = Token(in, "task version");
        const bool mirrorPermissions = version == "ICAX_NESTING_PROBLEM_4";
        const bool ringEligibility = version == "ICAX_NESTING_PROBLEM_3" || mirrorPermissions;
        if (!ringEligibility && version != "ICAX_NESTING_PROBLEM_2")
            throw std::runtime_error("Expected ICAX_NESTING_PROBLEM_2, ICAX_NESTING_PROBLEM_3 or ICAX_NESTING_PROBLEM_4");
        const auto parts = Count(in, "part row count", MaxRows);
        const auto stocks = Count(in, "stock row count", MaxRows);
        Problem problem;
        problem.PartGap = Real(in, "part gap");
        problem.EdgeMargin = Real(in, "edge margin");
        problem.Tolerance = Real(in, "tolerance");
        problem.AllowCommonEdges = Boolean(in, "AllowCommonEdges");
        std::size_t pointCount = 0;
        const auto ring = [&](std::size_t count)
        {
            if (count > MaxGeometryPoints - pointCount)
                throw std::runtime_error("Task exceeds the geometry point resource limit");
            pointCount += count;
            std::vector<Point> points(count);
            for (auto& point : points)
            {
                point.X = Real(in, "point X");
                point.Y = Real(in, "point Y");
            }
            return points;
        };
        problem.Parts.reserve(parts);
        for (std::size_t i = 0; i < parts; ++i)
        {
            Part part;
            part.ID = Quoted(in, "part ID");
            part.Quantity = Count(in, "part quantity", MaxRows);
            part.Material = Quoted(in, "part material");
            part.FreeRotation = Boolean(in, "free rotation");
            if(mirrorPermissions)part.AllowMirror=Boolean(in,"allow mirror");
            const auto angles = Count(in, "allowed angle count", MaxAngles);
            part.AllowedAnglesDegrees.reserve(angles);
            for (std::size_t j = 0; j < angles; ++j)
                part.AllowedAnglesDegrees.push_back(Real(in, "allowed angle"));
            part.Geometry.Radius = Real(in, "circle radius");
            part.Geometry.Center.X = Real(in, "circle center X");
            part.Geometry.Center.Y = Real(in, "circle center Y");
            const auto outer = Count(in, "outer point count", MaxGeometryPoints);
            const auto holes = Count(in, "hole count", MaxRows);
            part.Geometry.Outer = ring(outer);
            part.Geometry.Holes.reserve(holes);
            for (std::size_t j = 0; j < holes; ++j)
                part.Geometry.Holes.push_back(ring(Count(in, "hole point count", MaxGeometryPoints)));
            if (ringEligibility)
            {
                const auto flags = Count(in, "common-edge ring count", holes + 1);
                for (std::size_t j = 0; j < flags; ++j)
                    part.Geometry.CommonEdgeRings.push_back(Boolean(in, "common-edge ring eligibility"));
            }
            problem.Parts.push_back(std::move(part));
        }
        problem.Stocks.reserve(stocks);
        for (std::size_t i = 0; i < stocks; ++i)
        {
            Stock stock;
            stock.ID = Quoted(in, "stock ID");
            stock.Width = Real(in, "stock width");
            stock.Height = Real(in, "stock height");
            stock.Quantity = Count(in, "stock quantity", MaxRows);
            stock.Cost = Real(in, "stock cost");
            stock.Material = Quoted(in, "stock material");
            problem.Stocks.push_back(std::move(stock));
        }
        in >> std::ws;
        if (!in.eof()) throw std::runtime_error("Unexpected trailing task data");

        // The public validator owns geometry, stock and demand validity.  An
        // empty layout conserving the entire demand validates the input alone.
        Result unplaced;
        unplaced.State = Status::Partial;
        for (const auto& part : problem.Parts)
            if (part.Quantity) unplaced.Remaining.push_back({part.ID, part.Quantity});
        const auto checked = Validate(problem, unplaced);
        if (!checked.Valid)
        {
            std::ostringstream message;
            message << "Invalid task input";
            for (const auto& error : checked.Errors) message << "; " << error;
            throw std::runtime_error(message.str());
        }
        return problem;
    }

    void Text(std::ostream& out, const std::string& text)
    {
        static constexpr char digits[] = "0123456789abcdef";
        out << '"';
        for (const unsigned char c : text)
        {
            switch (c)
            {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) out << "\\u00" << digits[c >> 4] << digits[c & 15];
                else out << static_cast<char>(c);
            }
        }
        out << '"';
    }

    void Number(std::ostream& out, double value)
    {
        if (std::isfinite(value)) out << value;
        else out << "null";
    }

    void Strings(std::ostream& out, const std::vector<std::string>& values)
    {
        out << '[';
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (i) out << ',';
            Text(out, values[i]);
        }
        out << ']';
    }

    void Points(std::ostream& out, const std::vector<Point>& points)
    {
        out << '[';
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            if (i) out << ',';
            out << '['; Number(out, points[i].X); out << ','; Number(out, points[i].Y); out << ']';
        }
        out << ']';
    }

    void ProblemJson(std::ostream& out, const Problem& problem)
    {
        out << "{\"partGap\":" << problem.PartGap << ",\"edgeMargin\":" << problem.EdgeMargin
            << ",\"tolerance\":" << problem.Tolerance << ",\"allowCommonEdges\":" << problem.AllowCommonEdges
            << ",\"parts\":[";
        for (std::size_t i = 0; i < problem.Parts.size(); ++i)
        {
            if (i) out << ',';
            const auto& part = problem.Parts[i];
            out << "{\"id\":"; Text(out, part.ID);
            out << ",\"quantity\":" << part.Quantity << ",\"material\":"; Text(out, part.Material);
            out << ",\"freeRotation\":" << part.FreeRotation << ",\"allowMirror\":" << part.AllowMirror << ",\"allowedAngles\":[";
            for (std::size_t j = 0; j < part.AllowedAnglesDegrees.size(); ++j)
            {
                if (j) out << ',';
                out << part.AllowedAnglesDegrees[j];
            }
            out << "],\"geometry\":{\"radius\":" << part.Geometry.Radius << ",\"center\":["
                << part.Geometry.Center.X << ',' << part.Geometry.Center.Y << "],\"outer\":";
            Points(out, part.Geometry.Outer);
            out << ",\"holes\":[";
            for (std::size_t j = 0; j < part.Geometry.Holes.size(); ++j)
            {
                if (j) out << ',';
                Points(out, part.Geometry.Holes[j]);
            }
            out << "],\"commonEdgeRings\":[";
            for (std::size_t j = 0; j < part.Geometry.CommonEdgeRings.size(); ++j)
            {
                if (j) out << ',';
                out << (part.Geometry.CommonEdgeRings[j] ? "true" : "false");
            }
            out << "]}}";
        }
        out << "],\"stocks\":[";
        for (std::size_t i = 0; i < problem.Stocks.size(); ++i)
        {
            if (i) out << ',';
            const auto& stock = problem.Stocks[i];
            out << "{\"id\":"; Text(out, stock.ID);
            out << ",\"width\":" << stock.Width << ",\"height\":" << stock.Height << ",\"quantity\":"
                << stock.Quantity << ",\"cost\":" << stock.Cost << ",\"material\":"; Text(out, stock.Material);
            out << '}';
        }
        out << "]}";
    }

    const char* StatusName(Status status)
    {
        switch (status)
        {
        case Status::Complete: return "complete";
        case Status::Partial: return "partial";
        case Status::InvalidInput: return "invalid-input";
        case Status::Failed: return "failed";
        case Status::Cancelled: return "cancelled";
        }
        return "unknown";
    }

    RankingPolicy RankingArgument(const std::string& value)
    {
        if (value == "utilization") return RankingPolicy::Utilization;
        if (value == "regular-remnant") return RankingPolicy::RegularRemnant;
        if (value == "low-left") return RankingPolicy::LowThenLeft;
        if (value == "compact-area") return RankingPolicy::CompactArea;
        throw std::runtime_error("ranking must be utilization, regular-remnant, low-left or compact-area");
    }

    const char* RankingName(RankingPolicy ranking)
    {
        switch (ranking)
        {
        case RankingPolicy::Utilization: return "utilization";
        case RankingPolicy::RegularRemnant: return "regular-remnant";
        case RankingPolicy::LowThenLeft: return "low-left";
        case RankingPolicy::CompactArea: return "compact-area";
        }
        throw std::runtime_error("Unknown ranking policy");
    }

    void StatisticsJson(std::ostream& out, const Statistics& stats)
    {
        out << "{\"candidatePatterns\":" << stats.CandidatePatterns << ",\"combinationGroups\":" << stats.CombinationGroups
            << ",\"circleLatticeGroups\":" << stats.CircleLatticeGroups << ",\"gapFilledParts\":" << stats.GapFilledParts
            << ",\"placementAttempts\":" << stats.PlacementAttempts << ",\"elapsedSeconds\":";
        Number(out, stats.ElapsedSeconds);
        out << ",\"preparationSeconds\":"; Number(out, stats.PreparationSeconds);
        out << ",\"placementSeconds\":"; Number(out, stats.PlacementSeconds);
        out << ",\"verificationSeconds\":"; Number(out, stats.VerificationSeconds);
        out << ",\"evaluationSeconds\":"; Number(out, stats.EvaluationSeconds);
        out << ",\"postProcessingSeconds\":"; Number(out, stats.PostProcessingSeconds);
        out << '}';
    }

    void ValidationJson(std::ostream& out, const Validation& checked)
    {
        out << "{\"valid\":" << checked.Valid << ",\"complete\":" << checked.Complete
            << ",\"placed\":" << checked.PlacedCount << ",\"materialCost\":"; Number(out, checked.MaterialCost);
        out << ",\"materialArea\":"; Number(out, checked.MaterialArea);
        out << ",\"sheetArea\":"; Number(out, checked.SheetArea);
        out << ",\"usedEnvelopeArea\":"; Number(out, checked.UsedEnvelopeArea);
        out << ",\"errors\":"; Strings(out, checked.Errors); out << '}';
    }

    void RemnantsJson(std::ostream& out, const RemnantEvaluation& value)
    {
        out << "{\"measured\":" << value.Measured << ",\"lastSheetFreeArea\":";
        Number(out,value.LastSheetFreeArea);
        out << ",\"lastSheetRegularArea\":"; Number(out,value.LastSheetRegularArea);
        out << ",\"lastSheetAreaUpperBound\":"; Number(out,value.LastSheetAreaUpperBound);
        out << ",\"finalLayoutIndices\":[";
        for(std::size_t i=0;i<value.FinalLayoutIndices.size();++i)
        {if(i)out << ',';out << value.FinalLayoutIndices[i];}
        out << "],\"perLayout\":[";
        for(std::size_t i=0;i<value.PerLayout.size();++i)
        {
            if(i)out << ',';
            const auto& item=value.PerLayout[i];
            out << "{\"verified\":" << item.Verified << ",\"area\":"; Number(out,item.Area);
            out << ",\"areaUpperBound\":"; Number(out,item.AreaUpperBound);
            out << ",\"resolution\":" << item.Resolution << ",\"bounds\":{\"x\":";
            Number(out,item.Bounds.X);out << ",\"y\":";Number(out,item.Bounds.Y);
            out << ",\"width\":";Number(out,item.Bounds.Width);
            out << ",\"height\":";Number(out,item.Bounds.Height);out << "}}";
        }
        out << "]}";
    }

    void ResultJson(std::ostream& out, const JobResult& job)
    {
        const auto& result = job.Output;
        std::size_t remaining = 0;
        for (const auto& part : result.Remaining) remaining += part.Quantity;
        out << "{\"state\":" << static_cast<int>(result.State) << ",\"status\":"; Text(out, StatusName(result.State));
        out << ",\"valid\":" << job.Checked.Valid << ",\"complete\":" << job.Checked.Complete
            << ",\"placed\":" << job.Checked.PlacedCount << ",\"remainingCount\":" << remaining
            << ",\"boards\":" << result.Layouts.size() << ",\"geometryValidated\":" << result.GeometryValidated
            << ",\"layouts\":[";
        for (std::size_t i = 0; i < result.Layouts.size(); ++i)
        {
            if (i) out << ',';
            const auto& layout = result.Layouts[i];
            out << "{\"stock\":"; Text(out, layout.StockID);
            out << ",\"instance\":" << layout.StockInstance << ",\"width\":"; Number(out, layout.Width);
            out << ",\"height\":"; Number(out, layout.Height);
            out << ",\"materialArea\":"; Number(out, layout.MaterialArea);
            out << ",\"placements\":[";
            for (std::size_t j = 0; j < layout.Placements.size(); ++j)
            {
                if (j) out << ',';
                const auto& placed = layout.Placements[j];
                out << "{\"part\":"; Text(out, placed.PartID);
                out << ",\"instance\":" << placed.Instance << ",\"x\":"; Number(out, placed.Transform.X);
                out << ",\"y\":"; Number(out, placed.Transform.Y);
                out << ",\"angle\":"; Number(out, placed.Transform.AngleDegrees);
                out << ",\"mirrored\":" << placed.Transform.Mirrored;
                out << ",\"group\":" << placed.Group << ",\"gapFiller\":" << placed.GapFiller << '}';
            }
            out << "]}";
        }
        out << "],\"remaining\":[";
        for (std::size_t i = 0; i < result.Remaining.size(); ++i)
        {
            if (i) out << ',';
            out << "{\"part\":"; Text(out, result.Remaining[i].PartID);
            out << ",\"quantity\":" << result.Remaining[i].Quantity << '}';
        }
        out << "],\"diagnostics\":"; Strings(out, result.Diagnostics);
        out << ",\"stats\":"; StatisticsJson(out, result.Stats);
        out << ",\"remnants\":"; RemnantsJson(out,job.Remnants);out << '}';
    }

    double PositiveArgument(const char* text, const char* field)
    {
        std::istringstream in(text);
        const double value = Real(in, field);
        in >> std::ws;
        if (value <= 0 || !in.eof()) throw std::runtime_error(std::string(field) + " must be positive");
        return value;
    }

    bool BooleanArgument(const char* text, const char* field)
    {
        std::istringstream option(text);
        const bool value = Boolean(option, field);
        option >> std::ws;
        if (!option.eof()) throw std::runtime_error(std::string("Unexpected trailing ") + field + " configuration");
        return value;
    }

    void WriteNew(const std::filesystem::path& path, const std::string& data)
    {
#ifdef _WIN32
        std::FILE* raw = _wfopen(path.c_str(), L"wbx");
#else
        std::FILE* raw = std::fopen(path.c_str(), "wbx");
#endif
        if (!raw) throw std::runtime_error("Cannot create output exclusively (it may already exist)");
        const std::unique_ptr<std::FILE, decltype(&std::fclose)> file(raw, &std::fclose);
        if (std::fwrite(data.data(), 1, data.size(), file.get()) != data.size() || std::fflush(file.get()) != 0)
            throw std::runtime_error("Cannot write complete task output");
    }

    std::size_t PositiveCountArgument(const char* text, const char* field, std::size_t limit)
    {
        std::istringstream in(text);
        const auto value = Count(in, field, limit);
        in >> std::ws;
        if (!value || !in.eof()) throw std::runtime_error(std::string(field) + " must be a positive integer");
        return value;
    }

    int RunInitial(int argc, char** argv)
    {
        if (argc < 7 || (argc - 7) % 2)
            throw std::runtime_error("usage: SheetNestingTask --initial <problem> <output.json> <seconds> <attempts> <workers=1..32> [--trace <absolute.trace-index.json>] [--constructor <registered-pure-constructor>]");
        std::string traceArgument, constructorArgument;
        for (int i = 7; i < argc; i += 2)
        {
            const std::string flag = argv[i];
            if (flag == "--trace" && traceArgument.empty()) traceArgument = argv[i+1];
            else if (flag == "--constructor" && constructorArgument.empty()) constructorArgument = argv[i+1];
            else throw std::runtime_error("Unknown or duplicate initial-construction option: " + flag);
        }
        const std::filesystem::path output = argv[3];
        if (std::filesystem::exists(output)) throw std::runtime_error("Refusing to overwrite task output");
        Options options;
        options.TimeLimitSeconds = PositiveArgument(argv[4], "seconds");
        options.MaxPlacementAttempts = PositiveCountArgument(argv[5], "attempts", std::numeric_limits<std::size_t>::max());
        options.EnableBasicCompaction = false;
        ParallelConstructionSettings settings;
        settings.Workers = PositiveCountArgument(argv[6], "workers", 32);
        settings.FairScheduling = true;
        const auto problem = ReadProblem(argv[2]);
        const auto started = std::chrono::steady_clock::now();
        std::shared_ptr<Tools::NativeConstructionTraceWriter> trace;
        if (!traceArgument.empty())
        {
            const std::filesystem::path index = traceArgument;
            auto expected = std::filesystem::absolute(output).lexically_normal();
            expected.replace_extension(".trace-index.json");
            if (index.lexically_normal() != expected)
                throw std::runtime_error("--trace must name the absolute .trace-index.json adjacent to the matching result");
            options.ConstructionObservation = std::make_shared<ConstructionTraceHealth>();
            options.ConstructionTraceRoute = "plane.initial";
            trace = std::make_shared<Tools::NativeConstructionTraceWriter>(index, output, problem, options, ProblemJson);
            options.ConstructionTrace = [trace](const ConstructionTraceEvent& event) { trace->Record(event); };
        }
        const auto traceMetadata = [&](std::ostream& stream)
        {
            if (!trace) return;
            stream << ",\"nativeConstructionTrace\":"; Text(stream, trace->IndexPath().filename().string());
            stream << ",\"instrumentedTiming\":true,\"constructionObservation\":"; trace->StatusJson(stream);
        };
        double firstCompleteSeconds = -1;
        auto incumbentFile = output;
        incumbentFile.replace_extension(".incumbent.json");
        settings.OnImprovement = [&](const JobResult& incumbent)
        {
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            if (firstCompleteSeconds < 0) firstCompleteSeconds = elapsed;
            std::ostringstream snapshot;
            snapshot.imbue(std::locale::classic());
            snapshot << std::setprecision(std::numeric_limits<double>::max_digits10) << std::boolalpha;
            snapshot << "{\"format\":\"ICAX_NESTING_TASK_RESULT_1\",\"strategy\":\"plane.initial\",\"live\":true,\"firstCompleteWallSeconds\":"
                << firstCompleteSeconds << ",\"wholeJobWallSeconds\":" << elapsed;
            traceMetadata(snapshot);
            snapshot << ",\"settings\":{\"timeLimitSeconds\":" << options.TimeLimitSeconds
                << ",\"maxPlacementAttempts\":" << options.MaxPlacementAttempts << ",\"workers\":" << settings.Workers
                << ",\"enableBasicCompaction\":false,\"refill\":false,\"externalLayoutSeed\":false},\"problem\":";
            ProblemJson(snapshot, problem);
            snapshot << ",\"result\":"; ResultJson(snapshot, incumbent);
            snapshot << ",\"validation\":"; ValidationJson(snapshot, incumbent.Checked); snapshot << "}\n";
            auto staging = incumbentFile; staging += ".tmp";
            WriteNew(staging, snapshot.str());
            std::filesystem::rename(staging, incumbentFile);
        };
        StrategyRegistry registry;
        if (constructorArgument.empty()) registry.Register("plane.initial", MakePlaneInitialFactory(settings));
        else
        {
            // Isolate an existing production constructor for mechanism and
            // timing diagnosis. The original problem, executor, validation,
            // observation and final selection remain the normal code path.
            StrategyRegistry constructors; RegisterPlaneStrategies(constructors);
            auto selected = constructors.Create(constructorArgument);
            if (constructorArgument == "plane.initial" || !selected || !selected->IsPureConstruction())
                throw std::runtime_error("--constructor requires one registered pure initial constructor");
            registry.Register("plane.initial", MakeParallelConstructionPortfolioFactory(constructors,{constructorArgument},settings));
        }
        auto job = RunJob({"task", "plane.initial", problem, options}, registry);
        if (trace && !trace->Finish(true))
            job.Output.Diagnostics.push_back("Construction observation failed; original validated solver result retained.");
        const auto wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(std::numeric_limits<double>::max_digits10) << std::boolalpha;
        out << "{\"format\":\"ICAX_NESTING_TASK_RESULT_1\",\"strategy\":\"plane.initial\"";
        traceMetadata(out);
        out << ",\"settings\":{\"timeLimitSeconds\":"
            << options.TimeLimitSeconds << ",\"maxPlacementAttempts\":" << options.MaxPlacementAttempts
            << ",\"workers\":" << settings.Workers << ",\"fairScheduling\":" << settings.FairScheduling
            << ",\"enableBasicCompaction\":" << options.EnableBasicCompaction
            << ",\"seed\":" << options.Seed
            << ",\"constructor\":\"production-plane-initial\",\"refill\":false,\"externalLayoutSeed\":false,\"ranking\":";
        Text(out, RankingName(options.Ranking));
        out << ",\"isolatedConstructor\":"; Text(out, constructorArgument);
        out << "},\"wholeJobWallSeconds\":" << wall << ",\"firstCompleteWallSeconds\":" << firstCompleteSeconds << ",\"problem\":";
        ProblemJson(out, problem);
        out << ",\"result\":"; ResultJson(out, job);
        out << ",\"validation\":"; ValidationJson(out, job.Checked); out << "}\n";
        WriteNew(output, out.str());
        std::cout << "status=" << StatusName(job.Output.State) << " valid=" << std::boolalpha << job.Checked.Valid
            << " complete=" << job.Checked.Complete << " placed=" << job.Checked.PlacedCount
            << " boards=" << job.Output.Layouts.size() << " wall=" << wall << '\n';
        return 0;
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc >= 2 && std::string(argv[1]) == "--initial") return RunInitial(argc, argv);
#ifdef ICAX_SHEET_NESTING_WITH_CP_SAT
        if (argc < 4 || argc > 9)
            throw std::runtime_error("usage: SheetNestingTask <problem> <output.json> <seconds> [secondsPerBoardCount] [interiorRectangles=0|1] [ranking=utilization|regular-remnant|low-left|compact-area] [refill=0|1] [workers=1..64]");
        const std::filesystem::path output = argv[2];
        if (std::filesystem::exists(output)) throw std::runtime_error("Refusing to overwrite task output");
        Options options;
        options.TimeLimitSeconds = PositiveArgument(argv[3], "seconds");
        SheetCountSettings settings;
        if (argc >= 5) settings.SecondsPerBoardCount = PositiveArgument(argv[4], "secondsPerBoardCount");
        if (argc >= 6)
            settings.EnableInteriorRectangles = BooleanArgument(argv[5], "interiorRectangles");
        if (argc >= 7) options.Ranking = RankingArgument(argv[6]);
        const bool refill = argc < 8 || BooleanArgument(argv[7], "refill");
        if (argc >= 9)
        {
            const double workers = PositiveArgument(argv[8], "workers");
            if (workers > 64 || std::floor(workers) != workers)
                throw std::runtime_error("workers must be an integer between 1 and 64");
            settings.Workers = static_cast<int>(workers);
        }
        const double constructorBudgetFraction = refill ? .5 : 1.;
        SheetCountRefillSettings refillSettings;
        refillSettings.Solver = settings;
        refillSettings.MaxWindowBoards = 4;
        refillSettings.MaxReleasedInstances = 32;
        refillSettings.SecondsPerWindow = 10;
        refillSettings.MaxPlans = 8;
        refillSettings.EnableDonorTransfers = true;
        refillSettings.EnableExchanges = true;
        const auto problem = ReadProblem(argv[1]);
        const auto captured = std::make_shared<ConstructionCapture>();
        const StrategyFactory constructor = [settings] { return std::make_unique<CurrentTaskConstructor>(settings); };
        StrategyFactory factory = [constructor, captured, constructorBudgetFraction]
        {
            return std::make_unique<CaptureStrategy>(constructor(), captured, constructorBudgetFraction);
        };
        if (refill)
        {
            StrategyPipeline pipeline;
            pipeline.Construct = std::move(factory);
            pipeline.PostProcessors.push_back(MakeSheetCountRefillStage(refillSettings));
            factory = MakePipelineFactory(std::move(pipeline));
        }
        StrategyRegistry registry;
        registry.Register("sheet-count", std::move(factory));
        const auto started = std::chrono::steady_clock::now();
        const auto job = RunJob({"task", "sheet-count", problem, options}, registry);
        const auto wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::optional<JobResult> construction;
        if (captured->Output)
        {
            construction.emplace();
            construction->ID = "task-construction";
            construction->StrategyID = "sheet-count";
            construction->Output = std::move(captured->Output.value());
            construction->Checked = Validate(problem, construction->Output);
            construction->Output.GeometryValidated = construction->Checked.Valid;
        }
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(std::numeric_limits<double>::max_digits10) << std::boolalpha;
        out << "{\"format\":\"ICAX_NESTING_TASK_RESULT_1\",\"strategy\":\"sheet-count\",\"settings\":{\"timeLimitSeconds\":"
            << options.TimeLimitSeconds << ",\"secondsPerBoardCount\":" << settings.SecondsPerBoardCount
            << ",\"coordinateScale\":" << settings.CoordinateScale << ",\"workers\":" << settings.Workers
            << ",\"maxConflicts\":" << settings.MaxConflicts << ",\"maxInstances\":" << settings.MaxInstances
            << ",\"enableInteriorRectangles\":" << settings.EnableInteriorRectangles
            << ",\"enableBasicCompaction\":" << options.EnableBasicCompaction
            << ",\"basicCompactionBudgetFraction\":" << options.BasicCompactionBudgetFraction
            << ",\"seed\":" << options.Seed << ",\"maxPlacementAttempts\":" << options.MaxPlacementAttempts
            << ",\"constructor\":\"first-complete-then-current-task-guided-sheet-count\",\"ranking\":";
        Text(out, RankingName(options.Ranking));
        out << ",\"refill\":" << refill << ",\"constructorBudgetFraction\":" << constructorBudgetFraction
            << ",\"actualConstructionBudgetSeconds\":" << captured->ActualBudgetSeconds
            << ",\"refillSettings\":{\"minWindowBoards\":" << refillSettings.MinWindowBoards
            << ",\"maxWindowBoards\":" << refillSettings.MaxWindowBoards
            << ",\"maxReleasedInstances\":" << refillSettings.MaxReleasedInstances
            << ",\"secondsPerWindow\":" << refillSettings.SecondsPerWindow
            << ",\"maxPlans\":" << refillSettings.MaxPlans
            << ",\"enableDonorTransfers\":" << refillSettings.EnableDonorTransfers
            << ",\"maxTransferPlans\":" << refillSettings.MaxTransferPlans
            << ",\"secondsPerTransfer\":" << refillSettings.SecondsPerTransfer
            << ",\"enableExchanges\":" << refillSettings.EnableExchanges
            << ",\"maxPerturbations\":" << refillSettings.MaxPerturbations
            << ",\"secondsPerPerturbation\":" << refillSettings.SecondsPerPerturbation
            << ",\"maxStagnation\":" << refillSettings.MaxStagnation
            << ",\"planQuotaScope\":";
        Text(out, refillSettings.EnableExchanges ? "per-reduction-phase" : "stage");
        out
            << ",\"solverUsesConstructionSettings\":true}}"
            << ",\"wholeJobWallSeconds\":" << wall << ",\"problem\":";
        ProblemJson(out, problem);
        out << ",\"constructionResult\":";
        if (construction) ResultJson(out, *construction);
        else out << "null";
        out << ",\"constructionValidation\":";
        if (construction) ValidationJson(out, construction->Checked);
        else out << "null";
        out << ",\"result\":"; ResultJson(out, job);
        out << ",\"validation\":"; ValidationJson(out, job.Checked); out << "}\n";
        WriteNew(output, out.str());
        std::cout << "status=" << StatusName(job.Output.State) << " valid=" << std::boolalpha << job.Checked.Valid
            << " complete=" << job.Checked.Complete << " placed=" << job.Checked.PlacedCount
            << " boards=" << job.Output.Layouts.size() << " wall=" << wall << '\n';
        // An executed task may legitimately be partial or unsupported.  Those
        // outcomes are explicit in JSON, rather than confused with CLI failure.
        return 0;
#else
        throw std::runtime_error("usage: SheetNestingTask --initial <problem> <output.json> <seconds> <attempts> <workers=1..32> [--trace <absolute.trace-index.json>]; the legacy sheet-count mode requires ICAX_SHEET_NESTING_WITH_CP_SAT");
#endif
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
