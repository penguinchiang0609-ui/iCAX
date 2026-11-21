#pragma once
#include "recovered_escher_transposed.hpp"
#include "recovered_convolution_polygon.hpp"
#include <map>
#include <mutex>
namespace study::recovered {
struct EscherProviderLengthLimits {
    double target=0;
    int32_t flags30=0,flags34=0;
    bool operator==(const EscherProviderLengthLimits &)const=default;
};
struct EscherProviderRequest {
    int32_t maximumCount=0,discard=0;
    double width=0,height=0;
    const ConvolutionIntegerPolygon *lengthBoundary=nullptr;
    bool singleMode=false,special=false,maximize=false,useCountCache=false;
    EscherProviderLengthLimits lengthLimits;
};
struct EscherProviderLengthResult {
    EscherPatternLayout layout;
    int32_t attempts=0;
    bool operator==(const EscherProviderLengthResult &)const=default;
};
class EscherProviderHost {
public:
    virtual ~EscherProviderHost()=default;
    virtual EscherProviderLengthResult length(const ConvolutionIntegerPolygon &,bool singleMode,bool special,
        const EscherProviderLengthLimits &,int32_t previousAttempts)=0; //788970
    virtual EscherPatternLayout maximum(EscherIntegerBox,bool singleMode,bool special,uint64_t limit)=0; //786ee0
    virtual EscherPatternLayout partial(EscherIntegerBox,bool special,uint64_t limit)=0; //7877c0
    virtual EscherPatternLayout solve(EscherIntegerBox,bool singleMode,bool special,uint64_t required,int64_t &length)=0; //7881a0
};
class EscherMaximumHost {
public:
    virtual ~EscherMaximumHost()=default;
    virtual size_t singleCount()const=0;
    virtual bool singleIsPrimitive(size_t)const=0;
    virtual EscherPatternLayout singleMaximum(size_t,EscherIntegerBox,bool singleMode,uint64_t previousBest,uint64_t limit)=0; //78ec10
    virtual size_t pairCount()const=0;
    virtual bool pairIsPrimitive(size_t)const=0;
    virtual EscherPatternLayout pairLayout(size_t,EscherIntegerBox)=0; //78dd40
};
EscherPatternLayout escherMaximumPattern(EscherMaximumHost &,EscherIntegerBox,bool singleMode,bool primitivesOnly,uint64_t limit); //786ee0
using EscherMaximumPhaseProvider=std::function<EscherPatternLayout(bool sameWidth,EscherIntegerBox,
    bool specialCatalog,EscherIntegerPoint axis,uint64_t previousBest,uint64_t limit)>;
EscherPatternLayout escherSingleMaximumPattern(const EscherEnumerationDescriptor &,EscherIntegerBox,
    bool specialCatalog,uint64_t previousBest,uint64_t limit,std::span<const EscherRecordGeometry>,
    const EscherMaximumPhaseProvider &); //78ec10; phase providers are 7c5f20/7c7840
class EscherProvider {
    double scale_;
    int32_t defaultCount_;
    bool suppressDefaultCount_;
    mutable std::mutex countMutex_,lengthMutex_;
    std::map<std::pair<double,double>,int32_t> countCache_;
    std::map<uint64_t,EscherProviderLengthResult> lengthCache_;
public:
    EscherProvider(double scale,int32_t defaultCount,bool suppressDefaultCount=false)
        :scale_(scale),defaultCount_(defaultCount),suppressDefaultCount_(suppressDefaultCount){}
    EscherPatternLayout prepare(const EscherProviderRequest &,EscherProviderHost &); //493b00
    std::map<std::pair<double,double>,int32_t> countCache()const;
    std::map<uint64_t,EscherProviderLengthResult> lengthCache()const;
};
}
