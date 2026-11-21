#pragma once
#include "recovered_shaker.hpp"
#include <memory>
namespace study::recovered {
using ShakerBox=std::array<double,4>; // xmin,ymin,xmax,ymax
struct ShakerBroadItem {ShakerPoint first{},second{};uintptr_t identity=0;};
struct ShakerBroadEntry {ShakerBox box{};size_t index=0;};
struct ShakerBroadSplit {
    std::array<std::vector<ShakerBroadEntry>,2> groups;
    std::array<ShakerBox,2> boxes;
};
// Original linear R-tree split, 17 entries, minimum occupancy four.
ShakerBroadSplit shakerBroadSplit(std::span<const ShakerBroadEntry>);
struct ShakerBroadNode {
    bool leaf=true;
    std::vector<ShakerBroadEntry> entries;
    std::vector<std::unique_ptr<ShakerBroadNode>> children;
};
struct ShakerBroadTree {
    std::unique_ptr<ShakerBroadNode> root;
    size_t height=0;
    void insert(ShakerBroadEntry);
    std::vector<size_t> query(const ShakerBox &) const;
};
// 72bfd0: flattened scene order, query expanded bounds before inserting the
// unexpanded item, with original depth-first R-tree traversal/pair ordering.
std::vector<std::array<size_t,2>> shakerBroadphase(std::span<const ShakerBroadItem>,double radius);
}
