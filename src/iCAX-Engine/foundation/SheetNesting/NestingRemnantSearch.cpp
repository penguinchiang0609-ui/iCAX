#include "NestingInternal.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <numeric>

namespace iCAX::SheetNesting::Detail
{
    // Repack one actual used sheet inside several narrower/shorter working regions.
    // Trial failure is only a failed heuristic attempt, never an infeasibility bound.
    // Every accepted change preserves the physical stock and original instance IDs.
    Result RunStripRules(const Problem& problem, const Options& options, bool narrowWidth)
    {
        const auto started=std::chrono::steady_clock::now();
        const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();};
        bool cancellationObserved=false;
        const auto cancelled=[&]
        {
            if(!cancellationObserved&&options.Cancel&&options.Cancel())cancellationObserved=true;
            return cancellationObserved;
        };
        auto baseOptions=options;
        baseOptions.Cancel=cancelled;
        baseOptions.TimeLimitSeconds=options.TimeLimitSeconds*.35;
        baseOptions.MaxPlacementAttempts=std::max<std::size_t>(1,options.MaxPlacementAttempts/3);
        auto best=RunRules(problem,baseOptions,RectScore::Area,true,true);
        std::size_t totalAttempts=best.Stats.PlacementAttempts,totalPatterns=best.Stats.CandidatePatterns;
        if(best.State!=Status::Complete||best.Layouts.empty()||cancelled())
        {
            if(cancellationObserved)best.State=Status::Cancelled;
            return best;
        }
        std::map<std::string,const Part*> byID;
        for(const auto& part:problem.Parts)byID.emplace(part.ID,&part);
        std::vector<std::size_t> order(best.Layouts.size());std::iota(order.begin(),order.end(),0);
        std::stable_sort(order.begin(),order.end(),[&](auto a,auto b)
        {return best.Layouts[a].MaterialArea<best.Layouts[b].MaterialArea;});
        if(order.size()>4)order.resize(4);
        constexpr std::array<double,7> fractions{.75,.875,.5,.625,.9375,.375,.25};
        std::size_t accepted=0;
        for(const auto layoutIndex:order)
        {
            if(elapsed()>=options.TimeLimitSeconds||totalAttempts>=options.MaxPlacementAttempts||cancelled())break;
            const auto original=best.Layouts[layoutIndex];
            auto retained=FindRegularRemnant(problem,original,64);
            if(!retained.Verified)continue;
            Problem local;local.EdgeMargin=problem.EdgeMargin;local.PartGap=problem.PartGap;local.Tolerance=problem.Tolerance;
            std::map<std::string,std::vector<std::size_t>> instances;
            for(const auto& placed:original.Placements)instances[placed.PartID].push_back(placed.Instance);
            double materialArea=0;
            for(auto& [id,ids]:instances)
            {
                std::sort(ids.begin(),ids.end());auto part=*byID.at(id);part.Quantity=ids.size();
                materialArea+=Area(part.Geometry)*part.Quantity;local.Parts.push_back(std::move(part));
            }
            const auto sourceStock=std::find_if(problem.Stocks.begin(),problem.Stocks.end(),[&](const auto& s){return s.ID==original.StockID;});
            if(sourceStock==problem.Stocks.end())continue;
            auto stock=*sourceStock;stock.Quantity=1;local.Stocks={stock};
            const double fullDimension=narrowWidth?stock.Width:stock.Height;
            const double otherUsable=(narrowWidth?stock.Height:stock.Width)-2*problem.EdgeMargin;
            if(otherUsable<=0)continue;
            const double areaBound=2*problem.EdgeMargin+materialArea/otherUsable;
            for(std::size_t fi=0;fi<fractions.size();++fi)
            {
                const double left=options.TimeLimitSeconds-elapsed();
                if(left<=0||totalAttempts>=options.MaxPlacementAttempts||cancelled())break;
                const double dimension=fullDimension*fractions[fi];
                if(dimension+problem.Tolerance<areaBound)continue;
                local.Stocks[0]=stock;
                if(narrowWidth)local.Stocks[0].Width=dimension;else local.Stocks[0].Height=dimension;
                auto trialOptions=options;
                trialOptions.Cancel=cancelled;
                trialOptions.TimeLimitSeconds=left/static_cast<double>(fractions.size()-fi);
                trialOptions.MaxPlacementAttempts=std::max<std::size_t>(1,(options.MaxPlacementAttempts-totalAttempts)/(fractions.size()-fi));
                trialOptions.MaxPatterns=std::min<std::size_t>(options.MaxPatterns,96);
                auto trial=RunRules(local,trialOptions,fi%2?RectScore::ShortSide:RectScore::Area,true,true);
                totalAttempts+=trial.Stats.PlacementAttempts;totalPatterns+=trial.Stats.CandidatePatterns;
                if(trial.State!=Status::Complete||trial.Layouts.size()!=1||!Validate(local,trial).Valid)continue;
                auto candidate=std::move(trial.Layouts[0]);
                candidate.StockInstance=original.StockInstance;candidate.Width=original.Width;candidate.Height=original.Height;
                for(auto& placed:candidate.Placements)placed.Instance=instances.at(placed.PartID).at(placed.Instance);
                const auto measure=FindRegularRemnant(problem,candidate,64);
                if(measure.Verified&&measure.Area>retained.Area+problem.Tolerance*(original.Width+original.Height))
                {
                    std::size_t nextGroup=0;
                    for(const auto& layout:best.Layouts)for(const auto& placed:layout.Placements)nextGroup=std::max(nextGroup,placed.Group);
                    std::map<std::size_t,std::size_t> groups;
                    for(auto& placed:candidate.Placements)
                    {
                        auto [found,inserted]=groups.emplace(placed.Group,0);
                        if(inserted)found->second=++nextGroup;
                        placed.Group=found->second;
                    }
                    auto proposed=best;proposed.Layouts[layoutIndex]=candidate;
                    if(Validate(problem,proposed).Valid)
                    {best.Layouts[layoutIndex]=std::move(candidate);retained=measure;++accepted;}
                }
            }
        }
        best.Stats.PlacementAttempts=totalAttempts;best.Stats.CandidatePatterns=totalPatterns;
        best.Stats.ElapsedSeconds=elapsed();
        // Subsolves and conservative measurement are interleaved; do not publish fictitious phase timings.
        best.Stats.PreparationSeconds=best.Stats.PlacementSeconds=-1;
        if(cancelled())best.State=Status::Cancelled;
        best.Diagnostics.push_back("Rule-strip repacking accepted "+std::to_string(accepted)+" larger verified retained-rectangle certificates.");
        return best;
    }
}
