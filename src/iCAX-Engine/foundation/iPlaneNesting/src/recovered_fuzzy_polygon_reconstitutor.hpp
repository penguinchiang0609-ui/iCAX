#pragma once
#include "recovered_fuzzy_polygon_build.hpp"
#include <set>
namespace study::recovered {
struct FuzzyWeightMinimum {double value=0.;int32_t position=-1;};
class FuzzyWeightRange {
    std::vector<std::vector<double>> minima_,lazy_;
    std::vector<std::vector<int32_t>> positions_;
    std::set<int32_t> zeros_;
    FuzzyWeightMinimum minimum(int32_t,int32_t,int32_t)const;
    void add(int32_t,int32_t,double,int32_t,double);
    void refresh(int32_t,int32_t);
    void zeroScan(int32_t,int32_t,int32_t);
public:
    explicit FuzzyWeightRange(std::span<const double> values={}); //84a360
    FuzzyWeightMinimum minimum(int32_t begin,int32_t end)const; //849f70
    void add(int32_t begin,int32_t end,double delta); //84b0c0
    int32_t rangeEnd(int32_t begin)const; //845ed0
    size_t size()const;
    bool empty()const; //846db0 per-path count
    const std::vector<std::vector<double>> &minima()const{return minima_;}
    const std::vector<std::vector<double>> &lazy()const{return lazy_;}
    const std::vector<std::vector<int32_t>> &positions()const{return positions_;}
    const std::set<int32_t> &zeros()const{return zeros_;}
};
class FuzzyReconstitutor {
    struct State;
    std::unique_ptr<State> state_;
public:
    explicit FuzzyReconstitutor(std::span<const FuzzyWeightedSegment>); //83dc90
    ~FuzzyReconstitutor();
    FuzzyReconstitutor(FuzzyReconstitutor &&) noexcept;
    FuzzyReconstitutor &operator=(FuzzyReconstitutor &&) noexcept;
    const std::vector<std::shared_ptr<const std::vector<ConvolutionIntegerSegment>>> &paths()const;
    const std::map<EscherIntegerPoint,std::vector<EscherIntegerPoint>> &adjacency()const;
    FuzzyGeometryRange registerPath(std::span<const ConvolutionIntegerSegment>); //83fee0/842570: copies into a new owner
    size_t pendingCount()const;
    std::optional<FuzzyTopologySegmentIndex> lookup(const ConvolutionIntegerSegment &)const; //842a90
    int32_t rangeEnd(int32_t path,int32_t begin)const; //845ed0
    double weight(int32_t path,int32_t begin,int32_t end)const;
    bool hasHot(EscherIntegerPoint); //83e650
    const std::vector<EscherIntegerPoint> &neighbors(EscherIntegerPoint); //840ed0
    ConvolutionIntegerSegment clockwiseHot(const ConvolutionIntegerSegment &); //83e9c0
    ConvolutionIntegerSegment nextSeed(); //840570, includes840c00 lazy path creation
    double withdraw(std::span<const FuzzyGeometryRange>); //83f990
    bool empty()const; //846db0
};
}
