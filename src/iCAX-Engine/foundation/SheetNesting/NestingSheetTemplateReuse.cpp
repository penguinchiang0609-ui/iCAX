#include "NestingSheetTemplateReuse.h"
#include "NestingInternal.h"
#include "NestingTrace.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
SheetTemplateReuseReport AppendSheetTemplateCopies(const Problem& problem,
    const Layout& prototype,const Stock& stock,std::size_t& nextStockInstance,
    std::vector<std::size_t>& used,std::size_t& nextGroup,Result& output,
    const Options& options,double minimumUtilization)
{
    using Clock=std::chrono::steady_clock;
    const auto start=Clock::now();
    SheetTemplateReuseReport report;
    // In the normal caller prototype aliases output.Layouts.back(). Stabilize
    // it before any append can reallocate the enclosing vector.
    const Layout recipe=prototype;
    bool callbackFailed=false,prefixVerified=false;
    const auto stop=[&](bool chargeLimit=true)
    {
        if(report.Cancelled||callbackFailed)return true;
        if(options.Cancel)
        {
            try{if(options.Cancel()){report.Cancelled=true;return true;}}
            catch(...){callbackFailed=true;report.Truncated=true;report.Diagnostic="sheet-template callback failed safely";return true;}
        }
        if(!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<=0||
            std::chrono::duration<double>(Clock::now()-start).count()>=options.TimeLimitSeconds||
            (chargeLimit&&report.Attempts>=options.MaxPlacementAttempts))
        {report.Truncated=true;return true;}
        return false;
    };
    const auto finish=[&](const std::string& reason)
    {
        if(report.Diagnostic.empty())report.Diagnostic=reason;
        if(prefixVerified)
        {
            output.Remaining.clear();
            for(std::size_t i=0;i<problem.Parts.size();++i)
                if(used[i]<problem.Parts[i].Quantity)
                    output.Remaining.push_back({problem.Parts[i].ID,problem.Parts[i].Quantity-used[i]});
            output.State=report.Cancelled?Status::Cancelled:output.Remaining.empty()?Status::Complete:Status::Partial;
        }
        else if(report.Cancelled)output.State=Status::Cancelled;
        return report;
    };
    if(used.size()!=problem.Parts.size()||!std::isfinite(minimumUtilization)||minimumUtilization<0||minimumUtilization>1)
        return finish("sheet-template invalid inventory vector or utilization gate");
    const Stock* originalStock=nullptr;
    for(const auto& value:problem.Stocks)if(value.ID==stock.ID)
    {if(originalStock)return finish("sheet-template duplicate stock identity");originalStock=&value;}
    if(!originalStock||stock.Width!=originalStock->Width||stock.Height!=originalStock->Height||
        stock.Material!=originalStock->Material||stock.Quantity!=originalStock->Quantity||
        recipe.StockID!=stock.ID||recipe.Width!=stock.Width||recipe.Height!=stock.Height||
        recipe.Placements.empty()||nextStockInstance>stock.Quantity)
        return finish("sheet-template source and stock identity, dimensions or inventory differ");
    std::map<std::string,std::size_t> indices;
    for(std::size_t i=0;i<problem.Parts.size();++i)
    {
        if(used[i]>problem.Parts[i].Quantity||!indices.emplace(problem.Parts[i].ID,i).second)
            return finish("sheet-template invalid original inventory");
    }
    std::vector<std::size_t> need(problem.Parts.size()),committed(problem.Parts.size());
    for(const auto& item:recipe.Placements)
    {
        const auto found=indices.find(item.PartID);
        if(found==indices.end()||problem.Parts[found->second].Material!=stock.Material)
            return finish("sheet-template unknown or incompatible original");
        ++need[found->second];
    }
    // Most tail/quantity-one boards have no repeat opportunity. These gates
    // inspect only this recipe and current counters, never the growing output.
    std::size_t copies=stock.Quantity-nextStockInstance;
    for(std::size_t i=0;i<need.size();++i)if(need[i])copies=std::min(copies,(problem.Parts[i].Quantity-used[i])/need[i]);
    if(!copies)return finish("sheet-template no additional complete copy fits remaining inventory");
    double material=0;
    for(std::size_t i=0;i<need.size();++i)if(need[i])
    {
        if(stop(false))return finish("sheet-template stopped during material measurement");
        const auto amount=Area(problem.Parts[i].Geometry)*static_cast<double>(need[i]);
        if(!std::isfinite(amount)||amount<=0||!std::isfinite(material+amount))return finish("sheet-template original material area is invalid");
        material+=amount;
    }
    const auto sheetArea=stock.Width*stock.Height;
    if(!std::isfinite(sheetArea)||sheetArea<=0||material/sheetArea<minimumUtilization)
        return finish("sheet-template source does not meet original-material utilization gate");
    // Check metadata once. The source's geometry is checked separately below;
    // earlier boards are unchanged and are not re-solved or revalidated here.
    std::set<std::pair<std::string,std::size_t>> partIDs,stockIDs;
    std::size_t stockCount=0,largestGroup=0;
    bool sourceCommitted=false;
    const auto samePlacement=[](const Placement& a,const Placement& b)
    {return std::tie(a.PartID,a.Instance,a.Transform.X,a.Transform.Y,a.Transform.AngleDegrees,a.Transform.Mirrored,a.Group,a.GapFiller)==
            std::tie(b.PartID,b.Instance,b.Transform.X,b.Transform.Y,b.Transform.AngleDegrees,b.Transform.Mirrored,b.Group,b.GapFiller);};
    for(const auto& board:output.Layouts)
    {
        if(stop(false))return finish("sheet-template stopped during prefix verification");
        if(!stockIDs.emplace(board.StockID,board.StockInstance).second)
            return finish("sheet-template duplicate committed board identity");
        if(board.StockID==stock.ID)
        {
            ++stockCount;
            if(board.StockInstance>=nextStockInstance)return finish("sheet-template noncontiguous committed stock identities");
            if(board.StockInstance==recipe.StockInstance)
            {
                sourceCommitted=board.Width==recipe.Width&&board.Height==recipe.Height&&
                    board.Placements.size()==recipe.Placements.size()&&
                    std::equal(board.Placements.begin(),board.Placements.end(),recipe.Placements.begin(),samePlacement);
            }
        }
        for(const auto& item:board.Placements)
        {
            if(stop(false))return finish("sheet-template stopped during original identity verification");
            const auto found=indices.find(item.PartID);
            if(found==indices.end()||item.Instance>=used[found->second]||
                !partIDs.emplace(item.PartID,item.Instance).second)
                return finish("sheet-template committed original identities do not match used inventory");
            ++committed[found->second];largestGroup=std::max(largestGroup,item.Group);
        }
    }
    if(!sourceCommitted||stockCount!=nextStockInstance||committed!=used)
        return finish("sheet-template source board or committed inventory is inconsistent");
    prefixVerified=true;
    if(stop())return finish("sheet-template stopped before source validation");
    Result local;local.Layouts={recipe};
    for(std::size_t i=0;i<need.size();++i)
    {
        if(need[i]>problem.Parts[i].Quantity)return finish("sheet-template source exceeds original demand");
        if(need[i]<problem.Parts[i].Quantity)local.Remaining.push_back({problem.Parts[i].ID,problem.Parts[i].Quantity-need[i]});
    }
    local.State=local.Remaining.empty()?Status::Complete:Status::Partial;
    if(!Validate(problem,local).Valid)return finish("sheet-template source failed original geometry or inventory validation");
    if(stop())return finish("sheet-template stopped after source validation");
    std::map<std::size_t,std::size_t> groupCounts;
    for(const auto& item:recipe.Placements)if(item.Group)++groupCounts[item.Group];
    if(!groupCounts.empty()&&largestGroup==std::numeric_limits<std::size_t>::max())
        return finish("sheet-template group identity space exhausted");
    std::size_t groupCursor=groupCounts.empty()?nextGroup:std::max(nextGroup,largestGroup+1);
    const auto combinations=static_cast<std::size_t>(std::count_if(groupCounts.begin(),groupCounts.end(),[](const auto& value){return value.second>1;}));
    const auto fillers=static_cast<std::size_t>(std::count_if(recipe.Placements.begin(),recipe.Placements.end(),[](const Placement& value){return value.GapFiller;}));
    for(std::size_t copy=0;copy<copies;++copy)
    {
        if(stop())return finish("sheet-template expansion stopped with committed whole boards retained");
        if(groupCounts.size()>std::numeric_limits<std::size_t>::max()-groupCursor)
            return finish("sheet-template group identity space exhausted");
        Layout board=recipe;board.StockInstance=nextStockInstance;board.MaterialArea=material;
        auto nextUsed=used;auto nextCursor=groupCursor;
        std::map<std::size_t,std::size_t> groups;
        for(const auto& [old,count]:groupCounts){(void)count;groups.emplace(old,nextCursor++);}
        for(auto& item:board.Placements)
        {
            if(stop())return finish("sheet-template incomplete draft discarded; committed whole boards retained");
            ++report.Attempts;
            const auto pi=indices.at(item.PartID);item.Instance=nextUsed[pi]++;
            if(item.Group)item.Group=groups.at(item.Group);
        }
        // A completely built copy may commit on the last permitted attempt.
        // Cancellation/time still discard it atomically before publication.
        if(stop(false))return finish("sheet-template completed draft interrupted before commit");
        output.Layouts.push_back(std::move(board));
        used=std::move(nextUsed);++nextStockInstance;nextGroup=groupCursor=nextCursor;
        ++report.Copies;report.Parts+=recipe.Placements.size();
        output.Stats.CombinationGroups+=combinations;output.Stats.GapFilledParts+=fillers;
        ObserveConstruction(options,ConstructionTraceStage::Commit,[&](ConstructionTraceEvent& event)
        {
            // One real whole-board operation, with the source and committed
            // target in matching placement order. Do not invent member commits.
            const auto& target=output.Layouts.back();
            event.Source="sheet-template.copy";event.Layouts={recipe,target};
            event.StockIndex=static_cast<std::size_t>(originalStock-problem.Stocks.data());
            event.StockInstance=target.StockInstance;event.BoardIndex=output.Layouts.size()-1;
            event.BoardWidth=target.Width;event.BoardHeight=target.Height;event.MaterialArea=material;
            event.Attempts=report.Attempts;
            event.ElapsedSeconds=std::chrono::duration<double>(Clock::now()-start).count();
            event.Accepted=true;event.Reason="whole-board-copy-committed-with-ordered-original-identity-mapping";
            event.Measures={{"sourceLayoutIndex",0.},{"targetLayoutIndex",1.},{"copyIndex",double(copy)}};
        });
    }
    return finish("sheet-template additional whole-board copies committed; residual demand retained for tail construction");
}
}
