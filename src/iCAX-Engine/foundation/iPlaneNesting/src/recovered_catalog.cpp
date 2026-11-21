#include "recovered_catalog.hpp"
namespace study::recovered {
void shuffle(std::vector<CandidateRef> &items, uint32_t seed) {
    uint64_t state = seed % 2147483647U;
    if (!state) state = 1;
    auto next = [&] { state = state * 48271 % 2147483647; return state; };
    for (size_t i = 1; i < items.size(); ++i) {
        uint64_t sample, mask;
        const uint64_t range = i + 1;
        do {
            sample = mask = 0;
            while (mask < i) {
                uint64_t chunk;
                do { chunk = next() - 1; } while (chunk > 1073741823);
                sample = (sample << 30) | chunk;
                mask = (mask << 30) | 1073741823;
            }
        } while (mask / range <= sample / range && mask % range != i);
        std::swap(items[i], items[static_cast<size_t>(sample % range)]);
    }
}
Catalog::Catalog(const std::vector<std::vector<size_t>> &modules, const std::vector<int> &quantities) {
    require(modules.size() == quantities.size(), PN2S_INTERNAL, "Catalog group count mismatch");
    groups.resize(modules.size());
    for (size_t group = 0; group < modules.size(); ++group) {
        std::vector<size_t> pool;
        for (int i = 0; i < quantities[group]; ++i) {
            pool.push_back(instances.size());
            instances.push_back({group, static_cast<size_t>(i), false});
        }
        for (size_t module : modules[group]) {
            groups[group].push_back(records.size());
            records.push_back({0, module, pool, 0});
        }
    }
}
void Catalog::append(size_t index, std::vector<CandidateRef> &out, const Eligible &eligible) {
    auto &r = records.at(index);
    if (r.nestedCount >= static_cast<int64_t>(r.instances.size())) return;
    const size_t start = r.cursor;
    do {
        const size_t instance = r.instances[r.cursor];
        if (!instances[instance].placed && (!eligible || eligible(instance, r.module))) {
            out.push_back({r.module, instance}); return;
        }
        if (++r.cursor >= r.instances.size()) r.cursor = 0;
    } while (r.cursor != start);
}
std::vector<CandidateRef> Catalog::available(const std::vector<size_t> &keys, uint32_t seed, const Eligible &eligible) {
    std::vector<CandidateRef> out;
    if (keys.size() < groups.size()) {
        for (size_t key : keys) for (size_t record : groups.at(key)) append(record, out, eligible);
    } else {
        for (size_t i = 0; i < records.size(); ++i) append(i, out, eligible);
    }
    if (keys.size() == 1 && out.size() > 1) shuffle(out, seed);
    return out;
}
void Catalog::setPlaced(size_t instance, bool placed) {
    auto &p = instances.at(instance);
    require(p.placed != placed, PN2S_INTERNAL, "Catalog duplicate placement/removal");
    p.placed = placed;
    for (auto record : groups[p.group]) records[record].nestedCount += placed ? 1 : -1;
}
}
