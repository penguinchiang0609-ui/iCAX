#include "NestingInternal.h"
#include "NestingSpatialIndex.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>
#include <unordered_map>

namespace iCAX::SheetNesting::Detail
{
    namespace
    {
        struct HoleSlot
        {
            std::size_t Parent, Seen=0;
            Rect Domain;
            MaxRects Packing;
            HoleSlot(std::size_t parent,Rect domain,const Problem& p)
                :Parent(parent),Domain(domain),Packing(domain.Width+p.PartGap,domain.Height+p.PartGap,p.Tolerance){}
        };
        struct Bin
        {
            std::size_t StockIndex;
            Layout Output;
            MaxRects Packing;
            std::vector<HoleSlot> HoleSlots;
            std::size_t HoleParentsScanned=0;
            std::vector<SpatialBounds> CollisionBounds;
            std::unique_ptr<SpatialIndex> CollisionIndex;
            std::size_t IndexedCount = 0;
            std::optional<std::size_t> FailedGlobalGapCount;
            std::optional<std::size_t> FailedMainCandidateCount;
            Bin(std::size_t index, std::size_t instance, const Stock& stock, const Problem& p)
                : StockIndex(index), Output{stock.ID, instance, stock.Width, stock.Height, {}, 0},
                Packing(std::max(0.0, stock.Width - 2 * p.EdgeMargin + p.PartGap),
                    std::max(0.0, stock.Height - 2 * p.EdgeMargin + p.PartGap), p.Tolerance) {}
        };
        struct Choice
        {
            std::size_t BinIndex = 0, PatternIndex = 0;
            Rect Box;
            double Primary = 0, Secondary = 0, Fill = 0, Area = 0, DensityRank = 0, AnchorRank = 0, BasicUnitRank = 0;
            std::size_t Columns = 1, Copies = 1;
            double BatchArea = 0;
            double BatchSavings = 0;
        };
        auto Rank(const Choice& c) { return std::tuple(-c.BatchSavings, -c.BatchArea, c.AnchorRank, c.BasicUnitRank, c.DensityRank, c.Primary, c.Secondary, -c.Fill, -c.Area, c.PatternIndex, c.BinIndex, c.Columns); }
        auto AreaFirstRank(const Choice& c) { return std::tuple(-c.BatchArea, c.DensityRank, -c.BatchSavings, c.Primary, c.Secondary, -c.Fill, -c.Area, c.PatternIndex, c.BinIndex, c.Columns); }
    }

