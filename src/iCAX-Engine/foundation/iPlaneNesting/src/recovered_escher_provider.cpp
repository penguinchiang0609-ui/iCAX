#include "recovered_escher_provider.hpp"
#include <bit>
namespace study::recovered {
namespace {
int32_t subtract(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));}
int32_t count(const EscherPatternLayout &layout){return std::bit_cast<int32_t>(uint32_t(layout.primary.size()));}
int64_t integerExtent(double value){
    const double shifted=value<0?value-.5:value+.5;
    return shifted>=-0x1p63 && shifted<0x1p63?int64_t(shifted):INT64_MIN;
}
int64_t negative(int64_t value){return std::bit_cast<int64_t>(uint64_t(0)-uint64_t(value));}
std::vector<EscherRecordGeometry> recordGeometries(std::span<const EscherPatternRecord> records,
    std::span<const EscherPatternRecord> source,std::span<const EscherRecordGeometry> geometries){
    std::vector<EscherRecordGeometry> result;result.reserve(records.size());
    for(const auto &record:records){const auto found=std::find_if(source.begin(),source.end(),[&](const auto &part){return part.geometry==record.geometry;});
        require(found!=source.end(),PN2S_INVALID,"Escher maximum record has no prepared geometry");result.push_back(geometries[size_t(found-source.begin())]);}
    return result;
}
}
EscherPatternLayout escherSingleMaximumPattern(const EscherEnumerationDescriptor &descriptor,EscherIntegerBox rectangle,
    bool specialCatalog,uint64_t previousBest,uint64_t limit,std::span<const EscherRecordGeometry> geometries,
    const EscherMaximumPhaseProvider &phase){
    require(descriptor.records.size()==geometries.size(),PN2S_INVALID,"Escher maximum descriptor geometry count");
    EscherPatternLayout result;
    for(const bool transposed:descriptor.orientationChoices){
        auto trial=phase(descriptor.sameWidth,transposed?escherTransposeRectangle(rectangle):rectangle,
            specialCatalog,transposed?EscherIntegerPoint{0,-1}:EscherIntegerPoint{1,0},previousBest,limit);
        if(trial.primary.size()>result.primary.size()){
            result=std::move(trial);previousBest=result.primary.size();
            if(transposed){
                const auto primary=recordGeometries(result.primary,descriptor.records,geometries),secondary=recordGeometries(result.secondary,descriptor.records,geometries);
                result.primary=escherRotateRecords(result.primary,2700000,primary);result.secondary=escherRotateRecords(result.secondary,2700000,secondary);
                result.basis={result.basis[1],negative(result.basis[0]),result.basis[3],negative(result.basis[2])};result.bounds=escherLayoutBounds(result.primary);
            }
        }
        if(result.primary.size()>=limit)break;
    }
    if(descriptor.restrictionDegrees!=0){
        const auto secondary=recordGeometries(result.secondary,descriptor.records,geometries);
        bool accepted=escherLayoutRotationAccept(result.secondary,descriptor.restrictionDegrees,secondary);
        if(accepted){const auto primary=recordGeometries(result.primary,descriptor.records,geometries);accepted=escherLayoutRotationAccept(result.primary,descriptor.restrictionDegrees,primary);}
        if(!accepted){result.primary.clear();result.secondary.clear();result.basis={};}
    }
    return result;
}
EscherPatternLayout escherMaximumPattern(EscherMaximumHost &host,EscherIntegerBox rectangle,bool singleMode,bool primitivesOnly,uint64_t limit){
    EscherPatternLayout result;
    for(size_t index=0;index<host.singleCount();++index){
        if(primitivesOnly&&!host.singleIsPrimitive(index))continue;
        auto trial=host.singleMaximum(index,rectangle,singleMode,result.primary.size(),limit);
        if(trial.primary.size()>result.primary.size())std::swap(result,trial);
        if(result.primary.size()>=limit)return result;
    }
    if(!singleMode)for(size_t index=0;index<host.pairCount();++index){
        if(primitivesOnly&&!host.pairIsPrimitive(index))continue;
        auto trial=host.pairLayout(index,rectangle);
        if(trial.primary.size()>result.primary.size() && trial.primary.size()<=limit)std::swap(result,trial);
    }
    return result;
}
EscherPatternLayout EscherProvider::prepare(const EscherProviderRequest &request,EscherProviderHost &host){
    const int32_t defaultCount=suppressDefaultCount_?0:defaultCount_;
    const int32_t maximum=request.maximumCount==0?defaultCount:request.maximumCount;
    if(request.width==0. && request.lengthBoundary){
        const auto boundaryKey=reinterpret_cast<uintptr_t>(request.lengthBoundary);
        EscherProviderLengthResult previous;
        {
            const std::scoped_lock lock(lengthMutex_);
            previous=lengthCache_[boundaryKey];
        }
        auto next=host.length(*request.lengthBoundary,request.singleMode,request.special,
            request.lengthLimits,previous.attempts);
        auto result=previous.layout.primary.size()>next.layout.primary.size()?std::move(previous.layout):std::move(next.layout);
        {
            const std::scoped_lock lock(lengthMutex_);
            lengthCache_[boundaryKey]={result,std::bit_cast<int32_t>(uint32_t(previous.attempts)+uint32_t(next.attempts))};
        }
        return result;
    }
    const double width=scale_*request.width,height=scale_*request.height;
    const EscherIntegerBox rectangle{0,0,integerExtent(width),integerExtent(height)};
    const bool cached=request.useCountCache&&!request.singleMode&&!request.special;
    int32_t required=maximum;
    if(request.maximize)required=count(host.partial(rectangle,request.special,UINT64_MAX));
    else {
        const std::scoped_lock lock(countMutex_);
        const std::pair<double,double> key{width,height};
        const auto found=cached?countCache_.find(key):countCache_.end();
        if(found!=countCache_.end())required=subtract(std::min(maximum,found->second),request.discard);
        else {
            const int32_t available=count(host.maximum(rectangle,request.singleMode,request.special,
                uint64_t(int64_t(maximum))));
            if(cached && maximum==defaultCount)countCache_.emplace(key,available);
            const int32_t capped=std::min(maximum,available);
            const int32_t reduced=subtract(capped,request.discard);
            required=reduced<1?capped:reduced;
        }
    }
    if(required<1)return {};
    int64_t length=0;
    return host.solve(rectangle,request.singleMode,request.special,uint64_t(required),length);
}
std::map<std::pair<double,double>,int32_t> EscherProvider::countCache()const{
    const std::scoped_lock lock(countMutex_);return countCache_;
}
std::map<uint64_t,EscherProviderLengthResult> EscherProvider::lengthCache()const{
    const std::scoped_lock lock(lengthMutex_);return lengthCache_;
}
}
