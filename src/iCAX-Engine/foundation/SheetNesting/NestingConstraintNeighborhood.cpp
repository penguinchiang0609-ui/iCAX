#include "SheetNestingConstraint.h"
#include "NestingConstraintKernel.h"
#include "NestingConstraintArithmetic.h"
#include "NestingNfpPlacement.h"
#include <iomanip>

namespace iCAX::SheetNesting
{
namespace
{
    using namespace ConstraintKernel;
    struct NeighborhoodPlan {std::vector<std::size_t> Released;Pose MissingHint;};
    struct MovingOriginal {std::size_t LayoutIndex,PartIndex,Instance;};
    class ConstraintNeighborhood final:public IPostProcessor
    {
        ConstraintNeighborhoodSettings Settings;
    public:
        explicit ConstraintNeighborhood(ConstraintNeighborhoodSettings settings):Settings(settings){}
        Result Run(const Problem& p,const Result& original,const Options& options)override
        {
            const auto started=Clock::now();Result kept=original;Statistics work;
            work.CombinationGroups=original.Stats.CombinationGroups;work.CircleLatticeGroups=original.Stats.CircleLatticeGroups;work.GapFilledParts=original.Stats.GapFilledParts;
            std::atomic<bool> cancelled{false};std::size_t plansTried=0,fixedCount=0,builtCells=0,keptCells=0;
            std::size_t preparedPieces=0,unusedAtoms=0;std::int64_t conflicts=0;bool solverStarted=false;
            const auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-started).count();};
            const auto pollCancel=[&]{if(!cancelled.load()&&options.Cancel&&options.Cancel())cancelled.store(true);return cancelled.load();};
            const auto stop=[&]{return pollCancel()||!std::isfinite(options.TimeLimitSeconds)||elapsed()>=options.TimeLimitSeconds;};
            const auto finish=[&](const std::string& reason)
            {
                if(cancelled.load())kept.State=Status::Cancelled;
                work.ElapsedSeconds=work.PostProcessingSeconds=elapsed();kept.Stats=work;
                std::ostringstream note;note<<"repair.constraint-neighborhood: "<<reason<<"; plans="<<plansTried
                    <<"; coordinate-scale="<<Settings.Solver.CoordinateScale<<"; workers="<<Settings.Solver.Workers
                    <<"; solver-started="<<solverStarted<<"; solver-conflicts="<<conflicts<<"; fixed-originals="<<fixedCount
                    <<"; max-movable="<<Settings.MaxMovableInstances<<"; max-plans="<<Settings.MaxPlans
                    <<"; seconds-per-plan="<<Settings.SecondsPerPlan<<"; forbidden-cells="<<keptCells<<"; built-forbidden-cells="<<builtCells
                    <<"; contact-order="<<(Settings.ContactOrder==ConstraintContactOrder::SmallMaterialContact?"small-material":"layout")
                    <<"; fixed-double-poses-preserved; lattice-failure-is-not-continuous-infeasibility";
                kept.Diagnostics.push_back(note.str());return kept;
            };
            if(stop())return finish("budget-before-model");
            if(!Detail::CheckProblem(p).empty()||!Validate(p,original).Valid)return finish("invalid original problem/incumbent");
            std::size_t missing=0;for(const auto& entry:original.Remaining)missing+=entry.Quantity;
            if(missing==0)return finish("already complete");
            for(const auto& layout:original.Layouts)for(const auto& placed:layout.Placements)
                if(placed.Transform.Mirrored)return finish("unsupported mirrored incumbent in the exact-quarter model; original retained");
            if(missing!=1||p.Stocks.size()!=1||original.Layouts.size()!=1||p.PartGap!=0||
                original.Layouts[0].Placements.size()>Settings.MaxFixedObstacles)return finish("unsupported one-missing single-sheet scope");
            const auto& board=original.Layouts[0];const auto& stock=p.Stocks[0];
            const auto total=board.Placements.size()+1;if(total>options.MaxPlacementAttempts)return finish("insufficient original verification budget");
            const auto scale=Settings.Solver.CoordinateScale;
            if(stock.Width*scale>10000000.||stock.Height*scale>10000000.)return finish("scaled board scope limit");
            const Integer low=static_cast<Integer>(std::ceil(static_cast<long double>(p.EdgeMargin)*scale-1e-10L));
            const Integer highX=static_cast<Integer>(std::floor((static_cast<long double>(stock.Width)-p.EdgeMargin)*scale+1e-10L));
            const Integer highY=static_cast<Integer>(std::floor((static_cast<long double>(stock.Height)-p.EdgeMargin)*scale+1e-10L));
            if(highX<low||highY<low)return finish("empty coordinate lattice");
            std::map<std::string,std::size_t> indices;for(std::size_t i=0;i<p.Parts.size();++i)indices.emplace(p.Parts[i].ID,i);
            const auto missingEntry=std::find_if(original.Remaining.begin(),original.Remaining.end(),[](const Unplaced& q){return q.Quantity>0;});
            const auto mi=indices.at(missingEntry->PartID);std::size_t missingInstance=0;
            for(;;++missingInstance)
            {if(std::none_of(board.Placements.begin(),board.Placements.end(),[&](const Placement& q){return q.PartID==missingEntry->PartID&&q.Instance==missingInstance;}))break;}
            if(missingInstance>=p.Parts[mi].Quantity)return finish("invalid missing original instance");
            auto cache=std::make_shared<NfpCache>();Detail::NfpPlacementGenerator generator(p,cache,stop);
            if(!generator.Ready())return finish("unsupported NFP source");
            std::vector<Detail::NfpBlockedPlacement> proposals;
            const auto validateCandidate=[&](Result candidate)
            {
                if(stop()||options.MaxPlacementAttempts-work.PlacementAttempts<total)return false;
                candidate.Layouts[0].MaterialArea=0;
                for(const auto& placed:candidate.Layouts[0].Placements)candidate.Layouts[0].MaterialArea+=Detail::Area(p.Parts[indices.at(placed.PartID)].Geometry);
                work.PlacementAttempts+=total;const auto checked=Validate(p,candidate);
                if(!checked.Valid||!checked.Complete)
                {kept.Diagnostics.push_back("repair.constraint-neighborhood: original validation rejected model candidate"+
                    (checked.Errors.empty()?std::string():"; "+checked.Errors.front()));return false;}
                if(stop())return false;
                candidate.GeometryValidated=true;candidate.State=Status::Complete;kept=std::move(candidate);return true;
            };
            for(double angle:Detail::CandidateAngles(p.Parts[mi],p.Tolerance))
            {
                if(stop())return finish("budget during NFP neighborhoods");
                if(options.MaxPlacementAttempts-work.PlacementAttempts<=total)break;
                const auto available=options.MaxPlacementAttempts-work.PlacementAttempts-total;
                if(!available)break;
                Detail::NfpPlacementLimits limits;limits.MaxCandidates=32;limits.MaxRawCandidates=4096;limits.MaxContactSegments=256;
                limits.MaxBlockers=std::min<std::size_t>(Settings.MaxMovableInstances-1,8);limits.MaxBlockedCandidates=std::max<std::size_t>(32,Settings.MaxPlans*8);
                limits.MaxAttempts=std::min<std::size_t>(available,std::max<std::size_t>(128,Settings.Solver.MaxHintCandidates*4));
                const auto query=generator.Find(board,mi,angle,limits,stop);work.PlacementAttempts+=query.Attempts;
                for(const auto pose:query.Fits)
                {
                    auto candidate=original;candidate.Remaining.clear();candidate.Layouts[0].Placements.push_back({p.Parts[mi].ID,missingInstance,pose,0,false});
                    candidate.Layouts[0].MaterialArea+=Detail::Area(p.Parts[mi].Geometry);
                    if(validateCandidate(std::move(candidate)))return finish("validated direct original NFP insertion");
                }
                proposals.insert(proposals.end(),query.Blocked.begin(),query.Blocked.end());
            }
            if(stop())return finish("budget after NFP neighborhoods");
            std::stable_sort(proposals.begin(),proposals.end(),[](const auto& a,const auto& b)
                {return std::tuple(a.Blockers.size(),a.OverlapArea,a.Transform.Y,a.Transform.X,a.Transform.AngleDegrees)<
                    std::tuple(b.Blockers.size(),b.OverlapArea,b.Transform.Y,b.Transform.X,b.Transform.AngleDegrees);});
            std::vector<NeighborhoodPlan> bases;std::set<std::vector<std::size_t>> seen;
            for(const auto& proposal:proposals)
            {
                auto released=proposal.Blockers;std::sort(released.begin(),released.end());released.erase(std::unique(released.begin(),released.end()),released.end());
                if(released.empty()||released.size()>=Settings.MaxMovableInstances)continue;
                const auto quarter=Quarter(proposal.Transform.AngleDegrees);
                if(quarter<0||!seen.insert(released).second)continue;
                bases.push_back({released,proposal.Transform});if(bases.size()>=std::max<std::size_t>(1,Settings.MaxPlans/2))break;
            }
            auto plans=bases;
            for(const auto& base:bases)
            {
                if(plans.size()>=Settings.MaxPlans)break;
                if(stop())return finish("budget during contact neighbors");
                auto expanded=base;std::set<std::size_t> released(base.Released.begin(),base.Released.end());
                if(Settings.ContactOrder==ConstraintContactOrder::SmallMaterialContact)
                {
                    const auto target=Detail::Bounds(p.Parts[mi].Geometry,base.MissingHint);
                    while(expanded.Released.size()+1<Settings.MaxMovableInstances)
                    {
                        std::size_t next=board.Placements.size();
                        auto best=std::tuple(std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity(),std::string(),std::size_t(0));
                        for(std::size_t j=0;j<board.Placements.size();++j)
                        {
                            if(stop())return finish("budget during material contact neighbors");if(released.contains(j))continue;
                            const auto& candidate=board.Placements[j];const auto& part=p.Parts[indices.at(candidate.PartID)];bool touches=false;
                            for(const auto i:expanded.Released)
                            {const auto& a=board.Placements[i];
                                if(Detail::ShapesConflict(p.Parts[indices.at(a.PartID)].Geometry,a.Transform,part.Geometry,candidate.Transform,p.Tolerance*16,p.Tolerance))
                                {touches=true;break;}}
                            if(!touches)continue;const auto box=Detail::Bounds(part.Geometry,candidate.Transform);
                            const double distance=std::hypot(box.X+box.Width*.5-target.X-target.Width*.5,box.Y+box.Height*.5-target.Y-target.Height*.5);
                            const auto score=std::tuple(Detail::Area(part.Geometry),distance,candidate.PartID,candidate.Instance);
                            if(score<best){best=score;next=j;}
                        }
                        if(next==board.Placements.size())break;released.insert(next);expanded.Released.push_back(next);
                    }
                }
                else for(std::size_t cursor=0;cursor<expanded.Released.size()&&expanded.Released.size()+1<Settings.MaxMovableInstances;++cursor)
                {
                    const auto& a=board.Placements[expanded.Released[cursor]];
                    for(std::size_t j=0;j<board.Placements.size()&&expanded.Released.size()+1<Settings.MaxMovableInstances;++j)
                    {
                        if(stop())return finish("budget during contact neighbors");if(released.contains(j))continue;
                        const auto& b=board.Placements[j];
                        if(Detail::ShapesConflict(p.Parts[indices.at(a.PartID)].Geometry,a.Transform,p.Parts[indices.at(b.PartID)].Geometry,b.Transform,p.Tolerance*16,p.Tolerance))
                        {released.insert(j);expanded.Released.push_back(j);}
                    }
                }
                std::sort(expanded.Released.begin(),expanded.Released.end());
                if(expanded.Released!=base.Released&&seen.insert(expanded.Released).second)plans.push_back(std::move(expanded));
                if(plans.size()>=Settings.MaxPlans)break;
            }
            if(plans.empty())return finish("no bounded NFP-blocker neighborhood");
            std::vector<Type> types(p.Parts.size());
            for(std::size_t pi=0;pi<p.Parts.size();++pi)if(p.Parts[pi].Quantity)
            {
                if(stop())return finish("budget during source preparation");const auto& part=p.Parts[pi];
                if(part.FreeRotation||part.Material!=stock.Material||part.Geometry.Radius>0||!part.Geometry.Holes.empty()||part.Geometry.Outer.size()>128)
                    return finish("unsupported source/material/angle scope");
                std::string error;if(!PrepareType(part,pi,scale,false,*cache,stop,[&]{return options.TimeLimitSeconds-elapsed();},types[pi],error,preparedPieces,unusedAtoms))return finish(error);
            }
            using Key=std::tuple<std::size_t,std::size_t,std::size_t,std::size_t>;std::map<Key,std::vector<Cell>> regions;
            const auto region=[&](std::size_t a,std::size_t ai,std::size_t b,std::size_t bi)->const std::vector<Cell>&
            {
                const Key key{a,ai,b,bi};const auto found=regions.find(key);if(found!=regions.end())return found->second;
                auto cells=PairCells(types[a].Directions[ai],types[b].Directions[bi],stop,builtCells);keptCells+=cells.size();
                return regions.emplace(key,std::move(cells)).first->second;
            };
            for(const auto& plan:plans)
            {
                if(stop()||options.MaxPlacementAttempts-work.PlacementAttempts<total)break;++plansTried;
                std::vector<MovingOriginal> moving;
                for(auto index:plan.Released){const auto& q=board.Placements[index];moving.push_back({index,indices.at(q.PartID),q.Instance});}
                moving.push_back({board.Placements.size(),mi,missingInstance});fixedCount=board.Placements.size()-plan.Released.size();
                Sat::CpModelBuilder cp;std::vector<Item> items;std::vector<Hint> hints;
                for(const auto originalItem:moving)
                {
                    if(stop())return finish("budget during neighborhood variables");
                    Item item;item.TypeIndex=originalItem.PartIndex;item.Instance=originalItem.Instance;
                    item.X=cp.NewIntVar(operations_research::Domain(low,highX));item.Y=cp.NewIntVar(operations_research::Domain(low,highY));
                    for(const auto& d:types[item.TypeIndex].Directions)
                    {auto present=cp.NewBoolVar();item.Present.push_back(present);cp.AddLessOrEqual(item.X,highX-d.Width).OnlyEnforceIf(present);cp.AddLessOrEqual(item.Y,highY-d.Height).OnlyEnforceIf(present);}
                    cp.AddExactlyOne(item.Present);
                    const auto pose=originalItem.LayoutIndex<board.Placements.size()?board.Placements[originalItem.LayoutIndex].Transform:plan.MissingHint;
                    if(pose.Mirrored)return finish("unsupported mirrored seed in the exact-quarter model; original retained");
                    const int quarter=Quarter(pose.AngleDegrees);if(quarter<0)return finish("unsupported incumbent angle");
                    const int direction=types[item.TypeIndex].QuarterToDirection[quarter];if(direction<0)return finish("incumbent direction unavailable");
                    const auto& d=types[item.TypeIndex].Directions[direction];
                    const auto& sourceBounds=types[item.TypeIndex].QuarterBounds[quarter];
                    Hint hint{direction,std::clamp(static_cast<Integer>(std::llround((pose.X+sourceBounds.X)*scale)),low,std::max(low,highX-d.Width)),
                        std::clamp(static_cast<Integer>(std::llround((pose.Y+sourceBounds.Y)*scale)),low,std::max(low,highY-d.Height))};
                    cp.AddHint(item.X,hint.X);cp.AddHint(item.Y,hint.Y);
                    for(std::size_t a=0;a<item.Present.size();++a)cp.AddHint(item.Present[a],static_cast<int>(a)==hint.Direction);
                    items.push_back(std::move(item));hints.push_back(hint);
                }
                const auto coefficientSafe=[&](const Axis& axis)
                {return std::abs(static_cast<long double>(axis.A))*(highX-low)+std::abs(static_cast<long double>(axis.B))*(highY-low)+std::abs(static_cast<long double>(axis.C))<static_cast<long double>(std::numeric_limits<Integer>::max())/16;};
                for(std::size_t i=0;i<items.size();++i)for(std::size_t j=i+1;j<items.size();++j)
                    for(std::size_t ai=0;ai<items[i].Present.size();++ai)for(std::size_t aj=0;aj<items[j].Present.size();++aj)
                    {
                        if(stop())return finish("budget during moving separation");
                        for(const auto& c:region(items[i].TypeIndex,ai,items[j].TypeIndex,aj))
                        {
                            std::vector<Sat::BoolVar> clause{items[i].Present[ai].Not(),items[j].Present[aj].Not()};std::vector<Sat::BoolVar> alternatives;
                            int chosen=-1;double slack=-std::numeric_limits<double>::infinity();
                            for(const auto axis:c.Axes)
                            {
                                if(!coefficientSafe(axis))return finish("coefficient scope limit");auto outside=cp.NewBoolVar();Sat::LinearExpr lhs;
                                lhs+=Sat::LinearExpr::Term(items[j].X,axis.A);lhs+=Sat::LinearExpr::Term(items[i].X,-axis.A);
                                lhs+=Sat::LinearExpr::Term(items[j].Y,axis.B);lhs+=Sat::LinearExpr::Term(items[i].Y,-axis.B);
                                cp.AddLessOrEqual(lhs,axis.C).OnlyEnforceIf(outside);cp.AddImplication(outside,items[i].Present[ai]);cp.AddImplication(outside,items[j].Present[aj]);clause.push_back(outside);
                                if(Settings.Solver.CompleteSeparationHints)
                                {const double value=double(axis.C-axis.A*(hints[j].X-hints[i].X)-axis.B*(hints[j].Y-hints[i].Y))/std::hypot(double(axis.A),double(axis.B));
                                    if(hints[i].Direction==static_cast<int>(ai)&&hints[j].Direction==static_cast<int>(aj)&&value>slack){slack=value;chosen=static_cast<int>(alternatives.size());}alternatives.push_back(outside);}
                            }
                            cp.AddBoolOr(clause);for(std::size_t a=0;a<alternatives.size();++a)cp.AddHint(alternatives[a],static_cast<int>(a)==chosen);
                        }
                    }
                for(std::size_t i=0;i<items.size();++i)for(std::size_t fixed=0;fixed<board.Placements.size();++fixed)
                {
                    if(std::binary_search(plan.Released.begin(),plan.Released.end(),fixed))continue;
                    const auto& obstacle=board.Placements[fixed];const auto fi=indices.at(obstacle.PartID);const int quarter=Quarter(obstacle.Transform.AngleDegrees);
                    if(quarter<0||types[fi].QuarterToDirection[quarter]<0)return finish("fixed original angle unsupported");
                    const auto fa=static_cast<std::size_t>(types[fi].QuarterToDirection[quarter]);const auto& fixedBounds=types[fi].QuarterBounds[quarter];
                    for(std::size_t ai=0;ai<items[i].Present.size();++ai)
                    {
                        if(stop())return finish("budget during fixed-original separation");
                        for(const auto& c:region(fi,fa,items[i].TypeIndex,ai))
                        {
                            std::vector<Sat::BoolVar> clause{items[i].Present[ai].Not()};std::vector<Sat::BoolVar> alternatives;
                            int chosen=-1;double slack=-std::numeric_limits<double>::infinity();
                            for(const auto axis:c.Axes)
                            {
                                if(!coefficientSafe(axis)||std::abs(axis.A)>std::numeric_limits<Integer>::max()/scale||std::abs(axis.B)>std::numeric_limits<Integer>::max()/scale)return finish("fixed coefficient scope limit");
                                Integer rhs;
                                if(!Detail::FloorConstraintAffine(axis.C,{{axis.A*scale,obstacle.Transform.X},{axis.A*scale,fixedBounds.X},
                                    {axis.B*scale,obstacle.Transform.Y},{axis.B*scale,fixedBounds.Y}},rhs))return finish("unsupported exact fixed right-hand-side arithmetic");
                                auto outside=cp.NewBoolVar();auto lhs=Sat::LinearExpr::Term(items[i].X,axis.A)+Sat::LinearExpr::Term(items[i].Y,axis.B);
                                cp.AddLessOrEqual(lhs,rhs).OnlyEnforceIf(outside);cp.AddImplication(outside,items[i].Present[ai]);clause.push_back(outside);
                                if(Settings.Solver.CompleteSeparationHints)
                                {const double value=double(rhs-axis.A*hints[i].X-axis.B*hints[i].Y)/std::hypot(double(axis.A),double(axis.B));
                                    if(hints[i].Direction==static_cast<int>(ai)&&value>slack){slack=value;chosen=static_cast<int>(alternatives.size());}alternatives.push_back(outside);}
                            }
                            cp.AddBoolOr(clause);for(std::size_t a=0;a<alternatives.size();++a)cp.AddHint(alternatives[a],static_cast<int>(a)==chosen);
                        }
                    }
                }
                if(stop())return finish("budget before neighborhood solve");const auto proto=cp.Build();
                const double left=options.TimeLimitSeconds-elapsed(),budget=std::min(Settings.SecondsPerPlan,left);
                const double reserve=std::min(.25,budget*.1);if(budget<=reserve)break;
                const auto solveStarted=Clock::now();solverStarted=true;
                const auto response=SolveBounded(proto,Settings.Solver,options,budget-reserve,pollCancel,
                    [&]{return elapsed()>=options.TimeLimitSeconds||std::chrono::duration<double>(Clock::now()-solveStarted).count()>=budget;});
                conflicts+=response.num_conflicts();
                std::ostringstream trace;trace<<std::setprecision(17)<<"repair.constraint-neighborhood.plan: "<<plansTried
                    <<"; status="<<Sat::CpSolverStatus_Name(response.status())<<"; solve-wall="<<std::chrono::duration<double>(Clock::now()-solveStarted).count()
                    <<"; fixed-originals="<<fixedCount<<"; released=";
                for(const auto index:plan.Released){const auto& q=board.Placements[index];trace<<q.PartID<<'#'<<q.Instance<<',';}
                trace<<"; missing-hint="<<plan.MissingHint.X<<','<<plan.MissingHint.Y<<','<<plan.MissingHint.AngleDegrees
                    <<"; variables="<<proto.variables_size()<<"; constraints="<<proto.constraints_size();
                kept.Diagnostics.push_back(trace.str());if(stop())break;
                if(response.status()!=Sat::CpSolverStatus::FEASIBLE&&response.status()!=Sat::CpSolverStatus::OPTIMAL)continue;
                auto candidate=original;candidate.Diagnostics=kept.Diagnostics;candidate.Remaining.clear();candidate.Layouts[0].MaterialArea+=Detail::Area(p.Parts[mi].Geometry);
                for(std::size_t i=0;i<items.size();++i)
                {
                    std::size_t ai=0;while(ai<items[i].Present.size()&&!Sat::SolutionBooleanValue(response,items[i].Present[ai]))++ai;
                    if(ai==items[i].Present.size())return finish("invalid exported direction");const auto& d=types[items[i].TypeIndex].Directions[ai];
                    const Pose pose{double(Sat::SolutionIntegerValue(response,items[i].X))/scale-d.OriginalBounds.X,
                        double(Sat::SolutionIntegerValue(response,items[i].Y))/scale-d.OriginalBounds.Y,d.Angle};
                    if(moving[i].LayoutIndex<board.Placements.size())
                    {auto& changed=candidate.Layouts[0].Placements[moving[i].LayoutIndex];changed.Transform=pose;changed.Group=0;}
                    else candidate.Layouts[0].Placements.push_back({p.Parts[mi].ID,missingInstance,pose,0,false});
                }
                if(validateCandidate(std::move(candidate)))return finish("validated original neighborhood completion");
            }
            return finish("no original completion in bounded neighborhoods");
        }
    };
}
PostProcessStage MakeConstraintNeighborhoodStage(ConstraintNeighborhoodSettings settings)
{
    if(!ConstraintKernel::ValidSolverSettings(settings.Solver)||settings.MaxMovableInstances<2||settings.MaxMovableInstances>32||
        settings.MaxMovableInstances>settings.Solver.MaxInstances||settings.MaxPlans<1||settings.MaxPlans>128||
        settings.MaxFixedObstacles<1||settings.MaxFixedObstacles>128||!std::isfinite(settings.SecondsPerPlan)||settings.SecondsPerPlan<=0||
        (settings.ContactOrder!=ConstraintContactOrder::LayoutOrder&&settings.ContactOrder!=ConstraintContactOrder::SmallMaterialContact))
        throw std::invalid_argument("Invalid optional constraint neighborhood settings");
    return {"repair.constraint-neighborhood",[settings]{return std::make_unique<ConstraintNeighborhood>(settings);}};
}
}
