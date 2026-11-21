#include "ordinary_pair_map.hpp"
#include "recovered_amo_convolution.hpp"
#include "recovered_amo_geometry.hpp"
#include "recovered_convolution_polygon.hpp"
#include "recovered_part_normalization.hpp"
#include <bit>
namespace study {
std::vector<Polygon> ordinaryPreparedContours(const PartInput &part,Pose pose){std::vector<Polygon> rings{transform(part.polygon,part.pivot,pose)};for(const auto &hole:part.holes){auto ring=transform(hole,part.pivot,pose);if(area(ring)>0.)std::reverse(ring.begin(),ring.end());rings.push_back(std::move(ring));}return recovered::partNormalizeCleanContours(rings);}
bool ordinaryPreparedDomain(const Input &input){if(input.gap!=0.)return false;try{for(const auto &part:input.parts){if(part.protection!=0.||part.poses.empty()||part.polygon.size()<3)return false;for(const auto pose:part.poses){if(pose.upper!=pose.angle)return false;const auto rings=ordinaryPreparedContours(part,pose);if(rings.empty()||rings.front().size()<3)return false;for(const auto &ring:rings)for(const auto point:ring)if(!grid_exact(point.x,1e6)||!grid_exact(point.y,1000.))return false;}}}catch(const Failure &){return false;}catch(const std::invalid_argument &){return false;}return true;}
namespace {
Row intersectPairRows(const Row &a,const Row &b){Row result;size_t first=0,second=0;while(first<a.size()&&second<b.size()){const auto low=std::max(a[first][0],b[second][0]),high=std::min(a[first][1],b[second][1]);if(low<=high)result.push_back({low,high});if(a[first][1]<b[second][1])++first;else ++second;}return result;}
int64_t floorPairDivide(int64_t value,int64_t pitch){auto result=value/pitch;if(value<0&&value%pitch)--result;return result;}
}
std::vector<Polygon> ordinaryPairContours(std::span<const Polygon> first,std::span<const Polygon> second,bool opposite,Budget &budget,OrdinaryPairGeometryTrace *trace){
    recovered::ConvolutionCurves curves;std::array<std::vector<recovered::ConvolutionTopology>,2> sources;
    const recovered::AmoTilingMatrix direct{1,0,0,1,0,0,1},reflected{1,0,0,1,0,0,-1};
    for(const auto &ring:first){budget.check();sources[0].push_back(recovered::amoTilingBooleanRing(ring,direct,curves));}
    for(const auto &ring:second){budget.check();sources[1].push_back(recovered::amoTilingBooleanRing(ring,opposite?reflected:direct,curves));}
    std::vector<Polygon> result;
    for(const auto &a:sources[0]){const double areaA=recovered::booleanStraightPathArea(a,a.firstEdge,a.lastEdge);
        for(const auto &b:sources[1]){budget.check();const double areaB=recovered::booleanStraightPathArea(b,b.firstEdge,b.lastEdge);if(trace)++trace->considered;
            if(!(0.<=areaA||0.<=areaB)){if(trace)++trace->holePairsSkipped;continue;}
            if(!(0.<=areaB*areaA||areaA<=-areaB)){if(trace)++trace->smallHolesSkipped;continue;}
            if(trace)++trace->convolved;
            auto paths=recovered::amoConvolveStraightPaths(a,b,curves);for(const auto &path:paths){Polygon ring;for(const auto point:recovered::convolutionTopologyPoints(path))ring.push_back({point[0],point[1]});result.push_back(std::move(ring));}
        }
    }
    return result;
}
Raster ordinaryPairRaster(std::span<const Polygon> input,int64_t pitch,bool duplicate){
    std::vector<Polygon> contours(input.begin(),input.end());if(duplicate){const double shift=double(pitch)/1000000.;for(const auto &ring:input){auto copy=ring;for(auto &point:copy){point.x=shift+point.x;point.y+=0.;}contours.push_back(std::move(copy));}}
    const auto source=recovered::noMapRaster(contours,{},pitch,true);Raster result;result.offsetStrip=std::bit_cast<int32_t>(uint32_t(floorPairDivide(source.offsetX,pitch)));result.offsetY=source.offsetY;result.rows.resize(source.footprint.rows.size());for(size_t row=1;row<result.rows.size();++row)result.rows[row]=intersectPairRows(source.footprint.rows[row-1],source.footprint.rows[row]);return result;
}
struct OrdinaryPairMapProvider::Impl {std::vector<OrdinaryNoMapModule> modules;int64_t pitch;Budget &budget;OrdinaryPairMapTrace statistics;std::map<std::pair<size_t,size_t>,std::array<Raster,2>> maps;Impl(std::span<const OrdinaryNoMapModule> source,int64_t step,Budget &b):modules(source.begin(),source.end()),pitch(step),budget(b) {require(pitch>0,PN2S_INVALID,"Ordinary pair Map requires positive pitch");}};
OrdinaryPairMapProvider::OrdinaryPairMapProvider(std::span<const OrdinaryNoMapModule> modules,int64_t pitch,Budget &budget):impl_(std::make_unique<Impl>(modules,pitch,budget)){}
OrdinaryPairMapProvider::~OrdinaryPairMapProvider()=default;
const Raster &OrdinaryPairMapProvider::pair(size_t occupied,size_t candidate,bool duplicate){const auto key=std::pair{occupied,candidate};auto found=impl_->maps.find(key);if(found==impl_->maps.end()){const auto &a=impl_->modules.at(occupied).geometry,&b=impl_->modules.at(candidate).geometry;const auto contours=ordinaryPairContours(a.layer(false,0),b.layer(true,0),true,impl_->budget,&impl_->statistics.geometry);require(!contours.empty()&&!contours.front().empty(),PN2S_UNSUPPORTED,"Original pair C-space did not produce a nonempty first contour");std::array<Raster,2> rasters{ordinaryPairRaster(contours,impl_->pitch),ordinaryPairRaster(contours,impl_->pitch,true)};found=impl_->maps.emplace(key,std::move(rasters)).first;++impl_->statistics.builds;}else ++impl_->statistics.hits;return found->second[size_t(duplicate)];}
const OrdinaryPairMapTrace &OrdinaryPairMapProvider::trace()const{return impl_->statistics;}
OrdinarySelectedModules ordinarySelectedModules(std::span<const int64_t> ids,uint64_t bitCount,int32_t catalogSize){require(bitCount<=2000000,PN2S_RESOURCE_LIMIT,"Selected module bitset resource limit");OrdinarySelectedModules result;result.bitCount=bitCount;result.words.assign(size_t((bitCount+31)/32),0);for(const auto id:ids){require(id>=0&&uint64_t(id)<bitCount,PN2S_INVALID,"Selected module id is outside the actual module catalog bit capacity");result.words[size_t(uint64_t(id)>>5)]|=uint32_t(1)<<(uint64_t(id)&31);}uint32_t count=0;for(const auto word:result.words)count+=uint32_t(std::popcount(word));result.all=std::bit_cast<int32_t>(count)==catalogSize;return result;}
}
