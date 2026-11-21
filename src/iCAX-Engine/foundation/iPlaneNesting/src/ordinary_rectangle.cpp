#include "rectangle_runtime.hpp"
#include "ordinary_rectangle_tiling.hpp"
#include "ordinary_general_tiling.hpp"
namespace study {
namespace {
struct PoseMapping { size_t source; double angle; Box actual, normalized; };
bool normalize(const Input &input, Input &normalized, std::vector<std::vector<PoseMapping>> &maps) {
    normalized = input;
    if (!grid_exact(input.gap, 1e6) || !grid_exact(input.gap, 1000)) return false;
    for (size_t group = 0; group < input.parts.size(); ++group) {
        const auto &part = input.parts[group];
        if (!part.holes.empty() || part.protection > 0 || part.polygon.size() != 4) return false;
        const auto box = bounds(part.polygon);
        const double width = box.x1 - box.x0, height = box.y1 - box.y0;
        if (!(width > 0. && height > 0.) || !grid_exact(width, 1e6) || !grid_exact(width, 1000) ||
            !grid_exact(height, 1e6) || !grid_exact(height, 1000)) return false;
        for (size_t index = 0; index < 4; ++index) {
            const auto a = part.polygon[index], b = part.polygon[(index + 1) % 4];
            if (a.x != b.x && a.y != b.y) return false;
        }
        if (std::abs(area(part.polygon) - width * height) > 1e-6) return false;
        auto &target = normalized.parts[group];
        target.polygon = {{0, 0}, {width, 0}, {width, height}, {0, height}};
        target.pivot = {0, 0}; target.poses.clear();
        maps.emplace_back();
        std::array<bool, 2> included{};
        for (size_t index = 0; index < part.poses.size(); ++index) {
            const auto pose = part.poses[index];
            const int low = int(std::ceil((pose.angle - 1e-9) / 90.));
            const int high = int(std::floor((pose.upper + 1e-9) / 90.));
            for (int quarter = low; quarter <= high; ++quarter) {
                const int direction = (quarter % 2 + 2) % 2;
                if (included[direction]) continue;
                included[direction] = true;
                const double angle = std::clamp(90. * quarter, pose.angle, pose.upper);
                const Pose canonical{90. * direction, false};
                target.poses.push_back(canonical);
                maps.back().push_back({index, angle, bounds(transform(part.polygon, part.pivot, {angle, pose.flip})),
                                      bounds(transform(target.polygon, {}, canonical))});
            }
        }
        if (target.poses.empty()) return false;
    }
    return !input.parts.empty();
}
class RectangleGeometryHost final : public recovered::RectangleHost {
    const Input &input;
    const RectangleProgram &program;
    Budget &budget;
    OrdinaryRectangleTrace *trace;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    bool expired = false;
    std::vector<recovered::RectanglePriceGroup> groups;
    std::vector<std::vector<recovered::RectangleComposition>> modules;
    double volume, stockPrice;
public:
    RectangleGeometryHost(const Input &source, const RectangleProgram &settings, Budget &time,
                          OrdinaryRectangleTrace *record)
        : input(source), program(settings), budget(time), trace(record),
          volume((source.width + source.gap) * (source.height + source.gap)),
          stockPrice(settings.stockPrice == 0. ? source.width * source.height * 1e-5 : settings.stockPrice) {
        require(program.groupPrices.empty() || program.groupPrices.size() == input.parts.size(),
                PN2S_INVALID, "Rectangle price override group count mismatch");
        require(stockPrice > 0., PN2S_INVALID, "Rectangle stock price must be positive");
        for (size_t group = 0; group < input.parts.size(); ++group) {
            const auto &part = input.parts[group];
            const auto original = bounds(part.polygon);
            const double weight = std::max(.001, program.groupPrices.empty() ?
                area(part.polygon) : program.groupPrices[group]);
            recovered::RectanglePriceGroup pricing{part.quantity, part.quantity, weight, std::nullopt};
            double bestCost = 0.;
            for (const auto pose : part.poses) {
                const double width = (pose.angle == 0. ? original.x1 : original.y1) + input.gap;
                const double height = (pose.angle == 0. ? original.y1 : original.x1) + input.gap;
                const auto count = [](double span, double dimension) {
                    if (span - dimension > 1e-9) return 0;
                    const double residual = dimension - span;
                    if (residual < 1e-9) return 1;
                    return int(std::min(1000., std::floor((residual + 1e-9) / span) + 1.));
                };
                const int nx = count(width, input.width + input.gap),
                          ny = count(height, input.height + input.gap);
                if (nx && ny && part.quantity > 0) {
                    const double cost = ((width + (nx - 1) * width) * (height + (ny - 1) * height)) /
                                        double(nx * ny);
                    if (!pricing.referenceSpan || cost < bestCost) {
                        pricing.referenceSpan = std::array{width, height, 1.}; bestCost = cost;
                    }
                }
                if (settings.modules.empty()) modules.push_back({{group, 1}});
            }
            groups.push_back(pricing);
        }
        if (!settings.modules.empty()) {
            for (auto &group : groups) group.referenceSpan.reset();
            std::vector<bool> covered(groups.size());
            for (const auto &model : settings.modules) for (const auto &member : model.members) covered.at(member.group) = true;
            for (size_t group = 0; group < groups.size(); ++group) if (!covered[group])
                groups[group].required = groups[group].maximum = 0;
            std::vector<double> referenceCost(groups.size());
            for (const auto &model : settings.modules) {
                const auto box = rectangleModelBounds(input, model);
                const std::array<double, 3> span{box.x1 - box.x0 + input.gap,
                                                box.y1 - box.y0 + input.gap, 1.};
                const std::array<double, 2> pitch{model.u.x, model.v.y};
                std::map<size_t, int32_t> composition;
                for (const auto &member : model.members) ++composition[member.group];
                std::vector<recovered::RectangleComposition> entries;
                int maximum = INT32_MAX;
                for (const auto [group, coefficient] : composition) {
                    entries.push_back({group, coefficient});
                    maximum = std::min(maximum, groups[group].maximum / coefficient);
                }
                modules.push_back(std::move(entries));
                const auto count = [](double base, double step, double dimension, int32_t limit) {
                    if (base - dimension > 1e-9) return 0;
                    if (dimension - base < 1e-9) return 1;
                    const double q = step > 1e-9 ? std::floor((dimension - base + 1e-9) / step) : 0.;
                    return int(std::min(q + 1., double(limit == 0 ? 1000 : std::max(limit, 1))));
                };
                const int nx = count(span[0], pitch[0], input.width + input.gap, model.maximumX);
                const int ny = count(span[1], pitch[1], input.height + input.gap, model.maximumY);
                if (maximum == 0 || nx == 0 || ny == 0) continue;
                const double cost = ((span[0] + (nx - 1) * pitch[0]) * (span[1] + (ny - 1) * pitch[1])) /
                                    double(nx * ny);
                // 73d160's ordinary module list is homogeneous: only the
                // coefficient of its first composition entry divides the cost.
                for (const auto [group, coefficient] : composition)
                    if (!groups[group].referenceSpan || cost / coefficient < referenceCost[group]) {
                        groups[group].referenceSpan = span; referenceCost[group] = cost / coefficient;
                    }
            }
        }
    }
    int callbackResult() override { return expired ? 1 : 0; }
    double elapsed() override { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); }
    std::optional<recovered::RectangleCandidate> construct(double target, recovered::RectangleNoise noise,
                                                          recovered::Random &random) override {
        auto prices = recovered::rectanglePrices(random, groups, modules, volume, noise.alpha, 0., noise.module);
        if (trace) { ++trace->recursiveTrials; trace->modulePrices.push_back(prices.modules); }
        RectangleCoreOptions options;
        options.modules = program.modules;
        options.targetWidth = target; options.groupPrices = std::move(prices.groups);
        options.modulePrices = std::move(prices.modules);
        try {
            auto tree = std::make_shared<RectangleCoreResult>(rectangleCore(input, budget, options));
            if (!tree->hasTree) return std::nullopt;
            double originalValue = 0.;
            for (size_t group = 0; group < groups.size(); ++group)
                originalValue += double(tree->counts[group]) * groups[group].weight;
            return recovered::RectangleCandidate{tree->complete, volume, tree->dimensions[0],
                                                 originalValue / stockPrice, tree};
        } catch (const Failure &error) {
            if (error.code != PN2S_TIMEOUT) throw;
            expired = true; return std::nullopt;
        }
    }
    void finalFill(recovered::RectangleCandidate &candidate) override {
        const auto tree = std::static_pointer_cast<RectangleCoreResult>(candidate.payload);
        // 758eb0 can add offcuts or still-available modules. Ordinary demand
        // has required==maximum; a complete tree exhausts every represented
        // group, including groups consumed through multi-member compositions.
        require(tree && tree->complete, PN2S_INTERNAL, "Rectangle final fill requires exhausted ordinary demand");
    }
};
}
std::vector<RectangleTilingModel> ordinaryRectangleTilingModelsForInput(const Input &input, Budget &budget,
    double contextSeconds,OrdinaryGeneralTilingTrace *trace) {
    Input normalized = input; normalized.parts.clear();
    std::vector<std::vector<PoseMapping>> maps; std::vector<size_t> originalGroups;
    for (size_t group = 0; group < input.parts.size(); ++group) {
        Input single = input; single.parts = {input.parts[group]};
        Input canonical; std::vector<std::vector<PoseMapping>> oneMap;
        if (!normalize(single, canonical, oneMap)) continue;
        normalized.parts.push_back(std::move(canonical.parts.front()));
        maps.push_back(std::move(oneMap.front())); originalGroups.push_back(group);
    }
    auto models = ordinaryRectangleTilingModels(normalized, budget);
    for (auto &model : models) for (auto &member : model.members) {
        const auto &mapping = maps[member.group][member.poseIndex];
        member.exactX = double(member.x) / 1e6 + mapping.normalized.x0 - mapping.actual.x0;
        member.exactY = double(member.y) / 1000. + mapping.normalized.y0 - mapping.actual.y0;
        member.poseIndex = mapping.source; member.solvedAngle = mapping.angle;
        member.x = rounded(member.exactX * 1e6); member.y = rounded(member.exactY * 1000.);
        member.group = originalGroups[member.group];
    }
    auto general=ordinaryGeneralTilingModelsForInput(input,budget,
        std::isfinite(contextSeconds)?contextSeconds:budget.requestedSeconds,trace);
    models.insert(models.end(),std::make_move_iterator(general.begin()),std::make_move_iterator(general.end()));
    return models;
}
bool ordinaryRectangle(const Input &input, Budget &budget, Output &output, const RectangleProgram &settings,
                       OrdinaryRectangleTrace *trace) {
    Input normalized; std::vector<std::vector<PoseMapping>> maps;
    if (settings.modules.empty() && !normalize(input, normalized, maps)) return false;
    auto config = settings.config;
    const double remaining = std::chrono::duration<double>(budget.deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0.) return true;
    if (config.timeBudget == 0. || config.timeBudget > remaining) config.timeBudget = remaining;
    if (trace) *trace = {};
    const bool mapped = settings.modules.empty();
    auto program = settings;
    if (mapped) {
        program.modules = ordinaryRectangleTilingModels(normalized, budget);
        if (program.modules.empty()) { output = {}; return true; }
    }
    program.modules = rectangleExpandModelLimits(program.modules);
    std::vector<bool> covered(input.parts.size());
    for (const auto &model : program.modules) for (const auto &member : model.members)
        if (input.parts.at(member.group).quantity > 0) covered.at(member.group) = true;
    const size_t participating = size_t(std::count(covered.begin(), covered.end(), true));
    if (trace) trace->participatingGroups = participating;
    if (!participating) { output = {}; return true; }
    if (!config.iterationLimit) config.iterationLimit = recovered::rectangleIterationLimit(participating);
    RectangleGeometryHost host(mapped ? normalized : input, program, budget, trace);
    recovered::Random random(input.randomSeed + uint32_t(1));
    auto best = recovered::rectangleSearch(host, random, config, trace ? &trace->search : nullptr);
    if (!best) { output = {}; return true; }
    output = std::static_pointer_cast<RectangleCoreResult>(best->payload)->output;
    if (mapped) for (auto &placement : output.placements) {
        const auto &mapping = maps[placement.group][placement.poseIndex];
        placement.exactX = double(placement.x) / 1e6 + mapping.normalized.x0 - mapping.actual.x0;
        placement.exactY = double(placement.y) / 1000. + mapping.normalized.y0 - mapping.actual.y0;
        placement.poseIndex = mapping.source; placement.solvedAngle = mapping.angle;
        placement.x = rounded(placement.exactX * 1e6); placement.y = rounded(placement.exactY * 1000.);
    }
    return true;
}
}
