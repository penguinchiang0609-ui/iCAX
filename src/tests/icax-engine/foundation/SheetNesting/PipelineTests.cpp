#include "SheetNestingPipeline.h"
#include "NestingExecution.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
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
    {
        Options o; o.TimeLimitSeconds = 30; o.MaxPlacementAttempts = 1000;
        // These isolate explicit pipeline acceptance, cancellation and budget
        // contracts. Default common refinement has separate lifecycle tests.
        o.EnableBasicCompaction = false;
        return o;
    }
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
    JobResult Assessed(const Problem& p,const Result& result)
    { JobResult job;job.Output=result;Detail::AssessCandidate(p,job);Complete(job);return job; }
    template<std::size_t... I>
    auto QualityPrefixOf(const Detail::QualityKey& key,std::index_sequence<I...>)
    {
        return std::tuple(std::get<I>(key)...);
    }
    auto QualityPrefix(const Detail::QualityKey& key)
    { return QualityPrefixOf(key,std::make_index_sequence<std::tuple_size_v<Detail::QualityKey>-2>{}); }
    void CheckMaterialMean(const Problem& p,const Result& result,double x,double y)
    {
        const auto job=Assessed(p,result);
        const auto quantized=[&](double value){return std::round(value/p.Tolerance)*p.Tolerance;};
        for(const auto policy:{RankingPolicy::RegularRemnant,RankingPolicy::LowThenLeft,RankingPolicy::CompactArea})
        {
            const auto key=Detail::Quality(job,p,policy);
            constexpr auto count=std::tuple_size_v<Detail::QualityKey>;
            Require(std::abs(std::get<count-2>(key)-quantized(y))<1e-9&&std::abs(std::get<count-1>(key)-quantized(x))<1e-9,
                "quality must use the analytic material-area centroid, not a bounding-box center");
        }
    }
    std::size_t Missing(const Result& r)
    { std::size_t n = 0; for (const auto& item : r.Remaining) n += item.Quantity; return n; }
}

