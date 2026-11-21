#include "recovered_amo_map.hpp"
namespace study::recovered {
namespace {
std::array<double,4> rotation(double angle){const double radians=(angle*pi)/180.,cosine=std::cos(radians),sine=std::sin(radians),negativeSine=-sine;const double norm=std::sqrt(negativeSine*negativeSine+cosine*cosine);return {cosine,norm!=0?negativeSine/norm:1.,sine,norm!=0?cosine/norm:0.};}
}
void amoTilingRotateContourMap(AmoTilingContourMap &map,double angle){
    if(map.prepared&&!(std::fabs(angle-map.angle)>1e-12))return;
    map.angle=angle;map.prepared=true;const auto matrix=rotation(angle);
    const auto rotate=[&](const AmoTilingContours &source){auto output=source;for(auto &ring:output)for(auto &point:ring)point={point.y*matrix[1]+point.x*matrix[0]+0.,point.y*matrix[3]+point.x*matrix[2]+0.};return output;};
    map.rotatedSimplified=rotate(map.simplified);map.rotatedDetailed=rotate(map.detailed);map.cachedPeriods={};map.cachedIndices={};
}
std::shared_ptr<AmoTilingContourMap> amoTilingBuildContourMap(AmoTilingMapCache &cache,const AmoTilingMapSource &first,
    const AmoTilingMapSource &second,double angle,const AmoTilingMapGeometry &geometry){
    require(first.shape&&second.shape,PN2S_INVALID,"Amo map operands require live shape identities");const auto key=std::pair{first.shape,second.shape};auto found=cache.entries.find(key);
    if(found==cache.entries.end()){
        auto map=std::make_shared<AmoTilingContourMap>();map->first=first;map->second=second;
        if(!first.flag59&&!first.flag5a&&!second.flag59&&!second.flag5a){
            require(bool(geometry.simplified)&&bool(geometry.detailed),PN2S_INVALID,"Amo map construction requires the actual floating convolution geometry producers");geometry.simplified(*map);geometry.detailed(*map);
            size_t simplified=0,detailed=0;for(const auto &ring:map->simplified)simplified+=ring.size();for(const auto &ring:map->detailed)detailed+=ring.size();if(simplified*5<detailed)map->detailed=map->simplified;map->prepared=false;
        }
        found=cache.entries.emplace(key,std::move(map)).first;
    }
    amoTilingRotateContourMap(*found->second,angle);return found->second;
}
AmoTilingCandidateFamilies amoTilingPrepareFamilies(AmoTilingFamilyPreparation &preparation,const AmoTilingMapSource &first,
    const AmoTilingMapSource &second,double angle,const AmoTilingMapGeometry &geometry){
    require(preparation.maps!=nullptr,PN2S_INVALID,"Amo family preparation requires its shared MapSet");
    bool usable=amoTilingBuildContourMap(*preparation.maps,first,first,angle,geometry)->usable;
    if(usable)usable=amoTilingBuildContourMap(*preparation.maps,second,second,angle,geometry)->usable;
    if(usable)usable=amoTilingBuildContourMap(*preparation.maps,second,first,angle,geometry)->usable;
    if(usable)usable=amoTilingBuildContourMap(*preparation.maps,first,second,angle,geometry)->usable;
    const auto map=amoTilingBuildContourMap(*preparation.maps,first,second,angle,geometry);AmoTilingCandidateFamilies output;if(!usable)return output;
    if(preparation.fixedTranslation){const auto matrix=rotation(angle);auto candidate=std::make_shared<AmoTilingCandidate>();candidate->translation={preparation.translation.y*matrix[1]+preparation.translation.x*matrix[0],preparation.translation.x*matrix[2]+preparation.translation.y*matrix[3]};candidate->angle=angle;output.push_back({std::move(candidate)});}
    else for(const auto &ring:map->rotatedSimplified){output.emplace_back();for(const auto point:ring){auto candidate=std::make_shared<AmoTilingCandidate>();candidate->translation=point;candidate->angle=angle;output.back().push_back(std::move(candidate));}}
    return output;
}
}
