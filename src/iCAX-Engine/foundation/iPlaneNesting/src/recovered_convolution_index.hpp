#pragma once
#include "recovered_convolution_hierarchy.hpp"
namespace study::recovered {
using ConvolutionIntegerBox=std::array<int64_t,4>;
struct ConvolutionIndexEntry {ConvolutionIntegerBox box{};ConvolutionIntegerSegment edge{};size_t index=0;};
struct ConvolutionIndexSplit {
    std::array<std::vector<ConvolutionIndexEntry>,2> groups;
    std::array<ConvolutionIntegerBox,2> boxes;
};
// 822b60/8230f0: nine-entry linear split, minimum occupancy two.
ConvolutionIndexSplit convolutionIndexSplit(std::span<const ConvolutionIndexEntry>);
struct ConvolutionIndexNode {
    bool leaf=true;
    std::vector<ConvolutionIndexEntry> entries;
    std::vector<std::unique_ptr<ConvolutionIndexNode>> children;
};
struct ConvolutionEdgeIndex {
    std::unique_ptr<ConvolutionIndexNode> root;
    size_t height=0,count=0;
    void insert(ConvolutionIntegerSegment); // 81f440/820300
    void build(const ConvolutionIntegerPolygon &); // 81e9c0
    std::vector<ConvolutionIntegerSegment> query(const ConvolutionIntegerSegment &) const; // 8200f0
};
}
