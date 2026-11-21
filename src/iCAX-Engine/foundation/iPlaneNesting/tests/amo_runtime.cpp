#include "recovered_amo_solver.hpp"
#include "recovered_amo_geometry.hpp"
#include "recovered_amo_orientation.hpp"
#include <iostream>
using namespace study;using namespace study::recovered;
int main(){try{
    AmoTilingJob job;for(auto &shape:job.members){shape.rings={{{0,0},{5,0},{5,3},{0,3}}};shape.bounds=amoTilingShapeBounds(shape);}
    auto geometry=amoTilingFloatingMapGeometry();AmoTilingMapCache cache;
    auto map=amoTilingBuildContourMap(cache,{&job.members[0]},{&job.members[1]},0.,geometry);
    require(map->usable&&!map->simplified.empty()&&!map->detailed.empty(),PN2S_INTERNAL,"Real floating rectangle map must contain usable boundaries");
    AmoTilingSolveRequest request;request.angles={0,0,0,false};request.criterion={20,10,0,0,0};
    auto descriptors=amoTilingProduceDescriptors(job,request,12,geometry);
    require(!descriptors.empty(),PN2S_INTERNAL,"Real floating Amo solver must return rectangle descriptors");
    AmoTilingOrientationGeometry conversion;conversion.part=1;conversion.originalBounds={0,0,5,3};conversion.required=12;
    conversion.variantBounds=[](PlacementOrientation orientation){require(orientation.angle==0&&orientation.field4==0&&orientation.field8==0,PN2S_INTERNAL,"Axis rectangle test retains its orientation");return Box{0,0,5,3};};
    auto patterns=amoTilingConvertDescriptors(descriptors,conversion);std::vector<TilingRepetitionCounts> repetitions;
    for(const auto &descriptor:descriptors){TilingRepetitionCounts counts;counts.commonX=descriptor.repeatX;counts.commonY=descriptor.repeatY;for(size_t n=0;n<2;++n){counts.extraX[n]=descriptor.extra[n][0];counts.extraY[n]=descriptor.extra[n][1];}repetitions.push_back(counts);}
    const auto candidate=selectGeneralTilingRepetition(patterns,repetitions);
    const auto expanded=expandTilingCandidate(candidate);require(!expanded.members.empty(),PN2S_INTERNAL,"Amo rectangle descriptor must expand to placed members");
    const auto box=tilingCandidateBounds(candidate);
    require(box.x1-box.x0<=20.+1e-7&&box.y1-box.y0<=10.+1e-7,PN2S_INTERNAL,"Real Amo rectangle pattern stays inside requested dimensions");
    for(size_t i=0;i<expanded.members.size();++i){const auto a=expanded.members[i].position;for(size_t j=0;j<i;++j){const auto b=expanded.members[j].position;
        require(std::min(a.x+5,b.x+5)-std::max(a.x,b.x)<=1e-7||std::min(a.y+3,b.y+3)-std::max(a.y,b.y)<=1e-7,PN2S_INTERNAL,"Real Amo rectangle placements have no interior overlap");}}
    require(expanded.members.size()==12,PN2S_INTERNAL,"20x10 sheet fits the expected twelve 5x3 rectangles");
    std::cout<<"{\"pass\":true,\"simplifiedRings\":"<<map->simplified.size()<<",\"detailedRings\":"<<map->detailed.size()<<",\"descriptors\":"<<descriptors.size()<<",\"placements\":"<<expanded.members.size()<<",\"width\":"<<box.x1-box.x0<<",\"height\":"<<box.y1-box.y0<<"}\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
