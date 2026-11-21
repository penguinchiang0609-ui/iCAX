#include "shaker_runtime.hpp"
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace study::recovered;
// A normalized fixed-angle scene, independent of the still-incomplete SDK
// geometry/technology adapter. All four board parts are created by the
// recovered input producer; objectives and windows use the recovered config.
int main(){try{
    ShakerInputScene scene;shakerConfigureOrdinaryRelations(scene.relations);
    scene.shapes={{{},{{-1,{{0,0},{8,0},{8,3},{0,3}},false,0}}},{{},{{-1,{{0,0},{4,0},{4,1},{1,1},{1,5},{0,5}},false,0}}}};
    for(size_t n=0;n<8;++n){const auto i=shakerCreatePart(scene,n%2,{4+10*double(n%4),4+10*double(n/4)},0);shakerSetMobility(scene.parts[i],{0,0,1},scene.parts[i].bounds,scene.parts[i].angleBounds);shakerSetPartLabel(scene,i,int32_t(n));}
    const auto before=scene.parts;shakerCreateBoundaryGeometry(scene,100,100,10);
    if(shakerSetBounds(scene,0,45,0,25))throw std::runtime_error("Cannot create board boundaries");
    const auto result=compactShakerFixedScene(scene,0,2,10);if(result.status||!result.iteration.success)throw std::runtime_error("Compaction did not produce an accepted scene");
    const auto placements=[&](const auto &parts){std::cout<<'[';for(size_t n=0;n<8;++n){if(n)std::cout<<',';std::cout<<"{\"shape\":"<<n%2<<",\"x\":"<<parts[n].position[0]<<",\"y\":"<<parts[n].position[1]<<'}';}std::cout<<']';};
    std::cout<<std::setprecision(17)<<"{\"pass\":true,\"scope\":\"normalized fixed-angle recovered scene\",\"width\":45,\"height\":25,\"shapes\":[";
    for(size_t s=0;s<2;++s){if(s)std::cout<<',';std::cout<<'[';const auto &polygon=scene.shapes[s].components[0].points;for(size_t n=0;n<polygon.size();++n){if(n)std::cout<<',';std::cout<<'['<<polygon[n][0]<<','<<polygon[n][1]<<']';}std::cout<<']';}
    std::cout<<"],\"before\":";placements(before);std::cout<<",\"after\":";placements(scene.parts);std::cout<<",\"solves\":"<<result.solves<<",\"pairs\":"<<result.pairs<<"}\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
