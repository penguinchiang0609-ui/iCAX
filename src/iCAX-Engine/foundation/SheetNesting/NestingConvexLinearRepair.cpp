#include "SheetNestingLinearRepair.h"
#include "SheetNestingNfp.h"
#include "NestingExecution.h"
#include "NestingLinearFeasibility.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace iCAX::SheetNesting
{
namespace
{
    using namespace Detail;
    using Clock=std::chrono::steady_clock;
    using Row=Detail::LinearRow;
    using LinearState=Detail::LinearState;
    using LinearFeasibility=Detail::LinearFeasibility;
    struct Polygon {std::size_t Index;Placement Original;std::vector<Point> Ring;Rect Box;};
    struct Axis {double X,Y,B;};
    struct Pair {std::size_t I,J;std::vector<Axis> Axes;};
    struct Choice {std::size_t PairIndex,AxisIndex;bool operator<(const Choice& b)const{return std::tie(PairIndex,AxisIndex)<std::tie(b.PairIndex,b.AxisIndex);}};
    struct SearchNode {std::vector<Choice> Choices;double Estimate=0;std::size_t Serial=0;};
    struct NodeLess {bool operator()(const SearchNode& a,const SearchNode& b)const{return std::tie(a.Estimate,a.Serial)<std::tie(b.Estimate,b.Serial);}};
    bool convex(const Shape& shape,double tolerance)
    {
        if(shape.Radius>0||!shape.Holes.empty()||shape.Outer.size()<3||shape.Outer.size()>64)return false;
        int sign=0;
        for(std::size_t i=0;i<shape.Outer.size();++i)
        {
            const auto a=shape.Outer[i],b=shape.Outer[(i+1)%shape.Outer.size()],c=shape.Outer[(i+2)%shape.Outer.size()];
            const auto x=(b.X-a.X)*(c.Y-b.Y)-(b.Y-a.Y)*(c.X-b.X);
            if(std::abs(x)<=tolerance*tolerance)continue;const int s=x>0?1:-1;if(sign&&s!=sign)return false;sign=s;
        }
        return sign!=0;
    }
    std::vector<std::pair<long long,long long>> directionKey(const Part& part,double angle,double tolerance)
    {
        const auto b=Bounds(part.Geometry,{0,0,angle});std::vector<std::pair<long long,long long>> ring;
        const double unit=std::max(tolerance*.1,1e-9);
        for(auto v:part.Geometry.Outer){v=Transform(v,{0,0,angle});ring.emplace_back(std::llround((v.X-b.X)/unit),std::llround((v.Y-b.Y)/unit));}
        auto key=ring;for(std::size_t j=1;j<ring.size();++j){std::rotate(ring.begin(),ring.begin()+1,ring.end());key=std::min(key,ring);}
        return key;
    }
    std::vector<double> distinctAngles(const Part& part,double tolerance)
    {
        std::vector<double> angles;std::set<std::vector<std::pair<long long,long long>>> directions;
        for(const auto angle:CandidateAngles(part,tolerance))if(directions.insert(directionKey(part,angle,tolerance)).second)angles.push_back(angle);
        return angles;
    }
    class LinearRepair final:public IPostProcessor
    {
        ConvexLinearRepairSettings Config;
        std::shared_ptr<NfpCache> Cache;
    public:
        LinearRepair(ConvexLinearRepairSettings config,std::shared_ptr<NfpCache> cache):Config(config),Cache(std::move(cache)){}
        Result Run(const Problem& p,const Result& original,const Options& options)override
        {
            const auto started=Clock::now();Result kept=original;std::size_t work=0,nodes=0,lpInfeasible=0,seedCount=0,learned=0,pruned=0,plansTried=0;bool cancelled=false;
            const auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-started).count();};
            const auto stop=[&]{if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;
                return cancelled||elapsed()>=options.TimeLimitSeconds||work>=options.MaxPlacementAttempts;};
            const auto finish=[&](const std::string& why)
            {
                auto stats=kept.Stats;stats.PlacementAttempts=work;stats.ElapsedSeconds=stats.PostProcessingSeconds=elapsed();kept.Stats=stats;
                if(cancelled)kept.State=Status::Cancelled;
                kept.Diagnostics.push_back("repair.convex-linear: "+why+"; seeds="+std::to_string(seedCount)+"; nodes="+std::to_string(nodes)+"; infeasible="+std::to_string(lpInfeasible)+
                    "; learned="+std::to_string(learned)+"; pruned="+std::to_string(pruned)+"; plans="+std::to_string(plansTried));return kept;
            };
            if(stop())return finish("budget");
            std::size_t missing=0;for(const auto& r:original.Remaining)missing+=r.Quantity;
            if(p.PartGap!=0||p.Stocks.size()!=1||original.Layouts.size()!=1||original.State!=Status::Partial||missing!=1||
                original.Layouts[0].Placements.size()>31||!Validate(p,original).Valid)return finish("one-missing convex single-sheet scope");
            std::map<std::string,std::size_t> indices;
            for(std::size_t i=0;i<p.Parts.size();++i)
            {if(p.Parts[i].FreeRotation||!convex(p.Parts[i].Geometry,p.Tolerance))return finish("convex fixed-angle polygon scope");indices.emplace(p.Parts[i].ID,i);}
            const auto missingEntry=std::find_if(original.Remaining.begin(),original.Remaining.end(),[](const Unplaced& q){return q.Quantity>0;});
            const auto mi=indices.at(missingEntry->PartID);const auto& mp=p.Parts[mi];const auto& board=original.Layouts[0];
            std::set<std::size_t> used;for(const auto& x:board.Placements)if(x.PartID==mp.ID)used.insert(x.Instance);
            std::size_t instance=0;while(used.contains(instance))++instance;if(instance>=mp.Quantity)return finish("invalid unused instance");
            const double scale=std::max(board.Width,board.Height),tol=p.Tolerance/scale;
            auto cache=Cache;if(!cache)cache=std::make_shared<NfpCache>();
            std::vector<NfpPreparedShape> prepared;
            for(const auto& part:p.Parts){NfpBuildOptions b;b.Cancel=stop;prepared.push_back(cache->Prepare(part.Geometry,b));if(!prepared.back())return finish("NFP preparation");}
            struct Seed {Pose Transform;double Score;};std::vector<Seed> seeds;
            const auto angles=distinctAngles(mp,p.Tolerance);
            for(const auto angle:angles)
            {
                if(stop())break;const auto b=Bounds(mp.Geometry,{0,0,angle});
                const double lx=p.EdgeMargin-b.X,ly=p.EdgeMargin-b.Y,rx=board.Width-p.EdgeMargin-b.X-b.Width,ty=board.Height-p.EdgeMargin-b.Y-b.Height;
                if(rx<lx||ty<ly)continue;std::set<std::pair<long long,long long>> seen;
                const auto emit=[&](Point v)
                {
                    if(stop()||v.X<lx-p.Tolerance||v.X>rx+p.Tolerance||v.Y<ly-p.Tolerance||v.Y>ty+p.Tolerance)return;
                    v.X=std::clamp(v.X,lx,rx);v.Y=std::clamp(v.Y,ly,ty);const auto unit=std::max(p.Tolerance,1e-8);
                    if(!seen.emplace(std::llround(v.X/unit),std::llround(v.Y/unit)).second)return;
                    ++work;double overlap=0;const Pose pose{v.X,v.Y,angle};
                    for(const auto& x:board.Placements)if(ShapesConflict(mp.Geometry,pose,p.Parts[indices.at(x.PartID)].Geometry,x.Transform,0,p.Tolerance))
                    {const auto c=Bounds(p.Parts[indices.at(x.PartID)].Geometry,x.Transform);overlap+=std::max(0.,std::min(v.X+b.X+b.Width,c.X+c.Width)-std::max(v.X+b.X,c.X))*
                        std::max(0.,std::min(v.Y+b.Y+b.Height,c.Y+c.Height)-std::max(v.Y+b.Y,c.Y));}
                    seeds.push_back({pose,overlap/std::max(Area(mp.Geometry),p.Tolerance)});
                };
                emit({lx,ly});emit({rx,ly});emit({lx,ty});emit({rx,ty});emit({(lx+rx)*.5,(ly+ty)*.5});
                for(const auto& x:board.Placements)
                {
                    if(stop())break;NfpBuildOptions b2;b2.Cancel=stop;b2.TimeLimitSeconds=std::max(0.,options.TimeLimitSeconds-elapsed());
                    const auto region=cache->Get(prepared[indices.at(x.PartID)],x.Transform.AngleDegrees,prepared[mi],angle,b2);
                    if(region.State!=NfpStatus::Ready||!region.Region)continue;
                    for(const auto& loop:region.Region->Loops)for(std::size_t j=0;j<loop.size();++j)
                    {
                        auto a=Transform(loop[j],region.Transform),c=Transform(loop[(j+1)%loop.size()],region.Transform);
                        a.X+=x.Transform.X;a.Y+=x.Transform.Y;c.X+=x.Transform.X;c.Y+=x.Transform.Y;
                        emit(a);emit({(a.X+c.X)*.5,(a.Y+c.Y)*.5});
                    }
                }
            }
            std::sort(seeds.begin(),seeds.end(),[](const Seed& a,const Seed& b){return std::tie(a.Score,a.Transform.X,a.Transform.Y)<std::tie(b.Score,b.Transform.X,b.Transform.Y);});
            std::vector<Seed> selected;
            // Reserve orientation representatives before taking nearby seeds.
            for(const auto angle:angles)for(const auto& s:seeds)if(s.Transform.AngleDegrees==angle)
            {selected.push_back(s);break;}
            for(const auto& s:seeds)
            {
                bool distinct=true;for(const auto& old:selected)if(s.Transform.AngleDegrees==old.Transform.AngleDegrees&&
                    std::hypot(s.Transform.X-old.Transform.X,s.Transform.Y-old.Transform.Y)<scale*.04){distinct=false;break;}
                if(distinct)selected.push_back(s);if(selected.size()>=Config.MaxSeeds)break;
            }
            if(selected.size()>Config.MaxSeeds)selected.resize(Config.MaxSeeds);
            std::size_t serial=0;
            // Position feasibility does not depend on the L1 anchor. A small
            // impossible set of separation choices can therefore be reused
            // across seeds with the same original allowed-angle assignment.
            std::map<std::vector<double>,std::vector<std::vector<Choice>>> conflictsByAngle;
            for(std::size_t si=0;si<selected.size()&&!stop()&&nodes<Config.MaxSearchNodes;++si)
            {
                ++seedCount;
                struct Spin {std::size_t Index;double Angle,Score;};std::vector<Spin> spins;
                if(Config.MaxOrientationPlans>1)for(std::size_t i=0;i<board.Placements.size()&&!stop();++i)
                {
                    const auto& x=board.Placements[i];const auto index=indices.at(x.PartID);
                    if(!ShapesConflict(mp.Geometry,selected[si].Transform,p.Parts[index].Geometry,x.Transform,0,p.Tolerance))continue;
                    for(const auto angle:distinctAngles(p.Parts[index],p.Tolerance))
                    {
                        if(directionKey(p.Parts[index],angle,p.Tolerance)==directionKey(p.Parts[index],x.Transform.AngleDegrees,p.Tolerance))continue;
                        auto pose=x.Transform;pose.AngleDegrees=angle;const auto b=Bounds(p.Parts[index].Geometry,pose);
                        if(b.Width>board.Width||b.Height>board.Height)continue;
                        double score=0;const auto overlap=[&](Rect c)
                        {return std::max(0.,std::min(b.X+b.Width,c.X+c.Width)-std::max(b.X,c.X))*
                            std::max(0.,std::min(b.Y+b.Height,c.Y+c.Height)-std::max(b.Y,c.Y));};
                        score=overlap(Bounds(mp.Geometry,selected[si].Transform));
                        for(std::size_t j=0;j<board.Placements.size();++j)if(j!=i)
                            score+=.25*overlap(Bounds(p.Parts[indices.at(board.Placements[j].PartID)].Geometry,board.Placements[j].Transform));
                        spins.push_back({i,angle,score});
                    }
                }
                std::sort(spins.begin(),spins.end(),[](Spin a,Spin b){return a.Score<b.Score;});
                const auto planCount=std::min(Config.MaxOrientationPlans,spins.size()+1);
                const auto remainingSeeds=selected.size()-si,seedWeight=si==0?std::size_t(4):std::size_t(1);
                const auto seedNodeEnd=nodes+std::max<std::size_t>(1,(Config.MaxSearchNodes-nodes)*seedWeight/(remainingSeeds+seedWeight-1));
                const auto seedDeadline=elapsed()+std::max(0.,options.TimeLimitSeconds-elapsed())*seedWeight/(remainingSeeds+seedWeight-1);
                for(std::size_t plan=0;plan<planCount&&!stop()&&nodes<seedNodeEnd&&elapsed()<seedDeadline;++plan)
                {
                ++plansTried;const auto count=board.Placements.size()+1,positions=2*count,variables=2*positions;
                std::vector<Polygon> shapes;std::vector<double> lower(positions),upper(positions),anchor(positions);
                auto originals=board.Placements;
                if(plan)originals[spins[plan-1].Index].Transform.AngleDegrees=spins[plan-1].Angle;
                originals.push_back({mp.ID,instance,selected[si].Transform,0,true});
                for(std::size_t i=0;i<count;++i)
                {
                    const auto index=indices.at(originals[i].PartID);Polygon q{index,originals[i],{},Bounds(p.Parts[index].Geometry,{0,0,originals[i].Transform.AngleDegrees})};
                    for(const auto point:p.Parts[index].Geometry.Outer){auto v=Transform(point,{0,0,originals[i].Transform.AngleDegrees});q.Ring.push_back({v.X/scale,v.Y/scale});}
                    lower[i]=(p.EdgeMargin-q.Box.X)/scale;upper[i]=(board.Width-p.EdgeMargin-q.Box.X-q.Box.Width)/scale;
                    lower[count+i]=(p.EdgeMargin-q.Box.Y)/scale;upper[count+i]=(board.Height-p.EdgeMargin-q.Box.Y-q.Box.Height)/scale;
                    anchor[i]=originals[i].Transform.X/scale;anchor[count+i]=originals[i].Transform.Y/scale;shapes.push_back(std::move(q));
                }
                std::vector<Pair> pairs;
                for(std::size_t i=0;i<count;++i)for(std::size_t j=0;j<i;++j)
                {
                    Pair pair{i,j,{}};
                    for(const auto* shape:{&shapes[i],&shapes[j]})for(std::size_t k=0;k<shape->Ring.size();++k)
                    {
                        const auto a=shape->Ring[k],b=shape->Ring[(k+1)%shape->Ring.size()];const double len=std::hypot(b.X-a.X,b.Y-a.Y);if(len<=1e-15)continue;
                        for(const int sign:{-1,1})
                        {
                            const double nx=sign*(a.Y-b.Y)/len,ny=sign*(b.X-a.X)/len;
                            if(std::any_of(pair.Axes.begin(),pair.Axes.end(),[&](Axis axis){return std::hypot(axis.X-nx,axis.Y-ny)<1e-10;}))continue;
                            double maximum=-std::numeric_limits<double>::infinity(),minimum=-maximum;
                            for(auto v:shapes[i].Ring)maximum=std::max(maximum,v.X*nx+v.Y*ny);
                            for(auto v:shapes[j].Ring)minimum=std::min(minimum,v.X*nx+v.Y*ny);
                            pair.Axes.push_back({nx,ny,minimum-maximum});
                        }
                    }
                    pairs.push_back(std::move(pair));
                }
                std::vector<Row> base;
                for(std::size_t k=0;k<positions;++k)
                {
                    Row bound;bound.A.resize(variables);bound.A[k]=1;bound.B=upper[k]-lower[k];base.push_back(std::move(bound));
                    Row plus;plus.A.resize(variables);plus.A[k]=1;plus.A[positions+k]=-1;plus.B=anchor[k]-lower[k];base.push_back(std::move(plus));
                    Row minus;minus.A.resize(variables);minus.A[k]=-1;minus.A[positions+k]=-1;minus.B=lower[k]-anchor[k];base.push_back(std::move(minus));
                }
                const auto remaining=planCount-plan;const auto quota=std::max<std::size_t>(1,(seedNodeEnd-nodes)/remaining);
                const auto nodeEnd=nodes+quota;const auto deadline=elapsed()+std::max(0.,seedDeadline-elapsed())/remaining;
                const auto localStop=[&]{return stop()||elapsed()>=deadline;};
                std::vector<double> orientationKey;for(const auto& x:originals)orientationKey.push_back(NormalizeAngle(x.Transform.AngleDegrees));
                auto& learnedConflicts=conflictsByAngle[orientationKey];
                const auto covered=[&](const std::vector<Choice>& choices)
                {
                    return std::any_of(learnedConflicts.begin(),learnedConflicts.end(),[&](const auto& clause)
                    {return std::includes(choices.begin(),choices.end(),clause.begin(),clause.end());});
                };
                const auto separationRow=[&](Choice choice,std::size_t columns)
                {
                    const auto& pair=pairs[choice.PairIndex];const auto a=pair.Axes[choice.AxisIndex];Row r;r.A.resize(columns);
                    r.A[pair.I]=a.X;r.A[pair.J]=-a.X;r.A[count+pair.I]=a.Y;r.A[count+pair.J]=-a.Y;
                    r.B=a.B-a.X*(lower[pair.I]-lower[pair.J])-a.Y*(lower[count+pair.I]-lower[count+pair.J]);return r;
                };
                const auto learnConflict=[&](const std::vector<Choice>& choices)
                {
                    if(!Config.LearnConflicts||choices.empty()||choices.size()>32||localStop()||covered(choices))return;
                    auto clause=choices;
                    {
                        std::vector<Row> probe;
                        for(std::size_t k=0;k<positions;++k)
                        {Row r;r.A.resize(positions);r.A[k]=1;r.B=upper[k]-lower[k];probe.push_back(std::move(r));}
                        for(auto choice:clause)probe.push_back(separationRow(choice,positions));
                        std::vector<double> unused;LinearFeasibility feasibility(positions,probe,work,localStop);
                        if(feasibility.solve(positions,unused,true)!=LinearState::Infeasible)return;
                    }
                    // Deletion filtering uses only bounded positions. Absolute
                    // deviation variables affect preference, never feasibility.
                    for(std::size_t drop=0;drop<clause.size()&&!localStop();)
                    {
                        std::vector<Row> probe;
                        for(std::size_t k=0;k<positions;++k)
                        {Row r;r.A.resize(positions);r.A[k]=1;r.B=upper[k]-lower[k];probe.push_back(std::move(r));}
                        for(std::size_t k=0;k<clause.size();++k)if(k!=drop)probe.push_back(separationRow(clause[k],positions));
                        std::vector<double> unused;LinearFeasibility feasibility(positions,probe,work,localStop);
                        const auto state=feasibility.solve(positions,unused,true);
                        if(state==LinearState::Limited)return;
                        if(state==LinearState::Infeasible)clause.erase(clause.begin()+static_cast<std::ptrdiff_t>(drop));else ++drop;
                    }
                    if(clause.empty()||localStop()||covered(clause))return;
                    learnedConflicts.erase(std::remove_if(learnedConflicts.begin(),learnedConflicts.end(),[&](const auto& old)
                        {return std::includes(old.begin(),old.end(),clause.begin(),clause.end());}),learnedConflicts.end());
                    if(learnedConflicts.size()>=Config.MaxLearnedConflicts)learnedConflicts.erase(learnedConflicts.begin());
                    learnedConflicts.push_back(std::move(clause));++learned;
                };
                std::multiset<SearchNode,NodeLess> open;open.insert({{},0,serial++});std::set<std::vector<std::pair<std::size_t,std::size_t>>> seen;
                while(!open.empty()&&!localStop()&&nodes<nodeEnd)
                {
                    auto search=*open.begin();open.erase(open.begin());++nodes;
                    if(Config.LearnConflicts&&covered(search.Choices)){++pruned;continue;}
                    auto rows=base;
                    for(auto choice:search.Choices)rows.push_back(separationRow(choice,variables));
                    std::vector<double> x;LinearFeasibility lp(variables,rows,work,localStop);const auto state=lp.solve(positions,x);
                    if(state==LinearState::Limited)break;if(state==LinearState::Infeasible){++lpInfeasible;learnConflict(search.Choices);continue;}
                    for(std::size_t i=0;i<positions;++i)x[i]+=lower[i];
                    double objective=0;for(std::size_t i=0;i<positions;++i)objective+=std::abs(x[i]-anchor[i]);
                    std::size_t conflict=pairs.size();double worst=0;
                    for(std::size_t k=0;k<pairs.size();++k)
                    {
                        const auto& pair=pairs[k];double separation=-std::numeric_limits<double>::infinity();
                        for(auto a:pair.Axes)separation=std::max(separation,a.B-a.X*(x[pair.I]-x[pair.J])-a.Y*(x[count+pair.I]-x[count+pair.J]));
                        if(separation< -tol*.05&&separation<worst){worst=separation;conflict=k;}
                    }
                    if(conflict==pairs.size())
                    {
                        auto candidate=original;auto& out=candidate.Layouts[0];out.Placements=originals;out.MaterialArea=0;
                        for(std::size_t i=0;i<count;++i){out.Placements[i].Transform.X=x[i]*scale;out.Placements[i].Transform.Y=x[count+i]*scale;out.Placements[i].Group=0;out.MaterialArea+=Area(p.Parts[shapes[i].Index].Geometry);}
                        candidate.Remaining.clear();candidate.State=Status::Complete;candidate.GeometryValidated=false;
                        const auto checked=Validate(p,candidate);
                        if(checked.Valid&&checked.Complete){kept=std::move(candidate);kept.GeometryValidated=true;return finish("validated-complete");}
                        continue;
                    }
                    if(std::any_of(search.Choices.begin(),search.Choices.end(),[&](Choice c){return c.PairIndex==conflict;}))continue;
                    const auto& pair=pairs[conflict];std::vector<std::size_t> axes(pair.Axes.size());std::iota(axes.begin(),axes.end(),0);
                    const auto gap=[&](std::size_t a){const auto axis=pair.Axes[a];return axis.B-axis.X*(x[pair.I]-x[pair.J])-axis.Y*(x[count+pair.I]-x[count+pair.J]);};
                    std::sort(axes.begin(),axes.end(),[&](auto a,auto b){return gap(a)>gap(b);});
                    for(std::size_t k=0;k<std::min(Config.MaxAxisChoices,axes.size());++k)
                    {
                        auto choices=search.Choices;choices.push_back({conflict,axes[k]});std::sort(choices.begin(),choices.end());
                        if(Config.LearnConflicts&&covered(choices)){++pruned;continue;}
                        std::vector<std::pair<std::size_t,std::size_t>> key;for(auto c:choices)key.emplace_back(c.PairIndex,c.AxisIndex);
                        if(!seen.insert(std::move(key)).second)continue;
                        open.insert({std::move(choices),objective+std::max(0.,-gap(axes[k]))*.5,serial++});
                        if(open.size()>Config.MaxOpenNodes)open.erase(std::prev(open.end()));
                    }
                }
                }
            }
            return finish(stop()?"budget":"finite-search-exhausted");
        }
    };
}
PostProcessStage MakeConvexLinearRepairStage(ConvexLinearRepairSettings settings,std::shared_ptr<NfpCache> cache)
{
    if(!settings.MaxSeeds||settings.MaxSeeds>128||!settings.MaxSearchNodes||settings.MaxSearchNodes>1000000||
        !settings.MaxOpenNodes||settings.MaxOpenNodes>16384||!settings.MaxAxisChoices||settings.MaxAxisChoices>64||
        !settings.MaxLearnedConflicts||settings.MaxLearnedConflicts>8192||!settings.MaxOrientationPlans||settings.MaxOrientationPlans>32)
        throw std::invalid_argument("Invalid convex linear repair limits.");
    return {"repair.convex-linear",[settings,cache=std::move(cache)]{return std::make_unique<LinearRepair>(settings,cache);}};
}
}
