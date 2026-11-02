#include "NestingInternal.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    Shape Box(double width, double height, double x = 0, double y = 0)
    { Shape shape; shape.Outer = {{x,y},{x+width,y},{x+width,y+height},{x,y+height}}; return shape; }
    Part Item(std::string id, Shape geometry, std::size_t quantity = 1,
        std::vector<double> angles = {0}, std::string material = "steel")
    { return {std::move(id), std::move(geometry), quantity, std::move(material), false, std::move(angles)}; }
    Stock Sheet(double width, double height, std::size_t quantity = 1,
        std::string id = "sheet", std::string material = "steel")
    { return {std::move(id), width, height, quantity, 1, std::move(material)}; }
    Options Budget() { Options options; options.TimeLimitSeconds = 10; options.MaxPlacementAttempts = 100000; return options; }
    Result Solve(const Problem& p, const Options& options = Budget(), Detail::RectScore score = Detail::RectScore::BottomLeft)
    {
        auto result = Detail::RunRectangleOrdered(p, options, score);
        const auto checked = Validate(p, result);
        if (!checked.Valid) for (const auto& error : checked.Errors) std::cerr << error << '\n';
        Require(checked.Valid, "ordered constructor returned invalid original geometry or inventory");
        return result;
    }
    void Complete(const Problem& p, const Result& result, std::size_t sheets)
    {
        if (result.State != Status::Complete || !Validate(p,result).Complete)
        {
            std::cerr << "Incomplete case: " << (p.Parts.empty() ? "empty" : p.Parts.front().ID)
                << ", attempts=" << result.Stats.PlacementAttempts << '\n';
            for (const auto& item : result.Remaining) std::cerr << item.PartID << " remaining=" << item.Quantity << '\n';
            for (const auto& note : result.Diagnostics) std::cerr << note << '\n';
            throw std::runtime_error("expected complete demand");
        }
        Require(result.Layouts.size() == sheets, "unexpected sheet count");
    }
    std::size_t Remaining(const Result& result, const std::string& id)
    {
        for (const auto& item : result.Remaining) if (item.PartID == id) return item.Quantity;
        return 0;
    }
}

