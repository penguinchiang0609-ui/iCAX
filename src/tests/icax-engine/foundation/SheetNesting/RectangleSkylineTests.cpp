#include "NestingInternal.h"
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace iCAX::SheetNesting;
namespace iCAX::SheetNesting::Detail { Result RunRectangleSkyline(const Problem&, const Options&); }
namespace
{
    void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    Part Box(std::string id, double width, double height, std::size_t count = 1)
    {
        Part part; part.ID = std::move(id); part.Quantity = count; part.Material = "m";
        part.FreeRotation = false; part.AllowedAnglesDegrees = {0};
        part.Geometry.Outer = {{0,0},{width,0},{width,height},{0,height}};
        return part;
    }
    Options Budget()
    {
        Options options; options.TimeLimitSeconds = 3; options.MaxPlacementAttempts = 200000;
        options.EnableCombinations = false; return options;
    }
    JobResult Solve(const Problem& problem, Options options = Budget())
    { return RunJob({"skyline-test", "rect.skyline-bl", problem, std::move(options)}, DefaultStrategies()); }
    void Valid(const Problem& problem, const JobResult& result)
    {
        for (const auto& error : result.Checked.Errors) std::cerr << error << '\n';
        for (const auto& error : result.Output.Diagnostics) if (result.Output.State == Status::Failed) std::cerr << error << '\n';
        Require(result.Checked.Valid, "skyline must retain legal original geometry and finite inventory");
        std::size_t demanded = 0, remaining = 0, placed = 0;
        for (const auto& part : problem.Parts) demanded += part.Quantity;
        for (const auto& item : result.Output.Remaining) remaining += item.Quantity;
        std::set<std::pair<std::string, std::size_t>> identities;
        for (const auto& layout : result.Output.Layouts)
            for (const auto& item : layout.Placements)
            { ++placed; Require(identities.emplace(item.PartID, item.Instance).second, "original instance identity is unique"); }
        Require(placed + remaining == demanded, "all original demand must be conserved");
    }
    void OneSheet(const Problem& problem, const JobResult& result)
    {
        Valid(problem, result);
        Require(result.Checked.Complete && result.Output.State == Status::Complete, "expected complete skyline result");
        Require(result.Output.Layouts.size() == 1, "expected one physical sheet");
    }
    Problem C7P2()
    {
        // Public 2DPackLib C/HT c7-p2.ins2D, original dimensions and row order.
        // https://site.unibo.it/operations-research/en/research/2dpacklib
        // Input is reproduced as data, not a solution or an algorithm.
        static constexpr std::pair<int, int> sizes[] = {
        {15,75}, {12,80}, {27,6}, {3,13}, {10,3}, {9,21}, {8,11}, {6,13},
        {2,9}, {51,10}, {6,6}, {11,12}, {2,10}, {8,12}, {13,47}, {14,37},
        {3,11}, {3,6}, {1,4}, {1,6}, {27,5}, {14,3}, {10,4}, {5,20},
        {3,11}, {14,5}, {5,2}, {7,2}, {12,9}, {2,1}, {12,8}, {5,28},
        {8,6}, {35,7}, {7,14}, {10,45}, {4,19}, {13,17}, {62,9}, {36,11},
        {38,18}, {4,2}, {9,5}, {8,3}, {30,20}, {6,7}, {36,10}, {27,7},
        {17,3}, {6,4}, {11,10}, {5,25}, {7,26}, {10,44}, {19,4}, {8,31},
        {33,6}, {11,73}, {8,27}, {6,2}, {2,9}, {25,9}, {9,39}, {9,17},
        {12,7}, {21,101}, {3,10}, {3,7}, {4,10}, {8,22}, {5,3}, {2,1},
        {1,2}, {22,59}, {1,2}, {1,1}, {2,1}, {11,56}, {10,39}, {5,5},
        {3,20}, {2,1}, {2,7}, {7,6}, {13,12}, {17,34}, {16,46}, {1,1},
        {6,13}, {3,12}, {6,10}, {9,15}, {6,24}, {1,1}, {5,7}, {4,1},
        {2,13}, {11,9}, {1,6}, {3,16}, {7,11}, {8,15}, {6,10}, {6,21},
        {3,9}, {1,4}, {10,7}, {3,3}, {5,15}, {2,4}, {33,8}, {16,5},
        {9,12}, {10,11}, {6,3}, {10,11}, {39,8}, {17,113}, {13,36}, {28,8},
        {17,7}, {42,9}, {22,57}, {2,1}, {15,9}, {30,8}, {26,7}, {16,32},
        {71,25}, {12,25}, {22,20}, {9,55}, {13,27}, {65,16}, {1,3}, {5,2},
        {16,57}, {2,1}, {3,3}, {3,2}, {25,11}, {11,8}, {6,4}, {10,9},
        {25,8}, {10,6}, {12,15}, {6,19}, {2,4}, {4,7}, {4,2}, {6,7},
        {13,3}, {29,5}, {2,2}, {8,15}, {5,28}, {8,42}, {4,27}, {3,2},
        {18,4}, {17,5}, {2,8}, {2,11}, {1,2}, {3,18}, {17,10}, {7,5},
        {3,10}, {5,31}, {19,9}, {7,8}, {10,3}, {11,37}, {1,4}, {6,32},
        {1,5}, {12,5}, {7,33}, {2,8}, {6,11}, {9,9}, {4,27}, {10,11},
        {38,9}, {5,7}, {6,2}, {3,6}, {6,2}, {32,5}, {12,4}, {16,3},
        {72,15}, {14,14}, {2,6}, {3,9}, {12,3}
        };
        static_assert(sizeof(sizes) / sizeof(sizes[0]) == 197);
        Problem problem; problem.Stocks = {{"C7-stock",160,240,1,1,"m"}};
        for (const auto& size : sizes)
            problem.Parts.push_back(Box(std::to_string(problem.Parts.size() + 1), size.first, size.second));
        return problem;
    }
    Problem BridgeCavity()
    {
        Problem problem;
        problem.Parts = {Box("left",2,4), Box("right",4,2), Box("bridge",6,2), Box("cavity",4,2)};
        problem.Stocks = {{"stock",6,6,1,1,"m"}};
        return problem;
    }
}

