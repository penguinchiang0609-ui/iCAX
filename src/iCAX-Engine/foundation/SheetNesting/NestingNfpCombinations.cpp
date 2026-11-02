#include "NestingNfpCombinations.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    struct State { Pattern Value;std::vector<std::size_t> Demand;double Fill=0; };
    void NormalizeLight(Pattern& pattern,const Problem& p)
    {
        double x0=std::numeric_limits<double>::infinity(),y0=x0,x1=-x0,y1=-x0;pattern.MaterialArea=0;
        for(const auto& member:pattern.Members)
        {
            const auto b=Bounds(p.Parts[member.PartIndex].Geometry,member.Transform);
            x0=std::min(x0,b.X);y0=std::min(y0,b.Y);x1=std::max(x1,b.X+b.Width);y1=std::max(y1,b.Y+b.Height);
            pattern.MaterialArea+=Area(p.Parts[member.PartIndex].Geometry);
        }
        for(auto& member:pattern.Members){member.Transform.X-=x0;member.Transform.Y-=y0;member.Transform.AngleDegrees=NormalizeAngle(member.Transform.AngleDegrees);}
        pattern.Width=x1-x0;pattern.Height=y1-y0;
    }
    std::string Signature(const State& state)
    {
        std::string s;for(std::size_t i=0;i<state.Demand.size();++i)if(state.Demand[i])s+=std::to_string(i)+":"+std::to_string(state.Demand[i])+";";
        return s;
    }
    std::string GeometryKey(const State& state,double tolerance)
    {
        std::vector<std::tuple<std::size_t,long long,long long,long long>> values;
        const auto step=std::max(tolerance,1e-7);
        for(const auto& m:state.Value.Members)values.emplace_back(m.PartIndex,std::llround(m.Transform.X/step),
            std::llround(m.Transform.Y/step),std::llround(NormalizeAngle(m.Transform.AngleDegrees)*1e6));
        std::sort(values.begin(),values.end());std::ostringstream out;
        for(const auto& [i,x,y,a]:values)out<<i<<':'<<x<<':'<<y<<':'<<a<<';';
        return out.str();
    }
}
std::vector<Pattern> BuildNfpCombinationPatterns(const Problem& p,NfpCache& cache,
    const std::vector<NfpPreparedShape>& prepared,const NfpCombinationOptions& o,std::size_t& attempts)
{
    std::vector<Pattern> output;
    if(p.PartGap!=0||p.Stocks.size()!=1||prepared.size()!=p.Parts.size()||o.MaxMembers<2||!o.MaxResults||!o.MaxStates||!o.MaxAttempts)return output;
    const auto stopped=[&]{return attempts>=o.MaxAttempts||(o.Stop&&o.Stop());};
    std::vector<std::vector<double>> angles(p.Parts.size());
    for(std::size_t i=0;i<p.Parts.size();++i)if(p.Parts[i].Quantity&&prepared[i])angles[i]=CandidateAngles(p.Parts[i],p.Tolerance);
    std::vector<State> beam;
    for(std::size_t ai=0;!stopped();++ai)
    {
        bool any=false;
        for(std::size_t i=0;i<angles.size()&&beam.size()<o.MaxStates;++i)if(ai<angles[i].size())
        {
            any=true;State s;s.Demand.resize(p.Parts.size());s.Demand[i]=1;s.Value.Members.push_back({i,{0,0,angles[i][ai]}});
            NormalizeLight(s.Value,p);if(s.Value.Width<=0||s.Value.Height<=0)continue;
            s.Fill=s.Value.MaterialArea/(s.Value.Width*s.Value.Height);beam.push_back(std::move(s));
        }
        if(!any||beam.size()>=o.MaxStates)break;
    }
    std::set<std::string> seen;
    // Leave a small explicit budget for repeating a dense pair as a 2-by-2 unit.
    const auto repeatReserve=std::min<std::size_t>(128,o.MaxAttempts/10);
    const auto mainLimit=o.MaxAttempts-repeatReserve;
    for(std::size_t depth=2;depth<=std::min<std::size_t>(4,o.MaxMembers)&&!beam.empty()&&!stopped();++depth)
    {
        const auto maxDepth=std::min<std::size_t>(4,o.MaxMembers);
        // Reserve work for later depths: exhausting the entire budget on
        // pairs prevents sparse intermediate states from ever closing.
        const auto stageBudget=(mainLimit>attempts?mainLimit-attempts:0)/(maxDepth-depth+1);
        const auto stageEnd=attempts+std::max<std::size_t>(1,stageBudget);
        const auto stageStopped=[&]{return stopped()||attempts>=mainLimit||attempts>=stageEnd;};
        const auto stageLimit=std::min(o.MaxResults,(o.MaxResults*(depth-1)+(maxDepth-2))/(maxDepth-1));
        std::vector<State> next,finalCandidates;
        for(const auto& state:beam)
        {
            if(stageStopped())break;
            // Give each seed a chance, and try its own specification first.
            // Otherwise early input specifications consume the entire stage.
            const auto stateEnd=std::min(stageEnd,attempts+std::max<std::size_t>(1,stageBudget/beam.size()));
            const auto stateStopped=[&]{return stageStopped()||attempts>=stateEnd;};
            const auto first=state.Value.Members.front().PartIndex;
            const auto material=p.Parts[first].Material;
            for(std::size_t pass=0;pass<p.Parts.size()&&!stateStopped();++pass)
            {
                const auto j=(first+pass)%p.Parts.size();
                if(!prepared[j]||p.Parts[j].Material!=material||state.Demand[j]>=p.Parts[j].Quantity)continue;
                for(const auto angle:angles[j])
                {
                    if(stateStopped())break;
                    for(const auto& anchor:state.Value.Members)
                    {
                        if(stateStopped())break;
                        NfpBuildOptions build;build.Cancel=o.Stop;
                        const auto nfp=cache.Get(prepared[anchor.PartIndex],anchor.Transform.AngleDegrees,prepared[j],angle,build);
                        if(nfp.State!=NfpStatus::Ready||!nfp.Region)continue;
                        for(const auto& loop:nfp.Region->Loops)for(std::size_t edge=0;edge<loop.size();++edge)
                        {
                            const auto a=Transform(loop[edge],nfp.Transform);
                            const auto b=Transform(loop[(edge+1)%loop.size()],nfp.Transform);
                            std::vector<double> cuts{0,.5};
                            // Add every bounding-box regime change along this
                            // NFP segment for the entire existing assembly.
                            const auto movingBounds=Bounds(p.Parts[j].Geometry,{0,0,angle});
                            const auto addCut=[&](double v,double start,double end)
                            {
                                if(std::abs(end-start)<=p.Tolerance)return;
                                const auto t=(v-start)/(end-start);if(t>0&&t<1)cuts.push_back(t);
                            };
                            addCut(-movingBounds.X,a.X+anchor.Transform.X,b.X+anchor.Transform.X);
                            addCut(state.Value.Width-movingBounds.X-movingBounds.Width,a.X+anchor.Transform.X,b.X+anchor.Transform.X);
                            addCut(-movingBounds.Y,a.Y+anchor.Transform.Y,b.Y+anchor.Transform.Y);
                            addCut(state.Value.Height-movingBounds.Y-movingBounds.Height,a.Y+anchor.Transform.Y,b.Y+anchor.Transform.Y);
                            // The best contact may lie inside an NFP edge, where
                            // vertices align along an axis, rather than at its end.
                            for(const auto& va:p.Parts[anchor.PartIndex].Geometry.Outer)
                            for(const auto& vb:p.Parts[j].Geometry.Outer)
                            {
                                const auto aa=Transform(va,{0,0,anchor.Transform.AngleDegrees});
                                const auto bb=Transform(vb,{0,0,angle});
                                if(std::abs(b.X-a.X)>p.Tolerance)
                                {const auto t=(aa.X-bb.X-a.X)/(b.X-a.X);if(t>0&&t<1)cuts.push_back(t);}
                                if(std::abs(b.Y-a.Y)>p.Tolerance)
                                {const auto t=(aa.Y-bb.Y-a.Y)/(b.Y-a.Y);if(t>0&&t<1)cuts.push_back(t);}
                            }
                            std::sort(cuts.begin(),cuts.end());
                            cuts.erase(std::unique(cuts.begin(),cuts.end(),[](double x,double y){return std::abs(x-y)<1e-9;}),cuts.end());
                            for(const auto t:cuts)
                            {
                            if(stateStopped())break;++attempts;
                            const Pose pose{a.X+(b.X-a.X)*t+anchor.Transform.X,a.Y+(b.Y-a.Y)*t+anchor.Transform.Y,angle};
                            bool conflict=false;
                            for(const auto& m:state.Value.Members)
                                if(ShapesConflict(p.Parts[m.PartIndex].Geometry,m.Transform,p.Parts[j].Geometry,pose,0,p.Tolerance)){conflict=true;break;}
                            if(conflict)continue;
                            State candidate=state;candidate.Value.Members.push_back({j,pose});++candidate.Demand[j];NormalizeLight(candidate.Value,p);
                            if(candidate.Value.Width<=0||candidate.Value.Height<=0||
                                candidate.Value.Width>p.Stocks[0].Width-2*p.EdgeMargin+p.Tolerance||
                                candidate.Value.Height>p.Stocks[0].Height-2*p.EdgeMargin+p.Tolerance)continue;
                            candidate.Fill=candidate.Value.MaterialArea/(candidate.Value.Width*candidate.Value.Height);
                            if(candidate.Fill<o.MinimumIntermediateFill)continue;
                            const auto key=GeometryKey(candidate,p.Tolerance);if(!seen.insert(key).second)continue;
                            if(candidate.Fill>=o.MinimumFinalFill)finalCandidates.push_back(candidate);
                            if(depth<o.MaxMembers)next.push_back(std::move(candidate));
                            }
                        }
                    }
                }
            }
        }
        // Keep the most compact real geometry for each inventory signature,
        // not merely the first candidate encountered on an NFP edge.
        std::stable_sort(finalCandidates.begin(),finalCandidates.end(),[](const State& a,const State& b){return a.Fill>b.Fill;});
        std::map<std::string,std::size_t> resultsPerDemand;
        for(const auto& candidate:finalCandidates)
        {
            if(output.size()>=stageLimit)break;
            const auto signature=Signature(candidate);if(resultsPerDemand[signature]>=2)continue;
            ++resultsPerDemand[signature];auto pattern=candidate.Value;NormalizePattern(pattern,p);output.push_back(std::move(pattern));
        }
        std::stable_sort(next.begin(),next.end(),[](const State& a,const State& b){return a.Fill>b.Fill;});
        std::map<std::string,std::size_t> survivors;beam.clear();
        for(auto& candidate:next)
        {
            auto& count=survivors[Signature(candidate)];if(count>=4)continue;
            ++count;beam.push_back(std::move(candidate));if(beam.size()>=o.MaxStates)break;
        }
    }
    if(o.MaxMembers>=4)
    {
        const auto initial=output.size();
        for(std::size_t k=0;k<initial&&!stopped()&&output.size()<o.MaxResults;++k)
        {
            const auto pair=output[k];if(pair.Members.size()!=2)continue;
            std::vector<std::size_t> demand(p.Parts.size());
            for(const auto& m:pair.Members)demand[m.PartIndex]+=2;
            bool enough=true;for(std::size_t i=0;i<demand.size();++i)if(demand[i]>p.Parts[i].Quantity)enough=false;
            if(!enough)continue;
            for(int axis=0;axis<2&&!stopped()&&output.size()<o.MaxResults;++axis)
            {
                ++attempts;auto repeated=pair;
                for(auto member:pair.Members)
                {
                    if(axis==0)member.Transform.X+=pair.Width;else member.Transform.Y+=pair.Height;
                    repeated.Members.push_back(member);
                }
                NormalizeLight(repeated,p);
                if(repeated.Width>p.Stocks[0].Width-2*p.EdgeMargin+p.Tolerance||repeated.Height>p.Stocks[0].Height-2*p.EdgeMargin+p.Tolerance)continue;
                bool conflict=false;
                for(std::size_t i=0;i<2&&!conflict;++i)for(std::size_t j=2;j<4;++j)
                    if(ShapesConflict(p.Parts[repeated.Members[i].PartIndex].Geometry,repeated.Members[i].Transform,
                        p.Parts[repeated.Members[j].PartIndex].Geometry,repeated.Members[j].Transform,0,p.Tolerance)){conflict=true;break;}
                if(conflict)continue;
                NormalizePattern(repeated,p);output.push_back(std::move(repeated));
            }
        }
    }
    return output;
}
}

