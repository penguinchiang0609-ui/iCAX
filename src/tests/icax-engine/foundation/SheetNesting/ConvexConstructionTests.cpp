#include "SheetNestingConvexConstruction.h"
#include "SheetNestingPipeline.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
    int Passed = 0, Failed = 0;
    void Require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }
    void Test(const char* name, const std::function<void()>& body)
    {
        try { body(); ++Passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++Failed; std::cerr << "FAIL " << name << " " << error.what() << '\n'; }
    }
    Part Polygon(std::string id, std::vector<Point> ring)
    {
        Part part; part.ID = std::move(id); part.Geometry.Outer = std::move(ring);
        part.Material = "original-material"; part.FreeRotation = false;
        part.AllowedAnglesDegrees = {0, 90, 180, 270};
        return part;
    }
    // Original contours, counts and fixed angle domains from NDTM Three_90
    // and Fu_6_90. Only published scalar target widths are used.
    // https://github.com/gfpantoja/NDTM/tree/7d935e38ce390b047c074e47e97ca99ce32b2bb7/Instances
    Problem Three()
    {
        Problem problem; problem.Tolerance = 1e-7;
        problem.Stocks = {{"original-sheet", 5.4, 7, 1, 1, "original-material"}};
        problem.Parts = {
            Polygon("diamond", {{2, -2}, {0, 0}, {2, 2}, {4, 0}}),
            Polygon("square", {{0, -3}, {0, 0}, {3, 0}, {3, -3}}),
            Polygon("triangle", {{0, 0}, {2, 3}, {4, 0}})
        };
        return problem;
    }
    Problem FuSix()
    {
        Problem problem; problem.Tolerance = 1e-7;
        problem.Stocks = {{"original-sheet", 19, 38, 1, 1, "original-material"}};
        problem.Parts = {
            Polygon("square-a", {{0, 0}, {0, 10}, {10, 10}, {10, 0}}),
            Polygon("square-b", {{0, 0}, {0, 10}, {10, 10}, {10, 0}}),
            Polygon("rectangle", {{0, 0}, {0, 9}, {14, 9}, {14, 0}}),
            Polygon("triangle", {{0, 0}, {7, 7}, {14, 0}}),
            Polygon("right-triangle", {{0, -9}, {0, 0}, {14, 0}}),
            Polygon("square-c", {{0, 0}, {0, 14}, {14, 14}, {14, 0}})
        };
        return problem;
    }
    Options Budget()
    {
        Options options; options.TimeLimitSeconds = 4; options.MaxPlacementAttempts = 200000;
        options.EnableBasicCompaction = false;
        return options;
    }
    std::string Fingerprint(const Problem& input)
    {
        std::ostringstream text; text << std::setprecision(17);
        text << input.PartGap << ',' << input.EdgeMargin << ',' << input.Tolerance << ',' << input.AllowCommonEdges;
        for (const auto& part : input.Parts)
        {
            text << part.ID << ',' << part.Quantity << ',' << part.Material << ',' << part.FreeRotation;
            for (const auto angle : part.AllowedAnglesDegrees) text << ',' << angle;
            text << "outer";
            for (const auto point : part.Geometry.Outer) text << ',' << point.X << ',' << point.Y;
            text << "holes";
            for (const auto& hole : part.Geometry.Holes)
            {
                text << "ring";
                for (const auto point : hole) text << ',' << point.X << ',' << point.Y;
            }
            text << ',' << part.Geometry.Radius << ',' << part.Geometry.Center.X << ',' << part.Geometry.Center.Y;
        }
        for (const auto& stock : input.Stocks)
            text << stock.ID << ',' << stock.Width << ',' << stock.Height << ',' << stock.Quantity << ',' << stock.Cost << ',' << stock.Material;
        return text.str();
    }
    JobResult Solve(const Problem& input, const Options& options = Budget())
    {
        const auto source = Fingerprint(input);
        StrategyRegistry registry; registry.Register("joint", MakeConvexConstructionFactory());
        auto result = RunJob({"original-order", "joint", input, options}, registry);
        Require(source == Fingerprint(input), "construction changed the original input");
        return result;
    }
    void EngineeringValid(const Problem& input, const JobResult& job)
    {
        const auto checked = Validate(input, job.Output);
        Require(checked.Valid && job.Checked.Valid, "ordinary original-input validation failed");
        Require(checked.Complete == job.Checked.Complete && checked.PlacedCount == job.Checked.PlacedCount,
            "job assessment differs from original validation");
    }
    void ExactGeometry(const Problem& input, const Result& result)
    {
        for (const auto& layout : result.Layouts)
        {
            std::vector<AxisSafety::Geometry> geometries;
            for (const auto& placement : layout.Placements)
            {
                const auto part = std::find_if(input.Parts.begin(), input.Parts.end(), [&](const Part& source)
                    { return source.ID == placement.PartID; });
                Require(part != input.Parts.end(), "exact audit found an unknown original part");
                const auto actual = AxisSafety::Make(part->Geometry, placement.Transform);
                Require(AxisSafety::Inside(actual, layout.Width, layout.Height, input.EdgeMargin),
                    "actual-world geometry crossed the original sheet");
                for (const auto& other : geometries)
                    Require(AxisSafety::PairExact(actual, other, input.PartGap, input.AllowCommonEdges, input.Tolerance) ==
                        AxisSafety::Verdict::Safe, "exact material audit found overlap or uncertain clearance");
                geometries.push_back(actual);
            }
        }
    }
    double DiagnosticField(const Result& result, const std::string& field)
    {
        for (const auto& note : result.Diagnostics)
            if (const auto position = note.find(field + '='); position != std::string::npos)
                return std::stod(note.substr(position + field.size() + 1));
        throw std::runtime_error("missing construction diagnostic " + field);
    }
    bool HasDiagnostic(const Result& result, const std::string& text)
    {
        return std::any_of(result.Diagnostics.begin(), result.Diagnostics.end(), [&](const auto& note)
            { return note.find(text) != std::string::npos; });
    }
    class EmptyConstructor final : public IStrategy
    {
    public:
        Result Run(const Problem& input, const Options& options) override
        {
            Require(!options.EnableBasicCompaction, "competing constructor received automatic refinement");
            Result result; result.State = Status::Partial;
            for (const auto& part : input.Parts) if (part.Quantity) result.Remaining.push_back({part.ID, part.Quantity});
            return result;
        }
    };
    class InspectRefinement final : public IPostProcessor
    {
        int& Calls;
    public:
        explicit InspectRefinement(int& calls) : Calls(calls) {}
        Result Run(const Problem& input, const Result& incumbent, const Options&) override
        {
            ++Calls;
            Require(Validate(input, incumbent).Complete, "incomplete construction entered refinement");
            Require(HasDiagnostic(incumbent, "selected=joint"), "refinement received a losing construction");
            auto result = incumbent; result.Stats = {};
            return result;
        }
    };
}

