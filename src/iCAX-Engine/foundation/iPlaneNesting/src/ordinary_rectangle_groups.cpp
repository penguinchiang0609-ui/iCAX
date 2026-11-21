#include "ordinary_rectangle_groups.hpp"
namespace study {
namespace {
bool sameRing(const Polygon &first, const Polygon &second) {
    if (first.size() != second.size()) return false;
    const auto equal = [](Point a, Point b) { return rounded(a.x * 1e6) == rounded(b.x * 1e6) &&
                                                    rounded(a.y * 1000.) == rounded(b.y * 1000.); };
    for (size_t offset = 0; offset < second.size(); ++offset) {
        bool forward = true, reverse = true;
        for (size_t index = 0; index < first.size(); ++index) {
            forward &= equal(first[index], second[(offset + index) % second.size()]);
            reverse &= equal(first[index], second[(offset + second.size() - index) % second.size()]);
        }
        if (forward || reverse) return true;
    }
    return false;
}
std::vector<Polygon> normalizedShape(const PartInput &part, Pose pose) {
    std::vector<Polygon> rings{transform(part.polygon, part.pivot, pose)};
    const auto origin = bounds(rings.front());
    for (const auto &hole : part.holes) rings.push_back(transform(hole, part.pivot, pose));
    // Every ring uses its outer origin; centering holes independently would
    // incorrectly identify an off-centre void as rotationally symmetric.
    for (auto &ring : rings) for (auto &point : ring) { point.x -= origin.x0; point.y -= origin.y0; }
    return rings;
}
bool sameShape(const std::vector<Polygon> &first, const std::vector<Polygon> &second) {
    if (first.size() != second.size() || !sameRing(first.front(), second.front())) return false;
    std::vector<bool> used(second.size(), false);
    for (size_t ring = 1; ring < first.size(); ++ring) {
        bool found = false;
        for (size_t candidate = 1; candidate < second.size(); ++candidate)
            if (!used[candidate] && sameRing(first[ring], second[candidate])) {
                used[candidate] = true; found = true; break;
            }
        if (!found) return false;
    }
    return true;
}
}
RectanglePrimitiveGroups ordinaryRectanglePrimitiveGroups(const Input &input, const RectanglePrimitiveSettings &settings) {
    RectanglePrimitiveGroups result;
    for (size_t group = 0; group < input.parts.size(); ++group) {
        const auto &part = input.parts[group];
        if (part.quantity <= 0) continue;
        for (const auto &pose : part.poses)
            require(pose.angle == pose.upper, PN2S_UNSUPPORTED, "Primitive tiling requires normalized finite pose variants");
        const auto box = bounds(part.polygon);
        recovered::RectangleFallbackInput branch;
        branch.required254 = part.quantity; branch.remaining = settings.remaining;
        branch.specialPreNest = settings.specialPreNest; branch.forceB0 = settings.forceB0;
        branch.minimumX = box.x0; branch.minimumY = box.y0; branch.maximumX = box.x1; branch.maximumY = box.y1;
        branch.area68 = material_area(part);
        if (!recovered::rectangleUsePrimitiveFallback(branch)) { result.complexGroups.push_back(group); continue; }
        if (!recovered::rectangleAdmitPrimitiveFallback(branch)) continue;
        for (const double request : {0., 90.}) {
            const auto requested = normalizedShape(part, {request, false});
            std::optional<size_t> currentModule;
            for (size_t pose = 0; pose < part.poses.size(); ++pose)
                if (sameShape(requested, normalizedShape(part, part.poses[pose]))) { currentModule = pose; break; }
            if (!currentModule) continue;
            const auto variant = bounds(transform(part.polygon, part.pivot, part.poses[*currentModule]));
            recovered::PrimitiveTilingInput primitive;
            primitive.orientation = recovered::quantizeSdkOrientation({0, request, request}, true);
            primitive.variantPresent = true; primitive.gap210 = input.gap;
            primitive.bounds = {variant.x0, variant.y0, variant.x1, variant.y1};
            const auto plan = recovered::primitiveTilingPlan(primitive);
            RectangleTilingModel model;
            model.members.push_back({group, 0, *currentModule, 0, 0});
            model.u = plan->u; model.v = plan->v; model.style = plan->style;
            result.models.push_back(std::move(model));
        }
    }
    return result;
}
}