int main()
{
    try
    {
        const auto publicCase = C7P2();
        for (bool recovery : {false, true})
        {
            auto options = Budget(); options.EnableGapFill = recovery;
            const auto result = Solve(publicCase, options);
            OneSheet(publicCase, result);
            Require(result.Checked.PlacedCount == 197, "public C7-p2 must include all 197 original parts");
            Require(std::abs(result.Checked.MaterialArea - 160 * 240) < 1e-8, "public exact tiling material area");
            const auto& placements = result.Output.Layouts.front().Placements;
            for (std::size_t i = 0; i < placements.size(); ++i)
                Require(placements[i].PartID == publicCase.Parts[i].ID, "constructor must preserve original specification order");
        }

        // The full-width bridge hides a 4x2 empty rectangle below its skyline.
        const auto cavity = BridgeCavity();
        const auto recovered = Solve(cavity);
        OneSheet(cavity, recovered);
        Require(recovered.Output.Stats.GapFilledParts == 1, "one under-skyline cavity must be recovered");
        Require(recovered.Output.Layouts.front().Placements.back().PartID == "cavity" &&
                recovered.Output.Layouts.front().Placements.back().GapFiller, "recovered original instance keeps provenance");
        auto noRecovery = Budget(); noRecovery.EnableGapFill = false;
        const auto blocked = Solve(cavity, noRecovery);
        Valid(cavity, blocked);
        Require(!blocked.Checked.Complete && blocked.Output.Remaining.size() == 1 &&
                blocked.Output.Remaining.front().PartID == "cavity", "plain skyline leaves its hidden empty cavity explicit");

        // Several fills split and merge cavity space without raising the profile.
        auto split = cavity;
        split.Parts.back() = Box("small-cavity", 1, 1, 8);
        OneSheet(split, Solve(split));

        for (double gap : {0., .2})
        {
            Problem repeated; repeated.PartGap = gap; repeated.EdgeMargin = .7;
            repeated.Parts = {Box("repeat",2,3,20)};
            repeated.Stocks = {{"stock",10 + 4*gap + 1.4,12 + 3*gap + 1.4,1,1,"m"}};
            OneSheet(repeated, Solve(repeated));
            repeated.Parts.front().Quantity = 21;
            const auto limited = Solve(repeated);
            Valid(repeated, limited);
            Require(!limited.Checked.Complete && limited.Output.Layouts.size() == 1 &&
                    limited.Output.Remaining.front().Quantity == 1, "cannot invent a second finite sheet");
        }

        Problem materials;
        materials.Parts = {Box("A",3,2,3), Box("B",2,2,2)};
        materials.Parts[0].Material = "A"; materials.Parts[1].Material = "B";
        materials.Stocks = {{"none",100,100,0,1,"A"}, {"small",1,1,1,1,"A"},
                            {"A-first",6,2,1,1,"A"}, {"A-second",3,2,1,1,"A"}, {"B",4,2,1,1,"B"}};
        const auto mixed = Solve(materials); Valid(materials, mixed);
        Require(mixed.Checked.Complete && mixed.Output.Layouts.size() == 3, "material-specific finite stocks must remain separate");
        for (const auto& layout : mixed.Output.Layouts)
            Require(layout.StockID != "none" && layout.StockID != "small", "unusable stock is not reported as consumed");

        Problem moved; moved.Parts = {Box("offset",3,2)};
        for (auto& point : moved.Parts.front().Geometry.Outer) { point.X -= 11; point.Y += 4; }
        moved.Parts.front().AllowedAnglesDegrees = {90};
        moved.Stocks = {{"stock",2,3,1,1,"m"}};
        OneSheet(moved, Solve(moved));
        moved.Parts.front().AllowedAnglesDegrees = {0};
        const auto illegalTurn = Solve(moved); Valid(moved, illegalTurn);
        Require(!illegalTurn.Checked.Complete && illegalTurn.Output.Layouts.empty(), "forbidden quarter turn cannot be invented");

        Problem oblique; oblique.Parts = {Box("tilted-source",3,2)};
        for (auto& point : oblique.Parts.front().Geometry.Outer)
            point = Detail::Transform(point, {7,-2,30});
        oblique.Parts.front().AllowedAnglesDegrees = {-30};
        oblique.Stocks = {{"stock",3,2,1,1,"m"}};
        OneSheet(oblique, Solve(oblique));

        Problem unsupported; unsupported.Parts = {Box("hole",4,4), Box("concave",3,3), Box("circle",1,1), Box("plain",2,2)};
        unsupported.Parts[0].Geometry.Holes = {{{1,1},{2,1},{2,2},{1,2}}};
        unsupported.Parts[1].Geometry.Outer = {{0,0},{3,0},{3,1},{1,1},{1,3},{0,3}};
        unsupported.Parts[2].Geometry = {}; unsupported.Parts[2].Geometry.Radius = 1;
        unsupported.Stocks = {{"stock",20,20,1,1,"m"}};
        const auto partial = Solve(unsupported); Valid(unsupported, partial);
        Require(!partial.Checked.Complete && partial.Output.Remaining.size() == 3 &&
                partial.Checked.PlacedCount == 1 && partial.Output.Layouts[0].Placements[0].PartID == "plain",
                "holes and nonrectangles must not be silently replaced by filled boxes");

        for (std::size_t cap : {1u,2u,4u,8u,16u,32u,64u})
        {
            auto options = Budget(); options.MaxPlacementAttempts = cap;
            const auto limited = Solve(publicCase, options); Valid(publicCase, limited);
            Require(limited.Output.Stats.PlacementAttempts <= cap, "skyline placement attempt cap");
        }
        for (std::size_t pulse : {1u,2u,8u,32u,128u,1024u})
        {
            std::size_t calls = 0; bool seen = false; auto options = Budget();
            options.Cancel = [&] { if (++calls == pulse) { seen = true; return true; } return false; };
            const auto cancelled = Solve(publicCase, options); Valid(publicCase, cancelled);
            Require(!seen || (calls == pulse && cancelled.Output.State == Status::Cancelled), "single-pulse cancellation is latched");
        }
        auto expired = Budget(); expired.TimeLimitSeconds = 1e-9;
        const auto timed = Solve(publicCase, expired); Valid(publicCase, timed);
        Require(timed.Output.State == Status::Partial && timed.Output.Layouts.empty(), "expired construction retains whole unmet demand");

        // Cancellation remains responsive during a large subdivided source ring.
        auto dense = moved; dense.Parts[0].AllowedAnglesDegrees = {90};
        const auto corners = dense.Parts[0].Geometry.Outer;
        dense.Parts[0].Geometry.Outer.clear();
        for (std::size_t edge = 0; edge < corners.size(); ++edge)
            for (int i = 0; i < 5000; ++i)
            {
                const auto a = corners[edge], b = corners[(edge + 1) % corners.size()]; const double t = i / 5000.;
                dense.Parts[0].Geometry.Outer.push_back({a.X + (b.X-a.X)*t, a.Y + (b.Y-a.Y)*t});
            }
        std::size_t calls = 0; auto cancellation = Budget(); cancellation.Cancel = [&] { return ++calls == 8; };
        const auto cancelled = Detail::RunRectangleSkyline(dense, cancellation);
        Require(calls == 8 && cancelled.State == Status::Cancelled && cancelled.Layouts.empty() &&
                cancelled.Remaining.size() == 1 && cancelled.Remaining[0].Quantity == 1,
                "dense direction preprocessing cancellation retains source demand");

        std::cout << "RectangleSkylineTests passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

