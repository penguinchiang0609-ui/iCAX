#include "ordinary_rectangle_tiling.hpp"
#include "recovered_rectangle_tiling.hpp"
#include "ordinary_rectangle_groups.hpp"
namespace study {
std::vector<RectangleTilingModel> ordinaryRectangleTilingModels(const Input &input, Budget &budget) {
    auto primitive = ordinaryRectanglePrimitiveGroups(input);
    auto complex = ordinaryRectangleComplexModels(input, primitive.complexGroups, budget);
    primitive.models.insert(primitive.models.end(), std::make_move_iterator(complex.begin()), std::make_move_iterator(complex.end()));
    return rectangleExpandModelLimits(primitive.models);
}
std::vector<RectangleTilingModel> rectangleExpandModelLimits(const std::vector<RectangleTilingModel> &models) {
    std::vector<RectangleTilingModel> result;
    for (const auto &model : models)
        for (const auto limit : recovered::rectangleTilingRepetitionLimits(model.maximumX, model.maximumY, model.maximumTotal)) {
            auto expanded = model; expanded.maximumTotal = 0; expanded.maximumX = limit[0]; expanded.maximumY = limit[1];
            result.push_back(std::move(expanded));
        }
    return result;
}
std::vector<RectangleTilingModel> ordinaryRectangleComplexModels(
    const Input &input, const std::vector<size_t> &groups, Budget &budget, bool contextDisabled) {
    std::vector<RectangleTilingModel> result;
    for (const auto group : groups) {
        budget.check();
        const auto &part = input.parts.at(group);
        if (part.quantity <= 0 || (input.gap <= 1e-9 && part.protection > 0.)) continue;
        const auto original = bounds(part.polygon);
        if (!part.holes.empty() || part.polygon.size() != 4 ||
            std::abs(area(part.polygon) - (original.x1 - original.x0) * (original.y1 - original.y0)) > 1e-6)
            continue;
        recovered::RectangleTilingBranchInput branchInput;
        branchInput.gap = input.gap; branchInput.width = original.x1 - original.x0;
        branchInput.height = original.y1 - original.y0; branchInput.area = area(part.polygon);
        branchInput.required = part.quantity; branchInput.protectedGeometry = part.protection > 0.;
        branchInput.totalStockArea = branchInput.maximumStockArea = input.width * input.height;
        branchInput.contextDisabled = contextDisabled;
        const auto branch = recovered::rectangleTilingBranch(branchInput);
        if (!branch.obvious) continue;
        std::array<std::optional<size_t>, 2> poses;
        std::array<Box, 2> boxes;
        for (size_t pose = 0; pose < part.poses.size(); ++pose) {
            const auto &orientation = part.poses[pose];
            if (orientation.flip || orientation.angle != orientation.upper) continue;
            if (orientation.angle != 0. && orientation.angle != 90.) continue;
            const size_t direction = orientation.angle == 90.;
            if (poses[direction]) continue;
            poses[direction] = pose;
            boxes[direction] = bounds(transform(part.polygon, part.pivot, orientation));
        }
        for (size_t direction = 0; direction < 2; ++direction) if (poses[direction]) {
            RectangleTilingModel model;
            model.members.push_back({group, 0, *poses[direction], 0, 0});
            model.u = {boxes[direction].x1 - boxes[direction].x0 + input.gap, 0.};
            model.v = {0., boxes[direction].y1 - boxes[direction].y0 + input.gap};
            result.push_back(std::move(model));
        }
        if (!branch.rectanglePatterns || !poses[0] || !poses[1]) continue;
        const double width = original.x1 - original.x0 + input.gap;
        const double height = original.y1 - original.y0 + input.gap;
        const double ratio = height / width;
        if (std::abs(std::trunc(ratio + .5) - ratio) < 1e-5) continue;
        const bool swapped = width > height;
        const size_t first = swapped ? 1 : 0, second = swapped ? 0 : 1;
        const double shorter = std::min(width, height), longer = std::max(width, height);
        const auto build = [&](const recovered::GammadionCounts &counts) {
            const auto plan = recovered::gammadionPlan(counts, longer, shorter,
                {boxes[first].x0, boxes[first].y0}, {boxes[second].x0, boxes[second].y0});
            RectangleTilingModel model; model.u = plan.u; model.v = plan.v;
            for (const auto &prototype : plan.members) {
                Placement placement{group, 0, *poses[prototype.second ? second : first],
                    rounded(prototype.translation.x * 1e6), rounded(prototype.translation.y * 1000.)};
                model.members.push_back(placement);
            }
            return model;
        };
        auto ratios = recovered::gammadionRatios(shorter, longer, &budget);
        recovered::gammadionSortRatios(ratios);
        for (const auto &candidate : ratios) {
            auto model = build({candidate.second, 1, candidate.first, 1, candidate.second, 1, candidate.first, 1});
            const auto box = rectangleModelBounds(input, model);
            const double w = box.x1 - box.x0, h = box.y1 - box.y0;
            if ((w <= input.width && h < input.height) || (w <= input.height && h < input.width))
                result.push_back(std::move(model));
        }
        const auto counts = recovered::gammadionSheetFit(input.width, input.height, longer, shorter, part.quantity, &budget);
        if (std::all_of(counts.begin(), counts.end(), [](int32_t count) { return count != 0; }))
            result.push_back(build(counts));
    }
    return result;
}
}
