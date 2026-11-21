#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include "NestingPairSummary.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    struct Edge { Point A, B; double Length = 0; bool Reentrant = false; Point ExteriorNormal; };
    void SelectEdges(std::vector<Edge>& edges)
    {
        std::stable_sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b)
        { if (a.Reentrant != b.Reentrant) return a.Reentrant; return a.Length > b.Length; });
        if (edges.size() > 64) edges.resize(64);
    }
    std::vector<Edge> Edges(const Shape& shape, Pose pose, double tolerance)
    {
        std::vector<Edge> edges;
        auto append = [&](const std::vector<Point>& ring)
        {
            auto points = ring;
            const auto distance = [](Point a, Point b) { return std::hypot(a.X - b.X, a.Y - b.Y); };
            if (points.size() > 1 && distance(points.front(), points.back()) <= tolerance) points.pop_back();
            // Imported collinear subdivisions are not distinct contact features. Without reducing them,
            // a few long outside walls can crowd every short notch edge out of the feature budget.
            bool changed = true;
            while (changed && points.size() > 3)
            {
                changed = false;
                for (std::size_t i = 0; i < points.size(); ++i)
                {
                    const auto a = points[(i + points.size() - 1) % points.size()], p = points[i], b = points[(i + 1) % points.size()];
                    const double dx = b.X - a.X, dy = b.Y - a.Y, length2 = dx * dx + dy * dy;
                    const double t = length2 > 0 ? std::clamp(((p.X - a.X) * dx + (p.Y - a.Y) * dy) / length2, 0.0, 1.0) : 0;
                    if (distance(p, { a.X + t * dx, a.Y + t * dy }) <= tolerance)
                    { points.erase(points.begin() + static_cast<std::ptrdiff_t>(i)); changed = true; break; }
                }
            }
            long double signedArea = 0;
            const Point origin = points.empty() ? Point{} : points.front();
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                const auto a = points[i], b = points[(i + 1) % points.size()];
                signedArea += (static_cast<long double>(a.X) - origin.X) * (static_cast<long double>(b.Y) - origin.Y)
                    - (static_cast<long double>(a.Y) - origin.Y) * (static_cast<long double>(b.X) - origin.X);
            }
            const auto reentrant = [&](std::size_t i)
            {
                const auto a = points[(i + points.size() - 1) % points.size()], b = points[i], c = points[(i + 1) % points.size()];
                const double cross = (b.X - a.X) * (c.Y - b.Y) - (b.Y - a.Y) * (c.X - b.X);
                return (signedArea >= 0 ? cross : -cross) < -tolerance * std::max(distance(a, b), distance(b, c));
            };
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                const auto a = Transform(points[i], pose), b = Transform(points[(i + 1) % points.size()], pose);
                const double length = std::hypot(b.X - a.X, b.Y - a.Y);
                if (length > tolerance)
                {
                    const double side = (signedArea >= 0 ? 1.0 : -1.0)*(pose.Mirrored?-1.:1.);
                    edges.push_back({ a, b, length, reentrant(i) || reentrant((i + 1) % points.size()),
                        { side * (b.Y - a.Y) / length, -side * (b.X - a.X) / length } });
                }
            }
        };
        // Closed holes are reserved for the later gap-fill pass. External concave edges remain candidates.
        append(shape.Outer);
        SelectEdges(edges);
        return edges;
    }
    double EdgeAngle(const Edge& edge) { return std::atan2(edge.B.Y - edge.A.Y, edge.B.X - edge.A.X) * 180 / Pi; }
    Rect PatternBounds(const Pattern& pattern, const Problem& problem)
    {
        if (pattern.Members.empty()) return {};
        double x0 = std::numeric_limits<double>::infinity(), y0 = x0, x1 = -x0, y1 = -x0;
        for (const auto& m : pattern.Members)
        {
            const auto b = Bounds(problem.Parts[m.PartIndex].Geometry, m.Transform);
            x0 = std::min(x0, b.X); y0 = std::min(y0, b.Y);
            x1 = std::max(x1, b.X + b.Width); y1 = std::max(y1, b.Y + b.Height);
        }
        return { x0, y0, x1 - x0, y1 - y0 };
    }
    std::string Key(const Pattern& pattern, double tolerance)
    {
        std::vector<std::string> members;
        const double quantum = std::max(tolerance, 1e-7);
        for (const auto& m : pattern.Members)
        {
            std::ostringstream s; s << m.PartIndex << ':' << std::llround(m.Transform.X / quantum) << ':'
                << std::llround(m.Transform.Y / quantum) << ':' << std::llround(NormalizeAngle(m.Transform.AngleDegrees) * 1e6)<<':'<<m.Transform.Mirrored;
            members.push_back(s.str());
        }
        std::sort(members.begin(), members.end()); std::string key;
        for (const auto& s : members) key += s + ';';
        return key;
    }
    ShapeKind FilledBasicKind(const Pattern& pattern, const Problem& problem)
    {
        if (problem.PartGap > 0 || pattern.Members.size() < 2) return ShapeKind::Irregular;
        Clipper2Lib::PathsD paths;
        for (std::size_t i = 0; i < pattern.Members.size(); ++i)
        {
            const auto& member = pattern.Members[i];
            const auto& shape = problem.Parts[member.PartIndex].Geometry;
            if (shape.Radius > 0) return ShapeKind::Irregular; // Never replace analytic discs by inscribed polygons.
            for (std::size_t j = 0; j < i; ++j)
            {
                const auto& other = pattern.Members[j];
                if (ShapesConflict(shape, member.Transform, problem.Parts[other.PartIndex].Geometry,
                    other.Transform, 0, problem.Tolerance)) return ShapeKind::Irregular;
            }
            const auto append = [&](const std::vector<Point>& ring)
            {
                auto& path = paths.emplace_back();
                for (auto point : ring) { point = Transform(point, member.Transform); path.emplace_back(point.X, point.Y); }
            };
            append(shape.Outer); for (const auto& hole : shape.Holes) append(hole);
        }
        try
        {
            const auto material = Clipper2Lib::Union(paths, Clipper2Lib::FillRule::EvenOdd, 8);
            if (material.size() != 1) return ShapeKind::Irregular; // Holes and disconnected material remain explicit.
            std::vector<Point> points;
            for (const auto& p : material.front()) points.push_back({ p.x, p.y });
            std::sort(points.begin(), points.end(), [](Point a, Point b) { return a.X < b.X || (a.X == b.X && a.Y < b.Y); });
            points.erase(std::unique(points.begin(), points.end(), [](Point a, Point b) { return a.X == b.X && a.Y == b.Y; }), points.end());
            if (points.size() < 4) return ShapeKind::Irregular;
            const auto cross = [](Point a, Point b, Point c)
            { return (static_cast<long double>(b.X) - a.X) * (c.Y - a.Y) - (static_cast<long double>(b.Y) - a.Y) * (c.X - a.X); };
            std::vector<Point> hull(2 * points.size()); std::size_t count = 0;
            for (auto p : points) { while (count >= 2 && cross(hull[count - 2], hull[count - 1], p) <= 0) --count; hull[count++] = p; }
            const auto lower = count + 1;
            for (std::size_t i = points.size() - 1; i-- > 0;)
            { while (count >= lower && cross(hull[count - 2], hull[count - 1], points[i]) <= 0) --count; hull[count++] = points[i]; }
            hull.resize(count - 1);
            Shape envelope; envelope.Outer = std::move(hull);
            const auto kind = Classify(envelope, problem.Tolerance).Kind;
            if (kind != ShapeKind::Rectangle && kind != ShapeKind::Parallelogram) return ShapeKind::Irregular;
            double perimeter = 0;
            for (std::size_t i = 0; i < envelope.Outer.size(); ++i)
            { const auto a = envelope.Outer[i], b = envelope.Outer[(i + 1) % envelope.Outer.size()]; perimeter += std::hypot(b.X - a.X, b.Y - a.Y); }
            const double materialArea = std::abs(Clipper2Lib::Area(material));
            const double allowance = perimeter * problem.Tolerance;
            if (std::abs(Area(envelope) - materialArea) > allowance
                || std::abs(pattern.MaterialArea - materialArea) > allowance) return ShapeKind::Irregular;
            return kind;
        }
        catch (...) { return ShapeKind::Irregular; }
    }
    std::vector<Edge> PatternEdges(const Pattern& pattern, const Problem& problem)
    {
        std::vector<Edge> edges;
        bool polygonal = problem.PartGap == 0 && pattern.Members.size() > 1;
        for (const auto& m : pattern.Members) polygonal &= problem.Parts[m.PartIndex].Geometry.Radius == 0;
        if (polygonal)
        {
            Clipper2Lib::PathsD paths;
            for (const auto& m : pattern.Members)
            {
                const auto append = [&](const std::vector<Point>& ring)
                { auto& path = paths.emplace_back(); for (auto p : ring) { p = Transform(p, m.Transform); path.emplace_back(p.X, p.Y); } };
                const auto& shape = problem.Parts[m.PartIndex].Geometry;
                append(shape.Outer); for (const auto& hole : shape.Holes) append(hole);
            }
            try
            {
                const auto material = Clipper2Lib::Union(paths, Clipper2Lib::FillRule::EvenOdd, 8);
                for (const auto& path : material)
                {
                    if (Clipper2Lib::Area(path) <= 0) continue; // Closed internal voids remain for gap filling.
                    Shape outline; for (const auto& p : path) outline.Outer.push_back({ p.x, p.y });
                    auto visible = Edges(outline, {}, problem.Tolerance);
                    edges.insert(edges.end(), visible.begin(), visible.end());
                }
                if (!edges.empty()) { SelectEdges(edges); return edges; }
            }
            catch (...) { /* Preserve original contact features when union construction is unavailable. */ }
        }
        for (const auto& m : pattern.Members)
        { auto e = Edges(problem.Parts[m.PartIndex].Geometry, m.Transform, problem.Tolerance); edges.insert(edges.end(), e.begin(), e.end()); }
        SelectEdges(edges); return edges;
    }
    double PatternScore(const Pattern& p, const Problem&)
    {
        // Net original material over the actual stored-pose planning envelope.
        // Shape recognition and savings never promote a looser placement.
        return p.Width>0&&p.Height>0?p.MaterialArea/(p.Width*p.Height):
            -std::numeric_limits<double>::infinity();
    }
    bool Better(const Pattern& a, const Pattern& b, const Problem& problem)
    {
        const double sa = PatternScore(a, problem), sb = PatternScore(b, problem);
        if (sa != sb) return sa > sb;
        return Key(a, problem.Tolerance) < Key(b, problem.Tolerance);
    }
}

