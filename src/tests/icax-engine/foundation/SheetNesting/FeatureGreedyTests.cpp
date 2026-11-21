#include "SheetNestingFeatureGreedy.h"
#include "SheetNestingNfp.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <tuple>

using namespace iCAX::SheetNesting;

namespace
{
    int Passed = 0, Failed = 0;

    void Require(bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    void Test(const char* name, const std::function<void()>& body)
    {
        try { body(); ++Passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error)
        { ++Failed; std::cerr << "FAIL " << name << " " << error.what() << '\n'; }
    }

    Part Polygon(std::string id, std::vector<Point> points, std::size_t quantity = 1)
    {
        Part part;
        part.ID = std::move(id);
        part.Geometry.Outer = std::move(points);
        part.Quantity = quantity;
        part.Material = "material";
        part.FreeRotation = false;
        part.AllowedAnglesDegrees = {0};
        return part;
    }

    Part Rectangle(std::string id, double width, double height, std::size_t quantity = 1)
    {
        return Polygon(std::move(id), {{0,0},{width,0},{width,height},{0,height}}, quantity);
    }

    Part Channel(std::string id = "channel", std::size_t quantity = 1)
    {
        return Polygon(std::move(id), {{0,5},{14,5},{14,0},{12,0},
            {12,3},{2,3},{2,0},{0,0}}, quantity);
    }

    Problem Sheet(std::vector<Part> parts, double width = 40, double height = 30)
    {
        Problem problem;
        problem.Tolerance = 1e-7;
        problem.Parts = std::move(parts);
        problem.Stocks = {{"sheet", width, height, 1, 1, "material"}};
        return problem;
    }

    Options Budget()
    {
        Options options;
        options.TimeLimitSeconds = 4;
        options.MaxPlacementAttempts = 200000;
        options.EnableBasicCompaction = false;
        return options;
    }

    std::string Fingerprint(const Problem& problem)
    {
        std::ostringstream text;
        text << std::setprecision(17) << problem.PartGap << ',' << problem.EdgeMargin << ','
            << problem.Tolerance << ',' << problem.AllowCommonEdges;
        for (const auto& part : problem.Parts)
        {
            text << " part " << part.ID << ',' << part.Quantity << ',' << part.Material << ',' << part.FreeRotation;
            for (const auto angle : part.AllowedAnglesDegrees) text << " angle " << angle;
            for (const auto point : part.Geometry.Outer) text << " outer " << point.X << ',' << point.Y;
            for (const auto& hole : part.Geometry.Holes)
            {
                text << " hole";
                for (const auto point : hole) text << ' ' << point.X << ',' << point.Y;
            }
            text << " circle " << part.Geometry.Radius << ',' << part.Geometry.Center.X << ',' << part.Geometry.Center.Y;
        }
        for (const auto& stock : problem.Stocks)
            text << " stock " << stock.ID << ',' << stock.Width << ',' << stock.Height << ','
                << stock.Quantity << ',' << stock.Cost << ',' << stock.Material;
        return text.str();
    }

    using PlacementKey = std::tuple<std::string, std::size_t, double, double, double, std::size_t>;

    std::vector<PlacementKey> Placements(const std::vector<Placement>& placements)
    {
        std::vector<PlacementKey> keys;
        for (const auto& placement : placements)
            keys.emplace_back(placement.PartID, placement.Instance, placement.Transform.X,
                placement.Transform.Y, placement.Transform.AngleDegrees, placement.Group);
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    std::string ResultFingerprint(const Result& result)
    {
        std::ostringstream text;
        text << std::setprecision(17) << static_cast<int>(result.State);
        for (const auto& board : result.Layouts)
        {
            text << " stock " << board.StockID << ',' << board.StockInstance << ',' << board.Width << ',' << board.Height;
            for (const auto& [part, instance, x, y, angle, group] : Placements(board.Placements))
                text << " part " << part << ',' << instance << ',' << x << ',' << y << ',' << angle << ',' << group;
        }
        for (const auto& missing : result.Remaining) text << " remaining " << missing.PartID << ',' << missing.Quantity;
        return text.str();
    }

    void OriginalAudit(const Problem& problem, const Result& result)
    {
        Require(Validate(problem, result).Valid, "original problem validation failed");
        Require(result.Layouts.size() <= 1, "single-sheet greedy consumed another physical sheet");
        std::map<std::string, const Part*> parts;
        std::map<std::string, std::size_t> placed, remaining;
        std::set<std::pair<std::string, std::size_t>> instances;
        for (const auto& part : problem.Parts) parts.emplace(part.ID, &part);
        for (const auto& item : result.Remaining)
        {
            Require(parts.count(item.PartID) && item.Quantity && !remaining.count(item.PartID),
                "remaining original demand is unknown, repeated or empty");
            remaining[item.PartID] = item.Quantity;
        }
        for (const auto& board : result.Layouts)
        {
            std::vector<AxisSafety::Geometry> geometry;
            for (const auto& placement : board.Placements)
            {
                Require(parts.count(placement.PartID), "greedy invented a specification");
                const auto& part = *parts.at(placement.PartID);
                Require(placement.Instance < part.Quantity && instances.emplace(part.ID, placement.Instance).second,
                    "greedy repeated or exceeded an original instance");
                Require(placement.Group == 0, "greedy locked an original instance into a group");
                Require(Detail::AngleAllowed(part, placement.Transform.AngleDegrees, problem.Tolerance),
                    "greedy widened the original angle domain");
                const auto actual = AxisSafety::Make(part.Geometry, placement.Transform);
                Require(actual.Valid && AxisSafety::Inside(actual, board.Width, board.Height, problem.EdgeMargin),
                    "original material crossed the actual sheet boundary");
                for (const auto& other : geometry)
                    Require(AxisSafety::PairExact(actual, other, problem.PartGap, problem.AllowCommonEdges,
                        problem.Tolerance) == AxisSafety::Verdict::Safe, "original material or required clearance overlaps");
                geometry.push_back(actual);
                ++placed[part.ID];
            }
        }
        for (const auto& part : problem.Parts)
            Require(placed[part.ID] + remaining[part.ID] == part.Quantity, "greedy lost original demand");
    }

    JobResult Solve(const Problem& problem, Options options = Budget(), FeatureGreedySettings settings = {},
        std::shared_ptr<NfpCache> cache = {})
    {
        const auto before = Fingerprint(problem);
        StrategyRegistry registry;
        registry.Register("greedy", MakeFeatureGreedyFactory(settings, std::move(cache)));
        const auto solved = RunJob({"greedy-original-input", "greedy", problem, options}, registry);
        Require(Fingerprint(problem) == before, "greedy mutated original source geometry or inventory");
        Require(solved.Output.Stats.PlacementAttempts <= options.MaxPlacementAttempts, "job attempt cap was exceeded");
        return solved;
    }

    Result StateResult(const Problem& problem, const ConstructionTraceEvent& event,
        const std::vector<Placement>& placements)
    {
        Require(event.StockIndex < problem.Stocks.size(), "trace lost its stock identity");
        const auto& stock = problem.Stocks[event.StockIndex];
        Require(event.BoardIndex == 0 && event.BoardWidth == stock.Width && event.BoardHeight == stock.Height,
            "trace changed the actual board dimensions or board index");
        Result result;
        result.State = Status::Complete;
        Layout board;
        board.StockID = stock.ID;
        board.StockInstance = event.StockInstance;
        board.Width = stock.Width;
        board.Height = stock.Height;
        board.Placements = placements;
        std::map<std::string, std::size_t> used;
        for (const auto& placement : placements) ++used[placement.PartID];
        for (const auto& part : problem.Parts)
        {
            board.MaterialArea += used[part.ID] * Detail::Area(part.Geometry);
            Require(used[part.ID] <= part.Quantity, "trace delta exceeds original demand");
            if (used[part.ID] < part.Quantity)
            {
                result.State = Status::Partial;
                result.Remaining.push_back({part.ID, part.Quantity - used[part.ID]});
            }
        }
        if (!placements.empty()) result.Layouts.push_back(std::move(board));
        return result;
    }

    void VerifyTrace(const Problem& problem, const Result& result, const std::vector<ConstructionTraceEvent>& events)
    {
        Require(!events.empty() && events.front().Stage == ConstructionTraceStage::Begin, "greedy trace lacks Begin");
        std::map<std::uint64_t, std::vector<Placement>> states{{1, {}}};
        const ConstructionTraceEvent* selected = nullptr;
        std::uint64_t previousStep = 0;
        bool firstEvent = true;
        for (const auto& event : events)
        {
            Require((firstEvent || event.Step > previousStep) && !event.Truncated,
                "unlimited trace lost or reordered events");
            previousStep = event.Step;
            firstEvent = false;
            if (event.Stage == ConstructionTraceStage::Candidate && event.Accepted)
            {
                Require(event.StateID && !states.count(event.StateID) && states.count(event.ParentStateID),
                    "accepted greedy delta has an invalid state or missing parent");
                Require(event.Members.size() == 1, "one greedy step placed multiple original instances");
                const auto& member = event.Members.front();
                Require(member.PartIndex < problem.Parts.size() && member.Group == 0,
                    "accepted greedy delta invented or grouped original material");
                auto placements = states.at(event.ParentStateID);
                const auto previousCount = placements.size();
                placements.push_back({problem.Parts[member.PartIndex].ID, member.Instance, member.Transform, member.Group});
                Require(placements.size() == previousCount + 1, "greedy step did not add exactly one original");
                OriginalAudit(problem, StateResult(problem, event, placements));
                states.emplace(event.StateID, std::move(placements));
            }
            if (event.Stage == ConstructionTraceStage::Candidate && !event.Accepted)
                Require(event.StateID == 0, "rejected greedy fit entered the accepted state tree");
            if (event.Stage == ConstructionTraceStage::Selected)
            {
                Require(!selected, "greedy emitted more than one final Selected event");
                selected = &event;
            }
        }
        Require(selected && states.count(selected->StateID), "greedy Selected has no recoverable state");
        std::vector<Placement> selectedMembers, actual;
        for (const auto& member : selected->Members)
        {
            Require(member.PartIndex < problem.Parts.size() && member.Group == 0, "Selected lost an original identity");
            selectedMembers.push_back({problem.Parts[member.PartIndex].ID, member.Instance, member.Transform, member.Group});
        }
        for (const auto& board : result.Layouts)
            actual.insert(actual.end(), board.Placements.begin(), board.Placements.end());
        Require(Placements(states.at(selected->StateID)) == Placements(actual) &&
            Placements(selectedMembers) == Placements(actual), "trace final poses differ from the original native result");
    }
}

int main()
{
    Test("greedy trace adds one original per state with no locked groups", []
    {
        const auto problem = Sheet({Rectangle("tile", 2, 2, 6)}, 6, 4);
        std::vector<ConstructionTraceEvent> events;
        FeatureGreedySettings settings;
        settings.Trace = [&](const auto& event) { events.push_back(event); };
        const auto job = Solve(problem, Budget(), settings);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 6, "six independent rectangles were not completed");
        VerifyTrace(problem, job.Output, events);
    });

    Test("greedy closes U and original rectangular filler without preassembly", []
    {
        const auto problem = Sheet({Channel(), Rectangle("fill", 10, 3)}, 14, 5);
        std::vector<ConstructionTraceEvent> events;
        FeatureGreedySettings settings;
        settings.Trace = [&](const auto& event) { events.push_back(event); };
        const auto job = Solve(problem, Budget(), settings);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 2 &&
            std::abs(job.Checked.MaterialArea - 70) <= 1e-7, "feasible U and filler were not packed into their exact material rectangle");
        Require(std::none_of(events.begin(), events.end(), [](const auto& event)
            { return event.Stage == ConstructionTraceStage::Match; }),
            "normal different-original single placements still prepared an excluded mixed pair");
        VerifyTrace(problem, job.Output, events);
    });

