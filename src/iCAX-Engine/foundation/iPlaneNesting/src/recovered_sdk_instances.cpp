#include "recovered_sdk_instances.hpp"
#include <limits>
#include <stdexcept>

namespace study::recovered {
SdkInstanceOrder sdkInstanceOrder(std::span<const SdkInstanceSource> records,
                                  std::span<const uintptr_t> parts){
    SdkInstanceOrder result;
    for(const auto part:parts)for(const auto &record:records)if(record.nativePart==part){
        if(result.identities.size()>size_t(INT32_MAX))throw std::length_error("SDK native instance indices");
        result.lastIndex[record.identity]=int32_t(result.identities.size());
        result.identities.push_back(record.identity);result.nativeParts.push_back(part);
    }
    return result;
}
std::map<uintptr_t,uintptr_t> sdkInstanceBindings(const SdkInstanceOrder &order,
                                                std::span<const uintptr_t> instances){
    if(instances.size()!=order.identities.size())throw std::invalid_argument("SDK native instance queue size");
    std::map<uintptr_t,uintptr_t> result;
    for(const auto &[id,index]:order.lastIndex){
        if(index<0||size_t(index)>=instances.size())throw std::out_of_range("SDK native instance mapping");
        result[id]=instances[size_t(index)];
    }
    return result;
}
std::vector<size_t> sdkResultOccurrenceSources(const SdkInstanceOrder &order){
    std::vector<size_t> result;result.reserve(order.identities.size());
    for(const auto id:order.identities){const auto index=order.lastIndex.at(id);
        if(index<0||size_t(index)>=order.identities.size())throw std::out_of_range("SDK result occurrence mapping");
        result.push_back(size_t(index));
    }
    return result;
}
}
