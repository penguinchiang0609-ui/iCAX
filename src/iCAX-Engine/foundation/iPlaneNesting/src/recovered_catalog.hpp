#pragma once
#include "core.hpp"
#include <functional>
namespace study::recovered {
struct CandidateRef { size_t module, instance; };
void shuffle(std::vector<CandidateRef> &, uint32_t seed);
struct Catalog {
    struct Instance { size_t group, ordinal; bool placed = false; };
    struct Record { size_t cursor = 0, module = 0; std::vector<size_t> instances; int32_t nestedCount = 0; };
    std::vector<Instance> instances;
    std::vector<Record> records;
    std::vector<std::vector<size_t>> groups;
    Catalog(const std::vector<std::vector<size_t>> &modules, const std::vector<int> &quantities);
    using Eligible = std::function<bool(size_t instance, size_t module)>;
    void append(size_t record, std::vector<CandidateRef> &, const Eligible & = {});
    std::vector<CandidateRef> available(const std::vector<size_t> &keys, uint32_t seed, const Eligible & = {});
    void setPlaced(size_t instance, bool placed);
    // Value-owned copies include cursors. Bt rollback deliberately does NOT copy
    // this whole object: unplacing restores counts but preserves search cursors.
};
}
