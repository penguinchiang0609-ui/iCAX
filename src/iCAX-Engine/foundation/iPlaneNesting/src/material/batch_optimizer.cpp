#include "batch_optimizer.hpp"
#include <optional>

namespace icax::iplane::batch {
namespace {
using namespace study;
using Identity = std::pair<size_t, size_t>;
using Pool = std::vector<std::vector<size_t>>;
using Clock = std::chrono::steady_clock;
constexpr double tolerance = 1e-6;
struct Physical {
    size_t stock, ordinal;
};
struct NormalStock {
    geometry::Board geometry;
    Point origin;
    double width, height, area;
};
double remaining(const Budget &time) {
    return std::max(0., std::chrono::duration<double>(time.deadline - Clock::now()).count());
}
Budget child(Budget &parent, double seconds, Clock::time_point deadline) {
    Budget local(std::max(0., seconds));
    local.deadline = std::min({local.deadline, parent.deadline, deadline});
    return local;
}
uint64_t priority(uint32_t seed, size_t a, size_t b = 0) {
    uint64_t x = (uint64_t(seed) << 32) ^ (a + 1) * 0x9e3779b97f4a7c15ULL ^ (b + 1);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
void grid(Placement &record) {
    record.x = std::abs(record.exactX) < 9e12 ? rounded(record.exactX * 1e6) : 0;
    record.y = std::abs(record.exactY) < 9e15 ? rounded(record.exactY * 1e3) : 0;
}
size_t count(const Pool &pool) {
    size_t result = 0;
    for (const auto &group : pool)
        result += group.size();
    return result;
}
void remove(Pool &pool, const Output &output) {
    for (const auto &part : output.placements) {
        auto &group = pool.at(part.group);
        const auto item = std::find(group.begin(), group.end(), part.ordinal);
        require(item != group.end(), PN2S_INVALID, "批次中出现重复的零件实例");
        group.erase(item);
    }
}
bool better(const Score &a, const Score &b) {
    if (a.placed != b.placed)
        return a.placed > b.placed;
    if (std::abs(a.materialArea - b.materialArea) > tolerance)
        return a.materialArea > b.materialArea;
    if (a.boards != b.boards)
        return a.boards < b.boards;
    if (std::abs(a.netStockArea - b.netStockArea) > tolerance)
        return a.netStockArea < b.netStockArea;
    return a.envelopeArea < b.envelopeArea - tolerance;
}
class Optimizer {
    const Input &input;
    const std::vector<Stock> &stocks;
    Budget &parent;
    Budget search;
    double initialSeconds;
    std::vector<NormalStock> normalized;
    std::vector<Physical> physical;
    std::vector<double> partAreas;
    mutable std::map<std::tuple<size_t, size_t, double>, Box> poseBounds;
    Pool demand;
    Result result;
    size_t callSequence = 0;

    void warning(const std::string &message) {
        if (std::find(result.warnings.begin(), result.warnings.end(), message) == result.warnings.end())
            result.warnings.push_back(message);
    }
    void stopped(const Failure &failure) {
        if (failure.code != PN2S_TIMEOUT && failure.code != PN2S_RESOURCE_LIMIT)
            throw failure;
        result.stopped = true;
        warning(failure.code == PN2S_TIMEOUT ? "整批排样已达到分配的搜索时段，保留已验证的最佳布局"
                                             : "部分图形达到几何处理上限，保留已验证的最佳布局");
    }
    Score score(const std::vector<BoardLayout> &boards) const {
        Score total;
        std::set<Identity> instances, sheets;
        for (const auto &board : boards) {
            require(board.stock < stocks.size() &&
                        board.stockOrdinal < size_t(stocks[board.stock].quantity),
                    PN2S_INVALID, "板材实例不属于当前库存");
            require(sheets.emplace(board.stock, board.stockOrdinal).second, PN2S_INVALID,
                    "同一张物理板材不能重复使用");
            require(!board.output.placements.empty(), PN2S_INVALID, "空板材不能计入消耗库存");
            ++total.boards;
            total.netStockArea += normalized[board.stock].area;
            Box occupied{};
            bool first = true;
            for (const auto &record : board.output.placements) {
                require(record.group < input.parts.size(), PN2S_INVALID, "未知零件种类");
                const auto &part = input.parts[record.group];
                require(record.ordinal < size_t(part.quantity) &&
                            instances.emplace(record.group, record.ordinal).second,
                        PN2S_INVALID, "零件实例重复或超出需求");
                require(record.poseIndex < part.poses.size() && std::isfinite(record.solvedAngle) &&
                            std::isfinite(record.exactX) && std::isfinite(record.exactY),
                        PN2S_INVALID, "零件的姿态或位置无效");
                auto pose = part.poses[record.poseIndex];
                pose.angle = record.solvedAngle;
                const auto key = std::tuple{record.group, record.poseIndex, record.solvedAngle};
                auto found = poseBounds.find(key);
                Box box;
                if (found != poseBounds.end())
                    box = found->second;
                else {
                    box = bounds(transform(part.polygon, part.pivot, pose));
                    if (poseBounds.size() < 16384)
                        poseBounds.emplace(key, box);
                }
                const auto origin = normalized[board.stock].origin;
                box.x0 += record.exactX - origin.x;
                box.x1 += record.exactX - origin.x;
                box.y0 += record.exactY - origin.y;
                box.y1 += record.exactY - origin.y;
                if (first) {
                    occupied = box;
                    first = false;
                } else {
                    occupied.x0 = std::min(occupied.x0, box.x0);
                    occupied.x1 = std::max(occupied.x1, box.x1);
                    occupied.y0 = std::min(occupied.y0, box.y0);
                    occupied.y1 = std::max(occupied.y1, box.y1);
                }
                ++total.placed;
                total.materialArea += partAreas[record.group];
            }
            total.envelopeArea += (occupied.x1 - occupied.x0) * (occupied.y1 - occupied.y0);
        }
        return total;
    }
    bool validate(const BoardLayout &board) {
        Input local = input;
        const auto &stock = normalized.at(board.stock);
        local.width = stock.width;
        local.height = stock.height;
        auto output = board.output;
        for (auto &record : output.placements) {
            record.exactX -= stock.origin.x;
            record.exactY -= stock.origin.y;
            grid(record);
        }
        if (!geometry::validate(local, stock.geometry, output, &parent).valid)
            return false;
        return geometry::validate(input, stocks[board.stock].geometry, board.output, &parent).valid;
    }
    bool accept(BoardLayout &board) {
        if (validate(board))
            return true;
        // Coordinate restoration changes the integer-grid phase. Recover exact
        // nearby source contacts/events instead of relaxing a material tolerance.
        // Every proposed repair is rechecked in both coordinate frames.
        size_t attempts = 0;
        const auto &stock = stocks[board.stock].geometry;
        for (auto &record : board.output.placements) {
            const auto original = record;
            const auto &part = input.parts[record.group];
            const auto &range = part.poses[record.poseIndex];
            std::vector<double> angles{record.solvedAngle};
            for (const double angle : {range.angle, range.upper, (range.angle + range.upper) * .5})
                if (std::abs(angle - original.solvedAngle) <= 1e-5 &&
                    std::find(angles.begin(), angles.end(), angle) == angles.end())
                    angles.push_back(angle);
            for (const auto angle : angles) {
                parent.check(true);
                auto pose = range;
                pose.angle = angle;
                const auto moving = transform(part.polygon, part.pivot, pose);
                auto attempt = [&](Point position) {
                    if (++attempts > 64)
                        return false;
                    record.solvedAngle = angle;
                    record.exactX = position.x;
                    record.exactY = position.y;
                    grid(record);
                    const bool valid = validate(board);
                    if (!valid)
                        record = original;
                    return valid;
                };
                if (attempt({original.exactX, original.exactY}))
                    return true;
                size_t pairs = 0;
                for (const auto fixed : stock.outer)
                    for (const auto point : moving) {
                        if (++pairs > 4096 || attempts >= 64)
                            break;
                        const Point position{fixed.x - point.x, fixed.y - point.y};
                        if (std::abs(position.x - original.exactX) <= 4 * tolerance &&
                            std::abs(position.y - original.exactY) <= 4 * tolerance && attempt(position))
                            return true;
                    }
                if (attempts >= 64)
                    break;
            }
            record = original;
            if (attempts >= 64)
                break;
        }
        return false;
    }
    void consider(const std::vector<BoardLayout> &boards, const char *source) {
        const auto current = score(boards);
        if (!better(current, result.score))
            return;
        result.boards = boards;
        result.score = current;
        result.statistics.bestSource = source;
        ++result.statistics.acceptedImprovements;
    }
    Pool poolWithout(const std::vector<BoardLayout> &boards) const {
        auto pool = demand;
        for (const auto &board : boards)
            remove(pool, board.output);
        return pool;
    }
    bool occupied(Physical candidate, const std::vector<BoardLayout> &boards) const {
        return std::any_of(boards.begin(), boards.end(), [&](const auto &board) {
            return board.stock == candidate.stock && board.stockOrdinal == candidate.ordinal;
        });
    }
    std::optional<BoardLayout> place(Physical sheet, const Pool &pool, Budget &phase,
                                     size_t expectedCalls, bool preserveSeed = false) {
        if (!count(pool))
            return std::nullopt;
        phase.check(true);
        search.check(true);
        parent.check(true);
        ++result.statistics.stockComparisons;
        const auto &stock = normalized[sheet.stock];
        Input local = input;
        local.width = stock.width;
        local.height = stock.height;
        local.randomSeed =
            preserveSeed ? input.randomSeed : input.randomSeed ^ uint32_t(callSequence++) * 0x9e3779b9U;
        for (size_t group = 0; group < local.parts.size(); ++group)
            local.parts[group].quantity = int(pool[group].size());
        auto time =
            child(search, remaining(phase) / double(std::max(size_t(1), expectedCalls)), phase.deadline);
        const auto run = geometry::nest(local, stock.geometry, time);
        if (run.stopped) {
            result.stopped = true;
            if (!run.warning.empty())
                warning(run.warning);
        }
        if (run.output.placements.empty())
            return std::nullopt;
        BoardLayout board{sheet.stock, sheet.ordinal, run.output, "material-dll"};
        for (auto &record : board.output.placements) {
            record.ordinal = pool.at(record.group).at(record.ordinal);
            record.exactX += stock.origin.x;
            record.exactY += stock.origin.y;
            grid(record);
        }
        // Validate after restoring the original origin, then subtract it again
        // inside the validator. This also catches precision lost in restoration.
        require(accept(board), PN2S_RESOURCE_LIMIT, "板材坐标或几何精度不足，未接受候选布局");
        return board;
    }
    std::vector<Physical> order(int mode, size_t variation = 0) const {
        auto list = physical;
        if (mode == 0)
            return list;
        std::stable_sort(list.begin(), list.end(), [&](Physical a, Physical b) {
            const auto x = normalized[a.stock].area, y = normalized[b.stock].area;
            if (mode == 3)
                return priority(input.randomSeed ^ uint32_t(variation), a.stock, a.ordinal) <
                       priority(input.randomSeed ^ uint32_t(variation), b.stock, b.ordinal);
            if (x != y)
                return mode == 1 ? x > y : x < y;
            return a.stock < b.stock || (a.stock == b.stock && a.ordinal < b.ordinal);
        });
        return list;
    }
    void construct(const std::vector<Physical> &list, std::vector<BoardLayout> trial, Pool pool,
                   size_t maximumBoards, bool compareTypes, double seconds, const char *source,
                   bool preserveSeed = false) {
        ++result.statistics.constructionTrials;
        auto phase = child(search, seconds, search.deadline);
        try {
            if (!compareTypes) {
                for (size_t n = 0; n < list.size() && count(pool) && trial.size() < maximumBoards; ++n) {
                    if (occupied(list[n], trial))
                        continue;
                    try {
                        auto board = place(list[n], pool, phase, list.size() - n, preserveSeed);
                        if (!board)
                            continue;
                        remove(pool, board->output);
                        trial.push_back(std::move(*board));
                        consider(trial, source);
                    } catch (const Failure &failure) {
                        if (failure.code != PN2S_RESOURCE_LIMIT)
                            throw;
                        stopped(failure);
                    }
                }
            } else {
                while (count(pool) && trial.size() < maximumBoards) {
                    std::optional<BoardLayout> selected;
                    Score selectedScore;
                    bool exhausted = false;
                    std::set<size_t> compared;
                    size_t available = 0;
                    for (const auto sheet : list)
                        if (!occupied(sheet, trial))
                            ++available;
                    if (!available)
                        break;
                    for (const auto sheet : list) {
                        if (occupied(sheet, trial) || !compared.insert(sheet.stock).second)
                            continue;
                        try {
                            auto board =
                                place(sheet, pool, phase, available + stocks.size(), preserveSeed);
                            if (!board)
                                continue;
                            const auto value = score({*board});
                            if (!selected || better(value, selectedScore)) {
                                selected = std::move(board);
                                selectedScore = value;
                            }
                        } catch (const Failure &failure) {
                            if (failure.code == PN2S_TIMEOUT) {
                                stopped(failure);
                                exhausted = true;
                                break;
                            }
                            if (failure.code != PN2S_RESOURCE_LIMIT)
                                throw;
                            stopped(failure);
                        }
                    }
                    if (!selected)
                        break;
                    remove(pool, selected->output);
                    trial.push_back(std::move(*selected));
                    consider(trial, source);
                    if (exhausted)
                        break;
                }
            }
        } catch (const Failure &failure) {
            stopped(failure);
        }
        consider(trial, source);
    }
    void localRepairs(double seconds) {
        auto phase = child(search, seconds, search.deadline);
        const auto snapshot = result.boards;
        std::vector<size_t> indices(snapshot.size());
        std::iota(indices.begin(), indices.end(), size_t(0));
        std::stable_sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
            return snapshot[a].output.placements.size() < snapshot[b].output.placements.size();
        });
        const auto candidates = order(2);
        for (size_t n = 0; n < indices.size(); ++n) {
            phase.check(true);
            for (size_t m = n; m < indices.size(); ++m) {
                const size_t a = indices[n], b = indices[m];
                std::vector<BoardLayout> outside;
                Pool pool(input.parts.size());
                for (size_t k = 0; k < snapshot.size(); ++k) {
                    if (k != a && k != b)
                        outside.push_back(snapshot[k]);
                    else
                        for (const auto &p : snapshot[k].output.placements)
                            pool[p.group].push_back(p.ordinal);
                }
                double material = 0;
                for (size_t k = 0; k < pool.size(); ++k)
                    material += partAreas[k] * pool[k].size();
                for (const auto sheet : candidates) {
                    if (occupied(sheet, outside) || normalized[sheet.stock].area + tolerance < material)
                        continue;
                    if (a == b &&
                        normalized[sheet.stock].area > normalized[snapshot[a].stock].area + tolerance)
                        continue;
                    if (a == b)
                        ++result.statistics.replacementTrials;
                    else
                        ++result.statistics.consolidationTrials;
                    try {
                        auto board = place(sheet, pool, phase, std::max(size_t(1), candidates.size()));
                        if (!board || board->output.placements.size() != count(pool))
                            continue;
                        auto trial = outside;
                        trial.push_back(std::move(*board));
                        consider(trial, a == b ? "replacement" : "consolidation");
                    } catch (const Failure &failure) {
                        if (failure.code == PN2S_RESOURCE_LIMIT) {
                            stopped(failure);
                            continue;
                        }
                        throw;
                    }
                }
            }
        }
    }

