#include "SheetNestingPlaneManufacturing.h"
#include "NestingInternal.h"
#include "NestingRectangleDirections.h"
#include "NestingNfpPlacement.h"
#include "NestingPlaneDomainGeometry.h"
#include "SheetNestingNfp.h"
#include "NestingClearancePlacement.h"
#include "NestingSheetTemplateReuse.h"
#include "NestingExecution.h"
#include "SheetNestingAxisCompaction.h"
#include "SheetNestingRotationCompaction.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <limits>
#include <optional>
#include <set>
#include <tuple>

namespace iCAX::SheetNesting
{
namespace
{
using namespace Detail;
using Mode=PlaneManufacturingMode;
bool Adjacent(const Shape& a,Pose pa,const Shape& b,Pose pb,double maximum,double tolerance)
{
    // Ask the shared material-distance predicate, including holes and circles.
    // Expanding the threshold by its own tolerance includes physical contact.
    return ShapesConflict(a,pa,b,pb,maximum+2*tolerance,tolerance,false);
}
bool Contains(Rect a,Rect b,double t)
{return b.X>=a.X-t&&b.Y>=a.Y-t&&b.X+b.Width<=a.X+a.Width+t&&b.Y+b.Height<=a.Y+a.Height+t;}
bool EqualRect(Rect a,Rect b,double t)
{return std::abs(a.X-b.X)<=t&&std::abs(a.Y-b.Y)<=t&&std::abs(a.Width-b.Width)<=t&&std::abs(a.Height-b.Height)<=t;}
struct Shared{double Length=0;std::vector<double> Directions;};
Shared SharedBoundary(const Shape& sa,Pose pa,const Shape& sb,Pose pb,double tolerance)
{
    Shared out;
    for(std::size_t i=0;i<sa.Outer.size();++i)
    {
        auto a=Transform(sa.Outer[i],pa),b=Transform(sa.Outer[(i+1)%sa.Outer.size()],pa);
        double dx=b.X-a.X,dy=b.Y-a.Y,len=std::hypot(dx,dy);if(len<=tolerance)continue;dx/=len;dy/=len;
        for(std::size_t j=0;j<sb.Outer.size();++j)
        {
            auto c=Transform(sb.Outer[j],pb),d=Transform(sb.Outer[(j+1)%sb.Outer.size()],pb);
            if(std::abs((c.X-a.X)*dy-(c.Y-a.Y)*dx)>tolerance||std::abs((d.X-a.X)*dy-(d.Y-a.Y)*dx)>tolerance)continue;
            auto lo=(c.X-a.X)*dx+(c.Y-a.Y)*dy,hi=(d.X-a.X)*dx+(d.Y-a.Y)*dy;if(lo>hi)std::swap(lo,hi);
            const double shared=std::min(len,hi)-std::max(0.,lo);
            if(shared>tolerance){out.Length+=shared;out.Directions.push_back(std::atan2(dy,dx));}
        }
    }
    return out;
}
std::vector<PlaneAdjacentPair> Pairs(const PlaneManufacturingTask& task)
{
    if(!task.Pairs.empty())return task.Pairs;
    std::vector<PlaneAdjacentPair> result;
    if(task.Mode==Mode::AdjacentRectangles)for(const auto& a:task.Input.Parts)
        if(!a.ID.empty()&&a.ID.back()=='A')
        {auto name=a.ID;name.back()='B';if(std::any_of(task.Input.Parts.begin(),task.Input.Parts.end(),[&](const Part& b){return b.ID==name;}))result.push_back({a.ID,name});}
    return result;
}
bool GroupMode(Mode m){return m==Mode::CommonEdgeArray||m==Mode::CommonEdgeDistance;}
bool Saw(const PlaneManufacturingTask& task,const Part& part)
{
    if(task.Mode==Mode::Guillotine)return true;
    if(!task.ProcessAssignments.empty())
    {
        const auto it=std::find_if(task.ProcessAssignments.begin(),task.ProcessAssignments.end(),[&](const auto& a){return a.PartID==part.ID;});
        return it!=task.ProcessAssignments.end()&&it->Process==PlanePartProcess::Saw;
    }
    if(task.ProcessClassification==PlaneSawMillClassification::RectanglesSawPolygonsMill)
        return Classify(part.Geometry,task.Input.Tolerance).Kind==ShapeKind::Rectangle;
    return std::find(task.SawPartIDs.begin(),task.SawPartIDs.end(),part.ID)!=task.SawPartIDs.end();
}
std::vector<std::string> Errors(const PlaneManufacturingTask& task)
{
    auto errors=CheckProblem(task.Input);
    if(task.Mode<Mode::AdjacentRectangles||task.Mode>Mode::SawMill)
        errors.push_back("Unknown manufacturing mode.");
    const auto finite=[](double n){return std::isfinite(n)&&n>=0;};
    if(!task.MaxCandidates||task.MaxCandidates>4096||!task.MaxGroupMembers||task.MaxGroupMembers>512||!finite(task.GroupGap)||!finite(task.MinimumSharedLength)||!finite(task.SawGap)||!finite(task.MillGap))errors.push_back("Invalid manufacturing limits/distances.");
    std::map<std::string,const Part*> parts;for(const auto& p:task.Input.Parts)parts[p.ID]=&p;
    std::set<std::string> paired;
    for(const auto& pair:Pairs(task))
    {
        if(!parts.contains(pair.First)||!parts.contains(pair.Second)||pair.First==pair.Second||!paired.insert(pair.First).second||!paired.insert(pair.Second).second||!finite(pair.MaximumSeparation)){errors.push_back("Invalid/ambiguous adjacent pair.");continue;}
        if(parts[pair.First]->Material!=parts[pair.Second]->Material)errors.push_back("Paired parts need compatible material.");
        if(!pair.AllowUnpairedRemainder&&parts[pair.First]->Quantity!=parts[pair.Second]->Quantity)errors.push_back("Unequal pair quantities need explicit unpaired-remainder permission.");
    }
    std::set<std::string> classified;
    for(const auto& id:task.SawPartIDs)if(!parts.contains(id)||!classified.insert(id).second)errors.push_back("Unknown or repeated saw part ID.");
    if(task.ProcessClassification!=PlaneSawMillClassification::Explicit&&task.ProcessClassification!=PlaneSawMillClassification::RectanglesSawPolygonsMill)
        errors.push_back("Unknown saw/mill classification rule.");
    if(task.ProcessClassification!=PlaneSawMillClassification::Explicit&&(!task.SawPartIDs.empty()||!task.ProcessAssignments.empty()))
        errors.push_back("Select either a geometric classification rule or explicit assignments.");
    if(!task.ProcessAssignments.empty())
    {
        if(!task.SawPartIDs.empty())errors.push_back("Select only one explicit process assignment table.");
        classified.clear();
        for(const auto& a:task.ProcessAssignments)
            if(!parts.contains(a.PartID)||!classified.insert(a.PartID).second||(a.Process!=PlanePartProcess::Saw&&a.Process!=PlanePartProcess::Mill))
                errors.push_back("Invalid or repeated process assignment.");
        if(classified.size()!=parts.size())errors.push_back("The process assignment table must classify every original.");
    }
    if(!std::isfinite(task.MinimumTemplateUtilization)||task.MinimumTemplateUtilization<0||task.MinimumTemplateUtilization>1)
        errors.push_back("Invalid manufacturing sheet-template utilization gate.");
    if(GroupMode(task.Mode)&&task.Input.PartGap>0&&!task.Input.AllowCommonEdges)errors.push_back("Common-edge mode with positive gap needs explicit AllowCommonEdges.");
    return errors;
}
std::vector<Rect> Leaves(const PlaneManufacturingTask& task,const PlaneManufacturingResult& result,const Layout& board,std::vector<std::string>& errors)
{
    const auto t=task.Input.Tolerance,m=task.Input.EdgeMargin;
    std::vector<Rect> leaves{{m,m,board.Width-2*m,board.Height-2*m}};
    for(const auto& cut:result.Cuts)
    {
        if(cut.StockID!=board.StockID||cut.StockInstance!=board.StockInstance)continue;
        auto it=std::find_if(leaves.begin(),leaves.end(),[&](Rect r){return EqualRect(r,cut.Region,t);});
        if(it==leaves.end()||!std::isfinite(cut.Position)||!std::isfinite(cut.Kerf)||cut.Kerf<0){errors.push_back("Cut does not address a current rectangular leaf.");continue;}
        if(cut.Kerf+t<std::max(task.Input.PartGap,task.SawGap))
            errors.push_back("Cut kerf is below the declared saw/part clearance.");
        auto region=*it;const auto low=cut.Vertical?region.X:region.Y,high=low+(cut.Vertical?region.Width:region.Height);
        if(cut.Position<=low+t||cut.Position+cut.Kerf>=high-t){errors.push_back("Degenerate or out-of-region guillotine cut.");continue;}
        leaves.erase(it);auto a=region,b=region;
        if(cut.Vertical){a.Width=cut.Position-region.X;b.X=cut.Position+cut.Kerf;b.Width=high-b.X;}
        else{a.Height=cut.Position-region.Y;b.Y=cut.Position+cut.Kerf;b.Height=high-b.Y;}
        leaves.push_back(a);leaves.push_back(b);
    }
    return leaves;
}
}
Validation ValidatePlaneManufacturingLayout(const PlaneManufacturingTask& task,const PlaneManufacturingResult& result)
{
    const auto& p=task.Input;auto checked=Validate(p,result.Output);auto errors=Errors(task);checked.Errors.insert(checked.Errors.end(),errors.begin(),errors.end());
    if(!checked.Errors.empty()){checked.Valid=false;return checked;}
    std::map<std::string,const Part*> parts;for(const auto& part:p.Parts)parts[part.ID]=&part;
    if(task.Mode==Mode::AdjacentRectangles)for(const auto& pair:Pairs(task))
    {
        const auto count=std::min(parts.at(pair.First)->Quantity,parts.at(pair.Second)->Quantity);
        using Found=std::pair<const Layout*,const Placement*>;std::map<std::size_t,Found> a,b;
        for(const auto& l:result.Output.Layouts)for(const auto& q:l.Placements){if(q.PartID==pair.First)a[q.Instance]={&l,&q};if(q.PartID==pair.Second)b[q.Instance]={&l,&q};}
        for(std::size_t i=0;i<count;++i)
        {
            if(a.contains(i)!=b.contains(i)){checked.Errors.push_back("Only one member of a hard pair was placed.");continue;}
            if(!a.contains(i))continue;
            if(a[i].first!=b[i].first||!Adjacent(parts.at(pair.First)->Geometry,a[i].second->Transform,parts.at(pair.Second)->Geometry,b[i].second->Transform,pair.MaximumSeparation,p.Tolerance))checked.Errors.push_back("Adjacent pair violated.");
        }
    }
    for(const auto& board:result.Output.Layouts)
    {
        if(GroupMode(task.Mode))
        {
            std::map<std::size_t,std::vector<const Placement*>> groups;
            for(const auto& q:board.Placements)groups[q.Group].push_back(&q);
            for(const auto& [id,members]:groups)
            {
                if(!id||members.size()>task.MaxGroupMembers)checked.Errors.push_back("Invalid common-edge group size.");
                std::vector<bool> reached(members.size());if(!reached.empty())reached[0]=true;std::vector<double> directions;
                for(std::size_t pass=0;pass<members.size();++pass)for(std::size_t i=0;i<members.size();++i)if(reached[i])for(std::size_t j=0;j<members.size();++j)if(!reached[j])
                {const auto shared=SharedBoundary(parts.at(members[i]->PartID)->Geometry,members[i]->Transform,parts.at(members[j]->PartID)->Geometry,members[j]->Transform,p.Tolerance);if(shared.Length+p.Tolerance>=task.MinimumSharedLength&&shared.Length>p.Tolerance){reached[j]=true;directions.insert(directions.end(),shared.Directions.begin(),shared.Directions.end());}}
                if(std::any_of(reached.begin(),reached.end(),[](bool x){return !x;}))checked.Errors.push_back("Common-edge group is disconnected or below minimum edge length.");
                if(task.ParallelSharedEdgesOnly)
                {
                    // Connectivity only needs a spanning tree, but the direction
                    // policy applies to every physical contact, including cycles.
                    directions.clear();
                    for(std::size_t i=0;i<members.size();++i)for(std::size_t j=0;j<i;++j)
                    {
                        const auto shared=SharedBoundary(parts.at(members[i]->PartID)->Geometry,members[i]->Transform,parts.at(members[j]->PartID)->Geometry,members[j]->Transform,p.Tolerance);
                        directions.insert(directions.end(),shared.Directions.begin(),shared.Directions.end());
                    }
                    if(!directions.empty())for(auto a:directions)if(std::abs(std::sin(a-directions.front()))>1e-7)checked.Errors.push_back("Common-edge group changes its declared shared-edge direction.");
                }
            }
            for(std::size_t i=0;i<board.Placements.size();++i)for(std::size_t j=0;j<i;++j)
            {
                const auto& a=board.Placements[i];const auto& b=board.Placements[j];
                if(a.Group!=b.Group&&ShapesConflict(parts.at(a.PartID)->Geometry,a.Transform,parts.at(b.PartID)->Geometry,b.Transform,std::max(p.PartGap,task.GroupGap),p.Tolerance,false))checked.Errors.push_back("Insufficient inter-group distance.");
            }
        }
        if(task.Mode==Mode::Guillotine||task.Mode==Mode::SawMill)
        {
            const auto leaves=Leaves(task,result,board,checked.Errors);std::vector<std::size_t> sawCounts(leaves.size());
            for(const auto& q:board.Placements)
            {
                const auto box=Bounds(parts.at(q.PartID)->Geometry,q.Transform);auto it=std::find_if(leaves.begin(),leaves.end(),[&](Rect r){return Contains(r,box,p.Tolerance);});
                if(it==leaves.end()){checked.Errors.push_back("A part crosses an actual guillotine cut.");continue;}
                if(Saw(task,*parts.at(q.PartID)))++sawCounts[it-leaves.begin()];
            }
            if(std::any_of(sawCounts.begin(),sawCounts.end(),[](auto n){return n>1;}))checked.Errors.push_back("Saw leaf contains more than one original.");
            if(task.Mode==Mode::SawMill)for(std::size_t i=0;i<board.Placements.size();++i)for(std::size_t j=0;j<i;++j)
            {const auto& a=board.Placements[i];const auto& b=board.Placements[j];const double gap=std::max(p.PartGap,Saw(task,*parts.at(a.PartID))&&Saw(task,*parts.at(b.PartID))?task.SawGap:task.MillGap);if(ShapesConflict(parts.at(a.PartID)->Geometry,a.Transform,parts.at(b.PartID)->Geometry,b.Transform,gap,p.Tolerance))checked.Errors.push_back("Saw/mill process distance violated.");}
        }
    }
    for(const auto& cut:result.Cuts)if(std::none_of(result.Output.Layouts.begin(),result.Output.Layouts.end(),[&](const Layout& l){return l.StockID==cut.StockID&&l.StockInstance==cut.StockInstance;}))checked.Errors.push_back("Cut refers to an unused sheet.");
    checked.Valid=checked.Errors.empty();return checked;
}

PlaneManufacturingResult RunPlaneManufacturingTask(const PlaneManufacturingTask& task,const Options& options)
{
    PlaneManufacturingResult result;auto& output=result.Output;const auto& p=task.Input;
    output.Diagnostics=Errors(task);if(!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<=0||!options.MaxPlacementAttempts)output.Diagnostics.push_back("Invalid process solve budget.");
    if(!output.Diagnostics.empty()){output.State=Status::InvalidInput;return result;}
    const auto start=std::chrono::steady_clock::now();std::size_t attempts=0,nextGroup=1;bool cancelled=false;
    const auto stop=[&]{if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;return cancelled||attempts>=options.MaxPlacementAttempts||std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>=options.TimeLimitSeconds;};
    std::map<std::string,std::size_t> indices;std::vector<std::vector<RectangleDirection>> directions;std::vector<std::size_t> used(p.Parts.size());
    for(std::size_t i=0;i<p.Parts.size();++i){indices[p.Parts[i].ID]=i;auto d=FindRectangleDirections(p.Parts[i],p.Tolerance,stop);directions.push_back(d.Supported?d.Directions:std::vector<RectangleDirection>{});}
    const auto pairs=Pairs(task);std::map<std::size_t,std::pair<std::size_t,PlaneAdjacentPair>> mates;
    for(const auto& pair:pairs){auto a=indices.at(pair.First),b=indices.at(pair.Second);mates[a]={b,pair};mates[b]={a,pair};}
    for(const auto& stock:p.Stocks)for(std::size_t sheet=0;sheet<stock.Quantity&&!stop();++sheet)
    {
        Layout board{stock.ID,sheet,stock.Width,stock.Height,{},0};
        const auto initialCutCount=result.Cuts.size();
        MaxRects free(stock.Width-2*p.EdgeMargin+p.PartGap,stock.Height-2*p.EdgeMargin+p.PartGap,p.Tolerance);
        std::vector<Rect> leaves{{p.EdgeMargin,p.EdgeMargin,stock.Width-2*p.EdgeMargin,stock.Height-2*p.EdgeMargin}};
        const auto fits=[&](const std::vector<Member>& members,double gap)
        {
            std::vector<std::size_t> needs(p.Parts.size());
            for(std::size_t i=0;i<members.size();++i)
            {
                if(stop())return false;++attempts;const auto& m=members[i];const auto& part=p.Parts[m.PartIndex];
                if(part.Material!=stock.Material||++needs[m.PartIndex]>part.Quantity-used[m.PartIndex]||!InsideSheet(part.Geometry,m.Transform,stock.Width,stock.Height,p.EdgeMargin,p.Tolerance))return false;
                for(const auto& q:board.Placements)if(ShapesConflict(part.Geometry,m.Transform,p.Parts[indices.at(q.PartID)].Geometry,q.Transform,gap,p.Tolerance,false))return false;
                for(std::size_t j=0;j<i;++j)if(ShapesConflict(part.Geometry,m.Transform,p.Parts[members[j].PartIndex].Geometry,members[j].Transform,p.PartGap,p.Tolerance,GroupMode(task.Mode)))return false;
            }
            return true;
        };
        const auto commit=[&](const std::vector<Member>& members,std::size_t group)
        {
            std::optional<Rect> groupBox;
            for(const auto& m:members){const auto& part=p.Parts[m.PartIndex];board.Placements.push_back({part.ID,used[m.PartIndex]++,m.Transform,group,false});board.MaterialArea+=Area(part.Geometry);auto b=Bounds(part.Geometry,m.Transform);free.Occupy({b.X-p.EdgeMargin,b.Y-p.EdgeMargin,b.Width+p.PartGap,b.Height+p.PartGap});
                if(!groupBox)groupBox=b;else{const auto x=std::min(groupBox->X,b.X),y=std::min(groupBox->Y,b.Y);groupBox=Rect{x,y,std::max(groupBox->X+groupBox->Width,b.X+b.Width)-x,std::max(groupBox->Y+groupBox->Height,b.Y+b.Height)-y};}}
            if(GroupMode(task.Mode)&&groupBox){auto b=*groupBox;const auto gap=std::max(task.GroupGap,p.PartGap);free.Occupy({b.X-p.EdgeMargin,b.Y-p.EdgeMargin,b.Width+gap,b.Height+gap});}
        };
        if(task.Mode==Mode::Guillotine||task.Mode==Mode::SawMill)
        {
            const auto saw=[&](std::size_t i){return Saw(task,p.Parts[i]);};
            const double kerf=std::max(p.PartGap,task.SawGap);
            const auto split=[&](Rect region,bool vertical,double at)
            {
                const auto startAxis=vertical?region.X:region.Y,end=startAxis+(vertical?region.Width:region.Height);
                if(at<=startAxis+p.Tolerance||at+kerf>=end-p.Tolerance)return;
                auto it=std::find_if(leaves.begin(),leaves.end(),[&](Rect r){return EqualRect(r,region,p.Tolerance);});if(it==leaves.end())return;
                leaves.erase(it);result.Cuts.push_back({stock.ID,sheet,region,vertical,at,kerf});auto a=region,b=region;
                if(vertical){a.Width=at-region.X;b.X=at+kerf;b.Width=end-b.X;}else{a.Height=at-region.Y;b.Y=at+kerf;b.Height=end-b.Y;}leaves.push_back(a);leaves.push_back(b);
            };
            while(!stop())
            {
                std::vector<Member> best;Rect source;double bestArea=0;
                std::vector<double> spans;double thickness=0;bool column=false;
                for(const auto region:leaves)
                {
                    if(stop())break;
                    bool occupied=false;for(const auto& q:board.Placements)if(Contains(region,Bounds(p.Parts[indices.at(q.PartID)].Geometry,q.Transform),p.Tolerance)){occupied=true;break;}if(occupied)continue;
                    for(std::size_t anchor=0;anchor<p.Parts.size()&&!stop();++anchor)if(saw(anchor)&&used[anchor]<p.Parts[anchor].Quantity)
                    for(const auto& ad:directions[anchor])
                    for(bool vertical:{false,true})
                    {
                        if(stop())break;
                        const double thick=vertical?ad.Width:ad.Height;
                        const double capacity=vertical?region.Height:region.Width;
                        if(thick>(vertical?region.Width:region.Height)+p.Tolerance)continue;
                        struct Choice{std::size_t PartIndex;RectangleDirection Direction;double Span;};
                        std::vector<Choice> choices;
                        for(std::size_t pi=0;pi<p.Parts.size();++pi)
                        {
                            if(!saw(pi)||p.Parts[pi].Material!=stock.Material||used[pi]>=p.Parts[pi].Quantity)continue;
                            for(const auto& d:directions[pi])
                            {
                                const auto span=vertical?d.Height:d.Width;
                                if(std::abs((vertical?d.Width:d.Height)-thick)>p.Tolerance||span>capacity+p.Tolerance)continue;
                                if(std::none_of(choices.begin(),choices.end(),[&](const Choice& c){return c.PartIndex==pi&&std::abs(c.Span-span)<=p.Tolerance;}))choices.push_back({pi,d,span});
                            }
                        }
                        std::stable_sort(choices.begin(),choices.end(),[](const Choice& a,const Choice& b){return a.Span>b.Span;});
                        std::vector<Member> members;std::vector<double> memberSpans;auto trialUsed=used;
                        std::size_t nodes=0;const auto nodeLimit=std::min<std::size_t>(1024,task.MaxCandidates*8);
                        std::function<void(std::size_t,double,double)> search;
                        search=[&](std::size_t ci,double length,double area)
                        {
                            if(stop()||nodes++>=nodeLimit)return;++attempts;
                            if(area>bestArea&&!members.empty()&&fits(members,std::max(p.PartGap,task.SawGap)))
                            {best=members;source=region;bestArea=area;spans=memberSpans;thickness=thick;column=vertical;}
                            if(ci>=choices.size()||members.size()>=task.MaxCandidates)return;
                            const auto& choice=choices[ci];const auto pi=choice.PartIndex;
                            const double room=capacity-length+(members.empty()?kerf:0);
                            const auto byLength=static_cast<std::size_t>(std::max(0.,std::floor((room+p.Tolerance)/(choice.Span+kerf))));
                            const auto maximum=std::min({p.Parts[pi].Quantity-trialUsed[pi],byLength,task.MaxCandidates-members.size()});
                            // Finite count backtracking repairs greedy strip tails.
                            // Shared trialUsed prevents duplicate directions overusing a spec.
                            std::vector<std::size_t> counts;
                            for(std::size_t delta=0;delta<=4&&delta<=maximum;++delta)counts.push_back(maximum-delta);
                            if(counts.empty()||counts.back()!=0)counts.push_back(0);
                            for(const auto count:counts)
                            {
                                if(stop()||nodes>=nodeLimit)break;
                                const auto oldSize=members.size();auto nextLength=length;
                                for(std::size_t n=0;n<count;++n)
                                {
                                    const double at=nextLength+(members.empty()?0:kerf);auto pose=choice.Direction.Offset;
                                    pose.X+=region.X+(vertical?0:at);pose.Y+=region.Y+(vertical?at:0);
                                    members.push_back({pi,pose});memberSpans.push_back(choice.Span);nextLength=at+choice.Span;++trialUsed[pi];
                                }
                                search(ci+1,nextLength,area+count*Area(p.Parts[pi].Geometry));
                                trialUsed[pi]-=count;members.resize(oldSize);memberSpans.resize(oldSize);
                            }
                        };
                        search(0,0,0);
                    }
                }
                if(best.empty()||cancelled)break;
                split(source,column,column?source.X+thickness:source.Y+thickness);
                auto strip=source;
                if(column){if(source.X+thickness+kerf<source.X+source.Width-p.Tolerance)strip.Width=thickness;}
                else if(source.Y+thickness+kerf<source.Y+source.Height-p.Tolerance)strip.Height=thickness;
                for(std::size_t i=0;i<spans.size();++i)
                {
                    const double at=(column?strip.Y:strip.X)+spans[i];split(strip,!column,at);
                    const auto end=(column?strip.Y+strip.Height:strip.X+strip.Width);
                    if(at+kerf>=end-p.Tolerance)break;
                    if(column){strip.Y=at+kerf;strip.Height=end-strip.Y;}
                    else{strip.X=at+kerf;strip.Width=end-strip.X;}
                }
                commit(best,nextGroup++);
            }
            if(task.Mode==Mode::SawMill)for(std::size_t pi=0;pi<p.Parts.size()&&!stop();++pi)if(!saw(pi)&&p.Parts[pi].Material==stock.Material)
            {
                while(used[pi]<p.Parts[pi].Quantity&&!stop())
                {
                    bool placed=false;
                    for(const auto region:leaves)for(auto angle:CandidateAngles(p.Parts[pi],p.Tolerance))
                    {
                        if(stop())break;const auto b=Bounds(p.Parts[pi].Geometry,{0,0,angle});
                        std::vector<Point> points{{region.X,region.Y}};
                        for(const auto& q:board.Placements){auto box=Bounds(p.Parts[indices.at(q.PartID)].Geometry,q.Transform);points.push_back({box.X+box.Width+std::max(p.PartGap,task.MillGap),box.Y});points.push_back({box.X,box.Y+box.Height+std::max(p.PartGap,task.MillGap)});}
                        for(auto point:points)
                        {
                            if(placed||stop())break;Pose pose{point.X-b.X,point.Y-b.Y,angle};
                            if(Contains(region,Bounds(p.Parts[pi].Geometry,pose),p.Tolerance)&&fits({{pi,pose}},std::max(p.PartGap,task.MillGap))){commit({{pi,pose}},nextGroup++);placed=true;}
                        }
                        if(placed)break;
                    }
                    if(!placed)break;
                }
            }
        }
        else if(task.Mode==Mode::AdjacentRectangles)
        {
            Problem pairScene=p;pairScene.AllowCommonEdges=false;
            auto pairCache=std::make_shared<NfpCache>();
            ClearanceNfpPlacement polygonPlacement(pairScene,stock,pairCache,stop);
            while(!stop())
            {
                std::vector<Member> best;double score=-1;
                for(std::size_t pi=0;pi<p.Parts.size()&&!stop();++pi)
                {
                    if(used[pi]>=p.Parts[pi].Quantity||p.Parts[pi].Material!=stock.Material)continue;
                    auto mate=mates.find(pi);bool paired=task.Mode==Mode::AdjacentRectangles&&mate!=mates.end()&&used[pi]<std::min(p.Parts[pi].Quantity,p.Parts[mate->second.first].Quantity);
                    if(paired&&pi>mate->second.first)continue;
                    if(polygonPlacement.Ready()&&(directions[pi].empty()||(paired&&directions[mate->second.first].empty())))
                    {
                        const auto query=[&](const Layout& environment,std::size_t index,double angle)
                        {
                            NfpPlacementLimits limits;limits.MaxCandidates=task.MaxCandidates;limits.MaxAttempts=options.MaxPlacementAttempts-attempts;
                            auto candidates=polygonPlacement.Find(environment,index,angle,limits,stop);attempts+=candidates.Attempts;return candidates;
                        };
                        for(const auto angle:CandidateAngles(p.Parts[pi],p.Tolerance))
                        {
                            if(stop())break;const auto firsts=query(board,pi,angle);
                            for(const auto first:firsts.Fits)
                            {
                                if(stop())break;
                                if(!paired)
                                {
                                    if(fits({{pi,first}},p.PartGap)&&Area(p.Parts[pi].Geometry)>score)
                                    {score=Area(p.Parts[pi].Geometry);best={{pi,first}};}
                                    continue;
                                }
                                const auto pj=mate->second.first;
                                auto environment=board;environment.Placements.push_back({p.Parts[pi].ID,used[pi],first,nextGroup,false});
                                for(const auto secondAngle:CandidateAngles(p.Parts[pj],p.Tolerance))
                                {
                                    if(stop())break;const auto seconds=query(environment,pj,secondAngle);
                                    for(const auto second:seconds.Fits)
                                    {
                                        if(stop())break;
                                        if(!Adjacent(p.Parts[pi].Geometry,first,p.Parts[pj].Geometry,second,mate->second.second.MaximumSeparation,p.Tolerance))continue;
                                        const auto area=Area(p.Parts[pi].Geometry)+Area(p.Parts[pj].Geometry);
                                        if(area>score&&fits({{pi,first},{pj,second}},p.PartGap))
                                        {score=area;best={{pi,first},{pj,second}};}
                                    }
                                }
                            }
                        }
                    }
                    for(const auto& d:directions[pi])for(auto space:free.FreeRectangles())
                    {
                        if(stop())break;Pose first=d.Offset;first.X+=space.X+p.EdgeMargin;first.Y+=space.Y+p.EdgeMargin;
                        std::vector<Member> candidate{{pi,first}};
                        if(paired)
                        {
                            auto pj=mate->second.first;if(used[pj]>=p.Parts[pj].Quantity)continue;
                            auto copy=free;copy.Occupy({space.X,space.Y,d.Width+p.PartGap,d.Height+p.PartGap});
                            std::optional<Member> second;double compact=std::numeric_limits<double>::infinity();std::size_t tested=0;
                            for(const auto& e:directions[pj])for(auto remaining:copy.FreeRectangles())
                            {
                                if(stop()||tested++>=task.MaxCandidates)break;Pose pose=e.Offset;pose.X+=remaining.X+p.EdgeMargin;pose.Y+=remaining.Y+p.EdgeMargin;
                                const auto a=Bounds(p.Parts[pi].Geometry,first),b=Bounds(p.Parts[pj].Geometry,pose);
                                if(!Adjacent(p.Parts[pi].Geometry,first,p.Parts[pj].Geometry,pose,mate->second.second.MaximumSeparation,p.Tolerance))continue;
                                const double combined=(std::max(a.X+a.Width,b.X+b.Width)-std::min(a.X,b.X))*(std::max(a.Y+a.Height,b.Y+b.Height)-std::min(a.Y,b.Y));
                                if(combined<compact&&fits({{pi,first},{pj,pose}},p.PartGap)){compact=combined;second=Member{pj,pose};}
                            }
                            if(!second)continue;candidate.push_back(*second);
                        }
                        if(!fits(candidate,p.PartGap))continue;
                        double value=0;for(const auto& m:candidate)value+=Area(p.Parts[m.PartIndex].Geometry);
                        if(value>score){score=value;best=std::move(candidate);}
                    }
                    if(!best.empty()&&task.Mode==Mode::AdjacentRectangles)break;
                }
                if(best.empty())break;commit(best,nextGroup++);
            }
        }
        else
        {
            // Joining uses unchanged originals. Opening a group uses a separate
            // full-clearance fixed-obstacle domain; moving contours and real
            // stock margins never change. Mixed materials are filtered inside
            // the shared placement service rather than invalidating the scene.
            Problem inner=p;inner.PartGap=0;inner.AllowCommonEdges=true;
            Problem outer=inner;const double gap=std::max(p.PartGap,task.GroupGap);outer.PartGap=gap;outer.AllowCommonEdges=false;
            auto cache=std::make_shared<NfpCache>();ClearanceNfpPlacement normal(inner,stock,cache,stop),separate(outer,stock,cache,stop);
            if(!normal.Ready()||!separate.Ready())output.Diagnostics.push_back("Common-edge NFP domain unavailable for this geometry/part count; unsupported demand is retained.");
            while(normal.Ready()&&!stop())
            {
                bool placed=false;std::map<std::size_t,std::vector<const Placement*>> groups;for(const auto& q:board.Placements)groups[q.Group].push_back(&q);
                for(std::size_t pi=0;pi<p.Parts.size()&&!placed&&!stop();++pi)if(used[pi]<p.Parts[pi].Quantity&&p.Parts[pi].Material==stock.Material)
                for(auto angle:CandidateAngles(p.Parts[pi],p.Tolerance))
                {
                    if(stop()||placed)break;
                    for(int domain=0;domain<2&&!placed&&!stop();++domain)
                    {
                        auto& generator=domain?separate:normal;if(!generator.Ready())continue;
                        NfpPlacementLimits limits;limits.MaxCandidates=task.MaxCandidates;limits.MaxAttempts=options.MaxPlacementAttempts-attempts;
                        auto query=generator.Find(board,pi,angle,limits,stop);attempts+=query.Attempts;
                        for(auto pose:query.Fits)
                        {
                            if(stop())break;std::size_t group=0;
                            if(domain==0)for(const auto& [id,members]:groups)if(members.size()<task.MaxGroupMembers)
                            {for(const auto* q:members){const auto length=SharedBoundary(p.Parts[pi].Geometry,pose,p.Parts[indices.at(q->PartID)].Geometry,q->Transform,p.Tolerance).Length;if(length>p.Tolerance&&length+p.Tolerance>=task.MinimumSharedLength){group=id;break;}}if(group)break;}
                            if(domain==0&&!board.Placements.empty()&&!group)continue;
                            if(!group)group=nextGroup;
                            // Validate the prospective group locally, then exact
                            // original gap/material checks below; no inventory commit.
                            bool safe=InsideSheet(p.Parts[pi].Geometry,pose,stock.Width,stock.Height,p.EdgeMargin,p.Tolerance);
                            for(const auto& q:board.Placements)if(ShapesConflict(p.Parts[pi].Geometry,pose,p.Parts[indices.at(q.PartID)].Geometry,q.Transform,q.Group==group?p.PartGap:gap,p.Tolerance,q.Group==group))safe=false;
                            if(!safe)continue;
                            // Parallel-edge policy is checked on all new contacts.
                            if(task.ParallelSharedEdgesOnly&&groups.contains(group))
                            {std::optional<double> direction;for(const auto* a:groups[group])for(const auto* b:groups[group])for(auto d:SharedBoundary(p.Parts[indices.at(a->PartID)].Geometry,a->Transform,p.Parts[indices.at(b->PartID)].Geometry,b->Transform,p.Tolerance).Directions)if(a!=b)direction=d;
                             for(const auto* q:groups[group])for(auto d:SharedBoundary(p.Parts[pi].Geometry,pose,p.Parts[indices.at(q->PartID)].Geometry,q->Transform,p.Tolerance).Directions){if(direction&&std::abs(std::sin(*direction-d))>1e-7)safe=false;direction=d;}}
                            if(!safe)continue;commit({{pi,pose}},group);if(group==nextGroup)++nextGroup;placed=true;break;
                        }
                    }
                }
                if(!placed)break;
            }
        }
        if(board.Placements.empty()){result.Cuts.resize(initialCutCount);break;}output.Layouts.push_back(std::move(board));
        if(task.EnableSheetTemplateReuse&&sheet+1<stock.Quantity&&!stop())
        {
            output.Remaining.clear();
            for(std::size_t i=0;i<p.Parts.size();++i)if(used[i]<p.Parts[i].Quantity)output.Remaining.push_back({p.Parts[i].ID,p.Parts[i].Quantity-used[i]});
            output.State=output.Remaining.empty()?Status::Complete:Status::Partial;
            // The complete typed prefix is the publication boundary. Generic
            // copies and their process evidence remain private until both the
            // source and the expanded prefix pass the dedicated validator.
            if(ValidatePlaneManufacturingLayout(task,result).Valid&&!stop())
            {
                auto draft=result;auto draftUsed=used;auto draftGroup=nextGroup;auto nextSheet=sheet+1;
                const auto layoutCount=output.Layouts.size();const auto prototype=output.Layouts.back();
                std::vector<PlaneCut> sourceCuts;
                for(const auto& cut:result.Cuts)if(cut.StockID==prototype.StockID&&cut.StockInstance==prototype.StockInstance)sourceCuts.push_back(cut);
                auto budget=options;budget.EnableBasicCompaction=false;
                budget.TimeLimitSeconds=options.TimeLimitSeconds-std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                budget.MaxPlacementAttempts=options.MaxPlacementAttempts-attempts;
                budget.Cancel=[&]{if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;return cancelled;};
                const auto reused=AppendSheetTemplateCopies(p,prototype,stock,nextSheet,draftUsed,draftGroup,draft.Output,budget,task.MinimumTemplateUtilization);
                attempts+=reused.Attempts;cancelled=cancelled||reused.Cancelled;
                for(std::size_t li=layoutCount;li<draft.Output.Layouts.size();++li)
                    for(auto cut:sourceCuts){cut.StockInstance=draft.Output.Layouts[li].StockInstance;draft.Cuts.push_back(cut);}
                std::size_t published=0;
                if(reused.Copies)
                {
                    if(ValidatePlaneManufacturingLayout(task,draft).Valid)
                    {result=std::move(draft);used=std::move(draftUsed);nextGroup=draftGroup;sheet=nextSheet-1;published=reused.Copies;}
                    else output.Diagnostics.push_back("plane.manufacturing sheet-template process-invalid draft rejected; original typed prefix retained");
                }
                if(!reused.Diagnostic.empty())output.Diagnostics.push_back("plane.manufacturing "+reused.Diagnostic+"; process-validated copies="+std::to_string(published));
            }
        }
    }
    output.Remaining.clear();
    for(std::size_t i=0;i<p.Parts.size();++i)if(used[i]<p.Parts[i].Quantity)output.Remaining.push_back({p.Parts[i].ID,p.Parts[i].Quantity-used[i]});
    output.State=cancelled?Status::Cancelled:output.Remaining.empty()?Status::Complete:Status::Partial;output.Stats.PlacementAttempts=attempts;output.Stats.ElapsedSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    auto checked=ValidatePlaneManufacturingLayout(task,result);output.GeometryValidated=checked.Valid;if(!checked.Valid){output.State=Status::Failed;output.Diagnostics.insert(output.Diagnostics.end(),checked.Errors.begin(),checked.Errors.end());}
    output.Diagnostics.push_back("plane.manufacturing mode="+std::to_string(static_cast<int>(task.Mode))+"; explicit process validator and original demand");return result;
}

PlaneManufacturingResult OptimizePlaneManufacturingLayout(const PlaneManufacturingTask& task,
    const PlaneManufacturingResult& initial,const Options& options,PlaneManufacturingOptimizationSettings settings)
{
    using Clock=std::chrono::steady_clock;const auto start=Clock::now();auto best=initial;
    const auto checked=ValidatePlaneManufacturingLayout(task,initial);
    if(!checked.Valid||!checked.Complete)
    {best.Output.Diagnostics.push_back("Manufacturing optimization needs a complete process-valid incumbent.");return best;}
    if(!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<=0||!options.MaxPlacementAttempts||!settings.MaxIterations||settings.MaxIterations>64)
    {best.Output.Diagnostics.push_back("Invalid manufacturing optimization budget/settings; incumbent retained.");return best;}
    std::size_t attempts=0,accepted=0,rejected=0;bool cancelled=false;
    const auto stop=[&]
    {
        if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;
        return cancelled||attempts>=options.MaxPlacementAttempts||std::chrono::duration<double>(Clock::now()-start).count()>=options.TimeLimitSeconds;
    };
    if(stop()){if(cancelled)best.Output.State=Status::Cancelled;return best;}
    JobResult incumbent;incumbent.Output=best.Output;AssessCandidate(task.Input,incumbent);
    if(!Eligible(incumbent))return best;
    auto key=Quality(incumbent,task.Input,options.Ranking);
    std::vector<PostProcessStage> stages;
    if(settings.Translate)stages.push_back(MakeAxisCompactionStage());
    if(settings.Rotate)stages.push_back(MakeRotationCompactionStage());
    for(std::size_t iteration=0;iteration<settings.MaxIterations&&!stop();++iteration)
    {
        bool improved=false;
        for(const auto& stage:stages)
        {
            if(stop())break;auto budget=options;budget.EnableBasicCompaction=false;
            budget.TimeLimitSeconds=options.TimeLimitSeconds-std::chrono::duration<double>(Clock::now()-start).count();
            budget.MaxPlacementAttempts=options.MaxPlacementAttempts-attempts;
            budget.Cancel=[&]{if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;return cancelled;};
            PlaneManufacturingResult candidate=best;
            try{candidate.Output=stage.Factory()->Run(task.Input,best.Output,budget);}
            catch(...){++rejected;best.Output.Diagnostics.push_back("Manufacturing optimization stage failed safely; incumbent retained.");continue;}
            attempts+=std::min(candidate.Output.Stats.PlacementAttempts,budget.MaxPlacementAttempts);
            // Ordinary geometry refinement is only a proposal. In particular,
            // common-edge connectivity, hard pairs and fixed cut leaves are
            // not inferred from generic group tags or generic Validate().
            const auto typed=ValidatePlaneManufacturingLayout(task,candidate);
            if(!typed.Valid||!typed.Complete){++rejected;continue;}
            JobResult measured;measured.Output=candidate.Output;AssessCandidate(task.Input,measured);
            if(!Eligible(measured)){++rejected;continue;}
            const auto candidateKey=Quality(measured,task.Input,options.Ranking);
            if(candidateKey<key)
            {candidate.Output=std::move(measured.Output);best=std::move(candidate);key=candidateKey;improved=true;++accepted;}
            else ++rejected;
        }
        if(!improved)break;
    }
    best.Output.Stats.PlacementAttempts=initial.Output.Stats.PlacementAttempts+attempts;
    best.Output.Stats.ElapsedSeconds=initial.Output.Stats.ElapsedSeconds+std::chrono::duration<double>(Clock::now()-start).count();
    best.Output.Stats.CandidatePatterns=initial.Output.Stats.CandidatePatterns;
    best.Output.Stats.CombinationGroups=initial.Output.Stats.CombinationGroups;
    best.Output.Stats.CircleLatticeGroups=initial.Output.Stats.CircleLatticeGroups;
    best.Output.Stats.GapFilledParts=initial.Output.Stats.GapFilledParts;
    best.Output.GeometryValidated=true;if(cancelled)best.Output.State=Status::Cancelled;
    best.Output.Diagnostics.push_back("plane.manufacturing optimization; dedicated-validator accepted="+std::to_string(accepted)+"; retained/rejected="+std::to_string(rejected));
    return best;
}
}
