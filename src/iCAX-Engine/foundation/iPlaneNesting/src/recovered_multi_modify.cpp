#include "recovered_multi_modify.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace study::recovered {
namespace {
struct RatioRecord { double ratio; int32_t index; };
class RatioSelection {
    std::vector<RatioRecord> records;
    static bool before(const RatioRecord &a, const RatioRecord &b) {
        return a.ratio < b.ratio || (a.ratio == b.ratio && a.index < b.index);
    }
    void insertion(size_t first, size_t last) {
        for (size_t index = first + 1; index < last; ++index) {
            const auto item = records[index];
            size_t hole = index;
            if (before(item, records[first])) {
                std::move_backward(records.begin() + first, records.begin() + index, records.begin() + index + 1);
                hole = first;
            } else {
                while (hole > first && before(item, records[hole - 1])) {
                    records[hole] = records[hole - 1]; --hole;
                }
            }
            records[hole] = item;
        }
    }
    void median(size_t first, size_t middle, size_t last) {
        if (before(records[middle], records[first])) std::swap(records[middle], records[first]);
        if (before(records[last], records[middle])) {
            std::swap(records[last], records[middle]);
            if (before(records[middle], records[first])) std::swap(records[middle], records[first]);
        }
    }
    std::pair<size_t, size_t> partition(size_t first, size_t last) {
        const size_t middle = first + (last - first) / 2;
        if (last - first > 41) {
            const size_t step = (last - first) / 8;
            median(first, first + step, first + step * 2);
            median(middle - step, middle, middle + step);
            median(last - 1 - step * 2, last - 1 - step, last - 1);
            median(first + step, middle, last - 1 - step);
        } else median(first, middle, last - 1);
        size_t equalFirst = middle, equalLast = middle + 1;
        while (first < equalFirst && !before(records[equalFirst - 1], records[equalFirst]) &&
               !before(records[equalFirst], records[equalFirst - 1])) --equalFirst;
        while (equalLast < last && !before(records[equalLast], records[equalFirst]) &&
               !before(records[equalFirst], records[equalLast])) ++equalLast;
        size_t right = equalLast, left = equalFirst;
        for (;;) {
            for (; right < last; ++right) {
                if (before(records[equalFirst], records[right])) continue;
                if (before(records[right], records[equalFirst])) break;
                if (equalLast != right) std::swap(records[equalLast], records[right]);
                ++equalLast;
            }
            for (; first < left; --left) {
                if (before(records[left - 1], records[equalFirst])) continue;
                if (before(records[equalFirst], records[left - 1])) break;
                --equalFirst;
                if (equalFirst != left - 1) std::swap(records[equalFirst], records[left - 1]);
            }
            if (left == first && right == last) return {equalFirst, equalLast};
            if (left == first) {
                if (equalLast != right) std::swap(records[equalFirst], records[equalLast]);
                ++equalLast;
                std::swap(records[equalFirst++], records[right++]);
            } else if (right == last) {
                --left; --equalFirst;
                if (left != equalFirst) std::swap(records[left], records[equalFirst]);
                std::swap(records[equalFirst], records[--equalLast]);
            } else std::swap(records[right++], records[--left]);
        }
    }
public:
    explicit RatioSelection(std::vector<RatioRecord> input) : records(std::move(input)) {}
    std::vector<RatioRecord> select(size_t count) {
        if (count == records.size()) return std::move(records);
        size_t first = 0, last = records.size();
        while (last - first > 32) {
            const auto [begin, end] = partition(first, last);
            if (count >= end) first = end;
            else if (count < begin) last = begin;
            else { records.resize(count); return std::move(records); }
        }
        if (first != last) insertion(first, last);
        records.resize(count);
        return std::move(records);
    }
};
}
std::vector<size_t> multiLayoutRemovalIndices(std::span<const MultiLnsSheet> sheets, bool includeLast,
    int32_t lowestRatioCount, int32_t randomCount, const std::function<size_t(size_t)> &randomIndex) {
    if (sheets.empty()) return {};
    if (sheets.size() > size_t(INT32_MAX)) throw std::length_error("Multi layout board count");
    std::set<size_t> selected;
    if (includeLast) selected.insert(sheets.size() - 1);
    for (int32_t iteration = 0; iteration < randomCount; ++iteration) {
        const auto index = randomIndex(sheets.size());
        if (index >= sheets.size()) throw std::out_of_range("Multi layout random board index");
        selected.insert(index);
    }
    std::vector<RatioRecord> ratios;
    for (size_t index = 0; index + 1 < sheets.size(); ++index) {
        const auto cost = multiLnsSheetCost(sheets[index]);
        if (!(.001 > cost)) ratios.push_back({sheets[index].informerRequiredArea / cost, int32_t(index)});
    }
    // MOVSXD followed by an unsigned CMOVB: a negative request selects all
    // available ratio candidates, matching the original internal integer path.
    const auto count = size_t(std::min(uint64_t(ratios.size()), uint64_t(int64_t(lowestRatioCount))));
    for (const auto &record : RatioSelection(std::move(ratios)).select(count)) selected.insert(size_t(record.index));
    return {selected.rbegin(), selected.rend()};
}
uint32_t multiModifyLayout(MultiLnsHost &host, bool includeLast, int32_t lowestRatioCount, int32_t randomCount) {
    const auto indices = multiLayoutRemovalIndices(host.sheets(), includeLast, lowestRatioCount, randomCount, host.randomIndex);
    for (const auto index : indices) host.remove(index);
    return uint32_t(indices.size());
}
MultiColumnCompletionResult multiCompleteColumns(MultiColumnCompletionHost &host) {
    MultiColumnCompletionResult result;
    if (!host.stopped() && host.unit() < .75 && !host.hasTechnology) {
        const auto randomCount = host.integer(0, 2);
        const auto lowestCount = host.integer(0, 2);
        host.modify(false, lowestCount, randomCount);
        result.modifiedFirst = true;
    }
    result.addedFirst = host.complete();
    if (result.addedFirst < 2 && !host.stopped() && !host.hasTechnology) {
        const auto lowestCount = host.integer(1, 3);
        const auto randomCount = host.integer(1, 3);
        host.modify(!host.preserveLast, lowestCount, randomCount);
        result.modifiedSecond = true;
        result.addedSecond = host.complete();
    }
    return result;
}
}