  public:
    Optimizer(const Input &source, const std::vector<Stock> &inventory, Budget &time)
        : input(source), stocks(inventory), parent(time), search(remaining(time) * .8),
          initialSeconds(remaining(time)) {
        search.deadline = std::min(search.deadline, parent.deadline);
        require(input.parts.size() <= 500 && stocks.size() <= 100, PN2S_RESOURCE_LIMIT,
                "批次种类超过几何处理上限");
        require(input.pointGroups.empty() && !input.lOffcut, PN2S_UNSUPPORTED,
                "当前材料排样不支持点邻近或专用余料约束");
        demand.resize(input.parts.size());
        size_t required = 0;
        for (size_t n = 0; n < input.parts.size(); ++n) {
            const auto &part = input.parts[n];
            require(part.quantity >= 0 && (part.quantity == 0 || !part.poses.empty()), PN2S_INVALID,
                    "零件需求或姿态域无效");
            required += size_t(part.quantity);
            require(required <= 500, PN2S_RESOURCE_LIMIT, "批次零件数量超过几何处理上限");
            demand[n].resize(size_t(part.quantity));
            std::iota(demand[n].begin(), demand[n].end(), size_t(0));
            const auto outer = transform(part.polygon, part.pivot, Pose{});
            std::vector<Polygon> holes;
            for (const auto &hole : part.holes)
                holes.push_back(transform(hole, part.pivot, Pose{}));
            partAreas.push_back(geometry::materialArea(outer, holes));
        }
        for (size_t n = 0; n < stocks.size(); ++n) {
            require(stocks[n].quantity >= 0, PN2S_INVALID, "板材库存无效");
            if (stocks[n].quantity == 0) {
                normalized.push_back({stocks[n].geometry, {}, 0, 0, 0});
                continue;
            }
            const auto box = bounds(stocks[n].geometry.outer);
            require(std::max({std::abs(box.x0), std::abs(box.x1), std::abs(box.y0), std::abs(box.y1)}) <
                        1e10,
                    PN2S_RESOURCE_LIMIT, "板材原点超出当前整数几何计算范围");
            NormalStock stock{stocks[n].geometry, {box.x0, box.y0}, box.x1 - box.x0, box.y1 - box.y0, 0};
            for (auto &p : stock.geometry.outer) {
                p.x -= box.x0;
                p.y -= box.y0;
            }
            for (auto &hole : stock.geometry.holes)
                for (auto &p : hole) {
                    p.x -= box.x0;
                    p.y -= box.y0;
                }
            stock.area = geometry::materialArea(stock.geometry.outer, stock.geometry.holes);
            require(stock.width > 0 && stock.height > 0 && stock.area > tolerance, PN2S_INVALID,
                    "板材有效材料无效");
            normalized.push_back(std::move(stock));
            for (int k = 0; k < stocks[n].quantity; ++k) {
                require(physical.size() < 100, PN2S_RESOURCE_LIMIT, "物理板材数量超过几何处理上限");
                physical.push_back({n, size_t(k)});
            }
        }
    }
    Result run(const std::vector<BoardLayout> &incumbent) {
        result.boards = incumbent;
        for (auto &board : result.boards)
            for (auto &record : board.output.placements) {
                if (!std::isfinite(record.exactX))
                    record.exactX = double(record.x) / 1e6;
                if (!std::isfinite(record.exactY))
                    record.exactY = double(record.y) / 1e3;
                if (!std::isfinite(record.solvedAngle) && record.group < input.parts.size() &&
                    record.poseIndex < input.parts[record.group].poses.size()) {
                    const auto &pose = input.parts[record.group].poses[record.poseIndex];
                    if (pose.angle == pose.upper)
                        record.solvedAngle = pose.angle;
                }
            }
        result.score = score(result.boards);
        try {
            for (const auto &board : result.boards)
                require(validate(board), PN2S_INVALID, "传入的已有布局未通过真实材料校验");
            if (physical.size() == 1) {
                // No inventory-order or cross-board decision exists here.
                // Preserve the full remaining search window for the sheet host.
                construct(order(0), {}, demand, 1, false, remaining(search), "single-board", true);
            } else {
                if (incumbent.empty())
                    // Establish the stable-seed inventory-order baseline before
                    // spending surplus time on cross-board alternatives. Each
                    // physical sheet still receives only its remaining share.
                    construct(order(0), {}, demand, physical.size(), false, remaining(search),
                              "inventory-order", true);
                else
                    construct(order(2), result.boards, poolWithout(result.boards), physical.size(), true,
                              initialSeconds * .12, "extend");
                try {
                    localRepairs(initialSeconds * .15);
                } catch (const Failure &failure) {
                    stopped(failure);
                }
                for (int mode : {1, 2}) {
                    search.check(true);
                    construct(order(mode), {}, demand, physical.size(), false, initialSeconds * .13,
                              mode == 1 ? "large-first" : "small-first");
                }
                construct(order(2), {}, demand, physical.size(), true, initialSeconds * .15,
                          "stock-comparison");
                search.check(true);
                try {
                    localRepairs(initialSeconds * .13);
                } catch (const Failure &failure) {
                    stopped(failure);
                }
                for (size_t pass = 0; pass < 8 && remaining(search) > .002; ++pass) {
                    ++result.statistics.repackTrials;
                    const auto maximum = result.boards.empty() ? physical.size() : result.boards.size();
                    auto list = order(pass % 3 == 0 ? 1 : pass % 3 == 1 ? 2 : 3, pass + 1);
                    construct(list, {}, demand, maximum, false,
                              std::min(initialSeconds * .1, remaining(search)), "repack");
                    if (result.score.placed == count(demand) && result.boards.size() > 1) {
                        ++result.statistics.repackTrials;
                        construct(list, {}, demand, result.boards.size() - 1, false,
                                  std::min(initialSeconds * .1, remaining(search)), "remove-board");
                    }
                }
            }
        } catch (const Failure &failure) {
            stopped(failure);
        }
        // Every new board was strictly accepted under parent. The final pass
        // checks batch inventory and demand together without resetting its clock.
        result.score = score(result.boards);
        const auto missing = poolWithout(result.boards);
        for (size_t group = 0; group < missing.size(); ++group)
            for (const auto ordinal : missing[group])
                result.unplaced.emplace_back(group, ordinal);
        require(result.score.placed + result.unplaced.size() == count(demand), PN2S_INVALID,
                "批次需求守恒校验失败");
        return std::move(result);
    }
};
} // namespace
Result optimize(const study::Input &input, const std::vector<Stock> &inventory,
                const std::vector<BoardLayout> &incumbent, study::Budget &parent) {
    if (std::all_of(input.parts.begin(), input.parts.end(),
                    [](const auto &part) { return part.quantity == 0; })) {
        require(incumbent.empty(), PN2S_INVALID, "零需求批次不能包含已有零件布局");
        return {};
    }
    Optimizer optimizer(input, inventory, parent);
    return optimizer.run(incumbent);
}
} // namespace icax::iplane::batch
