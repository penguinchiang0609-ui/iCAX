#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <functional>
#include <map>

namespace iCAX::FiveAxisCAM::Ordering
{
    // Product-owned machining dependencies. Geometry services do not own these rules.
    struct Cut
    {
        std::string ID, Placement;
        std::size_t Number=0;
        int Phase=1; // marking, cutting, release trim, within each placement
        bool Enabled=true;
        bool Sortable=true;
        std::array<double,3> Start{},End{},StartDirection{0,0,1},EndDirection{0,0,1};
        std::array<double,3> Center{};
    };
    struct Node { std::vector<std::size_t> Children,Leaves; bool Locked=false,Anchored=false; };
    struct Limits { double LinearSpeed=100,LinearAcceleration=1000,AngularSpeed=1,AngularAcceleration=2,Lift=10; bool Retract=true; };
    inline double Distance(const std::array<double,3>& a,const std::array<double,3>& b)
    { return std::hypot(a[0]-b[0],a[1]-b[1],a[2]-b[2]); }
    inline double MotionTime(double length,double speed,double acceleration)
    { return length<=speed*speed/acceleration?2*std::sqrt(length/acceleration):length/speed+speed/acceleration; }
    inline double Travel(const Cut& a,const Cut& b,const Limits& limits)
    {
        double dot=0,na=0,nb=0;for(int i=0;i<3;++i){dot+=a.EndDirection[i]*b.StartDirection[i];na+=a.EndDirection[i]*a.EndDirection[i];nb+=b.StartDirection[i]*b.StartDirection[i];}
        const auto angle=std::acos(std::clamp(dot/std::sqrt(na*nb),-1.,1.));
        auto from=a.End,to=b.Start;
        const bool retract=limits.Retract||a.Placement!=b.Placement;
        if(retract)for(int i=0;i<3;++i){from[i]+=limits.Lift*a.EndDirection[i];to[i]+=limits.Lift*b.StartDirection[i];}
        // Sorting estimate only. Selected rapid strategy is not changed;
        // collision checking later inspects the actual generated route.
        return std::max(MotionTime(Distance(from,to),limits.LinearSpeed,limits.LinearAcceleration),MotionTime(angle,limits.AngularSpeed,limits.AngularAcceleration))
            +(retract?2*MotionTime(limits.Lift,limits.LinearSpeed,limits.LinearAcceleration):0);
    }
    struct Solver
    {
        std::vector<Cut> Cuts;
        std::vector<Node> Nodes;
        Limits Settings;
        bool CombineMarkAndHole=false;
        bool GlobalProcess=false,DistanceObjective=false,UseOrigin=false,Bottom=true,Right=false,Inward=true,Clockwise=true;
        std::string Pattern="Smart";
        unsigned Skip=0;
        double HeightPenaltyWeight=0;
        std::array<double,3> PlaneX{1,0,0},PlaneY{0,1,0},Origin{},OriginDirection{0,0,1};
        std::vector<std::vector<bool>> SpatialEdges;
        std::vector<std::vector<double>> Costs;
        std::size_t Expanded=0,Refinements=0;
        std::function<bool()> Cancelled;
        void CheckCancelled() const {if(Cancelled&&Cancelled())throw std::runtime_error("CAM_ORDER_CANCELLED");}
        bool Before(std::size_t a,std::size_t b) const
        { const auto phase=[&](const Cut& cut){return CombineMarkAndHole&&cut.Phase<2?0:cut.Phase;};
          if(!Cuts[a].Enabled||!Cuts[b].Enabled)return false;
          return ((GlobalProcess||Cuts[a].Placement==Cuts[b].Placement)&&phase(Cuts[a])<phase(Cuts[b]))
              ||(!SpatialEdges.empty()&&SpatialEdges[a][b]); }
        double Connection(std::size_t from,std::size_t to) const
        {
            if(to>=Cuts.size())return 0;
            if(from<Cuts.size())return Costs[from][to];
            if(from!=Cuts.size()+1||!UseOrigin)return 0;
            Cut origin;origin.End=Origin;origin.EndDirection=OriginDirection;origin.Placement=Cuts[to].Placement;
            return Measure(origin,Cuts[to]);
        }
        double BaseMeasure(const Cut& a,const Cut& b) const
        {
            if(!DistanceObjective)return Travel(a,b,Settings);
            auto from=a.End,to=b.Start;const bool retract=Settings.Retract||a.Placement!=b.Placement;
            if(retract)for(int k=0;k<3;++k){from[k]+=Settings.Lift*a.EndDirection[k];to[k]+=Settings.Lift*b.StartDirection[k];}
            return Distance(from,to)+(retract?2*Settings.Lift:0);
        }
        double Measure(const Cut& a,const Cut& b) const
        {
            // A soft preference in setup Z, independent of the display/sorting
            // plane. It never adds a precedence edge or certifies clearance.
            const auto penalty=HeightPenaltyWeight*std::abs(a.End[2]-b.Start[2]);
            return BaseMeasure(a,b)+(DistanceObjective?penalty:penalty/Settings.LinearSpeed);
        }
        void PrepareSpatial()
        {
            SpatialEdges.assign(Cuts.size(),std::vector<bool>(Cuts.size()));
            if(Pattern=="Smart"&&!Skip)return;
            struct Point {std::size_t Index;double X,Y;};
            std::map<std::pair<std::string,int>,std::vector<Point>> scopes;
            for(std::size_t i=0;i<Cuts.size();++i)if(Cuts[i].Enabled&&Cuts[i].Sortable){double x=0,y=0;for(int k=0;k<3;++k){x+=Cuts[i].Center[k]*PlaneX[k];y+=Cuts[i].Center[k]*PlaneY[k];}
                scopes[{Cuts[i].Placement,CombineMarkAndHole&&Cuts[i].Phase<2?0:Cuts[i].Phase}].push_back({i,x,y});}
            for(auto& [_,points]:scopes){CheckCancelled();if(points.size()<2)continue;
                double loX=points[0].X,hiX=loX,loY=points[0].Y,hiY=loY;
                for(const auto& p:points){loX=std::min(loX,p.X);hiX=std::max(hiX,p.X);loY=std::min(loY,p.Y);hiY=std::max(hiY,p.Y);}
                const auto identity=[&](const Point& a,const Point& b){return std::tie(Cuts[a.Index].Number,Cuts[a.Index].ID)<std::tie(Cuts[b.Index].Number,Cuts[b.Index].ID);};
                std::vector<std::vector<Point>> bands;
                if(Pattern=="Ring"){
                    const auto radius=[&](const Point& p){return std::max(std::abs(p.X-(loX+hiX)/2)/std::max(1e-6,(hiX-loX)/2),std::abs(p.Y-(loY+hiY)/2)/std::max(1e-6,(hiY-loY)/2));};
                    std::sort(points.begin(),points.end(),[&](const auto& a,const auto& b){return radius(a)!=radius(b)?radius(a)>radius(b):identity(a,b);});
                    double last=-1;for(const auto& p:points){if(bands.empty()||std::abs(radius(p)-last)>.15){bands.push_back({});last=radius(p);}bands.back().push_back(p);}
                    if(!Inward)std::reverse(bands.begin(),bands.end());
                    for(auto& band:bands)std::sort(band.begin(),band.end(),[&](const auto& a,const auto& b){const auto angle=[&](const auto& p){return std::atan2(p.X-(loX+hiX)/2,p.Y-(loY+hiY)/2);};const auto aa=angle(a),bb=angle(b);return aa!=bb?(Clockwise?aa<bb:aa>bb):identity(a,b);});
                }else{
                    std::sort(points.begin(),points.end(),[&](const auto& a,const auto& b){return a.Y!=b.Y?(Bottom?a.Y<b.Y:a.Y>b.Y):identity(a,b);});
                    const auto tolerance=std::max(1e-6,(hiY-loY)*.04);double last=0;
                    for(const auto& p:points){if(bands.empty()||std::abs(p.Y-last)>tolerance){bands.push_back({});last=p.Y;}bands.back().push_back(p);}
                    for(std::size_t row=0;row<bands.size();++row){const bool reverse=Right^((Pattern=="Snake")&&row%2);std::sort(bands[row].begin(),bands[row].end(),[&](const auto& a,const auto& b){return a.X!=b.X?(reverse?a.X>b.X:a.X<b.X):identity(a,b);});}
                    if(Pattern=="Smart"){std::vector<Point> all;for(const auto& band:bands)all.insert(all.end(),band.begin(),band.end());bands={std::move(all)};}
                }
                std::vector<std::size_t> order;for(const auto& band:bands)for(unsigned offset=0;offset<=Skip;++offset)for(std::size_t i=offset;i<band.size();i+=Skip+1)order.push_back(band[i].Index);
                for(std::size_t a=0;a<order.size();++a)for(std::size_t b=a+1;b<order.size();++b)SpatialEdges[order[a]][order[b]]=true;
            }
        }
        void Prepare()
        {
            CheckCancelled();
            PrepareSpatial();
            for(auto value:{Settings.LinearSpeed,Settings.LinearAcceleration,Settings.AngularSpeed,Settings.AngularAcceleration})
                if(!std::isfinite(value)||value<=0)throw std::invalid_argument("CAM_ORDER_DYNAMICS_INVALID");
            if(!std::isfinite(Settings.Lift)||Settings.Lift<0)throw std::invalid_argument("CAM_ORDER_DYNAMICS_INVALID");
            if(!std::isfinite(HeightPenaltyWeight))throw std::invalid_argument("CAM_ORDER_HEIGHT_PENALTY_INVALID");
            for(const auto& cut:Cuts)if(cut.Enabled){for(const auto& v:{cut.Start,cut.End,cut.StartDirection,cut.EndDirection})for(auto x:v)
                if(!std::isfinite(x))throw std::invalid_argument("CAM_ORDER_ENDPOINT_INVALID");
                if(Distance(cut.StartDirection,{})<1e-9||Distance(cut.EndDirection,{})<1e-9)throw std::invalid_argument("CAM_ORDER_ENDPOINT_INVALID");}
            Costs.assign(Cuts.size(),std::vector<double>(Cuts.size()));
            for(std::size_t a=0;a<Cuts.size();++a)for(std::size_t b=0;b<Cuts.size();++b)if(a!=b&&Cuts[a].Enabled&&Cuts[b].Enabled)Costs[a][b]=Measure(Cuts[a],Cuts[b]);
        }
        std::size_t First(const std::vector<std::size_t>& path) const
        { for(auto i:path)if(Cuts[i].Enabled)return i;return Cuts.size(); }
        std::size_t Last(const std::vector<std::size_t>& path) const
        { for(auto i=path.rbegin();i!=path.rend();++i)if(Cuts[*i].Enabled)return *i;return Cuts.size(); }
        double Score(const std::vector<std::size_t>& path,std::size_t entry,std::size_t exit) const
        {
            double cost=0;auto previous=entry;
            for(auto i:path)if(Cuts[i].Enabled){cost+=Connection(previous,i);previous=i;}
            cost+=Connection(previous,exit);return cost;
        }
        bool Valid(const std::vector<std::size_t>& path) const
        {
            for(std::size_t a=0;a<path.size();++a)for(std::size_t b=a+1;b<path.size();++b)if(Before(path[b],path[a]))return false;
            return true;
        }
        std::vector<std::size_t> Solve(std::size_t index,std::size_t entry,std::size_t exit)
        {
            CheckCancelled();
            auto& node=Nodes[index];
            if(node.Children.empty()||node.Locked){if(!Valid(node.Leaves))throw std::invalid_argument("CAM_ORDER_DEPENDENCY_CONFLICT: locked group contains a release before its prerequisite");return node.Leaves;}
            if(node.Children.size()==1)return Solve(node.Children.front(),entry,exit);
            const auto count=node.Children.size();std::vector<std::vector<std::size_t>> paths;
            for(auto child:node.Children)paths.push_back(Solve(child,Cuts.size(),Cuts.size()));
            std::vector<std::vector<bool>> edge(count,std::vector<bool>(count));
            for(std::size_t a=0;a<count;++a)for(std::size_t b=0;b<count;++b)if(a!=b){
                for(auto x:paths[a])for(auto y:paths[b])if(Before(x,y))edge[a][b]=true;
                // No unit may cross a locked sibling. Descendant locks anchor the whole enclosing unit.
                if(a<b&&(Nodes[node.Children[a]].Anchored||Nodes[node.Children[b]].Anchored))edge[a][b]=true;
            }
            std::vector<std::vector<std::size_t>> prerequisites(count);
            for(std::size_t a=0;a<count;++a)for(std::size_t b=0;b<count;++b)if(edge[a][b])prerequisites[b].push_back(a);
            std::vector<std::pair<std::size_t,std::string>> keys(count,{std::numeric_limits<std::size_t>::max(),{}});
            for(std::size_t i=0;i<count;++i)for(auto leaf:paths[i])keys[i]=std::min(keys[i],std::pair{Cuts[leaf].Number,Cuts[leaf].ID});
            const auto sequenceLess=[&](const auto& a,const auto& b){return std::lexicographical_compare(a.begin(),a.end(),b.begin(),b.end(),[&](auto x,auto y){return keys[x]<keys[y];});};
            const auto flatten=[&](const auto& sequence){std::vector<std::size_t> path;path.reserve(node.Leaves.size());for(auto i:sequence)path.insert(path.end(),paths[i].begin(),paths[i].end());return path;};
            std::vector<std::size_t> first(count),last(count);std::vector<double> internal(count);
            for(std::size_t i=0;i<count;++i){first[i]=First(paths[i]);last[i]=Last(paths[i]);internal[i]=Score(paths[i],Cuts.size(),Cuts.size());}
            const auto score=[&](const auto& sequence){double cost=0;auto previous=entry;for(auto i:sequence)if(first[i]<Cuts.size()){
                cost+=internal[i]+Connection(previous,first[i]);previous=last[i];}cost+=Connection(previous,exit);return cost;};
            const auto valid=[&](const auto& sequence){std::vector<std::size_t> position(count);for(std::size_t i=0;i<count;++i)position[sequence[i]]=i;
                for(std::size_t a=0;a<count;++a)for(std::size_t b=0;b<count;++b)if(edge[a][b]&&position[a]>position[b])return false;return true;};
            struct State { std::vector<std::size_t> Sequence;std::vector<bool> Used;double Cost=0;std::size_t Last; };
            std::vector<State> beam{{{},std::vector<bool>(count),0,entry}};
            for(std::size_t depth=0;depth<count;++depth){std::vector<State> next;
                CheckCancelled();
                for(const auto& state:beam){std::vector<std::tuple<double,std::pair<std::size_t,std::string>,std::size_t>> choices;
                for(std::size_t candidate=0;candidate<count;++candidate){if(state.Used[candidate])continue;
                    bool ready=true;for(auto p:prerequisites[candidate])if(!state.Used[p]){ready=false;break;}if(!ready)continue;
                    choices.emplace_back(Score(paths[candidate],state.Last,depth+1==count?exit:Cuts.size()),keys[candidate],candidate);}
                const auto branches=std::min<std::size_t>(choices.size(),count>64?12:32);std::partial_sort(choices.begin(),choices.begin()+branches,choices.end());
                for(std::size_t k=0;k<branches;++k){const auto [cost,key,candidate]=choices[k];State s=state;s.Used[candidate]=true;s.Sequence.push_back(candidate);s.Cost+=cost;
                    if(last[candidate]<Cuts.size())s.Last=last[candidate];next.push_back(std::move(s));++Expanded;}}
                if(next.empty())throw std::invalid_argument("CAM_ORDER_DEPENDENCY_CONFLICT: groups or locks prevent machining prerequisites");
                std::sort(next.begin(),next.end(),[&](const auto& a,const auto& b){return a.Cost!=b.Cost?a.Cost<b.Cost:sequenceLess(a.Sequence,b.Sequence);});
                const std::size_t width=count>64?12:32;if(next.size()>width)next.resize(width);beam=std::move(next);
            }
            auto best=beam.front().Sequence;auto bestCost=score(best);
            // Keep a feasible existing route if beam pruning lost a better solution.
            std::vector<std::size_t> existing;for(std::size_t i=0;i<count;++i)existing.push_back(i);
            if(valid(existing)){const auto cost=score(existing);if(cost<=bestCost+1e-9){best=existing;bestCost=cost;}}
            // Directed relocate improvement includes both boundary connections and respects the full DAG.
            const bool nonempty=std::all_of(first.begin(),first.end(),[&](auto i){return i<Cuts.size();});
            const auto link=[&](std::size_t a,std::size_t b){return Connection(a==count?entry:last[a],b==count?exit:first[b]);};
            for(int pass=0;pass<4;++pass){bool improved=false;
                CheckCancelled();
                for(std::size_t from=0;from<count;++from)for(std::size_t to=0;to<count;++to){if(from==to)continue;const auto item=best[from];
                    bool allowed=true;for(std::size_t k=std::min(from,to);k<=std::max(from,to);++k)if(k!=from&&(from<to?edge[item][best[k]]:edge[best[k]][item])){allowed=false;break;}
                    if(!allowed)continue;double cost=bestCost;
                    if(nonempty){const auto prev=from?best[from-1]:count,succ=from+1<count?best[from+1]:count;
                        const auto at=[&](std::size_t pos){return best[pos>=from?pos+1:pos];};const auto before=to?at(to-1):count,after=to+1<count?at(to):count;
                        cost+=link(prev,succ)-link(prev,item)-link(item,succ)+link(before,item)+link(item,after)-link(before,after);
                        if(cost+1e-9>=bestCost)continue;}
                    auto trial=best;trial.erase(trial.begin()+from);trial.insert(trial.begin()+to,item);if(!nonempty)cost=score(trial);
                    if(cost+1e-9<bestCost){best=std::move(trial);bestCost=cost;improved=true;}}
                if(!improved)break;
            }
            // Revisit each group's internal route with its actual predecessor and successor.
            for(std::size_t i=0;i<best.size();++i){auto before=entry,after=exit;
                for(std::size_t j=0;j<i;++j){const auto p=Last(paths[best[j]]);if(p<Cuts.size())before=p;}
                for(std::size_t j=i+1;j<best.size();++j){const auto p=First(paths[best[j]]);if(p<Cuts.size()){after=p;break;}}
                const auto child=node.Children[best[i]];if(!Nodes[child].Children.empty()&&!Nodes[child].Locked&&Refinements<32){++Refinements;auto refined=Solve(child,before,after);
                    if(Score(refined,before,after)+1e-9<Score(paths[best[i]],before,after))paths[best[i]]=std::move(refined);}
            }
            auto result=flatten(best);if(!Valid(result))throw std::invalid_argument("CAM_ORDER_DEPENDENCY_CONFLICT");
            if(Valid(node.Leaves)&&Score(node.Leaves,entry,exit)<=Score(result,entry,exit)+1e-9)return node.Leaves;
            return result;
        }
    };
}