int main()
{
    Test("construction portfolio selects a complete winner before one refinement phase", []
    {
        const auto p = OneBox(); std::vector<std::string> events;
        StrategyRegistry constructors;
        constructors.Register("loose", Construct([&](const Problem& input, const Options& options)
        {
            Require(!options.EnableBasicCompaction, "a competing constructor received automatic refinement");
            events.push_back("loose"); auto r = SingleAt(input, 30, 30);
            r.Stats.PlacementAttempts = 2; r.Stats.PreparationSeconds = .1;
            r.Stats.CombinationGroups = 11; return r;
        }));
        constructors.Register("dense", Construct([&](const Problem& input, const Options& options)
        {
            Require(!options.EnableBasicCompaction && events == std::vector<std::string>{"loose"},
                "constructor competition was interleaved with refinement");
            events.push_back("dense"); auto r = SingleAt(input, 10, 10);
            r.Stats.PlacementAttempts = 3; r.Stats.PreparationSeconds = .2;
            r.Stats.CombinationGroups = 22; return r;
        }));
        StrategyPipeline pipeline;
        pipeline.Construct = MakeConstructionPortfolioFactory(constructors, {"loose", "dense"});
        pipeline.RequireCompleteConstruction = true;
        pipeline.PostProcessors.push_back(PostStage("winner-only", [&](const Problem&, const Result& incumbent, const Options&)
        {
            Require(events == std::vector<std::string>{"loose", "dense"} &&
                incumbent.Layouts[0].Placements[0].Transform.X == 10 && incumbent.Stats.CombinationGroups == 22,
                "refinement did not receive the completed construction winner");
            events.push_back("refine"); auto candidate = incumbent; candidate.Stats = {};
            candidate.Stats.CombinationGroups = 22; candidate.Stats.PlacementAttempts = 1;
            candidate.Layouts[0].Placements[0].Transform = {0, 0, 0}; return candidate;
        }));
        auto options = Budget(); options.Ranking = RankingPolicy::LowThenLeft; options.MaxPlacementAttempts = 10;
        const auto result = Solve(p, std::move(pipeline), options); Complete(result);
        Require(events == std::vector<std::string>{"loose", "dense", "refine"} &&
            result.Output.Layouts[0].Placements[0].Transform.X == 0,
            "two-phase execution refined an intermediate or losing candidate");
        Require(result.Output.Stats.PlacementAttempts == 6 && result.Output.Stats.CombinationGroups == 22 &&
            std::abs(result.Output.Stats.PreparationSeconds - .3) < 1e-12,
            "portfolio work or selected construction provenance was lost");
    });
    Test("complete construction outranks partial candidates and rejects invalid geometry", []
    {
        auto p = OneBox(); p.Parts[0].Quantity = 2; int calls = 0;
        StrategyRegistry constructors;
        constructors.Register("partial", Construct([](const Problem& input, const Options&)
        {
            auto r = SingleAt(input, 0, 0); r.State = Status::Partial;
            r.Remaining = {{input.Parts[0].ID, 1}}; return r;
        }));
        constructors.Register("invalid", Construct([](const Problem& input, const Options&)
        { auto r = InLine(input); r.Layouts[0].Placements[0].Transform.X = -1; return r; }));
        constructors.Register("complete", Construct([](const Problem& input, const Options&)
        { return InLine(input); }));
        StrategyPipeline pipeline; pipeline.RequireCompleteConstruction = true;
        pipeline.Construct = MakeConstructionPortfolioFactory(constructors);
        pipeline.PostProcessors.push_back(PostStage("inspect-complete", [&](const Problem& input, const Result& source, const Options&)
        {
            ++calls; Require(Validate(input, source).Complete && source.Layouts[0].Placements.size() == 2,
                "an incomplete or invalid construction entered refinement");
            auto r = source; r.Stats = {}; return r;
        }));
        const auto result = Solve(p, std::move(pipeline)); Complete(result);
        Require(calls == 1 && p.Parts[0].Quantity == 2 && p.Parts[0].Geometry.Outer[0].X == 0 &&
            p.Parts[0].AllowedAnglesDegrees == std::vector<double>{0} && p.Stocks[0].Quantity == 2,
            "constructor selection changed original demand, contours, angle domain or inventory");
    });
    Test("incomplete construction retains its best partial and starts no refinement", []
    {
        auto p = OneBox(); p.Parts[0].Quantity = 2; int postCalls = 0;
        StrategyRegistry constructors;
        constructors.Register("empty", Construct([](const Problem& input, const Options&)
        { Result r; r.State = Status::Partial; r.Remaining = {{input.Parts[0].ID, 2}}; return r; }));
        constructors.Register("partial", Construct([](const Problem& input, const Options&)
        {
            auto r = SingleAt(input, 20, 30); r.State = Status::Partial;
            r.Remaining = {{input.Parts[0].ID, 1}}; return r;
        }));
        StrategyPipeline composed; composed.Construct = Construct([](const Problem& input, const Options&)
        { return InLine(input); });
        constructors.Register("composed", MakePipelineFactory(composed));
        StrategyPipeline pipeline; pipeline.RequireCompleteConstruction = true;
        pipeline.Construct = MakeConstructionPortfolioFactory(constructors, {"missing", "composed", "empty", "partial"});
        pipeline.PostProcessors.push_back(PostStage("never", [&](const Problem&, const Result& source, const Options&)
        { ++postCalls; return source; }));
        auto options = Budget(); options.EnableBasicCompaction = true;
        const auto result = Solve(p, std::move(pipeline), options);
        Require(result.Checked.Valid && !result.Checked.Complete && result.Checked.PlacedCount == 1 &&
            Missing(result.Output) == 1 && postCalls == 0 &&
            result.Output.Layouts[0].Placements[0].Transform.X == 20 &&
            result.Output.Layouts[0].Placements[0].Transform.Y == 30,
            "an incomplete order was polished or lost its best partial construction");
        Require(std::any_of(result.Output.Diagnostics.begin(), result.Output.Diagnostics.end(), [](const std::string& note)
            { return note.find("composed refinement strategy was skipped") != std::string::npos; }),
            "a composed strategy silently entered the pure construction pool");
        StrategyPipeline invalid; invalid.RequireCompleteConstruction = true;
        invalid.Construct = Construct([](const Problem& input, const Options&)
        { auto r = InLine(input); r.Layouts[0].Placements[0].Transform.X = -1; return r; });
        const auto raw = Solve(p, std::move(invalid), Budget());
        Require(std::any_of(raw.Output.Diagnostics.begin(), raw.Output.Diagnostics.end(), [](const std::string& note)
            { return note.find("refinement deferred") != std::string::npos; }),
            "the complete-construction gate skipped assessment when basic and explicit refinement were empty");
    });
    Test("constructor competition shares attempts and creates independent strategy state", []
    {
        const auto p = OneBox(); std::vector<std::size_t> shares;
        StrategyRegistry constructors;
        constructors.Register("bounded", Construct([calls = 0, &shares](const Problem& input, const Options& options) mutable
        {
            Require(++calls == 1 && !options.EnableBasicCompaction, "independent construction jobs shared mutable strategy state");
            shares.push_back(options.MaxPlacementAttempts); auto r = SingleAt(input, 0, 0);
            r.Stats.PlacementAttempts = options.MaxPlacementAttempts; return r;
        }));
        auto factory = MakeConstructionPortfolioFactory(constructors, {"bounded", "bounded", "bounded"});
        auto strategy = factory(); auto options = Budget(); options.MaxPlacementAttempts = 10;
        for (int run = 0; run < 2; ++run)
        {
            const auto result = strategy->Run(p, options);
            Require(Validate(p, result).Complete && result.Stats.PlacementAttempts == 10,
                "constructor work exceeded or reset the shared attempt cap");
        }
        Require(shares == std::vector<std::size_t>{3, 3, 4, 3, 3, 4}, "remaining work was not fairly allocated to independent jobs");
        shares.clear(); options.MaxPlacementAttempts = 1;
        const auto limited = strategy->Run(p, options);
        Require(Validate(p, limited).Complete && limited.Stats.PlacementAttempts == 1 &&
            shares == std::vector<std::size_t>{1}, "a tiny shared budget started zero-budget or excess construction jobs");
    });
    Test("construction exceptions charge their allocated attempts", []
    {
        const auto p = OneBox();
        for (const bool standardException : {true, false})
        {
            std::vector<std::size_t> shares; std::size_t consumed = 0;
            StrategyRegistry constructors;
            constructors.Register("throw", Construct([&](const Problem&, const Options& options) -> Result
            {
                shares.push_back(options.MaxPlacementAttempts); consumed += options.MaxPlacementAttempts;
                if (standardException) throw std::runtime_error("search exhausted its share");
                throw 1;
            }));
            auto options = Budget(); options.MaxPlacementAttempts = 10;
            const auto factory = MakeConstructionPortfolioFactory(constructors, {"throw", "throw", "throw"});
            const auto result = factory()->Run(p, options);
            Require(Validate(p, result).Valid && Missing(result) == 1 &&
                shares == std::vector<std::size_t>{3, 3, 4} && consumed == 10 &&
                result.Stats.PlacementAttempts == 10,
                "throwing construction recovered consumed attempt shares or lost their accounting");
            Require(std::count_if(result.Diagnostics.begin(), result.Diagnostics.end(), [](const std::string& note)
                { return note.find("allocated attempt cap charged conservatively") != std::string::npos; }) == 3,
                "conservative exception charges were not disclosed");
            shares.clear(); consumed = 0; options.MaxPlacementAttempts = 1;
            const auto limited = factory()->Run(p, options);
            Require(shares == std::vector<std::size_t>{1} && consumed == 1 && limited.Stats.PlacementAttempts == 1,
                "a constructor started after a throwing peer consumed the final attempt share");
        }
    });
    Test("constructor factory failures leave attempts for actual search", []
    {
        const auto p = OneBox(); std::size_t consumed = 0; int factories = 0, calls = 0;
        StrategyRegistry constructors;
        constructors.Register("throw-factory", [&]() -> std::unique_ptr<IStrategy>
        { ++factories; throw std::runtime_error("factory failed before search"); });
        constructors.Register("null-factory", []() -> std::unique_ptr<IStrategy> { return {}; });
        constructors.Register("bounded", Construct([&](const Problem& input, const Options& options)
        {
            ++calls; consumed += options.MaxPlacementAttempts;
            auto result = SingleAt(input, 0, 0); result.Stats.PlacementAttempts = options.MaxPlacementAttempts;
            return result;
        }));
        auto options = Budget(); options.MaxPlacementAttempts = 10;
        const auto result = MakeConstructionPortfolioFactory(constructors,
            {"missing", "throw-factory", "null-factory", "bounded"})()->Run(p, options);
        Require(Validate(p, result).Complete && factories == 1 && calls == 1 && consumed == 10 &&
            result.Stats.PlacementAttempts == 10,
            "a factory failure consumed search attempts before an algorithm started");
    });
    Test("failed construction retains its legal winner and continues within remaining work", []
    {
        const auto p = OneBox(); std::size_t consumed = 0; bool laterRan = false;
        StrategyRegistry constructors;
        constructors.Register("winner", Construct([&](const Problem& input, const Options&)
        {
            consumed += 2; auto result = SingleAt(input, 0, 0);
            result.Stats.PlacementAttempts = 2; result.Stats.CombinationGroups = 33; return result;
        }));
        constructors.Register("throw", Construct([&](const Problem&, const Options& options) -> Result
        { consumed += options.MaxPlacementAttempts; throw std::runtime_error("search failed after consuming share"); }));
        constructors.Register("later", Construct([&](const Problem& input, const Options& options)
        {
            laterRan = true; Require(options.MaxPlacementAttempts == 4, "an exception changed the remaining search balance");
            consumed += options.MaxPlacementAttempts; auto result = SingleAt(input, 30, 30);
            result.Stats.PlacementAttempts = options.MaxPlacementAttempts; return result;
        }));
        auto options = Budget(); options.MaxPlacementAttempts = 10; options.Ranking = RankingPolicy::LowThenLeft;
        const auto result = MakeConstructionPortfolioFactory(constructors, {"winner", "throw", "later"})()->Run(p, options);
        Require(Validate(p, result).Complete && laterRan && consumed == 10 && result.Stats.PlacementAttempts == 10 &&
            result.Layouts[0].Placements[0].Transform.X == 0 && result.Stats.CombinationGroups == 33,
            "an exceptional peer erased the complete winner or prevented budgeted independent search");
    });
    Test("constructor competition latches cancellation and stops at its shared time deadline", []
    {
        const auto p = OneBox(); int firstCalls = 0, laterCalls = 0, postCalls = 0;
        bool armed = false, pulse = false;
        StrategyRegistry constructors;
        constructors.Register("first", Construct([&](const Problem& input, const Options& options)
        {
            ++firstCalls; armed = true;
            Require(options.Cancel() && options.Cancel(), "the construction cancellation pulse was not latched");
            return SingleAt(input, 0, 0);
        }));
        constructors.Register("later", Construct([&](const Problem& input, const Options&)
        { ++laterCalls; return SingleAt(input, 0, 0); }));
        StrategyPipeline pipeline; pipeline.RequireCompleteConstruction = true;
        pipeline.Construct = MakeConstructionPortfolioFactory(constructors);
        pipeline.PostProcessors.push_back(PostStage("never", [&](const Problem&, const Result& source, const Options&)
        { ++postCalls; return source; }));
        auto options = Budget(); options.Cancel = [&]
        { if (armed && !pulse) { pulse = true; return true; } return false; };
        const auto cancelled = Solve(p, std::move(pipeline), options); Complete(cancelled);
        Require(firstCalls == 1 && laterCalls == 0 && postCalls == 0 && cancelled.Output.State == Status::Cancelled,
            "a later constructor or refinement erased observed cancellation");
        options.Cancel = [] { return true; };
        const auto preCancelled = MakeConstructionPortfolioFactory(constructors)()->Run(p, options);
        Require(Validate(p, preCancelled).Valid && preCancelled.State == Status::Cancelled &&
            preCancelled.Layouts.empty() && Missing(preCancelled) == 1 && firstCalls == 1,
            "pre-cancelled construction consumed original demand or entered a job");
        constructors = {};
        constructors.Register("slow", Construct([&](const Problem& input, const Options&)
        { ++firstCalls; std::this_thread::sleep_for(std::chrono::milliseconds(20)); return SingleAt(input, 0, 0); }));
        constructors.Register("later", Construct([&](const Problem& input, const Options&)
        { ++laterCalls; return SingleAt(input, 0, 0); }));
        options = Budget(); options.TimeLimitSeconds = .001;
        const auto timed = MakeConstructionPortfolioFactory(constructors)()->Run(p, options);
        Require(Validate(p, timed).Complete && firstCalls == 2 && laterCalls == 0 && timed.State == Status::Complete,
            "the shared construction deadline launched another job or discarded a legal incumbent");
        constructors = {};
        constructors.Register("slow-factory", [&]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return std::make_unique<Constructor>([&](const Problem& input, const Options&)
            { ++laterCalls; return SingleAt(input, 0, 0); });
        });
        const auto prepared = MakeConstructionPortfolioFactory(constructors)()->Run(p, options);
        Require(Validate(p, prepared).Valid && prepared.Layouts.empty() && Missing(prepared) == 1 && laterCalls == 0,
            "an over-budget factory started construction after the shared deadline");
    });
    Test("construction fractions reserve refinement work and preserve the default budget", []
    {
        const auto p = OneBox();
        for (const double fraction : {1., .5})
        {
            std::vector<std::size_t> shares; std::vector<double> seconds;
            StrategyRegistry constructors;
            constructors.Register("initial", Construct([&](const Problem& input, const Options& options)
            {
                shares.push_back(options.MaxPlacementAttempts); seconds.push_back(options.TimeLimitSeconds);
                auto result = SingleAt(input, 10, 10);
                result.Stats.PlacementAttempts = options.MaxPlacementAttempts; return result;
            }));
            StrategyPipeline pipeline;
            Require(pipeline.ConstructionBudgetFraction == 1, "the default construction fraction changed");
            pipeline.RequireCompleteConstruction = true;
            pipeline.ConstructionBudgetFraction = fraction;
            pipeline.Construct = MakeConstructionPortfolioFactory(constructors, {"initial", "initial"});
            int posts = 0;
            pipeline.PostProcessors.push_back(PostStage("reserved-refinement", [&](const Problem&, const Result& incumbent, const Options& options)
            {
                ++posts; Require(shares == std::vector<std::size_t>{2, 3} && options.MaxPlacementAttempts == 5,
                    "refinement did not receive the attempts reserved after initial competition");
                Require(options.TimeLimitSeconds > 15,
                    "unused constructor time disappeared instead of remaining available to refinement");
                auto result = incumbent; result.Stats = {}; result.Stats.PlacementAttempts = 2;
                result.Layouts[0].Placements[0].Transform = {0, 0, 0}; return result;
            }));
            auto options = Budget(); options.MaxPlacementAttempts = 10; options.Ranking = RankingPolicy::LowThenLeft;
            const auto result = Solve(p, std::move(pipeline), options); Complete(result);
            if (fraction == 1)
                Require(shares == std::vector<std::size_t>{5, 5} && posts == 0 && result.Output.Stats.PlacementAttempts == 10,
                    "the default construction budget changed or refined after exhausting its attempts");
            else
                Require(posts == 1 && result.Output.Stats.PlacementAttempts == 7 &&
                    seconds.size() == 2 && seconds[0] <= 7.5 && seconds[1] <= 15,
                    "the construction share or cumulative phase work exceeded its budget");
        }
        int constructed = 0, postCalls = 0;
        StrategyPipeline tiny; tiny.ConstructionBudgetFraction = .5; tiny.RequireCompleteConstruction = true;
        tiny.Construct = Construct([&](const Problem& input, const Options&)
        { ++constructed; return SingleAt(input, 0, 0); });
        tiny.PostProcessors.push_back(PostStage("never", [&](const Problem&, const Result& source, const Options&)
        { ++postCalls; return source; }));
        auto options = Budget(); options.MaxPlacementAttempts = 1;
        const auto empty = Solve(p, std::move(tiny), options);
        Require(constructed == 0 && postCalls == 0 && empty.Checked.Valid && Missing(empty.Output) == 1 &&
            empty.Output.Stats.PlacementAttempts == 0,
            "a reserved construction share rounded below one attempt still entered an algorithm");
    });
    Test("all policies retain interior down then left moves after an unchanged quality prefix", []
    {
        Problem p;p.Parts={Box("frame",10,10),Box("moving",1,1)};
        p.Parts[0].Geometry.Holes={{{1,1},{9,1},{9,9},{1,9}}};
        p.Stocks={{"sheet",20,20,1,1,"steel"}};
        Result original;original.State=Status::Complete;auto layout=NewLayout(p);
        layout.Placements={{"frame",0,{0,0,0}},{"moving",0,{4,5,0}}};original.Layouts.push_back(layout);
        auto down=original;down.Layouts[0].Placements[1].Transform.Y=4;
        auto left=down;left.Layouts[0].Placements[1].Transform.X=3;
        auto downRight=down;downRight.Layouts[0].Placements[1].Transform.X=5;
        auto upLeft=original;upLeft.Layouts[0].Placements[1].Transform.X=3;upLeft.Layouts[0].Placements[1].Transform.Y=6;
        const auto before=Assessed(p,original),lower=Assessed(p,down),lowerLeft=Assessed(p,left);
        const auto lowerRight=Assessed(p,downRight),higherLeft=Assessed(p,upLeft);
        for(const auto policy:{RankingPolicy::RegularRemnant,RankingPolicy::LowThenLeft,RankingPolicy::CompactArea})
        {
            const auto a=Detail::Quality(before,p,policy),b=Detail::Quality(lower,p,policy),c=Detail::Quality(lowerLeft,p,policy);
            Require(QualityPrefix(a)==QualityPrefix(b)&&QualityPrefix(b)==QualityPrefix(c),
                "fixture must retain every prior priority, including envelope and regular-remnant area");
            Require(b<a&&c<b,"material centroid must break equal-prefix ties down before left");
            const auto dr=Detail::Quality(lowerRight,p,policy),ul=Detail::Quality(higherLeft,p,policy);
            Require(QualityPrefix(dr)==QualityPrefix(a)&&QualityPrefix(ul)==QualityPrefix(a)&&dr<a&&a<ul,
                "downward material motion must precede a conflicting horizontal preference");
            int calls=0;StrategyPipeline pipeline;
            pipeline.Construct=Construct([original](const Problem&,const Options&){return original;});
            pipeline.PostProcessors.push_back(PostStage("local-down",[&](const Problem&,const Result& incumbent,const Options&)
            {++calls;auto candidate=incumbent;candidate.Stats={};candidate.Layouts[0].Placements[1].Transform.Y-=1;return candidate;}));
            pipeline.PostProcessors.push_back(PostStage("local-left",[&](const Problem&,const Result& incumbent,const Options&)
            {
                ++calls;Require(incumbent.Layouts[0].Placements[1].Transform.Y==4,"safe downward stage was discarded");
                auto candidate=incumbent;candidate.Stats={};candidate.Layouts[0].Placements[1].Transform.X-=1;return candidate;
            }));
            pipeline.PostProcessors.push_back(PostStage("reverse-up",[&](const Problem&,const Result& incumbent,const Options&)
            {++calls;auto candidate=incumbent;candidate.Stats={};candidate.Layouts[0].Placements[1].Transform.Y+=1;return candidate;}));
            pipeline.PostProcessors.push_back(PostStage("reverse-right",[&](const Problem&,const Result& incumbent,const Options&)
            {
                ++calls;Require(incumbent.Layouts[0].Placements[1].Transform.Y==4,"reverse upward stage was accepted");
                auto candidate=incumbent;candidate.Stats={};candidate.Layouts[0].Placements[1].Transform.X+=1;return candidate;
            }));
            auto options=Budget();options.Ranking=policy;const auto result=Solve(p,std::move(pipeline),options);Complete(result);
            const auto pose=result.Output.Layouts[0].Placements[1].Transform;
            Require(calls==4&&pose.X==3&&pose.Y==4,"pipeline must keep down/left and reject the reverse moves");
            Require(result.Output.Layouts[0].Placements[0].Transform.X==0&&result.Output.Layouts[0].Placements[0].Transform.Y==0,
                "fixed anchor geometry must not be moved by quality evaluation");
        }
    });
    Test("material moments respect rotated non-origin polygons holes and analytic circles", []
    {
        auto triangle=Box("triangle",1,1);triangle.Geometry.Outer={{10,20},{16,20},{10,23}};
        triangle.AllowedAnglesDegrees={90};
        auto holed=Box("holed",10,10,1,{10,20});holed.Geometry.Holes={{{11,21},{13,21},{13,23},{11,23}}};
        holed.AllowedAnglesDegrees={90};
        Part circle;circle.ID="circle";circle.Material="steel";circle.FreeRotation=false;circle.AllowedAnglesDegrees={90};
        circle.Geometry.Radius=2;circle.Geometry.Center={10,-4};
        const auto single=[](Part part,Pose pose)
        {
            Problem p;p.Parts={part};p.Stocks={{"sheet",100,100,1,1,"steel"}};
            Result r;r.State=Status::Complete;auto l=NewLayout(p);l.Placements.push_back({part.ID,0,pose});r.Layouts.push_back(l);
            return std::pair(p,r);
        };
        auto [tp,tr]=single(triangle,{30,5,90});CheckMaterialMean(tp,tr,9,17);
        auto [cp,cr]=single(circle,{80,60,90});CheckMaterialMean(cp,cr,84,70);
        const double circleArea=4*std::acos(-1.);
        for(int winding=0;winding<4;++winding)
        {
            auto shape=holed;
            if(winding&1)std::reverse(shape.Geometry.Outer.begin(),shape.Geometry.Outer.end());
            if(winding&2)std::reverse(shape.Geometry.Holes[0].begin(),shape.Geometry.Holes[0].end());
            auto [hp,hr]=single(shape,{60,5,90});CheckMaterialMean(hp,hr,34.875,20.125);
            Problem mixed;mixed.Parts={triangle,shape,circle};mixed.Stocks={{"sheet",100,100,1,1,"steel"}};
            Result r;r.State=Status::Complete;auto l=NewLayout(mixed);
            l.Placements={{"triangle",0,{30,5,90}},{"holed",0,{60,5,90}},{"circle",0,{80,60,90}}};r.Layouts.push_back(l);
            const double mass=9+96+circleArea;
            CheckMaterialMean(mixed,r,(9*9+96*34.875+circleArea*84)/mass,(9*17+96*20.125+circleArea*70)/mass);
        }
    });
    Test("thin-frame material moments retain binary64 vertex precision at the coordinate limit", []
    {
        const double length=1e7,inset=1e-4;
        // These are exact-rational expectations of the actual binary64 input
        // vertices. length-inset has a different rounding error from inset;
        // the true centroid therefore differs from the bounding-box center.
        const double expectedCenter=4999997.876582726;
        const double expectedRotatedX=5000002.123417274;
        for(int winding=0;winding<4;++winding)
        {
            Problem p;p.Parts={Box("frame",length,length)};
            auto& frame=p.Parts[0];frame.AllowedAnglesDegrees={0,90};
            frame.Geometry.Holes={{{inset,inset},{length-inset,inset},
                {length-inset,length-inset},{inset,length-inset}}};
            if(winding&1)std::reverse(frame.Geometry.Outer.begin(),frame.Geometry.Outer.end());
            if(winding&2)std::reverse(frame.Geometry.Holes[0].begin(),frame.Geometry.Holes[0].end());
            p.Stocks={{"sheet",length,length,1,1,"steel"}};
            const auto original=SingleAt(p,0,0);
            CheckMaterialMean(p,original,expectedCenter,expectedCenter);
            auto rotated=original;rotated.Layouts[0].Placements[0].Transform={length,0,90};
            CheckMaterialMean(p,rotated,expectedRotatedX,expectedCenter);
            p.Parts.push_back(Box("payload",60,50));
            auto mixed=original;mixed.Layouts[0].Placements.push_back({"payload",0,{1e6,2e6,0}});
            // True frame area is 3999.9966024952478, not the rounded 4000
            // retained by the historical quality prefix's area calculation.
            CheckMaterialMean(p,mixed,3285725.0974401417,3714294.5911639836);
        }
    });
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
    Test("rejected neighborhood diagnostics remain visible without publishing candidate certificates", []
    {
        const auto p = OneBox();
        StrategyPipeline pipeline;
        pipeline.Construct = Construct([](const Problem& input, const Options&)
            { return SingleAt(input, 0, 0); });
        pipeline.PostProcessors.push_back(PostStage("neighborhood", [](const Problem& input,
            const Result&, const Options&)
        {
            auto candidate = SingleAt(input, 10, 10);
            candidate.Diagnostics.push_back("tried a complete cross-board neighborhood");
            candidate.Diagnostics.push_back("compact.axis.v1 closed=1 candidate-only-certificate");
            return candidate;
        }));
        const auto outcome = Solve(p, std::move(pipeline));
        Complete(outcome);
        const auto& pose = outcome.Output.Layouts.front().Placements.front().Transform;
        Require(pose.X == 0 && pose.Y == 0, "rejected candidate replaced the checked incumbent");
        bool visible = false;
        for (const auto& entry : outcome.Output.Diagnostics)
        {
            visible |= entry == "pipeline.rejected=neighborhood | tried a complete cross-board neighborhood";
            Require(entry.rfind("compact.axis.v1 ", 0) != 0,
                "a rejected candidate certificate was attached to retained poses");
        }
        Require(visible, "rejected search work disappeared from the final diagnostics");
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
        for (const double fraction : {0., -1., 1.01, std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN()})
        {
            auto definition = valid(); definition.ConstructionBudgetFraction = fraction; invalid(std::move(definition));
        }
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
