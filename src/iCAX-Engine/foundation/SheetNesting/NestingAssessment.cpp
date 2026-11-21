#include "NestingExecution.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <numeric>
#include <unordered_map>

namespace iCAX::SheetNesting::Detail
{
    namespace
    {
        void ArrangeUsedLayouts(const Problem& input,JobResult& candidate)
        {
            auto& layouts=candidate.Output.Layouts;
            if(layouts.size()<2)return;
            std::unordered_map<std::string,double> areas;
            std::unordered_map<std::string,std::string> materials;
            for(const auto& part:input.Parts)areas.emplace(part.ID,Area(part.Geometry));
            for(const auto& stock:input.Stocks)materials.emplace(stock.ID,stock.Material);
            std::map<std::string,std::vector<std::size_t>> groups;
            std::vector<double> utilization(layouts.size());
            std::vector<bool> final(layouts.size());
            for(auto i:candidate.Remnants.FinalLayoutIndices)if(i<final.size())final[i]=true;
            for(std::size_t i=0;i<layouts.size();++i)
            {
                double material=0;for(const auto& p:layouts[i].Placements)material+=areas.at(p.PartID);
                utilization[i]=material/(layouts[i].Width*layouts[i].Height);
                groups[materials.at(layouts[i].StockID)].push_back(i);
            }
            std::vector<std::size_t> order(layouts.size());std::iota(order.begin(),order.end(),0);
            for(const auto& [material,indices]:groups)
            {
                (void)material;auto sorted=indices;
                std::stable_sort(sorted.begin(),sorted.end(),[&](auto a,auto b)
                {if(final[a]!=final[b])return !final[a];return utilization[a]>utilization[b];});
                for(std::size_t i=0;i<indices.size();++i)order[indices[i]]=sorted[i];
            }
            std::vector<Layout> arranged;arranged.reserve(layouts.size());
            auto remnants=candidate.Remnants.PerLayout;
            std::vector<std::size_t> positions(layouts.size());
            for(std::size_t i=0;i<order.size();++i)
            {
                arranged.push_back(std::move(layouts[order[i]]));positions[order[i]]=i;
                if(remnants.size()==order.size())candidate.Remnants.PerLayout[i]=remnants[order[i]];
            }
            layouts=std::move(arranged);
            for(auto& i:candidate.Remnants.FinalLayoutIndices)i=positions[i];
        }
    }
    void AssessCandidate(const Problem& input, JobResult& candidate)
    {
        candidate.Remnants = {};
        const auto verifyStarted = std::chrono::steady_clock::now();
        candidate.Checked = Validate(input, candidate.Output);
        candidate.Output.Stats.VerificationSeconds = std::max(0.0, candidate.Output.Stats.VerificationSeconds) +
            std::chrono::duration<double>(std::chrono::steady_clock::now() - verifyStarted).count();
        candidate.Output.GeometryValidated = candidate.Checked.Valid;
        if (!candidate.Checked.Valid)
        {
            candidate.Output.State = Status::Failed;
            candidate.Output.Diagnostics.insert(candidate.Output.Diagnostics.end(), candidate.Checked.Errors.begin(), candidate.Checked.Errors.end());
        }
        else
        {
            const auto evaluateStarted = std::chrono::steady_clock::now();
            try { candidate.Remnants = EvaluateRemnants(input, candidate.Output); }
            catch (...) { candidate.Remnants = {}; }
            ArrangeUsedLayouts(input,candidate);
            candidate.Output.Stats.EvaluationSeconds = std::max(0.0, candidate.Output.Stats.EvaluationSeconds) +
                std::chrono::duration<double>(std::chrono::steady_clock::now() - evaluateStarted).count();
            if (!candidate.Remnants.Measured) candidate.Output.Diagnostics.push_back("Reusable-remnant score unavailable; valid geometry is retained.");
        }
    }
}