int main()
{
    Test("independent construction completes original Three90 from empty input", []
    {
        const auto input = Three(); const auto job = Solve(input);
        EngineeringValid(input, job); ExactGeometry(input, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 3, "original Three90 remains incomplete");
        Require(job.Output.Stats.PlacementAttempts <= 200000, "constructor exceeded attempts");
    });
    Test("independent construction completes original Fu6_90 from empty input", []
    {
        const auto input = FuSix(); const auto job = Solve(input);
        EngineeringValid(input, job); ExactGeometry(input, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 6, "original Fu6_90 remains incomplete");
    });
    Test("finite separation retries complete original Fu6_0 and Fu6_180", []
    {
        for (const int turn : {0, 180})
        {
            auto input = FuSix(); input.Stocks[0].Width = 23;
            for (auto& part : input.Parts)
                part.AllowedAnglesDegrees = turn == 0 ? std::vector<double>{0} : std::vector<double>{0, 180};
            const auto job = Solve(input);
            EngineeringValid(input, job); ExactGeometry(input, job.Output);
            Require(job.Checked.Complete && job.Checked.PlacedCount == 6, "representable separation discarded a complete original Fu6 order");
            Require(DiagnosticField(job.Output, "numerical-repairs") > 0, "separation regression did not exercise a finite LP retry");
            Require(job.Output.Stats.PlacementAttempts <= 200000, "separation retries exceeded the original work cap");
        }
    });
    Test("finite separation retries preserve anonymous reordered original identities", []
    {
        const std::vector<std::size_t> order{1, 4, 2, 3, 0, 5};
        for (const int turn : {0, 180})
        {
            auto original = FuSix(); original.Stocks[0].Width = 23;
            for (auto& part : original.Parts)
                part.AllowedAnglesDegrees = turn == 0 ? std::vector<double>{0} : std::vector<double>{0, 180};
            auto input = original;
            for (std::size_t i = 0; i < order.size(); ++i)
            {
                input.Parts[i] = original.Parts[order[i]];
                input.Parts[i].ID = "anonymous-regression-" + std::to_string(29 * i + 1337);
            }
            const auto job = Solve(input);
            EngineeringValid(input, job); ExactGeometry(input, job.Output);
            Require(job.Checked.Complete && job.Checked.PlacedCount == 6, "anonymous reordered originals lost complete construction");
        }
    });
    Test("fixed non-quarter-turn polygons retain their original angle domain", []
    {
        Problem input; input.Stocks = {{"original-sheet", 30, 30, 1, 1, "original-material"}};
        auto rectangle = Polygon("fixed-oblique", {{0, 0}, {3, 0}, {3, 1}, {0, 1}});
        rectangle.Quantity = 3; rectangle.AllowedAnglesDegrees = {37}; input.Parts = {rectangle};
        const auto job = Solve(input);
        EngineeringValid(input, job); ExactGeometry(input, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 3, "fixed 37-degree inventory remains incomplete");
        for (const auto& layout : job.Output.Layouts) for (const auto& placement : layout.Placements)
            Require(placement.Transform.AngleDegrees == 37, "construction widened a fixed oblique angle domain");
    });
    Test("normal job execution may refine the complete constructed result", []
    {
        const auto input = Three(); auto options = Budget(); options.EnableBasicCompaction = true;
        const auto job = Solve(input, options);
        EngineeringValid(input, job); ExactGeometry(input, job.Output);
        Require(job.Checked.Complete, "normal executor lost a complete construction");
    });
    Test("joint constructor competes before the single complete-winner refinement phase", []
    {
        const auto input = Three(); int calls = 0;
        StrategyRegistry constructors;
        constructors.Register("empty", [] { return std::make_unique<EmptyConstructor>(); });
        constructors.Register("joint", MakeConvexConstructionFactory());
        StrategyPipeline pipeline; pipeline.RequireCompleteConstruction = true;
        pipeline.ConstructionBudgetFraction = .75;
        pipeline.Construct = MakeConstructionPortfolioFactory(constructors, {"empty", "joint"});
        pipeline.PostProcessors.push_back({"inspect", [&] { return std::make_unique<InspectRefinement>(calls); }});
        StrategyRegistry registry; registry.Register("two-phase", MakePipelineFactory(std::move(pipeline)));
        const auto job = RunJob({"two-phase", "two-phase", input, Budget()}, registry);
        EngineeringValid(input, job); ExactGeometry(input, job.Output);
        Require(job.Checked.Complete && calls == 1, "construction and refinement were interleaved or incomplete");
    });
    Test("anonymous identity and input permutations preserve small-case construction", []
    {
        auto zero = Three(), half = Three(); zero.Stocks[0].Width = half.Stocks[0].Width = 6;
        for (auto& part : zero.Parts) part.AllowedAnglesDegrees = {0};
        for (auto& part : half.Parts) part.AllowedAnglesDegrees = {0, 180};
        for (const auto& original : {zero, half, Three(), FuSix()})
        {
            std::vector<std::size_t> order(original.Parts.size()); std::iota(order.begin(), order.end(), 0);
            std::mt19937 generator(71107);
            const std::size_t variants = order.size() == 3 ? 6 : 12;
            for (std::size_t variant = 0; variant < variants; ++variant)
            {
                auto input = original;
                for (std::size_t i = 0; i < order.size(); ++i)
                {
                    input.Parts[i] = original.Parts[order[i]];
                    input.Parts[i].ID = "anonymous-" + std::to_string(variant * 997 + i * 23);
                }
                const auto job = Solve(input);
                EngineeringValid(input, job); ExactGeometry(input, job.Output);
                Require(job.Checked.Complete, "anonymous reordered input exposed a case-dependent construction");
                if (order.size() == 3) std::next_permutation(order.begin(), order.end());
                else std::shuffle(order.begin(), order.end(), generator);
            }
        }
    });
    Test("mixed quantities reconstruct representable original-world sheet boundaries", []
    {
        auto input = Three(); input.Parts[0].Quantity = 2; input.Parts[2].Quantity = 3;
        input.Stocks[0].Width = 21.6; input.Stocks[0].Height = 14;
        const auto job = Solve(input);
        EngineeringValid(input, job); ExactGeometry(input, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 6, "mixed quantities lost a representable boundary solution");
        Require(DiagnosticField(job.Output, "board-adjustments") > 0, "boundary reconstruction regression did not exercise an actual correction");
        auto fu = FuSix(); fu.Stocks[0].Width = 38; fu.Stocks[0].Height = 76;
        fu.Parts[0].Quantity = fu.Parts[4].Quantity = 2;
        const auto mixed = Solve(fu);
        EngineeringValid(fu, mixed); ExactGeometry(fu, mixed.Output);
        Require(mixed.Checked.Complete && mixed.Checked.PlacedCount == 8, "mixed Fu inventory was not conserved");
    });
    Test("small budgets retain valid original demand and bounded work", []
    {
        const auto input = Three();
        for (const auto cap : {std::size_t(1), std::size_t(2), std::size_t(7), std::size_t(31)})
        {
            auto options = Budget(); options.MaxPlacementAttempts = cap;
            const auto job = Solve(input, options);
            EngineeringValid(input, job); ExactGeometry(input, job.Output);
            Require(job.Output.Stats.PlacementAttempts <= cap && !job.Checked.Complete, "tiny attempt cap was exceeded or fabricated completion");
        }
        auto timed = Budget(); timed.TimeLimitSeconds = 1e-9;
        const auto job = Solve(input, timed);
        EngineeringValid(input, job); ExactGeometry(input, job.Output);
        Require(job.Output.Stats.PlacementAttempts == 0 && job.Output.Layouts.empty(), "expired budget started a position search");
    });
    Test("cancellation is latched across one job and private state resets on reuse", []
    {
        const auto input = Three(); auto strategy = MakeConvexConstructionFactory()();
        auto options = Budget(); std::size_t polls = 0;
        options.Cancel = [&] { return ++polls == 300; };
        const auto cancelled = strategy->Run(input, options);
        Require(cancelled.State == Status::Cancelled && polls == 300 && Validate(input, cancelled).Valid,
            "one-shot cancellation was lost or original demand became invalid");
        ExactGeometry(input, cancelled);
        const auto reused = strategy->Run(input, Budget());
        Require(Validate(input, reused).Complete && reused.State == Status::Complete, "reused strategy retained an earlier job state");
        ExactGeometry(input, reused);
        options.Cancel = [] { return true; };
        const auto preCancelled = Solve(input, options);
        EngineeringValid(input, preCancelled); ExactGeometry(input, preCancelled.Output);
        Require(preCancelled.Output.State == Status::Cancelled && preCancelled.Output.Layouts.empty() &&
            !preCancelled.Checked.Complete, "pre-cancelled input entered construction");
    });
    Test("unsupported geometry and stock scope are diagnosed with demand intact", []
    {
        std::vector<Problem> cases;
        auto input = Three(); input.Parts[0].Geometry.Outer = {{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}}; cases.push_back(input);
        input = Three(); input.Parts[1].Geometry.Holes = {{{1, -2}, {2, -2}, {2, -1}, {1, -1}}}; cases.push_back(input);
        input = Three(); input.Parts[0].Geometry.Outer.clear(); input.Parts[0].Geometry.Radius = 1; cases.push_back(input);
        input = Three(); input.Parts[0].FreeRotation = true; cases.push_back(input);
        input = Three(); input.Parts[0].Quantity = 11; cases.push_back(input);
        input = Three(); input.Stocks[0].Quantity = 2; cases.push_back(input);
        input = Three(); input.PartGap = .1; cases.push_back(input);
        input.AllowCommonEdges = true; cases.push_back(input);
        for (const auto& unsupported : cases)
        {
            const auto job = Solve(unsupported);
            EngineeringValid(unsupported, job); ExactGeometry(unsupported, job.Output);
            Require(!job.Checked.Complete && job.Output.Layouts.empty() && HasDiagnostic(job.Output, "unsupported"),
                "unsupported scope entered construction or silently changed the source");
        }
    });
    Test("zero demand needs no stock and excessive quantities cannot wrap into supported scope", []
    {
        auto input = Three(); for (auto& part : input.Parts) part.Quantity = 0; input.Stocks.clear();
        const auto empty = Solve(input); EngineeringValid(input, empty); ExactGeometry(input, empty.Output);
        Require(empty.Checked.Complete && empty.Output.Layouts.empty() && empty.Output.Stats.PlacementAttempts == 0,
            "zero demand consumed inventory or search work");
        input = Three(); input.Parts[0].Quantity = std::numeric_limits<std::size_t>::max();
        const auto rejected = MakeConvexConstructionFactory()()->Run(input, Budget());
        Require(rejected.State == Status::InvalidInput && rejected.Layouts.empty() && rejected.Stats.PlacementAttempts == 0,
            "excessive demand wrapped into the small construction scope");
    });
    Test("infeasible or incomplete construction conserves demand and starts no refinement", []
    {
        auto input = Three(); input.Stocks[0].Width = input.Stocks[0].Height = 1;
        const auto direct = Solve(input); EngineeringValid(input, direct); ExactGeometry(input, direct.Output);
        Require(!direct.Checked.Complete && direct.Output.Layouts.empty() && direct.Output.Remaining.size() == input.Parts.size(),
            "individually impossible originals were dropped");
        int calls = 0; StrategyRegistry constructors; constructors.Register("joint", MakeConvexConstructionFactory());
        StrategyPipeline pipeline; pipeline.Construct = MakeConstructionPortfolioFactory(constructors);
        pipeline.RequireCompleteConstruction = true;
        pipeline.PostProcessors.push_back({"never", [&] { return std::make_unique<InspectRefinement>(calls); }});
        StrategyRegistry registry; registry.Register("pipeline", MakePipelineFactory(std::move(pipeline)));
        const auto job = RunJob({"incomplete", "pipeline", input, Budget()}, registry);
        EngineeringValid(input, job); ExactGeometry(input, job.Output);
        Require(calls == 0 && !job.Checked.Complete && HasDiagnostic(job.Output, "refinement deferred"),
            "an incomplete construction entered the refinement phase");
        auto overlap = Three(); overlap.Parts.resize(1);
        overlap.Parts[0] = Polygon("original-square", {{0, 0}, {3, 0}, {3, 3}, {0, 3}});
        overlap.Parts[0].Quantity = 2; overlap.Stocks[0].Width = overlap.Stocks[0].Height = 4;
        const auto partial = Solve(overlap); EngineeringValid(overlap, partial); ExactGeometry(overlap, partial.Output);
        Require(!partial.Checked.Complete && partial.Checked.PlacedCount == 1 && partial.Output.Remaining.size() == 1 &&
            partial.Output.Remaining[0].Quantity == 1, "impossible joint placement lost the feasible partial or missing demand");
    });
    Test("common-edge policy is forwarded to the original geometry guards", []
    {
        for (const bool allow : {false, true})
        {
            auto input = Three(); input.AllowCommonEdges = allow;
            const auto job = Solve(input); EngineeringValid(input, job); ExactGeometry(input, job.Output);
            Require(job.Checked.Complete && DiagnosticField(job.Output, "common-edge-exemption") == double(allow),
                "construction silently changed the original common-edge policy");
        }
    });
    std::cout << Passed << " convex construction tests passed, " << Failed << " failed\n";
    return Failed ? 1 : 0;
}
