#include "recovered_multiheads_query.hpp"
#include <algorithm>
#include <bit>

namespace study::recovered {
namespace {
const std::vector<uintptr_t> *findGroup(std::span<const std::vector<uintptr_t>> groups,uintptr_t part) {
    for(const auto &group:groups)if(std::find(group.begin(),group.end(),part)!=group.end())return &group;
    return nullptr;
}
}
int32_t multiheadsLinkedCount(std::span<const std::vector<uintptr_t>> groups,uintptr_t part) {
    const auto group=findGroup(groups,part);
    const auto count=group?std::bit_cast<int32_t>(uint32_t(group->size())):0;
    return count>1?count:1;
}
int32_t multiheadsLinkedPart(std::span<const std::vector<uintptr_t>> groups,uintptr_t part,
    int32_t index,uintptr_t &linkedPart) {
    const auto group=findGroup(groups,part);
    if(!group) {if(index!=0)return -973;linkedPart=part;return 0;}
    if(index<0||size_t(index)>=group->size())return -973;
    linkedPart=(*group)[size_t(index)];return 0;
}
}