    Test("greedy retains distant verified fits when local bounding-box proposals overlap", []
    {
        const auto problem = Sheet({Rectangle("tile", 2, 2, 72)}, 24, 12);
        auto options = Budget();
        options.TimeLimitSeconds = 8;
        const auto job = Solve(problem, options);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 72 && job.Output.Layouts.size() == 1,
            "overlapping local proposals hid feasible original tiles elsewhere on the same sheet");
        Require(std::abs(job.Checked.MaterialArea - 288) <= 1e-7,
            "complete tile packing changed the original material area");
    });

    Test("greedy preserves unequal specification quantities and instances", []
    {
        const auto problem = Sheet({Rectangle("wide", 5, 2, 3), Rectangle("small", 2, 2, 7), Channel("one-U")}, 50, 30);
        const auto job = Solve(problem);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 11, "unequal quantities were not completed on a generous sheet");
    });

    Test("greedy respects an original ninety degree only orientation", []
    {
        auto part = Rectangle("quarter-only", 8, 3);
        part.AllowedAnglesDegrees = {90};
        const auto problem = Sheet({part}, 3, 8);
        const auto job = Solve(problem);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 1, "the sole legal quarter-turn placement was missed");
    });

    Test("greedy arbitrary angle source remains free and unchanged", []
    {
        auto part = Rectangle("free", 8, 3, 2);
        for (auto& point : part.Geometry.Outer) point = Detail::Transform(point, {2,-7,17});
        part.FreeRotation = true;
        part.AllowedAnglesDegrees.clear();
        const auto problem = Sheet({part}, 40, 30);
        const auto job = Solve(problem);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 2, "free source originals were not placed on a generous sheet");
    });

    Test("greedy retains positive gap and original edge margin", []
    {
        auto problem = Sheet({Rectangle("spaced", 3, 2, 4)}, 8, 6);
        problem.PartGap = .25;
        problem.EdgeMargin = .5;
        const auto job = Solve(problem);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 4, "feasible spaced rectangles were left incomplete");
    });

    Test("greedy preserves translated reversed contours and original holes", []
    {
        auto channel = Channel("translated", 2);
        std::reverse(channel.Geometry.Outer.begin(), channel.Geometry.Outer.end());
        for (auto& point : channel.Geometry.Outer) { point.X += 100; point.Y -= 30; }
        auto frame = Rectangle("frame", 12, 12);
        frame.Geometry.Holes = {{{2,2},{2,10},{10,10},{10,2}}};
        const auto problem = Sheet({channel, frame}, 60, 40);
        const auto job = Solve(problem);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 3, "translated source shapes or holed material were omitted");
    });

    Test("greedy single-sheet scope preserves remaining stock and part inventory", []
    {
        auto problem = Sheet({Rectangle("full-sheet", 5, 2, 3)}, 5, 2);
        problem.Stocks.front().Quantity = 3;
        const auto job = Solve(problem);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.PlacedCount == 1 && !job.Checked.Complete && job.Output.Layouts.size() == 1,
            "single-sheet job consumed another sheet or claimed missing originals complete");
    });

    Test("greedy skips incompatible and zero-quantity stock alternatives", []
    {
        auto problem = Sheet({Rectangle("compatible", 3, 2)}, 3, 2);
        problem.Stocks.insert(problem.Stocks.begin(), {"wrong", 3, 2, 1, 1, "other-material"});
        problem.Stocks.insert(problem.Stocks.begin(), {"empty-stock", 3, 2, 0, 1, "material"});
        const auto job = Solve(problem);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Output.Layouts.front().StockID == "sheet",
            "valid compatible stock was hidden by an unavailable alternative");
    });

    Test("greedy leaves incompatible original material explicitly unplaced", []
    {
        auto incompatible = Rectangle("other", 2, 2);
        incompatible.Material = "other-material";
        const auto problem = Sheet({Rectangle("matching", 2, 2), incompatible});
        const auto job = Solve(problem);
        OriginalAudit(problem, job.Output);
        Require(!job.Checked.Complete && job.Checked.PlacedCount == 1, "incompatible material crossed the selected stock");
    });

    Test("greedy cancellation before placement preserves all originals", []
    {
        const auto problem = Sheet({Channel("cancelled-U", 5), Rectangle("cancelled-fill", 10, 3, 3)});
        auto options = Budget();
        options.Cancel = [] { return true; };
        const auto job = Solve(problem, options);
        OriginalAudit(problem, job.Output);
        Require(job.Output.State == Status::Cancelled && job.Checked.PlacedCount == 0,
            "pre-cancelled job placed material or lost cancellation status");
    });

    Test("greedy low attempt and memory caps retain truthful original demand", []
    {
        const auto problem = Sheet({Channel("bulk-U", 5000), Rectangle("bulk-fill", 10, 3, 5000)}, 14, 5);
        auto options = Budget();
        options.TimeLimitSeconds = .1;
        options.MaxPlacementAttempts = 2;
        FeatureGreedySettings settings;
        settings.MaxRetainedBytes = 4096;
        settings.MaxSpecificationsPerStep = 1;
        settings.MaxAnglesPerPart = 1;
        settings.MaxCandidatesPerPart = 1;
        const auto job = Solve(problem, options, settings);
        OriginalAudit(problem, job.Output);
        Require(!job.Checked.Complete && job.Checked.PlacedCount <= 2 && job.Output.State != Status::Failed,
            "resource exhaustion escaped as failure or fabricated completion");
    });

    Test("greedy zero demand completes without consuming a physical sheet", []
    {
        const auto problem = Sheet({Rectangle("none", 2, 2, 0)});
        const auto job = Solve(problem);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 0 && job.Output.Layouts.empty(),
            "empty original demand consumed or invented material");
    });

    Test("normal greedy job entry rejects invalid budgets and material", []
    {
        const auto problem = Sheet({Rectangle("valid", 2, 2)});
        auto options = Budget();
        options.TimeLimitSeconds = 0;
        auto job = Solve(problem, options);
        Require(job.Output.State == Status::InvalidInput && job.Output.Layouts.empty(), "zero-time job was accepted");
        options = Budget(); options.MaxPlacementAttempts = 0;
        job = Solve(problem, options);
        Require(job.Output.State == Status::InvalidInput && job.Output.Layouts.empty(), "zero-attempt job was accepted");
        options = Budget(); options.TimeLimitSeconds = std::numeric_limits<double>::quiet_NaN();
        job = Solve(problem, options);
        Require(job.Output.State == Status::InvalidInput, "nonfinite budget entered native construction");
        job = Solve(Sheet({Polygon("crossed-source", {{0,0},{4,4},{0,4},{4,0}})}));
        Require(job.Output.State == Status::InvalidInput && job.Output.Layouts.empty(), "self-crossing original material entered search");
    });

    Test("greedy trace cap preserves native output and reports observation loss", []
    {
        const auto problem = Sheet({Rectangle("observed", 2, 2, 6)}, 6, 4);
        const auto normal = Solve(problem);
        std::vector<ConstructionTraceEvent> events;
        FeatureGreedySettings settings;
        settings.MaxTraceEvents = 3;
        settings.Trace = [&](const auto& event) { events.push_back(event); };
        const auto traced = Solve(problem, Budget(), settings);
        OriginalAudit(problem, traced.Output);
        Require(events.size() <= settings.MaxTraceEvents, "trace observer exceeded its cap");
        Require(std::any_of(events.begin(), events.end(), [](const auto& event) { return event.Truncated; }),
            "bounded trace omitted actual observation without a truncation marker");
        Require(ResultFingerprint(normal.Output) == ResultFingerprint(traced.Output), "trace cap changed the actual native layout");
    });

    Test("greedy observer exception is isolated from normal original placement", []
    {
        const auto problem = Sheet({Rectangle("observed", 2, 2, 6)}, 6, 4);
        const auto normal = Solve(problem);
        std::size_t calls = 0;
        FeatureGreedySettings settings;
        settings.Trace = [&](const auto&) { ++calls; throw std::runtime_error("observer-only failure"); };
        const auto observed = Solve(problem, Budget(), settings);
        OriginalAudit(problem, observed.Output);
        Require(calls > 0 && ResultFingerprint(normal.Output) == ResultFingerprint(observed.Output),
            "observer exception escaped or changed the actual native output");
    });

    Test("all greedy ordering jobs preserve independent ungrouped originals", []
    {
        const auto problem = Sheet({Rectangle("A", 4, 2, 3), Rectangle("B", 2, 2, 2)}, 16, 12);
        for (const auto order : {FeatureGreedyOrder::IncrementalArea, FeatureGreedyOrder::IncrementalWaste,
            FeatureGreedyOrder::LowThenLeft, FeatureGreedyOrder::RemainingArea})
        {
            FeatureGreedySettings settings;
            settings.Order = order;
            const auto job = Solve(problem, Budget(), settings);
            OriginalAudit(problem, job.Output);
            Require(job.Checked.Complete && job.Checked.PlacedCount == 5, "an ordering job lost feasible original quantities");
        }
    });

    Test("concurrent greedy jobs share cache without mutable placement state", []
    {
        const auto first = Sheet({Rectangle("first", 2, 2, 6)}, 6, 4);
        auto rotated = Rectangle("second", 8, 3);
        rotated.AllowedAnglesDegrees = {90};
        const auto second = Sheet({rotated}, 3, 8);
        auto cache = std::make_shared<NfpCache>();
        JobResult firstJob, secondJob;
        std::exception_ptr firstError, secondError;
        std::thread a([&] { try { firstJob = Solve(first, Budget(), {}, cache); } catch (...) { firstError = std::current_exception(); } });
        std::thread b([&] { try { secondJob = Solve(second, Budget(), {}, cache); } catch (...) { secondError = std::current_exception(); } });
        a.join(); b.join();
        if (firstError) std::rethrow_exception(firstError);
        if (secondError) std::rethrow_exception(secondError);
        OriginalAudit(first, firstJob.Output);
        OriginalAudit(second, secondJob.Output);
        Require(firstJob.Checked.Complete && secondJob.Checked.Complete, "concurrent original inventory or angles leaked");
    });

    Test("pinwheel quantity controls report actual greedy completion without forcing an optimum", []
    {
        const std::vector<std::pair<FeatureGreedyOrder, const char*>> orders{
            {FeatureGreedyOrder::IncrementalArea, "incremental-area"},
            {FeatureGreedyOrder::IncrementalWaste, "incremental-waste"},
            {FeatureGreedyOrder::LowThenLeft, "low-then-left"},
            {FeatureGreedyOrder::RemainingArea, "remaining-area"}};
        for (const std::size_t repetitions : {std::size_t{1}, std::size_t{2}})
        {
            const auto quantity = repetitions * 2;
            const auto problem = Sheet({Rectangle("vertical", 4, 6, quantity),
                Rectangle("horizontal", 6, 4, quantity)}, 10 * static_cast<double>(repetitions), 10);
            for (const auto& [order, name] : orders)
            {
                FeatureGreedySettings settings;
                settings.Order = order;
                const auto job = Solve(problem, Budget(), settings);
                OriginalAudit(problem, job.Output);
                std::cout << "OBSERVE pinwheel order=" << name << " types=2 quantity-each=" << quantity
                    << " placed=" << job.Checked.PlacedCount << '/' << quantity * 2
                    << " complete=" << job.Checked.Complete << '\n';
            }
        }
    });

    std::cout << Passed << " passed, " << Failed << " failed\n";
    return Failed ? 1 : 0;
}
