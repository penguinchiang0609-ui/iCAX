#pragma once
#include "recovered_fuzzy_polygon_reconstitutor.hpp"
namespace study::recovered {
class FuzzyPathIterator {
    struct Entry {
        std::shared_ptr<std::vector<ConvolutionIntegerSegment>> fresh;
        std::shared_ptr<const std::vector<ConvolutionIntegerSegment>> existing;
        int32_t begin=-1,end=-1;
    };
    FuzzyReconstitutor &state_;
    std::vector<Entry> stack_;
    std::map<EscherIntegerPoint,std::pair<size_t,int32_t>> points_;
    std::map<ConvolutionIntegerSegment,size_t> ends_;
    FuzzyGeometryRange view(size_t); //84b750
    void restoreFreshEnd();
public:
    explicit FuzzyPathIterator(FuzzyReconstitutor &state):state_(state){} //84da20
    bool empty()const; //84b5c0
    ConvolutionIntegerSegment back()const; //84d7f0
    std::optional<FuzzyRegionGeometry> append(const ConvolutionIntegerSegment &); //84bd30
    std::optional<FuzzyRegionGeometry> appendRange(
        std::shared_ptr<const std::vector<ConvolutionIntegerSegment>>,int32_t begin,int32_t end); //84cb10
};
}
