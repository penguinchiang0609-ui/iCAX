#include "SheetNesting.h"
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool condition, const char* message)
    { if (!condition) throw std::runtime_error(message); }

    Part Notched(const std::string& id, std::size_t quantity = 2, double x = 0, double y = 0, bool reverse = false)
    {
        Part p; p.ID = id; p.Quantity = quantity; p.FreeRotation = false; p.AllowedAnglesDegrees = {0, 180};
        p.Geometry.Outer = {{605,920},{605,0},{0,0},{0,250},{315,250},{315,580},{100,580},{100,920}};
        for (auto& point : p.Geometry.Outer) { point.X += x; point.Y += y; }
        if (reverse) std::reverse(p.Geometry.Outer.begin(), p.Geometry.Outer.end());
        return p;
    }

    Problem TightPair(double gap = 3, double x = 0, double y = 0, bool reverse = false)
    {
        Problem p; p.PartGap = gap; p.EdgeMargin = 10;
        p.Parts.push_back(Notched("notched", 2, x, y, reverse));
        // A legal 180-degree interlock has width 1110+gap and height 920.
        // Same-direction copies cannot both fit in this single stock.
        p.Stocks = {{"tight", 1130 + gap, 940, 1, 1, ""}};
        return p;
    }

    Options Budget()
    {
        Options o; o.TimeLimitSeconds = 2; o.MaxPlacementAttempts = 40000;
        o.MaxPatterns = 128; o.MaxPatternMembers = 2;
        o.EnableGapFill = false; o.EnableCircleLattice = false;
        return o;
    }

    JobResult Solve(const Problem& p, const char* strategy, const Options& o = Budget())
    { return RunJob({"interlock-regression", strategy, p, o}, DefaultStrategies()); }

    void CheckComplete(const Problem& p, const JobResult& job)
    {
        const auto checked = Validate(p, job.Output);
        if (!checked.Valid || !checked.Complete || !job.Checked.Complete)
        {
            std::cerr << job.StrategyID << ": gap=" << p.PartGap << ", firstQuantity=" << p.Parts.front().Quantity
                << ", origin=" << p.Parts.front().Geometry.Outer.front().X << ',' << p.Parts.front().Geometry.Outer.front().Y
                << ", placed=" << checked.PlacedCount << ", layouts=" << job.Output.Layouts.size() << '\n';
            for (const auto& error : checked.Errors) std::cerr << error << '\n';
            for (const auto& layout : job.Output.Layouts) for (const auto& a : layout.Placements)
                std::cerr << a.PartID << ':' << a.Instance << " group=" << a.Group << " pose=" << a.Transform.X << ','
                    << a.Transform.Y << ',' << a.Transform.AngleDegrees << '\n';
        }
        Require(checked.Valid && checked.Complete && job.Checked.Complete, "legal concave interlock must preserve and place the full demand");
        std::set<std::pair<std::string, std::size_t>> identities;
        for (const auto& layout : job.Output.Layouts)
            for (const auto& pose : layout.Placements)
                Require(identities.emplace(pose.PartID, pose.Instance).second, "interlock duplicated a member instance");
        std::size_t requested = 0;
        for (const auto& part : p.Parts) requested += part.Quantity;
        Require(identities.size() == requested, "interlock lost an original member");
    }
}

int main()
{
    try
    {
        std::size_t groups = 0;
        for (const auto* strategy : {"rules.composite-fit", "rules.batch-on-stall"})
        {
            for (double gap : {0., 1., 3., 7.})
            {
                const auto p = TightPair(gap); const auto job = Solve(p, strategy);
                CheckComplete(p, job); Require(job.Output.Layouts.size() == 1, "tight interlock must use its single finite stock");
                std::set<int> angles;
                for (const auto& pose : job.Output.Layouts.front().Placements) angles.insert(static_cast<int>(pose.Transform.AngleDegrees));
                Require(angles == std::set<int>{0,180}, "tight stock requires opposite allowed orientations");
                ++groups;
            }
            for (const bool reverse : {false, true})
            {
                const auto p = TightPair(3, 2684, 3371, reverse);
                CheckComplete(p, Solve(p, strategy)); ++groups;
            }
            for (const bool swapStockAxes : {false, true})
            {
                auto p = TightPair(); p.Parts.front().AllowedAnglesDegrees = {0, 90, 180, 270};
                if (swapStockAxes) std::swap(p.Stocks.front().Width, p.Stocks.front().Height);
                CheckComplete(p, Solve(p, strategy)); ++groups;
            }
            {
                auto p = TightPair(); p.Parts.front().Quantity = 1;
                auto other = p.Parts.front(); other.ID = "other-specification"; p.Parts.push_back(other);
                CheckComplete(p, Solve(p, strategy)); ++groups;
            }
            {
                auto p = TightPair(); p.Parts.front().AllowedAnglesDegrees = {0};
                const auto job = Solve(p, strategy);
                Require(job.Checked.Valid && !job.Checked.Complete, "fixed orientation must not acquire a forbidden half turn");
                for (const auto& layout : job.Output.Layouts) for (const auto& pose : layout.Placements)
                    Require(pose.Transform.AngleDegrees == 0, "fixed orientation was changed");
                ++groups;
            }
            {
                // Three finite tight stocks accommodate two pairs and the odd singleton.
                auto p = TightPair(); p.Parts.front().Quantity = 5; p.Stocks.front().Quantity = 3;
                CheckComplete(p, Solve(p, strategy)); ++groups;
            }
            {
                auto p = TightPair(); p.Parts.front().Quantity = 1; p.Parts.front().Material = "material-A";
                auto other = p.Parts.front(); other.ID = "material-B-part"; other.Material = "material-B"; p.Parts.push_back(other);
                p.Stocks.front().Material = "material-A";
                auto secondStock = p.Stocks.front(); secondStock.ID = "material-B-stock"; secondStock.Material = "material-B"; p.Stocks.push_back(secondStock);
                const auto job = Solve(p, strategy); CheckComplete(p, job);
                Require(job.Output.Layouts.size() == 2, "interlocking candidates must not merge different materials"); ++groups;
            }
            {
                const auto p = TightPair(); auto o = Budget(); o.EnableCombinations = false;
                const auto job = Solve(p, strategy, o);
                Require(job.Checked.Valid && !job.Checked.Complete, "disabled combinations must remain disabled"); ++groups;
            }
            {
                const auto p = TightPair(); auto o = Budget(); o.MaxPlacementAttempts = 1;
                const auto limited = Solve(p, strategy, o);
                Require(limited.Checked.Valid && limited.Output.Stats.PlacementAttempts <= 1, "interlock exceeded the attempt budget");
                o = Budget(); o.Cancel = [] { return true; };
                const auto cancelled = Solve(p, strategy, o);
                Require(cancelled.Checked.Valid && cancelled.Output.State == Status::Cancelled && cancelled.Checked.PlacedCount == 0,
                    "interlock preparation ignored cancellation");
                ++groups;
            }
        }
        std::cout << "Concave interlock regression groups passed: " << groups << '\n';
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
