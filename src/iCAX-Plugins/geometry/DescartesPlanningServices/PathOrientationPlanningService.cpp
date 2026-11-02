#include <GeometryServices/PathOrientationPlanningService.h>
#include <Services/ServicesHelper.h>
#include <descartes_light/solvers/ladder_graph/ladder_graph_solver.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>

namespace {
using namespace iCAX::GeometryServices;
using Clock=std::chrono::steady_clock;
using Key=std::pair<std::size_t,std::size_t>;
using EdgeKey=std::tuple<std::size_t,std::size_t,std::size_t>;
constexpr double Pi=3.14159265358979323846;
double Milliseconds(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
double Angle(const OrientationVector& a,const OrientationVector& b){
    double dot=0,cross=0;for(int i=0;i<3;++i){dot+=a[i]*b[i];const auto c=a[(i+1)%3]*b[(i+2)%3]-a[(i+2)%3]*b[(i+1)%3];cross+=c*c;}
    return std::atan2(std::sqrt(cross),std::clamp(dot,-1.,1.));
}
void ValidatePose(const PathOrientation& p,std::size_t axes){
    double norm=0;for(auto x:p.Direction){if(!std::isfinite(x))throw std::invalid_argument("ORIENTATION_DIRECTION_INVALID");norm+=x*x;}
    if(std::abs(norm-1)>1e-8||p.Axes.size()!=axes)throw std::invalid_argument("ORIENTATION_CONFIGURATION_INVALID");
    for(auto x:p.Axes)if(!std::isfinite(x))throw std::invalid_argument("ORIENTATION_AXIS_INVALID");
}
void Validate(const PathOrientationQuery& q){
    if(q.Waypoints.size()<2||q.Waypoints.size()>32768||!q.CheckState||!q.CheckMotion||
       !std::isfinite(q.MaximumSeconds)||q.MaximumSeconds<=0||q.MaximumSeconds>600||
       !std::isfinite(q.MaximumDirectionStepRadians)||q.MaximumDirectionStepRadians<=0||q.MaximumDirectionStepRadians>Pi||
       !std::isfinite(q.DeviationWeight)||q.DeviationWeight<0||!std::isfinite(q.DirectionChangeWeight)||q.DirectionChangeWeight<0||
       !q.MaximumGraphEdges||q.MaximumGraphEdges>25000000||!q.MaximumSearchIterations||q.MaximumSearchIterations>10000)
        throw std::invalid_argument("ORIENTATION_QUERY_INVALID");
    for(const auto& axis:q.Axes)if(!std::isfinite(axis.Minimum)||!std::isfinite(axis.Maximum)||axis.Minimum>=axis.Maximum||
        !std::isfinite(axis.MaximumStep)||axis.MaximumStep<=0||!std::isfinite(axis.Weight)||axis.Weight<0)throw std::invalid_argument("ORIENTATION_AXIS_LIMIT_INVALID");
    std::size_t last=0;bool first=true;
    for(const auto& range:q.EditableRanges){if(range.First>=range.Last||range.Last>=q.Waypoints.size()||(!first&&range.First<last))throw std::invalid_argument("ORIENTATION_RANGE_INVALID");last=range.Last;first=false;}
    for(const auto& p:q.Waypoints){ValidatePose(p.Original,q.Axes.size());for(auto x:p.Position)if(!std::isfinite(x))throw std::invalid_argument("ORIENTATION_POSITION_INVALID");
        if(!std::isfinite(p.MaximumDeviationRadians)||p.MaximumDeviationRadians<0||p.MaximumDeviationRadians>Pi||p.Candidates.size()>4096)throw std::invalid_argument("ORIENTATION_CONE_INVALID");
        for(const auto& c:p.Candidates)ValidatePose(c,q.Axes.size());}
}
struct Context {
    const PathOrientationQuery& Q;const std::function<bool()>& Cancel;
    PathOrientationResult R;Clock::time_point Started=Clock::now();
    std::vector<std::vector<PathOrientation>> Nodes;
    std::set<Key> BadStates,GoodStates;
    std::set<EdgeKey> BadEdges,GoodEdges;
    bool Check(){if(Cancel&&Cancel()){R.Status="Cancelled";return false;}if(Milliseconds(Started)>=Q.MaximumSeconds*1000){R.Status="Timeout";return false;}return true;}
    bool Editable(std::size_t i)const{if(Q.Waypoints[i].Locked)return false;if(Q.AllWaypointsEditable)return true;for(const auto& r:Q.EditableRanges)if(i>r.First&&i<r.Last)return true;return false;}
    bool Limits(const PathOrientation& c)const{for(std::size_t j=0;j<Q.Axes.size();++j)if(c.Axes[j]<Q.Axes[j].Minimum||c.Axes[j]>Q.Axes[j].Maximum)return false;return true;}
    bool Prepare(){
        Nodes.resize(Q.Waypoints.size());std::size_t edges=0;
        for(std::size_t i=0;i<Nodes.size();++i){if(!Check())return false;const auto& p=Q.Waypoints[i];auto& nodes=Nodes[i];
            if(Limits(p.Original))nodes.push_back(p.Original);
            if(Editable(i))for(const auto& c:p.Candidates){
                if(!Limits(c)||Angle(p.Original.Direction,c.Direction)>p.MaximumDeviationRadians+1e-12)continue;
                if(std::none_of(nodes.begin(),nodes.end(),[&](const auto& n){return n.Direction==c.Direction&&n.Axes==c.Axes;}))nodes.push_back(c);
            }
            R.CandidateCount+=nodes.size();
            if(nodes.empty()){R.Status="NoSolution";R.FailedWaypoint=i;return false;}
            if(i){edges+=Nodes[i-1].size()*nodes.size();if(edges>Q.MaximumGraphEdges){R.Status="GraphBudgetExceeded";return false;}}
        }
        return true;
    }
    std::pair<bool,double> Edge(std::size_t i,std::size_t a,std::size_t b)const{
        if(BadEdges.contains({i,a,b}))return {false,0};const auto& x=Nodes[i][a];const auto& y=Nodes[i+1][b];const auto theta=Angle(x.Direction,y.Direction);
        if(theta>Q.MaximumDirectionStepRadians+1e-12)return {false,0};double cost=Q.DirectionChangeWeight*theta*theta;
        for(std::size_t j=0;j<Q.Axes.size();++j){const auto delta=y.Axes[j]-x.Axes[j];if(std::abs(delta)>Q.Axes[j].MaximumStep+1e-12)return {false,0};cost+=Q.Axes[j].Weight*delta*delta;}
        return {true,cost};
    }
};
class Sampler final:public descartes_light::WaypointSamplerD {
    const Context& C;std::size_t I;
public:
    Sampler(const Context& c,std::size_t i):C(c),I(i){}
    std::vector<descartes_light::StateSample<double>> sample()const override{
        std::vector<descartes_light::StateSample<double>> out;
        for(std::size_t j=0;j<C.Nodes[I].size();++j)if(!C.BadStates.contains({I,j})){
            const auto theta=Angle(C.Q.Waypoints[I].Original.Direction,C.Nodes[I][j].Direction);
            out.emplace_back(std::make_shared<descartes_light::State<double>>(std::initializer_list<double>{static_cast<double>(j)}),C.Q.DeviationWeight*theta*theta);
        }
        return out;
    }
};
class EdgeEvaluator final:public descartes_light::EdgeEvaluatorD {
    const Context& C;std::size_t I;
public:
    EdgeEvaluator(const Context& c,std::size_t i):C(c),I(i){}
    std::pair<bool,double> evaluate(const descartes_light::State<double>& a,const descartes_light::State<double>& b)const override{
        return C.Edge(I,static_cast<std::size_t>(a.values[0]),static_cast<std::size_t>(b.values[0]));
    }
};
class Service final:public IPathOrientationPlanningService {
    AUTO_REGIST_SERVICE(IPathOrientationPlanningService,Service)
public:
    void OnLoad()override{}void OnUnload()override{}
    PathOrientationResult Plan(const PathOrientationQuery& q,const std::function<bool()>& cancelled)const override{
        Validate(q);Context c{q,cancelled};
        const auto finish=[&](){c.R.ElapsedMs=Milliseconds(c.Started);if(c.R.Status!="Solved")c.R.Orientations.clear();return c.R;};
        if(!c.Prepare())return finish();
        std::vector<descartes_light::WaypointSamplerD::ConstPtr> samplers;
        std::vector<descartes_light::EdgeEvaluatorD::ConstPtr> evaluators;
        for(std::size_t i=0;i<c.Nodes.size();++i){samplers.push_back(std::make_shared<Sampler>(c,i));if(i+1<c.Nodes.size())evaluators.push_back(std::make_shared<EdgeEvaluator>(c,i));}
        // Lazy certification: inexpensive optimistic edges are searched by the
        // UPSTREAM Descartes DAG solver. Only selected states/edges ask the
        // geometry owner for expensive checks. A failed state/edge is removed
        // and the graph is solved again. Never publish an optimistic result.
        std::size_t priority=0;
        for(std::size_t iteration=0;iteration<q.MaximumSearchIterations;++iteration){
            if(!c.Check())return finish();const auto graphStart=Clock::now();++c.R.GraphBuilds;
            // Geometry callbacks run below, outside OpenMP, on this worker.
            // One graph thread also makes candidate tie ordering reproducible.
            descartes_light::LadderGraphSolverD solver(1);const auto status=solver.build(samplers,evaluators,{});
            if(!status){c.R.GraphMs+=Milliseconds(graphStart);c.R.Status="NoSolution";
                if(!status.failed_vertices.empty())c.R.FailedWaypoint=status.failed_vertices.front();if(!status.failed_edges.empty())c.R.FailedEdge=status.failed_edges.front();return finish();}
            descartes_light::SearchResult<double> selected;
            try{selected=solver.search();}catch(const std::runtime_error&){c.R.GraphMs+=Milliseconds(graphStart);c.R.Status="NoSolution";return finish();}
            c.R.GraphMs+=Milliseconds(graphStart);if(!c.Check())return finish();bool retry=false;
            // A changed route must first clear the connection that rejected
            // its predecessor. Do not certify a fresh expensive prefix only
            // to rediscover the same late bottleneck. Both endpoint states
            // and every selected edge remain mandatory before publication.
            std::vector<std::size_t> order{priority};
            for(std::size_t i=0;i<selected.trajectory.size();++i)if(i!=priority)order.push_back(i);
            const auto certifyState=[&](std::size_t i){
                const auto a=static_cast<std::size_t>(selected.trajectory[i]->values[0]);const Key state{i,a};
                if(c.GoodStates.contains(state))return true;if(!c.Check())return false;
                const auto stamp=Clock::now();++c.R.StateChecks;const auto good=q.CheckState(i,c.Nodes[i][a]);c.R.ValidationMs+=Milliseconds(stamp);
                if(!c.Check())return false;if(!good){c.BadStates.insert(state);c.R.FailedWaypoint=i;priority=i;return false;}c.GoodStates.insert(state);return true;
            };
            for(const auto i:order){
                if((i&&!certifyState(i-1))||!certifyState(i)){if(!c.Check())return finish();retry=true;break;}
                const auto a=static_cast<std::size_t>(selected.trajectory[i]->values[0]);
                if(i){const auto b=static_cast<std::size_t>(selected.trajectory[i-1]->values[0]);const EdgeKey edge{i-1,b,a};
                    if(!c.GoodEdges.contains(edge)){if(!c.Check())return finish();const auto stamp=Clock::now();++c.R.MotionChecks;const auto good=q.CheckMotion(i-1,c.Nodes[i-1][b],c.Nodes[i][a]);c.R.ValidationMs+=Milliseconds(stamp);
                        if(!c.Check())return finish();if(!good){c.BadEdges.insert(edge);c.R.FailedEdge=i-1;priority=i;retry=true;break;}c.GoodEdges.insert(edge);}}
            }
            if(!retry){c.R.Status="Solved";c.R.Cost=selected.cost;c.R.FailedWaypoint=c.R.FailedEdge=std::numeric_limits<std::size_t>::max();
                for(std::size_t i=0;i<selected.trajectory.size();++i)c.R.Orientations.push_back(c.Nodes[i][static_cast<std::size_t>(selected.trajectory[i]->values[0])]);return finish();}
        }
        c.R.Status="SearchBudgetExceeded";return finish();
    }
};
}
