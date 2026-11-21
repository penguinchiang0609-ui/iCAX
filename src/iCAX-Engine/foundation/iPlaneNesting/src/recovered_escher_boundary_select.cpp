#include "recovered_escher_boundary_select.hpp"
#include <bit>
namespace study::recovered {
namespace {int64_t subtract(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}}
EscherBoundarySampleSelection escherBoundaryBooleanSelect(const ConvolutionIntegerPolygon &rings,std::span<const double> suppliedWeights,
    int32_t retries,const EscherFuzzySelectionProvider &provider,const EscherBoundaryOffsetProvider &offset,
    const ConvolutionHierarchyDiagnostic &diagnostic){
    require(bool(provider),PN2S_UNSUPPORTED,"Escher boundary selection requires the fuzzy polygon provider");
    std::vector<double> weights(suppliedWeights.begin(),suppliedWeights.end());if(weights.empty())weights.assign(rings.size(),1.);
    require(weights.size()==rings.size(),PN2S_INVALID,"Escher fuzzy selection requires one weight per ring");
    auto selected=provider(rings,weights,{-1,0},{0,-1});if(selected.success)return std::move(selected.selection);
    if(retries<0){if(diagnostic)diagnostic("could not properly calculate the best point of  a fuzzy polygon");return std::move(selected.selection);}
    require(!rings.empty()&&!rings.front().empty(),PN2S_INVALID,"Escher fuzzy retry requires nonempty contours");
    auto lower=rings.front().front(),upper=lower;for(const auto &ring:rings)for(auto point:ring)for(size_t axis=0;axis<2;++axis){lower[axis]=std::min(lower[axis],point[axis]);upper[axis]=std::max(upper[axis],point[axis]);}
    const double x=double(subtract(upper[0],lower[0])),y=double(subtract(upper[1],lower[1]));
    const auto radius=std::min<int64_t>(10,int64_t(std::sqrt(y*y+x*x)*0x1p-20));
    ConvolutionIntegerPolygon expanded;std::vector<double> expandedWeights;std::vector<size_t> parents;
    for(size_t index=0;index<rings.size();++index){const auto winding=convolutionIntegerOrientation(rings[index]);require(winding==1,PN2S_INVALID,"Escher fuzzy retry requires positive source ring orientation");
        auto components=offset?offset(rings[index],radius):convolutionIntegerOffset({rings[index]},radius,3,diagnostic).rings;
        for(auto &component:components){const auto orientation=convolutionIntegerOrientation(component);const auto sign=orientation==1?1.:-1.;if(orientation!=winding)std::reverse(component.begin(),component.end());
            expanded.push_back(std::move(component));parents.push_back(index);expandedWeights.push_back(sign*weights[index]);}}
    auto result=escherBoundaryBooleanSelect(expanded,expandedWeights,retries-1,provider,offset,diagnostic);
    std::vector<uint32_t> occurrences(rings.size());for(const auto index:result.indices){require(index>=0&&size_t(index)<parents.size(),PN2S_INVALID,"Escher fuzzy selected ring index is out of range");++occurrences[parents[size_t(index)]];}
    result.indices.clear();for(size_t index=0;index<occurrences.size();++index){if(occurrences[index]==1)result.indices.push_back(int32_t(index));else if(occurrences[index]>1&&diagnostic)diagnostic("polygons have bad pertinence value");}return result;
}
}
