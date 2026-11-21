#include "SheetNestingFeatureConstruction.h"
#include "SheetNestingNfp.h"
#include "NestingShapeFeatures.h"
#include "NestingAxisSafety.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <cmath>
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

    bool Near(double actual, double expected, double tolerance = 1e-6)
    { return std::abs(actual - expected) <= tolerance; }

    std::vector<Point> ConvexHull(std::vector<Point> points)
    {
        std::sort(points.begin(), points.end(), [](Point a, Point b)
            { return std::tie(a.X, a.Y) < std::tie(b.X, b.Y); });
        points.erase(std::unique(points.begin(), points.end(), [](Point a, Point b)
            { return a.X == b.X && a.Y == b.Y; }), points.end());
        const auto cross = [](Point a, Point b, Point c)
            { return (static_cast<long double>(b.X) - a.X) * (c.Y - a.Y) -
                (static_cast<long double>(b.Y) - a.Y) * (c.X - a.X); };
        std::vector<Point> hull;
        for (const auto point : points)
        {
            while (hull.size() > 1 && cross(hull[hull.size()-2], hull.back(), point) <= 0) hull.pop_back();
            hull.push_back(point);
        }
        const auto lower = hull.size();
        for (auto point = points.rbegin() + 1; point != points.rend(); ++point)
        {
            while (hull.size() > lower && cross(hull[hull.size()-2], hull.back(), *point) <= 0) hull.pop_back();
            hull.push_back(*point);
        }
        if (!hull.empty()) hull.pop_back();
        return hull;
    }

    double MinimumObbArea(const std::vector<Point>& points)
    {
        const auto hull = ConvexHull(points);
        Require(hull.size() >= 3, "independent OBB reference received degenerate geometry");
        long double best = std::numeric_limits<long double>::infinity();
        for (std::size_t i = 0; i < hull.size(); ++i)
        {
            const auto a = hull[i], b = hull[(i + 1) % hull.size()];
            const long double dx = static_cast<long double>(b.X) - a.X;
            const long double dy = static_cast<long double>(b.Y) - a.Y;
            const auto length = std::hypot(dx, dy), ux = dx / length, uy = dy / length;
            const auto infinity = std::numeric_limits<long double>::infinity();
            long double minX = infinity, minY = infinity, maxX = -infinity, maxY = -infinity;
            for (const auto point : hull)
            {
                const auto x = (static_cast<long double>(point.X) - a.X) * ux +
                    (static_cast<long double>(point.Y) - a.Y) * uy;
                const auto y = -(static_cast<long double>(point.X) - a.X) * uy +
                    (static_cast<long double>(point.Y) - a.Y) * ux;
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
            best = std::min(best, (maxX - minX) * (maxY - minY));
        }
        return static_cast<double>(best);
    }

    Part Polygon(std::string id, std::vector<Point> points, std::size_t quantity = 1)
    {
        Part part;
        part.ID = std::move(id); part.Geometry.Outer = std::move(points);
        part.Quantity = quantity; part.Material = "original-material";
        part.FreeRotation = false; part.AllowedAnglesDegrees = {0};
        return part;
    }

    Part Rectangle(std::string id, double width, double height, std::size_t quantity = 1)
    { return Polygon(std::move(id), {{0,0},{width,0},{width,height},{0,height}}, quantity); }

    Part Channel(std::string id = "channel", std::size_t quantity = 1)
    {
        return Polygon(std::move(id), {{0,5},{14,5},{14,0},{12,0},
            {12,3},{2,3},{2,0},{0,0}}, quantity);
    }

    Part Cross(std::string id = "cross", std::size_t quantity = 1)
    {
        return Polygon(std::move(id), {{0,2},{2,2},{2,4},{4,4},{4,2},{6,2},
            {6,0},{4,0},{4,-2},{2,-2},{2,0},{0,0}}, quantity);
    }

    Part Diamond(std::string id = "diamond", std::size_t quantity = 1)
    { return Polygon(std::move(id), {{6,-6},{0,0},{6,6},{12,0}}, quantity); }

    Problem Sheet(std::vector<Part> parts, double width = 40, double height = 30)
    {
        Problem problem;
        problem.Tolerance = 1e-7;
        problem.Parts = std::move(parts);
        problem.Stocks = {{"original-sheet", width, height, 1, 1, "original-material"}};
        return problem;
    }

    Options Budget()
    {
        Options options;
        options.TimeLimitSeconds = 4; options.MaxPlacementAttempts = 200000;
        options.EnableBasicCompaction = false;
        return options;
    }

    double DiagnosticValue(const Result& result, const std::string& key)
    {
        for (const auto& note : result.Diagnostics)
            if (const auto position = note.find(key + '='); position != std::string::npos)
                return std::stod(note.substr(position + key.size() + 1));
        throw std::runtime_error("missing feature-construction diagnostic " + key);
    }

    std::string DescribeMatches(const Detail::ShapeFeatureMatches& matches)
    {
        std::ostringstream text; text << std::setprecision(17);
        text << " candidates=" << matches.Matches.size() << " truncated=" << matches.Truncated;
        for (const auto& match : matches.Matches)
        {
            text << " [width=" << match.Value.Width << " height=" << match.Value.Height
                << " saving=" << match.FootprintSaving << " fill=" << match.CarrierFill;
            for (const auto& member : match.Value.Members)
                text << " member=" << member.PartIndex << ',' << member.Transform.X << ','
                    << member.Transform.Y << ',' << member.Transform.AngleDegrees;
            text << ']';
        }
        return text.str();
    }

    std::string DescribeJob(const JobResult& job)
    {
        std::ostringstream text; text << std::setprecision(17);
        text << " state=" << static_cast<int>(job.Output.State) << " valid=" << job.Checked.Valid
            << " complete=" << job.Checked.Complete << " placed=" << job.Checked.PlacedCount
            << " attempts=" << job.Output.Stats.PlacementAttempts;
        for (const auto& item : job.Output.Remaining)
            text << " remaining=" << item.PartID << ',' << item.Quantity;
        for (const auto& layout : job.Output.Layouts)
        {
            text << " [sheet=" << layout.StockID << ',' << layout.StockInstance << ','
                << layout.Width << ',' << layout.Height;
            for (const auto& pose : layout.Placements)
                text << " pose=" << pose.PartID << ',' << pose.Instance << ',' << pose.Transform.X << ','
                    << pose.Transform.Y << ',' << pose.Transform.AngleDegrees;
            text << ']';
        }
        for (const auto& note : job.Output.Diagnostics) text << " diagnostic=[" << note << ']';
        return text.str();
    }

    std::string Fingerprint(const Problem& problem)
    {
        std::ostringstream text; text << std::setprecision(17);
        text << problem.PartGap << ',' << problem.EdgeMargin << ',' << problem.Tolerance
            << ',' << problem.AllowCommonEdges;
        for (const auto& part : problem.Parts)
        {
            text << "part " << part.ID << ',' << part.Material << ',' << part.Quantity << ',' << part.FreeRotation;
            for (const auto angle : part.AllowedAnglesDegrees) text << ',' << angle;
            text << " outer";
            for (const auto point : part.Geometry.Outer) text << ',' << point.X << ',' << point.Y;
            for (const auto& hole : part.Geometry.Holes)
            {
                text << " hole";
                for (const auto point : hole) text << ',' << point.X << ',' << point.Y;
            }
            text << " circle " << part.Geometry.Radius << ',' << part.Geometry.Center.X << ',' << part.Geometry.Center.Y;
        }
        for (const auto& stock : problem.Stocks)
            text << " stock " << stock.ID << ',' << stock.Width << ',' << stock.Height << ','
                << stock.Quantity << ',' << stock.Material << ',' << stock.Cost;
        return text.str();
    }

    std::string PlacementFingerprint(const Result& result)
    {
        std::ostringstream text;
        text << std::setprecision(17) << static_cast<int>(result.State);
        for (const auto& board : result.Layouts)
        {
            text << ' ' << board.StockID << ',' << board.StockInstance << ',' << board.Width << ',' << board.Height;
            for (const auto& placement : board.Placements)
                text << ' ' << placement.PartID << ',' << placement.Instance << ',' << placement.Transform.X << ','
                    << placement.Transform.Y << ',' << placement.Transform.AngleDegrees << ',' << placement.Group;
        }
        for (const auto& item : result.Remaining)
            text << " remaining " << item.PartID << ',' << item.Quantity;
        return text.str();
    }

    Clipper2Lib::PathsD Paths(const Shape& shape)
    {
        Clipper2Lib::PathsD paths;
        const auto append = [&](const std::vector<Point>& ring)
        {
            auto& path = paths.emplace_back();
            for (const auto point : ring) path.emplace_back(point.X, point.Y);
        };
        append(shape.Outer);
        for (const auto& hole : shape.Holes) append(hole);
        return paths;
    }

    Clipper2Lib::PathsD Paths(const Detail::EncodedShapeFeature& feature)
    {
        Clipper2Lib::PathsD paths;
        for (const auto& ring : feature.Rings)
        {
            auto& path = paths.emplace_back();
            for (const auto point : ring) path.emplace_back(point.X, point.Y);
        }
        return paths;
    }

    double ReconstructionAllowance(const Shape& shape)
    {
        double perimeter = 0;
        const auto append = [&](const std::vector<Point>& ring)
        {
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const auto a = ring[i], b = ring[(i + 1) % ring.size()];
                perimeter += std::hypot(a.X - b.X, a.Y - b.Y);
            }
        };
        append(shape.Outer);
        for (const auto& hole : shape.Holes) append(hole);
        return std::max(1e-10, perimeter * 1e-7 * 4);
    }

    void VerifyReconstruction(const Shape& original, const Detail::ShapeFeatureModel& model)
    {
        using namespace Clipper2Lib;
        const auto source = Union(Paths(original), FillRule::EvenOdd, 8);
        for (const auto& carrier : model.Carriers)
        {
            Require(carrier.ReconstructionVerified, "retained ordinary carrier lacks reconstruction verification");
            auto rebuilt = Union(Paths(carrier.Geometry), FillRule::EvenOdd, 8);
            PathsD recesses, protrusions;
            for (const auto& feature : carrier.Features)
            {
                auto regions = Paths(feature);
                Require(Near(std::abs(Area(regions)), feature.Area, ReconstructionAllowance(original)),
                    "feature region boundaries disagree with declared area");
                auto& destination = feature.Polarity == Detail::FeaturePolarity::Recess ? recesses : protrusions;
                for (auto& region : regions) destination.push_back(std::move(region));
            }
            if (!recesses.empty()) rebuilt = Difference(rebuilt, Union(recesses, FillRule::NonZero, 8), FillRule::NonZero, 8);
            if (!protrusions.empty())
            {
                for (auto& region : protrusions) rebuilt.push_back(std::move(region));
                rebuilt = Union(rebuilt, FillRule::NonZero, 8);
            }
            const double error = std::abs(Area(Xor(source, rebuilt, FillRule::NonZero, 8)));
            Require(error <= ReconstructionAllowance(original), "carrier residuals do not reconstruct original material");
            Require(Near(error, carrier.ReconstructionErrorArea, ReconstructionAllowance(original)),
                "declared reconstruction error differs from independent Clipper XOR");
        }
    }

    std::vector<Detail::ShapeFeatureModel> Models(const Problem& problem, const Detail::ShapeFeatureLimits& limits = {})
    {
        std::vector<Detail::ShapeFeatureModel> models;
        for (const auto& part : problem.Parts)
            models.push_back(Detail::EncodeShapeFeatures(part.Geometry, problem.Tolerance, limits));
        return models;
    }

    void VerifyMatches(const Problem& problem, const Detail::ShapeFeatureMatches& matches)
    {
        double previousSaving = std::numeric_limits<double>::infinity();
        for (const auto& match : matches.Matches)
        {
            Require(match.Value.Members.size() >= 2, "feature match omitted original members");
            std::vector<std::size_t> counts(problem.Parts.size());
            std::vector<AxisSafety::Geometry> geometry;
            double material = 0;
            double separateObb = 0;
            std::vector<Point> allPoints;
            std::string materialID;
            for (const auto& member : match.Value.Members)
            {
                Require(member.PartIndex < problem.Parts.size(), "feature match invented a source index");
                Require(member.PartIndex == match.Value.Members.front().PartIndex,
                    "feature preassembly combined different original specifications");
                const auto& part = problem.Parts[member.PartIndex];
                Require(++counts[member.PartIndex] <= part.Quantity, "feature match exceeded original demand");
                Require(Detail::AngleAllowed(part, member.Transform.AngleDegrees, problem.Tolerance),
                    "feature match widened an original angle domain");
                if (geometry.empty()) materialID = part.Material;
                Require(part.Material == materialID, "feature match combined incompatible materials");
                auto actual = AxisSafety::Make(part.Geometry, member.Transform);
                Require(actual.Valid, "feature match has invalid original geometry");
                for (const auto& other : geometry)
                    Require(AxisSafety::PairExact(actual, other, problem.PartGap, problem.AllowCommonEdges,
                        problem.Tolerance) == AxisSafety::Verdict::Safe, "feature match overlaps original material");
                geometry.push_back(std::move(actual));
                material += Detail::Area(part.Geometry);
                separateObb += MinimumObbArea(part.Geometry.Outer);
                for (const auto point : part.Geometry.Outer)
                    allPoints.push_back(Detail::Transform(point, member.Transform));
            }
            Require(Near(match.Value.MaterialArea, material), "feature match changed original material area");
            const auto combinedObb = MinimumObbArea(allPoints);
            const auto saving = separateObb - combinedObb;
            Require(Near(match.FootprintSaving, saving, 1e-5),
                "pair saving differs from solo minimum OBB sum minus combined minimum OBB");
            Require(Near(match.CarrierFill, material / combinedObb, 1e-6),
                "pair carrier fill differs from original material over combined minimum OBB");
            Require(match.FootprintSaving <= previousSaving + 1e-5,
                "pair candidates are not sorted by descending absolute OBB saving");
            previousSaving = match.FootprintSaving;
        }
    }

    void OriginalAudit(const Problem& problem, const Result& result)
    {
        const auto checked = Validate(problem, result);
        Require(checked.Valid, "feature construction failed original-input validation");
        std::map<std::string, const Part*> parts;
        std::map<std::string, std::size_t> placed, remaining;
        std::set<std::pair<std::string, std::size_t>> identities, sheets;
        for (const auto& part : problem.Parts) parts.emplace(part.ID, &part);
        for (const auto& item : result.Remaining)
        {
            Require(parts.count(item.PartID) && item.Quantity && !remaining.count(item.PartID),
                "remaining inventory has an unknown or duplicated original specification");
            remaining[item.PartID] = item.Quantity;
        }
        for (const auto& layout : result.Layouts)
        {
            Require(sheets.emplace(layout.StockID, layout.StockInstance).second, "duplicate physical sheet identity");
            std::map<std::size_t, std::string> groupParts;
            std::vector<AxisSafety::Geometry> geometries;
            for (const auto& placement : layout.Placements)
            {
                Require(parts.count(placement.PartID), "construction invented an original part identity");
                if (placement.Group)
                {
                    const auto [same, inserted] = groupParts.emplace(placement.Group, placement.PartID);
                    Require(inserted || same->second == placement.PartID,
                        "one feature-construction object contains different original specifications");
                }
                const auto& part = *parts.at(placement.PartID);
                Require(placement.Instance < part.Quantity && identities.emplace(part.ID, placement.Instance).second,
                    "construction duplicated or renumbered an original instance beyond demand");
                Require(Detail::AngleAllowed(part, placement.Transform.AngleDegrees, problem.Tolerance),
                    "construction widened the source angle domain");
                const auto actual = AxisSafety::Make(part.Geometry, placement.Transform);
                Require(AxisSafety::Inside(actual, layout.Width, layout.Height, problem.EdgeMargin),
                    "written original geometry crossed the sheet");
                for (const auto& other : geometries)
                    Require(AxisSafety::PairExact(actual, other, problem.PartGap, problem.AllowCommonEdges,
                        problem.Tolerance) == AxisSafety::Verdict::Safe, "written original parts overlap");
                geometries.push_back(actual); ++placed[part.ID];
            }
        }
        for (const auto& part : problem.Parts)
            Require(placed[part.ID] + remaining[part.ID] == part.Quantity,
                "placed plus remaining does not conserve original demand");
    }

    JobResult Solve(const Problem& problem, Options options = Budget(), FeatureConstructionSettings settings = {},
        std::shared_ptr<NfpCache> cache = {})
    {
        const auto before = Fingerprint(problem);
        StrategyRegistry registry;
        registry.Register("feature", MakeFeatureConstructionFactory(settings, std::move(cache)));
        auto result = RunJob({"feature-original-input", "feature", problem, options}, registry);
        Require(before == Fingerprint(problem), "feature construction changed the original input");
        Require(result.Output.Stats.PlacementAttempts <= options.MaxPlacementAttempts, "shared attempt cap exceeded");
        return result;
    }
}

