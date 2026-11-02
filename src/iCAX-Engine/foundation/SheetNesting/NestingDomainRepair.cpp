#include "SheetNestingDomainRepair.h"
#include "SheetNestingDomainSearch.h"
#include "NestingInternal.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

namespace iCAX::SheetNesting {
namespace {
class DomainRepair final:public IPostProcessor {
public:
 Result Run(const Problem& p,const Result& incumbent,const Options& o) override {
    const auto start=std::chrono::steady_clock::now();Result best=incumbent;Statistics work;
    std::size_t attempts=0,queries=0,restorations=0,proposals=0,firstQueryCandidates=0,firstQueryAttempts=0;bool cancelled=false,truncated=false;
    std::size_t activeLimit=o.MaxPlacementAttempts;
    const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();};
    const auto stop=[&]{if(o.Cancel&&o.Cancel())cancelled=true;return cancelled||elapsed()>=o.TimeLimitSeconds||attempts>=o.MaxPlacementAttempts||attempts>=activeLimit;};
    const auto finish=[&]{auto original=best.Stats;best.Stats=work;best.Stats.PlacementAttempts=attempts;best.Stats.ElapsedSeconds=best.Stats.PostProcessingSeconds=elapsed();
        best.Stats.CombinationGroups=original.CombinationGroups;best.Stats.CircleLatticeGroups=original.CircleLatticeGroups;best.Stats.GapFilledParts=original.GapFilledParts;
        if(cancelled)best.State=Status::Cancelled;best.Diagnostics.push_back("repair.domain: queries="+std::to_string(queries)+", proposals="+std::to_string(proposals)+", restorations="+std::to_string(restorations)+", first candidates="+std::to_string(firstQueryCandidates)+", first attempts="+std::to_string(firstQueryAttempts)+", truncated="+(truncated||stop()?"yes":"no"));return best;};
    std::size_t missing=0;for(const auto& r:incumbent.Remaining)missing+=r.Quantity;
    if(stop()||!missing||missing>6||incumbent.State!=Status::Partial||p.Stocks.size()!=1||incumbent.Layouts.size()!=1||p.PartGap!=0||incumbent.Layouts[0].Placements.size()>128||p.Parts.size()>128||!Validate(p,incumbent).Valid)return finish();
    std::map<std::string,std::size_t> parts;for(std::size_t i=0;i<p.Parts.size();++i){if(p.Parts[i].Geometry.Radius>0||!p.Parts[i].Geometry.Holes.empty())return finish();parts.emplace(p.Parts[i].ID,i);}
    auto probe=Detail::MakePlacementDomainProbe();std::size_t bestMissing=missing;
    const auto query=[&](const Layout& layout,std::size_t index,std::size_t maxBlockers){
        Detail::PlacementDomainQueryResult q;if(stop())return q;
        auto options=o;options.TimeLimitSeconds=std::max(0.,o.TimeLimitSeconds-elapsed());options.MaxPlacementAttempts=std::min(o.MaxPlacementAttempts,activeLimit)-attempts;
        options.Cancel=[&]{return stop();};q=probe->Find(p,layout,index,maxBlockers,options);attempts+=q.Attempts;++queries;
        if(queries==1){firstQueryCandidates=q.Candidates.size();firstQueryAttempts=q.Attempts;}truncated|=q.Truncated;return q;
    };
    const auto updateArea=[&](Layout& layout){layout.MaterialArea=0;for(const auto& x:layout.Placements)layout.MaterialArea+=Detail::Area(p.Parts[parts.at(x.PartID)].Geometry);};
    using Key=std::pair<std::string,std::size_t>;
    std::function<bool(Result&,std::vector<Placement>,std::size_t,std::set<Key>)> restore;
    restore=[&](Result& result,std::vector<Placement> displaced,std::size_t pos,std::set<Key> protectedItems){
        if(stop())return false;if(pos>=displaced.size())return true;
        const auto item=displaced[pos];auto q=query(result.Layouts[0],parts.at(item.PartID),1);++restorations;
        std::stable_sort(q.Candidates.begin(),q.Candidates.end(),[](const auto& a,const auto& b){return std::tuple(a.Blockers.size(),a.Transform.Y,a.Transform.X,a.Transform.AngleDegrees)<std::tuple(b.Blockers.size(),b.Transform.Y,b.Transform.X,b.Transform.AngleDegrees);});
        std::map<std::vector<std::size_t>,std::size_t> families;std::size_t tried=0;
        for(const auto& candidate:q.Candidates){if(stop()||tried>=12)break;
            if(families[candidate.Blockers]++>=3)continue;
            auto trial=result;auto queue=displaced;auto protect=protectedItems;
            if(!candidate.Blockers.empty()){
                const auto i=candidate.Blockers.front();const auto blocked=trial.Layouts[0].Placements[i];
                if(queue.size()>=5||protect.count({blocked.PartID,blocked.Instance}))continue;
                queue.push_back(blocked);trial.Layouts[0].Placements.erase(trial.Layouts[0].Placements.begin()+i);
            }
            ++tried;auto moved=item;moved.Transform=candidate.Transform;moved.Group=0;
            trial.Layouts[0].Placements.push_back(moved);protect.emplace(moved.PartID,moved.Instance);
            if(restore(trial,std::move(queue),pos+1,std::move(protect))){result=std::move(trial);return true;}
        }
        return false;
    };
    std::function<void(const Result&,std::size_t)> search;
    search=[&](const Result& current,std::size_t depth){
        if(stop()||bestMissing==0||depth>=6)return;
        for(std::size_t entry=0;entry<current.Remaining.size()&&!stop();++entry){const auto partIndex=parts.at(current.Remaining[entry].PartID);const auto& part=p.Parts[partIndex];
            std::set<std::size_t> used;for(const auto& x:current.Layouts[0].Placements)if(x.PartID==part.ID)used.insert(x.Instance);
            std::size_t instance=0;while(instance<part.Quantity&&used.count(instance))++instance;if(instance>=part.Quantity)continue;
            auto q=query(current.Layouts[0],partIndex,2);
            std::stable_sort(q.Candidates.begin(),q.Candidates.end(),[&](const auto& a,const auto& b){
                const auto same=[&](const auto& x){std::size_t n=0;for(auto id:x.Blockers)if(current.Layouts[0].Placements[id].PartID==part.ID)++n;return n;};
                return std::tuple(a.Blockers.size(),same(a),a.Transform.Y,a.Transform.X,a.Transform.AngleDegrees)<std::tuple(b.Blockers.size(),same(b),b.Transform.Y,b.Transform.X,b.Transform.AngleDegrees);});
            std::map<std::vector<std::size_t>,std::size_t> families;std::size_t tried=0;
            for(const auto& candidate:q.Candidates){if(stop()||bestMissing==0||tried>=96)break;
                auto blockerKey=candidate.Blockers;std::sort(blockerKey.begin(),blockerKey.end());if(families[blockerKey]++>=6)continue;++tried;++proposals;
                Result trial=current;auto& layout=trial.Layouts[0];std::vector<Placement> displaced;
                for(auto id:blockerKey)displaced.push_back(layout.Placements[id]);
                for(auto it=blockerKey.rbegin();it!=blockerKey.rend();++it)layout.Placements.erase(layout.Placements.begin()+static_cast<std::ptrdiff_t>(*it));
                layout.Placements.push_back({part.ID,instance,candidate.Transform,0,false});
                if(--trial.Remaining[entry].Quantity==0)trial.Remaining.erase(trial.Remaining.begin()+entry);
                const std::set<Key> protectedItems{{part.ID,instance}};
                // Give different blocker families real execution opportunity;
                // one difficult restoration cannot consume the entire job.
                const auto parentLimit=activeLimit;
                const auto quota=std::max<std::size_t>(1,o.MaxPlacementAttempts/16);
                activeLimit=attempts+std::min(quota,parentLimit-attempts);
                bool ok=restore(trial,displaced,0,protectedItems);
                if(!ok&&displaced.size()==2&&!stop()) {std::reverse(displaced.begin(),displaced.end());ok=restore(trial,displaced,0,protectedItems);}
                activeLimit=parentLimit;
                if(!ok||stop())continue;updateArea(trial.Layouts[0]);trial.State=trial.Remaining.empty()?Status::Complete:Status::Partial;
                const auto checked=Validate(p,trial);if(!checked.Valid)continue;
                std::size_t left=0;for(const auto& r:trial.Remaining)left+=r.Quantity;
                if(left<bestMissing){best=trial;bestMissing=left;}if(left)search(trial,depth+1);
            }
        }
    };
    search(incumbent,0);return finish();
 }
};
}
PostProcessStage MakeDomainRepairStage(){return {"repair.domain",[]{return std::make_unique<DomainRepair>();}};}
}
