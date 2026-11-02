#include "NestingInternal.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

using namespace iCAX::SheetNesting;
using namespace iCAX::SheetNesting::Detail;

namespace
{
    void Require(bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    void RectangleBoundaryCases()
    {
        constexpr double tolerance = 1e-8;
        MaxRects zero(0, 10, tolerance);
        Require(zero.FreeRectangles().empty(), "A zero-width bin must have no free rectangle.");
        MaxRects invalid(std::numeric_limits<double>::infinity(), 10, tolerance);
        Require(invalid.FreeRectangles().empty(), "A non-finite bin must have no free rectangle.");

        MaxRects exact(10, 8, tolerance);
        Require(!exact.Find(0, 2, RectScore::Area), "A zero-width query must not fit.");
        Require(!exact.Find(11, 2, RectScore::Area), "An oversized query must not fit.");
        Require(!exact.Find(std::numeric_limits<double>::quiet_NaN(), 2, RectScore::Area),
            "A non-finite query must not fit.");
        const auto fit = exact.Find(10, 8, RectScore::Area);
        Require(fit.has_value(), "An exact fit must be retained.");
        exact.Occupy(fit->Box);
        Require(exact.FreeRectangles().empty(), "An exact occupancy must consume the bin.");

        MaxRects unrotated(6, 10, tolerance);
        Require(!unrotated.Find(10, 6, RectScore::Area), "MaxRects must not silently rotate a pattern.");
        MaxRects split(10, 10, tolerance);
        split.Occupy({0, 0, 3, 4});
        const auto vertical = split.Find(7, 10, RectScore::ShortSide);
        Require(vertical && vertical->Box.X == 3 && vertical->Box.Y == 0, "The full right strip must remain usable.");
        const auto horizontal = split.Find(10, 6, RectScore::Area);
        Require(horizontal && horizontal->Box.X == 0 && horizontal->Box.Y == 4, "The full top strip must remain usable.");

        // The right sliver gives the best short-side fit, while the top region
        // gives both the smallest wasted area and the best long-side fit.
        MaxRects scores(10, 1000, tolerance);
        scores.Occupy({0, 0, 9, 989});
        const auto shortSide = scores.Find(1, 10, RectScore::ShortSide);
        const auto area = scores.Find(1, 10, RectScore::Area);
        const auto longSide = scores.Find(1, 10, RectScore::LongSide);
        Require(shortSide && shortSide->Box.X == 9 && shortSide->Box.Y == 0, "Short-side scoring chose the wrong region.");
        Require(area && area->Box.X == 0 && area->Box.Y == 989, "Area scoring chose the wrong region.");
        Require(longSide && longSide->Box.X == 0 && longSide->Box.Y == 989, "Long-side scoring chose the wrong region.");
    }

    std::size_t RandomizedFreeSpaceCoverage()
    {
        constexpr int width = 32, height = 24;
        constexpr double tolerance = 1e-8;
        std::mt19937 random(173);
        std::size_t placements = 0;
        for (int trial = 0; trial < 200; ++trial)
        {
            MaxRects packing(width, height, tolerance);
            bool occupied[height][width]{};
            for (int step = 0; step < 100; ++step)
            {
                const int w = 1 + random() % 10, h = 1 + random() % 10;
                const auto fit = packing.Find(w, h, static_cast<RectScore>(step % 3));
                if (!fit) continue;
                const int x = static_cast<int>(fit->Box.X), y = static_cast<int>(fit->Box.Y);
                Require(x >= 0 && y >= 0 && x + w <= width && y + h <= height, "Random rectangle escaped its bin.");
                for (int v = y; v < y + h; ++v)
                    for (int u = x; u < x + w; ++u)
                    {
                        Require(!occupied[v][u], "Random rectangles overlap.");
                        occupied[v][u] = true;
                    }
                packing.Occupy(fit->Box);
                ++placements;

                // All coordinates here are integer. Unit-cell occupancy is an
                // independent oracle for the complete union of remaining space.
                // It detects lost slivers and stale intersecting free rectangles,
                // including failures that ordinary no-overlap tests would miss.
                const auto& free = packing.FreeRectangles();
                for (int v = 0; v < height; ++v)
                    for (int u = 0; u < width; ++u)
                    {
                        bool covered = false;
                        for (const auto& rectangle : free)
                            if (u + 0.5 > rectangle.X && u + 0.5 < rectangle.X + rectangle.Width
                                && v + 0.5 > rectangle.Y && v + 0.5 < rectangle.Y + rectangle.Height)
                                covered = true;
                        Require(covered != occupied[v][u], "The free-rectangle union differs from the actual remaining board.");
                    }
                for (std::size_t i = 0; i < free.size(); ++i)
                    for (std::size_t j = 0; j < free.size(); ++j)
                        if (i != j)
                        {
                            const auto& a = free[i];
                            const auto& b = free[j];
                            Require(!(a.X >= b.X && a.Y >= b.Y && a.X + a.Width <= b.X + b.Width
                                && a.Y + a.Height <= b.Y + b.Height), "A redundant contained free rectangle survived pruning.");
                        }
            }
        }
        return placements;
    }

    void CheckConservation(const JobResult& run, const Problem& input, std::size_t attempts)
    {
        std::string errors;
        for (const auto& message : run.Checked.Errors) errors += message + "; ";
        Require(run.Checked.Valid && run.Output.GeometryValidated, "An interrupted job returned invalid geometry or counts: " + errors);
        std::size_t remaining = 0;
        for (const auto& item : run.Output.Remaining) remaining += item.Quantity;
        Require(run.Checked.PlacedCount + remaining == input.Parts.front().Quantity,
            "An interrupted job lost or duplicated demanded instances.");
        Require(run.Output.Stats.PlacementAttempts <= attempts, "A job exceeded its placement-attempt budget.");
    }

    void BudgetAndCancellationCuts()
    {
        const auto registry = DefaultStrategies();
        std::size_t runs = 0, cancelledWithPlacements = 0;
        for (int shape = 0; shape < 2; ++shape)
        {
            Problem input;
            input.PartGap = 1;
            input.EdgeMargin = 2;
            Part part;
            part.ID = "part";
            part.Quantity = 30;
            if (shape == 0) part.Geometry.Outer = {{0, 0}, {10, 0}, {0, 10}};
            else part.Geometry.Radius = 5;
            input.Parts.push_back(part);
            input.Stocks.push_back({"stock", 90, 70, 3, 1, {}});
            const std::string strategy = shape == 0 ? "rules.composite" : "rules.circles";

            // Cut execution at many distinct control-flow points, including
            // pattern generation, circle-grid construction and partial commits.
            // The callback uses a deterministic call count, not wall-clock timing.
            for (int nth = 1; nth <= 200; ++nth)
            {
                int calls = 0;
                Options options;
                options.TimeLimitSeconds = 10;
                options.MaxPatterns = 64;
                options.MaxPatternMembers = 2;
                options.Cancel = [&calls, nth] { return ++calls >= nth; };
                const Job job{"cancel-review", strategy, input, options};
                const auto run = RunJob(job, registry);
                CheckConservation(run, input, options.MaxPlacementAttempts);
                if (run.Output.State == Status::Cancelled && run.Checked.PlacedCount > 0) ++cancelledWithPlacements;
                Require(input.Parts.front().Quantity == 30 && input.Stocks.front().Quantity == 3,
                    "Job execution mutated the caller's input inventory.");
                ++runs;
            }
            for (std::size_t attempts = 1; attempts <= 100; ++attempts)
            {
                Options options;
                options.TimeLimitSeconds = 10;
                options.MaxPlacementAttempts = attempts;
                options.MaxPatterns = 64;
                options.MaxPatternMembers = 2;
                const Job job{"budget-review", strategy, input, options};
                CheckConservation(RunJob(job, registry), input, attempts);
                ++runs;
            }
        }
        Require(cancelledWithPlacements > 0, "Cancellation coverage never exercised retention of a partial layout.");
        std::cout << "Passed " << runs << " cancellation/attempt-budget cuts; " << cancelledWithPlacements
            << " cancelled runs retained placements.\n";
    }
}

int main()
{
    try
    {
        RectangleBoundaryCases();
        const auto count = RandomizedFreeSpaceCoverage();
        std::cout << "Passed rectangle boundaries/scoring and 200 free-space coverage trials (" << count << " placements).\n";
        BudgetAndCancellationCuts();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Nesting rectangle/budget regression failed: " << error.what() << '\n';
        return 1;
    }
}
