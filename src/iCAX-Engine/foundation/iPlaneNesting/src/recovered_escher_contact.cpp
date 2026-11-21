#include "recovered_escher_contact.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <list>
#include <map>
#include <set>

namespace study::recovered {
namespace {
int64_t wrapped(uint64_t value) { return std::bit_cast<int64_t>(value); }
int64_t originalRound(double value) {
    value = value < 0. ? value - .5 : value + .5;
    return value >= -0x1p63 && value < 0x1p63 ? int64_t(value) : INT64_MIN;
}
uint64_t mix(uint64_t value) {
    value = (value ^ (value >> 32)) * 0xe9846af9b1a615dULL;
    value = (value ^ (value >> 32)) * 0xe9846af9b1a615dULL;
    return value ^ (value >> 28);
}
uint64_t hash(ConvolutionIntegerPoint point) {
    return mix(mix(uint64_t(point[0]) + 0x9e3779b9) + 0x9e3779b9 + uint64_t(point[1]));
}
}
ConvolutionIntegerRing escherIntegerCornerChamfer(ConvolutionIntegerPoint previous,
    ConvolutionIntegerPoint point, ConvolutionIntegerPoint next) {
    const double ax = double(wrapped(uint64_t(point[0]) - uint64_t(previous[0])));
    const double ay = double(wrapped(uint64_t(point[1]) - uint64_t(previous[1])));
    const double bx = double(wrapped(uint64_t(next[0]) - uint64_t(point[0])));
    const double by = double(wrapped(uint64_t(next[1]) - uint64_t(point[1])));
    const double aLength = std::sqrt(ay * ay + ax * ax);
    const double bLength = std::sqrt(by * by + bx * bx);
    if (aLength < 32. || bLength < 32.) return {};
    const double aFactor = (aLength - 32.) / aLength;
    const double bFactor = (bLength - 32.) / bLength;
    const int64_t incomingX = originalRound(aFactor * ax), incomingY = originalRound(aFactor * ay);
    const int64_t outgoingX = originalRound(bFactor * bx), outgoingY = originalRound(bFactor * by);
    const int64_t midX = originalRound(double(wrapped(uint64_t(previous[0]) - uint64_t(outgoingX)
        + uint64_t(next[0]) + uint64_t(incomingX))) * .5);
    const int64_t midY = originalRound(double(wrapped(uint64_t(incomingY) - uint64_t(outgoingY)
        + uint64_t(previous[1]) + uint64_t(next[1]))) * .5);
    const double normalX = bx / bLength - ax / aLength;
    const double normalY = by / bLength - ay / aLength;
    const double normalLength = std::sqrt(normalY * normalY + normalX * normalX);
    if (normalLength == 0.) return {};
    return {{originalRound((normalX + normalX) / normalLength + double(midX)),
             originalRound((normalY + normalY) / normalLength + double(midY))}};
}
void escherRemoveSharedIntegerPoint(ConvolutionIntegerRing &ring, ConvolutionIntegerPoint point) {
    for (;;) {
        const auto found = std::find(ring.begin(), ring.end(), point);
        if (found == ring.end()) return;
        const size_t index = size_t(found - ring.begin()), count = ring.size();
        auto replacement = escherIntegerCornerChamfer(ring[(index + count - 1) % count], point,
            ring[(index + 1) % count]);
        ring.erase(found);
        ring.insert(ring.begin() + index, replacement.begin(), replacement.end());
    }
}
bool escherIntegerRingHasRepeatedPoint(const ConvolutionIntegerRing &ring) {
    return std::set<ConvolutionIntegerPoint>(ring.begin(), ring.end()).size() != ring.size();
}
bool escherResolveSharedIntegerVertex(std::span<ConvolutionIntegerRing *> rings) {
    struct Node {ConvolutionIntegerPoint point;std::vector<ConvolutionIntegerRing *> rings;};
    std::vector<Node> nodes;
    std::map<ConvolutionIntegerPoint,size_t> indices;
    std::list<size_t> order;
    using Iterator = std::list<size_t>::iterator;
    std::vector<Iterator> buckets(8, order.end());
    const auto rehash = [&](size_t capacity) {
        buckets.assign(capacity, order.end());
        for (auto iterator=order.begin();iterator!=order.end();) {
            const auto next=std::next(iterator);
            auto &first=buckets[hash(nodes[*iterator].point)&(capacity-1)];
            if (first!=order.end()) order.splice(first,order,iterator);
            first=iterator;iterator=next;
        }
    };
    for (auto ring : rings) for (const auto point : *ring) {
        auto [entry,fresh]=indices.try_emplace(point,nodes.size());
        if (fresh) {
            const size_t required=size_t(std::ceil(float(nodes.size()+1)));
            if (buckets.size()<required) {
                size_t desired=std::max<size_t>(8,required);
                if (buckets.size()<512) desired=std::max(desired,buckets.size()*8);
                rehash(std::bit_ceil(desired));
            }
            const size_t index=nodes.size();nodes.push_back({point,{}});
            auto &first=buckets[hash(point)&(buckets.size()-1)];
            first=order.insert(first,index);
        }
        nodes[entry->second].rings.push_back(ring);
    }
    for (const auto index : order) if (nodes[index].rings.size()>1) {
        for (auto ring : nodes[index].rings) escherRemoveSharedIntegerPoint(*ring,nodes[index].point);
        return true;
    }
    return false;
}
}
