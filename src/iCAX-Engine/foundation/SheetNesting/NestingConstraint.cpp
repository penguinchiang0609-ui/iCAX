#include "SheetNestingConstraint.h"
#include "NestingConstraintKernel.h"
#include "SheetNestingNfp.h"
#include "NestingInternal.h"
#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/model.h"
#include "ortools/sat/sat_parameters.pb.h"
#include "ortools/util/time_limit.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <tuple>

#if defined(_MSC_VER) && defined(_DEBUG)
#error The pinned OR-Tools distribution requires the Release CRT; do not build this optional target with Debug STL/protobuf ABI.
#endif

namespace iCAX::SheetNesting
{
namespace
{
    using namespace ConstraintKernel;
    class ConstraintCompletion final:public IPostProcessor
    {
        ConstraintCompletionSettings Settings;
    public:
        explicit ConstraintCompletion(ConstraintCompletionSettings settings):Settings(settings){}
        Result Run(const Problem& p,const Result& original,const Options& o)override
        {
            const auto started=Clock::now();Result selected=original;Statistics work;
            std::size_t preparedPieces=0,forbiddenCells=0,retainedCells=0,rectangleAtoms=0;
            work.CombinationGroups=original.Stats.CombinationGroups;work.CircleLatticeGroups=original.Stats.CircleLatticeGroups;
            work.GapFilledParts=original.Stats.GapFilledParts;
            std::atomic<bool> cancelled{false};bool solverStarted=false,anchorSymmetryUsed=false;std::int64_t conflicts=0;
            const auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-started).count();};
            const auto pollCancel=[&]{if(!cancelled.load()&&o.Cancel&&o.Cancel())cancelled.store(true);return cancelled.load();};
            const auto stop=[&]{return pollCancel()||!std::isfinite(o.TimeLimitSeconds)||elapsed()>=o.TimeLimitSeconds;};
            const auto finish=[&](const std::string& reason)
            {
                if(cancelled.load())selected.State=Status::Cancelled;
                work.ElapsedSeconds=work.PostProcessingSeconds=elapsed();selected.Stats=work;
                std::ostringstream note;note<<"repair.constraint: "<<reason<<"; coordinate-scale="<<Settings.CoordinateScale
                    <<"; workers="<<Settings.Workers<<"; solver-conflicts="<<conflicts<<"; solver-started="<<solverStarted
                    <<"; anchor-half-turn="<<anchorSymmetryUsed
                    <<"; convex-pieces="<<preparedPieces<<"; forbidden-cells="<<retainedCells
                    <<"; built-forbidden-cells="<<forbiddenCells<<"; rectangle-atoms="<<rectangleAtoms
                    <<"; lattice-failure-is-not-continuous-infeasibility";
                selected.Diagnostics.push_back(note.str());return selected;
            };
            if(stop())return finish("budget-before-model");
            if(!Detail::CheckProblem(p).empty())return finish("invalid original problem/incumbent");
            const auto originalChecked=Validate(p,original);
            if(!originalChecked.Valid)return finish("invalid original problem/incumbent");
            if(stop())return finish("budget-after-input-validation");
            if(originalChecked.Complete)return finish("already complete");
            for(const auto& layout:original.Layouts)for(const auto& placed:layout.Placements)
                if(placed.Transform.Mirrored)return finish("unsupported mirrored incumbent in the exact-quarter model; original retained");
            if(p.Stocks.size()!=1||!p.Stocks[0].Quantity||p.PartGap!=0||original.Layouts.size()>1)
                return finish("unsupported one-stock zero-gap scope");
            std::size_t total=0;
            for(const auto& part:p.Parts)
            {if(part.Quantity>Settings.MaxInstances-total)return finish("instance scope limit");total+=part.Quantity;}
            if(total>o.MaxPlacementAttempts)return finish("insufficient original-candidate verification budget");
            const auto& stock=p.Stocks[0];const auto scale=Settings.CoordinateScale;
            const long double scaledW=static_cast<long double>(stock.Width)*scale,scaledH=static_cast<long double>(stock.Height)*scale;
            if(scaledW>10000000.L||scaledH>10000000.L)return finish("scaled board scope limit");
            const Integer low=static_cast<Integer>(std::ceil(static_cast<long double>(p.EdgeMargin)*scale-1e-10L));
            const Integer highX=static_cast<Integer>(std::floor((static_cast<long double>(stock.Width)-p.EdgeMargin)*scale+1e-10L));
            const Integer highY=static_cast<Integer>(std::floor((static_cast<long double>(stock.Height)-p.EdgeMargin)*scale+1e-10L));
            if(highX<low||highY<low)return finish("empty coordinate lattice");
            std::vector<Type> types;std::map<std::string,std::size_t> typeByID;NfpCache geometryCache;
            bool halfTurnSymmetry=true;
            for(std::size_t pi=0;pi<p.Parts.size();++pi)
            {
                if(stop())return finish("budget during source preparation");
                const auto& part=p.Parts[pi];if(!part.Quantity)continue;
                if(part.FreeRotation||part.Material!=stock.Material||part.Geometry.Radius>0||!part.Geometry.Holes.empty()||part.Geometry.Outer.size()>128)
                    return finish("unsupported polygon/material/angle scope");
                Type type;std::string preparationError;
                if(!PrepareType(part,pi,scale,Settings.UseInscribedRectangles,geometryCache,stop,
                    [&]{return o.TimeLimitSeconds-elapsed();},type,preparationError,preparedPieces,rectangleAtoms))return finish(preparationError);
                for(int q=0;q<4;++q)if(type.QuarterToDirection[q]>=0&&type.QuarterToDirection[(q+2)%4]<0)halfTurnSymmetry=false;
                typeByID.emplace(part.ID,types.size());types.push_back(std::move(type));
            }
            if(stop())return finish("budget after source preparation");
            using CellKey=std::tuple<std::size_t,std::size_t,std::size_t,std::size_t>;
            std::map<CellKey,std::vector<Cell>> cache;
            const auto region=[&](std::size_t a,std::size_t ai,std::size_t b,std::size_t bi)->const std::vector<Cell>&
            {
                CellKey key{a,ai,b,bi};const auto found=cache.find(key);if(found!=cache.end())return found->second;
                auto cells=PairCells(types[a].Directions[ai],types[b].Directions[bi],stop,forbiddenCells);
                retainedCells+=cells.size();
                return cache.emplace(key,std::move(cells)).first->second;
            };
            std::vector<Item> items;std::vector<Hint> hints;
            for(std::size_t ti=0;ti<types.size();++ti)
                for(std::size_t instance=0;instance<p.Parts[types[ti].Original].Quantity;++instance)
                {Item item;item.TypeIndex=ti;item.Instance=instance;items.push_back(std::move(item));hints.push_back({});}
            const auto clampHint=[&](Hint h,std::size_t ti)
            {
                const auto& d=types[ti].Directions[h.Direction];
                h.X=std::clamp(h.X,low,std::max(low,highX-d.Width));h.Y=std::clamp(h.Y,low,std::max(low,highY-d.Height));return h;
            };
            for(const auto& layout:original.Layouts)for(const auto& placed:layout.Placements)
            {
                const auto ti=typeByID.at(placed.PartID);const int q=Quarter(placed.Transform.AngleDegrees);
                if(q<0||types[ti].QuarterToDirection[q]<0)return finish("incumbent angle outside exact quarter scope");
                const auto b=Detail::Bounds(p.Parts[types[ti].Original].Geometry,placed.Transform);
                const auto it=std::find_if(items.begin(),items.end(),[&](const Item& item){return item.TypeIndex==ti&&item.Instance==placed.Instance;});
                hints[static_cast<std::size_t>(it-items.begin())]=clampHint({types[ti].QuarterToDirection[q],static_cast<Integer>(std::llround(b.X*scale)),static_cast<Integer>(std::llround(b.Y*scale))},ti);
            }
            // Missing-part hint proposals are computed from this incumbent's
            // real convex-piece contact events. They never constrain solver positions.
            for(std::size_t missing=0;missing<items.size();++missing)
            {
                if(hints[missing].Direction>=0)continue;if(stop())return finish("budget during hint preparation");
                const auto ti=items[missing].TypeIndex;Hint best;
                auto bestScore=std::tuple(std::numeric_limits<std::size_t>::max(),std::numeric_limits<std::size_t>::max(),
                    std::numeric_limits<double>::infinity(),Integer(0),Integer(0));
                for(std::size_t ai=0;ai<types[ti].Directions.size();++ai)
                {
                    if(stop())return finish("budget during contact hints");
                    const auto& d=types[ti].Directions[ai];const Integer right=highX-d.Width,top=highY-d.Height;
                    if(right<low||top<low)continue;
                    const auto reserve=items.size();const auto available=o.MaxPlacementAttempts-work.PlacementAttempts-reserve;
                    const auto quota=available/std::max<std::size_t>(1,types[ti].Directions.size()-ai);
                    std::vector<IPoint> candidates;std::set<IPoint> seen;
                    const auto emit=[&](Point q)
                    {
                        if(q.X<low-1e-6||q.Y<low-1e-6||q.X>right+1e-6||q.Y>top+1e-6||candidates.size()>=Settings.MaxHintCandidates)return;
                        const IPoint point{std::clamp(static_cast<Integer>(std::llround(q.X)),low,right),std::clamp(static_cast<Integer>(std::llround(q.Y)),low,top)};
                        if(seen.insert(point).second)candidates.push_back(point);
                    };
                    emit({double(low),double(low)});emit({double(right),double(low)});emit({double(low),double(top)});emit({double(right),double(top)});
                    std::vector<Segment> edges{{{double(low),double(low)},{double(right),double(low)},0},
                        {{double(right),double(low)},{double(right),double(top)},0},{{double(right),double(top)},{double(low),double(top)},0},
                        {{double(low),double(top)},{double(low),double(low)},0}};
                    if(quota&&Settings.MaxHintCandidates)
                    {
                        for(std::size_t j=0;j<items.size();++j)if(hints[j].Direction>=0)
                        {
                            if(stop())return finish("budget during contact regions");
                            for(const auto& c:region(items[j].TypeIndex,hints[j].Direction,ti,ai))
                            for(std::size_t v=0;v<c.Polygon.size();++v)
                            {const auto a=c.Polygon[v],b=c.Polygon[(v+1)%c.Polygon.size()];
                                Point x{double(a.X+hints[j].X),double(a.Y+hints[j].Y)},y{double(b.X+hints[j].X),double(b.Y+hints[j].Y)};
                                emit(x);edges.push_back({x,y,j+1});}
                        }
                        for(std::size_t e=0;e<edges.size();++e)
                        {
                            if((e&31)==0&&stop())return finish("budget during contact intersections");
                            const auto a=edges[e];const double ax=a.B.X-a.A.X,ay=a.B.Y-a.A.Y;
                            for(std::size_t f=e+1;f<edges.size();++f)
                            {
                                const auto b=edges[f];if(a.Owner==b.Owner)continue;const double bx=b.B.X-b.A.X,by=b.B.Y-b.A.Y,den=ax*by-ay*bx;
                                if(std::abs(den)<1e-12)continue;const double qx=b.A.X-a.A.X,qy=b.A.Y-a.A.Y;
                                const double t=(qx*by-qy*bx)/den,u=(qx*ay-qy*ax)/den;
                                if(t>=0&&t<=1&&u>=0&&u<=1)emit({a.A.X+t*ax,a.A.Y+t*ay});
                            }
                        }
                        std::size_t tested=0;
                        for(auto point:candidates)
                        {
                            if(tested>=quota||stop())break;++tested;++work.PlacementAttempts;
                            std::size_t blockers=0,same=0;double overlap=0;
                            for(std::size_t j=0;j<items.size();++j)if(hints[j].Direction>=0)
                            {
                                const auto& cells=region(items[j].TypeIndex,hints[j].Direction,ti,ai);const Integer dx=point.X-hints[j].X,dy=point.Y-hints[j].Y;
                                const bool conflict=std::any_of(cells.begin(),cells.end(),[&](const Cell& c)
                                    {return std::all_of(c.Axes.begin(),c.Axes.end(),[&](Axis a){return a.A*dx+a.B*dy>a.C;});});
                                if(!conflict)continue;++blockers;same+=items[j].TypeIndex==ti?1:0;
                                // Each decomposition has disjoint interiors,
                                // so the sum of convex-pair intersections is
                                // the actual original-material overlap area.
                                for(const auto& movingPiece:d.Pieces)
                                {
                                    std::vector<Point> moving;for(auto q:movingPiece)moving.push_back({double(q.X+point.X),double(q.Y+point.Y)});
                                    for(const auto& fixedPiece:types[items[j].TypeIndex].Directions[hints[j].Direction].Pieces)
                                    {std::vector<Point> fixed;for(auto q:fixedPiece)fixed.push_back({double(q.X+hints[j].X),double(q.Y+hints[j].Y)});
                                        overlap+=IntersectionArea(moving,fixed);}
                                }
                            }
                            const auto score=std::tuple(blockers,same,overlap,point.Y,point.X);
                            if(score<bestScore){bestScore=score;best={static_cast<int>(ai),point.X,point.Y};}
                        }
                    }
                    if(best.Direction<0)best={static_cast<int>(ai),low,low};
                }
                hints[missing]=best;
            }
            if(stop())return finish("budget after hint preparation");
            for(std::size_t i=0;i<items.size();++i)if(hints[i].Direction<0)return finish("no individually fitting orientation on lattice");
            std::size_t anchor=0;Integer largest=-1;
            for(std::size_t i=0;i<items.size();++i)
            {const auto& d=types[items[i].TypeIndex].Directions[0];if(d.Width*d.Height>largest){largest=d.Width*d.Height;anchor=i;}}
            if(halfTurnSymmetry)
            {
                const auto& a=types[items[anchor].TypeIndex].Directions[hints[anchor].Direction];
                if(2*hints[anchor].Y+a.Height>low+highY)
                    for(std::size_t i=0;i<items.size();++i)
                    {auto& h=hints[i];const auto& t=types[items[i].TypeIndex];const auto& d=t.Directions[h.Direction];
                        h.X=low+highX-h.X-d.Width;h.Y=low+highY-h.Y-d.Height;
                        h.Direction=t.QuarterToDirection[(Quarter(d.Angle)+2)%4];}
            }
            for(std::size_t ti=0;ti<types.size();++ti)
            {
                std::vector<std::size_t> indices;std::vector<Hint> sorted;
                for(std::size_t i=0;i<items.size();++i)if(items[i].TypeIndex==ti){indices.push_back(i);sorted.push_back(hints[i]);}
                std::sort(sorted.begin(),sorted.end(),[](Hint a,Hint b){return std::tie(a.X,a.Y,a.Direction)<std::tie(b.X,b.Y,b.Direction);});
                for(std::size_t j=0;j<indices.size();++j)hints[indices[j]]=sorted[j];
            }
            Sat::CpModelBuilder cp;std::map<std::size_t,std::size_t> lastCopy;
            for(std::size_t i=0;i<items.size();++i)
            {
                if(stop())return finish("budget during variables");auto& item=items[i];
                item.X=cp.NewIntVar(operations_research::Domain(low,highX));item.Y=cp.NewIntVar(operations_research::Domain(low,highY));
                for(const auto& d:types[item.TypeIndex].Directions)
                {auto present=cp.NewBoolVar();item.Present.push_back(present);cp.AddLessOrEqual(item.X,highX-d.Width).OnlyEnforceIf(present);cp.AddLessOrEqual(item.Y,highY-d.Height).OnlyEnforceIf(present);}
                cp.AddExactlyOne(item.Present);
                if(const auto it=lastCopy.find(item.TypeIndex);it!=lastCopy.end())cp.AddLessOrEqual(items[it->second].X,item.X);
                lastCopy[item.TypeIndex]=i;
                cp.AddHint(item.X,hints[i].X);cp.AddHint(item.Y,hints[i].Y);
                for(std::size_t a=0;a<item.Present.size();++a)cp.AddHint(item.Present[a],static_cast<int>(a)==hints[i].Direction);
            }
            std::vector<CellHint> cellHints;
            for(std::size_t i=0;i<items.size();++i)for(std::size_t j=i+1;j<items.size();++j)
                for(std::size_t ai=0;ai<items[i].Present.size();++ai)for(std::size_t aj=0;aj<items[j].Present.size();++aj)
                {
                    if(stop())return finish("budget during separation model");
                    // Every convex-pair forbidden cell must be avoided. One
                    // OR over all cells would admit overlap with other pieces.
                    for(const auto& c:region(items[i].TypeIndex,ai,items[j].TypeIndex,aj))
                    {
                    std::vector<Sat::BoolVar> clause{items[i].Present[ai].Not(),items[j].Present[aj].Not()};
                    CellHint hint{i,j,ai,aj,&c,{}};
                    for(auto axis:c.Axes)
                    {
                        const long double maximum=std::abs(static_cast<long double>(axis.A))*(highX-low)+std::abs(static_cast<long double>(axis.B))*(highY-low)+std::abs(static_cast<long double>(axis.C));
                        if(maximum>static_cast<long double>(std::numeric_limits<Integer>::max())/16)return finish("constraint coefficient scope limit");
                        auto outside=cp.NewBoolVar();Sat::LinearExpr lhs;
                        lhs+=Sat::LinearExpr::Term(items[j].X,axis.A);lhs+=Sat::LinearExpr::Term(items[i].X,-axis.A);
                        lhs+=Sat::LinearExpr::Term(items[j].Y,axis.B);lhs+=Sat::LinearExpr::Term(items[i].Y,-axis.B);
                        cp.AddLessOrEqual(lhs,axis.C).OnlyEnforceIf(outside);
                        cp.AddImplication(outside,items[i].Present[ai]);cp.AddImplication(outside,items[j].Present[aj]);clause.push_back(outside);
                        if(Settings.CompleteSeparationHints)hint.Alternatives.push_back(outside);
                    }
                    cp.AddBoolOr(clause);if(Settings.CompleteSeparationHints)cellHints.push_back(std::move(hint));
                    }
                }
            for(const auto& h:cellHints)
            {
                if(stop())return finish("budget during separation hints");
                int chosen=-1;double slack=-std::numeric_limits<double>::infinity();
                if(hints[h.I].Direction==static_cast<int>(h.AI)&&hints[h.J].Direction==static_cast<int>(h.AJ))
                {
                    const Integer dx=hints[h.J].X-hints[h.I].X,dy=hints[h.J].Y-hints[h.I].Y;
                    for(std::size_t e=0;e<h.Geometry->Axes.size();++e)
                    {const auto a=h.Geometry->Axes[e];const double value=double(a.C-a.A*dx-a.B*dy)/std::hypot(double(a.A),double(a.B));if(value>slack){slack=value;chosen=static_cast<int>(e);}}
                }
                for(std::size_t e=0;e<h.Alternatives.size();++e)cp.AddHint(h.Alternatives[e],static_cast<int>(e)==chosen);
            }
            // Copy-X ordering can swap which physical copy is the anchor after
            // a half turn. Only a unique-type anchor justifies the Y pruning.
            anchorSymmetryUsed=halfTurnSymmetry&&p.Parts[types[items[anchor].TypeIndex].Original].Quantity==1;
            if(anchorSymmetryUsed)
            {Sat::LinearExpr h;for(std::size_t a=0;a<items[anchor].Present.size();++a)h+=Sat::LinearExpr::Term(items[anchor].Present[a],types[items[anchor].TypeIndex].Directions[a].Height);
                cp.AddLessOrEqual(Sat::LinearExpr::Term(items[anchor].Y,2)+h,low+highY);}
            if(Settings.UseInscribedRectangles)
            {
                auto overlap=cp.AddNoOverlap2D();auto alongX=cp.AddCumulative(highY-low);auto alongY=cp.AddCumulative(highX-low);
                for(const auto& item:items)for(std::size_t a=0;a<item.Present.size();++a)
                    for(const auto rect:types[item.TypeIndex].Directions[a].Rectangles)
                    {if(stop())return finish("budget during global packing model");
                        const auto x=cp.NewOptionalFixedSizeIntervalVar(Sat::LinearExpr(item.X)+rect.X,rect.Width,item.Present[a]);
                        const auto y=cp.NewOptionalFixedSizeIntervalVar(Sat::LinearExpr(item.Y)+rect.Y,rect.Height,item.Present[a]);
                        overlap.AddRectangle(x,y);alongX.AddDemand(x,rect.Height);alongY.AddDemand(y,rect.Width);}
            }
            if(stop())return finish("budget before solve");
            const auto proto=cp.Build();if(stop())return finish("budget after model serialization");
            // The native solver also needs a bounded shutdown/export reserve;
            // its worker join time is outside max_time_in_seconds itself.
            const double left=o.TimeLimitSeconds-elapsed(),reserve=std::min(.25,left*.1);
            if(left<=reserve)return finish("no remaining solver budget");
            solverStarted=true;
            const auto response=SolveBounded(proto,Settings,o,left-reserve,pollCancel,[&]{return elapsed()>=o.TimeLimitSeconds;});
            conflicts=response.num_conflicts();
            if(pollCancel())return finish("user cancellation");
            if(response.status()!=Sat::CpSolverStatus::FEASIBLE&&response.status()!=Sat::CpSolverStatus::OPTIMAL)
                return finish("no complete solution on recorded lattice: "+Sat::CpSolverStatus_Name(response.status()));
            if(stop())return finish("budget before original-candidate verification");
            Result candidate;candidate.State=Status::Complete;
            Layout layout{stock.ID,original.Layouts.empty()?0:original.Layouts[0].StockInstance,stock.Width,stock.Height,{},0};
            std::vector<std::size_t> placedTypes;
            for(std::size_t i=0;i<items.size();++i)
            {
                if(stop()||work.PlacementAttempts>=o.MaxPlacementAttempts)return finish("original-candidate verification budget");
                ++work.PlacementAttempts;const auto& item=items[i];std::size_t a=0;
                while(a<item.Present.size()&&!Sat::SolutionBooleanValue(response,item.Present[a]))++a;
                if(a==item.Present.size())return finish("invalid exported direction");
                const auto& t=types[item.TypeIndex];const auto& d=t.Directions[a];const auto& part=p.Parts[t.Original];
                const Pose pose{double(Sat::SolutionIntegerValue(response,item.X))/scale-d.OriginalBounds.X,
                    double(Sat::SolutionIntegerValue(response,item.Y))/scale-d.OriginalBounds.Y,d.Angle};
                if(!Detail::AngleAllowed(part,pose.AngleDegrees,p.Tolerance)||!Detail::InsideSheet(part.Geometry,pose,stock.Width,stock.Height,p.EdgeMargin,p.Tolerance))
                    return finish("original export outside allowed stock/angle");
                for(std::size_t j=0;j<layout.Placements.size();++j)
                    if(Detail::ShapesConflict(part.Geometry,pose,p.Parts[types[placedTypes[j]].Original].Geometry,layout.Placements[j].Transform,p.PartGap,p.Tolerance))
                        return finish("original material rejects exported solver candidate");
                layout.Placements.push_back({part.ID,item.Instance,pose,0,false});placedTypes.push_back(item.TypeIndex);layout.MaterialArea+=Detail::Area(part.Geometry);
            }
            candidate.Layouts.push_back(std::move(layout));const auto checked=Validate(p,candidate);
            if(!checked.Valid||!checked.Complete)return finish("original validation rejected complete solver candidate");
            if(stop())return finish("budget after original validation");
            candidate.GeometryValidated=true;candidate.Diagnostics=original.Diagnostics;selected=std::move(candidate);
            return finish("validated original completion; not a width-optimality claim");
        }
    };
}
PostProcessStage MakeConstraintCompletionStage(ConstraintCompletionSettings settings)
{
    if(!ConstraintKernel::ValidSolverSettings(settings))
        throw std::invalid_argument("Invalid optional constraint completion settings");
    return {"repair.constraint",[settings]{return std::make_unique<ConstraintCompletion>(settings);}};
}
}
