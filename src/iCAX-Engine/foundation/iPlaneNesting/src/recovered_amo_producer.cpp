#include "recovered_amo_producer.hpp"
#include "recovered_amo_sort.hpp"
namespace study::recovered {
void amoTilingSortPatternResults(std::vector<AmoTilingPatternResult> &items,size_t depth){
    detail::AmoCandidateSort sort(items,[](const auto &a,const auto &b){return a.pattern->u.x*a.pattern->v.y<b.pattern->u.x*b.pattern->v.y;});
    sort.run(depth==SIZE_MAX?items.size():depth);
}
std::vector<AmoTilingPatternResult> amoTilingPrepareOrientationPatterns(AmoTilingJob &job,
    AmoTilingSolveRequest request,int32_t required,const AmoTilingOrientationGeometry &geometry){
    auto descriptors=amoTilingProduceDescriptors(job,request,required);auto patterns=amoTilingConvertDescriptors(descriptors,geometry);
    std::vector<AmoTilingPatternResult> results;results.reserve(patterns.size());
    for(size_t n=0;n<patterns.size();++n)results.push_back({std::make_shared<TilingPattern>(std::move(patterns[n])),descriptors[n]});
    amoTilingSortPatternResults(results);return results;
}
}
