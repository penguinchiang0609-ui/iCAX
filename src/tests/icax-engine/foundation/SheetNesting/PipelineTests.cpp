#include "SheetNestingPipeline.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

using namespace iCAX::SheetNesting;
namespace
{
    int Passed = 0, Failed = 0;
    void Require(bool value, const std::string& message)
    { if (!value) throw std::runtime_error(message); }
    void Test(const char* name, const std::function<void()>& body)
    {
        try { body(); ++Passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++Failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    using ConstructFunction = std::function<Result(const Problem&, const Options&)>;
    using OrderFunction = std::function<std::vector<std::size_t>(const Problem&, const Options&)>;
    using PostFunction = std::function<Result(const Problem&, const Result&, const Options&)>;
    class Constructor final : public IStrategy
    {
    public:
        explicit Constructor(ConstructFunction run) : Function(std::move(run)) {}
        Result Run(const Problem& p, const Options& o) override { return Function(p, o); }
    private: ConstructFunction Function;
    };
    class PartOrder final : public IPartOrder
    {
    public:
        explicit PartOrder(OrderFunction run) : Function(std::move(run)) {}
        std::vector<std::size_t> Order(const Problem& p, const Options& o) override { return Function(p, o); }
    private: OrderFunction Function;
    };
    class PostProcessor final : public IPostProcessor
    {
    public:
        explicit PostProcessor(PostFunction run) : Function(std::move(run)) {}
        Result Run(const Problem& p, const Result& r, const Options& o) override { return Function(p, r, o); }
    private: PostFunction Function;
    };
    StrategyFactory Construct(ConstructFunction run)
    { return [run = std::move(run)] { return std::make_unique<Constructor>(run); }; }
    PartOrderStage OrderStage(std::string id, OrderFunction run)
    { return {std::move(id), [run = std::move(run)] { return std::make_unique<PartOrder>(run); }}; }
    PostProcessStage PostStage(std::string id, PostFunction run)
    { return {std::move(id), [run = std::move(run)] { return std::make_unique<PostProcessor>(run); }}; }
    Options Budget()
    { Options o; o.TimeLimitSeconds = 30; o.MaxPlacementAttempts = 1000; return o; }
    Part Box(std::string id, double width, double height, std::size_t quantity = 1, Point origin = {})
    {
        Part p; p.ID = std::move(id); p.Quantity = quantity; p.Material = "steel";
        p.FreeRotation = false; p.AllowedAnglesDegrees = {0};
        p.Geometry.Outer = {{origin.X, origin.Y}, {origin.X + width, origin.Y},
            {origin.X + width, origin.Y + height}, {origin.X, origin.Y + height}};
        return p;
    }
    Problem OneBox()
    { Problem p; p.Parts = {Box("A", 10, 10)}; p.Stocks = {{"sheet", 100, 100, 2, 1, "steel"}}; return p; }
    Layout NewLayout(const Problem& p, std::size_t instance = 0)
    {
        Layout l; l.StockID = p.Stocks[0].ID; l.StockInstance = instance;
        l.Width = p.Stocks[0].Width; l.Height = p.Stocks[0].Height; return l;
    }
    Result SingleAt(const Problem& p, double x, double y)
    {
        Result r; r.State = Status::Complete; auto l = NewLayout(p);
        l.Placements.push_back({p.Parts[0].ID, 0, {x, y, 0}}); r.Layouts.push_back(std::move(l)); return r;
    }
    Result InLine(const Problem& p)
    {
        Result r; r.State = Status::Complete; auto l = NewLayout(p); double cursor = 0;
        for (const auto& part : p.Parts)
        {
            double minX = part.Geometry.Outer[0].X, maxX = minX, minY = part.Geometry.Outer[0].Y;
            for (const auto point : part.Geometry.Outer)
            { minX = std::min(minX, point.X); maxX = std::max(maxX, point.X); minY = std::min(minY, point.Y); }
            for (std::size_t instance = 0; instance < part.Quantity; ++instance)
            {
                l.Placements.push_back({part.ID, instance, {cursor - minX, -minY, 0}});
                cursor += maxX - minX + p.PartGap;
            }
        }
        r.Layouts.push_back(std::move(l)); return r;
    }
    JobResult Solve(const Problem& p, StrategyPipeline definition, Options options = Budget())
    {
        StrategyRegistry registry; registry.Register("pipeline", MakePipelineFactory(std::move(definition)));
        return RunJob({"pipeline-job", "pipeline", p, std::move(options)}, registry);
    }
    void Complete(const JobResult& r)
    {
        std::string errors;
        for (const auto& e : r.Checked.Errors) errors += e + "; ";
        for (const auto& e : r.Output.Diagnostics) errors += e + "; ";
        Require(r.Checked.Valid && r.Checked.Complete, "expected original-geometry completion: " + errors);
    }
    std::size_t Missing(const Result& r)
    { std::size_t n = 0; for (const auto& item : r.Remaining) n += item.Quantity; return n; }
}

int main()
{
    Test("part order is a permutation and preserves original demand", []
    {
        auto p = OneBox(); p.Parts[0] = Box("A", 10, 8, 2, {7, -3});
        p.Parts.push_back(Box("B", 6, 12, 1, {-11, 9}));
        p.PartGap = 2;
        bool sawReordered = false, sawOriginal = false;
        StrategyPipeline d;
        d.Order = OrderStage("reverse", [](const Problem&, const Options&) { return std::vector<std::size_t>{1, 0}; });
        d.Construct = Construct([&](const Problem& input, const Options&)
        {
            sawReordered = input.Parts[0].ID == "B" && input.Parts[1].ID == "A" && input.Parts[1].Quantity == 2;
            Require(input.Parts[0].Geometry.Outer[0].X == -11, "local geometry must survive ordering");
            return InLine(input);
        });
        d.PostProcessors.push_back(PostStage("identity", [&](const Problem& input, const Result& incumbent, const Options&)
        {
            sawOriginal = input.Parts[0].ID == "A" && input.Parts[1].ID == "B";
            auto r = incumbent; r.Stats = {}; return r;
        }));
        const auto r = Solve(p, std::move(d)); Complete(r);
        Require(sawReordered && sawOriginal, "construction receives ordered input; postprocessing receives original input");
        Require(p.Parts[0].ID == "A" && p.Parts[0].Quantity == 2 && p.Parts[0].Geometry.Outer[0].X == 7,
            "the caller's original problem must not change");
        Require(r.Output.Layouts[0].Placements[0].PartID == "B" && r.Checked.PlacedCount == 3,
            "ordering must retain original part identities and all instances");
    });
    Test("missing repeated and out-of-range permutation entries are rejected", []
    {
        auto p = OneBox(); p.Parts.push_back(Box("B", 5, 5));
        for (const auto& permutation : std::vector<std::vector<std::size_t>>{{0}, {0, 0}, {0, 2}})
        {
            int constructed = 0;
            StrategyPipeline d;
            d.Order = OrderStage("invalid", [permutation](const Problem&, const Options&) { return permutation; });
            d.Construct = Construct([&](const Problem& input, const Options&) { ++constructed; return InLine(input); });
            const auto r = Solve(p, std::move(d));
            Require(constructed == 0 && r.Output.State == Status::Failed && Missing(r.Output) == 2,
                "invalid ordering must stop before construction and conserve all demand");
        }
    });
    Test("invalid throwing failed and worse postprocessing retain the legal incumbent", []
    {
        const auto p = OneBox();
        for (int mode = 0; mode < 4; ++mode)
        {
            int calls = 0, laterCalls = 0;
            StrategyPipeline d;
            d.Construct = Construct([](const Problem& input, const Options&) { return SingleAt(input, 0, 0); });
            d.PostProcessors.push_back(PostStage("rejected", [&, mode](const Problem& input, const Result& incumbent, const Options&) -> Result
            {
                ++calls;
                if (mode == 1) throw std::runtime_error("intentional postprocessing failure");
                auto r = mode == 3 ? SingleAt(input, 45, 45) : incumbent;
                r.Stats = {};
                if (mode == 0)
                { r.GeometryValidated = true; r.Layouts[0].Placements[0].Transform.X = -1; }
                if (mode == 2) r.State = Status::Failed;
                return r;
            }));
            d.PostProcessors.push_back(PostStage("later", [&](const Problem&, const Result& incumbent, const Options&)
            { ++laterCalls; auto r = incumbent; r.Stats = {}; return r; }));
            const auto r = Solve(p, std::move(d)); Complete(r);
            Require(calls == 1 && r.Output.State == Status::Complete, "a rejected optional stage must not discard a legal result");
            Require(laterCalls == (mode == 1 ? 0 : 1),
                "an exception ends the chain because its attempt usage is unknown; reported rejected candidates may continue");
            const auto pose = r.Output.Layouts[0].Placements[0].Transform;
            Require(pose.X == 0 && pose.Y == 0, "forged validation and worse geometry must not replace the incumbent");
        }
    });
    Test("postprocessing accepts a larger verified regular remnant", []
    {
        const auto p = OneBox(); const auto original = SingleAt(p, 45, 45);
        const auto before = EvaluateRemnants(p, original);
        StrategyPipeline d; d.Construct = Construct([original](const Problem&, const Options&) { return original; });
        d.PostProcessors.push_back(PostStage("corner", [](const Problem& input, const Result&, const Options&) { return SingleAt(input, 0, 0); }));
        const auto r = Solve(p, std::move(d)); Complete(r);
        Require(r.Output.Layouts[0].Placements[0].Transform.X == 0 && r.Remnants.Measured &&
            r.Remnants.LastSheetRegularArea > before.LastSheetRegularArea + 1,
            "acceptance must use remnant measured from original geometry");
    });
    Test("one fewer sheet wins even with a smaller final-sheet remnant", []
    {
        auto p = OneBox(); p.Parts = {Box("A", 10, 100, 2)};
        Result original; original.State = Status::Complete;
        for (std::size_t i = 0; i < 2; ++i)
        { auto l = NewLayout(p, i); l.Placements.push_back({"A", i, {0, 0, 0}}); original.Layouts.push_back(std::move(l)); }
        const auto before = EvaluateRemnants(p, original);
        StrategyPipeline d; d.Construct = Construct([original](const Problem&, const Options&) { return original; });
        d.PostProcessors.push_back(PostStage("merge-sheets", [](const Problem& input, const Result&, const Options&) { return InLine(input); }));
        const auto r = Solve(p, std::move(d)); Complete(r);
        Require(r.Output.Layouts.size() == 1 && r.Remnants.LastSheetRegularArea < before.LastSheetRegularArea,
            "sheet count must precede regular-remnant area in the shared comparison");
    });
    Test("equal quality retains the original instance ordering", []
    {
        auto p = OneBox(); p.Parts[0].Quantity = 2;
        StrategyPipeline d; d.Construct = Construct([](const Problem& input, const Options&) { return InLine(input); });
        d.PostProcessors.push_back(PostStage("equivalent", [](const Problem&, const Result& incumbent, const Options&)
        {
            auto r = incumbent; r.Stats = {};
            std::swap(r.Layouts[0].Placements[0].Instance, r.Layouts[0].Placements[1].Instance); return r;
        }));
        const auto r = Solve(p, std::move(d)); Complete(r);
        Require(r.Output.Layouts[0].Placements[0].Instance == 0, "an equal candidate must not replace the stable incumbent");
    });
    Test("bounded precision acceptance preserves identities and enforces the declared shift", []
    {
        auto p=OneBox();p.Parts[0].AllowedAnglesDegrees={0,90};
        for(int mode=0;mode<5;++mode)
        {
            StrategyPipeline d;d.Construct=Construct([](const Problem& input,const Options&){return SingleAt(input,5,5);});
            auto stage=PostStage("precision",[mode](const Problem&,const Result& incumbent,const Options&)
            {
                auto candidate=incumbent;candidate.Stats={};auto& pose=candidate.Layouts[0].Placements[0];
                if(mode==0)pose.Transform.X+=1e-5;
                if(mode==1)pose.Transform.X+=1e-3;
                if(mode==2)pose.Transform.AngleDegrees=90;
                if(mode==3)pose.Group=42;
                if(mode==4)candidate.Layouts[0].StockInstance=1;
                return candidate;
            });stage.MaxPrecisionAxisShift=2e-4;d.PostProcessors.push_back(std::move(stage));
            const auto result=Solve(p,std::move(d));Complete(result);
            const auto& layout=result.Output.Layouts[0];const auto& pose=layout.Placements[0];
            if(mode==0)Require(std::abs(pose.Transform.X-5.00001)<1e-12,"valid precision shift rejected");
            else Require(pose.Transform.X==5&&pose.Transform.AngleDegrees==0&&pose.Group==0&&layout.StockInstance==0,"invalid precision change accepted");
        }
    });
    Test("precision acceptance cannot swap individual instances", []
    {
        auto p=OneBox();p.Parts[0].Quantity=2;
        StrategyPipeline d;d.Construct=Construct([](const Problem& input,const Options&){return InLine(input);});
        auto stage=PostStage("precision",[](const Problem&,const Result& incumbent,const Options&)
        {
            auto candidate=incumbent;candidate.Stats={};
            std::swap(candidate.Layouts[0].Placements[0].Instance,candidate.Layouts[0].Placements[1].Instance);
            return candidate;
        });stage.MaxPrecisionAxisShift=2e-4;d.PostProcessors.push_back(std::move(stage));
        const auto result=Solve(p,std::move(d));Complete(result);
        Require(result.Output.Layouts[0].Placements[0].Instance==0,"instance swap accepted as tiny movement");
    });
    Test("part order constructor and postprocessor objects are fresh for parallel jobs", []
    {
        const auto p = OneBox(); std::atomic<int> orders{0}, constructors{0}, processors{0};
        StrategyPipeline d;
        d.Order = {"local-order", [&]() -> std::unique_ptr<IPartOrder>
        {
            ++orders; return std::make_unique<PartOrder>([runs = 0](const Problem& input, const Options&) mutable
            {
                Require(++runs == 1, "part-order object reused");
                std::vector<std::size_t> ids(input.Parts.size()); std::iota(ids.begin(), ids.end(), 0); return ids;
            });
        }};
        d.Construct = [&]() -> std::unique_ptr<IStrategy>
        {
            ++constructors; return std::make_unique<Constructor>([runs = 0](const Problem& input, const Options&) mutable
            { Require(++runs == 1, "constructor object reused"); return InLine(input); });
        };
        d.PostProcessors.push_back({"local-post", [&]() -> std::unique_ptr<IPostProcessor>
        {
            ++processors; return std::make_unique<PostProcessor>([runs = 0](const Problem&, const Result& incumbent, const Options&) mutable
            { Require(++runs == 1, "postprocessor object reused"); auto r = incumbent; r.Stats = {}; return r; });
        }});
        const auto factory = MakePipelineFactory(std::move(d)); StrategyRegistry registry;
        for (int i = 0; i < 12; ++i) registry.Register("pipeline-" + std::to_string(i), factory);
        PortfolioOptions options; options.PerJob = Budget(); options.Workers = 4;
        const auto portfolio = RunPortfolio(p, options, registry);
        Require(portfolio.Jobs.size() == 12 && orders == 12 && constructors == 12 && processors == 12,
            "factories must create every stage separately for every job");
        for (const auto& result : portfolio.Jobs) Complete(result);
    });
    Test("placement-attempt budget is shared across accepted and rejected stages", []
    {
        const auto p = OneBox(); auto options = Budget(); options.MaxPlacementAttempts = 10;
        std::vector<std::size_t> seen; int exhaustedCalls = 0;
        StrategyPipeline d; d.Construct = Construct([&](const Problem& input, const Options& o)
        {
            seen.push_back(o.MaxPlacementAttempts); auto r = SingleAt(input, 0, 0); r.Stats.PlacementAttempts = 4; return r;
        });
        for (int i = 0; i < 2; ++i)
            d.PostProcessors.push_back(PostStage("consume-" + std::to_string(i), [&](const Problem&, const Result& incumbent, const Options& o)
            {
                seen.push_back(o.MaxPlacementAttempts); auto r = incumbent; r.Stats = {}; r.Stats.PlacementAttempts = 3; return r;
            }));
        d.PostProcessors.push_back(PostStage("exhausted", [&](const Problem&, const Result& incumbent, const Options&)
        { ++exhaustedCalls; auto r = incumbent; r.Stats = {}; return r; }));
        const auto r = Solve(p, std::move(d), options); Complete(r);
        Require(seen == std::vector<std::size_t>{10, 6, 3} && exhaustedCalls == 0,
            "each stage must receive only remaining attempts and stop at exhaustion");
        Require(r.Output.Stats.PlacementAttempts == 10, "rejected candidate work must still be included in job statistics");
    });
    Test("pulse cancellation latches and stops later stages without discarding a legal result", []
    {
        const auto p = OneBox(); auto options = Budget(); bool armed = false, fired = false; int postCalls = 0;
        options.Cancel = [&] { if (armed && !fired) { fired = true; return true; } return false; };
        StrategyPipeline d; d.Construct = Construct([&](const Problem& input, const Options& o)
        {
            armed = true; Require(o.Cancel && o.Cancel() && o.Cancel(), "the stage cancellation callback must latch a one-call pulse");
            return SingleAt(input, 0, 0);
        });
        d.PostProcessors.push_back(PostStage("never", [&](const Problem&, const Result& incumbent, const Options&)
        { ++postCalls; auto r = incumbent; r.Stats = {}; return r; }));
        const auto r = Solve(p, std::move(d), options); Complete(r);
        Require(fired && postCalls == 0 && r.Output.State == Status::Cancelled,
            "a later false callback or Complete result must not erase observed cancellation");
    });
    Test("pre-cancellation conserves all demand without entering any stage", []
    {
        auto p = OneBox(); p.Parts[0].Quantity = 3; auto options = Budget(); options.Cancel = [] { return true; };
        int calls = 0; StrategyPipeline d;
        d.Order = OrderStage("never", [&](const Problem&, const Options&) { ++calls; return std::vector<std::size_t>{0}; });
        d.Construct = Construct([&](const Problem& input, const Options&) { ++calls; return InLine(input); });
        d.PostProcessors.push_back(PostStage("never", [&](const Problem&, const Result& incumbent, const Options&) { ++calls; return incumbent; }));
        const auto r = Solve(p, std::move(d), options);
        Require(calls == 0 && r.Output.State == Status::Cancelled && r.Checked.Valid &&
            !r.Checked.Complete && r.Output.Layouts.empty() && Missing(r.Output) == 3,
            "pre-cancellation must keep a valid empty layout and explicit remaining demand");
    });
    Test("a cancelled postprocessor cannot relabel or replace the retained incumbent as Complete", []
    {
        const auto p = OneBox(); int laterCalls = 0; StrategyPipeline d;
        d.Construct = Construct([](const Problem& input, const Options&) { return SingleAt(input, 0, 0); });
        d.PostProcessors.push_back(PostStage("cancelled-invalid", [](const Problem&, const Result& incumbent, const Options&)
        { auto r = incumbent; r.Stats = {}; r.State = Status::Cancelled; r.Layouts[0].Placements[0].Transform.X = -1; return r; }));
        d.PostProcessors.push_back(PostStage("never", [&](const Problem& input, const Result&, const Options&)
        { ++laterCalls; return SingleAt(input, 45, 45); }));
        const auto r = Solve(p, std::move(d)); Complete(r);
        Require(laterCalls == 0 && r.Output.State == Status::Cancelled && r.Output.Layouts[0].Placements[0].Transform.X == 0,
            "cancellation is retained even when its candidate fails geometry validation");
    });
    Test("pipeline and portfolio use the same selected ranking policy", []
    {
        Problem p; Part c; c.ID = "C"; c.Material = "steel"; c.FreeRotation = false; c.AllowedAnglesDegrees = {0};
        c.Geometry.Outer = {{0,0},{100,0},{100,100},{0,100},{0,90},{80,90},{80,10},{0,10}};
        p.Parts = {c}; p.Stocks = {{"sheet",200,100,1,1,"steel"}};
        StrategyPipeline d; d.Construct = Construct([](const Problem& input, const Options&) { return SingleAt(input, 100, 0); });
        d.PostProcessors.push_back(PostStage("left", [](const Problem& input, const Result&, const Options&) { return SingleAt(input, 0, 0); }));
        auto options = Budget(); options.Ranking = RankingPolicy::RegularRemnant;
        auto r = Solve(p, d, options); Complete(r);
        Require(r.Output.Layouts[0].Placements[0].Transform.X == 100, "regular-remnant policy must retain the larger open region");
        options.Ranking = RankingPolicy::LowThenLeft; r = Solve(p, d, options); Complete(r);
        Require(r.Output.Layouts[0].Placements[0].Transform.X == 0, "low-left policy must accept the lower right boundary");
        StrategyRegistry registry; registry.Register("pipeline", MakePipelineFactory(d));
        PortfolioOptions portfolioOptions; portfolioOptions.PerJob = Budget();
        portfolioOptions.PerJob.Ranking = RankingPolicy::RegularRemnant; portfolioOptions.Ranking = RankingPolicy::LowThenLeft;
        const auto portfolio = RunPortfolio(p, portfolioOptions, registry);
        Require(portfolio.BestJob == 0, "portfolio must retain a legal pipeline winner"); Complete(portfolio.Jobs[0]);
        Require(portfolio.Jobs[0].Output.Layouts[0].Placements[0].Transform.X == 0,
            "portfolio ranking must reach internal postprocessing rather than only the final comparison");
    });
    Test("accepted postprocessing preserves construction timing and records separate new work", []
    {
        const auto p = OneBox(); StrategyPipeline d;
        d.Construct = Construct([](const Problem& input, const Options&)
        {
            auto r = SingleAt(input, 45, 45);
            r.Stats.PreparationSeconds = .2; r.Stats.PlacementSeconds = .8; return r;
        });
        d.PostProcessors.push_back(PostStage("improve", [](const Problem& input, const Result&, const Options&)
        {
            auto r = SingleAt(input, 0, 0);
            r.Stats.PreparationSeconds = 0; r.Stats.PlacementSeconds = .1; return r;
        }));
        const auto r = Solve(p, std::move(d)); Complete(r);
        Require(r.Output.Layouts[0].Placements[0].Transform.X == 0, "the timing check must exercise an accepted replacement");
        Require(r.Output.Stats.PreparationSeconds == .2 && r.Output.Stats.PlacementSeconds == .8,
            "postprocessor timing must not overwrite constructor preparation or placement timing");
        Require(r.Output.Stats.PostProcessingSeconds > 0 && r.Output.Stats.VerificationSeconds > 0 &&
            r.Output.Stats.EvaluationSeconds > 0,
            "postprocessing and cumulative assessment work must have separate measured durations");
    });
    Test("a stage exhausting the time budget prevents the next stage from starting", []
    {
        const auto p = OneBox(); auto options = Budget(); options.TimeLimitSeconds = .001;
        int constructed = 0;
        StrategyPipeline d;
        d.Order = OrderStage("slow-order", [](const Problem&, const Options&)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return std::vector<std::size_t>{0};
        });
        d.Construct = Construct([&](const Problem& input, const Options&) { ++constructed; return SingleAt(input, 0, 0); });
        const auto r = Solve(p, std::move(d), options);
        Require(constructed == 0 && r.Output.State == Status::Partial && r.Checked.Valid &&
            r.Output.Layouts.empty() && Missing(r.Output) == 1,
            "an over-budget ordering stage must leave valid explicit demand without launching construction");

        int postCalls = 0; StrategyPipeline afterConstruction;
        afterConstruction.Construct = Construct([](const Problem& input, const Options&)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); return SingleAt(input, 0, 0);
        });
        afterConstruction.PostProcessors.push_back(PostStage("never", [&](const Problem&, const Result& incumbent, const Options&)
        { ++postCalls; auto candidate = incumbent; candidate.Stats = {}; return candidate; }));
        const auto retained = Solve(p, std::move(afterConstruction), options); Complete(retained);
        Require(postCalls == 0 && retained.Output.State == Status::Complete,
            "a constructor finishing after its cooperative budget keeps its legal result but starts no postprocessing");
    });
    Test("pipeline definitions and null stage objects fail explicitly with demand intact", []
    {
        const auto valid = []
        {
            StrategyPipeline d; d.Construct = Construct([](const Problem& input, const Options&) { return InLine(input); }); return d;
        };
        const auto invalid = [](StrategyPipeline d)
        {
            bool rejected = false;
            try { (void)MakePipelineFactory(std::move(d)); }
            catch (const std::invalid_argument&) { rejected = true; }
            Require(rejected, "a malformed stage definition must be rejected before job execution");
        };
        invalid({});
        auto d = valid(); d.Order.ID = "missing-factory"; invalid(d);
        d = valid();
        d.Order.Factory = [] { return std::make_unique<PartOrder>([](const Problem&, const Options&) { return std::vector<std::size_t>{0}; }); };
        invalid(d);
        d = valid();
        d.PostProcessors.push_back(PostStage("", [](const Problem&, const Result& incumbent, const Options&) { return incumbent; })); invalid(d);
        d = valid(); d.PostProcessors.push_back({"missing-factory", {}}); invalid(d);

        auto p = OneBox(); p.Parts[0].Quantity = 3;
        for (int stage = 0; stage < 3; ++stage)
        {
            d = valid();
            if (stage == 0) d.Construct = [] { return std::unique_ptr<IStrategy>{}; };
            if (stage == 1) d.Order = {"null-order", [] { return std::unique_ptr<IPartOrder>{}; }};
            if (stage == 2) d.PostProcessors.push_back({"null-post", [] { return std::unique_ptr<IPostProcessor>{}; }});
            const auto r = Solve(p, std::move(d));
            Require(r.Output.State == Status::Failed && !r.Output.Diagnostics.empty() &&
                r.Output.Layouts.empty() && Missing(r.Output) == 3,
                "a factory returning null must produce a diagnosed job failure without consuming demand");
        }
    });
    std::cout << Passed << " pipeline tests passed, " << Failed << " failed\n";
    return Failed ? 1 : 0;
}
