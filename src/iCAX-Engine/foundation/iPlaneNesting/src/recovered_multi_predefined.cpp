#include "recovered_multi_predefined.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace study::recovered {
std::vector<MultiPredefinedSlice> multiPredefinedSlices(std::span<const uintptr_t> sheets,
                                                      std::span<const int32_t> counts){
    if(sheets.size()!=counts.size())throw std::invalid_argument("multi predefined sheet/count size");
    std::vector<MultiPredefinedSlice> output;output.reserve(sheets.size());int32_t first=0;
    for(size_t n=0;n<sheets.size();++n){
        const auto at=int64_t(first);output.push_back({sheets[n],counts[n],first,at*8,at*24,at*16});
        first=std::bit_cast<int32_t>(uint32_t(first)+uint32_t(counts[n]));
    }
    return output;
}
std::vector<uintptr_t> multiPredefinedUniqueDemand(std::span<const uintptr_t> records){
    std::vector<uintptr_t> output(records.begin(),records.end());std::sort(output.begin(),output.end());
    output.erase(std::unique(output.begin(),output.end()),output.end());return output;
}
bool runMultiPredefinedController(const MultiPredefinedControllerInput &input,MultiPredefinedControllerHost &host){
    if(input.sheets.empty())return false;
    if(!host.buildGlobal||!host.begin||!host.submit||!host.finish)throw std::invalid_argument("multi predefined global controller providers");
    const std::vector<int32_t> quantities(input.sheets.size(),1);
    const auto parts=multiPredefinedUniqueDemand(input.parts);std::vector<uintptr_t> flattened;
    for(const auto &group:input.groups)flattened.insert(flattened.end(),group.begin(),group.end());
    host.buildGlobal(input.sheets,quantities,parts,flattened);host.begin(1.,1,false);
    for(size_t group=0;group<input.groups.size();++group)host.submit(input.groups[group],group);
    host.finish();return true;
}
}
