#include "SheetNestingConstraint.h"
#include "NestingConstraintKernel.h"
#include "NestingConvexTailIntervals.h"
#include "NestingExecution.h"
#include <optional>

namespace iCAX::SheetNesting
{
namespace
{
    using namespace ConstraintKernel;
    struct Normal
    {
        Integer X=0,Y=0;
        bool operator<(const Normal& b)const{return std::tie(X,Y)<std::tie(b.X,b.Y);}
    };
    struct ConvexVersion
    {
        Ring Points;
        Integer OriginX=0,OriginY=0,Width=0,Height=0;
        std::set<Normal> Normals;
    };
    struct ConvexType
    {
        std::size_t Original=0;
        Integer Material2=0;
        std::array<ConvexVersion,4> Versions;
        std::vector<Integer> Quarters;
    };
    bool ExactInteger(double value,Integer& result,double limit=10000000.)
    {
        if(!std::isfinite(value)||std::abs(value)>limit||value!=std::trunc(value))return false;
        result=static_cast<Integer>(value);return true;
    }
    int ExactQuarter(double angle)
    {
        if(!std::isfinite(angle))return -1;
        const auto normalized=Detail::NormalizeAngle(angle);
        const auto q=static_cast<int>(std::round(normalized/90.));
        return normalized==q*90.?q%4:-1;
    }
    Normal EdgeNormal(IPoint a,IPoint b)
    {
        Normal n{a.Y-b.Y,b.X-a.X};const auto divisor=std::gcd(std::abs(n.X),std::abs(n.Y));
        if(divisor){n.X/=divisor;n.Y/=divisor;}
        if(n.X<0||(n.X==0&&n.Y<0)){n.X=-n.X;n.Y=-n.Y;}return n;
    }
    std::pair<Integer,Integer> Project(const Ring& points,Normal normal)
    {
        Integer low=std::numeric_limits<Integer>::max(),high=std::numeric_limits<Integer>::min();
        for(const auto p:points){const auto v=normal.X*p.X+normal.Y*p.Y;low=std::min(low,v);high=std::max(high,v);}
        return {low,high};
    }
    bool PrepareConvex(const Part& part,std::size_t index,ConvexType& type,std::string& why)
    {
        if(part.FreeRotation||part.Geometry.Radius!=0||!part.Geometry.Holes.empty())
        {why="unsupported original polygon/finite-angle scope: "+part.ID;return false;}
        Ring original;
        for(const auto p:part.Geometry.Outer)
        {IPoint q;if(!ExactInteger(p.X,q.X)||!ExactInteger(p.Y,q.Y)){why="unsupported noninteger original contour: "+part.ID;return false;}original.push_back(q);}
        if(original.size()>1&&original.front()==original.back())original.pop_back();
        if(original.size()<3||original.size()>128){why="unsupported original vertex count: "+part.ID;return false;}
        const auto signedArea=Area2(original);if(!signedArea){why="unsupported degenerate original: "+part.ID;return false;}
        const Integer sign=signedArea>0?1:-1;
        for(std::size_t i=0;i<original.size();++i)for(std::size_t j=0;j<original.size();++j)
            if(j!=i&&j!=(i+1)%original.size()&&sign*Cross(original[i],original[(i+1)%original.size()],original[j])<=0)
            {why="unsupported non-strictly-convex original: "+part.ID;return false;}
        const auto angles=part.AllowedAnglesDegrees.empty()?std::vector<double>{0}:part.AllowedAnglesDegrees;
        for(const auto angle:angles)
        {
            const int q=ExactQuarter(angle);if(q<0){why="unsupported nonquarter original angle: "+part.ID;return false;}
            if(std::find(type.Quarters.begin(),type.Quarters.end(),q)==type.Quarters.end())type.Quarters.push_back(q);
        }
        type.Original=index;type.Material2=std::abs(signedArea);
        for(int q=0;q<4;++q)
        {
            auto& v=type.Versions[q];Integer lowX=std::numeric_limits<Integer>::max(),lowY=lowX,highX=-lowX,highY=-lowX;
            for(const auto p:original)
            {
                IPoint r;switch(q){case 0:r=p;break;case 1:r={-p.Y,p.X};break;case 2:r={-p.X,-p.Y};break;default:r={p.Y,-p.X};}
                v.Points.push_back(r);lowX=std::min(lowX,r.X);lowY=std::min(lowY,r.Y);highX=std::max(highX,r.X);highY=std::max(highY,r.Y);
            }
            v.OriginX=lowX;v.OriginY=lowY;v.Width=highX-lowX;v.Height=highY-lowY;
            for(auto& p:v.Points){p.X-=lowX;p.Y-=lowY;}
            for(std::size_t i=0;i<v.Points.size();++i)v.Normals.insert(EdgeNormal(v.Points[i],v.Points[(i+1)%v.Points.size()]));
        }
        return true;
    }
    bool World(const Placement& placement,const ConvexType& type,Ring& points,int& quarter)
    {
        Integer x=0,y=0;quarter=ExactQuarter(placement.Transform.AngleDegrees);
        if(quarter<0||!ExactInteger(placement.Transform.X,x,30000000.)||!ExactInteger(placement.Transform.Y,y,30000000.)||
            std::find(type.Quarters.begin(),type.Quarters.end(),quarter)==type.Quarters.end())return false;
        const auto& v=type.Versions[quarter];points=v.Points;
        for(auto& p:points){p.X+=x+v.OriginX;p.Y+=y+v.OriginY;}return true;
    }
    bool Disjoint(const Ring& a,const ConvexVersion& av,const Ring& b,const ConvexVersion& bv)
    {
        const auto check=[&](const std::set<Normal>& normals)
        {for(const auto n:normals){const auto aa=Project(a,n),bb=Project(b,n);if(aa.second<=bb.first||bb.second<=aa.first)return true;}return false;};
        return check(av.Normals)||check(bv.Normals);
    }
    using Identity=std::pair<std::string,std::size_t>;
    using BoardIdentity=std::pair<std::string,std::size_t>;
    std::set<Identity> Members(const Layout& layout)
    {std::set<Identity> keys;for(const auto& q:layout.Placements)keys.emplace(q.PartID,q.Instance);return keys;}

