#include "SheetNestingReassembly.h"
#include "NestingNfpPlacement.h"
#include "NestingExecution.h"
#include "SheetNestingNfp.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

namespace iCAX::SheetNesting
{
namespace
{
    using namespace Detail;
    using Clock=std::chrono::steady_clock;
    using Identity=std::pair<std::string,std::size_t>;
    using RingKey=std::vector<std::pair<long long,long long>>;
    struct Direction {double Angle;Rect Box;std::vector<Point> Ring;RingKey Key;};
    RingKey NormalizedRing(const std::vector<Point>& ring,Rect bounds,double unit)
    {
        RingKey key;for(const auto q:ring)key.emplace_back(std::llround((q.X-bounds.X)/unit),std::llround((q.Y-bounds.Y)/unit));
        if(key.size()>1&&key.front()==key.back())key.pop_back();auto canonical=key;
        for(std::size_t shift=1;shift<key.size();++shift)
        {auto rotated=key;std::rotate(rotated.begin(),rotated.begin()+static_cast<std::ptrdiff_t>(shift),rotated.end());if(rotated<canonical)canonical=std::move(rotated);}
        return canonical;
    }
    Pose CanonicalPose(const Part& part,Pose pose,const std::vector<Direction>& directions,double tolerance)
    {
        for(const auto& direction:directions)
            if(std::abs(NormalizeAngle(pose.AngleDegrees-direction.Angle))<=tolerance)return pose;
        const auto box=Bounds(part.Geometry,{0,0,pose.AngleDegrees});std::vector<Point> ring;
        for(const auto p:part.Geometry.Outer)ring.push_back(Transform(p,{0,0,pose.AngleDegrees}));
        const auto key=NormalizedRing(ring,box,std::max(1e-8,tolerance*.25));
        for(const auto& direction:directions)if(direction.Key==key)
        {pose.X+=box.X-direction.Box.X;pose.Y+=box.Y-direction.Box.Y;pose.AngleDegrees=direction.Angle;return pose;}
        return pose;
    }
    struct Window
    {
        std::size_t LayoutIndex;
        std::vector<Identity> Members;
        std::size_t TargetIndex=std::numeric_limits<std::size_t>::max();
        Pose Target;
    };
    struct Family {std::size_t Index,Required;std::vector<Placement> Originals;};
    struct Node
    {
        Layout Board;
        std::vector<std::size_t> Used;
        std::vector<double> Profile;
        double Right=0,Top=0,Front=0,Material=0,Score=0;
        std::uint64_t Fingerprint=0;
        std::size_t OriginalRetained=0;
    };
    struct Proposal
    {
        Pose Transform;
        std::vector<double> Profile;
        double Right,Top,Front,Material,Score;
        bool Restore=false;
    };
    std::uint64_t Mix(std::uint64_t x)
    {x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);}
    double BoxDistance(Rect a,Rect b)
    {
        const double dx=std::max({0.,a.X-b.X-b.Width,b.X-a.X-a.Width});
        const double dy=std::max({0.,a.Y-b.Y-b.Height,b.Y-a.Y-a.Height});
        return std::hypot(dx,dy)+.025*std::hypot(a.X+a.Width*.5-b.X-b.Width*.5,a.Y+a.Height*.5-b.Y-b.Height*.5);
    }
    // A displacement chain keeps the missing part and every restored original
    // fixed, while the next displaced original may release one new neighbour.
    // Unlike a preselected spatial window, its neighbourhood follows actual
    // feasible contact events. No accepted layout contains overlapping items.
    class DisplacementChain
    {
        const Problem& P;NfpPlacementGenerator& Generator;const std::map<std::string,std::size_t>& Indices;
        const std::vector<std::vector<Direction>>& Directions;
        std::size_t& Attempts;std::size_t AttemptEnd,MaxMembers;std::function<bool()> Stop;
        std::map<std::string,std::size_t> NeutralByPart;
        struct Choice {Pose Transform;std::vector<std::size_t> Blockers;double Movement;double Overlap=0;};
        bool stopped()const{return Attempts>=AttemptEnd||(Stop&&Stop());}
        bool restore(Layout& board,std::vector<Placement> pending,std::set<Identity> protectedItems,std::set<Identity> touched)
        {
            if(pending.empty())return true;
            // One un-restored original means the board has the same placed
            // count as the incoming state but a different remaining identity.
            // Keep a small safe frontier rather than requiring every chain to
            // improve the final quantity immediately.
            if(pending.size()==1&&Neutral.size()<8&&NeutralByPart[pending.front().PartID]<2)
            {Neutral.push_back(board);++NeutralByPart[pending.front().PartID];}
            if(stopped())return false;++Nodes;
            // Put a pending part with no immediately safe position first. It
            // may need another neighbour displaced before an easy filler uses
            // its only cavity. These are heuristic queries, never infeasibility
            // certificates when their finite candidate collection is empty.
            std::size_t selected=0;bool selectedSafe=true;double selectedArea=-1,selectedDomain=std::numeric_limits<double>::infinity();
            std::set<std::string> representedParts;
            for(std::size_t j=0;j<pending.size()&&!stopped();++j)
            {
                if(!representedParts.insert(pending[j].PartID).second)continue;
                const auto index=Indices.at(pending[j].PartID);bool any=false;
                double domain=0;bool measured=true;
                for(const auto& direction:Directions[index])
                {
                    NfpPlacementLimits limits;limits.MaxCandidates=1;limits.MaxRawCandidates=1024;limits.MaxAttempts=AttemptEnd-Attempts;
                    limits.IncludeContactChannels=true;limits.StopAfterFirstFit=true;
                    const auto query=Generator.Find(board,index,direction.Angle,limits,[&]{return stopped();});Attempts+=query.Attempts;
                    any|=!query.Fits.empty();if(query.DomainMeasured)domain+=query.FreeTranslationArea;else measured=false;
                    if(stopped())break;
                }
                if(!measured)domain=std::numeric_limits<double>::infinity();
                const double area=Area(P.Parts[index].Geometry);
                if(j==0||std::tuple(any,domain,-area)<std::tuple(selectedSafe,selectedDomain,-selectedArea))
                {selected=j;selectedSafe=any;selectedArea=area;selectedDomain=domain;}
            }
            if(stopped())return false;const auto item=pending[selected];pending.erase(pending.begin()+static_cast<std::ptrdiff_t>(selected));
            const auto index=Indices.at(item.PartID);std::vector<Choice> choices;
            const auto movement=[&](Pose pose)
            {return std::hypot(pose.X-item.Transform.X,pose.Y-item.Transform.Y)+.001*(pose.X+pose.Y);};
            for(const auto& direction:Directions[index])
            {
                if(stopped())break;NfpPlacementLimits limits;limits.MaxCandidates=96;limits.MaxRawCandidates=4096;
                limits.MaxContactSegments=512;limits.MaxAttempts=AttemptEnd-Attempts;limits.MaxBlockers=1;limits.MaxBlockedCandidates=512;
                const auto query=Generator.Find(board,index,direction.Angle,limits,[&]{return stopped();});Attempts+=query.Attempts;
                for(const auto pose:query.Fits)choices.push_back({pose,{},movement(pose)});
                for(const auto& candidate:query.Blocked)
                {
                    bool allowed=true;for(const auto blocker:candidate.Blockers)
                    {const auto& x=board.Placements[blocker];if(protectedItems.contains({x.PartID,x.Instance}))allowed=false;}
                    if(allowed)choices.push_back({candidate.Transform,candidate.Blockers,movement(candidate.Transform),candidate.OverlapArea});
                }
            }
            // Preserve fractional incumbent positions in addition to contact
            // proposals, since removing neighbours can remove their events.
            const auto originalPose=CanonicalPose(P.Parts[index],item.Transform,Directions[index],P.Tolerance);
            if(!stopped()&&InsideSheet(P.Parts[index].Geometry,originalPose,board.Width,board.Height,P.EdgeMargin,P.Tolerance))
            {
                ++Attempts;std::vector<std::size_t> blockers;
                for(std::size_t j=0;j<board.Placements.size();++j)
                {
                    const auto& x=board.Placements[j];
                    if(ShapesConflict(P.Parts[index].Geometry,originalPose,P.Parts[Indices.at(x.PartID)].Geometry,x.Transform,0,P.Tolerance))
                    {blockers.push_back(j);if(blockers.size()>1||protectedItems.contains({x.PartID,x.Instance}))break;}
                }
                bool allowed=blockers.size()<=1;for(const auto j:blockers)
                {const auto& x=board.Placements[j];if(protectedItems.contains({x.PartID,x.Instance}))allowed=false;}
                if(allowed)choices.push_back({originalPose,std::move(blockers),0});
            }
            std::stable_sort(choices.begin(),choices.end(),[](const Choice& a,const Choice& b)
            {return std::tuple(a.Blockers.size(),a.Overlap,a.Movement,a.Transform.AngleDegrees)<std::tuple(b.Blockers.size(),b.Overlap,b.Movement,b.Transform.AngleDegrees);});
            std::map<std::tuple<std::vector<std::size_t>,long long>,std::size_t> familyIndex;std::vector<std::vector<std::size_t>> families;
            for(std::size_t j=0;j<choices.size();++j)
            {
                const auto key=std::tuple(choices[j].Blockers,std::llround(NormalizeAngle(choices[j].Transform.AngleDegrees)*1e6));
                const auto [it,inserted]=familyIndex.emplace(key,families.size());if(inserted)families.emplace_back();families[it->second].push_back(j);
            }
            std::vector<std::size_t> selectedChoices;std::vector<std::vector<std::size_t>> selectedByFamily(families.size());
            for(std::size_t round=0;round<4&&selectedChoices.size()<16;++round)
                for(std::size_t f=0;f<families.size()&&selectedChoices.size()<16;++f)
                {
                    const auto& ids=families[f];if(ids.size()<=round)continue;std::size_t chosen=ids.front();
                    if(round)
                    {
                        double bestDistance=-1;
                        for(const auto j:ids)
                        {
                            if(std::find(selectedByFamily[f].begin(),selectedByFamily[f].end(),j)!=selectedByFamily[f].end())continue;
                            double nearest=1e100;for(const auto previous:selectedByFamily[f])
                            {const double dx=(choices[j].Transform.X-choices[previous].Transform.X)/board.Width,dy=(choices[j].Transform.Y-choices[previous].Transform.Y)/board.Height;nearest=std::min(nearest,dx*dx+dy*dy);}
                            if(nearest>bestDistance){bestDistance=nearest;chosen=j;}
                        }
                    }
                    selectedByFamily[f].push_back(chosen);selectedChoices.push_back(chosen);
                }
            for(const auto ci:selectedChoices)
            {
                if(stopped())break;const auto& choice=choices[ci];
                auto next=board;auto queue=pending;auto protect=protectedItems;auto visited=touched;bool allowed=true;
                for(const auto blocker:choice.Blockers)
                {
                    const auto x=next.Placements[blocker];const Identity identity{x.PartID,x.Instance};
                    if(protect.contains(identity)||(!visited.contains(identity)&&visited.size()>=MaxMembers)){allowed=false;break;}
                    visited.insert(identity);queue.push_back(x);
                }
                if(!allowed)continue;
                for(auto it=choice.Blockers.rbegin();it!=choice.Blockers.rend();++it)next.Placements.erase(next.Placements.begin()+static_cast<std::ptrdiff_t>(*it));
                auto placed=item;placed.Transform=choice.Transform;placed.Group=0;next.Placements.push_back(placed);protect.emplace(item.PartID,item.Instance);
                if(restore(next,std::move(queue),std::move(protect),std::move(visited))){board=std::move(next);return true;}
            }
            return false;
        }
    public:
        std::size_t Nodes=0;
        std::vector<Layout> Neutral;
        DisplacementChain(const Problem& p,NfpPlacementGenerator& generator,const std::map<std::string,std::size_t>& indices,
            const std::vector<std::vector<Direction>>& directions,std::size_t& attempts,std::size_t end,std::size_t members,std::function<bool()> stop)
            :P(p),Generator(generator),Indices(indices),Directions(directions),Attempts(attempts),AttemptEnd(end),MaxMembers(members),Stop(std::move(stop)){}
        bool Run(Layout& board,Placement inserted)
        {
            if(stopped())return false;const auto index=Indices.at(inserted.PartID);std::vector<std::size_t> blockers;std::vector<Placement> pending;std::set<Identity> touched;
            if(!AngleAllowed(P.Parts[index],inserted.Transform.AngleDegrees,P.Tolerance)||
                !InsideSheet(P.Parts[index].Geometry,inserted.Transform,board.Width,board.Height,P.EdgeMargin,P.Tolerance))return false;
            ++Attempts;
            for(std::size_t j=0;j<board.Placements.size();++j)
            {
                const auto& x=board.Placements[j];if(ShapesConflict(P.Parts[index].Geometry,inserted.Transform,P.Parts[Indices.at(x.PartID)].Geometry,x.Transform,0,P.Tolerance))
                {blockers.push_back(j);pending.push_back(x);touched.emplace(x.PartID,x.Instance);if(blockers.size()>std::min<std::size_t>(3,MaxMembers))return false;}
            }
            auto trial=board;for(auto it=blockers.rbegin();it!=blockers.rend();++it)trial.Placements.erase(trial.Placements.begin()+static_cast<std::ptrdiff_t>(*it));
            trial.Placements.push_back(inserted);const std::set<Identity> protectedItems{{inserted.PartID,inserted.Instance}};
            if(!restore(trial,std::move(pending),protectedItems,std::move(touched)))return false;
            trial.MaterialArea=0;for(const auto& x:trial.Placements)trial.MaterialArea+=Area(P.Parts[Indices.at(x.PartID)].Geometry);
            board=std::move(trial);return true;
        }
    };
    // Small exact-within-candidates reconstruction. MRV and forward support
    // checks spend work on deep compatible assignments instead of discarding
    // them because a partial skyline happens to score worse in a beam layer.
    class ScarceReconstruction
    {
        const Problem& P;NfpPlacementGenerator& Generator;const std::map<std::string,std::size_t>& Indices;
        const std::vector<std::vector<Direction>>& Directions;std::size_t& Attempts;std::size_t AttemptEnd;std::function<bool()> Stop;
        struct Domain {std::size_t Pending,Index;std::vector<Pose> Values;double FreeArea=0;bool Fresh=true;};
        bool stopped()const{return Attempts>=AttemptEnd||(Stop&&Stop());}
        bool search(Layout& board,std::vector<Placement> pending,std::vector<Domain> domains={})
        {
            if(pending.empty())return true;if(stopped())return false;++Nodes;
            const auto populate=[&](Domain& domain)
            {
                const auto index=domain.Index;domain.FreeArea=0;
                for(const auto& direction:Directions[index])
                {
                    if(stopped())return false;NfpPlacementLimits limits;limits.MaxCandidates=128;limits.MaxRawCandidates=pending.size()<=2?8192:4096;
                    limits.MaxContactSegments=pending.size()<=2?1024:512;limits.MaxAttempts=AttemptEnd-Attempts;
                    const auto query=Generator.Find(board,index,direction.Angle,limits,[&]{return stopped();});Attempts+=query.Attempts;
                    domain.Values.insert(domain.Values.end(),query.Fits.begin(),query.Fits.end());domain.FreeArea+=query.FreeTranslationArea;
                }
                for(const auto& item:pending)if(Indices.at(item.PartID)==index)
                {
                    if(stopped())return false;++Attempts;const auto pose=CanonicalPose(P.Parts[index],item.Transform,Directions[index],P.Tolerance);
                    bool safe=InsideSheet(P.Parts[index].Geometry,pose,board.Width,board.Height,P.EdgeMargin,P.Tolerance);
                    for(const auto& obstacle:board.Placements)
                        if(safe&&ShapesConflict(P.Parts[index].Geometry,pose,P.Parts[Indices.at(obstacle.PartID)].Geometry,obstacle.Transform,0,P.Tolerance))safe=false;
                    if(safe)domain.Values.push_back(pose);
                }
                const double unit=std::max(1e-8,P.Tolerance*.25);std::set<std::tuple<long long,long long,long long>> seen;
                std::vector<Pose> unique;
                for(const auto pose:domain.Values)
                    if(seen.emplace(std::llround(pose.X/unit),std::llround(pose.Y/unit),std::llround(NormalizeAngle(pose.AngleDegrees)*1e6)).second)unique.push_back(pose);
                domain.Values=std::move(unique);domain.Fresh=true;return !domain.Values.empty()&&!stopped();
            };
            if(domains.empty())
            {
                std::set<std::string> represented;
                for(std::size_t j=0;j<pending.size();++j)
                {
                    if(!represented.insert(pending[j].PartID).second)continue;
                    Domain domain{j,Indices.at(pending[j].PartID),{}};if(!populate(domain))return false;domains.push_back(std::move(domain));
                }
            }
            else
            {
                domains.erase(std::remove_if(domains.begin(),domains.end(),[&](Domain& domain)
                {
                    const auto it=std::find_if(pending.begin(),pending.end(),[&](const Placement& x){return Indices.at(x.PartID)==domain.Index;});
                    if(it==pending.end())return true;domain.Pending=static_cast<std::size_t>(it-pending.begin());return false;
                }),domains.end());
                for(auto& domain:domains)if(domain.Values.empty()&&!populate(domain))return false;
            }
            if(stopped())return false;
            auto chosen=std::min_element(domains.begin(),domains.end(),[](const Domain& a,const Domain& b)
            {return std::tuple(a.Values.size(),a.FreeArea,a.Index)<std::tuple(b.Values.size(),b.FreeArea,b.Index);});
            // Other domains carry only verified placements surviving the last
            // added original. Refresh the selected part to add new contact
            // events; no whole-board query is repeated for every other part.
            if(!chosen->Fresh&&!populate(*chosen))return false;
            const auto item=pending[chosen->Pending];const auto index=chosen->Index;const auto values=chosen->Values;
            const auto movement=[&](Pose pose)
            {
                double result=1e100;for(const auto& old:pending)if(old.PartID==item.PartID)result=std::min(result,std::hypot(pose.X-old.Transform.X,pose.Y-old.Transform.Y));return result;
            };
            std::vector<std::size_t> order(values.size());std::iota(order.begin(),order.end(),0);
            std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return movement(values[a])<movement(values[b]);});
            std::vector<std::size_t> selected;for(std::size_t j=0;j<std::min<std::size_t>(4,order.size());++j)selected.push_back(order[j]);
            while(selected.size()<std::min<std::size_t>(24,order.size()))
            {
                double farthest=-1;std::size_t next=order.front();
                for(const auto j:order)
                {
                    if(std::find(selected.begin(),selected.end(),j)!=selected.end())continue;double nearest=1e100;
                    for(const auto old:selected)
                    {const double dx=(values[j].X-values[old].X)/board.Width,dy=(values[j].Y-values[old].Y)/board.Height;nearest=std::min(nearest,dx*dx+dy*dy+(values[j].AngleDegrees==values[old].AngleDegrees?0:.05));}
                    if(nearest>farthest){farthest=nearest;next=j;}
                }
                selected.push_back(next);
            }
            pending.erase(pending.begin()+static_cast<std::ptrdiff_t>(chosen->Pending));
            for(const auto ci:selected)
            {
                if(stopped())return false;auto placed=item;placed.Transform=values[ci];placed.Group=0;board.Placements.push_back(placed);bool supported=true;
                std::vector<Domain> nextDomains;
                for(const auto& domain:domains)
                {
                    if(!supported||stopped())break;bool stillNeeded=false;for(const auto& other:pending)if(Indices.at(other.PartID)==domain.Index){stillNeeded=true;break;}
                    if(!stillNeeded)continue;Domain next=domain;next.Values.clear();next.Fresh=false;
                    for(const auto pose:domain.Values)
                    {
                        if(stopped())break;++Attempts;
                        if(!ShapesConflict(P.Parts[index].Geometry,placed.Transform,P.Parts[domain.Index].Geometry,pose,0,P.Tolerance))next.Values.push_back(pose);
                    }
                    if(next.Values.empty()&&!stopped())for(const auto& direction:Directions[domain.Index])
                    {
                        NfpPlacementLimits limits;limits.MaxCandidates=1;limits.MaxRawCandidates=4096;limits.MaxContactSegments=512;
                        limits.MaxAttempts=AttemptEnd-Attempts;limits.StopAfterFirstFit=true;
                        const auto query=Generator.Find(board,domain.Index,direction.Angle,limits,[&]{return stopped();});Attempts+=query.Attempts;
                        if(!query.Fits.empty()){next.Values.push_back(query.Fits.front());break;}if(stopped())break;
                    }
                    if(next.Values.empty())supported=false;else nextDomains.push_back(std::move(next));
                }
                if(supported&&search(board,pending,std::move(nextDomains)))return true;board.Placements.pop_back();
            }
            return false;
        }
    public:
        std::size_t Nodes=0;
        ScarceReconstruction(const Problem& p,NfpPlacementGenerator& generator,const std::map<std::string,std::size_t>& indices,
            const std::vector<std::vector<Direction>>& directions,std::size_t& attempts,std::size_t end,std::function<bool()> stop)
            :P(p),Generator(generator),Indices(indices),Directions(directions),Attempts(attempts),AttemptEnd(end),Stop(std::move(stop)){}
        bool Run(Layout& board,std::vector<Placement> pending){return search(board,std::move(pending));}
    };
    class Reassembly final:public IPostProcessor
    {
        NfpReassemblySettings Config;
        std::shared_ptr<NfpCache> Cache;
    public:
        Reassembly(NfpReassemblySettings settings,std::shared_ptr<NfpCache> cache):Config(settings),Cache(std::move(cache)){}
        Result Run(const Problem& p,const Result& source,const Options& o) override
        {
            const auto start=Clock::now();Statistics work;std::size_t windowsTried=0,accepted=0,expanded=0,leaves=0,chainNodes=0,chainAccepted=0,archiveTried=0,scarceNodes=0,scarceAccepted=0;bool cancelled=false;
            Result incumbent=source;
            const auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-start).count();};
            const auto pollCancel=[&]{if(!cancelled&&o.Cancel&&o.Cancel())cancelled=true;return cancelled;};
            const auto stop=[&]{return pollCancel()||elapsed()>=o.TimeLimitSeconds||work.PlacementAttempts>=o.MaxPlacementAttempts;};
            const auto finish=[&](const std::string& reason)
            {
                work.CombinationGroups=incumbent.Stats.CombinationGroups;work.CircleLatticeGroups=incumbent.Stats.CircleLatticeGroups;
                work.GapFilledParts=incumbent.Stats.GapFilledParts;work.ElapsedSeconds=work.PostProcessingSeconds=elapsed();incumbent.Stats=work;
                if(cancelled)incumbent.State=Status::Cancelled;
                incumbent.Diagnostics.push_back("repair.nfp.reassembly: "+reason+"; windows="+std::to_string(windowsTried)+
                    ", accepted="+std::to_string(accepted)+", expanded="+std::to_string(expanded)+", reconstructed="+std::to_string(leaves)+
                    ", chain nodes="+std::to_string(chainNodes)+", chain accepted="+std::to_string(chainAccepted)+", neutral tried="+std::to_string(archiveTried)+
                    ", scarce nodes="+std::to_string(scarceNodes)+", scarce accepted="+std::to_string(scarceAccepted));
                return incumbent;
            };
            if(stop())return finish("budget");
            if(p.Stocks.size()!=1||p.PartGap!=0||source.Layouts.empty()||source.Layouts.size()>8||p.Parts.size()>128)
                return finish("one stock type and zero-gap polygon scope");
            std::size_t total=0;for(const auto& layout:source.Layouts)total+=layout.Placements.size();
            for(const auto& part:p.Parts)
                if(part.FreeRotation||part.Geometry.Radius>0||!part.Geometry.Holes.empty()||part.Geometry.Outer.size()>128||part.AllowedAnglesDegrees.size()>256)
                    return finish("bounded fixed-angle simple polygon scope");
            if(total>128||!Validate(p,source).Valid)return finish("unsupported or invalid incumbent");
            std::map<std::string,std::size_t> indices;std::vector<double> areas(p.Parts.size());std::vector<std::vector<Direction>> directions(p.Parts.size());
            for(std::size_t i=0;i<p.Parts.size();++i)
            {
                if(stop())return finish("source preparation budget");
                const auto& part=p.Parts[i];
                if(part.FreeRotation||part.Geometry.Radius>0||!part.Geometry.Holes.empty()||part.Geometry.Outer.size()>128)
                    return finish("fixed-angle simple polygon scope");
                indices.emplace(part.ID,i);areas[i]=Area(part.Geometry);
                std::set<std::vector<std::pair<long long,long long>>> orientations;
                for(const auto angle:CandidateAngles(part,p.Tolerance))
                {
                    if(stop())return finish("direction preparation budget");
                    Direction d{angle,Bounds(part.Geometry,{0,0,angle}),{}};
                    for(const auto q:part.Geometry.Outer)d.Ring.push_back(Transform(q,{0,0,angle}));
                    if(d.Ring.size()>1&&std::hypot(d.Ring.front().X-d.Ring.back().X,d.Ring.front().Y-d.Ring.back().Y)<=p.Tolerance)d.Ring.pop_back();
                    d.Key=NormalizedRing(d.Ring,d.Box,std::max(1e-8,p.Tolerance*.25));
                    if(!orientations.insert(d.Key).second)continue;
                    directions[i].push_back(std::move(d));
                }
            }
            auto cache=Cache;if(!cache)cache=std::make_shared<NfpCache>(NfpCacheSettings{512,4096,32*1024*1024});
            NfpPlacementGenerator generator(p,cache,stop);if(!generator.Ready())return finish("unsupported or interrupted NFP preparation");
            JobResult best;best.Output=incumbent;AssessCandidate(p,best);if(!Eligible(best))return finish("incumbent assessment failed");
            const bool needsRepair=Config.MaxMissing&&!source.Remaining.empty();
            const double constructionSeconds=o.TimeLimitSeconds*(needsRepair?2./3.:1.);
            const auto constructionAttempts=needsRepair?std::max<std::size_t>(1,o.MaxPlacementAttempts-o.MaxPlacementAttempts/3):o.MaxPlacementAttempts;
            const auto constructionStop=[&]{return stop()||elapsed()>=constructionSeconds||work.PlacementAttempts>=constructionAttempts;};
            const auto makeCandidate=[&](const Result& base,std::size_t li,Layout board)
            {
                Result result=base;board.MaterialArea=0;for(const auto& x:board.Placements)board.MaterialArea+=areas[indices.at(x.PartID)];
                result.Layouts[li]=std::move(board);result.Remaining.clear();std::vector<std::size_t> counts(p.Parts.size());
                for(const auto& l:result.Layouts)for(const auto& x:l.Placements)++counts[indices.at(x.PartID)];
                for(std::size_t i=0;i<p.Parts.size();++i)if(counts[i]<p.Parts[i].Quantity)result.Remaining.push_back({p.Parts[i].ID,p.Parts[i].Quantity-counts[i]});
                result.State=result.Remaining.empty()?Status::Complete:Status::Partial;return result;
            };
            struct Archived {Result Output;std::size_t Hops;};std::vector<Archived> archive;
            using InventoryKey=std::vector<std::pair<std::string,std::size_t>>;
            const auto inventoryKey=[](const Result& result)
            {InventoryKey key;for(const auto& item:result.Remaining)key.emplace_back(item.PartID,item.Quantity);std::sort(key.begin(),key.end());return key;};
            const auto physicalFingerprint=[&](const Result& result)
            {
                const double unit=std::max(p.Tolerance*.25,1e-8);std::uint64_t fingerprint=0;
                for(const auto& l:result.Layouts)for(const auto& x:l.Placements)
                {
                    const auto index=indices.at(x.PartID);std::vector<Point> ring;
                    for(const auto point:p.Parts[index].Geometry.Outer)ring.push_back(Transform(point,x.Transform));
                    const auto canonical=NormalizedRing(ring,{},unit);std::uint64_t partHash=Mix(index+1);
                    for(const auto [qx,qy]:canonical)partHash=Mix(partHash^Mix(static_cast<std::uint64_t>(qx))^Mix(static_cast<std::uint64_t>(qy)+0x9e3779b97f4a7c15ULL));
                    fingerprint+=partHash;
                }
                return fingerprint;
            };
            std::set<std::pair<InventoryKey,std::uint64_t>> archiveKeys;std::map<InventoryKey,std::size_t> archiveByRemaining;
            const auto rememberNeutral=[&](const Result& base,std::size_t li,const Layout& board,std::size_t hops)
            {
                if(hops>2||archive.size()>=8||stop())return;
                auto result=makeCandidate(base,li,board);const auto remaining=inventoryKey(result);
                if(archiveByRemaining[remaining]>=2)return;
                // Reject a pure instance exchange or symmetry-only rewrite of
                // the incoming physical layout before using an archive slot.
                archiveKeys.emplace(inventoryKey(base),physicalFingerprint(base));
                const auto fingerprint=physicalFingerprint(result);
                if(!archiveKeys.emplace(remaining,fingerprint).second)return;
                const auto checked=Validate(p,result);if(!checked.Valid||checked.PlacedCount<best.Checked.PlacedCount)return;
                ++archiveByRemaining[remaining];archive.push_back({std::move(result),hops});
            };
            std::vector<Window> windows;
            using WindowKey=std::tuple<std::size_t,std::vector<Identity>,std::size_t,long long,long long,long long>;
            std::set<WindowKey> seenWindows;
            const auto addWindow=[&](Window window)
            {
                if(windows.size()>=Config.MaxWindows)return;
                std::sort(window.Members.begin(),window.Members.end());const double unit=std::max(p.Tolerance*4,1e-7);
                WindowKey key{window.LayoutIndex,window.Members,window.TargetIndex,std::llround(window.Target.X/unit),
                    std::llround(window.Target.Y/unit),std::llround(NormalizeAngle(window.Target.AngleDegrees)*1e6)};
                if(seenWindows.insert(std::move(key)).second)windows.push_back(std::move(window));
            };
            // Probe real missing-part contact events against the incumbent.
            // A proposal records exactly which original members block that
            // pose; those members become the first local reconstruction jobs.
            // This probe has its own small fair share of the stage budget.
            if(Config.MaxMissing&&!source.Remaining.empty())
            {
                struct Target {std::size_t LayoutIndex,Index;Pose Transform;std::vector<std::size_t> Blockers;double Extent;double RelativeOverlap=0;};
                std::vector<Target> targets;const double probeEnd=elapsed()+std::min(3.,std::max(0.,o.TimeLimitSeconds-elapsed())*.18);
                const auto probeAttemptEnd=work.PlacementAttempts+(o.MaxPlacementAttempts-work.PlacementAttempts)/5;
                const auto probeStop=[&]{return stop()||elapsed()>=probeEnd||work.PlacementAttempts>=probeAttemptEnd;};
                for(const auto& missing:source.Remaining)
                {
                    if(probeStop())break;const auto movingIndex=indices.at(missing.PartID);
                    for(std::size_t li=source.Layouts.size();li-->0&&!probeStop();)
                    {
                        const auto& board=source.Layouts[li];
                        for(const auto& direction:directions[movingIndex])
                        {
                            if(probeStop())break;NfpPlacementLimits limits;limits.MaxCandidates=32;limits.MaxRawCandidates=8192;limits.MaxContactSegments=1024;
                            limits.MaxAttempts=probeAttemptEnd-work.PlacementAttempts;limits.MaxBlockers=std::min<std::size_t>(6,Config.MaxReleased);limits.MaxBlockedCandidates=2048;
                            const auto query=generator.Find(board,movingIndex,direction.Angle,limits,probeStop);work.PlacementAttempts+=query.Attempts;
                            const auto extent=[&](Pose pose){return pose.X+direction.Box.X+direction.Box.Width+.001*(pose.Y+direction.Box.Y+direction.Box.Height);};
                            for(const auto pose:query.Fits)targets.push_back({li,movingIndex,pose,{},extent(pose)});
                            for(const auto& blocked:query.Blocked)targets.push_back({li,movingIndex,blocked.Transform,blocked.Blockers,extent(blocked.Transform),blocked.OverlapArea/std::max(areas[movingIndex],p.Tolerance*p.Tolerance)});
                        }
                    }
                }
                std::stable_sort(targets.begin(),targets.end(),[](const Target& a,const Target& b)
                {return std::tuple(a.Blockers.size(),a.RelativeOverlap,a.Extent)<std::tuple(b.Blockers.size(),b.RelativeOverlap,b.Extent);});
                const auto targetQuota=std::max<std::size_t>(1,Config.MaxWindows*3/4);
                std::vector<std::size_t> selectedTargets;
                if(!targets.empty())selectedTargets.push_back(0);
                while(selectedTargets.size()<std::max<std::size_t>(1,targetQuota-1)&&selectedTargets.size()<targets.size())
                {
                    std::size_t chosen=targets.size();double bestDistance=-1;
                    for(std::size_t j=0;j<targets.size();++j)
                    {
                        if(std::find(selectedTargets.begin(),selectedTargets.end(),j)!=selectedTargets.end())continue;
                        const auto& candidate=targets[j];const auto& stock=source.Layouts[candidate.LayoutIndex];double nearest=1e100;
                        for(const auto previous:selectedTargets)
                        {
                            const auto& old=targets[previous];
                            if(old.Index!=candidate.Index||old.LayoutIndex!=candidate.LayoutIndex){nearest=std::min(nearest,2.);continue;}
                            const double dx=(old.Transform.X-candidate.Transform.X)/stock.Width,dy=(old.Transform.Y-candidate.Transform.Y)/stock.Height;
                            nearest=std::min(nearest,std::hypot(dx,dy)+(old.Transform.AngleDegrees==candidate.Transform.AngleDegrees?0:.15));
                        }
                        const double merit=nearest-.03*candidate.Blockers.size()-.05*candidate.RelativeOverlap;
                        if(merit>bestDistance){bestDistance=merit;chosen=j;}
                    }
                    if(chosen==targets.size())break;selectedTargets.push_back(chosen);
                }
                for(std::size_t rank=0;rank<selectedTargets.size();++rank)
                {
                    const auto& target=targets[selectedTargets[rank]];const auto& board=source.Layouts[target.LayoutIndex];
                    Window expandedWindow{target.LayoutIndex,{},target.Index,target.Transform};std::set<std::size_t> released(target.Blockers.begin(),target.Blockers.end());
                    const auto targetBounds=Bounds(p.Parts[target.Index].Geometry,target.Transform);std::vector<std::size_t> nearby(board.Placements.size());std::iota(nearby.begin(),nearby.end(),0);
                    std::stable_sort(nearby.begin(),nearby.end(),[&](auto a,auto b)
                    {
                        const auto distance=[&](auto i){const auto& x=board.Placements[i];return BoxDistance(targetBounds,Bounds(p.Parts[indices.at(x.PartID)].Geometry,x.Transform));};
                        return distance(a)<distance(b);
                    });
                    const auto size=std::min(board.Placements.size(),std::min(Config.MaxReleased,rank%2?std::size_t(8):std::size_t(6)));
                    for(const auto i:nearby)if(released.size()<size)released.insert(i);
                    for(const auto i:released){const auto& x=board.Placements[i];expandedWindow.Members.emplace_back(x.PartID,x.Instance);}
                    addWindow(std::move(expandedWindow));
                }
                // Keep a smaller exact-blocker job as well, after expanded
                // windows have had priority. Its distinct target pose belongs
                // in the key: equal member sets can have different repairs.
                for(const auto ti:selectedTargets)
                {
                    if(windows.size()>=targetQuota)break;const auto& target=targets[ti];const auto& board=source.Layouts[target.LayoutIndex];
                    Window exact{target.LayoutIndex,{},target.Index,target.Transform};
                    for(const auto i:target.Blockers){const auto& x=board.Placements[i];exact.Members.emplace_back(x.PartID,x.Instance);}
                    if(exact.Members.empty()&&!board.Placements.empty())exact.Members.emplace_back(board.Placements.back().PartID,board.Placements.back().Instance);
                    addWindow(std::move(exact));
                }
            }
            // Each seed produces a real spatial neighbourhood. Alternate outer
            // extent seeds and low intrinsic-fill seeds to revisit both a poor
            // right edge and concave pieces surrounded by wasted cavities.
            for(std::size_t li=source.Layouts.size();li-->0;)
            {
                const auto& board=source.Layouts[li];const auto n=board.Placements.size();if(n==0)continue;
                std::vector<Rect> boxes(n);std::vector<std::size_t> right(n),voids(n);std::iota(right.begin(),right.end(),0);voids=right;
                for(std::size_t j=0;j<n;++j)boxes[j]=Bounds(p.Parts[indices.at(board.Placements[j].PartID)].Geometry,board.Placements[j].Transform);
                std::stable_sort(right.begin(),right.end(),[&](auto a,auto b)
                {return boxes[a].X+boxes[a].Width>boxes[b].X+boxes[b].Width;});
                std::stable_sort(voids.begin(),voids.end(),[&](auto a,auto b)
                {
                    const auto waste=[&](auto j){return boxes[j].Width*boxes[j].Height-areas[indices.at(board.Placements[j].PartID)];};
                    return waste(a)>waste(b);
                });
                for(std::size_t round=0;round<Config.MaxWindows&&windows.size()<Config.MaxWindows;++round)
                {
                    const auto& seeds=round%2?voids:right;const auto seed=seeds[(round/2*std::max<std::size_t>(1,n/4))%n];
                    std::vector<std::size_t> near(n);std::iota(near.begin(),near.end(),0);
                    std::stable_sort(near.begin(),near.end(),[&](auto a,auto b){return BoxDistance(boxes[seed],boxes[a])<BoxDistance(boxes[seed],boxes[b]);});
                    const std::size_t count=std::min(n,round>=4?std::max<std::size_t>(1,Config.MaxReleased/2):Config.MaxReleased);
                    Window window{li,{}};for(std::size_t j=0;j<count;++j){const auto& x=board.Placements[near[j]];window.Members.emplace_back(x.PartID,x.Instance);}
                    addWindow(std::move(window));
                }
                if(windows.size()>=Config.MaxWindows)break;
            }
            const auto proposal=[&](const Node& node,std::size_t index,const Direction& d,Pose pose)
            {
                Proposal q{pose,node.Profile,std::max(node.Right,pose.X+d.Box.X+d.Box.Width),std::max(node.Top,pose.Y+d.Box.Y+d.Box.Height),0,node.Material+areas[index],0};
                const double step=(node.Board.Height-2*p.EdgeMargin)/Config.ProfileSamples;
                for(std::size_t k=0;k<q.Profile.size();++k)
                {
                    const double y=p.EdgeMargin+(k+.5)*step-pose.Y;if(y<d.Box.Y||y>d.Box.Y+d.Box.Height)continue;
                    double right=-std::numeric_limits<double>::infinity();
                    for(std::size_t j=0;j<d.Ring.size();++j)
                    {
                        const auto a=d.Ring[j],b=d.Ring[(j+1)%d.Ring.size()];
                        if((a.Y<=y&&y<b.Y)||(b.Y<=y&&y<a.Y))right=std::max(right,a.X+(b.X-a.X)*(y-a.Y)/(b.Y-a.Y));
                    }
                    if(std::isfinite(right))q.Profile[k]=std::max(q.Profile[k],pose.X+right-p.EdgeMargin);
                }
                q.Front=std::accumulate(q.Profile.begin(),q.Profile.end(),0.)*step;
                q.Score=(q.Front-q.Material)/std::sqrt(std::max(q.Material,p.Tolerance))+.025*q.Right;
                return q;
            };
            for(std::size_t wi=0;wi<windows.size()&&!constructionStop();++wi)
            {
                ++windowsTried;const auto& window=windows[wi];const auto board=incumbent.Layouts[window.LayoutIndex];
                const auto slots=windows.size()-wi;const double seconds=(constructionSeconds-elapsed())/slots;
                const double deadline=elapsed()+seconds*.82;const auto attemptQuota=(constructionAttempts-work.PlacementAttempts)/slots;
                const auto attemptEnd=work.PlacementAttempts+attemptQuota;
                const auto windowStop=[&]{return constructionStop()||elapsed()>=deadline||work.PlacementAttempts>=attemptEnd;};
                if(window.TargetIndex<p.Parts.size()&&Config.MaxMissing&&!windowStop())
                {
                    const auto ti=window.TargetIndex;std::set<std::size_t> used;
                    for(const auto& l:incumbent.Layouts)for(const auto& x:l.Placements)if(x.PartID==p.Parts[ti].ID)used.insert(x.Instance);
                    std::size_t instance=0;while(instance<p.Parts[ti].Quantity&&used.contains(instance))++instance;
                    if(instance<p.Parts[ti].Quantity)
                    {
                        const double chainDeadline=elapsed()+seconds*.38;
                        const auto chainAttemptEnd=work.PlacementAttempts+(attemptEnd-work.PlacementAttempts)*2/5;
                        const auto chainStop=[&]{return windowStop()||elapsed()>=chainDeadline;};
                        DisplacementChain chain(p,generator,indices,directions,work.PlacementAttempts,chainAttemptEnd,Config.MaxReleased,chainStop);
                        auto restored=board;const bool complete=chain.Run(restored,{p.Parts[ti].ID,instance,window.Target,0,true});chainNodes+=chain.Nodes;
                        for(const auto& neutral:chain.Neutral)rememberNeutral(incumbent,window.LayoutIndex,neutral,1);
                        if(complete&&!stop())
                        {
                            JobResult candidate;candidate.Output=incumbent;candidate.Output.Layouts[window.LayoutIndex]=std::move(restored);candidate.Output.Remaining.clear();
                            std::vector<std::size_t> counts(p.Parts.size());for(const auto& l:candidate.Output.Layouts)for(const auto& x:l.Placements)++counts[indices.at(x.PartID)];
                            for(std::size_t i=0;i<p.Parts.size();++i)if(counts[i]<p.Parts[i].Quantity)candidate.Output.Remaining.push_back({p.Parts[i].ID,p.Parts[i].Quantity-counts[i]});
                            candidate.Output.State=candidate.Output.Remaining.empty()?Status::Complete:Status::Partial;AssessCandidate(p,candidate);
                            if(Eligible(candidate)&&Quality(candidate,p,o.Ranking)<Quality(best,p,o.Ranking))
                            {best=std::move(candidate);incumbent=best.Output;++accepted;++chainAccepted;continue;}
                        }
                    }
                }
                Node root;root.Board=board;root.Board.Placements.clear();root.Board.MaterialArea=0;root.Profile.resize(Config.ProfileSamples);
                std::map<std::size_t,Family> byPart;
                for(const auto& x:board.Placements)
                {
                    const auto index=indices.at(x.PartID);
                    if(std::binary_search(window.Members.begin(),window.Members.end(),Identity{x.PartID,x.Instance}))
                    {auto& family=byPart[index];family.Index=index;family.Originals.push_back(x);++family.Required;}
                    else root.Board.Placements.push_back(x);
                }
                std::size_t released=0;for(const auto& [index,family]:byPart){(void)index;released+=family.Required;}
                if(released==0)continue;
                std::size_t missingAdded=0,fixedMissing=0;
                std::vector<std::set<std::size_t>> occupied(p.Parts.size());
                for(const auto& l:incumbent.Layouts)for(const auto& x:l.Placements)occupied[indices.at(x.PartID)].insert(x.Instance);
                if(window.TargetIndex<p.Parts.size())
                {
                    const auto ti=window.TargetIndex;std::size_t instance=0;while(instance<p.Parts[ti].Quantity&&occupied[ti].contains(instance))++instance;
                    bool safe=instance<p.Parts[ti].Quantity;
                    for(const auto& fixed:root.Board.Placements)
                        if(safe&&ShapesConflict(p.Parts[ti].Geometry,window.Target,p.Parts[indices.at(fixed.PartID)].Geometry,fixed.Transform,0,p.Tolerance))safe=false;
                    if(safe)
                    {root.Board.Placements.push_back({p.Parts[ti].ID,instance,window.Target,0,true});occupied[ti].insert(instance);fixedMissing=1;}
                }
                // Missing originals use actual unused source instance IDs.
                for(std::size_t i=0;i<p.Parts.size()&&missingAdded+fixedMissing<Config.MaxMissing;++i)
                    for(std::size_t instance=0;instance<p.Parts[i].Quantity&&missingAdded+fixedMissing<Config.MaxMissing;++instance)
                        if(!occupied[i].contains(instance))
                        {auto& family=byPart[i];family.Index=i;family.Originals.push_back({p.Parts[i].ID,instance,{},0,true});++missingAdded;}
                std::vector<Family> families;for(auto& [index,family]:byPart){(void)index;families.push_back(std::move(family));}
                root.Used.resize(families.size());
                // Initialise the profile from every fixed original, without
                // altering their order, pose, identity or planning provenance.
                auto fixed=root.Board.Placements;root.Board.Placements.clear();
                for(const auto& x:fixed)
                {
                    const auto index=indices.at(x.PartID);Direction d{x.Transform.AngleDegrees,Bounds(p.Parts[index].Geometry,{0,0,x.Transform.AngleDegrees}),{}};
                    for(const auto point:p.Parts[index].Geometry.Outer)d.Ring.push_back(Transform(point,{0,0,d.Angle}));
                    const auto q=proposal(root,index,d,x.Transform);root.Profile=q.Profile;root.Right=q.Right;root.Top=q.Top;root.Front=q.Front;root.Material=q.Material;root.Score=q.Score;
                    root.Board.Placements.push_back(x);
                }
                root.Board.MaterialArea=root.Material;
                if(fixedMissing&&!windowStop())
                {
                    const double scarceDeadline=elapsed()+std::min(seconds*.22,std::max(0.,deadline-elapsed())*.45);
                    const auto scarceAttemptEnd=work.PlacementAttempts+(attemptEnd-work.PlacementAttempts)*2/5;
                    const auto scarceStop=[&]{return windowStop()||elapsed()>=scarceDeadline;};
                    std::vector<Placement> pending;for(const auto& family:families)
                        pending.insert(pending.end(),family.Originals.begin(),family.Originals.begin()+static_cast<std::ptrdiff_t>(family.Required));
                    auto reconstructed=root.Board;ScarceReconstruction worker(p,generator,indices,directions,work.PlacementAttempts,scarceAttemptEnd,scarceStop);
                    const bool complete=worker.Run(reconstructed,std::move(pending));scarceNodes+=worker.Nodes;
                    if(complete&&!stop())
                    {
                        JobResult candidate;candidate.Output=makeCandidate(incumbent,window.LayoutIndex,std::move(reconstructed));AssessCandidate(p,candidate);
                        if(Eligible(candidate)&&Quality(candidate,p,o.Ranking)<Quality(best,p,o.Ranking))
                        {best=std::move(candidate);incumbent=best.Output;++accepted;++scarceAccepted;continue;}
                    }
                }
                std::vector<Node> finalists;
                const auto leafKey=[&](const Node& node)
                {
                    std::size_t inserted=0;for(std::size_t i=0;i<families.size();++i)inserted+=node.Used[i]>families[i].Required?node.Used[i]-families[i].Required:0;
                    const double remnantProxy=std::max((board.Width-node.Right)*board.Height,(board.Height-node.Top)*board.Width);
                    return std::tuple(std::numeric_limits<std::size_t>::max()-inserted,-remnantProxy,node.Right,node.Front);
                };
                std::vector<std::size_t> widths;
                for(const auto desired:{std::size_t(2),std::size_t(4),std::size_t(8),std::size_t(16)})
                {const auto width=std::min(Config.BeamWidth,desired);if(widths.empty()||widths.back()!=width)widths.push_back(width);}
                // Reach complete reconstruction depth with a narrow beam before
                // spending the remaining fair window quota on wider choices.
                for(const auto width:widths)
                {
                    if(windowStop())break;std::vector<Node> beam{root};
                    for(std::size_t depth=0;depth<released+missingAdded&&!beam.empty()&&!windowStop();++depth)
                    {
                    std::vector<Node> pool;
                    for(const auto& node:beam)
                    {
                        if(windowStop())break;++expanded;
                        for(std::size_t fi=0;fi<families.size()&&!windowStop();++fi)
                        {
                            const auto& family=families[fi];if(node.Used[fi]>=family.Originals.size())continue;
                            std::vector<Proposal> choices;
                            for(const auto& direction:directions[family.Index])
                            {
                                if(windowStop())break;NfpPlacementLimits limits;limits.MaxCandidates=Config.CandidatesPerAngle;
                                const bool finalParts=released+missingAdded-depth<=2;
                                limits.MaxRawCandidates=finalParts?8192:2048;limits.MaxContactSegments=finalParts?1024:256;
                                limits.MaxAttempts=attemptEnd-work.PlacementAttempts;limits.IncludeContactChannels=true;
                                const auto query=generator.Find(node.Board,family.Index,direction.Angle,limits,windowStop);
                                work.PlacementAttempts+=query.Attempts;
                                for(const auto pose:query.Fits)choices.push_back(proposal(node,family.Index,direction,pose));
                            }
                            // A released original need not lie on a current
                            // contact event (e.g. a previous fractional pose).
                            // Keep its safe old pose as a real alternative.
                            if(node.Used[fi]<family.Required&&!windowStop())
                            {
                                const auto pose=CanonicalPose(p.Parts[family.Index],family.Originals[node.Used[fi]].Transform,directions[family.Index],p.Tolerance);
                                ++work.PlacementAttempts;bool safe=InsideSheet(p.Parts[family.Index].Geometry,pose,board.Width,board.Height,p.EdgeMargin,p.Tolerance);
                                for(const auto& obstacle:node.Board.Placements)
                                    if(safe&&ShapesConflict(p.Parts[family.Index].Geometry,pose,p.Parts[indices.at(obstacle.PartID)].Geometry,obstacle.Transform,0,p.Tolerance))safe=false;
                                if(safe)
                                {
                                    const auto same=std::find_if(choices.begin(),choices.end(),[&](const Proposal& q)
                                    {return std::abs(q.Transform.X-pose.X)<=p.Tolerance&&std::abs(q.Transform.Y-pose.Y)<=p.Tolerance&&
                                        std::abs(NormalizeAngle(q.Transform.AngleDegrees-pose.AngleDegrees))<=p.Tolerance;});
                                    if(same!=choices.end())same->Restore=true;
                                    else
                                    {
                                        Direction d{pose.AngleDegrees,Bounds(p.Parts[family.Index].Geometry,{0,0,pose.AngleDegrees}),{}};
                                        for(auto v:p.Parts[family.Index].Geometry.Outer)d.Ring.push_back(Transform(v,{0,0,pose.AngleDegrees}));
                                        auto q=proposal(node,family.Index,d,pose);q.Restore=true;choices.push_back(std::move(q));
                                    }
                                }
                            }
                            std::stable_sort(choices.begin(),choices.end(),[](const Proposal& a,const Proposal& b)
                            {return std::tie(a.Score,a.Right,a.Front,a.Transform.X,a.Transform.Y)<std::tie(b.Score,b.Right,b.Front,b.Transform.X,b.Transform.Y);});
                            const auto count=std::min(Config.BranchPerPart,choices.size());
                            std::vector<std::size_t> selected;for(std::size_t j=0;j<count;++j)selected.push_back(j);
                            const auto fallback=std::find_if(choices.begin(),choices.end(),[](const Proposal& q){return q.Restore;});
                            if(count>1&&fallback!=choices.end())
                            {const auto id=static_cast<std::size_t>(fallback-choices.begin());if(id>=count)selected.back()=id;}
                            for(auto j:selected)
                            {
                                const auto& q=choices[j];Node next=node;auto original=family.Originals[next.Used[fi]++];original.Transform=q.Transform;original.Group=0;
                                next.OriginalRetained+=q.Restore?1:0;
                                next.Board.Placements.push_back(std::move(original));next.Profile=q.Profile;next.Right=q.Right;next.Top=q.Top;next.Front=q.Front;next.Material=q.Material;next.Score=q.Score;next.Board.MaterialArea=next.Material;
                                const double unit=std::max(p.Tolerance*4,1e-7);
                                next.Fingerprint+=Mix(Mix(family.Index+1)^Mix(static_cast<std::uint64_t>(std::llround(q.Transform.X/unit)))^
                                    Mix(static_cast<std::uint64_t>(std::llround(q.Transform.Y/unit))+0x9e3779b97f4a7c15ULL)^
                                    Mix(static_cast<std::uint64_t>(std::llround(NormalizeAngle(q.Transform.AngleDegrees)*1e6))));
                                bool restored=true;for(std::size_t f=0;f<families.size();++f)if(next.Used[f]<families[f].Required){restored=false;break;}
                                if(restored){++leaves;finalists.push_back(next);}
                                if(depth+1<released+missingAdded)pool.push_back(std::move(next));
                            }
                        }
                    }
                    std::stable_sort(pool.begin(),pool.end(),[](const Node& a,const Node& b){return std::tie(a.Score,a.Right,a.Front)<std::tie(b.Score,b.Right,b.Front);});
                    std::map<std::vector<std::size_t>,std::vector<std::size_t>> buckets;std::unordered_set<std::uint64_t> seen;
                    const auto perInventory=std::min(width,std::max<std::size_t>(3,width/2));
                    for(std::size_t j=0;j<pool.size();++j)if(seen.insert(pool[j].Fingerprint).second)
                    {auto& ids=buckets[pool[j].Used];if(ids.size()<perInventory)ids.push_back(j);}
                    beam.clear();
                    // Reserve one route that preserves as many of the old
                    // neighbours as possible while other routes rearrange.
                    std::size_t restoreId=pool.size();
                    for(std::size_t j=0;j<pool.size();++j)
                        if(restoreId==pool.size()||pool[j].OriginalRetained>pool[restoreId].OriginalRetained)restoreId=j;
                    if(restoreId<pool.size()&&pool[restoreId].OriginalRetained)beam.push_back(std::move(pool[restoreId]));
                    for(std::size_t rank=0;rank<perInventory&&beam.size()<width;++rank)
                    {
                        std::vector<std::size_t> selected;for(const auto& [used,ids]:buckets){(void)used;if(ids.size()>rank)selected.push_back(ids[rank]);}
                        std::sort(selected.begin(),selected.end(),[&](auto a,auto b){return pool[a].Score<pool[b].Score;});
                        for(const auto j:selected){if(j==restoreId&&!beam.empty())continue;beam.push_back(std::move(pool[j]));if(beam.size()==width)break;}
                    }
                    std::stable_sort(finalists.begin(),finalists.end(),[&](const Node& a,const Node& b){return leafKey(a)<leafKey(b);});
                    if(finalists.size()>8)finalists.resize(8);
                    }
                }
                // True original-input validation and the common certified
                // remnant quality choose the accepted result, never the beam
                // profile heuristic used to propose neighbourhood alternatives.
                for(std::size_t j=0;j<std::min<std::size_t>(3,finalists.size())&&!stop();++j)
                {
                    JobResult candidate;candidate.Output=incumbent;candidate.Output.Layouts[window.LayoutIndex]=std::move(finalists[j].Board);candidate.Output.Remaining.clear();
                    std::vector<std::size_t> counts(p.Parts.size());for(const auto& l:candidate.Output.Layouts)for(const auto& x:l.Placements)++counts[indices.at(x.PartID)];
                    for(std::size_t i=0;i<p.Parts.size();++i)if(counts[i]<p.Parts[i].Quantity)candidate.Output.Remaining.push_back({p.Parts[i].ID,p.Parts[i].Quantity-counts[i]});
                    candidate.Output.State=candidate.Output.Remaining.empty()?Status::Complete:Status::Partial;
                    AssessCandidate(p,candidate);
                    if(Eligible(candidate)&&Quality(candidate,p,o.Ranking)<Quality(best,p,o.Ranking))
                    {best=std::move(candidate);incumbent=best.Output;++accepted;}
                }
            }
            // Quantity-neutral hole changes are internal search states only.
            // Revisit up to two such changes with fresh real NFP queries, then
            // return a result only after the common strict quality acceptance.
            for(std::size_t ai=0;ai<archive.size()&&!stop();++ai)
            {
                const auto state=archive[ai];++archiveTried;
                if(Validate(p,state.Output).PlacedCount<best.Checked.PlacedCount)continue;
                const auto slots=archive.size()-ai;const double stateDeadline=elapsed()+(o.TimeLimitSeconds-elapsed())/slots;
                const auto stateAttemptEnd=work.PlacementAttempts+(o.MaxPlacementAttempts-work.PlacementAttempts)/slots;
                const auto stateStop=[&]{return stop()||elapsed()>=stateDeadline||work.PlacementAttempts>=stateAttemptEnd;};
                struct Target {std::size_t LayoutIndex,Index;Pose Transform;std::vector<std::size_t> Blockers;double RelativeOverlap=0;};std::vector<Target> targets;
                for(const auto& missing:state.Output.Remaining)
                {
                    const auto index=indices.at(missing.PartID);
                    for(std::size_t li=0;li<state.Output.Layouts.size()&&!stateStop();++li)
                        for(const auto& direction:directions[index])
                        {
                            if(stateStop())break;NfpPlacementLimits limits;limits.MaxCandidates=16;limits.MaxRawCandidates=4096;limits.MaxContactSegments=512;
                            limits.MaxBlockers=2;limits.MaxBlockedCandidates=256;limits.MaxAttempts=stateAttemptEnd-work.PlacementAttempts;
                            const auto query=generator.Find(state.Output.Layouts[li],index,direction.Angle,limits,stateStop);work.PlacementAttempts+=query.Attempts;
                            for(const auto pose:query.Fits)targets.push_back({li,index,pose,{}});
                            for(const auto& blocked:query.Blocked)targets.push_back({li,index,blocked.Transform,blocked.Blockers,blocked.OverlapArea/std::max(areas[index],p.Tolerance*p.Tolerance)});
                        }
                }
                std::stable_sort(targets.begin(),targets.end(),[](const Target& a,const Target& b)
                {return std::tuple(a.Blockers.size(),a.RelativeOverlap,a.Transform.Y,a.Transform.X)<std::tuple(b.Blockers.size(),b.RelativeOverlap,b.Transform.Y,b.Transform.X);});
                std::map<std::tuple<std::size_t,std::size_t,std::vector<std::size_t>,long long>,std::size_t> targetFamilies;std::vector<Target> selected;
                for(auto& target:targets)
                {
                    const auto key=std::tuple(target.LayoutIndex,target.Index,target.Blockers,std::llround(NormalizeAngle(target.Transform.AngleDegrees)*1e6));
                    if(targetFamilies[key]++>=2)continue;selected.push_back(std::move(target));if(selected.size()==8)break;
                }
                for(std::size_t ti=0;ti<selected.size()&&!stateStop();++ti)
                {
                    const auto& target=selected[ti];std::set<std::size_t> used;
                    for(const auto& l:state.Output.Layouts)for(const auto& x:l.Placements)if(x.PartID==p.Parts[target.Index].ID)used.insert(x.Instance);
                    std::size_t instance=0;while(instance<p.Parts[target.Index].Quantity&&used.contains(instance))++instance;if(instance>=p.Parts[target.Index].Quantity)continue;
                    const auto targetSlots=selected.size()-ti;const double targetDeadline=elapsed()+(stateDeadline-elapsed())/targetSlots;
                    const auto targetAttemptEnd=work.PlacementAttempts+(stateAttemptEnd-work.PlacementAttempts)/targetSlots;
                    const auto targetStop=[&]{return stateStop()||elapsed()>=targetDeadline;};
                    DisplacementChain chain(p,generator,indices,directions,work.PlacementAttempts,targetAttemptEnd,Config.MaxReleased,targetStop);
                    auto board=state.Output.Layouts[target.LayoutIndex];const bool complete=chain.Run(board,{p.Parts[target.Index].ID,instance,target.Transform,0,true});chainNodes+=chain.Nodes;
                    if(state.Hops<2)for(const auto& neutral:chain.Neutral)rememberNeutral(state.Output,target.LayoutIndex,neutral,state.Hops+1);
                    if(!complete||stop())continue;JobResult candidate;candidate.Output=makeCandidate(state.Output,target.LayoutIndex,std::move(board));AssessCandidate(p,candidate);
                    if(Eligible(candidate)&&Quality(candidate,p,o.Ranking)<Quality(best,p,o.Ranking))
                    {best=std::move(candidate);incumbent=best.Output;++accepted;++chainAccepted;break;}
                }
            }
            return finish(stop()?"budget":"finished");
        }
    };
}
PostProcessStage MakeNfpReassemblyStage(NfpReassemblySettings settings,std::shared_ptr<NfpCache> cache)
{
    if(!settings.MaxWindows||settings.MaxWindows>32||!settings.MaxReleased||settings.MaxReleased>10||
        !settings.BeamWidth||settings.BeamWidth>64||settings.MaxMissing>2||!settings.CandidatesPerAngle||settings.CandidatesPerAngle>256||
        !settings.BranchPerPart||settings.BranchPerPart>32||settings.ProfileSamples<4||settings.ProfileSamples>128)
        throw std::invalid_argument("Invalid bounded NFP reassembly settings.");
    return {"repair.nfp.reassembly",[settings,cache=std::move(cache)]{return std::make_unique<Reassembly>(settings,cache);}};
}
}