int main()
{
    Test("U carrier reconstructs fourteen by five minus ten by three recess", []
    {
        const auto original = Channel();
        const auto model = Detail::EncodeShapeFeatures(original.Geometry, 1e-7);
        Require(model.Supported && !model.Truncated, "ordinary U encoding was unsupported or truncated");
        VerifyReconstruction(original.Geometry, model);
        bool found = false;
        for (const auto& carrier : model.Carriers)
        {
            if (!Near(Detail::Area(carrier.Geometry), 70)) continue;
            for (const auto& feature : carrier.Features)
                if (feature.Polarity == Detail::FeaturePolarity::Recess && Near(feature.Area, 30))
                    for (const auto& mouth : feature.Mouths)
                        found |= Near(mouth.Width, 10) && Near(mouth.Depth, 3);
        }
        Require(found && Near(Detail::Area(original.Geometry), 40), "U recess area or ten by three mouth missing");
    });

    Test("cross exposes outer corner recesses and core strip protrusions", []
    {
        const auto original = Cross();
        const auto model = Detail::EncodeShapeFeatures(original.Geometry, 1e-7);
        Require(model.Supported && !model.Truncated, "ordinary cross encoding was unsupported or truncated");
        VerifyReconstruction(original.Geometry, model);
        bool outer = false, core = false;
        for (const auto& carrier : model.Carriers)
        {
            std::size_t recesses = 0, protrusions = 0, mouths = 0;
            for (const auto& feature : carrier.Features)
            {
                if (feature.Polarity == Detail::FeaturePolarity::Recess && Near(feature.Area, 4)) ++recesses;
                if (feature.Polarity == Detail::FeaturePolarity::Protrusion && Near(feature.Area, 4))
                {
                    ++protrusions;
                    for (const auto& mouth : feature.Mouths)
                        mouths += Near(mouth.Width, 2) && Near(mouth.Depth, 2);
                }
            }
            outer |= Near(Detail::Area(carrier.Geometry), 36) && recesses == 4;
            core |= carrier.Kind == Detail::FeatureCarrierKind::CoreRectangle &&
                Near(Detail::Area(carrier.Geometry), 12) && protrusions == 2 && mouths >= 2;
        }
        Require(outer && core && Near(Detail::Area(original.Geometry), 20), "cross corner or core-tooth encoding missing");
    });

    Test("rotated diamond keeps its complete material carrier", []
    {
        const auto original = Diamond();
        const auto model = Detail::EncodeShapeFeatures(original.Geometry, 1e-7);
        Require(model.Supported && !model.Truncated, "ordinary diamond encoding failed");
        VerifyReconstruction(original.Geometry, model);
        Require(std::any_of(model.Carriers.begin(), model.Carriers.end(), [](const auto& carrier)
            { return Near(Detail::Area(carrier.Geometry), 72) && carrier.Features.empty(); }),
            "complete rotated carrier was lost because its axis-aligned box has empty corners");
    });

    Test("closed holes retain material differences without invented mouths", []
    {
        auto original = Rectangle("closed-hole", 14, 5);
        original.Geometry.Holes = {{{2,1},{12,1},{12,4},{2,4}}};
        const auto model = Detail::EncodeShapeFeatures(original.Geometry, 1e-7);
        Require(model.Supported, "polygonal hole encoding unsupported");
        VerifyReconstruction(original.Geometry, model);
        bool closed = false;
        for (const auto& carrier : model.Carriers) for (const auto& feature : carrier.Features)
            closed |= feature.Polarity == Detail::FeaturePolarity::Recess && Near(feature.Area, 30) && feature.Mouths.empty();
        Require(closed, "closed hole was discarded or given a fictitious carrier opening");
    });

    Test("different original specifications never enter feature preassembly", []
    {
        for (const std::size_t quantity : {std::size_t{1}, std::size_t{2}})
        {
            auto first = Channel("first-original", quantity);
            auto equivalent = first; equivalent.ID = "same-contour-other-original";
            const auto problem = Sheet({first, equivalent, Rectangle("filler", 10, 3, quantity)}, 40, 20);
            const auto before = Fingerprint(problem);
            Detail::ShapeFeatureLimits limits; std::size_t attempts = 0;
            const auto matches = Detail::BuildShapeFeatureMatches(problem, Models(problem), limits, attempts);
            VerifyMatches(problem, matches);
            Require(before == Fingerprint(problem), "matching changed original geometry or quantity");
            Require(attempts <= limits.MaxAttempts && matches.RetainedBytes <= limits.MaxBytes,
                "same-original feature matching exceeded a shared cap");
            if (quantity == 1)
                Require(matches.Matches.empty() && attempts == 0,
                    "quantity-one or same-contour different IDs entered a pair geometry trial");
        }
    });

    Test("cross-original narrow free-angle features are excluded before geometry trials", []
    {
        constexpr double angle = 61.213;
        auto fixed = Channel("synthetic-fixed-rotated-mouth");
        for (auto& point : fixed.Geometry.Outer) point = Detail::Transform(point, {0,0,angle});
        auto filler = Rectangle("synthetic-free-filler", 10 - 2e-5, 3 - 2e-5);
        filler.FreeRotation = true; filler.AllowedAnglesDegrees.clear();
        const auto problem = Sheet({fixed, filler}, 40, 40);
        const auto before = Fingerprint(problem);
        Detail::ShapeFeatureLimits limits; std::size_t attempts = 0;
        const auto matches = Detail::BuildShapeFeatureMatches(problem, Models(problem), limits, attempts);
        VerifyMatches(problem, matches);
        Require(matches.Matches.empty() && attempts == 0 && before == Fingerprint(problem),
            "a complementary different-original mouth spent pair work or changed the free-angle source");
    });

    Test("zero demand and incompatible materials cannot become matching inventory", []
    {
        auto problem = Sheet({Channel(), Rectangle("different-material", 10, 3), Rectangle("zero-demand", 10, 3, 0)});
        problem.Parts[1].Material = "other-material";
        Detail::ShapeFeatureLimits limits;
        std::size_t attempts = 0;
        const auto matches = Detail::BuildShapeFeatureMatches(problem, Models(problem), limits, attempts);
        VerifyMatches(problem, matches);
        Require(matches.Matches.empty(), "matching used incompatible material or nonexistent original instances");
    });

    Test("absolute OBB saving prioritizes thirty before fifteen between self pairs", []
    {
        auto large = Polygon("large-self-triangle", {{0,0},{6,0},{0,5}}, 2);
        auto small = Polygon("small-self-triangle", {{0,0},{5,0},{0,3}}, 2);
        large.AllowedAnglesDegrees = small.AllowedAnglesDegrees = {0,180};
        const auto problem = Sheet({large, small}, 30, 20);
        Detail::ShapeFeatureLimits limits; std::size_t attempts = 0;
        const auto matches = Detail::BuildShapeFeatureMatches(problem, Models(problem), limits, attempts);
        VerifyMatches(problem, matches);
        bool thirty = false, fifteen = false;
        for (const auto& match : matches.Matches)
        {
            thirty |= match.Value.Members.front().PartIndex == 0 && Near(match.FootprintSaving, 30);
            fifteen |= match.Value.Members.front().PartIndex == 1 && Near(match.FootprintSaving, 15);
        }
        Require(thirty && fifteen && Near(matches.Matches.front().FootprintSaving, 30),
            "absolute same-original OBB gains were lost or reversed");
    });

    Test("self matches require two original instances and triangles have positive OBB gain", []
    {
        auto triangle = Polygon("original-triangle", {{0,0},{4,0},{0,3}}, 0);
        triangle.AllowedAnglesDegrees = {0,180};
        Detail::ShapeFeatureLimits limits;
        for (const std::size_t quantity : {std::size_t{0}, std::size_t{1}, std::size_t{2}})
        {
            triangle.Quantity = quantity;
            const auto problem = Sheet({triangle}, 10, 10);
            std::size_t attempts = 0;
            const auto matches = Detail::BuildShapeFeatureMatches(problem, Models(problem), limits, attempts);
            VerifyMatches(problem, matches);
            if (quantity < 2)
                Require(matches.Matches.empty(), "self matching invented a second source instance");
            else
                Require(std::any_of(matches.Matches.begin(), matches.Matches.end(), [](const auto& match)
                    { return Near(match.FootprintSaving, 12) && Near(match.CarrierFill, 1); }),
                    "two complementary right triangles lost twelve units of OBB saving");
        }
    });

    Test("pair OBB gain is invariant under source-wide rotation and translation", []
    {
        auto original = Polygon("rotated-original-triangle", {{0,0},{4,0},{0,3}}, 2);
        original.AllowedAnglesDegrees = {0,180};
        double gains[2] = {};
        for (std::size_t version = 0; version < 2; ++version)
        {
            auto triangle = original;
            if (version)
                for (auto& point : triangle.Geometry.Outer)
                    point = Detail::Transform(point, {123.25,-47.125,23.789});
            const auto problem = Sheet({triangle}, 30, 30);
            Detail::ShapeFeatureLimits limits; std::size_t attempts = 0;
            const auto matches = Detail::BuildShapeFeatureMatches(problem, Models(problem), limits, attempts);
            VerifyMatches(problem, matches);
            Require(!matches.Matches.empty(), "rotating the original frame erased complementary triangle candidates");
            gains[version] = matches.Matches.front().FootprintSaving;
        }
        Require(Near(gains[0], 12) && Near(gains[1], gains[0], 1e-5),
            "OBB pair saving changed under a rigid source-frame transformation");
    });

    Test("ordinary different-original board evaluation retains saving and density", []
    {
        const auto problem = Sheet({Channel(), Rectangle("same-original-filler", 10, 3)}, 14, 6);
        Result compact, extended;
        compact.State = extended.State = Status::Complete;
        compact.Layouts = {{"original-sheet",0,14,6,
            {{"channel",0,{0,0,0},1,false},{"same-original-filler",0,{2,0,0},2,false}},70}};
        extended.Layouts = {{"original-sheet",0,14,6,
            {{"channel",0,{0,1,0},1,false},{"same-original-filler",0,{2,0,0},2,false}},70}};
        OriginalAudit(problem, compact); OriginalAudit(problem, extended);
        const auto combinedObb = [&](const Result& result)
        {
            std::vector<Point> points;
            for (std::size_t i = 0; i < problem.Parts.size(); ++i)
                for (const auto point : problem.Parts[i].Geometry.Outer)
                    points.push_back(Detail::Transform(point, result.Layouts[0].Placements[i].Transform));
            return MinimumObbArea(points);
        };
        const double solo = MinimumObbArea(problem.Parts[0].Geometry.Outer) +
            MinimumObbArea(problem.Parts[1].Geometry.Outer);
        const auto compactArea = combinedObb(compact), extendedArea = combinedObb(extended);
        Require(Near(compactArea, 70) && Near(extendedArea, 84) &&
            solo - compactArea > solo - extendedArea && 70 / compactArea > 70 / extendedArea,
            "same-pair minimum OBB saving and material density do not move together");
    });

    Test("bounded candidates retain self-pair saving and density tradeoffs", []
    {
        auto large = Channel("large-self-channel", 2);
        for (auto& point : large.Geometry.Outer) { point.X *= 2; point.Y *= 2; }
        large.AllowedAnglesDegrees = {0,180};
        auto compact = Polygon("compact-self-triangle", {{0,0},{4,0},{0,3}}, 2);
        compact.AllowedAnglesDegrees = {0,180};
        const auto problem = Sheet({large, compact}, 60, 40);
        Detail::ShapeFeatureLimits limits; limits.MaxProposals = 8;
        std::size_t attempts = 0;
        const auto matches = Detail::BuildShapeFeatureMatches(problem, Models(problem), limits, attempts);
        VerifyMatches(problem, matches);
        bool highSaving = false, highDensity = false;
        for (const auto& match : matches.Matches)
        {
            highSaving |= match.Value.Members.front().PartIndex == 0 &&
                match.FootprintSaving > 100 && match.CarrierFill < .8;
            highDensity |= match.Value.Members.front().PartIndex == 1 &&
                Near(match.FootprintSaving, 12) && Near(match.CarrierFill, 1);
        }
        Require(matches.Matches.size() <= limits.MaxProposals && highSaving && highDensity,
            "finite result cap erased a same-original saving/density tradeoff");
    });

    Test("same-original pair retains equal-OBB board directions", []
    {
        auto triangle = Polygon("directional-self-triangle", {{0,0},{4,0},{0,3}}, 2);
        triangle.AllowedAnglesDegrees = {0,90,180,270};
        const auto problem = Sheet({triangle}, 20, 20);
        Detail::ShapeFeatureLimits limits; limits.MaxProposals = 8;
        std::size_t attempts = 0;
        const auto matches = Detail::BuildShapeFeatureMatches(problem, Models(problem), limits, attempts);
        VerifyMatches(problem, matches);
        bool horizontal = false, vertical = false;
        for (const auto& match : matches.Matches)
        {
            if (!Near(match.FootprintSaving, 12) || !Near(match.CarrierFill, 1)) continue;
            horizontal |= Near(match.Value.Width, 4) && Near(match.Value.Height, 3);
            vertical |= Near(match.Value.Width, 3) && Near(match.Value.Height, 4);
        }
        Require(horizontal && vertical,
            "equal OBB saving erased a legal same-original pair orientation" + DescribeMatches(matches));
    });

    Test("encoding loss and retained memory caps are explicit", []
    {
        Detail::ShapeFeatureLimits limits;
        limits.MaxFeaturesPerCarrier = 0;
        const auto truncated = Detail::EncodeShapeFeatures(Cross().Geometry, 1e-7, limits);
        Require(truncated.Truncated, "omitted residual features were presented as complete encoding");
        for (const auto& carrier : truncated.Carriers)
            Require(carrier.Features.empty(), "feature count cap exceeded");
        limits = {}; limits.MaxBytes = 1;
        const auto tiny = Detail::EncodeShapeFeatures(Channel().Geometry, 1e-7, limits);
        Require(tiny.RetainedBytes <= 1 && tiny.Truncated, "encoder retained data beyond its byte cap");
    });

    Test("matcher cancellation and absolute cumulative attempts preserve input", []
    {
        const auto problem = Sheet({Channel(), Rectangle("filler", 10, 3)});
        const auto before = Fingerprint(problem); const auto models = Models(problem);
        Detail::ShapeFeatureLimits limits; limits.MaxAttempts = 3;
        std::size_t attempts = limits.MaxAttempts;
        const auto exhausted = Detail::BuildShapeFeatureMatches(problem, models, limits, attempts);
        Require(exhausted.Matches.empty() && attempts == limits.MaxAttempts,
            "matcher ignored the already exhausted cumulative counter");
        attempts = 0;
        const auto cancelled = Detail::BuildShapeFeatureMatches(problem, models, limits, attempts, [] { return true; });
        Require(cancelled.Matches.empty() && attempts <= limits.MaxAttempts,
            "cancelled matching retained unexamined candidates");
        Require(before == Fingerprint(problem), "cancelled matching changed the source");
    });

    Test("encoding cancellation makes omitted geometry analysis explicit", []
    {
        int polls = 0;
        const auto original = Cross();
        const auto model = Detail::EncodeShapeFeatures(original.Geometry, 1e-7, {},
            [&] { return ++polls > 8; });
        Require(polls == 9 && model.Truncated, "cancelled encoding continued or claimed complete feature coverage");
        VerifyReconstruction(original.Geometry, model);
    });

    Test("invalid source contour has explicit unsupported encoding", []
    {
        const auto bowtie = Polygon("invalid", {{0,0},{4,4},{0,4},{4,0}});
        const auto model = Detail::EncodeShapeFeatures(bowtie.Geometry, 1e-7);
        Require(!model.Supported && model.Carriers.empty() && !model.Diagnostic.empty(),
            "invalid material was encoded as a supported carrier");
    });

    Test("ordinary different-original placement completes without preassembly", []
    {
        const auto problem = Sheet({Channel(), Rectangle("filler", 10, 3)}, 30, 10);
        std::vector<ConstructionTraceEvent> events;
        FeatureConstructionSettings settings;
        settings.Trace = [&](const auto& event) { events.push_back(event); };
        const auto job = Solve(problem, Budget(), settings);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 2 && job.Output.Layouts.size() == 1 &&
            Near(job.Checked.MaterialArea, 70), "independently placeable U and filler remain incomplete");
        Require(DiagnosticValue(job.Output, "feature-proposals") == 0 &&
            DiagnosticValue(job.Output, "feature-match-groups-used") == 0 &&
            std::none_of(events.begin(), events.end(), [](const auto& event)
                { return event.Stage == ConstructionTraceStage::Match; }),
            "ordinary different-original placement used an excluded preassembly");
    });

    Test("constructor retains actual free-angle same-original feature groups", []
    {
        auto triangle = Polygon("free-original-self-triangle", {{0,0},{4,0},{0,3}}, 2);
        for (auto& point : triangle.Geometry.Outer) point = Detail::Transform(point, {7.25,-3.5,13.213});
        triangle.FreeRotation = true; triangle.AllowedAnglesDegrees.clear();
        const auto problem = Sheet({triangle}, 20, 20);
        const auto job = Solve(problem);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 2 &&
            DiagnosticValue(job.Output, "feature-proposals") > 0 &&
            DiagnosticValue(job.Output, "feature-match-groups-used") > 0,
            "free-angle same-original features were excluded from actual construction" + DescribeJob(job));
        Require(Near(job.Checked.MaterialArea, 12), "free-angle self group changed original net material");
    });

    Test("mixed quantities and original instance identities survive construction", []
    {
        const auto problem = Sheet({Channel("source-A", 3), Rectangle("source-B", 10, 3, 3)}, 42, 10);
        std::vector<ConstructionTraceEvent> events;
        FeatureConstructionSettings settings;
        settings.Trace = [&](const auto& event) { events.push_back(event); };
        const auto job = Solve(problem, Budget(), settings);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 6 && job.Output.Layouts.size() == 1 &&
            Near(job.Checked.MaterialArea, 210), "independent mixed original quantities remain incomplete");
        for (const auto& event : events) if (event.Stage == ConstructionTraceStage::Match)
            Require(!event.Members.empty() && std::all_of(event.Members.begin(), event.Members.end(),
                [&](const auto& member) { return member.PartIndex == event.Members.front().PartIndex; }),
                "mixed production quantities entered a cross-original feature proposal");
    });

    Test("reversed translated self-complementary originals retain source geometry", []
    {
        auto triangle = Polygon("translated-reversed-self-triangle", {{0,0},{4,0},{0,3}}, 2);
        triangle.AllowedAnglesDegrees = {0,180};
        std::reverse(triangle.Geometry.Outer.begin(), triangle.Geometry.Outer.end());
        for (auto& point : triangle.Geometry.Outer) { point.X += 123.125; point.Y -= 47.5; }
        const auto problem = Sheet({triangle}, 4, 3);
        const auto before = Fingerprint(problem);
        const auto models = Models(problem);
        Require(models[0].Supported && !models[0].Truncated, "translated reversed source encoding failed");
        VerifyReconstruction(triangle.Geometry, models[0]);
        Detail::ShapeFeatureLimits limits; std::size_t attempts = 0;
        const auto matches = Detail::BuildShapeFeatureMatches(problem, models, limits, attempts);
        VerifyMatches(problem, matches);
        Require(!matches.Matches.empty(), "source translation erased a legal same-original feature pair");
        const auto job = Solve(problem);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && before == Fingerprint(problem),
            "reversed translated originals changed or lost complete self-complementary placement");
    });

    Test("positive original gap and edge margin retain actual clearance", []
    {
        auto problem = Sheet({Channel(), Rectangle("gap-filler", 10, 3)}, 30, 12);
        problem.PartGap = .25; problem.EdgeMargin = .5;
        const auto job = Solve(problem);
        OriginalAudit(problem, job.Output);
        Require(job.Output.State != Status::Failed &&
            (job.Checked.Complete || !job.Output.Diagnostics.empty()),
            "nonzero-gap scope lacks a valid result or explicit diagnostic");
    });

    Test("multiple material and physical stock inventories stay separate", []
    {
        auto channel = Channel("red-U"); channel.Material = "red";
        auto filler = Rectangle("red-fill", 10, 3); filler.Material = "red";
        auto blue = Rectangle("blue-original", 5, 5, 2); blue.Material = "blue";
        auto problem = Sheet({channel, filler, blue});
        problem.Stocks = {{"red-stock",14,5,1,1,"red"},{"blue-stock",5,5,2,1,"blue"}};
        const auto job = Solve(problem);
        OriginalAudit(problem, job.Output);
        Require(job.Output.State != Status::Failed &&
            (job.Checked.Complete || !job.Output.Diagnostics.empty()),
            "multiple-stock scope lacks a valid result or explicit diagnostic");
    });

    Test("four fixed-angle diamonds retain real rotated-carrier material", []
    {
        const auto problem = Sheet({Diamond("source-diamond", 4)}, 24, 24);
        const auto job = Solve(problem);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 4, "four original diamonds remain incomplete");
        Require(Near(job.Checked.MaterialArea, 288), "rotated carrier was replaced by its enclosing rectangle");
    });

    Test("partial cancellation and small attempt budgets conserve every original", []
    {
        const auto problem = Sheet({Channel("large-demand", 20), Rectangle("filler-demand", 10, 3, 20)}, 42, 10);
        auto options = Budget(); options.MaxPlacementAttempts = 5;
        const auto limited = Solve(problem, options);
        OriginalAudit(problem, limited.Output);
        Require(!limited.Checked.Complete, "tiny work cap unexpectedly completed forty original pieces");
        options = Budget(); options.Cancel = [] { return true; };
        const auto cancelled = Solve(problem, options);
        OriginalAudit(problem, cancelled.Output);
        Require(cancelled.Output.State == Status::Cancelled && !cancelled.Checked.Complete,
            "cancelled construction did not preserve explicit cancelled state");
    });

    Test("zero original demand completes without invented sheet material", []
    {
        const auto problem = Sheet({Channel("empty-A", 0), Rectangle("empty-B", 10, 3, 0)});
        const auto job = Solve(problem);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 0 && job.Output.Layouts.empty(),
            "zero-demand construction invented placements or retained missing demand");
    });

    Test("normal job entry rejects invalid budget and original geometry", []
    {
        const auto problem = Sheet({Channel()});
        auto options = Budget(); options.TimeLimitSeconds = 0;
        auto job = Solve(problem, options);
        Require(job.Output.State == Status::InvalidInput && job.Output.Layouts.empty(), "zero-time job was accepted");
        options = Budget(); options.MaxPlacementAttempts = 0;
        job = Solve(problem, options);
        Require(job.Output.State == Status::InvalidInput && job.Output.Layouts.empty(), "zero-attempt job was accepted");
        options = Budget(); options.TimeLimitSeconds = std::numeric_limits<double>::quiet_NaN();
        job = Solve(problem, options);
        Require(job.Output.State == Status::InvalidInput, "nonfinite time was accepted");
        const auto invalid = Sheet({Polygon("invalid", {{0,0},{4,4},{0,4},{4,0}})});
        job = Solve(invalid);
        Require(job.Output.State == Status::InvalidInput && job.Output.Layouts.empty(), "invalid source material entered search");
    });

    Test("construction cap settings retain a valid bounded original result", []
    {
        const auto problem = Sheet({Channel("bounded-A", 8), Rectangle("bounded-B", 10, 3, 8)}, 42, 10);
        FeatureConstructionSettings settings;
        settings.MaxRetainedBytes = 4096; settings.MaxSearchStates = 1; settings.MaxArrayVariants = 1;
        auto options = Budget(); options.MaxPlacementAttempts = 100;
        const auto job = Solve(problem, options, settings);
        OriginalAudit(problem, job.Output);
        Require(job.Output.State != Status::Failed, "resource cap escaped as constructor failure");
        Require(DiagnosticValue(job.Output, "retained-high-water") <= settings.MaxRetainedBytes &&
            DiagnosticValue(job.Output, "expanded-states") <= settings.MaxSearchStates,
            "constructor diagnostics exceed the actual retained/state caps");
    });

    Test("one-state construction retains a completed singleton terminal", []
    {
        const auto problem = Sheet({Rectangle("only-original", 5, 3)}, 5, 3);
        FeatureConstructionSettings settings;
        settings.MaxSearchStates = 1; settings.MaxArrayVariants = 0;
        const auto job = Solve(problem, Budget(), settings);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete && job.Checked.PlacedCount == 1,
            "terminal completion was discarded at the exact search-state cap");
        Require(DiagnosticValue(job.Output, "expanded-states") <= 1,
            "singleton completion expanded beyond its state cap");
    });

    Test("large original quantities remain counted under small work and memory caps", []
    {
        const auto problem = Sheet({Channel("bulk-A", 5000), Rectangle("bulk-B", 10, 3, 5000)}, 14, 5);
        FeatureConstructionSettings settings;
        settings.MaxRetainedBytes = 1024 * 1024; settings.MaxSearchStates = 2; settings.MaxArrayVariants = 2;
        auto options = Budget(); options.TimeLimitSeconds = .1; options.MaxPlacementAttempts = 100;
        const auto job = Solve(problem, options, settings);
        OriginalAudit(problem, job.Output);
        Require(!job.Checked.Complete && job.Checked.PlacedCount <= 2 && job.Output.State != Status::Failed,
            "large quantities escaped sheet inventory, work limits or truthful partial status");
    });

    Test("concurrent independent feature jobs share only immutable geometry", []
    {
        auto triangle = Polygon("first-original", {{0,0},{4,0},{0,3}}, 2);
        triangle.AllowedAnglesDegrees = {0,180};
        const auto first = Sheet({triangle}, 4, 3);
        const auto second = Sheet({Diamond("second-original", 4)}, 24, 24);
        auto cache = std::make_shared<NfpCache>();
        JobResult firstJob, secondJob;
        std::exception_ptr firstError, secondError;
        std::thread a([&] { try { firstJob = Solve(first, Budget(), {}, cache); } catch (...) { firstError = std::current_exception(); } });
        std::thread b([&] { try { secondJob = Solve(second, Budget(), {}, cache); } catch (...) { secondError = std::current_exception(); } });
        a.join(); b.join();
        if (firstError) std::rethrow_exception(firstError);
        if (secondError) std::rethrow_exception(secondError);
        OriginalAudit(first, firstJob.Output); OriginalAudit(second, secondJob.Output);
        Require(firstJob.Checked.Complete && firstJob.Checked.PlacedCount == 2 &&
            secondJob.Checked.Complete && secondJob.Checked.PlacedCount == 4,
            "independent same-original job demand or angle state leaked");
    });

    Test("trace observes actual features proposals states and the selected original placements", []
    {
        auto triangle = Polygon("traced-self-triangle", {{0,0},{4,0},{0,3}}, 2);
        triangle.AllowedAnglesDegrees = {0,180};
        const auto problem = Sheet({triangle}, 4, 3);
        auto options = Budget();
        options.TimeLimitSeconds = 10;
        const auto baseline = Solve(problem, options);
        std::vector<ConstructionTraceEvent> events;
        FeatureConstructionSettings settings;
        settings.Trace = [&](const ConstructionTraceEvent& event) { events.push_back(event); };
        const auto observed = Solve(problem, options, settings);
        OriginalAudit(problem, observed.Output);
        Require(PlacementFingerprint(baseline.Output) == PlacementFingerprint(observed.Output),
            "trace observation changed the actual selected original placements");
        Require(!events.empty() && events.front().Stage == ConstructionTraceStage::Begin,
            "trace did not begin at the original empty state");
        bool analysis = false, match = false, selected = false;
        std::uint64_t previousStep = 0;
        std::map<std::uint64_t, std::vector<ConstructionTraceMember>> states;
        states.emplace(1, std::vector<ConstructionTraceMember>{});
        for (const auto& event : events)
        {
            Require(event.Step > previousStep, "trace steps are not strictly ordered");
            previousStep = event.Step;
            if (event.Stage == ConstructionTraceStage::Analysis)
            {
                Require(event.PartIndex < problem.Parts.size() && !event.Geometries.empty(),
                    "analysis trace has no original source geometry");
                analysis = true;
            }
            if (event.Stage == ConstructionTraceStage::Match)
            {
                Require(event.Members.size() >= 2 && event.Fill > 0 && event.FootprintArea > 0,
                    "match trace omitted the actual proposal or separate OBB measures");
                match = true;
            }
            if (event.Stage == ConstructionTraceStage::Candidate)
            {
                Require(states.count(event.ParentStateID),
                    "candidate references an unknown parent state");
                if (!event.Accepted)
                {
                    Require(event.StateID == 0 && !event.Reason.empty(),
                        "rejected candidate omitted its actual rejection reason");
                    continue;
                }
                Require(event.StateID != event.ParentStateID, "accepted candidate reused its parent state");
                auto members = states.at(event.ParentStateID);
                members.insert(members.end(), event.Members.begin(), event.Members.end());
                Require(states.emplace(event.StateID, std::move(members)).second,
                    "trace reused a real candidate state identity");
            }
            if (event.Stage == ConstructionTraceStage::Selected)
            {
                Require(event.Members.size() == observed.Checked.PlacedCount,
                    "selected trace does not contain every selected original instance");
                for (const auto& member : event.Members)
                {
                    Require(member.PartIndex < problem.Parts.size(), "selected trace invented a source part");
                    const auto& part = problem.Parts[member.PartIndex];
                    const auto& placements = observed.Output.Layouts.front().Placements;
                    const auto placement = std::find_if(placements.begin(), placements.end(), [&](const Placement& p)
                    { return p.PartID == part.ID && p.Instance == member.Instance; });
                    Require(placement != placements.end() && placement->Transform.X == member.Transform.X &&
                        placement->Transform.Y == member.Transform.Y &&
                        placement->Transform.AngleDegrees == member.Transform.AngleDegrees,
                        "selected trace has an interpolated or different original pose");
                }
                selected = true;
            }
        }
        Require(analysis && match && selected && states.size() > 1,
            "trace omitted a real construction phase");
    });

    Test("trace limits preserve the solver result and expose truncation", []
    {
        const auto problem = Sheet({Channel(), Rectangle("limited-trace-fill", 10, 3)}, 14, 5);
        auto options = Budget();
        options.TimeLimitSeconds = 10;
        const auto baseline = Solve(problem, options);
        std::vector<ConstructionTraceEvent> events;
        FeatureConstructionSettings settings;
        settings.MaxTraceEvents = 3;
        settings.Trace = [&](const ConstructionTraceEvent& event) { events.push_back(event); };
        const auto observed = Solve(problem, options, settings);
        OriginalAudit(problem, observed.Output);
        Require(events.size() <= settings.MaxTraceEvents,
            "trace exceeded its observer event limit");
        Require(PlacementFingerprint(baseline.Output) == PlacementFingerprint(observed.Output),
            "a full trace buffer changed selected original placements");
        const bool marked = std::any_of(events.begin(), events.end(), [](const ConstructionTraceEvent& event)
            { return event.Truncated; }) || DiagnosticValue(observed.Output, "trace-truncated") > 0;
        Require(marked, "limited trace appears to be a complete search history");
    });

    Test("a failing trace observer is isolated from original-material construction", []
    {
        const auto problem = Sheet({Channel(), Rectangle("observer-failure-fill", 10, 3)}, 14, 5);
        auto options = Budget();
        options.TimeLimitSeconds = 10;
        const auto baseline = Solve(problem, options);
        FeatureConstructionSettings settings;
        std::size_t calls = 0;
        settings.Trace = [&](const ConstructionTraceEvent&)
        {
            ++calls;
            throw std::runtime_error("synthetic observer failure");
        };
        const auto observed = Solve(problem, options, settings);
        OriginalAudit(problem, observed.Output);
        Require(calls == 1, "failing observer was invoked again");
        Require(PlacementFingerprint(baseline.Output) == PlacementFingerprint(observed.Output),
            "an observer exception changed the original construction result");
    });

    Test("limited feature analysis does not mark a complete observer stream as truncated", []
    {
        std::vector<Point> contour{{0,0}, {82,0}, {82,10}};
        for (int slot = 39; slot >= 0; --slot)
        {
            const double right = 2 * slot + 2;
            contour.push_back({right,10});
            contour.push_back({right,8});
            contour.push_back({right-1,8});
            contour.push_back({right-1,10});
        }
        contour.push_back({0,10});
        const auto problem = Sheet({Polygon("forty-independent-recesses", std::move(contour))}, 82, 10);
        std::vector<ConstructionTraceEvent> events;
        FeatureConstructionSettings settings;
        settings.Trace = [&](const ConstructionTraceEvent& event) { events.push_back(event); };
        const auto job = Solve(problem, Budget(), settings);
        OriginalAudit(problem, job.Output);
        Require(job.Checked.Complete, "bounded analysis prevented a legal original singleton placement");
        Require(std::any_of(events.begin(), events.end(), [](const ConstructionTraceEvent& event)
            { return event.Stage == ConstructionTraceStage::Analysis && event.AnalysisTruncated; }),
            "limited residual analysis was not separately identified");
        Require(std::none_of(events.begin(), events.end(), [](const ConstructionTraceEvent& event)
            { return event.Truncated; }), "feature-analysis limits falsely marked the recorded event stream as truncated");
        Require(DiagnosticValue(job.Output, "trace-truncated") == 0,
            "complete observer stream reported a trace buffer truncation");
    });

    Test("fixed-angle contact candidates retain tighter board-axis cross pairs", []
    {
        const auto problem = Sheet({Cross("same-angle-cross", 2)}, 24, 24);
        Detail::ShapeFeatureLimits limits;
        std::size_t attempts = 0;
        const auto matches = Detail::BuildShapeFeatureMatches(problem, Models(problem), limits, attempts);
        VerifyMatches(problem, matches);
        Require(std::any_of(matches.Matches.begin(), matches.Matches.end(), [](const auto& match)
        {
            return match.Value.Width * match.Value.Height <= 72 + 1e-5 && match.FootprintSaving < 0;
        }), "free-oriented OBB scoring erased a tighter legal stock-axis contact pair");
    });

    Test("opposite legal channel orientations retain their compact contact event", []
    {
        auto channel = Channel("opposite-channel", 2);
        channel.AllowedAnglesDegrees = {0, 180};
        const auto problem = Sheet({channel}, 24, 24);
        Detail::ShapeFeatureLimits limits;
        std::size_t attempts = 0;
        const auto matches = Detail::BuildShapeFeatureMatches(problem, Models(problem), limits, attempts);
        VerifyMatches(problem, matches);
        Require(std::any_of(matches.Matches.begin(), matches.Matches.end(), [](const auto& match)
        {
            return Near(match.FootprintSaving, 28) && Near(match.Value.Width * match.Value.Height, 112);
        }), "short-edge opposite-channel lock was absent from the original-angle contact candidates");
    });

    std::cout << Passed << " passed, " << Failed << " failed\n";
    return Failed ? 1 : 0;
}
