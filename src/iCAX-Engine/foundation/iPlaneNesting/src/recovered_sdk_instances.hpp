#pragma once
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace study::recovered {
struct SdkInstanceSource {uintptr_t identity=0,nativePart=0;};
struct SdkInstanceOrder {
    std::vector<uintptr_t> identities,nativeParts;
    std::map<uintptr_t,int32_t> lastIndex;
};
// Complete 559e10's source-record/native-part matching and ID overwrite order.
// Distinct input occurrences remain distinct native instances, even if the
// caller repeats a concrete SDK ID. The direct ID lookup then selects the last.
SdkInstanceOrder sdkInstanceOrder(std::span<const SdkInstanceSource>,
    std::span<const uintptr_t> nativePartOrder);
// 5d0190's concrete mapping, given the real queue's native instances in that
// order. The output is the SDK-ID-indexed lookup used by predefined constraints.
std::map<uintptr_t,uintptr_t> sdkInstanceBindings(const SdkInstanceOrder &,
    std::span<const uintptr_t> nativeInstances);
// 596be0's result loop keeps each occurrence but obtains its pose from the
// final SDK-ID lookup. Repeated IDs therefore alias the last placement.
std::vector<size_t> sdkResultOccurrenceSources(const SdkInstanceOrder &);
}
