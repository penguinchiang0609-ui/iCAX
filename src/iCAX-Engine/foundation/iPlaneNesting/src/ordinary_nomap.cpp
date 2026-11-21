#include "ordinary_nomap.hpp"
#include "recovered_engine.hpp"
#include <bit>
namespace study {
namespace {
int32_t add(int64_t a,int64_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
int32_t sub(int64_t a,int64_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));}
int64_t floorDivide(int64_t value,int64_t divisor){auto result=value/divisor;if(value<0&&value%divisor)--result;return result;}
}
OrdinaryPositionnerKind ordinaryPositionnerKind(const OrdinaryPositionnerFactoryInput &input){
    if(recovered::truncateInt32(input.forcePositionnerType)!=0){
        if(input.forceNoMap||input.target==1||input.moduleSlot4)return OrdinaryPositionnerKind::NoMap;
        require(bool(input.mapEligible),PN2S_INVALID,"Positionner factory needs the actual map eligibility provider");
        if(!input.mapEligible())return OrdinaryPositionnerKind::NoMap;
        require(bool(input.geometryTooComplex),PN2S_INVALID,"Positionner factory needs the actual geometry complexity provider");
        if(input.geometryTooComplex())return OrdinaryPositionnerKind::NoMap;
        if(input.exteriors>1&&((input.maximumPoints>0&&input.points>uint64_t(input.maximumPoints))||input.inwards>input.maximumInwards||input.exteriors>input.maximumExteriors))return OrdinaryPositionnerKind::NoMap;
    }
    if(!input.allPlacedModulesCached)for(const auto id:input.placedModuleIds){
        if(id<0||uint64_t(id)>=input.moduleBitCount)return OrdinaryPositionnerKind::NoMap;
        require(size_t(uint64_t(id)>>5)<input.moduleBits.size(),PN2S_INVALID,"Positionner module bitset storage is incomplete");
        if(((input.moduleBits[size_t(uint64_t(id)>>5)]>>(uint64_t(id)&31))&1u)==0)return OrdinaryPositionnerKind::NoMap;
    }
    return OrdinaryPositionnerKind::Map;
}
Rows ordinaryNoMapBase(OrdinaryNoMapGrid grid){
    require(grid.pitch>0&&std::isfinite(grid.width)&&std::isfinite(grid.height),PN2S_INVALID,"NoMap base grid is invalid");
    const auto count=std::max<int64_t>(0,floorDivide(rounded(grid.width*1000000.),grid.pitch));
    require(count<=200000,PN2S_RESOURCE_LIMIT,"NoMap physical strip-count limit");
    const auto upper=recovered::truncateInt32(std::ceil((grid.height-1e-7)*1000.));
    return Rows(size_t(count),Row{{0,upper}});
}
Row ordinaryNoMapSubtract(const Row &base,int32_t baseY,const Row &occupied,int32_t occupiedY){
    Row result;size_t at=0;
    for(const auto interval:base){int32_t low=add(interval[0],baseY),high=add(interval[1],baseY);
        while(at<occupied.size()&&add(occupied[at][1],occupiedY)<low)++at;
        size_t cut=at;
        while(cut<occupied.size()&&add(occupied[cut][0],occupiedY)<=high){
            const auto first=add(occupied[cut][0],occupiedY),end=add(occupied[cut][1],occupiedY);
            if(low<=first)result.push_back({low,std::min(first,high)});
            if(end>high){low=high;break;}
            low=end;++cut;
        }
        if(cut==occupied.size()||add(occupied[cut][0],occupiedY)>high||add(occupied[cut][1],occupiedY)<=high)
            result.push_back({low,high});
    }
    return result;
}
struct OrdinaryNoMapProvider::Impl {
    OrdinaryNoMapGrid grid;
    Budget &budget;
    std::vector<OrdinaryNoMapModule> modules;
    std::vector<std::array<std::optional<recovered::NoMapRaster>,4>> rasters;
    struct Engine {recovered::NoMapEndpointCache cache;int32_t firstPossible=0;};
    std::vector<Engine> engines;
    std::vector<OrdinaryNoMapPlacement> placed;
    Rows base;
    std::vector<int32_t> widths;
    size_t processed=0;
    bool dirty=true;
    OrdinaryNoMapTrace statistics;
    Impl(OrdinaryNoMapGrid g,std::span<const OrdinaryNoMapModule> m,Budget &b):grid(g),budget(b),modules(m.begin(),m.end()),rasters(m.size()),engines(m.size()){}
    const recovered::NoMapRaster &raster(size_t module,bool slot2,bool duplicate){
        budget.check();auto &cached=rasters.at(module)[size_t(slot2)*2+size_t(duplicate)];
        if(!cached){auto &geometry=modules.at(module).geometry;recovered::NoMapContourLayers input;input.available=2;input.maximum=1;input.pitch=grid.pitch;
            input.geometry=[&geometry](bool slot,int32_t layer){return geometry.layer(slot,layer);};
            auto contours=recovered::noMapContours(input,slot2,duplicate);
            cached=recovered::noMapRaster(contours,slot2?Point{}:Point{geometry.bounds.x0,geometry.bounds.y0},grid.pitch,!slot2);
        }
        return *cached;
    }
    void initialize(){
        base=ordinaryNoMapBase(grid);widths.assign(base.size(),INT32_MIN);processed=0;
        for(auto &engine:engines){engine={};auto &cache=engine.cache;cache.perStrip=grid.perStripCache;cache.perStripKey=true;const size_t n=cache.perStrip?base.size():1;cache.lower.assign(n,INT32_MIN+1);cache.upper=cache.lower;cache.keys.assign(n,0);}
        dirty=false;++statistics.baseBuilds;
    }
    void update(){
        budget.check(true);if(dirty)initialize();
        while(processed<placed.size()){
            budget.check();const auto &position=placed[processed];const auto &module=modules.at(position.module).geometry;
            const auto &occupied=raster(position.module,false,position.unaligned);
            const auto first=floorDivide(position.x-grid.originX+module.minimumOffset[0],grid.pitch)-floorDivide(occupied.offsetX,grid.pitch);
            const auto y=sub(sub(position.y,occupied.offsetY),grid.originY)+module.minimumOffset[1];
            const auto end=first+int64_t(occupied.footprint.rows.size());
            const auto firstBlock=int32_t(std::max<int64_t>(0,floorDivide(first,64))),endBlock=int32_t(std::min<int64_t>((base.size()+63)/64,floorDivide(end-1,64)+1));
            for(size_t m=0;m<engines.size();++m){auto &engine=engines[m];recovered::noMapInvalidate(engine.cache,engine.firstPossible,64,int32_t(raster(m,true,false).footprint.rows.size()),int32_t(base.size()),firstBlock,endBlock,1);++statistics.invalidations;}
            for(int64_t row=int64_t(firstBlock)*64;row<std::min<int64_t>(base.size(),int64_t(endBlock)*64);++row)widths[size_t(row)]=INT32_MIN;
            for(size_t row=0;row<occupied.footprint.rows.size();++row){const auto index=first+int64_t(row);if(index>=0&&uint64_t(index)<base.size())base[size_t(index)]=ordinaryNoMapSubtract(base[size_t(index)],0,occupied.footprint.rows[row],int32_t(y));}
            ++processed;++statistics.processedPlacements;
        }
    }
    Row query(size_t module,int32_t strip){
        update();const auto &shape=raster(module,true,false).footprint;
        require(strip>=0&&size_t(strip)+shape.rows.size()<=base.size(),PN2S_INVALID,"NoMap query strip exceeds its physical substrate");
        auto &engine=engines.at(module);
        if(auto cached=recovered::noMapCachedRow(engine.cache,strip,0,false)){++statistics.cacheHits;return *cached;}
        Row result;recovered::NoMapRowState state;const auto success=recovered::noMapComputeRow(state,shape,base,widths,strip,result);
        recovered::noMapStoreRow(engine.cache,strip,result,success,false);++statistics.rowCalculations;return success?result:Row{};
    }
};
OrdinaryNoMapProvider::OrdinaryNoMapProvider(OrdinaryNoMapGrid grid,std::span<const OrdinaryNoMapModule> modules,Budget &budget):impl_(std::make_unique<Impl>(grid,modules,budget)){}
OrdinaryNoMapProvider::~OrdinaryNoMapProvider()=default;
void OrdinaryNoMapProvider::append(OrdinaryNoMapPlacement value){require(value.module<impl_->modules.size(),PN2S_INVALID,"NoMap placement module is missing");impl_->placed.push_back(value);}
void OrdinaryNoMapProvider::remove(size_t index){impl_->placed.erase(impl_->placed.begin()+index);impl_->dirty=true;}
void OrdinaryNoMapProvider::move(size_t index,OrdinaryNoMapPlacement value){require(value.module<impl_->modules.size(),PN2S_INVALID,"NoMap placement module is missing");impl_->placed.at(index)=value;impl_->dirty=true;}
void OrdinaryNoMapProvider::reset(std::span<const OrdinaryNoMapPlacement> values){impl_->placed.assign(values.begin(),values.end());impl_->dirty=true;}
recovered::NoMapRange OrdinaryNoMapProvider::range(size_t module,recovered::NoMapRangeOptions options){impl_->update();auto &engine=impl_->engines.at(module);return recovered::noMapUnconstrainedRange(engine.cache,engine.firstPossible,impl_->raster(module,true,false).footprint,impl_->base,impl_->widths,options);}
Row OrdinaryNoMapProvider::row(size_t module,int32_t strip){return impl_->query(module,strip);}
std::vector<recovered::StripPoint> OrdinaryNoMapProvider::extrema(size_t module,recovered::ExtremalInput input){
    impl_->update();if(input.first>=input.end)return {};
    Rows rows(impl_->base.size());const auto strip=input.fromLow?input.first:input.end-1;rows[size_t(strip)]=row(module,strip);return recovered::noMapExtremalPoints(rows,input);
}
std::vector<recovered::StripPoint> OrdinaryNoMapProvider::directions(size_t module,recovered::NoMapRange bounds,std::span<const Point> directions,int32_t low,int32_t high){
    impl_->update();std::vector<recovered::StripPoint> result;if(bounds.first>=bounds.end)return result;
    for(const auto direction:directions){const bool lower=direction.y<0.;const double xStep=double(impl_->grid.pitch)/1000000.*direction.x;
        const bool forward=xStep<0.;int32_t strip=forward?bounds.first:bounds.end-1,last=forward?bounds.end-1:bounds.first;
        double best=-std::numeric_limits<double>::max();std::optional<recovered::StripPoint> point;
        const double bound=double(lower?low:high)/1000.*direction.y;
        const auto inspect=[&]{const auto intervals=row(module,strip);const auto y=intervals.empty()?INT32_MIN:int32_t(lower?intervals.front()[0]:intervals.back()[1]);const double x=double(strip)*xStep;
            if(y>=0){const double value=(double(y)/1000.)*direction.y+x;if(value>best){best=value;point=recovered::StripPoint{strip,y};}}return x;};
        inspect();while(strip!=last){strip+=forward?1:-1;const auto x=inspect();if(best>=bound+x)break;}
        if(point&&std::find(result.begin(),result.end(),*point)==result.end())result.push_back(*point);
    }
    return result;
}
const Rows &OrdinaryNoMapProvider::freeRows(){impl_->update();return impl_->base;}
const std::vector<OrdinaryNoMapPlacement> &OrdinaryNoMapProvider::placements()const{return impl_->placed;}
const OrdinaryNoMapTrace &OrdinaryNoMapProvider::trace()const{return impl_->statistics;}
const recovered::NoMapRaster &OrdinaryNoMapProvider::footprint(size_t module,bool slot2,bool duplicate){return impl_->raster(module,slot2,duplicate);}
}
