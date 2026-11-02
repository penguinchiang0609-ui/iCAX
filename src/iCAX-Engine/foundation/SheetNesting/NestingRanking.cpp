#include "NestingExecution.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace iCAX::SheetNesting::Detail
{
    bool Eligible(const JobResult& job)
    { return job.Checked.Valid && (job.Output.State == Status::Complete || job.Output.State == Status::Partial || job.Output.State == Status::Cancelled); }
    QualityKey Quality(const JobResult& job, const Problem& input, RankingPolicy policy)
    {
        std::size_t missing = 0;
        std::unordered_map<std::string,const Part*> parts;
        for(const auto& part:input.Parts)parts.emplace(part.ID,&part);
        for (const auto& item : job.Output.Remaining) missing += item.Quantity;
        double topSum = 0, rightSum = 0, extent = 1;
        for (const auto& stock : input.Stocks) extent = std::max(extent, stock.Width + stock.Height);
        for (const auto& layout : job.Output.Layouts)
        {
            double top = 0, right = 0;
            for (const auto& placement : layout.Placements)
            {
                const auto part = parts.find(placement.PartID);
                if (part == parts.end()) continue;
                const auto box = Detail::Bounds(part->second->Geometry, placement.Transform);
                top = std::max(top, box.Y + box.Height); right = std::max(right, box.X + box.Width);
            }
            topSum += top; rightSum += right;
        }
        const double linearQuantum = input.Tolerance;
        const double areaQuantum = input.Tolerance * extent;
        const auto linear = [&](double value) { return std::round(value / linearQuantum) * linearQuantum; };
        const double area = std::round(job.Checked.UsedEnvelopeArea / areaQuantum) * areaQuantum;
        if (policy == RankingPolicy::RegularRemnant)
        {
            const double remnant = job.Remnants.Measured
                ? -std::round(job.Remnants.LastSheetRegularArea / areaQuantum) * areaQuantum
                : std::numeric_limits<double>::infinity();
            return std::tuple(missing, job.Output.Layouts.size(), remnant, linear(topSum), linear(rightSum));
        }
        return policy == RankingPolicy::LowThenLeft ? std::tuple(missing, job.Output.Layouts.size(), linear(topSum), linear(rightSum), area)
            : std::tuple(missing, job.Output.Layouts.size(), area, linear(topSum), linear(rightSum));
    }
}