int RunTests()
{
    // This input-order witness fills a 6 x 6 square. Choosing the 4 x 4 piece
    // before the two narrow pieces changes the BL construction.
    Problem ordered;
    ordered.Stocks = {Sheet(6,6)};
    ordered.Parts = {Item("first",Box(2,6)), Item("second",Box(4,2)), Item("third",Box(4,4))};
    const auto original = Solve(ordered);
    Complete(ordered,original,1);
    const auto& placements = original.Layouts[0].Placements;
    Require(placements.size() == 3 && placements[0].PartID == "first" && placements[1].PartID == "second" && placements[2].PartID == "third", "received part order was changed");
    Require(placements[0].Transform.X == 0 && placements[0].Transform.Y == 0 &&
        placements[1].Transform.X == 2 && placements[1].Transform.Y == 0 &&
        placements[2].Transform.X == 2 && placements[2].Transform.Y == 2, "unexpected fixed-order BL witness");

    // The same item can turn. Bottom-left compares its TOP edge, so 20 x 10
    // must precede 10 x 20 even though the latter is the first allowed direction.
    Problem turn;
    turn.Stocks = {Sheet(25,25)};
    turn.Parts = {Item("turn",Box(10,20,700,-900),1,{0,90})};
    const auto turned = Solve(turn);
    Complete(turn,turned,1);
    Require(turned.Layouts[0].Placements[0].Transform.AngleDegrees == 90, "BL compared bottom rather than top across orientations");

    // Physical 90-degree placement has an offset source origin, a 3 mm gap,
    // and exact upper/right margin contact. Quantities are original instances.
    Problem boundary;
    boundary.PartGap = 3; boundary.EdgeMargin = 10;
    boundary.Stocks = {Sheet(49,81)};
    boundary.Parts = {Item("offset",Box(13,29,1250,-2100),4,{90})};
    const auto tight = Solve(boundary);
    Complete(boundary,tight,1);
    Require(tight.Layouts[0].Placements.size() == 4, "quantity lost at exact sheet boundary");
    for (std::size_t i = 0; i < 4; ++i)
    {
        const auto& placed = tight.Layouts[0].Placements[i];
        const auto box = Detail::Bounds(boundary.Parts[0].Geometry,placed.Transform);
        Require(placed.Instance == i && placed.Transform.AngleDegrees == 90, "original instance or legal orientation changed");
        Require(std::abs(box.X-10) < 1e-9 && std::abs(box.Y-(10+16*i)) < 1e-9, "gap or offset placement incorrect");
    }
    boundary.Stocks[0].Height -= .01;
    const auto tooShort = Solve(boundary);
    Require(tooShort.State == Status::Partial && Remaining(tooShort,"offset") == 1, "stock boundary silently relaxed");

    // Source geometry can already be rotated. Extracting its legal axial
    // direction must retain the actual source geometry and explicit angle rule.
    Problem slanted;
    auto geometry = Box(17,31,240,-330);
    for (auto& point : geometry.Outer) point = Detail::Transform(point,{0,0,37});
    slanted.Parts = {Item("slanted",geometry,3,{-37})};
    slanted.Stocks = {Sheet(51,31)};
    Complete(slanted,Solve(slanted),1);

    // The initial oversized and unsupported parts stay in Remaining. Other
    // materials and later smaller shapes still use only compatible stock.
    Problem mixed;
    auto triangle = Shape{}; triangle.Outer = {{0,0},{3,0},{0,3}};
    mixed.Parts = {Item("oversize",Box(100,100),2), Item("triangle",triangle,3),
        Item("steel",Box(10,10),5), Item("alu",Box(10,10),2,{0},"aluminium"), Item("zero",Box(2,2),0)};
    mixed.Stocks = {Sheet(10,10,0,"none"), Sheet(20,10,2,"steel-stock"), Sheet(10,10,1,"alu-stock","aluminium")};
    const auto partial = Solve(mixed);
    Require(partial.State == Status::Partial && partial.Layouts.size() == 3, "mixed finite stock result");
    Require(Remaining(partial,"oversize") == 2 && Remaining(partial,"triangle") == 3 &&
        Remaining(partial,"steel") == 1 && Remaining(partial,"alu") == 1 && Remaining(partial,"zero") == 0, "remaining inventory was dropped");
    Require(partial.Layouts[0].StockInstance == 0 && partial.Layouts[1].StockInstance == 1, "stock instances were reused");

    // Quantity > 20 is intentional: this constructor has no small-puzzle cap.
    Problem many;
    many.Parts = {Item("many",Box(7,11),101)};
    many.Stocks = {Sheet(70,110,2)};
    for (auto score : {Detail::RectScore::BottomLeft,Detail::RectScore::ShortSide,Detail::RectScore::Area,Detail::RectScore::LongSide})
        Complete(many,Solve(many,Budget(),score),2);
    for (std::size_t limit : {1u,2u,3u,10u,101u})
    {
        auto budget = Budget(); budget.MaxPlacementAttempts = limit;
        const auto limited = Solve(many,budget);
        Require(limited.Stats.PlacementAttempts <= limit, "attempt budget exceeded");
        Require(Validate(many,limited).PlacedCount + Remaining(limited,"many") == 101, "budget stop lost demand");
    }
    for (std::size_t pulse : {1u,4u,16u,64u,128u})
    {
        auto budget = Budget(); std::size_t calls = 0;
        budget.Cancel = [&] { return ++calls == pulse; };
        const auto cancelled = Solve(many,budget);
        Require(calls == pulse && cancelled.State == Status::Cancelled, "single-pulse cancellation not latched");
    }
    Problem onePerSheet;
    onePerSheet.Parts = {Item("one-per-sheet",Box(100,100),200)};
    onePerSheet.Stocks = {Sheet(100,100,200)};
    auto linearBudget = Budget(); linearBudget.MaxPlacementAttempts = 450;
    Complete(onePerSheet,Solve(onePerSheet,linearBudget),200);
    Problem empty; empty.Stocks = {Sheet(10,10)};
    Complete(empty,Solve(empty),0);
    std::cout << "RectangleOrderedTests passed\n";
    return 0;
}

int main()
{
    try { return RunTests(); }
    catch (const std::exception& error)
    { std::cerr << "RectangleOrderedTests failed: " << error.what() << '\n'; return 1; }
}