void NormalizePattern(Pattern& pattern, const Problem& problem)
{
    if (pattern.Members.empty()) { pattern.Width = pattern.Height = pattern.MaterialArea = 0; return; }
    const auto box = PatternBounds(pattern, problem);
    pattern.MaterialArea = 0;
    for (auto& m : pattern.Members)
    {
        m.Transform.X -= box.X; m.Transform.Y -= box.Y;
        m.Transform.AngleDegrees = NormalizeAngle(m.Transform.AngleDegrees);
        pattern.MaterialArea += Area(problem.Parts[m.PartIndex].Geometry);
    }
    pattern.Width = box.Width; pattern.Height = box.Height;
    if (pattern.Members.size() == 1) pattern.Kind = Classify(problem.Parts[pattern.Members.front().PartIndex].Geometry, problem.Tolerance).Kind;
    else
    {
        pattern.Kind = FilledBasicKind(pattern, problem);
        if (pattern.Kind == ShapeKind::Irregular && box.Width * box.Height > 0 && pattern.MaterialArea / (box.Width * box.Height) >= .85)
            pattern.Kind = ShapeKind::NearRectangle;
    }
}

std::vector<Pattern> BuildPatterns(const Problem& problem, const Options& options, const std::function<bool()>& stop, std::size_t* attemptsUsed)
{
    if(attemptsUsed)*attemptsUsed=0;
    std::vector<Pattern> output;
    PairPreparationSummary summary(problem,options,"ordinary",stop);
    if (options.MaxPatterns == 0 || options.MaxPatternMembers == 0) return output;
    const auto stopped = [&]() { return stop && stop(); };
    std::vector<std::vector<Pose>> angles(problem.Parts.size());
    for (std::size_t i = 0; i < problem.Parts.size() && !stopped(); ++i)
        if (problem.Parts[i].Quantity > 0)
        {
            angles[i] = CandidateOrientations(problem.Parts[i], problem.Tolerance,options.OrientationProposals);
            if(auto* record=summary.For(i))record->PlannedAngles=angles[i].size();
        }
    const std::size_t maxDepth = std::min<std::size_t>(4, options.MaxPatternMembers);
    const bool combinations = options.EnableCombinations && maxDepth >= 2;
    std::unordered_set<std::string> keys;
    // Classification belongs to the source specification and is independent of direction.
    std::vector<std::optional<ShapeKind>> singletonKinds(problem.Parts.size());
    // Singleton candidates are the baseline representation of every active type
    // and its supported directions. The additional-combination budget must not
    // silently remove an input type or its only direction that fits the sheet.
    // Round robin also keeps types represented fairly when the job is cancelled.
    const auto reflectedProposals=[&](std::size_t i)
    {return problem.Parts[i].AllowMirror&&options.OrientationProposals==OrientationProposalPolicy::AllowedReflections;};
    for (std::size_t orientationRound = 0; !stopped(); ++orientationRound)
    {
        bool any = false;
        for (std::size_t i = 0; i < angles.size() && !stopped(); ++i)
        for(std::size_t parity=0;parity<(reflectedProposals(i)?2u:1u)&&!stopped();++parity)
        {
            const auto angleIndex=reflectedProposals(i)?2*orientationRound+parity:orientationRound;
            if (angleIndex >= angles[i].size()) continue;
            if (!singletonKinds[i])
                singletonKinds[i] = Classify(problem.Parts[i].Geometry, problem.Tolerance).Kind;
            any = true; Pattern p; p.Members.push_back({ i, angles[i][angleIndex] });
            // Preserve NormalizePattern's exact bound reconstruction and pose arithmetic.
            const auto box = PatternBounds(p, problem);
            auto& pose = p.Members.front().Transform;
            pose.X -= box.X; pose.Y -= box.Y;
            pose.AngleDegrees = NormalizeAngle(pose.AngleDegrees);
            p.Width = box.Width; p.Height = box.Height;
            p.MaterialArea += Area(problem.Parts[i].Geometry);
            p.Kind = *singletonKinds[i];
            if (keys.insert(Key(p, problem.Tolerance)).second) output.push_back(std::move(p));
        }
        if (!any) break;
    }
    if (!combinations || stopped() || std::none_of(problem.Parts.begin(), problem.Parts.end(),
        [](const Part& part) { return part.Quantity >= 2; })) return output;
    const std::size_t singletonCount = output.size();
    const std::size_t totalLimit = options.MaxPatterns > std::numeric_limits<std::size_t>::max() - singletonCount
        ? std::numeric_limits<std::size_t>::max() : singletonCount + options.MaxPatterns;
    auto frontier = output;
    std::size_t attempts = 0;
    const std::size_t patternBudget = options.MaxPatterns > std::numeric_limits<std::size_t>::max() / 256
        ? std::numeric_limits<std::size_t>::max() : options.MaxPatterns * 256;
    const std::size_t attemptLimit = std::min(options.MaxPlacementAttempts, patternBudget);
    std::vector<double> partAreas(problem.Parts.size());
    std::vector<std::vector<Edge>> partFeatures(problem.Parts.size());
    std::unordered_map<std::string, double> largestSheetArea;
    for (std::size_t i = 0; i < problem.Parts.size() && !stopped(); ++i)
        if (problem.Parts[i].Quantity >= 2)
        { partAreas[i] = Area(problem.Parts[i].Geometry); partFeatures[i] = Edges(problem.Parts[i].Geometry, {}, problem.Tolerance); }
    for (const auto& stock : problem.Stocks) if (stock.Quantity)
        largestSheetArea[stock.Material] = std::max(largestSheetArea[stock.Material],
            std::max(0.0, stock.Width - 2 * problem.EdgeMargin) * std::max(0.0, stock.Height - 2 * problem.EdgeMargin));
    struct Work
    {
        const Pattern* Anchor = nullptr;
        std::vector<Edge> Contact;
        std::size_t PartIndex = 0;
        std::size_t ConcaveFeatures = 0;
        std::size_t AnchorRound = 0;
    };
    for (std::size_t depth = 2; depth <= maxDepth && output.size() < totalLimit && !stopped(); ++depth)
    {
        std::vector<Pattern> pool;
        std::vector<Work> work;
        std::vector<std::size_t> anchorRounds(problem.Parts.size());
        std::size_t taskCount = 0;
        for (const auto& anchor : frontier)
        {
            if (stopped()) break;
            if (!HasSinglePartType(anchor.Members)) continue;
            const auto partIndex = anchor.Members.front().PartIndex;
            const auto& part = problem.Parts[partIndex];
            // Decide eligibility before extracting contacts or considering any
            // partner. Different source indices never lend each other inventory,
            // even when their material and geometry are identical.
            if (anchor.Members.size() >= part.Quantity || angles[partIndex].empty()) continue;
            const double maxArea = largestSheetArea[part.Material];
            if (anchor.MaterialArea + partAreas[partIndex] > maxArea + std::max(1.0, maxArea) * 1e-12) continue;
            auto* preparationRecord=summary.For(partIndex);
            PairPreparationSummary::WorkTime preparationTime(preparationRecord,true);
            if(preparationRecord)++preparationRecord->PreworkVisits;
            Work w; w.Anchor = &anchor; w.PartIndex = partIndex;
            w.Contact = PatternEdges(anchor, problem);
            w.ConcaveFeatures = std::count_if(w.Contact.begin(), w.Contact.end(), [](const Edge& e) { return e.Reentrant; });
            w.AnchorRound = anchorRounds[partIndex]++;
            ++taskCount; work.push_back(std::move(w));
        }
        if (work.empty() || stopped()) break;
        std::stable_sort(work.begin(), work.end(), [](const Work& a, const Work& b)
        {
            if (a.AnchorRound != b.AnchorRound) return a.AnchorRound < b.AnchorRound;
            const auto basic = [](const Pattern& p) { return p.Kind == ShapeKind::Rectangle || p.Kind == ShapeKind::Parallelogram; };
            if (basic(*a.Anchor) != basic(*b.Anchor)) return basic(*a.Anchor);
            return a.ConcaveFeatures > b.ConcaveFeatures;
        });
        // Reserve work for deeper motifs instead of allowing exhaustive pairs to consume every attempt.
        const std::size_t depthEnd = attempts + (attemptLimit - attempts) / (maxDepth - depth + 1);
        if (depthEnd <= attempts) break;
        const std::size_t pairQuota = std::clamp<std::size_t>((depthEnd - attempts) / std::max<std::size_t>(1, taskCount), 48, 384);
        const auto processPair = [&](const Work& w)
        {
            if (attempts >= depthEnd || stopped()) return;
            const auto& anchor = *w.Anchor;
            const auto next = w.PartIndex;
            if (!HasSinglePartType(anchor.Members) || anchor.Members.front().PartIndex != next) return;
            const auto& part = problem.Parts[next];
            const bool selfPair=anchor.Members.size()==1&&anchor.Members.front().PartIndex==next;
            auto* pairRecord=selfPair?summary.For(next):nullptr;
            PairPreparationSummary::WorkTime pairTime(pairRecord);
            if(pairRecord)++pairRecord->Entered;
            const auto& anchorEdges = w.Contact;
            const auto localEdges = Edges(part.Geometry, {}, problem.Tolerance);
            const bool prioritizeContacts = w.ConcaveFeatures > 0 && std::any_of(localEdges.begin(), localEdges.end(),
                [](const Edge& edge) { return edge.Reentrant; });
            auto candidateAngles = angles[next];
            if (part.FreeRotation)
            {
                const auto addAngle = [&](double a,bool mirrored)
                {
                    a = NormalizeAngle(a);
                    for (auto old : candidateAngles)
                    { const auto d = std::abs(a - old.AngleDegrees); if (old.Mirrored==mirrored&&std::min(d, 360 - d) <= problem.Tolerance) return; }
                    if(std::count_if(candidateAngles.begin(),candidateAngles.end(),[&](Pose pose){return pose.Mirrored==mirrored;})<32)
                        candidateAngles.push_back({0,0,a,mirrored});
                };
                for (const auto& a : anchorEdges) for (const auto& b : localEdges)for(const bool mirrored:{false,true})
                {
                    if(mirrored&&(!part.AllowMirror||options.OrientationProposals==OrientationProposalPolicy::OriginalOnly))continue;
                    const auto ba=Transform(b.A,{0,0,0,mirrored}),bb=Transform(b.B,{0,0,0,mirrored});
                    const auto localAngle=std::atan2(bb.Y-ba.Y,bb.X-ba.X)*180/Pi;
                    addAngle(EdgeAngle(a)-localAngle,mirrored);addAngle(EdgeAngle(a)-localAngle+180,mirrored);
                }
            }
            const std::size_t pairEnd = attempts + std::min(pairQuota, depthEnd - attempts);
            std::size_t angleEnd = pairEnd;
            std::vector<Pattern> best;
            std::unordered_set<std::string> seenPoses;
            const auto pairDone = [&]() { return attempts >= pairEnd || stopped(); };
            const auto angleDone = [&]() { return attempts >= angleEnd || pairDone(); };
            const auto tryPose = [&](Pose pose)
            {
                if (angleDone()) return;
                std::ostringstream poseKey;
                poseKey << std::llround(pose.X / problem.Tolerance) << ':' << std::llround(pose.Y / problem.Tolerance)
                    << ':' << std::llround(NormalizeAngle(pose.AngleDegrees) * 1e6)<<':'<<pose.Mirrored;
                if (!seenPoses.insert(poseKey.str()).second) {if(pairRecord)++pairRecord->DuplicateRejected;return;}
                ++attempts;
                if(pairRecord){++pairRecord->Exact;PairPreparationSummary::ValidatedAngle(pairRecord,pose.AngleDegrees);}
                if (!PoseAllowed(part,pose,problem.Tolerance)) return;
                Pattern p = anchor; p.Members.push_back({ next, pose });
                const auto box = PatternBounds(p, problem);
                double separate = 0;
                for (const auto& m : p.Members)
                { const auto b = Bounds(problem.Parts[m.PartIndex].Geometry, m.Transform); separate += (b.Width + problem.PartGap) * (b.Height + problem.PartGap); }
                if ((box.Width + problem.PartGap) * (box.Height + problem.PartGap) > separate * 1.000001)
                {if(pairRecord)++pairRecord->EnvelopeRejected;return;}
                for (const auto& m : anchor.Members)
                {
                    if (ShapesConflict(problem.Parts[m.PartIndex].Geometry, m.Transform, part.Geometry, pose,
                        problem.PartGap, problem.Tolerance, problem.AllowCommonEdges))
                    {if(pairRecord)++pairRecord->ConflictRejected;return;}
                    for (const auto& hole : problem.Parts[m.PartIndex].Geometry.Holes)
                    {
                        Shape filledHole; filledHole.Outer = hole;
                        if (ShapesConflict(filledHole, m.Transform, part.Geometry, pose, 0, problem.Tolerance)) return;
                    }
                    for (const auto& hole : part.Geometry.Holes)
                    {
                        Shape filledHole; filledHole.Outer = hole;
                        if (ShapesConflict(filledHole, pose, problem.Parts[m.PartIndex].Geometry, m.Transform, 0, problem.Tolerance)) return;
                    }
                }
                NormalizePattern(p, problem);
                if(selfPair)
                {
                    if(AxisSafety::Pair(AxisSafety::Make(part.Geometry,p.Members[0].Transform),
                        AxisSafety::Make(part.Geometry,p.Members[1].Transform),problem.PartGap,problem.AllowCommonEdges,
                        problem.Tolerance)!=AxisSafety::Verdict::Safe)
                    {if(pairRecord)++pairRecord->NormalizationRejected;return;}
                    summary.Legal(next,p);
                }
                if (!keys.insert(Key(p, problem.Tolerance)).second) {if(pairRecord)++pairRecord->DuplicateRejected;return;}
                if (best.size() < 4) best.push_back(std::move(p));
                else
                {
                    const auto worst = std::min_element(best.begin(), best.end(), [&](const Pattern& a, const Pattern& b) { return Better(b, a, problem); });
                    if (Better(p, *worst, problem)) *worst = std::move(p);
                }
            };
            struct DirectionProposals { std::vector<Pose> Values; };
            std::vector<DirectionProposals> directions;
            const double gap = problem.PartGap;
            for (const auto orientation : candidateAngles)
            {
                const auto angle=orientation.AngleDegrees;
                if (pairDone()) break;
                PairPreparationSummary::Angle(pairRecord,anchor.Members.front().Transform.AngleDegrees,angle);
                const auto b = Bounds(part.Geometry,orientation);
                const double xs[] = { 0, (anchor.Width - b.Width) / 2, anchor.Width - b.Width };
                const double ys[] = { 0, (anchor.Height - b.Height) / 2, anchor.Height - b.Height };
                std::vector<Pose> overlays;
                for (const double x : xs) for (const double y : ys) overlays.push_back({ x - b.X, y - b.Y, angle,orientation.Mirrored });
                const auto rotatedEdges = Edges(part.Geometry,orientation,problem.Tolerance);
                struct ContactPose { Pose Transform; double EnvelopeArea = 0; bool Facing = false; };
                std::vector<ContactPose> contacts;
                // Inspect the bounded feature list before spending geometric trials. A single early
                // edge pair must not consume an angle's entire allowance while a shorter outside
                // wall supplies the useful interlock. Facing is independent of input ring winding.
                for (const auto& a : anchorEdges)
                {
                    if (angleDone()) break;
                    for (const auto& e : rotatedEdges)
                    {
                        if (angleDone()) break;
                        const double cross = (a.B.X - a.A.X) * (e.B.Y - e.A.Y) - (a.B.Y - a.A.Y) * (e.B.X - e.A.X);
                        if (std::abs(cross) > problem.Tolerance * std::max(a.Length, e.Length)) continue;
                        const Point n{ -(a.B.Y - a.A.Y) / a.Length, (a.B.X - a.A.X) / a.Length };
                        const Point ap[] = { a.A, a.B, { (a.A.X + a.B.X) / 2, (a.A.Y + a.B.Y) / 2 } };
                        const Point bp[] = { e.A, e.B, { (e.A.X + e.B.X) / 2, (e.A.Y + e.B.Y) / 2 } };
                        for (int side : { 0, -1, 1 }) for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
                        {
                            if (side == 0 && (!problem.AllowCommonEdges || gap == 0)) continue;
                            const Pose pose{ ap[i].X - bp[j].X + side * gap * n.X,
                                ap[i].Y - bp[j].Y + side * gap * n.Y, angle,orientation.Mirrored };
                            const double width = std::max(anchor.Width, b.X + pose.X + b.Width) - std::min(0.0, b.X + pose.X);
                            const double height = std::max(anchor.Height, b.Y + pose.Y + b.Height) - std::min(0.0, b.Y + pose.Y);
                            const double envelope = width * height;
                            // Disjoint real material cannot occupy a smaller box than its total area.
                            // Keep an area allowance derived from the geometry's linear tolerance.
                            const double allowance = 8 * problem.Tolerance * (width + height) * (anchor.Members.size() + 1)
                                + 64 * std::numeric_limits<double>::epsilon() * std::max(envelope, anchor.MaterialArea + partAreas[next]);
                            if (anchor.MaterialArea + partAreas[next] > envelope + allowance) continue;
                            const bool facing = a.ExteriorNormal.X * e.ExteriorNormal.X + a.ExteriorNormal.Y * e.ExteriorNormal.Y < -.5
                                && (side == 0 || side * (n.X * a.ExteriorNormal.X + n.Y * a.ExteriorNormal.Y) > .5);
                            contacts.push_back({ pose, envelope, facing });
                        }
                    }
                }
                std::stable_sort(contacts.begin(), contacts.end(), [](const ContactPose& a, const ContactPose& b)
                {
                    if (a.Facing != b.Facing) return a.Facing;
                    return a.EnvelopeArea < b.EnvelopeArea;
                });
                DirectionProposals proposals;
                // Exact contact candidates lead for concave originals. The
                // nine box overlays must not spend three quarters of every
                // direction's allowance before a real contour contact is tried.
                if (!prioritizeContacts) proposals.Values = overlays;
                const auto contactLimit = std::min(pairQuota, contacts.size());
                for (std::size_t i = 0; i < contactLimit; ++i) proposals.Values.push_back(contacts[i].Transform);
                if (prioritizeContacts) proposals.Values.insert(proposals.Values.end(), overlays.begin(), overlays.end());
                for (const double y : ys)
                { proposals.Values.push_back({ anchor.Width + gap - b.X, y - b.Y, angle,orientation.Mirrored }); proposals.Values.push_back({ -b.Width - gap - b.X, y - b.Y, angle,orientation.Mirrored }); }
                for (const double x : xs)
                { proposals.Values.push_back({ x - b.X, anchor.Height + gap - b.Y, angle,orientation.Mirrored }); proposals.Values.push_back({ x - b.X, -b.Height - gap - b.Y, angle,orientation.Mirrored }); }
                if(pairRecord)pairRecord->Generated+=proposals.Values.size();
                directions.push_back(std::move(proposals));
            }
            // Cover every sampled legal direction before revisiting an early
            // one. A 48-attempt pair budget used to stop after four directions
            // although a free original may have 20 to 32 useful directions.
            angleEnd = pairEnd;
            for (std::size_t rank = 0; !pairDone(); ++rank)
            {
                bool any = false;
                for (const auto& direction : directions)
                {
                    if (pairDone()) break;
                    if (rank >= direction.Values.size()) continue;
                    any = true; tryPose(direction.Values[rank]);
                }
                if (!any) break;
            }
            // A real self pair can be carried at another jointly legal angle
            // without rebuilding its contacts for each absolute orientation.
            // These are initial carrier candidates; the original pieces and
            // final normalized coordinates still receive exact safety checks.
            if (anchor.Members.size() == 1 && anchor.Members.front().PartIndex == next)
            {
                std::vector<Pattern> rotated;
                for (const auto& pattern : best) for (const auto orientation : angles[next])
                {
                    if (stopped()) break;
                    auto first=pattern.Members.front().Transform;first.X=first.Y=0;
                    const auto turn=ComposePose(orientation,InversePose(first));
                    if (!turn.Mirrored&&(turn.AngleDegrees<=problem.Tolerance||360-turn.AngleDegrees<=problem.Tolerance))continue;
                    auto variant = pattern; bool legal = true;
                    for (auto& member : variant.Members)
                    {
                        member.Transform=ComposePose(turn,member.Transform);
                        legal &= PoseAllowed(problem.Parts[member.PartIndex],member.Transform,problem.Tolerance);
                    }
                    if (!legal) continue;
                    NormalizePattern(variant, problem);
                    if (Better(variant, pattern, problem)) rotated.push_back(std::move(variant));
                }
                std::stable_sort(rotated.begin(), rotated.end(), [&](const auto& a, const auto& b) { return Better(a, b, problem); });
                std::size_t retained = 0;
                for (auto& variant : rotated)
                {
                    if (retained >= 4 || attempts >= depthEnd || stopped()) break;
                    if (!keys.insert(Key(variant, problem.Tolerance)).second) continue;
                    ++attempts;
                    if(pairRecord){++pairRecord->Exact;PairPreparationSummary::ValidatedAngle(pairRecord,variant.Members[1].Transform.AngleDegrees);}
                    const auto& a = variant.Members[0]; const auto& b = variant.Members[1];
                    if (ShapesConflict(part.Geometry, a.Transform, part.Geometry, b.Transform,
                        problem.PartGap, problem.Tolerance, problem.AllowCommonEdges) ||
                        AxisSafety::Pair(AxisSafety::Make(part.Geometry, a.Transform), AxisSafety::Make(part.Geometry, b.Transform),
                            problem.PartGap, problem.AllowCommonEdges, problem.Tolerance) != AxisSafety::Verdict::Safe)
                    {if(pairRecord)++pairRecord->NormalizationRejected;continue;}
                    summary.Legal(next,variant);
                    pool.push_back(std::move(variant)); ++retained;
                }
            }
            for (auto& p : best) pool.push_back(std::move(p));
        };
        // Each source type receives one self-combination anchor per round
        // before further orientations of a prolific type consume the layer.
        for (const auto& w : work)
        {
            if (attempts >= depthEnd || stopped()) break;
            processPair(w);
        }
        if (pool.empty()) break;
        std::stable_sort(pool.begin(), pool.end(), [&](const Pattern& a, const Pattern& b) { return Better(a, b, problem); });
        const auto remaining = totalLimit - output.size();
        const auto quota = std::max<std::size_t>(1, remaining / (maxDepth - depth + 1));
        const auto take = std::min(quota, pool.size());
        std::vector<bool> selected(pool.size());
        std::vector<std::size_t> represented(problem.Parts.size());
        std::vector<Rect> representedBounds(problem.Parts.size());
        std::size_t count = 0;
        // Keep the tightest certified result first. The remaining self quota
        // provides bounded search coverage for recurring concave demands; it
        // is not a quality bonus and cannot displace that best utilization.
        if(take){selected[0]=true;++count;}
        const auto selfQuota = count == take ? 0 : std::max<std::size_t>(1, (take - count) / 4);
        std::size_t selfCount = 0;
        for (std::size_t pass = 0; pass < 2 && selfCount < selfQuota; ++pass)
        for (std::size_t i = 0; i < pool.size() && selfCount < selfQuota; ++i)
        {
            const auto& pattern = pool[i];
            const auto partIndex = pattern.Members.front().PartIndex;
            if (selected[i] || represented[partIndex] != pass || !std::all_of(pattern.Members.begin(), pattern.Members.end(),
                [&](const Member& member) { return member.PartIndex == partIndex; })) continue;
            if (pass && representedBounds[partIndex].Width <= pattern.Width + problem.Tolerance
                && representedBounds[partIndex].Height <= pattern.Height + problem.Tolerance) continue;
            if (!std::any_of(partFeatures[partIndex].begin(), partFeatures[partIndex].end(),
                [](const Edge& edge) { return edge.Reentrant; })) continue;
            double separate = 0;
            for (const auto& member : pattern.Members)
            {
                const auto box = Bounds(problem.Parts[partIndex].Geometry, member.Transform);
                separate += (box.Width + problem.PartGap) * (box.Height + problem.PartGap);
            }
            const double combined = (pattern.Width + problem.PartGap) * (pattern.Height + problem.PartGap);
            if (separate - combined <= 8 * problem.Tolerance * (pattern.Width + pattern.Height) * pattern.Members.size()
                + 64 * std::numeric_limits<double>::epsilon() * std::max(separate, combined)) continue;
            if (!pass) representedBounds[partIndex] = { 0, 0, pattern.Width, pattern.Height };
            ++represented[partIndex]; selected[i] = true; ++selfCount; ++count;
        }
        for (std::size_t i = 0; i < pool.size() && count < take; ++i)
            if (!selected[i]) { selected[i] = true; ++count; }
        frontier.clear(); frontier.reserve(take);
        for (std::size_t i = 0; i < pool.size(); ++i)
            if (selected[i]) { summary.Retain(pool[i]);frontier.push_back(pool[i]); output.push_back(std::move(pool[i])); }
        if (attempts >= attemptLimit) break;
    }
    if(attemptsUsed)*attemptsUsed=attempts;
    return output;
}
}