    Result RunRules(const Problem& problem, const Options& options, RectScore score, bool combinations, bool circleLattice, double preparationShare, bool batchHoles, CombinationMode mode, PatternOrder order, PatternBatchMode batches)
    {
        const auto started = std::chrono::steady_clock::now();
        Result result;
        std::size_t committedBatches = 0;
        std::size_t globalGapProbes = 0, localGapProbes = 0, cachedGapFailures = 0;
        std::size_t cachedMainFailures = 0;
        double gapProbeSeconds = 0;
        const double gapTimeLimit = options.TimeLimitSeconds * .15;
        const auto gapAttemptLimit = std::max<std::size_t>(1, options.MaxPlacementAttempts / 8);
        std::size_t gapAttempts = 0, probeAttemptStart = 0, quotaSkippedProbes = 0;
        bool gapProbeActive = false, gapQuotaExhausted = false;
        auto probeStarted = started;
        std::vector<std::size_t> consumed(problem.Parts.size(), 0), stocksUsed(problem.Stocks.size(), 0);
        std::unordered_map<std::string, std::size_t> partByID;
        for (std::size_t i = 0; i < problem.Parts.size(); ++i) partByID.emplace(problem.Parts[i].ID, i);
        bool cancelled = false, exhausted = false;
        const auto stop = [&]
        {
            if (cancelled || exhausted) return true;
            if (options.Cancel && options.Cancel()) { cancelled = true; return true; }
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - started).count();
            if (elapsed >= options.TimeLimitSeconds || result.Stats.PlacementAttempts >= options.MaxPlacementAttempts)
            { exhausted = true; return true; }
            if (gapProbeActive && (gapProbeSeconds + std::chrono::duration<double>(now - probeStarted).count() >= gapTimeLimit ||
                result.Stats.PlacementAttempts - probeAttemptStart >= gapAttemptLimit - std::min(gapAttempts, gapAttemptLimit)))
            { gapQuotaExhausted = true; return true; }
            return false;
        };
        auto buildOptions = options;
        buildOptions.EnableCombinations = options.EnableCombinations && combinations;
        bool preparationLimited = false;
        const auto preparationStop = [&]
        {
            if (stop()) return true;
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() >= options.TimeLimitSeconds * preparationShare)
            { preparationLimited = true; return true; }
            return false;
        };
        auto patterns = mode==CombinationMode::CornerChains
            ?BuildComplementChains(problem,buildOptions,preparationStop)
            :mode==CombinationMode::Closure?BuildClosurePatterns(problem,buildOptions,preparationStop)
            :BuildPatterns(problem, buildOptions, preparationStop);
        result.Stats.CandidatePatterns = patterns.size();
        std::vector<double> anchorArea(problem.Parts.size());
        std::vector<double> partAreas(problem.Parts.size()),areaUncertainty(problem.Parts.size());
        std::vector<bool> rectangleParts(problem.Parts.size());
        for (std::size_t pi = 0; pi < problem.Parts.size() && !stop(); ++pi)
        {
            const auto type = Classify(problem.Parts[pi].Geometry, problem.Tolerance);
            anchorArea[pi] = type.EnvelopeWidth * type.EnvelopeHeight;
            partAreas[pi]=type.Area;
            double magnitude=1;std::size_t vertices=0;
            const auto account=[&](const auto& ring){for(auto point:ring){magnitude=std::max({magnitude,std::abs(point.X),std::abs(point.Y)});++vertices;}};
            account(problem.Parts[pi].Geometry.Outer);for(const auto& ring:problem.Parts[pi].Geometry.Holes)account(ring);
            areaUncertainty[pi]=64*std::numeric_limits<double>::epsilon()*
                (problem.Parts[pi].Geometry.Radius>0?type.Area:magnitude*magnitude*(vertices+1));
            rectangleParts[pi] = type.Kind==ShapeKind::Rectangle&&problem.Parts[pi].Geometry.Holes.empty();
        }
        std::vector<double> reservationSavings(batches == PatternBatchMode::Disabled ? 0 : patterns.size());
        if (batches != PatternBatchMode::Disabled)
            for (std::size_t pi = 0; pi < patterns.size() && !stop(); ++pi)
            {
                const auto& pattern = patterns[pi];
                if (pattern.Members.size() < 2) continue;
                if (pattern.Kind != ShapeKind::Rectangle && pattern.Kind != ShapeKind::Parallelogram &&
                    pattern.Kind != ShapeKind::NearRectangle)
                {
                    // Direction-changing interlocks may save space despite remaining
                    // visibly concave. Keep same-direction irregular groups as normal
                    // candidates: reserving their inventory can obstruct odd leftovers.
                    const double firstAngle = pattern.Members.front().Transform.AngleDegrees;
                    if (std::none_of(pattern.Members.begin(), pattern.Members.end(), [&](const Member& member)
                        { return std::abs(std::remainder(member.Transform.AngleDegrees - firstAngle, 360.)) > problem.Tolerance; })) continue;
                }
                double separate = 0, perimeter = 0;
                for (const auto& member : pattern.Members)
                {
                    const auto bounds = Bounds(problem.Parts[member.PartIndex].Geometry, member.Transform);
                    separate += (bounds.Width + problem.PartGap) * (bounds.Height + problem.PartGap);
                    perimeter += bounds.Width + bounds.Height + 2 * problem.PartGap;
                }
                const double combined = (pattern.Width + problem.PartGap) * (pattern.Height + problem.PartGap);
                const double uncertainty = 2 * problem.Tolerance * (perimeter + pattern.Width + pattern.Height + 2 * problem.PartGap)
                    + 64 * std::numeric_limits<double>::epsilon() * std::max(separate, combined);
                // Reserve verified basic groups and direction-changing interlocks
                // that save planning footprint even below the near-rectangle
                // density threshold. Its actual shape and holes remain unchanged;
                // this is neither material area nor a reusable-remnant certificate.
                if (separate - combined > uncertainty) reservationSavings[pi] = separate - combined;
            }
        result.Stats.PreparationSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::vector<Bin> bins;
        std::size_t nextGroup = 1;

        const auto available = [&](const Pattern& pattern, const Bin& bin)
        {
            std::vector<std::pair<std::size_t, std::size_t>> needed;
            for (const auto& member : pattern.Members)
            {
                if (member.PartIndex >= problem.Parts.size()) return false;
                const auto& part = problem.Parts[member.PartIndex];
                if (part.Material != problem.Stocks[bin.StockIndex].Material) return false;
                auto found = std::find_if(needed.begin(), needed.end(), [&](auto n) { return n.first == member.PartIndex; });
                if (found == needed.end()) needed.emplace_back(member.PartIndex, 1);
                else ++found->second;
            }
            for (auto [part, count] : needed) if (count > problem.Parts[part].Quantity - consumed[part]) return false;
            return !needed.empty();
        };
        const auto canPlace = [&](Bin& bin, std::size_t partIndex, Pose pose)
        {
            if (stop()) return false;
            ++result.Stats.PlacementAttempts;
            const auto& part = problem.Parts[partIndex];
            if (part.Material != problem.Stocks[bin.StockIndex].Material || !AngleAllowed(part, pose.AngleDegrees, problem.Tolerance) ||
                !InsideSheet(part.Geometry, pose, bin.Output.Width, bin.Output.Height, problem.EdgeMargin, problem.Tolerance)) return false;
            if (batches == PatternBatchMode::Disabled)
            {
                for (const auto& placed : bin.Output.Placements)
                {
                    if (stop()) return false;
                    const auto other = partByID.at(placed.PartID);
                    if (ShapesConflict(part.Geometry, pose, problem.Parts[other].Geometry, placed.Transform, problem.PartGap, problem.Tolerance)) return false;
                }
                return true;
            }
            std::vector<std::size_t> neighbors;
            {
                // Rebuild an immutable prefix occasionally; query the append-only
                // tail directly. The narrow predicate and its order stay unchanged.
                for (auto i = bin.CollisionBounds.size(); i < bin.Output.Placements.size(); ++i)
                {
                    const auto& placed = bin.Output.Placements[i];
                    bin.CollisionBounds.push_back(SpatialBounds::From(Bounds(problem.Parts[partByID.at(placed.PartID)].Geometry, placed.Transform)));
                }
                if (bin.CollisionBounds.size() - bin.IndexedCount >= 64)
                {
                    bin.CollisionIndex = std::make_unique<SpatialIndex>(bin.CollisionBounds);
                    bin.IndexedCount = bin.CollisionBounds.size();
                }
                if (bin.CollisionIndex)
                    neighbors = bin.CollisionIndex->Query(SpatialBounds::From(Bounds(part.Geometry, pose)), problem.PartGap);
                for (auto i = bin.IndexedCount; i < bin.Output.Placements.size(); ++i) neighbors.push_back(i);
            }
            for (auto placedIndex : neighbors)
            {
                if (stop()) return false;
                const auto& placed = bin.Output.Placements[placedIndex];
                const auto other = partByID.at(placed.PartID);
                if (ShapesConflict(part.Geometry, pose, problem.Parts[other].Geometry, placed.Transform, problem.PartGap, problem.Tolerance)) return false;
            }
            return true;
        };
        const auto validPattern = [&](Bin& bin, const Pattern& pattern, double x, double y)
        {
            if (!available(pattern, bin)) return false;
            for (std::size_t i = 0; i < pattern.Members.size(); ++i)
            {
                const auto& member = pattern.Members[i];
                Pose pose{member.Transform.X + x, member.Transform.Y + y, member.Transform.AngleDegrees};
                if (!canPlace(bin, member.PartIndex, pose)) return false;
                for (std::size_t j = 0; j < i; ++j)
                {
                    const auto& other = pattern.Members[j];
                    const Pose otherPose{other.Transform.X + x, other.Transform.Y + y, other.Transform.AngleDegrees};
                    if (ShapesConflict(problem.Parts[member.PartIndex].Geometry,
                        batches == PatternBatchMode::Disabled ? member.Transform : pose,
                        problem.Parts[other.PartIndex].Geometry,
                        batches == PatternBatchMode::Disabled ? other.Transform : otherPose, problem.PartGap, problem.Tolerance)) return false;
                }
            }
            return true;
        };
        const auto repetitions = [&](const Pattern& pattern)
        {
            std::vector<std::pair<std::size_t, std::size_t>> needed;
            for (const auto& member : pattern.Members)
            {
                auto found = std::find_if(needed.begin(), needed.end(), [&](auto n) { return n.first == member.PartIndex; });
                if (found == needed.end()) needed.emplace_back(member.PartIndex, 1);
                else ++found->second;
            }
            auto copies = std::numeric_limits<std::size_t>::max();
            for (auto [part, count] : needed)
                copies = std::min(copies, (problem.Parts[part].Quantity - consumed[part]) / count);
            return needed.empty() ? std::size_t{0} : copies;
        };
        const auto fitCount = [](double extent, double step)
        {
            if (!std::isfinite(extent) || !std::isfinite(step) || extent < step || step <= 0) return std::size_t{0};
            const auto n = std::floor(static_cast<long double>(extent) / step);
            return n >= static_cast<long double>(std::numeric_limits<std::size_t>::max())
                ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(n);
        };
        const auto choose = [&](Bin& bin, std::size_t binIndex) -> std::optional<Choice>
        {
            if (batches != PatternBatchMode::Disabled && bin.FailedMainCandidateCount == bin.Output.Placements.size())
            { ++cachedMainFailures; return std::nullopt; }
            std::optional<Choice> best;
            for (std::size_t pi = 0; pi < patterns.size() && !stop(); ++pi)
            {
                const auto& pattern = patterns[pi];
                if (!available(pattern, bin)) continue;
                const double w = pattern.Width + problem.PartGap, h = pattern.Height + problem.PartGap;
                const auto inventory = batches == PatternBatchMode::Disabled ? std::size_t{1} : repetitions(pattern);
                for (const auto& free : bin.Packing.FreeRectangles())
                {
                    if (w > free.Width + problem.Tolerance || h > free.Height + problem.Tolerance) continue;
                    const double shortSide = std::min(free.Width - w, free.Height - h);
                    const double longSide = std::max(free.Width - w, free.Height - h);
                    double primary = shortSide, secondary = longSide;
                    if (score == RectScore::Area) { primary = free.Width * free.Height - w * h; secondary = shortSide; }
                    if (score == RectScore::LongSide) { primary = longSide; secondary = shortSide; }
                    Choice candidate{binIndex, pi, {free.X, free.Y, w, h}, primary, secondary,
                        pattern.MaterialArea / std::max(pattern.Width * pattern.Height, 1e-20), pattern.MaterialArea};
                    if (combinations)
                    {
                        candidate.DensityRank = -std::round(std::clamp(candidate.Fill, 0.0, 1.0) * 1e8) / 1e8;
                        for (const auto& member : pattern.Members)
                            candidate.AnchorRank = std::min(candidate.AnchorRank, -anchorArea[member.PartIndex]);
                        if (pattern.Members.size() > 1 && (pattern.Kind == ShapeKind::Rectangle || pattern.Kind == ShapeKind::Parallelogram))
                            candidate.BasicUnitRank = -1;
                    }
                    // Independent fit branches retain the same validated motif pool.
                    // A dense/large motif is not always the best use of this free space.
                    if (order != PatternOrder::DenseAnchor)
                    {
                        candidate.BasicUnitRank = 0;
                        candidate.DensityRank = 0;
                        if (order == PatternOrder::BestFit) candidate.AnchorRank = 0;
                    }
                    const auto consider = [&](Choice trial)
                    {
                        if (best && !(batches == PatternBatchMode::AreaFirst
                            ? AreaFirstRank(trial) < AreaFirstRank(*best) : Rank(trial) < Rank(*best))) return;
                        if (validPattern(bin, pattern, free.X + problem.EdgeMargin, free.Y + problem.EdgeMargin)) best = trial;
                    };
                    if (batches == PatternBatchMode::Disabled) consider(candidate);
                    else
                    {
                        // Full-width and full-height row-major batches share the
                        // same available unit count. Short final rows stay free.
                        auto columns = std::min(inventory, fitCount(free.Width, w));
                        auto rows = std::min(inventory, fitCount(free.Height, h));
                        // Retain a tolerance-valid singleton at a tight boundary;
                        // it is still accepted only by actual geometry below.
                        columns = std::max<std::size_t>(1, columns);
                        rows = std::max<std::size_t>(1, rows);
                        const auto capacity = columns > inventory / rows ? inventory : columns * rows;
                        const auto count = std::min(inventory, capacity);
                        if (!count) continue;
                        const auto narrowColumns = count / rows + (count % rows != 0);
                        for (const auto nx : {columns, narrowColumns})
                        {
                            if (stop()) break;
                            const auto ny = count / nx + (count % nx != 0);
                            auto trial = candidate;
                            trial.Columns = nx; trial.Copies = count;
                            trial.BatchArea = pattern.MaterialArea * static_cast<double>(count);
                            trial.BatchSavings = reservationSavings[pi] * static_cast<double>(count);
                            // Equal material gain prefers the denser physical
                            // unit before spending more of this free rectangle.
                            trial.DensityRank = -std::round(std::clamp(trial.Fill, 0.0, 1.0) * 1e8) / 1e8;
                            const auto dx = std::max(0.0, free.Width - static_cast<double>(nx) * w);
                            const auto dy = std::max(0.0, free.Height - static_cast<double>(ny) * h);
                            trial.Primary = std::min(dx, dy); trial.Secondary = std::max(dx, dy);
                            consider(trial);
                            if (narrowColumns == columns) break;
                        }
                    }
                }
            }
            // With unchanged packing, the first-unit anchors are fixed; less
            // remaining inventory only removes available motifs. Batch counts
            // change rank, not the first-unit validity tested above.
            if (batches != PatternBatchMode::Disabled && !best && !stop())
                bin.FailedMainCandidateCount = bin.Output.Placements.size();
            return best;
        };
        const auto commit = [&](Bin& bin, const Pattern& pattern, const Rect& box)
        {
            for (const auto& member : pattern.Members)
            {
                const auto& part = problem.Parts[member.PartIndex];
                bin.Output.Placements.push_back({part.ID, consumed[member.PartIndex]++,
                    {member.Transform.X + box.X + problem.EdgeMargin, member.Transform.Y + box.Y + problem.EdgeMargin,
                        NormalizeAngle(member.Transform.AngleDegrees)}, nextGroup, false});
                bin.Output.MaterialArea += Area(part.Geometry);
            }
            if (pattern.Members.size() > 1) ++result.Stats.CombinationGroups;
            ++nextGroup;
            bin.Packing.Occupy(box);
        };
        const auto gapFillOne = [&](Bin& bin, const std::optional<Rect>& localDomain = std::nullopt)
        {
            if (!options.EnableGapFill) return false;
            const auto insideDomain = [&](std::size_t partIndex, Pose pose)
            {
                if (!localDomain) return true;
                const auto bounds = Bounds(problem.Parts[partIndex].Geometry, pose);
                const auto& domain = *localDomain;
                return bounds.X >= domain.X - problem.Tolerance && bounds.Y >= domain.Y - problem.Tolerance &&
                    bounds.X + bounds.Width <= domain.X + domain.Width + problem.Tolerance &&
                    bounds.Y + bounds.Height <= domain.Y + domain.Height + problem.Tolerance;
            };
            const double usableArea=std::max(0.0,bin.Output.Width-2*problem.EdgeMargin)*std::max(0.0,bin.Output.Height-2*problem.EdgeMargin);
            const double count=static_cast<double>(bin.Output.Placements.size()+1);
            // Do not repeatedly search full sheets before opening another stock.
            // The allowance covers tolerated boundary strips/pair contacts and
            // conservative floating area accumulation; tiny uncertain parts keep
            // the ordinary geometric search instead of being declared impossible.
            double allowance=4*problem.Tolerance*(bin.Output.Width+bin.Output.Height)*count*count
                +64*std::numeric_limits<double>::epsilon()*usableArea*count;
            for(const auto& placed:bin.Output.Placements)allowance+=areaUncertainty[partByID.at(placed.PartID)];
            double smallest=std::numeric_limits<double>::infinity();
            for(std::size_t pi=0;pi<problem.Parts.size();++pi)
                if(consumed[pi]<problem.Parts[pi].Quantity&&problem.Parts[pi].Material==problem.Stocks[bin.StockIndex].Material)
                    smallest=std::min(smallest,std::max(0.0,partAreas[pi]-areaUncertainty[pi]));
            if(smallest>std::max(0.0,usableArea-bin.Output.MaterialArea+allowance))return false;
            if(batchHoles)
            {
                // Discover physical rectangular holes once. Their local free-space
                // plans are updated incrementally instead of restarting a global
                // contour-anchor search for every small part.
                for(;bin.HoleParentsScanned<bin.Output.Placements.size();++bin.HoleParentsScanned)
                {
                    if(stop())return false;
                    const auto parent=bin.HoleParentsScanned;const auto& placed=bin.Output.Placements[parent];
                    const auto& geometry=problem.Parts[partByID.at(placed.PartID)].Geometry;
                    for(const auto& ring:geometry.Holes)
                    {
                        Shape hole;for(auto point:ring)hole.Outer.push_back(Transform(point,placed.Transform));
                        const auto type=Classify(hole,problem.Tolerance);const auto bounds=Bounds(hole);
                        if(type.Kind!=ShapeKind::Rectangle||std::abs(type.Area-bounds.Width*bounds.Height)
                            >problem.Tolerance*(bounds.Width+bounds.Height)*2)continue;
                        Rect domain{bounds.X+problem.PartGap,bounds.Y+problem.PartGap,
                            bounds.Width-2*problem.PartGap,bounds.Height-2*problem.PartGap};
                        if(domain.Width>problem.Tolerance&&domain.Height>problem.Tolerance)bin.HoleSlots.emplace_back(parent,domain,problem);
                    }
                }
                for(auto& slot:bin.HoleSlots)
                {
                    for(;slot.Seen<bin.Output.Placements.size();++slot.Seen)
                    {
                        if(stop())return false;
                        if(slot.Seen==slot.Parent)continue;
                        const auto& placed=bin.Output.Placements[slot.Seen];const auto& geometry=problem.Parts[partByID.at(placed.PartID)].Geometry;
                        const auto b=Bounds(geometry,placed.Transform);
                        if(b.X+b.Width+problem.PartGap<=slot.Domain.X||b.Y+b.Height+problem.PartGap<=slot.Domain.Y
                            ||b.X>=slot.Domain.X+slot.Domain.Width+problem.PartGap||b.Y>=slot.Domain.Y+slot.Domain.Height+problem.PartGap)continue;
                        Shape domain;domain.Outer={{0,0},{slot.Domain.Width,0},{slot.Domain.Width,slot.Domain.Height},{0,slot.Domain.Height}};
                        if(!ShapesConflict(domain,{slot.Domain.X,slot.Domain.Y,0},geometry,placed.Transform,problem.PartGap,problem.Tolerance))continue;
                        slot.Packing.Occupy({b.X-slot.Domain.X,b.Y-slot.Domain.Y,b.Width+problem.PartGap,b.Height+problem.PartGap});
                    }
                }
                std::optional<std::tuple<double,double,double,std::size_t,std::size_t,Rect>> holeBest;
                const double areaQuantum=problem.Tolerance*(bin.Output.Width+bin.Output.Height);
                for(std::size_t hi=0;hi<bin.HoleSlots.size()&&!stop();++hi)
                {
                    const auto& slot=bin.HoleSlots[hi];
                    if(slot.Packing.FreeRectangles().empty())continue;
                    for(std::size_t pi=0;pi<patterns.size()&&!stop();++pi)
                    {
                        const auto& pattern=patterns[pi];
                        if(pattern.Members.size()!=1||!rectangleParts[pattern.Members[0].PartIndex]||!available(pattern,bin))continue;
                        const auto member=pattern.Members[0];const double area=Area(problem.Parts[member.PartIndex].Geometry);
                        if(std::abs(area-pattern.Width*pattern.Height)>problem.Tolerance*(pattern.Width+pattern.Height)*2)continue;
                        const auto fit=slot.Packing.Find(pattern.Width+problem.PartGap,pattern.Height+problem.PartGap,score);
                        if(!fit)continue;
                        // A nominal 90-degree turn can change w*h by one ULP.
                        // It must not outrank an exactly fitting direction before
                        // the meaningful residual-side comparison is considered.
                        const auto rank=std::tuple(-area,fit->Primary,fit->Secondary,hi,pi);
                        if(holeBest)
                        {
                            const double values[]={std::get<0>(rank),std::get<1>(rank),std::get<2>(rank)};
                            const double previous[]={std::get<0>(*holeBest),std::get<1>(*holeBest),std::get<2>(*holeBest)};
                            const double tolerance[]={areaQuantum,score==RectScore::Area?areaQuantum:problem.Tolerance,problem.Tolerance};
                            bool better=std::pair(hi,pi)<std::pair(std::get<3>(*holeBest),std::get<4>(*holeBest));
                            for(std::size_t k=0;k<3;++k)
                            {
                                if(values[k]<previous[k]-tolerance[k]){better=true;break;}
                                if(values[k]>previous[k]+tolerance[k]){better=false;break;}
                            }
                            if(!better)continue;
                        }
                        const Pose pose{slot.Domain.X+fit->Box.X+member.Transform.X,slot.Domain.Y+fit->Box.Y+member.Transform.Y,member.Transform.AngleDegrees};
                        if(insideDomain(member.PartIndex,pose)&&canPlace(bin,member.PartIndex,pose))holeBest=std::tuple(std::get<0>(rank),std::get<1>(rank),std::get<2>(rank),hi,pi,fit->Box);
                    }
                }
                if(holeBest)
                {
                    const auto& slot=bin.HoleSlots[std::get<3>(*holeBest)];const auto& pattern=patterns[std::get<4>(*holeBest)];
                    const auto member=pattern.Members[0];const auto& part=problem.Parts[member.PartIndex];const auto box=std::get<5>(*holeBest);
                    const Pose pose{slot.Domain.X+box.X+member.Transform.X,slot.Domain.Y+box.Y+member.Transform.Y,member.Transform.AngleDegrees};
                    bin.Output.Placements.push_back({part.ID,consumed[member.PartIndex]++,pose,nextGroup++,true});
                    bin.Output.MaterialArea+=Area(part.Geometry);++result.Stats.GapFilledParts;
                    return true;
                }
            }
            std::vector<std::size_t> order(problem.Parts.size());
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(order.begin(), order.end(), [&](auto a, auto b)
                { return Area(problem.Parts[a].Geometry) < Area(problem.Parts[b].Geometry); });
            for (const auto index : order)
            {
                if (stop()) return false;
                const auto& part = problem.Parts[index];
                if (consumed[index] >= part.Quantity || part.Material != problem.Stocks[bin.StockIndex].Material) continue;
                for (double angle : CandidateAngles(part, problem.Tolerance))
                {
                    const auto bounds = Bounds(part.Geometry, {0, 0, angle});
                    std::vector<Point> origins;
                    const double margin = problem.EdgeMargin, gap = problem.PartGap;
                    double localPadding = 0;
                    if (localDomain)
                    {
                        const auto& domain = *localDomain;
                        localPadding = problem.Tolerance + 128 * std::numeric_limits<double>::epsilon() *
                            (1 + std::abs(domain.X) + std::abs(domain.Y) + domain.Width + domain.Height +
                                std::abs(bounds.X) + std::abs(bounds.Y) + bounds.Width + bounds.Height + gap);
                        // insideDomain permits tolerance on both opposing sides.
                        if (bounds.Width > domain.Width + 2 * localPadding || bounds.Height > domain.Height + 2 * localPadding) continue;
                        for (double x : {domain.X, domain.X + domain.Width - bounds.Width})
                            for (double y : {domain.Y, domain.Y + domain.Height - bounds.Height})
                                origins.push_back({x - bounds.X, y - bounds.Y});
                    }
                    else
                    {
                        origins.push_back({margin - bounds.X, margin - bounds.Y});
                        origins.push_back({bin.Output.Width - margin - bounds.X - bounds.Width, margin - bounds.Y});
                        origins.push_back({margin - bounds.X, bin.Output.Height - margin - bounds.Y - bounds.Height});
                    }
                    for (const auto& placed : bin.Output.Placements)
                    {
                        if (stop()) return false;
                        if (origins.size() >= 4096) break;
                        const auto& other = problem.Parts[partByID.at(placed.PartID)].Geometry;
                        const auto otherBox = Bounds(other, placed.Transform);
                        if (localDomain)
                        {
                            const auto& domain = *localDomain;
                            if (otherBox.X + otherBox.Width + gap + localPadding < domain.X ||
                                otherBox.Y + otherBox.Height + gap + localPadding < domain.Y ||
                                otherBox.X - gap - localPadding > domain.X + domain.Width ||
                                otherBox.Y - gap - localPadding > domain.Y + domain.Height) continue;
                        }
                        std::vector<Point> anchors;
                        const auto appendAnchor = [&](Point point)
                        {
                            if (localDomain)
                            {
                                const auto& domain = *localDomain;
                                if (point.X < domain.X - gap - localPadding || point.Y < domain.Y - gap - localPadding ||
                                    point.X > domain.X + domain.Width + gap + localPadding ||
                                    point.Y > domain.Y + domain.Height + gap + localPadding) return;
                            }
                            anchors.push_back(point);
                        };
                        const auto appendRing = [&](const auto& ring)
                        { for (auto point : ring) { if (anchors.size() >= 1024 || (localDomain && stop())) break; appendAnchor(Transform(point, placed.Transform)); } };
                        appendRing(other.Outer);
                        for (const auto& hole : other.Holes) appendRing(hole);
                        for (auto point : {Point{otherBox.X, otherBox.Y}, Point{otherBox.X + otherBox.Width, otherBox.Y},
                            Point{otherBox.X, otherBox.Y + otherBox.Height}, Point{otherBox.X + otherBox.Width, otherBox.Y + otherBox.Height}}) appendAnchor(point);
                        for (auto anchor : anchors)
                        {
                            if (stop()) return false;
                            for (double dx : {-gap, gap}) for (double dy : {-gap, gap})
                                for (double cornerX : {0.0, bounds.Width}) for (double cornerY : {0.0, bounds.Height})
                                    if (origins.size() < 4096) origins.push_back({anchor.X + dx - bounds.X - cornerX, anchor.Y + dy - bounds.Y - cornerY});
                        }
                        if (part.Geometry.Radius > 0 && other.Radius > 0)
                        {
                            const auto c = Transform(other.Center, placed.Transform);
                            const auto localCenter = Transform(part.Geometry.Center, {0, 0, angle});
                            const double distance = other.Radius + part.Geometry.Radius + gap;
                            for (int direction = 0; direction < 12; ++direction)
                                origins.push_back({c.X + distance * std::cos(direction * Pi / 6) - localCenter.X,
                                    c.Y + distance * std::sin(direction * Pi / 6) - localCenter.Y});
                        }
                    }
                    if (stop()) return false;
                    std::sort(origins.begin(), origins.end(), [](Point a, Point b) { return std::tie(a.Y, a.X) < std::tie(b.Y, b.X); });
                    origins.erase(std::unique(origins.begin(), origins.end(), [&](Point a, Point b)
                        { return std::abs(a.X - b.X) < problem.Tolerance && std::abs(a.Y - b.Y) < problem.Tolerance; }), origins.end());
                    for (auto origin : origins)
                    {
                        const Pose pose{origin.X, origin.Y, angle};
                        if (localDomain)
                        {
                            if (stop()) return false;
                            if (!insideDomain(index, pose)) continue;
                        }
                        if (!canPlace(bin, index, pose)) { if (stop()) return false; continue; }
                        bin.Output.Placements.push_back({part.ID, consumed[index]++, pose, nextGroup++, true});
                        bin.Output.MaterialArea += Area(part.Geometry);
                        ++result.Stats.GapFilledParts;
                        bin.Packing.Occupy({origin.X + bounds.X - margin, origin.Y + bounds.Y - margin,
                            bounds.Width + gap, bounds.Height + gap});
                        return true;
                    }
                }
            }
            return false;
        };
        const auto seedCircles = [&](Bin& bin)
        {
            if (!circleLattice || !options.EnableCircleLattice) return false;
            std::size_t bestPart = 0;
            double bestArea = 0;
            std::vector<Point> bestCenters;
            const double w = bin.Output.Width - 2 * problem.EdgeMargin, h = bin.Output.Height - 2 * problem.EdgeMargin;
            for (std::size_t pi = 0; pi < problem.Parts.size() && !stop(); ++pi)
            {
                const auto& part = problem.Parts[pi];
                if (part.Geometry.Radius <= 0 || consumed[pi] >= part.Quantity || part.Material != problem.Stocks[bin.StockIndex].Material) continue;
                const double r = part.Geometry.Radius, step = 2 * r + problem.PartGap;
                for (int transpose = 0; transpose < 2; ++transpose)
                    for (int hex = 0; hex < 2; ++hex) for (int phase = 0; phase < (hex ? 2 : 1); ++phase)
                    {
                        const double width = transpose ? h : w, height = transpose ? w : h;
                        const double rowPitch = hex ? std::sqrt(3.0) * step / 2 : step;
                        std::vector<Point> centers;
                        for (std::size_t row = 0; !stop(); ++row)
                        {
                            const double y = r + row * rowPitch;
                            if (y + r > height + problem.Tolerance) break;
                            const double shift = hex && ((row + phase) % 2) ? step / 2 : 0;
                            for (double x = r + shift; x + r <= width + problem.Tolerance; x += step)
                            {
                                centers.push_back(transpose ? Point{y, x} : Point{x, y});
                                if (centers.size() >= part.Quantity - consumed[pi] || centers.size() >= 10000) break;
                            }
                            if (centers.size() >= part.Quantity - consumed[pi] || centers.size() >= 10000) break;
                        }
                        const double area = centers.size() * Area(part.Geometry);
                        if (area > bestArea) { bestArea = area; bestPart = pi; bestCenters = std::move(centers); }
                    }
            }
            if (bestCenters.empty() || stop()) return false;
            const auto& part = problem.Parts[bestPart];
            const auto angles = CandidateAngles(part, problem.Tolerance);
            const double angle = angles.empty() ? 0 : angles.front();
            const auto c = Transform(part.Geometry.Center, {0, 0, angle});
            Rect envelope{0, 0, 0, 0};
            std::size_t committed = 0;
            for (auto center : bestCenters)
            {
                const Pose pose{center.X + problem.EdgeMargin - c.X, center.Y + problem.EdgeMargin - c.Y, angle};
                if (!canPlace(bin, bestPart, pose)) break;
                bin.Output.Placements.push_back({part.ID, consumed[bestPart]++, pose, nextGroup, false});
                bin.Output.MaterialArea += Area(part.Geometry);
                envelope.Width = std::max(envelope.Width, center.X + part.Geometry.Radius + problem.PartGap);
                envelope.Height = std::max(envelope.Height, center.Y + part.Geometry.Radius + problem.PartGap);
                ++committed;
            }
            if (committed)
            {
                bin.Packing.Occupy(envelope);
                ++result.Stats.CircleLatticeGroups;
                ++nextGroup;
            }
            return committed > 0;
        };

        const auto fill = [&](Bin& bin, const std::optional<Rect>& localDomain = std::nullopt)
        {
            if (batches == PatternBatchMode::Disabled) return gapFillOne(bin, localDomain);
            if (!options.EnableGapFill) return false;
            if (gapQuotaExhausted || gapAttempts >= gapAttemptLimit || gapProbeSeconds >= gapTimeLimit)
            { gapQuotaExhausted = true; ++quotaSkippedProbes; return false; }
            if (!localDomain && bin.FailedGlobalGapCount == bin.Output.Placements.size())
            { ++cachedGapFailures; return false; }
            const auto begin = std::chrono::steady_clock::now();
            if (localDomain) ++localGapProbes; else ++globalGapProbes;
            probeStarted = begin; probeAttemptStart = result.Stats.PlacementAttempts; gapProbeActive = true;
            const bool placed = gapFillOne(bin, localDomain);
            gapProbeActive = false;
            gapProbeSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
            gapAttempts += result.Stats.PlacementAttempts - probeAttemptStart;
            if (gapAttempts >= gapAttemptLimit || gapProbeSeconds >= gapTimeLimit) gapQuotaExhausted = true;
            // Quantities only decrease. With this exact placement sequence, all
            // retained specifications have the same finite candidate positions.
            // A local or interrupted failure must never mark a global failure.
            if (!localDomain && !placed && !gapQuotaExhausted && !stop()) bin.FailedGlobalGapCount = bin.Output.Placements.size();
            return placed;
        };

        const auto commitChoice = [&](Bin& bin, const Choice& choice)
        {
            const auto& pattern = patterns[choice.PatternIndex];
            if (batches == PatternBatchMode::Disabled) { commit(bin, pattern, choice.Box); return true; }
            std::size_t placed = 0;
            const double dx = pattern.Width + problem.PartGap, dy = pattern.Height + problem.PartGap;
            for (std::size_t copy = 0; copy < choice.Copies && !stop(); ++copy)
            {
                const Rect cell{choice.Box.X + static_cast<double>(copy % choice.Columns) * dx,
                    choice.Box.Y + static_cast<double>(copy / choice.Columns) * dy, dx, dy};
                // The first unit was checked by choose; every translated copy
                // is checked again in the actual evolving environment.
                if (copy && !validPattern(bin, pattern, cell.X + problem.EdgeMargin, cell.Y + problem.EdgeMargin)) break;
                commit(bin, pattern, cell); ++placed;
            }
            if (!placed) return false;
            ++committedBatches;
            if (batches == PatternBatchMode::AfterBatch && options.EnableGapFill)
            {
                const auto nx = std::min(choice.Columns, placed);
                const auto ny = placed / choice.Columns + (placed % choice.Columns != 0);
                const Rect domain{choice.Box.X + problem.EdgeMargin, choice.Box.Y + problem.EdgeMargin,
                    static_cast<double>(nx - 1) * dx + pattern.Width, static_cast<double>(ny - 1) * dy + pattern.Height};
                while (!stop() && fill(bin, domain)) {}
            }
            return true;
        };

        while (!stop())
        {
            bool allDone = true;
            for (std::size_t pi = 0; pi < problem.Parts.size(); ++pi) allDone &= consumed[pi] == problem.Parts[pi].Quantity;
            if (allDone) break;
            std::optional<Choice> best;
            for (std::size_t bi = 0; bi < bins.size() && !stop(); ++bi)
            {
                auto choice = choose(bins[bi], bi);
                if (choice && (!best || (batches == PatternBatchMode::AreaFirst
                    ? AreaFirstRank(*choice) < AreaFirstRank(*best) : Rank(*choice) < Rank(*best)))) best = choice;
            }
            if (best) { if (commitChoice(bins[best->BinIndex], *best)) continue; if (stop()) break; }
            bool filled = false;
            for (auto& bin : bins) if (fill(bin)) { filled = true; break; }
            if (filled) continue;
            if (stop()) break;
            // Select an available stock; each candidate is inspected without consuming its inventory.
            std::vector<std::size_t> stockOrder(problem.Stocks.size());
            std::iota(stockOrder.begin(), stockOrder.end(), 0);
            std::stable_sort(stockOrder.begin(), stockOrder.end(), [&](auto a, auto b)
            {
                const auto& sa = problem.Stocks[a]; const auto& sb = problem.Stocks[b];
                return sa.Cost / (sa.Width * sa.Height) < sb.Cost / (sb.Width * sb.Height);
            });
            bool opened = false;
            for (const auto si : stockOrder)
            {
                const auto& stock = problem.Stocks[si];
                if (stocksUsed[si] >= stock.Quantity || stock.Width <= 2 * problem.EdgeMargin || stock.Height <= 2 * problem.EdgeMargin) continue;
                Bin candidate(si, stocksUsed[si], stock, problem);
                if (seedCircles(candidate))
                { ++stocksUsed[si]; bins.push_back(std::move(candidate)); opened = true; break; }
                if (stop()) break;
                if (auto choice = choose(candidate, bins.size()))
                {
                    if (commitChoice(candidate, *choice))
                    { ++stocksUsed[si]; bins.push_back(std::move(candidate)); opened = true; break; }
                }
            }
            if (!opened) break;
        }
        for (auto& bin : bins) if (!bin.Output.Placements.empty()) result.Layouts.push_back(std::move(bin.Output));
        for (std::size_t pi = 0; pi < problem.Parts.size(); ++pi)
            if (consumed[pi] < problem.Parts[pi].Quantity) result.Remaining.push_back({problem.Parts[pi].ID, problem.Parts[pi].Quantity - consumed[pi]});
        result.State = cancelled ? Status::Cancelled : result.Remaining.empty() ? Status::Complete : Status::Partial;
        if (cancelled) result.Diagnostics.push_back("Job cancelled; only completed placements are returned.");
        if (exhausted) result.Diagnostics.push_back("Job time or placement-attempt budget exhausted.");
        if (!exhausted && !cancelled && !result.Remaining.empty()) result.Diagnostics.push_back("Rule candidates or available stock exhausted; this is not a proof of geometric infeasibility.");
        result.Stats.ElapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        result.Stats.PlacementSeconds = std::max(0.0, result.Stats.ElapsedSeconds - result.Stats.PreparationSeconds);
        if (preparationLimited) result.Diagnostics.push_back("Optional motif preparation stopped at its time share; base placements continued.");
        if (batches != PatternBatchMode::Disabled)
        {
            result.Diagnostics.push_back("Rule batches committed=" + std::to_string(committedBatches) +
                (batches == PatternBatchMode::AfterBatch ? "; local gap fill after each pattern batch, then on stall." : "; gap fill on main-candidate stall."));
            result.Diagnostics.push_back("Batch gap probes: global=" + std::to_string(globalGapProbes) + ", local=" +
                std::to_string(localGapProbes) + ", cached-failures=" + std::to_string(cachedGapFailures) +
                ", seconds=" + std::to_string(gapProbeSeconds) + ", attempts=" + std::to_string(gapAttempts) +
                ", quota-exhausted=" + std::to_string(gapQuotaExhausted) + ", quota-skipped=" + std::to_string(quotaSkippedProbes));
            result.Diagnostics.push_back("Batch main-candidate cached failures=" + std::to_string(cachedMainFailures));
        }
        return result;
    }
}
