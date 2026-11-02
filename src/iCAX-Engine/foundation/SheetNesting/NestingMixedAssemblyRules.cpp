#include "NestingExecution.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>

namespace iCAX::SheetNesting::Detail
{
Result RunRectangleLookahead(const Problem&,const Options&);
Result RunSeamAssemblyIf(const Problem&,const Options&,const std::function<bool(const Result&)>&);
namespace
{
    struct Unit {std::string ID;std::vector<std::pair<std::size_t,Pose>> Members;};
    Pose Compose(Pose a,Pose b)
    {const auto point=Transform({b.X,b.Y},a);return {point.X,point.Y,NormalizeAngle(a.AngleDegrees+b.AngleDegrees)};}
    Shape Hull(const Clipper2Lib::PathsD& paths)
    {
        std::vector<Point> points;
        for(const auto& path:paths)for(const auto& point:path)points.push_back({point.x,point.y});
        std::sort(points.begin(),points.end(),[](auto a,auto b){return a.X!=b.X?a.X<b.X:a.Y<b.Y;});
        points.erase(std::unique(points.begin(),points.end(),[](auto a,auto b){return a.X==b.X&&a.Y==b.Y;}),points.end());
        const auto cross=[](Point a,Point b,Point c){return (static_cast<long double>(b.X)-a.X)*(c.Y-a.Y)-(static_cast<long double>(b.Y)-a.Y)*(c.X-a.X);};
        Shape result;if(points.size()<3)return result;
        auto& hull=result.Outer;
        for(auto point:points){while(hull.size()>1&&cross(hull[hull.size()-2],hull.back(),point)<=0)hull.pop_back();hull.push_back(point);}
        const auto lower=hull.size()+1;
        for(std::size_t i=points.size()-1;i-->0;){const auto point=points[i];while(hull.size()>=lower&&cross(hull[hull.size()-2],hull.back(),point)<=0)hull.pop_back();hull.push_back(point);}
        hull.pop_back();return result;
    }
    bool FilledBasic(const Problem& p,const Result& r)
    {
        if(r.Layouts.size()!=1)return false;
        Clipper2Lib::PathsD paths;double material=0,perimeter=0;
        for(const auto& placed:r.Layouts[0].Placements)
        {
            const auto part=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const auto& a){return a.ID==placed.PartID;});
            if(part==p.Parts.end())return false;material+=Area(part->Geometry);
            const auto append=[&](const auto& ring)
            {
                auto& path=paths.emplace_back();
                for(std::size_t i=0;i<ring.size();++i)
                {const auto a=Transform(ring[i],placed.Transform),b=Transform(ring[(i+1)%ring.size()],placed.Transform);path.emplace_back(a.X,a.Y);perimeter+=std::hypot(b.X-a.X,b.Y-a.Y);}
            };
            append(part->Geometry.Outer);for(const auto& ring:part->Geometry.Holes)append(ring);
        }
        const auto shape=Hull(paths);const auto kind=Classify(shape,p.Tolerance).Kind;
        return (kind==ShapeKind::Rectangle||kind==ShapeKind::Parallelogram)&&std::abs(Area(shape)-material)<=perimeter*p.Tolerance;
    }
    Result RunMixedCells(const Problem& p,const Options& o,bool columns,bool lookahead=false)
    {
        Problem cells=p;std::map<std::string,std::pair<Point,double>> circles;
        for(auto& part:cells.Parts)if(part.Geometry.Radius>0)
        {
            const auto& original=*std::find_if(p.Parts.begin(),p.Parts.end(),[&](const auto& a){return a.ID==part.ID;});
            const auto angles=CandidateAngles(original,p.Tolerance);const auto angle=angles.front();
            circles[part.ID]={Transform(original.Geometry.Center,{0,0,angle}),angle};
            const auto r=part.Geometry.Radius;part.Geometry={};part.Geometry.Outer={{-r,-r},{r,-r},{r,r},{-r,r}};
            part.FreeRotation=false;part.AllowedAnglesDegrees={0};
        }
        auto result=lookahead?RunRectangleLookahead(cells,o):RunRectanglePatternShelves(cells,o,columns);
        for(auto& layout:result.Layouts)
        {
            layout.MaterialArea=0;
            for(auto& placed:layout.Placements)
            {
                if(const auto it=circles.find(placed.PartID);it!=circles.end())
                {
                    const auto center=Transform({0,0},placed.Transform);
                    placed.Transform={center.X-it->second.first.X,center.Y-it->second.first.Y,it->second.second};
                }
                const auto part=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const auto& a){return a.ID==placed.PartID;});
                layout.MaterialArea+=Area(part->Geometry);
            }
        }
        return result;
    }
}
Result RunMixedAssemblyRules(const Problem& p,const Options& options)
{
    constexpr bool useLookahead=true;
    const auto begin=std::chrono::steady_clock::now();bool cancelled=false;
    const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();};
    const auto cancel=[&]{if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;return cancelled;};
    std::size_t attempts=0;
    const auto stop=[&]{return cancel()||elapsed()>=options.TimeLimitSeconds||attempts>=options.MaxPlacementAttempts;};
    if(stop())
    {
        Result empty;empty.State=cancelled?Status::Cancelled:Status::Partial;
        for(const auto& part:p.Parts)if(part.Quantity)empty.Remaining.push_back({part.ID,part.Quantity});
        empty.Stats.PlacementAttempts=attempts;empty.Stats.ElapsedSeconds=elapsed();return empty;
    }
    Problem working=p;std::vector<Unit> units;
    std::vector<std::size_t> indices;
    for(std::size_t i=0;i<p.Parts.size();++i)if(p.Parts[i].Quantity&&p.Parts[i].Geometry.Radius==0)indices.push_back(i);
    const auto discoverStop=[&]{return stop()||elapsed()>=options.TimeLimitSeconds*.35||attempts>=options.MaxPlacementAttempts/3;};
    std::vector<std::size_t> parent(p.Parts.size());std::iota(parent.begin(),parent.end(),0);
    const auto find=[&](std::size_t i){while(parent[i]!=i)i=parent[i];return i;};
    const auto stockFor=[&](const std::string& material)->const Stock*
    {
        const Stock* best=nullptr;
        for(const auto& stock:p.Stocks)if(stock.Quantity&&stock.Material==material&&(!best||stock.Width*stock.Height>best->Width*best->Height))best=&stock;
        return best;
    };
    const auto assemblyCap=std::min<std::size_t>(12,options.MaxAssemblyMembers);
    if(options.EnableCombinations&&assemblyCap>=2&&p.PartGap==0&&indices.size()>=2&&indices.size()<=48)
    {
        for(std::size_t ai=0;ai<indices.size()&&!discoverStop();++ai)
            for(std::size_t bi=ai+1;bi<indices.size()&&!discoverStop();++bi)
            {
                const auto a=indices[ai],b=indices[bi];
                if(p.Parts[a].Material!=p.Parts[b].Material||find(a)==find(b))continue;
                const auto stock=stockFor(p.Parts[a].Material);if(!stock)continue;
                Problem local;local.Tolerance=p.Tolerance;local.EdgeMargin=p.EdgeMargin;local.Parts={p.Parts[a],p.Parts[b]};
                for(auto& part:local.Parts)part.Quantity=1;
                local.Stocks={*stock};local.Stocks[0].Quantity=1;
                auto o=options;o.Cancel=cancel;o.TimeLimitSeconds=std::min(.003,std::max(1e-9,options.TimeLimitSeconds*.35-elapsed()));
                o.MaxPlacementAttempts=std::max<std::size_t>(1,std::min<std::size_t>(256,options.MaxPlacementAttempts-attempts));
                const auto link=RunSeamAssemblyIf(local,o,{});attempts+=link.Stats.PlacementAttempts;
                if(link.State==Status::Complete)parent[find(b)]=find(a);
            }
        std::map<std::size_t,std::vector<std::size_t>> components;
        for(auto index:indices)components[find(index)].push_back(index);
        std::set<std::string> usedIDs;for(const auto& part:p.Parts)usedIDs.insert(part.ID);
        for(const auto& [root,members]:components)
        {
            if(discoverStop())break;
            if(members.size()<2||members.size()>assemblyCap)continue;
            const auto stock=stockFor(p.Parts[members.front()].Material);if(!stock)continue;
            Problem local;local.Tolerance=p.Tolerance;local.EdgeMargin=p.EdgeMargin;local.Stocks={*stock};local.Stocks[0].Quantity=1;
            for(auto index:members){local.Parts.push_back(p.Parts[index]);local.Parts.back().Quantity=1;}
            auto o=options;o.Cancel=cancel;o.TimeLimitSeconds=std::max(1e-9,options.TimeLimitSeconds*.35-elapsed());
            o.MaxPlacementAttempts=std::max<std::size_t>(1,options.MaxPlacementAttempts-attempts);
            const auto assembly=RunSeamAssemblyIf(local,o,[&](const Result& r){return FilledBasic(local,r);});attempts+=assembly.Stats.PlacementAttempts;
            if(assembly.State!=Status::Complete||assembly.Layouts.size()!=1)continue;
            Clipper2Lib::PathsD paths;Unit unit;
            std::size_t quantity=std::numeric_limits<std::size_t>::max();
            double materialArea=0,perimeter=0;
            for(const auto& placed:assembly.Layouts[0].Placements)
            {
                const auto item=std::find_if(members.begin(),members.end(),[&](auto index){return p.Parts[index].ID==placed.PartID;});
                if(item==members.end())continue;
                const auto index=*item;const auto& part=p.Parts[index];unit.Members.push_back({index,placed.Transform});
                quantity=std::min(quantity,working.Parts[index].Quantity);materialArea+=Area(part.Geometry);
                const auto append=[&](const auto& ring)
                {
                    auto& path=paths.emplace_back();
                    for(std::size_t i=0;i<ring.size();++i)
                    {const auto a=Transform(ring[i],placed.Transform),b=Transform(ring[(i+1)%ring.size()],placed.Transform);path.emplace_back(a.X,a.Y);perimeter+=std::hypot(b.X-a.X,b.Y-a.Y);}
                };
                append(part.Geometry.Outer);for(const auto& hole:part.Geometry.Holes)append(hole);
            }
            if(!quantity||unit.Members.size()!=members.size())continue;
            // A convex envelope conservatively contains every original member.
            // Tiny contact gaps must not turn one real unit into several Boolean
            // paths; require actual material area to fill the basic envelope.
            Shape geometry=Hull(paths);
            const auto classified=Classify(geometry,p.Tolerance);
            if((classified.Kind!=ShapeKind::Rectangle&&classified.Kind!=ShapeKind::Parallelogram)||std::abs(Area(geometry)-materialArea)>perimeter*p.Tolerance)continue;
            Part part;part.Geometry=std::move(geometry);part.Quantity=quantity;part.Material=p.Parts[members.front()].Material;
            part.ID="seam-unit-"+std::to_string(units.size());while(usedIDs.count(part.ID))part.ID+="_";
            unit.ID=part.ID;usedIDs.insert(part.ID);
            part.FreeRotation=std::all_of(members.begin(),members.end(),[&](auto index){return p.Parts[index].FreeRotation;});
            if(!part.FreeRotation)
            {
                std::vector<double> candidates{0};
                for(const auto& [index,pose]:unit.Members)if(!p.Parts[index].FreeRotation)
                {for(auto angle:CandidateAngles(p.Parts[index],p.Tolerance))candidates.push_back(NormalizeAngle(angle-pose.AngleDegrees));break;}
                for(auto angle:candidates)
                {
                    bool allowed=true;for(const auto& [index,pose]:unit.Members)allowed&=AngleAllowed(p.Parts[index],angle+pose.AngleDegrees,p.Tolerance);
                    if(allowed)part.AllowedAnglesDegrees.push_back(angle);
                }
                if(part.AllowedAnglesDegrees.empty())continue;
            }
            for(auto index:members)working.Parts[index].Quantity-=quantity;
            working.Parts.push_back(std::move(part));units.push_back(std::move(unit));
        }
    }
    if(stop())
    {
        Result empty;empty.State=cancelled?Status::Cancelled:Status::Partial;
        for(const auto& part:p.Parts)if(part.Quantity)empty.Remaining.push_back({part.ID,part.Quantity});
        empty.Stats.PlacementAttempts=attempts;empty.Stats.ElapsedSeconds=elapsed();return empty;
    }
    const auto missing=[&](const Result& r)
    {
        std::size_t count=0;
        for(const auto& item:r.Remaining)
        {const auto u=std::find_if(units.begin(),units.end(),[&](const auto& v){return v.ID==item.PartID;});count+=item.Quantity*(u==units.end()?1:u->Members.size());}
        return count;
    };
    Result result;result.State=Status::Partial;for(const auto& part:working.Parts)if(part.Quantity)result.Remaining.push_back({part.ID,part.Quantity});
    const auto mapToOriginal=[&](Result originalResult)
    {
    std::map<std::string,std::size_t> byID,next;
    for(std::size_t i=0;i<p.Parts.size();++i)byID[p.Parts[i].ID]=i;
    std::size_t group=0;
    for(auto& layout:originalResult.Layouts)
    {
        std::vector<Placement> mapped;layout.MaterialArea=0;
        for(const auto& placed:layout.Placements)
        {
            const auto unit=std::find_if(units.begin(),units.end(),[&](const auto& u){return u.ID==placed.PartID;});++group;
            if(unit==units.end())
            {
                auto original=placed;original.Instance=next[placed.PartID]++;original.Group=group;mapped.push_back(original);
                layout.MaterialArea+=Area(p.Parts[byID.at(placed.PartID)].Geometry);
            }
            else for(const auto& [index,relative]:unit->Members)
            {
                const auto& part=p.Parts[index];mapped.push_back({part.ID,next[part.ID]++,Compose(placed.Transform,relative),group,placed.GapFiller});
                layout.MaterialArea+=Area(part.Geometry);
            }
        }
        layout.Placements=std::move(mapped);
    }
    originalResult.Remaining.clear();for(const auto& part:p.Parts)if(next[part.ID]<part.Quantity)originalResult.Remaining.push_back({part.ID,part.Quantity-next[part.ID]});
    originalResult.State=cancelled?Status::Cancelled:originalResult.Remaining.empty()?Status::Complete:Status::Partial;
        return originalResult;
    };
    std::vector<std::string> mappingRejections,backboneTrials;
    // At most one candidate per backbone; defer same-prefix measurement until
    // construction has spent its budget, so scoring cannot starve completion.
    struct Candidate { Result Working; std::string Backbone; };
    std::vector<Candidate> alternatives;
    double verificationSeconds=0,evaluationSeconds=0;
    std::string selectedBackbone="none";
    std::set<std::string> activeMaterials;
    for(const auto& part:working.Parts)if(part.Quantity)activeMaterials.insert(part.Material);
    const auto minimumBoards=activeMaterials.size();
    double constructionDeadline=options.TimeLimitSeconds;
    std::size_t constructionAttempts=options.MaxPlacementAttempts;
    bool refining=false;
    const auto constructionStop=[&]
    {return stop()||elapsed()>=constructionDeadline||attempts>=constructionAttempts;};
    const auto cancelledCandidate=[&]()
    {
        // The retained candidate already passed original geometry validation;
        // an empty retained plan conserves the complete original demand.
        auto retained=mapToOriginal(result);retained.State=Status::Cancelled;
        retained.Stats.PlacementAttempts=attempts;
        retained.Stats.ElapsedSeconds=std::max(0.0,elapsed()-verificationSeconds-evaluationSeconds);
        retained.Stats.VerificationSeconds=verificationSeconds;
        retained.Stats.EvaluationSeconds=evaluationSeconds;
        retained.Stats.PreparationSeconds=retained.Stats.PlacementSeconds=-1;
        retained.Diagnostics.push_back("Mixed assembly cancelled; last verified candidate retained.");
        return retained;
    };
    for(int slot=0;slot<(useLookahead?4:3)&&!constructionStop();++slot)
    {
        const int mode=useLookahead?slot-1:slot;
        auto o=options;o.Cancel=cancel;
        o.TimeLimitSeconds=std::max(1e-9,(constructionDeadline-elapsed())*(mode<0?.5:mode<2?.3:1));
        o.MaxPlacementAttempts=std::max<std::size_t>(1,(constructionAttempts-attempts)/(mode<0?2:mode<2?3:1));
        auto trial=mode<2?RunMixedCells(working,o,mode==1,mode<0):RunRules(working,o,RectScore::ShortSide,true,true,.6,false,CombinationMode::Contacts,PatternOrder::BestFit);
        attempts+=trial.Stats.PlacementAttempts;
        if(trial.State==Status::Cancelled)cancelled=true;
        if(cancelled)return cancelledCandidate();
        const std::string backbone=mode<0?"lookahead":mode==0?"rows":mode==1?"columns":"generic";
        backboneTrials.push_back("Mixed backbone trial "+backbone+": original missing="+std::to_string(missing(trial))+", attempts="+std::to_string(trial.Stats.PlacementAttempts)+", state="+std::to_string(static_cast<int>(trial.State)));
        for(std::size_t i=0;i<std::min<std::size_t>(2,trial.Diagnostics.size());++i)backboneTrials.push_back(backbone+": "+trial.Diagnostics[i]);
        const bool usable=trial.State==Status::Complete||trial.State==Status::Partial;
        const auto prefix=std::pair(missing(trial),trial.Layouts.size());
        const auto retainedPrefix=std::pair(missing(result),result.Layouts.size());
        if(usable&&prefix<retainedPrefix&&Validate(working,trial).Valid)
        {
            const auto original=mapToOriginal(trial);const auto check=Validate(p,original);
            if(check.Valid)
            {
                alternatives.clear();
                result=trial;selectedBackbone=backbone;
                alternatives.push_back({std::move(trial),backbone});
                if(!refining&&result.State==Status::Complete&&result.Layouts.size()<=minimumBoards)
                {
                    // Board count is already at a safe lower bound. Spend one
                    // shared tenth of the remaining construction resources on
                    // equal-board alternatives; completion retains the full budget.
                    refining=true;
                    constructionDeadline=elapsed()+std::max(0.0,options.TimeLimitSeconds-elapsed())*.1;
                    constructionAttempts=attempts+(options.MaxPlacementAttempts-attempts)/10;
                }
            }
            else
            {
                std::string diagnostic="Mapped working candidate rejected from "+backbone+":";
                for(const auto& error:check.Errors)diagnostic+=" "+error;
                mappingRejections.push_back(std::move(diagnostic));
            }
        }
        else if(usable&&prefix==retainedPrefix)
        {
            // This alternative cannot displace the safe incumbent before the
            // original-demand gate below has accepted its real member poses.
            alternatives.push_back({std::move(trial),backbone});
        }
    }
    if(cancel())return cancelledCandidate();
    std::optional<QualityKey> bestQuality;
    Result selectedOriginal;
    bool selected=false;
    if(alternatives.size()>1)
        for(auto& alternative:alternatives)
        {
            if(cancel())break;
            const auto verifyStarted=std::chrono::steady_clock::now();
            const auto workingCheck=Validate(working,alternative.Working);
            verificationSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-verifyStarted).count();
            if(!workingCheck.Valid)continue;
            JobResult candidate;
            candidate.Output=mapToOriginal(std::move(alternative.Working));
            candidate.Output.Stats.VerificationSeconds=-1;
            candidate.Output.Stats.EvaluationSeconds=-1;
            AssessCandidate(p,candidate);
            verificationSeconds+=std::max(0.0,candidate.Output.Stats.VerificationSeconds);
            evaluationSeconds+=std::max(0.0,candidate.Output.Stats.EvaluationSeconds);
            if(!Eligible(candidate))
            {
                std::string diagnostic="Mapped working candidate rejected from "+alternative.Backbone+":";
                for(const auto& error:candidate.Checked.Errors)diagnostic+=" "+error;
                mappingRejections.push_back(std::move(diagnostic));
                continue;
            }
            const auto quality=Quality(candidate,p,options.Ranking);
            backboneTrials.push_back("Mixed final candidate "+alternative.Backbone+": regular remnant="+
                std::to_string(candidate.Remnants.LastSheetRegularArea));
            if(!bestQuality||quality<*bestQuality)
            {
                bestQuality=quality;selectedOriginal=std::move(candidate.Output);
                selectedBackbone=alternative.Backbone;selected=true;
            }
        }
    if(cancelled)
    {
        if(!selected)return cancelledCandidate();
        selectedOriginal.State=Status::Cancelled;
    }
    result=mapToOriginal(std::move(result));
    if(selected)result=std::move(selectedOriginal);
    const auto finalVerifyStarted=std::chrono::steady_clock::now();
    const auto finalCheck=Validate(p,result);
    verificationSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-finalVerifyStarted).count();
    if(!finalCheck.Valid)
    {
        if(stop())
        {
            result={};result.State=cancelled?Status::Cancelled:Status::Partial;
            for(const auto& part:p.Parts)if(part.Quantity)result.Remaining.push_back({part.ID,part.Quantity});
            result.Stats.PlacementAttempts=attempts;result.Stats.ElapsedSeconds=elapsed();return result;
        }
        auto fallback=options;fallback.Cancel=cancel;fallback.TimeLimitSeconds=std::max(1e-9,options.TimeLimitSeconds-elapsed());
        fallback.MaxPlacementAttempts=std::max<std::size_t>(1,options.MaxPlacementAttempts>attempts?options.MaxPlacementAttempts-attempts:0);
        result=RunRules(p,fallback,RectScore::Area,false,true);attempts+=result.Stats.PlacementAttempts;selectedBackbone="original-fallback";
    }
    if(cancel()&&(result.State==Status::Complete||result.State==Status::Partial))result.State=Status::Cancelled;
    result.Diagnostics.insert(result.Diagnostics.end(),mappingRejections.begin(),mappingRejections.end());
    result.Diagnostics.insert(result.Diagnostics.end(),backboneTrials.begin(),backboneTrials.end());
    result.Diagnostics.push_back("Selected mixed backbone="+selectedBackbone);
    result.Stats.PlacementAttempts=attempts;
    result.Stats.ElapsedSeconds=std::max(0.0,elapsed()-verificationSeconds-evaluationSeconds);
    result.Stats.PreparationSeconds=result.Stats.PlacementSeconds=-1;
    result.Stats.VerificationSeconds=verificationSeconds;
    result.Stats.EvaluationSeconds=evaluationSeconds;
    result.Diagnostics.push_back("Seam components converted "+std::to_string(units.size())+" actual basic-shape units; original members retained.");
    return result;
}

}