namespace iCAX::SheetNesting::Detail
{
std::vector<Pattern> BuildClosurePatterns(const Problem& problem, const Options& options, const std::function<bool()>& stop)
{
    bool interrupted = false;
    const auto halted = [&]() { return interrupted || (interrupted = stop && stop()); };
    // This is an independent preparation strategy. The broad BuildPatterns
    // strategy retains its entire original budget and candidate order.
    const std::size_t maxDepth = std::min<std::size_t>(4, options.MaxPatternMembers);
    std::size_t inventory = 0;
    for (const auto& part : problem.Parts)
    {
        if (part.Quantity > maxDepth - inventory) { inventory = maxDepth + 1; break; }
        inventory += part.Quantity;
    }
    const bool hasSelfDemand = std::any_of(problem.Parts.begin(), problem.Parts.end(),
        [](const Part& part) { return part.Quantity >= 2; });
    if (!options.EnableCombinations || problem.PartGap > 0 || inventory < 2 || inventory > maxDepth || !hasSelfDemand)
    {
        auto base = options; base.EnableCombinations = false;
        return BuildPatterns(problem, base, halted);
    }
    // Reach the complete inventory before a wide layer of partial assemblies
    // consumes the preparation allowance. Every narrow-beam candidate uses the
    // unchanged member geometry, angle, quantity and conflict validation.
    auto narrow = options;
    narrow.MaxPatterns = std::min<std::size_t>(12, options.MaxPatterns);
    narrow.MaxPlacementAttempts = std::min<std::size_t>(3072, options.MaxPlacementAttempts / 4);
    auto closed = BuildPatterns(problem, narrow, halted);
    for (const auto& p : closed)
    {
        if (p.Members.size() != inventory || (p.Kind != ShapeKind::Rectangle && p.Kind != ShapeKind::Parallelogram)) continue;
        for (const auto& stock : problem.Stocks)
            if (stock.Quantity && stock.Material == problem.Parts[p.Members.front().PartIndex].Material
                && p.Width <= stock.Width - 2 * problem.EdgeMargin + problem.Tolerance
                && p.Height <= stock.Height - 2 * problem.EdgeMargin + problem.Tolerance)
                return closed;
    }
    if (halted()) return closed;
    // Failure to close is not infeasibility. Spend only this job's remaining
    // attempt/time allowance on the normal wider preparation.
    auto remaining = options;
    remaining.MaxPlacementAttempts -= narrow.MaxPlacementAttempts;
    auto wider = BuildPatterns(problem, remaining, halted);
    return wider.empty() ? closed : wider;
}
}
