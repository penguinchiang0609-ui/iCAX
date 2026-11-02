#include "NestingRectangleDirections.h"
#include <bit>
#include <chrono>
#include <numeric>
#include <set>
#include <tuple>
#include <unordered_set>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    struct CavitySpec { std::size_t Index; RectangleDirectionSet Shape; std::vector<std::pair<double,double>> Footprints; };
    struct Level { double X, Width, Y; };
    struct CavityChoice { std::size_t Spec, Direction; double W, H, Area; };
}

// Fill the lowest uncovered corner, branching on the part rather than committing
// a global order. Flat-front placement never seals a hole below a rectangle.
// Optional discarded strips consume actual area slack. This is bounded search,
// not a general infeasibility proof, and does not impose guillotine cuts.
Result RunRectangleCavity(const Problem& p, const Options& options, CavityOrder order, bool columns)
{
    const auto start = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(); };
    Result best; best.State = Status::Partial;
    for (const auto& part : p.Parts) if (part.Quantity) best.Remaining.push_back({part.ID, part.Quantity});
    bool cancelled = false, exhausted = false;
    std::size_t attempts = 0, nodes = 0, bestCount = 0, count = 0;
    double preparation = 0;
    const auto stop = [&]
    {
        if (cancelled || exhausted) return true;
        if (options.Cancel && options.Cancel()) { cancelled = true; return true; }
        if (attempts >= options.MaxPlacementAttempts || elapsed() >= options.TimeLimitSeconds)
        { exhausted = true; return true; }
        return false;
    };
    const auto finish = [&]
    {
        if (cancelled) best.State = Status::Cancelled;
        best.Stats.PlacementAttempts = attempts; best.Stats.ElapsedSeconds = elapsed();
        best.Stats.PreparationSeconds = preparation;
        best.Stats.PlacementSeconds = std::max(0.0, elapsed()-preparation);
        best.Diagnostics.push_back("Rectangle cavity search visited " + std::to_string(nodes) + " states.");
        if (exhausted) best.Diagnostics.push_back("Cavity time or attempt budget exhausted; remaining parts are not proven infeasible.");
        return best;
    };
    std::vector<CavitySpec> specs;
    std::string material;
    double needed = 0;
    for (std::size_t i = 0; i < p.Parts.size() && !stop(); ++i)
    {
        const auto& part = p.Parts[i]; if (!part.Quantity) continue;
        if (specs.empty()) material = part.Material;
        if (part.Material != material || part.Quantity > 512-count)
        { best.Diagnostics.push_back("Rectangle cavity scope: one material, <=512 instances on one stock."); return finish(); }
        auto shape = FindRectangleDirections(part, p.Tolerance, stop);
        if (shape.Interrupted || stop()) return finish();
        if (!shape.Supported)
        { best.Diagnostics.push_back("Rectangle cavity requires rectangles with legal axis-aligned directions."); return finish(); }
        // Transpose the planning axes, never the part's allowed physical pose.
        if(columns)for(auto& direction:shape.Directions)std::swap(direction.Width,direction.Height);
        // All rectangle quarter-turns have identical gap-expanded area.
        const auto& d = shape.Directions.front();
        needed += (d.Width+p.PartGap)*(d.Height+p.PartGap)*part.Quantity;
        count += part.Quantity;
        std::vector<std::pair<double,double>> footprints;
        for(const auto& direction:shape.Directions)footprints.emplace_back(direction.Width,direction.Height);
        std::sort(footprints.begin(),footprints.end());
        footprints.erase(std::unique(footprints.begin(),footprints.end()),footprints.end());
        specs.push_back({i, std::move(shape), std::move(footprints)});
    }
    preparation = elapsed();
    if (stop()) return finish();
    if (!count) { best.State = Status::Complete; best.Remaining.clear(); return finish(); }
    for (const auto& stock : p.Stocks)
    {
        if (stop()) break;
        const double maxWidth = (columns?stock.Height:stock.Width)-2*p.EdgeMargin+p.PartGap;
        const double maxHeight = (columns?stock.Width:stock.Height)-2*p.EdgeMargin+p.PartGap;
        if (!stock.Quantity || stock.Material != material || maxWidth <= p.Tolerance || maxHeight <= p.Tolerance)continue;
        if(needed>maxWidth*maxHeight+p.Tolerance*(maxWidth+maxHeight)*count)continue;
        std::vector<std::pair<double,double>> envelopes;
        // Near-full integer orders may tile a slightly smaller rectangle. Infer
        // its dimensions from area only; no construction coordinates are used.
        if(needed==std::floor(needed) && needed>0 && needed<9e15 && maxWidth<65536 && maxHeight<65536
            && needed/maxHeight<=maxWidth)
        {
            const auto area=static_cast<std::uint64_t>(needed);
            const auto low=static_cast<std::uint64_t>(std::ceil(needed/maxHeight));
            for(auto w=std::max<std::uint64_t>(1,low);w<=static_cast<std::uint64_t>(maxWidth)&&!stop();++w)
                if(area%w==0 && static_cast<double>(area/w)<=maxHeight)
                    envelopes.emplace_back(static_cast<double>(w),static_cast<double>(area/w));
            std::sort(envelopes.begin(),envelopes.end(),[](const auto& a,const auto& b)
            {return std::tie(a.second,a.first)<std::tie(b.second,b.first);});
        }
        if(std::find(envelopes.begin(),envelopes.end(),std::pair(maxWidth,maxHeight))==envelopes.end())
            envelopes.emplace_back(maxWidth,maxHeight);
        for(const auto& [width,height]:envelopes)
        {
        const double areaTolerance = p.Tolerance*(width+height)*count;
        if (!stock.Quantity || stock.Material != material || width <= p.Tolerance || height <= p.Tolerance
            || needed > width*height+areaTolerance) continue;
        std::vector<std::size_t> remaining;
        for (const auto& s : specs) remaining.push_back(p.Parts[s.Index].Quantity);
        Layout layout{stock.ID, 0, stock.Width, stock.Height, {}, 0};
        std::unordered_set<std::string> visited;
        const auto save = [&](bool complete)
        {
            best.Layouts = {layout}; best.Remaining.clear();
            if (layout.Placements.empty()) best.Layouts.clear();
            for (std::size_t i=0; i<specs.size(); ++i)
                if (remaining[i]) best.Remaining.push_back({p.Parts[specs[i].Index].ID, remaining[i]});
            best.State = complete ? Status::Complete : Status::Partial;
            bestCount = layout.Placements.size();
        };
        const auto keyOf = [&](const std::vector<Level>& front)
        {
            std::string key;
            const auto add = [&](auto value) { key.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
            for (auto n : remaining) add(n);
            for (const auto& l : front) { add(l.X); add(l.Width); add(l.Y); }
            return key;
        };
        const auto raise = [&](const std::vector<Level>& front, std::size_t index, double w, double y)
        {
            std::vector<Level> result;
            const auto append = [&](Level l)
            {
                if (l.Width <= 0) return;
                if (!result.empty() && result.back().Y == l.Y) result.back().Width += l.Width;
                else result.push_back(l);
            };
            for (std::size_t i=0; i<front.size(); ++i)
                if (i != index) append(front[i]);
                else
                {
                    const auto& l = front[i]; append({l.X, w, y});
                    if (l.Width > w) append({l.X+w, l.Width-w, l.Y});
                }
            return result;
        };
        const auto spansPossible = [&](const std::vector<double>& spans,double crossAvailable,bool vertical)
        {
            const auto maximum=*std::max_element(spans.begin(),spans.end());
            if(maximum>65536)return true;
            std::vector<std::size_t> targets;
            for(auto length:spans)
            {
                if(length!=std::floor(length)||length<0)return true;
                targets.push_back(static_cast<std::size_t>(length));
            }
            const auto capacity=static_cast<std::size_t>(maximum);
            std::vector<std::uint64_t> bits(capacity/64+1,0);bits[0]=1;
            const auto reached=[&]
            {return std::all_of(targets.begin(),targets.end(),[&](auto n){return (bits[n/64]>>(n%64))&1;});};
            for(std::size_t i=0;i<specs.size();++i)if(remaining[i])
            {
                if(stop())return true;
                for(const auto& footprint:specs[i].Footprints)
                {
                    const double raw=(vertical?footprint.second:footprint.first)+p.PartGap;
                    const double cross=(vertical?footprint.first:footprint.second)+p.PartGap;
                    if(raw>maximum||cross>crossAvailable)continue;
                    // Only exact integer lengths are projected. Nonintegral
                    // inputs keep all branches; there is no raster rounding.
                    if(raw!=std::floor(raw)||raw<1)return true;
                    const auto weight=static_cast<std::size_t>(raw);
                    auto available=std::min(remaining[i],capacity/weight);
                    for(std::size_t batch=1;available;batch*=2)
                    {
                        const auto take=std::min(batch,available);available-=take;
                        const auto shift=weight*take,words=shift/64,offset=shift%64;
                        for(std::size_t j=bits.size();j-->words;)
                        {
                            auto value=bits[j-words]<<offset;
                            if(offset&&j>words)value|=bits[j-words-1]>>(64-offset);
                            bits[j]|=value;
                        }
                        if(reached())return true;
                    }
                }
            }
            return false;
        };
        struct Frame
        {
            std::vector<Level> Front;
            double Slack;
            std::optional<std::size_t> AddedSpec;
            std::vector<CavityChoice> Choices;
            std::size_t Next=0,CellIndex=0;
            bool Initialized=false,WasteTried=false;
        };
        // Large repeated quantities must not consume the process's call stack.
        // Each DFS frame lives on the heap and explicitly owns its alternatives.
        std::vector<Frame> frames;
        frames.push_back({{{0,width,0}},width*height-needed,{},{}});
        const auto pop=[&]
        {
            const auto added=frames.back().AddedSpec;
            if(added)
            {
                ++remaining[*added];layout.MaterialArea-=specs[*added].Shape.Area;
                layout.Placements.pop_back();
            }
            frames.pop_back();
        };
        while(!frames.empty()&&!stop())
        {
            auto& frame=frames.back();
            auto& front=frame.Front;
            if(!frame.Initialized)
            {
                ++nodes;
                if (layout.Placements.size() == count)
                {
                    Result candidate;candidate.State=Status::Complete;candidate.Layouts={layout};
                    if(Validate(p,candidate).Valid){save(true);break;}
                    pop();continue;
                }
                if (layout.Placements.size() > bestCount) save(false);
                if (visited.size() < 16000 && !visited.insert(keyOf(front)).second){pop();continue;}
                const auto it=std::min_element(front.begin(),front.end(),[](const auto& a,const auto& b)
                {return std::tie(a.Y,a.X)<std::tie(b.Y,b.X);});
                frame.CellIndex=static_cast<std::size_t>(it-front.begin());
                const auto cell=*it;
                if(cell.Y>=height-p.Tolerance){pop();continue;}
                if(frame.Slack==0)
                {
                    if(!spansPossible({cell.Width},height-cell.Y,false)){pop();continue;}
                    std::vector<double> verticalSpans;
                    for(const auto& level:front)verticalSpans.push_back(height-level.Y);
                    if(!spansPossible(verticalSpans,width,true)){pop();continue;}
                }
            std::set<std::tuple<double,double,std::vector<std::pair<double,double>>>> sizes;
            for (std::size_t i=0; i<specs.size() && !stop(); ++i) if (remaining[i])
                for (std::size_t di=0; di<specs[i].Shape.Directions.size() && !stop(); ++di)
                {
                    ++attempts;
                    const auto& d = specs[i].Shape.Directions[di];
                    const double w=d.Width+p.PartGap, h=d.Height+p.PartGap;
                    // Keep the front an exact partition. A tolerance-sized
                    // overhang would accumulate or overlap an adjacent segment.
                    if (w > cell.Width || cell.Y+h > height) continue;
                    // Identical dimensions are interchangeable here, but original
                    // identities and their quantities remain in the saved placements.
                    if (!sizes.emplace(w,h,specs[i].Footprints).second) continue;
                    frame.Choices.push_back({i,di,w,h,w*h});
                }
            std::stable_sort(frame.Choices.begin(), frame.Choices.end(), [&](const auto& a, const auto& b)
            {
                const auto rank = [&](const auto& c)
                { return std::tuple(std::abs(cell.Width-c.W)>p.Tolerance,
                    order==CavityOrder::Width?-c.W:order==CavityOrder::Height?-c.H:-c.Area, -c.W, -c.H); };
                return rank(a)<rank(b);
            });
                frame.Initialized=true;
            }
            if(stop())break;
            const auto index=frame.CellIndex;
            const auto cell=front[index];
            if(frame.Next<frame.Choices.size())
            {
                const auto c=frame.Choices[frame.Next++];
                const auto& s=specs[c.Spec]; const auto& part=p.Parts[s.Index];
                const auto& d=s.Shape.Directions[c.Direction];
                const Pose pose{(columns?cell.Y:cell.X)+p.EdgeMargin+d.Offset.X,
                    (columns?cell.X:cell.Y)+p.EdgeMargin+d.Offset.Y,d.Offset.AngleDegrees};
                if (!InsideSheet(part.Geometry,pose,stock.Width,stock.Height,p.EdgeMargin,p.Tolerance)) continue;
                layout.Placements.push_back({part.ID,part.Quantity-remaining[c.Spec],pose,layout.Placements.size()+1,false});
                layout.MaterialArea += s.Shape.Area; --remaining[c.Spec];
                frames.push_back({raise(front,index,c.W,cell.Y+c.H),frame.Slack,c.Spec,{}});
                continue;
            }
            // Leave a strip only if the remaining material still fits by area.
            double next=height;
            if (index) next=std::min(next,front[index-1].Y);
            if (index+1<front.size()) next=std::min(next,front[index+1].Y);
            const double waste=(next-cell.Y)*cell.Width;
            if (!frame.WasteTried && waste>areaTolerance && waste<=frame.Slack+areaTolerance)
            {
                frame.WasteTried=true;++attempts;
                frames.push_back({raise(front,index,cell.Width,next),frame.Slack-waste,{},{}});
                continue;
            }
            pop();
        }
        if(best.State==Status::Complete)break;
        }
        if(best.State==Status::Complete)break;
    }
    return finish();
}
}
