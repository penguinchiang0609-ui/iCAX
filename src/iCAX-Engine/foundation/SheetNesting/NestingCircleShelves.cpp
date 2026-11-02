#include "NestingInternal.h"
#include <algorithm>
#include <chrono>
#include <map>

namespace iCAX::SheetNesting::Detail
{
    Result RunCircleShelves(const Problem& problem,const Options& options)
    {
        const auto started=std::chrono::steady_clock::now();
        const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();};
        bool cancelled=false;
        const auto cancellationRequested=[&]
        {
            if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;
            return cancelled;
        };
        Result result;
        const auto emptyResult=[&](bool budget)
        {
            for(const auto& part:problem.Parts)if(part.Quantity)result.Remaining.push_back({part.ID,part.Quantity});
            result.State=cancelled?Status::Cancelled:result.Remaining.empty()?Status::Complete:Status::Partial;
            if(cancelled)result.Diagnostics.push_back("Circle cell-row job cancelled; original demand retained.");
            else if(budget)result.Diagnostics.push_back("Circle cell-row preparation exhausted its time or attempt budget.");
            else result.Diagnostics.push_back("Circle cell-row rules support analytic circles; other geometry remains for other jobs.");
            result.Stats.PreparationSeconds=result.Stats.ElapsedSeconds=elapsed();result.Stats.PlacementSeconds=0;
            return result;
        };
        if(cancellationRequested())return emptyResult(false);
        if(options.MaxPlacementAttempts==0||elapsed()>=options.TimeLimitSeconds)return emptyResult(true);
        Problem cells;cells.Stocks=problem.Stocks;cells.EdgeMargin=problem.EdgeMargin;
        cells.PartGap=problem.PartGap;cells.Tolerance=problem.Tolerance;
        std::map<std::string,std::size_t> originalByID;
        std::vector<double> angles(problem.Parts.size());
        bool unsupported=false;
        for(std::size_t index=0;index<problem.Parts.size();++index)
        {
            if(cancellationRequested())return emptyResult(false);
            if(elapsed()>=options.TimeLimitSeconds)return emptyResult(true);
            const auto& original=problem.Parts[index];
            if(original.Geometry.Radius<=0){unsupported|=original.Quantity>0;continue;}
            const auto allowed=CandidateAngles(original,problem.Tolerance);
            if(allowed.empty()){unsupported|=original.Quantity>0;continue;}
            angles[index]=allowed.front();originalByID.emplace(original.ID,index);
            const double radius=original.Geometry.Radius;
            Part cell;cell.ID=original.ID;cell.Quantity=original.Quantity;cell.Material=original.Material;
            cell.FreeRotation=false;cell.AllowedAnglesDegrees={0};
            cell.Geometry.Outer={{-radius,-radius},{radius,-radius},{radius,radius},{-radius,radius}};
            cells.Parts.push_back(std::move(cell));
        }
        if(cells.Parts.empty())return emptyResult(false);
        const double preparation=elapsed();
        auto inner=options;inner.Cancel=cancellationRequested;
        inner.TimeLimitSeconds=options.TimeLimitSeconds-preparation;
        if(inner.TimeLimitSeconds<=0)return emptyResult(true);
        auto planned=RunRectanglePatternShelves(cells,inner,false);
        result.Stats=planned.Stats;result.Diagnostics=std::move(planned.Diagnostics);
        std::vector<std::size_t> consumed(problem.Parts.size());
        // Publishing an already computed cell is an atomic representation change.
        // Cancellation must retain every completed cell and its original identity.
        for(auto& layout:planned.Layouts)
        {
            layout.MaterialArea=0;
            for(auto& placed:layout.Placements)
            {
                const auto index=originalByID.at(placed.PartID);const auto& original=problem.Parts[index];
                const auto center=Transform({0,0},placed.Transform);
                const auto localCenter=Transform(original.Geometry.Center,{0,0,angles[index]});
                placed.Transform={center.X-localCenter.X,center.Y-localCenter.Y,NormalizeAngle(angles[index])};
                layout.MaterialArea+=Area(original.Geometry);++consumed[index];
            }
            if(!layout.Placements.empty())result.Layouts.push_back(std::move(layout));
        }
        for(std::size_t index=0;index<problem.Parts.size();++index)
            if(consumed[index]<problem.Parts[index].Quantity)
                result.Remaining.push_back({problem.Parts[index].ID,problem.Parts[index].Quantity-consumed[index]});
        cancellationRequested();
        if(cancelled||planned.State==Status::Cancelled)result.State=Status::Cancelled;
        else if(planned.State==Status::Failed||planned.State==Status::InvalidInput)result.State=planned.State;
        else result.State=result.Remaining.empty()?Status::Complete:Status::Partial;
        if(cancelled)result.Diagnostics.push_back("Circle cell-row job cancelled; completed original circle placements retained.");
        if(unsupported)result.Diagnostics.push_back("Non-circle geometry remains unplaced for other independent jobs.");
        result.Diagnostics.push_back("Conservative circle cells reuse bounded rectangle row patterns; no infeasibility or optimality proof.");
        result.Stats.ElapsedSeconds=elapsed();
        if(planned.Stats.PreparationSeconds>=0)
        {
            result.Stats.PreparationSeconds=preparation+planned.Stats.PreparationSeconds;
            result.Stats.PlacementSeconds=std::max(0.0,result.Stats.ElapsedSeconds-result.Stats.PreparationSeconds);
        }
        else result.Stats.PreparationSeconds=result.Stats.PlacementSeconds=-1;
        return result;
    }
}
