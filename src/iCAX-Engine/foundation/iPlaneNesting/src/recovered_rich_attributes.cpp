#include "recovered_rich_attributes.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <typeinfo>

namespace study::recovered {
std::shared_ptr<const RichAttribute> RichAttributeCopies::copy(
    const std::shared_ptr<const RichAttribute> &source) {
    if (!source)
        throw std::invalid_argument("Rich attribute copying requires a live attribute");
    if (reuseSource_)
        return source;
    if (!source->sharedClone())
        return source->clone();
    const auto found = copies_.find(source);
    if (found != copies_.end())
        return found->second;
    return copies_.emplace(source, source->clone()).first->second;
}

void richCopyAttributes(RichAttributes &destination,
                        std::span<const std::shared_ptr<const RichAttribute>> source,
                        RichAttributeCopies &copies) {
    for (const auto &attribute : source) {
        auto cloned = copies.copy(attribute);
        if (!cloned)
            continue;
        const auto sameType = [&](const auto &existing) {
            return typeid(*existing) == typeid(*cloned);
        };
        if (std::none_of(destination.begin(), destination.end(), sameType))
            destination.push_back(std::move(cloned));
    }
}

int32_t RichSourceAttributes::insert(const RichAttributes &attributes) {
    if (attributes.empty())
        return 0;
    const auto key = std::bit_cast<int32_t>(++sequence);
    entries[key] = attributes;
    return key;
}
}