    class ConvexTailRepack final:public IPostProcessor
    {
        const ConvexTailRepackSettings Settings;
    public:
        explicit ConvexTailRepack(ConvexTailRepackSettings settings):Settings(settings){}
        Result Run(const Problem& p,const Result& source,const Options& options)override
        {
            const auto started=Clock::now();Result kept=source;Statistics work;work.VerificationSeconds=work.EvaluationSeconds=0;
            bool cancelled=false;std::size_t plans=0,accepted=0,sweeps=0,intervalsBuilt=0,compactionStops=0;std::int64_t conflicts=0;
            const auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-started).count();};
            const auto pollCancel=[&]{if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;return cancelled;};
            const auto lifecycleStop=[&]{return pollCancel()||!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<=0||elapsed()>=options.TimeLimitSeconds;};
            const auto stop=[&]{return lifecycleStop()||work.PlacementAttempts>=options.MaxPlacementAttempts;};
            const auto finish=[&](const std::string& reason)
            {
                kept.Stats=work;kept.Stats.CombinationGroups=source.Stats.CombinationGroups;kept.Stats.CircleLatticeGroups=source.Stats.CircleLatticeGroups;kept.Stats.GapFilledParts=source.Stats.GapFilledParts;
                kept.Stats.ElapsedSeconds=kept.Stats.PostProcessingSeconds=elapsed();if(cancelled)kept.State=Status::Cancelled;
                std::ostringstream note;note<<"convex.tail-repack: "<<reason<<"; plans="<<plans<<"; accepted="<<accepted<<"; solver-conflicts="<<conflicts
                    <<"; left-down-complete-sweeps="<<sweeps<<"; compaction-forbidden-intervals="<<intervalsBuilt<<"; compaction-budget-stops="<<compactionStops
                    <<"; attempts="<<work.PlacementAttempts<<"; integer-positions; original-quarter-domains; solver-first-solution=0; no-author-coordinate-seed; lattice-failure-is-not-global-infeasibility";
                kept.Diagnostics.push_back(note.str());return kept;
            };
            try
            {
                if(stop())return finish("cancelled-or-budget-before-model");
                if(!Detail::CheckProblem(p).empty()||source.State!=Status::Complete||!source.Remaining.empty())return finish("requires-complete-original-incumbent");
                if(p.PartGap!=0||p.EdgeMargin!=0)return finish("unsupported nonzero original gap/margin");
                std::size_t total=0;for(const auto& part:p.Parts)total+=part.Quantity;
                if(!total||source.Layouts.size()<2)return finish("no-tail-reconstruction-needed");
                if(total>512)return finish("unsupported whole-original instance scope (>512)");
                if(total>options.MaxPlacementAttempts)return finish("insufficient full-original verification budget");
                std::map<std::string,ConvexType> types;std::map<std::string,const Stock*> stocks;
                for(const auto& stock:p.Stocks)
                {Integer w=0,h=0;if(!ExactInteger(stock.Width,w)||!ExactInteger(stock.Height,h))return finish("unsupported noninteger original stock");stocks.emplace(stock.ID,&stock);}
                for(std::size_t i=0;i<p.Parts.size();++i)
                {
                    if(stop())return finish("budget during source preparation");if(!p.Parts[i].Quantity)continue;
                    ConvexType type;std::string why;if(!PrepareConvex(p.Parts[i],i,type,why))return finish(why);types.emplace(p.Parts[i].ID,std::move(type));
                }
                const auto exactMaterial=[&](const Result& result)
                {
                    for(const auto& l:result.Layouts)
                    {
                        if(stop())return false;std::vector<Ring> world;std::vector<int> quarters;
                        const auto& stock=*stocks.at(l.StockID);
                        for(const auto& q:l.Placements)
                        {
                            Ring points;int quarter=0;if(!World(q,types.at(q.PartID),points,quarter))return false;
                            for(const auto point:points)if(point.X<0||point.Y<0||point.X>stock.Width||point.Y>stock.Height)return false;
                            quarters.push_back(quarter);world.push_back(std::move(points));
                        }
                        for(std::size_t i=0;i<world.size();++i)for(std::size_t j=0;j<i;++j)
                            if(!Disjoint(world[i],types.at(l.Placements[i].PartID).Versions[quarters[i]],world[j],types.at(l.Placements[j].PartID).Versions[quarters[j]]))return false;
                    }
                    return true;
                };
                JobResult best;best.Output=kept;best.Output.Stats={};Detail::AssessCandidate(p,best);
                work.VerificationSeconds+=std::max(0.,best.Output.Stats.VerificationSeconds);work.EvaluationSeconds+=std::max(0.,best.Output.Stats.EvaluationSeconds);
                if(!Detail::Eligible(best)||!best.Checked.Complete)return finish("invalid original incumbent");
                if(!exactMaterial(kept))return finish("unsupported noninteger poses or invalid exact original material");
                std::vector<std::string> order;
                for(auto it=kept.Layouts.rbegin();it!=kept.Layouts.rend();++it)if(std::find(order.begin(),order.end(),it->StockID)==order.end())order.push_back(it->StockID);
                ConstraintCompletionSettings solverSettings;solverSettings.CoordinateScale=1;solverSettings.Workers=Settings.Workers;solverSettings.MaxConflicts=Settings.MaxConflicts;solverSettings.MaxInstances=Settings.MaxReleasedInstances;
                for(const auto& sid:order)for(int phase=0;phase<(Settings.AllowSameSheetCount?2:1)&&!stop();++phase)
                for(std::size_t window=Settings.MinWindowBoards;window<=Settings.MaxWindowBoards&&!stop();++window)
                {
                    std::vector<std::size_t> cohort;for(std::size_t i=0;i<kept.Layouts.size();++i)if(kept.Layouts[i].StockID==sid)cohort.push_back(i);
                    if(window>cohort.size())break;std::vector<std::size_t> released(cohort.end()-window,cohort.end());
                    const auto target=phase?window:(window>Settings.TargetBoardReduction?window-Settings.TargetBoardReduction:0);
                    if(!target)continue;
                    std::vector<Placement> members;for(const auto i:released)members.insert(members.end(),kept.Layouts[i].Placements.begin(),kept.Layouts[i].Placements.end());
                    if(members.size()>Settings.MaxReleasedInstances)continue;
                    std::stable_sort(members.begin(),members.end(),[&](const auto& a,const auto& b)
                    {return std::tuple(-types.at(a.PartID).Material2,a.PartID,a.Instance)<std::tuple(-types.at(b.PartID).Material2,b.PartID,b.Instance);});
                    const auto& stock=*stocks.at(sid);const auto W=static_cast<Integer>(stock.Width),H=static_cast<Integer>(stock.Height),cap=2*W*H;
                    Integer material=0;for(const auto& q:members)material+=types.at(q.PartID).Material2;
                    if(material>cap*static_cast<Integer>(target))continue;
                    ++plans;++work.CandidatePatterns;Sat::CpModelBuilder cp;
                    struct Variable{Sat::IntVar X,Y,Rotation,Bin;std::vector<Sat::BoolVar> In;};std::vector<Variable> vars;
                    for(std::size_t i=0;i<members.size();++i)
                    {
                        if(stop())return finish("budget during variables");const auto& type=types.at(members[i].PartID);std::vector<Integer> allowed,widths,heights;
                        for(int q=0;q<4;++q){const auto& v=type.Versions[q];widths.push_back(v.Width);heights.push_back(v.Height);
                            if(v.Width<=W&&v.Height<=H&&std::find(type.Quarters.begin(),type.Quarters.end(),q)!=type.Quarters.end())allowed.push_back(q);}
                        if(allowed.empty())return finish("no original orientation fits original stock");
                        Variable v;v.X=cp.NewIntVar(operations_research::Domain(0,W));v.Y=cp.NewIntVar(operations_research::Domain(0,H));
                        v.Rotation=cp.NewIntVar(operations_research::Domain::FromValues(allowed));v.Bin=cp.NewIntVar(operations_research::Domain(0,target-1));
                        auto width=cp.NewIntVar(operations_research::Domain(*std::min_element(widths.begin(),widths.end()),*std::max_element(widths.begin(),widths.end())));
                        auto height=cp.NewIntVar(operations_research::Domain(*std::min_element(heights.begin(),heights.end()),*std::max_element(heights.begin(),heights.end())));
                        cp.AddElement(v.Rotation,widths,width);cp.AddElement(v.Rotation,heights,height);cp.AddLessOrEqual(v.X+width,W);cp.AddLessOrEqual(v.Y+height,H);
                        Sat::LinearExpr bin;
                        for(std::size_t b=0;b<target;++b){v.In.push_back(cp.NewBoolVar());bin+=Sat::LinearExpr(v.In.back())*static_cast<Integer>(b);}
                        cp.AddExactlyOne(v.In);cp.AddEquality(v.Bin,bin);vars.push_back(std::move(v));
                    }
                    std::vector<Sat::BoolVar> used,last;std::vector<Sat::IntVar> boardAreas;Sat::LinearExpr usedCount;
                    for(std::size_t b=0;b<target;++b)
                    {
                        used.push_back(cp.NewBoolVar());usedCount+=used.back();auto area=cp.NewIntVar(operations_research::Domain(0,cap));boardAreas.push_back(area);Sat::LinearExpr sum;
                        for(std::size_t i=0;i<members.size();++i)sum+=Sat::LinearExpr(vars[i].In[b])*types.at(members[i].PartID).Material2;
                        cp.AddEquality(area,sum);cp.AddLessOrEqual(area,Sat::LinearExpr(used[b])*cap);cp.AddGreaterOrEqual(area,used[b]);
                        if(b){cp.AddGreaterOrEqual(used[b-1],used[b]);cp.AddGreaterOrEqual(boardAreas[b-1],area);}
                    }
                    auto tail=cp.NewIntVar(operations_research::Domain(0,cap));
                    for(std::size_t b=0;b<target;++b)
                    {
                        auto l=cp.NewBoolVar();last.push_back(l);
                        if(b+1==target)cp.AddEquality(l,used[b]);else
                        {cp.AddLessOrEqual(l,used[b]);cp.AddLessOrEqual(Sat::LinearExpr(l)+used[b+1],1);cp.AddGreaterOrEqual(l,Sat::LinearExpr(used[b])-used[b+1]);}
                        cp.AddEquality(tail,boardAreas[b]).OnlyEnforceIf(l);
                    }
                    cp.AddExactlyOne(last);
                    // Material-sorted bins are a label symmetry only. No largest
                    // original is also fixed to bin0, and no angle is pruned.
                    std::map<std::tuple<std::size_t,Integer,Integer,bool>,Sat::IntVar> projections;
                    const auto projection=[&](std::size_t i,Normal n,bool high)
                    {
                        const auto key=std::make_tuple(i,n.X,n.Y,high);const auto found=projections.find(key);if(found!=projections.end())return found->second;
                        std::vector<Integer> table;for(const auto& v:types.at(members[i].PartID).Versions){const auto a=Project(v.Points,n);table.push_back(high?a.second:a.first);}
                        auto variable=cp.NewIntVar(operations_research::Domain(*std::min_element(table.begin(),table.end()),*std::max_element(table.begin(),table.end())));
                        cp.AddElement(vars[i].Rotation,table,variable);projections.emplace(key,variable);return variable;
                    };
                    std::map<std::pair<std::string,std::string>,std::set<Normal>> normals;
                    for(std::size_t i=0;i<members.size();++i)for(std::size_t j=0;j<i;++j)
                    {
                        if(stop())return finish("budget during original separating axes");auto same=cp.NewBoolVar();cp.AddEquality(vars[i].Bin,vars[j].Bin).OnlyEnforceIf(same);cp.AddNotEqual(vars[i].Bin,vars[j].Bin).OnlyEnforceIf(same.Not());
                        auto key=std::minmax(members[i].PartID,members[j].PartID);const auto pair=std::make_pair(key.first,key.second);auto found=normals.find(pair);
                        if(found==normals.end())
                        {std::set<Normal> all;for(const auto& pid:{members[i].PartID,members[j].PartID})for(const auto& v:types.at(pid).Versions)all.insert(v.Normals.begin(),v.Normals.end());found=normals.emplace(pair,std::move(all)).first;}
                        std::vector<Sat::BoolVar> clause{same.Not()};
                        for(const auto n:found->second)
                        {
                            auto a=cp.NewBoolVar(),b=cp.NewBoolVar();clause.push_back(a);clause.push_back(b);
                            const auto ai=Sat::LinearExpr(vars[i].X)*n.X+Sat::LinearExpr(vars[i].Y)*n.Y;
                            const auto bj=Sat::LinearExpr(vars[j].X)*n.X+Sat::LinearExpr(vars[j].Y)*n.Y;
                            cp.AddLessOrEqual(ai+projection(i,n,true),bj+projection(j,n,false)).OnlyEnforceIf(a);
                            cp.AddLessOrEqual(bj+projection(j,n,true),ai+projection(i,n,false)).OnlyEnforceIf(b);
                        }
                        cp.AddBoolOr(clause);
                    }
                    cp.Minimize(usedCount*(cap+1)+tail);
                    if(stop())return finish("budget before independent window solve");
                    const double budget=std::min(Settings.SecondsPerWindow,options.TimeLimitSeconds-elapsed());const double reserve=std::min(1.,budget*.25);
                    if(budget<=reserve)break;const auto solveStarted=Clock::now();
                    const auto response=SolveBounded(cp.Build(),solverSettings,options,budget-reserve,pollCancel,
                        [&]{return elapsed()>=options.TimeLimitSeconds||std::chrono::duration<double>(Clock::now()-solveStarted).count()>=budget;},false);
                    conflicts+=response.num_conflicts();
                    kept.Diagnostics.push_back("convex.tail-repack.window: stock="+sid+"; released-boards="+std::to_string(window)+"; released-originals="+std::to_string(members.size())+
                        "; target="+std::to_string(target)+"; same-count="+std::to_string(phase)+"; status="+Sat::CpSolverStatus_Name(response.status()));
                    if(stop())break;if(response.status()!=Sat::CpSolverStatus::FEASIBLE&&response.status()!=Sat::CpSolverStatus::OPTIMAL)continue;
                    std::vector<Layout> changed;
                    for(std::size_t b=0;b<target;++b)
                    {
                        if(!Sat::SolutionBooleanValue(response,used[b]))continue;Layout board{sid,kept.Layouts[released[changed.size()]].StockInstance,stock.Width,stock.Height,{},0};
                        for(std::size_t i=0;i<members.size();++i)if(Sat::SolutionIntegerValue(response,vars[i].Bin)==static_cast<Integer>(b))
                        {
                            const auto q=static_cast<int>(Sat::SolutionIntegerValue(response,vars[i].Rotation));const auto& v=types.at(members[i].PartID).Versions[q];auto placed=members[i];
                            placed.Transform={double(Sat::SolutionIntegerValue(response,vars[i].X)-v.OriginX),double(Sat::SolutionIntegerValue(response,vars[i].Y)-v.OriginY),q*90.};placed.Group=0;
                            board.MaterialArea+=types.at(placed.PartID).Material2*.5;board.Placements.push_back(std::move(placed));
                        }
                        changed.push_back(std::move(board));
                    }
                    auto candidate=kept;candidate.Layouts.clear();candidate.Stats={};std::size_t next=0;std::set<std::size_t> moving(released.begin(),released.end());std::set<BoardIdentity> changedKeys;
                    for(const auto& l:changed)changedKeys.emplace(l.StockID,l.StockInstance);
                    for(std::size_t i=0;i<kept.Layouts.size();++i)
                    {if(!moving.contains(i))candidate.Layouts.push_back(kept.Layouts[i]);else if(next<changed.size())candidate.Layouts.push_back(std::move(changed[next++]));}
                    // Restore denser arrangements already present in this own
                    // incumbent when exactly the same originals still share a
                    // sheet. This is a job-local motif, never an author seed.
                    const auto envelope=[&](const Layout& l)
                    {Integer lx=std::numeric_limits<Integer>::max(),ly=lx,rx=-lx,ty=-lx;for(const auto& q:l.Placements){Ring w;int r=0;World(q,types.at(q.PartID),w,r);for(const auto v:w){lx=std::min(lx,v.X);ly=std::min(ly,v.Y);rx=std::max(rx,v.X);ty=std::max(ty,v.Y);}}return(rx-lx)*(ty-ly);};
                    for(auto& l:candidate.Layouts)if(changedKeys.contains({l.StockID,l.StockInstance}))for(const auto& old:kept.Layouts)
                        if(old.StockID==l.StockID&&Members(old)==Members(l)&&envelope(old)<envelope(l))
                        {l.Placements=old.Placements;for(auto& q:l.Placements)q.Group=0;}
                    if(!exactMaterial(candidate))continue;
                    bool completed=true;
                    for(std::size_t pass=0;pass<Settings.MaxCompactionPasses;++pass)
                    {
                        bool moved=false;
                        for(const int axis:{0,1})for(auto& l:candidate.Layouts)if(changedKeys.contains({l.StockID,l.StockInstance}))
                        for(std::size_t i=0;i<l.Placements.size();++i)
                        {
                            if(stop()){completed=false;break;}auto& q=l.Placements[i];Ring world;int quarter=0;World(q,types.at(q.PartID),world,quarter);
                            Integer low=std::numeric_limits<Integer>::max();for(const auto v:world)low=std::min(low,axis?v.Y:v.X);
                            std::vector<ConvexTailDetail::IntegerRange> forbidden;
                            for(std::size_t j=0;j<l.Placements.size();++j)if(j!=i)
                            {
                                if(stop()){completed=false;break;}Ring other;int r=0;World(l.Placements[j],types.at(l.Placements[j].PartID),other,r);
                                ConvexTailDetail::IntegerRange range{0,low};
                                const auto intersect=[&](const std::set<Normal>& normals)
                                {
                                    for(const auto n:normals)
                                    {
                                        const auto a=Project(world,n),b=Project(other,n);const auto c=axis?n.Y:n.X;
                                        if(!ConvexTailDetail::IntersectStrictProjection(range,c,a.first-c*low,a.second-c*low,b.first,b.second))return false;
                                    }
                                    return true;
                                };
                                if(intersect(types.at(q.PartID).Versions[quarter].Normals)&&intersect(types.at(l.Placements[j].PartID).Versions[r].Normals))
                                {forbidden.push_back(range);++intervalsBuilt;}
                            }
                            if(!completed)break;
                            const auto position=ConvexTailDetail::FirstAllowedInteger(low,std::move(forbidden));
                            if(!position)throw std::logic_error("Exact compaction interval excluded its validated original position");
                            if(stop()){completed=false;break;}++work.PlacementAttempts;const auto delta=*position-low;auto proposed=world;
                            for(auto& v:proposed){if(axis)v.Y+=delta;else v.X+=delta;}
                            bool safe=true;
                            for(std::size_t j=0;j<l.Placements.size()&&safe;++j)if(j!=i)
                            {Ring other;int r=0;World(l.Placements[j],types.at(l.Placements[j].PartID),other,r);safe=Disjoint(proposed,types.at(q.PartID).Versions[quarter],other,types.at(l.Placements[j].PartID).Versions[r]);}
                            if(!safe)throw std::logic_error("Exact compaction endpoint failed original material verification");
                            if(delta){if(axis)q.Transform.Y+=double(delta);else q.Transform.X+=double(delta);q.Group=0;moved=true;}
                        }
                        if(!completed)break;++sweeps;if(!moved)break;
                    }
                    if(!completed||stop()){++compactionStops;break;}
                    if(!exactMaterial(candidate)||stop())continue;
                    if(total>options.MaxPlacementAttempts-work.PlacementAttempts)break;work.PlacementAttempts+=total;
                    JobResult trial;trial.Output=std::move(candidate);Detail::AssessCandidate(p,trial);
                    work.VerificationSeconds+=std::max(0.,trial.Output.Stats.VerificationSeconds);work.EvaluationSeconds+=std::max(0.,trial.Output.Stats.EvaluationSeconds);
                    // The last available attempt may have paid for the entire
                    // original verification. Accept that checked result before
                    // the attempt cap stops the next independent window.
                    if(lifecycleStop())break;
                    if(Detail::Eligible(trial)&&trial.Checked.Complete&&trial.Checked.PlacedCount==total&&Detail::Quality(trial,p,options.Ranking)<Detail::Quality(best,p,options.Ranking))
                    {best=std::move(trial);kept=best.Output;kept.GeometryValidated=true;++accepted;}
                }
                return finish("bounded-original-tail-windows-complete");
            }
            catch(const std::exception& error){return finish(std::string("failed safely: ")+error.what());}
            catch(...){return finish("failed safely with nonstandard exception");}
        }
    };
}
PostProcessStage MakeConvexTailRepackStage(ConvexTailRepackSettings settings)
{
    if(settings.MinWindowBoards<2||settings.MinWindowBoards>settings.MaxWindowBoards||settings.MaxWindowBoards>8||
       settings.MaxReleasedInstances<1||settings.MaxReleasedInstances>18||settings.TargetBoardReduction<1||settings.TargetBoardReduction>7||
       !std::isfinite(settings.SecondsPerWindow)||settings.SecondsPerWindow<=0||settings.SecondsPerWindow>60||
       settings.Workers<1||settings.Workers>8||settings.MaxConflicts<1||settings.MaxCompactionPasses<1||settings.MaxCompactionPasses>8)
        throw std::invalid_argument("Invalid optional convex tail reconstruction settings");
    return {"convex.tail-repack",[settings]{return std::make_unique<ConvexTailRepack>(settings);}};
}
}
