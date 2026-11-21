#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace study::recovered {
// 6a1bb0/6a1c80 consume the result's serialized sister-ID lists, rather
// than reconstructing groups or sorting their coordinates at query time.
int32_t multiheadsLinkedCount(std::span<const std::vector<uintptr_t>>,uintptr_t part);
// Unlinked index zero returns the input part. Invalid indexes return -973
// without changing the destination. The first containing group wins.
int32_t multiheadsLinkedPart(std::span<const std::vector<uintptr_t>>,uintptr_t part,
    int32_t index,uintptr_t &linkedPart);
}
