#include "SheetNestingBeam.h"
#include "NestingNfpPlacement.h"
#include "NestingExecution.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_set>

namespace iCAX::SheetNesting
{
namespace
{
    using namespace Detail;
    using Clock = std::chrono::steady_clock;
    double Cross(Point a, Point b, Point c)
    { return (b.X-a.X)*(c.Y-a.Y)-(b.Y-a.Y)*(c.X-a.X); }
    double HullArea(std::vector<Point> ring)
    {
        std::sort(ring.begin(),ring.end(),[](Point a,Point b){return std::tie(a.X,a.Y)<std::tie(b.X,b.Y);});
        std::vector<Point> hull;
        for(auto p:ring){while(hull.size()>1&&Cross(hull[hull.size()-2],hull.back(),p)<=0)hull.pop_back();hull.push_back(p);}
        const auto lower=hull.size();
        for(auto i=ring.rbegin()+1;i!=ring.rend();++i){while(hull.size()>lower&&Cross(hull[hull.size()-2],hull.back(),*i)<=0)hull.pop_back();hull.push_back(*i);}
        double a=0;for(std::size_t i=1;i<hull.size();++i)a+=hull[i-1].X*hull[i].Y-hull[i].X*hull[i-1].Y;
        return std::abs(a)*.5;
    }
    std::uint64_t Mix(std::uint64_t x)
    { x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31); }
    struct Direction
    {
        double Angle=0;
        Rect Bounds;
        std::vector<Point> Ring;
    };
    struct Node
    {
        Layout Board;
        std::vector<std::size_t> Used;
        std::vector<double> Profile;
        std::vector<std::pair<std::size_t,Pose>> VerifiedNext;
        // Original-material samples guide diversity only. They never decide
        // whether a placement fits or whether two polygons collide.
        std::array<std::uint64_t,8> MaterialSamples{};
        double Material=0,Hull=0,MaxLeft=0,Right=0,Top=0,Front=0,Score=0;
        std::uint64_t Fingerprint=0;
    };
    class Beam final : public IStrategy
    {
        NfpBeamSettings Config;
        std::shared_ptr<NfpCache> Cache;
        const Problem* P=nullptr;
        const Options* O=nullptr;
        Clock::time_point Start;
        std::size_t Attempts=0,Expanded=0,Generated=0,Completed=0,Rounds=0,Total=0,
            ForwardQueries=0,ForwardBlocked=0,EndgamePeakGeometry=0,PairCompletions=0;
        std::size_t SearchAttemptLimit=0;
        double SearchTimeLimit=0;
        bool Cancelled=false;
        std::vector<double> Areas,Hulls;
        std::vector<std::vector<Direction>> Directions;
        JobResult Best;
        double BestFront=std::numeric_limits<double>::infinity();
        double elapsed()const{return std::chrono::duration<double>(Clock::now()-Start).count();}
        bool stop()
        {
            if(Cancelled)return true;
            if(O->Cancel&&O->Cancel()){Cancelled=true;return true;}
            return elapsed()>=SearchTimeLimit||Attempts>=SearchAttemptLimit;
        }
        Result result(const Node& node)const
        {
            Result out;out.State=node.Board.Placements.size()==Total?Status::Complete:Status::Partial;
            if(!node.Board.Placements.empty())
            {
                out.Layouts.push_back(node.Board);out.Layouts[0].MaterialArea=node.Material;
                out.Layouts[0].Width=P->Stocks[0].Width;out.Layouts[0].Height=P->Stocks[0].Height;
            }
            for(std::size_t i=0;i<P->Parts.size();++i)if(node.Used[i]<P->Parts[i].Quantity)
                out.Remaining.push_back({P->Parts[i].ID,P->Parts[i].Quantity-node.Used[i]});
            return out;
        }
        void remember(const Node& node,bool exact=false)
        {
            const auto old=Best.Checked.PlacedCount;
            const auto count=node.Board.Placements.size();
            if(count<old)return;
            if(!exact&&count<Total&&count==old&&node.Front>=BestFront-P->Tolerance)return;
            JobResult job;job.Output=result(node);
            // Complete alternatives use the same certified remnant ranking as
            // the portfolio. Partial frontier scores are search heuristics only.
            if(exact||count==Total)AssessCandidate(*P,job);
            else{job.Checked=Validate(*P,job.Output);job.Output.GeometryValidated=job.Checked.Valid;}
            if(!job.Checked.Valid)return;
            bool better=count>old;
            if(!better&&count==Total)better=Quality(job,*P,O->Ranking)<Quality(Best,*P,O->Ranking);
            else if(!better)better=node.Front<BestFront;
            if(better){Best=std::move(job);BestFront=node.Front;}
        }
        Node child(const Node& parent,std::size_t index,const Direction& d,Pose pose)const
        {
            Node next=parent;const auto& part=P->Parts[index];
            // Adding material invalidates all layout-dependent feasibility
            // witnesses inherited from the parent. Pure pair NFPs stay cached.
            next.VerifiedNext.clear();
            next.Board.Placements.push_back({part.ID,next.Used[index]++,pose,0,false});
            next.Material+=Areas[index];next.Hull+=Hulls[index];
            next.MaxLeft=std::max(next.MaxLeft,pose.X+d.Bounds.X-P->EdgeMargin);
            next.Right=std::max(next.Right,pose.X+d.Bounds.X+d.Bounds.Width);
            next.Top=std::max(next.Top,pose.Y+d.Bounds.Y+d.Bounds.Height);
            const double step=(P->Stocks[0].Height-2*P->EdgeMargin)/next.Profile.size();
            for(std::size_t k=0;k<next.Profile.size();++k)
            {
                const double y=P->EdgeMargin+(k+.5)*step-pose.Y;
                if(y<d.Bounds.Y||y>d.Bounds.Y+d.Bounds.Height)continue;
                double right=-std::numeric_limits<double>::infinity();
                for(std::size_t v=0;v<d.Ring.size();++v)
                {
                    const auto a=d.Ring[v],b=d.Ring[(v+1)%d.Ring.size()];
                    if((a.Y<=y&&y<b.Y)||(b.Y<=y&&y<a.Y))
                        right=std::max(right,a.X+(b.X-a.X)*(y-a.Y)/(b.Y-a.Y));
                }
                if(std::isfinite(right))next.Profile[k]=std::max(next.Profile[k],pose.X+right-P->EdgeMargin);
            }
            next.Front=0;for(double x:next.Profile)next.Front+=std::max(x,next.MaxLeft)*step;
            if(Config.EndgameDepth)
            {
            const double gridX=(next.Board.Width-2*P->EdgeMargin)/32;
            const double gridY=(next.Board.Height-2*P->EdgeMargin)/16;
            for(std::size_t iy=0;iy<16;++iy)
            {
                const double y=P->EdgeMargin+(iy+.5)*gridY-pose.Y;
                if(y<d.Bounds.Y||y>d.Bounds.Y+d.Bounds.Height)continue;
                std::vector<double> intersections;
                for(std::size_t v=0;v<d.Ring.size();++v)
                {
                    const auto a=d.Ring[v],b=d.Ring[(v+1)%d.Ring.size()];
                    if((a.Y<=y&&y<b.Y)||(b.Y<=y&&y<a.Y))
                        intersections.push_back(a.X+(b.X-a.X)*(y-a.Y)/(b.Y-a.Y)+pose.X);
                }
                std::sort(intersections.begin(),intersections.end());
                for(std::size_t ix=0;ix<32;++ix)
                {
                    const double x=P->EdgeMargin+(ix+.5)*gridX;
                    for(std::size_t j=1;j<intersections.size();j+=2)
                        if(x>intersections[j-1]&&x<intersections[j])
                        {const auto bit=iy*32+ix;next.MaterialSamples[bit/64]|=std::uint64_t(1)<<(bit%64);break;}
                }
            }
            }
            if(Config.Score==NfpBeamScore::HullProgress)
                next.Score=next.Front/(next.Hull*next.Hull);
            else next.Score=(next.Front-next.Material)/std::sqrt(next.Hull)+.025*next.Right;
            // Commutative geometry signature collapses permutations of the same
            // original placements. It never replaces a legality check.
            const double unit=std::max(P->Tolerance*4,1e-7);
            const auto px=static_cast<std::uint64_t>(std::llround(pose.X/unit));
            const auto py=static_cast<std::uint64_t>(std::llround(pose.Y/unit));
            const auto pa=static_cast<std::uint64_t>(std::llround(NormalizeAngle(pose.AngleDegrees)*1e6));
            next.Fingerprint+=Mix(Mix(index+1)^Mix(px)^Mix(py+0x9e3779b97f4a7c15ULL)^Mix(pa));
            return next;
        }
        std::vector<Node> retain(std::vector<Node> pool,std::size_t limit)
        {
            const auto less=[](const Node& a,const Node& b)
            {return std::tie(a.Score,a.Right,a.Front,a.Fingerprint)<std::tie(b.Score,b.Right,b.Front,b.Fingerprint);};
            std::sort(pool.begin(),pool.end(),less);
            std::map<std::vector<std::size_t>,std::vector<std::size_t>> buckets;
            std::unordered_set<std::uint64_t> seen;
            for(std::size_t i=0;i<pool.size();++i)if(seen.insert(pool[i].Fingerprint).second)
                buckets[pool[i].Used].push_back(i);
            if(buckets.empty())return {};
            const auto remaining=Total-pool.front().Board.Placements.size();
            const bool late=Config.EndgameDepth&&remaining<=Config.EndgameDepth;
            std::vector<std::vector<std::size_t>*> active;
            for(auto& [used,ids]:buckets){(void)used;active.push_back(&ids);}
            std::sort(active.begin(),active.end(),[&](auto a,auto b){return less(pool[a->front()],pool[b->front()]);});
            if(late)active.resize(std::min(active.size(),std::max<std::size_t>(1,limit/Config.EndgameGeometry)));
            const auto quota=late?std::min(limit,std::max(Config.EndgameGeometry,(limit+active.size()-1)/active.size())):Config.PerInventory;
            const auto distance=[&](std::size_t a,std::size_t b)
            {
                unsigned count=0;for(std::size_t k=0;k<pool[a].MaterialSamples.size();++k)
                    count+=std::popcount(pool[a].MaterialSamples[k]^pool[b].MaterialSamples[k]);
                return count;
            };
            for(auto* ids:active)
            {
                if(!late){ids->resize(std::min(quota,ids->size()));continue;}
                const auto candidates=std::min(ids->size(),std::max<std::size_t>(quota*4,24));
                std::vector<std::size_t> diverse{ids->front()};
                const auto wanted=std::min(quota,ids->size());
                // Keep the score champion, then prefer different material
                // neighbourhoods among reasonably competitive alternatives.
                while(diverse.size()<wanted)
                {
                    std::size_t best=pool.size();double bestValue=-std::numeric_limits<double>::infinity();
                    for(std::size_t j=0;j<candidates;++j)
                    {
                        const auto index=(*ids)[j];
                        if(std::find(diverse.begin(),diverse.end(),index)!=diverse.end())continue;
                        unsigned nearest=512;for(auto old:diverse)nearest=std::min(nearest,distance(index,old));
                        const auto gap=(pool[index].Score-pool[ids->front()].Score)/std::max(1e-12,std::abs(pool[ids->front()].Score));
                        const double value=double(nearest)/512.-.1*gap;
                        if(value>bestValue){bestValue=value;best=index;}
                    }
                    if(best==pool.size())break;diverse.push_back(best);
                }
                *ids=std::move(diverse);
                if(late)EndgamePeakGeometry=std::max(EndgamePeakGeometry,ids->size());
            }
            std::vector<std::size_t> selected;
            // First give different inventory states a representative. Then use
            // remaining slots for different geometries of each inventory.
            for(std::size_t rank=0;rank<quota&&selected.size()<limit;++rank)
            {
                std::vector<std::size_t> round;
                for(const auto* ids:active)if(ids->size()>rank)round.push_back((*ids)[rank]);
                std::sort(round.begin(),round.end(),[&](auto a,auto b){return less(pool[a],pool[b]);});
                for(auto i:round){selected.push_back(i);if(selected.size()==limit)break;}
            }
            std::vector<Node> kept;kept.reserve(selected.size());
            for(auto i:selected)kept.push_back(std::move(pool[i]));
            return kept;
        }
        NfpPlacementQuery probe(const Node& node,std::size_t index,double angle,NfpPlacementGenerator& generator)
        {
            NfpPlacementLimits limits;limits.MaxCandidates=1;limits.MaxRawCandidates=8192;
            limits.MaxContactSegments=1024;limits.MaxAttempts=SearchAttemptLimit-Attempts;
            limits.IncludeContactChannels=true;limits.StopAfterFirstFit=true;
            auto query=generator.Find(node.Board,index,angle,limits,[&]{return stop();});
            ++ForwardQueries;Attempts+=query.Attempts;Generated+=query.Generated;return query;
        }
        bool forwardFits(Node& node,NfpPlacementGenerator& generator)
        {
            node.VerifiedNext.clear();
            for(std::size_t index=0;index<P->Parts.size()&&!stop();++index)
            {
                if(node.Used[index]>=P->Parts[index].Quantity)continue;
                bool found=false,unknown=false;
                for(const auto& d:Directions[index])
                {
                    if(stop())return true;
                    const auto query=probe(node,index,d.Angle,generator);
                    if(!query.Fits.empty())
                    {
                        node.VerifiedNext.emplace_back(index,query.Fits.front());found=true;
                        if(Total-node.Board.Placements.size()>2)break;
                    }
                    unknown|=query.Truncated||!query.Supported;
                }
                // Event-set exhaustion guides the beam; it is not a proof of
                // continuous infeasibility. The caller keeps a fallback if all
                // geometries are exhausted by this necessary-condition probe.
                if(!found&&!unknown&&!stop()){++ForwardBlocked;return false;}
            }
            return true;
        }
        double completePair(const Node& node,NfpPlacementGenerator& generator)
        {
            for(const auto& [index,pose]:node.VerifiedNext)
            {
                if(stop())break;
                const auto direction=std::find_if(Directions[index].begin(),Directions[index].end(),
                    [&](const Direction& d){return d.Angle==pose.AngleDegrees;});
                if(direction==Directions[index].end())continue;
                auto first=child(node,index,*direction,pose);
                for(std::size_t second=0;second<P->Parts.size()&&!stop();++second)
                {
                    if(first.Used[second]>=P->Parts[second].Quantity)continue;
                    for(const auto& d:Directions[second])
                    {
                        if(stop())break;const auto query=probe(first,second,d.Angle,generator);
                        if(query.Fits.empty())continue;
                        auto full=child(first,second,d,query.Fits.front());++Completed;++PairCompletions;
                        remember(full,true);return full.Right;
                    }
                }
            }
            return std::numeric_limits<double>::infinity();
        }
        // Advance one original at a time. Main beam competition retains its
        // original breadth; quick construction prioritizes large hull occupancy.
        void quickSearch(NfpPlacementGenerator& generator)
        {
            std::vector<std::size_t> order(P->Parts.size());
            std::iota(order.begin(),order.end(),0);
            std::stable_sort(order.begin(),order.end(),[&](auto a,auto b)
            {return std::tie(Hulls[a],Areas[a])>std::tie(Hulls[b],Areas[b]);});
            Node node;node.Board={P->Stocks[0].ID,0,P->Stocks[0].Width,P->Stocks[0].Height,{},0};
            node.Used.resize(P->Parts.size());node.Profile.resize(Config.ProfileSamples);
            for(std::size_t depth=0;depth<Total&&!stop();++depth)
            {
                ++Expanded;
                std::optional<Node> selected;
                for(const auto index:order)
                {
                    if(stop())break;
                    if(depth==0&&Config.FirstPartIndex!=static_cast<std::size_t>(-1)&&index!=Config.FirstPartIndex)continue;
                    if(node.Used[index]>=P->Parts[index].Quantity)continue;
                    for(const auto& direction:Directions[index])
                    {
                        if(stop())break;
                        NfpPlacementLimits limits;limits.MaxCandidates=1;limits.MaxRawCandidates=2048;
                        limits.MaxAttempts=SearchAttemptLimit-Attempts;
                        limits.StopAfterFirstFit=true;limits.FastAdjacency=true;limits.IncludeContactChannels=Total-depth<=8;
                        const auto query=generator.Find(node.Board,index,direction.Angle,limits,[&]{return stop();});
                        Attempts+=query.Attempts;Generated+=query.Generated;
                        for(const auto pose:query.Fits)
                        {
                            auto candidate=child(node,index,direction,pose);
                            if(!selected||std::tie(candidate.Score,candidate.Right,candidate.Fingerprint)<
                                std::tie(selected->Score,selected->Right,selected->Fingerprint))selected=std::move(candidate);
                        }
                    }
                    if(selected)break;
                }
                if(!selected)break;
                node=std::move(*selected);
            }
            if(node.Board.Placements.size()==Total)++Completed;
            remember(node,node.Board.Placements.size()==Total);
        }
        double search(double width,NfpPlacementGenerator& generator,std::size_t beamWidth,bool firstFit=false)
        {
            const auto savedWidth=Config.BeamWidth;Config.BeamWidth=beamWidth;
            Node empty;empty.Board={P->Stocks[0].ID,0,width,P->Stocks[0].Height,{ },0};
            empty.Used.resize(P->Parts.size());empty.Profile.resize(Config.ProfileSamples);
            std::vector<Node> beam{empty};double completeRight=std::numeric_limits<double>::infinity();
            for(std::size_t depth=0;depth<Total&&!beam.empty()&&!stop();++depth)
            {
                std::vector<Node> pool,complete;
                for(const auto& node:beam)
                {
                    if(stop())break;++Expanded;
                    for(std::size_t index=0;index<P->Parts.size()&&!stop();++index)
                    {
                        if(depth==0&&Config.FirstPartIndex!=static_cast<std::size_t>(-1)&&index!=Config.FirstPartIndex)continue;
                        if(node.Used[index]>=P->Parts[index].Quantity)continue;
                        std::vector<Node> family;
                        for(const auto& d:Directions[index])
                        {
                            if(stop())break;
                            NfpPlacementLimits limits;limits.MaxCandidates=firstFit?1:64;limits.MaxRawCandidates=2048;
                            limits.StopAfterFirstFit=firstFit;
                            if(Total-depth<=2&&(Config.EndgameDepth||Config.RemainingLookahead))
                            {limits.MaxRawCandidates=8192;limits.MaxContactSegments=1024;}
                            limits.MaxAttempts=SearchAttemptLimit-Attempts;
                            limits.IncludeContactChannels=Total-depth<=8;
                            const auto query=generator.Find(node.Board,index,d.Angle,limits,[&]{return stop();});
                            Attempts+=query.Attempts;Generated+=query.Generated;
                            for(auto pose:query.Fits)family.push_back(child(node,index,d,pose));
                            for(const auto& [spec,pose]:node.VerifiedNext)
                                if(spec==index&&pose.AngleDegrees==d.Angle)family.push_back(child(node,index,d,pose));
                        }
                        std::sort(family.begin(),family.end(),[](const Node& a,const Node& b)
                        {return std::tie(a.Score,a.Right,a.Fingerprint)<std::tie(b.Score,b.Right,b.Fingerprint);});
                        const auto count=std::min(Config.PlacementsPerPart,family.size());
                        std::vector<std::size_t> choices;
                        const auto take=[&](std::size_t i)
                        {if(choices.size()<count&&std::find(choices.begin(),choices.end(),i)==choices.end())choices.push_back(i);};
                        for(std::size_t i=0;i<std::min<std::size_t>(4,count);++i)take(i);
                        // Preserve the best contacts at different heights and
                        // genuinely different orientations. A later score cap
                        // must not erase the generator's spatial diversity.
                        for(std::size_t band=0;band<4;++band)
                            for(std::size_t i=0;i<family.size();++i)
                            {
                                const auto box=Bounds(P->Parts[index].Geometry,family[i].Board.Placements.back().Transform);
                                const auto y=(box.Y+box.Height*.5-P->EdgeMargin)/(P->Stocks[0].Height-2*P->EdgeMargin);
                                if(std::min<std::size_t>(3,static_cast<std::size_t>(std::max(0.,y)*4))==band){take(i);break;}
                            }
                        for(const auto& d:Directions[index])
                            for(std::size_t i=0;i<family.size();++i)
                                if(family[i].Board.Placements.back().Transform.AngleDegrees==d.Angle){take(i);break;}
                        for(std::size_t i=0;i<family.size()&&choices.size()<count;++i)take(i);
                        for(auto i:choices)
                        {
                            auto& next=family[i];
                            if(depth+1==Total){++Completed;completeRight=std::min(completeRight,next.Right);complete.push_back(std::move(next));}
                            else pool.push_back(std::move(next));
                        }
                    }
                }
                // Expensive true remnant measurement is reserved for a small
                // finalist set, after all final-depth branches have competed.
                std::sort(complete.begin(),complete.end(),[&](const Node& a,const Node& b)
                {
                    const auto proxy=[&](const Node& n){return std::max((P->Stocks[0].Width-n.Right)*P->Stocks[0].Height,
                        (P->Stocks[0].Height-n.Top)*P->Stocks[0].Width);};
                    return std::tuple(-proxy(a),a.Right,a.Front)<std::tuple(-proxy(b),b.Right,b.Front);
                });
                for(std::size_t i=0;i<std::min<std::size_t>(8,complete.size());++i)
                {if(i&&stop())break;remember(complete[i],true);}
                const bool lookahead=Config.RemainingLookahead&&Total-depth-1<=Config.RemainingLookahead;
                auto retained=retain(std::move(pool),Config.BeamWidth*(lookahead?3:1));
                if(!retained.empty())remember(retained.front());
                if(lookahead)
                {
                    std::vector<Node> viable;std::size_t fallback=retained.size();
                    for(std::size_t i=0;i<retained.size()&&viable.size()<Config.BeamWidth&&!stop();++i)
                    {
                        if(forwardFits(retained[i],generator))
                        {
                            if(Total-depth-1==2&&!stop())
                                completeRight=std::min(completeRight,completePair(retained[i],generator));
                            // A verified final original is already a complete
                            // construction. Do not lose its pose to a smaller
                            // event cap or a later frontier-score truncation.
                            if(Total-depth-1==1&&!stop())
                                for(const auto& [index,pose]:retained[i].VerifiedNext)
                                    for(const auto& d:Directions[index])if(d.Angle==pose.AngleDegrees)
                                    {
                                        auto full=child(retained[i],index,d,pose);++Completed;
                                        completeRight=std::min(completeRight,full.Right);remember(full,true);break;
                                    }
                            viable.push_back(std::move(retained[i]));
                        }
                        else if(fallback==retained.size())fallback=i;
                    }
                    if(viable.empty()&&fallback<retained.size())viable.push_back(std::move(retained[fallback]));
                    beam=std::move(viable);
                }
                else beam=std::move(retained);
                if(!beam.empty())remember(beam.front());
            }
            Config.BeamWidth=savedWidth;return completeRight;
        }
    public:
        Beam(NfpBeamSettings settings,std::shared_ptr<NfpCache> cache):Config(settings),Cache(std::move(cache)){}
        Result Run(const Problem& input,const Options& options)override
        {
            P=&input;O=&options;Start=Clock::now();Attempts=Expanded=Generated=Completed=Rounds=Total=0;
            SearchAttemptLimit=options.MaxPlacementAttempts;SearchTimeLimit=options.TimeLimitSeconds;
            ForwardQueries=ForwardBlocked=EndgamePeakGeometry=PairCompletions=0;Cancelled=false;Best={};BestFront=std::numeric_limits<double>::infinity();
            const auto fail=[&](std::string reason)
            {
                Result r;r.State=Cancelled?Status::Cancelled:Status::Partial;r.Diagnostics.push_back(std::move(reason));
                for(const auto& p:input.Parts)if(p.Quantity)r.Remaining.push_back({p.ID,p.Quantity});return r;
            };
            if(const auto errors=CheckProblem(input);!errors.empty())
            {auto r=fail("NFP beam received invalid original geometry.");r.State=Status::InvalidInput;r.Diagnostics=errors;return r;}
            if(input.Stocks.size()!=1||input.Stocks[0].Quantity==0||input.Parts.empty()||input.Parts.size()>128||
                Config.BeamWidth==0||Config.BeamWidth>2048||Config.InitialBeamWidth==0||Config.InitialBeamWidth>2048||
                Config.PerInventory==0||Config.PerInventory>64||
                !std::isfinite(Config.QuickPassBudgetFraction)||Config.QuickPassBudgetFraction<0||Config.QuickPassBudgetFraction>.5||
                (Config.QuickPolicy!=NfpBeamQuickPolicy::Scored&&Config.QuickPolicy!=NfpBeamQuickPolicy::HullGreedy)||
                Config.PlacementsPerPart==0||Config.PlacementsPerPart>128||Config.ProfileSamples<8||Config.ProfileSamples>256||
                Config.EndgameDepth>32||Config.EndgameGeometry==0||Config.EndgameGeometry>2048||Config.RemainingLookahead>8)
                return fail("NFP beam: unsupported scope or invalid search limits.");
            if(Config.FirstPartIndex!=static_cast<std::size_t>(-1)&&
                (Config.FirstPartIndex>=input.Parts.size()||input.Parts[Config.FirstPartIndex].Quantity==0))
                return fail("NFP beam: the requested first original specification has no demand.");
            Areas.clear();Hulls.clear();Directions.clear();
            double minWidth=std::numeric_limits<double>::infinity();
            for(const auto& part:input.Parts)
            {
                if(part.Quantity>256-Total||part.Material!=input.Stocks[0].Material)
                    return fail("NFP beam requires <=256 original polygons on one compatible sheet.");
                Total+=part.Quantity;Areas.push_back(Area(part.Geometry));Hulls.push_back(HullArea(part.Geometry.Outer));
                auto& ds=Directions.emplace_back();std::set<std::vector<std::pair<long long,long long>>> orientations;
                for(auto angle:CandidateAngles(part,input.Tolerance))
                {
                    if(stop())return fail("NFP beam stopped during orientation preparation.");
                    Direction d;d.Angle=angle;d.Bounds=Bounds(part.Geometry,{0,0,angle});
                    for(auto p:part.Geometry.Outer)d.Ring.push_back(Transform(p,{0,0,angle}));
                    std::vector<std::pair<long long,long long>> ring;
                    const double unit=std::max(input.Tolerance*.1,1e-9);
                    for(auto q:d.Ring)ring.emplace_back(std::llround((q.X-d.Bounds.X)/unit),std::llround((q.Y-d.Bounds.Y)/unit));
                    auto key=ring;for(std::size_t j=1;j<ring.size();++j)
                    {std::rotate(ring.begin(),ring.begin()+1,ring.end());key=std::min(key,ring);}
                    if(!orientations.insert(std::move(key)).second)continue;
                    minWidth=std::min(minWidth,d.Bounds.Width);ds.push_back(std::move(d));
                }
            }
            NfpPlacementGenerator generator(input,Cache,[&]{return stop();});
            if(!generator.Ready())return fail("NFP beam supports simple polygon exteriors and zero part gap only.");
            // The quick pass has a local limit only. The same generator and
            // its geometry caches then serve every configured search round.
            JobResult quick;
            std::size_t quickAttempts=0,quickPlaced=0;
            double quickSeconds=0;
            const auto quickAllowance=static_cast<std::size_t>(
                static_cast<double>(options.MaxPlacementAttempts-Attempts)*Config.QuickPassBudgetFraction);
            if(quickAllowance&&!stop())
            {
                const auto quickStarted=elapsed();
                SearchAttemptLimit=Attempts+quickAllowance;
                SearchTimeLimit=quickStarted+(options.TimeLimitSeconds-quickStarted)*Config.QuickPassBudgetFraction;
                if(Config.QuickPolicy==NfpBeamQuickPolicy::HullGreedy)quickSearch(generator);
                else search(input.Stocks[0].Width,generator,1,true);
                quickAttempts=Attempts;quickSeconds=elapsed()-quickStarted;
                quickPlaced=Best.Checked.PlacedCount;quick=std::move(Best);
                Best={};BestFront=std::numeric_limits<double>::infinity();
                SearchAttemptLimit=options.MaxPlacementAttempts;SearchTimeLimit=options.TimeLimitSeconds;
            }
            // Keep the caller's original first width and round count. A quick
            // partial never seeds or constrains these reconstructions.
            double width=input.Stocks[0].Width,bestRight=std::numeric_limits<double>::infinity();
            auto beamWidth=std::min(Config.InitialBeamWidth,Config.BeamWidth);
            double failedWidth=-1;
            for(std::size_t round=0;round<=Config.MaxNarrowingRounds&&!stop();++round)
            {
                const auto found=search(width,generator,beamWidth);++Rounds;
                if(std::isfinite(found))
                {
                    bestRight=std::min(bestRight,found);
                    const auto step=std::max(input.Tolerance*8,std::min(minWidth*.25,bestRight*.03));
                    const auto next=failedWidth>=0?(failedWidth+bestRight)*.5:bestRight-step;
                    if(bestRight-next<=input.Tolerance*8)break;
                    width=std::max(input.EdgeMargin*2+minWidth,next);beamWidth=std::min(Config.InitialBeamWidth,Config.BeamWidth);
                }
                else if(beamWidth<Config.BeamWidth&&(!std::isfinite(bestRight)||beamWidth<32))
                    beamWidth=std::min(Config.BeamWidth,beamWidth*2);
                else if(std::isfinite(bestRight))
                {
                    failedWidth=std::max(failedWidth,width);const auto relaxed=(width+bestRight)*.5;
                    if(relaxed-width<=input.Tolerance*8)break;
                    width=relaxed;beamWidth=std::min(Config.InitialBeamWidth,Config.BeamWidth);
                }
                else break;
            }
            const auto mainPlaced=Best.Checked.PlacedCount;
            bool selectedQuick=false;
            if(Eligible(quick))
            {
                if(!quick.Remnants.Measured)AssessCandidate(input,quick);
                if(Eligible(Best)&&!Best.Remnants.Measured)AssessCandidate(input,Best);
                selectedQuick=Eligible(quick)&&(!Eligible(Best)||Quality(quick,input,options.Ranking)<Quality(Best,input,options.Ranking));
                if(selectedQuick)Best=std::move(quick);
            }
            auto out=std::move(Best.Output);
            if(out.State==Status::Failed)out=fail("NFP beam exhausted its budget before a placement was retained.");
            if(Cancelled)out.State=Status::Cancelled;
            out.Stats.PlacementAttempts=Attempts;out.Stats.ElapsedSeconds=elapsed();
            std::ostringstream note;note<<"NFP beam nodes="<<Expanded<<" generated="<<Generated<<" completeCandidates="<<Completed<<" rounds="<<Rounds
                <<" endgameGeometry="<<EndgamePeakGeometry<<" forwardQueries="<<ForwardQueries<<" forwardBlocked="<<ForwardBlocked
                <<" pairCompletions="<<PairCompletions;
            note<<" quickPlaced="<<quickPlaced<<" quickAttempts="<<quickAttempts<<" quickSeconds="<<quickSeconds
                <<" mainPlaced="<<mainPlaced<<" selectedQuick="<<(selectedQuick?1:0);
            note<<" quickPolicy="<<(Config.QuickPolicy==NfpBeamQuickPolicy::Scored?"scored":"hull-greedy");
            if(Config.FirstPartIndex!=static_cast<std::size_t>(-1))note<<" firstPart="<<Config.FirstPartIndex;
            out.Diagnostics.push_back(note.str());return out;
        }
    };
}
StrategyFactory MakeNfpBeamFactory(NfpBeamSettings settings)
{return MakeNfpBeamFactory(settings,{});}
StrategyFactory MakeNfpBeamFactory(NfpBeamSettings settings,std::shared_ptr<NfpCache> cache)
{return [settings,cache=std::move(cache)]{return std::make_unique<Beam>(settings,cache);};}
}
